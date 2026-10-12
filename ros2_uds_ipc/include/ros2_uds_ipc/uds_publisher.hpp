#ifndef ROS2_UDS_IPC__UDS_PUBLISHER_HPP_
#define ROS2_UDS_IPC__UDS_PUBLISHER_HPP_

#include <chrono>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>

#include <rclcpp/generic_publisher.hpp>
#include <rclcpp/serialization.hpp>
#include <rclcpp/serialized_message.hpp>

#include "ros2_uds_ipc/detail/uds_socket.hpp"
#include "ros2_uds_ipc/uds_mirror.hpp"

namespace uds_ipc
{
  /// Sends rclcpp messages (any rosidl-generated type) over an AF_UNIX SOCK_SEQPACKET socket.
  template <typename MessageT>
  class UdsPublisher
  {
  public:
    // mirror: a publisher from create_uds_mirror(). If set, sent messages are also forwarded to DDS.
    // send_buffer_bytes: how many bytes of messages may wait for the subscriber (SO_SNDBUF) before publish()
    // fails or waits. 0 keeps the system default; the kernel clamps the value to net.core.wmem_max.
    explicit UdsPublisher(
      std::string path, rclcpp::GenericPublisher::SharedPtr mirror = nullptr, size_t send_buffer_bytes = 0)
    : socket_(std::move(path), send_buffer_bytes),
      mirror_(mirror ? std::make_unique<UdsMirrorWorker>(std::move(mirror)) : nullptr)
    {}

    UdsPublisher(const UdsPublisher &) = delete;
    UdsPublisher & operator=(const UdsPublisher &) = delete;

    /// Returns false if not connected or if sending failed.
    /// send_timeout: how long to wait while the subscriber's receive queue is full. With the default (zero)
    /// publish() never blocks and the message is dropped instead; a message is never dropped silently.
    bool publish(
      const MessageT & msg, std::chrono::nanoseconds send_timeout = std::chrono::nanoseconds::zero())
    {
      const bool mirroring = mirror_ && mirror_->has_subscribers();
      if (!socket_.is_connected() && !socket_.try_connect() && !mirroring) {
        return false;
      }

      serializer_.serialize_message(&msg, &serialized_);
      const auto & rcl_msg = serialized_.get_rcl_serialized_message();

      const bool sent = socket_.is_connected() && socket_.send(rcl_msg.buffer, rcl_msg.buffer_length, send_timeout);
      if (mirroring) {
        mirror_->enqueue(rcl_msg.buffer, rcl_msg.buffer_length);
      }
      return sent;
    }

  private:
    detail::UdsClientSocket socket_;
    rclcpp::Serialization<MessageT> serializer_;
    rclcpp::SerializedMessage serialized_;
    std::unique_ptr<UdsMirrorWorker> mirror_;
  };

}  // namespace uds_ipc

#endif  // ROS2_UDS_IPC__UDS_PUBLISHER_HPP_
