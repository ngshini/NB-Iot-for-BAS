#include <Arduino.h>
#include "wind_speed_protocol.h"

// Same ATMC onboard RS485 connection as the working ES-WS-04 setup.
#ifndef RS485_RX_PIN
#define RS485_RX_PIN 16
#endif
#ifndef RS485_TX_PIN
#define RS485_TX_PIN 17
#endif
#ifndef RS485_DE_RE_PIN
#define RS485_DE_RE_PIN -1
#endif
#ifndef SENSOR_ADDRESS
#define SENSOR_ADDRESS 1
#endif

static const uint32_t BAUDS[] = {4800, 9600, 2400, 19200, 38400, 56000, 115200};
static HardwareSerial rs485(2);
static uint8_t baudIndex = 0, failures = 0;
static uint32_t responses = 0, errors = 0;

static void transmit(bool enabled) {
  if (RS485_DE_RE_PIN >= 0) {
    digitalWrite(RS485_DE_RE_PIN, enabled ? HIGH : LOW);
    delayMicroseconds(200);
  }
}
static void beginSensor() {
  rs485.end();
  delay(20);
  rs485.begin(BAUDS[baudIndex], SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);
  Serial.printf("[RS485] ES-WS-02 baud=%lu address=%u RX=%d TX=%d\n",
                (unsigned long)BAUDS[baudIndex], SENSOR_ADDRESS, RS485_RX_PIN, RS485_TX_PIN);
}
void setup() {
  Serial.begin(115200);
  delay(400);
  if (RS485_DE_RE_PIN >= 0) { pinMode(RS485_DE_RE_PIN, OUTPUT); transmit(false); }
  Serial.println("ES-WS-02 Wind Speed / Modbus RTU; register 0 / 10 = m/s");
  Serial.println("10-30 VDC: brown=positive black=GND yellow=A/+ blue=B/-");
  beginSensor();
}
void loop() {
  static uint32_t lastRead = 0;
  if (millis() - lastRead < 1000) { delay(1); return; }
  lastRead = millis();
  uint8_t request[8], raw[64], exception = 0;
  uint16_t tenths = 0;
  WindSpeed::request(SENSOR_ADDRESS, request);
  while (rs485.available()) rs485.read();
  // Quiet interval exceeds 3.5 characters even at 2400 baud.
  delay(16);
  transmit(true);
  rs485.write(request, sizeof(request));
  rs485.flush();
  transmit(false);
  size_t length = 0;
  const uint32_t started = millis();
  uint32_t lastByte = started;
  while (millis() - started < 350 && length < sizeof(raw)) {
    while (rs485.available() && length < sizeof(raw)) {
      raw[length++] = uint8_t(rs485.read());
      lastByte = millis();
    }
    if (length && millis() - lastByte >= 20) break;
    delay(1);
  }
  const WindSpeed::Result result = WindSpeed::decode(raw, length, SENSOR_ADDRESS, tenths, exception);
  if (result == WindSpeed::OK) {
    ++responses; failures = 0;
    Serial.printf("[ESWS02] {\"sensor\":\"ES-WS-02\",\"speed_mps\":%u.%u,\"unit\":\"m/s\",\"raw\":%u,",
                  tenths / 10, tenths % 10, tenths);
  } else {
    ++errors; ++failures;
    Serial.print("[ESWS02] {\"sensor\":\"ES-WS-02\",\"speed_mps\":null,\"unit\":\"m/s\",\"raw\":null,");
  }
  Serial.printf("\"status\":\"%s\",\"exception\":%u,\"baud\":%lu,\"address\":%u,\"responses\":%lu,\"errors\":%lu}\n",
                WindSpeed::status(result), exception, (unsigned long)BAUDS[baudIndex],
                SENSOR_ADDRESS, (unsigned long)responses, (unsigned long)errors);
  if (result != WindSpeed::OK && length) {
    Serial.print("[RS485] RX:");
    for (size_t i = 0; i < length; ++i) Serial.printf(" %02X", raw[i]);
    Serial.println();
  }
  if (failures >= 3) {
    failures = 0;
    baudIndex = (baudIndex + 1) % (sizeof(BAUDS) / sizeof(BAUDS[0]));
    beginSensor();
  }
}
