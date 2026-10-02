#include <Arduino.h>
#include <HardwareSerial.h>
#include <Preferences.h>
#include <stdarg.h>
#include "common.h"
#if __has_include("config.h")
#include "config.h"
#else
#include "config.example.h"
#endif
#include "tf03_settings.h"
#include "tf03_parser.h"
#include <driver/gpio.h>

volatile uint8_t gChannels = CH_WIFI;
volatile bool gPayloadMinimal = true;
volatile bool gPublishNow[2] = {false, false};
char gWifiSsid[33] = WIFI_SSID;
char gWifiPass[65] = WIFI_PASS;
volatile bool gWifiChanged = false, gWifiScanReq = false;
static HardwareSerial sensor(1);
static SemaphoreHandle_t logMutex;
static portMUX_TYPE sampleMux = portMUX_INITIALIZER_UNLOCKED;
static uint16_t distanceCm = 0, strength = 0;
static uint32_t sampleTime = 0;
static bool received = false, sensorEnabled = false;
static int activeRx = TF03_RX_PIN;

void logLine(const char *tag, const char *fmt, ...) {
  char line[512];
  va_list args;
  va_start(args, fmt);
  vsnprintf(line, sizeof line, fmt, args);
  va_end(args);
  if (logMutex) xSemaphoreTake(logMutex, portMAX_DELAY);
  Serial.printf("[%s] %s\n", tag, line);
  if (logMutex) xSemaphoreGive(logMutex);
}

size_t buildPayload(char *out, size_t len) {
  portENTER_CRITICAL(&sampleMux);
  uint16_t d = distanceCm, s = strength;
  uint32_t at = sampleTime;
  bool seen = received;
  portEXIT_CRITICAL(&sampleMux);
  uint32_t age = millis() - at;
  const char *status = !sensorEnabled ? "pin_not_configured" : !seen ? "no_data" :
                      age > TF03_STALE_MS ? "stale" :
                      (s < 40 || d == 0 || d >= TF03_OVER_RANGE_CM) ? "invalid" : "ok";
  char distance[24], ageText[24], strengthText[16];
  if (!strcmp(status, "ok")) snprintf(distance, sizeof distance, "%.2f", d / 100.0);
  else strcpy(distance, "null");
  if (seen) {
    snprintf(ageText, sizeof ageText, "%lu", (unsigned long)age);
    snprintf(strengthText, sizeof strengthText, "%u", s);
  } else { strcpy(ageText, "null"); strcpy(strengthText, "null"); }
  int n = snprintf(out, len,
    "{\"distance\":%s,\"unit\":\"m\",\"sensor\":\"TF03\",\"status\":\"%s\",\"strength\":%s,\"age_ms\":%s}",
    distance, status, strengthText, ageText);
  return n > 0 && size_t(n) < len ? size_t(n) : 0;
}

void setup() {
  Serial.begin(115200);
  logMutex = xSemaphoreCreateMutex();
  // Use the Wi-Fi credentials compiled in config.h for unattended startup.
  if (TF03_RX_PIN >= 0 && GPIO_IS_VALID_GPIO(TF03_RX_PIN) &&
      TF03_RX_PIN != 1 && TF03_RX_PIN != 3 && !(TF03_RX_PIN >= 6 && TF03_RX_PIN <= 11)) {
    sensor.setRxBufferSize(4096);
    sensor.begin(TF03_BAUD, SERIAL_8N1, TF03_RX_PIN, -1);
    // Receive only: do not assign a TX pin or send sensor configuration commands.
    sensorEnabled = true;
    logLine("TF03", "RX GPIO%d, %u baud", TF03_RX_PIN, TF03_BAUD);
  } else logLine("TF03", "Set TF03_RX_PIN in tf03_settings.h to the ATM GPIO connected to sensor TX.");
  logLine("SYS", "TF03 -> Wi-Fi -> MQTT; distance in metres, no simulated values");
  xTaskCreatePinnedToCore(wifiTask, "wifi", 16384, nullptr, 1, nullptr, 1);
}

void loop() {
  static Tf03Parser parser;
  static uint32_t probeAt = 0;
  uint32_t now = millis();
  if (sensorEnabled && now - probeAt > 3000 && (!received || now - sampleTime > 3000)) {
    sensor.end();
    activeRx = activeRx == TF03_RX_PIN ? TF03_ALT_RX_PIN : TF03_RX_PIN;
    parser = Tf03Parser();
    sensor.begin(TF03_BAUD, SERIAL_8N1, activeRx, -1);
    probeAt = now;
    logLine("TF03", "Listening RX GPIO%d (receive only)", activeRx);
  }
  // Bound work so diagnostics still run with continuous high-rate input.
  for (unsigned i = 0; sensorEnabled && i < 4096 && sensor.available(); ++i) {
    if (parser.feed(uint8_t(sensor.read()))) {
      portENTER_CRITICAL(&sampleMux);
      distanceCm = parser.distance;
      strength = parser.strength;
      sampleTime = millis();
      received = true;
      portEXIT_CRITICAL(&sampleMux);
    }
  }
  static uint32_t lastLog = 0;
  if (millis() - lastLog >= 2000) {
    lastLog = millis();
    char payload[256];
    buildPayload(payload, sizeof payload);
    logLine("TF03", "%s", payload);
  }
  delay(1);
}
