#include "usb_scanner.hpp"
#include "device.hpp"
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <libusb-1.0/libusb.h>
#include <sstream>
#include <string>
#include <sys/types.h>

namespace fs = std::filesystem;

static constexpr uint8_t USB_CLASS_DEVICE = 0x00;
static constexpr uint8_t USB_CLASS_AUDIO = 0x01;
static constexpr uint8_t USB_CLASS_CDC =
    0x02; // Beacon probe will fall under this
static constexpr uint8_t USB_CLASS_HID = 0x03;
static constexpr uint8_t USB_CLASS_IMAGE =
    0x06; // still imaging, digital cameras and scanners
static constexpr uint8_t USB_CLASS_MASS_STORAGE = 0x08;
static constexpr uint8_t USB_CLASS_HUB = 0x09;
static constexpr uint8_t USB_CLASS_VIDEO = 0x0E;
static constexpr uint8_t USB_CLASS_MISC = 0xEF;
static constexpr uint8_t USB_CLASS_VENDOR = 0xFF;

// libusb context Constructor
USBScanner::USBScanner() {
  int r = libusb_init(&ctx_);
  if (r < 0) {
    std::cerr << "[USBScanner] libusb_init failed: " << libusb_error_name(r)
              << "\n";
    ctx_ = nullptr;
  }
}

// libusb context Destructor
USBScanner::~USBScanner() {
  if (ctx_)
    libusb_exit(ctx_);
}

std::string USBScanner::resolveVideoPath(int vendor_id, int product_id) const {
  const std::string v4l_path = "/sys/class/video4linux";
  if (!fs::exists(v4l_path))
    return "";

  std::ostringstream vid_ss, pid_ss;

  vid_ss << std::hex << std::setw(4) << std::setfill('0') << vendor_id;
  pid_ss << std::hex << std::setw(4) << std::setfill('0') << product_id;

  std::string vid_str = vid_ss.str();
  std::string pid_str = pid_ss.str();

  for (const auto &entry : fs::directory_iterator(v4l_path)) {
    fs::path vendor_file = entry.path() / "device" / "idVendor";
    fs::path product_file = entry.path() / "device" / "idProduct";

    if (!fs::exists(vendor_file))
      continue;

    auto read_file = [](const fs::path &p) -> std::string {
      std::ifstream f(p);
      std::string s;
      std::getline(f, s);
      while (!s.empty() &&
             (s.back() == '\n' || s.back() == '\r' || s.back() == ' '))
        s.pop_back();
      return s;
    };

    std::string vid = read_file(vendor_file);
    std::string pid = read_file(product_file);

    if (vid == vid_str && pid == pid_str) {
      return "/dev/" + entry.path().filename().string();
    }
  }
  return ""; // No v4l subsystem
}

std::string USBScanner::readStringDescriptor(libusb_device *dev,
                                             uint8_t index) const {
  if (index == 0)
    return "";
  libusb_device_handle *handle = nullptr;
  if (libusb_open(dev, &handle) != LIBUSB_SUCCESS)
    return "";

  unsigned char buf[256] = {};
  int r = libusb_get_string_descriptor_ascii(handle, index, buf, sizeof(buf));
  libusb_close(handle);
  if (r < 0)
    return "";
  return std::string(reinterpret_cast<char *>(buf));
}

USBClass USBScanner::classifyDevice(libusb_device *dev) const {

  libusb_config_descriptor *config = nullptr;

  if (libusb_get_active_config_descriptor(dev, &config) != LIBUSB_SUCCESS)
    return USBClass::Unknown;

  USBClass result = USBClass::Unknown;

  for (int i = 0; i < config->bNumInterfaces; i++) {
    const libusb_interface &iface = config->interface[i];
    for (int j = 0; j < iface.num_altsetting; j++) {
      uint8_t cls = iface.altsetting[j].bInterfaceClass;
      switch (cls) {
      case USB_CLASS_VIDEO:
        result = USBClass::Video;
        libusb_free_config_descriptor(config);
        return result;
      case USB_CLASS_CDC:
        result = USBClass::CDCSerial;
        break;
      case USB_CLASS_AUDIO:
        if (result == USBClass::Unknown)
          result = USBClass::Audio;
        break;
      case USB_CLASS_HID:
        if (result == USBClass::Unknown)
          result = USBClass::HID;
        break;
      case USB_CLASS_MASS_STORAGE:
        if (result == USBClass::Unknown)
          result = USBClass::MassStorage;
        break;
      case USB_CLASS_HUB:
        if (result == USBClass::Unknown)
          result = USBClass::Hub;
        break;
      case USB_CLASS_VENDOR:
        if (result == USBClass::Unknown)
          result = USBClass::VendorSpecific;
        break;
      default:
        break;
      };
    }
  }
  libusb_free_config_descriptor(config);
  return result;
}

Device USBScanner::buildDevice(libusb_device *dev) const {
  Device d;
  d.connection = ConnectionType::USB;
  d.firmware = FirmwareState::Unknown;

  libusb_device_descriptor desc;

  if (libusb_get_device_descriptor(dev, &desc) != LIBUSB_SUCCESS)
    return d;

  d.vendor_id = desc.idVendor;
  d.product_id = desc.idProduct;
  d.usb_bus_number = libusb_get_bus_number(dev);
  d.usb_device_address = libusb_get_device_address(dev);
  d.usb_class = classifyDevice(dev);

  std::ostringstream oss;
  oss << "USB 0x" << std::hex << std::setw(4) << std::setfill('0')
      << desc.idVendor << ":0x" << std::setw(4) << desc.idProduct;
  d.name = oss.str();

  d.manufacturer = readStringDescriptor(dev, desc.iManufacturer);
  d.product = readStringDescriptor(dev, desc.iProduct);
  d.serial_number = readStringDescriptor(dev, desc.iSerialNumber);

  if (!d.manufacturer.empty() && !d.product.empty())
    d.name = d.manufacturer + " " + d.product;
  else if (!d.product.empty())
    d.name = d.product;
  else if (!d.manufacturer.empty())
    d.name = d.manufacturer;

  if (d.usb_class == USBClass::Video) {
    d.video_path = resolveVideoPath(d.vendor_id, d.product_id);
  }
  return d;
}

std::vector<Device> USBScanner::scanImpl() {

  std::vector<Device> devices;

  if (!ctx_) {
    std::cerr << "[USBScanner] libusb not initialised.\n";
    return devices;
  }

  libusb_device **list = nullptr;
  ssize_t count = libusb_get_device_list(ctx_, &list);

  if (count < 0) {
    std::cerr << "[USBScanner] libusb_get_device_list failed: "
              << libusb_error_name(static_cast<int>(count)) << "\n";
    return devices;
  }

  for (ssize_t i = 0; i < count; i++) {
    Device d = buildDevice(list[i]);
    if (d.usb_class != USBClass::Hub)
      devices.push_back(std::move(d));
  }

  libusb_free_device_list(list, 1);
  return devices;
}

std::vector<Device> USBScanner::scanCameras() {
  auto all = scan();
  std::vector<Device> result;
  std::copy_if(all.begin(), all.end(), std::back_inserter(result),
               [](const Device &d) { return d.usb_class == USBClass::Video; });
  return result;
}

std::vector<Device> USBScanner::scanSerialAdapters() {

  auto all = scan();
  std::vector<Device> result;
  std::copy_if(all.begin(), all.end(), std::back_inserter(result),
               [](const Device &d) {
                 return d.usb_class == USBClass::CDCSerial ||
                        d.usb_class == USBClass::VendorSpecific;
               });
  return result;
}

std::string USBScanner::findVideoPath(int vendor_id, int product_id) {
  return resolveVideoPath(vendor_id, product_id);
}
