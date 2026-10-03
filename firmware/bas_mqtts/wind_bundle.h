#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>

// Both values use the web's numeric fields; null never masquerades as calm wind.
inline size_t formatWindBundle(char *out, size_t len, bool directionValid,
                              uint16_t direction, uint32_t directionAge,
                              bool speedValid, uint16_t speed, uint32_t speedAge) {
  char d[16] = "null", s[16] = "null";
  if (directionValid) snprintf(d, sizeof d, "%u.%u", direction / 10, direction % 10);
  if (speedValid) snprintf(s, sizeof s, "%u.%u", speed / 10, speed % 10);
  const int n = snprintf(out, len,
    "{\"sensor\":\"wind-pair\",\"windDirection\":%s,\"windSpeed\":%s,"
    "\"status\":\"ok\",\"complete\":%s,\"direction_unit\":\"deg\",\"speed_unit\":\"m/s\","
    "\"direction_status\":\"%s\",\"speed_status\":\"%s\","
    "\"direction_age_ms\":%lu,\"speed_age_ms\":%lu}",
    d, s, directionValid && speedValid ? "true" : "false",
    directionValid ? "ok" : "unavailable", speedValid ? "ok" : "unavailable",
    (unsigned long)directionAge, (unsigned long)speedAge);
  return n > 0 && (size_t)n < len ? (size_t)n : 0;
}
