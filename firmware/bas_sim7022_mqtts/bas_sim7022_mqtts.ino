#include <Arduino.h>
#include <math.h>
#include <stdarg.h>
#if __has_include("config.h")
#include "config.h"
#else
#include "config.example.h"
#endif
#include "ca_cert.h"

// ESP32 + SIM7022 NB-IoT -> MQTT 3.1.1 over TLS using the modem's own MQTT(S) stack.
// Commands follow SIM7022 Series AT Command Manual V1.05 (ch. 7 SSL, ch. 8 MQTT(S))
// and SIM7022 Series SSL Application Note V1.00. No PPP, Wi-Fi or MQTT library.
// Only fixed buffers are used; no String concatenation.

HardwareSerial modem(1);

static char rxBuf[256];   // line being received from the modem
static size_t rxLen = 0;
static char line[256];    // last complete line
static char cmd[384];     // outgoing AT command
static char cmdLog[384];  // same command with the password masked
static char payload[512];
static char lastErr[96];  // last ERROR / +CME ERROR text
static char fwVersion[96];

static bool mqttConnected = false;
static bool mqttLost = false;
static bool modemInfoDone = false;
static bool caReady = false;
static bool caReloadDone = false;
static bool halted = false;
static const char *haltReason = "";
static uint32_t nextAttempt = 0, lastPublish = 0, publishCount = 0;
static const uint8_t BACKOFF_S[] = {5, 10, 20, 30, 60};
static uint8_t backoffIndex = 0;

// Declared before any function: the Arduino builder inserts prototypes there.
struct Telemetry {
  float distance, sternDistance, bowSpeed, sternSpeed, angle, waterLevel, waterFlow;
  const char *waterDirection;
  int windForce;
  const char *windDirection;
};

enum AtResult { AT_OK, AT_ERROR, AT_TIMEOUT };
static const int RESULT_ERROR = -1, RESULT_TIMEOUT = -2;

// ---------------------------------------------------------------- helpers

static bool startsWith(const char *s, const char *p) { return strncmp(s, p, strlen(p)) == 0; }
static void copyStr(char *dst, size_t n, const char *src) { snprintf(dst, n, "%s", src); }

static bool containsNoCase(const char *s, const char *needle) {
  size_t n = strlen(needle);
  for (; *s; s++) if (strncasecmp(s, needle, n) == 0) return true;
  return false;
}

// Result code of "+XXX: a,b,<err>" lines: the last integer field.
static int lastInt(const char *l) {
  const char *p = strrchr(l, ',');
  if (!p) p = strchr(l, ':');
  return p ? atoi(p + 1) : RESULT_ERROR;
}

// SIM7022 AT Manual V1.05, 8.3.1
static const char *mqttErrText(int e) {
  static const char *const T[] = {
    "operation succeeded", "failed", "bad UTF-8 string", "sock connect fail", "sock create fail",
    "sock close fail", "message receive fail", "network open fail", "network close fail",
    "network not opened", "client index error", "no connection", "invalid parameter",
    "not supported operation", "client is busy", "require connection fail", "sock sending fail",
    "timeout", "topic is empty", "client is used", "client not acquired", "client not released",
    "length out of range", "network is opened", "packet fail", "DNS error",
    "socket is closed by server", "connection refused: unaccepted protocol version",
    "connection refused: identifier rejected", "connection refused: server unavailable",
    "connection refused: bad user name or password", "connection refused: not authorized",
    "handshake fail", "not set certificate", "Open session failed", "Disconnect from server failed"};
  return (e >= 0 && e < (int)(sizeof(T) / sizeof(T[0]))) ? T[e] : "unknown";
}

// ---------------------------------------------------------------- modem I/O

static void handleUrc(const char *l) {
  if (startsWith(l, "+CMQTTCONNLOST:")) {
    int idx = -1, cause = -1;
    sscanf(l, "+CMQTTCONNLOST: %d,%d", &idx, &cause);
    Serial.printf("! MQTT connection lost, cause %d (%s)\n", cause,
                  cause == 1 ? "socket closed passively" : cause == 2 ? "socket reset" :
                  cause == 3 ? "network closed" : "unknown");
    mqttLost = true;
  }
}

// Non-blocking. Returns true when `line` holds a complete, trimmed, non-empty line.
// With wantPrompt, a '>' at the start of a line is returned at once (it has no newline).
static bool pollLine(bool wantPrompt = false) {
  while (modem.available()) {
    char c = (char)modem.read();
    if (c == ' ' && rxLen == 0) continue;
    if (wantPrompt && c == '>' && rxLen == 0) { copyStr(line, sizeof line, ">"); return true; }
    if (c == '\r' || c == '\n') {
      if (!rxLen) continue;
      while (rxLen && rxBuf[rxLen - 1] == ' ') rxLen--;
      rxBuf[rxLen] = 0; rxLen = 0;
      copyStr(line, sizeof line, rxBuf);
      Serial.printf("< %s\n", line);
      handleUrc(line);
      return true;
    }
    if (rxLen < sizeof(rxBuf) - 1) rxBuf[rxLen++] = c;  // longer lines are truncated
  }
  return false;
}

static void drainLines(uint32_t ms) {
  uint32_t start = millis();
  while (millis() - start < ms) if (!pollLine()) delay(1);
}

static bool isErrorLine(const char *l) {
  return !strcmp(l, "ERROR") || startsWith(l, "+CME ERROR") || startsWith(l, "+CMS ERROR");
}

static void sendCmd(const char *c, const char *logText = nullptr) {
  drainLines(20);
  Serial.printf("> %s\n", logText ? logText : c);
  modem.print(c);
  modem.print("\r");
}

// Called for every intermediate line until OK / ERROR.
typedef void (*LineHook)(const char *l, void *ctx);

struct Capture { const char *prefix; char *out; size_t len; };
static void captureHook(const char *l, void *ctx) {
  Capture *c = (Capture *)ctx;
  if (!c->out[0] && startsWith(l, c->prefix)) copyStr(c->out, c->len, l);
}

struct Scan { const char *needle; bool found; };
static void scanHook(const char *l, void *ctx) {
  Scan *s = (Scan *)ctx;
  if (containsNoCase(l, s->needle)) s->found = true;
}

static AtResult waitFinal(uint32_t timeoutMs, LineHook hook = nullptr, void *ctx = nullptr) {
  uint32_t start = millis();
  lastErr[0] = 0;
  while (millis() - start < timeoutMs) {
    if (!pollLine()) { delay(1); continue; }
    if (!strcmp(line, "OK")) return AT_OK;
    if (isErrorLine(line)) { copyStr(lastErr, sizeof lastErr, line); return AT_ERROR; }
    if (hook) hook(line, ctx);
  }
  copyStr(lastErr, sizeof lastErr, "no response (timeout)");
  Serial.printf("! timeout after %lu ms\n", (unsigned long)timeoutMs);
  return AT_TIMEOUT;
}

static AtResult at(const char *c, uint32_t timeoutMs = 5000) {
  sendCmd(c);
  return waitFinal(timeoutMs);
}

// Sends a command and copies the first line starting with prefix into out.
static AtResult atGet(const char *c, uint32_t timeoutMs, const char *prefix, char *out, size_t len) {
  out[0] = 0;
  Capture cap = {prefix, out, len};
  sendCmd(c);
  return waitFinal(timeoutMs, captureHook, &cap);
}

static AtResult atf(uint32_t timeoutMs, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(cmd, sizeof cmd, fmt, ap);
  va_end(ap);
  return at(cmd, timeoutMs);
}

// For commands whose outcome arrives as "<prefix> ...,<err>" before or after OK/ERROR.
// Returns the result code, RESULT_ERROR when only ERROR came, or RESULT_TIMEOUT.
static int atResult(const char *c, const char *logText, const char *prefix, uint32_t timeoutMs) {
  sendCmd(c, logText);
  uint32_t start = millis(), errorAt = 0;
  lastErr[0] = 0;
  while (millis() - start < timeoutMs) {
    if (pollLine()) {
      if (startsWith(line, prefix)) {
        int rc = lastInt(line);
        drainLines(100);  // swallow a trailing ERROR that belongs to this result
        return rc;
      }
      if (isErrorLine(line)) {
        copyStr(lastErr, sizeof lastErr, line);
        if (!errorAt) errorAt = millis();
      }
    } else {
      if (errorAt && millis() - errorAt > 3000) return RESULT_ERROR;
      delay(1);
    }
  }
  if (errorAt) return RESULT_ERROR;
  copyStr(lastErr, sizeof lastErr, "no result (timeout)");
  return RESULT_TIMEOUT;
}

// "<cmd>" -> '>' prompt -> raw data -> OK. A "+XXX: idx,err" failure line goes to errOut.
static AtResult atInput(const char *c, const char *data, size_t len, uint32_t timeoutMs,
                        const char *errPrefix, char *errOut, size_t errLen) {
  errOut[0] = 0;
  sendCmd(c);
  uint32_t start = millis();
  while (millis() - start < 5000) {
    if (!pollLine(true)) { delay(1); continue; }
    if (!strcmp(line, ">")) {
      modem.write((const uint8_t *)data, len);
      Capture cap = {errPrefix, errOut, errLen};
      return waitFinal(timeoutMs, captureHook, &cap);
    }
    if (isErrorLine(line)) { copyStr(lastErr, sizeof lastErr, line); return AT_ERROR; }
    if (startsWith(line, errPrefix)) copyStr(errOut, errLen, line);
  }
  copyStr(lastErr, sizeof lastErr, "no '>' prompt");
  return AT_TIMEOUT;
}

static int rcOf(AtResult r, const char *errLine) {
  if (r == AT_OK) return 0;
  if (errLine[0]) return lastInt(errLine);
  return r == AT_TIMEOUT ? RESULT_TIMEOUT : RESULT_ERROR;
}

// ---------------------------------------------------------------- diagnostics

static bool fail(const char *step, const char *hint) {
  Serial.printf("! [%s] FAILED: %s\n", step, hint);
  if (lastErr[0]) Serial.printf("  last modem response: %s\n", lastErr);
  return false;
}

static bool mqttFail(const char *step, int rc) {
  if (rc >= 0) Serial.printf("! [%s] FAILED: MQTT error %d = %s\n", step, rc, mqttErrText(rc));
  else Serial.printf("! [%s] FAILED: %s\n", step, rc == RESULT_TIMEOUT ? "no result (timeout)" : "ERROR");
  if (lastErr[0]) Serial.printf("  last modem response: %s\n", lastErr);
  switch (rc) {
    case 32:
      Serial.println("  hint: TLS handshake failed. Check CA matches the broker chain, modem clock (AT+CCLK?),");
      Serial.println("        TLS1.2 support, and that the NB-IoT link is stable during the handshake.");
      Serial.println("        Tested 2026-09-24 on SIM7022 2110B07: servers with a wildcard certificate");
      Serial.println("        (*.emqx.io, *.flespi.io) always fail with 32; exact-name certificates connect.");
      caReady = false;  // re-check the CA on the next attempt
      break;
    case 33:
      Serial.println("  hint: certificate not set. Check AT+CCERTLIST and AT+CSSLCFG? (cacert of context 0).");
      caReady = false;
      break;
    case 25: Serial.println("  hint: DNS failed. The APN must provide Internet/DNS; check AT+CGPADDR."); break;
    case 3: case 4: case 26:
      Serial.println("  hint: TCP to broker failed/closed. Port 8883 blocked on APN? Broker down? Weak signal?");
      break;
    case 27: case 28: case 29: case 30: case 31:
      Serial.println("  hint: broker refused CONNECT (protocol, client id or credentials).");
      break;
    case 14: case 19: case 21:
      Serial.println("  hint: modem MQTT client still busy/in use; it is cleaned up on the next attempt.");
      break;
    case 11: Serial.println("  hint: not connected; reconnecting."); break;
    default: break;
  }
  return false;
}

// ---------------------------------------------------------------- bring-up steps

static int modemRxPin = MODEM_RX_PIN, modemTxPin = MODEM_TX_PIN;
static uint32_t modemBaud = MODEM_BAUD;
static bool uartProbed = false;

static void modemBegin(uint32_t baud, int rx, int tx) {
  modem.end();
  modem.setRxBufferSize(2048);
  modem.begin(baud, SERIAL_8N1, rx, tx);
  rxLen = 0;
}

// Runs once per boot when AT gets no answer: tries both pin orders and common baud rates,
// prints what arrives on RX, and adopts the first combination that answers OK.
static bool probeUart() {
  static const uint32_t BAUDS[] = {115200, 9600, 19200, 38400, 57600, 230400, 460800, 921600};
  Serial.println("[2d] UART probe: both TX/RX orders x common baud rates (pins swapped in software)");
  for (int swap = 0; swap < 2; swap++) {
    int rx = swap ? MODEM_TX_PIN : MODEM_RX_PIN, tx = swap ? MODEM_RX_PIN : MODEM_TX_PIN;
    for (uint32_t baud : BAUDS) {
      modemBegin(baud, rx, tx);
      delay(30);
      while (modem.available()) modem.read();
      uint8_t buf[48];
      size_t n = 0;
      for (int k = 0; k < 2; k++) {
        modem.print("AT\r");
        uint32_t start = millis();
        while (millis() - start < 400) {
          while (modem.available()) { int c = modem.read(); if (n < sizeof buf) buf[n++] = (uint8_t)c; }
          delay(1);
        }
      }
      bool ok = false, allZero = n > 0;
      for (size_t i = 0; i < n; i++) {
        if (buf[i]) allZero = false;
        if (i + 1 < n && buf[i] == 'O' && buf[i + 1] == 'K') ok = true;
      }
      Serial.printf("  RX=GPIO%d TX=GPIO%d %6lu baud: %2u bytes", rx, tx, (unsigned long)baud, (unsigned)n);
      for (size_t i = 0; i < n && i < 12; i++) Serial.printf(" %02X", buf[i]);
      Serial.println(ok ? "  <- OK" : allZero ? "  (only 0x00: line held low)" : "");
      if (ok) {
        modemRxPin = rx; modemTxPin = tx; modemBaud = baud;
        Serial.printf("  => modem answers on RX=GPIO%d TX=GPIO%d at %lu baud (used for this boot).\n",
                      rx, tx, (unsigned long)baud);
        Serial.println("     Put these values in MODEM_RX_PIN / MODEM_TX_PIN / MODEM_BAUD (config.h).");
        drainLines(200);
        return true;
      }
    }
  }
  modemBegin(modemBaud, modemRxPin, modemTxPin);
  Serial.println("  => no OK on any combination.");
  Serial.println("     0 bytes everywhere : nothing arrives from the modem: modem off/asleep, TXD not wired, no GND, or no 5V.");
  Serial.println("     only 0x00          : modem UART line held low: modem powered but not running (press WAKE/RST).");
  Serial.println("     other bytes, no OK : something is connected; check which header pin is TXD/RXD.");
  return false;
}

static bool stepModem() {
  Serial.println("[2] Modem check (AT)");
  for (int i = 0;; i++) {
    if (at("AT", 1000) == AT_OK) break;
    if (i == 4) {
      if (!uartProbed) {
        uartProbed = true;
        if (probeUart() && at("AT", 1000) == AT_OK) break;
      }
      return fail("AT", "no answer: check modem power/PWRKEY, GND, TX/RX crossover, baud, UART voltage level");
    }
    delay(1000);
  }
  if (at("ATE0") != AT_OK) return fail("ATE0", "cannot disable echo");
  at("AT+CMEE=2");  // verbose +CME ERROR text
  return true;
}

static bool stepVersion() {
  if (modemInfoDone) return true;
  Serial.println("[3] Modem firmware (AT+CGMR)");
  at("ATI", 3000);
  if (atGet("AT+CGMR", 3000, "", fwVersion, sizeof fwVersion) != AT_OK) return fail("CGMR", "cannot read firmware version");
  Serial.printf("  firmware: %s\n", fwVersion);
  // The MQTT(S) and SSL command groups exist only on newer SIM7022 firmware (AT manual >= V1.04).
  const char *const probes[] = {"AT+CSSLCFG=?", "AT+CCERTDOWN=?", "AT+CMQTTSTART=?"};
  for (const char *p : probes) {
    AtResult r = at(p, 3000);
    if (r == AT_TIMEOUT) return fail("probe", "modem did not answer the capability probe");
    if (r == AT_ERROR) {
      halted = true;
      haltReason = "modem firmware has no AT+CSSLCFG/AT+CCERTDOWN/AT+CMQTT* (MQTT over TLS). "
                   "Send the AT+CGMR value to SIMCom/distributor for a firmware update.";
      return fail("probe", haltReason);
    }
  }
  modemInfoDone = true;
  return true;
}

static bool stepSim() {
  Serial.println("[4] SIM (AT+CPIN?)");
  char r[64];
  for (int i = 0; i < 5; i++) {
    if (atGet("AT+CPIN?", 5000, "+CPIN:", r, sizeof r) == AT_OK && strstr(r, "READY")) return true;
    delay(2000);
  }
  return fail("SIM", "SIM not READY (missing, PIN locked or not activated). PIN is never entered automatically.");
}

static bool waitRegistered(uint32_t timeoutMs) {
  uint32_t start = millis(), lastCsq = 0;
  char r[64];
  for (;;) {
    int n = -1, stat = -1;
    if (atGet("AT+CEREG?", 5000, "+CEREG:", r, sizeof r) == AT_OK) sscanf(r, "+CEREG: %d,%d", &n, &stat);
    if (stat == 1 || stat == 5) {
      Serial.printf("  registered (%s)\n", stat == 1 ? "home" : "roaming");
      at("AT+COPS?");  // operator and access technology (9 = NB-IoT)
      return true;
    }
    if (stat == 3) Serial.println("  registration denied by the network");
    if (millis() - start >= timeoutMs) return false;
    if (!lastCsq || millis() - lastCsq >= 15000) { at("AT+CSQ"); lastCsq = millis(); }
    delay(3000);
  }
}

static bool stepRegister() {
  Serial.println("[5] NB-IoT registration (AT+CEREG?)");
  char r[32];
  if (atGet("AT+CFUN?", 5000, "+CFUN:", r, sizeof r) == AT_OK && lastInt(r) != 1 &&
      at("AT+CFUN=1", 15000) != AT_OK) return fail("CFUN=1", "cannot switch the radio on");
  at("AT+CPSMS=0");   // keep the modem awake during the test
  at("AT+CEDRXS=0");
  if (!waitRegistered(180000))
    return fail("CEREG", "not registered in 180 s: antenna, NB-IoT coverage, SIM NB-IoT service, band");
  return true;
}

static bool stepApn() {
  Serial.println("[6] APN / PDP context (AT+CGDCONT)");
  if (!strlen(NB_APN)) {
    at("AT+CGDCONT?");
    Serial.println("  NB_APN is empty: using the PDP profile stored in the modem");
    return true;
  }
  char want[80];
  snprintf(want, sizeof want, ",\"%s", NB_APN);
  Scan scan = {want, false};
  sendCmd("AT+CGDCONT?");
  waitFinal(5000, scanHook, &scan);
  if (scan.found) { Serial.println("  APN already configured"); return true; }
  Serial.printf("  setting APN \"%s\" (radio off/on, then re-registration)\n", NB_APN);
  if (at("AT+CFUN=0", 15000) != AT_OK) return fail("CFUN=0", "cannot switch the radio off");
  if (atf(5000, "AT+CGDCONT=1,\"IP\",\"%s\"", NB_APN) != AT_OK) return fail("CGDCONT", "APN rejected");
  if (at("AT+CFUN=1", 15000) != AT_OK) return fail("CFUN=1", "cannot switch the radio on");
  if (!waitRegistered(180000)) return fail("CEREG", "not registered after the APN change");
  return true;
}

// Accepts "+CGPADDR: <cid>,<addr>" with a non-zero IPv4/IPv6 address.
struct IpScan { char ip[48]; };
static void ipHook(const char *l, void *ctx) {
  IpScan *s = (IpScan *)ctx;
  if (s->ip[0] || !startsWith(l, "+CGPADDR:")) return;
  const char *p = strchr(l, ',');
  if (!p) return;
  p++;
  if (*p == '"') p++;
  char ip[48];
  size_t n = 0;
  while (*p && *p != '"' && *p != ',' && n < sizeof(ip) - 1) ip[n++] = *p++;
  ip[n] = 0;
  if ((strchr(ip, '.') || strchr(ip, ':')) && strcmp(ip, "0.0.0.0")) copyStr(s->ip, sizeof s->ip, ip);
}

static bool stepIp() {
  Serial.println("[7] IP address (AT+CGPADDR)");
  uint32_t start = millis();
  while (millis() - start < 60000) {
    IpScan s = {""};
    sendCmd("AT+CGPADDR");
    if (waitFinal(5000, ipHook, &s) == AT_OK && s.ip[0]) {
      Serial.printf("  IP: %s\n", s.ip);
      return true;
    }
    at("AT+CGATT?");
    delay(3000);
  }
  return fail("IP", "no IP address in 60 s: check the APN has Internet access and attach (AT+CGATT?)");
}

static bool clockValid(char *out, size_t len) {
  if (atGet("AT+CCLK?", 5000, "+CCLK:", out, len) != AT_OK) return false;
  const char *q = strchr(out, '"');
  int yy = q ? atoi(q + 1) : 0;  // "yy/MM/dd,..." or "yyyy/MM/dd,..." (SIM7022 R2110)
  if (yy >= 2000) yy -= 2000;
  return yy >= 25 && yy < 70;
}

static bool stepTime() {
  Serial.println("[8] Network time (NITZ, then NTP)");
  at("AT+CTZU=1");  // accept time/zone updates from the network
  char r[64];
  if (clockValid(r, sizeof r)) { Serial.printf("  clock OK: %s\n", r); return true; }
  Serial.println("  clock not set by the network, trying NTP");
  if (atf(5000, "AT+CNTP=\"%s\",%d", NTP_SERVER, NTP_TZ_QUARTERS) != AT_OK) return fail("CNTP", "cannot set the NTP server");
  int rc = atResult("AT+CNTP", nullptr, "+CNTP:", 65000);
  if (rc != 0) {
    Serial.printf("  NTP result code %d\n", rc);
    return fail("CNTP", "NTP sync failed (IP/DNS/UDP 123). Certificate time check needs a correct clock.");
  }
  if (!clockValid(r, sizeof r)) return fail("CCLK", "clock still invalid after NTP");
  Serial.printf("  clock OK: %s\n", r);
  return true;
}

static bool stepCa() {
  if (caReady) return true;
  Serial.println("[9a] Root CA in modem (AT+CCERTLIST)");
  Scan scan = {CA_FILE_NAME, false};
  sendCmd("AT+CCERTLIST");
  if (waitFinal(12000, scanHook, &scan) != AT_OK) return fail("CCERTLIST", "cannot list certificates");
  if (scan.found && CA_FORCE_RELOAD && !caReloadDone) {
    atf(12000, "AT+CCERTDELE=\"%s\"", CA_FILE_NAME);
    scan.found = false;
  }
  caReloadDone = true;
  if (!scan.found) {
    size_t len = sizeof(CA_CERT_PEM) - 1;
    Serial.printf("  downloading %s (%u bytes PEM)\n", CA_FILE_NAME, (unsigned)len);
    snprintf(cmd, sizeof cmd, "AT+CCERTDOWN=\"%s\",%u", CA_FILE_NAME, (unsigned)len);
    char r[48];
    if (atInput(cmd, CA_CERT_PEM, len, 120000, "+CCERTDOWN:", r, sizeof r) != AT_OK)
      return fail("CCERTDOWN", "CA download failed");
    scan.found = false;
    sendCmd("AT+CCERTLIST");
    if (waitFinal(12000, scanHook, &scan) != AT_OK || !scan.found)
      return fail("CCERTLIST", "CA not listed after download");
  }
  Serial.printf("  CA %s present\n", CA_FILE_NAME);
  caReady = true;
  return true;
}

static bool stepSslConfig() {
  Serial.println("[9b] SSL context 0: TLS1.2, verify server, check time, SNI");
  if (atf(12000, "AT+CSSLCFG=\"sslversion\",0,3") != AT_OK ||          // 3 = TLS1.2
      atf(12000, "AT+CSSLCFG=\"authmode\",0,1") != AT_OK ||            // 1 = verify server
      atf(12000, "AT+CSSLCFG=\"ignorelocaltime\",0,0") != AT_OK ||     // 0 = check validity time
      atf(12000, "AT+CSSLCFG=\"negotiatetime\",0,%d", TLS_NEGOTIATE_S) != AT_OK ||
      atf(12000, "AT+CSSLCFG=\"cacert\",0,\"%s\"", CA_FILE_NAME) != AT_OK ||
      atf(12000, "AT+CSSLCFG=\"enableSNI\",0,1") != AT_OK)
    return fail("CSSLCFG", "SSL context configuration rejected");
  at("AT+CSSLCFG?", 12000);  // log the effective settings
  return true;
}

// Best effort: after an ESP32 reset the modem may still hold the old client or service.
static void mqttCleanup() {
  atResult("AT+CMQTTDISC=0,60", nullptr, "+CMQTTDISC:", 65000);
  at("AT+CMQTTREL=0");
  atResult("AT+CMQTTSTOP", nullptr, "+CMQTTSTOP:", 15000);
  mqttConnected = false;
}

static bool stepMqttConnect() {
  Serial.println("[9c] MQTT over TLS");
  mqttCleanup();
  int rc = atResult("AT+CMQTTSTART", nullptr, "+CMQTTSTART:", 15000);
  if (rc == RESULT_ERROR) Serial.println("  MQTT service already started, continuing");
  else if (rc != 0) return mqttFail("CMQTTSTART", rc);

  char r[64];
  snprintf(cmd, sizeof cmd, "AT+CMQTTACCQ=0,\"%s\",1", MQTT_CLIENT_ID);  // server_type 1 = SSL/TLS
  AtResult a = atGet(cmd, 5000, "+CMQTTACCQ:", r, sizeof r);
  if (a != AT_OK) return mqttFail("CMQTTACCQ", rcOf(a, r));
  if (at("AT+CMQTTSSLCFG=0,0") != AT_OK) return fail("CMQTTSSLCFG", "cannot bind SSL context 0 to MQTT client 0");

  // server_addr must start with "tcp://" even for TLS; TLS is chosen by server_type above.
  int n = snprintf(cmd, sizeof cmd, "AT+CMQTTCONNECT=0,\"tcp://%s:%d\",%d,%d",
                   MQTT_HOST, MQTT_PORT, MQTT_KEEPALIVE_S, MQTT_CLEAN_SESSION);
  copyStr(cmdLog, sizeof cmdLog, cmd);
  if (strlen(MQTT_USER)) {
    snprintf(cmd + n, sizeof cmd - n, ",\"%s\"", MQTT_USER);
    snprintf(cmdLog + n, sizeof cmdLog - n, ",\"%s\"", MQTT_USER);
    if (strlen(MQTT_PASS)) {
      size_t m = strlen(cmd), k = strlen(cmdLog);
      snprintf(cmd + m, sizeof cmd - m, ",\"%s\"", MQTT_PASS);
      snprintf(cmdLog + k, sizeof cmdLog - k, ",\"***\"");
    }
  }
  Serial.println("  TLS handshake + MQTT CONNECT (can take a few minutes on NB-IoT)");
  rc = atResult(cmd, cmdLog, "+CMQTTCONNECT:", (TLS_NEGOTIATE_S + 60) * 1000UL);
  if (rc != 0) return mqttFail("CMQTTCONNECT", rc);
  mqttConnected = true;
  mqttLost = false;
  Serial.printf("[9] MQTT connected to %s:%d over TLS as %s\n", MQTT_HOST, MQTT_PORT, MQTT_CLIENT_ID);
  return true;
}

static bool bringUp() {
  return stepModem() && stepVersion() && stepSim() && stepRegister() && stepApn() && stepIp() &&
         stepTime() && stepCa() && stepSslConfig() && stepMqttConnect();
}

// ---------------------------------------------------------------- telemetry

// BAS test values. Replace with real sensor readings; field names are fixed by BAS.
static Telemetry readTelemetry() {
  return {120.5f, 118.2f, 2.1f, 1.8f, 1.2f, 4.2f, 0.3f, "NE", 15, "NE"};
}

static const char *jsonNum(char *buf, size_t len, float v) {
  if (isfinite(v)) snprintf(buf, len, "%.1f", v);
  else copyStr(buf, len, "null");  // NaN/Inf are not valid JSON
  return buf;
}

// Only 16-point compass values are emitted as strings, so no escaping is ever needed.
[[maybe_unused]] static const char *jsonDir(char *buf, size_t len, const char *d) {
  static const char *const DIRS[] = {"N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
                                     "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"};
  for (const char *x : DIRS)
    if (d && !strcmp(d, x)) { snprintf(buf, len, "\"%s\"", x); return buf; }
  copyStr(buf, len, "null");
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
  char r[48];
  snprintf(cmd, sizeof cmd, "AT+CMQTTTOPIC=0,%u", (unsigned)strlen(MQTT_TOPIC));
  AtResult a = atInput(cmd, MQTT_TOPIC, strlen(MQTT_TOPIC), 10000, "+CMQTTTOPIC:", r, sizeof r);
  if (a != AT_OK) return mqttFail("CMQTTTOPIC", rcOf(a, r));
  snprintf(cmd, sizeof cmd, "AT+CMQTTPAYLOAD=0,%u", (unsigned)len);
  a = atInput(cmd, payload, len, 10000, "+CMQTTPAYLOAD:", r, sizeof r);
  if (a != AT_OK) return mqttFail("CMQTTPAYLOAD", rcOf(a, r));
  snprintf(cmd, sizeof cmd, "AT+CMQTTPUB=0,%d,60,%d", MQTT_QOS, MQTT_RETAIN);
  int rc = atResult(cmd, nullptr, "+CMQTTPUB:", 70000);
  if (rc != 0) return mqttFail("CMQTTPUB", rc);
  if (MQTT_QOS == 0)
    Serial.println("[11] Publish OK (QoS 0: the modem sent it; the broker sends no PUBACK, confirm with a subscriber)");
  else
    Serial.println("[11] Publish OK (acknowledged by the broker)");
  return true;
}

// ---------------------------------------------------------------- main

static bool configValid() {
  const char *const quoted[] = {MQTT_HOST, MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS, NB_APN, CA_FILE_NAME, NTP_SERVER};
  for (const char *s : quoted)
    if (strchr(s, '"')) { haltReason = "config.h: values must not contain '\"'"; return false; }
  if (!strlen(MQTT_CLIENT_ID) || strlen(MQTT_CLIENT_ID) > 256) { haltReason = "MQTT_CLIENT_ID must be 1-256 bytes"; return false; }
  if (!strlen(MQTT_TOPIC) || strlen(MQTT_TOPIC) > 1024) { haltReason = "MQTT_TOPIC must be 1-1024 bytes"; return false; }
  if (!strlen(CA_FILE_NAME) || strlen(CA_FILE_NAME) > 53) { haltReason = "CA_FILE_NAME must be 1-53 bytes"; return false; }
  if (sizeof(CA_CERT_PEM) - 1 > 10240) { haltReason = "CA certificate larger than 10240 bytes"; return false; }
  if (MQTT_QOS < 0 || MQTT_QOS > 2) { haltReason = "MQTT_QOS must be 0-2"; return false; }
  if (TLS_NEGOTIATE_S < 10 || TLS_NEGOTIATE_S > 300) { haltReason = "TLS_NEGOTIATE_S must be 10-300"; return false; }
  return true;
}

static void scheduleRetry() {
  uint8_t s = BACKOFF_S[backoffIndex];
  if (backoffIndex < sizeof(BACKOFF_S) - 1) backoffIndex++;
  nextAttempt = millis() + s * 1000UL;
  Serial.printf("Retry in %u s\n", s);
}

void setup() {
  Serial.begin(115200);
  modemBegin(MODEM_BAUD, MODEM_RX_PIN, MODEM_TX_PIN);
  delay(1500);
  Serial.println();
  Serial.println("ESP32 + SIM7022 NB-IoT -> MQTT over TLS (BAS test)");
  Serial.printf("[1] Modem UART: ESP32 RX=GPIO%d, TX=GPIO%d, %d baud\n", MODEM_RX_PIN, MODEM_TX_PIN, MODEM_BAUD);
  Serial.printf("  broker %s:%d TLS, MQTT 3.1.1, client id %s\n", MQTT_HOST, MQTT_PORT, MQTT_CLIENT_ID);
  Serial.printf("  topic %s, keepalive %d s, QoS %d, retain %d, clean session %d, credentials %s\n",
                MQTT_TOPIC, MQTT_KEEPALIVE_S, MQTT_QOS, MQTT_RETAIN, MQTT_CLEAN_SESSION,
                strlen(MQTT_USER) ? "set (hidden)" : "none");
  Serial.println("  Power the modem on manually; RESET/PWRKEY are not driven.");
  if (!configValid()) halted = true;
}

void loop() {
  if (AT_BRIDGE_ONLY) {
    while (Serial.available()) modem.write(Serial.read());
    while (modem.available()) Serial.write(modem.read());
    delay(1);
    return;
  }
  if (halted) {
    static uint32_t lastMsg = 0;
    if (!lastMsg || millis() - lastMsg >= 60000) {
      lastMsg = millis();
      Serial.printf("! HALTED: %s\n", haltReason);
    }
    delay(100);
    return;
  }
  while (pollLine()) {}
  if (mqttConnected && mqttLost) {
    mqttConnected = false;
    Serial.println("MQTT link lost, reconnecting");
    scheduleRetry();
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
      scheduleRetry();
    }
  }
  delay(10);
}
