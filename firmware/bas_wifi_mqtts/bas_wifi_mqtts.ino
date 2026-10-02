#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <math.h>
#include <time.h>
#if __has_include("config.h")
#include "config.h"
#else
#include "config.example.h"
#endif
#include "ca_cert.h"

// ESP32 Wi-Fi -> MQTT 3.1.1 over TLS, parallel test path to the SIM7022 NB-IoT firmware.
// Same broker, CA (verified, hostname checked), topic, payload and retry schedule; TLS is done
// by mbedTLS on the ESP32. Log lines reuse the [7]..[11] markers of the NB-IoT firmware so the
// BAS Serial Monitor page can track them. Fixed buffers only; no String concatenation.

// Declared before any function: the Arduino builder inserts prototypes there.
struct Telemetry {
  float distance, sternDistance, bowSpeed, sternSpeed, angle, waterLevel, waterFlow;
  const char *waterDirection;
  int windForce;
  const char *windDirection;
};

WiFiClientSecure tls;
PubSubClient mqtt(tls);

static char payload[512];
static bool mqttConnected = false;
static uint32_t nextAttempt = 0, lastPublish = 0, publishCount = 0;
static const uint8_t BACKOFF_S[] = {5, 10, 20, 30, 60};
static uint8_t backoffIndex = 0;

// ---------------------------------------------------------------- diagnostics

static const char *wifiStatusText(wl_status_t s) {
  switch (s) {
    case WL_IDLE_STATUS: return "idle";
    case WL_NO_SSID_AVAIL: return "SSID not found";
    case WL_CONNECTED: return "connected";
    case WL_CONNECT_FAILED: return "connect failed (wrong password?)";
    case WL_CONNECTION_LOST: return "connection lost";
    case WL_DISCONNECTED: return "disconnected";
    default: return "unknown";
  }
}

// PubSubClient::state()
static const char *mqttStateText(int s) {
  switch (s) {
    case -4: return "connection timeout";
    case -3: return "connection lost";
    case -2: return "connect failed (TCP/TLS)";
    case -1: return "disconnected";
    case 0: return "connected";
    case 1: return "refused: unacceptable protocol version";
    case 2: return "refused: identifier rejected";
    case 3: return "refused: server unavailable";
    case 4: return "refused: bad user name or password";
    case 5: return "refused: not authorized";
    default: return "unknown";
  }
}

static bool fail(const char *step, const char *hint) {
  Serial.printf("! [%s] FAILED: %s\n", step, hint);
  return false;
}

// ---------------------------------------------------------------- bring-up steps

static bool stepWifi() {
  if (WiFi.status() == WL_CONNECTED) return true;
  Serial.printf("[7] Wi-Fi: joining \"%s\"\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) delay(250);
  wl_status_t s = WiFi.status();
  if (s != WL_CONNECTED) {
    Serial.printf("  Wi-Fi status %d (%s)\n", (int)s, wifiStatusText(s));
    WiFi.disconnect();
    return fail("WIFI", "not connected in 20 s: SSID/password, 2.4 GHz band, signal");
  }
  Serial.printf("  IP: %s\n", WiFi.localIP().toString().c_str());
  Serial.printf("  RSSI %d dBm, channel %d, gateway %s, DNS %s\n", (int)WiFi.RSSI(), (int)WiFi.channel(),
                WiFi.gatewayIP().toString().c_str(), WiFi.dnsIP().toString().c_str());
  return true;
}

static bool stepTime() {
  Serial.println("[8] Network time (NTP)");
  configTime(TZ_OFFSET_S, 0, NTP_SERVER_1, NTP_SERVER_2);
  uint32_t start = millis();
  time_t now = time(nullptr);
  while (now < 1735689600 && millis() - start < 20000) {  // 2025-01-01
    delay(250);
    now = time(nullptr);
  }
  if (now < 1735689600) return fail("NTP", "time not synchronised in 20 s (UDP 123 blocked?)");
  struct tm local;
  localtime_r(&now, &local);
  char buf[32];
  strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", &local);
  Serial.printf("  clock OK: %s (UTC+%d)\n", buf, TZ_OFFSET_S / 3600);
  return true;
}

static bool stepMqttConnect() {
  Serial.println("[9c] MQTT over TLS (ESP32 mbedTLS, CA DigiCert Global Root G2, hostname checked)");
  tls.stop();
  tls.setCACert(CA_CERT_PEM);  // server certificate and hostname are verified
  tls.setHandshakeTimeout(30);
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setKeepAlive(MQTT_KEEPALIVE_S);
  mqtt.setSocketTimeout(20);
  mqtt.setBufferSize(768);
  Serial.printf("> CONNECT %s:%d client id %s, keepalive %d, clean %d, credentials %s\n", MQTT_HOST, MQTT_PORT,
                MQTT_CLIENT_ID, MQTT_KEEPALIVE_S, MQTT_CLEAN_SESSION, strlen(MQTT_USER) ? "set (hidden)" : "none");
  uint32_t start = millis();
  bool ok = mqtt.connect(MQTT_CLIENT_ID, strlen(MQTT_USER) ? MQTT_USER : nullptr,
                         strlen(MQTT_PASS) ? MQTT_PASS : nullptr, nullptr, 0, false, nullptr, MQTT_CLEAN_SESSION);
  if (!ok) {
    int s = mqtt.state();
    Serial.printf("! [MQTTCONNECT] FAILED: state %d = %s (%lu ms)\n", s, mqttStateText(s),
                  (unsigned long)(millis() - start));
    char err[128];
    int code = tls.lastError(err, sizeof err);
    if (code) Serial.printf("  TLS error %d: %s\n", code, err);
    if (s == -2) Serial.println("  hint: DNS/TCP 8883 blocked, CA mismatch or clock wrong (see TLS error above).");
    return false;
  }
  mqttConnected = true;
  Serial.printf("[9] MQTT connected to %s:%d over TLS as %s (%lu ms)\n", MQTT_HOST, MQTT_PORT, MQTT_CLIENT_ID,
                (unsigned long)(millis() - start));
  return true;
}

static bool bringUp() { return stepWifi() && stepTime() && stepMqttConnect(); }

// ---------------------------------------------------------------- telemetry

// BAS test values. Replace with real sensor readings; field names are fixed by BAS.
static Telemetry readTelemetry() {
  return {120.5f, 118.2f, 2.1f, 1.8f, 1.2f, 4.2f, 0.3f, "NE", 15, "NE"};
}

static const char *jsonNum(char *buf, size_t len, float v) {
  if (isfinite(v)) snprintf(buf, len, "%.1f", v);
  else snprintf(buf, len, "null");  // NaN/Inf are not valid JSON
  return buf;
}

// Only 16-point compass values are emitted as strings, so no escaping is ever needed.
[[maybe_unused]] static const char *jsonDir(char *buf, size_t len, const char *d) {
  static const char *const DIRS[] = {"N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
                                     "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"};
  for (const char *x : DIRS)
    if (d && !strcmp(d, x)) { snprintf(buf, len, "\"%s\"", x); return buf; }
  snprintf(buf, len, "null");
  return buf;
}

static size_t buildPayload() {
  Telemetry t = readTelemetry();
  char a[7][16];
  int n;
#if PAYLOAD_MINIMAL
  n = snprintf(payload, sizeof payload, "{\"distance\":%s}", jsonNum(a[0], 16, t.distance));
#else
  char wd[8], wn[8];
  n = snprintf(payload, sizeof payload,
               "{\"distance\":%s,\"sternDistance\":%s,\"bowSpeed\":%s,\"sternSpeed\":%s,"
               "\"angle\":%s,\"waterLevel\":%s,\"waterFlow\":%s,\"waterDirection\":%s,"
               "\"windForce\":%d,\"windDirection\":%s}",
               jsonNum(a[0], 16, t.distance), jsonNum(a[1], 16, t.sternDistance),
               jsonNum(a[2], 16, t.bowSpeed), jsonNum(a[3], 16, t.sternSpeed),
               jsonNum(a[4], 16, t.angle), jsonNum(a[5], 16, t.waterLevel),
               jsonNum(a[6], 16, t.waterFlow), jsonDir(wd, sizeof wd, t.waterDirection),
               t.windForce, jsonDir(wn, sizeof wn, t.windDirection));
#endif
  return (n > 0 && (size_t)n < sizeof payload) ? (size_t)n : 0;
}

static bool publishTelemetry() {
  size_t len = buildPayload();
  if (!len) { Serial.println("! payload does not fit the buffer; not sent"); return true; }
  publishCount++;
  Serial.printf("[10] Publish #%lu to %s (%u bytes): %s\n", (unsigned long)publishCount, MQTT_TOPIC,
                (unsigned)len, payload);
  if (!mqtt.publish(MQTT_TOPIC, (const uint8_t *)payload, len, MQTT_RETAIN)) {
    int s = mqtt.state();
    Serial.printf("! [MQTTPUB] FAILED: state %d = %s\n", s, mqttStateText(s));
    return false;
  }
  Serial.println("[11] Publish OK (QoS 0: written to the TLS socket; the broker sends no PUBACK, confirm with a subscriber)");
  return true;
}

// ---------------------------------------------------------------- main

static void scheduleRetry() {
  uint8_t s = BACKOFF_S[backoffIndex];
  if (backoffIndex < sizeof(BACKOFF_S) - 1) backoffIndex++;
  nextAttempt = millis() + s * 1000UL;
  Serial.printf("Retry in %u s\n", s);
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("ESP32 Wi-Fi -> MQTT over TLS (BAS test, parallel to SIM7022 NB-IoT)");
  Serial.printf("[1] Wi-Fi SSID \"%s\", password hidden\n", WIFI_SSID);
  Serial.printf("  broker %s:%d TLS, MQTT 3.1.1, client id %s\n", MQTT_HOST, MQTT_PORT, MQTT_CLIENT_ID);
  Serial.printf("  topic %s, keepalive %d s, QoS 0, retain %d, clean session %d\n", MQTT_TOPIC,
                MQTT_KEEPALIVE_S, MQTT_RETAIN, MQTT_CLEAN_SESSION);
  if (!strcmp(WIFI_SSID, "CHANGE_ME")) Serial.println("! Set WIFI_SSID / WIFI_PASS in config.h");
}

void loop() {
  if (mqttConnected) {
    mqtt.loop();
    if (!mqtt.connected()) {
      mqttConnected = false;
      int s = mqtt.state();
      Serial.printf("! MQTT connection lost, state %d (%s); Wi-Fi %s\n", s, mqttStateText(s),
                    wifiStatusText(WiFi.status()));
      scheduleRetry();
    }
  }
  if (!mqttConnected) {
    if ((int32_t)(millis() - nextAttempt) < 0) { delay(10); return; }
    Serial.println("=== Connecting ===");
    if (bringUp()) {
      backoffIndex = 0;
      lastPublish = millis() - PUBLISH_INTERVAL_MS;  // publish immediately
    } else {
      scheduleRetry();
    }
    return;
  }
  if (millis() - lastPublish >= PUBLISH_INTERVAL_MS) {
    lastPublish = millis();
    if (!publishTelemetry()) {
      mqttConnected = false;
      mqtt.disconnect();
      scheduleRetry();
    }
  }
  delay(10);
}
