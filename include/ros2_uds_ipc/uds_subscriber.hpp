#ifndef ROS2_UDS_IPC__UDS_SUBSCRIBER_HPP_
#define ROS2_UDS_IPC__UDS_SUBSCRIBER_HPP_

#include <cstddef>
#include <functional>
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
  /// Counterpart of UdsPublisher. The callback is invoked from the subscriber's own thread,
  /// not from an rclcpp executor thread.
  template <typename MessageT>
  class UdsSubscriber
  {
  public:
    using Callback = std::function<void(const MessageT &)>;

    // max_message_size: serialized messages larger than this are truncated.
    // mirror: a publisher from create_uds_mirror(). If set, received messages are also forwarded to DDS.
    // Throws std::runtime_error if the socket cannot be set up.
    UdsSubscriber(
      std::string path, Callback callback, size_t max_message_size = 65536,
      rclcpp::GenericPublisher::SharedPtr mirror = nullptr)
    : callback_(std::move(callback)),
      mirror_(mirror ? std::make_unique<UdsMirrorWorker>(std::move(mirror)) : nullptr),
      serialized_(max_message_size),
      socket_(
        std::move(path), serialized_.get_rcl_serialized_message().buffer,
        serialized_.get_rcl_serialized_message().buffer_capacity,
        [this](size_t length) { on_receive(length); })
    {}

    UdsSubscriber(const UdsSubscriber &) = delete;
    UdsSubscriber & operator=(const UdsSubscriber &) = delete;

  private:
    void on_receive(size_t length)
    {
      auto & rcl_msg = serialized_.get_rcl_serialized_message();
      rcl_msg.buffer_length = length;

      serializer_.deserialize_message(&serialized_, &msg_);
      callback_(msg_);

      if (mirror_ && mirror_->has_subscribers()) {
        mirror_->enqueue(rcl_msg.buffer, rcl_msg.buffer_length);
      }
    }

    Callback callback_;
    std::unique_ptr<UdsMirrorWorker> mirror_;
    rclcpp::Serialization<MessageT> serializer_;
    rclcpp::SerializedMessage serialized_;
    MessageT msg_;
    // Must be declared last: its thread uses the members above.
    detail::UdsServerSocket socket_;
  };

}  // namespace uds_ipc

#endif  // ROS2_UDS_IPC__UDS_SUBSCRIBER_HPP_
