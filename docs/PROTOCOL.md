# JCMK ESP-Now mesh protocol — message types

Piglet's mesh is wire-compatible with the JCMK / Biscuit Pro coordinator family.
All frames begin with the 4-byte magic `ENOW` followed by a 1-byte type. Nodes
forward observations to a single Core, which GPS-stamps and logs them.

## Message types

| Type | Name | Direction | Size | Purpose |
|---|---|---|---|---|
| 1 | `CORE_REQUEST` | Node → Core | 212 | Node looking for a Core |
| 2 | `CORE_REPLY` | Core → Node | 212 | Core acknowledges, pairs |
| 3 | `HEARTBEAT` | Node → Core | 212 | Keep-alive |
| 4 | `TEXT` | Node → Core | 212 | Wi-Fi observation, CSV `BSSID,SSID,AUTH,CH,RSSI,W` |
| 5 | `ADMIN` | Core → Node | varies | Channel/role assignment |
| 6 | `BLE_OBS` | Node → Core | **212** | BLE observation (Piglet extension) |

Types 1–5 are unchanged from the upstream JCMK protocol. **Type 6 is a Piglet
addition** — legacy/third-party Cores (Biscuit Pro, JCMK C5) dispatch on type
with no default case, so they accept the 212-byte length and silently drop the
frame. No interop is broken in any direction.

## Type 6 — `jcmk_ble_obs_msg_t` (frozen, 212 bytes)

Defined in `Arduino Files/Piglet/JcmkBle.h`. **Do not reorder or resize existing
fields** — append only, via the `_pad` block, and shrink `_pad` to match so the
total stays 212. A `static_assert` enforces the size.

| Offset | Field | Type | Notes |
|---|---|---|---|
| 0  | `magic` | `char[4]` | `ENOW` |
| 4  | `type` | `uint8_t` | 6 |
| 5  | `counter` | `uint32_t` | monotonic per sender |
| 9  | `len` | `uint16_t` | active prefix length (informational) |
| 11 | `bda` | `uint8_t[6]` | big-endian (display order) |
| 17 | `addrType` | `uint8_t` | 0=public 1=random 2=RPA 3=NRPA |
| 18 | `channel` | `uint8_t` | 37/38/39 |
| 19 | `rssi` | `int8_t` | dBm |
| 20 | `mfgrId` | `uint16_t` | AD 0xFF company id, LE-decoded |
| 22 | `observedAtMsRel` | `uint32_t` | node's observe time (advisory; Core re-stamps) |
| 26 | `name` | `char[33]` | null-terminated |
| 59 | `serviceUuids` | `char[64]` | `;`-joined 16-bit UUIDs, null-terminated |
| 123 | `_pad` | `uint8_t[89]` | pad to 212 |

The Core never trusts `observedAtMsRel` for geotagging — it stamps each forwarded
observation with its own GPS fix and UTC time, identical to the Wi-Fi `TEXT` path
(nodes carry no GPS). Pack/unpack is via `jcmkBleBuild` / `jcmkBleParse`, covered
by `test/test_jcmk_ble.cpp`.
