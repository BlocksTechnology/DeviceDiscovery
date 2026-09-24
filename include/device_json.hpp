#pragma once
#include "device.hpp"
#include <nlohmann/json_fwd.hpp>

// Kept separate from device.hpp on purpose: device.hpp is included by the
// pybind11 module, which has no need to drag a JSON dependency in with it.
nlohmann::json deviceToJson(const Device &d);
