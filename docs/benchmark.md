# Benchmark details

Results that do not fit in the [top-level README](../README.md#benchmark). The conditions are the same:
a publisher node and a subscriber node in separate processes, 6 payload sizes x 4 publish rates
(100 Hz, 1 kHz, 10 kHz, unlimited), idle and with 20 busy-loop threads competing for the CPU.
Each condition was run once on an Intel Core i7-13700H (20 threads), WSL2 (kernel 6.18), ROS 2 Jazzy,
`rmw_fastrtps_cpp`, without CPU pinning or real-time scheduling.

- **UDS** — `publish(msg)`, non-blocking
- **UDS (send timeout)** — `publish(msg, 1s)`, waits for the subscriber instead of dropping
- **DDS (Fast DDS)** — a plain `rclcpp` publisher / subscription, reliable + keep-all, default transport

Raw data:

- [`latency_wsl2_fastdds.csv`](benchmark/latency_wsl2_fastdds.csv) — all 144 conditions, one run each. The tables on this
  page come from it. With a single run, differences of a few tens of microseconds are within run-to-run noise.
- [`latency_wsl2_fastdds_charts.csv`](benchmark/latency_wsl2_fastdds_charts.csv) — the conditions shown in the top-level
  README charts (1 kHz, and 10 kHz idle), 5 runs each. The charts show the median.

## Message loss

Share of messages that the non-blocking `publish(msg)` reported as not sent (returned `false`), by payload size.
With a send timeout, and over DDS, no message was lost in any condition.

| CPU | Rate | 64 | 256 | 1K | 4K | 16K | 64K |
|---|---|---|---|---|---|---|---|
| Idle | 100 Hz | 0% | 0% | 0% | 0% | 0% | 0% |
| Idle | 1 kHz | 0% | 0% | 0% | 0% | 0% | 0.2% |
| Idle | 10 kHz | 0% | 0% | 0% | 0.5% | 0.7% | 1.3% |
| Idle | Unlimited | 23.6% | 82.0% | 70.6% | 83.4% | 85.8% | 74.7% |
| Loaded | 100 Hz | 0% | 0% | 0% | 0% | 0% | 0% |
| Loaded | 1 kHz | 0% | 0% | 0% | 0.8% | 0.1% | 2.0% |
| Loaded | 10 kHz | 0% | 0% | 0% | 16.7% | 33.5% | 18.9% |
| Loaded | Unlimited | 53.4% | 85.0% | 84.8% | 91.3% | 91.4% | 77.4% |

## Time to connect

From creating the publisher until a subscriber that is already running can be reached
(median over the 24 conditions, with the range in parentheses).

| CPU | UDS | DDS (Fast DDS) |
|---|---|---|
| Idle | 0.2 ms (0.2 - 0.4) | 438 ms (103 - 446) |
| Loaded | 1.0 ms (0.3 - 8.5) | 1566 ms (59 - 1650) |

## Memory

Peak RSS was 27 - 28 MB per process for UDS in every condition. DDS was the same at rates the subscriber can
keep up with, and grew up to 394 MB when messages piled up (64K payload, unlimited rate). Both sides include
an `rclcpp` node, which the benchmark processes use to receive their parameters.

## Unlimited rate

When publishing as fast as possible, the latency columns of the CSV measure how long messages wait in a
queue rather than how fast the transport is. DDS (reliable + keep-all) queues everything, so its median
latency in this condition is 169 ms - 1.6 s. UDS with a send timeout makes the publisher wait once the
socket buffer is full, which keeps the median at 55 µs - 3.3 ms. Non-blocking UDS drops instead (see above).

## CSV columns

| Column | Meaning |
|---|---|
| `transport`, `rmw` | `uds`, `uds_reliable` or `dds`; the RMW implementation for `dds` |
| `payload_bytes`, `rate_hz`, `load_threads` | Condition. `rate_hz` 0 means unlimited |
| `count`, `publish_failed`, `received` | Messages published, reported as not sent, and received |
| `min_us` ... `max_us` | Latency from just before `publish()` to the subscriber callback |
| `pub_cpu_percent`, `sub_cpu_percent` | CPU usage of each process (all threads), 100 = one core |
| `pub_cpu_us_per_msg`, `sub_cpu_us_per_msg` | CPU time of each process per message |
| `connect_ms` | From creating the publisher until the subscriber can be reached |
| `pub_max_rss_kb`, `sub_max_rss_kb` | Peak resident set size of each process |
| `throughput_hz` | Messages received per second |

## Running it yourself

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

# CPU load x transport x publish rate x payload size (override with LOADS / RATES / SIZES / COUNT / RMWS),
# 5 times each.
# dds is measured once per installed RMW implementation.
REPEAT=5 ros2 run ros2_uds_ipc_benchmark run_latency_matrix.sh result.csv

# Render the CSV into charts: the median of the repeated runs (needs matplotlib)
ros2 run ros2_uds_ipc_benchmark plot_latency.py result.csv charts/
```
