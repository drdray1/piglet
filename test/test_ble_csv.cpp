// Host-side tests for BleCsv.h — the pure WiGLE-1.6 BLE row formatter.
// Pins the channel->frequency map, the address-type AuthMode strings, CSV
// quote-escaping, and the exact column layout so the on-disk BLE format
// (written via SDUtils.cpp::appendBleRow) can never silently drift.
#include "doctest.h"
#include "BleCsv.h"

TEST_CASE("bleChannelToFreq: the three BLE advertising channels") {
  CHECK(bleChannelToFreq(37) == 2402u);
  CHECK(bleChannelToFreq(38) == 2426u);
  CHECK(bleChannelToFreq(39) == 2480u);
}

TEST_CASE("bleChannelToFreq: non-advertising channels yield 0") {
  CHECK(bleChannelToFreq(0) == 0u);
  CHECK(bleChannelToFreq(6) == 0u);    // a Wi-Fi channel, not BLE
  CHECK(bleChannelToFreq(40) == 0u);
}

TEST_CASE("formatBda: 6 big-endian bytes -> AA:BB:CC:DD:EE:FF") {
  const uint8_t bda[6] = {0xAA, 0xBB, 0xCC, 0x44, 0x55, 0x66};
  char out[18];
  formatBda(bda, out);
  CHECK(std::string(out) == "AA:BB:CC:44:55:66");
}

TEST_CASE("formatBda: zero-pads single-digit octets") {
  const uint8_t bda[6] = {0x01, 0x02, 0x00, 0x0F, 0xA0, 0x00};
  char out[18];
  formatBda(bda, out);
  CHECK(std::string(out) == "01:02:00:0F:A0:00");
}

TEST_CASE("normalizeBleUuid16: NimBLE 0x-prefixed short forms -> 4-hex upper") {
  char out[5];
  // The exact bug seen on hardware: "0xfe9f" was truncated to "0xfe".
  CHECK(normalizeBleUuid16("0xfe9f", out)); CHECK(std::string(out) == "FE9F");
  CHECK(normalizeBleUuid16("0x180f", out)); CHECK(std::string(out) == "180F");
  CHECK(normalizeBleUuid16("0xfe", out));   CHECK(std::string(out) == "00FE");
  CHECK(normalizeBleUuid16("0x18", out));   CHECK(std::string(out) == "0018");
}

TEST_CASE("normalizeBleUuid16: bare hex and uppercase prefix") {
  char out[5];
  CHECK(normalizeBleUuid16("57b4", out)); CHECK(std::string(out) == "57B4");
  CHECK(normalizeBleUuid16("0X6BA1", out)); CHECK(std::string(out) == "6BA1");
}

TEST_CASE("normalizeBleUuid16: 128-bit base alias yields its 16-bit value") {
  char out[5];
  CHECK(normalizeBleUuid16("0000fe9f-0000-1000-8000-00805f9b34fb", out));
  CHECK(std::string(out) == "FE9F");
}

TEST_CASE("normalizeBleUuid16: genuine 128-bit custom UUID is skipped") {
  char out[5];
  CHECK_FALSE(normalizeBleUuid16("12345678-1234-1234-1234-1234567890ab", out));
  CHECK_FALSE(normalizeBleUuid16("", out));
  CHECK_FALSE(normalizeBleUuid16("0x", out));       // no digits
}

TEST_CASE("bleAddrTypeToString: NimBLE address-type codes") {
  CHECK(std::string(bleAddrTypeToString(0)) == "[LE Public]");
  CHECK(std::string(bleAddrTypeToString(1)) == "[LE Random]");
  CHECK(std::string(bleAddrTypeToString(2)) == "[LE Resolvable]");
  CHECK(std::string(bleAddrTypeToString(3)) == "[LE NonResolvable]");
  CHECK(std::string(bleAddrTypeToString(9)) == "[LE Unknown]");
}

TEST_CASE("bleCsvEscapeQuotes: doubles embedded quotes only") {
  CHECK(bleCsvEscapeQuotes("") == "");
  CHECK(bleCsvEscapeQuotes("plain") == "plain");
  CHECK(bleCsvEscapeQuotes("a\"b") == "a\"\"b");
  CHECK(bleCsvEscapeQuotes("\"\"") == "\"\"\"\"");
}

TEST_CASE("formatBleRow: AirTag-style row matches the documented layout") {
  // From piglet_bluetooth_implementation.md §4 (with the device's actual
  // space-separated timestamp rather than the doc's aspirational ISO form).
  std::string row = formatBleRow(
      "AA:BB:CC:44:55:66", /*name*/ "", /*addrType*/ 1,
      "2026-05-28 14:23:01", /*channel*/ 38, /*rssi*/ -74,
      40.7128, -74.006, 15.2, 1.4, /*serviceUuids*/ "FE9F", /*mfgrId*/ 76);
  CHECK(row ==
        "AA:BB:CC:44:55:66,\"\",[LE Random],2026-05-28 14:23:01,38,2426,-74,"
        "40.712800,-74.006000,15.2,1.4,FE9F,76,BLE");
}

TEST_CASE("formatBleRow: named device with quotes and no mfgr/service data") {
  std::string row = formatBleRow(
      "11:22:33:44:55:66", /*name*/ "My \"Speaker\"", /*addrType*/ 0,
      "2026-05-28 14:23:01", /*channel*/ 37, /*rssi*/ -55,
      0.0, 0.0, 0.0, 0.0, /*serviceUuids*/ "", /*mfgrId*/ 0);
  CHECK(row ==
        "11:22:33:44:55:66,\"My \"\"Speaker\"\"\",[LE Public],"
        "2026-05-28 14:23:01,37,2402,-55,0.000000,0.000000,0.0,0.0,,0,BLE");
}

TEST_CASE("formatBleRow: column count matches the 14-field WiGLE header") {
  std::string row = formatBleRow("AA:BB:CC:DD:EE:FF", "x", 2,
                                 "2026-05-28 14:23:01", 39, -60,
                                 1.0, 2.0, 3.0, 4.0, "180F;FE9F", 6);
  size_t commas = 0;
  for (char c : row) if (c == ',') commas++;
  CHECK(commas == 13);  // 14 columns -> 13 separators
}
