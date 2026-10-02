#pragma once
// BAS test configuration. To customise, copy this file to config.h
// (config.h is preferred when present and is ignored by git).

// ---- ESP32 DevKit <-> SIM7022 UART (same wiring as firmware/sim7022_mqtt) ----
#define MODEM_RX_PIN 16        // ESP32 GPIO16 (RX) <- SIM7022 TXD
#define MODEM_TX_PIN 17        // ESP32 GPIO17 (TX) -> SIM7022 RXD
#define MODEM_BAUD 115200

// ---- NB-IoT APN ----
// "" keeps the PDP profile already stored in the modem (AT+CGDCONT? is logged).
// Otherwise put the APN given by Viettel for this SIM, e.g. "your.apn".
#define NB_APN ""

// ---- Device identity ----
// Backend BAS maps the device from the topic; keep DEVICE_ID unique per device.
#define DEVICE_ID "BAS_TEST_001"
#define MQTT_CLIENT_ID "bas-" DEVICE_ID
#define MQTT_TOPIC "bas/" DEVICE_ID "/telemetry"

// ---- Broker (MQTT 3.1.1 over TLS) ----
#define MQTT_HOST "broker.emqx.io"
#define MQTT_PORT 8883
#define MQTT_USER ""           // empty: no username/password sent
#define MQTT_PASS ""           // never printed to Serial
#define MQTT_KEEPALIVE_S 60
#define MQTT_CLEAN_SESSION 1
#define MQTT_QOS 0
#define MQTT_RETAIN 0
#define PUBLISH_INTERVAL_MS 60000UL

// 1: {"distance":120.5}   0: full 10-field BAS test payload
#define PAYLOAD_MINIMAL 1

// ---- TLS ----
// Root CA file stored in the modem file system (AT+CCERTDOWN), 1-53 chars.
#define CA_FILE_NAME "digicert_g2.pem"
// 1: delete and re-download the CA once after boot (use after changing ca_cert.h).
#define CA_FORCE_RELOAD 0
#define TLS_NEGOTIATE_S 120    // AT+CSSLCFG="negotiatetime", 10-300 s

// ---- Network time (needed because certificate time check is enabled) ----
#define NTP_SERVER "pool.ntp.org"
#define NTP_TZ_QUARTERS 28     // AT+CNTP time zone in quarter hours: UTC+7 = 28

// true: USB Serial <-> modem UART bridge for manual AT commands (CR+LF).
#define AT_BRIDGE_ONLY false
