// Host-side tests for BleDedupe.h — the per-device BLE dedupe ring.
// Clock is injected (nowMs) so these run without Arduino/NimBLE.
#include "doctest.h"
#include "BleDedupe.h"

namespace {
// Helper: build a BDA from a single varying byte so tests read clearly.
void mkBda(uint8_t first, uint8_t out[6]) {
  out[0] = first; out[1] = 0x22; out[2] = 0x33;
  out[3] = 0x44;  out[4] = 0x55; out[5] = 0x66;
}
}  // namespace

TEST_CASE("BleDedupe: first sighting emits") {
  BleDedupe d(300, 500);
  uint8_t bda[6]; mkBda(0x11, bda);
  CHECK(d.shouldEmit(bda, 0, 0));
  CHECK(d.size() == 1);
}

TEST_CASE("BleDedupe: second sighting within window is suppressed") {
  BleDedupe d(300, 500);
  uint8_t bda[6]; mkBda(0x11, bda);
  CHECK(d.shouldEmit(bda, 0, 0));
  CHECK_FALSE(d.shouldEmit(bda, 0, 1000));     // +1 s, still inside 300 s
  CHECK_FALSE(d.shouldEmit(bda, 0, 299000));   // +299 s, still inside
  CHECK(d.size() == 1);
}

TEST_CASE("BleDedupe: re-emitted after the window elapses") {
  BleDedupe d(300, 500);
  uint8_t bda[6]; mkBda(0x11, bda);
  CHECK(d.shouldEmit(bda, 0, 0));
  CHECK(d.shouldEmit(bda, 0, 300000));         // exactly one window later
  CHECK(d.shouldEmit(bda, 0, 600001));         // another window later
}

TEST_CASE("BleDedupe: window measured from last emit, not last sighting") {
  // Suppressed sightings must NOT push the re-emit deadline out.
  BleDedupe d(300, 500);
  uint8_t bda[6]; mkBda(0x11, bda);
  CHECK(d.shouldEmit(bda, 0, 0));
  CHECK_FALSE(d.shouldEmit(bda, 0, 200000));   // suppressed, no refresh
  CHECK(d.shouldEmit(bda, 0, 300000));         // 300 s after the *emit* -> emit
}

TEST_CASE("BleDedupe: same BDA bytes but different addrType is a different key") {
  BleDedupe d(300, 500);
  uint8_t bda[6]; mkBda(0x11, bda);
  CHECK(d.shouldEmit(bda, 0, 0));   // public
  CHECK(d.shouldEmit(bda, 1, 0));   // random — distinct device
  CHECK(d.size() == 2);
}

TEST_CASE("BleDedupe: window=0 never suppresses") {
  BleDedupe d(0, 500);
  uint8_t bda[6]; mkBda(0x11, bda);
  CHECK(d.shouldEmit(bda, 0, 0));
  CHECK(d.shouldEmit(bda, 0, 0));   // same instant, still emits
  CHECK(d.shouldEmit(bda, 0, 1));
}

TEST_CASE("BleDedupe: bounded size evicts oldest") {
  BleDedupe d(300, 3);
  for (uint8_t i = 0; i < 5; i++) {
    uint8_t bda[6]; mkBda(i, bda);
    d.shouldEmit(bda, 0, i * 10);   // distinct devices, increasing time
  }
  CHECK(d.size() == 3);             // capped

  // The two oldest (0x00, 0x01) were evicted, so they emit as "new" again.
  uint8_t old0[6]; mkBda(0x00, old0);
  CHECK(d.shouldEmit(old0, 0, 1000));
  // A survivor (0x04) seen recently is still suppressed.
  uint8_t keep[6]; mkBda(0x04, keep);
  CHECK_FALSE(d.shouldEmit(keep, 0, 1000));
}

TEST_CASE("BleDedupe: expire() prunes stale entries, frees re-emit") {
  BleDedupe d(300, 500);
  uint8_t bda[6]; mkBda(0x11, bda);
  CHECK(d.shouldEmit(bda, 0, 0));
  CHECK(d.size() == 1);
  d.expire(301000);                 // older than one window -> pruned
  CHECK(d.size() == 0);
  CHECK(d.shouldEmit(bda, 0, 301000));  // now a fresh sighting
}

TEST_CASE("BleDedupe: clear() empties the ring") {
  BleDedupe d(300, 500);
  uint8_t bda[6]; mkBda(0x11, bda);
  d.shouldEmit(bda, 0, 0);
  d.clear();
  CHECK(d.size() == 0);
  CHECK(d.shouldEmit(bda, 0, 0));
}
