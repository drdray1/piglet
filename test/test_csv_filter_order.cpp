// Host-side tests for the save-file blacklist filter ordering that gates CSV
// writes in the TDongleC5 sketch — appendBleRow (TDongleC5_Piglet.ino:704-711)
// and the mirrored guard in appendWigleRow (:650-657). appendBleRow/appendWigleRow
// touch logFile/SD and can't run host-side, so this exercises the decision the
// sketch makes BEFORE the write: drop-on-SSID-or-name, then drop-on-MAC but only
// when the address string is full-length (the `>= 17` gate), then format. The
// format step is the same formatBleRow() the firmware writes (BleCsv.h), so the
// "drop happens before format, never after" contract is pinned end to end.
#include "doctest.h"
#include <string>
#include <algorithm>
#include "Blacklist.h"
#include "BleCsv.h"   // parseBda / formatBleRow

namespace {

// Mirror of the appendBleRow / appendWigleRow pre-write guard. Returns true when
// the row would be dropped (blacklisted), matching the early `return;`s in the
// sketch. `addr` is the display-form BDA/BSSID string ("AA:BB:CC:DD:EE:FF").
bool wouldDrop(const BlacklistMac* macs, uint8_t macCount,
               const BlacklistSsid* ssids, uint8_t ssidCount,
               const std::string& name, const std::string& addr) {
  // 1) name/SSID checked first, before any MAC parse.
  if (blacklistHasSsid(ssids, ssidCount, name.c_str())) return true;
  // 2) MAC checked only for a full-length address (the `>= 17` gate).
  if (addr.length() >= 17) {
    uint8_t bl[6];
    parseBda(addr.c_str(), bl);
    if (blacklistHasMac(macs, macCount, bl)) return true;
  }
  return false;
}

// Small builder so each test starts from a known blacklist.
struct BL {
  BlacklistMac  macs[CFG_MAX_BLACKLIST];  uint8_t macCount = 0;
  BlacklistSsid ssids[CFG_MAX_BLACKLIST]; uint8_t ssidCount = 0;
  void addMac(const char* v)  { blacklistMacAdd(macs, &macCount, CFG_MAX_BLACKLIST, v); }
  void addSsid(const char* v) { blacklistSsidAdd(ssids, &ssidCount, CFG_MAX_BLACKLIST, v); }
  bool drops(const std::string& name, const std::string& addr) {
    return wouldDrop(macs, macCount, ssids, ssidCount, name, addr);
  }
};

}  // namespace

TEST_CASE("blacklisted SSID/name drops the row before the MAC is even parsed") {
  BL bl;
  bl.addSsid("iPhone");
  // Address is NOT blacklisted, but the name is -> dropped on the name check.
  CHECK(bl.drops("iPhone", "AA:BB:CC:DD:EE:FF"));
  // Case-insensitive, carried through from blacklistHasSsid.
  CHECK(bl.drops("IPHONE", "AA:BB:CC:DD:EE:FF"));
  // A different name with the same (unlisted) address passes.
  CHECK_FALSE(bl.drops("SomeBeacon", "AA:BB:CC:DD:EE:FF"));
}

TEST_CASE("blacklisted MAC drops the row when the address is full-length") {
  BL bl;
  bl.addMac("AA:BB:CC:DD:EE:FF");
  CHECK(bl.drops("", "AA:BB:CC:DD:EE:FF"));
  CHECK(bl.drops("AnyName", "aa:bb:cc:dd:ee:ff"));  // parseBda is case-insensitive
  // A different, unlisted address passes.
  CHECK_FALSE(bl.drops("", "11:22:33:44:55:66"));
}

TEST_CASE("the >= 17 gate: short/garbage address is never parsed or matched") {
  BL bl;
  bl.addMac("AA:BB:CC:DD:EE:FF");
  // Anything shorter than a full "AA:BB:CC:DD:EE:FF" (17 chars) skips the MAC
  // check entirely — guards parseBda against reading past a truncated string.
  CHECK_FALSE(bl.drops("", ""));
  CHECK_FALSE(bl.drops("", "AA:BB:CC"));        // 8 chars
  CHECK_FALSE(bl.drops("", "AA:BB:CC:DD:EE:F")); // 16 chars, one short
}

TEST_CASE("clean row passes the filter, then formats to the 14-column BLE line") {
  BL bl;
  bl.addSsid("MyHomeNet");
  bl.addMac("AA:BB:CC:DD:EE:FF");

  const std::string name = "AirTagLike";
  const std::string addr = "11:22:33:44:55:66";  // neither blacklisted
  REQUIRE_FALSE(bl.drops(name, addr));

  // Only after passing the guard does the sketch format/write the row.
  std::string row = formatBleRow(addr, name, /*addrType*/1, "2026-06-23T00:00:00",
                                 /*channel*/37, /*rssi*/-60, 0.0, 0.0, 0.0, 0.0,
                                 /*serviceUuids*/"", /*mfgrId*/0x004C);
  // 14 WiGLE columns -> 13 commas, and the BLE type marker is last.
  CHECK(std::count(row.begin(), row.end(), ',') == 13);
  CHECK(row.rfind(",BLE") == row.size() - 4);
  CHECK(row.rfind("11:22:33:44:55:66", 0) == 0);  // row starts with the BDA
}

TEST_CASE("name blacklist beats a clean MAC; MAC blacklist beats a clean name") {
  BL bl;
  bl.addSsid("BadName");
  bl.addMac("AA:BB:CC:DD:EE:FF");
  // Either condition alone is sufficient to drop (independent early returns).
  CHECK(bl.drops("BadName", "11:22:33:44:55:66"));   // name only
  CHECK(bl.drops("GoodName", "AA:BB:CC:DD:EE:FF"));   // MAC only
  CHECK_FALSE(bl.drops("GoodName", "11:22:33:44:55:66"));
}
