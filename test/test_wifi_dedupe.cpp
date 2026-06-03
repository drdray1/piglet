// Host-side tests for the Wi-Fi dedupe path: BSSID strings are parsed to 6
// bytes (parseBda) and fed to the shared BleDedupe ring with addrType 0, the
// same way appendWigleRow() and the node forward path do it. This guards both
// the parse and the log-once contract for Wi-Fi without needing SD/Arduino.
#include "doctest.h"
#include "BleDedupe.h"
#include "BleCsv.h"   // parseBda / formatBda

namespace {
// Emit-once-if-new through the ring, exactly as the firmware call sites do.
bool wifiEmit(BleDedupe& ring, const char* bssid) {
  uint8_t mac[6];
  parseBda(bssid, mac);
  return ring.shouldEmit(mac, 0);
}
}  // namespace

TEST_CASE("parseBda round-trips a BSSID display string") {
  uint8_t mac[6];
  parseBda("A1:B2:C3:D4:E5:F6", mac);
  CHECK(mac[0] == 0xA1);
  CHECK(mac[1] == 0xB2);
  CHECK(mac[2] == 0xC3);
  CHECK(mac[3] == 0xD4);
  CHECK(mac[4] == 0xE5);
  CHECK(mac[5] == 0xF6);

  char out[18];
  formatBda(mac, out);
  CHECK(std::string(out) == "A1:B2:C3:D4:E5:F6");
}

TEST_CASE("WiFi dedupe: a BSSID is logged once, repeats suppressed") {
  BleDedupe ring;
  CHECK(wifiEmit(ring, "DE:AD:BE:EF:00:01"));        // first -> log
  CHECK_FALSE(wifiEmit(ring, "DE:AD:BE:EF:00:01"));  // repeat -> skip
  CHECK_FALSE(wifiEmit(ring, "de:ad:be:ef:00:01"));  // strtoul is case-insensitive
  CHECK(ring.size() == 1);
}

TEST_CASE("WiFi dedupe: distinct BSSIDs each log once") {
  BleDedupe ring;
  CHECK(wifiEmit(ring, "DE:AD:BE:EF:00:01"));
  CHECK(wifiEmit(ring, "DE:AD:BE:EF:00:02"));
  CHECK_FALSE(wifiEmit(ring, "DE:AD:BE:EF:00:01"));
  CHECK_FALSE(wifiEmit(ring, "DE:AD:BE:EF:00:02"));
  CHECK(ring.size() == 2);
}

TEST_CASE("WiFi dedupe: shared ring dedupes across sources (solo + Core)") {
  // One ring stands in for the single appendWigleRow() chokepoint: a network
  // seen by the Core's own scan and forwarded by a node is written only once.
  BleDedupe ring;
  CHECK(wifiEmit(ring, "11:22:33:44:55:66"));        // Core's own scan
  CHECK_FALSE(wifiEmit(ring, "11:22:33:44:55:66"));  // same AP from a node
}
