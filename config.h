#pragma once
#include <Arduino.h>

#ifndef WIFI_SSID
#error WIFI_SSID is not defined
#endif
#ifndef WIFI_PASSWORD
#error WIFI_PASSWORD is not defined
#endif
#ifndef SERVER_URL
#error SERVER_URL is not defined
#endif
#ifndef DEVICE_ID
#define DEVICE_ID "fall-detector-1"
#endif
#ifndef STREAM_ACCEL
#define STREAM_ACCEL 0
#endif

constexpr int PIN_SDA = 21;
constexpr int PIN_SCL = 22;
constexpr int PIN_MPU_INT = 27;
constexpr int PIN_BUTTON = 25;
constexpr int PIN_BUZZER = 26;
constexpr int PIN_LED = 2;

constexpr uint8_t MPU_ADDR = 0x68;

constexpr float FREE_FALL_G = 0.6f;
constexpr uint8_t FREE_FALL_MIN_SAMPLES = 3;
constexpr float IMPACT_G = 2.0f;
constexpr uint32_t IMPACT_WINDOW_MS = 600;
constexpr uint32_t PEAK_TRACK_MS = 150;
constexpr uint32_t SETTLE_MS = 1000;
constexpr uint32_t STILLNESS_MS = 3000;
constexpr float MOVE_DEV_G = 0.25f;

constexpr uint32_t CANCEL_WINDOW_MS = 10000;
constexpr uint32_t COOLDOWN_MS = 5000;
constexpr uint32_t BUTTON_DEBOUNCE_MS = 200;
constexpr uint32_t SENSOR_TIMEOUT_MS = 50;
constexpr uint8_t I2C_MAX_ERRORS = 20;

constexpr uint32_t HEARTBEAT_MS = 60000;
constexpr uint32_t WIFI_KICK_MS = 15000;
constexpr uint32_t MAX_RETRY_MS = 30000;
