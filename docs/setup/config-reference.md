# `wardriver.cfg` key reference

Plain text, one `key=value` per line, `#` starts a comment. Lives at the root of
the SD card as `/wardriver.cfg`. Unknown keys are ignored, so one file can be
shared across boards. Unless noted, changes take effect on **reboot**.

Ready-made examples: [`piglet-xiao/`](piglet-xiao/) · [`tdongle-c5/`](tdongle-c5/) ·
[`waveshare-c6/`](waveshare-c6/). PigletNode has no config file — see
[`pigletnode/`](pigletnode/).

**Columns:** X = XIAO (main firmware) · T = T-Dongle C5 · W = Waveshare C6

## Uploads and networking

| Key | Default | X | T | W | Meaning |
|---|---|:-:|:-:|:-:|---|
| `homeSsid` | *(empty)* | ✅ | ✅ | ✅ | Network joined at boot to upload. Blank skips uploading. |
| `homePsk` | *(empty)* | ✅ | ✅ | ✅ | Password for the above. |
| `wigleBasicToken` | *(empty)* | ✅ | ✅ | ✅ | Full `Basic ...` token from <https://wigle.net/account>. |
| `wdgwarsApiKey` | *(empty)* | ✅ | ✅ | ❌ | WatchdogsGoWars API key. |
| `maxBootUploads` | `-1` | ✅ | ✅ | ❌ | `-1` all, `0` never, `1-100` cap per boot. |
| `wardriverSsid` | `Piglet-WARDRIVE` | ✅ | ✅ | ✅ | Fallback AP name (`http://192.168.4.1`). |
| `wardriverPsk` | `wardrive1234` | ✅ | ✅ | ✅ | Fallback AP password. |
| `autoStartAfterUpload` | `false` | ✅ | ✅ | ❌ | Drop the home link right after uploads and start scanning, instead of waiting for it to time out. Skipped in mesh mode. |

## Scanning and logging

| Key | Default | X | T | W | Meaning |
|---|---|:-:|:-:|:-:|---|
| `scanMode` | `aggressive` | ✅ | ✅ | ✅ | `aggressive` ≈ every 4.5 s, `powersaving` ≈ every 12 s. |
| `speedUnits` | `mph` | ✅ | ✅ | ✅ | `mph` or `kmh`. Display only. |
| `gpsBaud` | `9600` | ✅ | ✅ | ✅ | GPS UART baud. Most modules are 9600. |
| `deviceName` | *(empty)* | ✅ | ✅ | ❌ | Added to the CSV filename and WiGLE header. |
| `board` | `auto` | ✅ | ❌ | ❌ | `auto`/`s3`/`c5`/`c6`/`c3`/`exp`. T-Dongle and Waveshare have fixed pinmaps. |
| `rotateScreen180` | `false` | ✅ | ✅ | ❌ | Flip the display for an upside-down mount. |
| `battPin` | `-1` | ✅ | ❌ | ❌ | Battery ADC pin; `-1` disables. No battery on a dongle. |
| `batteryTest` | `false` | ✅ | ❌ | ❌ | Log seconds on battery power. |

## Mesh

| Key | Default | X | T | W | Meaning |
|---|---|:-:|:-:|:-:|---|
| `meshModeOnBoot` | `none` | ✅ | ✅ | ❌ | `none`, `core`, or `node`. `core`/`node` skip the AP window entirely. |
| `nodeDefaultRole` | `both` | ✅ | ✅ | ❌ | **Core only.** Role for any node not listed: `wifi`, `ble`, `both`. |
| `node.<12hex>` | — | ✅ | ✅ | ❌ | **Core only.** Per-node role by full MAC, no colons: `node.A1B2C3D4E5F6=ble`. Up to 8 entries. |

Wire format: [`../PROTOCOL.md`](../PROTOCOL.md).

## BLE

| Key | Default | X | T | W | Meaning |
|---|---|:-:|:-:|:-:|---|
| `bleEnabled` | `false` | ✅ | ✅ | ❌ | Master switch. A **Core never scans BLE locally** whatever this says. |
| `bleScanDuration` | `5` | ✅ | ✅ | ❌ | Window length in seconds, 1–10. Wi-Fi scanning pauses during a window. |
| `bleScanInterval` | `30` | ✅ | ✅ | ❌ | Seconds between windows. Forced to ≥ `bleScanDuration + 5`. |
| `bleDedupeWindow` | — | ✅ | ✅ | ❌ | **Deprecated.** Parsed and ignored; dedup is log-once, not time-windowed. |

PigletNode always scans BLE and has no switch.

## Filtering

| Key | Default | X | T | W | Meaning |
|---|---|:-:|:-:|:-:|---|
| `dedupEnabled` | `true` | ✅ | ✅ | ❌ | Log each BSSID/BLE MAC once per boot. `false` logs every sighting. |
| `bleMaxResults` | `200` | ✅ | ✅ | ❌ | Dedup ring capacity, 100–2000. Also caps the BLE hand-off queue. |
| `blacklistMac` | — | ✅ | ✅ | ❌ | Never log this MAC. Repeatable, up to 16. Colons or bare 12-hex. |
| `blacklistSsid` | — | ✅ | ✅ | ❌ | Never log this SSID (also matches BLE names). Repeatable, up to 16. |

Blacklisting runs **before** dedup, so a blacklisted device never occupies a
ring slot. Both are enforced at CSV write time, so on a Core they filter
mesh-forwarded observations too. Matching is exact and case-insensitive — no
wildcards, so `MyHomeNet` does not filter `MyHomeNet2`.

```
blacklistMac=AA:BB:CC:DD:EE:FF    # My phone
blacklistSsid=MyHomeNet           # Home network
```

An inline `# label` is preserved when the firmware re-saves the file.

## Gotchas

- **`bleEnabled=true` on a Core does nothing.** Cores log what Nodes forward and
  never scan locally. For BLE from one device, run it standalone.
- **A node needs its own `bleEnabled=true`** to honour a `ble`/`both` role. The
  role gates a scanner that must already be enabled. PigletNode is the exception.
- **Role edits are read once at boot** — reboot the *Core* after editing.
- **`dedupEnabled=false` makes CSVs much larger.** It is what you want for
  dwell-time or RSSI-over-time work, and wrong for routine wardriving.
