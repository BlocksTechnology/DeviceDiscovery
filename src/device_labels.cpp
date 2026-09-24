#include "device_labels.hpp"

namespace device_labels {

std::string firmware(FirmwareState fw) {
  switch (fw) {
  case FirmwareState::Klipper:
    return "Klipper";
  case FirmwareState::Katapult:
    return "Katapult";
  case FirmwareState::Unflashed:
    return "Unflashed";
  default:
    return "Unknown";
  }
}

std::string connection(ConnectionType type) {
  switch (type) {
  case ConnectionType::Serial:
    return "Serial";
  case ConnectionType::CAN:
    return "CAN";
  case ConnectionType::USB:
    return "USB";
  default:
    return "Unknown";
  }
}

std::string usbClass(USBClass cls) {
  switch (cls) {
  case USBClass::Video:
    return "Video";
  case USBClass::Audio:
    return "Audio";
  case USBClass::HID:
    return "HID";
  case USBClass::MassStorage:
    return "MassStorage";
  case USBClass::CDCSerial:
    return "CDCSerial";
  case USBClass::Hub:
    return "Hub";
  case USBClass::VendorSpecific:
    return "VendorSpecific";
  default:
    return "Unknown";
  }
}

} // namespace device_labels
