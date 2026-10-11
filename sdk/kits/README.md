<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# ZXV kits

A **language SDK** (in [`sdk/`](../)) wraps `libzxv` for one programming
language. A **kit** is a ready-made building block for a kind of
application, written on top of the same C ABI
([`zxv_api.h`](../libzxv/include/zxv_api.h)) and held to the same rules:
opaque handles, `int32_t` status codes, exact integers and rationals, and
no floats.

| Kit | Folder | What it is | Status | Tests |
|---|---|---|---|---|
| Z-Wasm | [`z-wasm`](z-wasm/) | Plugin sandbox. A no-float load-time validator for WebAssembly plugins, fuel metering charged to an `rmag` budget, a wasm3 host with capability-gated host functions, call checks on exports, and the S0 proposal queue (Tri-Isolate runs it across a process boundary). | kit API 0.2 (preview) | 1140 validator checks, 149 host checks, mutation fuzzing under ASan + UBSan, a V8 cross-check |
| z-contract | [`z-contract`](z-contract/) | Contracts for plugin boundaries. An IDL with exact-rational bounds, a parser with precise error positions, a checker, a Z-Wasm binding that stops a violating plugin like a trap, a SHA3-256 hash-chained evidence log, and a sink that writes each violation as a Forbidden record into the ABI 1.3 quarantine log (`zxv_hexlog`). Tags use the ABI 1.3 64-state API. | kit API 0.2 (preview) | 726 checks, parser fuzzing under ASan + UBSan |
| z-ffi | [`z-ffi`](z-ffi/) | A typed boundary for C libraries. A spec generates wrappers that need a capability, check integer ranges, take float arguments only when exact and return float results only as exact rationals; signatures are proved at compile time. A wrapper generator, not a sandbox. | kit API 0.1 (preview) | 369 checks over libc and libm, compile-time signature checks, spec fuzzing under ASan + UBSan |
| adapter-matrix | [`adapter-matrix`](adapter-matrix/) | Inbound data adapters: JSON (HTTP bodies), MQTT payloads and FIX tag=value. Exact decimals, a label on every record (accepted-unverified or refused with reasons) carrying an ABI 1.3 64-state tag and action, z-contract validation with evidence, host attestation behind `ZXV_CAP_ATTEST`. Labels data; cannot make a source accurate. | kit API 0.2 (preview) | 704 checks, adapter fuzzing under ASan + UBSan |
| z-audit | [`z-audit`](z-audit/) | A static checker run in CI before a plugin is published: the Z-Wasm validator, every float/SIMD use, imports against the allow-list, a z-contract declaration with well-formed bounds for every export, and bounded interval checks on straight-line export bodies. JSON and text reports; non-zero exit on failure. Bounded checks, not a proof. | kit API 0.1 (preview) | 109 checks, CLI exit-code and report tests, module fuzzing under ASan + UBSan |
| Tri-Isolate | [`tri-isolate`](tri-isolate/) | Runs each Z-Wasm plugin in its own worker process (fork + exec, POSIX) over a length-prefixed socket protocol. The host validates, pays the fuel, keeps the proposal queue and decides with its own capability; it kills the worker on fuel exhaustion, a trap, a timeout or a protocol error. Optional Linux seccomp filter. A process boundary, not a VM; Windows not supported. | kit API 0.1 (preview) | 351 checks with hostile workers, frame fuzzing under ASan + UBSan |

## Rules every kit follows

- **Only the public ABI.** A kit calls `zxv_api.h` and nothing under
  `kernel/`. A kit never needs a change to `libzxv` to build.
- **No floats** in kit interfaces, and none in kit code that decides
  anything (rule R11 in [ABI.md](../docs/ABI.md)).
- **Honest limits.** Each kit README has a threat model or limits section
  that says what the kit does not guarantee.
- **Versioning.** A kit has its own version. Until a kit reaches 1.0, its
  API may change. The libzxv ABI under it does not
  ([STABILITY.md](../docs/STABILITY.md)).
- **Third-party code** is vendored unmodified, with its licence and
  checksums recorded in the kit's `THIRD_PARTY.md`, and only under licences
  compatible with Apache-2.0.
- **CI.** Each kit has jobs in
  [`.github/workflows/sdk.yml`](../../.github/workflows/sdk.yml) on Linux and
  macOS, plus a sanitizer run.
