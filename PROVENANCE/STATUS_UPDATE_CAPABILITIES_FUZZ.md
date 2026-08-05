# ZXV ARM64 — Capabilities, Real Entropy, Fuzz/CI (Status Update)

**Date:** 2026-08-04
**Path:** `05_KERNEL/`
**Scope:** ARM64. Closes audit P0-1 (raw user pointers), P0-4 (fake
entropy), and the "capability-based syscalls" + "fuzzing/sanitizers for
host parsers" + "fail-closed release gate" items.

## Changes

1. **Capability-based syscalls.** `user_proc_t` now carries a `capabilities`
   bitmask (`CAP_WRITE/PROC/EXEC/IPC/FS`). The SVC dispatcher maps each
   syscall to the capability it requires and denies with `-EPERM` (and a
   kernel log line) if the caller lacks it. Kernel-created processes (the
   P-TERM shell, test procs) get `CAP_TRUSTED_ALL`; **applications loaded
   from disk get `CAP_APP_DEFAULT` = CAP_WRITE|CAP_PROC (least privilege)**.
   Demonstrated live: `hello.elf` attempts `SYS_EXEC` and is denied —
   `[EL0][EPERM] pid=3 denied syscall 11`; the app confirms
   "privileged SYS_EXEC correctly DENIED (least privilege ok)".

2. **P0-1: no more raw user pointers.** `SYS_EXEC`, `SYS_SEND`, and
   `SYS_RECV` now validate the user buffer through `proc_user_range_ok`
   (a page-table walk confirming every page is mapped and EL0-accessible)
   before EL1 reads/writes it, and bound the length. A hostile EL0 pointer
   into kernel space now returns `-EFAULT` instead of faulting/leaking.

3. **P0-4: real entropy with honest fallback.** New
   `kernel/arch/arm64/entropy.c` uses the architected hardware RNG
   (FEAT_RNG / `RNDR`) with a health test + retries when the CPU
   implements it. The vault-key derivation uses it and prints
   `[ENTROPY] Vault key from FEAT_RNG (RNDR) ... + health test`. When
   FEAT_RNG is absent (QEMU cortex-a53), it **no longer claims hardware
   entropy** — it prints `[ENTROPY][WARN] ... NON-cryptographic dev
   material (predictable). Not for production secrets.` Verified both
   paths: `-cpu cortex-a53` → WARN; `-cpu max` → RNDR.

4. **Adversarial parser fuzzers (ASan+UBSan).**
   `kernel/tests/fuzz/fuzz_elf.c` and `fuzz_zxvfs.c`, wired as
   `make fuzz` in `kernel/Makefile`. `fuzz_elf` throws 400k+ random and
   mutated/truncated inputs at the ELF loader; `fuzz_zxvfs` mounts tens
   of thousands of random disk images. Both run clean under
   AddressSanitizer + UndefinedBehaviorSanitizer — no crash, OOB read, or
   UB, and the loader always returns a valid result enum.

5. **Fail-closed CI gate.** `build_system/ci_gate.sh` runs, stopping on
   the first failure: clean ARM64 build → `make verify-all` (must report
   ALL STAGE-1 CHECKS PASSED) → both fuzzers → an N-boot QEMU harness
   (`ci_boot_harness.sh`) that requires every boot to reach `[BOOT_OK]`
   with no fault markers. Parameterized to 100 boots for the release gate.

6. **Cleanup.** The verbose per-`proc_create` bring-up dump is now behind
   `-DEL0_PROC_DEBUG=1` (default off) so the boot log stays clean.

## Verification

- `make verify-all` still green (82 checks); `make fuzz` PASS; boot harness
  green. Config matrix (default / EL0=0 / EVENT_LOOP=0 / WX_TEST=1) builds.
- Capability denial, P0-1 rejection, and both entropy paths reproduced in
  QEMU this session.

## Remaining (honest)

- Capabilities are a coarse bitmask, not fine-grained object handles; good
  enough for the MVP least-privilege gate, not a full capability system.
- Entropy: RNDR + health test seeds the key directly; a reviewed DRBG and
  full key lifecycle/rotation remain future work (audit SEC_002).
- Fuzzers are custom (seeded PRNG), not coverage-guided libFuzzer; fine for
  the gate, extendable later.
