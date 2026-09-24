cmake_minimum_required(VERSION 3.20)
project(DeviceDiscovery VERSION 0.0.1 LANGUAGES CXX)


set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(default_build_type "Debug")
  message(STATUS "Set the build type to `${default_build_type}` as none was specified.")
  set(CMAKE_BUILD_TYPE ${default_build_type} CACHE STRING "Chooce the build type." FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
  "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()
message(STATUS "DeviceDiscovery Build Type: ${CMAKE_BUILD_TYPE}")

# The pybind11 module is the slowest target to build; on-device updates
# (scripts/build.sh) only need the daemon, so they turn this off.
option(DD_BUILD_PYTHON "Build the DeviceDiscovery pybind11 module" ON)

if (DD_BUILD_PYTHON)
  find_package(pybind11 QUIET)
  if (NOT pybind11_FOUND)
    message(STATUS "pybind11 not found system-wide, fetching from GitHub...")
    include(FetchContent)
    FetchContent_Declare(
      pybind11
      GIT_REPOSITORY https://github.com/pybind/pybind11.git
      GIT_TAG        v3.1.0
    )
    FetchContent_MakeAvailable(pybind11)
  endif()
endif()

find_package(nlohmann_json QUIET)
if (NOT nlohmann_json_FOUND)
  message(STATUS "nlohmann_json not found system-wide, fetching from GitHub...")
  include(FetchContent)
  FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
  )
  FetchContent_MakeAvailable(nlohmann_json)
endif()

find_package(Threads REQUIRED)

# libusb / libudev
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUSB REQUIRED libusb-1.0)
pkg_check_modules(LIBUDEV REQUIRED libudev)

# Core cpp libraries
add_library(discovery_core STATIC
  src/serial_scanner.cpp
  src/usb_scanner.cpp
  src/device_labels.cpp
)
target_include_directories(discovery_core PUBLIC
  include/
  ${LIBUSB_INCLUDE_DIRS}
)

target_link_libraries(discovery_core PUBLIC ${LIBUSB_LIBRARIES})
set_target_properties(discovery_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Standalone daemon mode (mode 2): udev-driven background scanning +
# Unix-socket JSON reports, kept separate from discovery_core so the
# pybind11 module (mode 1) doesn't need libudev/nlohmann_json/pthread.
add_library(discovery_daemon STATIC
  src/device_monitor.cpp
  src/ipc_server.cpp
  src/device_json.cpp
)
target_include_directories(discovery_daemon PUBLIC
  include/
  ${LIBUDEV_INCLUDE_DIRS}
)
target_link_libraries(discovery_daemon PUBLIC
  discovery_core
  nlohmann_json::nlohmann_json
  Threads::Threads
  ${LIBUDEV_LIBRARIES}
)
set_target_properties(discovery_daemon PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_executable(device_discoveryd src/discoveryd_main.cpp)
target_link_libraries(device_discoveryd PRIVATE discovery_daemon)


# add_executable(device_discovery src/main.cpp)
# target_link_libraries(device_discovery PRIVATE discovery_core)
add_executable(device_discovery
  src/main.cpp
  src/serial_scanner.cpp
  src/usb_scanner.cpp
)

target_link_libraries(device_discovery PRIVATE discovery_core)

if (DD_BUILD_PYTHON)
  pybind11_add_module(DeviceDiscovery bindings/py_bindings.cpp)
  target_link_libraries(DeviceDiscovery PRIVATE discovery_core)

  # scikit-build-core installs into the wheel root
  install(TARGETS DeviceDiscovery DESTINATION .)
endif()

# Set the version for myProject
set(DeviceDiscovery_Version_Major 0)
set(DeviceDiscovery_Version_Minor 1)
set(DeviceDiscovery_Version_Patch 0)
set(DeviceDiscovery_Version_Status "-dev")
set(PROJECT_VERSION
  "${DeviceDiscovery_Version_Major}.${DeviceDiscovery_Version_Minor}.${DeviceDiscovery_Version_Patch}${DeviceDiscovery_Version_Status}"
)
message(STATUS "${PROJECT_NAME} version: ${PROJECT_VERSION}")

# target_include_directories(DeviceDiscovery PRIVATE include/)
# target_include_directories(DeviceDiscovery PRIVATE src/)
