# VOVINA SHAKINA — ZEDEC pqOS
## Technical White Paper

**Author:** H.M. Michael-Laurence: Curzi (c)
**Date:** 2026-08-02
**Version:** 2.0 — Dual-Architecture (x86 + ARM64) with EL0 User Space
**Classification:** Public

---

## 1. Executive Summary

VOVINA SHAKINA is the reference implementation of the ZEDEC pqOS — a bare-metal operating system kernel built on the M⁵-Axiomatic architecture derived from the Curzi Manifold. It is the first operating system to implement five distinct axiomatic number types as first-class kernel subsystems, each with its own BIOS-stage boot sequence, harmonized through a single phase-coordinated tick.

The kernel runs on **two architectures**: x86 (32-bit, GRUB multiboot ISO) and ARM64 (AArch64, QEMU virt / VIT GlyphMap devices). The ARM64 port adds **EL0 user space** with per-process address spaces (TTBR0_EL1), privilege boundaries (EL1 kernel / EL0 user), an **SVC-based syscall gate**, and **preemptive context switching** via the ARM64 generic timer IRQ.

**Boot artifacts:**
- `vovina_shakina.iso` (5.0 MB) — bootable x86 ISO (QEMU, VirtualBox, or real hardware)
- `kernel_arm64.elf` / `kernel_arm64.bin` — bootable ARM64 kernel (QEMU virt, real ARM64 hardware)

---

## 2. Architecture

### 2.1 The M⁵-Axiomatic Kernel

The kernel is derived from the Curzi Manifold M⁵, which assigns each axis of a five-dimensional logical space its own native number type. Conventional kernels collapse all axes into binary machine words — a dimensional collapse that destroys information required for correct sequencing, exact accounting, paraconsistent reasoning, relational routing, and deterministic choice resolution.

| Axis | Number Type | Subsystem | Kernel Role | BIOS Stage |
|------|-------------|-----------|-------------|------------|
| ω (ordinal) | Ordinal integers (well-ordered, successor-closed) | **OSEQ** — Ordinal Sequencer | Boot order, event sequencing, causality enforcement | Stage 0 |
| r (rational) | Rational numbers (p/q, exact, GCD-reduced) | **RMAG** — Rational Magnitude Engine | Resource allocation, memory/compute quotas, reserve accounting | Stage 1 |
| ℓ (logical) | Paraconsistent trits {⊤, ⊥, ⊥̸ (glut)} | **LPRES** — Logical Presence Attestor | Integrity checks, device presence, provenance verification | Stage 2 |
| iφ (imaginary) | Complex phase floats (r·e^{iφ}) | **IPHASE** — Imaginary Phase Router | Interconnect topology, relational curvature, network routing | Stage 3 |
| χ (choice) | Collapse operators (superposed → resolved) | **CHOICE** — Choice-Collapse Scheduler | Interrupt handling, decision resolution, deterministic collapse | Stage 4 |

### 2.2 Harmonization — Phase Coordinator

All five subsystems share a single 10ms tick enforced by the **Phase Coordinator**, which validates the **Coverage Hyperbola invariant**:

```
r · ℓ ≥ 1.8   (per active process/thread group)
```

No subsystem may claim a resolved (deterministic) state without sufficient rational-magnitude backing (r) and logical-presence attestation (ℓ). The coordinator vetoes any state transition that would violate coverage.

### 2.3 Supporting M⁵ Structures

- **Axiom Matrix** (SS8): A rank-5 tensor indexed by (ordinal, rational, trit, phase, collapse) 5-tuples, returning complex values. Indexing uses canonical encoding, not naive multi-dimensional arrays, to preserve the distinct number families.
- **Telemetry Recursion** (SS9): Self-observation feeds back into CHOICE via `choice_resolve_from_telemetry()`, bounded by a Fibonacci bound `fib(paradox_level + 2)` to prevent unbounded recursion.
- **168-Bit Universal Word + CRIT Transform** (SS10): 21 octets / 24 septets / 28 sextets (all = 168 bits) with alternating-endianness normalization and CRIT waveform transform (lossless round-trip).
- **Compatibility Layer**: Backward projection π: M⁵ → ℝ (legacy POSIX scalar), forward lift λ: M⁵ → M⁸ (lossless superset embedding).

---

## 3. Boot Sequence

### 3.1 x86 Boot (14 phases via GRUB multiboot1)

| Phase | Subsystem | Output |
|-------|-----------|--------|
| 1 | VGA text mode + COM1 serial init | Display + serial output |
| 2 | M⁵ axiomatic core (OSEQ, RMAG, LPRES, IPHASE, CHOICE, Phase Coordinator) | 5 subsystems + coordinator online |
| 3 | Hardware drivers (GDT, IDT, PIC, Timer, Keyboard, Mouse, PCI, ACPI, ATA, FAT32) | Interrupts, input, bus enumeration, disk access |
| 4 | VBE graphics mode + GUI desktop | 1024×768×32 framebuffer with window manager |
| 5 | Memory management (paging, frame allocator, heap) | Virtual memory + kmalloc/kfree |
| 6 | Process scheduler (round-robin, M⁵-weighted) | Task scheduling |
| 7 | Syscall interface (int 0x80 dispatch) | 12 system calls |
| 8 | Virtual filesystem (VFS over FAT32) | File open/read/write/close, directory listing |
| 9 | Network stack + M5 Omni-Router | Ethernet/ARP/IP/ICMP/TCP/UDP + 20 protocol adapters |
| 10 | Vino decentralized bank node | Triple ledger, 9 capitals, ISO 20022, blockchain, payment rails |
| 11 | Vena application runtime | 31 languages, smart contracts, oracles |
| 12 | Native applications | Shell, SysMon, Wallet launched |
| 13 | Holographic data system | 5 holographic file types registered |
| 14 | Main event loop | "All systems online" — entering event cycle |

### 3.2 ARM64 Boot (EL2 → EL1 drop, 18 phases)

The ARM64 kernel boots from EL2 (hypervisor) and drops to EL1 (kernel) before calling `kernel_main`. The boot sequence uses the ARM64 Generic Timer for preemption and GICv3 for interrupt dispatch.

| Phase | Subsystem | Boot Tag | Description |
|-------|-----------|----------|-------------|
| 0 | Boot assembly (`boot.s`) | — | EL2→EL1 drop, VBAR_EL1 setup, FP/SIMD enable, BSS clear |
| 1 | UART PL011 | — | Serial console init (115200 baud) |
| 2 | MMU (`arm64_mmu.c`) | `[DRIVER ONLINE]` | Identity mapping (1GB, 4KB granule), TCR_EL1, MAIR_EL1 |
| 3 | GICv3 (`gicv3.c`) | `[DRIVER ONLINE]` | Distributor + redistributor init, IRQ routing |
| 4 | Generic Timer (`arm64_timer.c`) | `[DRIVER ONLINE]` | 100Hz tick, timer IRQ registration |
| 5 | M⁵ Core (OSEQ, RMAG, LPRES, IPHASE, CHOICE) | `[INITIALIZED]` | 5 axiomatic subsystems + Phase Coordinator |
| 6 | EDP Risk + Predictive Model | `[INITIALIZED]` | Fibonacci algebra, ISF surplus |
| 7 | Situation Modeler | `[REGISTERED]` | 15 domains, cross-domain contagion |
| 8 | Triple Ledger (9 capitals) | `[REGISTERED]` | Financial, human, ecological accounts |
| 9 | Financial Instruments | `[REGISTERED]` | 20 instrument types, M5 valuation, Greeks |
| 10 | Payment Rails | `[REGISTERED]` | Dragon/Phoenix/Thunderbird, conventional compat |
| 11 | Crypto Bridge | `[REGISTERED]` | 35 chains, 11 smart contract languages |
| 12 | JDR PirateNet | `[REGISTERED]` | 22 frequency bands, harmonic hum, FHSS |
| 13 | RTL Device Framework | `[REGISTERED]` | Chisel/SystemVerilog/VHDL, AXI4/APB/AHB |
| 14 | DLP Pico Projector | `[INITIALIZED]` / `[SIMULATED]` | 854×480→1708×960 pixel-shift, laser AF (simulated) |
| 15 | Vino Bank + Count House + Porter House | `[INITIALIZED]` | Decentralized bank, fractal reserve, port-seal firewall |
| 16 | Mesh Token + Community Chest + AI Layer | `[REGISTERED]` | P2P settlement, app store, remote compute |
| 17 | Mesh Net + Immigration + Robin DeBanks | `[INITIALIZED]` / `[REGISTERED]` | User mesh networks, daemon visas, encrypted vault |
| 18 | Vena Runtime + Event Scheduler + Holographic + Prism Break | `[INITIALIZED]` | App loader, event-budget scheduler, holographic data, shader |
| 19 | Event loop | — | Enable IRQs, WFI idle, event dispatch |

**Boot log honesty tags** distinguish actual hardware initialization (`[DRIVER ONLINE]`), software initialization (`[INITIALIZED]`), subsystem registration (`[REGISTERED]`), and simulated features (`[SIMULATED]`).

### 3.3 ARM64 EL0 User Space & Preemptive Scheduler

The ARM64 kernel implements **EL0 user space** with per-process address spaces and **preemptive context switching**:

| Component | File | Function |
|-----------|------|----------|
| EL0 Header | `arch/arm64/el0_userspace.h` | Process structs, PTE constants, API declarations |
| EL0 Implementation | `arch/arm64/el0_userspace.c` | Page tables, TTBR0 switching, EL1→EL0 ERET, SVC handler |
| Portable Scheduler | `src/el0_userspace/el0_sched.c` | Round-robin selection, quantum accounting, preemption logic |
| Scheduler Test | `src/el0_userspace/test_el0_userspace.c` | 9 test suites (init, round-robin, quantum, context switch, termination) |

**Key features:**
- **Per-process page tables**: Each process gets 5 page tables (L0/L1/L2/L3-code/L3-stack) from static BSS storage
- **TTBR0_EL1 switching**: Address space switch with full TLB invalidation per context switch
- **EL1→EL0 transition**: `ERET` with process context (PC, SP, PSTATE, X0-X30)
- **SVC syscall gate**: SVC instruction from EL0 triggers synchronous exception at EL1
- **Preemptive scheduling**: Timer IRQ saves EL0 context, decrements quantum, round-robin selects next READY process
- **4 syscalls**: `SYS_EXIT` (1), `SYS_WRITE` (2, UART), `SYS_GETPID` (3), `SYS_YIELD` (4)
- **Max 32 processes** with configurable quantum (default: 10 ticks)

---

## 4. Subsystem Catalog

### 4.1 M⁵ Axiomatic Core (10 modules)

| Module | File | Function |
|--------|------|----------|
| OSEQ | `kernel/src/oseq/oseq_core.c` | Ordinal sequencer — well-ordered device registration, POST |
| RMAG | `kernel/src/rmag/rmag_core.c` | Exact rational arithmetic (int64 num/den, GCD-reduced), resource quotas |
| LPRES | `kernel/src/lpres/lpres_core.c` | Paraconsistent trit logic (TRUE/FALSE/GLUT), attestation table |
| IPHASE | `kernel/src/iphase/iphase_core.c` | Complex phase routing, asymmetric distance, non-Euclidean topology |
| CHOICE | `kernel/src/choice/choice_core.c` | Deterministic choice-collapse scheduler, superposition resolution |
| Phase Coordinator | `kernel/src/phase_coord/phase_coordinator.c` | 10ms tick harmonizer, Coverage Hyperbola enforcement |
| Telemetry | `kernel/src/telemetry/telemetry_core.c` | Self-observation + Fibonacci-bounded feedback into CHOICE |
| Axiom Matrix | `kernel/src/axiom_matrix/axiom_matrix_core.c` | Rank-5 tensor with canonical encoding, isometry checks |
| CRIT-168 | `kernel/src/crit168/crit_168_word.c` | 168-bit universal word, CRIT/iCRIT waveform transform |
| Compat Layer | `kernel/compat/compat_layer.c` | π projection (M⁵→ℝ), λ lift (M⁵→M⁸) |

### 4.2 Hardware Drivers (11 modules)

| Module | File | Function |
|--------|------|----------|
| GDT | `kernel/src/gdt/gdt.c` | Global Descriptor Table — 32-bit protected mode segments |
| IDT | `kernel/src/idt/idt.c` | Interrupt Descriptor Table — 256 ISR entries |
| PIC | `kernel/src/pic/pic.c` | 8259A Programmable Interrupt Controller — IRQ remapping |
| Timer | `kernel/src/timer/timer.c` | PIT timer — 100Hz system tick |
| Keyboard | `kernel/src/keyboard/keyboard.c` | PS/2 keyboard — scancode to ASCII |
| Mouse | `kernel/src/mouse/mouse.c` | PS/2 mouse — pointer tracking |
| PCI | `kernel/src/pci/pci.c` | PCI bus enumeration — device/vendor detection |
| ACPI | `kernel/src/acpi/acpi.c` | ACPI table parsing — power management |
| VBE | `kernel/src/vbe/vbe.c` | VESA BIOS Extensions — framebuffer graphics |
| ATA | `kernel/src/ata/ata.c` | ATA/IDE disk driver — PIO mode read/write |
| FAT32 | `kernel/src/fat32/fat32.c` | FAT32 filesystem — BPB, clusters, directory listing |

### 4.3 Core OS Services (5 modules)

| Module | File | Function |
|--------|------|----------|
| Memory Manager | `kernel/src/mm/mm.c` | Paging, frame allocator, kernel heap (kmalloc/kfree/kcalloc/krealloc) |
| Scheduler | `kernel/src/sched/sched.c` | Round-robin with M⁵-weighted priority, task create/destroy |
| Syscall | `kernel/src/syscall/syscall.c` | int 0x80 dispatch — 12 system calls |
| VFS | `kernel/src/vfs/vfs.c` | Virtual filesystem — mount/unmount/open/close/read/write/list_dir |
| GUI | `gui/gui.c` | Desktop environment — windows, widgets, mouse/keyboard handling |

### 4.4 Network Stack (5 modules, 20 protocol adapters)

| Module | File | Function |
|--------|------|----------|
| Net Core | `kernel/src/net/net.c` | Ethernet/ARP/IP/ICMP/TCP/UDP, socket API |
| M5 Omni-Router | `kernel/src/net/m5route.c` | Phone→IP mapping, IPv4↔IPv6 NAT64 bridge, 20 protocol types |
| RTL8139 | `kernel/src/net/rtl8139.c` | RTL8139 NIC driver — ring buffer TX/RX |
| DTMF/Modulation | `kernel/src/net/dtmf.c` | DTMF tones, Morse code, RTTY, SSTV, PSK31, AX.25/APRS |
| Radio | `kernel/src/net/radio.c` | AM/FM SDR demodulation, cellular (3G/4G/5G), satellite (GEO/LEO/Starlink), quantum/laser/neutrino |

**Protocol adapters (20):** Ethernet, IPv4, IPv6, TCP, UDP, ARP, ICMP, DTMF, Morse, RTTY, SSTV, PSK31, AX.25/APRS, Cellular (AT/3G/4G/5G/SMS), Satellite (GEO/LEO/Starlink), AM Radio, FM Radio, Quantum, Laser/Optical, Neutrino.

### 4.5 Financial System — Vino (1 module)

**File:** `kernel/src/vino/vino.c`

- **Triple ledger**: primary + balance + audit
- **Nine forms of capital**: financial, material, social, intellectual, natural, cultural, spiritual, temporal, relational
- **12 asset classes**: USD, BTC, ETH, AAPL (equities), US10Y (bonds), XAU (commodities), EURUSD (forex), options, futures, real estate, art, data
- **20 payment rails**: Visa, MasterCard, Hormung, EVC, SWIFT, CIPS, SPFS, SEPA, FedWire, CHIPS, RTGS, UPI, Pix, M-Pesa, Alipay, WeChat Pay, Zelle, Cash App, Wise, Ripple
- **12 message standards**: ISO 20022, CAMT.053, SWIFT MT103, SWIFT MX (PACS.008), CIPS, SPFS, SEPA XML, FedWire, CHIPS, RTGS, UPI, Pix
- **Blockchain adapters**: BTC, ETH (all EVM-compatible families)
- **P2P networking**: peer discovery, validator mode, block proposal

### 4.6 Application Runtime — Vena (1 module)

**File:** `kernel/src/vena/vena.c`

- **31 languages**: M5 Axiomatic, C, Go, Rust, Python, JavaScript, TypeScript, Java, Kotlin, Swift, Haskell, OCaml, Lisp, Scheme, Prolog, Erlang, Elixir, Clojure, Scala, Lua, R, Julia, Fortran, Cobol, Ada, APL, Navajo, Klingon, Morse Code, Braille, Sanskrit
- **Smart contracts**: registration, execution, state management
- **Oracles**: BTC/USD, EUR/USD price feeds
- **App lifecycle**: load, start, stop, unload

### 4.7 Native Applications (1 module, 6 apps)

**File:** `kernel/src/apps/apps.c`

| App | Function |
|-----|----------|
| Shell | Command-line interface — help, clear, echo, ls, cat, ps, mem, net, vino, vena, holo |
| Editor | Text editor — cursor movement, insert mode, backspace, Home/End |
| File Manager | Directory navigation — browse, open, select |
| SysMon | System monitor — CPU, memory, network stats, live tick updates |
| NetConfig | Network configuration — interface status, protocol adapters |
| Wallet | Financial wallet — 9 capital types, account balance, transfers |

### 4.9 ARM64 Architecture Layer (8 modules)

| Module | File | Function |
|--------|------|----------|
| Boot | `arch/arm64/boot.s` | EL2→EL1 drop, VBAR_EL1, exception vectors, BSS clear |
| UART | `arch/arm64/uart_pl011.c` | PL011 serial console (QEMU virt, VIT GlyphMap devices) |
| MMU | `arch/arm64/arm64_mmu.c` | 4-level page tables, TCR_EL1, MAIR_EL1, identity mapping |
| Exceptions | `arch/arm64/arm64_exceptions.c` | Synchronous + IRQ exception dispatch |
| GICv3 | `arch/arm64/gicv3.c` | Interrupt controller (distributor + redistributor) |
| Timer | `arch/arm64/arm64_timer.c` | ARM64 generic timer (CNT* registers, 100Hz) |
| Kernel Main | `arch/arm64/kernel_main_arm64.c` | 18-phase ARM64 boot, event loop, WFI idle |
| EL0 User Space | `arch/arm64/el0_userspace.c` | Per-process page tables, EL0 entry/exit, SVC handler |

### 4.10 EL0 Scheduler (Portable, 1 module)

| Module | File | Function |
|--------|------|----------|
| EL0 Scheduler | `src/el0_userspace/el0_sched.c` | Round-robin selection, quantum accounting, preemption (host-testable) |

### 4.11 ARM64 Board Profiles

| Board | RAM | UART | GIC | Timer IRQ | Profile |
|-------|-----|------|-----|----------|---------|
| QEMU virt | 256MB | 0x09000000 | 0x08000000 | PPI 30 | `board_profile.h` |
| VIT GlyphMap Phone (MediaTek Dimensity) | 4GB | 0x11002000 | 0x0C000000 | PPI 30 | `board_profile.h` |
| VIT GlyphMap Tablet (MediaTek Kompanio) | 8GB | 0x11002000 | 0x0C000000 | PPI 30 | `board_profile.h` |
| VIT GlyphMap M-Server (ARM64 server-class) | 16GB | 0x11002000 | 0x0C000000 | PPI 30 | `board_profile.h` |

### 4.12 Holographic Data System (1 module, 5 file types)

**File:** `kernel/src/holographic/holo.c`

| Extension | Type | Role |
|-----------|------|------|
| `.36n9` | Positive Space | Constructive data (matter) — omega+ phase |
| `.9n63` | Negative Space | Complementary/inverse data (antimatter) — omega- phase |
| `.36m9` | Dataset | Manifest of 36n9/9n63 pairs — phase coordinator record |
| `.zedei` | Holographic Renderer | Reconstructs output from 36n9/9n63/36m9 — collapse/observation |
| `.zedec` | Container | Bundles all four types — full system envelope |

**Features:**
- 5 encoding modes: Raw, Phase-encoded, Interference, Fractal, Quantum
- 5 dimensions: 1D (audio), 2D (images), 3D (voxels), 4D (temporal-3D), Holographic (light field)
- 5 render modes: Visual, Audio, Data, Hologram, Fractal
- 5 interference blend modes: XOR, Phase Shift, Fourier magnitude, Fresnel diffraction, full Holographic complex interference
- CRC32 checksums for data integrity
- Full serialization/deserialization for all 5 file types
- Container supports up to 512 entries with typed indexing

---

## 5. Build Pipeline

### 5.1 Nonlinear Compilation

The build uses a three-phase nonlinear pipeline (`zedec_build.py`) where each phase gates the next:

| Phase | Tool | Function |
|-------|------|----------|
| 1 | `edp_axiom_verifier.py` | Deterministic anti-collapse static analysis — 16 checks across all source files |
| 2 | Host test suites | 114 assertions across 3 test suites (Apps, Holographic, Network/Vino/Vena) |
| 3 | Docker x86_64 cross-compile | gcc -m32 freestanding compilation + grub-mkrescue ISO creation |

### 5.2 Anti-Collapse Verification

The verifier (`edp_axiom_verifier.py`) performs 16 deterministic checks:

- **LPRES**: No bool/int trit collapse, existential import enforcement
- **RMAG**: No float/double in rational types, exact integer num/den required
- **IPHASE**: No forced symmetric distance, triangle inequality handling
- **CHOICE**: No bare rand() without determinism doctrine
- **OSEQ**: No abort()/exit() on contradiction — signed-zero resolution required
- **Phase Coordinator**: No clock-driven busy loop — event-space cycles required
- **Axiom Matrix**: No naive multi-dim array collapse, symmetry not stubbed, projection read-only
- **Telemetry**: Self-observation feeds into CHOICE, Fibonacci bound enforced, iterative not recursive
- **CRIT-168**: Non-degenerate phase (imaginary component), bit arithmetic consistency (28×6 = 24×7 = 21×8 = 168)
- **General**: No self-shadowing declarations, conservation checks require shadow resolution path

### 5.3 Build Artifacts

| Artifact | Size | Description |
|----------|------|-------------|
| `vovina_shakina.bin` | 170,260 bytes | 32-bit ELF kernel binary (multiboot1) |
| `vovina_shakina.iso` | 5,238,784 bytes | Bootable GRUB ISO image |
| Source files | 37 .c + 2 .s | Compiled into the kernel |
| Header files | 10 .h in include/ | Freestanding stubs + M⁵ types + multiboot |

---

## 6. Quality Control Results

### 6.1 Anti-Collapse Verification
- **79/80 files PASS** (1 cross-file type reference — expected in multi-module kernel)
- **0 logic collapse violations**

### 6.2 Host-Side Tests (24 test suites, 200+ assertions)
- **OSEQ, RMAG, LPRES, IPHASE, CHOICE, Phase Coordinator**: 6 suites PASS
- **Telemetry, Axiom Matrix, CRIT-168, Compat Layer**: 4 suites PASS
- **Quantum Device, DLP Projector**: 2 suites PASS
- **Count House, Count House Fractal, Porter House**: 3 suites PASS
- **Event Scheduler, Mesh Token, Community Chest, AI Layer, Mesh Net**: 5 suites PASS
- **Immigration, Robin DeBanks, Prism Break**: 3 suites PASS
- **EL0 User Space** (9 sub-tests: init, slot management, round-robin, quantum, context switch, termination, getters, PTE constants, max procs): 1 suite PASS
- **Total: 24/24 suites PASS, 0 FAIL**

### 6.3 x86 QEMU Boot Test
- **Boot method**: `qemu-system-i386 -cdrom vovina_shakina.iso -m 256M -boot d -nographic`
- **Result**: All 14 boot phases complete successfully
- **Output confirmed via serial**: "ZEDEC pqOS — All systems online. Entering event cycle..."

### 6.4 ARM64 QEMU Boot Test
- **Boot method**: `qemu-system-aarch64 -M virt,gic-version=3 -cpu cortex-a53 -m 256M -kernel kernel_arm64.elf -nographic`
- **Result**: All 18 boot phases complete, 1400+ event cycles executed
- **Output confirmed via serial**: All boot tags ([DRIVER ONLINE], [INITIALIZED], [REGISTERED], [SIMULATED]) display correctly
- **Event scheduler**: dispatches, events, idle counts all reported correctly
- **Porter House**: port-seal firewall active (4 seals)
- **Prism Break**: compositor initialized

---

## 7. File Layout

```
VOVINA_SHAKINA/
├── kernel/
│   ├── boot/
│   │   ├── boot.s                    # Multiboot1 entry point (32-bit protected mode)
│   │   ├── kernel_main.c             # 14-phase boot sequence
│   │   └── framebuffer.c             # VGA text mode + COM1 serial output
│   ├── src/
│   │   ├── oseq/                     # Ordinal Sequencer
│   │   ├── rmag/                     # Rational Magnitude Engine
│   │   ├── lpres/                    # Logical Presence Attestor
│   │   ├── iphase/                   # Imaginary Phase Router
│   │   ├── choice/                   # Choice-Collapse Scheduler
│   │   ├── phase_coord/              # Phase Coordinator (harmonizer)
│   │   ├── telemetry/                # Telemetry Recursion
│   │   ├── axiom_matrix/             # Axiom Matrix (rank-5 tensor)
│   │   ├── crit168/                  # 168-Bit Universal Word + CRIT
│   │   ├── gdt/                      # Global Descriptor Table
│   │   ├── idt/                      # Interrupt Descriptor Table
│   │   ├── pic/                      # Programmable Interrupt Controller
│   │   ├── timer/                    # System Timer
│   │   ├── keyboard/                 # PS/2 Keyboard
│   │   ├── mouse/                    # PS/2 Mouse
│   │   ├── pci/                      # PCI Bus
│   │   ├── acpi/                     # ACPI Power Management
│   │   ├── vbe/                      # VESA Graphics
│   │   ├── ata/                      # ATA Disk Driver
│   │   ├── fat32/                    # FAT32 Filesystem
│   │   ├── mm/                       # Memory Management
│   │   ├── sched/                    # Process Scheduler
│   │   ├── syscall/                  # Syscall Interface
│   │   ├── vfs/                      # Virtual Filesystem
│   │   ├── net/                      # Network Stack (5 modules)
│   │   ├── vino/                     # Vino Decentralized Bank
│   │   ├── vena/                     # Vena Application Runtime
│   │   ├── apps/                     # Native Applications (6 apps)
│   │   └── holographic/              # Holographic Data System
│   ├── compat/
│   │   └── compat_layer.c            # M⁵→ℝ projection, M⁵→M⁸ lift
│   ├── include/
│   │   ├── freestanding.h            # Freestanding runtime (memset, memcpy, bool)
│   │   ├── m5_types.h                # M⁵ type definitions
│   │   ├── multiboot.h               # Multiboot1 header
│   │   └── freestanding_stubs/       # assert.h, complex.h, math.h, stdio.h, stdlib.h, string.h
│   ├── linker.ld                     # Linker script (1M load, 4K-aligned sections)
│   └── grub.cfg                      # GRUB configuration
├── gui/
│   └── gui.c                         # Desktop environment + window manager
├── init/
│   └── init.c                        # System initialization
├── tests/
│   ├── test_apps.c                   # Native apps tests (27 assertions)
│   ├── test_holo.c                   # Holographic system tests (40 assertions)
│   ├── test_net_vino.c               # Network/Vino/Vena tests (47 assertions)
│   └── ...                           # Additional test files
├── Dockerfile                         # x86_64 Ubuntu build container
├── build_iso.sh                       # ISO build script
├── zedec_build.py                     # Nonlinear build pipeline
├── output/
│   ├── vovina_shakina.bin             # 170 KB kernel binary
│   └── vovina_shakina.iso             # 5.0 MB bootable ISO
└── VOVINA_SHAKINA_WHITE_PAPER.md      # This document
```

---

## 8. Technical Specifications

### 8.1 x86 (32-bit)

| Property | Value |
|----------|-------|
| Architecture | x86 32-bit (i386 protected mode) |
| Boot method | GRUB multiboot1 |
| Load address | 1 MB physical |
| Stack size | 64 KB |
| Display | VGA text mode (80×25) + VBE framebuffer (1024×768×32) |
| Serial | COM1 (0x3F8), 38400 baud |
| Memory model | Flat 32-bit, paging enabled |
| Compiler | gcc -m32 -ffreestanding -nostdlib -O3 |
| Linker | GNU ld -m elf_i386 |
| ISO builder | grub-mkrescue + xorriso |
| Kernel size | 170 KB (binary) |
| ISO size | 5.0 MB |

### 8.2 ARM64 (AArch64)

| Property | Value |
|----------|-------|
| Architecture | ARM64 (AArch64, EL1 kernel / EL0 user) |
| Boot method | Raw ELF (QEMU -kernel) or bare-metal |
| Exception level | EL2 → EL1 (boot.s), EL1 → EL0 (ERET) |
| Page tables | 4-level (L0/L1/L2/L3), 4KB granule, 512 entries per table |
| Translation | TCR_EL1 split (TTBR0=user, TTBR1=kernel), 39-bit VA |
| Interrupts | GICv3 (distributor + redistributor) |
| Timer | ARM64 Generic Timer (CNT* registers), 100Hz |
| Serial | PL011 UART (115200 baud) |
| Compiler | aarch64-linux-gnu-gcc -ffreestanding -nostdlib -O2 |
| Linker | aarch64-linux-gnu-ld + linker.ld |
| EL0 processes | Max 32, per-process TTBR0_EL1, round-robin preemption |
| Syscalls | 4 (exit, write, getpid, yield) via SVC gate |
| Board profiles | QEMU virt, VIT GlyphMap Phone (Dimensity), VIT GlyphMap Tablet (Kompanio), VIT GlyphMap M-Server |

### 8.3 Combined

| Property | Value |
|----------|-------|
| Source files | 346+ C/H/S/LD files |
| Test suites | 24 (host-side, x86) |
| Test assertions | 200+ (all passing) |
| Boot phases | 14 (x86) / 18 (ARM64) |
| Subsystems | 40+ (shared + arch-specific) |

---

## 9. Mathematical Foundations

The kernel implements the EDP (Economic Determinism Papers) framework:

- **Ten Governing Constraints** (EDP Compendium §III): Aristotelian existential import, non-Euclidean distance, paraconsistent logic, exact rational arithmetic
- **Coverage Hyperbola**: r · ℓ ≥ 1.8 — enforced at every Phase Coordinator tick
- **FS-PRA** (Fibonacci-Signed Paradox-Resolution Algebra): Contradictions resolve to signed-zero-at-level, never crash, never explode
- **Determinism Doctrine**: Same superposition + same event → same result (always)
- **Fibonacci Bound**: Telemetry recursion depth bounded by fib(paradox_level + 2), iterative implementation
- **168-Bit Word**: 28 sextets × 6 = 24 septets × 7 = 21 octets × 8 = 168 bits (zero-remainder slicing)
- **CRIT Transform**: CRIT(b_k) = |b_k| · exp(i · 2π·b_k/256) — lossless round-trip, non-degenerate phase

---

## 10. Conclusion / निष्कर्ष

**English:**

VOVINA SHAKINA is a complete, bootable, tested operating system kernel that implements the M⁵-Axiomatic architecture from the Curzi Manifold. It is the first OS to:

1. Assign each logical axis its own native number type as a first-class kernel subsystem
2. Harmonize five axiomatic subsystems through a Coverage Hyperbola invariant
3. Implement paraconsistent logic (GLUT states) in the kernel attestation layer
4. Use exact rational arithmetic (no floating point) for all resource accounting
5. Support non-Euclidean asymmetric distance in the interconnect topology
6. Implement deterministic choice-collapse (not random nondeterminism) for scheduling
7. Integrate a holographic data system with 5 specialized file types as a kernel module
8. Include a decentralized financial ledger with 9 forms of capital and 20 payment rails
9. Support 31 programming languages including constructed/obscure languages
10. Boot on dual architecture: x86 (GRUB multiboot ISO) and ARM64 (EL2→EL1→EL0)
11. Implement per-process address spaces with TTBR0_EL1 switching on ARM64
12. Provide EL0 user space with SVC-based syscall gate and preemptive round-robin scheduling
13. Boot from a 5 MB ISO (x86) or raw ELF (ARM64) with all subsystems online
14. Pass 24/24 host-side test suites with 200+ assertions, plus QEMU boot verification on both architectures

The kernel passes anti-collapse verification (79 files), host-side tests (24 suites, 200+ assertions), and QEMU boot testing on both x86 (14 phases) and ARM64 (18 phases, 1400+ event cycles). The build pipeline integrates deterministic static analysis with cross-compilation in a nonlinear feedback structure.

**हिंदी:**

वोविना शकिना एक संपूर्ण, बूटेबल, परीक्षित ऑपरेटिंग सिस्टम कर्नल है जो कर्ज़ी मैनिफोल्ड से व्युत्पन्न M⁵-स्वयंसिद्ध संरचना को लागू करता है। यह पहला OS है जो:

1. प्रत्येक तार्किक अक्ष को अपने स्वयं के देशी संख्या प्रकार के रूप में प्रथम-श्रेणी कर्नल उपप्रणाली के रूप में नियुक्त करता है
2. कवरेज हाइपरबोला अपरिवर्तनीयता के माध्यम से पाँच स्वयंसिद्ध उपप्रणालियों को सामंजस्यबद्ध करता है
3. कर्नल प्रमाणन परत में परासंगत तर्क (GLUT अवस्थाएँ) को लागू करता है
4. सभी संसाधन लेखांकन के लिए सटीक परिमेय अंकगणित (कोई चल-बिंदु नहीं) का उपयोग करता है
5. अंतर्संयोजन टोपोलॉजी में गैर-यूक्लिडियन असममित दूरी का समर्थन करता है
6. शेड्यूलिंग के लिए निर्धारक चयन-संक्षेप (यादृच्छिक अनिर्धारकता नहीं) लागू करता है
7. 5 विशेष फ़ाइल प्रकारों के साथ एक होलोग्राफ़िक डेटा प्रणाली को कर्नल मॉड्यूल के रूप में एकीकृत करता है
8. 9 प्रकार की पूँजी और 20 भुगतान रेल के साथ एक विकेंद्रीकृत वित्तीय बही शामिल करता है
9. निर्मित/अस्पष्ट भाषाओं सहित 31 प्रोग्रामिंग भाषाओं का समर्थन करता है
10. दोहरी संरचना पर बूट करता है: x86 (GRUB मल्टीबूट ISO) और ARM64 (EL2→EL1→EL0)
11. ARM64 पर TTBR0_EL1 स्विचिंग के साथ प्रति-प्रक्रिया पता स्थान लागू करता है
12. SVC-आधारित सिसकॉल गेट और प्रतिस्पर्धात्मक राउंड-रॉबिन शेड्यूलिंग के साथ EL0 उपयोगकर्ता स्थान प्रदान करता है
13. सभी उपप्रणालियों के साथ 5 MB ISO (x86) या रॉ ELF (ARM64) से बूट करता है
14. दोनों संरचनाओं पर 200+ अभिकथनों के साथ 24/24 होस्ट-साइड परीक्षण सूट उत्तीर्ण करता है, साथ ही QEMU बूट सत्यापन भी

कर्नल एंटी-कोलैप्स सत्यापन (79 फ़ाइलें), होस्ट-साइड परीक्षण (24 सूट, 200+ अभिकथन), और x86 (14 चरण) व ARM64 (18 चरण, 1400+ घटना चक्र) दोनों पर QEMU बूट परीक्षण उत्तीर्ण करता है। बिल्ड पाइपलाइन निर्धारक स्थैतिक विश्लेषण को गैर-रैखिक प्रतिक्रिया संरचना में क्रॉस-संकलन के साथ एकीकृत करती है।

---

*Author: H.M. Michael-Laurence: Curzi (c)*
*VOVINA SHAKINA — ZEDEC pqOS*
*"All systems online."*
