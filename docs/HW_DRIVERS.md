<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# Standard class drivers (bare metal)

ZXV drives PC hardware through drivers written against **standards, not
chips** (layer 1 of the driver plan in the turnkey OS gap list). One NVMe
driver covers every NVMe SSD, one AHCI driver every SATA controller, one xHCI
driver every USB 3 host, and so on. Everything here is polled (no interrupts
yet), integer-only, and uses no libc.

Code: `kernel/src/hwdrv/`. Platform glue for x86_64: `kernel/arch/x86_64/hwplat_x86.c`.

## How a driver is chosen

At boot, after the economy self-checks and before `[BOOT_OK]`, the x86_64
kernel scans PCI, matches each function against `hw_drivers[]`
(`hwdrv_probe.c`), and calls the driver's bind. Specific vendor/device ids win
over class matches. Every bind returns one of four verdicts, the same shape
the paraconsistent firmware adapter (`kernel/src/fwadapt`) joins over:

| Verdict | Meaning | Boot line |
|---|---|---|
| T | bound, device answered correctly | `[DRIVER ONLINE]` |
| F | device answered but failed (timeout, fatal status) | `[DRIVER FAILED]` |
| B | device contradicts itself (e.g. NVMe class without the NVM command set) | `[DRIVER HELD] ... contradiction:` |
| N | nothing to drive (no BAR, no media, no codec) | `[DRIVER HELD]` |

Failed or held hardware is logged and skipped; it never stops the boot.

Then each device runs a short self-test: disks read LBA 1 and look for the GPT
header, then mount their EFI system partition as FAT32 and read
`/MEGAROM.TVL`; NICs broadcast a DHCPDISCOVER and wait for an offer; keyboards
poll for two seconds and echo what was typed; audio plays 0.1 s of tone and
checks the stream's DMA position moved.

## Drivers

| Driver | Standard | Devices | Tested in QEMU | Host test |
|---|---|---|---|---|
| `nvme.c` | NVMe 1.4 | every NVMe SSD (class 01/08/02) | `-device nvme` | `test_nvme.c` |
| `hw_storage.c` | UEFI GPT + FAT32 | any disk the drivers above expose | all three disks | `test_hw_storage.c` |
| `hwdrv_core.c`, `hwdrv_probe.c` | PCI 3.0 config space | BAR sizing incl. 64-bit, capabilities | every boot | `test_hwdrv_core.c` |

(Rows for AHCI, xHCI + USB, the Intel NIC family and HD Audio are added as
those drivers land in this PR.)

## Testing

- Host: `make -C kernel test-hwdrv`. Each driver runs against a
  register-level fake device (`kernel/src/hwdrv/test/hwfake.c`): config
  space with real BAR-sizing behaviour, BARs backed by host memory, and time
  that only moves when the driver polls, so every timeout path is exercised
  without waiting. ASan and UBSan are on.
- QEMU: `bash build_system/baremetal_images.sh x86_64` then
  `bash build_system/hw_drivers_check.sh`. It boots the UEFI disc under OVMF
  with one of every covered device, types `zxv` into the USB keyboard through
  the QEMU monitor, and fails unless every driver binds, every disk reads the
  GPT and the file off its ESP, both NICs get a DHCP offer, the keys arrive,
  and the audio stream runs. CI runs it in the `uefi-disc` job.

## Platform contract (`hw_plat_t`)

| Hook | x86_64 bare metal |
|---|---|
| config space | CAM ports 0xCF8/0xCFC |
| `mmio_map` | identity map with 2 MB pages, cache disabled, anywhere below 2^48 |
| `dma_alloc` | 8 MB static pool in the identity-mapped low 1 GB (bus address = virtual address) |
| `delay_us` | TSC, calibrated against PIT channel 2 (falls back to 2 GHz if there is no PIT) |

An arm64 platform (ECAM config space, generic timer, `dsb` barriers) would let
the same drivers run on arm64 bare metal in QEMU `virt`; Apple Silicon stays
hosted either way.

## Not done yet

- Interrupts (MSI/MSI-X). Polling is fine for boot and a single-threaded
  kernel loop, not for throughput.
- IOMMU. DMA is not fenced off from the rest of memory.
- Layer 3, the Linux driver VM, is not started. It needs a hypervisor in ZXV
  (VT-x/AMD-V with EPT/NPT) and IOMMU passthrough (VT-d/AMD-Vi) so a small
  Linux guest can own Wi-Fi, GPU and vendor devices and hand them back to ZXV
  as virtio devices. It is a separate piece of work.
- Nothing here has run on a physical machine. QEMU's models follow the specs
  closely but are more forgiving than real silicon about timing and reset.
