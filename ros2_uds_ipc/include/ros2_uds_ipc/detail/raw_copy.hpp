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


#ifndef ROS2_UDS_IPC__DETAIL__RAW_COPY_HPP_
#define ROS2_UDS_IPC__DETAIL__RAW_COPY_HPP_

#include <type_traits>

#include <rosidl_runtime_cpp/traits.hpp>

namespace uds_ipc
{
namespace detail
{
  /// True for messages made only of primitives and fixed-size arrays. Their in-memory bytes are
  /// sent as-is,
  /// skipping CDR serialization. Both ends are on the same machine, so the layout matches as long
  /// as they
  /// are built against the same message definition.
template<typename MessageT>
inline constexpr bool is_raw_copyable =
  rosidl_generator_traits::has_fixed_size<MessageT>::value &&
  std::is_trivially_copyable_v<MessageT>;

}  // namespace detail
}  // namespace uds_ipc

#endif  // ROS2_UDS_IPC__DETAIL__RAW_COPY_HPP_
