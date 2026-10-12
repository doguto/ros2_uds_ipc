// Keeps `threads` cores busy until shut down, to measure the benchmark under CPU contention.

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

#include <rclcpp/rclcpp.hpp>

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  {
    auto node = std::make_shared<rclcpp::Node>("cpu_load");
    // 0: one thread per hardware thread.
    int64_t thread_count = node->declare_parameter<int64_t>("threads", 0);
    if (thread_count <= 0) {
      thread_count = static_cast<int64_t>(std::thread::hardware_concurrency());
    }
    RCLCPP_INFO(node->get_logger(), "Running %ld busy-loop threads.", static_cast<long>(thread_count));

    std::atomic<bool> running{true};
    std::vector<std::thread> threads;
    for (int64_t i = 0; i < thread_count; ++i) {
      threads.emplace_back([&running] {
        while (running.load(std::memory_order_relaxed)) {}
      });
    }

    rclcpp::spin(node);

    running = false;
    for (auto & thread : threads) {
      thread.join();
    }
  }
  rclcpp::shutdown();
  return 0;
}
