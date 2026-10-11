<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# ZXV on microcontrollers and RTOSes (`sdk/embedded`)

This folder is for firmware developers who want ZXV's exact rationals,
four-valued logic, capability-gated budgets, phase clock and a balanced
ledger on a microcontroller or a small gateway. It contains:

| folder | what it is |
|---|---|
| [`port/`](port) | **zxp**, the port layer. Mutex, entropy, time and allocator for POSIX, bare metal, FreeRTOS and Zephyr. Every libzxv allocation, lock and random byte goes through it. |
| [`zxe/`](zxe) | **zxe_ledger**, a fixed-memory ledger with libzxv's rules and status codes, and **zxe_clock**, which drives the phase clock from a hardware tick. |
| [`freertos-zxv/`](freertos-zxv) | A FreeRTOS owner task (ledger posting plus budget tick), run on the FreeRTOS POSIX simulator. See its [README](freertos-zxv/README.md). |
| [`zephyr-zxv/`](zephyr-zxv) | A Zephyr module (Kconfig, CMake) and the sample `ledger_budget`. See its [README](zephyr-zxv/README.md). |
| [`test/`](test) | Host tests, a differential test against libzxv, and a QEMU Cortex-M3/M4 target test. |
| [`tools/`](tools) | Size report, binary no-float check, and the source-list consistency check. |
| [`PROPOSED_LIBZXV_CHANGES.md`](PROPOSED_LIBZXV_CHANGES.md) | Changes to libzxv that would make this cleaner. **Not applied.** |

## Safety: advisory only

**Do not let ZXV actuate anything.** This applies to PLCs, grid switching,
water treatment, flight controls, medical devices, motor drives, valves,
breakers and anything else whose failure can hurt people or equipment.
ZXV's outputs are *recommendations*: a budget decision, a ledger balance, a
phase-clock event, a four-valued verdict. In a system that actuates:

- ZXV runs **beside** the control loop, never in it. The certified safety
  function (interlocks, safety PLC, protection relays, the flight-control
  computer, the medical device's own safety system) decides, independently
  of ZXV.
- A **human approves** any action that ZXV recommends and that the safety
  function would allow.
- When ZXV is closed, poisoned, out of memory or out of budget, the result
  is "no recommendation". It is never a default action.

Nothing here has been developed, assessed or certified to IEC 61508,
ISO 26262, DO-178C, IEC 62304, IEC 61511, EN 50128 or any other functional
safety standard. No safety integrity level, ASIL or DAL is claimed, and
none should be inferred from the tests.

**Timing.** There are no real-time guarantees. Nothing in this folder has a
measured or bounded worst-case execution time:

- libzxv allocates, takes mutexes and calls `vsnprintf` on error paths.
- 64-bit division goes through libgcc, whose running time depends on the
  data.

Measure on your hardware before relying on any timing, and keep ZXV out of
interrupt context and out of hard-deadline tasks.

## What runs where (findings)

All of this was measured, not estimated. The commands are under
"Reproduce".

| part of libzxv | MCU (Cortex-M0+/M4/M33, RV32) | gateway (Linux, Zephyr/FreeRTOS on a bigger part) |
|---|---|---|
| rationals, four-valued logic, hexagram tags, SHA3/SHAKE, Keccak | **yes**, no context and no heap | yes |
| context, capabilities, quarantine log | **yes** (heap: ctx 1,344 B, cap 56 B, hexlog(16) 1,952 B) | yes |
| rmag budgets | **yes**, but 34,960 B of heap per handle | yes |
| phase clock | **yes**, but 21,792 B per handle and growing (see RAM) | yes |
| libzxv's own ledger (`zxv_ledger_*`) | **no**: one kernel book is about 24 MB. Use `zxe_ledger`. | only with plenty of RAM (about 24 MB per currency segment) |
| ISO 20022 / card / EVM / econ helpers in the subset | compiled; whether they fit depends on the part | yes |
| ML-KEM-1024, ML-DSA-87, Ed25519 (`ZXE_PQC=1`) | builds (Cortex-M4: 114 KB flash), but ML-DSA-87 signing needs about 121 KB of stack: **gateway only** | yes |
| integer inference engine (tensor) | **no** (own thread pool, large models) | host only; not in the RTOS kits |

### How libzxv is built here (no fork, no copy)

The kits compile `sdk/libzxv/src/*.c` and the kernel modules it needs
straight from the tree. The list is in [`sources.mk`](sources.mk). The
Zephyr CMake keeps the same list, and `tools/check_lists.py` checks that
they agree. libzxv is unchanged. Its OS calls are rerouted as follows:

- `-include port/compat/zxp_prelude.h` renames `malloc`, `calloc`,
  `realloc`, `free`, `strdup` and `getentropy` to the `zxp_*` versions.
- `-I port/compat` puts the shim `pthread.h` first, mapping
  `pthread_mutex_*`/`pthread_cond_*` to zxp.
- `-DZXV_RANDOM_GETENTROPY` selects libzxv's `getentropy` branch, which the
  prelude routes to `zxp_getentropy`.
- `-Dretain=used` stops the kernel's bring-up records being kept on
  toolchains that honour `retain` (P7). Without it, rv32 keeps about 39 KB
  of unused static RAM.

The proof that the rerouting is complete is the **conformance run**.
libzxv's own C tests (`test_zxv_api`, `test_zxv_abi11/12/13`), unchanged,
are linked against a libzxv built through the port layer, with PQC and
engine on, and pass. A cleaner hook header is proposed as P1.

### Entropy: the integrator's job, fail closed

The port has **no default entropy source**. Until your firmware calls

```c
zxp_set_entropy(board_trng_read, NULL);   /* int fn(void *user, uint8_t *buf, size_t len), 0 = ok */
int rc = zxp_init();                      /* runs the start-up health test */
```

and `zxp_init()` returns `ZXP_OK`:

- `zxv_ctx_create()` returns `ZXV_E_INTERNAL`.
- `zxv_random()` returns `ZXV_E_IO`.
- Every mutex initialisation is refused, so no ZXV object can exist.

The health checks:

- **At start-up:** two 32-byte reads that must succeed and must differ.
- **Continuously:** every 16-byte block is compared with the previous one.
  A repeat, or any error from the source, latches the port **closed** until
  reset.

These tests catch a dead or stuck source. They do **not** make a weak
source strong. Use the part's TRNG or a vetted DRBG seeded from it.

Sources supplied:

| port | source | use |
|---|---|---|
| POSIX | `zxp_posix_entropy` (`getrandom`/`getentropy`) | Linux gateways, host tests |
| Zephyr | `CONFIG_ZXV_ENTROPY_CSPRNG` (`sys_csrand_get`, needs a real entropy driver; not offered when Zephyr would use its insecure test CSPRNG) | boards |
| Zephyr | `CONFIG_ZXV_ENTROPY_INSECURE_TEST` | QEMU/native_sim only; prints a warning |
| bare metal, FreeRTOS | none: install your TRNG | — |

The QEMU target test and the samples use clearly labelled **insecure,
emulator-only** sources, because the emulators have no TRNG.

### Threads: TLS or single-owner mode

libzxv keeps its last-error text in a `_Thread_local` buffer. FreeRTOS, bare
metal, and Zephyr with the GNU Arm Embedded toolchain have no thread-local
storage. For those, build with `ZXP_NO_TLS`, which the Makefiles and the
Zephyr module select automatically. In that mode:

- `_Thread_local` is compiled as nothing.
- The port enforces **single-owner use**. The first task that creates a
  ZXV mutex owns ZXV (or call `zxp_set_owner_current()`). A lock or mutex
  initialisation from any other task calls `zxp_fatal()`, which stops the
  firmware; it does not silently share state.
- The FreeRTOS kit shows the intended pattern: one owner task, and a request
  queue for the others.

With TLS, as on POSIX, ZXV is thread-safe as in the hosted library.

### Floats

No source in `port/`, `zxe/`, the samples or the tests uses a float type.
`check-nofloat-src` checks this; on hosts that support it, the sources are
also built with `-mgeneral-regs-only`. Every cross object is built
soft-float (`-mfloat-abi=soft`, `ilp32`), so any floating-point operation
appears as a libgcc helper call, and `check-nofloat-bin` lists those calls
per object.

| profile | soft-float helpers | why |
|---|---|---|
| `ZXE_PROFILE=exact` (default) | only in `zxv_api.o` and `triple_ledger.o` (Cortex-M4: 2,220 B flash; M0+: 7,596 B; rv32: 6,234 B), and only in the ledger probe | libzxv builds the kernel's triple-ledger mirror with `-DTEST_HOST`, where its number type is a `double` (exact for integers up to 2^53). The kits keep that so results are bit-identical to the hosted library. |
| `ZXE_PROFILE=q32` | **none** (ledger probe: Cortex-M0+ 42,231 B, M4 41,455 B, rv32 50,051 B) | the kernel's Q32.32 path. Amounts, rail totals and the context's total of all debits are capped at 2^31 - 1 minor units (`ZXV_E_RANGE` beyond; P2, applied), in libzxv and `zxe_ledger` alike. Use it when that lifetime volume is enough. |

The MCU set (`pure`, `core`) never links a float helper in either profile,
because `zxe_ledger` is integer-only.

### 64-bit division

`zxe_clock`, `iso20022`, `rational`, `rmag` and `zxv_core` call libgcc's
64-bit division (`__aeabi_ldivmod`/`__aeabi_uldivmod` on Arm, `__divdi3`
and related helpers on RV32). This is correct but slow on Cortex-M0+, and
its running time depends on the operands. Do not treat it as constant-time.

### RAM

**Static RAM** for the MCU set is about 2.3 KB on Arm and 1.7 KB on RV32,
including newlib-nano's own 556 B on Arm. The heap needs (32-bit, measured
on QEMU Cortex-M3/M4 by `test_target`) are:

| handle | heap bytes |
|---|---|
| `zxv_ctx` | 1,344 |
| `zxv_cap` | 56 |
| `zxv_rmag` | 34,960 |
| `zxv_phase` | 21,792 when created; the event table grows (below) |
| `zxv_hexlog` (16 entries) | 1,952 |
| test_target peak (ctx + caps + rmag + phase + hexlog + work) | 68,328 |

The phase clock reuses an event slot only after 4096 newer events. A clock
that schedules steadily therefore grows its table, by doubling, to about
196 KB. Bound the number of events you schedule, or see P4. The sizes come
from fixed kernel tables (P4).

Practical consequences:

- A context plus a ledger plus a budget fits on a 64 KB-RAM part.
- Adding a phase clock needs roughly 96 KB or more. The Zephyr sample on
  `qemu_cortex_m3` (64 KB) shows this: the phase clock is refused with
  `ZXV_E_NOMEM` and the sample continues without it.

Set a hard cap with `zxp_set_heap_limit()`. Read live, peak and failure
counts with `zxp_heap_stats()`.

**Stack** (from `-fstack-usage`, host-compiled for Cortex-M4 `-Os`; not
measured on a target):

| PQC function | stack bytes |
|---|---|
| ML-DSA-87 signing (`signature_internal`) | 121,352 |
| ML-DSA-87 key generation | 96,488 |
| ML-DSA-87 verification | 91,600 |
| ML-KEM-1024 encapsulation | 17,984 |
| ML-KEM-1024 key generation | 14,440 |

ML-DSA-87 belongs on a gateway. The non-PQC set runs in the 4 KB to 8 KB
task stacks the samples use.

### zxe_ledger: why a second ledger

libzxv's `zxv_ledger_*` sits on the kernel triple ledger, whose book is a
fixed 512 accounts × 256 entries, about 24 MB (P3). `zxe_ledger` uses only
the memory you give it (`zxe_account[]` and a power-of-two reference table)
and implements the same rules:

- **Currencies:** ISO currency enablement, with at most 4 minor units.
- **Accounts:** holder accounts cannot go negative.
- **Posting checks, in libzxv's order:** a positive amount up to 2^53 - 1;
  the same currency on both sides (no implicit FX); rail totals within the
  exact range; and no duplicate reference (stored as a 16-byte SHA3 key).
- **Status codes:** the same as libzxv's.
- **Capability gates:** `ZXV_CAP_LEDGER_POST` and `ZXV_CAP_LEDGER_READ`.

It also adds two firmware features:

- A **write-ahead journal callback**. If your flash write fails, the posting
  is refused with `ZXV_E_IO` and nothing changes.
- A **SHA3-256 hash chain head**, for evidence.

`zxe_ledger_check()` re-sums every rail. If an invariant fails, it poisons
the ledger and halts the `zxv_ctx` (fail closed).

A **differential test** runs random operation streams against both ledgers
and compares every status code and balance. Result: 75,244 operations,
0 disagreements.

## Sizes

These are bytes after `--gc-sections`, `-Os`, newlib-nano on Arm and
picolibc on RV32, from `make size` (`build/size-report.md` has the full
breakdown by origin). Flash = text + rodata + data; static RAM = data + bss.
Heap is separate (see RAM).

| target | `pure` | `core` (the MCU set) | `ledger` (+ libzxv ledger code) |
|---|---|---|---|
| Cortex-M0+ | 14,640 / 884 | 34,964 / 2,308 | 49,199 / 2,308 |
| Cortex-M4 | 13,824 / 884 | 34,668 / 2,308 | 43,281 / 2,308 |
| Cortex-M33 | 13,576 / 884 | 34,150 / 2,308 | 42,763 / 2,308 |
| RV32IMAC | 19,374 / 300 | 42,100 / 1,745 | 55,895 / 1,745 |

Each cell is flash / static RAM, in bytes. The probes are:

- **`pure`:** rationals, four-valued logic, hexagram tags and SHA3-256, with
  no context and no heap.
- **`core`:** `pure` plus context, capabilities, rmag, phase clock,
  quarantine log, `zxe_ledger` and `zxe_clock`.
- **`ledger`:** `core` plus the code of libzxv's own ledger. That ledger
  cannot run on an MCU (RAM); the probe shows what its code costs.

Notes on the numbers:

- **Cortex-M4 with PQC** (`make cross TARGET=cortex-m4 ZXE_PQC=1`): the
  `pqc` probe is 114,153 / 20,835, of which the kernel modules are 84,028 B
  of flash.
- **Error text:** in `core`, about 4.9 KB is string literals merged across
  objects, mostly libzxv's error texts, and 5 to 7.7 KB is C library
  (mostly `vsnprintf` and `malloc`). P8 proposes an option to drop the
  error text.
- **Probes are link-time only:** they are not run, and their entropy hook
  always fails.

## Using it in your firmware

1. **Choose a port:** `posix`, `baremetal`, `freertos` or `zephyr`. On
   Zephyr, add the module and set `CONFIG_ZXV=y`; see
   [zephyr-zxv/README.md](zephyr-zxv/README.md). Otherwise build the three
   libraries with your compiler:

   ```sh
   make -f lib.mk CC=arm-none-eabi-gcc AR=arm-none-eabi-ar OUT=build/fw \
        PORT=baremetal NO_TLS=1 TARGET_CFLAGS="-mcpu=cortex-m4 -mthumb -mfloat-abi=soft"
   # -> build/fw/libzxe.a build/fw/libzxv_embedded.a build/fw/libzxp.a (link in this order)
   ```

   For FreeRTOS, add `PORT=freertos` and your `FreeRTOSConfig.h` and kernel
   include paths to `TARGET_CFLAGS`; see
   [freertos-zxv/README.md](freertos-zxv/README.md).

2. **At boot,** before any ZXV call:
   - Optionally call `zxp_set_allocator()` and `zxp_set_heap_limit()`.
   - On bare metal, call `zxp_baremetal_set_clock()`.
   - Call `zxp_set_entropy()` with your TRNG, then `zxp_init()`.
   - Call `zxp_set_fatal_handler()` to put the device in its safe state.
3. **Keep ZXV in one task** (in a `ZXP_NO_TLS` build), out of ISRs, and away
   from hard deadlines.
4. **Include headers:** `zxp.h`, `zxe.h` and libzxv's `zxv_api.h`. Use
   `zxe_ledger` in place of `zxv_ledger_*`.

## Reproduce

These are the commands run for this kit, from `sdk/embedded`, on Ubuntu 24.04
x86-64. The tools came from apt: gcc 13.3, clang 18.1, gcc-arm-none-eabi 13.2.1,
libnewlib-arm-none-eabi, gcc-riscv64-unknown-elf, picolibc-riscv64-unknown-elf
and qemu-system-arm 8.2.

| command | result |
|---|---|
| `make test` | exit 0. Lines below. |
| `make test CC=clang OUT=build/clang` | exit 0, same counts |
| `make test SANITIZE=1 OUT=build/san` | exit 0. ASan + UBSan; test_port is 78 checks each, because the deliberate double-free check is skipped under ASan. |
| `make size && make check-nofloat` | exit 0. Table above; soft-float only in the two allowed objects. |
| `make size OUT=build/q32 ZXE_PROFILE=q32 && make check-nofloat OUT=build/q32 ZXE_PROFILE=q32` | no soft-float at all |
| `make qemu-test` | QEMU mps2-an385 (Cortex-M3) and mps2-an386 (Cortex-M4): `test_target: 43 checks, 0 failures` each |
| `make -C freertos-zxv test` | `freertos-zxv: 14 checks, 0 failures`; `--no-entropy: 2 checks, 0 failures`; `--violate` stopped with exit 70 |
| Zephyr v4.2.2, `native_sim/native/64` | `zxv sample: 30 checks, 0 failures` |
| Zephyr v4.2.2, `qemu_cortex_m3` (GNU Arm Embedded) | FLASH 29,828 B, RAM 49,848 B of 64 KB; on QEMU: phase clock refused (`ZXV_E_NOMEM`), `zxv sample: 24 checks, 0 failures` |

The lines `make test` prints:

```
check-nofloat: OK (27 files, no float types)
check_lists: 19 sources agree (sources.mk, zephyr-zxv/CMakeLists.txt)
test_port: 79 checks, 0 failures
test_port (ZXP_NO_TLS): 79 checks, 0 failures
test_subset: 311 checks, 0 failures
differential: 75244 operations (3079 postings accepted, 64773 refused), 0 disagreements
test_zxe_ledger: 11499 checks, 0 failures
68 checks, 0 failed                    (test_zxv_api)
test_zxv_abi11: 831 checks, 0 failures
test_zxv_abi12: 314 checks, 0 failures
test_zxv_abi13: 2245 checks, 0 failures
```

`make test` also runs libzxv's `fixtures` target in `sdk/libzxv` for the
ABI 1.1/1.2 tests. Its build output is git-ignored, as is everything under
`build/` here.

## Not done or not measured

- **No real hardware.** Everything ran on the host, the FreeRTOS POSIX
  simulator, Zephyr `native_sim`, or QEMU. Timing, stack depth on target,
  and TRNG behaviour have not been measured on a real board.
- **PQC was size-built only.** It was not run on a target. Its stack figures
  come from `-fstack-usage`.
- **Zephyr build path:** Zephyr was built with plain `cmake` + `ninja`,
  using the GNU Arm Embedded and host toolchains. The Zephyr SDK toolchain,
  `west` and `twister` were not used locally. The CI job
  (`.github/workflows/sdk-embedded.yml`) installs them with
  `west sdk install` and runs twister; that job has not run yet.
- **Zephyr with TLS:** `CONFIG_THREAD_LOCAL_STORAGE=y` (needs the Zephyr
  SDK) was not built locally.
- **No certification** of any kind, and no WCET analysis (see Safety).
