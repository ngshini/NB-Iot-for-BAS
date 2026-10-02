#include <Arduino.h>

// ATMC onboard RS485 transceiver wiring, confirmed by the earlier ES-WS-02
// project supplied with this board.
#ifndef RS485_RX_PIN
#define RS485_RX_PIN 16
#endif
#ifndef RS485_TX_PIN
#define RS485_TX_PIN 17
#endif
// -1 means the ATMC transceiver controls TX/RX direction automatically.
#ifndef RS485_DE_RE_PIN
#define RS485_DE_RE_PIN -1
#endif

#ifndef SENSOR_ADDRESS
#define SENSOR_ADDRESS 0x01
#endif
#ifndef ANGLE_OFFSET_DEG
#define ANGLE_OFFSET_DEG 0
#endif

static constexpr uint32_t USB_BAUD = 115200;
static constexpr uint32_t READ_INTERVAL_MS = 1000;
static constexpr uint32_t RESPONSE_TIMEOUT_MS = 350;
static constexpr uint8_t FAILURES_BEFORE_NEXT_BAUD = 3;
static constexpr uint32_t SENSOR_BAUDS[] = {9600, 4800, 2400};

HardwareSerial rs485(2);
static uint8_t baudIndex = 0;
static uint8_t consecutiveFailures = 0;
static uint32_t successfulReads = 0;
static uint32_t failedReads = 0;

static uint16_t modbusCrc16(const uint8_t *data, size_t length) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit)
      crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
  }
  return crc;
}

static void setTransmit(bool enabled) {
  if (RS485_DE_RE_PIN >= 0) {
    digitalWrite(RS485_DE_RE_PIN, enabled ? HIGH : LOW);
    delayMicroseconds(200);
  }
}

static void printHex(const char *prefix, const uint8_t *data, size_t length) {
  Serial.print(prefix);
  for (size_t i = 0; i < length; ++i) Serial.printf("%02X%s", data[i], i + 1 == length ? "" : " ");
  Serial.println();
}

static const char *directionName(uint16_t angleTenths) {
  static const char *names[] = {
    "N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
    "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"
  };
  return names[((uint32_t(angleTenths) * 16 + 1800) / 3600) % 16];
}

static void beginSensorBaud(uint32_t baud) {
  rs485.end();
  delay(20);
  rs485.begin(baud, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);
  while (rs485.available()) rs485.read();
  Serial.printf("[RS485] Thu baud %lu, 8N1, slave 0x%02X, RX=%d, TX=%d, DE/RE=%d\n",
                (unsigned long)baud, SENSOR_ADDRESS, RS485_RX_PIN, RS485_TX_PIN, RS485_DE_RE_PIN);
}

static constexpr uint8_t READ_OK = 0;
static constexpr uint8_t READ_TIMEOUT = 1;
static constexpr uint8_t READ_EXCEPTION = 2;
static constexpr uint8_t READ_BAD_FRAME = 3;
static constexpr uint8_t READ_BAD_CRC = 4;
static constexpr uint8_t READ_INVALID_ANGLE = 5;

static const char *resultText(uint8_t result) {
  switch (result) {
    case READ_OK: return "ok";
    case READ_TIMEOUT: return "timeout";
    case READ_EXCEPTION: return "modbus_exception";
    case READ_BAD_FRAME: return "bad_frame";
    case READ_BAD_CRC: return "bad_crc";
    case READ_INVALID_ANGLE: return "invalid_angle";
  }
  return "unknown";
}

static uint8_t readWindDirection(uint16_t &angleTenths, uint8_t *raw, size_t &rawLength) {
  uint8_t request[8] = {SENSOR_ADDRESS, 0x03, 0x00, 0x00, 0x00, 0x01, 0, 0};
  const uint16_t requestCrc = modbusCrc16(request, 6);
  request[6] = requestCrc & 0xFF;
  request[7] = requestCrc >> 8;

  while (rs485.available()) rs485.read();
  setTransmit(true);
  rs485.write(request, sizeof(request));
  rs485.flush();
  setTransmit(false);

  rawLength = 0;
  uint32_t started = millis();
  uint32_t lastByte = started;
  while (millis() - started < RESPONSE_TIMEOUT_MS && rawLength < 64) {
    while (rs485.available() && rawLength < 64) {
      raw[rawLength++] = uint8_t(rs485.read());
      lastByte = millis();
    }
    if (rawLength >= 7 && millis() - lastByte >= 8) break;
    delay(1);
  }
  if (!rawLength) return READ_TIMEOUT;

  // Search the receive buffer so the code also works with transceivers that echo
  // the 8-byte request before the 7-byte sensor response.
  for (size_t offset = 0; offset + 5 <= rawLength; ++offset) {
    if (raw[offset] != SENSOR_ADDRESS) continue;
    if (raw[offset + 1] == uint8_t(0x03 | 0x80) && offset + 5 <= rawLength) {
      const uint16_t receivedCrc = raw[offset + 3] | (uint16_t(raw[offset + 4]) << 8);
      if (modbusCrc16(raw + offset, 3) == receivedCrc) return READ_EXCEPTION;
    }
    if (raw[offset + 1] != 0x03 || raw[offset + 2] != 0x02 || offset + 7 > rawLength) continue;
    const uint16_t receivedCrc = raw[offset + 5] | (uint16_t(raw[offset + 6]) << 8);
    if (modbusCrc16(raw + offset, 5) != receivedCrc) return READ_BAD_CRC;
    // The product range is 0-359.9 degrees. The tested unit returns tenths
    // of a degree (for example 0x0DB8 = 3512 = 351.2 degrees), although the
    // manual's worked example incorrectly describes an unscaled integer.
    const uint16_t sensorAngleTenths = (uint16_t(raw[offset + 3]) << 8) | raw[offset + 4];
    if (sensorAngleTenths > 3599) return READ_INVALID_ANGLE;
    int corrected = int(sensorAngleTenths) + ANGLE_OFFSET_DEG * 10;
    corrected %= 3600;
    if (corrected < 0) corrected += 3600;
    angleTenths = uint16_t(corrected);
    return READ_OK;
  }
  return READ_BAD_FRAME;
}

static void printMeasurement(uint8_t result, uint16_t angleTenths, const uint8_t *raw, size_t rawLength) {
  const uint32_t baud = SENSOR_BAUDS[baudIndex];
  if (result == READ_OK) {
    ++successfulReads;
    consecutiveFailures = 0;
    Serial.printf("Huong gio: %u.%u do (%s)\n", angleTenths / 10, angleTenths % 10, directionName(angleTenths));
    Serial.printf("[ESWS04] {\"sensor\":\"ES-WS-04\",\"angle_deg\":%u.%u,\"raw\":%u,\"direction\":\"%s\","
                  "\"status\":\"ok\",\"baud\":%lu,\"address\":%u,\"responses\":%lu,\"errors\":%lu}\n",
                  angleTenths / 10, angleTenths % 10, angleTenths, directionName(angleTenths),
                  (unsigned long)baud, SENSOR_ADDRESS,
                  (unsigned long)successfulReads, (unsigned long)failedReads);
    return;
  }

  ++failedReads;
  ++consecutiveFailures;
  Serial.printf("[ESWS04] {\"sensor\":\"ES-WS-04\",\"angle_deg\":null,\"direction\":null,"
                "\"status\":\"%s\",\"baud\":%lu,\"address\":%u,\"responses\":%lu,\"errors\":%lu}\n",
                resultText(result), (unsigned long)baud, SENSOR_ADDRESS,
                (unsigned long)successfulReads, (unsigned long)failedReads);
  if (rawLength) printHex("[RS485] RX: ", raw, rawLength);
  else Serial.println("[RS485] Khong co phan hoi. Kiem tra nguon 10-30V, GND, day vang=A, xanh=B.");

  if (consecutiveFailures >= FAILURES_BEFORE_NEXT_BAUD) {
    consecutiveFailures = 0;
    baudIndex = (baudIndex + 1) % (sizeof(SENSOR_BAUDS) / sizeof(SENSOR_BAUDS[0]));
    beginSensorBaud(SENSOR_BAUDS[baudIndex]);
  }
}

void setup() {
  Serial.begin(USB_BAUD);
  delay(400);
  if (RS485_DE_RE_PIN >= 0) {
    pinMode(RS485_DE_RE_PIN, OUTPUT);
    setTransmit(false);
  }
  Serial.println();
  Serial.println("ES-WS-04 Wind Direction Sensor - RS485 Modbus RTU");
  Serial.println("Datasheet: 10-30VDC; yellow=A; green=B; register 0x0000; function 0x03");
  Serial.println("USB Serial 115200 baud. Cam bien mac dinh 9600 8N1, slave 0x01.");
  beginSensorBaud(SENSOR_BAUDS[baudIndex]);
}

void loop() {
  static uint32_t lastRead = 0;
  if (millis() - lastRead < READ_INTERVAL_MS) {
    delay(1);
    return;
  }
  lastRead = millis();
  uint16_t angleTenths = 0;
  uint8_t raw[64];
  size_t rawLength = 0;
  const uint8_t result = readWindDirection(angleTenths, raw, rawLength);
  printMeasurement(result, angleTenths, raw, rawLength);
}
