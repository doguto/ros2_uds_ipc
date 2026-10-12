# ros2_uds_ipc

[English](README.md) | [日本語](README_ja.md)

A thin C++ library for ROS 2 that publishes and subscribes `rclcpp` message types as-is over Unix domain
sockets (`AF_UNIX` / `SOCK_SEQPACKET`), bypassing DDS.

## Concept

DDS brings discovery, QoS matching and the RTPS protocol layer. For an event-driven path between two
processes on the same machine that must react the moment a message arrives (e.g. `ros2_control` -> CAN
gateway), that machinery is latency you do not need.

### Where the latency comes from

The path from the publisher handing over a message to the subscriber callback being invoked has four stages:

| Stage | What happens |
|---|---|
| 1. Serialization | The ROS message is turned into CDR bytes |
| 2. Data transfer | The bytes are moved from the sending process to the receiving process |
| 3. Receiver wake-up | The kernel wakes the waiting receive thread and puts it on a CPU |
| 4. Middleware layers | RTPS processing -> DDS receive thread -> wait set -> executor -> callback |

For commands of tens to hundreds of bytes, the copy in stage 2 takes very little time and almost all of the
latency comes from stages 3 and 4. Shared memory and zero-copy speed up stage 2, so stages 3 and 4 remain
unchanged when you use them.

### What ros2_uds_ipc skips

```
DDS:           kernel -> DDS receive thread -> RTPS processing -> wait set -> executor -> callback
ros2_uds_ipc:  kernel -> dedicated thread waiting in poll() -> callback
```

- **Stage 4 is skipped entirely.** The receive thread woken by the kernel deserializes the message and
  invokes the callback itself. There is no hand-off between threads.
- **Stage 2 is only a copy through the kernel**, which is negligible for small messages.
- **Stage 1 remains.** CDR is used to support any rosidl type, including variable-length ones. DDS pays
  the same cost.
- **Stage 3 remains.** It is determined by the scheduler and CPU power states, and is addressed outside
  the library (real-time scheduling, CPU pinning and so on).

### Features

`ros2_uds_ipc` keeps the ROS 2 message types but replaces the transport:

- **Any rosidl message type** — a message is serialized to CDR once and sent with a single `send()`.
  Variable-length messages work.
- **Reliable, boundary-preserving** — `SOCK_SEQPACKET` is connection-oriented and keeps datagram
  boundaries (one `send` = one `recv`), so there is no manual framing and delivery is not best-effort.
- **Never blocks the sender by default** — `publish()` is non-blocking and returns `false` when the peer is
  not connected or its receive queue is full, so a message is never dropped silently. The caller decides
  whether to log or fall back.
- **Optional back-pressure** — `publish(msg, timeout)` waits for a slow subscriber for up to `timeout`
  instead of dropping the message.
- **Automatic reconnection** — either side can be restarted; the publisher reconnects on the next
  `publish()`.
- **No per-message heap allocation** on the hot path (buffers are reused).
- **Optional DDS mirror** — the UDS path is invisible to `ros2 topic echo` and rosbag, so the same CDR
  bytes can also be forwarded to a regular DDS topic for debugging and recording. The DDS publish runs
  on a dedicated thread and only while the mirror topic has subscribers.

The connection is 1:1: one `UdsPublisher` talks to one `UdsSubscriber` through one socket path.

## Requirements

- Linux
- ROS 2 (developed on Jazzy)
- C++17

## Installation

```bash
cd ~/ros2_ws/src
git clone https://github.com/doguto/ros2_uds_ipc.git
cd ~/ros2_ws
colcon build --packages-select ros2_uds_ipc
```

## How to Use

`package.xml`:

```xml
<depend>ros2_uds_ipc</depend>
```

`CMakeLists.txt`:

```cmake
find_package(ros2_uds_ipc REQUIRED)
target_link_libraries(my_node ros2_uds_ipc::ros2_uds_ipc)
```

### Subscriber

The subscriber owns the socket file. It creates the parent directory if needed and removes the file on
destruction.

```cpp
#include <ros2_uds_ipc/uds_subscriber.hpp>

uds_ipc::UdsSubscriber<sensor_msgs::msg::JointState> subscriber(
  "/run/my_robot/joint_command.sock",
  [](const sensor_msgs::msg::JointState & msg) {
    // Called from the subscriber's own thread, not from an rclcpp executor thread.
    // Synchronize access to shared state yourself.
  });
```

The constructor throws `std::runtime_error` if the socket cannot be set up. The third argument,
`max_message_size` (default 65536 bytes), is the largest serialized message that can be received;
anything larger is truncated.

### Publisher

```cpp
#include <ros2_uds_ipc/uds_publisher.hpp>

uds_ipc::UdsPublisher<sensor_msgs::msg::JointState> publisher("/run/my_robot/joint_command.sock");

sensor_msgs::msg::JointState msg;
if (!publisher.publish(msg)) {
  // Not connected (e.g. the subscriber has not started yet) or the subscriber's queue is full.
}

// Wait for up to 10 ms for a slow subscriber instead of dropping the message.
publisher.publish(msg, std::chrono::milliseconds(10));
```

How many messages may wait for the subscriber is bounded by the socket send buffer. The third constructor
argument sets it in bytes (`SO_SNDBUF`; 0 keeps the system default). The kernel clamps it to
`net.core.wmem_max`, so raise that sysctl to go beyond it.

```cpp
uds_ipc::UdsPublisher<sensor_msgs::msg::JointState> publisher(
  "/run/my_robot/joint_command.sock", nullptr, 1024 * 1024);
```

### Mirroring to DDS

Pass a publisher created with `create_uds_mirror()` to either side to make the messages visible to
`ros2 topic echo` and rosbag. Mirrored messages may be delayed by up to 10 ms and are dropped (oldest
first) if DDS cannot keep up, so use the mirror for observation only.

```cpp
#include <ros2_uds_ipc/uds_mirror.hpp>

auto mirror = uds_ipc::create_uds_mirror<sensor_msgs::msg::JointState>(*node, "joint_command");
uds_ipc::UdsPublisher<sensor_msgs::msg::JointState> publisher("/run/my_robot/joint_command.sock", mirror);
```

## Benchmark

Measured with the `ros2_uds_ipc_benchmark` package: a publisher node and a subscriber node in separate
processes, 6 payload sizes, idle and with 20 busy-loop threads competing for the CPU.

- **UDS (send timeout)** — `publish(msg, 1s)`, which waits for the subscriber instead of dropping a message
  when its queue is full. Below the rates where that happens, the non-blocking `publish(msg)` performs the same.
- **DDS (Fast DDS)** — a plain `rclcpp` publisher / subscription, reliable + keep-all, default transport

Environment: Intel Core i7-13700H (20 threads), WSL2 (kernel 6.18), ROS 2 Jazzy, `rmw_fastrtps_cpp`.
The charts show the median of 5 runs per condition, measured without CPU pinning or real-time scheduling.

Other publish rates, message loss, time to connect, memory, the raw data and how to run the benchmark
yourself are in [docs/benchmark.md](docs/benchmark.md).

### Latency

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/benchmark/latency_1khz-dark.svg">
  <img src="docs/benchmark/latency_1khz.svg" alt="Median and p99 latency at 1 kHz by payload size, idle and under CPU load">
</picture>

### CPU usage

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/benchmark/cpu_per_message-dark.svg">
  <img src="docs/benchmark/cpu_per_message.svg" alt="CPU time per message at 10 kHz, publisher and subscriber combined">
</picture>

## Tech Stack

- C++17, `rclcpp` (CDR serialization, `GenericPublisher` for the mirror)
- POSIX sockets: `AF_UNIX` + `SOCK_SEQPACKET`, `poll()`
- `ament_cmake`, GoogleTest

## Testing

```bash
colcon test --packages-select ros2_uds_ipc
colcon test-result --verbose
```

## License

[MIT](LICENSE)
