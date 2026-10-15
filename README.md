<!--
Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
SPDX-License-Identifier: Apache-2.0
-->

# ZEDEC XERO VULPINE (ZXV)

[![CI](https://github.com/sykomiked/ZEDEC-XERO-VULPINE-V1.0.0/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/sykomiked/ZEDEC-XERO-VULPINE-V1.0.0/actions/workflows/ci.yml)
[![CodeQL](https://github.com/sykomiked/ZEDEC-XERO-VULPINE-V1.0.0/actions/workflows/codeql.yml/badge.svg?branch=main)](https://github.com/sykomiked/ZEDEC-XERO-VULPINE-V1.0.0/actions/workflows/codeql.yml)
[![License: Apache-2.0](https://img.shields.io/badge/license-Apache--2.0-informational)](LICENSE)

**A sovereign, post-quantum operating system with a local multi-agent AI swarm at its core.**

ZXV is one freestanding C code base, written from scratch, that is meant to be a
complete personal operating system: its own kernel, file system, network stack,
security, user interface, apps, languages and economy, with a swarm of small AI
models running locally on an integer-only engine. The swarm is the system's
local AI agency, Tiamat Productive Agency AI. Nothing depends on a cloud
service to work.

The same source runs three ways:

| Way to run it | What it is | Where it stands |
|---|---|---|
| **Desktop app** | ZXV runs as an app beside your existing OS: a universal Mac `.app` (Apple Silicon and Intel), then Linux and Windows | Builds and runs; a real model answers in it |
| **Golden disc (bare metal)** | ZXV boots as the whole machine from a UEFI disc, with a separated user space | Boots in QEMU on x86_64 and arm64; still far from a full consumer OS, and never booted on a physical board |
| **libzxv** | The engine as a library with a frozen C ABI and SDKs for 12 languages | On `main`, with an add-only ABI check in CI |

The goal is one universal platform: the desktop app and the golden disc are the
same system, and an HPC version follows the local one.

## How the system fits together

| Layer | What it holds |
|---|---|
| **Apps** | 407 apps in three tiers: 144 base modules (L1), 127 apps built from them (L2), 136 built from those (L3). Any app can be a block inside another, and users can build apps without writing C. |
| **User interface** | `zui`, a native UI toolkit that never looks like a browser: vector graphics everywhere (`zui_vec`, ZXVG format), the ASCW Golden font family for every script, an astrolabe dock, five spaces (Home, Office, Team, Cloud, Park), the Banker file manager over the Hoard, and a boot screen that each app reuses as its launch screen with real progress bars. |
| **Languages** | Five language pillars (COBOL with Sutra, Fortran with Trebago, Ada with Eve, APL/K with KRON, MUMPS with MYRA), a shared IR for legacy languages with an AI twin for each, and spoken languages treated on par with programming languages. |
| **AI swarm** | A swarm of small models on the integer tensor engine, scheduled by a tokens-per-cycle budget with Fibonacci scaling and an emotional economy, with GPU-first compute on Metal. |
| **Logic and audit** | Four-valued paraconsistent logic (`lpres`) and a 64-state tag on decisions, a paradox engine, a paraconsistent ledger, peer audit of every posting, and a policy engine. |
| **Economy** | An exact, interest-free ledger (assets = liabilities + equity every cycle), the 0.08889% assurance fee, escrow markets, V-Bills, the Crown Raffle and Player Cards. |
| **Network** | Vinea peer-to-peer nodes over post-quantum sessions, Web 4 twins of classic network protocols, the AIRC chat layer, IPFS content addressing. |
| **Security** | ML-KEM, ML-DSA, SLH-DSA and HQC from vendored reference code, checked against NIST vectors; capability-checked syscalls; signed updates. |
| **Kernel** | Phase-tick scheduling, zxvfs with journal and extents, floating point banned at compile time, builds for arm64, x86_64, riscv64, riscv32 and arm32. |
| **Hardware** | An open hardware emulation standard (ZHP-1 profiles, signed reconfiguration with rollback) and class drivers. |

## What works today

"On `main`" means merged and tested in CI. "Draft" means built and tested on an
open pull request, not merged yet. A capability is listed only where code and a
test back it; [docs/supplemental/CLAIMS_AND_EVIDENCE.md](docs/supplemental/CLAIMS_AND_EVIDENCE.md)
has the evidence for each claim.

**On `main`**
- Integer-only tensor engine with the same output bits on every CPU, a GGUF
  loader, native Q4_K/Q6_K kernels (AVX2, AVX-512, NEON) and a Metal GPU
  backend. Qwen2.5-1.5B-Instruct runs on it with perplexity 9.371 against
  llama.cpp's 9.362; it is still slower than llama.cpp ([bench/](bench/README.md)).
- Grammar-constrained decoding and a paraconsistent evidence head; image and
  voice generation; the first tensor-parallel HPC slice.
- The swarm budget, emotional economy and cooperative market over nine forms
  of capital ([docs/SWARM_ECONOMY.md](docs/SWARM_ECONOMY.md)); the ledger,
  settlement spine, fee split and peer audit ([docs/PEER_AUDIT.md](docs/PEER_AUDIT.md)).
- The paradox engine, paraconsistent ledger and agents, and the policy engine.
- Kernel images for five CPU families that boot under emulation in CI, and
  UEFI golden discs for x86_64 and arm64 that boot in QEMU
  ([docs/BAREMETAL.md](docs/BAREMETAL.md)).
- The Mac app with model install, a local API guard, signed updates and
  notifications ([docs/MAC_APP.md](docs/MAC_APP.md)); the `zxvd` daemon with
  JSON-RPC over a local socket ([docs/ZXVD.md](docs/ZXVD.md)); the Linux and
  Windows desktop build.
- Post-quantum cryptography and one PQ session layer for peer links
  ([docs/PQ_MATRIX.md](docs/PQ_MATRIX.md)); Vinea nodes and a two-node demo
  that trades compute and settles the fee ([docs/VINEA.md](docs/VINEA.md)).
- libzxv with SDKs for C, C++, Java, .NET, Python, TypeScript, Go, Rust, Ruby,
  PHP, Swift and Solidity, plus the Z-Wasm plugin sandbox ([sdk/](sdk/)).
- The `zui` toolkit's system pieces and the ASCW Golden fonts
  ([docs/FONTS.md](docs/FONTS.md)).

**In draft pull requests**
- Bare-metal consumer OS: a ranked map of the gaps, a page-frame allocator and
  process fixes, then ACPI. Today the x86_64 disc runs one user-space self-test;
  arm64 runs user processes but cannot yet see the disc over PCI.
- The whole model forward pass on Metal, bit-exact on an M3 Max. Generation is
  still slower than on the CPU.
- The first wave of the 407 apps, the visual circuit app builder, vector
  graphics with the astrolabe dock, login with vault and depth effect, crash
  and safe mode, and app launch screens.
- Legacy languages with AI twins, network protocol twins, the AIRC gateway and
  core, and the end-user licence agreement.

**Not built yet**: a boot on physical hardware, a notarised Mac release, the
full app catalogue, the device mesh and any public release.

## Design rules

- **No hollow capabilities.** Nothing claims what the code does not do;
  boundaries fail closed; tests check computed values against an external
  anchor (an RFC or FIPS vector, a known answer, a conservation identity).
- **Integer-only and deterministic.** No floating point in the kernel; the
  same input gives the same bits on every machine.
- **GPU first, CPU checked.** Heavy compute goes to the GPU, never CPU only.
- **Paraconsistent by design.** Contradictions are tagged and routed, never
  allowed to crash a decision or grant one.
- **Vector first.** Icons, logos and boot art are native vectors.
- **People decide.** Life-safety, clinical, grid and financial actions are
  advisory and need a human's approval.

The reasoning is in [DESIGN_PHILOSOPHY.md](DESIGN_PHILOSOPHY.md).

## Build and run

The reference kernel target is arm64. From the repository root:

```bash
./build.sh arm64
qemu-system-aarch64 -M virt,gic-version=3 -cpu cortex-a53 -m 256M \
  -kernel kernel_arm64.elf -nographic -serial mon:stdio
```

Host integrity gate (from `kernel/`, needs gcc 11):

```bash
make verify-all
```

Desktop app for every platform from one machine (needs zig 0.13 and python3,
or Docker):

```bash
build_system/build_desktop.sh
# or
docker build -f build_system/Dockerfile.desktop -t zxv-desktop .
docker run --rm -v "$PWD/dist:/src/dist" zxv-desktop
```

See [CONTRIBUTING.md](CONTRIBUTING.md) and [BUILD_DEPS.md](BUILD_DEPS.md) for
toolchains, and [docs/CI.md](docs/CI.md) for what CI checks.

## Find your way around

| I want to… | Read |
|---|---|
| See the architecture | [ARCHITECTURE.md](ARCHITECTURE.md), [docs/ARCHITECTURE_OVERVIEW.md](docs/ARCHITECTURE_OVERVIEW.md) |
| Navigate the source | [SOURCE_CODE_NAVIGATION_GUIDE.md](SOURCE_CODE_NAVIGATION_GUIDE.md), [SUBSYSTEM_INDEX.md](SUBSYSTEM_INDEX.md) |
| Look up a term or see why a choice was made | [docs/supplemental/](docs/supplemental/README.md) |
| Read the academic papers | [docs/academic/](docs/academic/README.md) |
| See the roadmap | [ROADMAP.md](ROADMAP.md) |
| Build on ZXV | [sdk/](sdk/), [docs/APP_DEVELOPMENT.md](docs/APP_DEVELOPMENT.md) |
| Report a vulnerability | [SECURITY.md](SECURITY.md) |
| Cite ZXV | [CITATION.cff](CITATION.cff) |

## Licence and ownership

ZXV is created by and copyright **36N9 GENETICS LLC** (Michael Laurence Curzi),
PO Box 6, Calpine, CA 96124-0006, United States, deal@zedec.ai.

The software is licensed under the [Apache License, Version 2.0](LICENSE), and
every source file carries `SPDX-License-Identifier: Apache-2.0`. Anyone who
redistributes it must keep the attribution in [NOTICE](NOTICE). Vendored
third-party code keeps its own licence. The software is provided as is, with
no warranty of any kind. The ready-to-run ZXV system is licensed to end users,
and supported, by **SCENIC BYPASS CONSULTING, LLC** (Arizona), under the ZXV
End-User Agreement. Support tickets go to SCENIC BYPASS CONSULTING, LLC, not
to 36N9 GENETICS LLC: see [SUPPORT.md](SUPPORT.md).

**Partnerships.** 36N9 GENETICS LLC is the root organisation: it originated the
concept and the designs, and manufacturers partner with it under its master
partnership licence. Prospective partners approach it only through one of its
two pre-approved partners, both based in Arizona, **ILLUMAHEART FOUNDATION** or
**SCENIC BYPASS CONSULTING, LLC**, never 36N9 GENETICS LLC directly.

Commercial use is free under Apache-2.0. We ask, but never require, for-profit
users to give 18% of gross revenue from ZXV to ILLUMAHEART FOUNDATION; see
[COMMERCIAL_USE.md](COMMERCIAL_USE.md). Hardware material under
[hardware/](hardware/) is under the [ZXV Hardware License](hardware/LICENSE),
not Apache-2.0; see [LICENSE-HARDWARE.md](LICENSE-HARDWARE.md). Contributors
sign the [CLA](CLA.md). Model weights are never stored in this repository.
