// #include "can_scanner.hpp"
#include "device.hpp"
#include "serial_scanner.hpp"
#include "usb_scanner.hpp"
#include <iomanip>
#include <pybind11/detail/common.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <sstream>

namespace py = pybind11;

PYBIND11_MODULE(DeviceDiscovery, m) {

  m.doc() = "Device discovery: captures serial, usb and can devices connected "
            "to the device";

  py::enum_<FirmwareState>(m, "FirmwareState")
      .value("Klipper", FirmwareState::Klipper)
      .value("Katapult", FirmwareState::Katapult)
      .value("Unflashed", FirmwareState::Unflashed)
      .value("Unknown", FirmwareState::Unknown)
      .export_values();

  py::enum_<ConnectionType>(m, "ConnectionType")
      .value("Serial", ConnectionType::Serial)
      .value("CAN", ConnectionType::CAN)
      .value("USB", ConnectionType::USB)
      .value("Unknown", ConnectionType::Unknown)
      .export_values();

  py::enum_<USBClass>(m, "USBClass")
      .value("Video", USBClass::Video)
      .value("Audio", USBClass::Audio)
      .value("HID", USBClass::HID)
      .value("MassStorage", USBClass::MassStorage)
      .value("CDCSerial", USBClass::CDCSerial)
      .value("Hub", USBClass::Hub)
      .value("VendorSpecific", USBClass::VendorSpecific)
      .value("Unknown", USBClass::Unknown)
      .export_values();

  py::class_<Device>(m, "Device")
      .def(py::init<>())
      .def_readwrite("name", &Device::name)
      .def_readwrite("manufacturer", &Device::manufacturer)
      .def_readwrite("product", &Device::product)
      .def_readwrite("serial_number", &Device::serial_number)
      .def_readwrite("symlink_name", &Device::symlink_name)
      .def_readwrite("device_path", &Device::device_path)
      .def_readwrite("mcu_type", &Device::mcu_type)
      .def_readwrite("interface", &Device::interface)
      .def_readwrite("video_path", &Device::video_path)
      .def_readwrite("can_uuid", &Device::can_uuid)
      .def_readwrite("can_interface", &Device::can_interface)
      .def_readwrite("usb_bus_number", &Device::usb_bus_number)
      .def_readwrite("usb_device_address", &Device::usb_device_address)
      .def_property_readonly(
          "is_klipper",
          [](const Device &d) { return d.firmware == FirmwareState::Klipper; })
      .def_property_readonly(
          "is_katapult",
          [](const Device &d) { return d.firmware == FirmwareState::Katapult; })
      .def_property_readonly("is_unflashed",
                             [](const Device &d) {
                               return d.firmware == FirmwareState::Unflashed ||
                                      d.firmware == FirmwareState::Unknown;
                             })
      .def_property_readonly(
          "is_camera",
          [](const Device &d) { return d.usb_class == USBClass::Video; })

      .def_property_readonly("vid_hex",
                             [](const Device &d) -> std::string {
                               std::ostringstream ss;
                               ss << "0x" << std::hex << std::setw(4)
                                  << std::setfill('0') << d.vendor_id;
                               return ss.str();
                             })
      .def_property_readonly("pid_hex",
                             [](const Device &d) -> std::string {
                               std::ostringstream ss;
                               ss << "0x" << std::hex << std::setw(4)
                                  << std::setfill('0') << d.product_id;
                               return ss.str();
                             })

      .def("__repr__", [](const Device &d) {
        // Serial device repr
        if (d.connection == ConnectionType::Serial) {
          std::string fw;
          switch (d.firmware) {
          case FirmwareState::Klipper:
            fw = "Klipper";
            break;
          case FirmwareState::Katapult:
            fw = "Katapult";
            break;
          case FirmwareState::Unflashed:
            fw = "Unflashed";
            break;
          default:
            fw = "Unknown";
            break;
          }
          return "<Device [Serial/" + fw + "] " + d.name + " → " +
                 d.device_path + ">";
        }
        if (d.connection == ConnectionType::USB) {
          std::string cls;
          switch (d.usb_class) {
          case USBClass::Video:
            cls = "Camera";
            break;
          case USBClass::CDCSerial:
            cls = "Serial";
            break;
          case USBClass::Audio:
            cls = "Audio";
            break;
          case USBClass::MassStorage:
            cls = "Storage";
            break;
          default:
            cls = "USB";
            break;
          }
          std::string path = d.video_path.empty() ? "" : " → " +
          d.video_path; return "<Device [" + cls + "] " + d.name + path +
          ">";
        }
        return "<Device " + d.name + ">";
      });

  py::class_<SerialScanner>(m, "SerialScanner")
      .def(py::init<>())
      .def(
          "scan", [](SerialScanner &s) { return s.scan(); },
          "All serial devices - equivalent to ls /dev/serial/by-id/*")
      .def("scan_klipper", &SerialScanner::scanKlipper,
           "Scan only devices running klipper")
      .def("scan_katapult", &SerialScanner::scanKatapult,
           "Scan only devices running katapult")
      .def("scan_unflashed", &SerialScanner::scanUnflashed,
           "Scan only unflashed devices");

  py::class_<USBScanner>(m, "USBScanner")
      .def(py::init<>())
      .def(
          "scan", [](USBScanner &s) { return s.scan(); },
          "Scans all usb devices (excluding hubs)")
      .def("scan_cameras", &USBScanner::scanCameras, "Scan only UVC cameras")
      .def("scan_serial_adapters", &USBScanner::scanSerialAdapters, "Scan only usb adapters ( CH340, FTDI, CP210x...)")
      .def("find_video_path", &USBScanner::findVideoPath, py::arg("vendor_id"), py::arg("product_id"), "Get /dev/video* path for cameras by VID/PID")
      .def("is_available", &USBScanner::isAvailable, "Whether libusb initialised successfully");

  // py::class_<CANScanner>(m, "CANScanner")
  //     .def(py::init<>())
  //     .def("get_interfaces", &CANScanner::getInterfaces,
  //          "List available CAN interfaces: ['can0', 'can1', ...]")
  //     .def("scan", &CANScanner::scan, py::arg("interface") = "can0",
  //          "Scan CAN bus for uuids");
  
  m.def(
      "scan_serial", []() { return SerialScanner().scan(); },
      "All serial devices (equivalent to ls /dev/serial/by-id/*");

  m.def(
      "scan_klipper_serial", []() { return SerialScanner().scanKlipper(); },
      "Scan Klipper flashed boards on serial");

  m.def("scan_cameras", [](){
    return USBScanner().scanCameras();
    }, "All USB cameras - Populates video_path (/dev/video*)");

  m.def("scan_all_usb", []() {
    return USBScanner().scan();
      }, "All USB devices except hubs");

//   m.def("get_can_interfaces", []() {
//     return CANScanner().getInterfaces();
//       }, "List CAN interfaces (can0, can1, ...");
};
