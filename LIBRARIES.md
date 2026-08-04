# Arduino libraries

Install via the Arduino IDE Library Manager
(`Sketch → Include Library → Manage Libraries…`).

## Core (always required)

| Library | Author | Notes |
|---|---|---|
| Adafruit SSD1306 | Adafruit | OLED display |
| Adafruit GFX Library | Adafruit | OLED graphics |
| Adafruit BusIO | Adafruit | I²C/SPI shim for the Adafruit libs |
| TinyGPSPlus | Mikal Hart | GPS NMEA parsing |
| ArduinoJson | Benoit Blanchon | WebUI `/status.json` etc. |

(The T-Dongle C5 variant swaps the SSD1306 driver for the Adafruit ST7735/ST7789
library — see the T-Dongle section in the README. On the C5 it needs
**NimBLE-Arduino installed to compile at all**, whatever `bleEnabled` is set to:
`PIGLET_HAS_BLE` keys off the chip's `CONFIG_BT_ENABLED`, so the observer is
built in and `bleEnabled` only gates it at runtime.)

## BLE wardriving (only for `bleEnabled=true` builds)

| Library | Author | Version | Notes |
|---|---|---|---|
| NimBLE-Arduino | h2zero | **2.1.x** | Passive BLE observer. Not needed when `bleEnabled=false` — the firmware compiles and runs without it as long as the BLE code paths are not enabled by the board's BT support. |

NimBLE-Arduino 2.x is required (the API differs from 1.x — time parameters are in
milliseconds, `onResult` takes a `const NimBLEAdvertisedDevice*`). The default
NimBLE configuration works; advanced users can trim flash by building NimBLE in
observer-only mode (see `piglet_bluetooth_implementation.md` §5).

> BLE is present on every current Piglet SoC (ESP32-S3 / C5 / C6). The code is
> guarded by `PIGLET_HAS_BLE`, derived from the Arduino core's `CONFIG_BT_ENABLED`,
> so a hypothetical BT-less target drops all BLE code automatically.
