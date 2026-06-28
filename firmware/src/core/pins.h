#pragma once

#include <Arduino.h>

// 严禁散布的数字 GPIO，必须从这里取

inline constexpr uint8_t PIN_IR_TX       = 14;  // D5 — 8x IR LED，低电平有效
inline constexpr uint8_t PIN_IR_RX       = 5;   // D1 — VS1838B
inline constexpr uint8_t PIN_SENSOR_TEMP = 2;   // D4 — DS18B20，启动脚必须保持高
inline constexpr uint8_t PIN_SENSOR_PIR  = 16;  // D0 — AM312

inline constexpr uint8_t PIN_LED_BLUE   = 4;   // D2 — 系统状态
inline constexpr uint8_t PIN_LED_RED    = 12;  // D6 — IR 活动
inline constexpr uint8_t PIN_LED_YELLOW = 13;  // D7 — 复用恢复出厂触点

// 板载 LED 阳极经限流电阻接 VCC，LOW=亮 HIGH=灭
inline constexpr uint8_t LED_LEVEL_ON  = LOW;
inline constexpr uint8_t LED_LEVEL_OFF = HIGH;

inline void ledOn(uint8_t pin)  { digitalWrite(pin, LED_LEVEL_ON); }
inline void ledOff(uint8_t pin) { digitalWrite(pin, LED_LEVEL_OFF); }
