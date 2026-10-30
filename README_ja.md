# ros2_uds_ipc

[English](README.md) | [日本語](README_ja.md)

[![CI](https://github.com/doguto/ros2_uds_ipc/actions/workflows/ci.yml/badge.svg)](https://github.com/doguto/ros2_uds_ipc/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
![ROS 2](https://img.shields.io/badge/ROS%202-Jazzy%20%7C%20Kilted%20%7C%20Rolling-22314E)

`rclcpp` のメッセージ型を、DDS を経由せずに Unix ドメインソケット (`AF_UNIX` / `SOCK_SEQPACKET`) 上で
そのまま publish / subscribe するための、ROS 2 向けの薄い C++ ライブラリです。

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/benchmark/latency_1khz-dark.svg">
  <img src="docs/benchmark/latency_1khz.svg" alt="1 kHz でのレイテンシ中央値と p99 (ペイロードサイズ別、無負荷 / CPU 負荷あり)">
</picture>

2 プロセス間、1 kHz での、`publish()` から subscriber のコールバックまでのレイテンシです (低いほど良い)。

- **CPU 負荷時** (1 KB): 中央値は UDS が 12〜15 µs、DDS (Fast DDS / Cyclone DDS) と Zenoh は 152〜967 µs です。
- **リアルタイム優先度を付けた場合** (256 B): 最大レイテンシは UDS が 27〜57 µs、他は 95〜145 µs です。UDS は優先度を付ける
  スレッドが 1 本で済み、[コンストラクタの引数](#リアルタイムスケジューリング)で指定できます。
- **無負荷で、CPU のアイドル状態を C1 までに制限した場合** (256 B): 中央値は UDS が 15〜34 µs、他は 144〜272 µs です。

すべての数値・計測方法・自分で計測する手順は [docs/benchmark_ja.md](docs/benchmark_ja.md) にあります。

## Concept

**既存の問題。** 同一マシン上の 2 プロセス間で小さいメッセージを送る場合、遅延のほとんどはデータのコピーではなく、
受信側のミドルウェアの層で生まれます。コールバックが呼ばれるまでにメッセージが複数のスレッドを順に渡り、
受け渡しのたびに他の処理の後ろで待たされる可能性があります。共有メモリやゼロコピーが速くするのはコピーだけなので、
この部分はそのまま残ります。

**このライブラリの解決方法。** `ros2_uds_ipc` は ROS 2 のメッセージ型はそのままに、次の 3 点でこれを解決します。

1. **Unix ドメインソケットによる直接通信。** 専用スレッドがソケットを待ち、そのスレッドが直接コールバックを呼びます。
   スレッド間の受け渡しはありません。

   ```
   DDS:           カーネル -> DDS 受信スレッド -> RTPS 処理 -> wait set -> executor -> コールバック
   ros2_uds_ipc:  カーネル -> poll() で待つ専用スレッド -> コールバック
   ```

2. **コンストラクタの引数 1 つで `SCHED_FIFO`。** 受信経路がスレッド 1 本なので、そのスレッドにリアルタイム優先度
   (と CPU 固定) を指定するだけで、他の処理の後ろで待たされなくなります。ミドルウェアごとの設定は不要です
   ([リアルタイムスケジューリング](#リアルタイムスケジューリング) を参照)。
3. **固定長メッセージはシリアライズなし。** プリミティブと固定長配列だけのメッセージは、メモリ上のバイト列を
   そのまま送り、メッセージオブジェクトに直接受信します。

## Features

`ros2_uds_ipc` は ROS 2 のメッセージ型はそのままに、トランスポートだけを置き換えます。

- **任意の rosidl メッセージ型** — メッセージを CDR に一度シリアライズし、1 回の `send()` で送ります。
  可変長メッセージも扱えます。
- **固定長メッセージはシリアライズしません** — プリミティブと固定長配列だけで構成されたメッセージ (例: `Float64[7]`) は、
  メモリ上のバイト列をそのまま送り、メッセージオブジェクトに直接受信します。送受信の両側が同じメッセージ定義で
  ビルドされている必要がありますが、同一マシンでは満たされます。
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
接続中の publisher がいる間、2 つ目の publisher は受け付けられず、その `publish()` は `false` を返します
(読まれないまま溜まることはありません)。1 つ目が切断すると 2 つ目が引き継ぎます。使用中のパスに
2 つ目の subscriber を作ると例外になります。

## Requirements

- Linux
- ROS 2 Jazzy 以降 (Fast CDR 2.x)
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

ソケットを準備できなかった場合、コンストラクタは `std::runtime_error` を投げます。別の subscriber が
そのパスで待ち受けている場合や、パスにソケット以外のファイルがある場合も同様です。クラッシュした
subscriber が残したソケットファイルは置き換えます。第 3 引数の
`max_message_size` (既定値 65536 バイト) は受信できるシリアライズ済みメッセージの最大サイズで、
これを超えるメッセージは破棄されます。

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

`publish()` は必要になった時点で接続し、subscriber が接続を受け付けてから送れるようになります。そのため
subscriber の起動直後のノンブロッキングな `publish()` は `false` を返すことがあります。最初のメッセージから
届けたい場合は、ループに入る前に接続してください:

```cpp
if (!publisher.connect(std::chrono::seconds(1))) {
  // subscriber が待ち受けていないか, 別の publisher が接続中.
}
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

### リアルタイムスケジューリング

既定では、受信スレッドは通常のスレッドです。CPU が他の処理 (画像処理や経路計画など) で埋まっていると、他と同じように
順番を待つため、遅れて呼ばれるコールバックが裾として現れます。締め切りのある少数の subscriber については、
受信スレッドを `SCHED_FIFO` で動かし、必要なら CPU に固定できます。

```cpp
uds_ipc::Scheduling scheduling;
scheduling.rt_priority = 40;  // 1 〜 49。0 (既定) なら OS に任せる
scheduling.cpu = -1;          // 固定する CPU 番号。-1 なら固定しない
scheduling.require = false;   // true: OS に拒否されたとき、警告ではなく例外にする

uds_ipc::UdsSubscriber<sensor_msgs::msg::JointState> subscriber(
  "/run/my_robot/joint_command.sock", callback, 65536, nullptr, scheduling);
if (!subscriber.scheduling_applied()) { /* OS に拒否された。既定のスケジューリングで動作中 */ }
```

| するとよい | 避ける |
|---|---|
| 締め切りのある 1〜2 本 (モータ指令など) だけに使う | すべての subscriber を `SCHED_FIFO` にする |
| コールバックを短くする (データをコピーして戻る) | ブロック、メモリ確保、通常スレッドと共有するロックの取得 (優先度逆転) |
| 30〜49 を使う | 大きい数値ほど速いと期待する。50 以上は拒否されます。Linux は割り込み処理スレッドを 50 で動かしており、受信スレッドがカーネルを追い越さないようにするためです |
| 他の処理から空けたコア (`isolcpus`、cpuset) にだけ固定する | 共有コアに固定する |

OS が許可していないと、警告を出して既定のスケジューリングのまま動きます (`require = true` なら、コンストラクタが
例外を投げます)。次のいずれかで許可します。

- `/etc/security/limits.d/99-realtime.conf` に `@realtime - rtprio 49` と `@realtime - memlock unlimited` を書き、
  ユーザーを `realtime` グループに入れる (再ログインが必要)
- systemd ユニットで `LimitRTPRIO=49`、Docker なら `--cap-add=SYS_NICE --ulimit rtprio=49`

送信側のスレッドは利用者のものです。制御ループが送信するなら、同様にそのスレッドへリアルタイム優先度を付けてください
(`pthread_setschedparam`)。起動時に `mlockall(MCL_CURRENT | MCL_FUTURE)` を呼ぶと、その経路のページフォルトを避けられます。

## Tech Stack

- C++17、`rclcpp` (ミラー用の `GenericPublisher`)
- Fast CDR (生成された型サポート `rosidl_typesupport_fastrtps_cpp` 経由) による CDR シリアライズ
- POSIX ソケット: `AF_UNIX` + `SOCK_SEQPACKET`、`poll()`
- `ament_cmake`、GoogleTest

## Testing

```bash
colcon test --packages-select ros2_uds_ipc
colcon test-result --verbose
```

## License

[MIT](LICENSE)
