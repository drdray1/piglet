// Portable channel-band classification — verbatim copy of the inline
// classification in Scanner.cpp's processScanResults(). Lives in a header so
// host-side tests can exercise it without pulling in WiFi.h or Arduino.h.
//
// If you change the band ranges, change both call sites at once.
#pragma once

struct ChannelBands {
  bool is2g;
  bool is5g;
};

inline ChannelBands classifyChannel(int ch) {
  bool chUnknown = (ch == 0);
  bool is2g = (ch >= 1 && ch <= 14) || chUnknown;
  bool is5g = (ch >= 32 && ch <= 177);
  return {is2g, is5g};
}
