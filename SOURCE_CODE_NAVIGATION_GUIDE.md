# ZXV OS SOURCE CODE NAVIGATION GUIDE
## For Trenton Systems Engineering Teams

**Version:** 1.0
**Date:** July 29, 2026
**Classification:** CONFIDENTIAL — For Trenton Systems Engineering Teams
**Prepared by:** 36N9 Genetics, LLC

---

## 1. REPOSITORY OVERVIEW

The ZXV OS (ZEDEC XERO VULPINE) kernel is a bare-metal, freestanding operating system written in C with x86_64 assembly boot code. It runs on Intel architecture (Trenton's existing platform) with additional ARM32, ARM64, and RISC-V build targets for future expansion.

| Metric | Value |
|---|---|
| **Total source files** | 244 (.c + .h) |
| **Total lines of code** | 17,647 (kernel only) |
| **Test suites** | 24 (all pass, 0 warnings, 0 errors) |
| **Boot phases** | 18 (Phase 0-17) |
| **Architecture** | x86_64 primary; ARM32/ARM64/RISC-V secondary |
| **Language** | C (freestanding, no libc) |
| **Build system** | GNU Make + GCC cross-compiler |
| **License** | Apache-2.0 (see `LICENSE`) |

---

## 2. DIRECTORY STRUCTURE

```
07_SOURCE_CODE_REFERENCE/
├── SUBSYSTEM_INDEX.md          ← Start here — full subsystem map
├── VOVINA_SHAKINA_README.md    ← Project README
├── VOVINA_SHAKINA_WHITE_PAPER.md  ← Technical white paper
├── ZEDEC_NEW_MODULES.md        ← New module specifications
├── kernel/                     ← Main kernel source tree (334 files)
│   ├── Makefile                ← Kernel build system
│   ├── linker.ld               ← Linker script
│   ├── grub.cfg                ← GRUB bootloader config
│   ├── ARCHITECTURE_EXTERNAL_CLOCK_BRIDGE.md
│   ├── arch/                   ← Architecture-specific code
│   │   ├── arm/                ← ARM port
│   │   ├── arm32/              ← ARM32 port
│   │   ├── arm64/              ← ARM64 port
│   │   └── riscv/              ← RISC-V port
│   ├── boot/                   ← Boot sequence (Phase 0-17)
│   ├── compat/                 ← Compatibility layers
│   ├── include/                ← Global headers
│   │   ├── m5_types.h          ← Core type definitions
│   │   ├── freestanding.h      ← Freestanding C stubs
│   │   ├── multiboot.h         ← Multiboot bootloader spec
│   │   ├── license.h           ← License enforcement
│   │   └── freestanding_stubs/ ← stdlib/string/stdio stubs
│   ├── src/                    ← Kernel subsystems (see Section 3)
│   └── telemetry_core.c        ← Telemetry core
├── kernel_server/              ← Server edition kernel (184 files)
│   ├── Makefile
│   ├── boot/                   ← Server boot sequence
│   ├── compat/                 ← Server compatibility layer
│   ├── include/                ← Server headers
│   └── src/                    ← Server subsystems
├── subsystems/                 ← Userspace subsystems
│   ├── audiogenomics/          ← Genomic audio therapy
│   ├── governance/             ← Governance engine
│   ├── gridchain/              ← GridChain blockchain
│   ├── gui/                    ← GUI subsystem
│   ├── hccs/                   ← HCCS subsystem
│   ├── init/                   ← Init system
│   ├── neon/                   ← Neon subsystem
│   ├── os_lattice/             ← OS lattice
│   ├── physics_sim/            ← Physics simulation
│   ├── security/               ← Security subsystem
│   └── tests/                  ← Test suite
├── build_system/               ← Build scripts & Makefiles
│   ├── Makefile                ← x86_64 build
│   ├── Makefile.arm32          ← ARM32 build
│   ├── Makefile.arm64          ← ARM64 build
│   ├── Makefile.riscv          ← RISC-V build
│   ├── Makefile.riscv32        ← RISC-V 32-bit build
│   ├── Dockerfile              ← x86_64 Docker build
│   ├── Dockerfile.arm          ← ARM Docker
│   ├── Dockerfile.arm64        ← ARM64 Docker
│   ├── Dockerfile.riscv        ← RISC-V Docker
│   ├── Dockerfile.riscv32      ← RISC-V 32 Docker
│   ├── Dockerfile.x86_64       ← x86_64 Docker
│   ├── build_arm.sh            ← ARM build script
│   ├── build_iso.sh            ← ISO image build
│   ├── build_x86_64.sh         ← x86_64 build script
│   ├── docker_build.sh         ← Docker build helper
│   ├── docker_build_server.sh  ← Server Docker build
│   ├── generate_vm_images.sh   ← VM image generation
│   └── server_full_build.sh    ← Full server build
├── binaries/                   ← Pre-built binaries
│   ├── vovina_shakina.bin      ← Kernel binary (x86_64)
│   ├── vovina_shakina.bin.zip  ← Compressed kernel
│   ├── vovina_shakina.iso      ← Bootable ISO (11.5MB)
│   ├── kernel_arm64.bin        ← ARM64 kernel binary
│   └── kernel_arm64.elf        ← ARM64 ELF
├── test_binaries/              ← Compiled test executables
│   ├── test_audiogenomics      ← Audio-pharma tests
│   ├── test_drivers            ← Hardware driver tests
│   ├── test_governance         ← Governance tests
│   ├── test_gridchain          ← Blockchain tests
│   ├── test_hccs               ← HCCS tests
│   ├── test_integration        ← Integration tests
│   ├── test_lattice            ← Lattice tests
│   ├── test_neon               ← Neon tests
│   ├── test_physics            ← Physics sim tests
│   └── test_security           ← Security tests
└── tools/                      ← Development tools
    ├── zedec_build.py          ← Build system (Python)
    ├── zedec_docgen.py         ← Documentation generator
    ├── zedec_ai_coordinator.py ← AI coordinator
    └── zenodo_metadata.json    ← Zenodo archive metadata
```

---

## 3. KERNEL SUBSYSTEM MAP

### 3.1 Core Kernel (Start Here)

| Directory | Function | Key Files | Trenton Integration Notes |
|---|---|---|---|
| `boot/` | Boot sequence (Phase 0-17) | `boot.s`, `kernel_main.c` | x86_64 multiboot. Works with Trenton's Intel BIOS/UEFI. |
| `include/` | Global headers | `m5_types.h`, `freestanding.h`, `multiboot.h` | Core types — no changes needed. |
| `src/sched/` | Preemptive scheduler | `sched.c`, `sched.h` | Software-only. No hardware dependency. |
| `src/mm/` | Memory manager | `mm.c`, `mm.h` | Uses Intel MMU/paging. No changes needed. |
| `src/syscall/` | System call interface | `syscall.c`, `syscall.h` | Software-only. |
| `src/timer/` | Timer subsystem | `timer.c`, `timer.h` | Adapt to Intel HPET (currently PIT). |

### 3.2 Hardware Drivers (Require Trenton Adaptation)

| Directory | Function | Key Files | Trenton Integration |
|---|---|---|---|
| `src/pci/` | PCI/PCIe bus | `pci.c`, `pci.h` | Works with Intel PCIe. No changes needed. |
| `src/pic/` | Interrupt controller | `pic.c`, `pic.h` | Adapt to Intel APIC (code in `arch/`). |
| `src/acpi/` | Power management | `acpi.c`, `acpi.h` | Works with Intel ACPI. No changes needed. |
| `src/ata/` | Storage (IDE/SATA) | `ata.c` | Works with Intel SATA/AHCI. Add NVMe for SSD. |
| `src/net/rtl8139.c` | Ethernet driver | `rtl8139.c`, `rtl8139.h` | **Replace with Intel I225/I350 driver.** |
| `src/net/radio.c` | SDR radio abstraction | `radio.c`, `radio.h` | Phase 2: adapt to AD9361 SDR. |
| `src/net/jdr_piratenet.c` | Mesh networking | `jdr_piratenet.c`, `jdr_piratenet.h` | Software protocol — runs over any NIC. |
| `src/wifi/wifi.h` | Wi-Fi driver framework | `wifi.h` | Adapt to Intel AX210 firmware interface. |
| `src/bluetooth/bluetooth.h` | Bluetooth framework | `bluetooth.h` | Adapt to Intel AX210 BT 5.3. |
| `src/audio/audio.h` | Audio driver framework | `audio.h` | Intel HDA already supported in framework. |
| `src/keyboard/` | Keyboard driver | `keyboard.c`, `keyboard.h` | USB HID — works with any USB keyboard. |
| `src/mouse/` | Mouse driver | `mouse.c`, `mouse.h` | USB HID — works with any USB mouse. |
| `src/vbe/` | VESA framebuffer | `vbe.c`, `vbe.h` | Works with Intel UHD/GOP. |
| `src/video/` | Video output | `video.h` | Uses VBE framebuffer. |
| `src/hardware/` | Hardware abstraction | `rtl_device.h`, `dlp_projector.h` | Phase 2: DLP projector via TI DLP2010. |

### 3.3 Security Subsystem

| Directory | Function | Key Files | Trenton Integration |
|---|---|---|---|
| `src/porter_house/` | Post-quantum kernel security | `porter_house.c`, `porter_house.h` | Software-only — uses Intel AES-NI/RDRAND. |
| `src/immigration/` | Process authentication | `immigration.h` | Software-only. |
| `src/prism_break/` | Anti-surveillance | `prism_break.c`, `prism_break.h` | Software-only. |
| `src/panopticon/` | VPN & encrypted tunnels | `panopticon.c`, `panopticon_vpn.c` | Software-only. |
| `src/identity/` | Device identity & attestation | `identity.c`, `identity.h` | Phase 2: add ATECC608A secure element. |

### 3.4 Finance & Economy

| Directory | Function | Key Files | Trenton Integration |
|---|---|---|---|
| `src/vino/` | Vino Voucher currency | `vino.c`, `vino.h` | Software-only. |
| `src/finance/` | Triple Ledger & transactions | `triple_ledger.c`, `triple_ledger.h`, `financial.h` | Software-only. |
| `src/community_chest/` | Developer marketplace | `community_chest.h` | Software-only. |
| `src/count_house/` | Transaction clearing | `count_house.c` | Software-only. |
| `src/robin_debanks/` | Sovereign finance | `robin_debanks.c`, `robin_debanks.h` | Software-only. |
| `src/crypto_wallet/` | Cryptocurrency wallet | (in finance) | Software-only. |

### 3.5 AI & Quantum

| Directory | Function | Key Files | Trenton Integration |
|---|---|---|---|
| `src/ai_layer/` | AI inference abstraction | `ai_layer.h` | Uses Intel NPU. SD-HAL virtualizes. |
| `src/quantum/` | Quantum device emulation | `quantum_device.c`, `quantum_device.h` | Software-only — runs on NPU. |
| `src/superpos/` | Superposition simulation | `superpos.c`, `superpos.h` | Software-only. |
| `src/epu/` | Emotional Processing Unit | `epu_device.h` | Phase 2-3 R&D. Software emulation in Phase 1. |
| `src/synthesis/` | AI synthesis engine | `synthesis_engine.c`, `synthesis_engine.h` | Software-only. |
| `src/predictive/` | Predictive analytics | `predictive_model.c`, `predictive_model.h` | Software-only. |
| `src/situation/` | Tactical situation model | `situation_model.c`, `situation_model.h` | Software-only. |

### 3.6 Mesh & P2P

| Directory | Function | Key Files | Trenton Integration |
|---|---|---|---|
| `src/mesh_net/` | Mesh networking | `mesh_net.c`, `mesh_net.h` | Software protocol — runs over any NIC. |
| `src/mesh_token/` | Mesh authentication | `mesh_token.c`, `mesh_token.h` | Software-only. |
| `src/net/m5route.c` | M5 routing protocol | `m5route.c`, `m5route.h` | Software-only. |
| `src/p2p_caracho/` | P2P content distribution | `p2p_caracho.c`, `p2p_caracho.h` | Software-only. |
| `src/bootlegger/` | P2P media streaming | `bootlegger.c`, `bootlegger.h` | Software-only. |

### 3.7 Specialized Subsystems

| Directory | Function | Key Files | Trenton Integration |
|---|---|---|---|
| `src/audiogenomics_pro/` | Genomic audio therapy | `audiogenomics_pro.c`, `digital_dna.c` | Software-only. Uses audio subsystem. |
| `src/holographic/` | Holographic render | `holo.c`, `holo.h` | Phase 2+ — requires DLP projector. |
| `src/lattice/` | OS lattice | `lattice.c`, `lattice.h` | Software-only. |
| `src/legal_engine/` | On-device legal compliance | `legal_engine.c`, `legal_engine.h` | Software-only. |
| `src/license/` | SEL-3.3 license enforcement | `license.c` | Software-only. |
| `src/gematria/` | Numerical encoding | `gematria.c` | Software-only. |
| `src/surplus/` | Interaction Surplus Framework | `surplus.c`, `surplus.h` | Software-only. |
| `src/telemetry/` | System health monitoring | `telemetry_core.c`, `telemetry_core.h` | Software-only — uses ACPI sensors. |
| `src/recon/` | System reconnaissance | `recon.c`, `recon.h` | Software-only. |
| `src/dualtrack/` | Dual-track processing | (in src) | Software-only. |
| `src/edp_risk/` | EDP risk management | `edp_risk.c`, `edp_risk.h` | Software-only. |
| `src/choice/` | Decision engine | (in src) | Software-only. |
| `src/ascent/` | Ascent subsystem | (in src) | Software-only. |
| `src/crit168/` | Critical path optimization | (in src) | Software-only. |
| `src/hdcm/` | Hardware-dependent config | (in src) | Software-only. |
| `src/plnp/` | PLNP subsystem | `plnp.c`, `plnp.h` | Software-only. |
| `src/nlb/` | NLB subsystem | `nlb.c`, `nlb.h` | Software-only. |
| `src/oseq/` | OSEQ core | `oseq_core.c`, `oseq_core.h` | Software-only. |
| `src/rmag/` | RMAG core | `rmag_core.c`, `rmag_core.h` | Software-only. |
| `src/iphase/` | I-phase core | `iphase_core.c`, `iphase_core.h` | Software-only. |
| `src/lpres/` | LPRES core | `lpres_core.c`, `lpres_core.h` | Software-only. |
| `src/phase_coord/` | Phase coordinator | `phase_coordinator.c` | Software-only. |
| `src/smap/` | SMAP subsystem | `smap.c`, `smap.h` | Software-only. |
| `src/pungent/` | Pungent subsystem | `pungent.c`, `pungent.h` | Software-only. |
| `src/vena/` | Vena subsystem | `vena.c`, `vena.h` | Software-only. |
| `src/vfs/` | Virtual filesystem | `vfs.c`, `vfs.h` | Software-only. |
| `src/fat32/` | FAT32 filesystem | `fat32.c` | Software-only. |
| `src/desktop/` | Desktop environment | (in src) | Software-only. |
| `src/browser/` | Web browser | (in src) | Software-only. |
| `src/apps/` | Applications | (in src) | Software-only. |
| `src/xedit/` | Text editor | `xedit.c`, `xedit.h` | Software-only. |
| `src/pterm/` | Terminal emulator | `pterm.c`, `pterm.h` | Software-only. |
| `src/event_sched/` | Event scheduler | (in src) | Software-only. |
| `src/clock/` | Clock subsystem | `event_clock.h` | Uses Intel RTC. |

---

## 4. HOW TO BUILD

### 4.1 Prerequisites

```bash
# Install GCC cross-compiler for x86_64
brew install x86_64-elf-gcc  # macOS
# or
apt install gcc-multilib     # Linux

# Install QEMU for testing
brew install qemu            # macOS
# or
apt install qemu-system-x86  # Linux
```

### 4.2 Build Kernel (x86_64)

```bash
cd 07_SOURCE_CODE_REFERENCE/build_system/
make -f Makefile
# Output: kernel/vovina_shakina.bin
```

### 4.3 Build Bootable ISO

```bash
cd 07_SOURCE_CODE_REFERENCE/build_system/
./build_iso.sh
# Output: binaries/vovina_shakina.iso
```

### 4.4 Test in QEMU

```bash
qemu-system-x86_64 -cdrom binaries/vovina_shakina.iso -m 256M -serial stdio
```

### 4.5 Run Test Suite

```bash
cd 07_SOURCE_CODE_REFERENCE/test_binaries/
./test_security
./test_drivers
./test_integration
# ... all 10 test binaries
```

### 4.6 Build for ARM64 (Future Trenton ARM Products)

```bash
cd 07_SOURCE_CODE_REFERENCE/build_system/
make -f Makefile.arm64
# Output: binaries/kernel_arm64.bin
```

### 4.7 Docker Build

```bash
cd 07_SOURCE_CODE_REFERENCE/build_system/
docker build -f Dockerfile.x86_64 -t zxv-os .
docker run -it zxv-os
```

---

## 5. INTEGRATION ROADMAP FOR TRENTON ENGINEERS

### Phase 1 — Weeks 1-12

| Week | Task | Team | Output |
|---|---|---|---|
| 1-2 | Kernel boots on Trenton TAC board (QEMU verification first, then bare metal) | Firmware | Boot log on Trenton hardware |
| 3-4 | Intel I225/I350 Ethernet driver (replace `rtl8139.c`) | Driver | Network connectivity |
| 5-6 | Intel HDA audio driver (adapt `audio.h` framework) | Driver | Audio output |
| 7-8 | Intel AX210 Wi-Fi+BT driver (adapt `wifi.h`/`bluetooth.h`) | Driver | Wireless connectivity |
| 9-10 | Intel HPET timer + APIC (adapt `timer.c`/`pic.c`) | Firmware | Stable timing & interrupts |
| 11-12 | SD-HAL BSP integration with Intel NPU | Core | SD-HAL runtime on Trenton board |

### Phase 1 — Weeks 13-24

| Week | Task | Team | Output |
|---|---|---|---|
| 13-16 | Vino Voucher Triple Ledger on hardware (secure enclave) | Security | On-device transactions |
| 17-20 | Chiglet AI runtime on Intel NPU | AI | Chiglet companion operational |
| 21-24 | Full system integration test + MIL-STD certification prep | QA | Production-ready firmware |

### Phase 2 — Months 7-18

| Month | Task | Team | Output |
|---|---|---|---|
| 7-9 | AD9361 SDR module + driver | Hardware/Driver | Mesh radio operational |
| 10-12 | Niobium acoustic engine prototype | Hardware | Audio-pharma engine |
| 13-15 | TI DLP2010 pico projector integration | Hardware | Holographic display |
| 16-18 | ATECC608A secure element integration | Security | Hardware attestation |

---

## 6. KEY FILES FOR TRENTON ENGINEERS

### If you only read 5 files, read these:

1. **`SUBSYSTEM_INDEX.md`** — Full map of every kernel subsystem
2. **`VOVINA_SHAKINA_WHITE_PAPER.md`** — Technical architecture overview
3. **`kernel/include/m5_types.h`** — Core type definitions (the foundation)
4. **`kernel/boot/kernel_main.c`** — Entry point (where the kernel starts)
5. **`TRENTON_CHIPSET_PROCUREMENT_GUIDE.md`** (in `05_HARDWARE_PROPOSALS/`) — Hardware adaptation guide

### For specific integration tasks:

| Task | Start With |
|---|---|
| **Boot the kernel on Trenton hardware** | `kernel/boot/kernel_main.c` + `build_system/Makefile` |
| **Write Intel NIC driver** | `kernel/src/net/rtl8139.c` (reference) + `kernel/src/pci/pci.c` |
| **Adapt audio for Intel HDA** | `kernel/src/audio/audio.h` (framework) |
| **Adapt Wi-Fi for Intel AX210** | `kernel/src/wifi/wifi.h` (framework) |
| **Integrate SD-HAL with Intel NPU** | `kernel/src/ai_layer/ai_layer.h` + `kernel/src/hardware/rtl_device.h` |
| **Understand security architecture** | `kernel/src/porter_house/porter_house.h` |
| **Understand Vino Voucher system** | `kernel/src/vino/vino.h` + `kernel/src/finance/triple_ledger.h` |
| **Understand Chiglet AI** | `kernel/src/ai_layer/ai_layer.h` + `kernel/src/surplus/surplus.h` |
| **Understand mesh networking** | `kernel/src/mesh_net/mesh_net.h` + `kernel/src/net/jdr_piratenet.h` |

---

## 7. CODE CONVENTIONS

- **Language:** C99, freestanding (no libc — custom stubs in `include/freestanding_stubs/`)
- **Types:** Use `m5_types.h` — `uint8_t`, `uint16_t`, `uint32_t`, `uint64_t`, etc.
- **Headers:** Every `.c` file has a corresponding `.h` file with the same name
- **Testing:** Every subsystem has a `test_*.c` file — run all tests before committing
- **License:** All files carry SEL-3.3 license header — do not remove
- **Author:** H.M. Michael-Laurence Curzi (c), 36N9 Genetics, LLC
- **Style:** K&R brace style, 4-space indentation, snake_case naming

---

© 36N9 Genetics, LLC. All Rights Reserved.
Confidential — For Trenton Systems Engineering Teams Only.
