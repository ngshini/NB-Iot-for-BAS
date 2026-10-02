#pragma once
// Copy to config.h, or run windows/setup.ps1 to generate it.
#define MODEM_RX 16
#define MODEM_TX 17
#define MODEM_BAUD 115200
#define VIETTEL_APN ""
#define MQTT_HOST "CHANGE_ME_PUBLIC_HOST"
#define MQTT_PORT 1884
#define MQTT_USER "nbiot"
#define MQTT_PASSWORD "CHANGE_ME"
#define DEVICE_ID "esp32-01"
#define MQTT_TOPIC "nbiot/esp32-01/telemetry"
#define PUBLISH_INTERVAL_MS 60000UL
// AUTO, CMQTT, QC_PLUS, QC_DOLLAR. AUTO probes supported test commands.
#define MQTT_DIALECT "AUTO"
// true: USB Serial <-> modem UART bridge for ATI / AT+CGMR diagnostics.
#define AT_BRIDGE_ONLY false
