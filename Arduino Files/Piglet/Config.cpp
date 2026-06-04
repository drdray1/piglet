#include "Config.h"
#include "Globals.h"
#include <SD.h>

#if __has_include(<esp_chip_info.h>)
  #include <esp_chip_info.h>
#else
  #include <esp_system.h>
#endif

static const char* CFG_PATH = "/wardriver.cfg";

// ---------------- Board / Pin detection ----------------

const PinMap& detectPinsByChip() {
  String m = ESP.getChipModel();  // e.g. "ESP32-C5"
  Serial.print("[CHIP] Model: ");
  Serial.println(m);

  m.toUpperCase();

  if (m.indexOf("C5") >= 0) return PINS_C5;
  if (m.indexOf("C6") >= 0) return PINS_C6;
  if (m.indexOf("S3") >= 0) return PINS_S3;

  // fallback
  return PINS_S3;
}

PinMap pickPinsFromConfig() {
  // cfg.board is expected to be: "auto" | "s3" | "c6" (lowercase)
  if (cfg.board == "s3") return PINS_S3;
  if (cfg.board == "exp") return PINS_S3_EXP_BASE;
  if (cfg.board == "c6") return PINS_C6;
  if (cfg.board == "c5") return PINS_C5;
  return detectPinsByChip(); // "auto" or anything else
}

// True when THIS wardriver hardware is configured as ESP32-C5 (5GHz capable)
bool wardriverIsC5() {
  // Explicit config override wins
  if (cfg.board == "c5") return true;
  if (cfg.board == "c6" || cfg.board == "s3" || cfg.board == "exp") return false;

  // Auto mode: infer from the active pinmap name (set during setup())
  String n = String(pins.name);
  n.toUpperCase();
  return (n.indexOf("C5") >= 0);
}

// ---------------- Parsing helpers ----------------

String trimCopy(String s) {
  s.trim();
  return s;
}

bool parseKeyValueLine(const String& lineIn, String& keyOut, String& valOut) {
  String line = lineIn;
  line.trim();
  if (line.length() == 0) return false;
  if (line[0] == '#') return false;           // comment
  if (line.startsWith("//")) return false;    // comment

  int eq = line.indexOf('=');
  if (eq < 0) return false;

  String k = line.substring(0, eq);
  String v = line.substring(eq + 1);

  k.trim();
  v.trim();

  // allow quoted values
  if (v.length() >= 2 && ((v[0] == '"' && v[v.length()-1] == '"') || (v[0] == '\'' && v[v.length()-1] == '\''))) {
    v = v.substring(1, v.length()-1);
    v.trim();
  }

  if (k.length() == 0) return false;
  keyOut = k;
  valOut = v;
  return true;
}

void cfgAssignKV(const String& k, const String& v) {
  // Per-node scan roles: node.<12hex>=wifi|ble|both. Handled first (prefix match).
  // Malformed entries are silently dropped — consistent with the ignore-unknown
  // behavior of the rest of the parser.
  if (k.startsWith("node.")) {
    uint8_t mac[6], role;
    String hex = k.substring(5);
    if (parseMac12(hex.c_str(), mac) && roleFromStr(v.c_str(), role))
      nodeRoleUpsert(cfg.nodeRoles, &cfg.nodeRoleCount, CFG_MAX_NODE_ROLES, mac, role);
    return;
  }
  if (k == "nodeDefaultRole") {
    uint8_t r;
    if (roleFromStr(v.c_str(), r)) cfg.nodeDefaultRole = r;
    return;
  }

  // Save-file blacklist: one entry per line (key repeated), exact match. The
  // value is a MAC (12-hex or colon form) / SSID, optionally followed by an
  // inline '# label'. blacklist*Add captures the label and upserts.
  if (k == "blacklistMac") {
    blacklistMacAdd(cfg.blacklistMacs, &cfg.blacklistMacCount, CFG_MAX_BLACKLIST, v.c_str());
    return;
  }
  if (k == "blacklistSsid") {
    blacklistSsidAdd(cfg.blacklistSsids, &cfg.blacklistSsidCount, CFG_MAX_BLACKLIST, v.c_str());
    return;
  }

  if (k == "wigleBasicToken") cfg.wigleBasicToken = v;
  else if (k == "homeSsid")   cfg.homeSsid = v;
  else if (k == "homePsk")    cfg.homePsk = v;
  else if (k == "wardriverSsid") cfg.wardriverSsid = v;
  else if (k == "wardriverPsk")  cfg.wardriverPsk = v;
  else if (k == "gpsBaud") {
    uint32_t b = (uint32_t)v.toInt();
    if (b > 0) cfg.gpsBaud = b;
  }
  else if (k == "scanMode") {
    if (v == "aggressive" || v == "powersaving") cfg.scanMode = v;
  }
  else if (k == "board") {
    String vv = v; vv.toLowerCase();
    if (vv == "auto" || vv == "s3" || vv == "exp" || vv == "c5" || vv == "c6") cfg.board = vv;
  }
  else if (k == "speedUnits") {
    String vv = v; vv.toLowerCase();
    if (vv == "kmh" || vv == "mph") cfg.speedUnits = vv;
  }
  else if (k == "battPin") {
    int p = v.toInt();
    cfg.battPin = (v == "0") ? 0 : (p > 0 ? p : -1);  // allow GPIO 0; treat non-numeric as disabled
  }
  else if (k == "batteryTest") {
    String vv = v; vv.toLowerCase();
    cfg.batteryTest = (vv == "true" || vv == "1");
  }
  else if (k == "maxBootUploads") {
    int n = v.toInt();
    if (n >= -1) cfg.maxBootUploads = n;  // -1=all, 0=disabled, 1+=limited
  }
  else if (k == "wdgwarsApiKey")    cfg.wdgwarsApiKey = v;
  else if (k == "deviceName")       cfg.deviceName = v;
  else if (k == "meshModeOnBoot") {
    String vv = v; vv.toLowerCase();
    if (vv == "core" || vv == "node" || vv == "none") cfg.meshModeOnBoot = vv;
  }
  else if (k == "rotateScreen180") {
    String vv = v; vv.toLowerCase();
    cfg.rotateScreen180 = (vv == "true" || vv == "1");
  }
  else if (k == "bleEnabled") {
    String vv = v; vv.toLowerCase();
    cfg.bleEnabled = (vv == "true" || vv == "1");
  }
  else if (k == "bleScanDuration") {
    int n = v.toInt();
    if (n >= 1 && n <= 10) cfg.bleScanDuration = (uint16_t)n;
  }
  else if (k == "bleScanInterval") {
    int n = v.toInt();
    // Cross-field minimum enforced in validateConfig(); accept any sane value here.
    if (n >= 5 && n <= 300) cfg.bleScanInterval = (uint16_t)n;
  }
  else if (k == "bleDedupeWindow") {
    // Deprecated: dedupe is now log-once (no time window). Parsed and ignored
    // so old saved configs still load without error.
  }
  else if (k == "bleMaxResults") {
    int n = v.toInt();
    if (n >= 100 && n <= 2000) cfg.bleMaxResults = (uint16_t)n;
  }
}

uint8_t cfgRoleForMac(const uint8_t mac[6]) {
  return roleForMacIn(cfg.nodeRoles, cfg.nodeRoleCount, mac, cfg.nodeDefaultRole);
}

bool cfgBlacklistedMac(const uint8_t mac[6]) {
  return blacklistHasMac(cfg.blacklistMacs, cfg.blacklistMacCount, mac);
}

bool cfgBlacklistedSsid(const char* ssid) {
  return blacklistHasSsid(cfg.blacklistSsids, cfg.blacklistSsidCount, ssid);
}

// Clamp cross-field / out-of-range constraints that single-key parsing in
// cfgAssignKV can't see. Idempotent and safe to call on default config.
void validateConfig() {
  if (cfg.bleScanDuration < 1)  cfg.bleScanDuration = 1;
  if (cfg.bleScanDuration > 10) cfg.bleScanDuration = 10;
  if (cfg.bleScanInterval < (uint16_t)(cfg.bleScanDuration + 5))
    cfg.bleScanInterval = (uint16_t)(cfg.bleScanDuration + 5);
  if (cfg.bleMaxResults < 100)  cfg.bleMaxResults = 100;
  if (cfg.bleMaxResults > 2000) cfg.bleMaxResults = 2000;
}

// ---------------- Load / Save ----------------

bool loadConfigFromSD() {
  Serial.println("[CFG] loadConfigFromSD() start");
  if (!sdOk) {
    Serial.println("[CFG] SD not OK, skipping config load");
    return false;
  }

  // Prefer new plain-text config
  if (SD.exists(CFG_PATH)) {
    File f = SD.open(CFG_PATH, FILE_READ);
    if (!f) {
      Serial.println("[CFG] Failed to open /wardriver.cfg for read");
      return false;
    }

    while (f.available()) {
      String line = f.readStringUntil('\n');
      String k, v;
      if (parseKeyValueLine(line, k, v)) {
        cfgAssignKV(k, v);
      }
    }
    f.close();

    Serial.println("[CFG] Loaded config from /wardriver.cfg:");
    Serial.print("      wardriverSsid: "); Serial.println(cfg.wardriverSsid);
    Serial.print("      wardriverPsk:  "); Serial.println(cfg.wardriverPsk.length() ? "(set)" : "(empty)");
    Serial.print("      homeSsid:      "); Serial.println(cfg.homeSsid);
    Serial.print("      homePsk:       "); Serial.println(cfg.homePsk.length() ? "(set)" : "(empty)");
    Serial.print("      gpsBaud:       "); Serial.println(cfg.gpsBaud);
    Serial.print("      scanMode:      "); Serial.println(cfg.scanMode);
    Serial.print("      wigle token:   "); Serial.println(cfg.wigleBasicToken.length() ? "(set)" : "(empty)");
    validateConfig();
    return true;
  }

  Serial.println("[CFG] No /wardriver.cfg. Using defaults.");
  validateConfig();
  return false;
}

bool saveConfigToSD() {
  Serial.println("[CFG] saveConfigToSD() start");
  if (!sdOk) {
    Serial.println("[CFG] SD not OK, cannot save config");
    return false;
  }

  // Remove old file if present
  if (SD.exists(CFG_PATH)) {
    if (!SD.remove(CFG_PATH)) {
      Serial.println("[CFG] WARNING: failed to remove existing /wardriver.cfg");
    }
  }

  File f = SD.open(CFG_PATH, FILE_WRITE);
  if (!f) {
    Serial.println("[CFG] Failed to open /wardriver.cfg for write");
    return false;
  }

  // Simple key=value format (human editable)
  f.println("# Piglet Wardriver config (key=value)");
  f.println("# Lines starting with # are comments");
  f.println("");

  f.println("# WiGLE API 'Encoded for use' token from wigle.net/account");
  f.print("wigleBasicToken="); f.println(cfg.wigleBasicToken);
  f.println("");

  f.println("# Home Wi-Fi credentials (for STA connection and uploads)");
  f.print("homeSsid=");        f.println(cfg.homeSsid);
  f.print("homePsk=");         f.println(cfg.homePsk);
  f.println("");

  f.println("# Device Access Point credentials (for web UI when home WiFi unavailable)");
  f.print("wardriverSsid=");   f.println(cfg.wardriverSsid);
  f.print("wardriverPsk=");    f.println(cfg.wardriverPsk);
  f.println("");

  f.println("# GPS UART baud rate (default: 9600)");
  f.print("gpsBaud=");         f.println(cfg.gpsBaud);
  f.println("");

  f.println("# Scan interval: aggressive (4.5s) or powersaving (12s)");
  f.print("scanMode=");        f.println(cfg.scanMode);
  f.println("");

  f.println("# Speed display: kmh or mph");
  f.print("speedUnits=");     f.println(cfg.speedUnits);
  f.println("");

  f.println("# Board type: auto, s3, c5, c6, exp (requires reboot)");
  f.print("board=");          f.println(cfg.board);
  f.println("");

  f.println("# Battery voltage ADC GPIO (-1 = disabled, requires 1:2 voltage divider)");
  f.print("battPin=");        f.println(cfg.battPin);
  f.println("");

  f.println("# Enable battery test (logs elapsed time to /battery_test.csv): true or false");
  f.print("batteryTest=");    f.println(cfg.batteryTest ? "true" : "false");
  f.println("");

  f.println("# Max CSV files to auto-upload at boot (-1=all, 0=disabled, 1-25=limited)");
  f.print("maxBootUploads="); f.println(cfg.maxBootUploads);

  f.println("");
  f.println("# WDGoWars API key from https://wdgwars.pl/profile (leave empty to disable)");
  f.print("wdgwarsApiKey="); f.println(cfg.wdgwarsApiKey);

  f.println("# Device name — identifies this Piglet in WiGLE headers and filenames.");
  f.println("# Alphanumeric + _ - only.  E.g.: rover1  backpack  car");
  f.print("deviceName="); f.println(cfg.deviceName);

  f.println("");
  f.println("# Mesh mode on boot: Core, Node, or None (default).");
  f.println("# Core  — become the mesh coordinator after uploads complete.");
  f.println("# Node  — become a scanning mesh node after uploads complete.");
  f.println("# None  — normal solo wardriving (default).");
  f.print("meshModeOnBoot="); f.println(cfg.meshModeOnBoot);

  f.println("");
  f.println("# Rotate screen 180 degrees (true = upside-down mount). Requires reboot.");
  f.print("rotateScreen180="); f.println(cfg.rotateScreen180 ? "true" : "false");

  f.println("");
  f.println("# ---- BLE Wardriving (optional) ----");
  f.println("# Passively scan for Bluetooth LE devices alongside Wi-Fi and log");
  f.println("# them to the same CSV with Type=BLE. Requires reboot if changing bleEnabled.");
  f.print("bleEnabled=");      f.println(cfg.bleEnabled ? "true" : "false");
  f.println("# BLE scan-window duration (s). 1-10.");
  f.print("bleScanDuration="); f.println(cfg.bleScanDuration);
  f.println("# Time between BLE scan windows (s). Forced to >= bleScanDuration + 5.");
  f.print("bleScanInterval="); f.println(cfg.bleScanInterval);
  f.println("# Log-once dedupe ring + pending FIFO cap (memory knob). 100-2000.");
  f.print("bleMaxResults=");   f.println(cfg.bleMaxResults);

  f.println("");
  f.println("# ---- Mesh per-node scan roles (Core only) ----");
  f.println("# Assign each cluster node a task by its FULL 12-hex MAC (no colons):");
  f.println("#   node.AABBCCDDEE01=wifi   (Wi-Fi only)");
  f.println("#   node.AABBCCDDEE02=ble    (BLE only)");
  f.println("#   node.AABBCCDDEE03=both   (Wi-Fi + BLE)");
  f.println("# A node's full MAC is printed to Serial on join and shown on the OLED.");
  f.println("# nodeDefaultRole applies to any node not listed. Reboot Core to apply edits.");
  f.print("nodeDefaultRole="); f.println(roleToStr(cfg.nodeDefaultRole));
  for (uint8_t i = 0; i < cfg.nodeRoleCount; i++) {
    const uint8_t* m = cfg.nodeRoles[i].mac;
    char line[48];
    snprintf(line, sizeof(line), "node.%02X%02X%02X%02X%02X%02X=%s",
             m[0], m[1], m[2], m[3], m[4], m[5], roleToStr(cfg.nodeRoles[i].role));
    f.println(line);
  }

  f.println("");
  f.println("# ---- Save-file blacklist (never written to the CSV) ----");
  f.println("# One entry per line. Exact match; up to 16 of each. MACs accept");
  f.println("# colons or bare 12-hex. SSIDs are case-insensitive and also match");
  f.println("# BLE device names. Add an inline '# label' to note what each is.");
  f.println("#   blacklistMac=AA:BB:CC:DD:EE:FF   # My phone");
  f.println("#   blacklistSsid=MyHomeNet          # Home network");
  for (uint8_t i = 0; i < cfg.blacklistMacCount; i++) {
    const uint8_t* m = cfg.blacklistMacs[i].mac;
    char line[80];
    snprintf(line, sizeof(line), "blacklistMac=%02X:%02X:%02X:%02X:%02X:%02X",
             m[0], m[1], m[2], m[3], m[4], m[5]);
    f.print(line);
    if (cfg.blacklistMacs[i].label[0]) { f.print("   # "); f.print(cfg.blacklistMacs[i].label); }
    f.println("");
  }
  for (uint8_t i = 0; i < cfg.blacklistSsidCount; i++) {
    f.print("blacklistSsid="); f.print(cfg.blacklistSsids[i].name);
    if (cfg.blacklistSsids[i].label[0]) { f.print("   # "); f.print(cfg.blacklistSsids[i].label); }
    f.println("");
  }

  f.flush();
  f.close();

  Serial.println("[CFG] Saved /wardriver.cfg OK");
  return true;
}
