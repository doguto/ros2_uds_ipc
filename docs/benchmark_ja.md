# Benchmark 詳細

[トップの README](../README_ja.md#benchmark) に載せていない結果です。条件は同じで、publisher ノードと subscriber ノードを
別プロセスで起動し、ペイロード 6 種 x 送信レート 4 種 (100 Hz / 1 kHz / 10 kHz / 無制限) を、無負荷と、busy loop
スレッド 4 本 (ハードウェアスレッド数と同じ) と CPU を奪い合う状態の両方で測っています。各条件 5 回の実行の中央値を載せています。
環境は Intel Core i5-3427U (4 スレッド), Linux 7.0.0-31-generic、ROS 2 Jazzy で、CPU 固定やリアルタイムスケジューリングは行っていません。

| 系列 | メッセージ | シリアライズ | 受信キューが満杯のとき |
|---|---|---|---|
| **UDS (variable-size)** | `std_msgs/UInt8MultiArray` | CDR | `publish(msg, 1s)` が subscriber を待つ |
| **UDS (fixed-size)** | `uint8[N]` (`msg/Fixed<N>.msg`) | なし。メッセージをそのまま送る | `publish(msg, 1s)` が subscriber を待つ |
| **DDS (Fast DDS)** | `std_msgs/UInt8MultiArray` | CDR | reliable + keep-all (`rmw_fastrtps_cpp`) |
| **DDS (Cyclone DDS)** | `std_msgs/UInt8MultiArray` | CDR | reliable + keep-all (`rmw_cyclonedds_cpp`) |
| **Zenoh** | `std_msgs/UInt8MultiArray` | CDR | reliable + keep-all (`rmw_zenoh_cpp`、ローカルの `rmw_zenohd` ルーター経由) |

UDS はどちらも send timeout 付きで、メッセージは届くか、失敗として報告されるかのどちらかです。黙って捨てることはありません。
ノンブロッキングの `publish(msg)` (`transport:=uds`) は既定の計測には含めていません。キューが満杯にならないレートでは同じ性能です。

生データ: [`latency.csv`](benchmark/latency.csv) — 全 1200 件の計測 (240 条件 x 5 回)。このページの表と README のグラフは、
5 回の中央値です。

## 補足

- **シリアライズを省くと速くなります。** 固定長メッセージは可変長に比べて 2〜3 倍速く、100 Hz の無負荷で 170 µs 対 330 µs、
  10 kHz の無負荷で 30 µs 対 75 µs、負荷ありの 100 Hz〜1 kHz で 12 µs 対 33 µs です (64 B〜1K)。
  ペイロードが大きくなると差の比率は縮みます (64K、100 Hz 無負荷で 290 µs 対 570 µs)。対象は、プリミティブと
  固定長配列だけで構成されたメッセージです。
- **UDS は無負荷のほうが遅くなります** (100 Hz で 330 µs 対 35 µs)。ほかに何も動いていないと、メッセージの合間に CPU が
  深いアイドル状態に入り、その復帰がレイテンシの大半を占めます (README のステージ 3)。busy loop がコアを起こし続けるため、
  負荷ありのほうが速くなります。
- **負荷時は、UDS とミドルウェアの差が大きく開きます** (1 kHz で 3〜30 倍)。DDS / Zenoh は受信経路で複数スレッドが順に
  スケジュールされる必要があるためです。
- **負荷ありの 10 kHz はマシンが飽和しています** (4 スレッドに busy loop 4 本)。UDS はスケジューリングに支配されて
  ばらつくため、系列間の差は読み取れません。
- **Fast DDS の 10 kHz・64K と無制限レート**では、メッセージがキューに溜まる (reliable + keep-all) ため、レイテンシ列は
  トランスポートの速さではなくキューでの待ち時間 (最大で秒単位) を表します。

## レイテンシ (µs)

`publish()` の直前から subscriber のコールバックまでの、中央値 (p99)。

### 無負荷

| レート | 方式 | 64 | 256 | 1K | 4K | 16K | 64K |
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
| 無制限 | UDS (variable-size) | 2315 (3914) | 1294 (2335) | 341 (1321) | 193 (436) | 41.1 (263) | 62.2 (171) |
| 無制限 | UDS (fixed-size) | 744 (1076) | 503 (885) | 292 (386) | 94.0 (131) | 44.5 (155) | 34.6 (87.9) |
| 無制限 | DDS (Fast DDS) | 2215634 (3017181) | 1784584 (1955586) | 4794702 (4953377) | 250270 (323059) | 287216 (553398) | 590137 (824326) |
| 無制限 | DDS (Cyclone DDS) | 55.8 (2606) | 52.4 (3085) | 51.9 (2443) | 52.0 (196) | 68.8 (168) | 156 (255) |
| 無制限 | Zenoh | 97025 (119672) | 95564 (108561) | 72177 (91509) | 34524 (51815) | 7603 (12396) | 3235 (4336) |

### busy loop 4 本

| レート | 方式 | 64 | 256 | 1K | 4K | 16K | 64K |
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
| 10 kHz | DDS (Fast DDS) | 2408 (5271) | 2018 (5289) | 2397 (5690) | 1337 (6010) | 4507 (47330) | 1681733 (2083718) |
| 10 kHz | DDS (Cyclone DDS) | 616 (3190) | 626 (2679) | 527 (2676) | 744 (3806) | 1226 (4328) | 1375 (4402) |
| 10 kHz | Zenoh | 1361 (4798) | 1395 (4334) | 1315 (4521) | 1638 (4203) | 1958 (16593) | 3086 (14795) |
| 無制限 | UDS (variable-size) | 4286 (12399) | 2962 (7607) | 1509 (3926) | 478 (3086) | 279 (2868) | 185 (1034) |
| 無制限 | UDS (fixed-size) | 1196 (6239) | 507 (4431) | 274 (3853) | 196 (2892) | 130 (2627) | 85.7 (235) |
| 無制限 | DDS (Fast DDS) | 175501 (303745) | 60894 (151734) | 235462 (372715) | 369018 (526475) | 790514 (1096712) | 1594747 (1850048) |
| 無制限 | DDS (Cyclone DDS) | 7071 (23956) | 4475 (23196) | 9189 (23124) | 4039 (10852) | 1176 (5151) | 1856 (3860) |
| 無制限 | Zenoh | 94876 (121026) | 76664 (155596) | 63717 (103256) | 40052 (55347) | 2996 (19472) | 3003 (13674) |

無制限レートでは、レイテンシはトランスポートの速さではなく、メッセージがキューで待った時間を表します。

## レート無制限の条件

できる限り速く送ると、CSV のレイテンシ列はトランスポートの速さではなく、メッセージがキューで待った時間を表します。
DDS (reliable + keep-all) と Zenoh は全部キューに溜めるため、この条件の中央値はミリ秒から秒単位になります。
send timeout 付きの UDS はソケットバッファが満杯になると publisher が待つため、レイテンシは低く保たれます。

## メッセージの取りこぼし

`publish()` が送れなかった (`false` を返した) メッセージの割合です。send timeout 付きの UDS と DDS / Zenoh は、
全条件で取りこぼしがありませんでした。ノンブロッキングの `publish(msg)` はキューが満杯になると `false` を返しますが、
既定の計測には含めていません。

## 接続確立までの時間

publisher を作ってから、すでに起動している subscriber に届くようになるまでの時間 (全条件の中央値、括弧内は範囲)。

| CPU | UDS (variable-size) | UDS (fixed-size) | DDS (Fast DDS) | DDS (Cyclone DDS) | Zenoh |
|---|---|---|---|---|---|
| 無負荷 | 0.4 ms (0.4 - 0.5) | 0.1 ms (0.1 - 0.1) | 100.0 ms (97.9 - 101.1) | 0.6 ms (0.6 - 0.7) | 0.6 ms (0.5 - 0.7) |
| busy loop 4 本 | 0.9 ms (0.4 - 3.0) | 0.1 ms (0.1 - 2.6) | 1579.5 ms (0.0 - 1592.7) | 1.2 ms (0.7 - 3.6) | 0.6 ms (0.6 - 3.1) |

## メモリ

publisher / subscriber プロセスのピーク RSS (MB)。中央値と、括弧内に最大値。

| 方式 | Publisher | Subscriber |
|---|---|---|
| UDS (variable-size) | 28 (28) | 28 (28) |
| UDS (fixed-size) | 27 (28) | 28 (28) |
| DDS (Fast DDS) | 28 (395) | 28 (298) |
| DDS (Cyclone DDS) | 16 (17) | 17 (20) |
| Zenoh | 23 (23) | 23 (50) |

## 1 メッセージあたりの CPU 時間 (µs、publisher + subscriber)

| CPU | レート | 方式 | 64 | 256 | 1K | 4K | 16K | 64K |
|---|---|---|---|---|---|---|---|---|
| 無負荷 | 100 Hz | UDS (variable-size) | 518 | 519 | 538 | 529 | 647 | 722 |
| 無負荷 | 100 Hz | UDS (fixed-size) | 348 | 344 | 353 | 360 | 401 | 479 |
| 無負荷 | 100 Hz | DDS (Fast DDS) | 1080 | 1101 | 1066 | 1116 | 1283 | 2006 |
| 無負荷 | 100 Hz | DDS (Cyclone DDS) | 919 | 892 | 919 | 1068 | 1597 | 2088 |
| 無負荷 | 100 Hz | Zenoh | 1338 | 1298 | 1337 | 1341 | 1390 | 1977 |
| 無負荷 | 1 kHz | UDS (variable-size) | 268 | 209 | 233 | 216 | 237 | 353 |
| 無負荷 | 1 kHz | UDS (fixed-size) | 144 | 137 | 216 | 204 | 239 | 214 |
| 無負荷 | 1 kHz | DDS (Fast DDS) | 410 | 424 | 413 | 434 | 476 | 548 |
| 無負荷 | 1 kHz | DDS (Cyclone DDS) | 365 | 358 | 332 | 352 | 432 | 757 |
| 無負荷 | 1 kHz | Zenoh | 512 | 514 | 547 | 505 | 537 | 745 |
| 無負荷 | 10 kHz | UDS (variable-size) | 104 | 105 | 105 | 112 | 130 | 135 |
| 無負荷 | 10 kHz | UDS (fixed-size) | 79.2 | 79.3 | 81.6 | 84.0 | 97.5 | 116 |
| 無負荷 | 10 kHz | DDS (Fast DDS) | 169 | 168 | 161 | 156 | 151 | 338 |
| 無負荷 | 10 kHz | DDS (Cyclone DDS) | 141 | 134 | 138 | 130 | 148 | 245 |
| 無負荷 | 10 kHz | Zenoh | 137 | 141 | 136 | 142 | 150 | 246 |
| busy loop 4 本 | 100 Hz | UDS (variable-size) | 79.4 | 80.7 | 82.1 | 81.9 | 91.9 | 114 |
| busy loop 4 本 | 100 Hz | UDS (fixed-size) | 56.1 | 56.7 | 52.8 | 58.6 | 62.4 | 70.4 |
| busy loop 4 本 | 100 Hz | DDS (Fast DDS) | 160 | 164 | 160 | 156 | 175 | 298 |
| busy loop 4 本 | 100 Hz | DDS (Cyclone DDS) | 118 | 120 | 125 | 138 | 200 | 274 |
| busy loop 4 本 | 100 Hz | Zenoh | 176 | 172 | 172 | 176 | 186 | 289 |
| busy loop 4 本 | 1 kHz | UDS (variable-size) | 61.7 | 59.5 | 58.3 | 64.1 | 72.3 | 90.5 |
| busy loop 4 本 | 1 kHz | UDS (fixed-size) | 39.2 | 37.4 | 37.4 | 40.8 | 47.6 | 59.2 |
| busy loop 4 本 | 1 kHz | DDS (Fast DDS) | 126 | 125 | 122 | 134 | 148 | 196 |
| busy loop 4 本 | 1 kHz | DDS (Cyclone DDS) | 104 | 107 | 108 | 115 | 141 | 250 |
| busy loop 4 本 | 1 kHz | Zenoh | 150 | 148 | 149 | 152 | 161 | 258 |
| busy loop 4 本 | 10 kHz | UDS (variable-size) | 33.8 | 32.5 | 32.0 | 38.4 | 33.6 | 77.2 |
| busy loop 4 本 | 10 kHz | UDS (fixed-size) | 24.2 | 18.4 | 18.3 | 20.5 | 29.7 | 39.4 |
| busy loop 4 本 | 10 kHz | DDS (Fast DDS) | 55.7 | 62.3 | 55.8 | 64.3 | 71.4 | 235 |
| busy loop 4 本 | 10 kHz | DDS (Cyclone DDS) | 57.1 | 54.9 | 58.7 | 59.5 | 79.0 | 184 |
| busy loop 4 本 | 10 kHz | Zenoh | 65.1 | 56.6 | 63.6 | 62.0 | 82.3 | 235 |

## CSV の列

| 列 | 意味 |
|---|---|
| `transport`, `rmw` | `uds` / `uds_reliable` / `uds_reliable_fixed` / `dds`。`dds` の場合は RMW 実装 (`rmw_fastrtps_cpp` / `rmw_cyclonedds_cpp` / `rmw_zenoh_cpp`) |
| `payload_bytes`, `rate_hz`, `load_threads` | 計測条件。`rate_hz` の 0 は無制限 |
| `count`, `publish_failed`, `received` | publish した数、送れなかったと報告された数、受信した数 |
| `min_us` ... `max_us` | `publish()` の直前から subscriber のコールバックまでのレイテンシ |
| `pub_cpu_percent`, `sub_cpu_percent` | 各プロセス (全スレッド) の CPU 使用率。100 = 1 コア |
| `pub_cpu_us_per_msg`, `sub_cpu_us_per_msg` | 各プロセスの 1 メッセージあたり CPU 時間 |
| `connect_ms` | publisher を生成してから subscriber に届く状態になるまで |
| `pub_max_rss_kb`, `sub_max_rss_kb` | 各プロセスの最大 RSS |
| `throughput_hz` | 毎秒の受信メッセージ数 |

## 自分で計測する

計測ツールは別パッケージ `ros2_uds_ipc_benchmark` に分離しており、`ros2_uds_ipc` のバイナリには含まれません。
UDS と通常の DDS pub / sub のプロセス間レイテンシと、publisher / subscriber 各プロセスの CPU 使用率・最大メモリ使用量、
publish 失敗数、接続確立までの時間を同一条件で計測し、結果を CSV で出力します。CPU に負荷をかけた状態 (`load_threads`) での計測もできます。

```bash
colcon build --packages-up-to ros2_uds_ipc_benchmark
source install/setup.bash

# 単一条件 (transport: uds / uds_reliable / uds_reliable_fixed / dds)
ros2 launch ros2_uds_ipc_benchmark latency.launch.py transport:=uds payload_bytes:=1024 count:=10000 rate_hz:=1000.0

# 同条件で、busy loop スレッド 8 本と CPU を奪い合う状態
ros2 launch ros2_uds_ipc_benchmark latency.launch.py transport:=uds payload_bytes:=1024 load_threads:=8

# CPU 負荷 x transport x 送信レート x ペイロードサイズ (LOADS / RATES / SIZES / COUNT / RMWS で上書き可能) を 5 回ずつ
# dds はインストール済みの RMW 実装ごとに計測
REPEAT=5 ros2 run ros2_uds_ipc_benchmark run_latency_matrix.sh result.csv

# CSV をグラフ化。繰り返し実行した条件は中央値を描く (matplotlib が必要)
ros2 run ros2_uds_ipc_benchmark plot_latency.py result.csv charts/

# CSV を、このページの全表を含む Markdown レポートにする
ros2 run ros2_uds_ipc_benchmark report_latency.py result.csv benchmark.md
```

ビルド、インストール済みの全 RMW (Zenoh ルーターの起動を含む) での計測、グラフとレポートの生成までを一度に行うには:

```bash
scripts/run_all_benchmarks.sh   # benchmark_result/<タイムスタンプ>/ に出力
```
