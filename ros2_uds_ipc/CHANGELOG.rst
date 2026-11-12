^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
Changelog for package ros2_uds_ipc
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

0.1.0 (2026-11-12)
------------------
* Initial release
* UdsPublisher / UdsSubscriber: publish and subscribe rclcpp message types over AF_UNIX SOCK_SEQPACKET
* UdsMirrorWorker for mirroring messages to a regular ROS 2 topic
* Enforce a single publisher per socket path and protect paths that are in use
* Idle timeout and lock file for UdsSubscriber
* Validate configuration values in constructors
