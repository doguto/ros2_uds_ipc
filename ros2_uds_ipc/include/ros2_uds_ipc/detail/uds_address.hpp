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


#ifndef ROS2_UDS_IPC__DETAIL__UDS_ADDRESS_HPP_
#define ROS2_UDS_IPC__DETAIL__UDS_ADDRESS_HPP_

#include <sys/socket.h>
#include <sys/un.h>

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>

namespace uds_ipc
{
namespace detail
{
inline void validate_socket_path(const std::string & path, const std::string & owner)
{
  constexpr size_t kMaxLength = sizeof(sockaddr_un::sun_path) - 1;
  if (path.empty()) {
    throw std::invalid_argument(owner + ": the socket path is empty");
  }
  if (path.find('\0') != std::string::npos) {
    throw std::invalid_argument(owner + ": the socket path contains a NUL character");
  }
  if (path.size() > kMaxLength) {
    throw std::invalid_argument(
            owner + ": the socket path is " + std::to_string(path.size()) + " bytes, over the " +
            std::to_string(kMaxLength) + "-byte limit of AF_UNIX: " + path);
  }
  if (path.back() == '/') {
    throw std::invalid_argument(owner + ": the socket path names a directory: " + path);
  }
}

  /// Builds an AF_UNIX address for path.
inline sockaddr_un make_unix_address(const std::string & path)
{
  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
  return addr;
}

  /// Sent on accept. Until then a connected publisher may only be queued in the listen backlog.
inline constexpr uint8_t kAcceptedByte = 1;

}  // namespace detail
}  // namespace uds_ipc

#endif  // ROS2_UDS_IPC__DETAIL__UDS_ADDRESS_HPP_
