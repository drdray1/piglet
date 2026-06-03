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

Types 1–4 are unchanged from the upstream JCMK protocol. **Type 5 carries a
backward-compatible Piglet extension** (a trailing `role` byte, see below).
**Type 6 is a Piglet addition** — legacy/third-party Cores (Biscuit Pro, JCMK
C5) dispatch on type with no default case, so they accept the 212-byte length
and silently drop the frame. No interop is broken in any direction.

## Type 5 — `jcmk_admin_msg_t` (channel + role)

Defined in `Arduino Files/Piglet/MeshNode.cpp` (and a byte-identical copy in
`PigletNode/PigletNode.ino`). The Core sends this to JCMK/Piglet nodes at join
and on a periodic refresh. Biscuit nodes instead get the Biscuit role+config
pair (type 5 text payload + type 10), selected by `isBiscuit`.

| Offset | Field | Type | Notes |
|---|---|---|---|
| 0  | `magic` | `char[4]` | `ENOW` |
| 4  | `type` | `uint8_t` | 5 |
| 5  | `assignment_version` | `uint8_t` | bumped on channel reassignment |
| 6  | `node_index` | `uint8_t` | this node's index |
| 7  | `node_count` | `uint8_t` | active node count |
| 8  | `start_channel_idx` | `uint8_t` | index into the channel table |
| 9  | `end_channel_idx` | `uint8_t` | index into the channel table |
| 10 | `role` | `uint8_t` | **Piglet extension:** 0=both, 1=wifi, 2=ble |

The legacy frame is 10 bytes (no `role`); Piglet's is 11. **Back-compat is
length-guarded both ways:** a Piglet node accepts an admin frame of length ≥ 10
(applies channels) and reads `role` only when length ≥ 11, so a 10-byte frame
from an old/third-party Core leaves the node at its default role (`both`). A
third-party node reading its own 10-byte struct ignores the trailing byte.
Channel fields are applied only when `assignment_version` advances; `role` is
applied unconditionally (idempotent). Role is set per-node from the Core's
`/wardriver.cfg` (`node.<MAC>=wifi|ble|both`); editing it requires a Core reboot.

**PigletNodes are flagged Biscuit.** A PigletNode sends full-size (212-byte)
`CORE_REQUEST`/`TEXT`/`HEARTBEAT` frames, so the Core's size heuristic classifies
it as a Biscuit node and drives it via the Biscuit role+config path (type 5 text
+ type 10 `MSG_CONFIG_UPDATE`) instead of the binary type-5 admin above. To carry
the role over that path, the Core appends `;role=wifi|ble|both` to the type-10
`channels=...;dwell=...` string, and PigletNode parses it (`role=` token, value
terminated by `;` or end). Real Biscuit nodes ignore the unknown key. So a Piglet
node receives its role whether it is classified JCMK (type-5 `role` byte) or
Biscuit (type-10 `;role=`); both set the same node-side role and are idempotent.

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
