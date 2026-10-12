<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# ZXV SDKs

Every ZXV SDK wraps one C library, [`libzxv`](libzxv/), so the same exact
arithmetic, four-valued logic, capability checks, post-quantum crypto and
integer inference engine are available in every language, and every SDK is
tested against the same answer key ([`conformance/vectors.json`](conformance/vectors.json)).

**New here?** Pick your language below, open its README, and run the
quickstart. Then read [Concepts](#concepts) once; the ideas are the same in
every language.

## Language SDKs

| Language | Folder | Package | How it binds | Tests |
|---|---|---|---|---|
| C / C ABI | [`libzxv`](libzxv/) | `libzxv.so` / `.dylib` | the library itself | 3,478 checks, ASan + UBSan |
| Java, Kotlin, Scala | [`java`](java/) | `org.zedec.zxv` | Java 22 FFM (no JNI) | 48 |
| .NET (C#, F#, Unity) | [`dotnet`](dotnet/) | `Zxv.Sdk` | `LibraryImport` | 141 |
| Python | [`python`](python/) | `zxv-sdk` | cffi, GIL released | 111, `mypy --strict` |
| TypeScript / JavaScript | [`typescript`](typescript/) | `@zedec/zxv` | Node-API addon and WebAssembly | 102 (both backends) |
| Solidity / EVM | [`solidity`](solidity/) | `ZxvStateChannel.sol` | verifies libzxv's state encoding | 10 |
| Ruby | [`ruby`](ruby/) | gem `zxv` | ffi | 50 runs |
| PHP | [`php`](php/) | `zedec/zxv` | PHP FFI | 41 |
| Rust | [`rust`](rust/) | crates `zxv`, `zxv-sys` | bindgen, `Result` types | 52 |
| Go | [`go`](go/) | `.../sdk/go/zxv` | cgo, `error` returns | 46, `-race` |
| C++ | [`cpp`](cpp/) | header-only `zxv.hpp` (C++20) | direct C ABI, RAII | 10,519 checks, ASan + UBSan + TSan |
| Swift | [`swift`](swift/) | SwiftPM `Zxv` | C module map | 30 |

## Kits

Kits are building blocks on top of the same C ABI; see
[`kits/`](kits/).

| Kit | What it is |
|---|---|
| [Z-Wasm](kits/z-wasm/) | A sandbox for WebAssembly plugins: a no-float validator, fuel charged to an `rmag` budget, and a proposal queue the host must approve. |
| [z-contract](kits/z-contract/) | Typed contracts with exact-rational bounds for plugin exports; a violation stops the plugin like a trap and is recorded in a SHA3-256 hash-chained evidence log, and optionally in the ABI 1.3 quarantine log. |
| [z-ffi](kits/z-ffi/) | Typed, capability-gated wrappers for calling C libraries, generated from a spec; float results only by exact conversion. Not a sandbox. |
| [adapter-matrix](kits/adapter-matrix/) | JSON, MQTT and FIX adapters that parse numbers exactly and label every inbound record (accepted-unverified or refused, with reasons). |
| [z-audit](kits/z-audit/) | A static checker for plugins, run in CI before publishing: validator, float/SIMD, import allow-list, contract declarations and bounds, and bounded checks on simple export bodies. Bounded checks, not a proof. |
| [Tri-Isolate](kits/tri-isolate/) | Runs each plugin in its own worker process; the host keeps the fuel budget and the proposal queue, and kills the worker on fuel exhaustion or timeout. A process boundary, not a VM (POSIX; optional Linux seccomp). |

## Frameworks

Framework kits wire a language SDK into an enterprise framework's own
idioms; see [`frameworks/`](frameworks/).

| Framework | Folder | Package | Over | Tests |
|---|---|---|---|---|
| Spring Boot 3 | [`spring-zxv`](frameworks/spring-zxv/) | `org.zedec:zxv-spring-boot-starter` | [`java`](java/) | 36 |
| ASP.NET Core 8 | [`aspnetcore-zxv`](frameworks/aspnetcore-zxv/) | `Zxv.AspNetCore` | [`dotnet`](dotnet/) | 56 |
| NestJS 12 | [`nest-zxv`](frameworks/nest-zxv/) | `@zedec/zxv-nestjs` | [`typescript`](typescript/) | 35 |
| Django 5.2+ | [`django-zxv`](frameworks/django-zxv/) | `django-zxv` | [`python`](python/) | 56 |

Each gives a managed context closed in ABI order, narrow capabilities
declared in configuration (the root is never injectable), a fail-closed
health check, exact `Rational` JSON that refuses floats, and a halted
context that is never answered as a normal response. CI:
[`.github/workflows/sdk-frameworks.yml`](../.github/workflows/sdk-frameworks.yml).

## Mobile

Mobile SDKs follow the same design as the language SDKs; see
[`mobile/`](mobile/).

| Platform | Folder | Package | How it binds | Tests |
|---|---|---|---|---|
| Android and JVM (Kotlin) | [`kotlin`](mobile/kotlin/) | `org.zedec.zxv.mobile` | JNI shim (Android has no FFM) | 34 on the host JVM; AAR builds for arm64-v8a and x86_64, not yet run on a device |
| Flutter and Dart | [`dart`](mobile/dart/) | `zxv` | `dart:ffi` | 33 on the host; no Flutter app or iOS run yet |

## Embedded and RTOS

Embedded kits run the exact core on microcontrollers through a small port
layer (allocator, mutex, entropy, time); see [`embedded/`](embedded/).

| Kit | Folder | What it gives you | Tests |
|---|---|---|---|
| Port + MCU subset | [`embedded`](embedded/) | Rationals, four-valued logic, hexagram tags, caps, quarantine log and budgets in about 35 KB flash on Cortex-M; a fixed-memory ledger (`zxe_ledger`); fails closed without an entropy source | 11,499 ledger checks, a 75,244-operation differential test against libzxv, libzxv's own suites through the port, QEMU Cortex-M3/M4 |
| FreeRTOS | [`freertos-zxv`](embedded/freertos-zxv/) | Port and a sample task (ledger posting, budget tick) | POSIX simulator |
| Zephyr | [`zephyr-zxv`](embedded/zephyr-zxv/) | Zephyr module (module.yml, Kconfig) and a sample | native_sim and qemu_cortex_m3 |
| ZXV-BareMetal | [`baremetal`](embedded/baremetal/) | Boot packaging of the ZXV kernel for board makers: a versioned board manifest (schema + stdlib validator) with the ZHP-1 profile digest, and a driver over the PR #25 UEFI disc build (x86_64, arm64; RISC-V QEMU only). No Secure Boot signing, no vendor keys | 122 manifest cases, dry-run plans, packaging against a fake build; QEMU boot in `ci.yml` |

Advisory only for any actuation; no safety-standard or real-time claims.
Nothing has run on real hardware yet.

## Silicon

Generated hardware description for people who put ZXV's rules into logic;
see [`silicon/`](silicon/).

| Kit | Folder | What it gives you | Tests |
|---|---|---|---|
| Z-Fabric | [`z-fabric`](silicon/z-fabric/) | Four-valued NOT/AND/OR, the hexagram action table, phase cycle and tag combinators as gate-level Verilog-2005 and VHDL-2008, generated from libzxv's own answers; CI fails if the HDL drifts from C | 8,368 exhaustive vectors under Icarus Verilog and GHDL, a Python netlist simulator, post-synthesis simulation (Yosys, GHDL) |

Combinational reference logic only: not timing-closed, no silicon.

## Game engines

Engine kits run deterministic lockstep games on exact rationals, with no
floats in game state; see [`engines/`](engines/).

| Kit | Folder | What it gives you | Tests |
|---|---|---|---|
| Emu-State | [`emu-state`](engines/emu-state/) | C11 lockstep core: per-tick inputs, prediction and rollback, a SHA3-256 state hash per tick, four-valued desync check | 1,138 checks + fuzzing |
| Godot 4 | [`godot-zxv`](engines/godot-zxv/) | GDExtension with `ZxvRational`, `ZxvHexagram`, `ZxvLockstep` and a two-peer demo | 1,010 core + 542 in headless Godot |
| Unity | [`unity-zxv`](engines/unity-zxv/) | Package `com.zedec.zxv`: serializable exact `Rational`, `Hexagram`, `ZxvLockstep` | 27 (runtime on .NET; not yet run inside Unity) |

## Legacy language pillars

Pillars bring an older language's own data formats onto the exact core;
see [`pillars/`](pillars/).

| Pillar | Folder | What it gives you | Tests |
|---|---|---|---|
| COBOL → Sutra | [`cobol-sutra`](pillars/cobol-sutra/) | Copybook parser and the Sutra shorthand (same layout from both), exact zoned/COMP/COMP-3 codecs, a capability-gated ledger posting bridge, a GnuCOBOL example | 154,569 checks (mostly codec round trips) + fuzzing, GnuCOBOL golden records byte-identical |
| Fortran → Trebago | [`fortran-trebago`](pillars/fortran-trebago/) | Fortran 2008+ module `zxv` (exact `rational`, four-valued `truth`, hexagram tags, capabilities, ledger) and Trebago, a short syntax for exact sum/dot/scale/prefix/matvec kernels over `INTEGER(INT64)` arrays that refuses overflow instead of rounding | 75,179 checks in C and Fortran, 26 golden cases, 14 compile-fail cases, fuzzed under ASan + UBSan |
| Ada → Eve | [`ada-eve`](pillars/ada-eve/) | Ada 2012 package `Zxv` (exact rationals, four-valued `Truth`, controlled capabilities, `Zxv.Fixed` proved free of run-time errors by gnatprove) and Eve, a compact form of Ada type declarations with the same canonical layout | 2,811 checks in C and Ada, 153 golden checks, GNAT agrees on 10 layouts, fuzzed under ASan + UBSan |
| APL / K → KRON | [`apl-kron`](pillars/apl-kron/) | KRON, an exact array language (rank ≤ 2, int64 rationals, four-valued truth) read from APL glyphs or K ASCII into one canonical program, with ngn/k and GNU APL bindings | 135,621 checks, 5,866 golden cases from Python fractions, bindings checked against ngn/k and GNU APL |
| MUMPS → MYRA | [`mumps-myra`](pillars/mumps-myra/) | M globals with typed subscripts and ^-piece values, M canonical numbers and collation, `$H` dates, a capability-gated ledger bridge and a GT.M binding; MYRA writes one line per node | 628,012 checks, golden files written by GT.M, 0 differences in a GT.M cross-check |

## What every SDK gives you

| Feature | Type names | Guide |
|---|---|---|
| Exact numbers, no floats | `Rational` | [SDK_DESIGN §3](docs/SDK_DESIGN.md#3-exactness-no-floats-at-the-boundary) |
| Four-valued logic | `Truth` (`NEITHER`, `TRUE`, `FALSE`, `BOTH`) | [§4](docs/SDK_DESIGN.md#4-four-valued-logic) |
| 64-state hexagram tag, quarantine log (ABI 1.3) | `Hexagram`, `QuarantineLog` | [§4](docs/SDK_DESIGN.md#the-hexagram-tag-abi-13), [ABI](docs/ABI.md#changes-in-13) |
| Errors and fail-closed | `ZxvException` family, `ZxvFatal…` | [§5](docs/SDK_DESIGN.md#5-errors) |
| Capabilities on every call | `Capability` | [§6](docs/SDK_DESIGN.md#6-capabilities-on-every-call) |
| Wiped secret keys | `SecretKey`, `MlKem1024`, `MlDsa87`, `Ed25519` | [§7](docs/SDK_DESIGN.md#7-secrets) |
| Budgets with async waits | `Budget` | [§8](docs/SDK_DESIGN.md#8-budgets-and-async) |
| Phase clock (27,720 ticks) | `PhaseClock` | [§9](docs/SDK_DESIGN.md#9-phase-ticks) |
| Integer inference engine | `Model`, `Session` | [§10](docs/SDK_DESIGN.md#10-engine) |
| UBH-168 wire frames | `Ubh168Frame` | [§11](docs/SDK_DESIGN.md#11-wire-framing-in-pure-host-code) |
| Swarm token economy | `Swarm` | [ABI](docs/ABI.md#groups-added-in-11) |
| EVM state channels | `StateChannel` | [solidity](solidity/) |

## Guides

Task-focused guides with tested examples live in
[`docs/guides/`](docs/guides/README.md). Start with
[Getting started](docs/guides/getting-started.md) (a first ledger in Java,
.NET, Python, TypeScript or C), then follow the "start here" path in the
catalogue. There are also guides on the ledger, budgets and time, crypto,
wire frames, plugins, web frameworks, writing a binding, conformance
testing and troubleshooting.

## Concepts

- [SDK design rules](docs/SDK_DESIGN.md): the names, error model and
  behaviour every SDK shares.
- [The C ABI](docs/ABI.md): the rules R1-R11 with examples, for binding
  authors.
- [ABI stability policy](docs/STABILITY.md): the never-break promise for
  binding maintainers, and the CI check that enforces it.
- [Security model and honest limits](docs/SECURITY.md): what is protected,
  and where protection stops.

## Building from source

```sh
make -C sdk/libzxv test fixtures   # the library, its tests, the tiny test model
```

Then follow the language README. CI builds and tests every SDK on Linux and
macOS ([`.github/workflows/sdk.yml`](../.github/workflows/sdk.yml)).

## Licence

Apache-2.0. Registered to 36N9 Genetics LLC, Michael Laurence Curzi.
Provided without warranty.
