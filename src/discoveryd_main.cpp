#include "device_json.hpp"
#include "device_monitor.hpp"
#include "ipc_server.hpp"

#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <unistd.h>
#include <vector>

namespace {
// Bumped on any incompatible change to the message format (docs/PROTOCOL.md).
// Clients compare it against the version they understand.
constexpr int kProtocolVersion = 1;

// Resolution order: --socket PATH, $DEVICE_DISCOVERY_SOCKET,
// $XDG_RUNTIME_DIR/blockscreen/device_discovery.sock, /tmp fallback.
// The systemd unit passes --socket so the path doesn't depend on the
// service's environment.
std::string resolveSocketPath(int argc, char **argv) {
  for (int i = 1; i + 1 < argc; i++)
    if (std::strcmp(argv[i], "--socket") == 0)
      return argv[i + 1];

  const char *envPath = std::getenv("DEVICE_DISCOVERY_SOCKET");
  if (envPath && *envPath)
    return envPath;

  const char *runtimeDir = std::getenv("XDG_RUNTIME_DIR");
  std::string base = (runtimeDir && *runtimeDir) ? runtimeDir : "/tmp";
  return base + "/blockscreen/device_discovery.sock";
}

std::mutex g_devicesMutex;
std::vector<Device>
    g_devices; // last known-good full scan, for new clients' snapshots

// Built under g_devicesMutex, from the IPC thread, whenever a client connects.
std::string buildSnapshotBlock() {
  nlohmann::json hello{{"type", "hello"},
                       {"proto", kProtocolVersion},
                       {"pid", static_cast<long>(getpid())}};
  nlohmann::json snapshot{{"type", "snapshot"},
                          {"devices", nlohmann::json::array()}};

  {
    std::lock_guard<std::mutex> lock(g_devicesMutex);
    for (const auto &d : g_devices)
      snapshot["devices"].push_back(deviceToJson(d));
  }
  return hello.dump() + "\n" + snapshot.dump() + "\n";
}

} // namespace

int main(int argc, char **argv) {
  // stdout goes to journald under systemd, where it would otherwise be
  // block-buffered and hotplug lines would show up late or not at all.
  std::cout << std::unitbuf;
  const std::string socketPath = resolveSocketPath(argc, argv);
  std::cout << "[discoveryd] listening on " << socketPath << "\n";

  DeviceMonitor monitor;
  IpcServer server(socketPath);

  {
    std::lock_guard<std::mutex> lock(g_devicesMutex);
    g_devices = monitor.scanNow();
  }
  server.setSnapshotProvider(buildSnapshotBlock);

  monitor.start([&server, &monitor](const std::vector<Device> &added,
                                    const std::vector<Device> &removed) {
    {
      // Re-scanning here (rather than patching g_devices from added/removed
      // directly) means the snapshot handed to the next connecting client
      // can never drift from what DeviceMonitor's own diff considers
      // ground truth - there's only one place that decides device identity.
      std::lock_guard<std::mutex> lock(g_devicesMutex);
      g_devices = monitor.scanNow();
    }
    for (const auto &d : added) {
      std::cout << "[discoveryd] + " << d.name << "\n";
      server.broadcast(
          nlohmann::json{{"type", "added"}, {"device", deviceToJson(d)}}
              .dump());
    }
    for (const auto &d : removed) {
      std::cout << "[discoveryd] - " << d.name << "\n";
      server.broadcast(
          nlohmann::json{{"type", "removed"}, {"device", deviceToJson(d)}}
              .dump());
    }
  });

  if (!server.start()) {
    std::cerr << "[discoveryd] failed to start IPC server\n";
    monitor.stop();
    return 1;
  }

  sigset_t set;
  sigemptyset(&set);
  sigaddset(&set, SIGINT);
  sigaddset(&set, SIGTERM);
  sigprocmask(SIG_BLOCK, &set, nullptr);

  int sig = 0;
  sigwait(&set, &sig);
  std::cout << "\n[discoveryd] shutting down\n";

  server.stop();
  monitor.stop();
  return 0;
}
