#include <Arduino.h>
#include "common.h"
#if __has_include("config.h")
#include "config.h"
#else
#include "config.example.h"
#endif

static HardwareSerial rs485(2);
static portMUX_TYPE readingMux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool valid = false;
static volatile uint16_t angleTenths = 0;
static volatile uint32_t lastOkMs = 0;
static volatile uint32_t responseCount = 0;
static volatile uint32_t errorCount = 0;
static const char *volatile lastStatus = "starting";

static uint16_t modbusCrc16(const uint8_t *data, size_t length) {
  uint16_t crc = 0xffff;
  for (size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit) crc = (crc & 1) ? (crc >> 1) ^ 0xa001 : crc >> 1;
  }
  return crc;
}

const char *windDirectionName(uint16_t value) {
  static const char *const names[] = {"N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
                                      "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"};
  return names[((uint32_t(value) * 16 + 1800) / 3600) % 16];
}

WindReading windReading() {
  WindReading r;
  portENTER_CRITICAL(&readingMux);
  r.valid = valid;
  r.angleTenths = angleTenths;
  const uint32_t sampleMs = lastOkMs;
  r.responses = responseCount;
  r.errors = errorCount;
  r.status = lastStatus;
  portEXIT_CRITICAL(&readingMux);
  r.ageMs = r.valid ? millis() - sampleMs : 0;
  if (r.valid && r.ageMs > WIND_STALE_MS) { r.valid = false; r.status = "stale"; }
  return r;
}

static bool readDirection(uint16_t &value, const char *&status) {
  uint8_t request[8] = {WIND_SENSOR_ADDRESS, 0x03, 0x00, 0x00, 0x00, 0x01, 0, 0};
  const uint16_t requestCrc = modbusCrc16(request, 6);
  request[6] = requestCrc & 0xff;
  request[7] = requestCrc >> 8;
  while (rs485.available()) rs485.read();
  rs485.write(request, sizeof request);
  rs485.flush();

  uint8_t raw[32];
  size_t count = 0;
  const uint32_t started = millis();
  uint32_t lastByte = started;
  while (millis() - started < WIND_RESPONSE_TIMEOUT_MS && count < sizeof raw) {
    while (rs485.available() && count < sizeof raw) { raw[count++] = uint8_t(rs485.read()); lastByte = millis(); }
    if (count >= 7 && millis() - lastByte >= 8) break;
    delay(1);
  }
  if (!count) { status = "timeout"; return false; }
  for (size_t i = 0; i + 7 <= count; ++i) {
    if (raw[i] != WIND_SENSOR_ADDRESS || raw[i + 1] != 0x03 || raw[i + 2] != 0x02) continue;
    const uint16_t got = raw[i + 5] | (uint16_t(raw[i + 6]) << 8);
    if (modbusCrc16(raw + i, 5) != got) { status = "bad_crc"; return false; }
    const uint16_t measured = (uint16_t(raw[i + 3]) << 8) | raw[i + 4];
    if (measured > 3599) { status = "invalid_angle"; return false; }
    int corrected = int(measured) + WIND_ANGLE_OFFSET_DEG * 10;
    corrected %= 3600;
    if (corrected < 0) corrected += 3600;
    value = uint16_t(corrected);
    status = "ok";
    return true;
  }
  status = "bad_frame";
  return false;
}

void windTask(void *) {
  rs485.begin(WIND_SENSOR_BAUD, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);
  logLine("SENSOR", "ES-WS-04 reader started");
  TickType_t nextRead = xTaskGetTickCount();
  for (;;) {
    uint16_t value = 0;
    const char *status = "unknown";
    const bool ok = readDirection(value, status);
    portENTER_CRITICAL(&readingMux);
    lastStatus = status;
    if (ok) {
      valid = true;
      angleTenths = value;
      lastOkMs = millis();
      ++responseCount;
    } else {
      ++errorCount;
      if (millis() - lastOkMs > WIND_STALE_MS) valid = false;
    }
    const uint32_t responses = responseCount, errors = errorCount;
    portEXIT_CRITICAL(&readingMux);
    if (ok) {
      logLine("SENSOR", "ES-WS-04 angle=%u.%u deg direction=%s responses=%lu errors=%lu",
              value / 10, value % 10, windDirectionName(value), (unsigned long)responses, (unsigned long)errors);
    } else {
      logLine("SENSOR", "ES-WS-04 read failed: %s (responses=%lu errors=%lu)", status,
              (unsigned long)responses, (unsigned long)errors);
    }
    // Keep the starts of consecutive Modbus polls 100 ms apart when the
    // transaction itself completes within that period.
    vTaskDelayUntil(&nextRead, pdMS_TO_TICKS(WIND_READ_INTERVAL_MS));
  }
}
