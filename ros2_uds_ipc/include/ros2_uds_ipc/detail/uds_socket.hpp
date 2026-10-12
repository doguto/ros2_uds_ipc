#ifndef ROS2_UDS_IPC__DETAIL__UDS_SOCKET_HPP_
#define ROS2_UDS_IPC__DETAIL__UDS_SOCKET_HPP_

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>

namespace uds_ipc
{
namespace detail
{
  /// Sending side of UdsPublisher.
  class UdsClientSocket
  {
  public:
    explicit UdsClientSocket(std::string path);
    ~UdsClientSocket();

    UdsClientSocket(const UdsClientSocket &) = delete;
    UdsClientSocket & operator=(const UdsClientSocket &) = delete;

    bool is_connected() const { return fd_ >= 0; }

    bool try_connect();

    /// Non-blocking. Closes the socket on failure.
    bool send(const uint8_t * data, size_t length);

  private:
    std::string path_;
    int fd_ = -1;
  };

  /// Receiving side of UdsSubscriber. accept/recv run on a dedicated thread.
  class UdsServerSocket
  {
  public:
    using ReceiveHandler = std::function<void(size_t length)>;

    // Each datagram is received into buffer, then on_receive is called with its length.
    // buffer must outlive this object.
    UdsServerSocket(std::string path, uint8_t * buffer, size_t capacity, ReceiveHandler on_receive);
    ~UdsServerSocket();

    UdsServerSocket(const UdsServerSocket &) = delete;
    UdsServerSocket & operator=(const UdsServerSocket &) = delete;

  private:
    void accept_loop();
    void recv_loop(int client_fd);

    std::string path_;
    uint8_t * buffer_;
    size_t capacity_;
    ReceiveHandler on_receive_;
    int listen_fd_ = -1;
    std::atomic<int> client_fd_{-1};
    std::atomic<bool> running_{true};
    std::thread thread_;
  };

}  // namespace detail
}  // namespace uds_ipc

#endif  // ROS2_UDS_IPC__DETAIL__UDS_SOCKET_HPP_
