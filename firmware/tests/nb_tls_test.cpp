#include <cassert>
#include <cstring>
#include "../bas_mqtts/nb_tls_profile.h"
int main() {
  assert(nbCertificateListed("+CCERTLIST: \"root.pem\"", "root.pem"));
  assert(!nbCertificateListed("+CCERTLIST: \"old-root.pem\"", "root.pem"));
  assert(!nbCertificateListed("+CCERTLIST: \"root.pem.old\"", "root.pem"));
  assert(!nbCertificateListed("ERROR root.pem", "root.pem"));
  assert(!nbCertificateListed("+CCERTLIST: \"ROOT.pem\"", "root.pem"));
  assert(!nbCertificateListed("+CCERTLIST: \"root.pem", "root.pem"));
  assert(!nbCertificateListed(nullptr, "root.pem"));
  assert(!nbCertificateListed("+CCERTLIST: \"\"", ""));
  assert(NB_TLS_PORT == 8883);
#if BAS_NB_TLS_PROFILE == 1
  assert(!strcmp(NB_TLS_HOST, "broker.emqx.io"));
  assert(!strcmp(NB_TLS_CA_FILE, "bas_digicert_g2_v2.pem"));
#else
  assert(!strcmp(NB_TLS_HOST, "test.mosquitto.org"));
  assert(!strcmp(NB_TLS_CA_FILE, "bas_mosquitto_v2.pem"));
#endif
  assert(strstr(CA_CERT_PEM, "-----BEGIN CERTIFICATE-----"));
}
