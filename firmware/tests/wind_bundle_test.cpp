#include "../bas_mqtts/wind_bundle.h"
#include <cassert>
#include <cstring>
int main() {
  char out[512];
  assert(formatWindBundle(out, sizeof out, true, 1370, 10, true, 36, 20));
  assert(strstr(out, "\"windDirection\":137.0"));
  assert(strstr(out, "\"windSpeed\":3.6"));
  assert(formatWindBundle(out, sizeof out, true, 0, 0, false, 36, 4000));
  assert(strstr(out, "\"windSpeed\":null"));
  assert(strstr(out, "\"windDirection\":0.0"));
  assert(formatWindBundle(out, sizeof out, false, 0, 0, true, 0, 0));
  assert(strstr(out, "\"windDirection\":null"));
  assert(strstr(out, "\"windSpeed\":0.0"));
  assert(formatWindBundle(out, sizeof out, false, 0, 0, false, 0, 0));
  assert(!formatWindBundle(out, 8, true, 100, 0, true, 10, 0));
}
