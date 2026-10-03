#include "../bas_mqtts/config.example.h"
#include "../bas_mqtts/nb_tls_profile.h"
#include <cassert>
int main() {
  static_assert(AUTO_START, "Firmware must resume MQTT automatically after reboot");
  static_assert(PUBLISH_INTERVAL_MS == 1000UL);
  static_assert(WIND_READ_INTERVAL_MS == 100UL);
  static_assert(RS485_RX_PIN != MODEM_RX_PIN && RS485_RX_PIN != MODEM_TX_PIN);
  static_assert(RS485_TX_PIN != MODEM_RX_PIN && RS485_TX_PIN != MODEM_TX_PIN);
  static_assert(WIND_SENSOR_ADDRESS == 1 && WIND_SENSOR_BAUD == 4800);
  static_assert(WIND_STALE_MS > WIND_READ_INTERVAL_MS);
  static_assert(WIND_RESPONSE_TIMEOUT_MS > 0);
  static_assert(BAS_NB_TLS_PROFILE == 2);
  assert(WIND_ANGLE_OFFSET_DEG == 0);
}
