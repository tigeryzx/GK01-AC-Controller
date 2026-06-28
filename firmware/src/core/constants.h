#pragma once

#include <stdint.h>
#include <stddef.h>

// ===== AP 模式网络 =====
inline constexpr const char* AP_DEFAULT_SSID  = "IR-AC";
inline constexpr const char* AP_DEFAULT_PASS  = "12345678";
inline constexpr const char* AP_IP_STR        = "10.1.1.1";
inline constexpr const char* AP_BROADCAST_STR = "10.1.1.255";
inline constexpr uint16_t    UDP_PORT         = 8888;

// ===== 重连/心跳 =====
inline constexpr uint32_t WIFI_RECONNECT_INTERVAL_MS = 5000;
inline constexpr uint32_t MQTT_RECONNECT_INTERVAL_MS = 5000;
inline constexpr uint32_t HELLO_INTERVAL_MS          = 30000;
inline constexpr uint32_t SLAVE_OFFLINE_TIMEOUT_MS   = 30000;

inline constexpr uint32_t PAIRING_DURATION_MS = 60000;

inline constexpr uint32_t SENSOR_INTERVAL_MS  = 10000;
inline constexpr float    SENSOR_TEMP_INVALID = -100.0f;
inline constexpr float    SENSOR_TEMP_MAX     = 85.0f;

inline constexpr uint32_t CONFIG_SAVE_DELAY_MS = 1000;

inline constexpr uint32_t BTN_CHECK_INTERVAL_MS  = 20;
inline constexpr uint32_t BTN_SHORT_PRESS_MIN_MS = 50;
inline constexpr uint32_t BTN_SHORT_PRESS_MAX_MS = 1500;
inline constexpr uint32_t BTN_LONG_PRESS_MS      = 5000;

inline constexpr int HVAC_MIN_TEMP_DEFAULT = 16;
inline constexpr int HVAC_MAX_TEMP_DEFAULT = 30;

// ===== LittleFS 路径 =====
inline constexpr const char* FS_CONFIG_TXT = "/config.txt";
inline constexpr const char* FS_CONFIG_BAK = "/config.bak";
inline constexpr const char* FS_SLAVES_TXT = "/slaves.txt";
inline constexpr const char* FS_SLAVES_BAK = "/slaves.bak";
inline constexpr const char* FS_OTA_STATE  = "/ota_state.bin";
inline constexpr const char* FS_OTA_BOOTS  = "/ota_boots.txt";

inline constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 20000;
inline constexpr uint32_t REBOOT_DEFAULT_DELAY_MS = 500;
inline constexpr uint32_t OTA_CONFIRM_DELAY_MS    = 30000;
inline constexpr uint8_t  OTA_MAX_CRASH_BOOTS     = 3;

inline constexpr uint16_t MQTT_DEFAULT_PORT       = 1883;
inline constexpr size_t   MQTT_DISCOVERY_BUF_SIZE = 1024;

inline constexpr size_t MAX_SLAVES = 4;

// ===== 短按预设 =====
inline constexpr const char* OFFICE_PRESET_VENDOR = "GREE";
inline constexpr const char* OFFICE_PRESET_MODE   = "Cool";
inline constexpr const char* OFFICE_PRESET_FAN    = "Auto";
inline constexpr const char* OFFICE_PRESET_SWING  = "Off";
inline constexpr int         OFFICE_PRESET_TEMP   = 26;
