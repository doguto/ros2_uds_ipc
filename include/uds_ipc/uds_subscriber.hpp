#ifndef UDS_IPC__UDS_SUBSCRIBER_HPP_
#define UDS_IPC__UDS_SUBSCRIBER_HPP_

#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <atomic>
#include <cstring>
#include <filesystem>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <rclcpp/generic_publisher.hpp>
#include <rclcpp/serialization.hpp>
#include <rclcpp/serialized_message.hpp>

#include "uds_ipc/uds_mirror.hpp"

namespace RoStep
{
  /// UdsPublisher の相手役. accept/recv は専用スレッドで行い, 受信ごとに CDR で
  /// MessageT へ deserialize してから callback を呼ぶ. callback は UdsSubscriber 自身の
  /// スレッドから呼ばれる (rclcpp executor のスレッドではない) ので, callback 内で
  /// 共有状態に触るなら呼び出し側で排他すること.
  template <typename MessageT>
  class UdsSubscriber
  {
  public:
    using Callback = std::function<void(const MessageT &)>;

    // max_message_size: 1回の recv で受け取れる最大バイト数. SOCK_SEQPACKET は
    // データグラム境界を保持するので, これより大きい message は切り捨てられる
    // (想定サイズに余裕を持たせておくこと).
    // mirror: create_uds_mirror() で作った publisher を渡すと, 受信したメッセージを DDS にも流す.
    // その topic の購読者がいるときだけ publish するので, 誰も見ていなければほぼコストは無い.
    UdsSubscriber(
      std::string path, Callback callback, size_t max_message_size = 65536,
      rclcpp::GenericPublisher::SharedPtr mirror = nullptr)
    : path_(std::move(path)), callback_(std::move(callback)),
      mirror_(mirror ? std::make_unique<UdsMirrorWorker>(std::move(mirror)) : nullptr),
      serialized_(max_message_size)
    {
      // ソケットファイルの親ディレクトリ (/run/rostep/ 等) が無いと bind() が失敗するので,
      // 無ければ作る (tmpfs の /run は再起動で消えるため, 事前作成に頼らない).
      const auto parent = std::filesystem::path(path_).parent_path();
      if (!parent.empty()) {
        std::filesystem::create_directories(parent);
      }

      ::unlink(path_.c_str());  // 前回のクラッシュ等で残ったソケットファイルを掃除してから bind する.

      listen_fd_ = ::socket(AF_UNIX, SOCK_SEQPACKET, 0);
      if (listen_fd_ < 0) {
        throw std::runtime_error("UdsSubscriber: socket() failed for " + path_);
      }
      sockaddr_un addr{};
      addr.sun_family = AF_UNIX;
      std::strncpy(addr.sun_path, path_.c_str(), sizeof(addr.sun_path) - 1);
      if (::bind(listen_fd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
        ::close(listen_fd_);
        throw std::runtime_error("UdsSubscriber: bind() failed for " + path_);
      }
      if (::listen(listen_fd_, 1) != 0) {
        ::close(listen_fd_);
        throw std::runtime_error("UdsSubscriber: listen() failed for " + path_);
      }
      thread_ = std::thread(&UdsSubscriber::accept_loop, this);
    }

    ~UdsSubscriber()
    {
      // close()/shutdown() した listening fd で, 別スレッドがブロックしている accept() が
      // 必ず起きるとは Linux では保証されない (よくある落とし穴). running_ を見てから
      // poll() のタイムアウトで戻ってくる作りにしているので, そちらで確実に抜ける.
      running_ = false;

      // recv() でブロックしている分は, 接続済み socket への shutdown() なら確実に起きる
      // (listening socket の accept() とは違う). こちらは記録してある fd を直接閉じる.
      const int client_fd = client_fd_.exchange(-1);
      if (client_fd >= 0) {
        ::shutdown(client_fd, SHUT_RDWR);
      }

      if (thread_.joinable()) {
        thread_.join();
      }
      if (listen_fd_ >= 0) {
        ::close(listen_fd_);
      }
      ::unlink(path_.c_str());
    }

    UdsSubscriber(const UdsSubscriber &) = delete;
    UdsSubscriber & operator=(const UdsSubscriber &) = delete;

  private:
    void accept_loop()
    {
      while (running_) {
        pollfd pfd{listen_fd_, POLLIN, 0};
        // running_ を定期的に見られるよう, accept() 自体をブロックさせず poll() で待つ.
        const int ready = ::poll(&pfd, 1, 200 /* ms */);
        if (ready <= 0) {
          continue;  // timeout か割り込み. running_ をもう一度見るだけ.
        }

        const int client_fd = ::accept(listen_fd_, nullptr, nullptr);
        if (client_fd < 0) {
          continue;
        }
        client_fd_ = client_fd;
        recv_loop(client_fd);
        client_fd_ = -1;
        ::close(client_fd);
        // publisher 側が再接続してくる可能性があるので, 次の accept へ戻る.
      }
    }

    void recv_loop(int client_fd)
    {
      while (running_) {
        // shutdown() と accept() 直後のごく狭い window が重なるケースへの対策も兼ねて,
        // こちらも poll() で待ってから running_ を見る (ブロックしたままにしない).
        pollfd pfd{client_fd, POLLIN, 0};
        const int ready = ::poll(&pfd, 1, 200 /* ms */);
        if (ready <= 0) {
          continue;
        }

        // serialized_ のバッファへ直接受け, msg_ も使い回す (受信ごとのヒープ確保とコピーを避ける).
        auto & rcl_msg = serialized_.get_rcl_serialized_message();
        const ssize_t received = ::recv(client_fd, rcl_msg.buffer, rcl_msg.buffer_capacity, 0);
        if (received <= 0) {
          return;  // 切断, もしくは破棄時の shutdown() による解放 (publisher 側の再接続を待つ).
        }
        rcl_msg.buffer_length = static_cast<size_t>(received);

        serializer_.deserialize_message(&serialized_, &msg_);
        callback_(msg_);

        // callback の後に, バイト列をミラー用のキューへコピーするだけ (DDS への publish は別スレッド).
        if (mirror_ && mirror_->has_subscribers()) {
          mirror_->enqueue(rcl_msg.buffer, rcl_msg.buffer_length);
        }
      }
    }

    std::string path_;
    Callback callback_;
    std::unique_ptr<UdsMirrorWorker> mirror_;
    rclcpp::Serialization<MessageT> serializer_;
    rclcpp::SerializedMessage serialized_;
    MessageT msg_;
    int listen_fd_ = -1;
    std::atomic<int> client_fd_{-1};
    std::thread thread_;
    std::atomic<bool> running_{true};
  };

}  // namespace RoStep

#endif  // UDS_IPC__UDS_SUBSCRIBER_HPP_
