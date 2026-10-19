# Contributing to ZEDEC XERO VULPINE (ZXV)

Thank you for your interest in contributing to ZXV. This is a from-scratch, freestanding operating system kernel built on the M5 axiomatic substrate with non-binary trit logic.

## Quick Start

### Prerequisites

You need a cross-compilation toolchain for your target architecture:

**macOS (Homebrew):**
```bash
brew install aarch64-linux-gnu gcc qemu xorriso
```

**Linux (Debian/Ubuntu):**
```bash
sudo apt-get install gcc-aarch64-linux-gnu qemu-system-arm xorriso
```

**Linux (Arch):**
```bash
sudo pacman install aarch64-linux-gnu-gcc qemu-system-arm xorriso
```

### Building

```bash
# ARM64 (reference target)
make -f build_system/Makefile.arm64 clean
make -f build_system/Makefile.arm64 all

# Run in QEMU
make -f build_system/Makefile.arm64 run
```

### Other Architectures

```bash
# x86_64
make -f build_system/Makefile.x86_64 all

# RISC-V 64-bit
make -f build_system/Makefile.riscv all

# RISC-V 32-bit
make -f build_system/Makefile.riscv32 all

# ARM32
make -f build_system/Makefile.arm32 all
```

## Development Workflow

1. **Fork the repository** and create a feature branch
2. **Make your changes** following the coding style below
3. **Build and test** on your target architecture
4. **Submit a pull request** with a clear description

## Coding Style

- **C Standard:** C11 (`-std=c11`)
- **Indentation:** Tabs for indentation, spaces for alignment
- **Line Length:** Prefer under 100 characters
- **Comments:** Explain *why*, not *what* — the code shows *what*
- **Headers:** Include guard with `#ifndef ZXV_<MODULE>_H`
- **Function Naming:** `zxv_<module>_<action>` for public API, `<action>_<subsystem>` for internal

## Architecture Overview

ZXV is **not** Linux, Unix, or DOS. It is a novel architecture:

### M5 Axiomatic Substrate

Five core axioms replace traditional kernel primitives:
- **OSEQ (K1):** Causal ordering / happens-before
- **RMAG (K2):** Exact Q32.32 arithmetic (no floating point)
- **LPRES (K3):** 4-valued logic (TRUE/FALSE/NEUTRAL/UNKNOWN)
- **IPHASE (K4):** Asymmetric routing
- **CHOICE (K5):** Deterministic collapse

### Non-Binary Trit Logic

Instead of Boolean logic, ZXV uses a 5-level trit lattice:
```
FALSE < GLUT- < NEUTRAL < GLUT+ < TRUE
```

Operators: GLB (meet) and LUB (join) are associative, commutative, idempotent.

### Modbind Dynamic Electrical Coupling

Module resolution evaluates electrical power factors:
```
P = V · I · pf(Δφ)
```

This prevents dead-code linking and interface mismatches at the hardware level.

### Phase-Tick Sequencing

No global clock. Execution is driven by phase-tick events from the phase coordinator.

## Testing

### Host Tests

```bash
cd kernel
make verify-all
```

This runs the fail-closed test harness including:
- M5 subsystem unit tests
- Crypto KATs (ML-KEM-768, SHA-256, AES-GCM)
- Event-space infrastructure tests
- Financial subsystem tests

### QEMU Boot Tests

```bash
# ARM64 boot to shell
make -f build_system/Makefile.arm64 run

# x86_64 boot to shell
make -f build_system/Makefile.x86_64 run
```

### Before you push

```bash
scripts/verify-local.sh            # what CI's fast tier checks, in minutes
scripts/verify-local.sh --all      # everything CI runs on Linux
scripts/verify-local.sh --docker   # same, inside CI's image (default on macOS)
```

`docs/CI.md` explains which CI jobs run when, and why draft pull requests
skip the slow tier.

## Submitting Changes

### Pull Request Checklist

- [ ] Code builds cleanly on at least one architecture
- [ ] New code has unit tests (where applicable)
- [ ] Documentation updated for new features
- [ ] Commit messages follow `conventional commits` format
- [ ] No trailing whitespace
- [ ] Copyright headers intact

### Commit Message Format

```
<type>(<scope>): <subject>

<body>

<footer>
```

Types: `feat`, `fix`, `docs`, `style`, `refactor`, `test`, `chore`

Example:
```
feat(timer): add ARM64 generic timer driver

Implements EL1 physical timer with 100Hz tick rate.
Uses GICv3 interrupt routing.

Fixes #42
```

## Areas Needing Help

See `GOOD_FIRST_ISSUE.md` for tasks suitable for new contributors.

## Getting Help

- **Documentation:** See `docs/ARCHITECTURE.md` for the architecture
- **Source Navigation:** See `docs/SYSTEM_REFERENCE.md`, one entry per module
- **Issues:** Open a GitHub issue for bugs or questions

## License

Software contributions are licensed under the Apache-2.0 License. Hardware contributions (anything under `hardware/`) are licensed under the ZXV Hardware License.

Before your first contribution is merged, please sign the [Contributor Licence Agreement](CLA.md): email a signed copy to deal@zedec.ai, or, for organisations, have an authorised officer sign its Corporate Signature. You keep the copyright in your work and are credited in [NOTICE](NOTICE).

Using ZXV commercially? See [COMMERCIAL_USE.md](COMMERCIAL_USE.md) for a voluntary request; it is not a licence condition.
