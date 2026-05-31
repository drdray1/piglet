// Pure, host-testable WiGLE-1.6 CSV formatting for BLE observations.
//
// Lives in a header (like Scanner_channels.h) so the band/freq mapping and the
// row layout can be exercised by the host-side doctest harness without pulling
// in NimBLE, SD, or the Arduino core. The device-side appendBleRow() in
// SDUtils.cpp builds its line through formatBleRow() so the on-disk format and
// the tested format can never drift apart.
//
// BLE rows share the same 14-column WigleWifi-1.6 header as Wi-Fi rows; only the
// per-field semantics differ (see piglet_bluetooth_implementation.md §4).
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

// BLE primary advertising channel -> centre frequency in MHz.
// NimBLE only ever reports 37/38/39; anything else yields 0 (should not happen).
inline uint32_t bleChannelToFreq(int channel) {
  switch (channel) {
    case 37: return 2402;
    case 38: return 2426;
    case 39: return 2480;
    default: return 0;
  }
}

// Format 6 raw address bytes (big-endian, i.e. bda[0] is the most-significant
// octet shown first) into "AA:BB:CC:DD:EE:FF". NimBLE delivers BDA bytes in LE
// order, so the scanner reverses them before calling this; the JCMK mesh struct
// stores them already big-endian. Output buffer must hold >= 18 bytes.
inline void formatBda(const uint8_t bda[6], char out[18]) {
  std::snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
                bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
}

// NimBLE NimBLEAddress::getType() code -> WiGLE AuthMode string.
//   0 = public, 1 = random static, 2 = resolvable private (RPA),
//   3 = non-resolvable private (NRPA). Bracketed so WiGLE never confuses these
//   with Wi-Fi auth modes (OPEN/WPA2/...).
inline const char* bleAddrTypeToString(uint8_t addrType) {
  switch (addrType) {
    case 0: return "[LE Public]";
    case 1: return "[LE Random]";
    case 2: return "[LE Resolvable]";
    case 3: return "[LE NonResolvable]";
    default: return "[LE Unknown]";
  }
}

// RFC4180-escape a field's interior: double every embedded quote. The caller
// wraps the result in quotes. Matches the Wi-Fi writer's SSID handling.
inline std::string bleCsvEscapeQuotes(const std::string& in) {
  std::string out;
  out.reserve(in.size());
  for (char c : in) {
    if (c == '"') out += "\"\"";
    else          out += c;
  }
  return out;
}

// Build one WigleWifi-1.6 CSV row for a BLE observation. Column order matches
// the header emitted in SDUtils.cpp::openLogFile():
//   MAC,SSID,AuthMode,FirstSeen,Channel,Frequency,RSSI,
//   CurrentLatitude,CurrentLongitude,AltitudeMeters,AccuracyMeters,
//   RCOIs,MfgrId,Type
//
//  - name is quote-wrapped and escaped (empty -> "").
//  - addrType is mapped to the bracketed [LE ...] AuthMode string.
//  - serviceUuids is emitted verbatim into RCOIs (already ';'-joined, may be "").
//  - mfgrId is the decimal LE-decoded company identifier (0 if none).
//  - Type is always BLE.
inline std::string formatBleRow(const std::string& bda,
                                const std::string& name,
                                uint8_t addrType,
                                const std::string& firstSeen,
                                int channel,
                                int rssi,
                                double lat,
                                double lon,
                                double altM,
                                double accM,
                                const std::string& serviceUuids,
                                uint16_t mfgrId) {
  std::string escapedName = bleCsvEscapeQuotes(name);
  char buf[384];
  std::snprintf(buf, sizeof(buf),
                "%s,\"%s\",%s,%s,%d,%u,%d,%.6f,%.6f,%.1f,%.1f,%s,%u,BLE",
                bda.c_str(), escapedName.c_str(), bleAddrTypeToString(addrType),
                firstSeen.c_str(), channel, (unsigned)bleChannelToFreq(channel),
                rssi, lat, lon, altM, accM, serviceUuids.c_str(),
                (unsigned)mfgrId);
  return std::string(buf);
}
