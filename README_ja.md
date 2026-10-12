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
- **送信側をブロックしない** — `publish()` はノンブロッキングで、未接続または送信失敗のときは `false` を
  返します。ログ出力やフォールバックは呼び出し側が判断します。
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
  // 未接続 (subscriber がまだ起動していない等) か, 送信に失敗した.
}
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

未計測です。

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
