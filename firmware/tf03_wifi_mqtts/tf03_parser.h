#pragma once
#include <stdint.h>
#include <string.h>

// Sliding window recovers after noise, dropped bytes, and false headers.
class Tf03Parser {
  uint8_t bytes[9] = {};
  unsigned count = 0;
public:
  uint16_t distance = 0, strength = 0;
  bool feed(uint8_t b) {
    if (count == 9) { memmove(bytes, bytes + 1, 8); count = 8; }
    bytes[count++] = b;
    if (count != 9 || bytes[0] != 0x59 || bytes[1] != 0x59) return false;
    uint8_t sum = 0;
    for (unsigned i = 0; i < 8; ++i) sum += bytes[i];
    if (sum != bytes[8]) return false;
    distance = bytes[2] | (uint16_t(bytes[3]) << 8);
    strength = bytes[4] | (uint16_t(bytes[5]) << 8);
    count = 0;
    return true;
  }
};
