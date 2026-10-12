# Benchmark 詳細

[トップの README](../README_ja.md#benchmark) に載せていない結果です。条件は同じで、publisher ノードと subscriber ノードを
別プロセスで起動し、ペイロード 6 種 x 送信レート 4 種 (100 Hz / 1 kHz / 10 kHz / 無制限) を、無負荷と、busy loop
スレッド 20 本と CPU を奪い合う状態の両方で測っています。各条件 1 回の実行で、環境は Intel Core i7-13700H (20 スレッド)、
WSL2 (kernel 6.18)、ROS 2 Jazzy、`rmw_fastrtps_cpp` です。CPU 固定やリアルタイムスケジューリングは行っていません。

- **UDS** — `publish(msg)`。ノンブロッキング
- **UDS (send timeout)** — `publish(msg, 1s)`。捨てずに subscriber を待つ
- **DDS (Fast DDS)** — 通常の `rclcpp` publisher / subscription。reliable + keep-all、トランスポートはデフォルト

生データ:

- [`latency_wsl2_fastdds.csv`](benchmark/latency_wsl2_fastdds.csv) — 全 144 条件、各 1 回。このページの表はこちらから作っています。
  1 回の実行なので、数十マイクロ秒の差は実行ごとのばらつきの範囲です。
- [`latency_wsl2_fastdds_charts.csv`](benchmark/latency_wsl2_fastdds_charts.csv) — トップの README のグラフに使った条件
  (1 kHz と、10 kHz の無負荷) を 5 回ずつ。グラフは中央値です。

## メッセージの取りこぼし

ノンブロッキングの `publish(msg)` が送れなかった (`false` を返した) メッセージの割合を、ペイロードサイズ別に示します。
send timeout を指定した場合と DDS では、全条件で取りこぼしはありませんでした。

| CPU | レート | 64 | 256 | 1K | 4K | 16K | 64K |
|---|---|---|---|---|---|---|---|
| 無負荷 | 100 Hz | 0% | 0% | 0% | 0% | 0% | 0% |
| 無負荷 | 1 kHz | 0% | 0% | 0% | 0% | 0% | 0.2% |
| 無負荷 | 10 kHz | 0% | 0% | 0% | 0.5% | 0.7% | 1.3% |
| 無負荷 | 無制限 | 23.6% | 82.0% | 70.6% | 83.4% | 85.8% | 74.7% |
| 負荷あり | 100 Hz | 0% | 0% | 0% | 0% | 0% | 0% |
| 負荷あり | 1 kHz | 0% | 0% | 0% | 0.8% | 0.1% | 2.0% |
| 負荷あり | 10 kHz | 0% | 0% | 0% | 16.7% | 33.5% | 18.9% |
| 負荷あり | 無制限 | 53.4% | 85.0% | 84.8% | 91.3% | 91.4% | 77.4% |

## 接続確立までの時間

publisher を生成してから、すでに起動している subscriber に届く状態になるまでの時間です
(24 条件の中央値。カッコ内は範囲)。

| CPU | UDS | DDS (Fast DDS) |
|---|---|---|
| 無負荷 | 0.2 ms (0.2 - 0.4) | 438 ms (103 - 446) |
| 負荷あり | 1.0 ms (0.3 - 8.5) | 1566 ms (59 - 1650) |

## メモリ

UDS の最大 RSS は全条件で 1 プロセスあたり 27 - 28 MB でした。DDS も subscriber が追いつけるレートでは同じですが、
メッセージが滞留すると最大 394 MB まで増えました (64K ペイロード、レート無制限)。どちらの値にも、計測用プロセスが
パラメータを受け取るために持っている `rclcpp` ノードのぶんが含まれます。

## レート無制限の条件

可能な限り速く publish した場合、CSV のレイテンシ列はトランスポートの速さではなく、キューでの待ち時間を表します。
DDS (reliable + keep-all) は全メッセージを溜めるので、この条件でのレイテンシ中央値は 169 ms - 1.6 s になります。
send timeout を指定した UDS は、ソケットバッファが埋まると publisher 側が待つため、中央値は 55 µs - 3.3 ms に収まります。
ノンブロッキングの UDS は溜めずに捨てます (前述)。

## CSV の列

| 列 | 意味 |
|---|---|
| `transport`, `rmw` | `uds` / `uds_reliable` / `dds`。`dds` の場合は RMW 実装 |
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

# 単一条件
ros2 launch ros2_uds_ipc_benchmark latency.launch.py transport:=uds payload_bytes:=1024 count:=10000 rate_hz:=1000.0

# 同条件で、busy loop スレッド 8 本と CPU を奪い合う状態
ros2 launch ros2_uds_ipc_benchmark latency.launch.py transport:=uds payload_bytes:=1024 load_threads:=8

# CPU 負荷 x transport x 送信レート x ペイロードサイズ (LOADS / RATES / SIZES / COUNT / RMWS で上書き可能) を 5 回ずつ
# dds はインストール済みの RMW 実装ごとに計測
REPEAT=5 ros2 run ros2_uds_ipc_benchmark run_latency_matrix.sh result.csv

# CSV をグラフ化。繰り返し実行した条件は中央値を描く (matplotlib が必要)
ros2 run ros2_uds_ipc_benchmark plot_latency.py result.csv charts/
```
