# BLE Wardriving — Phase 2 implementation plan

Status: **proposed, awaiting review** (no code written yet)
Branch: `feature/bluetooth-wardriving`
Builds on: Phase 1 (`BleCsv.h`, `appendBleRow`, config keys) — already committed.
Source of truth for design: `piglet_bluetooth_implementation.md` §3, §5–§10, §17.

Decisions locked with the maintainer:
- **BLE only** (no Bluetooth Classic), **passive observer** scan.
- **Config-toggled** via `bleEnabled` (Phase 1 keys already in place).
- **Runtime capability**: all current Piglet SoCs (S3/C5/C6) have BLE; gate at
  compile time with `PIGLET_HAS_BLE` for any future BT-less target.
- **Dependency packaging: Arduino IDE** — document the NimBLE-Arduino library in
  the README/LIBRARIES, no PlatformIO migration in this fork yet.

---

## Scope of Phase 2

The scanning engine + loop integration. After Phase 2, setting `bleEnabled=true`
and rebooting produces `Type=BLE` rows in the CSV alongside Wi-Fi. OLED/WebUI/mesh
surfacing is **Phase 3+** (out of scope here).

Co-scheduled in the existing main loop (doc §8 "Option A") — **no FreeRTOS task
split**. The NimBLE host runs on its own internal task; our loop only starts/stops
windows and drains a queue.

---

## New / changed files

### 1. `BleDedupe.h` (new) — pure, host-testable
Extract the dedupe ring from the doc's file-static map into a testable class so
the doc's §15 dedupe tests can run host-side (no NimBLE needed).

```cpp
class BleDedupe {
public:
  BleDedupe(uint32_t windowSec, size_t maxSize);
  // Returns true if this (bda,addrType) should be logged now; refreshes the
  // last-seen time and suppresses if seen within windowSec. Evicts oldest when
  // over maxSize. `nowMs` is injected so tests can advance a virtual clock.
  bool shouldEmit(const uint8_t bda[6], uint8_t addrType, uint32_t nowMs);
  void expire(uint32_t nowMs);          // drop entries older than the window
  size_t size() const;
  void clear();
private:
  // key = 6 BDA bytes | addrType<<48  -> last-seen millis
  std::unordered_map<uint64_t, uint32_t> ring_;
  // insertion order for O(1)-ish oldest eviction
  std::deque<uint64_t> order_;
  uint32_t windowMs_; size_t maxSize_;
};
```
Keying matches the doc: `makeKey(bda, addrType)`. Different addrType ⇒ different
device (RPA rotation is intentionally treated as distinct).

**Host tests** (`test/test_ble_dedupe.cpp`): first-sighting emits, dupe
suppressed, expiry after window, different addrType is a different key, bounded
size evicts oldest. ~6 cases.

### 2. `BleScanner.h` / `BleScanner.cpp` (new)
Mirrors `Scanner.{h,cpp}` idioms. Wraps NimBLE; owns a `BleDedupe` and the
pending-results FIFO. Public API (from doc §6, unchanged):
`begin() / startScan() / stopScan() / isScanning() / consumeResults() / tick() /
lifetimeUniqueCount() / dedupeWindowSize() / ready()`.

All NimBLE includes confined to the `.cpp`. Header forward-declares
`NimBLEAdvertisedDevice` and defines the POD `BleObservation` (doc §6).
Entire body wrapped in `#if PIGLET_HAS_BLE`.

Key implementation points (doc §3, §5, §6, §17):
- **Passive only**: `scan->setActiveScan(false)` — never transmit SCAN_REQ.
  Comment why (coex: two TX on one radio stalls).
- **Observer-only NimBLE**: roles trimmed (see build flags below).
- **Callback discipline**: `onResult()` runs in the NimBLE host task — it may
  ONLY copy bytes into the FIFO/dedupe map. No SD / WiFi / HTTP / `Serial.printf`.
- **Coex policy**: `esp_coex_preference_set(ESP_COEX_PREFER_BT)` on `startScan()`,
  back to `ESP_COEX_PREFER_BALANCE` on stop/window-end (doc §3).
- **Channel field**: NimBLE 2.x doesn't reliably expose the advertising channel;
  default `o.channel = 37` (doc §6/§17). `bleChannelToFreq(37)=2402`.
- **Address formatting**: NimBLE gives LE-order bytes — format big-endian
  `AA:BB:..` exactly as doc §6 `snprintf` does. **Pin this in a host test** by
  factoring the byte→string formatting into a pure helper in `BleCsv.h`.
- **mfgrId**: first 2 bytes of AD 0xFF, little-endian → uint16.
- **serviceUuids**: up to 6 × 16-bit UUIDs, `;`-joined.

### 3. `SDUtils.{h,cpp}` (extend)
Add the GPS-snapshot writer from doc §9 (Option A — one fix per window):
```cpp
void writeBleRowsFromObs(const std::vector<BleObservation>& obs);
```
Snapshots `iso8601NowUTC()` + lat/lon/alt/hdop once, applies to all rows in the
batch, calls the Phase 1 `appendBleRow()`. Note: our `appendBleRow` already takes
the raw `uint8_t addrType` (cleaner than the doc's `csvAddrType` String), so the
helper passes `o.addrType` straight through.

### 4. `Piglet.ino` (extend `loop()`)
Insert the BLE scheduler into the existing scan block (~line 604), guarded by
`#if PIGLET_HAS_BLE` + `cfg.bleEnabled` (doc §8):
- Start a window when not scanning and `bleScanInterval` elapsed, but **only if
  no Wi-Fi async scan is in flight** (`WiFi.scanComplete() != WIFI_SCAN_RUNNING`).
- Call `bleScanner.tick()` every loop; drain `consumeResults()` →
  `writeBleRowsFromObs()`.
- **Skip starting a new Wi-Fi sweep while a BLE window is active** (active Wi-Fi
  scan would starve BLE of coex airtime).
- `bleScanner.begin()` called once in `setup()` only when `cfg.bleEnabled` (after
  config load), so a `false` build pays nothing.

### 5. Docs
- `README.md` / new `LIBRARIES.md`: "Install **NimBLE-Arduino 2.1.x** via Arduino
  Library Manager; required only for `bleEnabled=true` builds."
- NimBLE compile-time trim flags (doc §5) — for Arduino IDE these go in a
  `build_opt.h` / documented `platform.local.txt`, or a header `#define` block
  included before `<NimBLEDevice.h>`. **Decision needed** (see open questions).

---

## NimBLE build configuration (observer-only trim, doc §5)

```
CONFIG_BT_NIMBLE_MAX_CONNECTIONS=0
CONFIG_BT_NIMBLE_ROLE_CENTRAL=0
CONFIG_BT_NIMBLE_ROLE_PERIPHERAL=0
CONFIG_BT_NIMBLE_ROLE_BROADCASTER=0
CONFIG_BT_NIMBLE_ROLE_OBSERVER=1
CONFIG_BT_NIMBLE_MAX_BONDS=0
CONFIG_BT_NIMBLE_LOG_LEVEL=1
```
Saves ~20 KB flash vs defaults. In the Arduino IDE these are awkward (no
`build_flags`); fallback is to accept NimBLE defaults for v1 and only document
the trim for advanced users. Flash budget (doc §10) has headroom either way.

---

## Test additions (host-side)

| File | Cases | Needs NimBLE? |
|---|---|---|
| `test_ble_dedupe.cpp` | ~6 (emit/suppress/expiry/addrType/eviction) | No |
| `test_ble_csv.cpp` (extend) | +1 BDA byte→string formatting | No |

`BleScanner` itself and the NimBLE callback path are **integration-only**
(hardware), same documented gap as the Wi-Fi scanner. On-device checklist from
doc §15: enable from cold boot → OLED count >0 in BLE-rich area; CSV has valid
`Type=BLE` rows; WiGLE accepts the upload; Wi-Fi row rate drops ≲16%.

---

## Risks / gotchas to carry (doc §3, §17)

1. **Active Wi-Fi + BLE coexistence**: BLE sees ~30% of adverts while Wi-Fi stays
   in active scan. Accepted for v1; dedupe recovers misses on later passes.
2. **Callback thread safety**: the single biggest footgun. Keep `onResult()` to
   byte-copies only.
3. **Channel not reported**: default 37; don't fabricate 38/39.
4. **RPA churn**: phones rotate random addresses ~every 15 min → same physical
   device logged multiple times. Expected; matches WiGLE behaviour.
5. **Heap on C5/C6** (single-core, tighter RAM): validate `consumeResults()`
   drains faster than adverts arrive; FIFO is bounded by `bleMaxResults`.

---

## Open questions for review

1. **NimBLE trim flags in Arduino IDE** — document-only (accept defaults) for v1,
   or ship a `build_opt.h`? (Leaning document-only; flash budget allows it.)
2. **No-GPS-fix BLE rows** — write with `0,0,0,0` like the Wi-Fi path does today,
   or skip until first fix? (Leaning: match Wi-Fi = write them.)
3. **`bleScanner.begin()` placement** — at boot when `bleEnabled`, vs. lazily on
   first window. (Leaning: boot, so the OLED can show readiness.)

---

## Phase 2 commit breakdown (proposed)

1. `BleDedupe.h` + `test_ble_dedupe.cpp` + BDA-format helper/test (all host-green).
2. `BleScanner.{h,cpp}` behind `PIGLET_HAS_BLE` (compiles; no call sites yet).
3. `writeBleRowsFromObs()` in SDUtils + `loop()` integration + `setup()` begin().
4. README/LIBRARIES NimBLE note + CHANGELOG.

Each step keeps `bleEnabled=false` builds byte-for-byte unchanged in behaviour.
