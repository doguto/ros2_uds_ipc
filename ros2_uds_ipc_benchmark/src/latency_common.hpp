#ifndef LATENCY_COMMON_HPP_
#define LATENCY_COMMON_HPP_

#include <chrono>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <fstream>
#include <pthread.h>
#include <sched.h>
#include <string>

#include <rclcpp/rclcpp.hpp>
#include <rosidl_runtime_cpp/traits.hpp>
#include <std_msgs/msg/u_int8_multi_array.hpp>

#include "ros2_uds_ipc_benchmark/msg/fixed1024.hpp"
#include "ros2_uds_ipc_benchmark/msg/fixed16384.hpp"
#include "ros2_uds_ipc_benchmark/msg/fixed256.hpp"
#include "ros2_uds_ipc_benchmark/msg/fixed4096.hpp"
#include "ros2_uds_ipc_benchmark/msg/fixed64.hpp"
#include "ros2_uds_ipc_benchmark/msg/fixed65536.hpp"

// Shared by latency_publisher and latency_subscriber. Each message carries its send time
// (steady clock, shared by all processes on the host) in the first bytes of the payload.
namespace latency_benchmark
{
  // Variable-size message, serialized to CDR on every transport.
  using Message = std_msgs::msg::UInt8MultiArray;

  // Special values of the stamp at the head of the payload.
  constexpr int64_t kWarmupStamp = 0;  // Sent until the subscriber is reachable; not measured.
  constexpr int64_t kEndStamp = -1;  // Sent once after the measured messages, as an EndMarker.

  /// CPU time a process consumed (all threads, user + system) over a wall-clock window.
  struct CpuWindow
  {
    int64_t cpu_ns = 0;
    int64_t wall_ns = 0;

    /// 100 means one core fully used.
    double percent() const
    {
      return wall_ns > 0 ? 100.0 * static_cast<double>(cpu_ns) / static_cast<double>(wall_ns) : 0.0;
    }
    double us_per_message(size_t messages) const
    {
      return messages > 0 ? static_cast<double>(cpu_ns) / 1e3 / static_cast<double>(messages) : 0.0;
    }
  };

  /// Payload of the end marker. Carries the publisher's measurements,
  /// so that the subscriber can report both sides in one row.
  struct EndMarker
  {
    int64_t stamp = kEndStamp;
    CpuWindow publisher_cpu;  // Over the measured messages.
    int64_t connect_ns = 0;  // From creating the publisher until the subscriber was reachable.
    int64_t publish_failed = 0;  // Measured messages that could not be sent.
    int64_t max_rss_kb = 0;
  };

  constexpr auto kStartTimeout = std::chrono::seconds(10);
  constexpr auto kIdleTimeout = std::chrono::seconds(10);
  constexpr char kTopic[] = "uds_ipc_benchmark/latency";
  // uds_reliable: how long publish() may wait for the subscriber instead of dropping the message.
  constexpr auto kReliableSendTimeout = std::chrono::seconds(1);

  struct Options
  {
    std::string transport;  // "uds", "uds_reliable", "uds_reliable_fixed" or "dds"
    std::string socket_path;
    size_t payload_bytes;
    size_t count;
    double rate_hz;  // 0: publish as fast as possible.
    // SCHED_FIFO priority, 0: not used. uds: the receive thread (Scheduling) and the publishing thread.
    // dds: latency.launch.py starts both processes under `chrt -f`, so that every thread of the middleware has it.
    int rt_priority;
  };

  /// Declares and reads the parameters common to both nodes. Throws std::invalid_argument on bad values.
  inline Options declare_options(rclcpp::Node & node)
  {
    Options options;
    options.transport = node.declare_parameter<std::string>("transport", "uds");
    options.socket_path = node.declare_parameter<std::string>("socket_path", "/tmp/uds_ipc_benchmark/latency.sock");
    const int64_t payload_bytes = node.declare_parameter<int64_t>("payload_bytes", 64);
    const int64_t count = node.declare_parameter<int64_t>("count", 10000);
    options.rate_hz = node.declare_parameter<double>("rate_hz", 1000.0);
    options.rt_priority = static_cast<int>(node.declare_parameter<int64_t>("rt_priority", 0));

    if (options.transport != "uds" && options.transport != "uds_reliable" && options.transport != "uds_reliable_fixed" &&
      options.transport != "dds")
    {
      throw std::invalid_argument("transport must be 'uds', 'uds_reliable', 'uds_reliable_fixed' or 'dds'");
    }
    if (payload_bytes < static_cast<int64_t>(sizeof(int64_t)) || count <= 0 || options.rate_hz < 0.0) {
      throw std::invalid_argument("payload_bytes must be >= 8, count > 0 and rate_hz >= 0");
    }
    if (options.rt_priority < 0) {
      throw std::invalid_argument("rt_priority must be >= 0");
    }
    options.payload_bytes = static_cast<size_t>(payload_bytes);
    options.count = static_cast<size_t>(count);
    return options;
  }

  inline int64_t now_ns()
  {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
  }

  /// CPU time consumed so far by all threads of this process.
  inline int64_t process_cpu_ns()
  {
    timespec ts{};
    ::clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000000000 + ts.tv_nsec;
  }

  /// Peak resident set size of this process.
  inline int64_t max_rss_kb()
  {
    // Not getrusage(): ru_maxrss survives exec, so it would report the peak of the launching process.
    std::ifstream status("/proc/self/status");
    std::string key;
    while (status >> key) {
      if (key != "VmHWM:") continue;

      int64_t kb = 0;
      status >> kb;
      return kb;
    }
    return 0;
  }

  /// Calls `run(MessageT{})` with the fixed-size message type of exactly `payload_bytes` bytes.
  /// Throws std::invalid_argument if there is none.
  template <typename RunT>
  inline int dispatch_fixed(size_t payload_bytes, RunT && run)
  {
    using namespace ros2_uds_ipc_benchmark::msg;  // NOLINT(build/namespaces)
    switch (payload_bytes) {
      case 64: return run(Fixed64{});
      case 256: return run(Fixed256{});
      case 1024: return run(Fixed1024{});
      case 4096: return run(Fixed4096{});
      case 16384: return run(Fixed16384{});
      case 65536: return run(Fixed65536{});
      default:
        throw std::invalid_argument(
          "uds_reliable_fixed supports payload_bytes of 64, 256, 1024, 4096, 16384 or 65536");
    }
  }

  /// Zero-fills the payload; a variable-size message is resized to `payload_bytes`.
  template <typename MessageT>
  inline void init_payload(MessageT & msg, size_t payload_bytes)
  {
    if constexpr (rosidl_generator_traits::has_fixed_size<MessageT>::value) {
      msg.data.fill(0);
    } else {
      msg.data.assign(payload_bytes, 0);
    }
  }

  template <typename MessageT>
  inline void set_stamp(MessageT & msg, int64_t stamp)
  {
    std::memcpy(msg.data.data(), &stamp, sizeof(stamp));
  }

  /// Puts the end marker at the head of the payload. A variable-size message shrinks to the marker.
  template <typename MessageT>
  inline void set_end_marker(MessageT & msg, const EndMarker & end_marker)
  {
    if constexpr (!rosidl_generator_traits::has_fixed_size<MessageT>::value) {
      msg.data.resize(sizeof(end_marker));
    }
    std::memcpy(msg.data.data(), &end_marker, sizeof(end_marker));
  }

  /// Gives the calling thread SCHED_FIFO with `priority`. Throws std::runtime_error if the OS refuses.
  inline void set_realtime(int priority)
  {
    sched_param param{};
    param.sched_priority = priority;
    const int result = ::pthread_setschedparam(::pthread_self(), SCHED_FIFO, &param);
    if (result != 0) {
      throw std::runtime_error(
        "could not set SCHED_FIFO priority " + std::to_string(priority) + ": " + std::strerror(result));
    }
  }

  inline rclcpp::QoS dds_qos()
  {
    // Reliable + keep-all, to match the delivery guarantee of SOCK_SEQPACKET.
    return rclcpp::QoS(rclcpp::KeepAll()).reliable();
  }

}  // namespace latency_benchmark

#endif  // LATENCY_COMMON_HPP_
