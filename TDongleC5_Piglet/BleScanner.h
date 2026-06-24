// Passive BLE observer for wardriving. Mirrors Scanner.{h,cpp} in shape.
//
// NimBLE is confined entirely to BleScanner.cpp; this header pulls in nothing
// from it. The BleObservation POD is defined unconditionally (no NimBLE deps)
// so SDUtils (writeBleRowsFromObs) and MeshNode (node-mode forwarding) can name
// it even in a hypothetical BLE-less build. The BleScanner class itself, and the
// `bleScanner` instance, only exist when PIGLET_HAS_BLE.
#pragma once

#include <Arduino.h>
#include <cstdint>
#include <vector>

// BLE is present on every current Piglet SoC (S3/C5/C6). Gate at compile time so
// a future BT-less target drops all BLE code without #ifdef sprinkling.
#if defined(CONFIG_BT_ENABLED) && CONFIG_BT_ENABLED
  #define PIGLET_HAS_BLE 1
#else
  #define PIGLET_HAS_BLE 0
#endif

// One observed BLE advertisement, copied out of the NimBLE callback context.
// Plain POD: trivially copyable for the pending FIFO and the JCMK mesh struct.
struct BleObservation {
  char     addr[18];         // "AA:BB:CC:DD:EE:FF" (big-endian display order)
  uint8_t  addrType;         // 0=public 1=random 2=RPA 3=NRPA (NimBLE getType())
  char     name[33];         // Complete/Shortened Local Name, truncated
  uint16_t mfgrId;           // first 2 bytes of AD 0xFF, little-endian; 0 if none
  char     serviceUuids[64]; // ';'-joined 16-bit UUIDs; "" if none
  uint8_t  channel;          // primary adv channel 37/38/39 (default 37)
  int8_t   rssi;             // dBm
  uint32_t observedAtMs;     // millis() at observation
};

#if PIGLET_HAS_BLE

class BleScanner {
public:
  // Initialise the NimBLE host stack + dedupe/FIFO. Idempotent; call once when
  // cfg.bleEnabled (boot, after config load). Cheap to call again after stop().
  void begin();

  // Start a passive scan window of cfg.bleScanDuration seconds. Non-blocking:
  // results arrive via the NimBLE callback into the dedupe ring + pending FIFO.
  void startScan();

  // Stop an in-progress window early (e.g. a Wi-Fi sweep / ESP-Now send is due).
  void stopScan();

  bool isScanning() const { return scanRunning_; }

  // Move all queued unique observations into `out`. Keeps dedupe-window state.
  // Returns the number appended.
  size_t consumeResults(std::vector<BleObservation>& out);

  // Per-loop housekeeping: detect window end, prune expired dedupe entries.
  void tick();

  // Total unique devices since boot. Bumped from the NimBLE callback task, so it
  // (and dedupeWindowSize) read shared state under the scanner's lock — defined
  // in the .cpp, not inlined here.
  uint32_t lifetimeUniqueCount() const;
  size_t   dedupeWindowSize() const;
  bool     ready() const { return initialised_; }

private:
  // Touched only by the loop task (begin/startScan/stopScan/tick), never the
  // NimBLE callback — so these need no lock.
  bool     initialised_ = false;
  bool     scanRunning_ = false;
  uint32_t scanEndsAtMs_ = 0;
};

extern BleScanner bleScanner;

#endif  // PIGLET_HAS_BLE
