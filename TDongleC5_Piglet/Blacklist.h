#pragma once
// Save-file blacklist: SSIDs / BLE-names and MAC/BSSID addresses that must never
// be written to the WiGLE CSV (e.g. the operator's own home Wi-Fi or phone).
//
// Pure C, no Arduino String or SD dependencies, so this header is included by
// both the device firmware (Config.h / SDUtils.cpp) AND the host-side doctest
// harness (test/test_blacklist.cpp). Keep it dependency-free.
//
// Config form is ONE ENTRY PER LINE, repeating the key:
//   blacklistMac=AA:BB:CC:DD:EE:FF   # My phone
//   blacklistMac=112233445566        # Home router (colons optional)
//   blacklistSsid=MyHomeNet          # Home network
//   blacklistSsid=iPhone             # phone BLE name
// A trailing inline comment (a '#' or '//' at the start of the value or preceded
// by whitespace) is captured as a human label, stored, and re-emitted on save.
// Matching is EXACT: a MAC on full 6-byte equality, an SSID/name on a
// case-insensitive whole-string compare (no wildcards/substrings).
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include "NodeRole.h"   // reuse parseMac12()

// Max blacklist entries of each kind held in config (memory knob). 16 covers a
// home network, a handful of personal devices, and a few noisy fixtures.
#ifndef CFG_MAX_BLACKLIST
#define CFG_MAX_BLACKLIST 16
#endif
// Per-entry human label (NUL-terminated). Stored so labels survive a config save.
#ifndef CFG_BLACKLIST_LABEL_LEN
#define CFG_BLACKLIST_LABEL_LEN 32
#endif

struct BlacklistMac  { uint8_t mac[6]; char label[CFG_BLACKLIST_LABEL_LEN]; };
struct BlacklistSsid { char name[33]; char label[CFG_BLACKLIST_LABEL_LEN]; };  // 32 + NUL

// Case-insensitive whole-string compare (host-portable; avoids the non-standard
// strcasecmp). Returns true when a and b are equal ignoring case.
inline bool blEqualsIgnoreCase(const char* a, const char* b) {
  if (!a || !b) return false;
  while (*a && *b) {
    if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
    a++; b++;
  }
  return *a == 0 && *b == 0;
}

// Find the start of an inline comment in a config value: the first '#' or "//"
// that is at the start of the string or preceded by whitespace. Returns a
// pointer to end-of-string when there is none. The whitespace-boundary rule lets
// an SSID contain a '#' mid-name (e.g. "Net#7") as long as it isn't space-preceded.
inline const char* blFindComment(const char* s) {
  for (const char* p = s; *p; p++) {
    bool boundary = (p == s) || p[-1] == ' ' || p[-1] == '\t';
    if (boundary && (*p == '#' || (p[0] == '/' && p[1] == '/'))) return p;
  }
  return s + strlen(s);
}

// Extract the label text following a comment marker into out (trimmed, truncated).
// `cmt` points at the marker (or end-of-string, which yields an empty label).
inline void blExtractLabel(const char* cmt, char* out, int outMax) {
  out[0] = 0;
  if (!*cmt) return;
  if (*cmt == '#') cmt++;
  else if (cmt[0] == '/' && cmt[1] == '/') cmt += 2;
  while (*cmt == ' ' || *cmt == '\t') cmt++;
  int n = 0; while (cmt[n]) n++;
  while (n > 0 && (cmt[n-1] == ' ' || cmt[n-1] == '\t' ||
                   cmt[n-1] == '\r' || cmt[n-1] == '\n')) n--;
  if (n > outMax - 1) n = outMax - 1;
  memcpy(out, cmt, n);
  out[n] = 0;
}

// Parse a MAC in 12-hex ("AABBCCDDEE01") or separated ("AA:BB:CC:DD:EE:FF",
// "AA-BB-..") form into out[6]. Separators (':' '-' space/tab) are ignored;
// exactly 12 hex digits must remain. Returns false otherwise.
inline bool parseMacLoose(const char* s, uint8_t out[6]) {
  if (!s) return false;
  char hex[13];
  int h = 0;
  for (const char* p = s; *p; p++) {
    if (*p == ':' || *p == '-' || *p == ' ' || *p == '\t') continue;
    if (h >= 12) return false;                    // too many hex digits
    if (!isxdigit((unsigned char)*p)) return false;
    hex[h++] = *p;
  }
  if (h != 12) return false;
  hex[12] = 0;
  return parseMac12(hex, out);
}

// Insert-or-update one MAC blacklist entry from a raw config value (identifier +
// optional inline-comment label). Updates the label in place if the MAC already
// exists; otherwise appends if there is room. Returns false on a malformed MAC
// or when the table is full and the MAC is new.
inline bool blacklistMacAdd(BlacklistMac* tbl, uint8_t* count, uint8_t maxCount,
                            const char* value) {
  if (!value) return false;
  const char* cmt = blFindComment(value);
  // Identifier = value up to the comment, trimmed.
  char ident[40];
  int n = (int)(cmt - value);
  if (n > (int)sizeof(ident) - 1) n = sizeof(ident) - 1;
  while (n > 0 && (value[n-1] == ' ' || value[n-1] == '\t')) n--;
  memcpy(ident, value, n);
  ident[n] = 0;

  uint8_t mac[6];
  if (!parseMacLoose(ident, mac)) return false;

  char label[CFG_BLACKLIST_LABEL_LEN];
  blExtractLabel(cmt, label, sizeof(label));

  for (uint8_t i = 0; i < *count; i++) {
    if (memcmp(tbl[i].mac, mac, 6) == 0) {
      memcpy(tbl[i].label, label, sizeof(label));
      return true;
    }
  }
  if (*count >= maxCount) return false;
  memcpy(tbl[*count].mac, mac, 6);
  memcpy(tbl[*count].label, label, sizeof(label));
  (*count)++;
  return true;
}

// Insert-or-update one SSID/BLE-name blacklist entry from a raw config value.
// Name is the value up to the inline comment, trimmed and truncated to 32 chars;
// the comment becomes the label. Upsert keys on the name (case-insensitive).
inline bool blacklistSsidAdd(BlacklistSsid* tbl, uint8_t* count, uint8_t maxCount,
                             const char* value) {
  if (!value) return false;
  const char* cmt = blFindComment(value);
  const char* start = value;
  const char* end = cmt;
  while (start < end && (*start == ' ' || *start == '\t')) start++;
  while (end > start && (end[-1] == ' ' || end[-1] == '\t' ||
                         end[-1] == '\r' || end[-1] == '\n')) end--;
  int len = (int)(end - start);
  if (len <= 0) return false;
  if (len > 32) len = 32;

  char name[33];
  memcpy(name, start, len);
  name[len] = 0;

  char label[CFG_BLACKLIST_LABEL_LEN];
  blExtractLabel(cmt, label, sizeof(label));

  for (uint8_t i = 0; i < *count; i++) {
    if (blEqualsIgnoreCase(tbl[i].name, name)) {
      memcpy(tbl[i].label, label, sizeof(label));
      return true;
    }
  }
  if (*count >= maxCount) return false;
  memcpy(tbl[*count].name, name, (size_t)len + 1);
  memcpy(tbl[*count].label, label, sizeof(label));
  (*count)++;
  return true;
}

// True if mac appears in the table (exact 6-byte match).
inline bool blacklistHasMac(const BlacklistMac* tbl, uint8_t count, const uint8_t mac[6]) {
  for (uint8_t i = 0; i < count; i++)
    if (memcmp(tbl[i].mac, mac, 6) == 0) return true;
  return false;
}

// True if ssid matches a table entry exactly (case-insensitive). An empty/NULL
// ssid never matches.
inline bool blacklistHasSsid(const BlacklistSsid* tbl, uint8_t count, const char* ssid) {
  if (!ssid || !ssid[0]) return false;
  for (uint8_t i = 0; i < count; i++)
    if (blEqualsIgnoreCase(tbl[i].name, ssid)) return true;
  return false;
}
