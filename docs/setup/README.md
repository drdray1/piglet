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

## Reference

- [Config key reference](config-reference.md) — every key, defaults, which
  firmwares support it
- [`../PROTOCOL.md`](../PROTOCOL.md) — the ESP-Now wire format between Core and Nodes
- [`../../LIBRARIES.md`](../../LIBRARIES.md) — Arduino libraries to install
- [`../../README.md`](../../README.md) — hardware, wiring, cases, button/page reference
