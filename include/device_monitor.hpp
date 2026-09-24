#pragma once
#include "device.hpp"
#include <atomic>
#include <functional>
#include <thread>
#include <vector>

struct udev;
struct udev_monitor;

// Event-driven USB + serial device watcher.
//
// Uses udev's "udev" netlink source (post-udev-rules events, so
// /dev/serial/by-id symlinks and similar rule-created state already exist
// by the time we see the event) on a dedicated thread, multiplexed with an
// eventfd via epoll so stop() wakes it immediately instead of waiting on a
// timeout.
//
// A single physical (dis)connect fires several udev events in quick
// succession (the USB device itself, each interface, the tty subdevice).
// Individual events don't trigger a rescan directly - they just mark the
// state dirty and reset a short debounce window; the actual rescan + diff
// only runs once events go quiet, so one physical plug/unplug is reported
// as one coherent set of changes instead of several partial ones.
class DeviceMonitor {
public:
  using ChangeCallback = std::function<void(const std::vector<Device> &added,
                                             const std::vector<Device> &removed)>;

  DeviceMonitor();
  ~DeviceMonitor();

  DeviceMonitor(const DeviceMonitor &) = delete;
  DeviceMonitor &operator=(const DeviceMonitor &) = delete;

  // Immediate full scan (USB + serial), independent of the debounce
  // machinery. Used to build the snapshot handed to a newly connected
  // IPC client.
  std::vector<Device> scanNow() const;

  // Starts the background monitor thread. onChange fires (from the
  // monitor thread, not the caller's) only for genuine changes seen
  // *after* start() - the pre-existing devices found by the first
  // internal scan are the baseline, not reported as "added".
  void start(ChangeCallback onChange);
  void stop();

private:
  void run(ChangeCallback onChange);
  void rescanAndDiff(const ChangeCallback &onChange,
                      std::vector<Device> &previous) const;

  udev *udev_ = nullptr;
  udev_monitor *monitor_ = nullptr;
  int stop_fd_ = -1; // eventfd used to wake epoll_wait() from stop()
  std::thread thread_;
  std::atomic<bool> running_{false};
};
