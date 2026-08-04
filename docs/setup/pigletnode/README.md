# PigletNode — dedicated mesh node

A stripped-down firmware in [`PigletNode/`](../../../PigletNode/) that boots
straight into mesh Node mode. No display, no GPS, no SD card, **no config file**.

Use this instead of a main-sketch node when a board's only job is to be a node.
Less to configure, less to go wrong.

## There is no wardriver.cfg here

That is not an omission — PigletNode has no config parser at all. Everything is
compiled in:

| Setting | Value |
|---|---|
| Role | Node, always, from boot |
| Wi-Fi scanning | Always on |
| BLE scanning | Always on (no `bleEnabled` to set) |
| Scan role | Assigned by the Core over the mesh |
| Channels | Assigned by the Core |
| Dedup | Log-once forwarding, always on |

Everything you'd otherwise configure per-node lives in the **Core's**
`wardriver.cfg` instead — see [`../piglet-xiao/core.cfg`](../piglet-xiao/core.cfg)
or [`../tdongle-c5/core.cfg`](../tdongle-c5/core.cfg).

## Flashing

1. esp32 core **v3.x** and NimBLE-Arduino **2.x** (see [`LIBRARIES.md`](../../../LIBRARIES.md)).
2. Board: **ESP32C5 Dev Module** (or `XIAO_ESP32C5` for a XIAO C5 board).
3. **Set Tools → Partition Scheme → Huge APP.** PigletNode is ~1.4 MB and
   overflows the default 1.25 MB partition at ~106%. This is the single most
   common failure flashing this firmware — it is a menu setting, not a code
   problem.
4. Flash. It needs no SD card and no GPS.

## Bringing it into a cluster

1. Power the Core first.
2. Power the node. It searches ESP-Now channel 6 and registers itself.
3. The Core prints the node's MAC and a paste-ready role line:
   `[CORE]   add to /wardriver.cfg: node.A1B2C3D4E5F6=both`
4. To dedicate it to one radio, paste that into the **Core's** config with
   `wifi`, `ble`, or `both`, then reboot the Core.

A PigletNode honours any role immediately — unlike a main-sketch or T-Dongle
node, it has no local `bleEnabled` to also switch on.

## Serial

115200. Every 10 seconds:

```
[NODE] Searching... (req interval 2000 ms)      <- not yet paired
[BLE] scan window start (5 s)
[MESH] forwarded 12 BLE obs (134 total)
```

If it never leaves "Searching", check that the Core is powered, is actually in
Core mode, and that nothing has moved it off channel 6.

## Known interop limit

PigletNode pairs with a **Piglet** Core (a XIAO or T-Dongle running Core mode).
It only half-works with a real **Biscuit Pro** Core: it pairs and forwards
Wi-Fi and BLE, but Biscuit sends channel and role assignments over an encrypted,
proprietary ESP-Now channel Piglet can't read, so the node gets no channel range
and no stable heartbeat and will repeatedly time out and re-search. Use a Piglet
Core for cluster control.
