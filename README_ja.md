# ros2_uds_ipc

[English](README.md) | [日本語](README_ja.md)

`rclcpp` のメッセージ型を、DDS を経由せずに Unix ドメインソケット (`AF_UNIX` / `SOCK_SEQPACKET`) 上で
そのまま publish / subscribe するための、ROS 2 向けの薄い C++ ライブラリです。

## Concept

DDS には discovery、QoS マッチング、RTPS プロトコル層が付いてきます。同一マシン上の 2 プロセス間で
「メッセージが来たら即反応する」イベント駆動の経路 (例: `ros2_control` -> CAN ゲートウェイ) では、
これらは不要な遅延になります。

`ros2_uds_ipc` は ROS 2 のメッセージ型はそのままに、トランスポートだけを置き換えます。

- **任意の rosidl メッセージ型** — メッセージを CDR に一度シリアライズし、1 回の `send()` で送ります。
  可変長メッセージも扱えます。
- **到達保証・境界保持** — `SOCK_SEQPACKET` は接続指向でデータグラム境界を保持します
  (1 回の `send` = 1 回の `recv`)。手動のフレーミングは不要で、ベストエフォートではありません。
- **デフォルトでは送信側をブロックしない** — `publish()` はノンブロッキングで、未接続または subscriber の
  受信キューが満杯のときは `false` を返します。メッセージが黙って捨てられることはありません。
  ログ出力やフォールバックは呼び出し側が判断します。
- **バックプレッシャー (任意)** — `publish(msg, timeout)` は、遅い subscriber を最大 `timeout` まで待ち、
  メッセージを捨てずに送ります。
- **自動再接続** — どちら側も再起動できます。publisher は次の `publish()` で再接続します。
- **メッセージごとのヒープ確保なし** — ホットパスではバッファを使い回します。
- **DDS へのミラー (任意)** — UDS の経路は `ros2 topic echo` や rosbag から見えないため、同じ CDR
  バイト列を通常の DDS トピックにも流せます。デバッグ・記録用です。DDS への publish は専用スレッドで
  行い、ミラートピックに購読者がいるときだけ動きます。

接続は 1:1 です。1 つのソケットパスにつき、1 つの `UdsPublisher` と 1 つの `UdsSubscriber` が通信します。

## Requirements

- Linux
- ROS 2 (Jazzy で開発)
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

ソケットファイルは subscriber が所有します。親ディレクトリが無ければ作成し、破棄時にファイルを削除します。

```cpp
#include <ros2_uds_ipc/uds_subscriber.hpp>

uds_ipc::UdsSubscriber<sensor_msgs::msg::JointState> subscriber(
  "/run/my_robot/joint_command.sock",
  [](const sensor_msgs::msg::JointState & msg) {
    // rclcpp の executor スレッドではなく, subscriber 自身のスレッドから呼ばれる.
    // 共有状態に触る場合は呼び出し側で排他すること.
  });
```

ソケットを準備できなかった場合、コンストラクタは `std::runtime_error` を投げます。第 3 引数の
`max_message_size` (既定値 65536 バイト) は受信できるシリアライズ済みメッセージの最大サイズで、
これを超える分は切り捨てられます。

### Publisher

```cpp
#include <ros2_uds_ipc/uds_publisher.hpp>

uds_ipc::UdsPublisher<sensor_msgs::msg::JointState> publisher("/run/my_robot/joint_command.sock");

sensor_msgs::msg::JointState msg;
if (!publisher.publish(msg)) {
  // 未接続 (subscriber がまだ起動していない等) か, subscriber の受信キューが満杯.
}

// 遅い subscriber を最大 10 ms 待ち, メッセージを捨てずに送る.
publisher.publish(msg, std::chrono::milliseconds(10));
```

subscriber 待ちで溜められるメッセージ量は、ソケットの送信バッファで決まります。コンストラクタの第 3 引数で
バイト数を指定できます (`SO_SNDBUF`。0 はシステムのデフォルト)。カーネルが `net.core.wmem_max` に丸めるので、
それを超える値にするには sysctl 側も上げてください。

```cpp
uds_ipc::UdsPublisher<sensor_msgs::msg::JointState> publisher(
  "/run/my_robot/joint_command.sock", nullptr, 1024 * 1024);
```

### DDS へのミラー

`create_uds_mirror()` で作った publisher をどちらかの側に渡すと、メッセージが `ros2 topic echo` や
rosbag から見えるようになります。ミラーされたメッセージは最大 10 ms 遅れることがあり、DDS 側が
追いつかない場合は古いものから捨てられるので、観測用途にのみ使ってください。

```cpp
#include <ros2_uds_ipc/uds_mirror.hpp>

auto mirror = uds_ipc::create_uds_mirror<sensor_msgs::msg::JointState>(*node, "joint_command");
uds_ipc::UdsPublisher<sensor_msgs::msg::JointState> publisher("/run/my_robot/joint_command.sock", mirror);
```

## Benchmark

`ros2_uds_ipc_benchmark` (後述) で計測しました。publisher ノードと subscriber ノードを別プロセスで起動し、
ペイロード 6 種 x 送信レート 4 種を、無負荷と、busy loop スレッド 20 本と CPU を奪い合う状態の両方で測っています。

- **UDS** — `publish(msg)`。ノンブロッキング
- **UDS (send timeout)** — `publish(msg, 1s)`。捨てずに subscriber を待つ
- **DDS (Fast DDS)** — 通常の `rclcpp` publisher / subscription。reliable + keep-all、トランスポートはデフォルト

環境: Intel Core i7-13700H (20 スレッド)、WSL2 (kernel 6.18)、ROS 2 Jazzy、`rmw_fastrtps_cpp`。
各条件 1 回の実行で、CPU 固定やリアルタイムスケジューリングは行っていないため、数十マイクロ秒の差は実行ごとの
ばらつきの範囲です。生データ: [`docs/benchmark/latency_wsl2_fastdds.csv`](docs/benchmark/latency_wsl2_fastdds.csv)

### レイテンシ

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/benchmark/latency_1khz-dark.svg">
  <img src="docs/benchmark/latency_1khz.svg" alt="1 kHz でのレイテンシ中央値と p99 (ペイロードサイズ別、無負荷 / CPU 負荷あり)">
</picture>

### スループット

可能な限り速く publish した場合、取りこぼしのない 2 方式はどちらも全メッセージを届けますが、UDS のほうが
2 - 36 倍速く届けます。滞留も短く、この条件でのレイテンシ中央値は UDS が 55 µs - 3.3 ms、DDS が 169 ms - 1.6 s です。
ノンブロッキングの UDS はメッセージを捨てる (後述) ため、ここには含めていません。

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/benchmark/throughput-dark.svg">
  <img src="docs/benchmark/throughput.svg" alt="可能な限り速く publish したときの毎秒受信メッセージ数">
</picture>

### CPU 使用量

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/benchmark/cpu_per_message-dark.svg">
  <img src="docs/benchmark/cpu_per_message.svg" alt="10 kHz での 1 メッセージあたり CPU 時間 (publisher と subscriber の合計)">
</picture>

### メッセージの取りこぼし

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

### 接続確立までの時間

publisher を生成してから、すでに起動している subscriber に届く状態になるまでの時間です
(24 条件の中央値。カッコ内は範囲)。

| CPU | UDS | DDS (Fast DDS) |
|---|---|---|
| 無負荷 | 0.2 ms (0.2 - 0.4) | 438 ms (103 - 446) |
| 負荷あり | 1.0 ms (0.3 - 8.5) | 1566 ms (59 - 1650) |

### メモリ

UDS の最大 RSS は全条件で 1 プロセスあたり 27 - 28 MB でした。DDS も subscriber が追いつけるレートでは同じですが、
メッセージが滞留すると最大 394 MB まで増えました (64K ペイロード、レート無制限)。どちらの値にも、計測用プロセスが
パラメータを受け取るために持っている `rclcpp` ノードのぶんが含まれます。

### 自分で計測する

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

# CPU 負荷 x transport x 送信レート x ペイロードサイズ (LOADS / RATES / SIZES / COUNT / RMWS で上書き可能)
# dds はインストール済みの RMW 実装ごとに計測
ros2 run ros2_uds_ipc_benchmark run_latency_matrix.sh result.csv

# CSV をグラフ化 (matplotlib が必要)
ros2 run ros2_uds_ipc_benchmark plot_latency.py result.csv charts/
```

## Tech Stack

- C++17、`rclcpp` (CDR シリアライズ、ミラー用の `GenericPublisher`)
- POSIX ソケット: `AF_UNIX` + `SOCK_SEQPACKET`、`poll()`
- `ament_cmake`、GoogleTest

## Testing

```bash
colcon test --packages-select ros2_uds_ipc
colcon test-result --verbose
```

## License

[MIT](LICENSE)
