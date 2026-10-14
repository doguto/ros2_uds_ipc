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
    explicit UdsClientSocket(std::string path, size_t send_buffer_bytes = 0);
    ~UdsClientSocket();

    UdsClientSocket(const UdsClientSocket &) = delete;
    UdsClientSocket & operator=(const UdsClientSocket &) = delete;

    bool is_connected() const { return fd_ >= 0; }

    bool try_connect();

    /// While the peer's receive queue is full, waits for room for up to timeout (zero: does not wait),
    /// then returns false and keeps the connection. Any other failure closes the socket.
    bool send(const uint8_t * data, size_t length, std::chrono::nanoseconds timeout);

  private:
    std::string path_;
    size_t send_buffer_bytes_;
    int fd_ = -1;
  };

}  // namespace detail
}  // namespace uds_ipc

#endif  // ROS2_UDS_IPC__DETAIL__UDS_CLIENT_SOCKET_HPP_
