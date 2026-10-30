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


#ifndef ROS2_UDS_IPC__DETAIL__UDS_SERVER_SOCKET_HPP_
#define ROS2_UDS_IPC__DETAIL__UDS_SERVER_SOCKET_HPP_

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>

#include "ros2_uds_ipc/scheduling.hpp"

namespace uds_ipc
{
namespace detail
{
  /// Receiving side of UdsSubscriber. accept/recv run on a dedicated thread.
class UdsServerSocket
{
public:
  using ReceiveHandler = std::function<void(size_t length)>;

    // Each datagram is received into buffer, then on_receive is called with its length.
    // buffer must outlive this object.
    // scheduling: applied to the receive thread. Throws std::invalid_argument if path is not a
    // usable socket path, capacity is 0 or scheduling is out of range, and
    // std::runtime_error if the OS refuses it and scheduling.require is set.
  UdsServerSocket(
    std::string path, uint8_t * buffer, size_t capacity, ReceiveHandler on_receive,
    Scheduling scheduling = {});
  ~UdsServerSocket();

  UdsServerSocket(const UdsServerSocket &) = delete;
  UdsServerSocket & operator=(const UdsServerSocket &) = delete;

    /// Whether the requested scheduling is in effect (also true when none was requested).
  bool scheduling_applied() const {return scheduling_applied_;}

private:
    /// Applies scheduling_ to the calling thread. Returns a description of what failed, empty on
    /// success.
  std::string apply_scheduling() const;
  void accept_loop();
  void recv_loop(int client_fd);

  std::string path_;
  uint8_t * buffer_;
  size_t capacity_;
  ReceiveHandler on_receive_;
  Scheduling scheduling_;
  bool scheduling_applied_ = true;
  int listen_fd_ = -1;
  std::atomic<bool> running_{true};
  std::thread thread_;
};

}  // namespace detail
}  // namespace uds_ipc

#endif  // ROS2_UDS_IPC__DETAIL__UDS_SERVER_SOCKET_HPP_
