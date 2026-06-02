# Piglet — Improvements Deep Dive

*Technical scoping for the maintainer*

- **Firmware version at scope:** `v2.52`
- **Document date:** 2026-05-27 *(status updates: 2026-05-28)*
- **Audience:** piglet maintainer (senior Elixir/Phoenix engineer with C++/embedded fluency).

> **Status — in flight on branch `improvements/mask-tokens-dedupe-single-pass`** ([PR #1](https://github.com/drdray1/piglet/pull/1), currently open). Items **1.2**, **4.5**, **6.2** are implemented in commit `7ca9179` ("Mask upload tokens, dedupe authModeToString, single-pass /logs walk"). A host-side doctest harness covering `authModeToString` and the new `maskedField` helper landed in commit `ce1935a`, partially closing item **3.1**. See per-item callouts below.

## Scope

Piglet is an Arduino-C++ ESP32 wardriving firmware in four sketches: the canonical multi-file `Arduino Files/Piglet/` (24 files, ~6.3 KLOC), `TDongleC5_Piglet/` (single-file, 3.5 KLOC) for the LilyGo T-Dongle C5, `PigletNode/` (single-file, 519 lines) for a standalone XIAO ESP32-C5 mesh node, and `waveshareDisplayMiniPiglet/` (single-file, 2.6 KLOC) for the Waveshare ESP32-C6 LCD 1.47" board. This document deep-dives each improvement identified in the earlier punch list, in the same nine categories, then scopes the addition of BLE wardriving as a major new feature.

Each item is structured as **Current state / Proposed change / Trade-offs / Acceptance**. Code sketches show the shape of the proposed API only — not full implementations. Final section sequences the work into waves with hard dependency notes.

Reference convention: file paths are relative to the repo root. Line numbers are against the firmware as of `v2.52` (the value at `Globals.h:2`). When code samples reference future modules (e.g. `piglet::` namespace), assume the extraction in improvement 1.1 has happened.

## At-a-glance summary

| # | Title | Category | Effort | Status |
|---|---|---|---|---|
| 1.1 | Extract shared piglet-core library | Code quality | L | Open |
| 1.2 | Deduplicate authModeToString | Code quality | S | ✅ Done — PR #1 (`7ca9179`) |
| 1.3 | Unify uploadAllCsvsToWigle and uploadAllCsvsToWdgwars | Code quality | M | Open |
| 1.4 | Extract a tiny HTTP helper for the uploader | Code quality | M | Open |
| 1.5 | Replace string-typed config with enums | Code quality | S | Open |
| 1.6 | Move the 1300-line HTML blob out of WebUI.cpp | Code quality | M | Open |
| 1.7 | Decompose Piglet.ino setup() | Code quality | M | Open |
| 2.1 | Split the cooperative loop into FreeRTOS tasks | Architecture | L | Open |
| 2.2 | Decompose the Globals god-object | Architecture | M | Open |
| 2.3 | Split MeshNode.cpp into Node + Core + Protocol | Architecture | M | Open |
| 2.4 | Board support via a single interface | Architecture | M | Open |
| 3.1 | Add host-side unit tests + Arduino-CLI compile CI | Testing | L | 🟡 Partial — doctest harness landed PR #1 (`ce1935a`); CI matrix still open |
| 3.2 | Test channel classification edge cases | Testing | S | Open |
| 4.1 | TLS keep-alive across uploads | Performance | L | Open |
| 4.2 | Replace String concatenation in appendWigleRow | Performance | M | Open |
| 4.3 | Reuse JsonDocument for /status.json | Performance | M | Open |
| 4.4 | Combine csvHasDataRows with the upload open | Performance | S | Open |
| 4.5 | Single-pass log directory scan | Performance | S | ✅ Done — PR #1 (`7ca9179`) |
| 5.1 | Severity-tagged logging with compile-time filter | Observability | M | Open |
| 5.2 | Persistent error counters in /status.json | Observability | S | Open |
| 5.3 | In-memory log ring buffer at /logs.txt | Observability | M | Open |
| 6.1 | Authenticate WebUI POST routes | Security | M | Open |
| 6.2 | Stop echoing WiGLE/WDGoWars tokens in /status.json | Security | S | ✅ Done — PR #1 (`7ca9179`) |
| 6.3 | Pin WiGLE / WDGoWars CA certs | Security | M | Open |
| 6.4 | Apply isAllowedDataPath to /wigle/upload | Security | S | Open |
| 6.5 | CSRF protection on POST routes | Security | S | Open |
| 7.1 | Adopt PlatformIO with pinned envs | DX | M | Open |
| 7.2 | Reduce Globals.h include footprint | DX | S | Open |
| 7.3 | Add .clang-format + pre-commit hook | DX | S | Open |
| 7.4 | Generate compile_commands.json for clangd | DX | S | Open |
| 8.1 | Reconcile README + CHANGELOG with FIRMWARE_VERSION | Documentation | S | Open |
| 8.2 | Add docs/PROTOCOL.md for JCMK wire format | Documentation | S | Open |
| 8.3 | Module-level comments in the multi-file sketch | Documentation | S | Open |
| 8.4 | Bring sample wardriver.cfg in line with the code | Documentation | S | Open |
| 9.1 | Pin all library versions | Dependencies | M | Open |
| 9.2 | Pin arduino-esp32 core version | Dependencies | S | Open |
| 9.3 | Migrate to ArduinoJson v7 JsonDocument | Dependencies | S | Open |
| 9.4 | Delete the legacy wardriver.json import path | Dependencies | S | Open |

---

# 1. Code quality / refactoring

## 1.1 Extract shared piglet-core library

*Category: Code quality / refactoring  ·  Effort: L*

**Current state.** Three single-file forks of the same firmware. `TDongleC5_Piglet/TDongleC5_Piglet.ino` (3565 LOC) and `waveshareDisplayMiniPiglet/waveshareDisplayMiniPiglet.ino` (2589 LOC) reimplement, end-to-end: the WiGLE upload state machine, the CSV writer (`WigleWifi-1.6` header, channel-to-frequency table, row formatting), the JCMK mesh protocol (`JCMK_MAGIC`, `JCMK_CHANNELS`, `jcmk_*_msg_t` packed structs), `WiFiClientSecure` handshake + `setInsecure()` boilerplate, the DNS-repair logic, and big slices of the HTML/JS UI. Concrete duplication points: `JCMK_MAGIC` defined in `Arduino Files/Piglet/MeshNode.cpp:11`, `TDongleC5_Piglet.ino:1743`, and `PigletNode/PigletNode.ino:44` — same bytes, three places. `JCMK_CHANNELS` table at `MeshNode.cpp:60` and `TDongleC5_Piglet.ino:1791`. `FIRMWARE_VERSION "v2.52"` macro at `Globals.h:2` and `TDongleC5_Piglet.ino:43`. Drift between forks is not theoretical: the T-Dongle sketch has 52 hits for the JCMK/wigle constants, the waveshare sketch has 14, the canonical firmware has 18+21. Every time you change the WiGLE row format you must remember to touch all three.

**Proposed change.** Pull a `lib/piglet-core/` out and have all four sketches `#include` it. The library is platform-neutral C++ that depends only on Arduino.h, `WiFiClientSecure`, `SD.h`, `TinyGPSPlus`, `ArduinoJson`, and the ESP-IDF `esp_now.h` / `esp_wifi.h` headers (no display, no board pin maps). Public surface:

```cpp
// lib/piglet-core/include/piglet/wigle_csv.h
namespace piglet {
struct WigleRow {
  const char* mac; const char* ssid; const char* auth;
  const char* firstSeen; int channel; int rssi;
  double lat, lon, altM, accM;
  enum class Type { WIFI, BLE } type = Type::WIFI;
  const char* mfgrId = "";   // BLE only
  const char* rcois  = "";   // BLE only
};
bool writeHeader(Stream& out, const char* fwVersion,
                 const char* board, const char* deviceField);
bool writeRow(Stream& out, const WigleRow& r);
uint32_t channelToFreqMHz(int channel, WigleRow::Type t);
}

// lib/piglet-core/include/piglet/jcmk.h
namespace piglet::jcmk {
constexpr uint8_t MAGIC[4] = {'E','N','O','W'};
constexpr uint8_t ESPNOW_CH = 6;
extern const uint8_t CHANNELS[40];
constexpr uint8_t NUM_CHANNELS = 40;
struct __attribute__((packed)) TextMsg { /* 212 bytes */ };
struct __attribute__((packed)) AdminMsg { /* 11 bytes */ };
// Builders so callers don't memcpy MAGIC themselves:
void buildHeartbeat(TextMsg& m, uint32_t counter);
void buildText(TextMsg& m, const char* s, uint32_t counter);
}

// lib/piglet-core/include/piglet/uploader.h
namespace piglet {
struct UploadTarget {
  const char* host; uint16_t port;
  const char* path;                   // POST endpoint
  void (*addAuth)(WiFiClientSecure&, const String& token);
  // Async post-upload: poll job status (WDGoWars). Optional.
  bool (*pollJob)(WiFiClientSecure&, int jobId, String& out);
};
struct UploadResult { int httpCode; String message; bool ok; };
UploadResult postCsv(const UploadTarget& tgt, const String& token,
                     const String& path, ProgressFn = nullptr);
}
```

**Trade-offs / risks.** Library-ifying Arduino sketches has a real cost: you must adopt either an Arduino library directory (`library.properties` + `src/`) or PlatformIO `lib_deps`. The Arduino IDE 2.x discovery only finds libraries in `~/Arduino/libraries/`, which means contributors clone the repo and then manually copy or symlink — friction. A PlatformIO `lib_extra_dirs` pointer in each sketch folder is cleaner but assumes the maintainer is willing to move off Arduino IDE 2.x (see DX section). Second risk: the standalone `PigletNode.ino` advertises "zero external library dependencies" in the README — if you extract `jcmk.h`, you break that property unless you also keep a single-header amalgamation that PigletNode can ship vendored.

**Acceptance.** All four sketches compile against the new `lib/piglet-core/`. Grep for `JCMK_MAGIC`, `WigleWifi-1.6`, `FIRMWARE_VERSION` returns one definition each. CI build matrix (xiao-s3, xiao-c5, xiao-c6, t-dongle-c5, waveshare-c6, pigletnode) all green. A deliberate edit to `writeRow` (e.g. adding a new column) takes effect on every sketch with no per-sketch follow-up.

## 1.2 Deduplicate authModeToString

*Category: Code quality / refactoring  ·  Effort: S*

> **✅ Done — PR #1, commit `7ca9179`.** `authModeToString` is now declared in `Scanner.h` and defined once in `Scanner.cpp`; the duplicate definition is removed from `MeshNode.cpp`. Both translation units still build. Covered by the `test/test_scanner.cpp` cases in commit `ce1935a`. Deferred work: the function still returns `String` (heap-allocating per row) — the `const char*` migration noted under Trade-offs has not happened yet, and is a clean follow-up.

**Current state** *(pre-PR #1)*. Identical 8-case `wifi_auth_mode_t` → `String` switch at `Scanner.cpp:7-19` and `MeshNode.cpp:126-138`. Both branches handle the same set: OPEN/WEP/WPA/WPA2/WPAWPA2/WPA2EAP/WPA3/WPA2WPA3. Anyone adding WPA3-Enterprise or OWE has to remember both places.

**Proposed change.** Move it next to the WiGLE writer (`piglet::wifiAuthString(wifi_auth_mode_t)`) as part of `piglet-core` (or, pre-extraction, lift it into `SDUtils.h` since both call sites already include it).

```cpp
// piglet/wifi_auth.h
namespace piglet {
const char* wifiAuthString(wifi_auth_mode_t m);  // returns const char*, no String alloc
}
```

**Trade-offs / risks.** Returning `const char*` (over the existing `String`) saves a heap allocation per scanned network — `appendWigleRow` already does `String + String + String` for every BSSID. The downside is callers that did `auth + ","` need a tiny adjustment. Zero behavioural risk if you keep the spelling.

**Acceptance.** Single definition. `grep -n authModeToString` returns one header + one .cpp. Per-scan heap allocation count drops by N (one per row).

## 1.3 Unify uploadAllCsvsToWigle and uploadAllCsvsToWdgwars

*Category: Code quality / refactoring  ·  Effort: M*

**Current state.** `WigleUpload.cpp:642-775` and `WigleUpload.cpp:780-868` are ~90% copy-paste. Both walk `/logs` twice (once to count, once to collect), both pause scanning with `uploadPausedScanWasEnabled`, both update `uploading`/`uploadTargetName`/`updateOLED(0)`, both have the same `csvHasDataRows` guard, the same per-file `delay(1500)`, and the same end-of-batch teardown. The only difference is which `uploadFileTo*()` they call. Adding a third destination today is a 130-line clone.

**Proposed change.** Express each destination as an `UploadTarget` (see 1.1 sketch) and write a single batch driver that takes a list of targets. The boot-time "WDGoWars then WiGLE" sequence becomes ordering of the targets vector; the WebUI "WDGW only" button becomes a 1-element vector. Move the SD walk / pause-scan / OLED hooks into the driver:

```cpp
uint32_t piglet::uploadBatch(std::initializer_list<const UploadTarget*> targets,
                             int maxFiles, ProgressFn progress) {
  auto paths = collectCsvs("/logs", maxFiles, currentCsvPath);
  ScanGuard g;  // RAII: pauses scanning, restores on dtor

  uint32_t ok = 0;
  for (auto& path : paths) {
    if (!csvHasDataRows(path)) { SD.remove(path); continue; }
    bool anyOk = false;
    for (auto* tgt : targets) {
      uploadTargetName = tgt->label;
      progress(path, tgt);
      auto r = postCsv(*tgt, tgt->getToken(), path);
      anyOk = anyOk || r.ok;
      delay(1500);
    }
    if (anyOk) { moveToUploaded(path); ok++; }
    delay(2000);
  }
  return ok;
}
```

**Trade-offs / risks.** The current code is forgiving of one-off behaviour differences (e.g. WDGoWars' 45 s job polling versus WiGLE's synchronous 200). Keep that asymmetry by letting `UploadTarget::pollJob` be optional and have `postCsv` call it after a 202. The risk is mostly readability: a strategy table can be harder to debug than two long functions side-by-side. Mitigate with concrete `Target` constants in a single file you can read top-to-bottom (`uploaders/wigle.cpp`, `uploaders/wdgwars.cpp`).

**Acceptance.** Both `uploadAllCsvsToWigle` and `uploadAllCsvsToWdgwars` collapse to one-line wrappers around `uploadBatch`. Adding a third destination is one struct literal + one route handler.

## 1.4 Extract a tiny HTTP helper for the uploader

*Category: Code quality / refactoring  ·  Effort: M*

**Current state.** Hand-rolled HTTP request construction in five places: `wigleTestToken` (`WigleUpload.cpp:13`), `uploadFileToWigle` (line 137), `wdgwarsTestKey` (line 283), `uploadFileToWdgwars` (line 373), and the WDGoWars job poll loop (line 542). Each repeats: TLS connect with 3-attempt retry, write status line/headers via repeated `client.print(String(...) + ...)`, scan for `"HTTP/"`, parse the status code with manual `indexOf`, drain headers in an ad-hoc loop. The job-poll inner block at lines 542-565 silently discards the HTTP status line entirely (`pc.readStringUntil('\n');  // skip HTTP status line`) — a real bug if the server starts returning 5xx.

**Proposed change.** One helper inside `piglet-core` that returns a struct, with a callback for streaming the body so file upload doesn't materialise the full POST in memory:

```cpp
struct HttpReply { int status; String headers; String body; };

HttpReply piglet::httpsRequest(
  const char* host, uint16_t port,
  const char* method, const char* path,
  std::initializer_list<std::pair<const char*, String>> headers,
  std::function<void(WiFiClient&)> writeBody = nullptr,   // null for GET
  uint32_t timeoutMs = 25000,
  uint8_t  retries = 3
);

// Usage:
auto r = httpsRequest("api.wigle.net", 443, "POST", "/api/v2/file/upload",
  {{"Authorization", "Basic " + cfg.wigleBasicToken},
   {"Content-Type",  "multipart/form-data; boundary=" + boundary}},
  [&](WiFiClient& c) { streamMultipartFile(c, path, boundary); });

if (r.status == 200) { ... }
```

**Trade-offs / risks.** A streaming-body callback is a slightly fancy API for embedded code but it preserves the current zero-copy file upload. Avoid `std::function` if you care about flash (it pulls in `<functional>` virtual-dispatch machinery — ~2-3 KB) — a templated `httpsRequest<BodyFn>` keeps it allocation-free. Risk: small TLS-stack regressions if you accidentally change the request-write ordering (the existing code carefully `print`s before flushing; preserve that). Validate with a real WiGLE upload before tagging the next firmware.

**Acceptance.** Five upload functions reduced to direct `httpsRequest()` calls plus their specific body builder. The WDGoWars poll loop reads the status line, and a non-2xx response surfaces in `uploadLastResult`.

## 1.5 Replace string-typed config with enums

*Category: Code quality / refactoring  ·  Effort: S*

**Current state.** Config values are compared as strings everywhere: `cfg.scanMode == "aggressive"` (`Scanner.cpp:79`), `cfg.board == "c5"` (`Config.cpp:23`, `SDUtils.cpp:197`, `Globals.cpp:?`, `Display.cpp`, `Piglet.ino`), `cfg.meshModeOnBoot == "node"` (`Piglet.ino:387, 485`), `cfg.speedUnits == "mph"` (`Piglet.ino:561`). Each comparison call site has its own `toLowerCase()` ritual.

**Proposed change.** Promote to enums in `Config.h`, parse once at load time, write back the canonical lowercase string at save time.

```cpp
enum class ScanMode  : uint8_t { Aggressive, PowerSaving };
enum class BoardKind : uint8_t { Auto, S3, S3Exp, C5, C6, TDongleC5, WaveshareC6 };
enum class MeshBoot  : uint8_t { None, Core, Node };
enum class SpeedUnit : uint8_t { Kmh, Mph };

struct Config {
  ScanMode  scanMode      = ScanMode::Aggressive;
  BoardKind board         = BoardKind::Auto;
  MeshBoot  meshModeOnBoot = MeshBoot::None;
  SpeedUnit speedUnits    = SpeedUnit::Mph;
  // ...
};

bool parseScanMode(const String&, ScanMode& out);
const char* scanModeName(ScanMode);
```

**Trade-offs / risks.** Touches every read site; mechanical search-and-replace. The JSON in `/status.json` should keep emitting the string form so the JS UI doesn't change. One real risk is the legacy `/wardriver.json` import path (`Config.cpp:170`) — exercise it before/after.

**Acceptance.** No string literal comparisons on config values outside `Config.cpp`. `/wardriver.cfg` round-trips identically. WebUI still renders human-readable strings.

## 1.6 Move the 1300-line HTML blob out of WebUI.cpp

*Category: Code quality / refactoring  ·  Effort: M*

**Current state.** `INDEX_HTML[] PROGMEM` runs from `WebUI.cpp:10` to `:769` — 759 lines of HTML/CSS/JS embedded as a raw string literal. The T-Dongle variant already moved its UI to `TDongleC5_Piglet/WebUI_HTML.h`, so the pattern exists. Editing the UI today means scrolling past hundreds of lines of CSS to reach the route handlers; syntax-highlighting in the IDE is broken because the whole blob is one C++ string.

**Proposed change.** Two-step. (a) Immediately: move the literal into `WebUI_HTML.h` exactly as the T-Dongle does — zero behaviour change, restores syntax highlighting and lets the page get version control diffs that don't span the entire C++ file. (b) Better: ship the page as a `.html.gz` baked into the firmware via `xxd -i` or PlatformIO's `embed_files`, served with `Content-Encoding: gzip` and a stable `ETag`. Same page, ~30 KB flash freed (gzip of the current page is ~6 KB versus ~30 KB raw), browser caches it correctly.

```cpp
// build step: tools/gen_webui.py
//   minify index.html -> index.min.html  (htmlmin)
//   gzip            -> index.html.gz
//   xxd -i index.html.gz > generated/index_html_gz.h

#include "generated/index_html_gz.h"  // extern const uint8_t index_html_gz[], size

server.on("/", HTTP_GET, []{
  server.sendHeader("Content-Encoding", "gzip");
  server.sendHeader("Cache-Control", "public, max-age=86400");
  server.sendHeader("ETag", FIRMWARE_VERSION);
  if (server.header("If-None-Match") == FIRMWARE_VERSION) { server.send(304); return; }
  server.send_P(200, "text/html", (const char*)index_html_gz, index_html_gz_len);
});
```

**Trade-offs / risks.** Adds a build step. PlatformIO has `extra_scripts` that handles this naturally; Arduino IDE does not — you'd commit the generated `_gz.h` so the IDE build still works (slightly ugly but workable). Browsers without gzip support (none in practice for the wardriving audience) wouldn't render. Validate the AP-mode case where the captive-portal heuristic in some OSes refuses cached responses.

**Acceptance.** `WebUI.cpp` is < 600 lines. `/` returns the same HTML byte-for-byte (after gunzip). Flash usage as reported by the linker drops by ~24 KB.

## 1.7 Decompose Piglet.ino setup()

*Category: Code quality / refactoring  ·  Effort: M*

**Current state.** `setup()` is 308 lines (`Piglet.ino:200-508`). The three-phase SD/pin bootstrap at lines 225-326 includes a `pinsChanged` diff computed inline, then repeated almost verbatim 30 lines later as `pinsChangedAgain`. The boot-time mesh decision is split across lines 380-410 (skip-STA logic) and lines 478-503 (enter-mode logic) with the same `cfg.meshModeOnBoot` string-parse done twice.

**Proposed change.** Split into named phases each returning a small struct.

```cpp
void setup() {
  Serial.begin(115200); delay(200);
  Boot::dumpResetReason();

  // Phase 1: Pins, SD, config — may reinit SD if config changes pins.
  BootEnv env = Boot::bringUpStorage();   // owns: PinMap, cfg, sdOk, cfgLoaded

  // Phase 2: Peripherals (BTN, OLED, GPS UART).
  Boot::bringUpPeripherals(env);

  // Phase 3: WiFi — STA + AP + boot upload, may be skipped for mesh.
  Boot::bringUpNetwork(env);

  // Phase 4: Logging + scheduled tasks.
  Boot::startScanLog(env);
  Boot::maybeEnterMesh(env);

  Serial.println("=== Boot complete ===");
}
```

**Trade-offs / risks.** Pure code-org change; risk is in getting the side-effects right (most of `setup()` mutates globals you can't yet box up). Do this AFTER 2.2 (`Globals` decomposition) or you'll have to do it twice.

**Acceptance.** `setup()` < 60 lines. No duplicated pin-diff blocks. `Boot::*` functions are unit-testable on host (none touch hardware directly — they take an `ITransport` for SD/Serial that you can mock).

---

# 2. Architecture

## 2.1 Split the cooperative loop into FreeRTOS tasks

*Category: Architecture  ·  Effort: L*

**Current state.** Everything runs on the Arduino loop task (8 KB stack, priority 1). `loop()` (`Piglet.ino:513-637`) interleaves: `server.handleClient()`, GPS UART drain, button polling, OLED refresh, scan FSM, mesh tick, battery test, and a 10 ms delay. `uploadFileToWdgwars` blocks for up to 45 s during job polling (`WigleUpload.cpp:540` is a synchronous `delay(3000)` in a 15-iteration loop). During that window the OLED freezes, the web server stops responding, and the GPS UART overruns (the SDK ring buffer is only 256 bytes; at 9600 baud you get ~2.7 s of grace, far less than 45 s). Comments throughout the code acknowledge this — `Piglet.ino:60` flushes the log file before sleep precisely because nothing else will.

**Proposed change.** Three pinned FreeRTOS tasks plus the existing loop, communicating via FreeRTOS queues. The ESP32 is dual-core (S3/C5/C6 are not — they're single-core RISC-V — see Trade-offs); even on single-core the priority-based preemption alone is a big win.

```cpp
// Core 0 (or single core, higher pri): real-time IO
xTaskCreatePinnedToCore(gpsTask,    "gps",  3072, NULL, 5, NULL, IO_CORE);
xTaskCreatePinnedToCore(sdLogTask,  "sd",   4096, NULL, 3, NULL, IO_CORE);

// Core 1: network
xTaskCreatePinnedToCore(httpTask,   "http", 6144, NULL, 2, NULL, NET_CORE);
xTaskCreatePinnedToCore(scanTask,   "scan", 4096, NULL, 4, NULL, NET_CORE);

// Loop stays — owns UI (web handlers run on loop, OLED refresh)

// Queues
QueueHandle_t qScanResult;   // Scanner --> sdLogTask  (struct ScanRow)
QueueHandle_t qUploadJob;    // WebUI   --> httpTask   (path, dest mask)
QueueHandle_t qOledMsg;      // any     --> loop       (transient status)
EventGroupHandle_t evState;  // wifi up, sd ok, uploading...

// httpTask body:
for (;;) {
  UploadJob j; xQueueReceive(qUploadJob, &j, portMAX_DELAY);
  uploadBatch(j.targets, j.maxFiles, [](auto& path, auto* tgt){
    UiUpdate u { path, tgt->label }; xQueueOverwrite(qOledMsg, &u);
  });
}
```

**Trade-offs / risks.** Three real concerns. (a) The XIAO C5/C6 are single-core RISC-V — `xTaskCreatePinnedToCore` still works (you pass `tskNO_AFFINITY` or core 0), but you don't get the load-balancing benefit you'd get on dual-core S3. The latency win still holds: a 45-s blocking poll on a lower-pri task can be preempted by GPS UART reads at priority 5. (b) `WiFiClientSecure` is not thread-safe — the Arduino-ESP32 wrapper uses TLS-state inside the client object. Confine all `WiFiClientSecure` use to `httpTask` only. (c) The `Adafruit_SSD1306` Wire transport is also not concurrency-safe; keep OLED writes on the loop. SD.h locking is undocumented — the safest pattern is one `sdLogTask` owner for writes; reads from the web server need a binary semaphore. Plan for a week of debugging hangs.

**Acceptance.** `/ping` responds in < 50 ms during an active upload. GPS fix count over a 10-minute drive matches the pre-change run within ±2%. Stack high-water-mark for each task reported and < 80% of allocation. No `delay()` > 200 ms left in `loop()`.

## 2.2 Decompose the Globals god-object

*Category: Architecture  ·  Effort: M*

**Current state.** `Globals.h` declares 30+ extern flags mutated from anywhere: `apForceClose`, `apExtended`, `apExtendedStartMs`, `userScanOverride`, `statusPagePaused`, `autoPaused`, `uploadPausedScanWasEnabled`, `uploadTotalFiles`, `uploadDoneFiles`, `uploadCurrentFile`, `wigleTokenStatus`, `wigleLastHttpCode`, etc. Cross-module pokes are the rule, not the exception — `WebUI.cpp:961` sets `apForceClose = true` and trusts `Piglet.ino:528`'s `stopAPIfAllowed()` to notice on the next loop iteration. The pause-precedence logic in `Piglet.ino:606-625` is a 20-line truth table that's only readable because all six booleans are visible at once.

**Proposed change.** Group by lifecycle and give each group an owner module that exposes a small API.

```cpp
// state/ap.h — owner: WiFiManager.cpp
struct ApState {
  bool active = false; bool clientSeen = false;
  bool extended = false; bool forceClose = false;
  uint32_t startMs = 0; uint32_t extendedStartMs = 0;
};
namespace Ap {
  const ApState& get();
  void requestClose();           // WebUI calls this
  void requestExtend();
  uint32_t remainingMs();        // for /status.json
  void tick();                   // called from loop, may close AP
}

// state/upload.h, state/scan.h similar
namespace Scan {
  bool isAllowed();   // single function replacing the 6-bool truth table
  void setUserOverride(bool enabled);
  void onPagePauseToggled();
}
```

**Trade-offs / risks.** Forces you to define ownership rules that are currently implicit. You'll discover one or two real bugs (the `statusPagePaused` reset on page-change at `Piglet.ino:93-95` is the kind of thing a `Scan::onPageChange` would either formalise or eliminate). Pre-req for 2.1 — without it, sharing globals across tasks needs locks everywhere; with it, ownership lines up with task lines.

**Acceptance.** Globals.h declares < 10 externs (hardware singletons: `display`, `gps`, `server`, `pins`, `cfg`). All upload/scan/AP state moved behind namespace facades. The pause-precedence block in `loop()` collapses to `if (Scan::isAllowed()) doScanOnce();`.

## 2.3 Split MeshNode.cpp into Node + Core + Protocol

*Category: Architecture  ·  Effort: M*

**Current state.** `MeshNode.cpp` is 916 lines holding both roles. Node-side state (`jcmkHaveCore`, `jcmkCoreMac`, `jcmkStartIdx`, ring buffer for core-found events) interleaves with Core-side state (`coreNodes[]`, `coreReqBuf`, `coreTextBuf`) and a shared ESP-Now `OnRecv` callback dispatches to both. The protocol structs and `JCMK_CHANNELS` table live at the top of the same file. Adding a third role (e.g. a sniffer) means more conditional code in the receive callback.

**Proposed change.** Three files. `piglet/jcmk_proto.{h,cpp}` (structs, magic, channel table, builders — also lives in `piglet-core` for sharing with `PigletNode`). `MeshNode.cpp` keeps node state and `nodeModeTick`. `MeshCore.cpp` keeps core state and `coreModeTick`. The ESP-Now `OnRecv` callback becomes a thin dispatcher to one or the other based on which mode is active.

```cpp
// MeshNode.h
namespace mesh::node {
  void enter(); void exit(); void tick();
  void onRecv(const uint8_t* mac, const uint8_t* data, int len);
  bool active(); const NodeStats& stats();
}
// MeshCore.h
namespace mesh::core {
  void enter(); void exit(); void tick();
  void onRecv(const uint8_t* mac, const uint8_t* data, int len);
  bool active(); const CoreStats& stats();
}
// Mesh.cpp (dispatcher)
static void jcmkOnRecv(const uint8_t* mac, const uint8_t* data, int len) {
  if (mesh::core::active())  mesh::core::onRecv(mac, data, len);
  else if (mesh::node::active()) mesh::node::onRecv(mac, data, len);
}
```

**Trade-offs / risks.** The two roles do legitimately share the channel-setting helper (`jcmkSetChannel`), the broadcast peer add (`jcmkAddPeer`), and `meshAuthModeToString` (which also dupes `Scanner.cpp`'s — see 1.2). Keep those in `jcmk_proto.cpp`. There's a risk of accidentally inviting concurrent ESP-Now sends from both roles during the brief transition; gate with a `meshActiveRole` enum check in the public `enter`/`exit` functions.

**Acceptance.** No file > 600 LOC. Adding a hypothetical "sniffer" role needs only a new `mesh::sniffer::*` module plus one dispatcher line.

## 2.4 Board support via a single interface

*Category: Architecture  ·  Effort: M*

**Current state.** Pin map selection is a 100-line dance in `Piglet.ino:215-326` across two phases, with `pickPinsFromConfig` (`Config.cpp:31`) doing string comparisons (`s3`/`c6`/`c5`/`exp`/`auto`) to pick from a static `PINS_*` constant. `wardriverIsC5` (`Config.cpp:41`) does a different string comparison on `cfg.board` AND falls back to substring-matching `pins.name` if `auto`. Six places in the code branch on whether the board has 5 GHz capability.

**Proposed change.** One `IBoardSupport` interface, one instance per supported board, chosen once at boot.

```cpp
struct BoardCaps {
  bool wifi5ghz; bool ble; bool hasOled; bool hasTft;
  uint8_t maxScanChannels; const char* displayName;
};
class IBoardSupport {
public:
  virtual const PinMap& pins() const = 0;
  virtual const BoardCaps& caps() const = 0;
  virtual void initDisplay() = 0;     // OLED / TFT init varies wildly
  virtual void powerLed(bool) {}
  virtual ~IBoardSupport() = default;
};
IBoardSupport& boardSupport();        // initialised once at boot

// Specific: XiaoS3.cpp, XiaoC5.cpp, XiaoC6.cpp, TDongleC5.cpp, WaveshareC6.cpp
```

**Trade-offs / risks.** Virtual dispatch on Arduino-ESP32 has near-zero cost in practice (one indirect call) but pulls in `-frtti` / vtable overhead per board class (~few hundred bytes flash). Acceptable. Bigger concern: the current code happily runs on a board it doesn't strictly know about because of the chip-detect fallback (`Config.cpp:23`). Preserve that — a `BoardKind::Auto` that creates a `XiaoS3` instance is fine, but log loudly.

**Acceptance.** All `cfg.board == "..."` comparisons gone except in `parseBoardKind`. `wardriverIsC5()` becomes `boardSupport().caps().wifi5ghz`. Adding a hypothetical XIAO ESP32-C3 takes one new file.

---

# 3. Testing

## 3.1 Add host-side unit tests + Arduino-CLI compile CI

*Category: Testing  ·  Effort: L*

> **🟡 Partial — PR #1, commit `ce1935a`.** A doctest-based host-side harness now exists at `test/` with `doctest.h` (single-header), `test_main.cpp`, `test_scanner.cpp` (covers `authModeToString`), `test_webui_masking.cpp` (covers `maskedField`), `test_wigle_upload.cpp` (placeholder, integration-only, skipped), and a `Makefile`. Run with `make -C test test` — current results: **5 cases / 16 assertions pass, 1 skipped, 0 failed**. This is a slimmer first slice of the recommendation below: doctest over Unity/Catch2 (single-header, no setup), no CMake yet, and a focused starter set rather than the broad-coverage list. The compile-only Arduino-CLI matrix in CI **is still open** — proceed with the GitHub Actions workflow exactly as scoped here. Future test additions slot into the same `test/` dir with no further bootstrap.

**Current state** *(pre-PR #1, modulo the doctest harness above)*. Zero tests, zero CI. No `.github/workflows`, no host-side build target. Cross-sketch drift is detected when a user reports a broken build.

**Proposed change.** Two layers. (a) Host-side unit tests for the pure-C++ pieces in `piglet-core` (after extraction in 1.1) using Unity, doctest, or Catch2 — anything that runs on a Linux x86 GH runner. Cover: `parseKeyValueLine`, `cfgAssignKV`, `normalizeSdPath`, `sanitiseDeviceName`, the channel→frequency math in `writeRow`, the CSV row formatting, and JCMK message builders/parsers. (b) An Arduino-CLI compile matrix in CI that proves all four sketches compile against pinned ESP32-Arduino-core versions and pinned library versions. Compile-only catches half the drift; unit tests catch the rest.

```yaml
# .github/workflows/ci.yml
jobs:
  unit:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - run: cmake -S test -B build && cmake --build build && ctest --test-dir build

  compile:
    runs-on: ubuntu-latest
    strategy:
      matrix:
        sketch:
          - { dir: "Arduino Files/Piglet", fqbn: "esp32:esp32:XIAO_ESP32S3" }
          - { dir: "Arduino Files/Piglet", fqbn: "esp32:esp32:XIAO_ESP32C5" }
          - { dir: "Arduino Files/Piglet", fqbn: "esp32:esp32:XIAO_ESP32C6" }
          - { dir: "TDongleC5_Piglet",     fqbn: "esp32:esp32:esp32c5" }
          - { dir: "waveshareDisplayMiniPiglet", fqbn: "esp32:esp32:esp32c6" }
          - { dir: "PigletNode",           fqbn: "esp32:esp32:XIAO_ESP32C5" }
    steps:
      - uses: arduino/setup-arduino-cli@v1
      - run: arduino-cli core install esp32:esp32@3.1.1
      - run: arduino-cli lib install "Adafruit SSD1306" "TinyGPSPlus" "ArduinoJson@7.1.0"
      - run: arduino-cli compile --fqbn ${{ matrix.sketch.fqbn }} "${{ matrix.sketch.dir }}"
```

**Trade-offs / risks.** Arduino-CLI install + build matrix takes ~6 min per job on the free runners (the ESP32 toolchain is heavy). Cache the cores dir to bring that to ~2 min. Risk: pinning library versions exposes that the README's `ArduinoJson v6.x or v7.x` claim isn't true — code uses `DynamicJsonDocument` which is deprecated in v7. You'll be forced to pick one. Pick v7 (see 9.3).

**Acceptance.** PRs cannot merge with broken builds. `git log --grep 'broke compile'` stops accumulating.

## 3.2 Test channel classification edge cases

*Category: Testing  ·  Effort: S*

**Current state.** `processScanResults` (`Scanner.cpp:36-43`) classifies channels with the rule `is2g = (ch >= 1 && ch <= 14) || chUnknown` and `is5g = (ch >= 32 && ch <= 177)`. The `chUnknown` branch silently treats channel 0 as 2.4 GHz (which is correct for the WiGLE schema but worth pinning) and the channel-176/177 case at the high end is on the edge of 6 GHz allocations. There's no test for either.

**Proposed change.** Once `Scanner` is parameterised on a writer callback, write a host-side test that drives a fake `WiFiScanResult` table through `processScanResults` and asserts the (`is2g`, `is5g`, written) tuple for: ch 0, ch 1, ch 13, ch 14, ch 36, ch 165, ch 177, ch 178. Same for `channelToFreqMHz` once it moves into the CSV writer.

**Trade-offs / risks.** Trivial once 1.1 / 3.1 are in. Without those, you can't reach `processScanResults` from a host test because it links against `WiFi`.

**Acceptance.** A test file at `test/test_scanner_classification.cpp` covers all eight cases. Future channel-band additions (6 GHz/UNII-5–8 hypotheticals) get a one-line table addition.

---

# 4. Performance

## 4.1 TLS keep-alive across uploads

*Category: Performance  ·  Effort: L*

**Current state.** Both `uploadFileToWigle` (`WigleUpload.cpp:137`) and `uploadFileToWdgwars` (line 373) open a fresh `WiFiClientSecure`, do a full TLS handshake, write `Connection: close`, then sleep `delay(2000)` before the next file (line 763). On ESP32-C5 a TLS handshake against `api.wigle.net` is ~3-6 s (PSRAM-required because mbedtls allocates ~16 KB per session). For a 50-file backlog you spend ~5 minutes on handshakes alone.

**Proposed change.** Use HTTP/1.1 with `Connection: keep-alive` and reuse a single `WiFiClientSecure`/session ticket across all uploads in a batch. mbedtls supports session resumption via the session ticket extension; the Arduino-ESP32 `WiFiClientSecure` exposes `setSession`/`getSession`. Reset only when the server closes or after every N uploads.

```cpp
WiFiClientSecure tls;
tls.setCACert(WIGLE_ROOT_CA);
tls.setHandshakeTimeout(15);

mbedtls_ssl_session sess; bool haveSession = false;

for (auto& path : paths) {
  if (!tls.connected()) {
    if (haveSession) tls.setSession(&sess);
    if (!tls.connect(WIGLE_HOST, 443)) { /* ... */ }
    tls.getSession(&sess); haveSession = true;
  }
  // POST with "Connection: keep-alive"
  // Read response, do NOT close
}
```

**Trade-offs / risks.** WiGLE's API needs to actually honour keep-alive — verify with a packet capture. The current code switched from HTTP/1.1 to HTTP/1.0 deliberately (CHANGELOG `v1.3-beta`: "Switched from HTTP/1.1 to HTTP/1.0 for WiGLE API compatibility") — find out why and whether that's still the case in 2026 (likely a chunked-encoding bug, fixable). Session resumption may not survive a server-side LB rotation; rebuild on failure. Real risk if you also change the per-file 2 s delay — there's an undocumented WiGLE rate limit that 2 s seemed to dodge.

**Acceptance.** 50-file upload completes in < 60 s (current: ~5 min). No new 429 responses from WiGLE. Heap usage stable across the batch (mbedtls not leaking per-iteration).

## 4.2 Replace String concatenation in appendWigleRow

*Category: Performance  ·  Effort: M*

**Current state.** `SDUtils.cpp:223-249` builds a CSV row by ~14 `String +=` operations per network. The string `reserve(256)` helps but every appended `String(int)` or `String(double, 1)` is itself a heap allocation. A single 30-network scan produces ~400 short-lived `String` objects, fragmenting the heap that mbedtls (4.1) needs in one big chunk during uploads.

**Proposed change.** Format into a stack `char[320]` with `snprintf`, write once.

```cpp
void piglet::writeRow(Stream& out, const WigleRow& r) {
  char ssidEsc[64];                 // SSID with "" -> "" doubled
  escapeCsvQuoted(r.ssid, ssidEsc, sizeof(ssidEsc));

  char line[320];
  int n = snprintf(line, sizeof(line),
    "%s,\"%s\",%s,%s,%d,%lu,%d,%.6f,%.6f,%.1f,%.1f,%s,%s,%s\r\n",
    r.mac, ssidEsc, r.auth, r.firstSeen,
    r.channel, (unsigned long)channelToFreqMHz(r.channel, r.type),
    r.rssi, r.lat, r.lon, r.altM, r.accM,
    r.rcois, r.mfgrId, r.type == WigleRow::Type::WIFI ? "WIFI" : "BLE");

  if (n > 0 && n < (int)sizeof(line)) out.write((const uint8_t*)line, n);
}
```

**Trade-offs / risks.** Stack budget: 320 B is comfortable on the 4 KB+ scan-task stack but plan for it in the FreeRTOS task allocation. SSID truncation: WiGLE allows up to 32-char SSIDs and you must escape `"` to `""` — handle in `escapeCsvQuoted`. Floating-point `%f` on ESP32 pulls in the FP formatter (~3 KB) — already linked because of `Display.cpp`'s speed math, so no marginal cost.

**Acceptance.** Heap free at end-of-scan equals heap free at start-of-scan (within ±200 B). 1000-row regression test produces byte-identical CSV against current implementation.

## 4.3 Reuse JsonDocument for /status.json

*Category: Performance  ·  Effort: M*

**Current state.** `handleStatus` (`WebUI.cpp:783`) allocates a `DynamicJsonDocument(2048)` on every hit. The WebUI polls it roughly 1/s. `handleFiles` (line 880) allocates a 4 KB doc per hit. Each allocation is a `malloc(2048)` + the resulting JSON is serialised to a `String` (another malloc + copy) before being passed to `server.send` (which copies again into the socket buffer).

**Proposed change.** Two changes. (a) Move to ArduinoJson v7's `JsonDocument` which uses a small reusable pool. (b) Stream serialise directly to the response: `server.setContentLength(...)`/`server.sendContent(...)` chunks, or use `chunkedResponseModeStart` + `serializeJson(doc, server.client())`.

```cpp
static JsonDocument statusDoc;            // file-scope, ~700 B static

static void handleStatus() {
  statusDoc.clear();
  populateStatus(statusDoc);
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");
  serializeJson(statusDoc, server.client());   // streams; zero String copy
  server.client().stop();
}
```

**Trade-offs / risks.** Module-scoped `JsonDocument` is fine on the loop task; if you also expose `/status.json` from a separate task (you shouldn't), you need a mutex. ArduinoJson v7 migration is required (1.5/3.1) and changes the `DynamicJsonDocument(N)` API. The chunked-encoding regression noted in CHANGELOG v1.3-beta (`Fixed chunked encoding errors in /status.json and /files.json endpoints`) was the previous root cause — verify against modern Arduino-ESP32 (3.1+).

**Acceptance.** Per-request allocations from `/status.json` < 1 KB total. Polled at 1 Hz for an hour, heap-free time series is flat.

## 4.4 Combine csvHasDataRows with the upload open

*Category: Performance  ·  Effort: S*

**Current state.** `csvHasDataRows` (`WigleUpload.cpp:596`) opens a file, reads two header lines, peeks at `available()`, closes it. Then `uploadFileToWigle` (line 159) immediately re-opens the same file. Each `SD.open` is one or more block reads.

**Proposed change.** One open. Take an open `File&`, or have `postCsv` itself probe-and-rewind via `f.seek(0)`.

```cpp
File f = SD.open(path, FILE_READ);
if (!hasDataRows(f)) { f.close(); SD.remove(path); continue; }
f.seek(0);
auto r = postCsv(target, token, f);
f.close();
```

**Trade-offs / risks.** Trivial. Just make sure `hasDataRows` rewinds (it currently doesn't because it expects a fresh open).

**Acceptance.** Each upload batch issues one `SD.open` per CSV file (verifiable in `SD` debug logs).

## 4.5 Single-pass log directory scan

*Category: Performance  ·  Effort: S*

> **✅ Done — PR #1, commit `7ca9179`.** Both `uploadAllCsvsToWigle` and `uploadAllCsvsToWdgwars` now do one `SD.openNextFile` traversal that fills a `std::vector<String>`; `uploadTotalFiles` is set from `vec.size()`. The double-walk is gone.

**Current state** *(pre-PR #1)*. `uploadAllCsvsToWigle` walks `/logs` twice — once to count (`WigleUpload.cpp:657`), once to collect paths (line 694) — before the upload loop. Same in `uploadAllCsvsToWdgwars` (lines 795 and 820). `SD.openNextFile` is not cheap on SDHC cards; each directory enumeration re-reads FAT entries.

**Proposed change.** Collect once into `std::vector<String>`, set `uploadTotalFiles = paths.size()`.

**Trade-offs / risks.** Memory: a path is ~40 bytes, even 200 files is < 10 KB on the heap. Should fit; if you're worried, bound the vector at 500 entries and warn.

**Acceptance.** `uploadAllCsvs*` opens `/logs` once. Count and collect are the same loop.

---

# 5. Observability

## 5.1 Severity-tagged logging with compile-time filter

*Category: Observability  ·  Effort: M*

**Current state.** Hundreds of `Serial.print` / `Serial.printf` calls with bracketed prefix conventions (`[SCAN]`, `[WIFI]`, `[WiGLE]`) but no severity, no level filtering, no compile-out for release. The string literals contribute ~6-8 KB of flash that's pure dev-time aid.

**Proposed change.** `LOG_ERR/WARN/INFO/DEBUG` macros gated by `PIGLET_LOG_LEVEL`. Variadic, route through `Serial.printf`. Add a per-module tag.

```cpp
// piglet/log.h
#ifndef PIGLET_LOG_LEVEL
  #define PIGLET_LOG_LEVEL 2  // INFO
#endif
#define _LOG(lvl, tag, fmt, ...) do { \
  if ((lvl) <= PIGLET_LOG_LEVEL) \
    Serial.printf("[%s] " fmt "\n", tag, ##__VA_ARGS__); \
} while (0)
#define LOG_ERR(tag, ...)  _LOG(0, tag, __VA_ARGS__)
#define LOG_WARN(tag, ...) _LOG(1, tag, __VA_ARGS__)
#define LOG_INFO(tag, ...) _LOG(2, tag, __VA_ARGS__)
#define LOG_DBG(tag, ...)  _LOG(3, tag, __VA_ARGS__)

// Use:
LOG_INFO("SCAN", "Wrote %lu rows", (unsigned long)wrote);
LOG_DBG ("WiGLE", "TLS connect attempt %d/3", attempt);
```

**Trade-offs / risks.** Mechanical replacement; biggest risk is accidentally changing a `printf` format string. Save the verbose dev-only lines as `LOG_DBG`; the release binary compiles them out. Pair with 5.3 (ring buffer) so the same macros also feed the in-memory log.

**Acceptance.** Release build (`PIGLET_LOG_LEVEL=0`) is ~6 KB smaller. Dev builds emit identical-to-current output.

## 5.2 Persistent error counters in /status.json

*Category: Observability  ·  Effort: S*

**Current state.** Upload failures (`uploadFailedFiles` exists but is reset per-batch), scan WiFi-radio resets (`Scanner.cpp:95-99` — increments `zeroScanCount` then clears), SD reopen attempts (`SDUtils.cpp:262`), TLS connect retries (`WigleUpload.cpp:195`) are logged to Serial but not surfaced anywhere persistent. User reports are necessarily anecdotal.

**Proposed change.** A `LifetimeStats` struct in NVS (`Preferences` API), bumped at each error site, exposed in `/status.json` and resettable from the WebUI.

```cpp
struct LifetimeStats {
  uint32_t boots;
  uint32_t uploadOk; uint32_t uploadFail;
  uint32_t scanRadioResets;
  uint32_t sdReopens; uint32_t sdWriteFails;
  uint32_t tlsConnectRetries;
  uint32_t apForceCloses; uint32_t apExtends;
};
namespace Stats {
  const LifetimeStats& get();
  void bump(uint32_t LifetimeStats::*field);
  void flushSoon();   // coalesced NVS write, max 1/min
}

// /status.json adds:
JsonObject s = doc["stats"].to<JsonObject>();
s["boots"] = Stats::get().boots; /* ... */
```

**Trade-offs / risks.** NVS writes wear flash — coalesce. Don't bump-and-flush on every `tlsConnectRetries++`; mark dirty and flush every ~60 s + on graceful shutdown. The WebUI gets a single new card. Useful even before fancier observability work.

**Acceptance.** WebUI shows lifetime counts. Counts survive reboot. `/stats/reset` POST clears them.

## 5.3 In-memory log ring buffer at /logs.txt

*Category: Observability  ·  Effort: M*

**Current state.** No way to retrieve serial logs after the fact unless the user has a USB cable + serial monitor. Bug reports are along the lines of "it stopped scanning".

**Proposed change.** Last ~512 lines of `LOG_*` output in a ring buffer (likely ~32 KB depending on line length), served at `/logs.txt`. Optional: stream `/logs/stream` via SSE / chunked transfer for live tailing in the WebUI.

```cpp
// piglet/log_ring.h
namespace LogRing {
  void init(size_t bytes);
  void append(const char* line);
  void writeToStream(Stream& s);    // dumps in order, oldest first
}

// In _LOG macro, after the Serial.printf, also:
//   char buf[160]; vsnprintf(buf, sizeof(buf), fmt, ap);
//   LogRing::append(buf);

server.on("/logs.txt", HTTP_GET, []{
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/plain", "");
  LogRing::writeToStream(server.client());
});
```

**Trade-offs / risks.** 32 KB on a device with ~250 KB free heap is fine, but if you don't have PSRAM enabled (you should — the README requires it for TLS already) you're competing with mbedtls. Place the ring in PSRAM (`ps_malloc`) to keep DRAM for the wifi stack. The ring should be lock-protected if you ever write to it from multiple tasks (2.1).

**Acceptance.** `curl http://piglet/logs.txt` returns recent log. Bug reports include the URL output.

---

# 6. Security

## 6.1 Authenticate WebUI POST routes

*Category: Security  ·  Effort: M*

**Current state.** WebUI is unauthenticated. Default AP credentials are `Piglet-WARDRIVE` / `wardrive1234` (`Arduino Files/Piglet/wardriver.cfg:58-59`). Routes that mutate state — `/saveConfig`, `/start`, `/stop`, `/extend`, `/delete`, `/deleteAll`, `/reboot`, `/wigle/uploadAll`, `/wdgwars/uploadAll`, `/cleanup` — accept any request. Anyone in RF range can change `homePsk` to attacker-controlled, force the device into mesh mode, or wipe `/logs`.

**Proposed change.** Setup-time admin password (separate from the AP PSK so attackers who guess the AP password can't admin). Stored as PBKDF2 hash in NVS. `server.authenticate("admin", hash)` on all POST routes; the GET `/` and `/status.json` stay open so the UI works without prompting for credentials on read.

```cpp
bool requireAuth() {
  if (!server.authenticate("admin", cfg.adminPassword.c_str()))
    { server.requestAuthentication(); return false; }
  return true;
}

server.on("/saveConfig", HTTP_POST, []{
  if (!requireAuth()) return;
  handleSaveConfig();
});

// First-boot flow: WebUI shows "Set admin password" modal if cfg.adminPassword empty.
```

**Trade-offs / risks.** HTTP Basic over an open AP is plaintext — the threat is users on the same network, not RF eavesdroppers (anyone with the WPA2 PSK can decrypt traffic anyway). Real fix is HTTPS on the device, but TLS server on ESP32 with mbedtls in addition to the TLS client is heavy (~80 KB extra). Basic auth is a reasonable mitigation; document the threat model. Friction: users who lose the admin password must factory-reset via SD card edit.

**Acceptance.** All write routes require auth. WebUI prompts once. Forcing AP to OPEN mode (PSK < 8 chars) doesn't bypass auth.

## 6.2 Stop echoing WiGLE/WDGoWars tokens in /status.json

*Category: Security  ·  Effort: S*

> **✅ Done — PR #1, commit `7ca9179`.** `wigleBasicToken` and `wdgwarsApiKey` in `/status.json` now return `"(set)"` instead of the raw token, mirroring the existing `homePsk` masking. **Side effect:** the inline ternary was extracted into a `maskedField()` helper in a new `Arduino Files/Piglet/WebUI_masking.h`; all three call sites (`homePsk`, `wigleBasicToken`, `wdgwarsApiKey`) updated to use it. This is the small architectural improvement that fell out of making the change testable — `maskedField()` is now host-testable and is covered by `test/test_webui_masking.cpp` (commit `ce1935a`). Deferred work: the `/secrets/reveal` admin-only POST endpoint sketched below was **not** added in this PR; track separately once **6.1** (auth) lands so there's an authentication mechanism to gate it on.

**Current state** *(pre-PR #1)*. `WebUI.cpp:829` writes `c["wigleBasicToken"] = cfg.wigleBasicToken;` — full token in the JSON. Same for `wdgwarsApiKey` (line 830). Compare with `homePsk` at line 832 which correctly returns `"(set)"`. The UI then ships those tokens back on save, but the GET is what's exposed to anyone who hits `/status.json` (unauthenticated today, GET-only after 6.1).

**Proposed change.** Mirror the `homePsk` pattern: return `"(set)"`/`""` for secrets in GET responses. Add a separate `/secrets/reveal` POST that requires admin auth and returns the token in response, used by the WebUI's "Show" button.

```cpp
c["wigleBasicToken"] = cfg.wigleBasicToken.length() ? "(set)" : "";
c["wdgwarsApiKey"]   = cfg.wdgwarsApiKey.length()   ? "(set)" : "";
// optional admin-only:
server.on("/secrets/reveal", HTTP_POST, []{
  if (!requireAuth()) return;
  DynamicJsonDocument d(256);
  d["wigleBasicToken"] = cfg.wigleBasicToken;
  d["wdgwarsApiKey"]   = cfg.wdgwarsApiKey;
  String out; serializeJson(d, out); server.send(200, "application/json", out);
});
```

**Trade-offs / risks.** Round-trip on save needs updating — if the WebUI POSTs the token back as `"(set)"` (because the user didn't edit it), the device must treat that as "keep existing". Match the existing `homePsk` save logic.

**Acceptance.** `curl http://piglet/status.json` shows no secret material. WebUI still works (token edit roundtrip preserved). `/secrets/reveal` requires auth.

## 6.3 Pin WiGLE / WDGoWars CA certs

*Category: Security  ·  Effort: M*

**Current state.** `client.setInsecure()` everywhere (`WigleUpload.cpp:36, 181, 296, 410, 543`). The wardriver happily sends `Authorization: Basic <token>` to anything that answers on `api.wigle.net:443` — which on a wardriving rig in the wild can absolutely be a hostile AP doing DNS hijacking.

**Proposed change.** Pin the LetsEncrypt ISRG Root X1 (WiGLE) and the appropriate root for WDGoWars via `client.setCACert`. Roots are ~1.5 KB PEM each. Keep `setInsecure()` available behind a config flag for users with a corporate MITM proxy.

```cpp
static const char* WIGLE_ROOT_CA =
  "-----BEGIN CERTIFICATE-----\n"
  "MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw\n"
  /* ... */
  "-----END CERTIFICATE-----\n";

WiFiClientSecure tls;
if (cfg.tlsInsecure) tls.setInsecure();
else                 tls.setCACert(WIGLE_ROOT_CA);
```

**Trade-offs / risks.** Root rotation: LetsEncrypt ISRG Root X1 expires June 2035; until then no risk. WDGoWars' root depends on their issuer — check during implementation. Increased binary size (~3 KB across two roots). Verify on a real device that `setCACert` actually validates (some Arduino-ESP32 versions had a regression).

**Acceptance.** Setting up a MITM (e.g. mitmproxy with a generated cert) causes uploads to fail with TLS error. Setting `tlsInsecure=true` restores upload. Default config has it false.

## 6.4 Apply isAllowedDataPath to /wigle/upload

*Category: Security  ·  Effort: S*

**Current state.** `handleDownload` (`WebUI.cpp:908`) and `handleDelete` (line 920) both check `isAllowedDataPath(name)` which restricts to `/logs/` or `/uploaded/`. `handleWigleUploadOne` (line 1125) does NOT — `POST /wigle/upload?name=/wardriver.cfg` would happily ship your config (including WiGLE token and home PSK) to api.wigle.net.

**Proposed change.** One line. Add the check.

```cpp
static void handleWigleUploadOne() {
  if (!sdOk) { server.send(500, "text/plain", "SD not available"); return; }
  if (WiFi.status() != WL_CONNECTED) { /* ... */ return; }
  if (!server.hasArg("name")) { /* ... */ return; }
  String path = server.arg("name");
  if (!isAllowedDataPath(path)) { server.send(403, "text/plain", "Forbidden"); return; }  // ADD
  if (!SD.exists(path)) { /* ... */ return; }
  /* ... */
}
```

**Trade-offs / risks.** Zero behavioural risk for legitimate use; UI only ever sends `/logs/...` paths.

**Acceptance.** `curl -X POST 'http://piglet/wigle/upload?name=/wardriver.cfg'` returns 403.

## 6.5 CSRF protection on POST routes

*Category: Security  ·  Effort: S*

**Current state.** No CSRF protection. Once authentication is in place (6.1), a browser tab open against `http://piglet` can still be tricked by an attacker page into POSTing to `/reboot`, `/deleteAll`, etc. Basic auth credentials are auto-attached by the browser.

**Proposed change.** Require a non-standard header on all POSTs (`X-Piglet-Req: 1`). Forms in the WebUI include it via fetch options; browsers will not include it on cross-origin form posts (and CORS preflight blocks the request before it reaches the handler).

```cpp
bool requireCsrf() {
  if (server.header("X-Piglet-Req") != "1") {
    server.send(403, "text/plain", "Missing CSRF header"); return false;
  }
  return true;
}

// fetch in WebUI:
//   fetch('/saveConfig', { method: 'POST', headers: {'X-Piglet-Req': '1'}, body });
```

**Trade-offs / risks.** Won't help if the attacker is on the same LAN and can read AP traffic (they can construct the request directly). The threat model is malicious page in a tab. Pair with `SameSite` if you ever issue cookies (you currently don't).

**Acceptance.** Cross-origin form POST to `/reboot` returns 403. WebUI works.

---

# 7. Developer experience

## 7.1 Adopt PlatformIO with pinned envs

*Category: Developer experience  ·  Effort: M*

**Current state.** Arduino IDE only. Library versions unpinned (README: "ArduinoJson v6.x or v7.x" — incompatible APIs). "Works on my IDE" is a recurring failure mode. No way to script multi-board builds.

**Proposed change.** Add `platformio.ini` with one env per target and pinned `lib_deps`. Keep Arduino IDE compat by leaving the sketch dirs alone (PlatformIO supports `.ino` sketches via `src_dir`).

```ini
; platformio.ini
[platformio]
src_dir = .

[env]
platform     = espressif32 @ 6.10.0   ; pins arduino-esp32 core
framework    = arduino
monitor_speed = 115200
build_flags  = -DPIGLET_LOG_LEVEL=2
lib_deps =
  bblanchon/ArduinoJson @ 7.1.0
  mikalhart/TinyGPSPlus @ 1.0.3
  adafruit/Adafruit SSD1306 @ 2.5.10
  adafruit/Adafruit GFX Library @ 1.11.10

[env:xiao-s3]
board = seeed_xiao_esp32s3
src_filter = +<Arduino Files/Piglet/*>
board_build.partitions = huge_app.csv

[env:xiao-c5]
board        = seeed_xiao_esp32c5
src_filter   = +<Arduino Files/Piglet/*>

[env:t-dongle]
board        = esp32-c5-devkitc-1
src_filter   = +<TDongleC5_Piglet/*>
lib_deps     = ${env.lib_deps}
               adafruit/Adafruit ST7735 and ST7789 Library @ 1.11.0

[env:pigletnode]
board        = seeed_xiao_esp32c5
src_filter   = +<PigletNode/*>
lib_deps     =
```

**Trade-offs / risks.** Adds a tool. Arduino IDE users still work because src files are untouched. The `seeed_xiao_esp32c5` board may not yet exist in espressif32 6.10.0 — fall back to a generic `esp32-c5-devkitc-1` env with explicit `board_build.f_cpu` / partition. Use PlatformIO with VS Code or CLion; the maintainer's existing Elixir DX (VS Code / ElixirLS) translates one-to-one.

**Acceptance.** `pio run -e xiao-s3` builds. CI matrix (3.1) uses this. `arduino-cli` continues to work from the sketch dirs.

## 7.2 Reduce Globals.h include footprint

*Category: Developer experience  ·  Effort: S*

**Current state.** `Globals.h:1-12` pulls in `Arduino.h`, `WiFi.h`, `WiFiClientSecure.h`, `WebServer.h`, `SD.h`, `Adafruit_GFX.h`, `Adafruit_SSD1306.h`, `TinyGPSPlus.h`. Every TU that includes `Globals.h` (almost all of them) compiles all of these. Arduino IDE has no build cache; a full rebuild is dozens of seconds.

**Proposed change.** Forward-declare the types `Globals.h` only points at. Move heavy includes to `.cpp` files that actually use them.

```cpp
// Globals.h
#pragma once
#include <Arduino.h>
class WebServer; class TinyGPSPlus;
class Adafruit_SSD1306; class HardwareSerial; class File;
struct PinMap; struct Config;
extern PinMap pins; extern Config cfg;
extern Adafruit_SSD1306 display;        // pointer would be cleaner but requires touching every use
extern TinyGPSPlus gps; extern HardwareSerial GPSSerial;
extern WebServer server;
// ...
```

**Trade-offs / risks.** `Adafruit_SSD1306` is hard to forward-declare because it's a concrete object — leave that include. The rest can be forward-declared. With PlatformIO (7.1) you also get incremental builds, which is the bigger win.

**Acceptance.** Arduino IDE full rebuild time drops measurably (anecdote-grade — measure your baseline).

## 7.3 Add .clang-format + pre-commit hook

*Category: Developer experience  ·  Effort: S*

**Current state.** Indentation is consistent within a file but braces and pointer-asterisk style drift between modules. `WigleUpload.cpp` uses `client.print(String(...))`; `WebUI.cpp` uses `String out;` and `serializeJson(doc, out)`. Nothing enforces this.

**Proposed change.** Drop a `.clang-format` rooted in WebKit or Google style, run `clang-format -i` once across the tree, install pre-commit hook (or GH Actions check).

```yaml
# .clang-format
BasedOnStyle: Google
ColumnLimit: 100
IndentWidth: 2
PointerAlignment: Left
AlignConsecutiveDeclarations: AcrossEmptyLines
```

**Trade-offs / risks.** First reformat will be a giant noisy commit — do it alone, tag the SHA in the PR description so blame can `--ignore-rev` it.

**Acceptance.** CI fails on style violations. Existing files round-trip through `clang-format` unchanged.

## 7.4 Generate compile_commands.json for clangd

*Category: Developer experience  ·  Effort: S*

**Current state.** No `compile_commands.json`, so clangd/VS Code C++ extension can't resolve includes properly. "Go to definition" lands on the wrong copy of `JCMK_MAGIC` (it doesn't know which sketch you're editing) until you manually configure include paths.

**Proposed change.** PlatformIO emits `compile_commands.json` via `pio run -t compiledb` per env. Commit a `.vscode/settings.json` pointing clangd at the per-env file.

**Trade-offs / risks.** Per-env: editing T-Dongle code while clangd is pointed at xiao-s3 yields red squiggles. Either accept this or generate a unified db. PlatformIO 6+ has `--unified-compile-db` (verify).

**Acceptance.** clangd in VS Code resolves all symbols. clang-tidy can run in CI on the same db.

---

# 8. Documentation

## 8.1 Reconcile README + CHANGELOG with FIRMWARE_VERSION

*Category: Documentation  ·  Effort: S*

**Current state.** `Globals.h:2` says `FIRMWARE_VERSION "v2.52"`. `CHANGELOG.md`'s most recent entry is `v1.3-beta (2026-02-23)`. The gap reads like the CHANGELOG was abandoned.

**Proposed change.** Either backfill the CHANGELOG (likely a one-evening exercise reading git log) or delete it and add a `Release` notes link in README. Then enforce: every PR that bumps `FIRMWARE_VERSION` must add a CHANGELOG entry (lint via a CI check).

**Trade-offs / risks.** If git history is intact, `git log --pretty='%s' v1.3-beta..HEAD` is your draft.

**Acceptance.** Latest CHANGELOG entry matches `FIRMWARE_VERSION`. CI fails if a PR bumps the macro without touching CHANGELOG.

## 8.2 Add docs/PROTOCOL.md for JCMK wire format

*Category: Documentation  ·  Effort: S*

**Current state.** JCMK packet structs (`MeshNode.cpp:30-57`), the `JCMK_CHANNELS` table, the admin window timing, and the "must always send 212 bytes" Biscuit-Pro compatibility rule are only discoverable by reading the code. There's also the channel-6 admin-window protocol nuance (`NODE_ADMIN_WIN_MS = 300`, heartbeat cadence `JCMK_HB_MS = 5000`) that interop implementers can't infer.

**Proposed change.** One page covering: packet layouts (with field widths and byte offsets), magic bytes, type enum, channel table, state machine (Node: search → paired → scanning cycle + admin window), Core: announce → record-receive), heartbeat cadence, packet-size requirements (212 bytes), known coordinator implementations (Biscuit Pro, JCMK C5 Wardriver).

**Trade-offs / risks.** Living doc — pair its update with any change to `jcmk_proto.h`.

**Acceptance.** `docs/PROTOCOL.md` exists and references concrete code lines. PigletNode README links to it.

## 8.3 Module-level comments in the multi-file sketch

*Category: Documentation  ·  Effort: S*

**Current state.** `Piglet.ino` opens with a useful intro but doesn't enumerate the responsibilities of the other 24 files. A new contributor has to grep.

**Proposed change.** Six-line header at the top of `Piglet.ino` mapping module → responsibility, plus a one-line `// Responsibility:` comment at the top of each .cpp.

```cpp
/*
 * Module map:
 *   Scanner.cpp    — Async WiFi scan FSM; channel classification.
 *   WigleUpload.cpp — HTTP upload to WiGLE/WDGoWars; batch driver.
 *   MeshNode.cpp   — JCMK ESP-Now node + core roles.
 *   Display.cpp    — OLED rendering; per-page drawing; pig animation.
 *   WebUI.cpp      — HTTP routes + embedded HTML/JS.
 *   SDUtils.cpp    — SD path normalisation; log file lifecycle; CSV writer.
 *   Config.cpp     — Pin map detection; wardriver.cfg parser.
 *   WiFiManager.cpp — STA/AP lifecycle; DNS repair.
 *   battery_test.cpp — Optional uptime-on-battery logger.
 */
```

**Trade-offs / risks.** Trivial. Will go stale unless paired with a checklist on PRs.

**Acceptance.** New contributors can navigate the codebase without grep.

## 8.4 Bring sample wardriver.cfg in line with the code

*Category: Documentation  ·  Effort: S*

**Current state.** `Arduino Files/Piglet/wardriver.cfg` says `# Values are True or False` above `maxBootUploads`, which actually takes an integer. README's "full default config" lists keys in a different order than the sample file and uses slightly different wording for the `meshModeOnBoot` enum cases. The sample file misses `wdgwarsApiKey` defaults documentation.

**Proposed change.** One canonical source. Generate the sample `wardriver.cfg` from `Config.cpp` at build time (or vice versa: generate the docs from a YAML/JSON config-schema file). Easiest for now: hand-fix the sample, write a tiny test that re-reads the sample and asserts it parses cleanly.

**Trade-offs / risks.** Generation is nice but adds tooling; the smaller win is a parse-the-sample test.

**Acceptance.** Sample config parses without warnings. README's example matches the sample byte-for-byte.

---

# 9. Dependencies

## 9.1 Pin all library versions

*Category: Dependencies  ·  Effort: M*

**Current state.** README accepts "ArduinoJson v6.x OR v7.x". v6 and v7 have incompatible APIs (`DynamicJsonDocument` is removed in v7). When v8 lands all four sketches break silently for new users. Same with `Adafruit_SSD1306` / `Adafruit_GFX` / `TinyGPSPlus`: no versions pinned.

**Proposed change.** PlatformIO `lib_deps` (see 7.1). For Arduino IDE users, add a `LIBRARIES.md` listing exact tested versions with installation commands.

```text
# LIBRARIES.md
| Library | Tested Version | Notes |
|---|---|---|
| ArduinoJson | 7.1.0 | v7's JsonDocument required |
| TinyGPSPlus | 1.0.3 | |
| Adafruit_GFX | 1.11.10 | |
| Adafruit_SSD1306 | 2.5.10 | |
| Adafruit_BusIO | 1.16.1 | dep of SSD1306 |
| Adafruit_ST7735 | 1.11.0 | T-Dongle only |

arduino-cli lib install \
  "ArduinoJson@7.1.0" "TinyGPSPlus@1.0.3" "Adafruit GFX Library@1.11.10" ...
```

**Trade-offs / risks.** User friction one-time, then they're locked. Update process is now a single bump-the-version PR per library.

**Acceptance.** Library versions are pinned in both `platformio.ini` and `LIBRARIES.md`. CI uses the same pins.

## 9.2 Pin arduino-esp32 core version

*Category: Dependencies  ·  Effort: S*

**Current state.** README says "Arduino-ESP32 core v3.0.0 or later". The mesh code at `MeshNode.cpp:144-148` already documents an IDF 5.x behaviour change ("On IDF 5.x, disabling promiscuous after a prior STA connection causes the driver to revert to the home router channel"). A core upgrade can move similar footing under you.

**Proposed change.** Pin to a specific minor (e.g. `arduino-esp32 3.1.1` → IDF 5.3.x). Document the upgrade procedure: "bump in `platformio.ini`, rerun full CI matrix, smoke-test all six target boards before tagging".

**Trade-offs / risks.** Lock-in. Pin to LTS-adjacent releases; espressif tags `release/v5.x` branches. ESP32-C5 support is recent (3.0.x); don't pin so old that C5 falls off.

**Acceptance.** `platformio.ini` pins `platform = espressif32 @ 6.10.0` (or current). CI matrix uses that. CHANGELOG records the next bump.

## 9.3 Migrate to ArduinoJson v7 JsonDocument

*Category: Dependencies  ·  Effort: S*

**Current state.** Code uses `DynamicJsonDocument` everywhere (`WebUI.cpp:787, 883, 948, 994, 1051, 1076, 1093, 1114, 1157`; `Config.cpp:179`). Deprecated in v7. New code adopting v7 patterns is allocation-light (small pool, elastic).

**Proposed change.** Search-and-replace. `DynamicJsonDocument doc(N)` → `JsonDocument doc;`. Where strictly needed for an explicit cap, use `SpiramJsonDocument` or `StaticJsonDocument`. Pair with 4.3.

```cpp
// before
DynamicJsonDocument doc(2048);
doc["scanningEnabled"] = scanningEnabled;
// after
JsonDocument doc;
doc["scanningEnabled"] = scanningEnabled;
```

**Trade-offs / risks.** The migration guide notes a few semantic differences around `as<T>()` defaults. Skim before applying. Without 7.1 in place, the v7 dep change forces Arduino IDE users to upgrade their library manually.

**Acceptance.** Build passes with `ArduinoJson @ 7.1.0`. No `DynamicJsonDocument` remains.

## 9.4 Delete the legacy wardriver.json import path

*Category: Dependencies  ·  Effort: S*

**Current state.** `Config.cpp:170-200` keeps a one-shot import path for old `/wardriver.json` configs. ~30 lines of code + the ArduinoJson dep for parsing the JSON. The project is at `FIRMWARE_VERSION "v2.52"` — anyone upgrading from a pre-CFG version is rare.

**Proposed change.** Delete the branch. Document in README that users coming from < v1.0 need to recreate their config (it's a 30-second manual edit).

**Trade-offs / risks.** Loss of automatic migration for ancient users. Almost certainly nobody. If you want to be polite, leave the code but mark it deprecated for one release before deleting.

**Acceptance.** `Config.cpp::loadConfigFromSD` < 50 lines. README notes the migration path for legacy users.

---

# Adding BLE wardriving

This section scopes BLE wardriving as a major feature. It is not a punch-list item; it is a design that crosses scanner, CSV writer, config, WebUI, mesh protocol, and tests. Treat the effort as **L (multi-week)**.

## Why

WiGLE accepts BLE rows in the same CSV format used for Wi-Fi today (Type column set to `BLE`, MfgrId and RCOIs columns populated). Every supported piglet target board has Bluetooth LE 5.0 capability — XIAO ESP32-S3, XIAO ESP32-C5, XIAO ESP32-C6, LilyGo T-Dongle C5 (ESP32-C5), and the Waveshare ESP32-C6 LCD 1.47" board all ship with the same 2.4 GHz radio that can do BLE alongside Wi-Fi. Today that radio is idle for BT entirely.

Users gain a ~50% increase in WiGLE point density for the same drive (typical urban environments have ~1:1 BLE-to-Wi-Fi observable density once you include beacons, fitness trackers, vehicle telematics, AirTags, and ETA tags). It also opens BLE-specific use cases (Bluetooth Mesh sniff for IoT mapping, EddyStone/iBeacon discovery, AirTag tracking).

## WiGLE CSV format changes

The current writer at `SDUtils.cpp:223-249` emits a fixed line shape with the trailing constant `,0,WIFI` (the RCOIs/MfgrId/Type triplet). For BLE rows the same column layout is used but the values change:

| Column | Wi-Fi value | BLE value |
|---|---|---|
| MAC | BSSID | Advertised address (random or public) |
| SSID | AP SSID | Complete Local Name (`0x09`) or empty |
| AuthMode | OPEN/WPA2/WPA3/... | Misc field — use `[BLE]` or `[LE Public]` / `[LE Random]` |
| FirstSeen | ISO-8601 UTC | Same |
| Channel | 1-14 / 36-177 | Advertising channel: 37, 38, or 39 |
| Frequency | MHz from channel | 2402 / 2426 / 2480 |
| RSSI | dBm | dBm — same |
| Lat/Lon/Alt/Acc | GPS at observation | Same |
| RCOIs | empty | BLE service-data UUIDs, semicolon-separated |
| MfgrId | 0 | 16-bit Bluetooth SIG company ID from Mfgr Data ADV (0x4C = Apple, etc.) |
| Type | WIFI | BLE |

The writer should be parameterized once (already covered by improvement 1.1 / 4.2): the `WigleRow::Type` enum determines (a) which channel→frequency table to use and (b) what literal goes in the Type column. The Mfgr ID is extracted from BLE manufacturer-specific data (advertising packet AD type `0xFF`); the first two bytes are the little-endian 16-bit company identifier. The RCOIs column for BLE conventionally holds service-data UUIDs (AD type `0x16`) and is useful for identifying device categories (e.g. Apple Continuity service UUIDs reveal whether a device is AirDrop/Handoff/Find-My-capable).

## Radio coexistence: per-target board notes

All four target boards have BLE 5.0 capability — the practical question is duty cycle, not whether BT exists.

| Board | Chip | BLE | Coexistence notes |
|---|---|---|---|
| XIAO ESP32-S3 | ESP32-S3 | BLE 5.0 | Dual-core Xtensa, IDF coexistence arbiter mature; Wi-Fi scan + BLE passive scan time-slice cleanly. PSRAM helps NimBLE. |
| XIAO ESP32-C5 | ESP32-C5 | BLE 5.0 | Single-core RISC-V; same radio also does 5 GHz Wi-Fi sweep. Coex contention is highest here because the radio juggles 2.4 GHz Wi-Fi, 5 GHz Wi-Fi, and BLE. |
| XIAO ESP32-C6 | ESP32-C6 | BLE 5.0 | Single-core RISC-V; also has 802.15.4 (Thread/Zigbee) which Piglet does not use. Coex mature in IDF 5.x. |
| T-Dongle C5 | ESP32-C5 | BLE 5.0 | Same chip as XIAO C5, same notes. |
| Waveshare 1.47" | ESP32-C6 | BLE 5.0 | Same chip as XIAO C6. |
| PigletNode | ESP32-C5 | BLE 5.0 | Standalone mesh node — BLE scanning could be added but would need to be forwarded over JCMK (see mesh impact below). |

The ESP32 coexistence arbiter (configured via `esp_coex_*` in IDF) scheduling Wi-Fi and BT on the shared 2.4 GHz front-end is what lets passive BLE scanning happen alongside Wi-Fi scanning. The two cannot run simultaneously — the radio is one PHY — but the arbiter slices time. For Wi-Fi scanning Piglet today uses async mode with a 100 ms dwell × 13 channels (aggressive) which gives ~1.3 s scan plus ~200 ms idle between cycles. Inserting BLE passive scan into that idle window is the natural fit. The cost: BLE will lose advertising packets on whichever advertising channel the radio is not on during the Wi-Fi window. Mitigation: scan BLE for longer when not actively sweeping Wi-Fi (e.g. during the existing 1500 ms inter-sweep gap).

## Implementation approach

### Library choice: NimBLE vs Arduino-BluetoothLE

`h2zero/NimBLE-Arduino` is the right choice. Compared to the stock Arduino-ESP32 BLE library it is ~50 KB lighter, has a saner callback API, supports BLE 5.0 features (extended advertising) that the stock library lacks, and is what every modern BLE-on-ESP32 project uses. Add it to `lib_deps` once 7.1 (PlatformIO) is in place.

### Scanner integration

A new module `BleScanner.{h,cpp}` parallels `Scanner.cpp`. It registers a passive scan with a callback. Each callback invocation pushes a row into the same `appendWigleRow` path (once 1.1 / 4.2 are done) with `WigleRow::Type::BLE`. The callback runs in NimBLE's host task context; that's a hint to push results to a queue rather than writing SD directly (the SD path is not safe from arbitrary task contexts unless 2.1 is in place).

```cpp
#include <NimBLEDevice.h>

class BleObserver : public NimBLEScanCallbacks {
public:
  void onResult(NimBLEAdvertisedDevice* dev) override {
    BleRow r;
    r.addr = dev->getAddress().toString().c_str();
    r.rssi = dev->getRSSI();
    r.name = dev->getName().c_str();
    r.mfgrId = parseMfgrId(dev->getManufacturerData());
    r.serviceUuids = joinUuids(dev->getServiceDataUUIDs());
    r.advType = dev->getAddressType();      // public vs random
    xQueueSend(qBleResult, &r, 0);          // non-blocking; drop on full
  }
};

void bleScannerStart() {
  NimBLEDevice::init("piglet");
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);   // +9 dBm max
  auto* scan = NimBLEDevice::getScan();
  scan->setScanCallbacks(new BleObserver(), /*wantDuplicates=*/false);
  scan->setActiveScan(false);               // passive: no SCAN_REQ tx
  scan->setInterval(160);                   // 100 ms units of 0.625 ms
  scan->setWindow(100);                     // 62.5 ms
  scan->start(0, false);                    // continuous
}
```

### Dedupe window

BLE devices advertise every 20-1000 ms; raw callback firing without dedupe would produce thousands of CSV rows per minute per device. NimBLE's `wantDuplicates=false` argument dedupes within a single scan call. For longer-window dedupe (e.g. don't log the same BLE MAC more than once per 30 s while we have GPS-equivalent position), keep a small hash table (mirror of the `seenTable` already used in `waveshareDisplayMiniPiglet.ino:257-268` — once you extract `piglet-core`, BLE and Wi-Fi share that hash table). The hash key for BLE is a `(addr, advType)` tuple because the same hardware can re-advertise with rotating RPAs.

### Interleave with Wi-Fi scanner FSM

Today `doScanOnce()` in `Scanner.cpp:66` kicks an async Wi-Fi scan, returns immediately, polls `WiFi.scanComplete()` on subsequent loop iterations. A BLE passive scan can run in parallel because NimBLE's host task handles it independently of the IDF Wi-Fi scan state machine. Two integration patterns:

- **(A) Continuous BLE, gated by Wi-Fi.** Start BLE passive scan in `setup()` and never stop it. The IDF coex arbiter time-slices around active Wi-Fi scans. Simple, but BLE callback fires from NimBLE's host task — needs queue + 2.1 in place.
- **(B) Stop/start BLE around each Wi-Fi sweep.** `Scanner.cpp:115` kicks off a Wi-Fi async scan — immediately before, stop BLE scan; on Wi-Fi scan complete (line 86), restart it. More overhead per cycle but predictable behaviour without 2.1.

Recommend (A) once 2.1 lands. Pre-2.1, go with (B) and accept a few percent BLE loss during the Wi-Fi sweep.

## Memory budget

NimBLE-Arduino footprint on ESP32-Arduino 3.x with default config:

- **Flash:** ~45 KB for the library; another ~5 KB once you use service/UUID parsing.
- **RAM:** ~9 KB static + NimBLE host task stack (4 KB default).
- **Heap:** ~3 KB during active scans for the advertised-device cache; tunable via `CONFIG_BT_NIMBLE_MAX_ADV_INSTANCES` etc.

Adding this to the current Piglet which is already running `WiFiClientSecure` (mbedtls + 16 KB per session in PSRAM), web server, OLED frame buffer (1 KB), GPS UART buffer, ESP-Now mesh (per-peer state), and the SD file handle — you need PSRAM to be enabled (already required for TLS, per README "CRITICAL: Enable PSRAM"). Without PSRAM, BLE scanning will allocate against the same internal DRAM that mbedtls needs and uploads will start failing intermittently.

## New config keys

Add to `Config.h`, `Config.cpp`'s `cfgAssignKV`/`saveConfigToSD`, the WebUI form, and the sample `wardriver.cfg`:

```ini
# BLE wardriving — set to true to also scan Bluetooth Low Energy devices
# and log them to the same CSV alongside Wi-Fi rows.
bleEnabled=false

# BLE scan window (ms). Lower = more BLE rows / more battery.
# Recommended: 500 (passive); 2000 if you want to capture rare advertisers.
bleScanWindowMs=500

# BLE dedupe window (s). Same device won't be logged more than once
# per this many seconds. 0 = log every advertising packet.
bleDedupeWindowSec=30

# BLE TX power: 0..9. Higher = better discovery, more battery.
# 0 = -12 dBm, 9 = +9 dBm. Default 9.
bleTxPower=9
```

## Mesh protocol impact

Two options:

1. **Piggyback on existing wifi record type.** The JCMK text record is just a comma-delimited line (`BSSID,SSID,AUTH,CHANNEL,RSSI,W` — see `MeshNode.cpp:466`). For BLE we'd need a sixth field (Type) or to abuse the AUTH field (gross). Backward incompatible with existing JCMK Cores.
2. **Add a new message type.** `JCMK_MSG_TEXT_BLE = 6` with its own packed struct shape carrying the BLE-specific fields. Cores that don't implement type 6 will drop the packet (defensive default). New Cores parse it and write a BLE row.

Recommendation: option (2). The protocol already has the type byte; using it as designed is the right move. Document in `docs/PROTOCOL.md` (improvement 8.2) and bump the protocol version field on the announce packet so Cores can refuse mismatched Nodes if needed. PigletNode (the standalone mesh node) is the natural first BLE-capable Node; add a `bleEnabled` compile flag with a sensible default of off.

## Test plan

- **Host-side:** CSV writer emits valid BLE rows. Channel-to-frequency function returns 2402 for ch 37, 2426 for ch 38, 2480 for ch 39 (these are the BLE primary advertising channels).
- **On-device:** passive scan in an environment with a known mix of BLE devices (phone, AirTag, fitness tracker) produces dedupe-correct row counts.
- **On-device coex:** simultaneous BLE + Wi-Fi scanning — verify with a packet capture (Wireshark or `esp32_sniffer`) that the Wi-Fi sweep still completes in expected time (< 2 s for aggressive 13-channel).
- **On-device upload:** a BLE-mixed CSV uploads to wigle.net without WiGLE rejecting it. WiGLE's validator is strict about the header line — confirm the Type column is allowed to vary per row.
- **Mesh:** a BLE-capable Node sends a `JCMK_MSG_TEXT_BLE`, a BLE-capable Core writes it; a legacy Core drops it without crashing.

## Effort

Realistic **L (multi-week).** Sequenced: 1–2 days getting NimBLE building under PlatformIO, 2–3 days on the scanner FSM + callback wiring, 2 days on coex tuning, 1–2 days on config + WebUI plumbing, 2–3 days on the mesh protocol extension, 2–3 days of field testing across all four boards. Real-world: budget 3–4 weeks calendar.

---

# Sequencing recommendation

Dependencies between these items are real. A naïve attempt at 2.1 (FreeRTOS task split) without 1.1 and 2.2 first means you carry forward all the cross-module global mutations into a multi-task setup, which is exactly the threading-bug factory you want to avoid. Recommended order, in waves:

## Wave 1 — Foundation (no code rewrites, just guardrails)

- **7.1 PlatformIO + pinned env.** Everything else is downstream of this — CI, library pins, embedded asset baking, multi-board test matrix.
- **9.1, 9.2 Pin library and core versions.** Falls out of 7.1 trivially.
- **3.1 Compile-only CI matrix.** As soon as PlatformIO is in, wire CI so future PRs cannot break a sketch build.
- **7.3, 7.4 .clang-format + compile_commands.json.** Cheap and pays dividends on every PR after.

Wave 1 is 2-3 days of work and unblocks everything else.

## Wave 2 — Extract shared core, kill duplication

- **1.1 Extract piglet-core.** Biggest single ROI move. Pull JCMK protocol + WiGLE writer + uploader strategy first.
- **1.2 Deduplicate authModeToString.** Falls out of 1.1.
- **1.3, 1.4 Unify uploader + HTTP helper.** Same module as 1.1.
- **1.5 Stringly-typed config → enums.** Mechanical, do it once everything compiles against piglet-core.
- **9.3 Migrate to ArduinoJson v7.** Trivial after pins land.
- **9.4 Delete legacy /wardriver.json import.**
- **3.2 Unit tests for the things you just extracted.** The library is now testable; write a few.

Wave 2 is the meat. Multi-week — call it 3-4 weeks if you're part-time.

## Wave 3 — Architecture clean-up

- **2.2 Decompose Globals.** Pre-req for 2.1; do this first.
- **2.3 Split MeshNode into Node + Core.**
- **2.4 Board support interface.** Pre-req for adding new boards cleanly.
- **1.6 Move HTML out of WebUI.cpp.** Independent; can do any time.
- **1.7 Decompose setup().** After 2.2.

## Wave 4 — Concurrency

- **2.1 FreeRTOS task split.** The OLED-freezes-during-upload root fix. Hardest single change; benefits from all of Wave 3 being done.
- **4.1 TLS keep-alive.** Big perf win, easier to verify once httpTask is its own thread.
- **4.2 snprintf-based row writer.** Heap-fragmentation fix.
- **4.3, 4.4, 4.5 Smaller perf fixes.**

## Wave 5 — Hardening

- **6.1 WebUI auth.**
- **6.2 Stop leaking tokens in /status.json.**
- **6.3 Pin TLS roots.**
- **6.4 Path check on /wigle/upload.**
- **6.5 CSRF header.**
- **5.1, 5.2, 5.3 Observability.** Logging macros, lifetime counters, ring buffer.

## Wave 6 — BLE wardriving

All of Wave 1-4 are pre-reqs. Specifically: 1.1 (so the CSV writer is parameterised on row type), 4.2 (so the row writer can format the BLE Type column and frequency table), 2.1 (so the NimBLE callback can safely queue to the SD task without context-crossing), 1.5 (so `cfg.bleEnabled` is a real type), and 8.2 (so the protocol extension lands documented).

## Wave 7 — Polish

- **8.1, 8.2, 8.3, 8.4 Documentation.** Throughout; pair with each Wave's commits.

## Time-boxed minimum viable sequence

If you can only spend two weekends on this: do Wave 1 in full (one weekend), then 1.1 + 1.3 + 6.1 + 6.2 + 6.4 (second weekend). You get CI, you stop the three-fork drift, and you close the unauthenticated config bypass. Everything else can wait. BLE is its own multi-week project that should not start until Wave 4 is done — premature BLE work without the FreeRTOS split will result in either spurious upload failures or BLE callbacks dropping data on the floor.

## What's already in flight (PR #1)

Branch `improvements/mask-tokens-dedupe-single-pass` ([PR #1](https://github.com/drdray1/piglet/pull/1)) lands a small, low-risk subset across three waves:

- **Wave 2 (extract / dedupe):** 1.2 (authModeToString moved to `Scanner.h`).
- **Wave 4 (perf):** 4.5 (single-pass `/logs` walk in both uploaders).
- **Wave 5 (hardening):** 6.2 (token masking in `/status.json`, with `maskedField()` extracted to `WebUI_masking.h`).
- **Wave 1 prerequisite (testing):** 3.1 partially — a doctest harness now exists at `test/` covering the two new helpers. Compile-only CI is still open.

Suggested next slice once PR #1 merges, in priority order: 6.4 (one-line path check), 6.1 (WebUI auth — unlocks the `/secrets/reveal` follow-up to 6.2), 7.1 (PlatformIO — unblocks the Arduino-CLI CI matrix that completes 3.1), then start Wave 2 proper with 1.1 (extract piglet-core).
