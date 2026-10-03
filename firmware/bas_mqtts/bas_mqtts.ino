// BAS telemetry over MQTT 3.1.1 + TLS through two independent channels that can run in parallel:
//   NB-IoT : SIM7022 MQTT(S) stack via AT commands (nbiot.cpp)
//   Wi-Fi  : ESP32 mbedTLS + PubSubClient (wifi.cpp)
// After boot no channel runs until one is chosen from the BAS Serial Monitor page (or AUTO_START).
// Serial commands: MODE NB|WIFI|BOTH (start/switch), STOP, PAYLOAD MIN|FULL, PUBLISH, STATUS.
// The last choice is kept in flash and offered as the default on the page.
#include <Arduino.h>
#include <Preferences.h>
#include <math.h>
#include <stdarg.h>
#include "common.h"
#include "wind_bundle.h"
#if __has_include("config.h")
#include "config.h"
#else
#include "config.example.h"
#endif

volatile uint8_t gChannels = 0;          // nothing runs until a channel is chosen
static uint8_t lastChannels = DEFAULT_CHANNELS;
volatile bool gPayloadMinimal = PAYLOAD_MINIMAL;
volatile bool gPublishNow[2] = {false, false};
char gWifiSsid[33] = WIFI_SSID;
char gWifiPass[65] = WIFI_PASS;
volatile bool gWifiChanged = false;
volatile bool gWifiScanReq = false;

static SemaphoreHandle_t logMutex;
static Preferences prefs;
static char cmdLine[128];
static size_t cmdLen = 0;

// Declared before any function: the Arduino builder inserts prototypes there.
struct Telemetry {
  float distance, sternDistance, bowSpeed, sternSpeed, angle, waterLevel, waterFlow;
  const char *waterDirection;
  int windForce;
  const char *windDirection;
};

void logLine(const char *tag, const char *fmt, ...) {
  char buf[400];
  int n = snprintf(buf, sizeof buf, "[%s] ", tag);
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf + n, sizeof buf - n, fmt, ap);
  va_end(ap);
  if (logMutex) xSemaphoreTake(logMutex, portMAX_DELAY);
  Serial.println(buf);
  if (logMutex) xSemaphoreGive(logMutex);
}

// ---------------------------------------------------------------- telemetry

static const char *jsonNum(char *buf, size_t len, float v) {
  if (isfinite(v)) snprintf(buf, len, "%.1f", v);
  else snprintf(buf, len, "null");  // NaN/Inf are not valid JSON
  return buf;
}

// Only 16-point compass values are emitted as strings, so no escaping is ever needed.
static const char *jsonDir(char *buf, size_t len, const char *d) {
  static const char *const DIRS[] = {"N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
                                     "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"};
  for (const char *x : DIRS)
    if (d && !strcmp(d, x)) { snprintf(buf, len, "\"%s\"", x); return buf; }
  snprintf(buf, len, "null");
  return buf;
}

size_t buildPayload(char *out, size_t len) {
  const WindReading w = windReading();
  return formatWindBundle(out, len, w.valid, w.angleTenths, w.ageMs,
                          w.speedValid, w.speedTenths, w.speedAgeMs);
}

// ---------------------------------------------------------------- commands from the UI

static const char *modeName(uint8_t m) {
  return m == (CH_NB | CH_WIFI) ? "both" : m == CH_NB ? "nb" : m == CH_WIFI ? "wifi" : "none";
}

static void printStatus() {
  logLine("SYS", "mode=%s payload=%s last=%s interval=%lums topic=%s ssid=\"%s\"", modeName(gChannels),
          gPayloadMinimal ? "min" : "full", modeName(lastChannels), (unsigned long)PUBLISH_INTERVAL_MS,
          MQTT_TOPIC, gWifiSsid);
}

// "WIFI <ssid>|<password>": the SSID may contain spaces; '|' separates the password.
static bool handleWifiCommand(const char *c) {
  if (strncasecmp(c, "WIFI ", 5)) return false;
  const char *arg = c + 5, *bar = strchr(arg, '|');
  size_t ssidLen = bar ? (size_t)(bar - arg) : strlen(arg);
  const char *pass = bar ? bar + 1 : "";
  if (!ssidLen || ssidLen > 32 || strlen(pass) > 64 || (strlen(pass) && strlen(pass) < 8)) {
    logLine("SYS", "wifi: SSID must be 1-32 bytes, password empty or 8-64 bytes");
    return true;
  }
  memcpy(gWifiSsid, arg, ssidLen);
  gWifiSsid[ssidLen] = 0;
  snprintf(gWifiPass, sizeof gWifiPass, "%s", pass);
  prefs.putString("ssid", gWifiSsid);
  prefs.putString("pass", gWifiPass);
  gWifiChanged = true;
  logLine("SYS", "wifi credentials saved for \"%s\" (password hidden)", gWifiSsid);
  printStatus();
  return true;
}

static void handleCommand(char *c) {
  if (handleWifiCommand(c)) return;
  for (char *p = c; *p; p++) *p = toupper((unsigned char)*p);
  if (!strcmp(c, "DIAG NB")) {
    gChannels &= ~CH_NB;
    gNbDiagnostics = true;
    logLine("SYS", "NB diagnostics queued; waiting for the current modem operation to finish");
    return;
  }
  if (!strcmp(c, "WIFISCAN")) {
    gWifiScanReq = true;
    logLine("SYS", "wifi scan requested");
    return;
  }
  if (!strcmp(c, "MODE NB") || !strcmp(c, "MODE WIFI") || !strcmp(c, "MODE BOTH")) {
    gChannels = !strcmp(c, "MODE NB") ? CH_NB : !strcmp(c, "MODE WIFI") ? CH_WIFI : (CH_NB | CH_WIFI);
    lastChannels = gChannels;
    prefs.putUChar("channels", gChannels);
  } else if (!strcmp(c, "STOP")) {
    gChannels = 0;
    logLine("SYS", "stopped: choose a channel to start (MODE NB|WIFI|BOTH)");
  } else if (!strcmp(c, "PAYLOAD MIN") || !strcmp(c, "PAYLOAD FULL")) {
    gPayloadMinimal = !strcmp(c, "PAYLOAD MIN");
    prefs.putBool("minimal", gPayloadMinimal);
  } else if (!strcmp(c, "PUBLISH")) {
    gPublishNow[0] = gChannels & CH_NB;
    gPublishNow[1] = gChannels & CH_WIFI;
    logLine("SYS", "publish requested");
  } else if (strcmp(c, "STATUS")) {
    logLine("SYS", "unknown command \"%s\" (MODE NB|WIFI|BOTH, STOP, PAYLOAD MIN|FULL, PUBLISH, STATUS, "
                   "DIAG NB, WIFISCAN, WIFI ssid|password)", c);
    return;
  }
  printStatus();
}

static void pollCommands() {
  while (Serial.available()) {
    char ch = (char)Serial.read();
    if (ch == '\r' || ch == '\n') {
      cmdLine[cmdLen] = 0;
      if (cmdLen) handleCommand(cmdLine);
      cmdLen = 0;
    } else if (cmdLen < sizeof(cmdLine) - 1) {
      cmdLine[cmdLen++] = ch;
    }
  }
}

// ---------------------------------------------------------------- main

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  if (AT_BRIDGE_ONLY) {
    Serial.println("AT bridge: USB Serial <-> SIM7022 UART (no MQTT)");
    return;
  }
  logMutex = xSemaphoreCreateMutex();
  prefs.begin("bas", false);
  uint8_t stored = prefs.getUChar("channels", DEFAULT_CHANNELS);
  lastChannels = (stored & (CH_NB | CH_WIFI)) ? stored : DEFAULT_CHANNELS;
  gPayloadMinimal = prefs.getBool("minimal", PAYLOAD_MINIMAL);
  if (prefs.isKey("ssid")) {
    prefs.getString("ssid", gWifiSsid, sizeof gWifiSsid);
    prefs.getString("pass", gWifiPass, sizeof gWifiPass);
  }
  if (AUTO_START) gChannels = lastChannels;
  logLine("SYS", "ESP32 BAS MQTT over TLS: NB-IoT (SIM7022) + Wi-Fi channels");
  logLine("SYS", "ES-WS-04 RS485: RX=GPIO%d TX=GPIO%d, 4800 8N1, slave 1", RS485_RX_PIN, RS485_TX_PIN);
  logLine("SYS", "broker %s:%d TLS, MQTT 3.1.1, QoS 0, retain 0, keepalive %d s, clean session %d", MQTT_HOST,
          MQTT_PORT, MQTT_KEEPALIVE_S, MQTT_CLEAN_SESSION);
  printStatus();
  if (!gChannels) logLine("SYS", "waiting: choose a channel to start (MODE NB|WIFI|BOTH)");
  xTaskCreatePinnedToCore(windTask, "wind", 4096, nullptr, 2, nullptr, 0);
  xTaskCreatePinnedToCore(nbTask, "nb", 8192, nullptr, 1, nullptr, 1);
  xTaskCreatePinnedToCore(wifiTask, "wifi", 16384, nullptr, 1, nullptr, 1);
}

void loop() {
  if (AT_BRIDGE_ONLY) {
    nbBridgeLoop();
    delay(1);
    return;
  }
  pollCommands();
  // While idle, repeat the status so a page that connects later still sees it.
  static uint32_t lastIdleMsg = 0;
  if (!gChannels && millis() - lastIdleMsg >= 15000) {
    lastIdleMsg = millis();
    printStatus();
  }
  delay(20);
}
