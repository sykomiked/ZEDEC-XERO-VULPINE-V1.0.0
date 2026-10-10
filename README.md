<!--
Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
SPDX-License-Identifier: Apache-2.0
-->

# ZEDEC XERO VULPINE — ZEDEC pqOS

[![CI](https://github.com/sykomiked/ZEDEC-XERO-VULPINE-V1.0.0/actions/workflows/ci.yml/badge.svg)](https://github.com/sykomiked/ZEDEC-XERO-VULPINE-V1.0.0/actions/workflows/ci.yml)
[![CodeQL](https://github.com/sykomiked/ZEDEC-XERO-VULPINE-V1.0.0/actions/workflows/codeql.yml/badge.svg)](https://github.com/sykomiked/ZEDEC-XERO-VULPINE-V1.0.0/actions/workflows/codeql.yml)
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

## Orientation

| I want to… | Read |
|---|---|
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

Redistributions must keep the attribution in [`NOTICE`](NOTICE). Vendored third-party code keeps its own licence (see each vendored directory's LICENSE and README.zxv). The software is provided as is, with no warranty of any kind.
