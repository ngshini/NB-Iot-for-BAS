#include <Arduino.h>
#if __has_include("config.h")
#include "config.h"
#else
#include "config.example.h"
#endif

// MQTT is handled inside SIM7022. No PPP, Wi-Fi or PubSubClient required.
HardwareSerial modem(1);
enum Dialect { UNKNOWN, CMQTT, QC_PLUS, QC_DOLLAR };
Dialect dialect = UNKNOWN;
String partial;
bool mqttUp = false;
uint32_t nextAttempt = 0, lastPublish = 0, sequence = 0;
uint32_t retryMs = 10000;
uint16_t messageId = 0;

void inspectUrc(const String &line) {
  if (line.startsWith("+CMQTTCONNLOST:") || line.startsWith("+QCMTSTAT:") ||
      line.startsWith("$QCMTSTAT:") || line.indexOf("PDP DEACT") >= 0) mqttUp = false;
}

// Bounded incremental line reader. A prompt has no terminating newline.
bool readLine(String &line, bool prompt = false) {
  while (modem.available()) {
    char c = modem.read();
    if (prompt && c == '>') { partial = ""; line = ">"; return true; }
    if (c == '\r') continue;
    if (c == '\n') {
      line = partial; partial = ""; line.trim();
      if (!line.length()) continue;
      inspectUrc(line);
      Serial.println("< " + line);
      return true;
    }
    if (partial.length() < 2048) partial += c;
    else { partial = ""; mqttUp = false; }
  }
  return false;
}

void drain(uint32_t duration = 100) {
  uint32_t start = millis(); String line;
  while (millis() - start < duration) { readLine(line); delay(1); }
}

bool isError(const String &s) {
  return s == "ERROR" || s.startsWith("+CME ERROR:") || s.startsWith("+CMS ERROR:");
}

bool waitOK(uint32_t timeout, String *response = nullptr) {
  uint32_t start = millis(); String line;
  while (millis() - start < timeout) {
    if (readLine(line)) {
      if (response && response->length() < 4096) *response += line + "\n";
      if (line == "OK") return true;
      if (isError(line)) return false;
    }
    delay(1);
  }
  Serial.println("! AT timeout"); return false;
}

void sendAT(const String &command, bool secret = false) {
  drain();
  Serial.println(secret ? "> [MQTT credentials hidden]" : "> " + command);
  modem.print(command); modem.print("\r\n");
}

bool command(const String &s, uint32_t timeout = 5000, String *response = nullptr) {
  sendAT(s); return waitOK(timeout, response);
}

// Wait for the asynchronous result, not just the initial OK.
// Numeric fields must match exactly: result 10 must not match result 1/0.
bool result(const String &prefix, const String &expected, uint32_t timeout) {
  uint32_t start = millis(); String line;
  while (millis() - start < timeout) {
    if (readLine(line)) {
      if (isError(line)) return false;
      if (line.startsWith(prefix)) {
        String fields = line.substring(prefix.length()); fields.replace(" ", "");
        bool ok = fields == expected;
        if (!ok) Serial.println("! MQTT result failed: " + fields);
        drain(); return ok;
      }
    }
    delay(1);
  }
  Serial.println("! MQTT result timeout"); return false;
}

String qc(const char *name) { return String(dialect == QC_DOLLAR ? "AT$" : "AT+") + name; }
String qr(const char *name) { return String(dialect == QC_DOLLAR ? "$" : "+") + name + ":"; }

bool detectDialect() {
  String choice = MQTT_DIALECT;
  if (choice == "CMQTT") dialect = CMQTT;
  else if (choice == "QC_PLUS") dialect = QC_PLUS;
  else if (choice == "QC_DOLLAR") dialect = QC_DOLLAR;
  else if (choice != "AUTO") return false;
  else if (command("AT+CMQTTSTART=?")) dialect = CMQTT;
  else if (command("AT+QCMTCFG=?")) dialect = QC_PLUS;
  else if (command("AT$QCMTCFG=?")) dialect = QC_DOLLAR;
  else { Serial.println("! Unknown MQTT firmware. Use AT_BRIDGE_ONLY and capture ATI / AT+CGMR."); return false; }
  Serial.printf("MQTT dialect: %d (1=CMQTT, 2=QC_PLUS, 3=QC_DOLLAR)\n", dialect);
  return true;
}

bool registered() {
  String response;
  if (!command("AT+CEREG?", 5000, &response)) return false;
  int at = response.indexOf("+CEREG:");
  int mode = -1, status = -1;
  return at >= 0 && sscanf(response.c_str() + at, "+CEREG: %d,%d", &mode, &status) == 2 &&
         (status == 1 || status == 5);
}

bool networkBegin() {
  if (!command("AT")) { Serial.println("! Check power, UART pins, baud and IO voltage."); return false; }
  // Echo must be off before credentials are sent.
  if (!command("ATE0")) return false;
  command("AT+CMEE=2"); command("ATI"); command("AT+CGMR");
  String sim;
  if (!command("AT+CPIN?", 5000, &sim) || sim.indexOf("+CPIN: READY") < 0) {
    Serial.println("! SIM not ready / PIN locked"); return false;
  }
  if (dialect == UNKNOWN && !detectDialect()) return false;
  // Empty APN preserves the existing profile. Do not guess a Viettel APN.
  if (strlen(VIETTEL_APN)) {
    if (!command("AT+CFUN=0", 15000)) return false;
    if (!command(String("AT+CGDCONT=1,\"IP\",\"") + VIETTEL_APN + "\"")) return false;
  }
  if (!command("AT+CFUN=1", 15000)) return false;
  command("AT+CPSMS=0"); command("AT+CEDRXS=0"); // demo: keep modem awake
  command("AT+CEREG=0");
  uint32_t start = millis();
  while (!registered()) {
    if (millis() - start >= 180000) { Serial.println("! NB-IoT registration timeout"); return false; }
    command("AT+CSQ"); delay(3000);
  }
  String attached;
  if (!command("AT+CGATT?", 5000, &attached)) return false;
  if (attached.indexOf("+CGATT: 1") < 0 && !command("AT+CGATT=1", 180000)) return false;
  // CMQTTSTART activates PDP itself; the legacy QC client needs an active IP context.
  if (dialect != CMQTT) {
    String active;
    if (!command("AT+CGACT?", 5000, &active)) return false;
    active.replace(" ", "");
    if (active.indexOf("+CGACT:1,1") < 0 && !command("AT+CGACT=1,1", 180000)) return false;
  }
  command("AT+CGDCONT?"); command("AT+CGPADDR=1");
  return true;
}

bool connectMQTT() {
  if (dialect == CMQTT) {
    // Best effort cleanup of stale client after ESP32 resets; errors here are normal.
    sendAT("AT+CMQTTDISC=0,60"); result("+CMQTTDISC:", "0,0", 65000);
    command("AT+CMQTTREL=0");
    sendAT("AT+CMQTTSTOP"); result("+CMQTTSTOP:", "0", 65000);
    sendAT("AT+CMQTTSTART");
    if (!result("+CMQTTSTART:", "0", 180000)) return false;
    if (!command(String("AT+CMQTTACCQ=0,\"") + DEVICE_ID + "\",0")) return false;
    sendAT(String("AT+CMQTTCONNECT=0,\"tcp://") + MQTT_HOST + ":" + MQTT_PORT +
           "\",120,1,\"" + MQTT_USER + "\",\"" + MQTT_PASSWORD + "\"", true);
    return result("+CMQTTCONNECT:", "0,0", 180000);
  }
  sendAT(qc("QCMTCLOSE") + "=0"); result(qr("QCMTCLOSE"), "0,0", 15000);
  if (!command(qc("QCMTCFG") + "=\"version\",0,4") ||
      !command(qc("QCMTCFG") + "=\"keepalive\",0,120") ||
      !command(qc("QCMTCFG") + "=\"session\",0,1") ||
      !command(qc("QCMTCFG") + "=\"dataformat\",0,0,0")) return false;
  sendAT(qc("QCMTOPEN") + "=0,\"" + MQTT_HOST + "\"," + MQTT_PORT);
  if (!result(qr("QCMTOPEN"), "0,0", 180000)) return false;
  sendAT(qc("QCMTCONN") + "=0,\"" + DEVICE_ID + "\",\"" + MQTT_USER + "\",\"" + MQTT_PASSWORD + "\"", true);
  return result(qr("QCMTCONN"), "0,0,0", 180000);
}

bool inputData(const String &cmd, const String &data) {
  sendAT(cmd); uint32_t start = millis(); String line;
  while (millis() - start < 10000) {
    if (readLine(line, true)) {
      if (isError(line)) return false;
      if (line == ">") { modem.print(data); return waitOK(10000); }
    }
    delay(1);
  }
  return false;
}

bool publishTelemetry() {
  // Clearly marked simulated values: replace with your sensor readings here.
  String data = String("device=") + DEVICE_ID + ";seq=" + (++sequence) +
      ";uptime_s=" + (millis() / 1000) + ";temperature=" + String(25.0f + (sequence % 30) / 10.0f, 1) +
      ";humidity=65;simulated=1";
  Serial.println("Publishing: " + data);
  if (dialect == CMQTT) {
    if (!inputData(String("AT+CMQTTTOPIC=0,") + strlen(MQTT_TOPIC), MQTT_TOPIC) ||
        !inputData(String("AT+CMQTTPAYLOAD=0,") + data.length(), data)) return false;
    sendAT("AT+CMQTTPUB=0,1,60");
    return result("+CMQTTPUB:", "0,0", 70000);
  }
  if (++messageId == 0) ++messageId;
  // Text key=value payload avoids firmware-specific JSON quote escaping.
  sendAT(qc("QCMTPUB") + "=0," + messageId + ",1,0,\"" + MQTT_TOPIC + "\",\"" + data + "\"");
  return result(qr("QCMTPUB"), String("0,") + messageId + ",0", 180000);
}

void setup() {
  Serial.begin(115200); modem.setRxBufferSize(4096);
  modem.begin(MODEM_BAUD, SERIAL_8N1, MODEM_RX, MODEM_TX);
  delay(2000);
  Serial.println("ESP32 / SIM7022 NB-IoT MQTT demo");
  Serial.println("Power on modem manually. No assumed RESET/PWRKEY wiring.");
}

void loop() {
  if (AT_BRIDGE_ONLY) {
    while (Serial.available()) modem.write(Serial.read());
    while (modem.available()) Serial.write(modem.read());
    delay(1); return;
  }
  if (String(MQTT_HOST).startsWith("CHANGE_ME") || !strlen(MQTT_HOST) ||
      String(MQTT_PASSWORD) == "CHANGE_ME" || !strlen(MQTT_PASSWORD)) {
    Serial.println("! Configure public MQTT host/port/password in config.h first.");
    delay(10000); return;
  }
  String line; while (readLine(line)) {}
  if (!mqttUp) {
    if ((int32_t)(millis() - nextAttempt) < 0) { delay(10); return; }
    mqttUp = networkBegin() && connectMQTT();
    if (!mqttUp) {
      nextAttempt = millis() + retryMs;
      retryMs = min(retryMs * 2, (uint32_t)300000);
      return;
    }
    retryMs = 10000; lastPublish = millis() - PUBLISH_INTERVAL_MS;
    Serial.println("MQTT connected");
  }
  if (millis() - lastPublish >= PUBLISH_INTERVAL_MS) {
    lastPublish = millis();
    if (!publishTelemetry()) { mqttUp = false; nextAttempt = millis() + retryMs; }
    else Serial.println("MQTT publish acknowledged");
  }
  delay(10);
}
