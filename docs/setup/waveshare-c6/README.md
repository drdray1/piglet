# Piglet on the Waveshare ESP32-C6 LCD 1.47"

A single-file port in
[`waveshareDisplayMiniPiglet/`](../../../waveshareDisplayMiniPiglet/) for the
Waveshare ESP32-C6 LCD 1.47" dev board.

| Role | Config |
|---|---|
| Standalone | [`standalone.cfg`](standalone.cfg) |

## Scope — read before choosing this board

This is the most limited of the four firmwares. It is **Wi-Fi wardriving only**:

| | |
|---|---|
| 2.4 GHz Wi-Fi scanning | ✅ |
| GPS + SD + WiGLE CSV | ✅ |
| ST7789 TFT status display | ✅ |
| Web UI (browse / download / config) | ✅ |
| WiGLE upload | ✅ |
| 5 GHz | ❌ C6 is 2.4 GHz only |
| BLE wardriving | ❌ not ported |
| Mesh Core / Node | ❌ not ported |
| WDGoWars upload | ❌ |
| Blacklist / log-once dedup | ❌ |

If you want BLE or a cluster, use a [XIAO](../piglet-xiao/) or a
[T-Dongle C5](../tdongle-c5/).

## Config keys

Eight keys, and that is the whole set — anything else in the file is ignored:

`homeSsid`, `homePsk`, `wigleBasicToken`, `wardriverSsid`, `wardriverPsk`,
`scanMode`, `speedUnits`, `gpsBaud`

## Setup

1. esp32 core **v3.x**; libraries per [`LIBRARIES.md`](../../../LIBRARIES.md).
   This port drives its ST7789 panel through **LovyanGFX**, which the other
   firmwares do not use — install it or the build fails immediately with
   `fatal error: LovyanGFX.hpp: No such file or directory`.
2. Board: **ESP32C6 Dev Module**. There is no "Waveshare ESP32-C6 LCD 1.47"
   entry in the esp32 core; the Waveshare boards it does list are different
   hardware.
   **Set Tools → Partition Scheme → Huge APP** — at the default scheme this
   sketch lands at 100% and fails to link.
3. Wire GPS and SD to the pins defined in the header comment at the top of the
   sketch — that block is the authoritative pinout for this port.
4. Copy [`standalone.cfg`](standalone.cfg) to the SD root as `wardriver.cfg`,
   fill in your Wi-Fi and WiGLE token.
5. Boot with Serial at 115200.

## Boot behaviour

1. Tries `homeSsid` first.
2. **Connected** → uploads any pending CSVs to WiGLE, then wardrives.
3. **Failed** → starts the Wardriver AP for a hard 60-second window
   (<http://192.168.4.1>) so you can fix the config, then wardrives.

The button toggles scanning on and off.
