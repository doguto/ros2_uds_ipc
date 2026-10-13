# Benchmark details

Results that do not fit in the [top-level README](../README.md#benchmark). The conditions are the same:
a publisher node and a subscriber node in separate processes, 6 payload sizes x 4 publish rates
(100 Hz, 1 kHz, 10 kHz, unlimited), idle and with 4 busy-loop threads competing for the CPU (one per hardware thread).
Each condition was run 5 times and the tables show the median. The machine was an Intel Core i5-3427U (4 threads), Linux 7.0.0-31-generic, ROS 2 Jazzy,
without CPU pinning or real-time scheduling.

| Series | Message | Serialization | When the receive queue is full |
|---|---|---|---|
| **UDS (variable-size)** | `std_msgs/UInt8MultiArray` | CDR | `publish(msg, 1s)` waits for the subscriber |
| **UDS (fixed-size)** | `uint8[N]` (`msg/Fixed<N>.msg`) | none, the message is sent as-is | `publish(msg, 1s)` waits for the subscriber |
| **DDS (Fast DDS)** | `std_msgs/UInt8MultiArray` | CDR | reliable + keep-all (`rmw_fastrtps_cpp`) |
| **DDS (Cyclone DDS)** | `std_msgs/UInt8MultiArray` | CDR | reliable + keep-all (`rmw_cyclonedds_cpp`) |
| **Zenoh** | `std_msgs/UInt8MultiArray` | CDR | reliable + keep-all (`rmw_zenoh_cpp`, local `rmw_zenohd` router) |

Both UDS series use a send timeout, so that a message is delivered or reported as failed, never dropped silently.
The non-blocking `publish(msg)` (`transport:=uds`) is not part of the default matrix; it behaves the same below
the rates where the queue fills up.

Raw data: [`latency.csv`](benchmark/latency.csv) — all 1200 measurements (240 conditions x 5 runs). The tables on this page
are the median of the 5 runs, which is also what the charts in the README show.

## Notes

- **Skipping serialization helps.** Fixed-size messages are about 2 - 3x faster than variable-size ones: 170 µs vs
  330 µs at 100 Hz idle, 30 µs vs 75 µs at 10 kHz idle, and 12 µs vs 33 µs at 100 Hz - 1 kHz under load (64 B -
  1K). The gain shrinks as the payload grows (64K: 290 vs 570 µs at 100 Hz idle), and it applies only to messages
  made only of primitives and fixed-size arrays.
- **Idle is slower than loaded for UDS** (330 µs vs 35 µs at 100 Hz). With nothing else running, the CPU sits in a deep
  idle state between messages and waking it up dominates the latency (stage 3 in the README). Busy-loop threads keep
  the cores awake.
- **Under load, the difference between UDS and the middlewares is much larger** (3x to 30x at 1 kHz), since the DDS /
  Zenoh receive path needs several threads to be scheduled in turn.
- **At 10 kHz under load the machine is saturated** (4 busy-loop threads on 4 hardware threads), so the UDS series are
  dominated by scheduling and are noisy; do not read differences between them there.
- **Fast DDS at 10 kHz with 64K, and at an unlimited rate**, queues messages (reliable + keep-all) and the latency
  columns then measure the time spent in the queue (up to seconds), not the transport speed. Under load, 2 of the 5
  runs of these conditions did not finish (the subscriber waited 10 s without receiving a message); they stayed
  incomplete when measured again, so they are left out of the medians and listed under "Incomplete runs".

## Latency (µs)

Median (p99) from just before `publish()` to the subscriber callback.

### Idle

| Rate | Transport | 64 | 256 | 1K | 4K | 16K | 64K |
|---|---|---|---|---|---|---|---|
| 100 Hz | UDS (variable-size) | 330 (372) | 334 (377) | 354 (431) | 388 (448) | 428 (518) | 572 (661) |
| 100 Hz | UDS (fixed-size) | 169 (193) | 171 (190) | 172 (198) | 183 (203) | 217 (251) | 293 (392) |
| 100 Hz | DDS (Fast DDS) | 645 (759) | 625 (738) | 663 (759) | 678 (779) | 688 (823) | 976 (1112) |
| 100 Hz | DDS (Cyclone DDS) | 611 (694) | 614 (695) | 576 (684) | 617 (714) | 773 (938) | 1060 (1375) |
| 100 Hz | Zenoh | 966 (1083) | 968 (1106) | 920 (1041) | 939 (1041) | 982 (1095) | 1340 (1489) |
| 1 kHz | UDS (variable-size) | 207 (340) | 127 (266) | 182 (277) | 155 (224) | 151 (265) | 267 (494) |
| 1 kHz | UDS (fixed-size) | 106 (150) | 75.5 (146) | 120 (179) | 117 (186) | 143 (219) | 128 (263) |
| 1 kHz | DDS (Fast DDS) | 243 (400) | 241 (361) | 236 (467) | 240 (373) | 263 (352) | 365 (446) |
| 1 kHz | DDS (Cyclone DDS) | 212 (445) | 234 (534) | 205 (408) | 215 (339) | 264 (523) | 500 (710) |
| 1 kHz | Zenoh | 365 (478) | 353 (499) | 358 (500) | 347 (475) | 385 (529) | 512 (644) |
| 10 kHz | UDS (variable-size) | 74.1 (168) | 74.8 (163) | 73.7 (165) | 80.8 (173) | 98.4 (196) | 108 (186) |
| 10 kHz | UDS (fixed-size) | 29.1 (73.8) | 29.7 (70.5) | 30.2 (65.4) | 34.5 (72.8) | 49.0 (134) | 84.8 (174) |
| 10 kHz | DDS (Fast DDS) | 118 (474) | 118 (361) | 112 (265) | 103 (313) | 94.4 (2278) | 581520 (854913) |
| 10 kHz | DDS (Cyclone DDS) | 99.3 (214) | 94.7 (206) | 97.0 (210) | 93.8 (211) | 89.3 (250) | 156 (270) |
| 10 kHz | Zenoh | 83.4 (125) | 85.6 (184) | 81.5 (142) | 83.8 (608) | 92.5 (137) | 144 (855) |
| Unlimited | UDS (variable-size) | 2315 (3914) | 1294 (2335) | 341 (1321) | 193 (436) | 41.1 (263) | 62.2 (171) |
| Unlimited | UDS (fixed-size) | 744 (1076) | 503 (885) | 292 (386) | 94.0 (131) | 44.5 (155) | 34.6 (87.9) |
| Unlimited | DDS (Fast DDS) | 2215634 (3017181) | 1784584 (1955586) | 4794702 (4953377) | 250270 (323059) | 287216 (553398) | 590137 (824326) |
| Unlimited | DDS (Cyclone DDS) | 55.8 (2606) | 52.4 (3085) | 51.9 (2443) | 52.0 (196) | 68.8 (168) | 156 (255) |
| Unlimited | Zenoh | 97025 (119672) | 95564 (108561) | 72177 (91509) | 34524 (51815) | 7497 (19613) | 3235 (4336) |

### 4 busy-loop threads

| Rate | Transport | 64 | 256 | 1K | 4K | 16K | 64K |
|---|---|---|---|---|---|---|---|
| 100 Hz | UDS (variable-size) | 34.8 (954) | 35.3 (598) | 35.1 (586) | 36.6 (674) | 47.1 (866) | 69.2 (1029) |
| 100 Hz | UDS (fixed-size) | 13.2 (96.6) | 13.5 (271) | 13.4 (306) | 15.3 (780) | 19.8 (179) | 33.0 (62.8) |
| 100 Hz | DDS (Fast DDS) | 141 (2997) | 162 (3104) | 380 (2614) | 558 (2501) | 514 (2750) | 240 (3731) |
| 100 Hz | DDS (Cyclone DDS) | 82.9 (2215) | 81.2 (2070) | 78.9 (2140) | 232 (1622) | 366 (2462) | 340 (3360) |
| 100 Hz | Zenoh | 657 (3316) | 859 (3686) | 684 (3345) | 240 (3245) | 297 (3382) | 891 (4003) |
| 1 kHz | UDS (variable-size) | 32.2 (436) | 31.9 (92.1) | 33.2 (585) | 35.1 (67.8) | 43.2 (115) | 64.5 (112) |
| 1 kHz | UDS (fixed-size) | 11.5 (36.5) | 11.6 (28.3) | 11.8 (37.2) | 13.6 (819) | 18.6 (46.2) | 31.9 (74.4) |
| 1 kHz | DDS (Fast DDS) | 555 (3293) | 510 (3525) | 251 (2737) | 1006 (3262) | 701 (4472) | 913 (4178) |
| 1 kHz | DDS (Cyclone DDS) | 667 (1250) | 86.5 (1540) | 141 (1413) | 573 (1913) | 588 (2632) | 1064 (3436) |
| 1 kHz | Zenoh | 977 (3230) | 838 (3753) | 921 (3746) | 693 (3643) | 961 (3748) | 1880 (5133) |
| 10 kHz | UDS (variable-size) | 301 (1307) | 353 (1885) | 374 (1408) | 380 (1768) | 1015 (2869) | 1037 (2927) |
| 10 kHz | UDS (fixed-size) | 298 (1812) | 10.9 (894) | 335 (1402) | 252 (1399) | 1013 (2941) | 1006 (2954) |
| 10 kHz | DDS (Fast DDS) | 2408 (5271) | 2018 (5289) | 2397 (5690) | 1337 (6010) | 4507 (47330) | 1777758 (2078931) |
| 10 kHz | DDS (Cyclone DDS) | 616 (3190) | 626 (2679) | 527 (2676) | 744 (3806) | 1226 (4328) | 1375 (4402) |
| 10 kHz | Zenoh | 1361 (4798) | 1395 (4334) | 1315 (4521) | 1638 (4203) | 1958 (16593) | 3086 (14795) |
| Unlimited | UDS (variable-size) | 4286 (12399) | 2962 (7607) | 1509 (3926) | 478 (3086) | 279 (2868) | 185 (1034) |
| Unlimited | UDS (fixed-size) | 1196 (6239) | 507 (4431) | 274 (3853) | 196 (2892) | 130 (2627) | 85.7 (235) |
| Unlimited | DDS (Fast DDS) | 175501 (303745) | 60894 (151734) | 235462 (372715) | 369018 (526475) | 790514 (1096712) | 1756033 (2188914) |
| Unlimited | DDS (Cyclone DDS) | 7071 (23956) | 4475 (23196) | 9189 (23124) | 4039 (10852) | 1176 (5151) | 1856 (3860) |
| Unlimited | Zenoh | 94876 (121026) | 76664 (155596) | 63717 (103256) | 40052 (55347) | 2996 (19472) | 3003 (13674) |

At an unlimited rate the latency measures how long messages wait in a queue rather than how fast the transport is.

### Incomplete runs

The subscriber gave up waiting before the end marker arrived (messages piled up in the middleware queue), so these runs are left out of the medians above.

| Transport | CPU | Rate | Payload | Complete runs |
|---|---|---|---|---|
| DDS (Fast DDS) | 4 busy-loop threads | Unlimited | 64K | 3 of 5 |
| DDS (Fast DDS) | 4 busy-loop threads | 10 kHz | 64K | 3 of 5 |

## Unlimited rate

When publishing as fast as possible, the latency columns of the CSV measure how long messages wait in a
queue rather than how fast the transport is. DDS (reliable + keep-all) and Zenoh queue everything, so their median
latency in this condition ranges from milliseconds to seconds. UDS with a send timeout makes the publisher wait once
the socket buffer is full, which keeps the latency low.

## Message loss

Share of messages that `publish()` reported as not sent. Series that never lost a message are listed below the table.

No message was lost: UDS (variable-size), UDS (fixed-size), DDS (Fast DDS), DDS (Cyclone DDS), Zenoh.

## Time to connect

From creating the publisher until a subscriber that is already running can be reached (median over all conditions, with the range in parentheses).

| CPU | UDS (variable-size) | UDS (fixed-size) | DDS (Fast DDS) | DDS (Cyclone DDS) | Zenoh |
|---|---|---|---|---|---|
| Idle | 0.4 ms (0.4 - 0.5) | 0.1 ms (0.1 - 0.1) | 100.0 ms (97.9 - 101.1) | 0.6 ms (0.6 - 0.7) | 0.6 ms (0.5 - 0.7) |
| 4 busy-loop threads | 0.9 ms (0.4 - 3.0) | 0.1 ms (0.1 - 2.6) | 1579.8 ms (410.3 - 1592.7) | 1.2 ms (0.7 - 3.6) | 0.6 ms (0.6 - 3.1) |

## Memory

Peak RSS of the publisher / subscriber process (MB): median, and maximum in parentheses.

| Transport | Publisher | Subscriber |
|---|---|---|
| UDS (variable-size) | 28 (28) | 28 (28) |
| UDS (fixed-size) | 27 (28) | 28 (28) |
| DDS (Fast DDS) | 28 (395) | 28 (298) |
| DDS (Cyclone DDS) | 16 (17) | 17 (20) |
| Zenoh | 23 (23) | 23 (50) |

## CPU time per message (µs, publisher + subscriber)

| CPU | Rate | Transport | 64 | 256 | 1K | 4K | 16K | 64K |
|---|---|---|---|---|---|---|---|---|
| Idle | 100 Hz | UDS (variable-size) | 518 | 519 | 538 | 529 | 647 | 722 |
| Idle | 100 Hz | UDS (fixed-size) | 348 | 344 | 353 | 360 | 401 | 479 |
| Idle | 100 Hz | DDS (Fast DDS) | 1080 | 1101 | 1066 | 1116 | 1283 | 2006 |
| Idle | 100 Hz | DDS (Cyclone DDS) | 919 | 892 | 919 | 1068 | 1597 | 2088 |
| Idle | 100 Hz | Zenoh | 1338 | 1298 | 1337 | 1341 | 1390 | 1977 |
| Idle | 1 kHz | UDS (variable-size) | 268 | 209 | 233 | 216 | 237 | 353 |
| Idle | 1 kHz | UDS (fixed-size) | 144 | 137 | 216 | 204 | 239 | 214 |
| Idle | 1 kHz | DDS (Fast DDS) | 410 | 424 | 413 | 434 | 476 | 548 |
| Idle | 1 kHz | DDS (Cyclone DDS) | 365 | 358 | 332 | 352 | 432 | 757 |
| Idle | 1 kHz | Zenoh | 512 | 514 | 547 | 505 | 537 | 745 |
| Idle | 10 kHz | UDS (variable-size) | 104 | 105 | 105 | 112 | 130 | 135 |
| Idle | 10 kHz | UDS (fixed-size) | 79.2 | 79.3 | 81.6 | 84.0 | 97.5 | 116 |
| Idle | 10 kHz | DDS (Fast DDS) | 169 | 168 | 161 | 156 | 151 | 338 |
| Idle | 10 kHz | DDS (Cyclone DDS) | 141 | 134 | 138 | 130 | 148 | 245 |
| Idle | 10 kHz | Zenoh | 137 | 141 | 136 | 142 | 150 | 246 |
| 4 busy-loop threads | 100 Hz | UDS (variable-size) | 79.4 | 80.7 | 82.1 | 81.9 | 91.9 | 114 |
| 4 busy-loop threads | 100 Hz | UDS (fixed-size) | 56.1 | 56.7 | 52.8 | 58.6 | 62.4 | 70.4 |
| 4 busy-loop threads | 100 Hz | DDS (Fast DDS) | 160 | 164 | 160 | 156 | 175 | 298 |
| 4 busy-loop threads | 100 Hz | DDS (Cyclone DDS) | 118 | 120 | 125 | 138 | 200 | 274 |
| 4 busy-loop threads | 100 Hz | Zenoh | 176 | 172 | 172 | 176 | 186 | 289 |
| 4 busy-loop threads | 1 kHz | UDS (variable-size) | 61.7 | 59.5 | 58.3 | 64.1 | 72.3 | 90.5 |
| 4 busy-loop threads | 1 kHz | UDS (fixed-size) | 39.2 | 37.4 | 37.4 | 40.8 | 47.6 | 59.2 |
| 4 busy-loop threads | 1 kHz | DDS (Fast DDS) | 126 | 125 | 122 | 134 | 148 | 196 |
| 4 busy-loop threads | 1 kHz | DDS (Cyclone DDS) | 104 | 107 | 108 | 115 | 141 | 250 |
| 4 busy-loop threads | 1 kHz | Zenoh | 150 | 148 | 149 | 152 | 161 | 258 |
| 4 busy-loop threads | 10 kHz | UDS (variable-size) | 33.8 | 32.5 | 32.0 | 38.4 | 33.6 | 77.2 |
| 4 busy-loop threads | 10 kHz | UDS (fixed-size) | 24.2 | 18.4 | 18.3 | 20.5 | 29.7 | 39.4 |
| 4 busy-loop threads | 10 kHz | DDS (Fast DDS) | 55.7 | 62.3 | 55.8 | 64.3 | 71.4 | 297 |
| 4 busy-loop threads | 10 kHz | DDS (Cyclone DDS) | 57.1 | 54.9 | 58.7 | 59.5 | 79.0 | 184 |
| 4 busy-loop threads | 10 kHz | Zenoh | 65.1 | 56.6 | 63.6 | 62.0 | 82.3 | 235 |

## Real-time scheduling

What a real-time priority does to the latency under load: 256 B at 1 kHz and 100 Hz, with 4 busy-loop threads
competing for the CPU. Each cell is "default scheduling → `SCHED_FIFO` priority 40", in µs, as the median of 5 runs;
"worst max" is the largest latency seen in any of the 5 runs. The two settings were measured alternately.

- **UDS** — the receive thread (`Scheduling::rt_priority`, see the [README](../README.md#real-time-scheduling)) and
  the publishing thread, nothing else in the process.
- **DDS / Zenoh** — the whole publisher and subscriber processes, started under `chrt -f 40`, so that every thread of
  the middleware has the priority. This is the most favorable setting for them; the Zenoh router keeps the default.

### 1 kHz

| Transport | Median | p99 | p99.9 | Max | Worst max of 5 runs | CPU time per message |
|---|---|---|---|---|---|---|
| UDS (fixed-size) | 12 → 11 | 35 → 14 | 717 → 18 | 2866 → 27 | 2929 → 33 | 41 → 32 |
| UDS (variable-size) | 33 → 27 | 71 → 35 | 728 → 45 | 2487 → 57 | 3152 → 66 | 61 → 51 |
| DDS (Cyclone DDS) | 90 → 50 | 1719 → 57 | 3005 → 71 | 3128 → 95 | 4072 → 102 | 105 → 92 |
| DDS (Fast DDS) | 1056 → 64 | 2882 → 73 | 4356 → 88 | 5355 → 115 | 7015 → 166 | 126 → 133 |
| Zenoh | 1362 → 91 | 3009 → 100 | 4803 → 122 | 5124 → 145 | 6840 → 205 | 153 → 158 |

### 100 Hz

| Transport | Median | p99 | p99.9 | Max | Worst max of 5 runs | CPU time per message |
|---|---|---|---|---|---|---|
| UDS (fixed-size) | 13 → 12 | 38 → 25 | 1009 → 35 | 1376 → 36 | 2932 → 47 | 59 → 43 |
| UDS (variable-size) | 35 → 34 | 507 → 70 | 1015 → 91 | 1017 → 95 | 3127 → 109 | 78 → 73 |
| DDS (Cyclone DDS) | 76 → 53 | 2015 → 102 | 2886 → 141 | 2973 → 147 | 3217 → 176 | 118 → 102 |
| DDS (Fast DDS) | 219 → 67 | 2394 → 102 | 3326 → 178 | 4064 → 184 | 5155 → 222 | 163 → 144 |
| Zenoh | 664 → 95 | 3612 → 154 | 4615 → 213 | 4615 → 229 | 15632 → 265 | 176 → 167 |

- **The tail is a matter of scheduling, for every transport.** With the default scheduling, a thread on the receive
  path waits behind the busy-loop threads every so often, and the largest latency of a run is 1 - 5 ms whatever the
  transport. With a real-time priority it stays below 300 µs for all of them, well within a 1 kHz control period.
- **UDS stays ahead once everything is real-time, by roughly 2x (variable-size) to 5x (fixed-size).** At 1 kHz the largest latency is 27 µs (fixed-size) and
  57 µs (variable-size), against 95 - 145 µs; the median is 11 µs and 27 µs against 50 - 91 µs, and the CPU time per
  message 32 µs and 51 µs against 92 - 158 µs.
- **UDS needs the priority on one thread.** The DDS numbers come from making whole processes real-time, which is
  not an option for a process that also does heavy work; prioritizing only the threads on the receive path takes
  middleware-specific configuration.
- **Without a real-time priority**, which is the situation when it cannot be configured, the difference is in p99:
  35 - 71 µs for UDS against 1.7 - 2.9 ms at 1 kHz.
- At 100 Hz a run has 1000 messages, so p99.9 and the maximum are single samples.

Raw data: [`latency_rt.csv`](benchmark/latency_rt.csv) (`rt_priority` 0 and 40, 50 runs each). To reproduce it,
pass `RT_PRIORITY=40` to `run_latency_matrix.sh`; the process needs an `rtprio` limit, and Zenoh a running router.

## CSV columns

| Column | Meaning |
|---|---|
| `transport`, `rmw` | `uds`, `uds_reliable`, `uds_reliable_fixed` or `dds`; the RMW implementation for `dds` (`rmw_fastrtps_cpp`, `rmw_cyclonedds_cpp`, `rmw_zenoh_cpp`) |
| `payload_bytes`, `rate_hz`, `load_threads` | Condition. `rate_hz` 0 means unlimited |
| `count`, `publish_failed`, `received` | Messages published, reported as not sent, and received |
| `min_us` ... `max_us` | Latency from just before `publish()` to the subscriber callback |
| `pub_cpu_percent`, `sub_cpu_percent` | CPU usage of each process (all threads), 100 = one core |
| `pub_cpu_us_per_msg`, `sub_cpu_us_per_msg` | CPU time of each process per message |
| `connect_ms` | From creating the publisher until the subscriber can be reached |
| `pub_max_rss_kb`, `sub_max_rss_kb` | Peak resident set size of each process |
| `throughput_hz` | Messages received per second |
| `rt_priority` | `SCHED_FIFO` priority, 0 if not used (newer files only). `uds`: the receive and publishing threads; `dds`: the whole processes |

## Running it yourself

The measurement tools live in a separate package, `ros2_uds_ipc_benchmark`, so that nothing from them
is built into `ros2_uds_ipc`. It measures cross-process latency of UDS and of plain DDS pub / sub
under the same conditions, together with the CPU usage and peak memory of the publisher and subscriber
processes, the number of failed publishes and the time to connect, and writes the result as CSV. It can also run under CPU load (`load_threads`).

```bash
colcon build --packages-up-to ros2_uds_ipc_benchmark
source install/setup.bash

# A single condition (transport: uds, uds_reliable, uds_reliable_fixed or dds)
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
