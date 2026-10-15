<!--
SPDX-FileCopyrightText: 2024-2026 Michael Laurence Curzi & 36N9 GENETICS LLC
SPDX-License-Identifier: LicenseRef-ZXV-Hardware-1.1
-->

# What ZXV needs from the hardware

This document is for both OEMs and for the ZXV kernel team. It says what
the hardware must provide so that ZXV runs on it, and what ZXV does not yet
support. Paths are relative to the repository root.

## 1. Processors and the ZXV ports that run on them

| Chip | Where | ISA | ZXV port | What runs there |
|---|---|---|---|---|
| AMD EPYC Embedded 8004 | Institutional host | x86-64 (AVX-512, AES-NI/VAES, PCLMULQDQ) | `kernel/arch/x86_64`, UEFI disc | ZXV kernel, ledgers, tensor engine (CPU) |
| AMD Ryzen AI Max+ 395 | Pioneer | x86-64 (AVX-512, AES-NI/VAES, PCLMULQDQ) | `kernel/arch/x86_64`, UEFI disc | ZXV kernel, tensor engine (CPU) |
| Microchip PolarFire SoC | Vanguard arbiter (or all tiers) | RV64GC | `kernel/arch/riscv` | Arbiter firmware |
| AMD Zynq UltraScale+ ZU7EV | Sovereign arbiter | AArch64 (Cortex-A53) + Cortex-R5F | `kernel/arch/arm64` | Arbiter firmware |
| AMD Versal Prime VM1802 | Citadel arbiter | AArch64 (Cortex-A72) + Cortex-R5F | `kernel/arch/arm64` | Arbiter firmware |
| NVIDIA Jetson AGX Thor | Institutional neural | AArch64 (Neoverse-V3AE) + Blackwell GPU | Vendor Linux, with `zxvd` hosted | Accelerator, outside the trust boundary |

ZXV bans floating point in kernel images, so no FPU feature is required
of any CPU. The tensor kernels use AVX2, AVX-512 and NEON where present, and
HQC uses `pclmulqdq` (x86) or `pmull` (AArch64) when built with those flags
([docs/PQ_MATRIX.md](../../../docs/PQ_MATRIX.md)). Build the x86 images with
`-march=znver4` (Siena) or `-march=znver5` (Strix Halo) to get them.

## 2. Firmware requirements (all x86 hosts)

| Requirement | Why |
|---|---|
| UEFI 2.x, 64-bit, no CSM needed | ZXV boots as `/EFI/BOOT/BOOTX64.EFI` ([docs/BAREMETAL.md](../../../docs/BAREMETAL.md)) |
| GOP with a 32-bit linear framebuffer on the centre panel | ZXV's boot console is the GOP framebuffer (`kernel/arch/x86_64/efifb_con.c`). Without GOP the screen stays black |
| A 16550-compatible UART at COM1 (0x3F8), brought out to the trench (institutional) or to a test pad (Pioneer) | ZXV logs `[BOOT_OK]` / `[AI_SELFCHECK_OK]` / `[AI_OK]` there at 38400 baud. It is the bring-up lifeline |
| Owner-controlled Secure Boot keys (custom PK/KEK/db) | The ZXV image is not yet signed by a CA the firmware trusts. Institutional: the arbiter does the real post-quantum check before UEFI runs ([OEM_INSTITUTIONAL.md](OEM_INSTITUTIONAL.md) section 6.1) |
| ACPI tables (MADT, FADT, MCFG, HPET or equivalent), complete and correct | `kernel/src/acpi` parses them for interrupts and PCIe configuration |
| Machine-check (MCA) error reporting enabled, with ECC errors reported, not hidden by firmware | The institutional spec promises ECC on every bus. ZXV must see corrected and uncorrected errors to act on them |
| No firmware-resident telemetry, and SMM handlers kept minimal and documented | Matches the zero-telemetry claim; long SMM stalls also break timing assumptions |

## 3. Device support in ZXV today, and the gaps

ZXV's drivers today are the ones QEMU needs: virtio block, net, GPU, input
and sound, an ATA driver, 16550 and PL011 UARTs, GICv3, the RISC-V PLIC,
the UEFI framebuffer and PCI enumeration, plus standard-class drivers for
real silicon in `kernel/src/hwdrv`: NVMe, AHCI, xHCI with USB keyboard and
mass storage, Intel e1000 and igb, and HD Audio, each bound and
self-tested at boot in QEMU (`build_system/hw_drivers_check.sh`), and a
Realtek r8169 driver tested on the host (`make -C kernel test-hwdrv`).
**None has run on a physical board yet, and no 10 GbE-class network, GPU
or Wi-Fi silicon is supported.** The Wi-Fi
subsystem (`kernel/src/wifi`) implements the 802.11 protocol layers and says
in its header that there is no Wi-Fi silicon driver.

Drivers that must exist before either product can ship, in priority order:

| # | Driver | Needed by | Notes |
|---|---|---|---|
| 1 | NVMe (PCIe) | All | Includes the TCG Opal security commands for the SED unlock flow |
| 2 | xHCI + USB HID keyboard | All | The pop-up keyboard is a USB device. On institutional models it is behind the arbiter, which presents it to the host as HID |
| 3 | Ethernet: Intel X710/E810 class | Institutional | Choose one NIC family for all tiers to write one driver |
| 3b | Ethernet: 2.5 GbE (Intel I226 or Realtek RTL8125 class) | Pioneer | Pick the part with the best public documentation |
| 4 | Arbiter mailbox and framebuffer device (PCIe) | Institutional | Defined by this project, so it can be simple: doorbells, message rings, and three linear framebuffers in a BAR. ZXV then draws all three panels without any GPU driver |
| 5 | Embedded controller, SMBus battery, fan and sensor access | All | Prefer an open EC firmware so the interface is documented |
| 6 | HDA or SoundWire codec + the audio DSP's control interface | All | The DSP runs the beamforming and cancellation itself; ZXV sends it PCM and settings |
| 7 | AMD display engine (DCN) for the wing panels | Pioneer | Large job. Until it exists, only the centre panel works under bare-metal ZXV (through GOP) |
| 8 | Wi-Fi silicon | All | Large job; most Wi-Fi chips need closed firmware blobs. Choose the chip by documentation quality |
| 9 | Cellular module (MBIM over USB) | Where fitted | |

**GPUs and AI accelerators.** A bare-metal driver for an AMD RDNA GPU or an
NVIDIA GPU is a multi-year effort, and NVIDIA's stack is closed. Plan for
two modes:

- **Bare metal:** ZXV runs its integer tensor engine on the CPU cores.
  On the Pioneer that is 16 Zen 5 cores with AVX-512; on a Siena host, 24
  to 64 cores.
- **Hosted:** ZXV's daemon `zxvd` runs on Linux (or Jetson Linux on the
  Thor modules) and reaches the GPU through the `vb_backend_t` contract in
  `kernel/src/vblock/vblock.h` (CUDA, Vulkan or ROCm). The accelerator's
  results come back through the co-processor tiers in `kernel/src/cotier/`,
  where the integer core reconciles them.

On institutional models this split is the design, not a stopgap: the host
runs bare-metal ZXV and the Thor modules are hosted accelerators outside the
trust boundary.

## 4. Arbiter board ports

| Arbiter | Interrupt controller | UART | ZXV status | Work |
|---|---|---|---|---|
| PolarFire SoC | RISC-V PLIC + CLINT | MMUART [CONFIRM 16550 compatibility] | riscv64 port boots under QEMU | Board profile (addresses), MMUART driver if not 16550-compatible |
| Zynq UltraScale+ | GIC-400 (GICv2) | Cadence UART | arm64 port uses GICv3 | GICv2 path for arm64, Cadence UART driver |
| Versal Prime | GIC-500 class (GICv3) [CONFIRM] | PL011 | arm64 port has GICv3 and PL011 | Board profile in `kernel/arch/arm64/board_profile.h` |

The PolarFire SoC needs the least new kernel code, which is another reason
to standardise on it ([OEM_INSTITUTIONAL.md](OEM_INSTITUTIONAL.md) section 6.4).

## 5. Telemetry the budget controller needs

`ct_budget` (`kernel/src/cotier/ct_budget.{h,c}`) throttles batch size,
context and the accelerator from three hardware metrics. Its tests use a
simulated machine; the briefcase is where it meets a real one. The hardware
must provide:

| Metric | Hardware |
|---|---|
| Power | A digital power monitor (TI INA228 class) on each of: CPU, neural module, DRAM, storage, projector, and the main DC bus. Read by the arbiter (institutional) or the EC (Pioneer) at ≥ 10 Hz |
| Stalls | The CPU's own performance counters (read by ZXV directly) |
| Thermal headroom | Die temperatures (CPU, neural, arbiter), intake and exhaust air temperatures, vault temperature |

The weights in `ct_budget` must be calibrated on each tier's prototype, as
[docs/COPROCESSOR_TIERS.md](../../../docs/COPROCESSOR_TIERS.md) already says. The
hot-ambient power caps in [PLATFORM_COMMON.md](PLATFORM_COMMON.md) section 6.3 are
enforced through this controller.

## 6. Other hardware services

- **Entropy:** the arbiter supplies an SP 800-90B entropy source over the
  mailbox. RDSEED on the x86 host is mixed in as a second source.
- **Real-time clock:** battery-backed RTC on the host, and a second one in
  the arbiter that the arbiter trusts for key and certificate validity.
- **Hardware kill switches:** each switch must cut power to its device
  directly, and report its position to the OS. ZXV must treat a device that
  vanishes from PCIe as a normal event, not a fault.

## 7. Development hardware to start on now

None of the custom boards exist yet. The kernel team can start every driver
in section 3 on off-the-shelf hardware with the same silicon:

| Target | Development board | Covers |
|---|---|---|
| Pioneer | Framework Desktop mainboard, Ryzen AI Max+ 395, 128 GB [CONFIRM availability] | Same APU, NVMe, USB, display, EC questions |
| Institutional host | Any Siena (SP6) server board, for example ASRock Rack SIENAD8 series [CONFIRM model] | Same CPU, ECC, MCA, NVMe, NICs |
| Arbiter, PolarFire | Microchip PolarFire SoC Icicle Kit (MPFS250T) | riscv64 board profile, mailbox, tamper I/O |
| Arbiter, Zynq | AMD ZCU104 (ZU7EV) | GICv2 and Cadence UART work |
| Arbiter, Versal | AMD VMK180 (VM1802) | arm64 board profile |
| Neural | NVIDIA Jetson AGX Thor developer kit | Hosted `zxvd`, CUDA `vb_backend_t`, reconciliation on real outputs |

Suggested order: (1) boot the existing x86_64 disc on the Siena board and
the Framework board, and get the serial log; (2) NVMe; (3) xHCI and HID;
(4) one NIC; (5) arbiter firmware on the Icicle Kit; (6) hosted `zxvd` on
Thor. That order gives a working, if headless, institutional host before any
custom PCB is ordered.
