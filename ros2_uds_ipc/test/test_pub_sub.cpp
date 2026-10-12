#include <unistd.h>

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
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
