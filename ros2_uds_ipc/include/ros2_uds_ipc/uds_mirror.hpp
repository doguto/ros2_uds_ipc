// Copyright 2026 kuto
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.


#ifndef ROS2_UDS_IPC__UDS_MIRROR_HPP_
#define ROS2_UDS_IPC__UDS_MIRROR_HPP_

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <rclcpp/generic_publisher.hpp>
#include <rclcpp/node.hpp>
#include <rclcpp/qos.hpp>
#include <rosidl_runtime_cpp/traits.hpp>

namespace uds_ipc
{
  /// Creates the DDS publisher used to mirror UDS traffic (for `ros2 topic echo` / rosbag).
template<typename MessageT>
rclcpp::GenericPublisher::SharedPtr create_uds_mirror(
  rclcpp::Node & node,
  const std::string & topic,
  const rclcpp::QoS & qos = rclcpp::SystemDefaultsQoS()
)
{
  return node.create_generic_publisher(topic, rosidl_generator_traits::name<MessageT>(), qos);
}

  /// Publishes queued CDR bytes to DDS from a dedicated thread, once per period,
  /// so that the caller's thread never runs a DDS publish.
class UdsMirrorWorker
{
public:
    // When more than max_queue messages are queued within one period, the oldest ones are dropped.
    // Throws std::invalid_argument if publisher is null, max_queue is 0 or period is not positive.
  explicit UdsMirrorWorker(
    rclcpp::GenericPublisher::SharedPtr publisher, size_t max_queue = 100,
    std::chrono::milliseconds period = std::chrono::milliseconds(10)
  );

  ~UdsMirrorWorker();

  UdsMirrorWorker(const UdsMirrorWorker &) = delete;
  UdsMirrorWorker & operator=(const UdsMirrorWorker &) = delete;

  bool has_subscribers() const {return has_subscribers_.load(std::memory_order_relaxed);}

  void enqueue(const uint8_t * data, size_t length);

private:
  void run();

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

}  // namespace uds_ipc

#endif  // ROS2_UDS_IPC__UDS_MIRROR_HPP_
