"""Launch latency_subscriber and latency_publisher as separate processes for one measurement.

With load_threads > 0, cpu_load also runs to keep that many cores busy during the measurement.
"""

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, EmitEvent, RegisterEventHandler, TimerAction
from launch.conditions import IfCondition
from launch.event_handlers import OnProcessExit
from launch.events import Shutdown
from launch.substitutions import LaunchConfiguration, PythonExpression
from launch_ros.actions import Node
from launch_ros.parameter_descriptions import ParameterValue


def generate_launch_description():
    arguments = [
        DeclareLaunchArgument('transport', default_value='uds', choices=['uds', 'uds_reliable', 'uds_reliable_fixed', 'dds'],
                              description='Transport to measure. uds_reliable: publish() waits for the '
                                          'subscriber instead of dropping when its queue is full. '
                                          'uds_reliable_fixed: the same with a fixed-size message, sent '
                                          'without serialization (payload_bytes 64 ... 65536, powers of 4)'),
        DeclareLaunchArgument('payload_bytes', default_value='64',
                              description='Payload size in bytes (at least 8)'),
        DeclareLaunchArgument('count', default_value='10000',
                              description='Number of measured messages'),
        DeclareLaunchArgument('rate_hz', default_value='1000.0',
                              description='Publish rate in Hz, 0 for no throttling'),
        DeclareLaunchArgument('socket_path', default_value='/tmp/uds_ipc_benchmark/latency.sock',
                              description='UDS socket path (uds transports only)'),
        DeclareLaunchArgument('rt_priority', default_value='0',
                              description='SCHED_FIFO priority, 0: not used, at most 49. uds: the subscriber '
                                          'receive thread and the publishing thread. dds: both processes are '
                                          'started under chrt, so every thread of the middleware gets it'),
        DeclareLaunchArgument('load_threads', default_value='0',
                              description='Number of busy-loop threads to run alongside (0: no load)'),
        DeclareLaunchArgument('publisher_delay', default_value='2.0',
                              description='Seconds to wait before starting the publisher, so that connect_ms is '
                                          'measured against a subscriber that is already up'),
        DeclareLaunchArgument('output_file', default_value='',
                              description='CSV file to append the result row to (empty: log only)'),
    ]

    common_parameters = {
        'transport': ParameterValue(LaunchConfiguration('transport'), value_type=str),
        'payload_bytes': ParameterValue(LaunchConfiguration('payload_bytes'), value_type=int),
        'count': ParameterValue(LaunchConfiguration('count'), value_type=int),
        'rate_hz': ParameterValue(LaunchConfiguration('rate_hz'), value_type=float),
        'socket_path': ParameterValue(LaunchConfiguration('socket_path'), value_type=str),
        'rt_priority': ParameterValue(LaunchConfiguration('rt_priority'), value_type=int),
    }

    # DDS has no single thread to prioritize, so the whole process is made real-time. The uds transports
    # set the priority themselves, on the receive thread and the publishing thread only.
    dds_prefix = PythonExpression([
        "'chrt -f ", LaunchConfiguration('rt_priority'), "' if '", LaunchConfiguration('transport'),
        "' == 'dds' and ", LaunchConfiguration('rt_priority'), " > 0 else ''"])

    subscriber = Node(
        package='ros2_uds_ipc_benchmark',
        executable='latency_subscriber',
        output='screen',
        prefix=dds_prefix,
        parameters=[common_parameters, {
            'output_file': ParameterValue(LaunchConfiguration('output_file'), value_type=str),
            'load_threads': ParameterValue(LaunchConfiguration('load_threads'), value_type=int),
        }],
    )
    publisher = Node(
        package='ros2_uds_ipc_benchmark',
        executable='latency_publisher',
        output='screen',
        prefix=dds_prefix,
        parameters=[common_parameters],
    )

    delayed_publisher = TimerAction(period=LaunchConfiguration('publisher_delay'), actions=[publisher])

    cpu_load = Node(
        package='ros2_uds_ipc_benchmark',
        executable='cpu_load',
        output='screen',
        parameters=[{'threads': ParameterValue(LaunchConfiguration('load_threads'), value_type=int)}],
        condition=IfCondition(PythonExpression([LaunchConfiguration('load_threads'), ' > 0'])),
    )

    # Both nodes exit by themselves (result reported, or timed out). Shut down once both are gone,
    # which also stops cpu_load.
    remaining = {subscriber, publisher}

    def on_node_exit(event, context):
        remaining.discard(event.action)
        return [EmitEvent(event=Shutdown())] if not remaining else None

    shutdown_on_exit = [
        RegisterEventHandler(OnProcessExit(target_action=node, on_exit=on_node_exit))
        for node in (subscriber, publisher)
    ]

    return LaunchDescription(arguments + [cpu_load, subscriber, delayed_publisher] + shutdown_on_exit)
