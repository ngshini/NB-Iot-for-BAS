#pragma once
#include <cstring>

// Select at build time; never silently fall back to another broker or plain MQTT.
// 1 = current EMQX service, 2 = isolated Mosquitto TLS diagnostic.
#ifndef BAS_NB_TLS_PROFILE
#define BAS_NB_TLS_PROFILE 2
#endif
#if BAS_NB_TLS_PROFILE == 1
#include "emqx_ca.h"
#define NB_TLS_HOST "broker.emqx.io"
#define NB_TLS_CA_FILE "bas_digicert_g2_v2.pem"
#elif BAS_NB_TLS_PROFILE == 2
#include "ca_cert.h"
#define NB_TLS_HOST "test.mosquitto.org"
#define NB_TLS_CA_FILE "bas_mosquitto_v2.pem"
#else
#error "Unsupported BAS_NB_TLS_PROFILE (use 1 or 2)"
#endif
#define NB_TLS_PORT 8883

inline bool nbCertificateListed(const char *line, const char *name) {
  if (!line || !name || !*name || std::strncmp(line, "+CCERTLIST:", 11)) return false;
  const char *start = std::strchr(line + 11, '"');
  if (!start) return false;
  const char *end = std::strchr(++start, '"');
  return end && static_cast<size_t>(end - start) == std::strlen(name) &&
         !std::strncmp(start, name, end - start);
}
