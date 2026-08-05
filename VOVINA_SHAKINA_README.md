# VOVINA SHAKINA — M5 Axiomatic Kernel
## Bootable ISO Image Documentation

**Author:** H.M. Michael-Laurence: Curzi (c)  
**Date:** July 23, 2026  
**Version:** 1.0 — Stable Convergence

---

## Overview

VOVINA SHAKINA is the first implementation of the M5 Axiomatic Kernel — a novel event-space-cycle-driven operating system kernel that replaces traditional clock-driven execution with an event-space temporal lattice. The kernel is built on the Ouroboros Principle (the build process is an instance of the system it creates) and the Reflexive Build Principle (iterative convergence via parallel ignition and feedback).

## Architecture

### Core Subsystems

| Subsystem | Directory | Description |
|-----------|-----------|-------------|
| **OSEQ** | `src/oseq/` | Ordinal Sequence Engine — device registration via ordinal numbering |
| **RMAG** | `src/rmag/` | Rational Magnitude Engine — exact-rational resource quotas |
| **LPRES** | `src/lpres/` | Local Presence — 8-cycle presence tracking with GLUT state |
| **IPHASE** | `src/iphase/` | Imaginary Phase Router — non-Euclidean phase vector routing |
| **CHOICE** | `src/choice/` | Choice-Collapse Scheduler — deterministic state vector collapse |
| **PHASE_COORD** | `src/phase_coord/` | Phase Coordinator — event-cycle-driven execution (DC/AC/PC profiles) |
| **TELEMETRY** | `src/telemetry/` | Telemetry & Self-Observation — Fibonacci-bounded recursion |
| **AXIOM_MATRIX** | `src/axiom_matrix/` | Axiom Matrix — 5-tuple indexed complex value store |
| **CRIT168** | `src/crit168/` | 168-bit Universal Word — CRIT transform with complex-exponential basis |
| **COMPAT** | `compat/` | Compatibility Layer — M5 to M8 lift and legacy projection |

### Key Types (m5_types.h)

- `ordinal_t` (uint64_t) — event-space cycle ordinal
- `rational_t` {int64_t num, den} — exact rational numbers
- `trit_t` {TRIT_FALSE, TRIT_TRUE, TRIT_GLUT} — three-valued logic
- `phase_t` {double r, i} — phase vector
- `collapse_t` {uint32_t bits[2]} — 64-bit collapse state
- `phase_tick_t` — complete event-cycle state vector
- `exec_profile_t` {EXEC_DC, EXEC_AC, EXEC_PC} — execution profiles

### Boot Flow

1. GRUB2 loads `vovina_shakina.bin` via multiboot1 protocol
2. `boot.s` sets up stack and calls `kernel_main()`
3. Kernel initializes all subsystems via VGA framebuffer output
4. Phase Coordinator runs 10 event cycles in EXEC_DC profile
5. Kernel halts awaiting external events (event-space, not clock)

## Build System

### Prerequisites

- GCC with 32-bit support (`gcc-multilib`)
- GRUB2 (`grub-pc-bin`, `grub-mkrescue`)
- `xorriso` and `mtools` for ISO creation
- Python 3 for the axiom verifier

### Building

```bash
cd kernel/
make clean
make iso       # Builds vovina_shakina.iso
make test      # Runs host-side unit tests
make verify    # Runs the axiom verifier
```

### Output Files

- `vovina_shakina.bin` — 32-bit ELF kernel binary (32KB)
- `vovina_shakina.iso` — Bootable ISO image (12MB, includes GRUB2)

## Booting

### QEMU

```bash
qemu-system-i386 -cdrom vovina_shakina.iso -boot d
```

### VirtualBox

Create a new 32-bit VM, mount the ISO as a CD-ROM, and boot.

### Physical Hardware

Write the ISO to a USB drive:

```bash
dd if=vovina_shakina.iso of=/dev/sdX bs=4M
```

Boot from the USB drive.

## Verification

The kernel passes all checks in the EDP Axiom Verifier:

- **No clock-driven busy loops** — Phase Coordinator uses event-cycle execution
- **Exact rational arithmetic** — RMAG uses int64_t num/den, no float collapse
- **Non-Euclidean metric** — IPHASE implements triangle-inequality checks
- **Complex-exponential CRIT** — 168-bit word transform uses cexp with imaginary axis
- **Fibonacci-bounded recursion** — Telemetry self-observation bounded by Fibonacci sequence
- **Axiom Matrix symmetry** — Real isometry branching, not stubbed
- **GLUT state handling** — LPRES properly tracks unresolved (GLUT) presence states

## File Manifest

```
kernel/
├── Makefile                    # Build system
├── linker.ld                   # Linker script (1MB load address)
├── grub.cfg                    # GRUB2 configuration
├── vovina_shakina.bin          # Kernel binary (32-bit ELF)
├── vovina_shakina.iso          # Bootable ISO image
├── include/
│   ├── m5_types.h              # Shared axiomatic types
│   ├── multiboot.h             # GRUB multiboot1 header
│   ├── framebuffer.h           # VGA text mode output
│   ├── freestanding.h          # Freestanding runtime (math, mem, alloc)
│   └── freestanding_stubs/     # Stub headers for -ffreestanding build
├── boot/
│   ├── boot.s                  # Multiboot entry point (32-bit assembly)
│   ├── kernel_main.c           # Kernel boot driver
│   └── framebuffer.c           # VGA text mode implementation
├── src/
│   ├── oseq/                   # Ordinal Sequence Engine
│   ├── rmag/                   # Rational Magnitude Engine
│   ├── lpres/                  # Local Presence
│   ├── iphase/                 # Imaginary Phase Router
│   ├── choice/                 # Choice-Collapse Scheduler
│   ├── phase_coord/            # Phase Coordinator
│   ├── telemetry/              # Telemetry & Self-Observation
│   ├── axiom_matrix/           # Axiom Matrix
│   └── crit168/                # 168-bit Universal Word
└── compat/
    ├── compat_layer.c          # M5→M8 compatibility
    └── test_compat_layer.c     # Compatibility tests
```

## Event-Space Cycle Principles

1. **No wall-clock timers** — Execution is driven by event-space cycles, not `sleep()` or `usleep()`
2. **Ouroboros Principle** — The build process that created this kernel is itself an instance of the event-space cycle system
3. **Reflexive Build** — The kernel was built through iterative convergence (8 reactor cycles), with each cycle igniting only tasks with feedback
4. **Anti-Collapse Verification** — The axiom verifier enforces that no subsystem collapses back into clock-driven execution

## License

Author: H.M. Michael-Laurence: Curzi (c)  
All rights reserved.
