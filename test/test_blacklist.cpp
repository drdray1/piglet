// Host-side tests for Blacklist.h — the pure-C save-file blacklist helpers used
// by Config (parse/save) and SDUtils.cpp (enforce at CSV write). Config.cpp is
// not host-includable (Arduino String / SD), so these exercise the header that
// holds the production parse/match logic. Mirrors test_node_role.cpp.
//
// Config form is one entry per line (key repeated), value = MAC/SSID plus an
// optional inline '# label': blacklistMac=AA:BB:CC:DD:EE:FF  # My phone.
#include "doctest.h"
#include "Blacklist.h"

TEST_CASE("parseMacLoose: bare 12-hex, colon and dash forms; rejects bad length/chars") {
  uint8_t m[6];
  CHECK(parseMacLoose("AABBCCDDEE01", m));
  CHECK(m[0] == 0xAA); CHECK(m[5] == 0x01);
  CHECK(parseMacLoose("AA:BB:CC:DD:EE:FF", m));
  CHECK(m[0] == 0xAA); CHECK(m[5] == 0xFF);
  CHECK(parseMacLoose("aa-bb-cc-dd-ee-01", m));   // dashes + lowercase
  CHECK(m[5] == 0x01);

  CHECK_FALSE(parseMacLoose("AA:BB:CC:DD:EE", m));      // 10 hex
  CHECK_FALSE(parseMacLoose("AA:BB:CC:DD:EE:FF:00", m));// 14 hex
  CHECK_FALSE(parseMacLoose("AA:BB:CC:DD:EE:ZZ", m));   // non-hex
  CHECK_FALSE(parseMacLoose("", m));
}

TEST_CASE("blacklistMacAdd: colon/bare forms, inline label captured, upsert, overflow") {
  BlacklistMac tbl[CFG_MAX_BLACKLIST];
  uint8_t count = 0;

  CHECK(blacklistMacAdd(tbl, &count, CFG_MAX_BLACKLIST, "AA:BB:CC:DD:EE:FF   # My phone"));
  CHECK(count == 1);
  CHECK(tbl[0].mac[0] == 0xAA); CHECK(tbl[0].mac[5] == 0xFF);
  CHECK(std::string(tbl[0].label) == "My phone");

  CHECK(blacklistMacAdd(tbl, &count, CFG_MAX_BLACKLIST, "112233445566"));  // bare, no label
  CHECK(count == 2);
  CHECK(tbl[1].mac[0] == 0x11);
  CHECK(tbl[1].label[0] == 0);

  // '//' comment style also works as a label.
  CHECK(blacklistMacAdd(tbl, &count, CFG_MAX_BLACKLIST, "01:02:03:04:05:06 // router"));
  CHECK(std::string(tbl[2].label) == "router");

  // Upsert: same MAC (different separator) updates label in place, count steady.
  CHECK(blacklistMacAdd(tbl, &count, CFG_MAX_BLACKLIST, "AABBCCDDEEFF # renamed"));
  CHECK(count == 3);
  CHECK(std::string(tbl[0].label) == "renamed");

  // Malformed MAC -> rejected, count unchanged.
  CHECK_FALSE(blacklistMacAdd(tbl, &count, CFG_MAX_BLACKLIST, "not-a-mac # x"));
  CHECK(count == 3);

  // Overflow: fill to cap with fresh MACs, then one more is dropped.
  count = 0;
  for (int i = 0; i < CFG_MAX_BLACKLIST; i++) {
    char line[32]; snprintf(line, sizeof(line), "AABBCCDDEE%02X", i & 0xFF);
    CHECK(blacklistMacAdd(tbl, &count, CFG_MAX_BLACKLIST, line));
  }
  CHECK(count == CFG_MAX_BLACKLIST);
  CHECK_FALSE(blacklistMacAdd(tbl, &count, CFG_MAX_BLACKLIST, "FFEEDDCCBBAA"));
  CHECK(count == CFG_MAX_BLACKLIST);
}

TEST_CASE("blacklistSsidAdd: label captured, spaces kept, '#' in name preserved, truncation, upsert") {
  BlacklistSsid tbl[CFG_MAX_BLACKLIST];
  uint8_t count = 0;

  CHECK(blacklistSsidAdd(tbl, &count, CFG_MAX_BLACKLIST, "MyHomeNet   # Home network"));
  CHECK(count == 1);
  CHECK(std::string(tbl[0].name) == "MyHomeNet");
  CHECK(std::string(tbl[0].label) == "Home network");

  // SSID with internal spaces and no comment.
  CHECK(blacklistSsidAdd(tbl, &count, CFG_MAX_BLACKLIST, "Coffee Shop Guest"));
  CHECK(std::string(tbl[1].name) == "Coffee Shop Guest");
  CHECK(tbl[1].label[0] == 0);

  // A '#' NOT preceded by whitespace stays part of the name (not a comment).
  CHECK(blacklistSsidAdd(tbl, &count, CFG_MAX_BLACKLIST, "Net#7 # seventh"));
  CHECK(std::string(tbl[2].name) == "Net#7");
  CHECK(std::string(tbl[2].label) == "seventh");

  // 33+ char SSID truncated to 32.
  CHECK(blacklistSsidAdd(tbl, &count, CFG_MAX_BLACKLIST, "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"));
  CHECK(strlen(tbl[3].name) == 32);

  // Upsert by case-insensitive name updates the label, count steady.
  uint8_t before = count;
  CHECK(blacklistSsidAdd(tbl, &count, CFG_MAX_BLACKLIST, "myhomenet # relabel"));
  CHECK(count == before);
  CHECK(std::string(tbl[0].label) == "relabel");

  // Empty value -> rejected.
  CHECK_FALSE(blacklistSsidAdd(tbl, &count, CFG_MAX_BLACKLIST, "   # only a comment"));
}

TEST_CASE("blacklistHasMac: hit / miss / empty table") {
  BlacklistMac tbl[2] = {};
  uint8_t a[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};
  uint8_t b[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x02};
  uint8_t c[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
  memcpy(tbl[0].mac, a, 6);
  memcpy(tbl[1].mac, b, 6);

  CHECK(blacklistHasMac(tbl, 2, a));
  CHECK(blacklistHasMac(tbl, 2, b));
  CHECK_FALSE(blacklistHasMac(tbl, 2, c));   // miss
  CHECK_FALSE(blacklistHasMac(tbl, 0, a));   // empty table
}

TEST_CASE("blacklistHasSsid: exact case-insensitive; substrings/near-miss do NOT match") {
  BlacklistSsid tbl[2] = {};
  snprintf(tbl[0].name, sizeof(tbl[0].name), "MyHomeNet");
  snprintf(tbl[1].name, sizeof(tbl[1].name), "iPhone");

  CHECK(blacklistHasSsid(tbl, 2, "MyHomeNet"));
  CHECK(blacklistHasSsid(tbl, 2, "myhomenet"));   // case-insensitive
  CHECK(blacklistHasSsid(tbl, 2, "IPHONE"));

  // Exact-only: a longer name that contains a blacklisted one must NOT match.
  CHECK_FALSE(blacklistHasSsid(tbl, 2, "MyHomeNet2"));
  CHECK_FALSE(blacklistHasSsid(tbl, 2, "MyHome"));
  CHECK_FALSE(blacklistHasSsid(tbl, 2, "iPhone of John"));

  CHECK_FALSE(blacklistHasSsid(tbl, 2, ""));      // empty never matches
  CHECK_FALSE(blacklistHasSsid(tbl, 2, nullptr));
  CHECK_FALSE(blacklistHasSsid(tbl, 0, "MyHomeNet"));  // empty table
}

TEST_CASE("end-to-end: configured entries filter as expected (Config/SDUtils flow)") {
  BlacklistMac  macs[CFG_MAX_BLACKLIST];
  BlacklistSsid ssids[CFG_MAX_BLACKLIST];
  uint8_t mc = 0, sc = 0;
  blacklistMacAdd(macs, &mc, CFG_MAX_BLACKLIST, "38:44:BE:A4:02:A4   # core");
  blacklistMacAdd(macs, &mc, CFG_MAX_BLACKLIST, "A1B2C3D4E5F6");
  blacklistSsidAdd(ssids, &sc, CFG_MAX_BLACKLIST, "MyHomeNet # home");
  blacklistSsidAdd(ssids, &sc, CFG_MAX_BLACKLIST, "iPhone");
  REQUIRE(mc == 2);
  REQUIRE(sc == 2);

  uint8_t listed[6] = {0x38, 0x44, 0xBE, 0xA4, 0x02, 0xA4};
  uint8_t other[6]  = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
  CHECK(blacklistHasMac(macs, mc, listed));
  CHECK_FALSE(blacklistHasMac(macs, mc, other));
  CHECK(blacklistHasSsid(ssids, sc, "iphone"));
  CHECK_FALSE(blacklistHasSsid(ssids, sc, "CoffeeShopWiFi"));
}
