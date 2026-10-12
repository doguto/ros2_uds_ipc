#ifndef ROS2_UDS_IPC__DETAIL__RAW_COPY_HPP_
#define ROS2_UDS_IPC__DETAIL__RAW_COPY_HPP_

#include <type_traits>

#include <rosidl_runtime_cpp/traits.hpp>

namespace uds_ipc
{
namespace detail
{
  /// True for messages made only of primitives and fixed-size arrays. Their in-memory bytes are sent as-is,
  /// skipping CDR serialization. Both ends are on the same machine, so the layout matches as long as they
  /// are built against the same message definition.
  template <typename MessageT>
  inline constexpr bool is_raw_copyable =
    rosidl_generator_traits::has_fixed_size<MessageT>::value && std::is_trivially_copyable_v<MessageT>;

}  // namespace detail
}  // namespace uds_ipc

#endif  // ROS2_UDS_IPC__DETAIL__RAW_COPY_HPP_
