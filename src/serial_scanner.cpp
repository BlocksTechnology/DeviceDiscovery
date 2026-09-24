#include "serial_scanner.hpp"
#include "device.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <libusb-1.0/libusb.h>
#include <vector>

namespace fs = std::filesystem;

const std::string SerialScanner::SERIAL_BY_ID_PATH = "/dev/serial/by-id/";

FirmwareState SerialScanner::detectFirmware(const std::string &name) const {
  if (name.find("Klipper") != std::string::npos)
    return FirmwareState::Klipper;

  if (name.find("Katapult") != std::string::npos ||
      name.find("CanBoot") != std::string::npos)
    return FirmwareState::Katapult;

  if (name.find("1a86") != std::string::npos ||
      name.find("FTDI") != std::string::npos ||
      name.find("Silicon_Labs") != std::string::npos ||
      name.find("Prolific") != std::string::npos)
    return FirmwareState::Unflashed;

  return FirmwareState::Unknown;
}

std::string SerialScanner::extractMcuType(const std::string &product) const {
  if (product.empty())
    return "";

  auto pos = product.find('_');
  if (pos != std::string::npos)
    return product.substr(0, pos);

  return product;
}

Device SerialScanner::parseSymlink(const std::string &symlinkName,
                                   const std::string &resolvedPath) const {

  Device d;
  d.connection = ConnectionType::Serial;
  d.symlink_name = symlinkName;
  d.device_path = resolvedPath;
  d.firmware = detectFirmware(symlinkName);
  d.is_klipper = d.firmware == FirmwareState::Klipper;
  d.is_katapult = d.firmware == FirmwareState::Katapult;

  std::string s = symlinkName;

  if (s.rfind("usb-", 0) == 0)
    s = s.substr(4);

  // Interface
  auto if_pos = s.rfind("-if");
  if (if_pos != std::string::npos) {
    d.interface = s.substr(if_pos + 3);
    auto nondigit = d.interface.find_first_not_of("0123456789");
    if (nondigit != std::string::npos)
      d.interface = d.interface.substr(0, nondigit);
    s = s.substr(0, if_pos);
  }

  // Manufacturer
  auto first = s.find('_');
  auto last = s.rfind('_');

  if (first != std::string::npos && first != last) {
    d.manufacturer = s.substr(0, first);
    d.serial_number = s.substr(last + 1);
    d.product = s.substr(first + 1, last - first - 1);
  } else if (first != std::string::npos) {
    d.manufacturer = s.substr(0, first);
    d.product = s.substr(first + 1);
  } else {
    d.manufacturer = s;
  }

  // mcu type
  if (d.firmware == FirmwareState::Klipper ||
      d.firmware == FirmwareState::Katapult) {
    d.mcu_type = extractMcuType(d.product);
  }

  if (!d.mcu_type.empty())
    d.name = d.manufacturer + " " + d.mcu_type;
  else if (!d.product.empty())
    d.name = d.manufacturer + " " + d.product;
  else
    d.name = d.manufacturer;

  return d;
}

std::vector<Device> SerialScanner::scanImpl() {
  std::vector<Device> devices;

  if (!fs::exists(SERIAL_BY_ID_PATH)) {
    std::cerr << "[SerialScanner] " << SERIAL_BY_ID_PATH
              << " not found - no serial devices connected. \n";
    return devices;
  }

  for (const auto &entry : fs::directory_iterator(SERIAL_BY_ID_PATH)) {
    try {
      std::string symlink = entry.path().filename().string();
      std::string resolved = fs::canonical(entry.path()).string();
      devices.push_back(parseSymlink(symlink, resolved));

    } catch (const fs::filesystem_error &e) {
      std::cerr << "[SerialScanner] Skipping entry: " << e.what() << "\n";
    }
  }
  return devices;
}

std::vector<Device> SerialScanner::scanKlipper() {

  auto all = scan();
  std::vector<Device> result;
  std::copy_if(
      all.begin(), all.end(), std::back_inserter(result),
      [](const Device &d) { return d.firmware == FirmwareState::Klipper; });
  return result;
}

std::vector<Device> SerialScanner::scanKatapult() {
  auto all = scan();
  std::vector<Device> result;
  std::copy_if(
      all.begin(), all.end(), std::back_inserter(result),
      [](const Device &d) { return d.firmware == FirmwareState::Katapult; });
  return result;
}

std::vector<Device> SerialScanner::scanUnflashed() {
  auto all = scan();
  std::vector<Device> result;
  std::copy_if(all.begin(), all.end(), std::back_inserter(result),
               [](const Device &d) {
                 return d.firmware == FirmwareState::Unflashed ||
                        d.firmware == FirmwareState::Unknown;
               });
  return result;
}

// std::vector<Device> SerialScanner::scanUsb() {
//   std::vector<Device> result;
//   libusb_context *ctx = nullptr;
//
//   libusb_init_context(&ctx);
//   lisbusb_device **list;
//   ssize_t count = libusb_get_device_list(ctx, &list);
//   std::count << count << std::endl;
//   libusb_free_device_list(list, 1);
//   libusb_exit(ctx);
// }
