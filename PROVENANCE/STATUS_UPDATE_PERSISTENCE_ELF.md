# ZXV ARM64 — Persistence + ELF Loader (Status Update)

**Date:** 2026-08-04
**Path:** `05_KERNEL/`
**Scope:** ARM64 (QEMU virt). Closes audit items STORE_001, the ELF-loader
half of the Technical-MVP gate, and hardens P0-1/P0-2.

## Summary

ZXV now has **real persistent storage** and a **real ELF application loader**.
An application authored as C, compiled to a static AArch64 ELF64, and stored on
disk is loaded into an isolated EL0 address space and executed — it is no longer
copied from a kernel function. Files written from the shell survive reboots, and
every filesystem write is crash-consistent via a redo journal.

## What was added

1. **virtio-blk driver** (`kernel/arch/arm64/virtio_blk.c`) — modern
   (VIRTIO 1.0, version 2) virtio-mmio block device over the QEMU virt
   transport. Single polled virtqueue, cache-maintained DMA (dc cvac / dc
   ivac + dsb around every device handoff), bounded-spin completion. The
   virtio-mmio window (0x0a000000) is added to the board profile and mapped
   Device-memory in the MMU.

2. **ZXVFS journaled filesystem** (`kernel/src/zxvfs/zxvfs.c`) — a small
   crash-consistent FS: superblock + inode table + data region, with a redo
   (write-ahead) journal. A transaction stages all changed sectors, writes a
   single-sector commit header (atomic), checkpoints to final locations, then
   clears the journal. A crash at any point leaves the FS fully before or
   fully after the write; `zxvfs_mount` replays a committed journal. Static
   asserts pin the on-disk struct sizes.

3. **ELF64 loader** (`kernel/src/loader/elf.c`) — validates a static AArch64
   ELF64 (magic/class/endian/type/machine, program-header table bounds,
   per-segment file-range and address-window bounds, integer-overflow-safe),
   **rejects any W+X segment (W^X)**, and drives PT_LOAD segments through a
   mapper callback. Transport-agnostic and host-unit-tested, so the x86-64
   port reuses it verbatim.

4. **proc_create_from_elf** + **copy_from_user** (`el0_userspace.c`) — creates
   an EL0 process from an ELF image: per-segment page allocation, W^X-correct
   mapping (code RO+X, data RW+NX), BSS zeroing, i-cache maintenance.
   `proc_user_range_ok` / `copy_from_user` walk the process page table to
   confirm a user pointer is mapped and EL0-accessible before EL1 touches it;
   `SYS_EXEC` now uses this instead of dereferencing a raw user pointer
   (audit **P0-1**).

5. **Shell + sample app** — new P-TERM commands `ls-p`, `write`, `cat`, `rm`,
   `sync`, `run`. `kernel/userapp/hello.c` builds to `hello.elf` (static, linked
   at 0x10000); the kernel embeds it (`hello_elf.h`) and seeds it to a fresh
   ZXVFS so `run hello.elf` loads a real ELF off disk. Rebuild with
   `kernel/userapp/build_hello.sh`.

## Verification (reproducible)

- Host suites: `make verify-all` = **82/82** ("ALL STAGE-1 CHECKS PASSED"),
  now including `test_zxvfs` (persistence + **crash-recovery**: commit-then-
  power-loss is replayed to the committed value) and `test_elf` (15 cases incl.
  bad magic/class/machine/type, **W^X**, out-of-window vaddr, past-EOF and
  overflow offsets, entry-not-in-segment). `make test` = 10/10.
- On target (QEMU virt + `-drive`):
  - Blank disk → format → write file → **reboot** → file read back identical.
  - `run hello.elf`: seeded to disk, then loaded from disk and executed at EL0;
    prints its banner + pid and exits; **shell survives**. On reboot the app is
    NOT re-seeded yet still runs (loaded from persisted disk).
  - No `-drive` → boots to BOOT_OK and cleanly SKIPs persistence.
  → `EVIDENCE/arm64_persistence_elf_session.log`,
    `EVIDENCE/host_test_zxvfs.log`, `EVIDENCE/host_test_elf.log`.
- Config matrix (default / EL0=0 / EVENT_LOOP=0 / WX_TEST=1) all build; W^X
  adversarial build still fault-terminates 2/2 while the shell survives.

## Honest scope / what remains (audit alignment)

- **Persistence** — MVP FS: single flat directory, fixed 7.5 KB/file extent,
  whole-file read/write. No encryption-at-rest, fsck, or quotas yet.
- **ELF loader** — static ET_EXEC only, segments confined to the first 2 MB
  EL0 block; no dynamic linking/PIE/ASLR. Sufficient for the MVP app slice.
- **P0-1** — `SYS_EXEC` is now checked+copied; `SYS_SEND`/`SYS_RECV` still need
  the same treatment (tracked in the capability-syscalls slice).
- **P0-4** (predictable vault entropy) and **P0-3** (unsigned installer) are the
  next slices (capabilities+RNDR, then signed A/B installer).
- Still QEMU-only; no physical-hardware storage path yet.
