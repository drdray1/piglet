# Changelog

## Unreleased

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
