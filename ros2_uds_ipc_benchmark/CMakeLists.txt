cmake_minimum_required(VERSION 3.8)
project(ros2_uds_ipc_benchmark)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

# Measurements from an unoptimized build are meaningless.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(ros2_uds_ipc REQUIRED)
find_package(std_msgs REQUIRED)

foreach(target latency_publisher latency_subscriber)
  add_executable(${target} src/${target}.cpp)
  target_compile_features(${target} PUBLIC cxx_std_17)
  target_link_libraries(${target}
    rclcpp::rclcpp
    ros2_uds_ipc::ros2_uds_ipc
    ${std_msgs_TARGETS}
  )
endforeach()

add_executable(cpu_load src/cpu_load.cpp)
target_compile_features(cpu_load PUBLIC cxx_std_17)
target_link_libraries(cpu_load rclcpp::rclcpp)

install(
  TARGETS cpu_load latency_publisher latency_subscriber
  RUNTIME DESTINATION lib/${PROJECT_NAME}
)

install(
  PROGRAMS scripts/plot_latency.py scripts/run_latency_matrix.sh
  DESTINATION lib/${PROJECT_NAME}
)

install(
  DIRECTORY launch
  DESTINATION share/${PROJECT_NAME}
)

ament_package()
