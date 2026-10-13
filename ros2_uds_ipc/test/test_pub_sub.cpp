#include <sched.h>
#include <sys/resource.h>
#include <unistd.h>

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <std_msgs/msg/color_rgba.hpp>
#include <std_msgs/msg/string.hpp>

#include "ros2_uds_ipc/uds_publisher.hpp"
#include "ros2_uds_ipc/uds_subscriber.hpp"

namespace
{
  std::string socket_path(const std::string & name)
  {
    const auto dir = std::filesystem::temp_directory_path() / ("uds_ipc_test_" + std::to_string(::getpid()));
    return (dir / name).string();
  }
}  // namespace

TEST(UdsPubSub, PublishFailsWithoutSubscriber)
{
  uds_ipc::UdsPublisher<std_msgs::msg::String> publisher(socket_path("no_subscriber.sock"));
  std_msgs::msg::String msg;
  msg.data = "hello";
  EXPECT_FALSE(publisher.publish(msg));
}

TEST(UdsPubSub, DeliversMessagesInOrder)
{
  const std::string path = socket_path("in_order.sock");
  const size_t message_count = 100;

  std::mutex mutex;
  std::condition_variable cv;
  std::vector<std::string> received;
  uds_ipc::UdsSubscriber<std_msgs::msg::String> subscriber(
    path, [&](const std_msgs::msg::String & msg) {
      std::lock_guard<std::mutex> lock(mutex);
      received.push_back(msg.data);
      cv.notify_one();
    });

  uds_ipc::UdsPublisher<std_msgs::msg::String> publisher(path);
  for (size_t i = 0; i < message_count; ++i) {
    std_msgs::msg::String msg;
    msg.data = "message " + std::to_string(i);
    ASSERT_TRUE(publisher.publish(msg));
  }

  std::unique_lock<std::mutex> lock(mutex);
  ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(5), [&] { return received.size() == message_count; }));
  for (size_t i = 0; i < message_count; ++i) {
    EXPECT_EQ(received[i], "message " + std::to_string(i));
  }
}

// Fixed-size messages skip CDR serialization and are sent as raw bytes.
TEST(UdsPubSub, DeliversFixedSizeMessagesAsRawBytes)
{
  static_assert(uds_ipc::detail::is_raw_copyable<std_msgs::msg::ColorRGBA>);
  static_assert(!uds_ipc::detail::is_raw_copyable<std_msgs::msg::String>);

  const std::string path = socket_path("fixed_size.sock");
  const size_t message_count = 100;

  std::mutex mutex;
  std::condition_variable cv;
  std::vector<std_msgs::msg::ColorRGBA> received;
  uds_ipc::UdsSubscriber<std_msgs::msg::ColorRGBA> subscriber(
    path, [&](const std_msgs::msg::ColorRGBA & msg) {
      std::lock_guard<std::mutex> lock(mutex);
      received.push_back(msg);
      cv.notify_one();
    });

  uds_ipc::UdsPublisher<std_msgs::msg::ColorRGBA> publisher(path);
  for (size_t i = 0; i < message_count; ++i) {
    std_msgs::msg::ColorRGBA msg;
    msg.r = static_cast<float>(i);
    msg.g = 0.25f;
    msg.b = -1.5f;
    msg.a = 1.0f;
    ASSERT_TRUE(publisher.publish(msg));
  }

  std::unique_lock<std::mutex> lock(mutex);
  ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(5), [&] { return received.size() == message_count; }));
  for (size_t i = 0; i < message_count; ++i) {
    EXPECT_EQ(received[i].r, static_cast<float>(i));
    EXPECT_EQ(received[i].g, 0.25f);
    EXPECT_EQ(received[i].b, -1.5f);
    EXPECT_EQ(received[i].a, 1.0f);
  }
}

// A subscriber slower than the publisher fills the receive queue.
TEST(UdsPubSub, FullQueueDropsWithoutTimeoutAndKeepsConnection)
{
  const std::string path = socket_path("full_queue.sock");
  std_msgs::msg::String msg;
  msg.data.assign(32 * 1024, 'x');

  std::mutex mutex;
  std::condition_variable cv;
  bool release = false;
  size_t received = 0;
  uds_ipc::UdsSubscriber<std_msgs::msg::String> subscriber(
    path, [&](const std_msgs::msg::String &) {
      std::unique_lock<std::mutex> lock(mutex);
      cv.wait(lock, [&] { return release; });
      ++received;
      cv.notify_all();
    });

  uds_ipc::UdsPublisher<std_msgs::msg::String> publisher(path);
  size_t sent = 0;
  bool dropped = false;
  for (size_t i = 0; i < 1000 && !dropped; ++i) {
    if (publisher.publish(msg)) {
      ++sent;
    } else {
      dropped = true;
    }
  }
  ASSERT_TRUE(dropped);
  ASSERT_GT(sent, 0u);

  // The connection survived: everything accepted so far is delivered, and so is a later message.
  std::unique_lock<std::mutex> lock(mutex);
  release = true;
  cv.notify_all();
  ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(5), [&] { return received == sent; }));
  lock.unlock();
  ASSERT_TRUE(publisher.publish(msg));
  lock.lock();
  EXPECT_TRUE(cv.wait_for(lock, std::chrono::seconds(5), [&] { return received == sent + 1; }));
}

TEST(UdsPubSub, SendBufferSizeBoundsTheQueue)
{
  std_msgs::msg::String msg;
  msg.data.assign(1024, 'x');

  // Number of messages accepted while the subscriber does not consume anything.
  const auto count_accepted = [&](const std::string & name, size_t send_buffer_bytes) {
    const std::string path = socket_path(name);
    std::mutex mutex;
    std::condition_variable cv;
    bool release = false;
    uds_ipc::UdsSubscriber<std_msgs::msg::String> subscriber(
      path, [&](const std_msgs::msg::String &) {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [&] { return release; });
      });

    uds_ipc::UdsPublisher<std_msgs::msg::String> publisher(path, nullptr, send_buffer_bytes);
    size_t accepted = 0;
    while (accepted < 100000 && publisher.publish(msg)) {
      ++accepted;
    }

    std::lock_guard<std::mutex> lock(mutex);
    release = true;
    cv.notify_all();
    return accepted;
  };

  const size_t accepted_default = count_accepted("buffer_default.sock", 0);
  const size_t accepted_small = count_accepted("buffer_small.sock", 16 * 1024);
  EXPECT_GT(accepted_small, 0u);
  EXPECT_LT(accepted_small, accepted_default);
}

TEST(UdsPubSub, SendTimeoutWaitsForSlowSubscriber)
{
  const std::string path = socket_path("send_timeout.sock");
  const size_t message_count = 200;
  std_msgs::msg::String msg;
  msg.data.assign(32 * 1024, 'x');

  std::mutex mutex;
  std::condition_variable cv;
  size_t received = 0;
  uds_ipc::UdsSubscriber<std_msgs::msg::String> subscriber(
    path, [&](const std_msgs::msg::String &) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      std::lock_guard<std::mutex> lock(mutex);
      ++received;
      cv.notify_one();
    });

  uds_ipc::UdsPublisher<std_msgs::msg::String> publisher(path);
  for (size_t i = 0; i < message_count; ++i) {
    ASSERT_TRUE(publisher.publish(msg, std::chrono::seconds(5)));
  }

  std::unique_lock<std::mutex> lock(mutex);
  EXPECT_TRUE(cv.wait_for(lock, std::chrono::seconds(5), [&] { return received == message_count; }));
}

// A message larger than max_message_size arrives truncated. It is dropped, and later messages still arrive.
TEST(UdsPubSub, DropsMessagesLargerThanMaxMessageSize)
{
  const std::string path = socket_path("oversized.sock");

  std::mutex mutex;
  std::condition_variable cv;
  std::vector<std::string> received;
  uds_ipc::UdsSubscriber<std_msgs::msg::String> subscriber(
    path, [&](const std_msgs::msg::String & msg) {
      std::lock_guard<std::mutex> lock(mutex);
      received.push_back(msg.data);
      cv.notify_one();
    }, 64);

  uds_ipc::UdsPublisher<std_msgs::msg::String> publisher(path);
  std_msgs::msg::String msg;
  msg.data = std::string(1000, 'x');
  ASSERT_TRUE(publisher.publish(msg));
  msg.data = "small";
  ASSERT_TRUE(publisher.publish(msg));

  std::unique_lock<std::mutex> lock(mutex);
  ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(5), [&] { return !received.empty(); }));
  ASSERT_EQ(received.size(), 1u);
  EXPECT_EQ(received[0], "small");
}

TEST(UdsPubSub, ReconnectsAfterSubscriberRestart)
{
  const std::string path = socket_path("reconnect.sock");
  std_msgs::msg::String msg;
  msg.data = "hello";

  std::mutex mutex;
  std::condition_variable cv;
  size_t received = 0;
  const auto callback = [&](const std_msgs::msg::String &) {
    std::lock_guard<std::mutex> lock(mutex);
    ++received;
    cv.notify_one();
  };

  uds_ipc::UdsPublisher<std_msgs::msg::String> publisher(path);
  {
    uds_ipc::UdsSubscriber<std_msgs::msg::String> subscriber(path, callback);
    ASSERT_TRUE(publisher.publish(msg));
    std::unique_lock<std::mutex> lock(mutex);
    ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(5), [&] { return received == 1; }));
  }

  // The first publish after the peer is gone detects the disconnection and fails.
  EXPECT_FALSE(publisher.publish(msg));

  uds_ipc::UdsSubscriber<std_msgs::msg::String> subscriber(path, callback);
  ASSERT_TRUE(publisher.publish(msg));
  std::unique_lock<std::mutex> lock(mutex);
  EXPECT_TRUE(cv.wait_for(lock, std::chrono::seconds(5), [&] { return received == 2; }));
}

// Receive thread scheduling

namespace
{
  /// Policy and CPU of the thread that invoked the callback, for the first message received.
  struct CallbackThread
  {
    int policy = -1;
    int priority = -1;
    int cpu = -1;
  };

  /// Publishes one message to a subscriber created with `scheduling` and returns what the callback thread looked like.
  CallbackThread run_with_scheduling(const std::string & path, const uds_ipc::Scheduling & scheduling, bool * applied)
  {
    std::mutex mutex;
    std::condition_variable cv;
    bool done = false;
    CallbackThread seen;
    uds_ipc::UdsSubscriber<std_msgs::msg::String> subscriber(
      path, [&](const std_msgs::msg::String &) {
        std::lock_guard<std::mutex> lock(mutex);
        sched_param param{};
        seen.policy = ::sched_getscheduler(0);
        ::sched_getparam(0, &param);
        seen.priority = param.sched_priority;
        seen.cpu = ::sched_getcpu();
        done = true;
        cv.notify_one();
      }, 65536, nullptr, scheduling);
    if (applied) *applied = subscriber.scheduling_applied();

    uds_ipc::UdsPublisher<std_msgs::msg::String> publisher(path);
    std_msgs::msg::String msg;
    msg.data = "x";
    EXPECT_TRUE(publisher.publish(msg));
    std::unique_lock<std::mutex> lock(mutex);
    EXPECT_TRUE(cv.wait_for(lock, std::chrono::seconds(5), [&] { return done; }));
    return seen;
  }

  /// True if an unprivileged process is not allowed to use SCHED_FIFO.
  bool rt_is_denied()
  {
    rlimit limit{};
    ::getrlimit(RLIMIT_RTPRIO, &limit);
    return ::geteuid() != 0 && limit.rlim_cur == 0;
  }
}  // namespace

TEST(UdsScheduling, DefaultLeavesTheThreadAlone)
{
  bool applied = false;
  const auto seen = run_with_scheduling(socket_path("sched_default.sock"), {}, &applied);
  EXPECT_TRUE(applied);
  EXPECT_NE(seen.policy, SCHED_FIFO);
}

TEST(UdsScheduling, RejectsPrioritiesAboveTheLimit)
{
  using Subscriber = uds_ipc::UdsSubscriber<std_msgs::msg::String>;
  const auto callback = [](const std_msgs::msg::String &) {};
  uds_ipc::Scheduling scheduling;

  scheduling.rt_priority = uds_ipc::Scheduling::kMaxRtPriority + 1;
  EXPECT_THROW(Subscriber(socket_path("sched_above.sock"), callback, 65536, nullptr, scheduling), std::invalid_argument);
  scheduling.rt_priority = 99;
  EXPECT_THROW(Subscriber(socket_path("sched_99.sock"), callback, 65536, nullptr, scheduling), std::invalid_argument);
  scheduling.rt_priority = -1;
  EXPECT_THROW(Subscriber(socket_path("sched_negative.sock"), callback, 65536, nullptr, scheduling),
    std::invalid_argument);

  // The limit itself is accepted. Without the OS permission it only warns.
  scheduling.rt_priority = uds_ipc::Scheduling::kMaxRtPriority;
  EXPECT_NO_THROW(Subscriber(socket_path("sched_limit.sock"), callback, 65536, nullptr, scheduling));
}

TEST(UdsScheduling, AppliesPriorityOrFallsBackAndStillDelivers)
{
  uds_ipc::Scheduling scheduling;
  scheduling.rt_priority = 30;

  bool applied = false;
  const auto seen = run_with_scheduling(socket_path("sched_fifo.sock"), scheduling, &applied);
  if (applied) {
    EXPECT_EQ(seen.policy, SCHED_FIFO);
    EXPECT_EQ(seen.priority, 30);
  } else {
    EXPECT_TRUE(rt_is_denied());  // Only a missing permission may keep it from applying.
    EXPECT_NE(seen.policy, SCHED_FIFO);
  }
}

TEST(UdsScheduling, RequireThrowsWhenTheOsRefuses)
{
  if (!rt_is_denied()) GTEST_SKIP() << "This process may use SCHED_FIFO.";

  uds_ipc::Scheduling scheduling;
  scheduling.rt_priority = 30;
  scheduling.require = true;
  const std::string path = socket_path("sched_require.sock");
  EXPECT_THROW(
    uds_ipc::UdsSubscriber<std_msgs::msg::String>(path, [](const std_msgs::msg::String &) {}, 65536, nullptr, scheduling),
    std::runtime_error);
  EXPECT_FALSE(std::filesystem::exists(path));  // Nothing is left behind.
}

TEST(UdsScheduling, PinsTheReceiveThreadToACpu)
{
  cpu_set_t allowed;
  CPU_ZERO(&allowed);
  ASSERT_EQ(::sched_getaffinity(0, sizeof(allowed), &allowed), 0);
  int cpu = -1;
  for (int i = CPU_SETSIZE - 1; i >= 0; --i) {  // The last one, to differ from where the test thread starts.
    if (CPU_ISSET(i, &allowed)) {
      cpu = i;
      break;
    }
  }
  ASSERT_GE(cpu, 0);

  uds_ipc::Scheduling scheduling;
  scheduling.cpu = cpu;
  bool applied = false;
  const auto seen = run_with_scheduling(socket_path("sched_cpu.sock"), scheduling, &applied);
  EXPECT_TRUE(applied);
  EXPECT_EQ(seen.cpu, cpu);
}
