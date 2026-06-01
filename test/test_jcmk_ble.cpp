// Host-side tests for the JCMK type-6 BLE mesh frame (JcmkBle.h).
// Pins the 212-byte wire size (legacy-Core compatibility) and the build->parse
// round-trip so the Node sender and Core receiver can never disagree.
#include "doctest.h"
#include "JcmkBle.h"

TEST_CASE("jcmk_ble_obs_msg_t is exactly 212 bytes") {
  CHECK(sizeof(jcmk_ble_obs_msg_t) == 212);
}

TEST_CASE("jcmkBleBuild sets header fields") {
  BleObservation o = {};
  std::strcpy(o.addr, "AA:BB:CC:DD:EE:FF");
  o.addrType = 1; o.channel = 38; o.rssi = -70; o.mfgrId = 76;

  jcmk_ble_obs_msg_t m;
  jcmkBleBuild(m, o, /*counter=*/42);

  CHECK(std::memcmp(m.magic, "ENOW", 4) == 0);
  CHECK(m.type == JCMK_MSG_BLE_OBS);
  CHECK(m.type == 6);
  CHECK(m.counter == 42u);
  CHECK(m.len == JCMK_BLE_ACTIVE_LEN);
  // BDA packed big-endian (display order).
  CHECK(m.bda[0] == 0xAA);
  CHECK(m.bda[5] == 0xFF);
}

TEST_CASE("build -> parse round-trip preserves every field") {
  BleObservation in = {};
  std::strcpy(in.addr, "12:34:56:78:9A:BC");
  in.addrType = 2;
  in.channel  = 39;
  in.rssi     = -91;
  in.mfgrId   = 6;
  in.observedAtMs = 123456;
  std::strcpy(in.name, "Pixel Buds");
  std::strcpy(in.serviceUuids, "FE9F;180F");

  jcmk_ble_obs_msg_t m;
  jcmkBleBuild(m, in, 7);

  BleObservation out = {};
  jcmkBleParse(m, out);

  CHECK(std::string(out.addr) == "12:34:56:78:9A:BC");
  CHECK(out.addrType == in.addrType);
  CHECK(out.channel  == in.channel);
  CHECK(out.rssi     == in.rssi);
  CHECK(out.mfgrId   == in.mfgrId);
  CHECK(out.observedAtMs == in.observedAtMs);
  CHECK(std::string(out.name) == "Pixel Buds");
  CHECK(std::string(out.serviceUuids) == "FE9F;180F");
}

TEST_CASE("round-trip handles empty name / service uuids / zero mfgr") {
  BleObservation in = {};
  std::strcpy(in.addr, "00:11:22:33:44:55");
  in.addrType = 0; in.channel = 37; in.rssi = -40;

  jcmk_ble_obs_msg_t m;
  jcmkBleBuild(m, in, 1);
  BleObservation out = {};
  jcmkBleParse(m, out);

  CHECK(std::string(out.addr) == "00:11:22:33:44:55");
  CHECK(std::string(out.name) == "");
  CHECK(std::string(out.serviceUuids) == "");
  CHECK(out.mfgrId == 0);
}
