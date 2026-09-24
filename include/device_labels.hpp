#pragma once
#include "device.hpp"
#include <string>

// Canonical string labels for Device enums. Single source of truth shared
// by the CLI (main.cpp) and the daemon's JSON reports (device_json.cpp) so
// the two can never disagree on what a given enum value is called.
namespace device_labels {

std::string firmware(FirmwareState fw);
std::string connection(ConnectionType type);
std::string usbClass(USBClass cls);

} // namespace device_labels
