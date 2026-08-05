
# Piglet Wardriver

**Piglet** is an open-source ESP32-based wardriving platform that scans nearby Wi-Fi networks, records GPS position, saves WiGLE-compatible CSV logs to SD, and provides a real-time web UI for control, uploads, and device status.

Designed for **[Seeed XIAO ESP32-S3](https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/), [XIAO ESP32-C5](https://wiki.seeedstudio.com/xiao_esp32c5_getting_started/), [XIAO ESP32-C6](https://wiki.seeedstudio.com/xiao_esp32c6_getting_started/), and [XIAO ESP32-C3](https://wiki.seeedstudio.com/XIAO_ESP32C3_Getting_Started/)**, Piglet focuses on:

- Reliable scanning while in motion  
- Clean WiGLE-ready data collection  
- Simple field deployment  
- Fully hackable open firmware


## Features

- 2.4 GHz Wi-Fi scanning  
- 5 GHz scanning on ESP32-C5 hardware  
- **Bluetooth LE wardriving** — passive BLE scan logged to the same WiGLE CSV (`Type=BLE`); opt-in via `bleEnabled=true`. In mesh mode, Nodes forward BLE observations to the Core. Requires the NimBLE-Arduino library.
- GPS position, heading, and speed logging  
- SD card logging in WiGLE CSV format  
- Web UI for:
  - Start / stop scanning  
  - Upload logs to WiGLE  
  - View device status  
  - Manage SD files  
  - Edit configuration  
- OLED live status display  
- Optional battery monitoring (board dependent)  
- Automatic STA connect with AP fallback  
- Optimized for mobile wardriving or warWalking!
- **ESP-Now Mesh Node mode** — pair with a coordinator device for multi-node wardriving
- **Mesh auto-start on boot** — configure `meshModeOnBoot` to automatically enter Core or Node mode after uploads complete, bypassing the AP window
- **Screen rotation** — mount the display upside-down and set `rotateScreen180=true` to flip 180°
- **Auto-start wardriving after uploads** — set `autoStartAfterUpload=true` to disconnect from home Wi-Fi immediately after boot uploads complete and begin scanning without delay
- **PigletNode** — standalone minimal firmware for XIAO ESP32-C5 that boots directly as a mesh node (no display, GPS, or SD required), scanning Wi-Fi **and** BLE


## Firmware variants

Four firmwares live in this repo. They share the same `wardriver.cfg` format and
the same WiGLE CSV output.

| Firmware | Board | Roles | BLE | Mesh | Setup guide |
|---|---|---|:-:|:-:|---|
| **Piglet** (`Arduino Files/Piglet/`) | XIAO S3 / C5 / C6 / C3 | standalone · core · node | opt-in | ✅ | [`docs/setup/piglet-xiao/`](docs/setup/piglet-xiao/) |
| **T-Dongle C5** (`TDongleC5_Piglet/`) | LilyGo T-Dongle C5 | standalone · core · node | opt-in | ✅ | [`docs/setup/tdongle-c5/`](docs/setup/tdongle-c5/) |
| **PigletNode** (`PigletNode/`) | XIAO ESP32-C5 | node only | always on | ✅ | [`docs/setup/pigletnode/`](docs/setup/pigletnode/) |
| **Waveshare C6** (`waveshareDisplayMiniPiglet/`) | Waveshare C6 LCD 1.47" | standalone only | ❌ | ❌ | [`docs/setup/waveshare-c6/`](docs/setup/waveshare-c6/) |

## Setup

**→ [`docs/setup/`](docs/setup/) has step-by-step guides and ready-to-copy
config files for every board and every role.**

The short version: flash the firmware, copy the matching `.cfg` from your
board's folder to the SD card root as `wardriver.cfg`, fill in your Wi-Fi and
WiGLE token, power on.

- [Config key reference](docs/setup/config-reference.md) — every key, its
  default, and which firmwares support it
- [ESP-Now protocol](docs/PROTOCOL.md) — the Core↔Node wire format
- [Libraries](LIBRARIES.md) — what to install

### Running a mesh cluster

One **Core** plus up to four **Nodes**. Nodes scan Wi-Fi and/or BLE and forward
everything over ESP-Now; the Core stamps it with *its* GPS and writes one
combined CSV. Only the Core needs GPS and an SD card, so nodes can be bare
boards. Each node can be dedicated to one radio (`wifi` / `ble` / `both`) from
the Core's config, addressed by MAC.

A Core deliberately does **not** scan BLE itself — it logs what its nodes send.
For BLE from a single device, run that device standalone.

Cores also interoperate with **Biscuit Pro** and **JCMK C5** coordinators for
Wi-Fi, with [caveats](docs/setup/pigletnode/README.md#known-interop-limit).

Full walkthrough: [`docs/setup/piglet-xiao/README.md`](docs/setup/piglet-xiao/README.md#building-a-cluster).

### Bluetooth LE wardriving

Passive, observer-only BLE logged into the **same WiGLE-1.6 CSV** as Wi-Fi with
`Type=BLE`, so one upload carries both. Opt in with `bleEnabled=true`
(always on for PigletNode). Per device it records address + type, advertised
name, RSSI, 16-bit service UUIDs, and manufacturer company ID — all GPS-stamped.

Tuning and the Core/standalone distinction: [config
reference](docs/setup/config-reference.md#ble).

### Filtering what gets logged

Piglet logs each BSSID and BLE MAC **once per boot** by default, keeping CSVs
compact, and can drop chosen networks entirely (your own home Wi-Fi, your
phone). Both are configured per device — see
[filtering](docs/setup/config-reference.md#filtering).

## Supported Hardware

### Microcontroller Boards

- [Seeed XIAO ESP32-S3](https://www.seeedstudio.com/XIAO-ESP32S3-p-5627.html)  
- [Seeed XIAO ESP32-C5](https://www.seeedstudio.com/Seeed-Studio-XIAO-ESP32C5-p-6609.html) *(required for 5 GHz scanning)*  
- [Seeed XIAO ESP32-C6](https://www.seeedstudio.com/Seeed-Studio-XIAO-ESP32C6-p-5884.html)  
- [Seeed XIAO ESP32-C3](https://www.seeedstudio.com/Seeed-XIAO-ESP32C3-p-5431.html) *(2.4 GHz only, headless — set `board=c3`)*  
- LilyGo T-Dongle C5 *(standalone variant — see above)*
- [Seeed XIAO ESP32-C5](https://www.seeedstudio.com/Seeed-Studio-XIAO-ESP32C5-p-6609.html)  *(PigletNode — mesh node only, see above)*  

### Required Peripherals

- I2C GPS module (ATGM336H)
- 128×64 SSD1306 OLED display (I2C)
- SPI SD card module
- Optional LiPo battery connected to XIAO battery inputs

### Peripheral Sourcing  
You can get everything on Amazon but its pricey.  if you dont mind waiting on aliexpress heres the build list.
- Xiao-C5 - 7$ [SeedStudio](https://www.seeedstudio.com/Seeed-Studio-XIAO-ESP32C5-p-6609.html)
- SSD1306 128x63 OLED - $2 [aliexpress](https://www.aliexpress.us/item/3256805954920554.html)
- ATGM-336h - $3.39 [aliexpress](https://www.aliexpress.us/item/3256809330278648.html)
- SD-Card Module - $1.33 [aliexpress](https://www.aliexpress.us/item/3256808167816573.html)
- User Button - $0.41 [Item:CS1211 From Digikey](https://www.digikey.com/en/products/detail/cit-relay-and-switch/CS1211/16607858)

$14 if you wanted to breadboard it yourself.


## Wiring / Pinouts

Pin mappings are automatically selected by firmware.

### XIAO ESP32-S3

| Function | Pin |
|----------|-----|
| I2C SDA | GPIO 5 |
| I2C SCL | GPIO 6 |
| GPS RX | GPIO 4 |
| GPS TX | GPIO 7 |
| Button | GPIO 1 |
| SD CS | GPIO 2 |
| SD MOSI | GPIO 10 |
| SD MISO | GPIO 9 |
| SD SCK | GPIO 8 |

### XIAO ESP32-C6 / ESP32-C5

| Function | Pin |
|----------|----- |
| I2C SDA | GPIO 23 |
| I2C SCL | GPIO 24 |
| GPS RX | GPIO 12 |
| GPS TX | GPIO 11 |
| Button | GPIO 0 |
| SD CS | GPIO 7 |
| SD MOSI | GPIO 10 |
| SD MISO | GPIO 9 |
| SD SCK | GPIO 8 |

**Note:** Only the ESP32-C5 supports 5 GHz Wi-Fi scanning.

### XIAO ESP32-C3

Set `board=c3` in `/wardriver.cfg` or select **XIAO C3** in the Web UI. Auto-detected from chip model on first boot.

| Function | Pin |
|----------|----- |
| I2C SDA | GPIO 6 (D4) |
| I2C SCL | GPIO 7 (D5) |
| GPS RX | GPIO 20 (D7) |
| GPS TX | GPIO 21 (D6) |
| Button | *none* (GPIO 9 = SPI MISO conflict — wire externally if needed) |
| SD CS | GPIO 2 (D0) |
| SD MOSI | GPIO 10 (D10) |
| SD MISO | GPIO 9 (D9) |
| SD SCK | GPIO 8 (D8) |

**Note:** 2.4 GHz only. No built-in display — attach an optional SSD1306 OLED on D4/D5.


## 3D Printed Cases

Print-ready STL files are available in the `Case Files/` directory. These cases are designed specifically for the Piglet PCB and module stack.

### 👏 Case Design by Bread — Breadbox Systems

The Piglet case was designed by **Bread** at [Breadbox Systems](https://breadboxsystems.com). If you print one, please take a moment to **like and boost the design on MakerWorld** — it helps the creator and makes the design easier for others to find.

> **[🖨️ Print the Piglet Wardriver Case on MakerWorld →](https://makerworld.com/en/models/2708429-piglet-wardriver-case#profileId-3000037)**

| File | Description |
|------|-------------|
| `Piglet Face.STL` | Front panel / lid |
| `Piglet Butt.STL` | Rear enclosure |
| `Piglet Butt with SMA hole.STL` | Rear enclosure with external antenna cutout |
| `Piglet Midboard.STL` | Internal standoff / mid-layer |
| `Piglet Features.stl` | Feature plate / accessory mount |

For the **T-Dongle C5** variant, GPS antenna mount STLs are included in `TDongleC5_Piglet/GPS STL/`.

> Print with standard PLA or PETG. No supports required on most parts. Recommend 0.2 mm layer height, 3 perimeters.


## PCB Design

Piglet includes custom PCB designs for compact, production-ready builds. KiCad project files and Gerber production files are available in the `PCB Files/` directory.

### PCB Images

| Board Front | Board Back | Board Close-up |
|-------------|------------|----------------|
| ![Board1](Images/Board1.jpg) | ![Board2](Images/Board2.jpg) | ![Board3](Images/Board3.jpg) |

### Assembled Piglet

| Module Arrangement | Front View | Back View |
|--------------------|------------|----------|
| ![Module Arrangement](Images/Module_Arrangement.png) | ![Built Piglet Front](Images/BuiltPiglet.jpg) | ![Built Piglet Back](Images/BackBuiltPiglet.jpg) |

**Assembly Note:** When stacking modules, apply **Kapton tape** between components to prevent electrical shorts. Pay special attention to exposed pins and solder joints that may contact adjacent modules.


## Configuration

Configuration is a plain-text `/wardriver.cfg` at the root of the SD card. If
none exists, the firmware boots with defaults and writes one you can edit.

**→ Ready-to-copy examples for every board and role: [`docs/setup/`](docs/setup/)**

**→ Every key, default, and per-firmware support: [config
reference](docs/setup/config-reference.md)**

Most settings are also editable from the web UI — connect to the Wardriver AP
(`http://192.168.4.1`) or reach the device on your home network, and use the
**Config** panel. A few (node roles, blacklist, dedup) are SD-config-only by
design.

> **`autoStartAfterUpload=true` hides the web UI.** The device drops the home
> Wi-Fi link straight after uploading, so it is no longer reachable on your home
> network. To undo it: power on away from home so the Wardriver AP comes up and
> browse to `http://192.168.4.1`, or pull the SD card and set
> `autoStartAfterUpload=false` directly.

## Button Functions

### Press Types

| Press | Timing | Action |
|-------|--------|--------|
| **Single press** | Quick tap | Advance to next page |
| **Double press** | Two taps within 350 ms | Toggle scan pause *(Status page only)* |
| **Long press** | Hold ≥ 2 seconds | Enter deep sleep |
| **Single press** *(while sleeping)* | Any | Wake from deep sleep / reboot |

### Pages (Single Press cycles through these)

| Page | Name | What it Shows | Scanning |
|------|------|---------------|----------|
| 0 | **Status** | Scan state, SD, GPS fix, WiFi, network counts, speed, IP, upload status | ✅ Active |
| 1 | **Networks** | Large display of 2.4 GHz, 5 GHz, and total network counts | ✅ Active |
| 2 | **Navigation** | Compass arrow, heading direction, current speed | ✅ Active |
| 3 | **Paused** | Pause icon — scanning fully stopped | ❌ Paused |
| 4 | **Pig** | Walking pig animation 🐷 | ✅ Active |
| 5 | **Mesh Node** | ESP-Now link state, coordinator MAC, channel range, Found/Sent counts | ↔ Forwarded via ESP-Now |

### Double Press — Status Page Only

When on the **Status page**, double-pressing toggles a scan pause without leaving the page. Useful for a quick stop without navigating to the Pause page.

- **Double press →** Scanning paused on status page
- **Double press again →** Scanning resumed
- **Single press (page change) →** Pause automatically cleared when leaving page 0

### Long Press — Deep Sleep

Hold the button for **2 seconds** from any page:
- Active log file is flushed and closed before sleeping
- OLED displays `Sleep...` then powers off
- A single button press wakes the device (full reboot)

### Mesh Node Page

Entering **page 5** automatically starts ESP-Now node mode. Leaving it (single press to advance) automatically restores normal wardriving. See [Running a mesh cluster](#running-a-mesh-cluster) above, or the full walkthrough in [`docs/setup/`](docs/setup/).

## Building Firmware

### Requirements

- Arduino IDE 2.x
- **esp32 by Espressif core v3.x** — core 4.x does not build this firmware
- Libraries per [`LIBRARIES.md`](LIBRARIES.md) (NimBLE-Arduino must be **2.x**)

Board selection, partition scheme, and PSRAM settings differ per variant and are
the most common cause of a failed build. Each [setup
guide](docs/setup/) states the exact target for its board — notably the T-Dongle
C5, which needs `XIAO_ESP32C5` rather than the obvious `ESP32C5 Dev Module`.

### Build troubleshooting

#### "Sketch too big" / `text section exceeds available space in board`

The single most common build failure. **This is a menu setting, not a code
problem** — the firmware has outgrown the 1.25 MB app partition that most
default schemes give you.

Fix it in one of two ways:

| | |
|---|---|
| **Switch the board profile** | The `XIAO_*` profiles map to larger layouts (3 MB app). For the **T-Dongle C5**, select **`XIAO_ESP32C5`** — despite the name it is the profile that fits, and the sketch has a hardcoded pinmap so nothing board-specific comes from the choice. |
| **Raise the partition scheme** | **Tools → Partition Scheme → `Huge APP (3MB No OTA/1MB SPIFFS)`** |

Which targets need it:

| Firmware | Board target | Default | With Huge APP |
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

Confirm it worked by checking the reported ceiling — you want
`Maximum is 3342336 bytes`, not `Maximum is 1310720 bytes`. If it still reads
1310720, the change didn't take; reselect the board.

> Shipping a `partitions.csv` with the sketch does **not** fix this. The IDE's
> size check reads `upload.maximum_size` from the board menu and never
> recomputes it from a partition file, so the build would still be rejected.

#### `fatal error: LovyanGFX.hpp: No such file or directory`

Only the Waveshare C6 port uses LovyanGFX. Install it — see [`LIBRARIES.md`](LIBRARIES.md).

#### NimBLE link errors, or `esp_deep_sleep_enable_gpio_wakeup` undefined

You are on **esp32 core 4.x**. Downgrade to **3.x** in Boards Manager.

#### Missing `NimBLEDevice.h` even with `bleEnabled=false`

NimBLE-Arduino is a **compile-time** dependency on any BT-capable chip, not just
when BLE is switched on — `PIGLET_HAS_BLE` keys off the chip's `CONFIG_BT_ENABLED`
and `bleEnabled` only gates it at runtime. Install NimBLE-Arduino **2.x** (1.x
has an incompatible API).

#### T-Dongle builds but its screen, SD, or GPS don't work

You built the wrong sketch. The dongle needs
`TDongleC5_Piglet/TDongleC5_Piglet.ino`, not `Arduino Files/Piglet/Piglet.ino` —
they have different display drivers and pinmaps.

### Where to Order

I always order everything from JLCPCB because i get the best deals.  However i do understand that people prefer the all in one project offers of PCBway so i have created this project here:

[PCBWayProjects](https://www.pcbway.com/project/shareproject/Piglet_Opensource_Wardriving_Project_1a21b94b.html)


## License

Creative Commons Attribution-NonCommercial 4.0 (CC BY-NC 4.0)

You may:

- Use  
- Modify  
- Share  

You may **not** use this project for commercial purposes.

https://creativecommons.org/licenses/by-nc/4.0/

---

Created by **Midwewest Gadgets LLC**

