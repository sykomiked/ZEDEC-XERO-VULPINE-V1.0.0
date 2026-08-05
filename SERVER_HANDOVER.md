<!--
Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
-->

# ZEDEC pqOS — Server Cross-Compile Handover Schema

**For: the AI operator who will run the cross-compile on a Linux server.**
**From: the development session on Apple Silicon (arm64 macOS).**
**Repo root for every command below: `05_KERNEL/` (the git root).**

This document is the contract. It says exactly what is already proven, what you
must do, the order to do it in, and how to know you succeeded. It is written to
the project's governing rule: **no hollow capabilities** — never report a target
as working unless an artifact was produced and a gate passed. A missing
toolchain is a `SKIP`, a compile error is a `FAIL`, only a produced-and-verified
artifact is `OK`.

A machine-readable version of this schema is at
[`build_system/handover.json`](build_system/handover.json) — parse that if you
want to drive the build programmatically.

---

## 0. TL;DR

```bash
# from 05_KERNEL/, on a fresh Ubuntu/Debian box:
bash build_system/provision_server.sh              # 1. toolchains
bash build_system/preflight_all_targets.sh         # 2. compile smoke-test every arch (clang)
bash build_system/build_all_targets.sh             # 3. LINK every arch that has a toolchain
bash build_system/ci_gate.sh 100                   # 4. fail-closed gate (build+verify+fuzz+100 boots)
bash build_system/mkuniversal_disc.sh              # 5. one disc, all chips (UEFI multi-arch)
bash build_system/mkuniversal_vm.sh                # 6. one VM file, all hypervisors
# 7. sign the native-format release (needs the root key — see §6)
ROOT_KEY=/secure/root_priv.pem bash build_system/sign_release.sh
```

If every step above ends green, you are done. The rest of this document is what
to do when they don't, and the two subsystems that need real work (§3).

---

## 1. Ground truth — what is already verified, and where

The **arm64 target is the reference build**. It compiles and boots to
`[E0106] [BOOT_OK]` (EL0 + P-TERM shell) on the dev machine under
`qemu-system-aarch64`. Everything below is measured, not asserted.

| Layer | Status on handover | How it was verified |
|---|---|---|
| M5 core (phase coord, rmag, lpres, iphase, choice, oseq) | ✅ boots | arm64 QEMU → BOOT_OK |
| Network (DHCP/DNS/TCP), TLS 1.3 (HKDF/X25519/ChaCha20-Poly1305) | ✅ host-tested vs RFC vectors | `make verify-all` |
| Tri-Space pkg (.zxvc/.cedez/.cedec) + ZSP Ed25519 verify | ✅ host-tested vs openssl sigs | `make verify-all` |
| **Platform layer** (deploy, theme, icon, font+TrueType, bridge, update, mage, reality) | ✅ **boots on arm64**, host-tested | `[FEAT] … 8/8 self-checked` in boot log + `make verify-all` |
| Font system (script itemization + TrueType rasteriser) | ✅ host-tested, known-answer | `test_truetype`, `test_font` |
| Web2/3/4 bridge (one resolver) | ✅ host-tested | `test_bridge` |
| Host integrity gate | ✅ **ALL STAGE-1 CHECKS PASSED** (229 sections) | `make verify-all` from `kernel/` |
| 11 platform modules cross-compile | ✅ x86_64-elf, aarch64-none-elf, armv7-none-eabi (clang) | per-file freestanding compile |

**Definition of "host-tested":** a native `gcc`/`clang` test binary asserts
computed values against an external anchor (an RFC vector, an openssl-produced
signature, a known-answer geometry), run under ASan+UBSan. `make verify-all`
(run from `05_KERNEL/kernel/`) is the aggregate gate and it is green.

---

## 2. The target matrix

`build_all_targets.sh` builds these (`TARGETS="arm64 x86_64 riscv riscv32 arm32"`):

| Target | Makefile | Toolchain (apt) | Local status | Server action |
|---|---|---|---|---|
| **arm64** | `build_system/Makefile.arm64` | `gcc-aarch64-linux-gnu` | ✅ builds + boots | rebuild & confirm parity |
| **x86_64** | `build_system/Makefile.x86_64` | `gcc-x86-64-linux-gnu` | ⚠️ compiles, **minimal kernel_main** | **§3 — real work** |
| **riscv64** | `build_system/Makefile.riscv` | `gcc-riscv64-linux-gnu` | ⚠️ not linkable locally (host clang lacks RISCV) | build & boot-test |
| **riscv32** | `build_system/Makefile.riscv32` | `gcc-riscv64-linux-gnu` (multilib) | ⚠️ same | build & boot-test |
| **arm32** | `build_system/Makefile.arm32` | `gcc-arm-linux-gnueabihf` | ⚠️ compiles | build & boot-test |

The dev machine is Apple Silicon with **only** an aarch64 GNU cross-toolchain
and a clang that lacks the RISCV backend. That is the sole reason the other four
are unproven here — **not** a code defect. `preflight_all_targets.sh` (clang,
compile-only) is the smoke test that catches portability defects before you
spend server time on linking.

---

## 3. What needs REAL work on the server (not just a rebuild)

Two items cannot be finished on Apple Silicon and were deliberately left for you.
Do **not** treat these as done.

### 3a. x86_64 ring-3 parity — the one true gap
`kernel/arch/x86_64/` has a **minimal** `kernel_main` (bootstrap only): no
ring-3/EL0-equivalent, no per-process page tables, no syscall path. The
arch-neutral modules (elf, zsp, zxvfs, the whole platform layer) are portable and
host-tested, but **x86_64 has no userspace yet**. To reach parity with arm64:
- page tables (PML4) + user/supervisor split,
- a syscall entry (`syscall`/`sysret`) mirroring the arm64 SVC path,
- a ring-3 process bring-up mirroring `arch/arm64/el0_userspace.c`,
- then wire the platform layer (§4) and confirm `[BOOT_OK]` under `qemu-system-x86_64`.

Until then, report x86_64 as **"boots, kernel-only"** — never as full parity.

### 3b. EFI boot payloads for the universal disc
`mkuniversal_disc.sh` assembles a GPT + FAT32 ESP and expects per-arch payloads
at `/EFI/BOOT/BOOTX64.EFI`, `BOOTAA64.EFI`, `BOOTRISCV64.EFI`. The mechanism is
real (UEFI removable-media multi-arch boot), but each arch's kernel must be
wrapped as (or chainloaded by) a UEFI PE binary. Produce the `.EFI` stubs
(GNU-EFI or a PE64 wrapper that loads the kernel blob) and drop them where the
script expects. The script documents the exact paths at its top.

### 3c. Wire the platform layer into the non-arm64 arches
arm64 already boots the platform layer (the `[FEAT]` lines). For each other arch,
two edits (see [`build_system/platform_layer.mk`](build_system/platform_layer.mk)):
```make
include build_system/platform_layer.mk
KERNEL_SRCS += $(PLATFORM_LAYER_SRCS)
CFLAGS      += $(PLATFORM_LAYER_INC)
```
and in that arch's `kernel_main`, just before its BOOT_OK milestone:
```c
#include "boot_features.h"
boot_features_init(<that_arch_uart_puts>, <cpu_cores_or_0>, <mem_mb_or_0>);
```
That is the whole integration. Verified portable: all 11 modules compile
freestanding for x86_64/aarch64/armv7.

---

## 4. Native-format artifacts + contingency for bugs

The compiled product must render programs in the **native Tri-Space formats**
(`.zxvc` = S+, `.cedez` = S−, `.cedec` = S0), not raw ELF. The pipeline:
- `build_system/mkzxpkg.c` — compile it (`cc -O2 mkzxpkg.c -o mkzxpkg`), it emits
  the `.seal` (the triad binding: all 5 hard requirements enforced by `tri_bind`).
- `build_system/sign_release.sh` — wraps the seal in a ZSP Ed25519 envelope
  (magic `ZSP1`); on-target `zxpkg_verify_release` checks triad-intact **and**
  signature-vs-root-key. This is **proven** to reproduce the test fixture.

**Contingency doctrine (already built in, keep it):** the loader and ops
boundaries **fail closed, never fake**. If a native artifact is malformed, the
loader returns not-bound rather than executing garbage; the font stack returns
"no glyph" rather than a fabricated box; the bridge returns `NOT_BOUND` rather
than an invented address. When you add the x86_64 loader path, preserve this: a
bug must degrade to a refusal with a diagnostic, not a silent wrong result. The
parser fuzzers in `ci_gate.sh` (ELF, ZXVFS, under ASan+UBSan) are the guard —
keep them green.

---

## 5. The universal artifacts

- **One disc, all chips** — `mkuniversal_disc.sh`. Not a fat binary (no CPU runs
  another's machine code); it is UEFI's multi-arch ESP. Needs §3b payloads.
  Boot-test each arch's firmware in QEMU with the matching OVMF/edk2 (`ovmf` for
  x86_64; build/fetch AAVMF for arm64, RISC-V edk2 for riscv).
- **One VM file, all hypervisors** — `mkuniversal_vm.sh`. The raw image *is* the
  universal disc; it also emits `zxv.ova` (DMTF standard) and, if `qemu-img` is
  present, `qcow2/vmdk/vhdx/vdi`. This path is **proven end-to-end** with
  `qemu-img` on the dev machine — the conversions work; they just need the real
  disc (§3b) as input.

---

## 6. Signing & secrets — READ BEFORE YOU BUILD

- The **Ed25519 root key is NOT in the repo** and must never be. `sign_release.sh`
  takes it via `ROOT_KEY=/secure/root_priv.pem`. `build_system/keys/root_priv.pem`
  is a **dev-only, gitignored** placeholder — do not ship anything signed with it.
- **SSH keys pasted in an earlier session are COMPROMISED — rotate them.** Never
  paste any private key, SSH key, or credential into an AI prompt (including
  mine). Generate/hold keys on the server; the operator handles them out-of-band.
- Choose privacy-preserving options; never put secrets in URLs or env dumps that
  get logged.

---

## 7. Definition of done (acceptance criteria)

Report the build complete only when **all** of these hold, each with evidence:

1. `preflight_all_targets.sh` → every target **compiles** (matrix printed).
2. `build_all_targets.sh` → every target with a toolchain **links** to an artifact
   (arm64 + x86_64 mandatory; riscv/arm32 OK-or-SKIP with reason).
3. `ci_gate.sh 100` → clean build **+** `verify-all` green **+** fuzzers green **+**
   100/100 arm64 boots reach `BOOT_OK` with no fault. Fail-closed: a missing
   required step fails the gate.
4. x86_64 boots under `qemu-system-x86_64` to its BOOT_OK (kernel-only is
   acceptable **if labelled**; full ring-3 parity is the goal — §3a).
5. `mkuniversal_disc.sh` → a disc that boots on ≥2 arches' firmware in QEMU.
6. `mkuniversal_vm.sh` → `zxv.raw` + `zxv.ova` produced; opens in ≥1 hypervisor.
7. A signed native-format release verifies with `zxpkg_verify_release` against
   the **real** root key.

For anything you cannot complete, say so explicitly with the reason and the
matrix cell — do not round up. That honesty is the deliverable.

---

## 8. Orientation — where things are

- Canonical source tree: `05_KERNEL/kernel/` (`arch/`, `src/`, `boot/`, `compat/`).
- Reference build: `build_system/Makefile.arm64`. Host gate: `kernel/Makefile`
  (`make verify-all`).
- Platform layer entry: `kernel/src/bootfeat/boot_features.c`
  (`boot_features_init`), called from `kernel/arch/arm64/kernel_main_arm64.c`.
- Do **not** treat `zxv_os/`, `zxv_build/`, `zxv_complete/`, `zxv_sdk/`,
  `05_KERNEL/subsystems/` as canonical — they are archived/partial snapshots,
  kept intentionally, not the source of truth.
- Licensing: `LICENSE` + `LICENSES/` (OPL-1.1, SEL-3.3, CC BY-SA 4.0, the Royal
  Writ). SPDX headers are on the sources. Preserve them.

Good building. Keep it honest, keep it fail-closed, and label anything you could
not verify.
