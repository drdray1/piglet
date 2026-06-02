# Piglet — Bluetooth/BLE wardriving implementation

*Implementation-ready engineering doc. Audience: the engineer writing the code.*

- **Firmware version at scope:** `v2.52`
- **Companion doc:** `piglet_improvements_deep_dive.md` (BLE was scoped there as a design overview; this doc is the order-of-operations to actually build it)
- **Branch convention:** `feature/ble/*`
- **Pre-requisites from the deep-dive that this doc assumes will land first or in parallel:** improvement **7.1** (PlatformIO + pinned env — needed to add NimBLE as a `lib_deps` entry), improvement **1.1** (extract `piglet-core` — the CSV writer is parameterised on row type), improvement **9.3** (ArduinoJson v7 — used for new `/status.json` BLE fields). Improvement **2.1** (FreeRTOS task split) is optional for v1 but unlocks the better interleave pattern in §8.

---

## Table of contents

1. [Goal and non-goals](#1-goal-and-non-goals)
2. [Hardware support matrix](#2-hardware-support-matrix)
3. [Radio coexistence reality](#3-radio-coexistence-reality)
4. [WiGLE BLE CSV format](#4-wigle-ble-csv-format)
5. [Library choice — NimBLE-Arduino vs Arduino BLE](#5-library-choice--nimble-arduino-vs-arduino-ble)
6. [New module structure: `BleScanner.{h,cpp}`](#6-new-module-structure-blescannerhcpp)
7. [Dedupe window strategy](#7-dedupe-window-strategy)
8. [Interleave with the WiFi scanner FSM](#8-interleave-with-the-wifi-scanner-fsm)
9. [GPS coupling](#9-gps-coupling)
10. [Memory budget](#10-memory-budget)
11. [New config keys](#11-new-config-keys)
12. [WebUI changes](#12-webui-changes)
13. [JCMK mesh protocol extension](#13-jcmk-mesh-protocol-extension)
14. [OLED display updates](#14-oled-display-updates)
15. [Test plan](#15-test-plan)
16. [Phased delivery](#16-phased-delivery)
17. [Risks and gotchas](#17-risks-and-gotchas)
18. [Acceptance](#18-acceptance)

---

# 1. Goal and non-goals

## Goal

Add BLE wardriving as a first-class feature alongside the existing Wi-Fi wardriving: passive scan for BLE advertisements, dedupe within a configurable window, GPS-stamp each unique observation, write WiGLE 1.6 CSV rows with `Type=BLE`, upload to WiGLE and WDGoWars using the existing uploader, forward over JCMK ESP-Now mesh as a new message type, and surface counts/status on the OLED and WebUI.

Concretely, after this lands:

- A Piglet driving past 30 Wi-Fi APs and 50 BLE devices in 60 s produces a CSV with ~80 rows mixing `Type=WIFI` and `Type=BLE` lines, all with the same `WigleWifi-1.6` header.
- WiGLE accepts the upload without errors and credits the BLE observations to the user's account.
- The OLED Networks page shows 2.4G / 5G / BLE counts.
- A BLE-capable Mesh Node forwards BLE observations to a BLE-capable Core; legacy Cores drop the new packet type cleanly.

## Non-goals (explicit)

Out of scope for this feature; track separately if anyone asks:

- **Bluetooth Classic (BR/EDR) scanning.** Most Piglet target SoCs are BLE-only (ESP32-C5/C6 do not implement Classic at all — see §2). Even on ESP32-S3 which does support Classic, the scanning model is fundamentally different (inquiry vs. passive observation), and modern wardriving telemetry is dominated by BLE anyway.
- **BLE GATT enumeration.** No connecting to peers, no service/characteristic discovery. Observation-only.
- **BLE pairing or attacks.** No JustWorks pairing attempts, no sniffing of connection events, no spoofing. This is a passive observer, not a sniffer or attacker.
- **BLE 5 long-range / coded-PHY scanning.** The default 1 Mbps PHY on the primary advertising channels (37/38/39) is the v1 target. Coded-PHY support is a follow-up if anyone has a use case.
- **BLE Mesh / Bluetooth Mesh sniffing.** Distinct protocol; tracked separately.
- **Decoding of vendor-specific manufacturer data.** We log the raw `MfgrId` (16-bit company identifier from AD type `0xFF`) and the first N bytes of manufacturer data; we don't decode Apple Continuity, Tile, AirTag, Eddystone, etc. WiGLE's server-side processing handles classification.

---

# 2. Hardware support matrix

Every Piglet target SoC has BLE, but the radios, antennas, and coexistence behaviour differ. The matrix below is the source of truth for which boards get BLE in which PR phase (§16).

| Board | SoC | BLE version | Antenna | Coex notes | BLE in v1? |
|---|---|---|---|---|---|
| **XIAO ESP32-S3** | ESP32-S3 | BLE 5.0, no Classic on XIAO build (Classic exists on stock S3 but the Arduino-ESP32 BLE-only config is what Piglet uses) | Internal PCB or external u.FL — shared 2.4 GHz with Wi-Fi via on-chip RF switch | Dual-core Xtensa LX7. IDF coex arbiter is mature on S3. With PSRAM enabled the NimBLE host stack can live in PSRAM, freeing internal DRAM for mbedtls during TLS uploads. Best v1 target. | ✅ |
| **XIAO ESP32-C5** | ESP32-C5 | BLE 5.0 | Shared 2.4 GHz front-end with Wi-Fi. C5 is dual-band Wi-Fi (2.4 + 5 GHz) — the BLE radio shares only the 2.4 GHz path. | Single-core RISC-V. Coex tighter than S3 because the radio juggles 2.4 GHz Wi-Fi sweep, 5 GHz Wi-Fi sweep, and BLE on one PHY. Use `ESP_COEX_PREFER_BT` only during BLE scan windows; leave at `BALANCE` otherwise. | ✅ |
| **XIAO ESP32-C6** | ESP32-C6 | BLE 5.0 | Shared 2.4 GHz front-end with Wi-Fi 6 and 802.15.4 (which Piglet ignores). | Single-core RISC-V. Coex arbiter mature in IDF 5.x. No 5 GHz, so contention pattern is simpler than C5 — just Wi-Fi vs BLE on 2.4 GHz. | ✅ |
| **T-Dongle C5** | ESP32-C5 | BLE 5.0 | Internal ceramic antenna shared 2.4 GHz; same RF path as XIAO C5. | Identical SoC notes to XIAO C5. T-Dongle uses GPIO11/12 for GPS UART which doesn't conflict with BT. Display SPI on GPIO2/6/7 runs at 40 MHz — verify no spurious EMI bleed into the 2.4 GHz front-end on the T-Dongle's compact PCB; if BLE RSSI looks unusually noisy, drop SPI to 27 MHz (the fallback already coded into `TDongleC5_Piglet.ino` LovyanGFX config). | ✅ |
| **Waveshare ESP32-C6 1.47"** | ESP32-C6 | BLE 5.0 | Internal antenna, same shared 2.4 GHz path as XIAO C6. | Same C6 coex notes. Larger PCB than XIAO so less RF cross-talk. The Waveshare board's TFT is on SPI2_HOST at GPIO6/7 — same caveat as T-Dongle re: SPI clock and 2.4 GHz noise, but in practice this board has been the cleanest in field reports. | ✅ |
| **PigletNode** (XIAO ESP32-C5) | ESP32-C5 | BLE 5.0 | Same as XIAO C5. | The standalone mesh node. Adding BLE here means PigletNode forwards BLE observations over JCMK to whatever Core it's paired with. This requires the JCMK BLE extension from §13. PigletNode is single-purpose and has the heap headroom — it's actually the cleanest target for v1 BLE because there's no OLED/SD/upload competing for radio time. | ✅ (in PR4, see §16) |

**Bottom line:** all six targets are BLE-capable. The ESP32-S3 is the most forgiving (dual-core, mature coex, PSRAM); the ESP32-C5 in T-Dongle form is the tightest (single-core, three radio modes, compact PCB). Develop on the XIAO S3, validate on the C5 before tagging.

---

# 3. Radio coexistence reality

This is the meaty section. Get this wrong and Wi-Fi scan RSSI will drop, BLE will lose 30%+ of adverts, or both. The ESP-IDF coexistence layer is the substrate; what we control is the policy and the timing.

## ESP-IDF coexistence scheduler — how it interleaves

The ESP32 family has one 2.4 GHz radio. Wi-Fi (PHY) and BT/BLE (PHY) cannot transmit or receive at the same instant — they share the front-end. The IDF schedules them via the `esp_coex` arbiter which lives below both stacks and decides "the next 10 ms belongs to Wi-Fi" or "the next 5 ms belongs to BLE."

For piglet's specific workload (scanning, not connecting), three modes matter:

1. **Wi-Fi active scan (`scanType=0`).** Today (`Scanner.cpp:115` uses `scanNetworks(async, show_hidden, passive=false, dwell)`) the radio transmits Probe Requests on each channel for the `dwell` window (100 ms aggressive, 200 ms powersaving). Active scan is *transmitting*, which holds the radio exclusively — the coex arbiter cannot interleave BLE scan windows inside it.
2. **Wi-Fi passive scan (`scanType=1`).** Radio only listens for beacons. Coex arbiter can slice in BLE windows.
3. **BLE passive scan.** Radio listens on one of advertising channels 37, 38, 39 (NimBLE rotates through them automatically based on `setInterval` / `setWindow`). Listen-only — coex-friendly.

**Practical rule for piglet:** if you stay on active Wi-Fi scan and add BLE passive scan, BLE will see ~30% of advertisements. If you switch Wi-Fi to passive scan + add BLE passive scan, you'll catch ~85% of BLE adverts but you'll miss APs that don't beacon (which is fine — wardriving still favours beaconing APs). For v1, keep Wi-Fi active and accept the BLE duty-cycle hit; the dedupe window (§7) means missed adverts of the same device are recovered on the next pass.

## Why active wifi + active BLE conflicts

Active BLE scan (NimBLE `setActiveScan(true)`) means the device transmits SCAN_REQ packets in response to ADV_IND. Two transmitters on one radio = arbiter rejects the conflict and one side stalls. Piglet has no reason to do active BLE — we're observing, not interrogating — so we always run **passive BLE scan** (`setActiveScan(false)`). Document this in `BleScanner.cpp` so nobody flips it for "more responsiveness."

## Channel hopping during BLE scan windows

When the IDF arbiter gives BLE a window, the Wi-Fi channel hopping pauses. If a Wi-Fi sweep is in progress, it resumes on the same channel after BLE releases the radio. This means:

- A Wi-Fi sweep that overlaps a BLE window takes longer in wall-clock time (by exactly the BLE window duration).
- The Wi-Fi sweep result count is not affected (channels are hit, just with a gap).
- A 1.5 s Wi-Fi sweep + a 500 ms BLE window inserted in the middle → 2.0 s total. The `Scanner.cpp` FSM polls `WiFi.scanComplete()` on the loop, so this just shows up as a slightly longer poll cycle.

## Time-budget math

Concrete numbers for the existing scan modes plus a proposed BLE window:

**Wi-Fi 2.4 GHz scan (aggressive, current default):**

- Channels swept: 1–13 (US default; ch 14 only Japan)
- Dwell per channel: 100 ms
- Sweep duration: ~1.3 s
- Inter-sweep gap (current `gapMs = 1500`): 1.5 s
- **Cycle: ~2.8 s, of which ~1.3 s the radio is active and ~1.5 s idle.**

**Wi-Fi 5 GHz scan (C5 only, aggressive):**

- Channels: UNII-1/2/2e/3 — typically 25 channels in piglet's table (`MeshNode.cpp:60`)
- Dwell: 100 ms
- Sweep duration: ~2.5 s
- Adds ~2.5 s to a combined 2.4+5 GHz cycle when interleaved

**Proposed BLE passive scan window:**

- NimBLE `setInterval(160) / setWindow(100)` (units of 0.625 ms) → scan interval = 100 ms, scan window = 62.5 ms → 62.5% radio duty cycle for BLE
- Run for **`bleScanDuration` seconds** (default 5 s) per cycle
- Inter-BLE-window gap (`bleScanInterval`) seconds (default 30 s) between starts

**Interleaved cycle (option A, recommended for v1):**

```
[ Wi-Fi 2.4 sweep ~1.3s ]  [ idle ~0.2s ]
   ↻ repeat 10 times (≈15s total)            ← normal wardriving flow
                          ↓ every 30s
[ BLE passive scan, 5s, 62.5% duty ]
```

In the 5-second BLE window, the IDF coex arbiter still grants Wi-Fi the channel briefly if a sweep is requested — but in piglet's FSM we deliberately don't request a Wi-Fi sweep during the BLE window (see §8). So those 5 seconds are 100% BLE radio time at 62.5% receive duty (the rest is BLE host-stack processing).

**Effective duty cycle over a minute, option A (v1):**

| Activity | Time per minute | % of minute |
|---|---|---|
| Wi-Fi 2.4 GHz radio active | ~26 s | 43% |
| BLE radio active | ~5 s | 8% (× 62.5% = 5% true receive) |
| Idle (recovery / GPS / SD / WebUI) | ~29 s | 49% |

For aggressive C5 dual-band, the Wi-Fi 5 GHz sweep eats another ~20 s, leaving less idle. Tighten `bleScanInterval` to 45 s on C5 dual-band by default.

## The `esp_coex_preference_set()` knob

IDF exposes:

```cpp
esp_err_t esp_coex_preference_set(esp_coex_prefer_t prefer);
// prefer ∈ { ESP_COEX_PREFER_WIFI, ESP_COEX_PREFER_BT, ESP_COEX_PREFER_BALANCE }
```

Default is `BALANCE` — IDF round-robins fairly. For piglet:

- Wi-Fi sweep in progress and a BLE window has been requested → leave at `BALANCE`. The arbiter will slice BLE listen windows in between Wi-Fi channel hops.
- Long-lived idle BLE listen with no Wi-Fi pressure → no change needed.
- During the dedicated `bleScanDuration` window (option A) → call `esp_coex_preference_set(ESP_COEX_PREFER_BT)` at window start, `BALANCE` at window end. This ensures the arbiter doesn't preempt BLE for a spurious Wi-Fi sweep we forgot to gate.

```cpp
// BleScanner.cpp — beginning of dedicated BLE window
void BleScanner::startScan() {
  esp_coex_preference_set(ESP_COEX_PREFER_BT);
  NimBLEDevice::getScan()->start(cfg.bleScanDuration, /*is_continue=*/false);
  scanRunning_ = true;
  scanEndsAt_  = millis() + cfg.bleScanDuration * 1000;
}

// In tick(), when scan window expires:
void BleScanner::onScanComplete() {
  esp_coex_preference_set(ESP_COEX_PREFER_BALANCE);
  scanRunning_ = false;
}
```

**Risk:** `ESP_COEX_PREFER_BT` for too long degrades the Wi-Fi STA connection that's holding the WebUI's HTTP session. Cap `bleScanDuration` at 10 s in the config validator.

---

# 4. WiGLE BLE CSV format

WiGLE accepts BLE rows in the same `WigleWifi-1.6` CSV format used for Wi-Fi. The Type column distinguishes them. WiGLE accepts both `BLE` and `BT` as Type values — use **`BLE`** for piglet (more specific; BT could imply Classic which we don't scan).

## Column-by-column comparison

The current Wi-Fi writer at `SDUtils.cpp:223-249` produces lines with this shape. BLE writer produces the same column count with different semantics for several fields:

| Column | Wi-Fi value | BLE value |
|---|---|---|
| MAC | BSSID (`AA:BB:CC:DD:EE:FF`) | BLE device address (`AA:BB:CC:DD:EE:FF`) — may be public, static random, or RPA (resolvable private address) |
| SSID | Quoted SSID, `""` escaped to `""""` | Complete Local Name (AD type `0x09`) or Shortened Local Name (`0x08`), quoted. Empty if no name advertised. |
| AuthMode | `OPEN`/`WPA2`/`WPA3`/... | One of `[LE Public]`, `[LE Random]`, `[LE Resolvable]`, `[LE NonResolvable]`. Bracketed to distinguish from Wi-Fi auth modes. |
| FirstSeen | ISO-8601 UTC (`2026-05-28T14:23:00Z`) | Same |
| Channel | Wi-Fi channel 1–14 (2.4G) or 32–177 (5G) | BLE primary advertising channel: **37**, **38**, or **39** |
| Frequency | MHz from channel via `freq = 2407 + ch*5` (2.4G) or `5000 + ch*5` (5G) | BLE: ch 37 → 2402, ch 38 → 2426, ch 39 → 2480 |
| RSSI | dBm (negative integer) | Same |
| CurrentLatitude | GPS lat (6 decimals) | Same |
| CurrentLongitude | GPS lon (6 decimals) | Same |
| AltitudeMeters | GPS alt | Same |
| AccuracyMeters | GPS HDOP | Same |
| RCOIs | Empty | **Semicolon-separated 16-bit service UUIDs** from AD type `0x03` (complete list) or `0x02` (incomplete list). E.g. `FE9F;180F` for Google Find-My + Battery service. |
| MfgrId | `0` | **16-bit Bluetooth SIG company identifier** from AD type `0xFF` (manufacturer-specific data), little-endian. E.g. `0x004C` = Apple, `0x0006` = Microsoft, `0x00E0` = Google. Encoded in CSV as the decimal value of the LE-decoded uint16 (e.g. `76` for Apple, not `4C00`). |
| Type | `WIFI` | `BLE` |

## Concrete example row

A Wi-Fi row today looks like:

```csv
AA:BB:CC:11:22:33,"my-ssid",WPA2,2026-05-28T14:23:00Z,6,2437,-67,40.712800,-74.006000,15.2,1.4,,0,WIFI
```

A BLE row (an AirTag observed on ch 38) would look like:

```csv
AA:BB:CC:44:55:66,"",[LE Random],2026-05-28T14:23:01Z,38,2426,-74,40.712800,-74.006000,15.2,1.4,FE9F,76,BLE
```

Empty SSID is two `""` (quoted empty string). RCOIs `FE9F` is Apple's Find-My service UUID; MfgrId `76` is Apple's company ID.

## `appendBleRow()` signature

Mirror the existing `appendWigleRow` in `SDUtils.cpp`. Pre-1.1 (before piglet-core extraction), this is a new function in `SDUtils.cpp`. Post-1.1, it disappears in favour of the unified `piglet::writeRow(WigleRow&)`.

```cpp
// SDUtils.h — additions
void appendBleRow(const String& bda, const String& name,
                  const String& addrType,   // "[LE Public]" / "[LE Random]" / etc.
                  const String& firstSeen,
                  int channel, int rssi,
                  double lat, double lon, double altM, double accM,
                  const String& serviceUuids,   // semicolon-separated, empty if none
                  uint16_t mfgrId);             // 0 if none
```

Implementation cribs from `appendWigleRow`. Same per-row flush-control logic (25 lines or 2 s, whichever comes first). Frequency derived from channel:

```cpp
// In appendBleRow body:
uint32_t freq = 0;
if      (channel == 37) freq = 2402;
else if (channel == 38) freq = 2426;
else if (channel == 39) freq = 2480;
// else: leave 0; should not happen in practice (NimBLE only reports 37/38/39)
```

**Important:** the WiGLE CSV header (`SDUtils.cpp:202-208`) lists 14 columns. BLE rows have to match exactly. Don't change the header. The `RCOIs,MfgrId,Type` triplet is already in the header — the current Wi-Fi writer just emits `,,0,WIFI` (empty RCOIs, MfgrId=0, Type=WIFI). BLE rows populate all three.

---

# 5. Library choice — NimBLE-Arduino vs Arduino BLE

**Recommendation: NimBLE-Arduino.** Specifically `h2zero/NimBLE-Arduino @ 2.1.x`.

| Aspect | Arduino-ESP32 stock BLE | NimBLE-Arduino |
|---|---|---|
| Flash footprint | ~250 KB (Bluedroid) | ~110 KB (NimBLE) |
| RAM footprint | ~28 KB | ~12 KB |
| BLE 5 features (extended advertising, coded PHY) | Limited | Full |
| Active maintenance | Espressif-tied | Independent, regular releases |
| API ergonomics | Callback-heavy with object lifecycle gotchas | Cleaner callbacks; explicit ownership |
| Multi-connection scaling | Heavy | Light (we don't need this anyway — observation only) |
| Documentation | Sparse | Good; examples included |

The flash and RAM delta alone is decisive on the C5/C6 (single-core, no L2 cache, tight RAM). Bluedroid would force the WebUI HTML PROGMEM shrink (improvement 1.6) as a hard prerequisite; NimBLE leaves enough headroom that you can add BLE without touching the WebUI.

## PlatformIO dependency

In the `[env]` block of `platformio.ini` (which will exist after improvement 7.1):

```ini
[env]
; ... existing deps ...
lib_deps =
  bblanchon/ArduinoJson @ 7.1.0
  mikalhart/TinyGPSPlus @ 1.0.3
  adafruit/Adafruit SSD1306 @ 2.5.10
  adafruit/Adafruit GFX Library @ 1.11.10
  h2zero/NimBLE-Arduino @ ^2.1.0   ; <-- ADD
```

For Arduino IDE users, document in `LIBRARIES.md` (improvement 9.1):

```
NimBLE-Arduino  2.1.0  Required for bleEnabled=true builds
```

## Build-time disable for legacy boards

For boards that don't have BLE (none of piglet's targets fall here today, but the multi-board build matrix may add one in the future), gate at compile time:

```cpp
// BleScanner.h
#if defined(CONFIG_BT_ENABLED) && CONFIG_BT_ENABLED
  #define PIGLET_HAS_BLE 1
#else
  #define PIGLET_HAS_BLE 0
#endif
```

All BLE-touching code wraps in `#if PIGLET_HAS_BLE`. Saves flash on a hypothetical BT-disabled build target without sprinkling `#ifdef ESP32_C5` everywhere.

## NimBLE config

NimBLE has compile-time tuning via PlatformIO `build_flags`. Tuned for piglet's observation-only workload:

```ini
build_flags =
  -DCONFIG_BT_NIMBLE_MAX_CONNECTIONS=0      ; observation only, no GATT connect
  -DCONFIG_BT_NIMBLE_ROLE_CENTRAL=0
  -DCONFIG_BT_NIMBLE_ROLE_PERIPHERAL=0
  -DCONFIG_BT_NIMBLE_ROLE_BROADCASTER=0
  -DCONFIG_BT_NIMBLE_ROLE_OBSERVER=1        ; <-- the only role we need
  -DCONFIG_BT_NIMBLE_MAX_BONDS=0
  -DCONFIG_BT_NIMBLE_TASK_STACK_SIZE=4096
  -DCONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE=4096
  -DCONFIG_BT_NIMBLE_LOG_LEVEL=1            ; warn only
```

This drops another ~20 KB of flash relative to defaults by disabling the connection-oriented machinery.

---

# 6. New module structure: `BleScanner.{h,cpp}`

Mirror the existing `Scanner.{h,cpp}` shape and idioms. Both files live in `Arduino Files/Piglet/`. Forward-declare types in the header; don't pull NimBLE headers in to keep `Globals.h` cheap (improvement 7.2 makes this principle explicit).

## `BleScanner.h`

```cpp
#pragma once
#include <Arduino.h>
#include <vector>

#if defined(CONFIG_BT_ENABLED) && CONFIG_BT_ENABLED
  #define PIGLET_HAS_BLE 1
#else
  #define PIGLET_HAS_BLE 0
#endif

// Forward decl — actual NimBLE types live in BleScanner.cpp.
class NimBLEAdvertisedDevice;

// One observed BLE advertisement, copied out of the NimBLE callback context.
struct BleObservation {
  // Address — 6 bytes BDA, formatted "AA:BB:CC:DD:EE:FF"
  char addr[18];

  // Address type: see NimBLEAddress::getType()
  //   0 = PUBLIC, 1 = RANDOM_STATIC, 2 = RPA, 3 = NRPA
  // Mapped to CSV string by csvAddrType() in BleScanner.cpp
  uint8_t addrType;

  // Complete Local Name or Shortened Local Name. Truncated at 32 bytes
  // (WiGLE limit; longer adverts are rare).
  char name[33];

  // First two bytes of manufacturer data (AD 0xFF), little-endian -> uint16.
  // 0 if no manufacturer data.
  uint16_t mfgrId;

  // Semicolon-separated 16-bit service UUIDs from AD 0x02/0x03.
  // Max 6 UUIDs → "FE9F;180F;..." → 41 chars + null.
  char serviceUuids[64];

  // Primary advertising channel — 37, 38, or 39.
  // Note: NimBLE does not always report this in v2.x; if unavailable, set to 37.
  uint8_t channel;

  // RSSI in dBm (negative).
  int8_t rssi;

  // millis() when observed.
  uint32_t observedAtMs;
};

#if PIGLET_HAS_BLE

class BleScanner {
public:
  // Initialise NimBLE host stack. Call once at boot (or first enable).
  // Idempotent — safe to call again after a stop().
  void begin();

  // Begin a scan window of duration `cfg.bleScanDuration` seconds.
  // Non-blocking; results delivered via internal callback into the
  // dedupe ring. Use isScanning() to check, consumeResults() to drain.
  void startScan();

  // Returns true if a scan window is currently active.
  bool isScanning() const { return scanRunning_; }

  // Stop an in-progress scan early (e.g. wifi sweep about to start).
  void stopScan();

  // Drain the dedupe ring of unique observations since last call.
  // Moves entries out of the ring; the ring keeps its dedupe-window state
  // (so a device seen 200 s ago still suppresses a duplicate now).
  // Returns the number of new observations appended to `out`.
  size_t consumeResults(std::vector<BleObservation>& out);

  // Total unique BLE devices observed since boot (after dedupe).
  uint32_t lifetimeUniqueCount() const { return lifetimeUnique_; }

  // For OLED status / /status.json: current dedupe window occupancy.
  size_t dedupeWindowSize() const;

  // Called periodically from loop() to:
  //   - expire dedupe entries older than cfg.bleDedupeWindow
  //   - check if scan window ended
  //   - emit follow-up state machine actions
  void tick();

  // True if the host stack is initialised and ready.
  bool ready() const { return initialised_; }

private:
  bool initialised_ = false;
  bool scanRunning_ = false;
  uint32_t scanEndsAtMs_ = 0;
  uint32_t lifetimeUnique_ = 0;

  // Dedupe + pending-result rings live in BleScanner.cpp
  // (file-static to keep this header free of NimBLE / hash-map deps).
};

extern BleScanner bleScanner;

#endif  // PIGLET_HAS_BLE
```

## `BleScanner.cpp` skeleton

```cpp
#include "BleScanner.h"
#include "Globals.h"
#include "Config.h"

#if PIGLET_HAS_BLE

#include <NimBLEDevice.h>
#include <unordered_map>
#include <deque>
#include "esp_coexist.h"

// ----- Globals -----
BleScanner bleScanner;

// ----- Internal state -----
namespace {

// Dedupe key: BDA as uint64 (6 bytes packed) | (addrType << 48)
// 8 bytes per key, fits in a 64-bit register, fast hash.
using BleKey = uint64_t;

// Dedupe ring: key -> last-seen millis.
// Expired entries (older than cfg.bleDedupeWindow) are pruned in tick().
std::unordered_map<BleKey, uint32_t> dedupeRing;

// FIFO of pending observations to be drained by consumeResults().
// Bounded by cfg.bleMaxResults to prevent unbounded growth if the
// SD writer fails to drain fast enough.
std::deque<BleObservation> pendingResults;

inline BleKey makeKey(const uint8_t bda[6], uint8_t addrType) {
  BleKey k = 0;
  for (int i = 0; i < 6; i++) k |= ((BleKey)bda[i]) << (i * 8);
  k |= ((BleKey)addrType) << 48;
  return k;
}

const char* csvAddrType(uint8_t t) {
  switch (t) {
    case 0: return "[LE Public]";
    case 1: return "[LE Random]";
    case 2: return "[LE Resolvable]";
    case 3: return "[LE NonResolvable]";
    default: return "[LE Unknown]";
  }
}

// ----- NimBLE callback -----
class Observer : public NimBLEScanCallbacks {
public:
  void onResult(NimBLEAdvertisedDevice* dev) override {
    // RUNS IN NIMBLE HOST TASK — keep short, no SD/WiFi/HTTP/Serial.printf.
    if (!dev) return;

    auto addr = dev->getAddress();
    uint8_t bda[6];
    memcpy(bda, addr.getNative(), 6);
    uint8_t addrType = addr.getType();
    BleKey key = makeKey(bda, addrType);

    uint32_t now = millis();
    auto it = dedupeRing.find(key);
    if (it != dedupeRing.end()) {
      it->second = now;  // refresh; suppress
      return;
    }
    dedupeRing[key] = now;
    bleScanner_lifetimeBump();

    if (pendingResults.size() >= (size_t)cfg.bleMaxResults) {
      pendingResults.pop_front();  // drop oldest to bound memory
    }

    BleObservation o = {};
    snprintf(o.addr, sizeof(o.addr), "%02X:%02X:%02X:%02X:%02X:%02X",
             bda[5], bda[4], bda[3], bda[2], bda[1], bda[0]);
    o.addrType = addrType;
    o.rssi = (int8_t)dev->getRSSI();
    o.observedAtMs = now;
    o.channel = 37;  // NimBLE 2.x does not always expose; see §17

    // Name
    std::string name = dev->getName();
    strncpy(o.name, name.c_str(), sizeof(o.name) - 1);

    // Manufacturer ID — first 2 bytes of AD 0xFF, LE
    if (dev->haveManufacturerData()) {
      std::string md = dev->getManufacturerData();
      if (md.size() >= 2) {
        o.mfgrId = (uint16_t)((uint8_t)md[0] | ((uint8_t)md[1] << 8));
      }
    }

    // Service UUIDs — up to 6, semicolon-joined
    if (dev->haveServiceUUID()) {
      char* p = o.serviceUuids; size_t rem = sizeof(o.serviceUuids);
      for (int i = 0; i < dev->getServiceUUIDCount() && rem > 6; i++) {
        std::string s = dev->getServiceUUID(i).toString();
        if (s.size() > 4) s = s.substr(0, 4);  // 16-bit form only
        int n = snprintf(p, rem, "%s%s", (p == o.serviceUuids ? "" : ";"), s.c_str());
        if (n < 0 || (size_t)n >= rem) break;
        p += n; rem -= n;
      }
    }

    pendingResults.push_back(std::move(o));
  }
};

Observer obs;
void bleScanner_lifetimeBump() { /* friend-helper to mutate BleScanner state */ }

}  // namespace

// ----- BleScanner public methods -----

void BleScanner::begin() {
  if (initialised_) return;
  NimBLEDevice::init("piglet");
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);  // +9 dBm
  auto* scan = NimBLEDevice::getScan();
  scan->setScanCallbacks(&obs, /*wantDuplicates=*/false);
  scan->setActiveScan(false);              // passive only — see §3
  scan->setInterval(160);                  // 0.625 ms units → 100 ms
  scan->setWindow(100);                    // 62.5 ms (62.5% duty)
  initialised_ = true;
}

void BleScanner::startScan() {
  if (!initialised_) begin();
  if (scanRunning_) return;
  esp_coex_preference_set(ESP_COEX_PREFER_BT);
  NimBLEDevice::getScan()->start(cfg.bleScanDuration, /*is_continue=*/false);
  scanRunning_ = true;
  scanEndsAtMs_ = millis() + cfg.bleScanDuration * 1000;
}

void BleScanner::stopScan() {
  if (!scanRunning_) return;
  NimBLEDevice::getScan()->stop();
  esp_coex_preference_set(ESP_COEX_PREFER_BALANCE);
  scanRunning_ = false;
}

size_t BleScanner::consumeResults(std::vector<BleObservation>& out) {
  size_t n = pendingResults.size();
  out.reserve(out.size() + n);
  while (!pendingResults.empty()) {
    out.push_back(std::move(pendingResults.front()));
    pendingResults.pop_front();
  }
  return n;
}

size_t BleScanner::dedupeWindowSize() const {
  return dedupeRing.size();
}

void BleScanner::tick() {
  // 1) Scan window expiry
  if (scanRunning_ && millis() >= scanEndsAtMs_) {
    esp_coex_preference_set(ESP_COEX_PREFER_BALANCE);
    scanRunning_ = false;  // NimBLE already stopped on its own timer
  }

  // 2) Expire dedupe entries older than cfg.bleDedupeWindow
  uint32_t cutoff = millis() - (uint32_t)cfg.bleDedupeWindow * 1000;
  for (auto it = dedupeRing.begin(); it != dedupeRing.end(); ) {
    if (it->second < cutoff) it = dedupeRing.erase(it);
    else                     ++it;
  }
}

#endif  // PIGLET_HAS_BLE
```

The NimBLE callback runs in NimBLE's host task — it's **not safe** to call SD, WiFi, HTTP, or even `Serial.printf` from inside (those take mutexes that may be held by the loop task). All we do in the callback is move bytes into a `std::deque`. The loop task drains it via `consumeResults()` at its own pace and writes to SD from there. This is the v1 pattern. Post-2.1 (FreeRTOS task split), the deque becomes a FreeRTOS queue feeding the SD writer task; see §8.

---

# 7. Dedupe window strategy

## Why dedupe matters

BLE advertisements come in continuously. A typical iPhone broadcasts every 100–200 ms. Without dedupe, a 5-second scan in a coffee shop with 30 BLE devices produces 30 × 50 = 1500 rows. That's:

- Unreadable CSV
- Wasted SD I/O
- Wasted WiGLE upload bandwidth + the WiGLE-side dedup wastes their cycles
- Wasted JCMK ESP-Now bandwidth on Nodes

Dedupe at the source. Log each unique device at most once per `bleDedupeWindow` seconds (default 300 s = 5 minutes). This matches WiGLE's behaviour and is what their backend expects.

## What to key on

**Recommended: BDA + addrType packed into a uint64.** This dedupes:

- A device that re-advertises its public/static address (most fitness trackers, beacons, classic Bluetooth headsets) — keyed reliably.
- A device using static random addresses (some IoT devices) — keyed reliably as long as it doesn't rotate.

What it doesn't catch:

- **Resolvable Private Addresses (RPAs).** Apple and Google rotate these every ~15 minutes by design. The same iPhone walking past you will be logged 3-4 times an hour. **This is unavoidable** without a private key to resolve the RPA (which we don't have and shouldn't have). WiGLE's backend tolerates this.
- **NRPAs (non-resolvable private addresses).** Rare in practice; same situation as RPAs.

**Alternative considered: BDA + manufacturer-data hash.** Would catch some RPA-rotating devices because the manufacturer payload sometimes carries a stable identifier. Rejected because (a) computing the hash in the callback adds cycles in the wrong place, (b) the de-randomisation is the whole point of RPA — defeating it is privacy-hostile.

**Decision:** key on `(BDA[6 bytes], addrType[1 byte])` packed into `uint64_t`. 8 bytes per key.

## Data structure

`std::unordered_map<uint64_t, uint32_t>`:

- Key: packed BDA + type
- Value: `millis()` of last sighting
- Insertion: O(1) average; the key is 8 bytes and the default hash is decent on uint64
- Memory per entry: ~24 bytes overhead + 16 bytes key+value = **~40 bytes**

At `bleMaxResults = 500` we cap at ~20 KB of heap for the dedupe ring. Allocate in PSRAM if `PIGLET_HAS_PSRAM` (use `psram_allocator<>` — see below).

```cpp
#include <esp_heap_caps.h>

template<typename T>
struct PsramAllocator {
  using value_type = T;
  T* allocate(size_t n) {
    void* p = heap_caps_malloc(n * sizeof(T), MALLOC_CAP_SPIRAM);
    if (!p) p = heap_caps_malloc(n * sizeof(T), MALLOC_CAP_DEFAULT);
    return (T*)p;
  }
  void deallocate(T* p, size_t) { heap_caps_free(p); }
};

// Use:
std::unordered_map<BleKey, uint32_t,
                   std::hash<BleKey>,
                   std::equal_to<BleKey>,
                   PsramAllocator<std::pair<const BleKey, uint32_t>>> dedupeRing;
```

This is overkill for v1 — start with a default-allocated `unordered_map` and only move to PSRAM if you see internal-DRAM pressure during TLS uploads.

## Expiry

`BleScanner::tick()` walks the map and removes entries with `lastSeen < millis() - bleDedupeWindow * 1000`. Linear in map size; at 500 entries this is microseconds even on a single-core C5. Call it from `loop()` every ~1 s (gate on a `lastDedupeExpiryMs` static).

**Gotcha — `millis()` wraps.** `uint32_t` `millis()` wraps after ~49 days. The subtraction `millis() - cutoff` is unsigned and wraps correctly *as long as no entry is older than 24 days*. Since `bleDedupeWindow` is bounded at 24 hours (86400 s) by the config validator (§11), this is safe. Add a comment so nobody widens the cap without realising.

```cpp
// bleDedupeWindow is bounded to 86400s by config validator. uint32_t millis()
// wraps every 49 days; subtraction-based expiry is correct as long as no entry
// is older than ~24 days. Do NOT increase the cap without revisiting.
uint32_t cutoff = millis() - (uint32_t)cfg.bleDedupeWindow * 1000;
```

## Bounded-size FIFO for pending results

Separate from the dedupe ring is the `pendingResults` deque — the queue between the NimBLE callback and `consumeResults()`. Bounded by `cfg.bleMaxResults`. If the SD writer falls behind (e.g. during a TLS upload), the oldest pending observation is dropped. **The dedupe ring still records the sighting** — we just don't emit a row. This is the right trade-off: better to lose one CSV row than to OOM the device during an upload.

---

# 8. Interleave with the WiFi scanner FSM

## Two options

**Option A — Co-scheduled in the existing main loop (v1).** No FreeRTOS task split required. Add BLE state to the same loop that already polls Wi-Fi scan. Recommended for v1 because it lands without depending on the deep-dive's improvement 2.1.

**Option B — FreeRTOS task split (v2).** Requires improvement 2.1 to land first. Wi-Fi scanner runs on its own task, BLE scanner runs on its own task, both feed a queue consumed by the SD writer task. Better latency under upload load; harder to debug. Track as a v2 follow-up.

This section describes (A) concretely.

## State machine

Add BLE to the loop body in `Piglet.ino` (the existing scan block at lines 604-631). Today the loop only runs Wi-Fi when `currentPage != 5` and `allowScan` is true. BLE follows the same gating but with an additional time-based schedule.

```cpp
// Piglet.ino — loop() body, replacing the scan block at ~line 604
if (currentPage == 5) {
  if (meshCoreActive) coreModeTick();
  else                nodeModeTick();
} else {
  autoPaused = shouldPauseScanning();
  wifi_mode_t m = WiFi.getMode();
  bool apActive = (m == WIFI_AP || m == WIFI_AP_STA);

  bool allowScan = scanningEnabled && sdOk && !apActive &&
                   (userScanOverride || !autoPaused);
  // (existing page-specific gating preserved)

  // --- BLE scheduling -------------------------------------------
  #if PIGLET_HAS_BLE
  if (cfg.bleEnabled && allowScan) {
    static uint32_t lastBleStartMs = 0;
    if (!bleScanner.isScanning() &&
        (millis() - lastBleStartMs) >= cfg.bleScanInterval * 1000) {
      // Don't start BLE if a Wi-Fi async scan is currently in flight;
      // wait for it to complete to avoid clobbering coex priority.
      if (WiFi.scanComplete() != WIFI_SCAN_RUNNING) {
        bleScanner.startScan();
        lastBleStartMs = millis();
      }
    }
    bleScanner.tick();

    // Drain results into CSV
    std::vector<BleObservation> obs;
    if (bleScanner.consumeResults(obs) > 0) {
      writeBleRowsFromObs(obs);   // helper in SDUtils.cpp; tags GPS, calls appendBleRow
    }
  }
  #endif

  // --- WiFi scan (existing) ------------------------------------
  if (allowScan) {
    // Skip starting a NEW wifi scan during an active BLE window —
    // active wifi scan would block coex arbiter from giving BLE airtime.
    #if PIGLET_HAS_BLE
    if (cfg.bleEnabled && bleScanner.isScanning()) {
      // BLE window in progress; let it finish before next wifi sweep.
    } else
    #endif
    {
      doScanOnce();
    }
  }
}
```

## Cycle visualisation

Default config (`bleScanInterval=30`, `bleScanDuration=5`, Wi-Fi aggressive):

```
time   0s        15s   20s          50s   55s          80s
       │         │     │            │     │            │
WiFi   ████░░████░░    ████░░████░░ ░░    ████░░████░░ ░░
BLE                    ░░░░░         ░░░░░             ░░░░░
       \_______/        \_/                              \_/
        5×~3s WiFi      5s BLE                          5s BLE
```

Wi-Fi cycles continue at their normal ~3 s period; BLE inserts a 5 s window every 30 s during which Wi-Fi backs off. Total BLE radio time over a minute: ~10 s. Wi-Fi loses ~10 s of its 60 s budget to BLE — a 16% throughput hit. Acceptable.

## Why not start BLE in parallel with passive Wi-Fi

You could run BLE continuously and let coex arbiter slice it with Wi-Fi sweeps. Two reasons not to in v1:

1. **Wi-Fi RSSI accuracy.** Continuous BLE listening introduces ~3 dB more noise into Wi-Fi RSSI readings because the coex arbiter switches the LNA frequently. WiGLE cares about RSSI consistency for triangulation.
2. **Debuggability.** Discrete BLE windows make it easy to correlate "device X observed at time T" with GPS position. Continuous BLE means observations stream in throughout the Wi-Fi cycles, which complicates the dedupe-window expiry logic.

Revisit in v2 with proper task isolation.

---

# 9. GPS coupling

BLE results need the same GPS-fix-at-observation that Wi-Fi results have.

## Current Wi-Fi pattern

`Scanner.cpp:processScanResults()` (lines 22-64) captures one `firstSeen` timestamp + lat/lon/altM/accM at the *start* of result processing and applies the same coordinates to every row in the batch. Acceptable because a 13-channel sweep takes ~1.3 s — GPS position can change ~10 m at 30 km/h, which is below GPS accuracy anyway.

## BLE pattern

A BLE scan window is **5 s** by default — 4× longer than a Wi-Fi sweep. At 30 km/h that's ~40 m of motion. Two options:

**Option A — single fix per window.** Snapshot lat/lon/altM/accM at scan window start. Apply to all rows from that window. Simple. Acceptable for v1 because:
- Wardriving is usually walking pace or slow driving (< 15 km/h on residential streets)
- WiGLE's spatial bucketing is ~10 m anyway

**Option B — per-observation fix.** Snapshot lat/lon at *each* `consumeResults()` call. Slightly more accurate. Requires plumbing the timestamp from the BleObservation into the SD writer.

**Decision: Option A for v1.** Concrete implementation:

```cpp
// SDUtils.cpp — new helper called from loop() after bleScanner.consumeResults()
struct GpsSnapshot { double lat, lon, altM, accM; String firstSeen; bool valid; };

GpsSnapshot snapshotGpsNow() {
  GpsSnapshot g{};
  g.firstSeen = iso8601NowUTC();
  if (gpsHasFix) {
    g.lat  = gps.location.lat();
    g.lon  = gps.location.lng();
    g.altM = gps.altitude.meters();
    g.accM = gps.hdop.hdop();
    g.valid = true;
  }
  return g;
}

void writeBleRowsFromObs(const std::vector<BleObservation>& obs) {
  if (obs.empty()) return;
  GpsSnapshot g = snapshotGpsNow();
  // Even without GPS fix, write rows with 0,0 — WiGLE accepts but doesn't credit them.
  // Or skip entirely if !g.valid — current Wi-Fi writes them with 0,0,0,0.
  for (const auto& o : obs) {
    String svc(o.serviceUuids);
    appendBleRow(o.addr, o.name, csvAddrType(o.addrType),
                 g.firstSeen, o.channel, o.rssi,
                 g.lat, g.lon, g.altM, g.accM,
                 svc, o.mfgrId);
  }
}
```

## Timestamp alignment

The `BleObservation::observedAtMs` field is the actual time of each advert (relative to boot). For v1, ignore it and use the `snapshotGpsNow()` timestamp uniformly. For v2 (Option B), use `observedAtMs` to interpolate GPS position from a 10-second buffer of past positions — out of scope for v1.

---

# 10. Memory budget

This is the make-or-break section for whether BLE can land without other improvements first.

## NimBLE-Arduino footprint

With the role-trimmed config in §5 (observer only, no GATT):

| Resource | Cost |
|---|---|
| Flash (text + rodata) | ~110 KB |
| Static RAM (.data + .bss) | ~9 KB |
| NimBLE host task stack | 4 KB |
| NimBLE controller task stack | 4 KB |
| Heap during scan (NimBLE internals) | ~2 KB |

## Piglet additions

- `BleScanner` instance: ~100 B static
- `dedupeRing` (unordered_map, default `bleMaxResults=500`): ~20 KB at full occupancy
- `pendingResults` deque (bounded 500): ~30 KB worst case at `sizeof(BleObservation)` ≈ 60 B
- Per-row write buffer in `appendBleRow`: 320 B on stack (matches Wi-Fi writer post-improvement-4.2; pre-4.2 it's `String` allocations)

**Total new allocation: ~70 KB peak (worst case), ~35 KB typical (~200 dedupe entries, ~100 pending).**

## Target board headroom (today, pre-BLE)

| Board | Flash free | DRAM free (typical) | PSRAM free | Verdict |
|---|---|---|---|---|
| XIAO ESP32-S3 + 8 MB PSRAM | ~1.5 MB | ~180 KB | ~7.8 MB | Plenty of room. Put dedupe ring in PSRAM. |
| XIAO ESP32-C5 + 2 MB PSRAM | ~400 KB | ~190 KB | ~1.8 MB | Tight but workable. WebUI HTML PROGMEM (improvement 1.6) would help by freeing flash. |
| XIAO ESP32-C6 + 2 MB PSRAM | ~500 KB | ~200 KB | ~1.8 MB | Workable. |
| T-Dongle C5 | ~400 KB | ~190 KB | ~1.8 MB | Same as XIAO C5. |
| Waveshare C6 | ~500 KB | ~200 KB | ~1.8 MB | Same as XIAO C6. |
| PigletNode (C5) | ~1.2 MB (no SD/OLED/WebUI) | ~250 KB | ~1.8 MB | Loads of room; cleanest target. |

**Numbers above are estimates from `arduino-cli compile --output-info` against the canonical multi-file build at v2.52. Verify on your build before tagging.**

## Does anything need to shrink first?

- **No** on S3, C6, Waveshare, PigletNode.
- **Borderline** on C5 + T-Dongle. With PSRAM enabled (already required per README) the heap pressure is manageable. The flash margin is the concern: NimBLE's 110 KB plus ~5 KB of our BLE module code lands around 285 KB of free flash on C5 — comfortable, but not if you also add a fat config blob or a third uploader. **Recommend** shipping improvement 1.6 (HTML PROGMEM gzip) before BLE on these boards, or be ready to ship them with the BLE PR.

## PSRAM placement

NimBLE itself stays in DRAM (host stack needs fast access). Our `dedupeRing` and `pendingResults` can live in PSRAM. Add the allocator wrapper from §7 if you observe DRAM pressure; otherwise leave defaults and profile post-deploy.

```cpp
// Sanity check, run at boot when bleEnabled:
size_t freeDram   = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
size_t freePsram  = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
Serial.printf("[BLE] free DRAM=%u  PSRAM=%u  needed (max)=70KB\n",
              (unsigned)freeDram, (unsigned)freePsram);
if (freeDram < 100 * 1024) {
  Serial.println("[BLE] WARNING: low DRAM, BLE scanning may destabilise TLS uploads");
}
```

---

# 11. New config keys

Five new keys, all parsed via the existing `cfgAssignKV` flow.

## Schema

| Key | Type | Default | Range | Purpose |
|---|---|---|---|---|
| `bleEnabled` | bool | `false` | true / false | Master enable. When false, NimBLE is not initialised — zero flash/RAM cost beyond a single bool. |
| `bleScanDuration` | int (seconds) | `5` | 1–10 | Length of each BLE scan window. Capped at 10 s to prevent indefinite STA-Wi-Fi starvation from `ESP_COEX_PREFER_BT`. |
| `bleScanInterval` | int (seconds) | `30` | `bleScanDuration + 5` to 300 | Time between scan window starts. Min enforced to ensure at least 5 s of clear-air between windows. |
| `bleDedupeWindow` | int (seconds) | `300` | 0–86400 | A device seen within this window is not logged again. 0 = log every advert (debug only). 86400 = once per day. |
| `bleMaxResults` | int | `500` | 100–2000 | Cap on dedupe ring + pending FIFO size. Memory budget knob. |

## Diff to `Config.h`

```cpp
struct Config {
  // ... existing fields ...

  // ---- BLE wardriving ----
  // Master enable. When false, NimBLE is not initialised.
  bool bleEnabled         = false;

  // BLE scan window duration (s). Capped at 10s.
  uint16_t bleScanDuration = 5;

  // Time between BLE scan windows (s). Min = bleScanDuration + 5.
  uint16_t bleScanInterval = 30;

  // Per-device dedupe window (s). 0 = no dedupe. Max 86400.
  uint32_t bleDedupeWindow = 300;

  // Dedupe ring + pending FIFO cap.
  uint16_t bleMaxResults  = 500;
};
```

## Diff to `Config.cpp::cfgAssignKV`

Append to the existing `if/else if` chain (around line 130):

```cpp
else if (k == "bleEnabled") {
  String vv = v; vv.toLowerCase();
  cfg.bleEnabled = (vv == "true" || vv == "1");
}
else if (k == "bleScanDuration") {
  int n = v.toInt();
  if (n >= 1 && n <= 10) cfg.bleScanDuration = (uint16_t)n;
}
else if (k == "bleScanInterval") {
  int n = v.toInt();
  // Enforce min after-load in validateConfig(); accept any reasonable here.
  if (n >= 5 && n <= 300) cfg.bleScanInterval = (uint16_t)n;
}
else if (k == "bleDedupeWindow") {
  int n = v.toInt();
  if (n >= 0 && n <= 86400) cfg.bleDedupeWindow = (uint32_t)n;
}
else if (k == "bleMaxResults") {
  int n = v.toInt();
  if (n >= 100 && n <= 2000) cfg.bleMaxResults = (uint16_t)n;
}
```

## Diff to `Config.cpp::saveConfigToSD`

Append to the existing print sequence (after `rotateScreen180`):

```cpp
f.println("");
f.println("# ---- BLE Wardriving ----");
f.println("# Set true to passively scan for Bluetooth LE devices alongside Wi-Fi.");
f.println("# Logs to the same CSV with Type=BLE. Requires reboot if changing.");
f.print("bleEnabled=");      f.println(cfg.bleEnabled ? "true" : "false");
f.println("");
f.println("# BLE scan window duration (s). 1-10.");
f.print("bleScanDuration=");  f.println(cfg.bleScanDuration);
f.println("");
f.println("# Time between BLE scan windows (s). Must exceed bleScanDuration.");
f.print("bleScanInterval=");  f.println(cfg.bleScanInterval);
f.println("");
f.println("# Per-device dedupe window (s). 0=no dedupe. Default 300.");
f.print("bleDedupeWindow=");  f.println(cfg.bleDedupeWindow);
f.println("");
f.println("# Dedupe ring + pending FIFO cap. 100-2000. Memory knob.");
f.print("bleMaxResults=");    f.println(cfg.bleMaxResults);
```

## Validator

Add `validateConfig()` called at the end of `loadConfigFromSD()` to enforce cross-field constraints:

```cpp
void validateConfig() {
  if (cfg.bleScanInterval < cfg.bleScanDuration + 5)
    cfg.bleScanInterval = cfg.bleScanDuration + 5;
  if (cfg.bleDedupeWindow > 86400)
    cfg.bleDedupeWindow = 86400;
  if (cfg.bleScanDuration > 10)
    cfg.bleScanDuration = 10;
}
```

## Sample `wardriver.cfg` additions

Append at the bottom of `Arduino Files/Piglet/wardriver.cfg`:

```ini
# ============================================================
# BLE Wardriving (optional)
# ============================================================
# Passive Bluetooth LE scanning alongside Wi-Fi. Logs to the same
# CSV with Type=BLE. Requires reboot if changing bleEnabled.

# Master enable. NimBLE stack is not loaded when false → no flash/RAM cost.
bleEnabled=false

# BLE scan window duration (s). Range 1-10.
# Each window dedicates the radio to BLE listening for this long.
bleScanDuration=5

# Time between BLE scan windows (s). Must exceed bleScanDuration + 5.
# Default 30s — BLE window every 30 seconds, ~17% radio time on BLE.
bleScanInterval=30

# Per-device dedupe window (s). Same MAC won't be logged more than
# once per this many seconds. Range 0-86400. 0 = log every advert.
bleDedupeWindow=300

# Memory cap for dedupe ring + pending results queue. Range 100-2000.
# Each entry is ~60 bytes; 500 entries = ~30 KB.
bleMaxResults=500
```

---

# 12. WebUI changes

Three areas: `/status.json` additions, `/files.json` additions, new BLE config card in the HTML.

## `/status.json` additions

`WebUI.cpp::handleStatus()` (around line 783) adds a `ble` object:

```cpp
JsonObject ble = doc["ble"].to<JsonObject>();
ble["enabled"]    = cfg.bleEnabled;
#if PIGLET_HAS_BLE
ble["scanning"]   = bleScanner.isScanning();
ble["lifetimeUnique"] = bleScanner.lifetimeUniqueCount();
ble["dedupeSize"] = (uint32_t)bleScanner.dedupeWindowSize();
#else
ble["scanning"]   = false;
ble["lifetimeUnique"] = 0;
ble["dedupeSize"] = 0;
#endif
```

And inside the existing `config` object, the five new BLE config keys:

```cpp
c["bleEnabled"]       = cfg.bleEnabled;
c["bleScanDuration"]  = cfg.bleScanDuration;
c["bleScanInterval"]  = cfg.bleScanInterval;
c["bleDedupeWindow"]  = cfg.bleDedupeWindow;
c["bleMaxResults"]    = cfg.bleMaxResults;
```

## `/files.json` additions

The current `/files.json` walks `/logs` and `/uploaded`. BLE rows go into the same CSV files as Wi-Fi rows (one mixed CSV per session). No new file directories. **No `/files.json` schema change required** for v1.

If a future version splits BLE into separate `.csv` files, add a `type` field to the file entries — but for v1, mixed CSVs are the right call (matches WiGLE's expected format).

## HTML form additions

Insert a new `<div class="card">` block between the existing "Wi-Fi" and "Mesh" cards in `INDEX_HTML[]` (around `WebUI.cpp:340`):

```html
<div class="card">
  <h3>Bluetooth LE</h3>
  <div class="row">
    <div>
      <label>BLE Enabled</label>
      <select id="bleEnabled">
        <option value="false">Off</option>
        <option value="true">On (requires reboot)</option>
      </select>
    </div>
    <div>
      <label>Scan Duration (s)</label>
      <input id="bleScanDuration" type="number" min="1" max="10">
    </div>
  </div>
  <div class="row">
    <div>
      <label>Scan Interval (s)</label>
      <input id="bleScanInterval" type="number" min="10" max="300">
    </div>
    <div>
      <label>Dedupe Window (s)</label>
      <input id="bleDedupeWindow" type="number" min="0" max="86400">
    </div>
  </div>
  <div class="kv" id="bleStats">
    <div><span class="k">BLE Scanning</span><span class="v" id="bleScanning">—</span></div>
    <div><span class="k">Unique BLE</span><span class="v" id="bleUnique">—</span></div>
    <div><span class="k">Dedupe Ring</span><span class="v" id="bleDedupeSize">—</span></div>
  </div>
</div>
```

Update the JS `applyStatus()` function (around line 503) to add the BLE config keys to the auto-populate list:

```js
for(const k of ['wigleBasicToken','wdgwarsApiKey','deviceName','board','gpsBaud',
                'homeSsid','wardriverSsid','wardriverPsk','scanMode','speedUnits',
                'battPin','batteryTest','maxBootUploads','meshModeOnBoot','rotateScreen180',
                'bleEnabled','bleScanDuration','bleScanInterval','bleDedupeWindow','bleMaxResults']){
  // ...
}
```

And the save-keys list (line 585):

```js
const keys=['board','wigleBasicToken','wdgwarsApiKey','deviceName','gpsBaud',
            'homeSsid','homePsk','wardriverSsid','wardriverPsk','scanMode','speedUnits',
            'battPin','batteryTest','maxBootUploads','meshModeOnBoot','rotateScreen180',
            'bleEnabled','bleScanDuration','bleScanInterval','bleDedupeWindow','bleMaxResults'];
```

And in the status-polling block, after the existing `applyStatus()` body, append:

```js
if (j.ble) {
  document.getElementById('bleScanning').textContent  = j.ble.scanning ? 'Yes' : 'No';
  document.getElementById('bleUnique').textContent    = j.ble.lifetimeUnique;
  document.getElementById('bleDedupeSize').textContent = j.ble.dedupeSize;
}
```

---

# 13. JCMK mesh protocol extension

## Recommendation: new message type 6

The JCMK protocol enum at `MeshNode.cpp:21-28`:

```cpp
enum JcmkMsgType : uint8_t {
  JCMK_MSG_CORE_REQUEST = 1,
  JCMK_MSG_CORE_REPLY   = 2,
  JCMK_MSG_HEARTBEAT    = 3,
  JCMK_MSG_TEXT         = 4,   // Wi-Fi observation
  JCMK_MSG_ADMIN        = 5,
};
```

Add **`JCMK_MSG_BLE_OBS = 6`**. Reasons not to overload `JCMK_MSG_TEXT`:

1. The existing TEXT message format is `BSSID,SSID,AUTH,CHANNEL,RSSI,W` (CSV-ish, see `MeshNode.cpp:466`). Adding a sixth field (type) would break parsing in deployed JCMK Cores (Biscuit Pro, JCMK C5 Wardriver).
2. The `AUTH` field in the existing format is the Wi-Fi auth mode string — for BLE we'd need to overload it with `[LE Random]` etc. Mixing semantic spaces is fragile.
3. A new message type lets Cores that don't speak BLE drop the packet cleanly. Forward-compatible by design.

## Packed struct

Mirror `jcmk_text_msg_t`'s 212-byte total size to satisfy the Biscuit Pro "drops packets < 212 bytes" rule documented in `MeshNode.cpp:160-162`.

```cpp
// piglet-core/include/piglet/jcmk_ble.h (post improvement 1.1)
// or MeshNode.cpp inline (pre-1.1)
typedef struct __attribute__((packed)) {
  char     magic[4];      // 'E','N','O','W'
  uint8_t  type;          // JCMK_MSG_BLE_OBS = 6
  uint32_t counter;       // monotonic per-sender
  uint16_t len;           // sizeof active fields below; informational

  // BLE observation payload
  uint8_t  bda[6];        // raw BDA, big-endian (matches "AA:BB:..." order)
  uint8_t  addrType;      // 0=public, 1=random, 2=RPA, 3=NRPA
  uint8_t  channel;       // 37/38/39
  int8_t   rssi;          // dBm
  uint16_t mfgrId;        // from AD 0xFF, LE
  uint32_t observedAtMsRel; // millis() relative; Core treats as fresh
  char     name[33];      // null-terminated, truncated
  char     serviceUuids[64]; // semicolon-separated, null-terminated

  // Padding to 212 bytes for Biscuit Pro compat
  uint8_t  _pad[212 - 4 - 1 - 4 - 2 - 6 - 1 - 1 - 1 - 2 - 4 - 33 - 64];
} jcmk_ble_obs_msg_t;
static_assert(sizeof(jcmk_ble_obs_msg_t) == 212, "JCMK BLE obs must be 212 bytes");
```

The `_pad` field computation is verbose; verify with the `static_assert`. Pad goes at the end (Biscuit Pro just checks length, doesn't read past the known prefix).

## Sender (Node side)

Mesh Node mode is currently in `MeshNode.cpp`. When `meshNodeActive` and `cfg.bleEnabled`, BLE observations should go over ESP-Now to the Core instead of being written to the (non-existent) local SD. Add an alternate sink:

```cpp
// MeshNode.cpp — new function called from nodeModeTick() drain path
static void jcmkSendBleObs(const BleObservation& o) {
  jcmk_ble_obs_msg_t msg = {};
  memcpy(msg.magic, JCMK_MAGIC, 4);
  msg.type    = JCMK_MSG_BLE_OBS;
  msg.counter = ++jcmkHbCounter;
  msg.len     = sizeof(msg) - sizeof(msg._pad);

  // Parse "AA:BB:CC:DD:EE:FF" back into 6 bytes (Node could carry raw too,
  // but BleObservation::addr is the canonical string form)
  for (int i = 0; i < 6; i++) {
    msg.bda[i] = strtoul(o.addr + i * 3, nullptr, 16);
  }
  msg.addrType        = o.addrType;
  msg.channel         = o.channel;
  msg.rssi            = o.rssi;
  msg.mfgrId          = o.mfgrId;
  msg.observedAtMsRel = millis() - o.observedAtMs;
  strncpy(msg.name,         o.name,         sizeof(msg.name) - 1);
  strncpy(msg.serviceUuids, o.serviceUuids, sizeof(msg.serviceUuids) - 1);

  esp_now_send(jcmkCoreMac, (uint8_t*)&msg, sizeof(msg));
  jcmkSentCount++;
}
```

Hook into the node-mode scan loop: after `bleScanner.consumeResults(obs)`, iterate and call `jcmkSendBleObs(o)` for each observation instead of `writeBleRowsFromObs(obs)`. Single branch in `Piglet.ino` based on `meshNodeActive`.

## Receiver (Core side)

In `jcmkOnRecv` (`MeshNode.cpp:241`), add a new case in the Core dispatch:

```cpp
// MeshNode.cpp — inside jcmkOnRecv, alongside JCMK_MSG_TEXT handling
} else if (type == JCMK_MSG_BLE_OBS && len >= (int)sizeof(jcmk_ble_obs_msg_t)) {
  const jcmk_ble_obs_msg_t* m = (const jcmk_ble_obs_msg_t*)data;

  // Format the BDA back to "AA:BB:CC:DD:EE:FF"
  char bdaStr[18];
  snprintf(bdaStr, sizeof(bdaStr), "%02X:%02X:%02X:%02X:%02X:%02X",
           m->bda[0], m->bda[1], m->bda[2], m->bda[3], m->bda[4], m->bda[5]);

  // Stamp with Core's GPS and write a BLE row to the Core's SD log
  double lat = 0, lon = 0, altM = 0, accM = 0;
  if (gpsHasFix) {
    lat  = gps.location.lat();
    lon  = gps.location.lng();
    altM = gps.altitude.meters();
    accM = gps.hdop.hdop();
  }
  String svc(m->serviceUuids);
  String name(m->name);
  appendBleRow(bdaStr, name, csvAddrType(m->addrType),
               iso8601NowUTC(), m->channel, m->rssi,
               lat, lon, altM, accM, svc, m->mfgrId);
  coreRecordsRx++;
}
```

**Legacy Cores (Biscuit Pro, older JCMK C5 Wardriver)** receive the packet, see `type=6` in the type-switch, fall through to the default branch (no-op) and the packet is silently dropped. No crash, no error log noise. The 212-byte length check passes because we padded — they just don't know what to do with it.

## Heartbeat / handshake compatibility

The existing CORE_REQUEST/CORE_REPLY/ADMIN handshake at `MeshNode.cpp:21-27` is unchanged. A BLE-capable Node uses the same handshake to pair with a Core. The Core doesn't need to know whether a Node will send BLE — it just routes by type when packets arrive.

Consider adding a capability bit in the next ADMIN message revision to negotiate BLE support proactively, but for v1 it's fine to let the Core silently drop unknown types.

## Wire-format stability

Once you commit the `jcmk_ble_obs_msg_t` struct shape, **don't change the field order or sizes**. Append-only via the `_pad` block if you need to add fields later. Document the layout in `docs/PROTOCOL.md` (improvement 8.2).

---

# 14. OLED display updates

The Networks page (`Display.cpp::drawPageNetworks`, line 702) currently shows 2.4G and 5G counts. Add a BLE count line.

## Current layout (`drawPageNetworks` at line 702-738)

```
┌────────────────────┐
│   2.4 GHz: N       │
│   5 GHz:   M       │
│                    │
│   Total:   N+M     │
└────────────────────┘
```

## Proposed layout

```
┌────────────────────┐
│   2.4 GHz: N       │
│   5 GHz:   M       │
│   BLE:     B       │
│                    │
│   Total:   N+M+B   │
└────────────────────┘
```

Concrete diff to `Display.cpp::drawPageNetworks` (around line 720):

```cpp
// After the existing 5G line and before the total:
#if PIGLET_HAS_BLE
if (cfg.bleEnabled) {
  display.setCursor(0, 36);                 // Adjust Y to fit
  display.setTextSize(2);
  display.print("BLE:");
  display.setCursor(64, 36);
  display.print(bleScanner.lifetimeUniqueCount());
}
#endif

// Adjust the Total line Y coordinate down by 12 px:
display.setCursor(0, 52);
display.print("Total:");
display.setCursor(64, 52);
uint32_t total = networksFound2G + networksFound5G;
#if PIGLET_HAS_BLE
if (cfg.bleEnabled) total += bleScanner.lifetimeUniqueCount();
#endif
display.print(total);
```

When `cfg.bleEnabled == false`, the page looks exactly as it does today — no extra row.

## Status page tweaks

`drawPageStatus` (line 449) currently shows scan state in a single line. Add a small indicator for BLE state if enabled:

```cpp
// In drawPageStatus, near the scan-state indicator
#if PIGLET_HAS_BLE
if (cfg.bleEnabled) {
  display.setCursor(96, 0);
  display.setTextSize(1);
  display.print(bleScanner.isScanning() ? "BLE*" : "BLE ");
}
#endif
```

`*` = currently scanning, space = waiting for next window.

## Pig page, Mesh page, etc.

No changes. Pig animation is independent. Mesh page already shows ESP-Now stats; if you want BLE-forwarded count, add a single line in `MeshNode.cpp`'s OLED drawer:

```cpp
// MeshNode.cpp — drawMeshNodePage or equivalent
display.setCursor(0, 56);
display.printf("BLE→: %u", (unsigned)jcmkBleSentCount);
```

(With a new `jcmkBleSentCount` counter that you bump in `jcmkSendBleObs`.)

---

# 15. Test plan

## Host-side unit tests (additions to `test/`, post-improvement 3.1)

Required tests, in `test/test_ble.cpp`:

```cpp
#include "doctest.h"
#include "piglet/ble_row.h"   // appendBleRow plumbing
#include "piglet/jcmk_ble.h"  // packet builder

TEST_CASE("BLE CSV row formatting — public address with name and mfgr") {
  std::stringstream out;
  appendBleRowTest(out,
    "AA:BB:CC:11:22:33", "MyDevice", "[LE Public]",
    "2026-05-28T14:23:00Z", 38, -67,
    40.712800, -74.006000, 15.2, 1.4,
    "FE9F", 76);
  CHECK(out.str() ==
    "AA:BB:CC:11:22:33,\"MyDevice\",[LE Public],"
    "2026-05-28T14:23:00Z,38,2426,-67,"
    "40.712800,-74.006000,15.2,1.4,"
    "FE9F,76,BLE\r\n");
}

TEST_CASE("BLE row — empty name, empty service uuids, mfgr=0") { /* ... */ }
TEST_CASE("BLE row — name with embedded quotes is properly escaped") { /* ... */ }
TEST_CASE("Channel-to-freq for BLE primaries — 37→2402, 38→2426, 39→2480") { /* ... */ }
TEST_CASE("addrType mapping — 0/1/2/3 maps to LE Public/Random/Resolvable/NonResolvable") { /* ... */ }

TEST_CASE("Dedupe ring — second sighting suppressed") {
  DedupeRing r(300);          // 300 s window
  uint8_t bda[6] = {1,2,3,4,5,6};
  CHECK(r.shouldEmit(bda, 0));   // first sighting
  CHECK(!r.shouldEmit(bda, 0));  // dupe
}

TEST_CASE("Dedupe ring — expiry after window") {
  DedupeRing r(300);
  uint8_t bda[6] = {1,2,3,4,5,6};
  r.shouldEmit(bda, 0);
  r.advanceClock(301 * 1000);   // simulated millis()
  CHECK(r.shouldEmit(bda, 0));   // re-emitted after expiry
}

TEST_CASE("Dedupe ring — different addrType is different key") {
  DedupeRing r(300);
  uint8_t bda[6] = {1,2,3,4,5,6};
  CHECK(r.shouldEmit(bda, 0));   // public
  CHECK(r.shouldEmit(bda, 1));   // same BDA bytes, random type — different device
}

TEST_CASE("Dedupe ring — bounded size evicts oldest") {
  DedupeRing r(300, /*maxSize=*/3);
  for (uint8_t i = 0; i < 5; i++) {
    uint8_t bda[6] = {i,0,0,0,0,0};
    r.shouldEmit(bda, 0);
  }
  CHECK(r.size() == 3);
}

TEST_CASE("JCMK BLE packet — round-trip preserves all fields") {
  BleObservation o = makeTestObservation();
  jcmk_ble_obs_msg_t pkt;
  buildBleObsPacket(pkt, o, /*counter=*/42);

  CHECK(pkt.type == JCMK_MSG_BLE_OBS);
  CHECK(memcmp(pkt.magic, "ENOW", 4) == 0);
  CHECK(pkt.counter == 42);

  // Send-side: parse it back
  BleObservation o2 = parseBleObsPacket(pkt);
  CHECK(strcmp(o2.addr, o.addr) == 0);
  CHECK(o2.rssi == o.rssi);
  CHECK(o2.mfgrId == o.mfgrId);
  // ... etc
}

TEST_CASE("JCMK BLE packet — sizeof == 212") {
  CHECK(sizeof(jcmk_ble_obs_msg_t) == 212);
}
```

Target: **15+ test cases, ~40+ assertions**. Add to `test/Makefile` so `make -C test test` picks them up.

## On-device integration tests

Manual tests run on actual hardware before tagging:

| # | Test | Setup | Pass criteria |
|---|---|---|---|
| 1 | BLE enable from cold boot | Edit `wardriver.cfg`, set `bleEnabled=true`, reboot | OLED Networks page shows `BLE: N` line within 30 s. Lifetime count > 0 in BLE-rich location. |
| 2 | Dedupe within window | Sit stationary for 5 minutes in a room with 5 BLE devices | `BLE` counter increases by exactly 5 (one per device), not 50× |
| 3 | Dedupe expiry | Set `bleDedupeWindow=60`, scan for 5 min | Counter increases at start, plateaus, then increases again ~60 s later for the same devices |
| 4 | Wi-Fi accuracy unchanged | Before BLE: log 100 Wi-Fi observations of a fixed AP. After: log another 100 with BLE on. Compare RSSI distribution. | Mean RSSI within ±2 dBm, stddev within ±1 dB. Not a meaningful regression. |
| 5 | GPS coupling | Drive ~1 km with known BLE devices in the car | All BLE rows in the CSV have non-zero lat/lon close to actual trajectory |
| 6 | WiGLE accepts the upload | Generate a mixed Wi-Fi+BLE CSV, upload via the WebUI button | `200 OK` from WiGLE, account dashboard shows new BLE observations within 24 h |
| 7 | WDGoWars accepts the upload | Same CSV, WDGoWars endpoint | `202 Accepted` + `done` poll result |
| 8 | Mesh BLE forwarding | Node with `bleEnabled=true` paired with BLE-aware Core | Core's CSV contains BLE rows with addresses observed by the Node |
| 9 | Mesh backward compat | Node with BLE enabled sending to a legacy Core (Biscuit Pro or pre-feature firmware) | No crash on the Core. Wi-Fi observations from same Node still recorded normally. |
| 10 | Long-run stability | Run for 8 h continuously in BLE-busy environment | No crash. Heap usage stable (< 5% growth). Dedupe ring size ≤ `bleMaxResults`. |
| 11 | Cold start with BLE disabled | `bleEnabled=false` (default) | Zero change to existing behaviour. Wi-Fi-only mode identical to pre-feature firmware. |
| 12 | Toggle live (no reboot) | Set `bleEnabled=true` via WebUI without rebooting | NimBLE init may fail because heap is fragmented after WebUI use → expected. Document that BLE toggle requires reboot. |

## Field test

One full wardriving drive (~1 hour, mixed urban/residential) and upload. Compare:

- Wi-Fi point density vs. a pre-BLE baseline drive of the same route — expect within ±5%
- BLE point density — expect ~30-100 unique devices per km in residential, ~500 per km in dense urban
- CSV size — expect 1.3-2.5× larger than Wi-Fi-only

---

# 16. Phased delivery

Five PRs. Each PR is independently shippable; you can pause after any of them. The default `bleEnabled=false` means PR1-PR4 are no-ops on production devices until the user explicitly opts in.

## PR1 — Config + stubs + flagged off (S)

**Scope:**
- New BLE config keys (§11) — `Config.h`, `Config.cpp::cfgAssignKV`, `Config.cpp::saveConfigToSD`, sample `wardriver.cfg`
- `BleScanner.h` skeleton (no-op when `PIGLET_HAS_BLE=0`)
- Validator function
- WebUI HTML/JS additions (read-only; just display the config)

**Touches:** `Config.{h,cpp}`, `WebUI.cpp`, `wardriver.cfg`. Does **not** touch Scanner, MeshNode, SDUtils, Display, or Piglet.ino.

**Acceptance:**
- Build succeeds on all six targets with and without `bleEnabled=true` in the config
- Sample `wardriver.cfg` parses
- WebUI shows the BLE card; toggling doesn't crash (it just writes the config)
- Tests added: config-key parsing for all five new keys
- No change in firmware behaviour with `bleEnabled=false` (which is default)

## PR2 — BleScanner module + dedupe + interleave option (M)

**Scope:**
- NimBLE-Arduino added to `platformio.ini` `lib_deps`
- Full `BleScanner.{h,cpp}` implementation (§6)
- Dedupe ring + bounded pending queue (§7)
- Coex preference hooks (§3)
- Scheduler integration in `Piglet.ino` loop (§8 option A)
- Memory diagnostics at boot
- No SD writing yet (debug-only: count visible on OLED + `/status.json`)

**Touches:** `BleScanner.cpp`, `Piglet.ino`, `Display.cpp` (BLE line on Networks page only when scanning observed).

**Acceptance:**
- `bleEnabled=true` + reboot → BLE counter increases when devices nearby
- Dedupe ring respects `bleDedupeWindow`
- Memory budget check at boot prints "[BLE] free DRAM=... PSRAM=..." with warning if low
- Wi-Fi performance unchanged (test #4 from §15)
- Tests added: dedupe ring behaviour
- No CSV writes for BLE yet — that's PR3

## PR3 — appendBleRow + CSV write + WiGLE upload (M)

**Scope:**
- `appendBleRow()` in `SDUtils.cpp` (§4)
- `writeBleRowsFromObs()` helper plumbing the GPS snapshot (§9)
- Hook from the loop scheduler to actually write rows
- WiGLE and WDGoWars uploaders unchanged — the existing batch uploader handles mixed CSVs transparently because the file shape is unchanged

**Touches:** `SDUtils.{h,cpp}`, `Piglet.ino`.

**Acceptance:**
- CSV files now contain `Type=BLE` rows
- A mixed CSV uploads to both WiGLE and WDGoWars (200/202 responses)
- WiGLE dashboard credits the user with new BLE observations within 24 h
- Field test #5 (GPS coupling) passes
- Tests added: CSV row formatting, channel→freq, addrType mapping

## PR4 — JCMK BLE_OBS mesh extension (M)

**Scope:**
- New `JCMK_MSG_BLE_OBS = 6` type (§13)
- `jcmk_ble_obs_msg_t` packed struct with `static_assert(sizeof == 212)`
- `jcmkSendBleObs()` sender
- Core-side receiver dispatching to `appendBleRow`
- PigletNode firmware updated to scan BLE and forward via new message type
- `docs/PROTOCOL.md` documenting the new message type (improvement 8.2 prerequisite — can land in this PR if 8.2 hasn't yet)

**Touches:** `MeshNode.{h,cpp}`, `PigletNode/PigletNode.ino`, new `docs/PROTOCOL.md` (or addition to existing).

**Acceptance:**
- Node-mode Piglet with `bleEnabled=true` forwards BLE observations
- Core-mode Piglet writes BLE rows to its CSV
- Legacy Cores (Biscuit Pro or pre-feature firmware): silently drop the unknown type, Wi-Fi observations still flow (test #9)
- Tests added: JCMK BLE packet round-trip
- Wire-format struct documented in PROTOCOL.md

## PR5 — WebUI + OLED polish + observability (S)

**Scope:**
- Final WebUI card layout, full edit/save (§12)
- OLED Networks page with proper BLE line layout (§14)
- Status page BLE indicator
- Optional: per-source counters in `/status.json` (`bleWifiRowsToday`, `bleObsToday`)
- Documentation pass: README section, `wardriver.cfg` examples

**Touches:** `WebUI.cpp`, `Display.cpp`, `README.md`, `CHANGELOG.md`.

**Acceptance:**
- WebUI BLE card matches design
- OLED layout test on real hardware
- CHANGELOG entry includes "BLE wardriving support" with config key reference
- README updated with the BLE feature listed

---

# 17. Risks and gotchas

## Antenna sharing → Wi-Fi RSSI drop

The 2.4 GHz Wi-Fi and BLE share an RF front-end. When BLE listens on ch 38 (2426 MHz), the LNA is tuned there; if a Wi-Fi sweep tries to sample ch 1 (2412 MHz) in the same millisecond, the coex arbiter switches the front-end and Wi-Fi may catch a partial frame. Empirically you'll see 1-3 dBm of additional Wi-Fi RSSI noise.

**Mitigation:** option A's discrete BLE windows minimise overlap. If a user reports degraded Wi-Fi accuracy, document the `bleScanInterval=60` recommendation (less BLE = less Wi-Fi noise).

## BLE MAC randomisation wrecking dedupe

iPhones, Apple Watches, AirPods, most Android phones, AirTags, modern fitness trackers — all use Resolvable Private Addresses (RPAs) that rotate every ~15 minutes by design. We cannot dedupe across rotation (we'd need the device's IRK private key). Same iPhone walking past will be logged 3-4 times per hour.

**Mitigation:** none clean. WiGLE backend tolerates this. Document in README. If users want fewer "duplicate" rows, they can raise `bleDedupeWindow` to 900 s (15 minutes) which matches typical RPA rotation period — same RPA will be deduped, but a new RPA will be a new row.

## WiGLE rate-limiting at higher row counts

WiGLE has documented quotas: 100 API calls/day for free accounts, ~25 for upload calls. Adding BLE means each CSV is 1.5-2.5× larger, but the number of API calls per day doesn't change. The risk is the CSV file size limit (currently 10 MB per upload).

**Mitigation:** the existing CSV rotation creates a new file per session. As long as a session generates < 10 MB, no change. If users start running 12-hour drives with `bleEnabled=true`, files can grow to 50+ MB.

Add a size-rotation rule: in `SDUtils.cpp::appendBleRow` and `appendWigleRow`, check `logFile.size() > 8 * 1024 * 1024` and rotate to a fresh file. Out of scope for v1; track as a follow-up.

## NimBLE callback context restrictions

`NimBLEScanCallbacks::onResult` runs in the NimBLE host task. Calls that are **not safe** from here:

- `Serial.print*` (takes the UART mutex which the loop task may hold — deadlock risk in pathological cases)
- `SD.open` / `File::write` (long-running, blocks the host task)
- `WiFi.*` (takes the Wi-Fi mutex — likely deadlock)
- `HTTPClient`, `WiFiClientSecure` (same)
- `Wire.beginTransmission` (OLED writes)
- Anything that takes more than ~50 µs

Calls that ARE safe:

- `std::deque::push_back`, `std::unordered_map::insert/find` (lock-free for our usage)
- `memcpy`, `strncpy`, `snprintf` (pure compute)
- `millis()`
- `heap_caps_*` allocation (mostly; PSRAM is safer than DRAM here)

Document in `BleScanner.cpp` near the `onResult` definition. Anyone adding a `Serial.printf("got %s", addr)` for debug will introduce intermittent hangs.

## Coex preference leaking

If `BleScanner::startScan()` calls `esp_coex_preference_set(ESP_COEX_PREFER_BT)` but the scan window times out via NimBLE's own timer before `tick()` notices and resets to `BALANCE`, the preference stays set. STA connections degrade.

**Mitigation:** add a watchdog in `tick()` that always restores `BALANCE` if `scanRunning_` is false. Already in the §6 sketch. Also add a max-duration safety:

```cpp
void BleScanner::tick() {
  // Watchdog: regardless of internal state, never let BT preference stay set
  // longer than 2× bleScanDuration in case NimBLE's timer firing missed our
  // state machine.
  static uint32_t btPreferredAtMs = 0;
  if (scanRunning_ && btPreferredAtMs == 0) btPreferredAtMs = millis();
  if (!scanRunning_ && btPreferredAtMs != 0) {
    esp_coex_preference_set(ESP_COEX_PREFER_BALANCE);
    btPreferredAtMs = 0;
  }
  if (btPreferredAtMs != 0 &&
      millis() - btPreferredAtMs > (uint32_t)cfg.bleScanDuration * 2 * 1000) {
    esp_coex_preference_set(ESP_COEX_PREFER_BALANCE);
    btPreferredAtMs = 0;
    NimBLEDevice::getScan()->stop();
    scanRunning_ = false;
    Serial.println("[BLE] watchdog: forced coex BALANCE");
  }
  // ... rest of tick
}
```

## PSRAM-required builds and accidental DRAM allocation

If the user builds without PSRAM (the README requires it, but some users dismiss the warning), `unordered_map` will use DRAM. With ~30 KB of dedupe ring + mbedtls's 16 KB session buffer + the WebServer's buffers, you can easily OOM during a TLS upload.

**Mitigation:**

```cpp
// At BleScanner::begin() entry:
if (!psramFound()) {
  Serial.println("[BLE] WARNING: no PSRAM detected. BLE + TLS uploads may OOM.");
  Serial.println("[BLE] Consider reducing bleMaxResults to 200.");
}
```

## `std::unordered_map` and heap fragmentation

`unordered_map` allocates a bucket array and per-node entries. Over a long run with many insert/erase cycles, the heap fragments. With PSRAM this is academic (lots of room); without PSRAM it bites.

**Mitigation:** measure free heap before and after a 4-hour run. If fragmentation is meaningful, switch to a fixed-size open-addressed hash table (linear probing, 2× capacity, no allocations after init). Out of scope for v1.

## Wi-Fi STA disconnect during long BLE windows

If `bleScanDuration` is set to the max (10 s) and `ESP_COEX_PREFER_BT` is set, the home Wi-Fi STA connection may drop because beacon misses accumulate. The user's WebUI session also stalls.

**Mitigation:** the 10 s cap. Also, in the WebUI, surface "BLE scanning…" while a window is active so users don't think the page froze.

## `NimBLEDevice::deinit()` race

If the user toggles `bleEnabled` at runtime (via the WebUI save+reboot flow), `NimBLEDevice::deinit()` is called before reboot. There's a known race in NimBLE 2.x where deinit during an active scan crashes (see h2zero/NimBLE-Arduino issues). The reboot masks it, but logs will show a wdt panic.

**Mitigation:** call `bleScanner.stopScan()` and wait 100 ms before `NimBLEDevice::deinit()`. Or just require a reboot for BLE enable/disable (which we already document in the config sample).

---

# 18. Acceptance

The feature is shippable when **all** of the following hold:

## Functional

- [ ] `bleEnabled=true` + reboot on all six target boards produces BLE rows in the CSV alongside Wi-Fi rows.
- [ ] WiGLE accepts mixed Wi-Fi+BLE CSVs (200 OK) and credits the user with BLE observations within 24 h of upload.
- [ ] WDGoWars accepts mixed CSVs (202 + done poll).
- [ ] BLE dedupe respects `bleDedupeWindow`.
- [ ] OLED Networks page shows correct BLE count.
- [ ] WebUI BLE card shows live `scanning`/`unique`/`dedupeSize` stats.
- [ ] Mesh Node with `bleEnabled` forwards BLE observations to a BLE-aware Core via `JCMK_MSG_BLE_OBS`.
- [ ] Legacy Cores (pre-feature firmware, Biscuit Pro) silently drop the new message type and continue handling Wi-Fi observations normally.

## Quality

- [ ] All unit tests pass — `make -C test test` reports ≥ 5 new BLE test cases, all green.
- [ ] On-device integration tests #1-#11 from §15 pass on at least XIAO ESP32-S3, XIAO ESP32-C5, and the T-Dongle C5.
- [ ] 8-hour stability test on the XIAO C5 (most-constrained target) — no crash, free-heap stddev < 5% of mean over the run.
- [ ] Wi-Fi RSSI regression test #4 within ±2 dB mean / ±1 dB stddev.

## Code health

- [ ] No file > 1000 LOC after the change (BLE additions distributed cleanly; no dumping ground).
- [ ] `BleScanner.cpp` has the documented "do not call X/Y/Z from `onResult`" comment near the callback.
- [ ] `jcmk_ble_obs_msg_t` has `static_assert(sizeof == 212)`.
- [ ] PROTOCOL.md documents the new message type with byte-offset table.
- [ ] CHANGELOG entry tying the feature to `FIRMWARE_VERSION` bump.
- [ ] README "Supported features" section lists BLE wardriving with the config key reference.

## Compatibility

- [ ] Build is green with `bleEnabled` both true and false on all six targets.
- [ ] Flash usage delta with `bleEnabled=true`: < 130 KB on all targets.
- [ ] Pre-existing Wi-Fi wardriving behaviour is byte-identical when `bleEnabled=false` (verify via CSV diff against a pre-feature firmware run on the same route).
- [ ] `wardriver.cfg` files from v2.52 load without warnings on the new firmware (new fields take defaults).

---

## Appendix A — Key file touch list (for the maintainer's `git diff --stat` mental model)

| PR | Files changed | Files added |
|---|---|---|
| PR1 | `Arduino Files/Piglet/Config.h`, `Config.cpp`, `wardriver.cfg`, `WebUI.cpp` | (none) |
| PR2 | `Piglet.ino`, `Display.cpp`, `platformio.ini` | `Arduino Files/Piglet/BleScanner.h`, `BleScanner.cpp` |
| PR3 | `SDUtils.h`, `SDUtils.cpp`, `Piglet.ino` | (none) |
| PR4 | `MeshNode.h`, `MeshNode.cpp`, `PigletNode/PigletNode.ino` | `docs/PROTOCOL.md` (if not already added) |
| PR5 | `WebUI.cpp`, `Display.cpp`, `README.md`, `CHANGELOG.md` | (none) |

Cumulative: 11 files changed, 3 files added. Estimated total diff: ~1500 lines added, ~50 lines modified.

## Appendix B — Estimated implementation time

| Phase | Estimate | Notes |
|---|---|---|
| PR1 | 0.5 day | Config plumbing, sample cfg, WebUI form. Mostly mechanical. |
| PR2 | 3-4 days | NimBLE integration, dedupe data structures, scheduler integration. Most failure modes (coex tuning, PSRAM placement, memory limits) live here. |
| PR3 | 1-2 days | CSV plumbing is small; the WiGLE round-trip test is the slow part (24h credit lag). |
| PR4 | 2-3 days | Packet struct design, sender/receiver, PROTOCOL.md, backward-compat testing with a legacy Core. |
| PR5 | 1 day | UI polish, docs. |
| Total | **2-3 weeks part-time** (~8-12 engineer-days). Calendar: 3-4 weeks accounting for hardware testing and WiGLE/WDGoWars upload-credit verification. |

---

*End of document.*
