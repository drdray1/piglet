# Piglet Setup

Start here. Pick your board, then follow that folder's guide.

Every Piglet reads a plain-text `/wardriver.cfg` from the root of its SD card.
Each folder below has ready-to-copy examples for every role that board supports.

## Pick your board

| Board | Guide | Roles | BLE | Mesh |
|---|---|---|---|---|
| XIAO ESP32-S3 / C5 / C6 / C3 | [`piglet-xiao/`](piglet-xiao/) | standalone · core · node | ✅ opt-in | ✅ |
| LilyGo T-Dongle C5 | [`tdongle-c5/`](tdongle-c5/) | standalone · core · node | ✅ opt-in | ✅ |
| XIAO ESP32-C5 (node-only build) | [`pigletnode/`](pigletnode/) | node only | ✅ always on | ✅ |
| Waveshare ESP32-C6 LCD 1.47" | [`waveshare-c6/`](waveshare-c6/) | standalone only | ❌ | ❌ |

## Pick your role

**Standalone** — one device does everything: scans, geotags from its own GPS,
writes CSVs to its own SD, uploads them. Start here if you have one board.

**Core + Nodes (a mesh cluster)** — one **Core** plus up to four **Nodes**.
Nodes scan and forward every observation over ESP-Now; the Core stamps them with
*its* GPS and writes one combined CSV. Only the Core needs a GPS module and an
SD card. More radios covering more channels at once means far more coverage per
drive.

> A Core does **not** scan BLE itself, by design — it logs what its Nodes send.
> If you want BLE from a single device, run it **standalone**, not as a Core.

## The 60-second version

1. Flash the firmware for your board (each guide has the exact board target —
   getting this wrong is the most common failure).
2. Copy the matching `.cfg` from your board's folder to the SD card root,
   renamed to `wardriver.cfg`.
3. Fill in `homeSsid` / `homePsk` and your `wigleBasicToken`.
4. Insert the SD card, power on, watch the Serial monitor at 115200.

## Verified build matrix

Every target below was clean-built against **esp32 core 3.3.10**
(2026-08-04). Percentages are program-storage use.

| Firmware | Board target | Default partition | With Huge APP |
|---|---|---|---|
| Piglet | `XIAO_ESP32S3` | ✅ 44% | — |
| Piglet | `XIAO_ESP32C5` | ✅ 54% | — |
| Piglet | `XIAO_ESP32C6` | ❌ **133%** | ✅ 55% |
| Piglet | `XIAO_ESP32C3` | ❌ **121%** | ✅ 50% |
| T-Dongle C5 | `XIAO_ESP32C5` | ✅ 52% | — |
| T-Dongle C5 | `ESP32C5 Dev Module` | ❌ **135%** | ✅ 56% |
| PigletNode | `XIAO_ESP32C5` | ✅ 41% | — |
| PigletNode | `ESP32C5 Dev Module` | ❌ **106%** | ✅ 44% |
| Waveshare C6 | `ESP32C6 Dev Module` | ❌ **100%** | ✅ 42% |

**Read that as: more than half of these need Tools → Partition Scheme →
Huge APP.** The firmware has grown past the 1.25 MB app partition that most
default schemes give you. The `XIAO_*` profiles are the exception — they map to
larger layouts (3 MB app), which is why the T-Dongle guide tells you to pick
`XIAO_ESP32C5` even though the dongle is not a XIAO.

An overflow looks like:

```
Sketch uses 1749660 bytes (133%) of program storage space.
Error during build: text section exceeds available space in board
```

That is a menu setting, not a code problem.

## Reference

- [Config key reference](config-reference.md) — every key, defaults, which
  firmwares support it
- [`../PROTOCOL.md`](../PROTOCOL.md) — the ESP-Now wire format between Core and Nodes
- [`../../LIBRARIES.md`](../../LIBRARIES.md) — Arduino libraries to install
- [`../../README.md`](../../README.md) — hardware, wiring, cases, button/page reference
