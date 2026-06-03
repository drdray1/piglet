// Host-side tests for BleDedupe.h — the "log once" MAC dedupe ring shared by
// Wi-Fi (addrType 0) and BLE (real addrType). No clock: a device emits the
// first time it is seen and is suppressed thereafter, until evicted past the
// cap (FIFO).
#include "doctest.h"
#include "BleDedupe.h"

namespace {
// Helper: build a MAC/BDA from a single varying byte so tests read clearly.
void mkMac(uint8_t first, uint8_t out[6]) {
  out[0] = first; out[1] = 0x22; out[2] = 0x33;
  out[3] = 0x44;  out[4] = 0x55; out[5] = 0x66;
}
}  // namespace

TEST_CASE("BleDedupe: first sighting emits, repeats suppressed") {
  BleDedupe d;
  uint8_t mac[6]; mkMac(0x11, mac);
  CHECK(d.shouldEmit(mac, 0));        // first time -> emit
  CHECK_FALSE(d.shouldEmit(mac, 0));  // seen -> suppress
  CHECK_FALSE(d.shouldEmit(mac, 0));  // still suppressed, no time window
  CHECK(d.size() == 1);
}

TEST_CASE("BleDedupe: distinct MACs each emit once") {
  BleDedupe d;
  for (uint8_t i = 0; i < 4; i++) {
    uint8_t mac[6]; mkMac(i, mac);
    CHECK(d.shouldEmit(mac, 0));
    CHECK_FALSE(d.shouldEmit(mac, 0));
  }
  CHECK(d.size() == 4);
}

TEST_CASE("BleDedupe: same MAC bytes but different addrType is a different key") {
  BleDedupe d;
  uint8_t mac[6]; mkMac(0x11, mac);
  CHECK(d.shouldEmit(mac, 0));   // public
  CHECK(d.shouldEmit(mac, 1));   // random — distinct device
  CHECK_FALSE(d.shouldEmit(mac, 0));
  CHECK_FALSE(d.shouldEmit(mac, 1));
  CHECK(d.size() == 2);
}

TEST_CASE("BleDedupe: Wi-Fi BSSIDs (addrType 0) log once") {
  BleDedupe d;
  uint8_t a[6]; mkMac(0xAA, a);
  uint8_t b[6]; mkMac(0xBB, b);
  CHECK(d.shouldEmit(a, 0));
  CHECK(d.shouldEmit(b, 0));
  CHECK_FALSE(d.shouldEmit(a, 0));
  CHECK_FALSE(d.shouldEmit(b, 0));
}

TEST_CASE("BleDedupe: bounded size evicts oldest, evicted re-emits") {
  BleDedupe d(3);
  for (uint8_t i = 0; i < 5; i++) {
    uint8_t mac[6]; mkMac(i, mac);
    d.shouldEmit(mac, 0);          // 5 distinct -> only last 3 retained
  }
  CHECK(d.size() == 3);            // capped

  // The two oldest (0x00, 0x01) were evicted, so they emit as "new" again.
  uint8_t old0[6]; mkMac(0x00, old0);
  CHECK(d.shouldEmit(old0, 0));
  // A survivor (0x04) is still suppressed.
  uint8_t keep[6]; mkMac(0x04, keep);
  CHECK_FALSE(d.shouldEmit(keep, 0));
}

TEST_CASE("BleDedupe: default cap is 200") {
  BleDedupe d;  // default
  // Insert 200 distinct two-byte-varying MACs to fill the ring exactly.
  auto mk = [](uint16_t v, uint8_t out[6]) {
    out[0] = (uint8_t)(v & 0xFF); out[1] = (uint8_t)(v >> 8);
    out[2] = 0x33; out[3] = 0x44; out[4] = 0x55; out[5] = 0x66;
  };
  for (uint16_t i = 0; i < 200; i++) {
    uint8_t mac[6]; mk(i, mac);
    CHECK(d.shouldEmit(mac, 0));
  }
  CHECK(d.size() == 200);

  // The very first entry is still present (not yet over cap).
  uint8_t first[6]; mk(0, first);
  CHECK_FALSE(d.shouldEmit(first, 0));

  // One more distinct MAC pushes size over 200 -> oldest (idx 0) evicted.
  uint8_t extra[6]; mk(1000, extra);
  CHECK(d.shouldEmit(extra, 0));
  CHECK(d.size() == 200);
}

TEST_CASE("BleDedupe: clear() empties the ring") {
  BleDedupe d;
  uint8_t mac[6]; mkMac(0x11, mac);
  d.shouldEmit(mac, 0);
  d.clear();
  CHECK(d.size() == 0);
  CHECK(d.shouldEmit(mac, 0));     // fresh again
}
