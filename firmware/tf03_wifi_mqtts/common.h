#pragma once
#include <Arduino.h>

// Shared between the main sketch, the NB-IoT channel (nbiot.cpp) and the Wi-Fi channel (wifi.cpp).
// Each channel runs in its own FreeRTOS task, so both can be connected at the same time.

enum ChannelBit : uint8_t { CH_NB = 1, CH_WIFI = 2 };

extern volatile uint8_t gChannels;       // enabled channels (CH_NB | CH_WIFI), set from Serial commands
extern volatile bool gPayloadMinimal;    // true: {"distance":...}, false: full 10-field payload
extern volatile bool gPublishNow[2];     // [0] NB, [1] Wi-Fi: publish at the next loop

// Wi-Fi credentials: config.h defaults, overridden from the UI ("WIFI ssid|password", kept in flash).
extern char gWifiSsid[33];
extern char gWifiPass[65];               // never printed
extern volatile bool gWifiChanged;       // reconnect with the new credentials
extern volatile bool gWifiScanReq;       // "WIFISCAN": list the 2.4 GHz networks the ESP32 can see

// One complete, prefixed log line ("[NB] ...", "[WF] ...", "[SYS] ..."), safe to call from any task.
void logLine(const char *tag, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

// Builds the BAS JSON payload into out; returns its length, 0 when it does not fit.
size_t buildPayload(char *out, size_t len);

void nbTask(void *);
void wifiTask(void *);
void nbBridgeLoop();                     // AT_BRIDGE_ONLY: USB Serial <-> modem UART
