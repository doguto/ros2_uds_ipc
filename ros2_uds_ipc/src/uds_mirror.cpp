#include "ros2_uds_ipc/uds_mirror.hpp"

#include <cstring>
#include <utility>

#include <rclcpp/serialized_message.hpp>

namespace uds_ipc
{
  UdsMirrorWorker::UdsMirrorWorker(
    rclcpp::GenericPublisher::SharedPtr publisher, size_t max_queue, std::chrono::milliseconds period)
  : publisher_(std::move(publisher)), period_(period), slots_(max_queue),
    thread_(&UdsMirrorWorker::run, this)
  {}

  UdsMirrorWorker::~UdsMirrorWorker()
  {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      running_ = false;
    }
    cv_.notify_one();
    thread_.join();
  }

  void UdsMirrorWorker::enqueue(const uint8_t * data, size_t length)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (count_ == slots_.size()) {
      head_ = (head_ + 1) % slots_.size();
      --count_;
    }
    slots_[(head_ + count_) % slots_.size()].assign(data, data + length);
    ++count_;
  }

  void UdsMirrorWorker::run()
  {
    std::vector<std::vector<uint8_t>> batch(slots_.size());
    rclcpp::SerializedMessage serialized;
    std::unique_lock<std::mutex> lock(mutex_);
    while (running_) {
      cv_.wait_for(lock, period_, [this] { return !running_; });

      // Swap instead of copy so that buffer capacity is reused.
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

}  // namespace uds_ipc
