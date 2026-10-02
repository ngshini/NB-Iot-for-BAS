#pragma once
// BAS test configuration for both channels. Copy to config.h (ignored by git) to customise;
// config.h is used when present. Channel selection and payload size can also be changed at
// run time from the BAS Serial Monitor page (stored in flash).

// ---- Channel selection ----
// false: after boot nothing runs until a channel is chosen on the BAS Serial Monitor page.
// true : start the last chosen channels automatically (unattended use).
#define AUTO_START false
// Choice offered before anything was ever selected: CH_NB, CH_WIFI or both.
#define DEFAULT_CHANNELS (CH_NB | CH_WIFI)
// 1: {"distance":120.5}   0: full 10-field BAS test payload
#define PAYLOAD_MINIMAL 1
#define PUBLISH_INTERVAL_MS 60000UL

// ---- Device identity ----
// Both channels publish to the same BAS topic; client ids differ so they never kick each other.
#define DEVICE_ID "BAS_TEST_001"
#define MQTT_TOPIC "bas/" DEVICE_ID "/telemetry"
#define NB_CLIENT_ID "bas-" DEVICE_ID
#define WIFI_CLIENT_ID "bas-" DEVICE_ID "-wifi"

// ---- Broker (MQTT 3.1.1 over TLS) ----
#define MQTT_HOST "broker.emqx.io"
#define MQTT_PORT 8883
#define MQTT_USER ""            // empty: no username/password sent
#define MQTT_PASS ""            // never printed to Serial
#define MQTT_KEEPALIVE_S 60
#define MQTT_CLEAN_SESSION 1
#define MQTT_QOS 0               // NB-IoT channel; the Wi-Fi channel always uses QoS 0
#define MQTT_RETAIN 0

// ---- NB-IoT: ESP32 DevKit <-> SIM7022 (never use RX0/TX0) ----
#define MODEM_RX_PIN 16          // ESP32 GPIO16 (RX) <- SIM7022 TXD
#define MODEM_TX_PIN 17          // ESP32 GPIO17 (TX) -> SIM7022 RXD
#define MODEM_BAUD 115200
#define NB_APN ""                // "" keeps the modem's PDP profile (Viettel: v-internet)
#define CA_FILE_NAME "digicert_g2.pem"
#define CA_FORCE_RELOAD 0
#define TLS_NEGOTIATE_S 120
#define NTP_TZ_QUARTERS 28       // AT+CNTP time zone, UTC+7

// ---- Wi-Fi (2.4 GHz) ----
#define WIFI_SSID "CHANGE_ME"
#define WIFI_PASS "CHANGE_ME"    // never printed to Serial

// ---- Network time ----
#define NTP_SERVER "pool.ntp.org"
#define NTP_SERVER_2 "time.google.com"
#define TZ_OFFSET_S (7 * 3600)

// true: USB Serial <-> modem UART bridge for manual AT commands (no MQTT at all).
#define AT_BRIDGE_ONLY false
