#include <unistd.h>

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <string>
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
