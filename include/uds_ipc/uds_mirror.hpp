#ifndef UDS_IPC__UDS_MIRROR_HPP_
#define UDS_IPC__UDS_MIRROR_HPP_

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <rclcpp/generic_publisher.hpp>
#include <rclcpp/node.hpp>
#include <rclcpp/serialized_message.hpp>
#include <rosidl_runtime_cpp/traits.hpp>

namespace RoStep
{
  /// uds_ipc で送受信するメッセージを, DDS の通常 topic としても流すための publisher を作る.
  /// uds_ipc の経路は ros2 topic echo / rosbag から見えないので, デバッグ・記録用に使う.
  /// MessageT の CDR バイト列をそのまま publish する (シリアライズし直さない) ため GenericPublisher を使う.
  template <typename MessageT>
  rclcpp::GenericPublisher::SharedPtr create_uds_mirror(
    rclcpp::Node & node, const std::string & topic, const rclcpp::QoS & qos = rclcpp::SystemDefaultsQoS())
  {
    return node.create_generic_publisher(topic, rosidl_generator_traits::name<MessageT>(), qos);
  }

  /// CDR バイト列を受け取り, 専用スレッドから DDS へ publish する.
  /// UdsPublisher::publish() は制御ループのスレッドから呼ばれるので, そこで DDS の publish を
  /// 実行しないためのもの. 呼び出し側の負担を最小にするため:
  ///   - enqueue() は確保済みのバッファにコピーするだけ (ヒープ確保なし, ワーカーも起こさない)
  ///   - ワーカーは period ごとに起きて, 溜まった分をまとめて publish する
  ///   - 購読者の有無もワーカーが period ごとに調べ, 呼び出し側は atomic を読むだけ
  /// ミラーはデバッグ・記録用なので, period ぶん遅れて届くのは許容する.
  class UdsMirrorWorker
  {
  public:
    // max_queue: 1 period の間に溜められる上限. 超えたら古いものから捨てる
    // (DDS 側が詰まっても呼び出し側を止めない).
    explicit UdsMirrorWorker(
      rclcpp::GenericPublisher::SharedPtr publisher, size_t max_queue = 100,
      std::chrono::milliseconds period = std::chrono::milliseconds(10))
    : publisher_(std::move(publisher)), period_(period), slots_(max_queue),
      thread_(&UdsMirrorWorker::run, this)
    {}

    ~UdsMirrorWorker()
    {
      {
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = false;
      }
      cv_.notify_one();
      thread_.join();
    }

    UdsMirrorWorker(const UdsMirrorWorker &) = delete;
    UdsMirrorWorker & operator=(const UdsMirrorWorker &) = delete;

    /// 購読者がいるときだけ true. 誰も見ていなければ呼び出し側はコピーもしない.
    bool has_subscribers() const { return has_subscribers_.load(std::memory_order_relaxed); }

    void enqueue(const uint8_t * data, size_t length)
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (count_ == slots_.size()) {  // 満杯なら最古を捨てる
        head_ = (head_ + 1) % slots_.size();
        --count_;
      }
      // assign は既存の容量を使い回すので, 2回目以降はヒープ確保が起きない.
      slots_[(head_ + count_) % slots_.size()].assign(data, data + length);
      ++count_;
    }

  private:
    void run()
    {
      std::vector<std::vector<uint8_t>> batch(slots_.size());
      rclcpp::SerializedMessage serialized;
      std::unique_lock<std::mutex> lock(mutex_);
      while (running_) {
        cv_.wait_for(lock, period_, [this] { return !running_; });

        // 溜まった分をバッファごと swap で取り出す (容量はスロットとの間で循環する).
        const size_t n = count_;
        for (size_t i = 0; i < n; ++i) {
          batch[i].swap(slots_[(head_ + i) % slots_.size()]);
        }
        head_ = (head_ + n) % slots_.size();
        count_ = 0;
        lock.unlock();

        has_subscribers_.store(publisher_->get_subscription_count() > 0, std::memory_order_relaxed);
        for (size_t i = 0; i < n; ++i) {
          auto & rcl_msg = serialized.get_rcl_serialized_message();
          if (rcl_msg.buffer_capacity < batch[i].size()) {
            serialized.reserve(batch[i].size());
          }
          std::memcpy(rcl_msg.buffer, batch[i].data(), batch[i].size());
          rcl_msg.buffer_length = batch[i].size();
          publisher_->publish(serialized);
        }

        lock.lock();
      }
    }

    rclcpp::GenericPublisher::SharedPtr publisher_;
    std::chrono::milliseconds period_;
    std::atomic<bool> has_subscribers_{false};
    std::mutex mutex_;
    std::condition_variable cv_;
    std::vector<std::vector<uint8_t>> slots_;
    size_t head_ = 0;
    size_t count_ = 0;
    bool running_ = true;
    std::thread thread_;
  };

}  // namespace RoStep

#endif  // UDS_IPC__UDS_MIRROR_HPP_
