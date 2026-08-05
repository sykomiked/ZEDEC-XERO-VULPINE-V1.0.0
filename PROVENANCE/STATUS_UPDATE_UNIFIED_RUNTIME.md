# ZXV ARM64 — Unified Runtime + Interactive P-TERM (Status Update)

**Date:** 2026-08-04
**Path:** `05_KERNEL/`
**Scope:** ARM64 (QEMU virt, cortex-a53, GICv3) — the canonical local build target.

## Summary

The ARM64 kernel now runs EL0 user space **and** the full multi-subsystem
event fabric **at the same time**, with a working interactive shell. Prior
to this session the two were mutually exclusive: the default build entered
EL0 and left all ~17 event-space/finance/mesh subsystems dormant, while the
event loop only ran with EL0 disabled (and that configuration did not even
compile).

## Changes

1. **Fixed a build break in the non-EL0 configuration.**
   `kernel_main_arm64.c` referenced `pid_b` unconditionally but declared it
   only in the `ENABLE_EL0_USERSPACE=1` branch, so every `ENABLE_EL0_USERSPACE=0`
   build failed to compile. Declared `pid_b = -1` in the `#else` branch.

2. **Unified runtime (external-clock bridge, extended to EL0).**
   Extracted the monolithic event-loop body into file-scope
   `kernel_event_cycle_run()` / `kernel_stats_report()`. Added
   `el0_set_event_cycle_hook()` in `arm64_exceptions.c`; the generic-timer
   IRQ (`el0_irq_handler_c`, and `irq_handler_c` for EL1-idle windows) now
   drives exactly one kernel event cycle per tick while user processes run.
   This is the same ISR→event-cycle boundary described in
   `kernel/ARCHITECTURE_EXTERNAL_CLOCK_BRIDGE.md`, now applied to the EL0
   path. The classic while(1) loop is unchanged for `ENABLE_EL0_USERSPACE=0`.
   `Makefile.arm64` now defaults `ENABLE_EVENT_LOOP=1`.

3. **Interactive P-TERM shell at EL0.**
   - `SYS_READ` is now non-blocking (`uart_rx_ready`), so a poll never
     freezes the timer tick / event cycle at EL1 (IRQs are masked during SVC).
   - New `SYS_EXEC` syscall copies the command line from user memory into a
     kernel buffer and dispatches it to `kernel_shell_exec()`, which runs
     kernel-native commands (`stats`, `uptime`, `clear`) against live
     subsystem state and delegates the rest to the P-TERM engine, flushing
     console rows to the serial UART.
   - `pterm_user_entry` rewritten as a real self-contained EL0 shell loop
     (stack line buffer, echo, backspace, `exit`); all helpers are
     `always_inline` since `proc_create` copies only the entry function's
     first 4KB into the user page.
   - Implemented `sched_list_tasks()` (was a stub) so `ps` lists real tasks.
   - Removed the `[SYS_YIELD]` debug spam.

4. **W^X privilege hardening + negative tests.**
   User code pages are now mapped **read-only + executable** at EL0 (were
   RWX); the loader writes code through the kernel identity map, so the user
   mapping never needs write. Stack stays RW + UXN (non-executable). Added
   `ENABLE_WX_TEST=1` adversarial processes that (a) write to their own code
   page and (b) execute from the stack; both are fault-terminated by the
   kernel while the shell keeps running.

5. **SYS_EXIT idle correctness.** When a process exits and no sibling is
   READY (only SLEEPING), the kernel now unmasks IRQs and WFI-idles at EL1
   so `proc_wake_eligible` can still fire, instead of halting forever.

## Verification (this session, reproducible)

- `make -f build_system/Makefile.arm64 all` — clean (exit 0). Configs built
  and booted: default (EL0+event-loop), `ENABLE_EL0_USERSPACE=0`,
  `ENABLE_EVENT_LOOP=0`, `ENABLE_WX_TEST=1` — all reach `[E0096] [BOOT_OK]`.
- Interactive shell over serial: `help`, `ps`, `vmstat`, `date`, `whoami`,
  `uptime`, `stats`, `exit` all produce live output.
  → `EVIDENCE/arm64_unified_shell_session.log`
- W^X negative test: 2/2 adversarial processes fault-terminated
  (Data Abort on code write, Instruction Abort on stack exec); no violation
  marker printed; shell survived. → `EVIDENCE/arm64_wx_negative_test.log`
- Host suites: `make test` 10/10; `make verify-all` 40/40, ALL STAGE-1
  CHECKS PASSED.

## What still remains (maps to the second-opinion Phase-1 gate)

- ELF/application loader from persistent storage (today: entry copied from a
  kernel function). virtio-blk persistence + crash journal; virtio-net.
- x86-64 boot parity for the same unified runtime.
- Signed manifest + A/B update/rollback; signed blank-disk installer.
- Capability-based syscalls (current syscall set is fixed-function).
- 100/100 boot harness + 1000-iteration fault injection as CI gates.
