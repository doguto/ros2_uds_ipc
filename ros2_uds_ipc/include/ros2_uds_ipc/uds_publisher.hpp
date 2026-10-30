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


#ifndef ROS2_UDS_IPC__UDS_PUBLISHER_HPP_
#define ROS2_UDS_IPC__UDS_PUBLISHER_HPP_

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include <rclcpp/generic_publisher.hpp>
#include <rclcpp/serialized_message.hpp>

#include "ros2_uds_ipc/detail/cdr_codec.hpp"
#include "ros2_uds_ipc/detail/raw_copy.hpp"
#include "ros2_uds_ipc/detail/uds_client_socket.hpp"
#include "ros2_uds_ipc/uds_mirror.hpp"

namespace uds_ipc
{
  /// Sends rclcpp messages (any rosidl-generated type) over an AF_UNIX SOCK_SEQPACKET socket.
template<typename MessageT>
class UdsPublisher
{
public:
    // mirror: a publisher from create_uds_mirror(). If set, sent messages are also forwarded to
    // DDS.
    // send_buffer_bytes: how many bytes of messages may wait for the subscriber (SO_SNDBUF) before
    // publish()
    // fails or waits. 0 keeps the system default; the kernel clamps the value to net.core.wmem_max.
    // Throws std::invalid_argument on invalid settings.
  explicit UdsPublisher(
    std::string path,
    rclcpp::GenericPublisher::SharedPtr mirror = nullptr,
    size_t send_buffer_bytes = 0
  )
  : socket_(std::move(path), send_buffer_bytes),
    mirror_(mirror ? std::make_unique<UdsMirrorWorker>(std::move(mirror)) : nullptr)
  {}

  UdsPublisher(const UdsPublisher &) = delete;
  UdsPublisher & operator=(const UdsPublisher &) = delete;

    /// Returns false if not connected or if sending failed.
    /// send_timeout: how long to wait while the subscriber's receive queue is full. With the
    /// default (zero)
    /// publish() never blocks and the message is dropped instead; a message is never dropped
    /// silently.
  bool publish(
    const MessageT & msg,
    std::chrono::nanoseconds send_timeout = std::chrono::nanoseconds::zero()
  )
  {
    const bool mirroring = mirror_ && mirror_->has_subscribers();
    if (!socket_.is_connected() && !socket_.try_connect(send_timeout) && !mirroring) {
      return false;
    }

    if constexpr (detail::is_raw_copyable<MessageT>) {
        // DDS still needs CDR, so serialize only while someone is listening to the mirror.
      const bool sent = socket_.is_connected() &&
        socket_.send(reinterpret_cast<const uint8_t *>(&msg), sizeof(MessageT), send_timeout);
      if (mirroring && codec_.serialize(msg, serialized_)) {
        const auto & rcl_msg = serialized_.get_rcl_serialized_message();
        mirror_->enqueue(rcl_msg.buffer, rcl_msg.buffer_length);
      }
      return sent;
    } else {
      if (!codec_.serialize(msg, serialized_)) {return false;}

      const auto & rcl_msg = serialized_.get_rcl_serialized_message();
      const bool sent =
        socket_.is_connected() && socket_.send(rcl_msg.buffer, rcl_msg.buffer_length, send_timeout);
      if (mirroring) {
        mirror_->enqueue(rcl_msg.buffer, rcl_msg.buffer_length);
      }
      return sent;
    }
  }

    /// Returns false while another publisher is connected (it takes over once that one leaves).
  bool connect(std::chrono::nanoseconds timeout)
  {
    return socket_.try_connect(timeout);
  }

  bool is_connected() const {return socket_.is_connected();}

private:
  detail::UdsClientSocket socket_;
  detail::CdrCodec<MessageT> codec_;
  rclcpp::SerializedMessage serialized_;
  std::unique_ptr<UdsMirrorWorker> mirror_;
};

}  // namespace uds_ipc

#endif  // ROS2_UDS_IPC__UDS_PUBLISHER_HPP_
