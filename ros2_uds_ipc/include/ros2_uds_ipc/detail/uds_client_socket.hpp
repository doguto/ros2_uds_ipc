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


#ifndef ROS2_UDS_IPC__DETAIL__UDS_CLIENT_SOCKET_HPP_
#define ROS2_UDS_IPC__DETAIL__UDS_CLIENT_SOCKET_HPP_

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>

namespace uds_ipc
{
namespace detail
{
  /// Sending side of UdsPublisher.
class UdsClientSocket
{
public:
    // send_buffer_bytes: SO_SNDBUF of the socket. 0 keeps the system default.
    // Throws std::invalid_argument if path is not a usable socket path or send_buffer_bytes does
    // not fit in an int.
  explicit UdsClientSocket(std::string path, size_t send_buffer_bytes = 0);
  ~UdsClientSocket();

  UdsClientSocket(const UdsClientSocket &) = delete;
  UdsClientSocket & operator=(const UdsClientSocket &) = delete;

    /// True once the subscriber has accepted this connection.
  bool is_connected() const {return accepted_;}

    /// Connects if needed, then waits up to timeout (zero: does not wait) for the subscriber to
    /// accept. Returns is_connected(). While another publisher is connected, this one stays queued
    /// and keeps returning false.
  bool try_connect(std::chrono::nanoseconds timeout);

    /// While the peer's receive queue is full, waits for room for up to timeout (zero: does not
    /// wait),
    /// then returns false and keeps the connection. Any other failure closes the socket.
  bool send(const uint8_t * data, size_t length, std::chrono::nanoseconds timeout);

private:
  void disconnect();

  std::string path_;
  size_t send_buffer_bytes_;
  int fd_ = -1;
  bool accepted_ = false;
};

}  // namespace detail
}  // namespace uds_ipc

#endif  // ROS2_UDS_IPC__DETAIL__UDS_CLIENT_SOCKET_HPP_
