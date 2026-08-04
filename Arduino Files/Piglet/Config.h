#pragma once
#include <Arduino.h>
#include "PinMapDefs.h"
#include "NodeRole.h"
#include "Blacklist.h"

struct Config {
  String wigleBasicToken;
  String homeSsid;
  String homePsk;
  String wardriverSsid = "Piglet-WARDRIVE";
  String wardriverPsk  = "wardrive1234";
  uint32_t gpsBaud     = 9600;
  String scanMode      = "aggressive"; // aggressive | powersaving
  String board = "auto"; // auto | s3 | c5 | c6 | c3 | exp  (pins selected at boot; reboot required after change)
  String speedUnits  = "kmh"; // kmh | mph
  int battPin        = -1;    // GPIO for battery voltage ADC (-1 = disabled). Expects 1:2 voltage divider from LiPo.
  bool batteryTest   = false; // Enable battery test (logs elapsed time to /battery_test.csv)
  
  // Boot auto-upload limit:
  //  -1 = upload ALL files at boot (no limit)
  //   0 = disabled (no auto-upload at boot)
  //  1+ = upload up to N files at boot (WiGLE allows 25 API calls/day)
  // IMPORTANT: Requires PSRAM enabled in Arduino IDE for reliable TLS connections.
  int maxBootUploads = 25;

  // WDGoWars API key from https://wdgwars.pl/profile -> "Generate API key".
  // If set, CSVs are uploaded to WDGoWars BEFORE WiGLE at every boot.
  // Leave empty to disable WDGoWars uploads.
  String wdgwarsApiKey;

  // Optional device name — appended to WiGLE CSV header and filename so
  // multiple Piglets uploading to the same account can be distinguished.
  // E.g. deviceName=rover1  →  device=Piglet-rover1  /  rover1_Piglet_WiGLE_....csv
  // Leave empty for default ("Piglet-Wardriver" / "WiGLE_....csv").
  String deviceName;

  // Auto-start mesh mode after boot uploads: core, node, or none (default).
  // core — become the mesh coordinator (receives wardriving records from nodes).
  // node — become a scanning node that forwards records to the Core.
  // none — normal solo wardriving mode.
  String meshModeOnBoot = "none";

  // Rotate the OLED display 180° (true = upside-down mount, false = normal).
  // Requires reboot to take effect.
  bool rotateScreen180 = false;

  // When true: after boot uploads complete, disconnect from home WiFi and
  // begin wardriving immediately instead of staying on the STA connection.
  // The web UI is still reachable if you connect to the Wardriver AP later,
  // but the device will not hold the STA link open. Requires reboot.
  bool autoStartAfterUpload = false;

  // ---- BLE wardriving (see piglet_bluetooth_implementation.md) ----
  // Master enable. When false the NimBLE stack is never initialised, so there
  // is no flash/RAM cost beyond this bool. Requires reboot to change.
  bool     bleEnabled      = false;
  // BLE scan-window duration (s). Clamped to 1–10 to avoid starving Wi-Fi.
  uint16_t bleScanDuration = 5;
  // Time between BLE scan-window starts (s). Forced >= bleScanDuration + 5.
  uint16_t bleScanInterval = 30;
  // Cap on the log-once dedupe rings (Wi-Fi + BLE) and the BLE pending FIFO
  // (memory knob). Each unique MAC/BSSID is logged once until evicted past this
  // cap. 100–2000; default 200 matches the upstream JCMK/Biscuit mac_history.
  uint16_t bleMaxResults   = 200;

  // Log-once dedup master switch. true = each MAC/BSSID logged once per boot
  // (WiFi + BLE). false = log every sighting. Reuses the bleMaxResults ring cap.
  bool     dedupEnabled    = true;

  // ---- Mesh per-node scan roles (Core only; SD-config-only management) ----
  // The Core assigns each node a task by full MAC via `node.<12hex>=wifi|ble|both`
  // lines in wardriver.cfg. Unlisted/new nodes use nodeDefaultRole. Roles are
  // delivered to nodes over the existing mesh admin frame. Reboot the Core to
  // apply edits (config is read once at boot).
  uint8_t       nodeDefaultRole = NODE_ROLE_BOTH;
  uint8_t       nodeRoleCount   = 0;
  NodeRoleEntry nodeRoles[CFG_MAX_NODE_ROLES] = {};

  // ---- Save-file blacklist (Core/standalone only; SD-config-only) ----
  // SSIDs/BLE-names and MAC/BSSIDs listed here are never written to the CSV.
  // One entry per line (key repeated), with an optional inline '# label':
  //   blacklistMac=AA:BB:CC:DD:EE:FF   # My phone     (colons or bare 12-hex)
  //   blacklistSsid=MyHomeNet          # Home network (also matches BLE names)
  // Matching is exact (MAC = 6-byte equality; SSID/name = case-insensitive whole
  // string). Enforced at CSV write time, so it covers both local scans and
  // mesh-forwarded node observations.
  uint8_t       blacklistMacCount  = 0;
  BlacklistMac  blacklistMacs[CFG_MAX_BLACKLIST]  = {};
  uint8_t       blacklistSsidCount = 0;
  BlacklistSsid blacklistSsids[CFG_MAX_BLACKLIST] = {};
};

const PinMap& detectPinsByChip();
PinMap pickPinsFromConfig();
bool wardriverIsC5();

String trimCopy(String s);
bool parseKeyValueLine(const String& lineIn, String& keyOut, String& valOut);
void cfgAssignKV(const String& k, const String& v);
uint8_t cfgRoleForMac(const uint8_t mac[6]);  // node role by MAC, or nodeDefaultRole
bool cfgBlacklistedMac(const uint8_t mac[6]);  // true if MAC/BSSID is blacklisted
bool cfgBlacklistedSsid(const char* ssid);     // true if SSID/BLE-name is blacklisted
void validateConfig();   // clamp cross-field constraints after a load
bool loadConfigFromSD();
bool saveConfigToSD();
