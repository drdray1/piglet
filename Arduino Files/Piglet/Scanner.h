#pragma once
#include <Arduino.h>
#include <esp_wifi_types.h>

void doScanOnce();
const char* authModeToString(wifi_auth_mode_t m);

// GPS last-known-good position cache (maintained by loop(), read by Scanner)
extern bool     lastGpsValid;
extern double   lastLat, lastLon, lastAlt, lastAcc;
extern uint32_t lastGpsValidMs;
extern const uint32_t GPS_CACHE_MAX_MS;
