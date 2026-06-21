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

## Tech Stack

| Layer | Tools |
|---|---|
| Language | C11 |
| Build system | CMake 3.16+ |
| Compilers | GCC (Debug/Release/ARM builds), Clang 18 (required for the fuzz harness) |
| Networking | Hand-rolled binary protocol over UDP sockets (POSIX), network byte order, CRC-16/CCITT-FALSE |
| Storage | Custom log-structured key/value store (flash/NVS emulation, no external library) |
| Testing | Custom ~20-line assert/count harness, CTest, AddressSanitizer + UndefinedBehaviorSanitizer |
| Fuzzing | libFuzzer (via Clang) |
| Static analysis | cppcheck |
| Style | clang-format |
| Cross-compilation | `arm-linux-gnueabihf-gcc` (armhf toolchain) |
| CI/CD | GitHub Actions (4 jobs: build+test, static-analysis, cross-compile-arm, fuzz-smoke-test) |
| Tooling | Python 3 (`device_sim.py` — independent protocol reimplementation used for integration testing) |
| License | MIT |

No third-party C library appears anywhere in the core — CRC, log-structured
storage, the state machine, and the test harness are all written from
scratch. That's a deliberate constraint, not an oversight: minimizing
dependencies matters far more on a target with a real flash budget than
it does on a host build.

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
| `fuzz/fuzz_protocol.c` | libFuzzer harness exercising `gw_proto_validate()` against arbitrary input — the network-facing trust boundary. |
| `cmake/toolchain-arm-linux-gnueabihf.cmake` | CMake toolchain file for cross-compiling the whole project to 32-bit ARM Linux. |

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

Code style follows `.clang-format` (run `clang-format -i src/*.c
include/*.h` before committing); `cppcheck` is run in CI on every push
(see [Static analysis](#static-analysis)).

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

## Cross-compiling for ARM

The whole point of "embedded" is that it doesn't just run on the dev
box. `cmake/toolchain-arm-linux-gnueabihf.cmake` cross-compiles the full
project — core library, `gateway_main`, and the test binaries — for
32-bit ARM Linux (`armhf`), the architecture family behind most
router/IoT-gateway SoCs:

```bash
cmake -B build-arm -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-arm-linux-gnueabihf.cmake
cmake --build build-arm
file build-arm/gateway_main
```

Verified clean (zero warnings under the same `-Wall -Wextra -Werror
-Wshadow` used everywhere else) with `arm-linux-gnueabihf-gcc`:

```
build-arm/gateway_main: ELF 32-bit LSB pie executable, ARM, EABI5 ...
   text    data     bss     dec     hex
  12175     464       4   12643    3163
```

This is a *build*-only sanity check, not target validation — see
[Notes on the host-vs-target boundary](#notes-on-the-host-vs-target-boundary)
for what that distinction means here.

## Fuzzing the wire protocol parser

`gateway_protocol.c`'s `gw_proto_validate()` is the actual trust
boundary of this project: it's the first thing that touches bytes
pulled straight off a UDP socket, before anything is assumed about
them. `fuzz/fuzz_protocol.c` is a small libFuzzer harness that feeds it
(and the field accessors built on top of it) arbitrary, attacker-shaped
input:

```bash
cmake -B build-fuzz -DCMAKE_C_COMPILER=clang -DENABLE_FUZZING=ON
cmake --build build-fuzz --target fuzz_protocol
./build-fuzz/fuzz_protocol -max_total_time=60
```

Run locally under AddressSanitizer + UBSan: **42,585,945 executions in
31 seconds, zero crashes, zero sanitizer findings.** CI runs a 30-second
smoke pass on every push as a regression guard, not a substitute for a
real fuzzing campaign — the value here is that malformed/truncated/
corrupted packets are something this project actively tests for, not
just something the CRC check is hoped to catch.

## Static analysis

```bash
cppcheck --enable=warning,style,performance,portability --error-exitcode=1 -I include src/
```

Clean — no findings — and run in CI on every push. Catches a different
class of issue than the sanitizers (e.g. parameters that should be
`const`, which several were until cppcheck flagged them).

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
- **Cross-compilation**: implemented — `cmake/toolchain-arm-linux-gnueabihf.cmake`
  cross-compiles cleanly for `armhf` (see [Cross-compiling for ARM](#cross-compiling-for-arm)).
  What's still simulated either way is the *target*: this proves the
  code has no x86-only assumptions, not that it's been run on actual
  router/IoT hardware.

## Possible extensions

- Replace the file-backed `nvs_store` with an actual flash block driver
  behind the same API (the one module designed to be swapped).
- Run the fuzz harness against an actual target build under qemu-user,
  rather than only on the host build.
- Multiple watchdog-monitored tasks beyond `net_rx` (e.g. a periodic
  NVS-compaction task) to exercise more than one entry in the table.
