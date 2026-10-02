#pragma once
#include <stddef.h>
#include <stdint.h>

namespace WindSpeed {
enum Result : uint8_t { OK, TIMEOUT, EXCEPTION, BAD_FRAME, BAD_CRC };
inline uint16_t crc16(const uint8_t *data, size_t length) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit)
      crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
  }
  return crc;
}
// Manual section 5.3 reads two registers. Only register 0 is documented
// as speed: unsigned tenths of m/s. Do not assign a meaning to register 1.
inline void request(uint8_t address, uint8_t *out) {
  const uint8_t bytes[] = {address, 3, 0, 0, 0, 2, 0, 0};
  for (size_t i = 0; i < sizeof(bytes); ++i) out[i] = bytes[i];
  const uint16_t crc = crc16(out, 6);
  out[6] = uint8_t(crc); out[7] = uint8_t(crc >> 8);
}
inline Result decode(const uint8_t *data, size_t length, uint8_t address,
                     uint16_t &tenths, uint8_t &exception) {
  if (!length) return TIMEOUT;
  bool badCrc = false;
  for (size_t i = 0; i + 5 <= length; ++i) {
    if (data[i] != address) continue;
    const bool isException = data[i + 1] == 0x83;
    if (!isException && (data[i + 1] != 3 || data[i + 2] != 4)) continue;
    const size_t size = isException ? 5 : 9;
    if (i + size > length) continue;
    const uint16_t received = data[i + size - 2] | (uint16_t(data[i + size - 1]) << 8);
    if (crc16(data + i, size - 2) != received) { badCrc = true; continue; }
    if (isException) { exception = data[i + 2]; return EXCEPTION; }
    tenths = (uint16_t(data[i + 3]) << 8) | data[i + 4];
    return OK;
  }
  return badCrc ? BAD_CRC : BAD_FRAME;
}
inline const char *status(Result result) {
  switch (result) {
    case OK: return "ok";
    case TIMEOUT: return "timeout";
    case EXCEPTION: return "modbus_exception";
    case BAD_CRC: return "bad_crc";
    default: return "bad_frame";
  }
}
}
