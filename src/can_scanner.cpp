#include "can_scanner.hpp"
#include <algorithm>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

const std::string CANScanner::NET_CLASS_PATH = "/sys/class/net";

std::vector<std::string> CANScanner::getInterfaces() {
  std::vector<std::string> interfaces;

  if (!fs::exists(NET_CLASS_PATH))
    return interfaces;

  for (const auto &entry : fs::directory_iterator(NET_CLASS_PATH)) {
    std::string iface = entry.path().filename().string();

    if (iface.rfind("can", 0) == 0 || iface.rfind("slcan", 0) == 0) {
      interfaces.push_back(iface);
    }
  }
  std::sort(interfaces.begin(), interfaces.end());
  return interfaces;
}

std::vector<Device> CANScanner::scanImpl(const std::string &interface) {
  std::vector<Device> devices;
  auto available = getInterfaces();
  bool exists = std::find(available.begin(), available.end(), interface) !=
                available.end();

  if (!exists) {
    std::cerr << "[CANScanner] Interface '"
              << interface << "' not found. Available: ";
    for (const auto &i : available)
      std::cerr << i << " ";
    std::cerr << "\n";
    return devices;
  }

  std::cerr << "[CANScanner] CAN scanning not yet implemented "
            << "- coming next, will send CAN query frame and collect UUID "
               "responses.\n" ;
  return devices;
}
