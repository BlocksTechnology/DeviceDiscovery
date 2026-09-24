#pragma once
#include <string>

enum class FirmwareState { Klipper, Katapult, Unflashed, Unknown };

enum class ConnectionType { Serial, CAN, USB, Unknown };

enum class Manufacturers {Klipper, Beacon, Katapult};

enum class USBClass {
  Video,          // 0x0E
  Audio,          // 0x01
  HID,            // 0x03
  MassStorage,    // 0x08
  CDCSerial,      // 0x02
  Hub,            // 0x09
  VendorSpecific, // 0xFF
  Unknown
};

struct Device {
  std::string name;
  std::string manufacturer;
  std::string product;
  std::string serial_number;

  std::string symlink_name;
  std::string device_path;
  std::string mcu_type;
  std::string interface;
  std::string video_path;

  std::string can_uuid;
  std::string can_interface;

  ConnectionType connection = ConnectionType::Unknown;
  FirmwareState firmware = FirmwareState::Unknown;
  USBClass usb_class = USBClass::Unknown;

  int vendor_id = 0;
  int product_id = 0;

  // Identify a physical USB port, not just a device model - two identical
  // VID:PID adapters with no serial number are otherwise indistinguishable.
  // Unique among concurrently-attached devices (not stable across replugs).
  int usb_bus_number = 0;
  int usb_device_address = 0;

  bool is_klipper = false;
  bool is_katapult = false;
};
