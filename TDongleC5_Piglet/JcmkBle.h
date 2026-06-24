// JCMK ESP-Now message type 6 — a single BLE observation forwarded from a mesh
// Node to the Core. Wire-format frozen: 212 bytes total (matches jcmk_text_msg_t)
// so legacy/third-party Cores (Biscuit Pro, JCMK C5) that gate on packet length
// accept the frame, fail their type switch, and drop it cleanly — no log
// corruption. Append-only via _pad if fields are ever added; never reorder.
//
// The pack/unpack helpers are pure (no ESP-Now/Arduino) so the round-trip is
// host-testable; MeshNode.cpp uses them on both ends.
#pragma once

#include <cstdint>
#include <cstring>

#include "BleScanner.h"  // BleObservation (POD, no NimBLE on host)
#include "BleCsv.h"      // formatBda / parseBda

// Matches the value added to MeshNode.cpp's JcmkMsgType enum.
static constexpr uint8_t JCMK_MSG_BLE_OBS = 6;

typedef struct __attribute__((packed)) {
  char     magic[4];          // 'E','N','O','W'
  uint8_t  type;              // JCMK_MSG_BLE_OBS = 6
  uint32_t counter;           // monotonic per-sender
  uint16_t len;               // active prefix length (informational)

  uint8_t  bda[6];            // big-endian, matches "AA:BB:.." display order
  uint8_t  addrType;          // 0=public 1=random 2=RPA 3=NRPA
  uint8_t  channel;           // 37/38/39
  int8_t   rssi;              // dBm
  uint16_t mfgrId;            // AD 0xFF company id, LE-decoded
  uint32_t observedAtMsRel;   // node's observedAtMs (informational; Core re-stamps)
  char     name[33];          // null-terminated, truncated
  char     serviceUuids[64];  // ';'-joined 16-bit UUIDs, null-terminated

  uint8_t  _pad[89];          // pad to 212; legacy Cores ignore past the prefix
} jcmk_ble_obs_msg_t;

static_assert(sizeof(jcmk_ble_obs_msg_t) == 212, "JCMK BLE obs must be 212 bytes");

// Length of the meaningful prefix (everything before _pad).
static constexpr uint16_t JCMK_BLE_ACTIVE_LEN =
    (uint16_t)(sizeof(jcmk_ble_obs_msg_t) - sizeof(((jcmk_ble_obs_msg_t*)0)->_pad));

// Pack an observation into a wire frame. Pure: caller supplies the counter.
inline void jcmkBleBuild(jcmk_ble_obs_msg_t& m, const BleObservation& o,
                         uint32_t counter) {
  std::memset(&m, 0, sizeof(m));
  m.magic[0] = 'E'; m.magic[1] = 'N'; m.magic[2] = 'O'; m.magic[3] = 'W';
  m.type    = JCMK_MSG_BLE_OBS;
  m.counter = counter;
  m.len     = JCMK_BLE_ACTIVE_LEN;
  parseBda(o.addr, m.bda);
  m.addrType        = o.addrType;
  m.channel         = o.channel;
  m.rssi            = o.rssi;
  m.mfgrId          = o.mfgrId;
  m.observedAtMsRel = o.observedAtMs;
  std::strncpy(m.name,         o.name,         sizeof(m.name) - 1);
  std::strncpy(m.serviceUuids, o.serviceUuids, sizeof(m.serviceUuids) - 1);
}

// Unpack a wire frame back into an observation (Core side). The Core stamps its
// own GPS + timestamp when logging, so observedAtMsRel is carried but advisory.
inline void jcmkBleParse(const jcmk_ble_obs_msg_t& m, BleObservation& o) {
  std::memset(&o, 0, sizeof(o));
  formatBda(m.bda, o.addr);
  o.addrType     = m.addrType;
  o.channel      = m.channel;
  o.rssi         = m.rssi;
  o.mfgrId       = m.mfgrId;
  o.observedAtMs = m.observedAtMsRel;
  std::strncpy(o.name,         m.name,         sizeof(o.name) - 1);
  std::strncpy(o.serviceUuids, m.serviceUuids, sizeof(o.serviceUuids) - 1);
}
