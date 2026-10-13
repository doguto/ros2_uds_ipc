# ros2_uds_ipc

[English](README.md) | [日本語](README_ja.md)

A thin C++ library for ROS 2 that publishes and subscribes `rclcpp` message types as-is over Unix domain
sockets (`AF_UNIX` / `SOCK_SEQPACKET`), bypassing DDS.

## Benchmark

At a glance: median latency in µs (p99 in parentheses) from `publish()` to the subscriber callback, for a 1 KB payload at 1 kHz between two processes.

| Transport | Idle | 4 busy-loop threads |
|---|---|---|
| UDS (fixed-size) | 120 (179) | 11.8 (37.2) |
| UDS (variable-size) | 182 (277) | 33.2 (585) |
| DDS (Fast DDS) | 236 (467) | 251 (2737) |
| DDS (Cyclone DDS) | 205 (408) | 141 (1413) |
| Zenoh | 358 (500) | 921 (3746) |

The idle numbers are dominated by the CPU waking up from a deep idle state, which no transport avoids; under load the middleware layers of DDS and Zenoh are what remains.

Under load, the tail is a matter of scheduling rather than of the transport: every so often a thread on the
receive path waits behind other work, and the largest latency of a run is 2 - 5 ms for all of them. A real-time
priority removes it. With one (256 B at 1 kHz), the largest latency was:

| Transport | Largest latency | What was made real-time |
|---|---|---|
| UDS (fixed-size) | 27 µs | The receive thread and the publishing thread |
| UDS (variable-size) | 57 µs | The receive thread and the publishing thread |
| DDS (Cyclone DDS) | 95 µs | The whole publisher and subscriber processes |
| DDS (Fast DDS) | 115 µs | The whole publisher and subscriber processes |
| Zenoh | 145 µs | The whole publisher and subscriber processes |

UDS stays ahead by roughly 2x (variable-size) to 5x (fixed-size), and needs the priority on a single thread, which is a
[constructor argument](#real-time-scheduling) ([details](docs/benchmark.md#real-time-scheduling)).

Measured with the `ros2_uds_ipc_benchmark` package: a publisher node and a subscriber node in separate
processes, 6 payload sizes, idle and with 4 busy-loop threads competing for the CPU.

- **UDS (variable-size)** — `publish(msg, 1s)` with `std_msgs/UInt8MultiArray`, serialized to CDR. It waits for the
  subscriber instead of dropping a message when its queue is full.
- **UDS (fixed-size)** — the same with a message made only of primitives and a fixed-size array, which is sent
  without serialization.
- **DDS (Fast DDS)** / **DDS (Cyclone DDS)** — a plain `rclcpp` publisher / subscription with `rmw_fastrtps_cpp` /
  `rmw_cyclonedds_cpp`, reliable + keep-all, default transport
- **Zenoh** — the same with `rmw_zenoh_cpp`, through a local `rmw_zenohd` router

Environment: Intel Core i5-3427U (4 threads), Linux 7.0.0-31-generic, ROS 2 Jazzy.
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
- **Stage 1 is skipped for fixed-size messages.** Messages made only of primitives and fixed-size arrays are sent
  as their in-memory bytes. For any other rosidl type, including variable-length ones, CDR is used, and DDS pays
  the same cost.
- **Stage 3 remains.** It is determined by the scheduler and CPU power states. The library cannot remove it,
  but a subscriber can opt in to a real-time priority and CPU pinning for its receive thread, so that it is not
  kept waiting behind other work (see [Real-time scheduling](#real-time-scheduling)).

### Features

`ros2_uds_ipc` keeps the ROS 2 message types but replaces the transport:

- **Any rosidl message type** — a message is serialized to CDR once and sent with a single `send()`.
  Variable-length messages work.
- **No serialization for fixed-size messages** — a message made only of primitives and fixed-size arrays (for
  example `Float64[7]`) is sent as its in-memory bytes and received straight into the message object. This needs
  both ends to be built against the same message definition, which holds on one machine.
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

### Real-time scheduling

By default the receive thread is an ordinary thread. When the CPU is busy with other work (image processing,
planning), it waits its turn like any other, which shows up as a tail of late callbacks. For the few
subscribers that have a deadline, you can run the receive thread as `SCHED_FIFO` and optionally pin it to a CPU:

```cpp
uds_ipc::Scheduling scheduling;
scheduling.rt_priority = 40;  // 1 - 49, 0 (default) leaves it to the OS
scheduling.cpu = -1;          // a CPU number to pin to, -1 for any
scheduling.require = false;   // true: throw instead of warning when the OS refuses

uds_ipc::UdsSubscriber<sensor_msgs::msg::JointState> subscriber(
  "/run/my_robot/joint_command.sock", callback, 65536, nullptr, scheduling);
if (!subscriber.scheduling_applied()) { /* the OS refused; running with the default scheduling */ }
```

| Do | Don't |
|---|---|
| Use it only for the one or two subscribers that have a deadline (e.g. motor commands) | Put every subscriber on `SCHED_FIFO` |
| Keep the callback short: copy the data and return | Block, allocate or take a lock that normal threads also take (priority inversion) |
| Use 30 - 49 | Expect more from a higher number. Priorities of 50 and above are rejected: Linux runs its interrupt handler threads at 50, and the receive thread must never get ahead of the kernel |
| Pin to a CPU only if it is kept free of other work (`isolcpus`, cpuset) | Pin to a shared core |

The OS must allow it, otherwise a warning is logged and the thread keeps the default scheduling
(`require = true` makes the constructor throw instead). Either:

- add `@realtime - rtprio 49` and `@realtime - memlock unlimited` to `/etc/security/limits.d/99-realtime.conf`,
  and put the user in the `realtime` group (log in again), or
- `LimitRTPRIO=49` in the systemd unit, or `--cap-add=SYS_NICE --ulimit rtprio=49` in Docker.

The publishing thread is yours: if it runs the control loop, give it a real-time priority the same way
(`pthread_setschedparam`). `mlockall(MCL_CURRENT | MCL_FUTURE)` at start-up avoids page faults on that path.

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
