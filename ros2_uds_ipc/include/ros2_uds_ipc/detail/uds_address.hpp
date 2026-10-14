#ifndef ROS2_UDS_IPC__DETAIL__UDS_ADDRESS_HPP_
#define ROS2_UDS_IPC__DETAIL__UDS_ADDRESS_HPP_

#include <sys/socket.h>
#include <sys/un.h>

#include <cstring>
#include <string>

namespace uds_ipc
{
namespace detail
{
  /// Builds an AF_UNIX address for path. A path longer than sun_path is truncated.
  inline sockaddr_un make_unix_address(const std::string & path)
  {
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    return addr;
  }

}  // namespace detail
}  // namespace uds_ipc

#endif  // ROS2_UDS_IPC__DETAIL__UDS_ADDRESS_HPP_
