#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <esp_task_wdt.h>
#include <esp_timer.h>

#include "config.h"
#include "mpu6050.h"
#include "fall_detector.h"

enum AlarmEventType : uint8_t { ALARM_EVT_FALL, ALARM_EVT_BUTTON };

struct AlarmEvent {
  AlarmEventType type;
  float peakG;
};

enum NetEventType : uint8_t { NET_FALL_ALERT, NET_FALL_CANCELLED, NET_SOS, NET_HEARTBEAT };

struct NetEvent {
  NetEventType type;
  float peakG;
  uint32_t timestampMs;
};

static MPU6050 mpu;
static QueueHandle_t alarmQueue;
static QueueHandle_t netQueue;
static TaskHandle_t sensorTaskHandle = nullptr;
static volatile bool alarmActive = false;
static volatile uint32_t lastButtonUs = 0;

static const char* netEventName(NetEventType t) {
  switch (t) {
    case NET_FALL_ALERT: return "fall_alert";
    case NET_FALL_CANCELLED: return "fall_cancelled";
    case NET_SOS: return "sos";
    default: return "heartbeat";
  }
}

void IRAM_ATTR onMpuInterrupt() {
  if (sensorTaskHandle == nullptr) return;
  BaseType_t woken = pdFALSE;
  vTaskNotifyGiveFromISR(sensorTaskHandle, &woken);
  if (woken) portYIELD_FROM_ISR();
}

void IRAM_ATTR onButtonInterrupt() {
  uint32_t now = (uint32_t)esp_timer_get_time();
  if (now - lastButtonUs < BUTTON_DEBOUNCE_MS * 1000UL) return;
  lastButtonUs = now;

  AlarmEvent evt = {ALARM_EVT_BUTTON, 0.0f};
  BaseType_t woken = pdFALSE;
  xQueueSendFromISR(alarmQueue, &evt, &woken);
  if (woken) portYIELD_FROM_ISR();
}

static void pushNetEvent(NetEventType type, float peakG) {
  NetEvent evt = {type, peakG, millis()};
  if (xQueueSend(netQueue, &evt, 0) != pdTRUE) {
    Serial.println("Net queue full, event dropped");
  }
}

static void setAlarmOutputs(bool on) {
  digitalWrite(PIN_BUZZER, on ? HIGH : LOW);
  digitalWrite(PIN_LED, on ? HIGH : LOW);
}

static void beep(uint8_t times, uint32_t onMs, uint32_t offMs) {
  for (uint8_t i = 0; i < times; i++) {
    setAlarmOutputs(true);
    vTaskDelay(pdMS_TO_TICKS(onMs));
    setAlarmOutputs(false);
    vTaskDelay(pdMS_TO_TICKS(offMs));
  }
}

static void sensorTask(void*) {
  esp_task_wdt_add(NULL);

  FallDetector detector;
  FallDetector::State lastState = FallDetector::IDLE;
  uint8_t i2cErrors = 0;

  for (;;) {
    esp_task_wdt_reset();
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(SENSOR_TIMEOUT_MS));

    AccelRaw raw;
    if (!mpu.readAccel(raw)) {
      if (++i2cErrors >= I2C_MAX_ERRORS) {
        Serial.println("I2C failing, reinitialising MPU6050");
        mpu.begin(Wire, MPU_ADDR);
        i2cErrors = 0;
      }
      continue;
    }
    i2cErrors = 0;

    float ax = raw.x / MPU_LSB_PER_G;
    float ay = raw.y / MPU_LSB_PER_G;
    float az = raw.z / MPU_LSB_PER_G;
    float g = sqrtf(ax * ax + ay * ay + az * az);

#if STREAM_ACCEL
    Serial.printf("%lu,%.3f,%.3f,%.3f,%.3f\n", (unsigned long)millis(), ax, ay, az, g);
#endif

    if (alarmActive) {
      detector.reset();
      lastState = FallDetector::IDLE;
      continue;
    }

    bool fallConfirmed = detector.update(g, millis());

    if (detector.state() != lastState) {
      lastState = detector.state();
      Serial.printf("State -> %s\n", FallDetector::stateName(lastState));
    }

    if (fallConfirmed) {
      Serial.printf("Fall confirmed, peak %.2f g\n", detector.peakG());
      alarmActive = true;
      AlarmEvent evt = {ALARM_EVT_FALL, detector.peakG()};
      xQueueSend(alarmQueue, &evt, 0);
    }
  }
}

static void alarmTask(void*) {
  esp_task_wdt_add(NULL);

  AlarmEvent evt;

  for (;;) {
    esp_task_wdt_reset();

    if (xQueueReceive(alarmQueue, &evt, pdMS_TO_TICKS(500)) != pdTRUE) continue;

    if (evt.type == ALARM_EVT_BUTTON) {
      Serial.println("Manual SOS");
      pushNetEvent(NET_SOS, 0.0f);
      beep(2, 100, 100);
      continue;
    }

    Serial.println("Fall alarm started, press button to cancel");
    bool cancelled = false;
    uint32_t start = millis();

    while (millis() - start < CANCEL_WINDOW_MS) {
      esp_task_wdt_reset();
      bool phase = ((millis() - start) / 250) % 2 == 0;
      setAlarmOutputs(phase);

      AlarmEvent inner;
      if (xQueueReceive(alarmQueue, &inner, pdMS_TO_TICKS(50)) == pdTRUE &&
          inner.type == ALARM_EVT_BUTTON) {
        cancelled = true;
        break;
      }
    }
    setAlarmOutputs(false);

    if (cancelled) {
      Serial.println("Alarm cancelled by user");
      pushNetEvent(NET_FALL_CANCELLED, evt.peakG);
      beep(1, 150, 0);
    } else {
      Serial.println("Alert sent");
      pushNetEvent(NET_FALL_ALERT, evt.peakG);
      beep(3, 150, 100);
    }

    vTaskDelay(pdMS_TO_TICKS(COOLDOWN_MS));
    xQueueReset(alarmQueue);
    alarmActive = false;
  }
}

static bool ensureWifi() {
  static uint32_t lastKick = 0;

  if (WiFi.status() == WL_CONNECTED) return true;

  uint32_t now = millis();
  if (now - lastKick >= WIFI_KICK_MS) {
    lastKick = now;
    Serial.println("WiFi down, reconnecting");
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }
  return false;
}

static bool sendEvent(const NetEvent& evt) {
  if (!ensureWifi()) return false;

  char body[256];
  snprintf(body, sizeof(body),
           "{\"deviceId\":\"%s\",\"event\":\"%s\",\"peakG\":%.2f,"
           "\"uptimeMs\":%lu,\"eventAgeMs\":%lu,\"rssi\":%d}",
           DEVICE_ID, netEventName(evt.type), evt.peakG,
           (unsigned long)millis(), (unsigned long)(millis() - evt.timestampMs),
           WiFi.RSSI());

  HTTPClient http;
  http.setConnectTimeout(3000);
  http.setTimeout(3000);
  if (!http.begin(SERVER_URL)) return false;
  http.addHeader("Content-Type", "application/json");

  int code = http.POST((uint8_t*)body, strlen(body));
  http.end();

  Serial.printf("POST %s -> %d\n", netEventName(evt.type), code);
  return code >= 200 && code < 300;
}

static void netTask(void*) {
  NetEvent pending;
  bool hasPending = false;
  uint32_t lastHeartbeat = millis();
  uint32_t retryDelayMs = 1000;

  for (;;) {
    if (!hasPending) {
      if (xQueueReceive(netQueue, &pending, pdMS_TO_TICKS(1000)) == pdTRUE) {
        hasPending = true;
      } else if (millis() - lastHeartbeat >= HEARTBEAT_MS) {
        pending = {NET_HEARTBEAT, 0.0f, millis()};
        hasPending = true;
        lastHeartbeat = millis();
      }
    }

    if (!hasPending) continue;

    if (sendEvent(pending)) {
      hasPending = false;
      retryDelayMs = 1000;
    } else if (pending.type == NET_HEARTBEAT) {
      hasPending = false;
    } else {
      Serial.printf("Send failed, retrying in %lu ms\n", (unsigned long)retryDelayMs);
      vTaskDelay(pdMS_TO_TICKS(retryDelayMs));
      retryDelayMs = min<uint32_t>(retryDelayMs * 2, MAX_RETRY_MS);
    }
  }
}

void setup() {
  setCpuFrequencyMhz(80);
  Serial.begin(115200);
  delay(200);

  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  pinMode(PIN_MPU_INT, INPUT);
  setAlarmOutputs(false);

  alarmQueue = xQueueCreate(8, sizeof(AlarmEvent));
  netQueue = xQueueCreate(10, sizeof(NetEvent));

  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(400000);

  if (!mpu.begin(Wire, MPU_ADDR)) {
    Serial.println("MPU6050 not responding, check wiring. Restarting in 5 s");
    delay(5000);
    ESP.restart();
  }
  Serial.printf("MPU6050 ready, WHO_AM_I = 0x%02X\n", mpu.whoAmI());

  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  xTaskCreatePinnedToCore(sensorTask, "sensor", 4096, NULL, 3, &sensorTaskHandle, 1);
  xTaskCreatePinnedToCore(alarmTask, "alarm", 4096, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(netTask, "net", 8192, NULL, 1, NULL, 0);

  attachInterrupt(digitalPinToInterrupt(PIN_MPU_INT), onMpuInterrupt, RISING);
  attachInterrupt(digitalPinToInterrupt(PIN_BUTTON), onButtonInterrupt, FALLING);

  pushNetEvent(NET_HEARTBEAT, 0.0f);
  Serial.println("Fall detector running");
}

void loop() {
  vTaskDelete(NULL);
}
