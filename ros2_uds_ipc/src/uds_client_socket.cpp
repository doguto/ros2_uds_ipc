#include "ros2_uds_ipc/detail/uds_client_socket.hpp"

#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <utility>

#include "ros2_uds_ipc/detail/uds_address.hpp"

namespace uds_ipc
{
namespace detail
{
  UdsClientSocket::UdsClientSocket(std::string path, size_t send_buffer_bytes)
  : path_(std::move(path)), send_buffer_bytes_(send_buffer_bytes)
  {}

  UdsClientSocket::~UdsClientSocket()
  {
    if (fd_ >= 0) {
      ::close(fd_);
    }
  }

  bool UdsClientSocket::try_connect()
  {
    const int fd = ::socket(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK, 0);
    if (fd < 0) return false;

    if (send_buffer_bytes_ > 0) {
      // On AF_UNIX the messages waiting in the peer's receive queue are charged to the sender's buffer,
      // so this is what bounds the queue. Best effort: the kernel clamps it to net.core.wmem_max.
      const int size = static_cast<int>(send_buffer_bytes_);
      ::setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &size, sizeof(size));
    }
    const sockaddr_un addr = make_unix_address(path_);
    if (::connect(fd, reinterpret_cast<const sockaddr *>(&addr), sizeof(addr)) != 0) {
      ::close(fd);
      return false;
    }
    fd_ = fd;
    return true;
  }

  bool UdsClientSocket::send(const uint8_t * data, size_t length, std::chrono::nanoseconds timeout)
  {
    std::chrono::steady_clock::time_point deadline;
    bool waiting = false;
    while (true) {
      const ssize_t sent = ::send(fd_, data, length, MSG_DONTWAIT | MSG_NOSIGNAL);
      if (sent == static_cast<ssize_t>(length)) return true;
      if (sent < 0 && errno == EINTR) continue;

      if (sent >= 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
        break;  // Disconnected, or the message can never be sent (e.g. larger than the send buffer).
      }

      // The peer is alive but has not consumed its queue yet.
      const auto now = std::chrono::steady_clock::now();
      if (!waiting) {
        deadline = now + timeout;
        waiting = true;
      }
      if (now >= deadline) return false;

      const auto remaining = deadline - now;
      timespec ts{};
      ts.tv_sec = std::chrono::duration_cast<std::chrono::seconds>(remaining).count();
      ts.tv_nsec = (remaining % std::chrono::seconds(1)).count();
      pollfd pfd{fd_, POLLOUT, 0};
      ::ppoll(&pfd, 1, &ts, nullptr);
    }
    ::close(fd_);
    fd_ = -1;
    return false;
  }

}  // namespace detail
}  // namespace uds_ipc
