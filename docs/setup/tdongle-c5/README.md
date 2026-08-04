# Piglet on the LilyGo T-Dongle C5

A self-contained single-file port in
[`TDongleC5_Piglet/`](../../../TDongleC5_Piglet/), with its own ST7735 TFT
driver, APA102 LED, and web UI. Feature parity with the XIAO firmware apart from
the hardware it doesn't have.

| Role | Config | Needs GPS | Needs SD |
|---|---|---|---|
| Standalone | [`standalone.cfg`](standalone.cfg) | yes | yes |
| Mesh Core | [`core.cfg`](core.cfg) | yes | yes |
| Mesh Node | [`node.cfg`](node.cfg) | no | no |

**Hardware:** LilyGo T-Dongle C5 — ESP32-C5, built-in 0.96" ST7735 TFT, APA102
LED, microSD slot in the shell.

## 1. Install the toolchain

Same as the XIAO: esp32 core **v3.x** (not 4.x), plus the libraries in
[`LIBRARIES.md`](../../../LIBRARIES.md). This port uses the Adafruit
ST7735/ST7789 driver rather than SSD1306.

**NimBLE-Arduino must be installed even if you never enable BLE.** On the C5 the
observer is compiled in unconditionally; `bleEnabled` only gates it at runtime.

## 2. Select the board — read this one carefully

Select **`XIAO_ESP32C5`**.

The dongle is not a XIAO, but that profile is the one that fits: it uses an 8 MB
layout with a **3 MB app partition**. The obvious-looking `ESP32C5 Dev Module`
defaults to a 4 MB layout with only a **1.25 MB** app partition, and this
firmware is ~1.75 MB with BLE compiled in — it will fail to link with
*"text section exceeds available space"*.

If you prefer `ESP32C5 Dev Module`, set **Tools → Partition Scheme → Huge APP**.

Verified against real hardware: ESP32-C5 rev v1.0 with 16 MB flash. Writing the
8 MB layout to a 16 MB chip is fine — the remainder is simply unused. (Flash
size is also a quick way to tell a dongle from a XIAO C5, which has 4 MB.)

## 3. Wire up GPS

Connect any UART GPS module to the Qwiic/JST connector:

| Signal | Pin |
|---|---|
| GPS TX → dongle RX | GPIO12 |
| GPS RX → dongle TX | GPIO11 |
| Power | 3.3 V |

GPS antenna mount STLs are in [`TDongleC5_Piglet/GPS STL/`](../../../TDongleC5_Piglet/GPS%20STL/).

## 4. Configure

Copy the `.cfg` for your role to the SD card root as `wardriver.cfg`. The dongle
reads the same format as the XIAO firmware, minus three keys it has no hardware
for: `board` (fixed pinmap), `battPin` and `batteryTest` (no battery). Unknown
keys are ignored, so a config file can be shared between a dongle and a XIAO.

Most keys are also editable from the web UI **Config** panel.

## 5. First boot

Serial at **115200**. A healthy standalone boot looks like:

```
[CFG] Loaded config:
[GPS] Checking wiring...
[GPS] UART OK — chars=... sentences=... failed=0
[SD] Log file: OK
=== Boot complete ===
```

With no GPS attached you'll instead get, every 10 seconds:

```
[GPS] chars=0 ok=0 fail=0 sats=0 fix=NO
[GPS]   No data — RX=GPIO12 not receiving. Check GPS TX wire.
```

That is the diagnostic working correctly, not a fault — it means nothing is
arriving on GPIO12.

## Enabling BLE — two settings, not one

BLE needs **both** of these:

```
bleEnabled=true
meshModeOnBoot=none      # or 'node'
```

`bleEnabled=true` on its own does nothing if the dongle boots as a **Core**,
because a Core never scans BLE locally by design — it only logs what its Nodes
forward. This catches people out: the config looks right and no BLE ever
appears. Run standalone (or as a Node) to scan BLE on the dongle itself.

Once enabled, the status page grows a `BLE:` row and `Type=BLE` rows appear in
the same CSV as Wi-Fi.

## Using a dongle as a mesh Node

Use [`node.cfg`](node.cfg). The Core assigns its scan role (`wifi` / `ble` /
`both`) over the mesh — see the [XIAO cluster
walkthrough](../piglet-xiao/README.md#building-a-cluster), which is identical
from the Core's side. Keep `bleEnabled=true` on the dongle or a `ble`/`both`
role has nothing to drive.

## Status

BLE on the dongle is **compile-verified and boot-verified, not field-tested.**
The firmware flashes, boots, mounts SD and logs, but no BLE capture run has been
done on hardware yet — and node-mode BLE/ESP-Now coexistence is the tightest
radio scenario in the design. Treat a first outing as a test.
