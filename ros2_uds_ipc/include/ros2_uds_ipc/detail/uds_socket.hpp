#ifndef ROS2_UDS_IPC__DETAIL__UDS_SOCKET_HPP_
#define ROS2_UDS_IPC__DETAIL__UDS_SOCKET_HPP_

#include <atomic>
#include <chrono>
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

  /// Receiving side of UdsSubscriber. accept/recv run on a dedicated thread.
  class UdsServerSocket
  {
  public:
    using ReceiveHandler = std::function<void(size_t length)>;

    // Each datagram is received into buffer, then on_receive is called with its length.
    // buffer must outlive this object.
    // scheduling: applied to the receive thread. Throws std::invalid_argument if it is out of range, and
    // std::runtime_error if the OS refuses it and scheduling.require is set.
    UdsServerSocket(
      std::string path, uint8_t * buffer, size_t capacity, ReceiveHandler on_receive,
      Scheduling scheduling = {});
    ~UdsServerSocket();

    UdsServerSocket(const UdsServerSocket &) = delete;
    UdsServerSocket & operator=(const UdsServerSocket &) = delete;

    /// Whether the requested scheduling is in effect (also true when none was requested).
    bool scheduling_applied() const { return scheduling_applied_; }

  private:
    /// Applies scheduling_ to the calling thread. Returns a description of what failed, empty on success.
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

#endif  // ROS2_UDS_IPC__DETAIL__UDS_SOCKET_HPP_
