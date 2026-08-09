# ZXV / ZEDEC pqOS — Remaining Work Map (to a clean, shippable product)

_Assessment date: 2026-08-08. Canonical tree: `05_KERNEL/`. Mirror: `../ZEDEC_pqOS_CLEAN/`._

## 0. Where we are (verified today)

- **arm64 = the flagship, and it is healthy.** Builds clean with `aarch64-linux-gnu-gcc`,
  boots to `[BOOT_OK]` → EL0 + interactive P-TERM + graphical desktop (ramfb). Every
  subsystem self-checks at boot, including this session's new graphics/codec modules.
- **Host CI is green**: `make verify-all` → ALL STAGE-1 CHECKS PASSED; ELF/ZXVFS fuzzers
  under ASan/UBSan; `ci_gate.sh` fail-closed.
- **This Mac has only the aarch64 toolchain.** x86_64 / riscv / riscv32 / arm32 cannot be
  linked or booted here — that is the entire reason for the Linux server session.

Bottom line: the **64-bit ARM product is real and demonstrable**. What remains is (a) the
other CPU architectures, (b) the installer/boot/signing chain, (c) a set of security
hardening P0s for a *defensible* MVP, and (d) one business/legal item.

---

## 1. Cross-compile the other architectures — THE LINUX SERVER JOB

**ALL 5 TARGETS LINK/BUILD on the server (2026-08-08, real toolchains).** Full-parity link achieved across every architecture; per-arch *boot* bring-up is the next layer.

| Arch | Server status | Boot remaining |
|------|-------|---------------|
| **arm64** | ✅ **BUILDS + BOOTS (`BOOT_OK`)** + EL0 | — (flagship, done) |
| **x86_64** | ✅ **BOOTS + RING-3 + FULL SUBSYSTEM SET (196 files) initialized** (M5 core, [FEAT] platform 8/8, economy 5/5) | — (done) |
| **riscv64** | ✅ **BOOTS (OpenSBI/S-mode)** — all 17 phases + live 100 Hz event loop | — (done) |
| **riscv32** | ✅ **BOOTS (OpenSBI-rv32/S-mode)** — all 17 phases + live 100 Hz event loop | — (done) |
| **arm32** | ✅ **BOOTS (`-M virt` / `BOOT_OK`)** — full subsystem set + live event loop | — (done) |

**ALL 5 of 5 arches now fully boot: arm64, x86_64 (BOOT_OK + user/kernel split + platform 8/8 +
economy 5/5), riscv64 + riscv32 (OpenSBI/S-mode: all 17 phases + a live 100 Hz timer-ticking
event loop), and arm32 (`-M virt`: full subsystem set + live event loop).**
x86_64 ring-3 done: GDT/TSS/IDT + int 0x80 syscall + U/S user pages + IRETQ; a ring-3
program runs, syscalls, and returns. (Also fixed: SSE was never enabled → x86_64 had
been triple-faulting at gcc's first `movdqa`, so it never reached BOOT_OK before.)

> **Progress 2026-08-09 — the last two arches boot.** Three distinct 32-bit bugs fixed:
> 1. **arm32** was linked for the versatilepb base with a versatilepb/MPCore UART, and its
>    boot never enabled the VFP unit. Retargeted to `qemu-system-arm -M virt` (RAM 0x40000000,
>    PL011 @ 0x09000000, GICv2), enabled VFP/NEON in `boot.s` (CPACR CP10/11 + FPEXC.EN — the
>    same class of bug as the x86_64 "SSE never enabled" triple-fault), and re-paced the event
>    loop off the CP15 generic-timer virtual counter (no GIC needed for the bring-up profile).
> 2. **riscv32** had no OpenSBI-rv32 firmware (QEMU 6.2 bundles only rv64) and `boot_rv32.s`
>    was still M-mode. Built OpenSBI rv32 (generic, XLEN=32) from source; converted the rv32
>    boot to S-mode (a0 hartid / stvec / scause / sret) and made the SBI timer + trap cause
>    XLEN-aware (rv32 needs rdtimeh and the a0/a1 64-bit split; the interrupt flag is scause
>    bit 31, not 63).
> 3. The real rv32 crash was `kernel/compat/softfloat_stubs.c`: the hand-rolled libgcc
>    replacement (used on rv32, where int64↔double has no native `fcvt.d.l`) defined
>    conversions as `return (double)i;`, which lowers to a call to the very routine being
>    defined → infinite self-recursion that marched the stack into OpenSBI's PMP-protected
>    region and fault-looped. Rewrote every 64-bit int↔float conversion by 32-bit-half
>    decomposition (native `fcvt.d.wu`/`fcvt.wu.d` only) and replaced the O(quotient)
>    repeated-subtraction 64-bit division with O(64) binary long division.

> **Progress 2026-08-08 (commit d179f5b):** arm32/riscv32 brought to the full portable
> subsystem set (142 of 144 missing modules were arch-neutral). One real 32-bit bug fixed
> (`broker.c` `__uint128_t` → exact uint64). Preflight CLEAN across all 5 targets. What
> remains for 32-bit is the **arch layer + link + boot on the server** (~33 arch-layer link
> symbols: el0/virtio/ramfb/framebuffer).

## 2. The 32-bit decision (needs your call)

arm32 and riscv32 **compile but are partial bring-ups** — the full subsystem set is not wired
into the 32-bit source lists (that is why they "didn't work" before). Three honest options:

- **(a) Full 32-bit parity** — bring every subsystem up on 32-bit. Large lift, limited
  near-term payoff (modern targets are 64-bit).
- **(b) Reduced bring-up profile (recommended)** — 32-bit ships as a documented core-kernel
  profile that boots, not the full stack. First-class product = **arm64 (full) + x86_64
  (full, after ring-3)**; arm32/riscv/riscv32 = labeled experimental/bring-up.
- **(c) Defer 32-bit** — drop from the first clean product, add later.

## 3. Installer / distribution / signing (P0)

- **EFI payloads per-arch — DONE for both UEFI-capable arches (2026-08-09).**
  - `BOOTX64.EFI` (**new**): PE/COFF Machine=0x8664, ms_abi stub, plus a long-mode →
    32-bit-protected-mode trampoline (UEFI hands off in long mode; the kernel is Multiboot1
    entered at `_entry32`). Reserves 0x100000, ExitBootServices, copies the embedded kernel,
    jumps. **Verified on OVMF → `[BOOT_OK]` with ring-3 live.**
  - `BOOTAA64.EFI` (**fixed**): the stub copied the kernel to 0x40080000 — exactly where
    AAVMF had loaded the EFI image — so it overwrote its own running code (Undefined
    Instruction at 0x40080000). Now stages the kernel into firmware-allocated memory and
    runs the final copy+jump from a **relocated trampoline** (`efi_tramp.S`).
    **Verified on AAVMF → EL0 + P-TERM + "UEFI GOP framebuffer — ZEDEC desktop is on screen".**
    (This also closes §6's "arm64 EFI stub + GOP → boot-to-desktop under UEFI".)
  - `BOOTRISCV64.EFI`: **firmware-blocked, not code-blocked.** The box has no RISC-V UEFI
    firmware (only OpenSBI) and no `grub-riscv64-efi` target, so there is nothing to verify a
    payload against. riscv64/riscv32 boot natively via OpenSBI today.
- **Universal disc — DONE and demonstrated.** `mkuniversal_disc.sh` now prefers a native
  self-contained `BOOT<arch>.EFI` over building a GRUB loader. **ONE `dist/zxv-universal.img`
  boots to `BOOT_OK` on BOTH x86_64 (OVMF) and arm64 (AAVMF)** — the multi-arch claim
  demonstrated, not asserted. All 5 kernels are carried under `/ZXV/<arch>/`; riscv64/riscv32/
  arm32 are reported CARRIED (not bootable) rather than claimed.
- **VM images** (`mkuniversal_vm.sh`: zxv.raw / ova / qcow2 / vmdk / vhdx) — the disc is also a
  valid raw disk, so this is now unblocked.
- **Signed release with the REAL root key** — generate the root key **offline on the server**;
  the dev key in the tree is compromised and must never ship (`sign_release.sh`).

## 4. Security hardening for a defensible MVP (Proposal-9 P0s)

- **P0-2** installer trusts an unsigned manifest; no path-traversal/symlink confinement.
- **P0-3** durability: A/B protects only the app, not kernel/bootloader; fsync ordering.
- **P0-4** EL0 user-copy: add `copy_to_user`, SYS_SEND/OPEN/CLOSE handlers, fault-safe copy.
- **P0-6** signed-exec ZSP v2: cover identity/version/arch/ABI/key-id/caps + monotonic
  anti-rollback floor (today's preimage is only magic+len+hash).
- **P0-7** cells: real protection domains + attestation (today: logical within one kernel,
  digest-equality auth).
- **Warnings**: ~104 compiler warnings → zero for a `-Werror`-quality product.

Most of these are **arch-neutral or arm64-verifiable → doable HERE before the server.**

## 5. Business / legal (not code)

- **P0-8** one lawyer-reviewed term sheet — the partner PDFs conflict on advance (₹2L vs
  ₹20L/wk), royalty %, worldwide/exclusive scope, and patent status. Resolve before circulation.

## 6. In-flight project tasks (non-blocking for the product)

- Game Master full-corpus fault-stress campaign (140k ROMs).
- MegaROM mechanic corpus (game_universe → Chiglet dedup).
- Universal chipset coverage + ecosystem file extensions.
- arm64 EFI stub + GOP → boot-to-desktop from the ISO under UEFI.

---

## Recommended sequence

**Phase A — here on the Mac (host / arm64-verifiable, no cross-toolchain):**
1. Fix preflight arm64 false-FAIL (recognize the `.S` EFI files) → server gate starts green.
2. Security P0s that are arch-neutral: P0-4 user-copy, P0-6 ZSP v2, P0-2 installer, P0-3 durability.
3. Zero out the ~104 compiler warnings.
4. Refresh `handover.json` / `SERVER_HANDOVER.md` to current state.

**Phase B — one clean Linux server session (cross-compile):**
5. Provision toolchains → preflight → `build_all_targets` (arm64 + x86_64 first-class).
6. x86_64 ring-3 parity → BOOT_OK.
7. EFI payloads → universal disc → VM images.
8. Offline root key → `sign_release.sh` → `ci_gate.sh 100` across arches.

**Phase C — business:** resolve the term sheet (parallel, non-blocking).

_Note: preflight's arm64 line reads FAIL only because it treats the EFI `.S` files as missing
`.c`; the real gcc build boots. Fixing that parser is item A.1._
