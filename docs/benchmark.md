# Benchmark

Latency, CPU usage, memory and time to connect of `ros2_uds_ipc` compared with DDS (Fast DDS / Cyclone DDS) and
Zenoh, and how they were measured. Back to the [README](../README.md).

## Summary

Median latency in µs (p99 in parentheses) from `publish()` to the subscriber callback, for a 1 KB payload at 1 kHz between two processes.

| Transport | Idle | 4 busy-loop threads |
|---|---|---|
| UDS (fixed-size) | 135 (178) | 12.0 (22.9) |
| UDS (variable-size) | 128 (203) | 14.7 (39.6) |
| DDS (Fast DDS) | 236 (333) | 712 (3128) |
| DDS (Cyclone DDS) | 213 (390) | 152 (1944) |
| Zenoh | 344 (496) | 967 (3221) |

The idle numbers are dominated by the CPU waking up from a deep idle state; under load the middleware layers of
DDS and Zenoh are what remains. With the idle states limited to C1 (256 B at 1 kHz), the idle latency of UDS drops
to that under load, 15 µs (fixed-size) and 34 µs (variable-size), while DDS and Zenoh stay at 144 - 272 µs
([details](#cpu-idle-states)).

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
[constructor argument](../README.md#real-time-scheduling) ([details](#real-time-scheduling)).

### Latency

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="benchmark/latency_1khz-dark.svg">
  <img src="benchmark/latency_1khz.svg" alt="Median and p99 latency at 1 kHz by payload size, idle and under CPU load">
</picture>

### CPU usage

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="benchmark/cpu_per_message-dark.svg">
  <img src="benchmark/cpu_per_message.svg" alt="CPU time per message at 10 kHz, publisher and subscriber combined">
</picture>

## Measurement setup

Measured with the `ros2_uds_ipc_benchmark` package: a publisher node and a subscriber node in separate processes,
6 payload sizes x 4 publish rates (100 Hz, 1 kHz, 10 kHz, unlimited), idle and with 4 busy-loop threads competing
for the CPU (one per hardware thread). Each condition was run 5 times and the tables and charts show the median.
The machine was an Intel Core i5-3427U (4 threads), Linux 7.0.0-31-generic, ROS 2 Jazzy, without CPU pinning or
real-time scheduling (except in [Real-time scheduling](#real-time-scheduling)).

| Series | Message | Serialization | When the receive queue is full |
|---|---|---|---|
| **UDS (variable-size)** | `std_msgs/UInt8MultiArray` | CDR | `publish(msg, 1s)` waits for the subscriber |
| **UDS (fixed-size)** | `uint8[N]` (`msg/Fixed<N>.msg`) | none, the message is sent as-is | `publish(msg, 1s)` waits for the subscriber |
| **DDS (Fast DDS)** | `std_msgs/UInt8MultiArray` | CDR | reliable + keep-all (`rmw_fastrtps_cpp`) |
| **DDS (Cyclone DDS)** | `std_msgs/UInt8MultiArray` | CDR | reliable + keep-all (`rmw_cyclonedds_cpp`) |
| **Zenoh** | `std_msgs/UInt8MultiArray` | CDR | reliable + keep-all (`rmw_zenoh_cpp`, local `rmw_zenohd` router) |

Both UDS series use a send timeout, so that a message is delivered or reported as failed, never dropped silently.
The non-blocking `publish(msg)` (`transport:=uds`) is not part of the default matrix; it behaves the same below
the rates where the queue fills up. DDS and Zenoh use a plain `rclcpp` publisher / subscription with the default transport.

Raw data: [`latency.csv`](benchmark/latency.csv) — all 1200 measurements (240 conditions x 5 runs).

## Notes

- **Skipping serialization helps a little.** Fixed-size messages are still the faster ones, by 10 - 30%: 170 - 178 µs
  vs 184 - 234 µs at 100 Hz idle, 30 µs vs 36 µs at 10 kHz idle (1K), and 12 µs vs 15 µs at 1 kHz under load (64 B -
  1K). The gap grows with the payload (64K: 295 vs 422 µs at 100 Hz idle), and it applies only to messages made
  only of primitives and fixed-size arrays.
- **Idle is slower than loaded for UDS** (184 µs vs 16 µs at 100 Hz). With nothing else running, the CPU sits in a deep
  idle state between messages and waking it up dominates the latency. Busy-loop threads keep
  the cores awake. See [CPU idle states](#cpu-idle-states) for what limiting them does.
- **Under load, the difference between UDS and the middlewares is much larger** (10x to 80x at 1 kHz, 1K), since the DDS /
  Zenoh receive path needs several threads to be scheduled in turn.
- **At 10 kHz under load the machine is saturated** (4 busy-loop threads on 4 hardware threads), so the UDS series are
  dominated by scheduling and are noisy; do not read differences between them there.
- **Fast DDS at 10 kHz with 64K, and at an unlimited rate**, queues messages (reliable + keep-all) and the latency
  columns then measure the time spent in the queue (up to seconds), not the transport speed. Under load, 3 or 4 of the 5
  runs of these conditions did not finish (the subscriber waited 10 s without receiving a message); they stayed
  incomplete when measured again, so they are left out of the medians and listed under "Incomplete runs".

## Latency (µs)

Median (p99) from just before `publish()` to the subscriber callback.

### Idle

| Rate | Transport | 64 | 256 | 1K | 4K | 16K | 64K |
|---|---|---|---|---|---|---|---|
| 100 Hz | UDS (variable-size) | 184 (213) | 197 (266) | 234 (282) | 212 (257) | 259 (303) | 422 (504) |
| 100 Hz | UDS (fixed-size) | 170 (235) | 173 (188) | 178 (199) | 184 (199) | 216 (247) | 295 (375) |
| 100 Hz | DDS (Fast DDS) | 660 (766) | 636 (732) | 670 (761) | 652 (773) | 726 (834) | 977 (1116) |
| 100 Hz | DDS (Cyclone DDS) | 560 (619) | 607 (685) | 586 (668) | 625 (729) | 761 (901) | 1095 (1360) |
| 100 Hz | Zenoh | 966 (1088) | 935 (1037) | 932 (1018) | 951 (1042) | 978 (1064) | 1303 (1450) |
| 1 kHz | UDS (variable-size) | 122 (172) | 112 (177) | 128 (203) | 103 (134) | 128 (162) | 195 (307) |
| 1 kHz | UDS (fixed-size) | 110 (171) | 87.6 (151) | 135 (178) | 115 (151) | 143 (224) | 197 (328) |
| 1 kHz | DDS (Fast DDS) | 239 (469) | 239 (520) | 236 (333) | 269 (595) | 271 (391) | 370 (474) |
| 1 kHz | DDS (Cyclone DDS) | 252 (494) | 205 (286) | 213 (390) | 231 (428) | 307 (545) | 450 (685) |
| 1 kHz | Zenoh | 365 (518) | 390 (509) | 344 (496) | 363 (500) | 379 (510) | 499 (655) |
| 10 kHz | UDS (variable-size) | 34.1 (84.3) | 40.8 (71.5) | 36.1 (74.0) | 42.2 (87.5) | 63.2 (144) | 94.9 (177) |
| 10 kHz | UDS (fixed-size) | 33.5 (70.7) | 28.9 (65.8) | 30.2 (76.8) | 34.6 (82.8) | 53.1 (109) | 82.2 (166) |
| 10 kHz | DDS (Fast DDS) | 117 (271) | 118 (313) | 113 (264) | 109 (266) | 101 (12579) | 579595 (854818) |
| 10 kHz | DDS (Cyclone DDS) | 101 (221) | 99.9 (213) | 97.4 (213) | 94.6 (204) | 91.4 (255) | 155 (254) |
| 10 kHz | Zenoh | 84.4 (129) | 82.4 (171) | 82.2 (143) | 83.6 (214) | 90.9 (122) | 142 (306) |
| Unlimited | UDS (variable-size) | 692 (1005) | 449 (780) | 277 (545) | 52.9 (153) | 58.3 (153) | 56.8 (118) |
| Unlimited | UDS (fixed-size) | 652 (1093) | 488 (663) | 286 (379) | 70.6 (112) | 47.6 (133) | 35.2 (90.3) |
| Unlimited | DDS (Fast DDS) | 4465683 (4649464) | 3008678 (3248924) | 4506229 (4695043) | 232801 (389631) | 282415 (532888) | 581707 (823755) |
| Unlimited | DDS (Cyclone DDS) | 48.8 (2257) | 57.5 (3378) | 52.8 (1952) | 53.6 (133) | 68.4 (162) | 156 (254) |
| Unlimited | Zenoh | 85555 (97482) | 87833 (98344) | 73752 (81890) | 33262 (59237) | 4297 (5686) | 3843 (4795) |

### 4 busy-loop threads

| Rate | Transport | 64 | 256 | 1K | 4K | 16K | 64K |
|---|---|---|---|---|---|---|---|
| 100 Hz | UDS (variable-size) | 16.0 (772) | 15.3 (30.7) | 16.4 (915) | 17.6 (123) | 26.5 (937) | 47.6 (901) |
| 100 Hz | UDS (fixed-size) | 12.9 (18.8) | 12.7 (19.1) | 13.0 (279) | 14.4 (21.7) | 20.1 (30.6) | 34.8 (1010) |
| 100 Hz | DDS (Fast DDS) | 157 (2436) | 429 (2724) | 819 (2622) | 485 (2022) | 726 (2696) | 597 (3642) |
| 100 Hz | DDS (Cyclone DDS) | 79.0 (1906) | 81.1 (1783) | 542 (1432) | 87.8 (2447) | 149 (2929) | 788 (3184) |
| 100 Hz | Zenoh | 543 (3265) | 817 (3285) | 884 (3021) | 849 (3306) | 951 (3918) | 954 (3786) |
| 1 kHz | UDS (variable-size) | 13.7 (38.8) | 14.2 (41.9) | 14.7 (39.6) | 16.5 (42.5) | 24.8 (67.0) | 46.6 (160) |
| 1 kHz | UDS (fixed-size) | 11.6 (24.4) | 11.7 (35.9) | 12.0 (22.9) | 13.5 (38.2) | 18.5 (51.3) | 32.4 (66.5) |
| 1 kHz | DDS (Fast DDS) | 659 (3989) | 836 (4017) | 712 (3128) | 754 (4045) | 1023 (3905) | 712 (3959) |
| 1 kHz | DDS (Cyclone DDS) | 163 (1119) | 852 (1107) | 152 (1944) | 668 (2048) | 684 (3226) | 823 (3185) |
| 1 kHz | Zenoh | 544 (3148) | 989 (3842) | 967 (3221) | 1036 (3296) | 1092 (3747) | 1997 (4996) |
| 10 kHz | UDS (variable-size) | 213 (1039) | 387 (1616) | 157 (1662) | 301 (1541) | 1010 (2922) | 162 (2939) |
| 10 kHz | UDS (fixed-size) | 118 (1074) | 76.2 (1030) | 306 (1868) | 56.7 (1009) | 1002 (2939) | 1004 (2951) |
| 10 kHz | DDS (Fast DDS) | 2326 (5465) | 644 (3912) | 2639 (5997) | 1253 (6226) | 10146 (112386) | 1666790 (2129516) |
| 10 kHz | DDS (Cyclone DDS) | 503 (2826) | 598 (3800) | 669 (3617) | 852 (7072) | 1677 (4157) | 1189 (4089) |
| 10 kHz | Zenoh | 2041 (4327) | 1570 (4211) | 793 (4588) | 1700 (4096) | 2029 (11409) | 3010 (15833) |
| Unlimited | UDS (variable-size) | 1138 (5856) | 871 (5186) | 336 (3935) | 210 (2258) | 106 (2501) | 138 (2399) |
| Unlimited | UDS (fixed-size) | 760 (4739) | 497 (4012) | 522 (3913) | 96.2 (2835) | 149 (2631) | 99.2 (2745) |
| Unlimited | DDS (Fast DDS) | 200925 (351589) | 205850 (522151) | 89641 (207256) | 459736 (820451) | 782750 (1140214) | 1551693 (2228613) |
| Unlimited | DDS (Cyclone DDS) | 15815 (39219) | 19654 (45942) | 7565 (16841) | 1820 (10147) | 1233 (5170) | 1307 (4053) |
| Unlimited | Zenoh | 90671 (134743) | 75264 (145078) | 73871 (106773) | 32429 (51325) | 3086 (20445) | 3000 (13132) |

At an unlimited rate the latency measures how long messages wait in a queue rather than how fast the transport is.

### Incomplete runs

The subscriber gave up waiting before the end marker arrived (messages piled up in the middleware queue), so these runs are left out of the medians above.

| Transport | CPU | Rate | Payload | Complete runs |
|---|---|---|---|---|
| DDS (Fast DDS) | 4 busy-loop threads | Unlimited | 64K | 1 of 5 |
| DDS (Fast DDS) | 4 busy-loop threads | 10 kHz | 64K | 2 of 5 |

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
| Idle | 0.3 ms (0.3 - 0.4) | 0.2 ms (0.2 - 0.2) | 100.0 ms (98.1 - 101.2) | 0.6 ms (0.6 - 0.7) | 0.5 ms (0.5 - 0.6) |
| 4 busy-loop threads | 0.5 ms (0.4 - 3.6) | 0.3 ms (0.3 - 2.6) | 1590.6 ms (828.0 - 1597.9) | 1.7 ms (0.7 - 3.5) | 0.6 ms (0.5 - 4.6) |

## Memory

Peak RSS of the publisher / subscriber process (MB): median, and maximum in parentheses.

| Transport | Publisher | Subscriber |
|---|---|---|
| UDS (variable-size) | 28 (28) | 28 (28) |
| UDS (fixed-size) | 27 (28) | 28 (28) |
| DDS (Fast DDS) | 28 (395) | 29 (299) |
| DDS (Cyclone DDS) | 16 (17) | 17 (20) |
| Zenoh | 23 (23) | 23 (51) |

## CPU time per message (µs, publisher + subscriber)

| CPU | Rate | Transport | 64 | 256 | 1K | 4K | 16K | 64K |
|---|---|---|---|---|---|---|---|---|
| Idle | 100 Hz | UDS (variable-size) | 371 | 402 | 385 | 393 | 446 | 566 |
| Idle | 100 Hz | UDS (fixed-size) | 356 | 352 | 356 | 363 | 411 | 473 |
| Idle | 100 Hz | DDS (Fast DDS) | 1122 | 1067 | 1111 | 1166 | 1225 | 2064 |
| Idle | 100 Hz | DDS (Cyclone DDS) | 844 | 918 | 904 | 1132 | 1604 | 2141 |
| Idle | 100 Hz | Zenoh | 1337 | 1340 | 1351 | 1294 | 1393 | 1968 |
| Idle | 1 kHz | UDS (variable-size) | 196 | 189 | 232 | 160 | 186 | 263 |
| Idle | 1 kHz | UDS (fixed-size) | 173 | 183 | 224 | 178 | 245 | 276 |
| Idle | 1 kHz | DDS (Fast DDS) | 427 | 454 | 412 | 495 | 473 | 591 |
| Idle | 1 kHz | DDS (Cyclone DDS) | 400 | 309 | 353 | 369 | 454 | 713 |
| Idle | 1 kHz | Zenoh | 512 | 517 | 515 | 528 | 541 | 719 |
| Idle | 10 kHz | UDS (variable-size) | 82.1 | 86.6 | 87.6 | 86.0 | 98.7 | 126 |
| Idle | 10 kHz | UDS (fixed-size) | 82.5 | 77.1 | 77.4 | 82.2 | 94.6 | 116 |
| Idle | 10 kHz | DDS (Fast DDS) | 169 | 170 | 166 | 160 | 154 | 324 |
| Idle | 10 kHz | DDS (Cyclone DDS) | 144 | 141 | 136 | 130 | 148 | 239 |
| Idle | 10 kHz | Zenoh | 138 | 137 | 139 | 142 | 149 | 243 |
| 4 busy-loop threads | 100 Hz | UDS (variable-size) | 56.9 | 50.8 | 59.2 | 60.1 | 68.2 | 86.8 |
| 4 busy-loop threads | 100 Hz | UDS (fixed-size) | 49.1 | 56.0 | 49.7 | 57.9 | 64.1 | 76.0 |
| 4 busy-loop threads | 100 Hz | DDS (Fast DDS) | 168 | 152 | 138 | 147 | 159 | 291 |
| 4 busy-loop threads | 100 Hz | DDS (Cyclone DDS) | 113 | 113 | 117 | 138 | 197 | 272 |
| 4 busy-loop threads | 100 Hz | Zenoh | 173 | 170 | 167 | 174 | 181 | 283 |
| 4 busy-loop threads | 1 kHz | UDS (variable-size) | 38.4 | 41.8 | 39.5 | 44.2 | 50.6 | 71.2 |
| 4 busy-loop threads | 1 kHz | UDS (fixed-size) | 36.7 | 33.0 | 36.7 | 39.5 | 44.0 | 61.9 |
| 4 busy-loop threads | 1 kHz | DDS (Fast DDS) | 127 | 121 | 126 | 129 | 141 | 198 |
| 4 busy-loop threads | 1 kHz | DDS (Cyclone DDS) | 104 | 99.4 | 105 | 110 | 140 | 241 |
| 4 busy-loop threads | 1 kHz | Zenoh | 150 | 151 | 148 | 150 | 158 | 252 |
| 4 busy-loop threads | 10 kHz | UDS (variable-size) | 26.2 | 26.1 | 27.8 | 28.4 | 35.9 | 52.8 |
| 4 busy-loop threads | 10 kHz | UDS (fixed-size) | 19.5 | 22.6 | 23.8 | 18.0 | 28.0 | 43.1 |
| 4 busy-loop threads | 10 kHz | DDS (Fast DDS) | 58.6 | 55.1 | 57.1 | 60.9 | 77.0 | 276 |
| 4 busy-loop threads | 10 kHz | DDS (Cyclone DDS) | 51.4 | 54.7 | 51.9 | 60.1 | 79.8 | 184 |
| 4 busy-loop threads | 10 kHz | Zenoh | 50.9 | 48.8 | 76.0 | 54.5 | 83.9 | 249 |

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

## CPU idle states

Why the idle numbers are higher than the loaded ones, and what limiting the CPU idle states (C-states) does: idle,
256 B at 1 kHz and 100 Hz. Each cell is "C7 (default) → C1E → C1", the deepest idle state the CPU was allowed to
enter, in µs, as the median of 3 runs. The three settings were measured alternately. The limit was set through
PM QoS: a process holds `/dev/cpu_dma_latency` open with the largest exit latency it accepts (10 µs for C1E, 1 µs
for C1 on this CPU).

| Setting | Share of the idle time spent in C1 | C1E | C3 | C7 |
|---|---|---|---|---|
| C7 (default) | 0.2% | 2.0% | 2.8% | 95.0% |
| C1E | 0.6% | 99.4% | 0% | 0% |
| C1 | 100% | 0% | 0% | 0% |

### 1 kHz

| Transport | Median | p99 | CPU time per message |
|---|---|---|---|
| UDS (fixed-size) | 134 → 34 → 15 | 172 → 58 → 51 | 207 → 88 → 54 |
| UDS (variable-size) | 184 → 83 → 34 | 300 → 118 → 115 | 254 → 150 → 83 |
| DDS (Cyclone DDS) | 205 → 158 → 144 | 274 → 231 → 199 | 294 → 248 → 182 |
| DDS (Fast DDS) | 242 → 191 → 185 | 453 → 247 → 230 | 410 → 326 → 253 |
| Zenoh | 358 → 286 → 272 | 467 → 357 → 321 | 505 → 425 → 355 |

### 100 Hz

| Transport | Median | p99 | CPU time per message |
|---|---|---|---|
| UDS (fixed-size) | 170 → 38 → 35 | 190 → 82 → 64 | 361 → 104 → 80 |
| UDS (variable-size) | 338 → 96 → 84 | 437 → 186 → 161 | 570 → 175 → 140 |
| DDS (Cyclone DDS) | 619 → 166 → 169 | 709 → 290 → 243 | 852 → 282 → 269 |
| DDS (Fast DDS) | 642 → 230 → 198 | 758 → 328 → 280 | 1104 → 412 → 344 |
| Zenoh | 979 → 299 → 304 | 1114 → 415 → 407 | 1323 → 472 → 440 |

- **The idle latency of UDS is the CPU waking up.** By default an idle core spends 95% of its time in C7 and has to
  come out of it for every message. Limited to C1, UDS (fixed-size) at 1 kHz goes from 134 µs to 15 µs, about the same
  as under load (12 µs), where the busy-loop threads keep the cores awake.
- **Limiting to C1E already gives most of it**: 134 µs to 34 µs at 1 kHz, and at 100 Hz there is little left between
  C1E and C1.
- **DDS and Zenoh gain less**, 144 - 272 µs at 1 kHz with C1, so the difference between Cyclone DDS, the fastest of them, and
  UDS grows from 1.5x to about 10x (fixed-size) and from 1.1x to about 4x (variable-size). Why they do not come down further was not investigated; the CPU
  frequency, which is also lower when idle, was not controlled.
- Limiting the idle states raises the power drawn when idle, which was not measured. On a machine dedicated to
  control it is a common setting, through PM QoS as above or through kernel parameters.
- 3 runs per setting; the maxima vary too much between runs to be reported.

Raw data: [`latency_idle_states.csv`](benchmark/latency_idle_states.csv) (90 runs, with the setting in
`deepest_idle_state`).

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
