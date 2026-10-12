#ifndef UDS_IPC__UDS_PUBLISHER_HPP_
#define UDS_IPC__UDS_PUBLISHER_HPP_

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstring>
#include <memory>
#include <string>

#include <rclcpp/generic_publisher.hpp>
#include <rclcpp/serialization.hpp>
#include <rclcpp/serialized_message.hpp>

#include "uds_ipc/uds_mirror.hpp"

namespace RoStep
{
  /// AF_UNIX の SOCK_SEQPACKET で rclcpp メッセージを送る publisher.
  /// MessageT は rosidl が生成する型なら何でもよい (固定長である必要はない.
  /// CDR で一度シリアライズしてから, その可変長バイト列を1回の send() で送るだけ).
  ///
  /// SOCK_SEQPACKET を選んでいる理由:
  ///   - データグラムの境界を保持する (1回の send = 1回の recv. 手動フレーミング不要)
  ///   - 接続指向でカーネルが到達を保証する (TCP と同様. SOCK_DGRAM と違いベストエフォートではない)
  ///
  /// 信頼性の方針: publish() はブロックしない (呼び出し側の制御/変換ループを止めないため).
  /// 未接続 or 送信失敗時は false を返すだけで, 揉み消さない. 呼び出し側がログ/フォールバックを判断すること.
  template <typename MessageT>
  class UdsPublisher
  {
  public:
    // mirror: create_uds_mirror() で作った publisher を渡すと, 送るメッセージを DDS にも流す.
    // 受信側 (UdsSubscriber) が起動していなくても ros2 topic echo / rosbag で見える.
    // DDS への publish は専用スレッドで行い, publish() の中では購読者がいるときに
    // バイト列をキューへコピーするだけなので, 呼び出し側の制御ループはほぼ遅れない.
    explicit UdsPublisher(std::string path, rclcpp::GenericPublisher::SharedPtr mirror = nullptr)
    : path_(std::move(path)),
      mirror_(mirror ? std::make_unique<UdsMirrorWorker>(std::move(mirror)) : nullptr)
    {}

    ~UdsPublisher()
    {
      if (fd_ >= 0) {
        ::close(fd_);
      }
    }

    UdsPublisher(const UdsPublisher &) = delete;
    UdsPublisher & operator=(const UdsPublisher &) = delete;

    /// false: 未接続 (相手がまだ起動していない等) か送信失敗. ミラーは送信の成否によらず行う.
    bool publish(const MessageT & msg)
    {
      const bool mirroring = mirror_ && mirror_->has_subscribers();
      if (fd_ < 0 && !try_connect() && !mirroring) {
        return false;
      }

      // serialized_ を使い回し, publish ごとのヒープ確保/解放を避ける
      // (容量が足りないときだけ serialize_message 内で拡張される).
      serializer_.serialize_message(&msg, &serialized_);
      const auto & rcl_msg = serialized_.get_rcl_serialized_message();

      const bool sent = fd_ >= 0 && send_serialized(rcl_msg);
      // UDS への送信を先に済ませてからミラーする (本来の経路を遅らせない).
      if (mirroring) {
        mirror_->enqueue(rcl_msg.buffer, rcl_msg.buffer_length);
      }
      return sent;
    }

  private:
    bool send_serialized(const rcl_serialized_message_t & rcl_msg)
    {
      const ssize_t sent = ::send(fd_, rcl_msg.buffer, rcl_msg.buffer_length, MSG_DONTWAIT | MSG_NOSIGNAL);
      if (sent == static_cast<ssize_t>(rcl_msg.buffer_length)) {
        return true;
      }
      // 相手が落ちた/切断した場合はここで検知できる. 次回 publish で再接続を試みる.
      ::close(fd_);
      fd_ = -1;
      return false;
    }

    bool try_connect()
    {
      const int fd = ::socket(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK, 0);
      if (fd < 0) {
        return false;
      }
      sockaddr_un addr{};
      addr.sun_family = AF_UNIX;
      std::strncpy(addr.sun_path, path_.c_str(), sizeof(addr.sun_path) - 1);
      if (::connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
        ::close(fd);
        return false;
      }
      fd_ = fd;
      return true;
    }

    std::string path_;
    int fd_ = -1;
    rclcpp::Serialization<MessageT> serializer_;
    rclcpp::SerializedMessage serialized_;
    std::unique_ptr<UdsMirrorWorker> mirror_;
  };

}  // namespace RoStep

#endif  // UDS_IPC__UDS_PUBLISHER_HPP_
