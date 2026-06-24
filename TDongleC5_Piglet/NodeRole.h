#pragma once
// Per-node mesh scan-role assignment (wifi / ble / both).
//
// Pure C, no Arduino String or SD dependencies, so this header is included by
// both the device firmware (Config.h / MeshNode.cpp / PigletNode.ino) AND the
// host-side doctest harness (test/test_node_role.cpp). Keep it dependency-free.
#include <stdint.h>
#include <string.h>
#include <ctype.h>

enum NodeRole : uint8_t {
  NODE_ROLE_BOTH = 0,   // scan Wi-Fi + BLE (default / current behavior)
  NODE_ROLE_WIFI = 1,   // Wi-Fi only
  NODE_ROLE_BLE  = 2,   // BLE only
};

// Max node→role entries stored in config. CORE_MAX_NODES is 4; extra slots let
// the user keep spare/retired boards listed without dropping assignments.
#ifndef CFG_MAX_NODE_ROLES
#define CFG_MAX_NODE_ROLES 8
#endif

struct NodeRoleEntry {
  uint8_t mac[6];
  uint8_t role;   // NodeRole
};

// Parse exactly 12 hex chars (no separators, e.g. "AABBCCDDEE01") into out[6].
// Returns false on wrong length or any non-hex char. Case-insensitive.
inline bool parseMac12(const char* s, uint8_t out[6]) {
  if (!s) return false;
  int n = 0; while (s[n]) n++;
  if (n != 12) return false;
  for (int i = 0; i < 12; i++)
    if (!isxdigit((unsigned char)s[i])) return false;
  auto hx = [](char c) -> uint8_t {
    if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
    if (c >= 'a' && c <= 'f') return (uint8_t)(c - 'a' + 10);
    return (uint8_t)(c - 'A' + 10);
  };
  for (int i = 0; i < 6; i++)
    out[i] = (uint8_t)((hx(s[2 * i]) << 4) | hx(s[2 * i + 1]));
  return true;
}

// Map "wifi" | "ble" | "both" (case-insensitive) to a NodeRole. Returns false
// and leaves `out` untouched if unrecognised. Leading whitespace is skipped and
// the keyword may be followed by end-of-string, whitespace, or '#', so a value
// with a trailing inline comment (e.g. "wifi # Top Right") still matches — the
// generic key=value parser keeps the raw value, so roles are cleaned here.
inline bool roleFromStr(const char* v, uint8_t& out) {
  if (!v) return false;
  while (*v == ' ' || *v == '\t') v++;          // skip leading whitespace
  struct { const char* kw; uint8_t role; } kws[] = {
    { "both", NODE_ROLE_BOTH }, { "wifi", NODE_ROLE_WIFI }, { "ble", NODE_ROLE_BLE },
  };
  for (auto& k : kws) {
    int n = 0; while (k.kw[n]) n++;
    bool match = true;
    for (int i = 0; i < n; i++)
      if (tolower((unsigned char)v[i]) != k.kw[i]) { match = false; break; }
    if (!match) continue;
    char after = v[n];                            // must end the token cleanly
    if (after == 0 || after == ' ' || after == '\t' || after == ';' ||
        after == '#' || after == '\r' || after == '\n') {
      out = k.role;
      return true;
    }
  }
  return false;
}

inline const char* roleToStr(uint8_t r) {
  return r == NODE_ROLE_WIFI ? "wifi"
       : r == NODE_ROLE_BLE  ? "ble"
                             : "both";
}

// Single-char OLED glyph for a role: W / B / 2 (2 = both).
inline char roleGlyph(uint8_t r) {
  return r == NODE_ROLE_WIFI ? 'W'
       : r == NODE_ROLE_BLE  ? 'B'
                             : '2';
}

// Look up a MAC's role in a contiguous entry table; fall back to defaultRole.
inline uint8_t roleForMacIn(const NodeRoleEntry* tbl, uint8_t count,
                            const uint8_t mac[6], uint8_t defaultRole) {
  for (uint8_t i = 0; i < count; i++)
    if (memcmp(tbl[i].mac, mac, 6) == 0) return tbl[i].role;
  return defaultRole;
}

// Insert-or-update a MAC→role entry. Updates in place if the MAC already exists;
// otherwise appends if there is room. Returns false only when the table is full
// and the MAC is new (entry dropped). *count is updated on append.
inline bool nodeRoleUpsert(NodeRoleEntry* tbl, uint8_t* count, uint8_t maxCount,
                           const uint8_t mac[6], uint8_t role) {
  for (uint8_t i = 0; i < *count; i++) {
    if (memcmp(tbl[i].mac, mac, 6) == 0) {
      tbl[i].role = role;
      return true;
    }
  }
  if (*count >= maxCount) return false;
  memcpy(tbl[*count].mac, mac, 6);
  tbl[*count].role = role;
  (*count)++;
  return true;
}
