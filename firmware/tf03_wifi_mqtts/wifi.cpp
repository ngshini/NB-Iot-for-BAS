// Wi-Fi channel: ESP32 station + mbedTLS (WiFiClientSecure) + PubSubClient (MQTT 3.1.1).
// Same broker, CA (server certificate and hostname verified), topic and payload as NB-IoT.
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <time.h>
#include "common.h"
#if __has_include("config.h")
#include "config.h"
#else
#include "config.example.h"
#endif
#include "ca_cert.h"
#include "tf03_settings.h"
#undef PUBLISH_INTERVAL_MS
#define PUBLISH_INTERVAL_MS TF03_PUBLISH_MS

#define WF_LOG(...) logLine("WF", __VA_ARGS__)

static WiFiClientSecure tls;
static PubSubClient mqtt(tls);

static char payload[512];
static bool mqttConnected = false;
static uint32_t nextAttempt = 0, lastPublish = 0, publishCount = 0;
static const uint8_t BACKOFF_S[] = {5, 10, 20, 30, 60};
static uint8_t backoffIndex = 0;

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
  WF_LOG("! [%s] FAILED: %s", step, hint);
  return false;
}

// Lists the networks the ESP32 radio can see (2.4 GHz only). Line format is parsed by the UI:
//   net "<ssid>" ch <n> <rssi> dBm <open|secured>
static void scanNetworks() {
  bool wasOff = WiFi.getMode() == WIFI_OFF;
  if (wasOff) WiFi.mode(WIFI_STA);
  WF_LOG("  scanning 2.4 GHz networks...");
  int n = WiFi.scanNetworks(false, true);
  if (n < 0) {
    WF_LOG("  scan failed (%d)", n);
  } else {
    WF_LOG("  scan: %d network(s) visible to the ESP32 (2.4 GHz only)", n);
    bool found = false;
    for (int i = 0; i < n && i < 25; i++) {
      char ssid[33];
      snprintf(ssid, sizeof ssid, "%s", WiFi.SSID(i).c_str());
      if (!strcmp(ssid, gWifiSsid)) found = true;
      WF_LOG("  net \"%s\" ch %d %d dBm %s", ssid, (int)WiFi.channel(i), (int)WiFi.RSSI(i),
             WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "open" : "secured");
    }
    if (!found)
      WF_LOG("  configured SSID \"%s\" is not among them: check the exact name, or the router only offers it on 5 GHz",
             gWifiSsid);
  }
  WiFi.scanDelete();
  if (wasOff) WiFi.mode(WIFI_OFF);
}

static volatile int lastDisconnectReason = 0;  // wifi_err_reason_t from the last STA disconnect

static const char *disconnectHint(int r) {
  switch (r) {
    case 15: case 204: return "4-way handshake failed: wrong password";
    case 202: return "authentication failed: wrong password or security mode";
    case 2: case 23: return "authentication expired/failed";
    case 201: return "SSID not found on 2.4 GHz";
    case 203: case 205: return "association rejected by the router (MAC filter, client limit)";
    case 210: case 211: return "router security mode not supported (WPA3-only / enterprise?)";
    case 200: return "beacon timeout: weak signal";
    default: return "see ESP-IDF wifi_err_reason_t";
  }
}

static bool stepWifi() {
  if (WiFi.status() == WL_CONNECTED) return true;
  WF_LOG("[7] Wi-Fi: joining \"%s\"", gWifiSsid);
  static bool eventHooked = false;
  if (!eventHooked) {
    eventHooked = true;
    WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t info) {
      lastDisconnectReason = info.wifi_sta_disconnected.reason;
    }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  }
  lastDisconnectReason = 0;
  if (!strcmp(gWifiSsid, "CHANGE_ME")) return fail("WIFI", "set the Wi-Fi SSID/password on the page or in config.h");
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(gWifiSsid, gWifiPass);
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) delay(250);
  wl_status_t s = WiFi.status();
  if (s != WL_CONNECTED) {
    WF_LOG("  Wi-Fi status %d (%s)", (int)s, wifiStatusText(s));
    if (lastDisconnectReason)
      WF_LOG("  disconnect reason %d: %s", (int)lastDisconnectReason, disconnectHint(lastDisconnectReason));
    WiFi.disconnect();
    scanNetworks();
    return fail("WIFI", "not connected in 20 s: SSID/password, 2.4 GHz band, signal");
  }
  WF_LOG("  IP: %s", WiFi.localIP().toString().c_str());
  WF_LOG("  RSSI %d dBm, channel %d, gateway %s, DNS %s", (int)WiFi.RSSI(), (int)WiFi.channel(),
         WiFi.gatewayIP().toString().c_str(), WiFi.dnsIP().toString().c_str());
  return true;
}

static bool stepTime() {
  WF_LOG("[8] Network time (NTP)");
  configTime(TZ_OFFSET_S, 0, NTP_SERVER, NTP_SERVER_2);
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
  WF_LOG("  clock OK: %s (UTC+%d)", buf, TZ_OFFSET_S / 3600);
  return true;
}

static bool stepMqttConnect() {
  WF_LOG("[9c] MQTT over TLS (ESP32 mbedTLS, CA DigiCert Global Root G2, hostname checked)");
  tls.stop();
  tls.setCACert(CA_CERT_PEM);  // server certificate and hostname are verified
  tls.setHandshakeTimeout(30);
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setKeepAlive(MQTT_KEEPALIVE_S);
  mqtt.setSocketTimeout(20);
  mqtt.setBufferSize(768);
  WF_LOG("> CONNECT %s:%d client id %s, keepalive %d, clean %d, credentials %s", MQTT_HOST, MQTT_PORT,
         WIFI_CLIENT_ID, MQTT_KEEPALIVE_S, MQTT_CLEAN_SESSION, strlen(MQTT_USER) ? "set (hidden)" : "none");
  uint32_t start = millis();
  bool ok = mqtt.connect(WIFI_CLIENT_ID, strlen(MQTT_USER) ? MQTT_USER : nullptr,
                         strlen(MQTT_PASS) ? MQTT_PASS : nullptr, nullptr, 0, false, nullptr, MQTT_CLEAN_SESSION);
  if (!ok) {
    int s = mqtt.state();
    WF_LOG("! [MQTTCONNECT] FAILED: state %d = %s (%lu ms)", s, mqttStateText(s), (unsigned long)(millis() - start));
    char err[128];
    int code = tls.lastError(err, sizeof err);
    if (code) WF_LOG("  TLS error %d: %s", code, err);
    if (s == -2) WF_LOG("  hint: DNS/TCP 8883 blocked, CA mismatch or clock wrong (see TLS error above).");
    return false;
  }
  mqttConnected = true;
  WF_LOG("[9] MQTT connected to %s:%d over TLS as %s (%lu ms)", MQTT_HOST, MQTT_PORT, WIFI_CLIENT_ID,
         (unsigned long)(millis() - start));
  return true;
}

static bool bringUp() { return stepWifi() && stepTime() && stepMqttConnect(); }

static bool publishTelemetry() {
  size_t len = buildPayload(payload, sizeof payload);
  if (!len) { WF_LOG("! payload does not fit the buffer; not sent"); return true; }
  publishCount++;
  WF_LOG("[10] Publish #%lu to %s (%u bytes): %s", (unsigned long)publishCount, MQTT_TOPIC, (unsigned)len, payload);
  if (!mqtt.publish(MQTT_TOPIC, (const uint8_t *)payload, len, MQTT_RETAIN)) {
    int s = mqtt.state();
    WF_LOG("! [MQTTPUB] FAILED: state %d = %s", s, mqttStateText(s));
    return false;
  }
  WF_LOG("[11] Publish OK (QoS 0: written to the TLS socket; the broker sends no PUBACK, confirm with a subscriber)");
  return true;
}

static void scheduleRetry() {
  uint8_t s = BACKOFF_S[backoffIndex];
  if (backoffIndex < sizeof(BACKOFF_S) - 1) backoffIndex++;
  nextAttempt = millis() + s * 1000UL;
  WF_LOG("Retry in %u s", s);
}

void wifiTask(void *) {
  WF_LOG("[1] Wi-Fi SSID \"%s\" (password hidden), client id %s", gWifiSsid, WIFI_CLIENT_ID);
  bool wasEnabled = true;
  for (;;) {
    if (gWifiScanReq) {
      gWifiScanReq = false;
      scanNetworks();
    }
    if (gWifiChanged) {
      gWifiChanged = false;
      WF_LOG("[1] Wi-Fi SSID \"%s\" (password hidden), client id %s", gWifiSsid, WIFI_CLIENT_ID);
      if (mqttConnected) mqtt.disconnect();
      mqttConnected = false;
      if (WiFi.getMode() != WIFI_OFF) WiFi.disconnect();
      backoffIndex = 0;
      nextAttempt = millis();
    }
    bool enabled = gChannels & CH_WIFI;
    if (!enabled) {
      if (wasEnabled) {
        if (mqttConnected) mqtt.disconnect();
        mqttConnected = false;
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        WF_LOG("Channel disabled (Wi-Fi off)");
      }
      wasEnabled = false;
      delay(200);
      continue;
    }
    if (!wasEnabled) {
      wasEnabled = true;
      backoffIndex = 0;
      nextAttempt = millis();
      WF_LOG("Channel enabled");
    }
    if (mqttConnected) {
      mqtt.loop();
      if (!mqtt.connected()) {
        mqttConnected = false;
        int s = mqtt.state();
        WF_LOG("! MQTT connection lost, state %d (%s); Wi-Fi %s", s, mqttStateText(s), wifiStatusText(WiFi.status()));
        scheduleRetry();
      }
    }
    if (!mqttConnected) {
      if ((int32_t)(millis() - nextAttempt) < 0) { delay(20); continue; }
      WF_LOG("=== Connecting ===");
      if (bringUp()) {
        backoffIndex = 0;
        lastPublish = millis() - PUBLISH_INTERVAL_MS;  // publish immediately
      } else {
        scheduleRetry();
      }
      continue;
    }
    if (gPublishNow[1] || millis() - lastPublish >= PUBLISH_INTERVAL_MS) {
      gPublishNow[1] = false;
      lastPublish = millis();
      if (!publishTelemetry()) {
        mqttConnected = false;
        mqtt.disconnect();
        scheduleRetry();
      }
    }
    delay(20);
  }
}
