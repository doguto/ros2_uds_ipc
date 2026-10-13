// Publisher side of the latency benchmark. Waits for latency_subscriber, publishes warm-up and
// measured messages over UDS or DDS at a fixed rate, sends the end marker and exits.

#include <chrono>
#include <cstdint>
#include <cstring>
#include <memory>
#include <thread>

#include <rclcpp/rclcpp.hpp>

#include "ros2_uds_ipc/uds_publisher.hpp"

#include "latency_common.hpp"

namespace
{
  using namespace latency_benchmark;  // NOLINT(build/namespaces)

  constexpr size_t kWarmupCount = 100;

  /// Publishes warm-up, measured and end-marker messages through `publish`, which returns false on failure.
  /// `ready` must return true once the subscriber is reachable. `created_ns` is when the publisher was created.
  template <typename MessageT, typename PublishT, typename ReadyT>
  int publish_all(
    rclcpp::Node & node,
    const Options & options,
    int64_t created_ns,
    PublishT && publish,
    ReadyT && ready
  )
  {
    EndMarker end_marker;
    MessageT msg;
    init_payload(msg, options.payload_bytes);
    set_stamp(msg, kWarmupStamp);

    const auto start_deadline = std::chrono::steady_clock::now() + kStartTimeout;
    while (!ready(msg)) {
      if (!rclcpp::ok() || std::chrono::steady_clock::now() > start_deadline) {
        RCLCPP_ERROR(node.get_logger(), "Timed out waiting for the subscriber.");
        return 1;
      }
      std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    end_marker.connect_ns = now_ns() - created_ns;

    const auto period = options.rate_hz > 0.0 ?
      std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::duration<double>(1.0 / options.rate_hz)) :
      std::chrono::nanoseconds(0);
    auto next = std::chrono::steady_clock::now();
    const auto wait_next = [&] {
      if (period.count() > 0) {
        next += period;
        std::this_thread::sleep_until(next);
      }
    };

    for (size_t i = 0; i < kWarmupCount && rclcpp::ok(); ++i) {
      publish(msg);
      wait_next();
    }

    const int64_t start_cpu_ns = process_cpu_ns();
    const int64_t start_wall_ns = now_ns();
    size_t failed = 0;
    for (size_t i = 0; i < options.count && rclcpp::ok(); ++i) {
      set_stamp(msg, now_ns());
      if (!publish(msg)) {
        ++failed;
      }
      wait_next();
    }
    end_marker.publisher_cpu.cpu_ns = process_cpu_ns() - start_cpu_ns;
    end_marker.publisher_cpu.wall_ns = now_ns() - start_wall_ns;
    end_marker.publish_failed = static_cast<int64_t>(failed);
    end_marker.max_rss_kb = max_rss_kb();

    // The end marker must arrive, so retry while the send buffer is full.
    set_end_marker(msg, end_marker);
    const auto end_deadline = std::chrono::steady_clock::now() + kStartTimeout;
    while (!publish(msg)) {
      if (!rclcpp::ok() || std::chrono::steady_clock::now() > end_deadline) {
        RCLCPP_ERROR(node.get_logger(), "Failed to send the end marker.");
        return 1;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    if (failed > 0) {
      RCLCPP_WARN(node.get_logger(), "Publish failed for %zu of %zu messages.", failed, options.count);
    }
    return 0;
  }

  template <typename MessageT>
  int run_uds(const rclcpp::Node::SharedPtr & node, const Options & options, int64_t created_ns)
  {
    const std::chrono::nanoseconds send_timeout =
      options.transport != "uds" ? kReliableSendTimeout : std::chrono::seconds(0);
    uds_ipc::UdsPublisher<MessageT> publisher(options.socket_path);
    const auto publish = [&](const MessageT & msg) { return publisher.publish(msg, send_timeout); };
    // publish() connects lazily and fails until the subscriber is listening.
    return publish_all<MessageT>(*node, options, created_ns, publish, publish);
  }

  int run(const rclcpp::Node::SharedPtr & node)
  {
    const Options options = declare_options(*node);

    const int64_t created_ns = now_ns();
    if (options.transport == "uds_reliable_fixed") {
      return dispatch_fixed(options.payload_bytes, [&](auto msg) {
        return run_uds<decltype(msg)>(node, options, created_ns);
      });
    }
    if (options.transport != "dds") {
      return run_uds<Message>(node, options, created_ns);
    }

    auto publisher = node->create_publisher<Message>(kTopic, dds_qos());
    const int result = publish_all<Message>(
      *node, options, created_ns,
      [&publisher](const Message & msg) {
        publisher->publish(msg);
        return true;
      },
      [&publisher](const Message &) { return publisher->get_subscription_count() > 0; });
    // Keep the writer alive until the reliable subscriber has acknowledged the end marker.
    publisher->wait_for_all_acked(kStartTimeout);
    return result;
  }
}  // namespace

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  int result = 1;
  {
    auto node = std::make_shared<rclcpp::Node>("latency_publisher");
    try {
      result = run(node);
    } catch (const std::exception & e) {
      RCLCPP_ERROR(node->get_logger(), "%s", e.what());
    }
  }
  rclcpp::shutdown();
  return result;
}
