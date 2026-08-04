# Piglet on XIAO ESP32 (S3 / C5 / C6 / C3)

The main firmware, in [`Arduino Files/Piglet/`](../../../Arduino%20Files/Piglet/).
Runs standalone, as a mesh Core, or as a mesh Node.

| Role | Config | Needs GPS | Needs SD |
|---|---|---|---|
| Standalone | [`standalone.cfg`](standalone.cfg) | yes | yes |
| Mesh Core | [`core.cfg`](core.cfg) | yes | yes |
| Mesh Node | [`node.cfg`](node.cfg) | no | no |

## 1. Install the toolchain

1. Arduino IDE → **Preferences → Additional Boards Manager URLs**, add:
   `https://espressif.github.io/arduino-esp32/package_esp32_dev_index.json`
2. **Tools → Board → Boards Manager** → install **esp32 by Espressif**.
   Use **v3.x**. Core 4.x does not build this firmware.
3. Install the libraries listed in [`LIBRARIES.md`](../../../LIBRARIES.md).
   NimBLE-Arduino must be **2.x** — the 1.x API is incompatible.

## 2. Select the board

Pick the entry matching your hardware: **XIAO_ESP32S3**, **XIAO_ESP32C5**,
**XIAO_ESP32C6**, or **XIAO_ESP32C3**.

**Enable PSRAM** (Tools → PSRAM): *OPI PSRAM* on C5/C6, *QSPI PSRAM* on S3.
HTTPS uploads to WiGLE need it.

**The C6 and C3 need Tools → Partition Scheme → Huge APP.** At the default
scheme they overflow (133% and 121%) and fail with *"text section exceeds
available space"*. S3 and C5 fit at their defaults (44% / 54%). See the [build
matrix](../README.md#verified-build-matrix).

## 3. Wire it up

Pinouts per board are in the [main README](../../../README.md#wiring--pinouts).
You need a UART GPS module and a microSD slot for standalone and Core roles.
A Node needs neither — it only talks over ESP-Now.

## 4. Configure

Copy the `.cfg` for your role to the SD card root as `wardriver.cfg`, then edit:

- `homeSsid` / `homePsk` — the network to upload from at boot. Leave blank to skip.
- `wigleBasicToken` — from <https://wigle.net/account>, "Show my token". Include
  the whole `Basic ...` string.
- `board` — leave `auto` unless detection picks wrong.

## 5. First boot

Open Serial at **115200**. A healthy boot prints roughly:

```
[CFG] Loaded config:
[GPS] UART OK — chars=... sentences=... failed=0
[SD] Log file: OK
=== Boot complete ===
```

Common first-boot problems:

| Symptom | Cause |
|---|---|
| `[GPS] WARNING: No data on RX=GPIO..` | GPS TX not wired to the listed RX pin, or module unpowered |
| `chars>0` but `failed > ok` | Wrong `gpsBaud` (most modules are 9600) |
| GPS data but no fix | Needs sky view; cold start can take minutes |
| `[SD] ... FAIL` | Card not FAT32, or not seated — these slots are reseat-sensitive |

No GPS fix is not an error at this stage — rows are stamped 0,0 until one
arrives, and a fix that drops mid-drive falls back to the last known position
for up to 3 minutes.

## Building a cluster

1. Flash one board with `core.cfg`, attach the GPS and SD.
2. Flash the others with `node.cfg` (or use the simpler
   [PigletNode](../pigletnode/) firmware).
3. Power the Core first, then the Nodes. They find it on ESP-Now channel 6.
4. Watch the Core's Serial as each Node joins — it prints a ready-to-paste
   role line:
   `[CORE]   add to /wardriver.cfg: node.A1B2C3D4E5F6=both`
5. To dedicate radios, paste those lines into the **Core's** `wardriver.cfg`
   with `wifi` / `ble` / `both`, and reboot the Core.

A node also needs its own `bleEnabled=true` to honour a `ble` or `both` role —
the role only gates a scanner that is already compiled in and enabled.

> **Careful:** on a Core booted via `meshModeOnBoot=core`, navigating to OLED
> page 5 calls `enterNodeMode()` and drops it into a searching Node. Reboot to
> recover. Don't visit page 5 on a Core.

## Enabling BLE

Set `bleEnabled=true` and reboot. Standalone, BLE windows are interleaved with
the Wi-Fi sweep and `Type=BLE` rows land in the same CSV. On a Node, BLE
observations are forwarded to the Core instead.

Setting `bleEnabled=true` on a **Core** does nothing — a Core never scans BLE
locally. Run standalone if you want BLE from one device.
