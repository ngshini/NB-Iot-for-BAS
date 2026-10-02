#pragma once
// BAS test over Wi-Fi (parallel to the SIM7022 NB-IoT firmware).
// Copy this file to config.h and fill in the Wi-Fi credentials. config.h is ignored by git.

// ---- Wi-Fi (2.4 GHz) ----
#define WIFI_SSID "CHANGE_ME"
#define WIFI_PASS "CHANGE_ME"   // never printed to Serial

// ---- Device identity ----
// Same topic as the NB-IoT device so BAS sees the data; a different client id so both
// firmwares can be connected at the same time without kicking each other off the broker.
#define DEVICE_ID "BAS_TEST_001"
#define MQTT_CLIENT_ID "bas-" DEVICE_ID "-wifi"
#define MQTT_TOPIC "bas/" DEVICE_ID "/telemetry"

// ---- Broker (MQTT 3.1.1 over TLS) ----
#define MQTT_HOST "broker.emqx.io"
#define MQTT_PORT 8883
#define MQTT_USER ""            // empty: no username/password sent
#define MQTT_PASS ""            // never printed to Serial
#define MQTT_KEEPALIVE_S 60
#define MQTT_CLEAN_SESSION true
#define MQTT_RETAIN false        // PubSubClient publishes with QoS 0
#define PUBLISH_INTERVAL_MS 60000UL

// 1: {"distance":120.5}   0: full 10-field BAS test payload
#define PAYLOAD_MINIMAL 1

// ---- Network time (certificate validity check) ----
#define NTP_SERVER_1 "pool.ntp.org"
#define NTP_SERVER_2 "time.google.com"
#define TZ_OFFSET_S (7 * 3600)   // UTC+7, only affects printed local time
