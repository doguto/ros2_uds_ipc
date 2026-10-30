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


#ifndef ROS2_UDS_IPC__UDS_SUBSCRIBER_HPP_
#define ROS2_UDS_IPC__UDS_SUBSCRIBER_HPP_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include <rclcpp/generic_publisher.hpp>
#include <rclcpp/serialized_message.hpp>

#include "ros2_uds_ipc/detail/cdr_codec.hpp"
#include "ros2_uds_ipc/detail/raw_copy.hpp"
#include "ros2_uds_ipc/detail/uds_server_socket.hpp"
#include "ros2_uds_ipc/scheduling.hpp"
#include "ros2_uds_ipc/uds_mirror.hpp"

namespace uds_ipc
{
  /// Counterpart of UdsPublisher. The callback is invoked from the subscriber's own thread,
  /// not from an rclcpp executor thread.
template<typename MessageT>
class UdsSubscriber
{
public:
  using Callback = std::function<void(const MessageT &)>;

    // max_message_size: serialized messages larger than this are dropped. Unused for fixed-size
    // messages.
    // mirror: a publisher from create_uds_mirror(). If set, received messages are also forwarded
    // to DDS.
    // scheduling: real-time priority and CPU pinning of the receive thread (see Scheduling). Off
    // by default.
    // Throws std::runtime_error if the socket cannot be set up, or if the OS refuses
    // scheduling.require.
    // Throws std::invalid_argument if path is not a usable socket path (empty, longer than 107
    // bytes, ...), callback is empty, max_message_size is 0 or scheduling is out of range.
  UdsSubscriber(
    std::string path,
    Callback callback,
    size_t max_message_size = 65536,
    rclcpp::GenericPublisher::SharedPtr mirror = nullptr,
    Scheduling scheduling = {}
  )
  : callback_(require_callback(std::move(callback))),
    mirror_(mirror ? std::make_unique<UdsMirrorWorker>(std::move(mirror)) : nullptr),
    serialized_(detail::is_raw_copyable<MessageT> ? 0 : max_message_size),
    socket_(
      std::move(path), receive_buffer(), receive_capacity(),
      [this](size_t length) {on_receive(length);}, scheduling
    )
  {}

    /// Whether the requested scheduling is in effect (also true when none was requested).
  bool scheduling_applied() const {return socket_.scheduling_applied();}

  UdsSubscriber(const UdsSubscriber &) = delete;
  UdsSubscriber & operator=(const UdsSubscriber &) = delete;

private:
  static Callback require_callback(Callback callback)
  {
    if (!callback) {
      throw std::invalid_argument("UdsSubscriber: the callback is empty");
    }
    return callback;
  }

    // Raw-copyable messages are received straight into msg_; others into the CDR buffer.
  uint8_t * receive_buffer()
  {
    if constexpr (detail::is_raw_copyable<MessageT>) {
      return reinterpret_cast<uint8_t *>(&msg_);
    } else {
      return serialized_.get_rcl_serialized_message().buffer;
    }
  }

  size_t receive_capacity() const
  {
    if constexpr (detail::is_raw_copyable<MessageT>) {
      return sizeof(MessageT);
    } else {
      return serialized_.get_rcl_serialized_message().buffer_capacity;
    }
  }

  void on_receive(size_t length)
  {
    if constexpr (detail::is_raw_copyable<MessageT>) {
      if (length != sizeof(MessageT)) {return;}  // Not this message type.

      callback_(msg_);

        // DDS still needs CDR, so serialize only while someone is listening to the mirror.
      if (mirror_ && mirror_->has_subscribers() && codec_.serialize(msg_, serialized_)) {
        const auto & rcl_msg = serialized_.get_rcl_serialized_message();
        mirror_->enqueue(rcl_msg.buffer, rcl_msg.buffer_length);
      }
    } else {
      auto & rcl_msg = serialized_.get_rcl_serialized_message();
      rcl_msg.buffer_length = length;

        // Truncated (larger than max_message_size), or not this message type.
      if (!codec_.deserialize(rcl_msg.buffer, length, msg_)) {return;}

      callback_(msg_);

      if (mirror_ && mirror_->has_subscribers()) {
        mirror_->enqueue(rcl_msg.buffer, rcl_msg.buffer_length);
      }
    }
  }

  Callback callback_;
  std::unique_ptr<UdsMirrorWorker> mirror_;
  detail::CdrCodec<MessageT> codec_;
  rclcpp::SerializedMessage serialized_;
  MessageT msg_;
    // Must be declared last: its thread uses the members above.
  detail::UdsServerSocket socket_;
};

}  // namespace uds_ipc

#endif  // ROS2_UDS_IPC__UDS_SUBSCRIBER_HPP_
