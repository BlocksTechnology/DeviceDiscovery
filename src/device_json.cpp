#include "device_json.hpp"
#include "device_labels.hpp"
#include <iomanip>
#include <nlohmann/json.hpp>
#include <sstream>

static std::string hexId(int id) {
  std::ostringstream ss;
  ss << "0x" << std::hex << std::setw(4) << std::setfill('0') << id;
  return ss.str();
}

nlohmann::json deviceToJson(const Device &d) {
  return nlohmann::json{
      {"name", d.name},
      {"manufacturer", d.manufacturer},
      {"product", d.product},
      {"serial_number", d.serial_number},
      {"symlink_name", d.symlink_name},
      {"device_path", d.device_path},
      {"mcu_type", d.mcu_type},
      {"interface", d.interface},
      {"video_path", d.video_path},
      {"can_uuid", d.can_uuid},
      {"can_interface", d.can_interface},
      {"connection", device_labels::connection(d.connection)},
      {"firmware", device_labels::firmware(d.firmware)},
      {"usb_class", device_labels::usbClass(d.usb_class)},
      {"vendor_id", d.vendor_id},
      {"product_id", d.product_id},
      {"usb_bus_number", d.usb_bus_number},
      {"usb_device_address", d.usb_device_address},
      {"vid_hex", hexId(d.vendor_id)},
      {"pid_hex", hexId(d.product_id)},
      {"is_klipper", d.firmware == FirmwareState::Klipper},
      {"is_katapult", d.firmware == FirmwareState::Katapult},
  };
}
