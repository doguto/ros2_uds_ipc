// Subscriber side of the latency benchmark. Receives over UDS or DDS, and once the end marker arrives,
// logs the distribution of (receive time - send time) and the CPU usage of both processes,
// and optionally appends them to a CSV file.

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <rclcpp/rclcpp.hpp>
#include <rmw/rmw.h>

#include "ros2_uds_ipc/uds_subscriber.hpp"

#include "latency_common.hpp"

namespace
{
  using namespace latency_benchmark;  // NOLINT(build/namespaces)

  /// Collects latency samples.
  class LatencyRecorder
  {
  public:
    explicit LatencyRecorder(size_t expected_count) { samples_ns_.reserve(expected_count); }

    template <typename MessageT>
    void on_message(const MessageT & msg)
    {
      const int64_t received_ns = now_ns();
      if (msg.data.size() < sizeof(int64_t)) return;

      int64_t stamp;
      std::memcpy(&stamp, msg.data.data(), sizeof(stamp));
      if (stamp == kWarmupStamp) return;

      std::lock_guard<std::mutex> lock(mutex_);
      if (stamp == kEndStamp) {
        subscriber_cpu_.cpu_ns = process_cpu_ns() - start_cpu_ns_;
        subscriber_cpu_.wall_ns = received_ns - start_wall_ns_;
        if (msg.data.size() >= sizeof(EndMarker)) {
          std::memcpy(&publisher_, msg.data.data(), sizeof(publisher_));
        }
        done_ = true;
      } else {
        if (samples_ns_.empty()) {
          start_cpu_ns_ = process_cpu_ns();
          start_wall_ns_ = received_ns;
        }
        samples_ns_.push_back(received_ns - stamp);
      }
      cv_.notify_one();
    }

    /// Blocks until the end marker arrives. Returns false if no message arrives for kIdleTimeout
    /// or if rclcpp is shut down.
    bool wait_done()
    {
      std::unique_lock<std::mutex> lock(mutex_);
      size_t last_size = samples_ns_.size();
      auto deadline = std::chrono::steady_clock::now() + kIdleTimeout;
      while (!done_) {
        cv_.wait_for(lock, std::chrono::milliseconds(100));
        if (samples_ns_.size() != last_size) {
          last_size = samples_ns_.size();
          deadline = std::chrono::steady_clock::now() + kIdleTimeout;
        } else if (!rclcpp::ok() || std::chrono::steady_clock::now() > deadline) {
          return done_;
        }
      }
      return true;
    }

    /// Sorted samples. Call after receiving has stopped.
    std::vector<int64_t> take_sorted()
    {
      std::lock_guard<std::mutex> lock(mutex_);
      std::sort(samples_ns_.begin(), samples_ns_.end());
      return std::move(samples_ns_);
    }

    /// Valid only if wait_done() returned true.
    EndMarker publisher() const { return publisher_; }
    /// CPU usage over the measured messages. Valid only if wait_done() returned true.
    CpuWindow subscriber_cpu() const { return subscriber_cpu_; }

  private:
    std::mutex mutex_;
    std::condition_variable cv_;
    std::vector<int64_t> samples_ns_;
    int64_t start_cpu_ns_ = 0;
    int64_t start_wall_ns_ = 0;
    EndMarker publisher_;
    CpuWindow subscriber_cpu_;
    bool done_ = false;
  };

  void report(
    rclcpp::Node & node,
    const Options & options,
    const std::string & output_file,
    int64_t load_threads,
    const std::vector<int64_t> & sorted_ns,
    const EndMarker & publisher,
    const CpuWindow & subscriber_cpu
  )
  {
    if (sorted_ns.empty()) {
      RCLCPP_ERROR(node.get_logger(), "No measured message was received.");
      return;
    }

    double sum_ns = 0.0;
    for (const int64_t sample : sorted_ns) {
      sum_ns += static_cast<double>(sample);
    }
    const auto percentile_us = [&](double p) {
      const auto index = static_cast<size_t>(p * static_cast<double>(sorted_ns.size() - 1));
      return static_cast<double>(sorted_ns[index]) / 1e3;
    };
    const double min_us = percentile_us(0.0);
    const double mean_us = sum_ns / static_cast<double>(sorted_ns.size()) / 1e3;
    const double p50_us = percentile_us(0.5);
    const double p99_us = percentile_us(0.99);
    const double p999_us = percentile_us(0.999);
    const double max_us = percentile_us(1.0);

    const double pub_cpu_percent = publisher.publisher_cpu.percent();
    const double pub_cpu_us_per_msg = publisher.publisher_cpu.us_per_message(options.count);
    const double connect_ms = static_cast<double>(publisher.connect_ns) / 1e6;
    const long publish_failed = static_cast<long>(publisher.publish_failed);
    const long pub_max_rss_kb = static_cast<long>(publisher.max_rss_kb);
    const long sub_max_rss_kb = static_cast<long>(max_rss_kb());
    // Messages received per second, between the first measured message and the end marker.
    const double throughput_hz = subscriber_cpu.wall_ns > 0 ?
      static_cast<double>(sorted_ns.size()) * 1e9 / static_cast<double>(subscriber_cpu.wall_ns) : 0.0;
    const double sub_cpu_percent = subscriber_cpu.percent();
    const double sub_cpu_us_per_msg = subscriber_cpu.us_per_message(sorted_ns.size());

    RCLCPP_INFO(
      node.get_logger(),
      "transport=%s payload_bytes=%zu rate_hz=%g load_threads=%ld received=%zu/%zu latency_us: min=%.3f mean=%.3f "
      "p50=%.3f p99=%.3f p99.9=%.3f max=%.3f",
      options.transport.c_str(), options.payload_bytes, options.rate_hz, static_cast<long>(load_threads),
      sorted_ns.size(), options.count, min_us, mean_us, p50_us, p99_us, p999_us, max_us
    );
    RCLCPP_INFO(
      node.get_logger(), "cpu: publisher=%.2f%% (%.3f us/msg) subscriber=%.2f%% (%.3f us/msg)", pub_cpu_percent,
      pub_cpu_us_per_msg, sub_cpu_percent, sub_cpu_us_per_msg
    );
    RCLCPP_INFO(
      node.get_logger(), "publish_failed=%ld throughput_hz=%.1f connect_ms=%.3f max_rss_kb: publisher=%ld subscriber=%ld",
      publish_failed, throughput_hz, connect_ms, pub_max_rss_kb, sub_max_rss_kb
    );

    if (output_file.empty()) return;

    std::error_code ec;
    const bool write_header = !std::filesystem::exists(output_file, ec) || std::filesystem::file_size(output_file, ec) == 0;
    std::FILE * file = std::fopen(output_file.c_str(), "a");
    if (!file) {
      RCLCPP_ERROR(node.get_logger(), "Failed to open output file: %s", output_file.c_str());
      return;
    }
    if (write_header) {
      std::fprintf(
        file,
        "transport,rmw,payload_bytes,rate_hz,load_threads,count,publish_failed,received,min_us,mean_us,p50_us,p99_us,"
        "p999_us,max_us,pub_cpu_percent,pub_cpu_us_per_msg,sub_cpu_percent,sub_cpu_us_per_msg,connect_ms,"
        "pub_max_rss_kb,sub_max_rss_kb,throughput_hz,rt_priority\n"
      );
    }
    // The RMW implementation only matters for the dds rows.
    std::fprintf(
      file, "%s,%s,%zu,%g,%ld,%zu,%ld,%zu,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.2f,%.3f,%.2f,%.3f,%.3f,%ld,%ld,%.1f,%d\n",
      options.transport.c_str(), options.transport == "dds" ? rmw_get_implementation_identifier() : "",
      options.payload_bytes, options.rate_hz, static_cast<long>(load_threads), options.count, publish_failed,
      sorted_ns.size(), min_us, mean_us, p50_us, p99_us, p999_us, max_us, pub_cpu_percent, pub_cpu_us_per_msg,
      sub_cpu_percent, sub_cpu_us_per_msg, connect_ms, pub_max_rss_kb, sub_max_rss_kb, throughput_hz,
      options.rt_priority
    );
    std::fclose(file);
  }

  int run(const rclcpp::Node::SharedPtr & node)
  {
    const Options options = declare_options(*node);
    const std::string output_file = node->declare_parameter<std::string>("output_file", "");
    // Only recorded in the result; the load itself is generated by the cpu_load node.
    const int64_t load_threads = node->declare_parameter<int64_t>("load_threads", 0);

    LatencyRecorder recorder(options.count);
    // Generic, for the UDS subscribers of the different message types. rclcpp needs a concrete one.
    const auto callback = [&recorder](const auto & msg) { recorder.on_message(msg); };
    const auto dds_callback = [&recorder](const Message & msg) { recorder.on_message(msg); };

    // A run labeled with a priority must really have it, so a refusal is an error and not a warning.
    uds_ipc::Scheduling scheduling;
    scheduling.rt_priority = options.rt_priority;
    scheduling.require = true;

    bool completed = false;
    if (options.transport == "uds_reliable_fixed") {
      dispatch_fixed(options.payload_bytes, [&](auto msg) {
        // Received straight into the message; there is no serialized buffer.
        uds_ipc::UdsSubscriber<decltype(msg)> subscriber(
          options.socket_path, callback, 65536, nullptr, scheduling);
        completed = recorder.wait_done();
        return 0;
      });
    } else if (options.transport != "dds") {
      // Headroom for the CDR header and the MultiArray layout.
      uds_ipc::UdsSubscriber<Message> subscriber(
        options.socket_path, callback, options.payload_bytes + 1024, nullptr, scheduling);
      completed = recorder.wait_done();
    } else {
      auto subscription = node->create_subscription<Message>(kTopic, dds_qos(), dds_callback);
      rclcpp::executors::SingleThreadedExecutor executor;
      executor.add_node(node);
      std::thread spin_thread([&executor] { executor.spin(); });
      completed = recorder.wait_done();
      executor.cancel();
      spin_thread.join();
    }

    if (!completed) {
      RCLCPP_ERROR(node->get_logger(), "Stopped before receiving the end marker.");
    }
    report(
      *node, options, output_file, load_threads, recorder.take_sorted(), recorder.publisher(),
      recorder.subscriber_cpu()
    );
    return completed ? 0 : 1;
  }
}  // namespace

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  int result = 1;
  {
    auto node = std::make_shared<rclcpp::Node>("latency_subscriber");
    try {
      result = run(node);
    } catch (const std::exception & e) {
      RCLCPP_ERROR(node->get_logger(), "%s", e.what());
    }
  }
  rclcpp::shutdown();
  return result;
}
