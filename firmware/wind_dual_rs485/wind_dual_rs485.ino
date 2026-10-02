#include <Arduino.h>
#include <Preferences.h>

// ATMC onboard auto-direction RS485: RX16/TX17, USB 115200.
static HardwareSerial bus(2);
static Preferences preferences;
static bool running = false;
static uint32_t currentBaud = 0;
static const uint32_t BAUDS[] = {4800, 9600, 2400, 19200, 38400, 56000, 115200};
struct Sensor {
  uint8_t address, registers, baudIndex, failures;
  uint32_t responses, errors;
};
static Sensor speed = {2, 2, 0, 0, 0, 0};
static Sensor direction = {1, 1, 0, 0, 0, 0};

static uint16_t crc16(const uint8_t *data, size_t length) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit) crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
  }
  return crc;
}
static size_t exchange(uint8_t address, uint8_t function, uint16_t reg,
                       uint16_t value, uint32_t baud, uint8_t *raw) {
  if (currentBaud != baud) {
    bus.end(); delay(20);
    bus.begin(baud, SERIAL_8N1, 16, 17);
    currentBaud = baud;
  }
  while (bus.available()) bus.read();
  delay(20);
  uint8_t request[] = {address, function, uint8_t(reg >> 8), uint8_t(reg), uint8_t(value >> 8), uint8_t(value), 0, 0};
  const uint16_t crc = crc16(request, 6);
  request[6] = uint8_t(crc); request[7] = uint8_t(crc >> 8);
  bus.write(request, sizeof(request)); bus.flush();
  size_t length = 0;
  uint32_t started = millis(), lastByte = started;
  while (millis() - started < 350 && length < 64) {
    while (bus.available() && length < 64) { raw[length++] = uint8_t(bus.read()); lastByte = millis(); }
    if (length && millis() - lastByte >= 20) break;
    delay(1);
  }
  return length;
}
static const char *readRegister(uint8_t address, uint16_t reg, uint8_t count,
                                uint32_t baud, uint16_t &value, uint8_t &exception) {
  uint8_t raw[64];
  const size_t length = exchange(address, 3, reg, count, baud, raw);
  if (!length) return "timeout";
  bool badCrc = false;
  for (size_t i = 0; i + 5 <= length; ++i) {
    if (raw[i] != address) continue;
    bool isException = raw[i + 1] == 0x83;
    if (!isException && (raw[i + 1] != 3 || raw[i + 2] != count * 2)) continue;
    size_t size = isException ? 5 : 5 + count * 2;
    if (i + size > length) continue;
    uint16_t crc = raw[i + size - 2] | (uint16_t(raw[i + size - 1]) << 8);
    if (crc16(raw + i, size - 2) != crc) { badCrc = true; continue; }
    if (isException) { exception = raw[i + 2]; return "modbus_exception"; }
    value = (uint16_t(raw[i + 3]) << 8) | raw[i + 4];
    return "ok";
  }
  return badCrc ? "bad_crc" : "bad_frame";
}
static const char *directionName(uint16_t tenths) {
  static const char *names[] = {"N","NNE","NE","ENE","E","ESE","SE","SSE","S","SSW","SW","WSW","W","WNW","NW","NNW"};
  return names[((uint32_t(tenths) * 16 + 1800) / 3600) % 16];
}
static void pollSensor(Sensor &sensor, bool isSpeed) {
  uint16_t value = 0; uint8_t exception = 0;
  const uint32_t baud = BAUDS[sensor.baudIndex];
  const char *status = readRegister(sensor.address, 0, sensor.registers, baud, value, exception);
  if (!isSpeed && !strcmp(status, "ok") && value > 3599) status = "invalid_angle";
  const bool valid = !strcmp(status, "ok");
  if (valid) { ++sensor.responses; sensor.failures = 0; }
  else { ++sensor.errors; ++sensor.failures; }
  Serial.printf("[ESWS0%d] {\"sensor\":\"ES-WS-0%d\",", isSpeed ? 2 : 4, isSpeed ? 2 : 4);
  if (isSpeed) {
    if (valid) Serial.printf("\"speed_mps\":%u.%u,\"raw\":%u,", value / 10, value % 10, value);
    else Serial.print("\"speed_mps\":null,\"raw\":null,");
    Serial.print("\"unit\":\"m/s\",");
  } else {
    if (valid) Serial.printf("\"angle_deg\":%u.%u,\"raw\":%u,\"direction\":\"%s\",", value / 10, value % 10, value, directionName(value));
    else Serial.print("\"angle_deg\":null,\"raw\":null,\"direction\":null,");
  }
  Serial.printf("\"status\":\"%s\",\"exception\":%u,\"baud\":%lu,\"address\":%u,\"responses\":%lu,\"errors\":%lu}\n",
    status, exception, (unsigned long)baud, sensor.address, (unsigned long)sensor.responses, (unsigned long)sensor.errors);
  if (sensor.failures >= 3) { sensor.failures = 0; sensor.baudIndex = (sensor.baudIndex + 1) % (sizeof(BAUDS) / sizeof(BAUDS[0])); }
}
// Run ONLY with ES-WS-02 alone on the bus. The manual's register table
// says 0x07D0; its address-write example says 0x0100. Do not guess/write both.
// Require a readback of 1 at 0x07D0, then verify address 2 after writing.
static void provisionSpeed() {
  if (running) { Serial.println("[SETUP] STOP first; connect ONLY ES-WS-02."); return; }
  uint16_t value = 0; uint8_t exception = 0;
  if (!strcmp(readRegister(2, 0x07D0, 1, 4800, value, exception), "ok") && value == 2) {
    Serial.println("[SETUP] SPEED_ADDRESS_2_OK already configured"); return;
  }
  const char *status = readRegister(1, 0x07D0, 1, 4800, value, exception);
  Serial.printf("[SETUP] Read 0x07D0: %s value=%u exception=%u\n", status, value, exception);
  if (strcmp(status, "ok") || value != 1) {
    Serial.println("[SETUP] ABORT: address register not confirmed; no write performed."); return;
  }
  uint8_t raw[64];
  exchange(1, 6, 0x07D0, 2, 4800, raw);
  delay(300);
  value = 0; exception = 0;
  if (!strcmp(readRegister(2, 0x07D0, 1, 4800, value, exception), "ok") && value == 2) {
    uint16_t measurement = 0;
    if (!strcmp(readRegister(2, 0, 2, 4800, measurement, exception), "ok")) {
      Serial.printf("[SETUP] SPEED_ADDRESS_2_OK speed=%u.%u m/s\n", measurement / 10, measurement % 10); return;
    }
  }
  Serial.println("[SETUP] VERIFY_FAILED: keep ONLY ES-WS-02 connected; do not start dual polling.");
}
static void command(const String &line) {
  if (line == "STOP") { running = false; preferences.putBool("run", false); Serial.println("[SYS] STOPPED"); }
  else if (line == "SET_SPEED_ADDRESS_2") provisionSpeed();
  else if (line == "START") { running = true; preferences.putBool("run", true); Serial.println("[SYS] RUN: speed=2 direction=1"); }
  else Serial.println("[SYS] Commands: STOP, SET_SPEED_ADDRESS_2 (ES-WS-02 alone), START");
}
void setup() {
  Serial.begin(115200); delay(400);
  preferences.begin("wind-dual", false);
  running = preferences.getBool("run", false);
  Serial.printf("[SYS] ATMC dual wind; speed=2 direction=1; %s\n", running ? "RUN" : "STOPPED (commission addresses, then START)");
}
void loop() {
  static String line;
  while (Serial.available()) {
    char c = char(Serial.read());
    if (c == '\n') { line.trim(); if (line.length()) command(line); line = ""; }
    else if (c != '\r') { if (line.length() < 64) line += c; else line = ""; }
  }
  static uint32_t lastRead = 0;
  if (!running || millis() - lastRead < 1000) { delay(1); return; }
  lastRead = millis();
  pollSensor(speed, true);
  pollSensor(direction, false);
}
