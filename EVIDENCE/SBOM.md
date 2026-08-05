# Software Bill of Materials (SBOM)
## Vovina Shakina M5 Axiomatic Kernel — ZEDEC pqOS
### Version: 1.0-dev | Date: 2026-08-03

## Kernel Core
| Component | Source | License | Version |
|-----------|--------|---------|---------|
| ARM64 Boot (boot.S) | kernel/arch/arm64/boot.S | SEL-3.3 | 1.0 |
| ARM64 UART (PL011) | kernel/arch/arm64/uart_pl011.c | SEL-3.3 | 1.0 |
| ARM64 GICv3 | kernel/arch/arm64/gicv3.c | SEL-3.3 | 1.0 |
| ARM64 Timer | kernel/arch/arm64/arm64_timer.c | SEL-3.3 | 1.0 |
| ARM64 MMU | kernel/arch/arm64/arm64_mmu.c | SEL-3.3 | 1.0 |
| ARM64 Exceptions | kernel/arch/arm64/arm64_exceptions.c | SEL-3.3 | 1.0 |
| ARM64 EL0 Userspace | kernel/arch/arm64/el0_userspace.c | SEL-3.3 | 1.0 |
| ARM64 RAM Disk | kernel/arch/arm64/ramdisk.c | SEL-3.3 | 1.0 |
| Kernel Main (ARM64) | kernel/arch/arm64/kernel_main_arm64.c | SEL-3.3 | 1.0 |
| Freestanding Runtime | kernel/freestanding.c, kernel/include/freestanding.h | SEL-3.3 | 1.0 |

## Scheduler & IPC
| Component | Source | License |
|-----------|--------|---------|
| Process Scheduler | kernel/src/sched/sched.c | SEL-3.3 |
| EL0 Scheduler (portable) | kernel/src/el0_userspace/el0_sched.c | SEL-3.3 |
| IPC Message Passing | kernel/arch/arm64/el0_userspace.c (ipc_send/recv) | SEL-3.3 |
| Syscall Interface | kernel/src/syscall/syscall.c | SEL-3.3 |

## Filesystem
| Component | Source | License |
|-----------|--------|---------|
| Virtual Filesystem (VFS) | kernel/src/vfs/vfs.c | SEL-3.3 |
| FAT32 Driver | kernel/src/fat32/fat32.c | SEL-3.3 |
| Block Device Abstraction | kernel/src/blockdev/blockdev.c | SEL-3.3 |
| RAM Disk (ARM64) | kernel/arch/arm64/ramdisk.c | SEL-3.3 |
| ATA Driver (x86) | kernel/src/ata/ata.c | SEL-3.3 |

## Networking
| Component | Source | License |
|-----------|--------|---------|
| TCP/IP Stack | kernel/src/net/net.c | SEL-3.3 |
| M5 Omni-Router | kernel/src/net/m5route.c | SEL-3.3 |
| JDR PirateNet | kernel/src/net/jdr_piratenet.c | SEL-3.3 |
| RTL8139 Driver (x86) | kernel/src/net/rtl8139.c | SEL-3.3 |

## Security
| Component | Source | License |
|-----------|--------|---------|
| AES-256-GCM | kernel/src/robin_debanks/aes256_gcm.c | SEL-3.3 |
| SHA-256 | kernel/src/robin_debanks/sha256.c | SEL-3.3 |
| Ed25519 Verification | kernel/src/robin_debanks/ed25519_verify.c | SEL-3.3 |
| Porter House Secure Boot | kernel/src/porter_house/porter_house.c | SEL-3.3 |
| Robin DeBanks Vault | kernel/src/robin_debanks/robin_debanks.c | SEL-3.3 |

## Sutra Language Toolchain
| Component | Source | License |
|-----------|--------|---------|
| Sutra Lexer | kernel/src/sutra/sutra_lexer.c | SEL-3.3 |
| Sutra Parser | kernel/src/sutra/sutra_parser.c | SEL-3.3 |
| Sutra Runtime | kernel/src/sutra/sutra_runtime.c | SEL-3.3 |
| Sutra Capital | kernel/src/sutra/sutra_capital.c | SEL-3.3 |
| Sutra Rails | kernel/src/sutra/sutra_rails.c | SEL-3.3 |
| Sutra Chiglet | kernel/src/sutra/sutra_chiglet.c | SEL-3.3 |
| Sutra Self-Audit | kernel/src/sutra/sutra_selfaudit.c | SEL-3.3 |

## Terminal & UI
| Component | Source | License |
|-----------|--------|---------|
| P-TERM Terminal | kernel/src/pterm/pterm.c | SEL-3.3 |
| P-TERM Commands | kernel/src/pterm/pterm_commands.c | SEL-3.3 |
| Prism Break Shader | kernel/src/prism_break/prism_break.c | SEL-3.3 |

## M5 Axiomatic Subsystems (31 total)
| Component | Source |
|-----------|--------|
| OSEQ (Ordinal Sequencer) | kernel/src/oseq/oseq_core.c |
| RMAG (Rational Magnitude) | kernel/src/rmag/rmag_core.c |
| LPRES (Logical Presence) | kernel/src/lpres/lpres_core.c |
| IPHASE (Imaginary Phase Router) | kernel/src/iphase/iphase_core.c |
| CHOICE (Choice Resolution) | kernel/src/choice/choice_core.c |
| Phase Coordinator | kernel/src/phase_coord/phase_coordinator.c |
| Telemetry | kernel/src/telemetry/telemetry_core.c |
| Axiom Matrix | kernel/src/axiom_matrix/axiom_matrix_core.c |
| CRIT168 | kernel/src/crit168/crit_168_word.c |
| Vino (Triple Ledger) | kernel/src/vino/vino.c |
| Vena (App Runtime) | kernel/src/vena/vena.c |
| Holographic Data | kernel/src/holographic/holo.c |
| + 19 additional subsystems | kernel/src/*/ |

## Build System
| Component | Source |
|-----------|--------|
| ARM64 Makefile | build_system/Makefile.arm64 |
| x86_64 Makefile | build_system/Makefile |
| ARM64 Linker Script | kernel/arch/arm64/linker.ld |

## External Dependencies
| Dependency | Version | Purpose |
|-----------|---------|---------|
| aarch64-linux-gnu-gcc | 15.2.0 | ARM64 cross-compiler |
| libgcc (static) | 15.2.0 | Soft-float, division |
| qemu-system-aarch64 | any | ARM64 emulation |

**Total source files: ~120 | Total subsystems: 31 | License: SEL-3.3**
