# Good First Issues

These issues are suitable for new contributors. They are well-scoped, have clear acceptance criteria, and include mentorship.

## Labels
- `good first issue` - Suitable for first-time contributors
- `help wanted` - Needs external contribution
- `arch:x86_64` - x86_64-specific
- `arch:aarch64` - ARM64-specific
- `arch:riscv64` - RISC-V 64-bit-specific
- `arch:riscv32` - RISC-V 32-bit-specific
- `arch:arm32` - ARM32-specific
- `component:boot` - Boot subsystem
- `component:crypto` - Cryptographic primitives
- `component:drivers` - Device drivers
- `component:fs` - Filesystem
- `component:net` - Networking
- `component:ui` - User interface

## Current Issues

### Boot & Initialization

- **[arch:aarch64] Add ARM64 SMC (Secure Monitor Call) handler**
  - Implement SMC conduit for EL3 communication
  - Add SMC ID decoding for standard calls
  - Add unit tests in `kernel/arch/arm64/test_smc.c`
  - Acceptance: SMC calls route correctly, no crashes on PSCI calls

- **[arch:riscv64] Implement RISC-V SBI (Supervisor Binary Interface) v2.0**
  - Add SBI extension detection and dispatch
  - Implement time, console, and IPI extensions
  - Add SBI call wrapper in `kernel/arch/riscv/sbi.c`
  - Acceptance: RISC-V boots via OpenSBI, timer works

- **[component:boot] Add multiboot2 protocol support for x86_64**
  - Parse multiboot2 info structure
  - Extract memory map and ACPI tables
  - Add to `kernel/arch/x86_64/multiboot2.c`
  - Acceptance: x86_64 boots via GRUB multiboot2

### Cryptographic Primitives

- **[component:crypto] Add ML-KEM-768 deterministic KAT vectors**
  - Integrate NIST ACVP published test vectors
  - Add vector parsing in `kernel/src/mlkem/test_mlkem_kat.c`
  - Acceptance: All 30 NIST vectors pass

- **[component:crypto] Implement Ed25519 batch verification**
  - Add multi-signature verification optimization
  - Add to `kernel/src/robin_debanks/ed25519_batch.c`
  - Acceptance: Batch verify 100 signatures in <10ms

### Device Drivers

- **[component:drivers] Add virtio-blk block device driver**
  - Implement virtio block device protocol
  - Add to `kernel/arch/arm64/virtio_blk.c`
  - Acceptance: Read/write sectors from QEMU virtio-blk

- **[component:drivers] Add PS/2 keyboard driver for x86_64**
  - Implement PS/2 keyboard controller
  - Add to `kernel/src/keyboard/ps2_keyboard.c`
  - Acceptance: Keyboard input works in x86_64 QEMU

- **[component:drivers] Add USB HID driver**
  - Implement USB host controller (OHCI/EHCI)
  - Add HID device class driver
  - Acceptance: USB keyboard/mouse recognized

### Filesystem

- **[component:fs] Add directory operations to ZXVFS**
  - Implement mkdir, rmdir, readdir
  - Add to `kernel/src/zxvfs/zxvfs_dir.c`
  - Acceptance: Can create/list directories

- **[component:fs] Add write support for FAT32**
  - Implement FAT32 write operations
  - Add to `kernel/src/fat32/fat32_write.c`
  - Acceptance: Can write files to FAT32 partition

### Networking

- **[component:net] Add TCP retransmission timeout**
  - Implement RTO calculation (RFC 6298)
  - Add to `kernel/src/net/tcp_rto.c`
  - Acceptance: TCP handles packet loss correctly

- **[component:net] Add DHCP client**
  - Implement DHCPv4 client (RFC 2131)
  - Add to `kernel/src/net/dhcp.c`
  - Acceptance: Obtains IP address from DHCP server

### User Interface

- **[component:ui] Add text rendering to desktop shell**
  - Implement bitmap font rendering
  - Add to `kernel/src/desktop/font_render.c`
  - Acceptance: Can render text on framebuffer

- **[component:ui] Add window manager**
  - Implement basic window management (move, resize, close)
  - Add to `kernel/src/desktop/wm.c`
  - Acceptance: Can open/close multiple windows

## How to Claim an Issue

1. Comment on the issue you want to work on
2. Wait for assignment (to avoid duplicate work)
3. Create a branch: `git checkout -b issue/<number>-<description>`
4. Implement the fix
5. Submit a pull request referencing the issue

## Need Help?

- Mention `@mlcurzi` in issue comments for questions
- Join the development chat (link in README)
- See `CONTRIBUTING.md` for general contribution guidelines
