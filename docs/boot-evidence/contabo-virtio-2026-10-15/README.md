<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# x86_64 on a KVM cloud's devices: virtio-scsi and virtio-net

A Contabo VM (and most KVM clouds) gives the guest a virtio-scsi disk and a
virtio-net NIC, under SeaBIOS or OVMF. Before the virtio drivers
(`kernel/src/hwdrv/virtio_pci.c`, `vscsi.c`, `vblk.c`, `vnet.c`) the kernel
reached `[BOOT_OK]` there, then printed `[MODELS_NONE]`: it could not see its
own disc. These runs show it now finds the disc, verifies the model on it,
and gets a DHCP offer, with no AHCI, NVMe or e1000 in the machine.

QEMU 8.2.2, software emulation (no KVM on the build host), SeaBIOS 1.16.3,
OVMF 2024.02. The core disc is `zxv-x86_64-disc1-core.img` built from
a6ff88fe (sha256 `5cadea4a…db21`, the CI-image build).

| Log | Firmware, machine | Devices | Result |
|---|---|---|---|
| `seabios-i440fx-virtio-scsi-net-serial.log` | SeaBIOS, `pc` (i440fx); the kernel loaded as multiboot1 by QEMU | virtio-scsi (transitional id 1004) with the core disc, virtio-net (1000) | `vsd0` 1067008 sectors, GPT + ESP read, DHCP offer, `[MODEL_OK]` qwen2.5-0.5b sha256 match, `[MODELS_OK] 1 verified` |
| `ovmf-q35-virtio-transitional-serial.log` | OVMF, `q35`; booted from the UEFI disc on virtio-scsi | the UEFI disc and the core disc on one virtio-scsi controller (targets 0 and 1), virtio-net | `vsd0` and `vsd1`, both GPT + ESP, DHCP offer, `[MODELS_OK] 1 verified` |
| `ovmf-q35-virtio-modern-only-serial.log` | as above | the same with `disable-legacy=on` (modern ids 1048 and 1041) | the same |
| `hw-drivers-check.log` | OVMF, `q35` | `build_system/hw_drivers_check.sh`: every class driver at once, now with virtio-blk, virtio-scsi and virtio-net | 5 disks read GPT and `/MEGAROM.TVL`, 4 NICs get DHCP offers, 11 self-tests pass |

The SeaBIOS run loads the kernel with QEMU's own multiboot loader, which
exercises the same 32-bit multiboot1 entry (`kernel/arch/x86_64/boot.asm`)
the disc's legacy-BIOS GRUB stage jumps to. The disc-side BIOS stage is the
Local IPFS build's; its three-way boot matrix is re-run there with these
drivers.

Commands (the disc paths are local):

```
qemu-system-x86_64 -machine pc -m 4G -kernel kernel_x86_64.elf \
  -device virtio-scsi-pci,id=vs \
  -drive if=none,id=d0,format=raw,snapshot=on,file=zxv-x86_64-disc1-core.img \
  -device scsi-hd,drive=d0,bus=vs.0 \
  -netdev user,id=n -device virtio-net-pci,netdev=n -display none -serial stdio

qemu-system-x86_64 -machine q35 -m 4G -bios /usr/share/ovmf/OVMF.fd \
  -device virtio-scsi-pci,id=vs[,disable-legacy=on] \
  -drive if=none,id=d0,format=raw,snapshot=on,file=zxv-x86_64-uefi.img \
  -device scsi-hd,drive=d0,bus=vs.0,bootindex=0 \
  -drive if=none,id=d1,format=raw,snapshot=on,file=zxv-x86_64-disc1-core.img \
  -device scsi-hd,drive=d1,bus=vs.0 \
  -netdev user,id=n -device virtio-net-pci,netdev=n[,disable-legacy=on] \
  -display none -serial stdio
```

Not shown here: a real Contabo VM. QEMU's virtio models follow the
specification closely; a cloud host's may differ in queue sizes and
`max_sectors`, which the drivers read from the device rather than assume.
