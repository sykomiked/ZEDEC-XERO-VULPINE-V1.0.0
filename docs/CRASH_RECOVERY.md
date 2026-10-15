<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# Crash log, safe mode and recovery boot

Before this change, a kernel fault printed its registers to the serial console
and halted. On a machine nobody was watching, the cause was lost. A kernel
that crashed during boot also crashed on every boot after it.

Now an ARM64 kernel with a persistent disk:

1. Writes a sealed crash record to disk when it faults. The record holds the
   fault registers, a backtrace, and the last 2 KiB of console output.
2. Reports that record on the next boot, then clears it.
3. Steps down to **safe mode** after a crash, or after two boots in a row
   that never reached the shell. After four such boots it steps down to
   **recovery mode**.

## Boot modes

| Mode | When | What runs |
|---|---|---|
| normal | by default | everything |
| safe | the previous boot crashed, two unconfirmed boots in a row, or asked for | storage and the shell. The platform layer, the economy, the AI self-check and user programs (`run`) stay off |
| recovery | four unconfirmed boots in a row, or asked for | safe mode, plus a banner pointing at the repair commands |

A boot counts as confirmed once it reaches `[BOOT_OK]`. A safe or recovery
boot that gets there returns the next boot to normal.

Shell commands:

| Command | Does |
|---|---|
| `crash` | shows the last crash: its reason, registers, call chain and the console output before it |
| `bootmode` | shows this boot's mode, the next boot's mode, and the boot and crash counts |
| `bootmode safe` / `bootmode recovery` | asks for that mode on the next boot only |
| `bootmode normal` | clears any request and returns the next boot to normal |
| `rollback` | reverts a signed A/B update (already existed) |
| `crashtest` | faults on purpose, to check the crash log end to end |

## Where it is kept

The reserved area is 16 sectors placed right after the ZXVFS region of the
disk (`ZXVFS_TOTAL_SECTORS`), so it never overlaps a file.

- Sector 0 holds the boot-control state.
- Sectors 1-8 hold the crash record.

Both are sealed with SHA-256. A blank or damaged area reads as "no crash" and
a fresh device. The crash path writes raw sectors through the polled
virtio-blk driver and never goes through the filesystem. It writes the
record's header sector last, so a write cut short by power loss leaves either
no record or one that fails its seal. It never leaves a wrong record.

## What it is not yet

- **ARM64 only.** The core (`kernel/src/crash`) is portable and builds
  freestanding for every target. x86-64, RISC-V and ARM32 still only print
  and halt; their fault handlers need the same few lines.
- **No disk, no record.** Without a persistent disk (CI boots run without
  one), the fault is printed as before and nothing is kept.
- **Not a full memory dump.** The record is the registers, the call chain and
  the console tail, which is what fits in 4 KiB and can be written safely from
  a broken kernel.
- **No boot-menu entry.** Safe and recovery mode are chosen by the counters
  or by `bootmode`. A key held at boot (`owner_safe` in `bootctl_begin`) is
  not wired yet. It needs early keyboard input.

## Tests

`make -C kernel test-crash` is part of `verify-all`. It runs:

- `test_crash` on a memory-backed disk:
  - every field survives the round trip;
  - a flipped byte anywhere is caught;
  - a save cut short after any number of sectors leaves no record;
  - a torn overwrite fails its seal;
  - nothing outside the area is touched;
  - the backtrace stops on a looping, out-of-bounds or misaligned frame;
  - the boot-mode counters behave as above;
  - the crash path does not re-enter itself.
- The same test under ASan and UBSan.
- A freestanding build for aarch64, i386, armv7a and rv32, which checks that
  nothing pulls in libc or a compiler helper.

It was also checked end to end in QEMU (`virt`, Cortex-A53) with an 8 MiB
disk, as five boots in a row:

1. `crashtest` faulted the kernel, and the record was saved.
2. The next boot reported it, with the call chain `kernel_shell_exec` ←
   `proc_handle_svc` ← `el0_sync_handler_c`, and came up in safe mode. `run`
   was refused.
3. The boot after that was normal again. `bootmode recovery` was set during
   it.
4. That boot came up in recovery mode.
5. The boot after recovery was normal.
