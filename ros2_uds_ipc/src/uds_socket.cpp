#include "ros2_uds_ipc/detail/uds_socket.hpp"

#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <future>
#include <memory>
#include <stdexcept>
#include <utility>

#include <rclcpp/logging.hpp>

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
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path_.c_str(), sizeof(addr.sun_path) - 1);
    if (::connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
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

  UdsServerSocket::UdsServerSocket(
    std::string path, uint8_t * buffer, size_t capacity, ReceiveHandler on_receive, Scheduling scheduling)
  : path_(std::move(path)), buffer_(buffer), capacity_(capacity), on_receive_(std::move(on_receive)),
    scheduling_(scheduling)
  {
    if (scheduling_.rt_priority < 0 || scheduling_.rt_priority > Scheduling::kMaxRtPriority) {
      throw std::invalid_argument(
        "UdsSubscriber: rt_priority must be 0 (off) or 1 - " + std::to_string(Scheduling::kMaxRtPriority));
    }
    if (scheduling_.cpu < -1 || scheduling_.cpu >= CPU_SETSIZE) {
      throw std::invalid_argument("UdsSubscriber: cpu must be -1 (any) or a valid CPU number");
    }

    const auto parent = std::filesystem::path(path_).parent_path();
    if (!parent.empty()) {
      std::filesystem::create_directories(parent);
    }

    ::unlink(path_.c_str());  // Remove a stale socket file left by a previous run.

    listen_fd_ = ::socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if (listen_fd_ < 0) {
      throw std::runtime_error("UdsSubscriber: socket() failed for " + path_);
    }
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path_.c_str(), sizeof(addr.sun_path) - 1);
    if (::bind(listen_fd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
      ::close(listen_fd_);
      throw std::runtime_error("UdsSubscriber: bind() failed for " + path_);
    }
    if (::listen(listen_fd_, 1) != 0) {
      ::close(listen_fd_);
      throw std::runtime_error("UdsSubscriber: listen() failed for " + path_);
    }

    // The thread reports whether it got its scheduling before it starts to receive.
    auto applied = std::make_shared<std::promise<std::string>>();
    auto result = applied->get_future();
    thread_ = std::thread([this, applied] {
      applied->set_value(apply_scheduling());
      accept_loop();
    });

    const std::string error = result.get();
    if (error.empty()) return;

    scheduling_applied_ = false;
    if (scheduling_.require) {
      running_ = false;
      thread_.join();
      ::close(listen_fd_);
      ::unlink(path_.c_str());
      throw std::runtime_error("UdsSubscriber: " + error);
    }
    RCLCPP_WARN(
      rclcpp::get_logger("ros2_uds_ipc"), "UdsSubscriber %s: %s. Running with the default scheduling.",
      path_.c_str(), error.c_str());
  }

  std::string UdsServerSocket::apply_scheduling() const
  {
    std::string error;
    if (scheduling_.rt_priority > 0) {
      sched_param param{};
      param.sched_priority = scheduling_.rt_priority;
      const int result = ::pthread_setschedparam(::pthread_self(), SCHED_FIFO, &param);
      if (result != 0) {
        error += "could not set SCHED_FIFO priority " + std::to_string(scheduling_.rt_priority) + " (" +
          std::strerror(result) + "; needs an rtprio limit or CAP_SYS_NICE)";
      }
    }
    if (scheduling_.cpu >= 0) {
      cpu_set_t set;
      CPU_ZERO(&set);
      CPU_SET(scheduling_.cpu, &set);
      const int result = ::pthread_setaffinity_np(::pthread_self(), sizeof(set), &set);
      if (result != 0) {
        error += std::string(error.empty() ? "" : "; ") + "could not pin to CPU " +
          std::to_string(scheduling_.cpu) + " (" + std::strerror(result) + ")";
      }
    }
    return error;
  }

  UdsServerSocket::~UdsServerSocket()
  {
    // Closing a listening fd does not reliably wake a blocked accept() on Linux,
    // so the loops poll() with a timeout and check running_ instead.
    running_ = false;

    if (thread_.joinable()) {
      thread_.join();
    }
    if (listen_fd_ >= 0) {
      ::close(listen_fd_);
    }
    ::unlink(path_.c_str());
  }

  void UdsServerSocket::accept_loop()
  {
    while (running_) {
      pollfd pfd{listen_fd_, POLLIN, 0};
      const int ready = ::poll(&pfd, 1, 200 /* ms */);
      if (ready <= 0) continue;

      const int client_fd = ::accept(listen_fd_, nullptr, nullptr);
      if (client_fd < 0) continue;

      recv_loop(client_fd);
      ::close(client_fd);
    }
  }

  void UdsServerSocket::recv_loop(int client_fd)
  {
    while (running_) {
      pollfd pfd{client_fd, POLLIN, 0};
      const int ready = ::poll(&pfd, 1, 200 /* ms */);
      if (ready <= 0) continue;

      const ssize_t received = ::recv(client_fd, buffer_, capacity_, 0);
      if (received <= 0) return;  // Disconnected.

      on_receive_(static_cast<size_t>(received));
    }
  }

}  // namespace detail
}  // namespace uds_ipc
