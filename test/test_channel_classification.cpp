// Host-side regression test for classifyChannel() in Scanner_channels.h.
//
// Documents the existing band ranges AND pins the known ch==0 misclassification
// so a future fix trips this test and prompts the fixer to update it.
#include "doctest.h"
#include "Scanner_channels.h"

TEST_CASE("classifyChannel: 2.4 GHz range 1-14") {
  for (int ch = 1; ch <= 14; ++ch) {
    ChannelBands b = classifyChannel(ch);
    CHECK(b.is2g);
    CHECK_FALSE(b.is5g);
  }
}

TEST_CASE("classifyChannel: 5 GHz range 36-165 (real-world subset)") {
  // The actual 5 GHz channel set is sparse — these are commonly seen.
  const int channels_5g[] = {36, 40, 44, 48, 52, 56, 60, 64,
                             100, 104, 108, 112, 116, 120, 124, 128,
                             132, 136, 140, 144, 149, 153, 157, 161, 165};
  for (int ch : channels_5g) {
    ChannelBands b = classifyChannel(ch);
    CHECK(b.is5g);
    CHECK_FALSE(b.is2g);
  }
}

TEST_CASE("classifyChannel: 5 GHz band edges (32 and 177)") {
  // The code uses 32-177 inclusive — wider than the real 5 GHz channel set.
  // Pin the edges so any tightening of the range trips the test.
  CHECK(classifyChannel(32).is5g);
  CHECK(classifyChannel(177).is5g);
  CHECK_FALSE(classifyChannel(31).is5g);
  CHECK_FALSE(classifyChannel(178).is5g);
}

TEST_CASE("classifyChannel: ch==0 known-bug — misclassified as 2.4 GHz") {
  // Channel 0 does not exist on any band. The current code lumps it in with
  // 2.4 GHz via the chUnknown fallback. When this gets fixed, both asserts
  // here will fire — update the test then.
  ChannelBands b = classifyChannel(0);
  CHECK(b.is2g);          // current (incorrect) behavior
  CHECK_FALSE(b.is5g);
  // What we'd want once fixed:
  //   CHECK_FALSE(b.is2g);
  //   CHECK_FALSE(b.is5g);
}

TEST_CASE("classifyChannel: 6 GHz channel numbers fall through both bands") {
  // The code has no 6 GHz concept — it only classifies by channel number.
  // 6 GHz channels in PSC numbering (1, 5, 9, ...) collide with 2.4 GHz
  // channel numbers, so they get classified as 2.4. Channels above 14 but
  // below 32 (15-31) fall through as neither.
  CHECK(classifyChannel(1).is2g);   // could be 2.4 OR 6 GHz; code can't tell
  CHECK_FALSE(classifyChannel(15).is2g);
  CHECK_FALSE(classifyChannel(15).is5g);
  CHECK_FALSE(classifyChannel(31).is2g);
  CHECK_FALSE(classifyChannel(31).is5g);
}
