// Host-side tests for NodeRole.h — the pure-C per-node scan-role helpers shared
// by Config (parse/save), MeshNode.cpp and PigletNode.ino. Config.cpp itself is
// not host-includable (Arduino String / SD), so these exercise the header that
// holds the production parse/lookup/upsert logic.
#include "doctest.h"
#include "NodeRole.h"

namespace {
void mkMac(uint8_t first, uint8_t out[6]) {
  out[0] = first; out[1] = 0x22; out[2] = 0x33;
  out[3] = 0x44;  out[4] = 0x55; out[5] = 0x66;
}
}  // namespace

TEST_CASE("parseMac12: valid 12-hex (upper/lower) -> bytes") {
  uint8_t mac[6];
  CHECK(parseMac12("A1B2C3D4E5F6", mac));
  CHECK(mac[0] == 0xA1); CHECK(mac[1] == 0xB2); CHECK(mac[2] == 0xC3);
  CHECK(mac[3] == 0xD4); CHECK(mac[4] == 0xE5); CHECK(mac[5] == 0xF6);

  uint8_t lo[6];
  CHECK(parseMac12("a1b2c3d4e5f6", lo));
  CHECK(memcmp(mac, lo, 6) == 0);  // case-insensitive
}

TEST_CASE("parseMac12: rejects wrong length and non-hex / separators") {
  uint8_t mac[6];
  CHECK_FALSE(parseMac12("A1B2C3D4E5F", mac));        // 11 chars
  CHECK_FALSE(parseMac12("A1B2C3D4E5F6A", mac));      // 13 chars
  CHECK_FALSE(parseMac12("A1:B2:C3:D4:E5:F6", mac));  // colons -> wrong length
  CHECK_FALSE(parseMac12("A1B2C3D4E5FZ", mac));       // non-hex 'Z'
  CHECK_FALSE(parseMac12("", mac));
}

TEST_CASE("roleFromStr / roleToStr round-trip; unknown rejected") {
  uint8_t r = 0xEE;
  CHECK(roleFromStr("wifi", r)); CHECK(r == NODE_ROLE_WIFI);
  CHECK(roleFromStr("BLE", r));  CHECK(r == NODE_ROLE_BLE);
  CHECK(roleFromStr("Both", r)); CHECK(r == NODE_ROLE_BOTH);

  CHECK(std::string(roleToStr(NODE_ROLE_WIFI)) == "wifi");
  CHECK(std::string(roleToStr(NODE_ROLE_BLE))  == "ble");
  CHECK(std::string(roleToStr(NODE_ROLE_BOTH)) == "both");

  uint8_t keep = NODE_ROLE_BOTH;
  CHECK_FALSE(roleFromStr("bogus", keep));  // unknown
  CHECK(keep == NODE_ROLE_BOTH);            // left untouched
  CHECK_FALSE(roleFromStr(nullptr, keep));
}

TEST_CASE("roleFromStr: tolerates inline comments / whitespace, rejects partials") {
  uint8_t r = 0xEE;
  CHECK(roleFromStr("wifi # Top Right", r)); CHECK(r == NODE_ROLE_WIFI);
  CHECK(roleFromStr("ble # Bottom Right (BLE wardriver)", r)); CHECK(r == NODE_ROLE_BLE);
  CHECK(roleFromStr("  both", r)); CHECK(r == NODE_ROLE_BOTH);   // leading ws
  CHECK(roleFromStr("wifi\t#x", r)); CHECK(r == NODE_ROLE_WIFI); // tab then comment

  uint8_t keep = NODE_ROLE_BOTH;
  CHECK_FALSE(roleFromStr("wifi2", keep));    // not a clean token end
  CHECK_FALSE(roleFromStr("blether", keep));  // 'ble' is a prefix but not a token
  CHECK(keep == NODE_ROLE_BOTH);
}

TEST_CASE("role delivered in Biscuit type-10 config string (;role=...)") {
  // Mirror PigletNode's type-10 parser: find "role=" in the config payload and
  // feed the remainder to roleFromStr (which stops at ';' / end).
  auto roleFromConfig = [](const char* cfg, uint8_t& out) {
    const char* r = strstr(cfg, "role=");
    return r ? roleFromStr(r + 5, out) : false;
  };
  uint8_t r = 0xEE;
  CHECK(roleFromConfig("channels=1,2,3;dwell=80;role=ble", r)); CHECK(r == NODE_ROLE_BLE);
  CHECK(roleFromConfig("channels=36,40;dwell=80;role=wifi", r)); CHECK(r == NODE_ROLE_WIFI);
  // role= not last (followed by another ';' token) still parses cleanly.
  CHECK(roleFromConfig("role=both;channels=1", r)); CHECK(r == NODE_ROLE_BOTH);
  // No role= token -> no change.
  uint8_t keep = NODE_ROLE_WIFI;
  CHECK_FALSE(roleFromConfig("channels=1,2;dwell=80", keep));
  CHECK(keep == NODE_ROLE_WIFI);
}

TEST_CASE("roleForMacIn: listed -> role, unlisted/empty -> default") {
  NodeRoleEntry tbl[2];
  mkMac(0xAA, tbl[0].mac); tbl[0].role = NODE_ROLE_WIFI;
  mkMac(0xBB, tbl[1].mac); tbl[1].role = NODE_ROLE_BLE;

  uint8_t a[6]; mkMac(0xAA, a);
  uint8_t c[6]; mkMac(0xCC, c);
  CHECK(roleForMacIn(tbl, 2, a, NODE_ROLE_BOTH) == NODE_ROLE_WIFI);
  CHECK(roleForMacIn(tbl, 2, c, NODE_ROLE_BOTH) == NODE_ROLE_BOTH);  // unlisted -> default
  CHECK(roleForMacIn(tbl, 0, a, NODE_ROLE_WIFI) == NODE_ROLE_WIFI);  // empty -> default
}

TEST_CASE("nodeRoleUpsert: insert, update-in-place, overflow no-op") {
  NodeRoleEntry tbl[3];
  uint8_t count = 0;
  uint8_t m1[6], m2[6], m3[6], m4[6];
  mkMac(0x01, m1); mkMac(0x02, m2); mkMac(0x03, m3); mkMac(0x04, m4);

  CHECK(nodeRoleUpsert(tbl, &count, 3, m1, NODE_ROLE_WIFI));
  CHECK(nodeRoleUpsert(tbl, &count, 3, m2, NODE_ROLE_BLE));
  CHECK(nodeRoleUpsert(tbl, &count, 3, m3, NODE_ROLE_BOTH));
  CHECK(count == 3);

  // Update in place -> count unchanged, role replaced.
  CHECK(nodeRoleUpsert(tbl, &count, 3, m1, NODE_ROLE_BLE));
  CHECK(count == 3);
  CHECK(roleForMacIn(tbl, count, m1, NODE_ROLE_BOTH) == NODE_ROLE_BLE);

  // Overflow with a new MAC -> dropped, no OOB, count stays at cap.
  CHECK_FALSE(nodeRoleUpsert(tbl, &count, 3, m4, NODE_ROLE_WIFI));
  CHECK(count == 3);
  CHECK(roleForMacIn(tbl, count, m4, NODE_ROLE_BOTH) == NODE_ROLE_BOTH);  // not stored
}

TEST_CASE("parse+save round-trip: node.<hex>=role re-emits identically") {
  // Mirror Config.cpp: parse "node.<hex>=role" -> upsert; save -> roleToStr.
  NodeRoleEntry tbl[CFG_MAX_NODE_ROLES];
  uint8_t count = 0;
  struct { const char* hex; const char* role; } in[] = {
    {"AABBCCDDEE01", "wifi"}, {"AABBCCDDEE02", "ble"}, {"AABBCCDDEE03", "both"},
  };
  for (auto& e : in) {
    uint8_t mac[6], role;
    REQUIRE(parseMac12(e.hex, mac));
    REQUIRE(roleFromStr(e.role, role));
    nodeRoleUpsert(tbl, &count, CFG_MAX_NODE_ROLES, mac, role);
  }
  REQUIRE(count == 3);

  // Write-back form must equal the input.
  for (int i = 0; i < 3; i++) {
    const uint8_t* m = tbl[i].mac;
    char line[48];
    snprintf(line, sizeof(line), "node.%02X%02X%02X%02X%02X%02X=%s",
             m[0], m[1], m[2], m[3], m[4], m[5], roleToStr(tbl[i].role));
    char expect[48];
    snprintf(expect, sizeof(expect), "node.%s=%s", in[i].hex, in[i].role);
    CHECK(std::string(line) == std::string(expect));
  }
}
