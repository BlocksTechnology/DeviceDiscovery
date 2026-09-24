#pragma once
#include "device.hpp"
#include "scanner.hpp"
#include <string>
#include <vector>

class SerialScanner : public Scanner<SerialScanner> {
  friend class Scanner<SerialScanner>;

public:
  SerialScanner() = default;
  ~SerialScanner() = default;

  std::vector<Device> scanKlipper();
  std::vector<Device> scanKatapult();
  std::vector<Device> scanUnflashed();

private:
  std::vector<Device> scanImpl();
  static const std::string SERIAL_BY_ID_PATH;
  Device parseSymlink(const std::string &symlinkName,
                      const std::string &resolvedPath) const;
  FirmwareState detectFirmware(const std::string &symlinkName) const;
  std::string extractMcuType(const std::string &product) const;
};
