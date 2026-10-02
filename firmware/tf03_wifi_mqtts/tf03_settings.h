#pragma once
// Set this to the ESP32 GPIO connected to TF03 TX, NOT the connector label.
// GPIO21/22 follow the existing ATMC PlatformIO sketch; receive-only probing.
#define TF03_RX_PIN 21
#define TF03_ALT_RX_PIN 22
#define TF03_BAUD 115200
#define TF03_STALE_MS 2000UL
#define TF03_PUBLISH_MS 1000UL
// Default TF03-180 over-range code, in cm; match your sensor configuration.
#define TF03_OVER_RANGE_CM 18000
