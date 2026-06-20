# IoT Gateway Device & Connectivity Manager

A C11 subsystem for the kind of job a home-networking gateway / router / IoT
hub actually does underneath the web UI: keep a table of client devices,
know whether each one is actually alive, recover gracefully when it isn't,
and not lose that state when the box reboots.

This is a **host-simulated embedded system**: it runs as a normal Linux
process and uses a UDP socket and a regular file where real firmware would
use a radio driver and a flash partition. The module boundaries are drawn
so that swapping either of those for real hardware touches exactly one
file (`gateway_protocol.c` for the radio/transport, `nvs_store.c` for
flash) and nothing else. See [Notes on the host-vs-target boundary](#notes-on-the-host-vs-target-boundary)
below for specifics.

## Why these four problems

Four things tend to actually break in connectivity firmware, so the
project is built around them:

1. **A device doesn't cleanly disconnect or reconnect** — it just goes
   quiet, or it half-reconnects and you get duplicate state. →
   [`state_machine.c`](src/state_machine.c)
2. **Flash can't be rewritten in place** — every "save" is actually an
   append, and something has to reclaim space later without corrupting
   live data. → [`nvs_store.c`](src/nvs_store.c)
3. **A subsystem can wedge without crashing** — the process is alive but
   a task inside it has stopped making progress, and nothing notices. →
   [`watchdog.c`](src/watchdog.c)
4. **A device table has to stay correct as devices churn**, in fixed
   memory, with no allocator surprises at 3am. → [`device_registry.c`](src/device_registry.c)

`gateway_protocol.c` ties them together with a small CRC-validated binary
protocol, and `main.c` runs it all from a single-threaded `poll()` loop —
the same shape as a typical embedded event loop, just without an RTOS
underneath it.

## Architecture

```
                    UDP :9500
                       │
                       ▼
            ┌─────────────────────┐
            │  gateway_protocol    │  build / validate / CRC16
            │  (52B packed wire    │  network-byte-order packets
            │   format)            │
            └──────────┬───────────┘
                       │ validated packet
                       ▼
            ┌─────────────────────┐        ┌──────────────────┐
            │  device_registry      │◄──────►│  state_machine    │
            │  fixed array,         │ event  │  UNKNOWN→DISCOVER-│
            │  GW_MAX_DEVICES=64    │        │  ING→CONNECTED→   │
            └──────────┬───────────┘        │  DEGRADED→        │
                       │                     │  RECONNECTING→    │
                       │ serialize/          │  OFFLINE          │
                       │ deserialize         └──────────────────┘
                       ▼
            ┌─────────────────────┐
            │  nvs_store             │  append-only log + compaction,
            │  (flash emulation)     │  survives process restart
            └─────────────────────┘

            ┌─────────────────────┐
            │  watchdog               │  per-task kick/check,
            │  (net_rx task, ...)     │  soft recovery callback
            └─────────────────────┘
```

`main.c`'s loop, once per tick: `poll()` the socket → validate + route any
packet through the registry/state machine → kick the watchdog → sweep
stale devices on a timer → check the watchdog. `SIGINT`/`SIGTERM` trigger
a clean shutdown that serializes the live device table into NVS and
compacts the log before exiting.

## Connectivity state machine

```
UNKNOWN ──DISCOVERED──► DISCOVERING ──HANDSHAKE_OK──► CONNECTED ─┐
                              │                            │ ▲   │ HEARTBEAT_OK (self-loop)
                       HANDSHAKE_FAIL                       │ │   │
                              │                  HEARTBEAT_TIMEOUT │
                              ▼                              ▼ │
                          OFFLINE ◄──────────────────── DEGRADED
                              ▲                              │
                              │                    HEARTBEAT_TIMEOUT
                       MAX_RETRIES /                          │
                       HEARTBEAT_TIMEOUT                      ▼
                              └──────────────────── RECONNECTING
```

A device that stops sending heartbeats doesn't disappear on the first
missed beat — it steps through `DEGRADED → RECONNECTING → OFFLINE`
across configurable timeout windows (`gw_registry_sweep_stale`), which
avoids evicting a device over one lost packet while still bounding how
long a truly dead device sits in the table. `USER_DISCONNECT` (an
explicit `BYE` message) and `HANDSHAKE_FAIL` short-circuit straight to
`OFFLINE`.

## Module reference

| Module | What it owns |
|---|---|
| `state_machine.c/h` | Pure transition-table logic for one device's connection lifecycle. No I/O, no allocation — easy to unit test exhaustively. |
| `device_registry.c/h` | Fixed `GW_MAX_DEVICES=64` table, allocated once. MAC-keyed lookup, stale-sweep escalation, and a flat serialize/deserialize format for warm-boot persistence. |
| `nvs_store.c/h` | Log-structured key/value store standing in for flash NVS: every write is an append, erase writes a tombstone record, `gw_nvs_compact()` reclaims space by rewriting only live keys. |
| `watchdog.c/h` | Per-task kick/deadline tracking with a configurable miss threshold before a *targeted* recovery callback fires — modeling "restart the network task" rather than "reboot the device." |
| `gateway_protocol.c/h` | 52-byte packed, CRC-16/CCITT-FALSE-checked binary message format in network byte order, for `DISCOVER` / `HANDSHAKE` / `HEARTBEAT` / `BYE`. |
| `main.c` | Single-threaded `poll()` event loop wiring the above together; clean shutdown persists state to NVS. |

## Building

```bash
cmake -B build
cmake --build build
```

Optional sanitizer build (recommended during development):

```bash
cmake -B build -DENABLE_SANITIZERS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

Compiles clean under `-Wall -Wextra -Werror -Wshadow` in both a sanitized
Debug build and a plain `-O3` Release build — no `Wstringop-truncation`,
no shadowed variables, no warnings suppressed.

## Testing

```bash
ctest --test-dir build --output-on-failure
```

5 suites, **194 assertions total**, all green under AddressSanitizer +
UndefinedBehaviorSanitizer:

```
[state_machine]    32 passed, 0 failed
[nvs_store]        31 passed, 0 failed
[device_registry]  99 passed, 0 failed
[watchdog]         19 passed, 0 failed
[protocol]         13 passed, 0 failed
```

No external test framework — `include/test.h` is a ~20-line
assert-and-count harness, in keeping with the "minimize dependencies"
mindset that matters more once a target has a real flash budget.

Coverage includes things that are easy to get subtly wrong: the
stale-eviction ladder is paced correctly across repeated sweep calls (a
real bug here regressed the original implementation during development —
caught by `test_device_registry.c` before it shipped), NVS replay
correctly reconstructs state including tombstoned/erased keys after a
simulated restart, and the wire protocol rejects single-bit corruption
anywhere in the payload.

## Running it / live demo

```bash
cmake --build build
./build/gateway_main &
python3 tools/device_sim.py --devices 3 --heartbeats 5 --interval 1
```

`tools/device_sim.py` is an independent Python implementation of the
wire format (own CRC-16 routine, own struct packing) — it doesn't import
any C code — so a successful exchange is a genuine interop check, not a
loopback of the same code against itself. Sample output:

```
gateway listening on UDP :9500 (0 device(s) restored)
[gw] kitchen-plug         -> DISCOVERING  (id=0, seq=1)
[gw] kitchen-plug         -> CONNECTED    (id=0, seq=2)
[gw] kitchen-plug         -> CONNECTED    (id=0, seq=3)
...
[gw] kitchen-plug         -> OFFLINE      (id=0, seq=6)
```

Send `SIGINT` (Ctrl-C) to the gateway while devices are connected, then
restart it — it reloads the device table from `gateway.nvs` and logs
`restored N device(s)` before the first packet even arrives. Verified
manually: a device connected in one run is present immediately on the
next run with no re-discovery needed.

## Verified footprint

Release build (`-O3`, no sanitizers): **`gateway_main` is 16,510 bytes**
(15,690 text / 804 data / 16 bss) on x86-64 — small enough that the
constraint that mattered most while writing this wasn't "will it fit,"
it was "don't reach for the heap or an unbounded buffer just because
this build has one to spare."

## Notes on the host-vs-target boundary

Being upfront about what's simulated and what would actually change on
real hardware:

- **Transport**: a UDP socket stands in for a Wi-Fi/Thread/BLE radio
  driver's RX path. `gateway_protocol.c`'s packet format and CRC don't
  change; only how bytes arrive (`gw_proto_validate()` is already
  decoupled from the socket call in `main.c`).
- **Storage**: a regular file stands in for a flash partition.
  `nvs_store.c`'s log-structured/append/compact design is the same
  shape ESP-IDF's NVS or a Zephyr settings backend uses — the file I/O
  in `gw_nvs_open`/`write_record` is the only part that would be
  replaced with a flash driver's erase/program/read calls.
- **Watchdog**: `on_net_wdt_fire()` currently just logs, because a host
  UDP socket can't wedge the way a hardware MAC/PHY can. On target, this
  callback would tear down and recreate the radio driver state.
- **Threading**: deliberately single-threaded with `poll()`, the same
  control-flow shape as a bare-metal or single-RTOS-task main loop —
  no thread-safety assumptions were taken that wouldn't hold on a
  single-core target.
- **Cross-compilation**: not wired up here, but `CMakeLists.txt` has no
  host-specific assumptions beyond `<arpa/inet.h>`/`<poll.h>`/`<sys/socket.h>`
  in `main.c` — a toolchain file plus a target-appropriate transport/NVS
  backend behind the same headers would be the actual porting work.

## Possible extensions

- Replace the file-backed `nvs_store` with an actual flash block driver
  behind the same API (the one module designed to be swapped).
- Add a `--cross` CMake toolchain file for `arm-none-eabi-gcc` and stub
  the socket layer for a target with a real Wi-Fi driver.
- Multiple watchdog-monitored tasks beyond `net_rx` (e.g. a periodic
  NVS-compaction task) to exercise more than one entry in the table.
