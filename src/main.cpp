#include "device.hpp"
#include "device_labels.hpp"
#include "serial_scanner.hpp"
#include "usb_scanner.hpp"
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

static std::string hexId(int id) {
  std::ostringstream ss;
  ss << "0x" << std::hex << std::setw(4) << std::setfill('0') << id;
  return ss.str();
}

std::vector<Device> serial_scan();

int main() {
  std::cout << "━━━━━━ Discovery tool ━━━━━━ \n";
  SerialScanner serial;
  auto devices = serial.scan();

  USBScanner uscanner;
  auto usbDevices = uscanner.scan();

  if (devices.empty()) {
    std::cout << "No serial devices found. \n";
    // return 0;
  }
  if (usbDevices.empty()) {
    std::cout << "No USB devices found. \n";
    // return 0;
  }

  for (const auto &d : devices) {
    std::cout << "\n";
    std::cout << "Symlink : " << d.symlink_name << " [" << device_labels::firmware(d.firmware)
               << "] -> " << d.device_path << "\n";
  }
  for (const auto &d : usbDevices) {
    std::cout << "\n";
    std::cout << "USB : " << d.name << " (" << hexId(d.vendor_id) << ":" << hexId(d.product_id)
               << ") [" << device_labels::usbClass(d.usb_class) << "]\n";
  }

  auto camd = uscanner.scanCameras();
  for (const auto &d : camd) {
    std::cout << "\n" << "USB camera : " << d.name << "\n";
  }

  auto susb = uscanner.scanSerialAdapters();
  for (const auto &d : susb) {
    std::cout << "\n";
    std::cout << "Serial Scan Adapters : " << d.name << "\n";
  }

  return 0;
}
