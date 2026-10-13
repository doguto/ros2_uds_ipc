#ifndef ROS2_UDS_IPC__DETAIL__CDR_CODEC_HPP_
#define ROS2_UDS_IPC__DETAIL__CDR_CODEC_HPP_

#include <cstddef>
#include <cstdint>
#include <stdexcept>

#include <fastcdr/Cdr.h>
#include <fastcdr/FastBuffer.h>
#include <fastcdr/config.h>
#include <fastcdr/exceptions/Exception.h>
#include <rclcpp/serialized_message.hpp>
#include <rosidl_typesupport_cpp/message_type_support.hpp>
#include <rosidl_typesupport_fastrtps_cpp/identifier.hpp>
#include <rosidl_typesupport_fastrtps_cpp/message_type_support.h>

#if FASTCDR_VERSION_MAJOR < 2
#error "ros2_uds_ipc needs Fast CDR 2.x (ROS 2 Jazzy or later)"
#endif

namespace uds_ipc
{
namespace detail
{
  /// Converts messages to and from CDR by calling the generated Fast CDR type support directly.
  /// rclcpp::Serialization goes through the RMW layer, which costs far more than the conversion itself.
  /// The bytes are the same, so they can be handed to a DDS publisher as they are.
  template <typename MessageT>
  class CdrCodec
  {
  public:
    CdrCodec()
    {
      const rosidl_message_type_support_t * type_support = get_message_typesupport_handle(
        rosidl_typesupport_cpp::get_message_type_support_handle<MessageT>(),
        rosidl_typesupport_fastrtps_cpp::typesupport_identifier);
      if (!type_support) {
        throw std::runtime_error("ros2_uds_ipc: the message type has no Fast CDR type support");
      }
      callbacks_ = static_cast<const message_type_support_callbacks_t *>(type_support->data);
    }

    /// Grows out as needed. Returns false if the message could not be serialized.
    bool serialize(const MessageT & msg, rclcpp::SerializedMessage & out) const
    {
      const size_t size = kEncapsulationBytes + callbacks_->get_serialized_size(&msg);
      if (out.capacity() < size) {
        out.reserve(size);
      }
      auto & rcl_msg = out.get_rcl_serialized_message();
      try {
        eprosima::fastcdr::FastBuffer buffer(reinterpret_cast<char *>(rcl_msg.buffer), rcl_msg.buffer_capacity);
        eprosima::fastcdr::Cdr cdr(
          buffer, eprosima::fastcdr::Cdr::DEFAULT_ENDIAN, eprosima::fastcdr::CdrVersion::XCDRv1);
        cdr.set_encoding_flag(eprosima::fastcdr::EncodingAlgorithmFlag::PLAIN_CDR);
        cdr.serialize_encapsulation();
        if (!callbacks_->cdr_serialize(&msg, cdr)) return false;

        rcl_msg.buffer_length = cdr.get_serialized_data_length();
        return true;
      } catch (const eprosima::fastcdr::exception::Exception &) {
        return false;
      }
    }

    /// Returns false if data is not a complete message of this type (e.g. it was truncated).
    bool deserialize(uint8_t * data, size_t length, MessageT & msg) const
    {
      try {
        eprosima::fastcdr::FastBuffer buffer(reinterpret_cast<char *>(data), length);
        eprosima::fastcdr::Cdr cdr(
          buffer, eprosima::fastcdr::Cdr::DEFAULT_ENDIAN, eprosima::fastcdr::CdrVersion::XCDRv1);
        cdr.read_encapsulation();
        return callbacks_->cdr_deserialize(cdr, &msg);
      } catch (const eprosima::fastcdr::exception::Exception &) {
        return false;
      }
    }

  private:
    static constexpr size_t kEncapsulationBytes = 4;

    const message_type_support_callbacks_t * callbacks_;
  };

}  // namespace detail
}  // namespace uds_ipc

#endif  // ROS2_UDS_IPC__DETAIL__CDR_CODEC_HPP_
