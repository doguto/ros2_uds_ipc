# ros2_uds_ipc

[English](README.md) | [日本語](README_ja.md)

A thin C++ library for ROS 2 that publishes and subscribes `rclcpp` message types as-is over Unix domain
sockets (`AF_UNIX` / `SOCK_SEQPACKET`), bypassing DDS.

## Concept

DDS brings discovery, QoS matching and the RTPS protocol layer. For an event-driven path between two
processes on the same machine that must react the moment a message arrives (e.g. `ros2_control` -> CAN
gateway), that machinery is latency you do not need.

`ros2_uds_ipc` keeps the ROS 2 message types but replaces the transport:

- **Any rosidl message type** — a message is serialized to CDR once and sent with a single `send()`.
  Variable-length messages work.
- **Reliable, boundary-preserving** — `SOCK_SEQPACKET` is connection-oriented and keeps datagram
  boundaries (one `send` = one `recv`), so there is no manual framing and delivery is not best-effort.
- **Never blocks the sender** — `publish()` is non-blocking and returns `false` when the peer is not
  connected or sending fails. The caller decides whether to log or fall back.
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
  // Not connected (e.g. the subscriber has not started yet) or sending failed.
}
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

Not measured yet.

The measurement tools live in a separate package, `ros2_uds_ipc_benchmark`, so that nothing from them
is built into `ros2_uds_ipc`. It measures cross-process latency of UDS and of plain DDS pub / sub
under the same conditions, together with the CPU usage and peak memory of the publisher and subscriber
processes, the number of failed publishes and the time to connect, and writes the result as CSV. It can also run under CPU load (`load_threads`).

```bash
colcon build --packages-up-to ros2_uds_ipc_benchmark
source install/setup.bash

# A single condition
ros2 launch ros2_uds_ipc_benchmark latency.launch.py transport:=uds payload_bytes:=1024 count:=10000 rate_hz:=1000.0

# Same, with 8 busy-loop threads competing for the CPU
ros2 launch ros2_uds_ipc_benchmark latency.launch.py transport:=uds payload_bytes:=1024 load_threads:=8

# CPU load x transport x publish rate x payload size (override with LOADS / RATES / SIZES / COUNT / RMWS).
# dds is measured once per installed RMW implementation.
ros2 run ros2_uds_ipc_benchmark run_latency_matrix.sh result.csv
```

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
