// Copyright 2026 kuto
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.


#include <sched.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <std_msgs/msg/color_rgba.hpp>
#include <std_msgs/msg/string.hpp>

#include "ros2_uds_ipc/detail/uds_address.hpp"
#include "ros2_uds_ipc/uds_publisher.hpp"
#include "ros2_uds_ipc/uds_subscriber.hpp"

namespace
{
std::string socket_path(const std::string & name)
{
  const auto dir = std::filesystem::temp_directory_path() /
    ("uds_ipc_test_" + std::to_string(::getpid()));
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
  ASSERT_TRUE(publisher.connect(std::chrono::seconds(5)));
  for (size_t i = 0; i < message_count; ++i) {
    std_msgs::msg::String msg;
    msg.data = "message " + std::to_string(i);
    ASSERT_TRUE(publisher.publish(msg));
  }

  std::unique_lock<std::mutex> lock(mutex);
  ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(5), [&] {
      return received.size() == message_count;
  }));
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
  ASSERT_TRUE(publisher.connect(std::chrono::seconds(5)));
  for (size_t i = 0; i < message_count; ++i) {
    std_msgs::msg::ColorRGBA msg;
    msg.r = static_cast<float>(i);
    msg.g = 0.25f;
    msg.b = -1.5f;
    msg.a = 1.0f;
    ASSERT_TRUE(publisher.publish(msg));
  }

  std::unique_lock<std::mutex> lock(mutex);
  ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(5), [&] {
      return received.size() == message_count;
  }));
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
      cv.wait(lock, [&] {return release;});
      ++received;
      cv.notify_all();
    });

  uds_ipc::UdsPublisher<std_msgs::msg::String> publisher(path);
  ASSERT_TRUE(publisher.connect(std::chrono::seconds(5)));
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
  ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(5), [&] {return received == sent;}));
  lock.unlock();
  ASSERT_TRUE(publisher.publish(msg));
  lock.lock();
  EXPECT_TRUE(cv.wait_for(lock, std::chrono::seconds(5), [&] {return received == sent + 1;}));
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
          cv.wait(lock, [&] {return release;});
        });

      uds_ipc::UdsPublisher<std_msgs::msg::String> publisher(path, nullptr, send_buffer_bytes);
      EXPECT_TRUE(publisher.connect(std::chrono::seconds(5)));
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
  ASSERT_TRUE(publisher.connect(std::chrono::seconds(5)));
  for (size_t i = 0; i < message_count; ++i) {
    ASSERT_TRUE(publisher.publish(msg, std::chrono::seconds(5)));
  }

  std::unique_lock<std::mutex> lock(mutex);
  EXPECT_TRUE(cv.wait_for(lock, std::chrono::seconds(5), [&] {return received == message_count;}));
}

// A message larger than max_message_size arrives truncated. It is dropped, and later messages
// still arrive.
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
  ASSERT_TRUE(publisher.connect(std::chrono::seconds(5)));
  std_msgs::msg::String msg;
  msg.data = std::string(1000, 'x');
  ASSERT_TRUE(publisher.publish(msg));
  msg.data = "small";
  ASSERT_TRUE(publisher.publish(msg));

  std::unique_lock<std::mutex> lock(mutex);
  ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(5), [&] {return !received.empty();}));
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
    ASSERT_TRUE(publisher.connect(std::chrono::seconds(5)));
    ASSERT_TRUE(publisher.publish(msg));
    std::unique_lock<std::mutex> lock(mutex);
    ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(5), [&] {return received == 1;}));
  }

  // The first publish after the peer is gone detects the disconnection and fails.
  EXPECT_FALSE(publisher.publish(msg));

  uds_ipc::UdsSubscriber<std_msgs::msg::String> subscriber(path, callback);
  ASSERT_TRUE(publisher.connect(std::chrono::seconds(5)));
  ASSERT_TRUE(publisher.publish(msg));
  std::unique_lock<std::mutex> lock(mutex);
  EXPECT_TRUE(cv.wait_for(lock, std::chrono::seconds(5), [&] {return received == 2;}));
}

// The connection is 1:1. A second publisher is not accepted while the first is connected, so its
// messages fail instead of piling up unread; it takes over once the first disconnects.
TEST(UdsPubSub, SecondPublisherWaitsUntilTheFirstDisconnects)
{
  const std::string path = socket_path("second_publisher.sock");

  std::mutex mutex;
  std::condition_variable cv;
  std::vector<std::string> received;
  uds_ipc::UdsSubscriber<std_msgs::msg::String> subscriber(
    path, [&](const std_msgs::msg::String & msg) {
      std::lock_guard<std::mutex> lock(mutex);
      received.push_back(msg.data);
      cv.notify_one();
    });

  uds_ipc::UdsPublisher<std_msgs::msg::String> second(path);
  std_msgs::msg::String msg;
  {
    uds_ipc::UdsPublisher<std_msgs::msg::String> first(path);
    ASSERT_TRUE(first.connect(std::chrono::seconds(5)));

    EXPECT_FALSE(second.connect(std::chrono::milliseconds(300)));
    msg.data = "from second while first is connected";
    EXPECT_FALSE(second.publish(msg));
    EXPECT_FALSE(second.publish(msg, std::chrono::milliseconds(100)));
    EXPECT_FALSE(second.is_connected());

    msg.data = "from first";
    ASSERT_TRUE(first.publish(msg));
  }

  ASSERT_TRUE(second.connect(std::chrono::seconds(5)));
  msg.data = "from second";
  ASSERT_TRUE(second.publish(msg));

  std::unique_lock<std::mutex> lock(mutex);
  ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(5), [&] {return received.size() == 2;}));
  EXPECT_EQ(received[0], "from first");
  EXPECT_EQ(received[1], "from second");
}

// Path checks when a subscriber starts

TEST(UdsPath, RefusesAPathAnotherSubscriberIsListeningOn)
{
  const std::string path = socket_path("taken.sock");
  std::mutex mutex;
  std::condition_variable cv;
  size_t received = 0;
  uds_ipc::UdsSubscriber<std_msgs::msg::String> subscriber(
    path, [&](const std_msgs::msg::String &) {
      std::lock_guard<std::mutex> lock(mutex);
      ++received;
      cv.notify_one();
    });

  EXPECT_THROW(
    uds_ipc::UdsSubscriber<std_msgs::msg::String>(path, [](const std_msgs::msg::String &) {}),
    std::runtime_error);

  // The first subscriber still owns the path.
  uds_ipc::UdsPublisher<std_msgs::msg::String> publisher(path);
  ASSERT_TRUE(publisher.connect(std::chrono::seconds(5)));
  std_msgs::msg::String msg;
  msg.data = "hello";
  ASSERT_TRUE(publisher.publish(msg));
  std::unique_lock<std::mutex> lock(mutex);
  EXPECT_TRUE(cv.wait_for(lock, std::chrono::seconds(5), [&] {return received == 1;}));
}

TEST(UdsPath, RefusesAPathThatIsNotASocket)
{
  const std::string path = socket_path("regular_file.sock");
  std::filesystem::create_directories(std::filesystem::path(path).parent_path());
  std::ofstream(path) << "keep me";

  EXPECT_THROW(
    uds_ipc::UdsSubscriber<std_msgs::msg::String>(path, [](const std_msgs::msg::String &) {}),
    std::runtime_error);
  EXPECT_TRUE(std::filesystem::is_regular_file(path));
  std::filesystem::remove(path);
}

// A socket file left by a subscriber that crashed is replaced.
TEST(UdsPath, ReplacesAStaleSocketFile)
{
  const std::string path = socket_path("stale.sock");
  std::filesystem::create_directories(std::filesystem::path(path).parent_path());
  const int fd = ::socket(AF_UNIX, SOCK_SEQPACKET, 0);
  const sockaddr_un addr = uds_ipc::detail::make_unix_address(path);
  ASSERT_EQ(::bind(fd, reinterpret_cast<const sockaddr *>(&addr), sizeof(addr)), 0);
  ::close(fd);
  ASSERT_TRUE(std::filesystem::is_socket(path));

  std::mutex mutex;
  std::condition_variable cv;
  size_t received = 0;
  uds_ipc::UdsSubscriber<std_msgs::msg::String> subscriber(
    path, [&](const std_msgs::msg::String &) {
      std::lock_guard<std::mutex> lock(mutex);
      ++received;
      cv.notify_one();
    });

  uds_ipc::UdsPublisher<std_msgs::msg::String> publisher(path);
  ASSERT_TRUE(publisher.connect(std::chrono::seconds(5)));
  std_msgs::msg::String msg;
  msg.data = "hello";
  ASSERT_TRUE(publisher.publish(msg));
  std::unique_lock<std::mutex> lock(mutex);
  EXPECT_TRUE(cv.wait_for(lock, std::chrono::seconds(5), [&] {return received == 1;}));
}

// Settings are checked when a publisher or subscriber is created

TEST(UdsValidation, RejectsUnusableSocketPaths)
{
  const auto noop = [](const std_msgs::msg::String &) {};
  const std::string too_long = "/tmp/" + std::string(sizeof(sockaddr_un::sun_path), 'x');
  const std::string max_length = "/tmp/" + std::string(sizeof(sockaddr_un::sun_path) - 6, 'x');
  for (const std::string & path :
    {std::string(), too_long, std::string("/tmp/dir/"), std::string("/tmp/a\0b", 8)})
  {
    EXPECT_THROW(uds_ipc::UdsPublisher<std_msgs::msg::String>{path}, std::invalid_argument);
    EXPECT_THROW(
      (uds_ipc::UdsSubscriber<std_msgs::msg::String>{path, noop}), std::invalid_argument);
  }
  EXPECT_NO_THROW(uds_ipc::UdsPublisher<std_msgs::msg::String>{max_length});
}

TEST(UdsValidation, RejectsInvalidSettings)
{
  const std::string path = socket_path("settings.sock");
  EXPECT_THROW(
    (uds_ipc::UdsSubscriber<std_msgs::msg::String>{path, nullptr}), std::invalid_argument);
  EXPECT_THROW(
    (uds_ipc::UdsSubscriber<std_msgs::msg::String>{path, [](const std_msgs::msg::String &) {}, 0}),
    std::invalid_argument);
  EXPECT_THROW(
    (uds_ipc::UdsPublisher<std_msgs::msg::String>{path, nullptr, size_t{1} << 40}),
    std::invalid_argument);

  // Nothing was left behind, so the path is still free.
  EXPECT_FALSE(std::filesystem::exists(path));
  EXPECT_NO_THROW(
    (uds_ipc::UdsSubscriber<std_msgs::msg::String>{path, [](const std_msgs::msg::String &) {}}));
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

  /// Publishes one message to a subscriber created with `scheduling` and returns what the callback
  /// thread looked like.
CallbackThread run_with_scheduling(
  const std::string & path, const uds_ipc::Scheduling & scheduling,
  bool * applied)
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
  if (applied) {*applied = subscriber.scheduling_applied();}

  uds_ipc::UdsPublisher<std_msgs::msg::String> publisher(path);
  EXPECT_TRUE(publisher.connect(std::chrono::seconds(5)));
  std_msgs::msg::String msg;
  msg.data = "x";
  EXPECT_TRUE(publisher.publish(msg));
  std::unique_lock<std::mutex> lock(mutex);
  EXPECT_TRUE(cv.wait_for(lock, std::chrono::seconds(5), [&] {return done;}));
  return seen;
}

/// True if this process is not allowed to use SCHED_FIFO. It tries on a throwaway thread, because
/// the answer depends on rlimits and capabilities
/// (a root user in a container may lack CAP_SYS_NICE).
bool rt_is_denied()
{
  bool denied = true;
  std::thread([&denied] {
      sched_param param{};
      param.sched_priority = 30;
      denied = ::sched_setscheduler(0, SCHED_FIFO, &param) != 0;
    }).join();
  return denied;
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
  EXPECT_THROW(Subscriber(socket_path("sched_above.sock"), callback, 65536, nullptr, scheduling),
    std::invalid_argument);
  scheduling.rt_priority = 99;
  EXPECT_THROW(Subscriber(socket_path("sched_99.sock"), callback, 65536, nullptr, scheduling),
    std::invalid_argument);
  scheduling.rt_priority = -1;
  EXPECT_THROW(Subscriber(socket_path("sched_negative.sock"), callback, 65536, nullptr, scheduling),
    std::invalid_argument);

  // The limit itself is accepted. Without the OS permission it only warns.
  scheduling.rt_priority = uds_ipc::Scheduling::kMaxRtPriority;
  EXPECT_NO_THROW(Subscriber(socket_path("sched_limit.sock"), callback, 65536, nullptr,
    scheduling));
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
  if (!rt_is_denied()) {GTEST_SKIP() << "This process may use SCHED_FIFO.";}

  uds_ipc::Scheduling scheduling;
  scheduling.rt_priority = 30;
  scheduling.require = true;
  const std::string path = socket_path("sched_require.sock");
  EXPECT_THROW(
    uds_ipc::UdsSubscriber<std_msgs::msg::String>(path, [](const std_msgs::msg::String &) {}, 65536,
    nullptr, scheduling),
    std::runtime_error);
  EXPECT_FALSE(std::filesystem::exists(path));  // Nothing is left behind.
}

TEST(UdsScheduling, PinsTheReceiveThreadToACpu)
{
  cpu_set_t allowed;
  CPU_ZERO(&allowed);
  ASSERT_EQ(::sched_getaffinity(0, sizeof(allowed), &allowed), 0);
  int cpu = -1;
  // The last one, to differ from where the test thread starts.
  for (int i = CPU_SETSIZE - 1; i >= 0; --i) {
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
