<!--
Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3
-->

# ZEDEC XERO VULPINE — ZEDEC pqOS

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
boundaries fail closed rather than fake a result; and every test asserts computed
values against an external anchor (an RFC/FIPS vector, a known-answer, a
conservation identity), never against the code's own output.

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

Each source file is licensed under the terms in its own header:

```
SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3
```

The software is provided as is, with no warranty of any kind.

Note: the [`LICENSE`](LICENSE) file still holds the Apache License 2.0 text from an earlier release. The source file headers take precedence until it is replaced with the texts of the licenses above.
