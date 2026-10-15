<!--
Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
SPDX-License-Identifier: Apache-2.0
-->

# ZEDEC XERO VULPINE — ZEDEC pqOS

[![CI](https://github.com/sykomiked/ZEDEC-XERO-VULPINE-V1.0.0/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/sykomiked/ZEDEC-XERO-VULPINE-V1.0.0/actions/workflows/ci.yml)
[![CodeQL](https://github.com/sykomiked/ZEDEC-XERO-VULPINE-V1.0.0/actions/workflows/codeql.yml/badge.svg?branch=main)](https://github.com/sykomiked/ZEDEC-XERO-VULPINE-V1.0.0/actions/workflows/codeql.yml)
[![sanitizers + fuzz](https://img.shields.io/badge/CI%20jobs-ASan%20%C2%B7%20UBSan%20%C2%B7%20integer%20%C2%B7%20MSan%20%C2%B7%20fuzz%20%C2%B7%20coverage-informational)](fuzz/README.md)

The CI workflow includes the sanitizer, fuzz-smoke and coverage jobs; see
[fuzz/README.md](fuzz/README.md), [fuzz/FUZZ_REPORT.md](fuzz/FUZZ_REPORT.md) and
[docs/COVERAGE.md](docs/COVERAGE.md).

**A freestanding, from-scratch, post-quantum operating system.**
Author: Michael Laurence Curzi · 36N9 Genetics, LLC

ZEDEC XERO VULPINE ("VOVINA SHAKINA," the M5 Axiomatic Kernel) is a freestanding
C kernel that replaces clock-driven execution with **phase-tick** event
sequencing, treats every system as an independent node, and is built to run equally well on a home computer, a
workstation, a server, and a supercomputer. It carries its own network and TLS
1.3 stack, a native content-addressed economy, a from-scratch font/graphics
layer, and its own Tri-Space native file formats (`.zxvc` / `.cedez` / `.cedec`).

Everything here follows one engineering rule without exception: **no hollow
capabilities.** Nothing claims a capability the code does not deliver; ops
boundaries fail closed rather than fake a result; and tests should assert computed
values against an external anchor (an RFC/FIPS vector, a known-answer, a
conservation identity) rather than the code's own output. That is the rule, not
yet the state of every test: older suites (for example `tests/test_drivers.c`)
still check structure sizes and constants, and `docs/ARCHITECTURE_OVERVIEW.md`
lists the modules whose claims are not yet backed.

## What's in this release

Each item below says where it stands. "On `main`" means it is merged and
tested in CI. "Landing" means it is built and tested on an open pull request
and is planned for this release, but is not on `main` yet. Nothing here is
listed as working unless the code does it.

### On `main`

**AI runtime**
- Integer-only tensor engine: no floating point, and the same output bits on
  every CPU ([docs/TENSOR_ENGINE.md](docs/TENSOR_ENGINE.md)).
- GGUF model loader, tokenizer, rotary position encoding, and native
  Q4_K / Q6_K kernels with AVX2, AVX-512 and NEON paths.
- A real model runs on it: Qwen2.5-1.5B-Instruct (Q4_K_M) gives the same
  answers as llama.cpp on the test prompts, with perplexity 9.371 against
  llama.cpp's 9.362. It is still slower than llama.cpp (see [bench/](bench/README.md)).
- E8 and Leech lattice quantisers.

**Swarm and its economies**
- Tokens-per-cycle budget with Fibonacci scaling across swarm levels, and a
  budget gate on every generated token.
- The emotional economy (emoji type and intensity steer token allocation)
  and a cooperative market over the nine forms of capital, with no monopoly
  ([docs/SWARM_ECONOMY.md](docs/SWARM_ECONOMY.md)).
- Ledger of record and settlement spine that check assets = liabilities +
  equity every cycle, the 0.08889% assurance fee with its 50/25/15/10 split,
  escrow market postings, and peer audit of every ledger posting
  ([docs/PEER_AUDIT.md](docs/PEER_AUDIT.md)).

**Operating system**
- Kernel builds for arm64, x86_64, riscv64, riscv32 and arm32 that boot under
  emulation in CI. It has not yet booted on a physical board.
- Floating point banned in kernel images at compile time; capability-checked
  syscalls; zxvfs with journal and extents; AI self-check at boot.
- Legacy console engines (6502, Z80, 68000 and others) used as a stress-test
  bench: fixed ROM programs exercise the kernel during development, and no
  commercial ROMs ship in the repository
  ([docs/supplemental/WHY_GAME_ROM_ENGINES.md](docs/supplemental/WHY_GAME_ROM_ENGINES.md)).

**Desktop app and daemon**
- ZXV Swarm desktop app: a universal Mac app (Apple Silicon and Intel), with
  a model install flow, a local API guard, a signed update checker and
  notifications ([docs/MAC_APP.md](docs/MAC_APP.md)). It is ad-hoc signed,
  not notarised.
- `zxvd`, a headless daemon with JSON-RPC over a local socket
  ([docs/ZXVD.md](docs/ZXVD.md)).

**Security and networking**
- Post-quantum cryptography from vendored upstream reference code: ML-KEM,
  ML-DSA, SLH-DSA and HQC, checked against NIST known-answer vectors on
  several CPUs ([docs/PQ_MATRIX.md](docs/PQ_MATRIX.md)).
- One PQ session layer (ML-KEM-1024 + ML-DSA-87) for peer links.
- Vinea peer-to-peer node (Kademlia-style), off by default; two app instances
  can trade compute end to end ([docs/VINEA.md](docs/VINEA.md)).

**Developer platform**
- libzxv, the engine as a library with a stable C ABI (version 1.3) and an
  add-only ABI check in CI, plus a conformance suite and the .NET SDK
  ([sdk/](sdk/), [docs/DOTNET_SDK.md](docs/DOTNET_SDK.md)).

**Quality**
- Host tests, sanitizers (ASan, UBSan, MSan, integer), fuzzing, a mutation
  gate, coverage, CodeQL, TLA+ models of the ledger, and Lean proofs of the
  rational and headroom code.

### Landing in this release

These are built and tested on open pull requests and will move to the list
above as each one merges.

- **SDKs for 11 more languages** (C++, Java, Python, TypeScript, Go, Rust,
  Ruby, PHP, Swift, Solidity, Kotlin), plugin kits, framework kits (Spring,
  ASP.NET Core, Nest, Django) and the Z-Wasm plugin sandbox, in seven parts
  (#5).
- **Hardware emulation standard**: a per-component emulation matrix, ZHP-1
  hardware profiles, and dual-signed hardware updates with rollback, as
  libzxv ABI 1.4 (#6).
- **Two-node demo**: one node asks another a question over a PQ session and
  pays it with the assurance fee (#31).
- **Linux and Windows desktop app** built on `zxvd` with a browser window (#32).
- **ZXV Studio**, a native SwiftUI Mac app on `zxvd`. It is built but has not
  been compiled on a Mac yet (#27).
- **Bare-metal discs**: UEFI images for x86_64 and arm64 that boot in QEMU
  (#25). Booting a physical machine is still to do.
- **COBOL treaties and Sutra**: COBOL copybooks as the treaty record format,
  with a compact Sutra syntax, checked for parity against GnuCOBOL (#24).
- **Faster prompt reading**: batched prefill, about 3x faster with the same
  output bits (#28).
- **AIPI**: content-addressed functions and data, with verified execution
  and escrowed bounties (#22).
- **Fibonacci-only VFV coins**, minting any amount as its Zeckendorf split (#29).
- **Audit fixes**: filesystem recovery, frame check v2 and the gcc 13 build (#30).
- **Paradox engine**: sorts each contradiction the four-valued logic finds
  into one of four kinds (constructive, destructive, neutral, unclassified)
  and handles each by containing it, deferring it or logging it. It is
  contradiction-tolerant: it routes contradictions, it does not make both
  sides true. It is in testing and not yet in a pull request.

## Orientation

| I want to… | Read |
|---|---|
| Read the design philosophy | [`DESIGN_PHILOSOPHY.md`](DESIGN_PHILOSOPHY.md) |
| Know why the unusual design choices were made, look up a term, or check the evidence behind a claim | [`docs/supplemental/`](docs/supplemental/README.md) |
| Read rigorous papers for mathematicians, logicians, physicists, economists, central bankers and computer scientists | [`docs/academic/`](docs/academic/README.md) |
| Understand the license | [`LICENSE`](LICENSE) |
| Build the kernel | [`CONTRIBUTING.md`](CONTRIBUTING.md) |
| Navigate the source | [`SOURCE_CODE_NAVIGATION_GUIDE.md`](SOURCE_CODE_NAVIGATION_GUIDE.md), [`SUBSYSTEM_INDEX.md`](SUBSYSTEM_INDEX.md) |
| Read the deep architecture | [`ARCHITECTURE.md`](ARCHITECTURE.md) |

## Build & verify

The reference target is **ARM64**. From the repository root:

```bash
./build.sh arm64
```

Or directly:

```bash
make -f build_system/Makefile.arm64 clean && make -f build_system/Makefile.arm64 all
qemu-system-aarch64 -M virt,gic-version=3 -cpu cortex-a53 -m 256M \
  -kernel kernel_arm64.elf -nographic -serial mon:stdio
```

Run the host integrity gate (from `kernel/`):

```bash
make verify-all
```

### ZXV Swarm desktop app

`build_system/build_desktop.sh` builds the swarm as a desktop app for every
platform from one machine: a macOS `.dmg` (one universal binary for Intel and
Apple Silicon), a Windows `.zip` and Linux `.tar.gz` files for x86_64 and
aarch64. It runs the swarm tests first, smoke-tests the binary that runs on
the build machine, and checks every package before writing `dist/`. It needs
zig 0.13 and python3; on a Mac it uses `hdiutil` for a native `.dmg`. Or use
Docker:

```bash
docker build -f build_system/Dockerfile.desktop -t zxv-desktop .
docker run --rm -v "$PWD/dist:/src/dist" zxv-desktop
```

Run `zxv-swarm --server` on a server and open its window from your own
computer through `ssh -N -L 8722:127.0.0.1:8722 you@server`.

## License

ZEDEC XERO VULPINE is registered to **36N9 Genetics, LLC** and **Michael Laurence Curzi**.

The code is licensed under the [Apache License, Version 2.0](LICENSE), and every source file says so in its header:

```
SPDX-License-Identifier: Apache-2.0
```

Credit: **Michael Laurence Curzi & 36N9 GENETICS LLC**, PO Box 6, Calpine, CA 96124-0006, United States, deal@zedec.ai.

The ready-to-run ZXV system is licensed to end users, and supported, by **SCENIC BYPASS CONSULTING, LLC** (Arizona). Support tickets go to SCENIC BYPASS CONSULTING, LLC, not to 36N9 GENETICS LLC: see [SUPPORT.md](SUPPORT.md).

Under section 4(d) of the Apache License, anyone who redistributes this software or a work derived from it must keep the attribution in [`NOTICE`](NOTICE). Vendored third-party code keeps its own licence (see each vendored directory's LICENSE and README.zxv). The software is provided as is, with no warranty of any kind.

**Hardware designs** under [`hardware/`](hardware/) are not covered by the Apache License. They are source-available under the [ZXV Hardware License 1.1](hardware/LICENSE): free for noncommercial use, with an 18% gross royalty to ILLUMAHEART FOUNDATION for commercial manufacture, and required attribution. See [`LICENSE-HARDWARE.md`](LICENSE-HARDWARE.md) and [`hardware/README.md`](hardware/README.md).

**Commercial use of the software** is free under Apache-2.0. We ask, but never require, for-profit users to contribute 18% of gross revenue from ZXV to ILLUMAHEART FOUNDATION; see [`COMMERCIAL_USE.md`](COMMERCIAL_USE.md). Contributors sign the [`CLA.md`](CLA.md).
