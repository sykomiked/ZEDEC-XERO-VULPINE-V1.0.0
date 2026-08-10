# CONSUMER READINESS AUDIT — ZXV / ZEDEC pqOS

**Scope:** `/Users/36n9/CascadeProjects/VIT_INDIA_PROPOSAL/05_KERNEL`
**Date:** 2026-08-09
**Method:** three dimension audits (boot-and-run, security-posture, data-safety) executed against the live tree — builds run, QEMU boots taken, screendumps analysed pixel-by-pixel, the kernel's own verifier compiled and attacked, host filesystem probes written and run — followed by an **adversarial verification pass** in which every BLOCKER was actively attacked to see whether it survived.
**Convention used throughout:** `CLAIMED` = what a file, log line, or document asserts. `VERIFIED` = what was measured by running something. `UNVERIFIED` = could not be checked in this environment, and is labelled as such rather than assumed.

---

## 1. The one-line verdict

**No. This is not shippable to consumers today, and it is not close — a non-technical person cannot obtain it, cannot install it, cannot see an uncorrupted screen on the one path that reaches real hardware, and cannot get their files back if anything goes wrong.**

What it **is** ready for, and this framing is the accurate one:

> **ZXV is a credible, unusually honest research kernel with a working ARM64 reference target.** It builds clean, boots reliably in QEMU, persists files across power cycles, talks real DHCP/DNS/TCP, boots from real UEFI firmware, and carries a cryptographically sound package verifier and a journalling filesystem with a genuine crash-fuzzer. That is a *technical demonstrator and a research platform*. It is not a product, and every consumer-facing word around it should say "reference implementation" until Phase 3 of §6 is passed.

The gap between those two sentences is not a polish gap. It is roughly **two to four engineer-years** (§8).

---

## 2. Readiness scorecard

| Dimension | Readiness | The single thing holding it back |
|---|---:|---|
| **Boot & run** | **15 %** | Nothing runnable is distributed — no ISO, no image, no remote, no tag, no CI; the "double-click to boot" launcher invokes a cross-compiler the user does not have (VERIFIED on a fresh clone) |
| **Data safety** | **12 %** | No write barrier or cache flush exists anywhere in the storage stack, and an unrecognised superblock is silently reformatted as "blank disk" |
| **Security posture** | **22 %** | The security features that are *advertised at boot* — encrypted vault, TLS, post-quantum key establishment — are not in the shipped binary or have no callers |
| **Weighted overall** | **≈ 16 %** | — |

**How the weighting was chosen (stated so it can be argued with):** `0.45 × boot-and-run + 0.30 × data-safety + 0.25 × security-posture` = `6.75 + 3.60 + 5.50 = 15.85 ≈ 16 %`.

The reasoning: readiness is **lexicographic before it is additive**. If a consumer cannot obtain and start the system, no other dimension is reachable, so boot-and-run gets the largest weight. Data safety is next because its failures are *irreversible* — a person loses work and cannot get it back — whereas most security failures here are currently *unexploitable in practice* because nothing has been distributed to attack. Security posture is weighted lowest **not** because it matters least in a shipped product (it would matter most) but because the pre-release state means the exposure is latent rather than live.

Three dimensions were audited in this pass. **Dimensions not yet audited** — and therefore not in the figure — include: accessibility, internationalisation, performance under load, multi-user/privacy, power management, and support/legal readiness. Every one of those is very likely below 20 % on current evidence, so **16 % should be read as an optimistic ceiling, not a midpoint.**

---

## 3. Blockers, verified

Ordered by how much each one unblocks. Fixing #1 is a precondition for anyone outside this machine ever observing #2 through #9.

### 3A — Adversarially attacked and still standing

#### B1. Nothing runnable is distributed *(unblocks: everything)*

**VERIFIED by fresh clone.** `git remote -v` → empty. `git tag` → empty. 1116 tracked files, no release, no remote. `.gitignore:14-17` excludes `*.elf,*.bin,*.iso,*.img`; `:23` excludes `kernel_arm64.bin`; `:30` excludes `dist/`; `:51` excludes `BOOTAA64.EFI`. `git ls-files | grep -iE '\.github|workflow|\.gitlab-ci'` → **no output — there is no CI of any kind.** `grep -niE 'download|releases/|\.iso|install|quick start' README.md` → **zero matches — the README contains no install instructions.**

Cloned fresh to scratchpad: no `*.bin`, `*.elf`, `*.img`, `*.iso`, `*.EFI`, `*.o` anywhere; `dist/` and `cells/` do not exist. `ZXV-Desktop.command:4` → `build_system/run_desktop.sh:33-35`, which runs `make -f build_system/Makefile.arm64 all` unconditionally with no check for an existing binary and no toolchain preflight (`Makefile.arm64:8` `CROSS_COMPILE ?= aarch64-linux-gnu-`, no host fallback). Run with a consumer PATH: `make: aarch64-linux-gnu-gcc: No such file or directory` → `Error 1` → `!! build failed`.

The installer is real tracked code, but `git check-ignore -v cells/kernel_arm64.bin` → `.gitignore:23` — **its payload is gitignored**, so on a fresh clone it reports `staged: []`. The one local "release" is untracked, 182 commits stale (`dist/MANIFEST.txt` records `ef968b0`), and `shasum -a 256 -c SHA256SUMS` → `2 computed checksums did NOT match`, with SHA256SUMS listing its own digest as `e3b0c442…b855` (the hash of the empty string).

**Consumer impact:** there is no channel to obtain this and no artifact to run. Zero users can start it.
**Work:** 2–4 weeks — CI that builds per-arch images, signs them, boot-tests each under edk2, and publishes on tag; stop gitignoring the release payload; make `run_desktop.sh` prefer a shipped binary.

#### B2. 43.3 % of the first screen a user sees is corruption *(unblocks: any demo, any screenshot, any trust)*

**VERIFIED, independently reproduced on a fresh boot.** 624 of 1440 rows are >30 % pure white = **43.3 %**; first bad row 704, last 1391; identical across two dumps ~45 s apart while 3,835,622 bytes changed — permanent, on a live animating frame.

**The originally reported root cause was WRONG and is corrected here** (this matters — a fix aimed at the reported cause would not have changed one pixel). The real cause: `kernel/src/desktop/zxv_shell.h:35-36` sizes the per-tile field arrays for 1280×720 —
```
#define FIELD_TX 80             /* 1280 / 16 */
#define FIELD_TY 45             /*  720 / 16 */
```
— giving 3600 entries (`g_tval`/`g_tdz`, `kernel_main_arm64.c:360-363`), but `zxv_present` indexes them for the full 2560×1440 screen (`kernel_main_arm64.c:384-390`): max `ti = (1439>>4)*80 + (2559>>4) = 7279`. Entries 3600–7279 are never written; `llvm-nm` places `g_tval[7279]` **inside `g_scanout`** and `g_tdz[7279]` inside `g_fdepth`, so displayed pixels are fed back as valence and saturate 16-px tiles to pure white/black. This predicts the measured boundary exactly: `ti ≥ 3600` first occurs at y=704 for x ≥ 1280 — and measurement shows row 704 white only from x=1280. The originally alleged boundary (row 810) shows **no transition at all**.

A *second, currently invisible* defect is real and should also be fixed: `prism_break.h:44-45` (1920×1080) vs a 2560×1440 surface means `pb_render_frame` (stride 1920) rewrites screen rows 810–1419 behind the compositor. It is contained inside the `prism_break` struct, so it leaks no unrelated kernel memory — the "leaked kernel memory" phrasing in the original finding is **not supported**.

**Consumer impact:** the first screen after boot reads as a crashed machine. (Correction: the dock and left rail *are* still legible on top of the corruption — "unusable" overstated those specific controls.)
**Work:** 1–3 days. Derive `FIELD_TX/TY` from `ZXV_DISPLAY_MAX_W/H`, or clamp `ti`. Separately make `pb_init` **refuse** rather than silently clamp — `display.h:85-86` already states the project's own rule: *a mode beyond the budget is refused with a reason, never silently clamped.*

#### B3. On real UEFI firmware the desktop is illegible — and it is an out-of-bounds write *(unblocks: physical hardware)*

**VERIFIED under edk2/AAVMF.** Serial line 15: `GOP fb=0x000000007c7a0000 res=800x600 stride=800 fmt=1`; line 196: `[E0123] [DRIVER ONLINE] UEFI GOP framebuffer — ZEDEC desktop is on screen`. The screen shows the desktop repeated and sub-pixel illegible.

Cause confirmed line-exact in the *built* translation unit (`Makefile.arm64:109` selects `kernel/arch/arm64/kernel_main_arm64.c`, 3474 lines — the 142-line `kernel/boot/kernel_main_arm64.c` is a decoy and is not built): compose side uses `g_disp.w` (`vbe.c:133,146`; `lattice_dimensions.c:63`; `kernel_main_arm64.c:1896`), present side uses the compile-time constant (`:385-386`, `ZXV_FB_W` = `ZXV_DISPLAY_MAX_W` = `2560u` per `display.h:89-93`; `grep -rn ZXV_DISPLAY_MAX build_system/` → nothing, no override). `zxv_present` (lines 367-422) references `g_disp` **zero times**. 2560/800 = 3.2 composed rows per scanout row.

**This finding was understated, not overstated.** `prism_break.h:102` declares `framebuffer[1920*1080]` = 2,073,600 words; `zxv_present` indexes to 3,686,399 — **1,612,800 words (6.45 MB) past the object, every frame**, including the event-loop re-present at `:2762`. On the default ramfb path `lattice_dim_render`'s `for(i=0;i<w*h;i++) fb[i]=` makes it a 6.45 MB out-of-bounds **write**. Even the failure fallback (`:1889`, 1280×720) shears.

**Consumer impact:** every real laptop or board hands the OS an 800×600 / 1024×768 / 1920×1080 GOP framebuffer. All of them get a smear. This is the only route to physical hardware.
**Work:** 2–5 days. Replace every `ZXV_FB_W/H` in `zxv_present` (`:368, 383-387, 411-415`) with `g_disp.w/h/stride`; add a boot self-check that composes a known pattern and verifies a sampled pixel round-trips at the negotiated stride.

### 3B — Reported as BLOCKER, evidence strong, **not yet put through the adversarial pass**

These are labelled honestly: the evidence below was gathered by execution and code reading, but unlike B1–B3 nobody has yet tried hard to refute them. Treat severity as provisional.

#### B4. Zero real hardware drivers are linked — this is a VM guest, not an OS for hardware
`aarch64-linux-gnu-nm kernel_arm64.elf | grep -icE " (t|T) (wifi|bluetooth|usb|ahci|nvme|ata|pci|ps2|acpi)_"` → **0**. The only device symbols are `ramfb_init`, `uart_*` (PL011), `virtio_blk_init`, `virtio_net_init`, `virtio_input_*`, `virtio_bus_poll`, `virtio_mmio_find`. Unbuilt on every arch: `wifi/wifi.c` (1996 L), `bluetooth/bluetooth.c` (2174), `audio/audio.c` (1145), `video/video.c` (1134), `net/rtl8139.c`, `ata/ata.c`, `acpi/acpi.c`, `keyboard/keyboard.c`, `mouse/mouse.c`, `pci/pci.c` — 7,526 lines total. There is no `kernel/src/usb` directory at all.
**Impact:** on a real machine there is no storage driver, no USB (therefore no keyboard or mouse on any modern laptop), no Wi-Fi, no Bluetooth, no audio, no PCIe enumeration. **Work:** multi-engineer-year.

#### B5. No write barrier or cache flush exists anywhere in the storage stack
`kernel/include/blockdev.h:21-32` — `block_device_t` has exactly two ops, `read_sector`/`write_sector`; **there is no flush member**. `virtio_blk.c:80-81` defines only `VIRTIO_BLK_T_IN`/`_OUT`; `VIRTIO_BLK_T_FLUSH` is never issued and `VIRTIO_BLK_F_FLUSH` is never negotiated. `zxvfs.c:95-106` writes the staged payload then the commit header with nothing between. `jchecksum()` (`zxvfs.c:42-47`) covers txn_id/count/target_lba only — **not the staged payload**, so replay cannot distinguish fresh staging from stale. `zxvfs_format()` (`:306-314`) never zeroes the 16 staging sectors. `make_release.sh:76` uses `-drive … format=raw` with no `cache=` → QEMU defaults to **writeback**.
Probe on a previously-used disk pre-filled with `0xDE`, dropping only the staged-payload writes: `zxvfs_write returned 0` / bitmap sector 18 now `DE DE …` / inode sector 20 now `DE DE` / `remount rc=0 count=0 free_sectors=2048 (fresh disk should be 8192)`.
**Impact:** power cut mid-write → next boot mounts cleanly, reports no error, shows zero files, and 3 MB of the 4 MB store is permanently marked in-use. **Work:** 1–2 weeks.

#### B6. An unrecognised superblock is silently reformatted as "blank disk"
`kernel_main_arm64.c:1978-1983`, verified verbatim in the tree today:
```
int mrc = zxvfs_mount(&g_zxvfs, &g_vblk);
if (mrc == -2) {
    /* No valid superblock -> blank disk -> format then mount. */
    boot_msg("  [FORMAT] blank disk — writing fresh ZXVFS");
```
`zxvfs.c` returns `-2` from **three** conditions — blank disk, bad magic/version (`:326`), and any geometry mismatch (`:335`). There is no distinct code for damaged vs foreign. The superblock is a single unreplicated sector (`ZXVFS_SB_SECTOR 0`) with **no checksum field and no backup copy**. Probe: `mount after ONE flipped SB bit rc=-2` → `after boot-path auto-format count=0 ← user data gone`; `mount on FOREIGN filesystem rc=-2 → boot path formats it`.
**Impact:** one bit of rot erases the user's files; plugging in a USB stick holding their photos formats it on sight. **Work:** 3–5 days (split return codes, CRC32 in the 119 spare `_pad` words, backup superblock at the last sector, confirm before formatting removable media).

#### B7. No recovery tooling a user can reach — no fsck, no backup, no undelete, no off-machine reader
An fsck exists **only as a host-only static function inside the test binary** (`test_zxvfs_fuzz.c:105-107`), not exported by `zxvfs.h`, in no kernel target. Shell dispatch (`kernel_main_arm64.c:3116-3180`) offers `ls-p / write / cat / rm / sync`; grep for `"fsck"|"backup"|"restore"|"export"` → nothing. `rm` unlinks with no confirmation and frees extents in the same commit — no trash. No host-side image tool exists (`grep -rn zxvfs build_system/*.c *.py` → nothing), so a dead machine's disk **cannot be read on a Mac or PC**. `ROADMAP.md:46` marks the recovery utility `[planned]`.
**Impact:** every other failure is terminal. **Work:** 3–4 weeks; the host-side `zxvfsdump`/FUSE reader alone is a few days and is the **highest value per hour in this document**.

#### B8. The root `/` is a volatile RAM disk reformatted every boot
`kernel_main_arm64.c:1950-1966` calls `ramdisk_format_fat32()` unconditionally then mounts it at `/`; backing store is `ramdisk.c:22-23`, a 16 MB BSS array. Persistence lives on a **separate** store reachable only through five ZXVFS-specific shell verbs with a flat 32-char namespace.
**Impact:** anything saved to a path — the only file model a normal person understands — is gone at the next reboot, while the banner says `[MOUNTED] FAT32 RAM disk (16MB) at /`. **Work:** 3–4 weeks (ZXVFS needs a directory namespace first).

#### B9. Four of five architectures have no persistent storage at all
`grep -rln zxvfs_mount kernel/arch/` → exactly one file. `virtio_blk.o` appears only at `Makefile.arm64:113`; the other four link the 26-line `blockdev.c` vtable helper with no driver behind it. Yet `zxvfs.c` is listed in **all five** Makefiles — dead code on four of them.
**Impact:** on x86_64, the architecture most consumers own, nothing is saved. **Work:** 4–8 weeks.

#### B10. Installer trust-pinning is fail-**open**
`install_zxv.py:229-241` looks for `PROVENANCE/ROOT_TRUST_ANCHOR.txt` or `install/ROOT_TRUST_ANCHOR.txt`; **both are absent** (only `.template` exists). `pinned_key_id()` returns `None`, so `enforce_pinned_anchor()` (`:249-252`) prints `WARNING: no pinned root trust anchor found; trusting key-id … on faith` and returns. The fail-closed mismatch branch (`:253-256`) is never reached.
**Impact:** any bundle signed by any key is accepted. **Work:** 1 week, but it is *gated on the production root ceremony*, which is a process task, not a code task.

---

## 4. What was claimed but is not there — the honesty gap

**This is the section that matters most.** The project's own standard is that overclaiming is the cardinal sin. Below, every row is `file:line` CLAIMED against a measured VERIFIED.

### 4.1 Direct internal contradictions — the repo disagrees with itself

| CLAIMED | VERIFIED |
|---|---|
| `PROVENANCE/ROADMAP_TO_COMPLETE.md:12` — **"architectures booting \| 5/5 — arm64, x86_64, riscv64, riscv32, arm32"** | `SERVER_HANDOVER.md:103-106` says x86_64 "compiles, minimal kernel_main", riscv64/riscv32 "not linkable locally", arm32 "compiles"; `build_system/handover.json:74` records x86_64 as `COMPILES_KERNEL_ONLY`, `:81` riscv as `UNLINKABLE_LOCALLY`. **Measurement matches SERVER_HANDOVER, not the ROADMAP.** Only arm64 was booted here (8/8). `EVIDENCE/x86_64_boot_trace.log` does record a real x86_64 `BOOT_OK`, so "5/5 booting" is not fabricated from nothing — but it is **not supportable as written** and must be corrected to a per-arch matrix. |
| `ROADMAP_TO_COMPLETE.md:13` — "one disc boots two arches" | `dist/zxv-disk-arm64.img` booted under edk2 with no `-kernel`: 0 occurrences of `BOOT_OK`, firmware printed `Image type X64 can't be loaded on AARCH64 UEFI system`, dropped to UEFI Shell. `strings | grep -iE 'BOOTX64\|BOOTAA64\|GRUB'` → no matches: **the image contains no EFI bootloader of any kind.** It boots zero arches. |
| `ROADMAP_TO_COMPLETE.md:15` — "in no kernel image on any arch \| 54 … ~22,000 dead lines (20 %)" | Measured **71 files / 23,268 lines / 24.6 %** (≈63 / ≈22.5 k excluding the 8 `mlkem *_validate.c` host validators). Directionally honest, **numerically stale — the figure has grown.** |
| commit `8c70a4c` (today) — "the whole stack now links on all five architectures" | `grep -n syscall build_system/Makefile.x86_64` → **zero matches**, yet `ring3.c:11` includes `syscall.h` and calls into it; a standalone freestanding compile leaves `zxv_syscall_count`/`_info`/`_permit` undefined, defined only in `syscall/syscall.c`, which appears **only** in `Makefile.arm64`. The x86_64 link should fail. **UNVERIFIED locally** — missing `nasm` aborts before the link step. Re-run on the build box. |

### 4.2 Boot messages that assert successes the system cannot know or does not have

| CLAIMED (boot log / banner) | VERIFIED |
|---|---|
| `[E0123] [DRIVER ONLINE] ramfb 2560x1440 — ZEDEC desktop is on screen` | 56.7 % of that screen is the desktop; **43.3 % is field-array overrun** (B2). The log reports a success the pixels contradict. |
| `[E0123] [DRIVER ONLINE] UEFI GOP framebuffer — ZEDEC desktop is on screen` | Illegible tripled smear plus a 6.45 MB per-frame OOB access (B3). |
| `[FORMAT] blank disk — writing fresh ZXVFS` (`kernel_main_arm64.c:1981`) | The code **cannot distinguish blank from corrupt from foreign** — all three reach that branch (B6). The message asserts a fact it has not established, while destroying data. |
| `[MOUNTED] FAT32 RAM disk (16MB) at /` | Reads to a user as storage; it is wiped every boot (B8). |
| `[INITIALIZED] Daemon visa system + encrypted vault` (`robin_debanks.c` banner at `:966`; comments `:936-939` promise "encrypted, time-locked storage for private keys, seed phrases, and credentials") | The shipped `kernel_arm64.elf` contains **no** `robin_store`, `robin_retrieve`, `aes*`, `gcm*`, or `ghash` symbols; the AES S-box bytes are **absent** from the ELF (`find()` → -1) though present in `aes256_gcm.o`. Zero non-test callers → `--gc-sections` (`Makefile.arm64:97`) drops the entire encryption path. Only `robin_init` survives. **The vault is inert; nothing is ever encrypted.** |
| `Post-quantum key establishment (ML-KEM-768) … both parties derived the same secret` (`:2093, :2108`) | ML-KEM-768 is genuinely in the ELF and genuinely round-trips — but its **only** non-test caller is a boot KAT with deterministic seeds (`:2102-2104`, whose own comment says "this is a KAT, not a live key"). It establishes keys with **no peer**. One CPU testing itself. |
| The product name **"post-quantum OS / pqOS"** | Code signing is **classical Ed25519 exclusively** (`zsp.c:45, :83`). `grep -rln 'ml_dsa\|mldsa\|dilithium' kernel/src` → **0 files**. `grep -rln mlkem` in `net/`, `tls/`, `denconnect/` → **none**. A quantum adversary breaks the signing root. **The headline security property is not delivered on the paths that matter.** |
| `SUBSYSTEM_INDEX` / net docs referencing a TLS slice | `kernel_arm64.elf` contains **no** `tls_client`/`tls13`/`x25519`/`chacha20`/`poly1305` symbols. Zero callers anywhere outside `kernel/src/tls/`, so `--gc-sections` drops it. **There is no transport encryption in the running kernel.** |
| `kernel_main_arm64.c:3388` — "A .zsp package is Ed25519-verified … BEFORE any byte is executed" | Only **v1** packages are gated. A raw ELF hits `run: [WARN] unsigned ELF (dev only) — no signature check` (`:3417`) and is then executed at EL0. A **ZSP v2** file (`ZSP2` magic) also fails the `is_zsp` test and bypasses verification entirely. |
| `install/README.md` quick start | The exact documented command dies on an unhandled Python traceback (`InstallSecurityError`, `install_zxv.py:197`). The refusal itself is **correct** fail-closed behaviour; the README never mentions it and there is no `.sig` in the tree. |
| `ZXV-Desktop.command:4` — "Double-click to boot the ZEDEC pqOS graphical desktop" | Triggers a cross-compile that fails on any machine without `aarch64-linux-gnu-gcc` (B1). |
| `smap.h:112-113` — "Reassembly: reconstruct original data from CID + S-Map" | `smap.c:258-303` **never writes `out`**; it returns `sm->total_size` as "proof of reassembly capability" (its own comment, `:271-280`). Its test (`test_new_modules.c:1142-1144`) asserts only the length and never compares the buffer — **a green test certifying a function that reconstructs nothing.** Built into all five Makefiles. Same pattern: `et_process_pending` (`event_transport.c:178-194`, "simulate successful delivery", **on the live arm64 boot path** at `kernel_main_arm64.c:2694`) and `cell_transport_receive` (`cellular_multikernel.c:403-410`). |
| `zxvfs.h:12-31` — "full old-or-new atomicity at ANY size"; `kernel_main_arm64.c:1970-1973` — "a redo journal makes every write crash-consistent" | Holds **only** under the shipped harness's crash model, where a dropped write is atomically absent and all prior writes are already durable (`test_zxvfs.c:33-43`, `test_zxvfs_fuzz.c:81-85`). Neither harness models reordering or a device cache. Under reordering the same code **destroys the filesystem** (B5). The header's own "WHERE ATOMICITY STOPS" section never mentions durability, flush, or barriers. |
| `dist/SHA256SUMS` | `shasum -c` → `2 computed checksums did NOT match`; the file lists its own digest as the SHA-256 of the empty string. |

### 4.3 What is genuinely honest — and should be preserved and copied

Refusing to praise this would itself be a distortion. These are places where the tree tells the truth against its own interest, and they are the template for fixing §4.1–4.2:

- `kernel/src/wifi/wifi.h:17-19` — **"There is no Wi-Fi silicon in this tree."** Plain, unhedged.
- `SERVER_HANDOVER.md:100-145` — a per-arch matrix that **matches measurement exactly**, plus `:131` "report x86_64 as 'boots, kernel-only' — never as full parity".
- `README.md:36-37` — "The reference target is ARM64 (the only target buildable on Apple Silicon)."
- `build_system/mkuniversal_disc.sh` — refuses to run without mtools/dosfstools and warns it will log an arch as `CARRIED-BUT-NOT-YET-BOOTABLE` rather than claim it boots.
- `handshake.h:7` — **"THIS CLIENT DOES NOT AUTHENTICATE THE SERVER"**, structurally enforced at `handshake.c:288-292`.
- `entropy.c` / `kernel_main_arm64.c:963-964` — labels the non-FEAT_RNG fallback "**NON-cryptographic … predictable. Not for production secrets.**"
- `PROVENANCE/ROOT_TRUST_ANCHOR.txt.template` — "There is deliberately NO real trust anchor committed."
- `test_zxvfs_fuzz.c:33-35` — states its own blind spot: "data corruption under consistent metadata is outside its view."
- `display.h:85-86` — "A mode beyond the budget is REFUSED with a reason, never silently clamped." (The rule is right; `pb_init` violates it — see B2.)
- `Makefile.arm32:6-11` — names its own three excluded files in the header. That is disclosure, not incrimination.

**The single highest-leverage honesty fix:** delete or correct `ROADMAP_TO_COMPLETE.md:12-15` and make `SERVER_HANDOVER.md`'s matrix the one canonical status source. Right now the project contains both an honest document and a flattering one, and an outside reader has no way to know which to believe.

---

## 5. What was refuted — do not spend effort here

Five findings were attacked and did **not** survive at BLOCKER severity. Recorded so the work is not misdirected.

| Finding | Verdict | Why |
|---|---|---|
| **"The installer cannot install — every bootloader payload is 'planned'"** | **→ MAJOR** | The load-bearing claim ("no path from powered-off hardware to a running desktop") is **refuted by measurement**: an ESP built with the tree's own `BOOTAA64.EFI` booted under real edk2/AAVMF to `BOOT_OK` and the EL0 P-TERM shell. A standards-correct UEFI payload exists and works. The claim also misattributes media creation to `install_zxv.py` when `mkuniversal_disc.sh` owns it. Residue is real: `install_zxv.py` is a kernel-cell *stager*, not an OS installer, and the stale manifest structurally prevents it writing the bootloader that does work. |
| **"4 of 5 architectures unbuildable, only arm64 has display/input/userspace"** | **→ MAJOR** | Two of four sub-claims are false. `kernel/arch/x86_64/ring3.c` is **318 lines of real ring-3 bring-up** — GDT/TSS `:49`, DPL-3 syscall gate `idt_set(0x80, …, 3)` `:97`, U/S page tables with 2 MB split `:103-134`, dispatcher `:183` — built (`Makefile.x86_64:71, :278, :287`) and wired into boot (`kernel_main_x86_64.c:168-182`). The original grep searched for arm64-specific *filenames* and inferred absence of a *capability*. `vbe.c` (linear framebuffer) is in **all four** non-arm64 Makefiles. `EVIDENCE/x86_64_boot_trace.log` shows a real x86_64 `BOOT_OK`. The "runs on your hardware whatever the chip" quote **does not exist in the tree** (grep → zero hits) — it was a strawman; the project's actual claims match reality. Only `nasm` is missing for x86_64, and `boot.gas` already exists unwired. Source parity is 201 vs 212 files, not "nothing at all". |
| **"Code-signing root is a DEMO key whose private half is in the source tree"** | **→ MAJOR** | **Factually refuted by experiment.** `root_priv.pem` is **not in the repo**: `.gitignore:45` is `*.pem`, `git ls-files \| grep keys/` → nothing, `git check-ignore -v` confirms. Cloning confers **zero** signing capability. Proof: an independently generated attacker key signed a payload with the project's own `sign_package.py` → `zsp_verify = -4 (not signed by root key) rejected`. `build_signed_app.sh:18-21` mints a **fresh random key per tree**, so it is not a shared demo key. The original "PROOF" was tautological — it showed only that the holder of a private key can sign. `test_zsp` → **8/8 PASS**, including "wrong root key rejected". The verifier is sound. Residue: shipped binaries embed a *development* anchor whose private half is an unencrypted PEM on a networked machine, the production root has never been minted, and the app-signing path lacks the dev-key tripwire that already guards the release path (`sign_release_offline.sh:33-42`). |
| **"No integrity checking on file data"** | **→ MAJOR** | The technical fact reproduced exactly (flip a byte → `read rc=1024, first byte 0x54, corruption reported? NO`) but the scope was overstated. The "wallet file" example is wrong twice: `crypto_wallet.c` (760 L) has **zero persistence** and already SHA-256s (`:389`) and HMACs (`:398`, verified `:625`) every payload. `zxvfs_tri.c:251-253` re-derives SHA-256 per role and **quarantines** on mismatch, so triad-stored artifacts *are* covered. `chronicle_persist.c:89-90` re-verifies its whole hash chain and refuses a corrupted image; `abupdate.c:55` runs `zsp_verify2` before staging. Corrupted code and updates do **not** execute. And the "false security claim" arm fails — the project documents this limit rather than hiding it. NTFS, ext4, HFS+ and APFS all behave the same way. Real gap, mainstream-normal, not disqualifying. |
| **"A failed update DELETES the working installed payload"** | **→ downgraded** | The mechanism is real and was reproduced (`transactional_disk.py:148-150` calls `rollback()` before `_write_journal()` at `:152`; `rollback()` `:180-185` reloads the stale on-disk journal and discards the in-memory backup; `:188-194` unlinks its targets) — with a clean control showing correct restore when `finalize()` ran. But the destructive path is **unreachable from any shipped or documented flow**: the built release tarball contains **no installer at all** (`tar tzf dist/zxv-universal.tar.gz` → only `zxv/install/README.txt`, because `make_release.sh:51` copies a non-existent path under `2>/dev/null \|\| true`), and `--install-to`, the only flag that reaches `TransactionalDiskInstall`, appears in **zero** docs. Fix the ordering bug — it is a genuine latent defect — but it is not a live consumer hazard. |

**Also worth crediting, discovered while refuting:** the ZSP verifier's cryptography is sound (accepts valid, rejects tampered `-3`, rejects wrong key `-4`, 8/8 tests); `abupdate` has real anti-rollback; `keyceremony_root.sh` refuses networked machines and won't overwrite an existing key; a broad grep for hardcoded-success security predicates in `decent/zab/invproof/crypto_verify` came back **empty** — the prior hardening passes were real, not claimed.

---

## 6. The ordered path to 100 %

Each phase has a **gate**: a measurable condition, not a judgement call. Do not start phase N+1 before phase N's gate is green.

### Phase 0 — Truth alignment *(2–4 days, no engineering dependency, do it first)*
Correct `ROADMAP_TO_COMPLETE.md:12-15`; make `SERVER_HANDOVER.md`'s matrix canonical. Remove or gate every boot banner that announces a capability the binary does not contain (vault, TLS, "post-quantum key establishment"). Replace `[FORMAT] blank disk` with a message that states what was actually observed. Move the 71 unbuilt files to `staging/` so the shipped surface and the reserve are not visually interchangeable.
**Gate:** every capability string in the boot log maps to a symbol present in `kernel_arm64.elf`, checked by a script in CI. No document in the tree contradicts another about arch status.

### Phase 1 — Make the display correct *(1–2 weeks)*
B2 (field-array sizing) and B3 (present-side stride), plus `pb_init` refusing instead of clamping.
**Gate:** screendumps at 800×600, 1024×768, 1920×1080 and 2560×1440 each show **0 rows** >30 % saturated garbage, and a boot-time pattern self-check passes at every negotiated stride. An ASAN/valgrind-equivalent host harness of `zxv_present` reports zero OOB accesses.

### Phase 2 — Make it obtainable *(2–4 weeks; depends on Phase 1, or you ship a broken screenshot)*
CI (there is none today) that builds per-arch, signs, boot-tests under edk2, and publishes on tag. Un-gitignore the release payload. `run_desktop.sh` prefers a shipped binary. Fix `mkuniversal_disc.sh` output so the ESP actually contains `BOOTAA64.EFI`/`BOOTX64.EFI`. Delete the stale, checksum-failing `dist/`.
**Gate:** a person with no toolchain, on a machine that has never seen this repo, downloads one file and reaches the desktop. Verified by an actual clean-machine run, and the release fails automatically if `BOOT_OK` is not observed for each published arch.

### Phase 3 — Make data survivable *(4–6 weeks; can run parallel to Phase 2)*
B5 flush/barrier (`block_device_t` op + `VIRTIO_BLK_T_FLUSH` + `jchecksum` covering the payload + zero the staging sectors + `cache=directsync` in every `-drive`), B6 split return codes + superblock CRC + backup superblock, B7 host-side `zxvfsdump` **first** (few days, highest value/hour), then in-kernel `fsck` + trash bin, plus a reordering-aware fuzzer mode and a torn-sector mode.
**Gate:** a reordering + torn-sector fuzzer runs ≥100 k injected crashes with zero filesystem-destroying outcomes; a deliberately bit-flipped superblock is **repaired**, never formatted; a foreign filesystem is never touched; a disk from a dead machine is read successfully on macOS.

### Phase 4 — Make it a filesystem a person recognises *(4–6 weeks)*
B8 (ZXVFS at `/`, RAM disk demoted to `/tmp`), directory namespace, geometry derived from `dev->total_sectors` instead of the 4 MB / 256-file / 64 KB-per-file compile-time caps, indirect extents.
**Gate:** a 500 MB image holds a 100 MB file across ≥50 fragmented extents, survives 1000 power-cut cycles, and the desktop's own save dialog writes to persistent storage by default.

### Phase 5 — Make the security real rather than announced *(6–10 weeks + process time)*
Air-gapped production root ceremony (`keyceremony_root.sh`, encrypted key, never on a build box, never in a session — this is a standing rule); ship `ROOT_TRUST_ANCHOR.txt` and publish the key-id out of band; make `enforce_pinned_anchor()` fail **closed** in release builds; refuse unsigned ELF and route ZSP-v2 through `zsp_verify2` (B-tier finding); wire TLS 1.3 into the net path with a real X.509 verifier and `TLS_VERIFY_REQUIRED`; wire the AES-GCM vault to a real flow with a store→retrieve boot self-test; require virtio-rng or refuse to derive keys; then either implement ML-DSA + hybrid X25519+ML-KEM **or** stop calling it post-quantum.
**Gate:** a release artifact signed by the production root installs; the same artifact re-signed by any other key is **refused**; `nm` shows TLS and AES symbols present in the shipped ELF; a real TLS 1.3 session completes against a public server with certificate validation on; `run` refuses an unsigned ELF.

### Phase 6 — Make it run on hardware *(6–18 months, the long pole)*
B4: PCIe enumeration, xHCI/USB HID, NVMe **or** AHCI, ACPI, a real display path beyond GOP, power management. Then B9 (block driver + mount per arch) and x86_64 parity per `SERVER_HANDOVER.md §3`.
**Gate:** the OS boots from its own installed media on ≥3 physically distinct machines, with USB keyboard and trackpad working, installs to internal NVMe, and survives suspend/resume.

### Phase 7 — Make it maintainable in the field *(8–12 weeks, depends on Phase 2 + 6)*
A/B extended from `app.slotA/B` to **kernel + bootloader** images, a recovery slot the firmware can select, a boot watchdog that auto-reverts after N failed boots, and signed OTA delivery.
**Gate:** a deliberately bricked kernel update auto-reverts unattended within 3 boots on real hardware.

### Phase 8 — Make it a product *(see §7; 6–12 months, largely non-code)*

---

## 7. What "100 % consumer ready" would actually require

Everything above is necessary and **not sufficient**. The following are not optional extras; every one of them is table stakes for a general-audience OS, and none exists today.

**Engineering, beyond §6**
- Accessibility: screen reader with a real accessibility tree, full keyboard navigation, high-contrast and reduced-motion modes, minimum contrast ratios, scalable UI. **Note: the shimmer field in B2 is exactly the kind of animated background that must be disable-able.** Nothing in the tree addresses any of this.
- Internationalisation: Unicode text rendering beyond the current font path, input methods for CJK/Indic/RTL, locale formats, translated UI.
- Multi-user and privacy: user accounts, permissions, disk encryption at rest, a privacy policy that matches actual telemetry behaviour (currently: no telemetry, which is the right default — say so explicitly).
- Application story: a consumer OS with no applications is a demo. The unbuilt `apps/` tree (clock, notes) is not an answer; a documented, stable ABI and an SDK is.
- Performance and stability budgets: boot time, memory floor, frame pacing, and a crash-free-session metric measured over fleet time, not a single boot.
- Automated regression: the current `verify-all` is strong, but it did not catch B2 or B3 because nothing inspects pixels. Add rendering, boot-to-desktop, install, and power-cut suites.

**Non-code — usually underestimated and frequently the actual long pole**
- **Legal review:** SBOM is present (`sbom_kernel_sources.txt`) but every third-party licence must be cleared, particularly around the GPL engine-integration strategy (id Tech) versus proprietary distribution — that combination has shipped-product consequences and needs counsel before, not after, release. Trademark clearance is separate and urgent given the memory-noted rule that proprietary names must replace dev-reference names in everything shipped.
- **Export control:** the product ships cryptography. Classification and any required notifications must be done before distribution, in every jurisdiction targeted.
- **Hardware certification:** for a shipped OS image this means, at minimum, a validated hardware compatibility list and per-model qualification. For preinstalled hardware it additionally means regulatory certification of the device, driver signing where the firmware requires it, and — if Secure Boot is to stay enabled on consumer machines — a signed shim, which is a months-long third-party process.
- **Update infrastructure:** signed OTA needs a CDN, an update server, staged rollout with kill-switch, telemetry sufficient to detect a bad rollout, and a key-rotation plan. The A/B mechanism is the smallest part of this.
- **Support:** documentation for non-technical users, a support channel with a staffed response SLA, a public bug tracker, a security-disclosure address with a stated response time, and a CVE process.
- **Warranty, terms, and data-protection posture:** EULA, warranty disclaimers, GDPR/CCPA-class obligations if any user data is ever transmitted, and a breach-notification plan.
- **Release governance:** the standing rule that the production root key is never minted in a session or on a build box needs to become a written, audited ceremony with named custodians and recorded witnesses.

---

## 8. Honest effort estimate

**Assumptions, stated so the numbers can be recalculated:**
1. A team of 3–5 experienced systems engineers, full time. A single developer multiplies every range by roughly 3–4×.
2. The Linux build box in `SERVER_HANDOVER.md` remains available for the non-arm64 work.
3. "Consumer ready" means **installable on a defined hardware compatibility list**, not "runs on any PC" — the latter is a decade-scale problem that Linux solved with thousands of contributors.
4. No pivot in scope; the unbuilt reserve stays unbuilt except where a phase requires it.
5. Estimates are engineering time only, excluding certification queue time (add 3–6 months of wall-clock for Secure Boot shim signing and regulatory processes, which run in parallel but cannot be compressed).

| Phase | Optimistic | Likely | Pessimistic |
|---|---:|---:|---:|
| 0 — Truth alignment | 2 days | 4 days | 1.5 weeks |
| 1 — Display correctness | 1 week | 2 weeks | 4 weeks |
| 2 — Distribution + CI | 2 weeks | 4 weeks | 8 weeks |
| 3 — Data survivability | 4 weeks | 6 weeks | 10 weeks |
| 4 — Real filesystem semantics | 4 weeks | 6 weeks | 12 weeks |
| 5 — Security made real | 6 weeks | 10 weeks | 20 weeks |
| 6 — Hardware (drivers, x86_64 parity) | 6 months | 12 months | 18 months |
| 7 — Field maintainability (A/B kernel, OTA) | 8 weeks | 12 weeks | 20 weeks |
| 8 — Product/non-code (legal, cert, support) | 6 months | 9 months | 12 months+ |

**Rolled up (phases 6 and 8 overlap substantially; 0–5 and 7 partly parallelise across a 3–5 person team):**

- **To "credible public beta on a defined HCL":** **9–15 months.**
- **To "consumer ready" as defined in §7:** **2–4 engineer-years of calendar time**, i.e. roughly **18–30 months** with a 3–5 person team — and that number is dominated by Phase 6, which is genuinely a driver-writing problem with no shortcut.
- **To "honest demonstrator anyone can download and run without lying to them":** **Phases 0 + 1 + 2 only — 5–13 weeks.** This is by far the best return on effort available, and it is what should be done next.

**Confidence:** high for phases 0–3 (all measured directly, small and well-scoped). Medium for 4–5 and 7. **Low for phase 6** — driver work is the classic underestimate, and the absence of any USB, PCIe, or storage driver today means there is no empirical basis in this tree for a tighter range.

---

### Closing note on method

Three of the eleven originally reported blockers did not survive adversarial verification, and one of the three that did survive had the **wrong root cause** — a fix aimed at the reported cause would not have changed a single pixel. That is a caution about this document too. Every claim here carries its `file:line` or its command output so it can be attacked the same way. Where something could not be checked in this environment — physical hardware, four of five architectures, the x86_64 link, concurrent filesystem access — it is marked **UNVERIFIED** rather than assumed, and those should be the first things run on the Linux build box.
