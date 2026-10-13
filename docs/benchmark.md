# Benchmark details

Results that do not fit in the [top-level README](../README.md#benchmark). The conditions are the same:
a publisher node and a subscriber node in separate processes, 6 payload sizes x 4 publish rates
(100 Hz, 1 kHz, 10 kHz, unlimited), idle and with 4 busy-loop threads competing for the CPU (one per hardware thread).
Each condition was run 5 times and the tables show the median. The machine was an Intel Core i5-3427U (4 threads), Linux 7.0.0-31-generic, ROS 2 Jazzy,
without CPU pinning or real-time scheduling.

- **UDS** — `publish(msg)`, non-blocking
- **UDS (send timeout)** — `publish(msg, 1s)`, waits for the subscriber instead of dropping
- **DDS (Fast DDS)** — a plain `rclcpp` publisher / subscription (`rmw_fastrtps_cpp`), reliable + keep-all, default transport
- **DDS (Cyclone DDS)** — the same with `rmw_cyclonedds_cpp`
- **Zenoh** — the same with `rmw_zenoh_cpp`, through a local `rmw_zenohd` router

Raw data: [`latency.csv`](benchmark/latency.csv) — all 1200 measurements (240 conditions x 5 runs). The tables on this page
are the median of the 5 runs, which is also what the charts in the README show.

## Notes

- **Idle is slower than loaded for UDS** (330 µs vs 35 µs at 100 Hz). With nothing else running, the CPU sits in a deep
  idle state between messages and waking it up dominates the latency (stage 3 in the README). Busy-loop threads keep
  the cores awake. This is why the "Idle" panels look worse than the loaded ones for the transports that are
  otherwise fast.
- **Under load, the difference between UDS and the middlewares is much larger** (3x to 30x at 1 kHz), since the DDS /
  Zenoh receive path needs several threads to be scheduled in turn.
- **Fast DDS at 10 kHz with 64K, and at an unlimited rate**, queues messages (reliable + keep-all) and the latency
  columns then measure the time spent in the queue (up to seconds), not the transport speed.
- Unlimited-rate rows are not a measure of speed for any transport; see below.

## Latency (µs)

Median (p99) from just before `publish()` to the subscriber callback.

### Idle

| Rate | Transport | 64 | 256 | 1K | 4K | 16K | 64K |
|---|---|---|---|---|---|---|---|
| 100 Hz | UDS | 332 (365) | 335 (387) | 330 (359) | 349 (407) | 394 (460) | 567 (659) |
| 100 Hz | UDS (send timeout) | 332 (378) | 336 (405) | 365 (449) | 392 (464) | 435 (500) | 563 (667) |
| 100 Hz | DDS (Fast DDS) | 648 (745) | 667 (776) | 647 (741) | 680 (759) | 715 (820) | 980 (1088) |
| 100 Hz | DDS (Cyclone DDS) | 622 (698) | 567 (695) | 598 (681) | 590 (723) | 771 (928) | 1104 (1424) |
| 100 Hz | Zenoh | 959 (1088) | 955 (1031) | 936 (1027) | 946 (1045) | 996 (1098) | 1348 (1458) |
| 1 kHz | UDS | 178 (271) | 161 (303) | 125 (193) | 190 (330) | 208 (286) | 270 (484) |
| 1 kHz | UDS (send timeout) | 123 (203) | 124 (189) | 184 (307) | 189 (266) | 212 (395) | 266 (485) |
| 1 kHz | DDS (Fast DDS) | 244 (381) | 235 (462) | 234 (327) | 244 (361) | 262 (346) | 362 (462) |
| 1 kHz | DDS (Cyclone DDS) | 207 (423) | 208 (444) | 207 (424) | 221 (364) | 263 (578) | 510 (701) |
| 1 kHz | Zenoh | 359 (484) | 364 (496) | 345 (475) | 348 (444) | 366 (492) | 511 (625) |
| 10 kHz | UDS | 72.2 (165) | 72.5 (151) | 72.4 (175) | 77.2 (156) | 98.9 (205) | 101 (178) |
| 10 kHz | UDS (send timeout) | 72.0 (165) | 67.3 (161) | 77.3 (163) | 81.3 (178) | 86.8 (189) | 109 (193) |
| 10 kHz | DDS (Fast DDS) | 117 (339) | 116 (277) | 112 (265) | 107 (266) | 95.7 (4863) | 579581 (831553) |
| 10 kHz | DDS (Cyclone DDS) | 100 (222) | 97.4 (211) | 95.6 (202) | 94.7 (211) | 90.0 (228) | 149 (250) |
| 10 kHz | Zenoh | 86.8 (167) | 84.5 (123) | 80.6 (120) | 80.3 (177) | 92.5 (121) | 145 (541) |
| Unlimited | UDS | 23.1 (4061) | 37.1 (2497) | 618 (1429) | 420 (440) | 130 (248) | 138 (167) |
| Unlimited | UDS (send timeout) | 1879 (3840) | 1403 (2374) | 24.4 (1359) | 25.1 (426) | 34.9 (256) | 63.6 (172) |
| Unlimited | DDS (Fast DDS) | 354670 (626048) | 3289859 (3501628) | 4743303 (4948243) | 253579 (375095) | 291738 (515220) | 614527 (837045) |
| Unlimited | DDS (Cyclone DDS) | 51.0 (7243) | 52.8 (2289) | 53.6 (2096) | 53.0 (141) | 72.3 (157) | 149 (253) |
| Unlimited | Zenoh | 111707 (130256) | 96101 (120074) | 76499 (95419) | 40129 (53261) | 6910 (14504) | 3778 (4678) |

### 4 busy-loop threads

| Rate | Transport | 64 | 256 | 1K | 4K | 16K | 64K |
|---|---|---|---|---|---|---|---|
| 100 Hz | UDS | 34.1 (573) | 35.1 (535) | 35.0 (426) | 39.4 (1015) | 47.9 (1017) | 69.9 (1029) |
| 100 Hz | UDS (send timeout) | 35.9 (959) | 35.6 (296) | 34.3 (261) | 39.0 (699) | 47.8 (1018) | 68.7 (1028) |
| 100 Hz | DDS (Fast DDS) | 806 (3086) | 143 (3003) | 120 (2673) | 714 (3020) | 616 (3012) | 198 (3270) |
| 100 Hz | DDS (Cyclone DDS) | 77.2 (1825) | 420 (1474) | 83.3 (2225) | 187 (2068) | 930 (3119) | 661 (3687) |
| 100 Hz | Zenoh | 828 (3322) | 800 (3711) | 387 (3348) | 755 (3311) | 410 (3435) | 921 (3847) |
| 1 kHz | UDS | 32.2 (155) | 31.9 (64.8) | 32.4 (571) | 34.6 (66.2) | 42.7 (93.2) | 64.3 (158) |
| 1 kHz | UDS (send timeout) | 32.4 (1002) | 32.5 (669) | 32.7 (73.4) | 34.6 (77.7) | 42.7 (116) | 64.3 (709) |
| 1 kHz | DDS (Fast DDS) | 692 (3205) | 667 (3694) | 1056 (3799) | 980 (3668) | 971 (3517) | 1037 (4467) |
| 1 kHz | DDS (Cyclone DDS) | 94.6 (1296) | 501 (2013) | 182 (1741) | 476 (2047) | 920 (3241) | 750 (3385) |
| 1 kHz | Zenoh | 1013 (3738) | 1001 (3293) | 1094 (3836) | 1008 (3518) | 801 (3737) | 2002 (5161) |
| 10 kHz | UDS | 424 (1315) | 415 (1877) | 364 (1135) | 348 (1730) | 260 (1648) | 377 (2172) |
| 10 kHz | UDS (send timeout) | 384 (1426) | 338 (1325) | 369 (1543) | 279 (1009) | 348 (2873) | 1053 (2930) |
| 10 kHz | DDS (Fast DDS) | 2747 (5596) | 777 (4832) | 1486 (5055) | 539 (6924) | 3733 (41156) | 1715994 (2165613) |
| 10 kHz | DDS (Cyclone DDS) | 1076 (3717) | 685 (3686) | 746 (3326) | 672 (8042) | 1297 (4276) | 1628 (4045) |
| 10 kHz | Zenoh | 1667 (4155) | 1478 (4528) | 1474 (4882) | 1608 (4515) | 1527 (8901) | 3005 (14911) |
| Unlimited | UDS | 5952 (14173) | 3224 (10364) | 2355 (6464) | 153 (3455) | 40.8 (2860) | 111 (3040) |
| Unlimited | UDS (send timeout) | 3654 (12744) | 2021 (8105) | 1047 (5857) | 489 (2770) | 196 (2865) | 364 (2924) |
| Unlimited | DDS (Fast DDS) | 178509 (304496) | 129296 (256117) | 144694 (340946) | 387307 (815302) | 613614 (1078183) | 1722089 (1877077) |
| Unlimited | DDS (Cyclone DDS) | 29912 (62271) | 12544 (37463) | 8273 (23327) | 2577 (32732) | 1420 (6822) | 1763 (4060) |
| Unlimited | Zenoh | 91741 (150565) | 107465 (152393) | 81344 (158891) | 29751 (57660) | 3201 (22358) | 2822 (15712) |

At an unlimited rate the latency measures how long messages wait in a queue rather than how fast the transport is.

## Unlimited rate

When publishing as fast as possible, the latency columns of the CSV measure how long messages wait in a
queue rather than how fast the transport is. DDS (reliable + keep-all) and Zenoh queue everything, so their median
latency in this condition ranges from milliseconds to seconds. UDS with a send timeout makes the publisher wait once
the socket buffer is full, which keeps the latency low. Non-blocking UDS drops instead (see Message loss).

## Message loss

Share of messages that `publish()` reported as not sent. Series that never lost a message are listed below the table.

| CPU | Rate | Transport | 64 | 256 | 1K | 4K | 16K | 64K |
|---|---|---|---|---|---|---|---|---|
| Idle | 100 Hz | UDS | 0.0% | 0.0% | 0.0% | 0.0% | 0.0% | 0.0% |
| Idle | 1 kHz | UDS | 0.0% | 0.0% | 0.0% | 0.0% | 0.0% | 0.0% |
| Idle | 10 kHz | UDS | 0.0% | 0.0% | 0.0% | 0.0% | 0.0% | 0.0% |
| Idle | Unlimited | UDS | 35.4% | 36.6% | 33.7% | 61.4% | 39.4% | 42.5% |
| 4 busy-loop threads | 100 Hz | UDS | 0.0% | 0.0% | 0.0% | 0.0% | 0.0% | 0.0% |
| 4 busy-loop threads | 1 kHz | UDS | 0.0% | 0.0% | 0.0% | 0.0% | 0.0% | 0.0% |
| 4 busy-loop threads | 10 kHz | UDS | 0.0% | 0.0% | 0.0% | 1.0% | 12.7% | 38.9% |
| 4 busy-loop threads | Unlimited | UDS | 50.4% | 58.3% | 72.1% | 79.4% | 82.0% | 80.6% |

No message was lost: UDS (send timeout), DDS (Fast DDS), DDS (Cyclone DDS), Zenoh.

## Time to connect

From creating the publisher until a subscriber that is already running can be reached (median over all conditions, with the range in parentheses).

| CPU | UDS | UDS (send timeout) | DDS (Fast DDS) | DDS (Cyclone DDS) | Zenoh |
|---|---|---|---|---|---|
| Idle | 0.4 ms (0.4 - 0.5) | 0.4 ms (0.4 - 0.5) | 100.2 ms (98.0 - 101.6) | 0.6 ms (0.6 - 0.7) | 0.5 ms (0.5 - 0.7) |
| 4 busy-loop threads | 0.8 ms (0.4 - 3.4) | 0.6 ms (0.4 - 3.2) | 1594.5 ms (0.0 - 1598.8) | 1.6 ms (0.7 - 3.9) | 0.7 ms (0.6 - 3.6) |

## Memory

Peak RSS of the publisher / subscriber process (MB): median, and maximum in parentheses.

| Transport | Publisher | Subscriber |
|---|---|---|
| UDS | 27 (28) | 28 (28) |
| UDS (send timeout) | 27 (28) | 28 (28) |
| DDS (Fast DDS) | 28 (395) | 28 (302) |
| DDS (Cyclone DDS) | 16 (17) | 17 (20) |
| Zenoh | 23 (23) | 23 (53) |

## CPU time per message (µs, publisher + subscriber)

| CPU | Rate | Transport | 64 | 256 | 1K | 4K | 16K | 64K |
|---|---|---|---|---|---|---|---|---|
| Idle | 100 Hz | UDS | 507 | 510 | 532 | 527 | 581 | 712 |
| Idle | 100 Hz | UDS (send timeout) | 514 | 546 | 523 | 537 | 629 | 733 |
| Idle | 100 Hz | DDS (Fast DDS) | 1080 | 1090 | 1105 | 1102 | 1247 | 1962 |
| Idle | 100 Hz | DDS (Cyclone DDS) | 901 | 934 | 933 | 1110 | 1554 | 2138 |
| Idle | 100 Hz | Zenoh | 1299 | 1322 | 1306 | 1306 | 1372 | 1893 |
| Idle | 1 kHz | UDS | 238 | 255 | 203 | 278 | 251 | 344 |
| Idle | 1 kHz | UDS (send timeout) | 208 | 201 | 258 | 229 | 309 | 372 |
| Idle | 1 kHz | DDS (Fast DDS) | 427 | 426 | 391 | 416 | 461 | 578 |
| Idle | 1 kHz | DDS (Cyclone DDS) | 351 | 337 | 324 | 332 | 450 | 742 |
| Idle | 1 kHz | Zenoh | 498 | 504 | 499 | 518 | 510 | 765 |
| Idle | 10 kHz | UDS | 106 | 104 | 111 | 109 | 128 | 125 |
| Idle | 10 kHz | UDS (send timeout) | 104 | 98.9 | 108 | 114 | 115 | 134 |
| Idle | 10 kHz | DDS (Fast DDS) | 166 | 165 | 162 | 157 | 150 | 332 |
| Idle | 10 kHz | DDS (Cyclone DDS) | 143 | 138 | 135 | 130 | 148 | 233 |
| Idle | 10 kHz | Zenoh | 143 | 141 | 135 | 136 | 152 | 249 |
| 4 busy-loop threads | 100 Hz | UDS | 74.1 | 74.4 | 76.8 | 83.6 | 93.4 | 109 |
| 4 busy-loop threads | 100 Hz | UDS (send timeout) | 78.5 | 76.8 | 69.1 | 81.8 | 91.1 | 115 |
| 4 busy-loop threads | 100 Hz | DDS (Fast DDS) | 171 | 169 | 165 | 168 | 164 | 289 |
| 4 busy-loop threads | 100 Hz | DDS (Cyclone DDS) | 116 | 118 | 125 | 140 | 196 | 272 |
| 4 busy-loop threads | 100 Hz | Zenoh | 171 | 170 | 174 | 172 | 184 | 276 |
| 4 busy-loop threads | 1 kHz | UDS | 61.5 | 59.4 | 60.2 | 61.6 | 71.0 | 90.1 |
| 4 busy-loop threads | 1 kHz | UDS (send timeout) | 56.0 | 58.4 | 61.7 | 61.8 | 71.6 | 88.2 |
| 4 busy-loop threads | 1 kHz | DDS (Fast DDS) | 119 | 129 | 127 | 129 | 148 | 194 |
| 4 busy-loop threads | 1 kHz | DDS (Cyclone DDS) | 104 | 96.9 | 110 | 112 | 138 | 250 |
| 4 busy-loop threads | 1 kHz | Zenoh | 150 | 147 | 145 | 154 | 156 | 256 |
| 4 busy-loop threads | 10 kHz | UDS | 29.9 | 34.1 | 35.0 | 37.7 | 46.4 | 74.0 |
| 4 busy-loop threads | 10 kHz | UDS (send timeout) | 29.4 | 36.9 | 32.1 | 36.6 | 44.7 | 74.3 |
| 4 busy-loop threads | 10 kHz | DDS (Fast DDS) | 52.7 | 57.5 | 65.8 | 68.8 | 80.4 | 247 |
| 4 busy-loop threads | 10 kHz | DDS (Cyclone DDS) | 61.5 | 52.5 | 58.7 | 57.2 | 82.2 | 190 |
| 4 busy-loop threads | 10 kHz | Zenoh | 56.5 | 54.6 | 63.4 | 77.6 | 82.7 | 244 |

## CSV columns

| Column | Meaning |
|---|---|
| `transport`, `rmw` | `uds`, `uds_reliable` or `dds`; the RMW implementation for `dds` (`rmw_fastrtps_cpp`, `rmw_cyclonedds_cpp`, `rmw_zenoh_cpp`) |
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

# Render the CSV into a Markdown report with all the tables on this page
ros2 run ros2_uds_ipc_benchmark report_latency.py result.csv benchmark.md
```

To do all of it in one go (build, every installed RMW including a Zenoh router, charts and report):

```bash
scripts/run_all_benchmarks.sh   # writes to benchmark_result/<timestamp>/
```
