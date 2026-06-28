#pragma once

enum class DeviceModeId {
    ApMaster,
    StaSlave,
    StaHome
};

class DeviceMode {
public:
    virtual ~DeviceMode() = default;
    virtual void onEnter() = 0;
    virtual void loop()    = 0;
    virtual const char* name() const = 0;
};
