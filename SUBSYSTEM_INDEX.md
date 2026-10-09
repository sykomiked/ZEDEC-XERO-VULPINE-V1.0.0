# ZXV OS — Source Code Reference & Subsystem Index

**Document:** SUBSYSTEM_INDEX.md
**Folder:** 07_SOURCE_CODE_REFERENCE
**Date:** July 26, 2026
**Kernel:** VOVINA SHAKINA M5 Axiomatic Kernel
**Source:** 244 files (.c + .h), 17,647 lines of code

---

## 1. Kernel Source Tree

The complete kernel source is copied to `07_SOURCE_CODE_REFERENCE/kernel/`.
Below is the full subsystem index with file paths, descriptions, and test status.

### 1.1 Core M5 Axiomatic Kernel

| Subsystem | Source File | Header | Lines | Tests | Status |
|-----------|-----------|--------|-------|-------|--------|
| **Phase Coordinator** | `src/phase_coord/phase_coordinator.c` | `phase_coordinator.h` | ~200 | `test_phase_coordinator.c` | PASS |
| **RMAG (Rational Magnitude)** | `src/rmag/rmag_core.c` | `rmag_core.h` | ~150 | `test_rmag.c` | PARTIAL |
| **LPRES (Logical Presence)** | `src/lpres/lpres_core.c` | `lpres_core.h` | ~180 | `test_lpres.c` | PARTIAL |
| **IPHASE (Interleaved Phase)** | `src/iphase/iphase_core.c` | `iphase_core.h` | ~120 | `test_iphase.c` | PASS |
| **CHOICE (Identity Collapse)** | `src/choice/choice_core.c` | `choice_core.h` | ~100 | `test_choice.c` | PASS |
| **OSEQ (Ordered Sequence)** | `src/oseq/oseq_core.c` | `oseq_core.h` | ~90 | `test_oseq.c` | PASS |
| **CRIT-168 Word** | `src/crit168/crit_168_word.c` | `crit_168_word.h` | ~80 | `test_crit_168_word.c` | PASS |
| **Surplus Real (Fixed-Point)** | `src/surplus/surplus.c` | `surplus.h` | ~200 | Used by all | PASS |
| **EDP Risk Calculus** | `src/edp_risk/edp_risk.c` | `edp_risk.h` | ~300 | `test_axiom_matrix.c` | PASS |
| **Axiom Matrix** | `src/axiom_matrix/axiom_matrix_core.c` | `axiom_matrix_core.h` | ~150 | `test_axiom_matrix.c` | PASS |
| **Telemetry** | `src/telemetry/telemetry_core.c` | `telemetry_core.h` | ~120 | `test_telemetry.c` | PASS |

### 1.2 ARM64 Architecture

| Subsystem | Source File | Lines | Status |
|-----------|-----------|-------|--------|
| **Boot** | `arch/arm64/boot.S` | ~100 | PASS (QEMU boot) |
| **MMU/Paging** | `arch/arm64/arm64_mmu.c` | ~150 | PASS |
| **GICv3 Interrupt Controller** | `arch/arm64/gicv3.c` | ~200 | PASS |
| **Generic Timer** | `arch/arm64/arm64_timer.c` | ~100 | PASS |
| **Exceptions** | `arch/arm64/arm64_exceptions.c` | ~80 | PASS |
| **UART (PL011)** | `arch/arm64/uart_pl011.c` | ~120 | PASS |
| **Kernel Main (ARM64)** | `arch/arm64/kernel_main_arm64.c` | ~555 | PASS |
| **Linker Script** | `arch/arm64/linker.ld` | ~60 | PASS (PHDRS RX/RW) |

### 1.3 Security & Admission Control

| Subsystem | Source File | Header | Lines | Tests | Status |
|-----------|-----------|--------|-------|-------|--------|
| **Porter House Firewall** | `src/porter_house/porter_house.c` | `porter_house.h` | ~250 | `test_porter_house.c` (11 groups) | PASS |
| **Immigration Enforcement** | `src/immigration/immigration.c` | `immigration.h` | ~191 | `test_immigration.c` (9 groups) | PASS |
| **Robin DeBanks Vault** | `src/robin_debanks/robin_debanks.c` | `robin_debanks.h` | ~209 | `test_robin_debanks.c` (10 groups) | PASS |

### 1.4 Financial & Economic Engine

| Subsystem | Source File | Header | Lines | Tests | Status |
|-----------|-----------|--------|-------|-------|--------|
| **Vino Decentralized Bank** | `src/vino/vino.c` | `vino.h` | ~400 | N/A (ledger layer) | PASS |
| **Count House** | `src/count_house/count_house.c` | `count_house.h` | ~350 | `test_count_house.c` (14 groups) | PASS |
| **Count House Fractal** | (same) | (same) | — | `test_count_house_fractal.c` (7 groups) | PASS |
| **Triple Ledger** | `src/finance/triple_ledger.c` | `triple_ledger.h` | ~250 | N/A | PASS |
| **Financial Instruments** | `src/finance/financial.c` | `financial.h` | ~300 | N/A | PASS |
| **Payment Rails** | `src/finance/rails.c` | `rails.h` | ~200 | N/A | PASS |
| **Crypto Bridge** | `src/finance/crypto_bridge.c` | `crypto_bridge.h` | ~350 | N/A | PASS |
| **Community Chest** | `src/community_chest/community_chest.c` | `community_chest.h` | ~300 | `test_community_chest.c` (18 tests) | PASS |
| **Mesh-Token Settlement** | `src/mesh_token/mesh_token.c` | `mesh_token.h` | ~250 | `test_mesh_token.c` (8 groups) | PASS |

### 1.5 Networking & P2P

| Subsystem | Source File | Header | Lines | Tests | Status |
|-----------|-----------|--------|-------|-------|--------|
| **JDR PirateNet** | `src/net/jdr_piratenet.c` | `jdr_piratenet.h` | ~300 | N/A (protocol) | PASS |
| **P2P Mesh Network** | `src/mesh_net/mesh_net.c` | `mesh_net.h` | ~305 | `test_mesh_net.c` (13 groups) | PASS |
| **AI Integration Layer** | `src/ai_layer/ai_layer.c` | `ai_layer.h` | ~280 | `test_ai_layer.c` (8 groups) | PASS |

### 1.6 Hardware-as-Code & Display

| Subsystem | Source File | Header | Lines | Tests | Status |
|-----------|-----------|--------|-------|-------|--------|
| **RTL Device Framework** | `src/hardware/rtl_device.c` | `rtl_device.h` | ~250 | N/A | PASS |
| **DLP Pico Projector** | `src/hardware/dlp_projector.c` | `dlp_projector.h` | ~200 | `test_dlp_projector.c` | PASS |
| **Prism Break Shader** | `src/prism_break/prism_break.c` | `prism_break.h` | ~290 | `test_prism_break.c` (13 tests) | PASS |

### 1.7 Application & Runtime

| Subsystem | Source File | Header | Lines | Tests | Status |
|-----------|-----------|--------|-------|-------|--------|
| **Vena Runtime** | `src/vena/vena.c` | `vena.h` | ~200 | N/A | PASS |
| **Event-Driven Scheduler** | `src/event_sched/event_sched.c` | `event_sched.h` | ~274 | `test_event_sched.c` (11 groups) | PASS |
| **Holographic Data** | `src/holographic/holo.c` | `holo.h` | ~150 | N/A | PASS |

### 1.8 Additional Subsystems

| Subsystem | Source File | Description |
|-----------|-----------|-------------|
| **Compatibility Layer** | `compat/compat_layer.c` | x86/ARM64 compat stubs |
| **Quantum Device** | `src/quantum/quantum_device.c` | Quantum/exotic-matter research (SIM_DEVICES gated) |
| **Situation Modeler** | `src/situation/situation_model.c` | 15-domain tactical/strategic analysis |
| **Predictive Model** | `src/predictive/predictive_model.c` | ISF surplus, predictive analytics |
| **Identity Registry** | `src/identity/identity.c` | National identity (SIM_DEVICES gated) |
| **Synthesis Engine** | `src/synthesis/synthesis_engine.c` | Hardware synthesis |
| **Crypto Wallet** | `src/crypto_wallet/crypto_wallet.c` | On-device cryptocurrency wallet |
| **NLB** | `src/nlb/nlb.c` | Natural Language Bridge |
| **PTerm** | `src/pterm/pterm.c` | ZXV native terminal |
| **XEdit** | `src/xedit/xedit.c` | ZXV native text editor |
| **Lattice** | `src/lattice/lattice.c` | Lattice computation |
| **Pungent** | `src/pungent/pungent.c` | Scent/chemical analysis |
| **Ascent** | `src/ascent/ascent.c` | Ascent trajectory computation |
| **PLNP** | `src/plnp/plnp.c` | Programming Language Natural Processor |
| **Audio Genomics Pro** | `src/audiogenomics_pro/` | DNA/RNA frequency synthesis |
| **Legal Engine** | `src/legal_engine/` | On-device legal compliance |
| **License Manager** | `src/license/license.c` | Apache-2.0 licence identity and attribution |
| **Bootlegger** | `src/bootlegger/bootlegger.c` | Boot management |
| **Decent** | `src/decent/decent.c` | Decentralized consensus |
| **DualTrack** | `src/dualtrack/dualtrack.c` | Dual-track execution |
| **Gematria** | `src/gematria/gematria.c` | Gematria computation |
| **HDCM** | `src/hdcm/hdcm.c` | Hardware Device Configuration Manager |
| **Panopticon** | `src/panopticon/` | Security monitoring + VPN |
| **Recon** | `src/recon/recon.c` | Reconnaissance |
| **Smap** | `src/smap/smap.c` | Spatial mapping |
| **Superpos** | `src/superpos/superpos.c` | Superposition management |
| **P2P Caracho** | `src/p2p_caracho/p2p_caracho.c` | P2P file sharing |
| **VFS** | `src/vfs/vfs.c` | Virtual File System |
| **FAT32** | `src/fat32/fat32.c` | FAT32 filesystem |
| **MM** | `src/mm/mm.c` | Memory management |
| **Scheduler (legacy)** | `src/sched/sched.c` | Round-robin scheduler (replaced by event_sched) |
| **Syscall** | `src/syscall/syscall.c` | System call interface |
| **Network Stack** | `src/net/net.c` | TCP/IP network stack |
| **Radio** | `src/net/radio.c` | Radio interface |
| **DTMF** | `src/net/dtmf.c` | DTMF tone processing |
| **M5 Route** | `src/net/m5route.c` | M5 routing engine |
| **RTL8139** | `src/net/rtl8139.c` | RTL8139 network driver |
| **PCI** | `src/pci/pci.c` | PCI bus driver |
| **ATA** | `src/ata/ata.c` | ATA/IDE disk driver |
| **VBE** | `src/vbe/vbe.c` | VBE framebuffer |
| **Keyboard** | `src/keyboard/keyboard.c` | Keyboard driver |
| **Mouse** | `src/mouse/mouse.c` | Mouse driver |
| **Timer** | `src/timer/timer.c` | Timer subsystem |
| **GDT** | `src/gdt/gdt.c` | GDT (x86) |
| **IDT** | `src/idt/idt.c` | IDT (x86) |
| **PIC** | `src/pic/pic.c` | PIC (x86) |
| **ACPI** | `src/acpi/acpi.c` | ACPI (x86) |
| **Apps** | `src/apps/apps.c` | Built-in applications |
| **Framebuffer** | `boot/framebuffer.c` | Boot framebuffer |

---

## 2. Build System

| File | Target | Description |
|------|--------|-------------|
| `Makefile` (kernel/) | x86 ISO | Host test suite + x86 kernel build |
| `Makefile.arm64` (root) | ARM64 ELF | ARM64 cross-compile for QEMU virt |

### Build Commands
```bash
# ARM64 cross-compile (zero warnings, zero errors)
make -f Makefile.arm64

# Run all tests (24/24 pass, zero warnings)
cd kernel && make test

# QEMU boot verification
qemu-system-aarch64 -M virt,gic-version=3 -cpu cortex-a53 -m 256M \
  -kernel kernel_arm64.elf -nographic -serial mon:stdio
```

---

## 3. Test Suite Summary

```
All OSEQ tests passed
All IPHASE tests passed
Identity collapse test passed
Zero collapse test passed
All-ones collapse test passed
All CHOICE tests passed
All Phase Coordinator tests passed
All Telemetry tests passed
All Axiom Matrix tests passed
All CRIT-168 tests passed
Compatibility layer tests passed.
All Quantum Device tests passed
All DLP Projector tests passed
All Count House tests passed
All Count House fractal scaling tests passed
All Porter House tests passed
All Event-Driven Scheduler tests passed
All Mesh-Token External Settlement tests passed
All Community Chest tests passed
All AI Integration Layer tests passed
All P2P Mesh Network tests passed
All Immigration Enforcement tests passed
All Robin DeBanks Vault tests passed
All Prism Break shader tests passed
```

**24/24 test suites pass. 0 warnings. 0 errors.**

---

© 36N9 Genetics, LLC & Michael Laurence Curzi. All Rights Reserved.
