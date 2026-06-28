#pragma once

#include "modes/device_mode.h"
#include <memory>

std::unique_ptr<DeviceMode> createMode(DeviceModeId id);
