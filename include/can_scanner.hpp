#pragma once
#include "device.hpp"
#include "scanner.hpp"
#include <cstdint>
#include <string>
#include <vector>

class CANScanner : public Scanner<CANScanner> {
  friend class Scanner<CANScanner>;

public:
  CANScanner() = default;
  ~CANScanner() = default;

  std::vector<std::string> getInterfaces();
  std::vector<Device> uuid_query(const std::string &interface = "can0");
  std::string vendor_query(uint8_t uuid);

private:
  std::vector<Device> scanImpl(const std::string &interface = "can0");
  static const std::string NET_CLASS_PATH;
};
