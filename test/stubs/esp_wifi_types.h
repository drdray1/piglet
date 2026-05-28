// Minimal host-side stub for esp_wifi_types.h.
// Enum values match arduino-esp32's WiFiType.h / esp-idf esp_wifi_types.h so
// the test exercises the real numeric constants.
#pragma once

typedef enum {
  WIFI_AUTH_OPEN = 0,
  WIFI_AUTH_WEP,
  WIFI_AUTH_WPA_PSK,
  WIFI_AUTH_WPA2_PSK,
  WIFI_AUTH_WPA_WPA2_PSK,
  WIFI_AUTH_WPA2_ENTERPRISE,
  WIFI_AUTH_WPA3_PSK,
  WIFI_AUTH_WPA2_WPA3_PSK,
  WIFI_AUTH_WAPI_PSK,
  WIFI_AUTH_MAX,
} wifi_auth_mode_t;
