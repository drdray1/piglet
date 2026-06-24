// Host-side tests for the config-key dispatch added to the TDongleC5 sketch
// (cfgAssignKV branches at TDongleC5_Piglet.ino:405-420). cfgAssignKV itself is
// not host-includable (it lives in a .ino that pulls in Arduino String / SD /
// WiFi), so this mirrors the dispatch — key routing, MAC-prefix extraction, and
// the per-branch validation guards — over a small fixture struct, exactly as
// test_node_role.cpp:116 mirrors the parse+save round-trip. The branch bodies
// call the same pure helpers the firmware does (roleFromStr / parseMac12 /
// nodeRoleUpsert / blacklist*), so a drift in routing or guard logic shows up here.
#include "doctest.h"
#include <string>
#include "NodeRole.h"
#include "Blacklist.h"

namespace {

// Minimal stand-in for the relevant slice of the sketch's `struct Config`.
struct CfgFixture {
  uint8_t       nodeDefaultRole = NODE_ROLE_BOTH;
  NodeRoleEntry nodeRoles[CFG_MAX_NODE_ROLES];
  uint8_t       nodeRoleCount = 0;
  BlacklistMac  blacklistMacs[CFG_MAX_BLACKLIST];
  uint8_t       blacklistMacCount = 0;
  BlacklistSsid blacklistSsids[CFG_MAX_BLACKLIST];
  uint8_t       blacklistSsidCount = 0;
};

// Byte-for-byte mirror of the new cfgAssignKV branches (TDongleC5_Piglet.ino).
// std::string here stands in for the firmware's Arduino String; .substr(5)
// mirrors k.substring(5), .c_str() matches the real call sites.
void assignKV(CfgFixture& cfg, const std::string& k, const std::string& v) {
  if (k == "nodeDefaultRole") {
    uint8_t r;
    if (roleFromStr(v.c_str(), r)) cfg.nodeDefaultRole = r;
  } else if (k.rfind("node.", 0) == 0) {  // k.startsWith("node.")
    uint8_t mac[6], role;
    std::string macHex = k.substr(5);
    if (parseMac12(macHex.c_str(), mac) && roleFromStr(v.c_str(), role))
      nodeRoleUpsert(cfg.nodeRoles, &cfg.nodeRoleCount, CFG_MAX_NODE_ROLES, mac, role);
  } else if (k == "blacklistMac") {
    blacklistMacAdd(cfg.blacklistMacs, &cfg.blacklistMacCount, CFG_MAX_BLACKLIST, v.c_str());
  } else if (k == "blacklistSsid") {
    blacklistSsidAdd(cfg.blacklistSsids, &cfg.blacklistSsidCount, CFG_MAX_BLACKLIST, v.c_str());
  }
}

}  // namespace

TEST_CASE("nodeDefaultRole: valid values set the default, unknown is rejected") {
  CfgFixture cfg;
  CHECK(cfg.nodeDefaultRole == NODE_ROLE_BOTH);  // sketch default

  assignKV(cfg, "nodeDefaultRole", "wifi");
  CHECK(cfg.nodeDefaultRole == NODE_ROLE_WIFI);

  assignKV(cfg, "nodeDefaultRole", "ble # inline comment tolerated");
  CHECK(cfg.nodeDefaultRole == NODE_ROLE_BLE);

  // Unknown value leaves the previous default untouched (roleFromStr returns false).
  assignKV(cfg, "nodeDefaultRole", "bogus");
  CHECK(cfg.nodeDefaultRole == NODE_ROLE_BLE);
}

TEST_CASE("node.<12hex>=role: upserts; malformed MAC or role is a no-op") {
  CfgFixture cfg;

  SUBCASE("well-formed entries land and resolve") {
    assignKV(cfg, "node.AABBCCDDEE01", "wifi");
    assignKV(cfg, "node.AABBCCDDEE02", "ble");
    CHECK(cfg.nodeRoleCount == 2);

    uint8_t m1[6], m2[6];
    REQUIRE(parseMac12("AABBCCDDEE01", m1));
    REQUIRE(parseMac12("AABBCCDDEE02", m2));
    CHECK(roleForMacIn(cfg.nodeRoles, cfg.nodeRoleCount, m1, NODE_ROLE_BOTH) == NODE_ROLE_WIFI);
    CHECK(roleForMacIn(cfg.nodeRoles, cfg.nodeRoleCount, m2, NODE_ROLE_BOTH) == NODE_ROLE_BLE);
  }

  SUBCASE("re-assigning the same MAC updates in place, count unchanged") {
    assignKV(cfg, "node.AABBCCDDEE01", "wifi");
    assignKV(cfg, "node.AABBCCDDEE01", "ble");
    CHECK(cfg.nodeRoleCount == 1);
    uint8_t m[6]; REQUIRE(parseMac12("AABBCCDDEE01", m));
    CHECK(roleForMacIn(cfg.nodeRoles, cfg.nodeRoleCount, m, NODE_ROLE_BOTH) == NODE_ROLE_BLE);
  }

  SUBCASE("malformed MAC -> no phantom entry") {
    assignKV(cfg, "node.ZZBBCCDDEE01", "wifi");  // non-hex
    assignKV(cfg, "node.AABBCC", "wifi");        // too short (6 hex)
    assignKV(cfg, "node.AABBCCDDEE0102", "ble"); // too long (16 hex)
    CHECK(cfg.nodeRoleCount == 0);
  }

  SUBCASE("valid MAC but bad role -> no entry (both guards must pass)") {
    assignKV(cfg, "node.AABBCCDDEE01", "bogus");
    CHECK(cfg.nodeRoleCount == 0);
  }
}

TEST_CASE("node. prefix extraction (substring(5)) takes exactly the MAC hex") {
  CfgFixture cfg;
  // "node." is 5 chars; the remainder must be the 12-hex MAC and nothing else.
  assignKV(cfg, "node.0011223344FF", "both");
  CHECK(cfg.nodeRoleCount == 1);
  uint8_t m[6]; REQUIRE(parseMac12("0011223344FF", m));
  CHECK(roleForMacIn(cfg.nodeRoles, cfg.nodeRoleCount, m, NODE_ROLE_WIFI) == NODE_ROLE_BOTH);
}

TEST_CASE("blacklistMac / blacklistSsid route to their tables and are findable") {
  CfgFixture cfg;

  assignKV(cfg, "blacklistMac", "AA:BB:CC:DD:EE:FF  # My phone");
  assignKV(cfg, "blacklistMac", "112233445566");        // colons optional
  assignKV(cfg, "blacklistSsid", "MyHomeNet # home wifi");
  assignKV(cfg, "blacklistSsid", "iPhone");

  CHECK(cfg.blacklistMacCount == 2);
  CHECK(cfg.blacklistSsidCount == 2);

  uint8_t a[6]; REQUIRE(parseMacLoose("AA:BB:CC:DD:EE:FF", a));
  uint8_t b[6]; REQUIRE(parseMacLoose("112233445566", b));
  uint8_t miss[6]; REQUIRE(parseMacLoose("0102030405FF", miss));
  CHECK(blacklistHasMac(cfg.blacklistMacs, cfg.blacklistMacCount, a));
  CHECK(blacklistHasMac(cfg.blacklistMacs, cfg.blacklistMacCount, b));
  CHECK_FALSE(blacklistHasMac(cfg.blacklistMacs, cfg.blacklistMacCount, miss));

  // SSID match is case-insensitive whole-string.
  CHECK(blacklistHasSsid(cfg.blacklistSsids, cfg.blacklistSsidCount, "MYHOMENET"));
  CHECK(blacklistHasSsid(cfg.blacklistSsids, cfg.blacklistSsidCount, "iphone"));
  CHECK_FALSE(blacklistHasSsid(cfg.blacklistSsids, cfg.blacklistSsidCount, "iPhone 14"));
}

TEST_CASE("full mini-config parses into the expected counts and lookups") {
  CfgFixture cfg;
  struct { const char* k; const char* v; } lines[] = {
    {"nodeDefaultRole", "ble"},
    {"node.AABBCCDDEE01", "wifi"},
    {"node.AABBCCDDEE02", "both"},
    {"blacklistMac", "38:44:BE:A4:02:A4 # core"},
    {"blacklistSsid", "Net#7"},               // '#' mid-name is part of the SSID
    {"unknownKey", "ignored"},                // unrelated key falls through
  };
  for (auto& l : lines) assignKV(cfg, l.k, l.v);

  CHECK(cfg.nodeDefaultRole == NODE_ROLE_BLE);
  CHECK(cfg.nodeRoleCount == 2);
  CHECK(cfg.blacklistMacCount == 1);
  CHECK(cfg.blacklistSsidCount == 1);

  // Unlisted MAC resolves to the configured default (ble), listed ones to theirs.
  uint8_t listed[6]; REQUIRE(parseMac12("AABBCCDDEE01", listed));
  uint8_t other[6];  REQUIRE(parseMac12("FFEEDDCCBBAA", other));
  CHECK(roleForMacIn(cfg.nodeRoles, cfg.nodeRoleCount, listed, cfg.nodeDefaultRole) == NODE_ROLE_WIFI);
  CHECK(roleForMacIn(cfg.nodeRoles, cfg.nodeRoleCount, other,  cfg.nodeDefaultRole) == NODE_ROLE_BLE);

  // The '#'-containing SSID survived as a whole-name entry (whitespace-boundary rule).
  CHECK(blacklistHasSsid(cfg.blacklistSsids, cfg.blacklistSsidCount, "Net#7"));
}
