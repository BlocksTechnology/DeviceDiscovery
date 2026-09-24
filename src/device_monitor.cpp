#include "device_monitor.hpp"
#include "serial_scanner.hpp"
#include "usb_scanner.hpp"

#include <iostream>
#include <libudev.h>
#include <map>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>

namespace {

constexpr int kDebounceMs = 200;

// Identifies a physical device for the purpose of diffing two scans.
// Serial devices key on their stable by-id symlink; USB devices key on
// bus+address, which is unique among concurrently-attached devices (see
// device.hpp for why VID:PID alone isn't enough).
std::string deviceKey(const Device &d) {
  switch (d.connection) {
  case ConnectionType::Serial:
    return "serial:" + d.symlink_name;
  case ConnectionType::USB:
    return "usb:" + std::to_string(d.usb_bus_number) + ":" +
           std::to_string(d.usb_device_address);
  default:
    return "other:" + d.name;
  }
}

std::vector<Device> fullScan() {
  std::vector<Device> devices;
  auto serial = SerialScanner().scan();
  auto usb = USBScanner().scan();
  devices.insert(devices.end(), std::make_move_iterator(serial.begin()),
                 std::make_move_iterator(serial.end()));
  devices.insert(devices.end(), std::make_move_iterator(usb.begin()),
                 std::make_move_iterator(usb.end()));
  return devices;
}

} // namespace

DeviceMonitor::DeviceMonitor() {
  udev_ = udev_new();
  if (!udev_) {
    std::cerr << "[DeviceMonitor] udev_new failed\n";
    return;
  }
  monitor_ = udev_monitor_new_from_netlink(udev_, "udev");
  if (!monitor_) {
    std::cerr << "[DeviceMonitor] udev_monitor_new_from_netlink failed\n";
    return;
  }
  udev_monitor_filter_add_match_subsystem_devtype(monitor_, "tty", nullptr);
  udev_monitor_filter_add_match_subsystem_devtype(monitor_, "usb", nullptr);
  // Enabled here, well before the monitor thread (and its epoll loop) even
  // exists: the kernel starts queuing events into the netlink socket's
  // receive buffer from this point on, so nothing plugged in between now
  // and start() is missed - it's just waiting to be read.
  udev_monitor_enable_receiving(monitor_);
}

DeviceMonitor::~DeviceMonitor() {
  stop();
  if (monitor_)
    udev_monitor_unref(monitor_);
  if (udev_)
    udev_unref(udev_);
}

std::vector<Device> DeviceMonitor::scanNow() const { return fullScan(); }

void DeviceMonitor::start(ChangeCallback onChange) {
  if (!monitor_ || running_.exchange(true))
    return;

  stop_fd_ = eventfd(0, EFD_NONBLOCK);
  thread_ = std::thread(&DeviceMonitor::run, this, std::move(onChange));
}

void DeviceMonitor::stop() {
  if (!running_.exchange(false))
    return;
  if (stop_fd_ >= 0) {
    uint64_t one = 1;
    ssize_t written = write(stop_fd_, &one, sizeof(one));
    (void)written; // best-effort wake; the loop also re-checks running_
  }
  if (thread_.joinable())
    thread_.join();
  if (stop_fd_ >= 0) {
    close(stop_fd_);
    stop_fd_ = -1;
  }
}

void DeviceMonitor::run(ChangeCallback onChange) {
  int udev_fd = udev_monitor_get_fd(monitor_);
  int epfd = epoll_create1(0);
  if (epfd < 0) {
    std::cerr << "[DeviceMonitor] epoll_create1 failed\n";
    return;
  }

  epoll_event ev{};
  ev.events = EPOLLIN;
  ev.data.fd = udev_fd;
  epoll_ctl(epfd, EPOLL_CTL_ADD, udev_fd, &ev);
  ev.data.fd = stop_fd_;
  epoll_ctl(epfd, EPOLL_CTL_ADD, stop_fd_, &ev);

  // Baseline: whatever is already connected when monitoring starts is not
  // reported as "added" - only genuine changes from here on are.
  std::vector<Device> previous = fullScan();
  bool dirty = false;

  epoll_event events[4];
  while (running_.load()) {
    int timeout_ms = dirty ? kDebounceMs : -1; // block when idle, no polling
    int n = epoll_wait(epfd, events, 4, timeout_ms);

    if (!running_.load())
      break;

    if (n == 0 && dirty) {
      // Quiet period elapsed with no new udev activity: settle on the
      // current device set and report the diff.
      rescanAndDiff(onChange, previous);
      dirty = false;
      continue;
    }

    bool stopRequested = false;
    for (int i = 0; i < n; i++) {
      if (events[i].data.fd == stop_fd_) {
        stopRequested = true;
        continue;
      }
      if (events[i].data.fd == udev_fd) {
        // Drain every queued event now; each one just extends the
        // debounce window rather than triggering its own rescan.
        udev_device *dev;
        while ((dev = udev_monitor_receive_device(monitor_)) != nullptr) {
          udev_device_unref(dev);
          dirty = true;
        }
      }
    }
    if (stopRequested)
      break;
  }
  close(epfd);
}

void DeviceMonitor::rescanAndDiff(const ChangeCallback &onChange,
                                   std::vector<Device> &previous) const {
  auto current = fullScan();

  std::map<std::string, const Device *> prevByKey;
  for (const auto &d : previous)
    prevByKey[deviceKey(d)] = &d;

  std::map<std::string, const Device *> curByKey;
  for (const auto &d : current)
    curByKey[deviceKey(d)] = &d;

  std::vector<Device> added, removed;
  for (const auto &[key, dev] : curByKey)
    if (!prevByKey.count(key))
      added.push_back(*dev);
  for (const auto &[key, dev] : prevByKey)
    if (!curByKey.count(key))
      removed.push_back(*dev);

  previous = std::move(current);

  if (!added.empty() || !removed.empty())
    onChange(added, removed);
}
