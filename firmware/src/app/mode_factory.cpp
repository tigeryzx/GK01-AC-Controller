#include "app/mode_factory.h"
#include "modes/ap_master_mode.h"
#include "modes/sta_slave_mode.h"
#include "modes/sta_home_mode.h"
#include <memory>

std::unique_ptr<DeviceMode> createMode(DeviceModeId id) {
    switch (id) {
        case DeviceModeId::ApMaster: return std::unique_ptr<DeviceMode>(new ApMasterMode());
        case DeviceModeId::StaSlave: return std::unique_ptr<DeviceMode>(new StaSlaveMode());
        case DeviceModeId::StaHome:  return std::unique_ptr<DeviceMode>(new StaHomeMode());
    }
    return nullptr;
}
