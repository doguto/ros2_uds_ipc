#ifndef ROS2_UDS_IPC__SCHEDULING_HPP_
#define ROS2_UDS_IPC__SCHEDULING_HPP_

namespace uds_ipc
{
  /// How the receive thread of a UdsSubscriber is scheduled. The default leaves it to the OS.
  ///
  /// A SCHED_FIFO thread preempts every normal thread the moment it becomes runnable, so a callback that is
  /// invoked while the CPU is busy with other work (image processing, planning) is not kept waiting. Use it
  /// only for the few subscribers that have a deadline, and keep their callbacks short and free of locks that
  /// normal threads also take.
  ///
  /// The OS needs to allow it: an `rtprio` limit (limits.conf, `LimitRTPRIO=` in systemd) or CAP_SYS_NICE.
  struct Scheduling
  {
    /// Highest accepted priority. Linux runs its threaded interrupt handlers (irq/*) and other kernel threads
    /// at SCHED_FIFO 50, so anything from 50 up could delay or starve the kernel itself and is rejected.
    static constexpr int kMaxRtPriority = 49;

    /// 0: the OS default policy. 1 - kMaxRtPriority: SCHED_FIFO with that priority.
    int rt_priority = 0;

    /// CPU to pin the receive thread to, -1 to let the OS choose. Pinning only helps when that core is
    /// kept free of other work (isolcpus, cpuset); on a shared core it can make things worse.
    int cpu = -1;

    /// What to do when the OS refuses the settings above. false: log a warning and run with the OS
    /// defaults (see UdsSubscriber::scheduling_applied()). true: the constructor throws std::runtime_error.
    bool require = false;
  };

}  // namespace uds_ipc

#endif  // ROS2_UDS_IPC__SCHEDULING_HPP_
