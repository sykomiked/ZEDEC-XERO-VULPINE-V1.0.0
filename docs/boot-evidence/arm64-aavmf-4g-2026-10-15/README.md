<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
     SPDX-License-Identifier: Apache-2.0 -->
# arm64 under AAVMF with 4 GB: the framebuffer above 4 GB, 2026-10-15

The same arm64 UEFI disc (`baremetal_images.sh arm64`) booted twice, the way
the test server boots a VM: QEMU 8.2.2 (software emulation) `-M
virt,gic-version=3 -cpu max -m 4096`, AAVMF firmware from pflash, the disc on
`virtio-blk-pci`, `-device ramfb`, 240 s.

| File | Built from | What happens |
|---|---|---|
| [arm64-before-fix-serial.log](arm64-before-fix-serial.log) | a6ff88fe | AAVMF puts the GOP framebuffer at `0x13C7A0000`, past 4 GB. The first frame written there is a kernel Data Abort (ESR `0x96000041`, FAR `0x13C7A0000`) and the kernel halts. |
| [arm64-after-fix-serial.log](arm64-after-fix-serial.log) | the commit that adds this folder | `[FB_MAP] firmware framebuffer at 000000013C7A0000, 1920000 bytes: mapped Normal Non-cacheable, never executable`, the desktop on the GOP framebuffer, `[BOOT_OK] ... EL0 ready`, the event cycle and the EL0 terminal. No abort. |

The server saw the same fault at FAR `0x138430000` with `-m 4096` (ESR
`0x96000045`).

There were two causes, and both are fixed:

- **The physical address range.** TCR_EL1.IPS was 32 bits, so nothing above
  4 GB could be reached. It now comes from the CPU's own PA range
  (ID_AA64MMFR0_EL1.PARange, at most 48 bits).
- **The page tables.** The framebuffer is now mapped wherever the firmware
  puts it (`kernel/arch/arm64/mmu_window.c`), before any user page table
  exists. Each user table copies every kernel L1 slot, not only 1 to 4 GB,
  so a kernel write to the framebuffer while a user process's tables are
  live still finds it mapped.

The `[FAULT CONTAINED] arm64-io` line is the event fabric's own fail-stop
self-test, not a CPU fault.
