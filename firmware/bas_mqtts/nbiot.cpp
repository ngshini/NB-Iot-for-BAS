// NB-IoT channel: SIM7022 MQTT(S) stack driven by AT commands over UART1.
// Commands follow SIM7022 Series AT Command Manual V1.05 (ch. 7 SSL, ch. 8 MQTT(S))
// and SIM7022 Series SSL Application Note V1.00. Fixed buffers only; no String concatenation.
#include <Arduino.h>
#include <stdarg.h>
#include "common.h"
#if __has_include("config.h")
#include "config.h"
#else
#include "config.example.h"
#endif
#include "nb_tls_profile.h"
// Keep endpoint and trust anchor paired even when a diagnostic build is selected.
#undef NB_MQTT_HOST
#undef NB_MQTT_PORT
#undef CA_FILE_NAME
#define NB_MQTT_HOST NB_TLS_HOST
#define NB_MQTT_PORT NB_TLS_PORT
#define CA_FILE_NAME NB_TLS_CA_FILE

#define NB_LOG(...) logLine("NB", __VA_ARGS__)

static HardwareSerial modem(1);
volatile bool gNbDiagnostics = false;

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

static int modemRxPin = MODEM_RX_PIN, modemTxPin = MODEM_TX_PIN;
static uint32_t modemBaud = MODEM_BAUD;
static bool uartProbed = false;

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
    NB_LOG("! MQTT connection lost, cause %d (%s)", cause,
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
      NB_LOG("< %s", line);
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
  NB_LOG("> %s", logText ? logText : c);
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

static void certificateHook(const char *l, void *ctx) {
  Scan *s = (Scan *)ctx;
  if (nbCertificateListed(l, s->needle)) s->found = true;
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
  NB_LOG("! timeout after %lu ms", (unsigned long)timeoutMs);
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
  NB_LOG("! [%s] FAILED: %s", step, hint);
  if (lastErr[0]) NB_LOG("  last modem response: %s", lastErr);
  return false;
}

static bool mqttFail(const char *step, int rc) {
  if (rc >= 0) NB_LOG("! [%s] FAILED: MQTT error %d = %s", step, rc, mqttErrText(rc));
  else NB_LOG("! [%s] FAILED: %s", step, rc == RESULT_TIMEOUT ? "no result (timeout)" : "ERROR");
  if (lastErr[0]) NB_LOG("  last modem response: %s", lastErr);
  switch (rc) {
    case 32:
      NB_LOG("  hint: TLS handshake failed. Check CA matches the broker chain, modem clock (AT+CCLK?),");
      NB_LOG("        TLS1.2 support, and that the NB-IoT link is stable during the handshake.");
      NB_LOG("        Code 32 does not identify the root cause; check fresh CA, time and TLS compatibility.");
      caReady = false;  // re-check the CA on the next attempt
      break;
    case 33:
      NB_LOG("  hint: certificate not set. Check AT+CCERTLIST and AT+CSSLCFG? (cacert of context 0).");
      caReady = false;
      break;
    case 25: NB_LOG("  hint: DNS failed. The APN must provide Internet/DNS; check AT+CGPADDR."); break;
    case 3: case 4: case 26:
      NB_LOG("  TCP to %s:%d failed. Check PDP, APN routing and radio; cause not yet determined.", NB_MQTT_HOST, NB_MQTT_PORT);
      break;
    case 27: case 28: case 29: case 30: case 31:
      NB_LOG("  hint: broker refused CONNECT (protocol, client id or credentials).");
      break;
    case 14: case 19: case 21:
      NB_LOG("  hint: modem MQTT client still busy/in use; it is cleaned up on the next attempt.");
      break;
    case 11: NB_LOG("  hint: not connected; reconnecting."); break;
    default: break;
  }
  return false;
}

// ---------------------------------------------------------------- bring-up steps

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
  NB_LOG("[2d] UART probe: both TX/RX orders x common baud rates (pins swapped in software)");
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
      char hex[40] = "";
      for (size_t i = 0; i < n; i++) {
        if (buf[i]) allZero = false;
        if (i + 1 < n && buf[i] == 'O' && buf[i + 1] == 'K') ok = true;
        if (i < 12) snprintf(hex + strlen(hex), sizeof hex - strlen(hex), " %02X", buf[i]);
      }
      NB_LOG("  RX=GPIO%d TX=GPIO%d %6lu baud: %2u bytes%s%s", rx, tx, (unsigned long)baud, (unsigned)n, hex,
             ok ? "  <- OK" : allZero ? "  (only 0x00: line held low)" : "");
      if (ok) {
        modemRxPin = rx; modemTxPin = tx; modemBaud = baud;
        NB_LOG("  => modem answers on RX=GPIO%d TX=GPIO%d at %lu baud (used for this boot).", rx, tx,
               (unsigned long)baud);
        NB_LOG("     Put these values in MODEM_RX_PIN / MODEM_TX_PIN / MODEM_BAUD (config.h).");
        drainLines(200);
        return true;
      }
    }
  }
  modemBegin(modemBaud, modemRxPin, modemTxPin);
  NB_LOG("  => no OK on any combination.");
  NB_LOG("     0 bytes everywhere : nothing arrives from the modem: modem off/asleep, TXD not wired, no GND, or no 5V.");
  NB_LOG("     only 0x00          : modem UART line held low: modem powered but not running (press WAKE/RST).");
  NB_LOG("     other bytes, no OK : something is connected; check which header pin is TXD/RXD.");
  return false;
}

static bool stepModem() {
  NB_LOG("[2] Modem check (AT)");
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
  NB_LOG("[3] Modem firmware (AT+CGMR)");
  at("ATI", 3000);
  if (atGet("AT+CGMR", 3000, "", fwVersion, sizeof fwVersion) != AT_OK) return fail("CGMR", "cannot read firmware version");
  NB_LOG("  firmware: %s", fwVersion);
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
  NB_LOG("[4] SIM (AT+CPIN?)");
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
      NB_LOG("  registered (%s)", stat == 1 ? "home" : "roaming");
      at("AT+COPS?");  // operator and access technology (9 = NB-IoT)
      return true;
    }
    if (stat == 3) NB_LOG("  registration denied by the network");
    if (millis() - start >= timeoutMs) return false;
    if (!lastCsq || millis() - lastCsq >= 15000) { at("AT+CSQ"); lastCsq = millis(); }
    delay(3000);
  }
}

static bool stepRegister() {
  NB_LOG("[5] NB-IoT registration (AT+CEREG?)");
  char r[32];
  if (atGet("AT+CFUN?", 5000, "+CFUN:", r, sizeof r) == AT_OK && lastInt(r) != 1 &&
      at("AT+CFUN=1", 15000) != AT_OK) return fail("CFUN=1", "cannot switch the radio on");
  at("AT+CPSMS=0");   // keep the modem awake during the test
  at("AT+CEDRXS=0");
  at("AT+CSQ");
  if (!waitRegistered(180000))
    return fail("CEREG", "not registered in 180 s: antenna, NB-IoT coverage, SIM NB-IoT service, band");
  return true;
}

static bool stepApn() {
  NB_LOG("[6] APN / PDP context (AT+CGDCONT)");
  if (!strlen(NB_APN)) {
    at("AT+CGDCONT?");
    NB_LOG("  NB_APN is empty: using the PDP profile stored in the modem");
    return true;
  }
  char want[80];
  snprintf(want, sizeof want, ",\"%s", NB_APN);
  Scan scan = {want, false};
  sendCmd("AT+CGDCONT?");
  waitFinal(5000, scanHook, &scan);
  if (scan.found) { NB_LOG("  APN already configured"); return true; }
  NB_LOG("  setting APN \"%s\" (radio off/on, then re-registration)", NB_APN);
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
  char attached[48];
  if (atGet("AT+CGATT?", 10000, "+CGATT:", attached, sizeof attached) != AT_OK || lastInt(attached) != 1)
    return fail("CGATT", "packet data not attached; verify SIM service and registration");
  at("AT+CGACT?", 10000);
  at("AT+CESQ", 10000);
  NB_LOG("[7] IP address (AT+CGPADDR)");
  uint32_t start = millis();
  while (millis() - start < 60000) {
    IpScan s = {""};
    sendCmd("AT+CGPADDR");
    if (waitFinal(5000, ipHook, &s) == AT_OK && s.ip[0]) {
      NB_LOG("  IP: %s", s.ip);
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
  NB_LOG("[8] Network time (NITZ, then NTP)");
  at("AT+CTZU=1");  // accept time/zone updates from the network
  char r[64];
  if (clockValid(r, sizeof r)) { NB_LOG("  clock OK: %s", r); return true; }
  NB_LOG("  clock not set by the network, trying NTP");
  if (atf(5000, "AT+CNTP=\"%s\",%d", NTP_SERVER, NTP_TZ_QUARTERS) != AT_OK) return fail("CNTP", "cannot set the NTP server");
  int rc = atResult("AT+CNTP", nullptr, "+CNTP:", 65000);
  if (rc != 0) {
    NB_LOG("  NTP result code %d", rc);
    return fail("CNTP", "NTP sync failed (IP/DNS/UDP 123). Certificate time check needs a correct clock.");
  }
  if (!clockValid(r, sizeof r)) return fail("CCLK", "clock still invalid after NTP");
  NB_LOG("  clock OK: %s", r);
  return true;
}

static bool stepCa() {
  if (caReady) return true;
  NB_LOG("[9a] Root CA in modem (AT+CCERTLIST)");
  Scan scan = {CA_FILE_NAME, false};
  sendCmd("AT+CCERTLIST");
  if (waitFinal(12000, certificateHook, &scan) != AT_OK) return fail("CCERTLIST", "cannot list certificates");
  if (scan.found && CA_FORCE_RELOAD && !caReloadDone) {
    if (atf(12000, "AT+CCERTDELE=\"%s\"", CA_FILE_NAME) != AT_OK)
      return fail("CCERTDELE", "cannot replace CA; refusing to continue with an unknown certificate");
    scan.found = false;
  }
  if (!scan.found) {
    size_t len = sizeof(CA_CERT_PEM) - 1;
    NB_LOG("  downloading %s (%u bytes PEM)", CA_FILE_NAME, (unsigned)len);
    snprintf(cmd, sizeof cmd, "AT+CCERTDOWN=\"%s\",%u", CA_FILE_NAME, (unsigned)len);
    char r[48];
    if (atInput(cmd, CA_CERT_PEM, len, 120000, "+CCERTDOWN:", r, sizeof r) != AT_OK)
      return fail("CCERTDOWN", "CA download failed");
    scan.found = false;
    sendCmd("AT+CCERTLIST");
    if (waitFinal(12000, certificateHook, &scan) != AT_OK || !scan.found)
      return fail("CCERTLIST", "CA not listed after download");
  }
  caReloadDone = true;
  NB_LOG("  CA %s present (filename only; TLS must still verify server)", CA_FILE_NAME);
  caReady = true;
  return true;
}

static bool stepSslConfig() {
  NB_LOG("[9b] SSL context 0: TLS1.2, verify server, check time, SNI");
  if (atf(12000, "AT+CSSLCFG=\"sslversion\",0,3") != AT_OK ||          // 3 = TLS1.2
      atf(12000, "AT+CSSLCFG=\"authmode\",0,1") != AT_OK ||            // 1 = verify server
      atf(12000, "AT+CSSLCFG=\"ignorelocaltime\",0,0") != AT_OK ||     // 0 = check validity time
      atf(12000, "AT+CSSLCFG=\"negotiatetime\",0,%d", TLS_NEGOTIATE_S) != AT_OK ||
      atf(12000, "AT+CSSLCFG=\"cacert\",0,\"%s\"", CA_FILE_NAME) != AT_OK ||
      atf(12000, "AT+CSSLCFG=\"enableSNI\",0,1") != AT_OK)
    return fail("CSSLCFG", "SSL context configuration rejected");
  if (at("AT+CSSLCFG?", 12000) != AT_OK)
    return fail("CSSLCFG", "cannot read SSL settings after configuration");
  return true;
}

// An ESP32 reset does not reset the SIM7022. A CONNECT started by the previous firmware
// can therefore finish tens of seconds later and its URC must be consumed before REL/STOP.
static bool waitPendingMqttConnect(uint32_t timeoutMs) {
  NB_LOG("  waiting for the previous MQTT CONNECT operation to finish");
  uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    if (!pollLine()) { delay(1); continue; }
    if (startsWith(line, "+CMQTTCONNECT:")) {
      int rc = lastInt(line);
      NB_LOG("  previous MQTT CONNECT finished with %d (%s)", rc, mqttErrText(rc));
      drainLines(200);
      return true;
    }
  }
  NB_LOG("  no delayed MQTT CONNECT result received; continuing forced cleanup");
  return false;
}

// Best effort: after an ESP32 reset the modem may still hold the old client or service.
static bool mqttCleanup() {
  int disc = atResult("AT+CMQTTDISC=0,60", nullptr, "+CMQTTDISC:", 65000);
  if (disc == RESULT_ERROR || disc == RESULT_TIMEOUT) {
    if (!waitPendingMqttConnect(70000)) return fail("cleanup", "modem state uncertain; power-cycle SIM7022 before retrying");
    // If the delayed CONNECT succeeded, explicitly disconnect it. If it failed this
    // quickly returns error 11 (no connection), which is also a clean state.
    disc = atResult("AT+CMQTTDISC=0,60", nullptr, "+CMQTTDISC:", 65000);
  }
  if (disc < 0) return mqttFail("CMQTTDISC", disc);
  // Explicit "network not opened" is the normal cold-start state. There is
  // no MQTT service to release/stop; START below still must succeed.
  if (disc == 9) {
    mqttConnected = false;
    NB_LOG("  MQTT network already closed; starting a fresh service");
    return true;
  }
  // An unused client may reject REL; STOP must still confirm the service is stopped.
  at("AT+CMQTTREL=0", 15000);
  int stopped = atResult("AT+CMQTTSTOP", nullptr, "+CMQTTSTOP:", 65000);
  drainLines(500);
  mqttConnected = false;
  return stopped == 0 || mqttFail("CMQTTSTOP", stopped);
}

static bool stepMqttConnect() {
  if (!(gChannels & CH_NB)) return false;
  if (!NB_MQTT_TLS) {
    NB_LOG("[9a] SKIP: plain MQTT does not use CA");
    NB_LOG("[9b] SKIP: plain MQTT does not use SSL");
  }
  NB_LOG("[9c] MQTT %s", NB_MQTT_TLS ? "over TLS" : "plain connectivity test");
  if (!mqttCleanup()) return false;
  int rc = atResult("AT+CMQTTSTART", nullptr, "+CMQTTSTART:", 15000);
  if (rc != 0) return mqttFail("CMQTTSTART", rc);

  char r[64];
  snprintf(cmd, sizeof cmd, "AT+CMQTTACCQ=0,\"%s\",%d", NB_CLIENT_ID, NB_MQTT_TLS ? 1 : 0);
  AtResult a = atGet(cmd, 5000, "+CMQTTACCQ:", r, sizeof r);
  if (a != AT_OK) return mqttFail("CMQTTACCQ", rcOf(a, r));
  if (NB_MQTT_TLS && at("AT+CMQTTSSLCFG=0,0") != AT_OK)
    return fail("CMQTTSSLCFG", "cannot bind SSL context 0 to MQTT client 0");

  // server_addr must start with "tcp://" even for TLS; TLS is chosen by server_type above.
  int n = snprintf(cmd, sizeof cmd, "AT+CMQTTCONNECT=0,\"tcp://%s:%d\",%d,%d",
                   NB_MQTT_HOST, NB_MQTT_PORT, MQTT_KEEPALIVE_S, MQTT_CLEAN_SESSION);
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
  NB_LOG("  %sMQTT CONNECT", NB_MQTT_TLS ? "TLS handshake + " : "");
  rc = atResult(cmd, cmdLog, "+CMQTTCONNECT:", NB_MQTT_TLS ? (TLS_NEGOTIATE_S + 120) * 1000UL : 130000UL);
  if (rc != 0) return mqttFail("CMQTTCONNECT", rc);
  mqttConnected = true;
  mqttLost = false;
  NB_LOG("[9] MQTT connected to %s:%d %s as %s", NB_MQTT_HOST, NB_MQTT_PORT,
         NB_MQTT_TLS ? "over TLS" : "without TLS (test only)", NB_CLIENT_ID);
  return true;
}

static bool bringUp() {
  if (!(gChannels & CH_NB)) return false;
  return stepModem() && stepVersion() && stepSim() && stepRegister() && stepApn() && stepIp() &&
         stepTime() && (!NB_MQTT_TLS || (stepCa() && stepSslConfig())) && stepMqttConnect();
}

// ---------------------------------------------------------------- telemetry

static bool publishTelemetry() {
  size_t len = buildPayload(payload, sizeof payload);
  if (!len) { NB_LOG("! payload does not fit the buffer; not sent"); return true; }
  publishCount++;
  NB_LOG("[10] Publish #%lu to %s (%u bytes): %s", (unsigned long)publishCount, MQTT_TOPIC, (unsigned)len, payload);
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
    NB_LOG("[11] Publish OK (QoS 0: the modem sent it; the broker sends no PUBACK, confirm with a subscriber)");
  else
    NB_LOG("[11] Publish OK (acknowledged by the broker)");
  return true;
}

// ---------------------------------------------------------------- task

static bool configValid() {
  const char *const quoted[] = {NB_MQTT_HOST, NB_CLIENT_ID, MQTT_USER, MQTT_PASS, NB_APN, CA_FILE_NAME, NTP_SERVER};
  for (const char *s : quoted)
    if (strpbrk(s, "\"\r\n")) { haltReason = "config.h: quotes and newlines are forbidden"; return false; }
  if (!strlen(NB_MQTT_HOST) || strlen(NB_MQTT_HOST) > 100 || NB_MQTT_PORT < 1 || NB_MQTT_PORT > 65535) {
    haltReason = "invalid NB broker host/port"; return false;
  }
  if (strlen(NB_CLIENT_ID) + strlen(MQTT_USER) + strlen(MQTT_PASS) + strlen(NB_MQTT_HOST) > 280) {
    haltReason = "MQTT configuration exceeds command buffer"; return false;
  }
  if (!strlen(NB_CLIENT_ID) || strlen(NB_CLIENT_ID) > 256) { haltReason = "NB_CLIENT_ID must be 1-256 bytes"; return false; }
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
  NB_LOG("Retry in %u s", s);
}

void nbBridgeLoop() {
  static bool started = false;
  if (!started) { modemBegin(MODEM_BAUD, MODEM_RX_PIN, MODEM_TX_PIN); started = true; }
  while (Serial.available()) modem.write(Serial.read());
  while (modem.available()) Serial.write(modem.read());
}

void nbTask(void *) {
  modemBegin(MODEM_BAUD, MODEM_RX_PIN, MODEM_TX_PIN);
  NB_LOG("[1] Modem UART: ESP32 RX=GPIO%d, TX=GPIO%d, %d baud, client id %s", MODEM_RX_PIN, MODEM_TX_PIN,
         MODEM_BAUD, NB_CLIENT_ID);
  if (!configValid()) halted = true;
  NB_LOG("TLS profile %d: %s:%d CA=%s; URL query does not select firmware broker",
         BAS_NB_TLS_PROFILE, NB_MQTT_HOST, NB_MQTT_PORT, CA_FILE_NAME);
  bool wasEnabled = true;
  uint32_t lastHaltMsg = 0;
  for (;;) {
    bool enabled = gChannels & CH_NB;
    if (!enabled) {
      if (wasEnabled) {
        if (mqttConnected) mqttCleanup();
        mqttConnected = false;
        NB_LOG("Channel disabled");
      }
      wasEnabled = false;
      if (gNbDiagnostics) {
        gNbDiagnostics = false;
        NB_LOG("Diagnostics begin (read-only; no APN changes)");
        at("AT+CGATT?", 10000);
        at("AT+CGACT?", 10000);
        at("AT+CGDCONT?", 10000);
        at("AT+CGPADDR", 10000);
        at("AT+CESQ", 10000);
        at("AT+CEREG?", 10000);
        NB_LOG("Diagnostics complete");
      }
      while (pollLine()) {}
      delay(200);
      continue;
    }
    if (!wasEnabled) {
      wasEnabled = true;
      backoffIndex = 0;
      nextAttempt = millis();
      NB_LOG("Channel enabled");
    }
    if (halted) {
      if (!lastHaltMsg || millis() - lastHaltMsg >= 60000) { lastHaltMsg = millis(); NB_LOG("! HALTED: %s", haltReason); }
      delay(200);
      continue;
    }
    while (pollLine()) {}
    if (mqttConnected && mqttLost) {
      mqttConnected = false;
      NB_LOG("MQTT link lost, reconnecting");
      scheduleRetry();
    }
    if (!mqttConnected) {
      if ((int32_t)(millis() - nextAttempt) < 0) { delay(20); continue; }
      NB_LOG("=== Connecting ===");
      if (bringUp()) {
        backoffIndex = 0;
        lastPublish = millis() - PUBLISH_INTERVAL_MS;  // publish immediately
      } else {
        scheduleRetry();
      }
      continue;
    }
    if (gPublishNow[0] || millis() - lastPublish >= PUBLISH_INTERVAL_MS) {
      gPublishNow[0] = false;
      lastPublish = millis();
      if (!publishTelemetry()) {
        mqttConnected = false;
        scheduleRetry();
      }
    }
    delay(20);
  }
}
