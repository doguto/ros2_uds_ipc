# uds_ipc

AF_UNIX の `SOCK_SEQPACKET` 上で rclcpp のメッセージ型をそのまま publish/subscribe するための,
ヘッダオンリーの薄いライブラリ。

## 使う場面

DDS (discovery / QoS マッチング / RTPS プロトコル層) を経由せず、プロセスを跨いだ低遅延な配送が
要る **event-driven** な経路のために使う (例: `ros2_control` の hardware plugin から
`hardware_can_api` への `hardware/*` topic)。

- **送信側がすでに自分のタイマーで動いている場合**（navigator のような制御ループ）には不要。
  その場合は intra-process composition（`use_intra_process_comms`）か, 欠損許容なら
  共有メモリの "最新値だけ保持する" 方式の方が向く。
- **受信側が来たら即反応する以外に手段がない場合**（本パッケージの対象）に使う。

## 信頼性

`SOCK_SEQPACKET` は接続指向で、カーネルが TCP と同様に到達を保証する。ベストエフォートではない。
`UdsPublisher::publish()` は未接続/送信失敗時に `false` を返すだけで, 揉み消さない
(呼び出し側がログ/フォールバックを判断すること)。

## 制約

- `MessageT` は rosidl が生成する型なら何でもよい (CDR で一度シリアライズするので可変長でも安全)。
- `UdsSubscriber` のコンストラクタで渡す `max_message_size` (既定 65536 byte) より大きい
  message は切り捨てられる。想定サイズに余裕を持たせること。
- `UdsSubscriber` の callback は専用スレッドから呼ばれる (rclcpp executor のスレッドではない)。
  共有状態に触るなら呼び出し側で排他すること。
- この経路は DDS から見えない。`ros2 topic echo` / rosbag で見たい場合は、下の「ミラー」を使うこと。

## ミラー (ros2 topic echo / rosbag で見る)

`create_uds_mirror()` で作った publisher を `UdsPublisher` または `UdsSubscriber` に渡すと、
送受信するメッセージを DDS の通常 topic にも流す。

| 渡す先 | 見えるもの | 使いどころ |
|---|---|---|
| `UdsPublisher` (送信側) | 送ろうとしたメッセージすべて。受信側が起動していなくても見える | 送信側が何を出しているかを確認・記録したいとき |
| `UdsSubscriber` (受信側) | 受信側に届いたメッセージだけ | 受信側に実際に何が届いたかを確認したいとき |

- 同じ topic 名で両側からミラーすると、同じメッセージが2回流れるので、どちらか一方にすること。
- 受信した / 送る CDR バイト列をそのまま publish する (シリアライズし直さない)。
- DDS への publish は専用スレッドが 10ms ごとにまとめて行う。送受信のスレッドでは、
  購読者がいるときに確保済みのバッファへコピーするだけなので、本来の経路はほぼ遅れない
  (1000Hz の `JointState` で、`echo` 中の `UdsPublisher::publish()` の増加は約2〜3µs)。
- 誰も購読していなければコピーもしない (購読者の有無は専用スレッドが 10ms ごとに調べる)。
  購読を始めてから最大 10ms は流れない。
- 10ms の間に 100 件を超えると古いものから捨てる。
- 逆方向 (`ros2 topic pub` で UDS 側へ送り込む) には対応していない。

```cpp
// 送信側でミラーする場合
RoStep::UdsPublisher<sensor_msgs::msg::JointState> pub(
  "/tmp/rostep/joint_commands.sock",
  RoStep::create_uds_mirror<sensor_msgs::msg::JointState>(node, "hardware/joint_commands"));

// 受信側でミラーする場合
RoStep::UdsSubscriber<sensor_msgs::msg::JointState> sub(
  "/tmp/rostep/joint_commands.sock",
  [](const sensor_msgs::msg::JointState & msg) { /* ... */ },
  65536,
  RoStep::create_uds_mirror<sensor_msgs::msg::JointState>(node, "hardware/joint_commands"));
```

## 使い方

```cpp
// 受信側
RoStep::UdsSubscriber<sensor_msgs::msg::JointState> sub(
  "/tmp/rostep/joint_commands.sock",
  [](const sensor_msgs::msg::JointState & msg) { /* ... */ });

// 送信側
RoStep::UdsPublisher<sensor_msgs::msg::JointState> pub("/tmp/rostep/joint_commands.sock");
if (!pub.publish(msg)) {
  // 未接続 or 送信失敗. ログ/フォールバックを判断する.
}
```
