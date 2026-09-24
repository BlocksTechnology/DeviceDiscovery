#pragma once
#include "device.hpp"
#include "scanner.hpp"
#include <cstdint>
#include <string>
#include <vector>

struct libusb_context;
struct libusb_device;

class USBScanner : public Scanner<USBScanner> {
  friend class Scanner<USBScanner>;

public:
  USBScanner();
  ~USBScanner();

  std::vector<Device> scanCameras();
  std::vector<Device> scanSerialAdapters();
  std::string findVideoPath(int vendor_id, int product_id);
  bool inline isAvailable() const { return ctx_ != nullptr; }

private:
  libusb_context *ctx_ = nullptr;
  std::vector<Device> scanImpl();
  Device buildDevice(libusb_device *dev) const;
  USBClass classifyDevice(libusb_device *dev) const;
  std::string resolveVideoPath(int vendor_id, int product_id) const;
  std::string readStringDescriptor(libusb_device *dev, uint8_t index) const;
};
