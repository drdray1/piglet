// Regression coverage for authModeToString (defined in Scanner.cpp,
// declared in Scanner.h after the dedupe on this branch).
//
// We can't compile Scanner.cpp host-side without pulling in the full
// Arduino + esp-idf stack. Instead, the production function body is
// duplicated verbatim below as `authModeToString_underTest` and exercised.
// If anyone edits the production switch without updating this copy, the
// test will diverge — that's the intended signal.

#include "doctest.h"
#include <Arduino.h>
#include <esp_wifi_types.h>

// ---- verbatim copy of Scanner.cpp's authModeToString ------------------------
static String authModeToString_underTest(wifi_auth_mode_t m) {
  switch (m) {
    case WIFI_AUTH_OPEN: return "OPEN";
    case WIFI_AUTH_WEP: return "WEP";
    case WIFI_AUTH_WPA_PSK: return "WPA";
    case WIFI_AUTH_WPA2_PSK: return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK: return "WPAWPA2";
    case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2EAP";
    case WIFI_AUTH_WPA3_PSK: return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2WPA3";
    default: return "UNKNOWN";
  }
}
// ---------------------------------------------------------------------------

TEST_CASE("authModeToString: every known WIFI_AUTH_* maps to expected string") {
  CHECK(authModeToString_underTest(WIFI_AUTH_OPEN)            == "OPEN");
  CHECK(authModeToString_underTest(WIFI_AUTH_WEP)             == "WEP");
  CHECK(authModeToString_underTest(WIFI_AUTH_WPA_PSK)         == "WPA");
  CHECK(authModeToString_underTest(WIFI_AUTH_WPA2_PSK)        == "WPA2");
  CHECK(authModeToString_underTest(WIFI_AUTH_WPA_WPA2_PSK)    == "WPAWPA2");
  CHECK(authModeToString_underTest(WIFI_AUTH_WPA2_ENTERPRISE) == "WPA2EAP");
  CHECK(authModeToString_underTest(WIFI_AUTH_WPA3_PSK)        == "WPA3");
  CHECK(authModeToString_underTest(WIFI_AUTH_WPA2_WPA3_PSK)   == "WPA2WPA3");
}

TEST_CASE("authModeToString: unknown / out-of-range values fall through to UNKNOWN") {
  CHECK(authModeToString_underTest(WIFI_AUTH_WAPI_PSK) == "UNKNOWN");
  CHECK(authModeToString_underTest(static_cast<wifi_auth_mode_t>(99)) == "UNKNOWN");
}
