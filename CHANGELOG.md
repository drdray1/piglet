# Changelog

## Unreleased

### BLE wardriving on the T-Dongle C5

The T-Dongle previously logged only the BLE observations its mesh nodes
forwarded — it had no scanner of its own, so a standalone dongle collected zero
BLE. It now scans locally, opt-in via `bleEnabled` exactly like the XIAO.

- **Passive observer** implemented inline in the sketch (single-file, so it
  can't use `BleScanner.cpp`), mirroring the XIAO's `BleScanner` and
  PigletNode's inline scanner: NimBLE 2.x, observer-only, `setActiveScan(false)`
  so it never transmits, 100 ms interval / 60 ms window, coex biased to BLE for
  the duration of a window and back to balanced after.
- **Standalone:** BLE windows are time-sliced against the Wi-Fi sweep — a window
  never opens mid-sweep and the sweep is held off while one is active, since an
  active Wi-Fi scan starves BLE of coex airtime. Observations are GPS-stamped
  via `captureGpsFix()` and written through the existing `appendBleRow()`, so
  they get the same blacklist, CSV rotation and writer as Wi-Fi rows.
- **Node mode:** observations are forwarded to the Core as 212-byte type-6
  frames (`jcmkBleBuild`), returning the radio to the admin channel before
  sending. Honors the Core-assigned role — a `wifi` node never opens a BLE
  window, a `ble` node never runs the channel sweep.
- **Core mode does not scan BLE locally**, matching the XIAO: a Core logs what
  its nodes forward.
- New config keys `bleEnabled` (default `false`), `bleScanDuration` (5 s),
  `bleScanInterval` (30 s); `bleDedupeWindow` is parsed and ignored for
  compatibility with older config files. Added a `validateConfig()` that clamps
  the cross-field constraints on load, as the XIAO does.
- `BleDedupe.h` copied into `TDongleC5_Piglet/` — the third copy of that header;
  all three now cross-reference each other.
- TFT: a BLE count row on the status page (only rendered when `bleEnabled`, so
  the layout is untouched otherwise) and a forwarded-BLE tally on the mesh node
  page. `status.json` gains `foundBle` and `bleEnabled`.

**Protocol fix found while wiring roles:** the T-Dongle's `jcmk_admin_msg_t` was
the legacy 10-byte struct with no trailing `role` byte, so as a Core it never
told nodes their role, and as a node it never learned its own. It is now the
11-byte frame that `docs/PROTOCOL.md` specifies and that the XIAO and PigletNode
already used, with the documented length guards: channels applied at length ≥ 10,
`role` read only at length ≥ 11. Third-party nodes read their own struct size and
ignore the trailing byte, so nothing breaks in either direction.

### T-Dongle C5 parity with upstream v2.57–v2.58

- **`autoStartAfterUpload` added to the T-Dongle firmware** — the option shipped
  upstream in v2.57 for the XIAO sketch only. Same semantics: once boot uploads
  finish, the STA link is dropped and wardriving starts immediately instead of
  waiting for the connection to time out. Skipped when `meshModeOnBoot` is set
  (mesh tears STA down itself). Settable from the web UI Config panel, from
  `/wardriver.cfg`, or via `POST /saveConfig`, and reported in `status.json`.
  Defaults to `false` — no behaviour change unless enabled.
- **`dedupEnabled` / `bleMaxResults` added to the T-Dongle firmware** — log-once
  dedup was previously hard-wired on with a fixed 200-entry ring, so unlike the
  XIAO there was no way to turn it off or resize it. Both rings are now gated:
  the CSV write path and the node-side forwarding ring (the node must not swallow
  repeats before they reach the Core when dedup is off). SD-config-only, matching
  the XIAO, which exposes neither in its web UI. Defaults (`true` / `200`)
  reproduce the previous behaviour exactly.
- **GPS last-known-position cache now covers every log path.** Upstream's v2.58
  cache was applied only to the Wi-Fi scan path, so Core-logged rows —
  `coreParseAndLogText()` (mesh-forwarded Wi-Fi) and `coreLogBleObs()`
  (mesh-forwarded BLE) — still wrote 0,0 whenever the fix dropped. All three now
  route through a shared `captureGpsFix()`, matching the XIAO firmware. Also
  picks up upstream's `isValid()` guards on altitude/HDOP.
- `GpsFix` moved into `TDongleC5_Piglet/GpsFix.h`: declaring it in the `.ino`
  body breaks the Arduino auto-prototype pass ("'GpsFix' does not name a type").

All three sketches verified to compile against ESP32 core 3.3.10
(PigletNode needs a `huge_app` partition scheme; it overflows the default).

Remaining T-Dongle differences from the XIAO firmware, all deliberate:
`battPin`/`batteryTest` (no battery on the dongle), `board` (fixed pinmap), and
the `ble*` scan keys — the T-Dongle logs mesh-forwarded BLE from nodes but has
no local BLE scanner of its own, so `bleEnabled` and friends have nothing to
drive. Porting the scanner is a separate piece of work.

### BLE wardriving (opt-in via `bleEnabled`)

Passive Bluetooth LE wardriving alongside Wi-Fi, solo and across a mesh cluster.
With `bleEnabled=false` (default) there is no behavioural or memory change, and
no NimBLE dependency.

**CSV / format**
- WiGLE-1.6 BLE rows: `appendBleRow()` emits `Type=BLE` into the same log as
  Wi-Fi — channel→frequency (37/38/39 → 2402/2426/2480),
  `[LE Public|Random|Resolvable|NonResolvable]` AuthMode, service UUIDs in RCOIs,
  LE-decoded company id in MfgrId. Pure, host-tested `BleCsv.h`; Wi-Fi and BLE
  writers share one `writeCsvLine()` flush/recovery path. Uploads to WiGLE and
  WDGoWars (and the merged-gzip download) carry BLE rows unchanged.

**Scanning**
- `BleScanner` (NimBLE-Arduino 2.x, observer-only, passive) with a host-tested
  dedupe ring (`BleDedupe.h`) and a bounded hand-off FIFO. Coex set to prefer BLE
  during a window. Solo mode time-slices BLE windows with the Wi-Fi sweep.
- Config keys (persisted in `wardriver.cfg`): `bleEnabled`, `bleScanDuration`,
  `bleScanInterval`, `bleDedupeWindow`, `bleMaxResults`; `validateConfig()` clamps
  cross-field constraints on load.

**Mesh cluster**
- New ESP-Now message type 6 (`JCMK_MSG_BLE_OBS`, 212-byte frame): Nodes scan
  BLE, dedupe, and forward to the single Core, which logs them with its own GPS —
  mirroring the Wi-Fi mesh flow. Legacy/third-party Cores (Biscuit Pro, JCMK C5)
  drop type-6 cleanly; types 1–5 and the Wi-Fi `TEXT` format are unchanged.
- OLED shows BLE tallies on the mesh page (Core `BLE:`, Node `B:`).

**Tests / docs**
- Host suite: `test_ble_csv`, `test_ble_dedupe`, `test_jcmk_ble` (row layout,
  dedupe semantics, 212-byte mesh round-trip).
- `LIBRARIES.md` (NimBLE install), `docs/PROTOCOL.md` (frozen type-6 layout).

On-hardware validation still pending, especially node-mode BLE/ESP-Now
coexistence (the tightest radio scenario).

## v2.58 (2026-07-23)

### Bug Fixes
- **GPS stale/incorrect coordinates** — Fixed a bug where the last-known position cache only updated when scan results were processed *and* networks were found. Driving through an area with no networks froze the cached position; when the fix was later lost those stale (potentially distant) coordinates were written to CSV. The cache now updates every loop iteration, decoupled from scan processing.
- **GPS bad-fix cache poisoning** — Low-quality re-acquisitions (e.g. brief fixes emerging from a tunnel or under heavy tree cover) no longer overwrite a good cached position. A quality gate now requires HDOP ≤†10 and ≥ 3 satellites before accepting a location into the cache.
- **GPS cache expiry** — Cached positions older than 3 minutes are discarded rather than used indefinitely. Networks logged after a 3-minute GPS outage correctly appear at 0,0 (null island) instead of an arbitrarily stale location.

### New Features
- **XIAO ESP32-C3 board support** — The main Piglet firmware now supports the Seeed XIAO ESP32-C3. Set `board=c3` in `/wardriver.cfg` or select **XIAO C3** in the Web UI Board dropdown; also auto-detected from the chip model string at boot. 2.4 GHz only; optional I2C OLED on D4/D5; no dedicated button (GPIO9 conflicts with SPI MISO — wire one externally to any free GPIO if needed).

### Improvements
- **GPS boot wiring check** — After `GPSSerial.begin()`, firmware waits 2 s and reports whether any data arrived: `chars=0` means RX is not connected to GPS TX; checksum errors indicate a baud-rate mismatch. Applied to both main and T-Dongle C5 firmware.
- **GPS 10-second health log** — While waiting for a GPS fix, a diagnostic line prints every 10 s showing chars processed, checksum pass/fail counts, and satellite count — immediately distinguishes no-data (wiring) from data-but-no-fix (sky view) situations.
- **GPS RX buffer increased to 512 bytes** — Prevents UART overflow during WiFi scan blocking windows at 9600 baud.

### T-Dongle C5
- All GPS fixes above applied to the T-Dongle C5 standalone firmware.

---

## v2.57 (2026-06-24)

### New Features
- **Auto-Start Wardriving After Uploads** (`autoStartAfterUpload`): new config option that disconnects from home Wi-Fi immediately after boot uploads complete and begins scanning without delay. Previously the device held the STA connection open, which paused scanning until the link dropped naturally. Configurable via web UI or `wardriver.cfg`. Disabled by default.

  > **Note:** Once enabled, the web UI is not reachable on the home network after boot (device disconnects immediately after uploading). To disable it, either power on away from the home network so the Wardriver AP broadcasts — connect to it and visit `http://192.168.4.1` — or remove the SD card and set `autoStartAfterUpload=false` in `wardriver.cfg` directly.

### Bug Fixes
- **Mesh node mode on S3 / C6**: nodes no longer attempt to scan 5 GHz channels (36–177) on 2.4 GHz-only hardware. Previously those scan attempts failed silently and wasted ~80 ms each per cycle; the node now skips channels > 14 when not running on a C5.

### T-Dongle C5
- Synced mesh WiFi init fix: `enterCoreMode()` and `enterNodeMode()` now use `WiFi.mode(WIFI_OFF) → WIFI_STA` (full deinit/reinit) instead of `WiFi.disconnect`. Matches the XIAO fix that restored Core/Node connectivity.
- Added `[CORE] RX CORE_REQUEST` diagnostic print in `jcmkOnRecv` and channel-verification prints in both enter functions.

---

## v2.52 (2026-05-28)

Summary entry — the per-release changelog between v1.3-beta and v2.52
was not maintained inline. See git history (`git log v1.3-beta..HEAD`)
for the full per-commit record. Headline changes that shipped in this
range:

- WatchdogsGoWars upload integration alongside WiGLE
- ESP-Now mesh mode (Core / Node) with `meshModeOnBoot` auto-start
- T-Dongle C5 board support and pinmap detection (`board=` config)
- Multi-board pin mapping (XIAO S3, C5, C6, S3 + Expansion Base)
- 5 GHz scanning on C5
- Screen rotation (`rotateScreen180`)
- Device naming in CSV filename + WiGLE header (`deviceName`)
- Battery test logging (`batteryTest`)
- Plain-text `/wardriver.cfg` replaces JSON
- T-Dongle C5: empty-CSV bug fix, SD write-failure recovery, post-batch flush (v2.52)

(Historical entries between v1.3-beta and v2.52 not captured here.)

---

## v1.3-beta (2026-02-23)

### New Features
- **WiGLE Upload History Tracking**: Web UI now displays upload statistics (new networks discovered, total networks) for uploaded files
- **Automatic Boot Upload with Quota Management**: Configurable `maxBootUploads` setting (default: 25) to control how many files upload automatically at boot
- **24-Hour History Caching**: Upload history API calls are cached for 24 hours to conserve WiGLE API quota (25 calls/day limit)
- **On-Demand History Refresh**: History automatically refreshes in web UI when cache expires (only when connected to home network)

### Improvements
- **Optimized Upload Performance**: Removed token pre-checks and reduced timeouts for faster batch uploads
- **Enhanced WiFi Stability**: Scanning now properly pauses when connected to home network to prevent connection drops
- **Web Server Startup Timing**: Web server now starts after WiGLE operations complete to avoid resource conflicts
- **Improved Configuration Management**: Added `maxBootUploads` and `speedUnits` configuration options
- **Better Status Display**: Config form in web UI now properly displays all saved values including WiGLE token

### Bug Fixes
- Fixed scanning interference causing 100% ping loss when connected to home WiFi
- Fixed web UI configuration display issues (all fields now populate correctly)
- Fixed chunked encoding errors in `/status.json` and `/files.json` endpoints
- Corrected WiGLE token display (now shows actual token instead of "(set)")
- Fixed JSON buffer overflow issues in files endpoint

### Technical Changes
- Increased JSON buffer for files endpoint from 4KB to 8KB to handle upload statistics
- Switched from HTTP/1.1 to HTTP/1.0 for WiGLE API compatibility
- Added proper `client.flush()` to ensure complete data transmission
- Reduced upload timeout from 60s to 25s for better reliability
- History parsing now uses incremental JSON parsing to reduce memory fragmentation

### Configuration
- New config option: `maxBootUploads` - Max CSV files to upload at boot (0-25, default: 25)
- Updated config option: `speedUnits` - Display speed in km/h or mph
- Config file now saves `maxBootUploads` setting to `/wardriver.cfg`

### Requirements
- **CRITICAL**: PSRAM must be enabled in Arduino IDE for reliable TLS/HTTPS uploads
  - ESP32-C5/C6: Use OPI PSRAM
  - ESP32-S3: Use QSPI PSRAM
- Arduino-ESP32 core v3.0.0 or later
- Updated library dependencies documented in README

### Known Issues
- ESP32-C5/C6 require PSRAM enabled or TLS connections will fail due to insufficient heap
- Initial boot may show "Failed to allocate dummy cacheline for PSRAM" warning (can be ignored)

### Migration Notes
- No breaking changes from v1.2
- Existing `/wardriver.cfg` files are compatible
- New `maxBootUploads` setting will default to 25 if not present in config

---

## v1.2 (Previous Release)
- Initial stable release with basic wardriving functionality
- SD card CSV logging
- Web UI for file management
- Manual WiGLE upload support
