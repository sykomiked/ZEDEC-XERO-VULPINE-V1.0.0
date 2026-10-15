<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# ZXV boot-to-disk path: gaps and designs

Audit date 2026-10-14, origin/main at 4bea0160. Read-only audit: I made no
edits in the repo. Branches 688qg0 (bootattest), 86si57 (seats) and 8jvjng
(recover) were read from a scratch clone.

Scope: firmware -> EFI stub -> kernel -> root filesystem -> init -> services
-> login, plus install, update, recovery, encryption, fsck, and loading the
disc's models into the tensor engine.

This report assumes you have read docs/BAREMETAL.md, docs/HW_DRIVERS.md,
TURNKEY_OS_GAPS.md and OS_FEATURE_AUDIT_2026-10-12.md. It repeats them only to
correct them (section 0).

---

## 0. Corrections to the existing docs

| Doc says | Actually | Evidence |
|---|---|---|
| BAREMETAL.md:90-93: "the x86_64 boot path has no code that opens or writes an internal disk (the ATA driver ... never initialized)" | **Opens: wrong. Writes: still true.** Since the hwdrv merge, NVMe and AHCI bind every internal disk. `hw_storage_selftest` then reads LBA 1, checks the GPT and mounts each disk's ESP as FAT32, read-only. | kernel_main_x86_64.c:296-306, hw_storage.c:129-195 (only reads; the write callback at :86 is never called on the boot path) |
| TURNKEY row 1 cites `kernel/grub.cfg` as boot evidence | That file is the legacy i386 Multiboot ISO (`multiboot /boot/vovina_shakina.bin`). It plays no part in the UEFI path. | kernel/grub.cfg:9, kernel/Makefile:188-192 |
| TURNKEY row 4: zxvfs is "journaled, tests + fuzz" | True, but the geometry is fixed: a 4 MB data region, 256 files, flat 32-byte names, 10 extents per file. It cannot be a root filesystem or hold /home. | zxvfs.h:59-65 |
| Audit: "Startup items, services: stub, `init/` untested" | Worse than a stub. `init/init.c` belongs to the legacy i386 kernel (`kernel/boot/kernel_main.c:132`). Neither the x86_64 nor the arm64 main calls it, and it marks every service RUNNING whatever the init call did. | init.c:59-87 (status set at :86 unconditionally) |
| Audit: "Disk management: no GPT" | GPT **read** exists: header CRC, entry-array CRC and bounds caps. It only looks for the ESP. There is no GPT write, no type-GUID enumeration and no mkfs. | hw_storage.c:35-77 |
| Audit: seats and hwkey are on branches | Both are on main now (`kernel/src/seats`, `kernel/src/hwkey`). Neither is linked into the bare-metal images. hwkey has host TPM/Secure Enclave providers only, with no bare-metal TPM driver. | build_system/Makefile.x86_64 (no seats/ident/zui/hwkey/terms sources); hwkey.h:31-38 |
| Audit: "Time: no NTP client" | SNTP with multi-server agreement and tz are on main and linked into x86_64, but never called. What is actually missing is an RTC source. | timesvc.h:1-30; Makefile.x86_64:376-378; no `timesvc` in kernel/arch/x86_64 |
| Audit: measured boot "on 688qg0" | Still branch-only and not wired into either stub (its own HONEST LIMITS say so). `boot_evidence` on main hashes the *log text*, not the images, so it is not measured boot. | ba.h (branch) "measurement hook ... NOT wired"; boot_evidence.c:67 |
| BAREMETAL.md:95: a 64 MB stick | True for the plain disc only. The golden image is 1.63 GB, so it needs a stick of 2 GB or more. | BAREMETAL.md:63 |

---

## 1. Ranked gaps

The ranking is for the milestone **"boot the golden disc on a PC -> mount root
from disk -> start services -> login"**, then the AI models, then
install/update/recovery. Sizes are rough new-code estimates (C LOC unless
noted).

| # | Gap | Status | Evidence (file:line) | Size | Depends on |
|---|---|---|---|---|---|
| 1 | **Auto-format on an unrecognised disk.** On arm64 the kernel formats any virtio-mmio disk whose sector 0 lacks the ZXVFS magic. A GPT disc qualifies: its sector 0 is the protective MBR. `zxvfs_format` then writes sector 0, sector 1 (the primary GPT header) and the inode sectors. CI misses this only because it attaches the disc as virtio-blk-**pci**, which the arm64 kernel does not drive. | hazard (destructive) | kernel_main_arm64.c:2111-2117; zxvfs.c:289-316, :326; uefi_boot_check.sh:54; virtio_blk.c:1 | ~15 | none |
| 2 | **The stub-to-kernel handoff carries only the framebuffer.** The memory map is fetched for ExitBootServices and then dropped. Nothing is handed over for: ACPI RSDP/SMBIOS/DTB, the boot disk and partition identity, the model verdicts (the `efi_check_models` return value is discarded), wall-clock time, or a cmdline/safe-mode flag. The x86 kernel ignores its arguments. | partial | x86 efi_stub.c:147, :150-176; arm64 efi_stub.c:74, :113-129, :136-145; zxv_bootinfo.h:26-33; kernel_main_x86_64.c:168-170 | ~300 | none |
| 3 | **x86 stub unsafe on real firmware.** It copies the kernel to 0x100000 even when `AllocatePages(AllocateAddress)` at that address failed (it is "best-effort"). It also continues after a second ExitBootServices failure (arm64 prints "ok" unconditionally). The arm64 stub writes a hardcoded QEMU PL011 address. The 5-minute firmware watchdog is never disarmed while 1.6 GB is hashed. | partial (QEMU-only assumptions) | x86 efi_stub.c:126-133, :156-161, :180-183; arm64 efi_stub.c:17, :140-145 | ~60 | none |
| 4 | **No physical memory manager on x86_64.** Only the low 1 GB is identity-mapped, and DMA is a static 8 MB pool. `mm/` is i386 32-bit paging with MAX_PAGES = 1 GB. CI boots with 512 MB. The two models need about 1.6 GB plus arena and KV cache. | missing | boot.gas:70; hwplat_x86.c:6-9, :82, :125-126; mm/mm.h:12-15; uefi_boot_check.sh:30,34 | ~300 | 2 |
| 5 | **Partition discovery is ESP-only, and blockdev is 1990s-shaped.** `block_device_t` is 512-byte sectors, a 32-bit LBA and one sector per call. 4Kn disks and anything past 2 TB are refused. Nothing enumerates partitions by type GUID. | partial | blockdev.h:17-30; hw_storage.c:35-77, :93-99 | ~220 | none |
| 6 | **The kernel does not know which disk it booted from.** Every disk is self-tested the same way, so with an installed copy and a stick both present, root is ambiguous. | missing | hw_storage.c:136-195 | ~60 | 2, 5 |
| 7 | **No root filesystem from disk.** x86 mounts nothing. arm64's root is a 16 MB FAT32 RAM disk formatted at every boot. zxvfs (4 MB, 256 files, flat) cannot hold a system. The FAT32 driver is read-only, needs clusters of 4 KiB or less, uses 8.3 names, reads whole files into a buffer (u32 size) and caps directories at 256 entries. | missing | kernel_main_arm64.c:2079-2101; arm64/ramdisk.c:22; zxvfs.h:59-65; fat32.h:10-15, :88-92 | ~450 (zxsys reader) | 5, 8 |
| 8 | **The golden discs have no system layout yet.** A core disc per arch (ESP: loader, assets, the core model from `build_system/golden.core`) and architecture-neutral swarm discs (one FAT32 partition named ZXV_SWARM_<GROUP>, GPT type 5A585653-5741-4D53-9000-000000000001, models grouped by `build_system/golden.swarm`), each at most 1.8 GB; the x86_64 kernel mounts every swarm disc over AHCI/NVMe and hashes its models after boot (`hw_models.h`). There is still no system, data, boot-control or crash partition. | partial | mk_golden_disc.sh; kernel/src/hwdrv/hw_models.c | ~300 host C (mkzxsys) | design of 7 |
| 9 | **No service manager on bare metal.** x86 main ends in a `hlt` loop with no timer interrupt; the PIT is used only to calibrate the TSC. There is no supervision, restart, ordering or unit list. | missing | kernel_main_x86_64.c:328-335; hwplat_x86.c (PIT only); init.c:86 | ~450 + ~120 timer/LAPIC | 2 |
| 10 | **Login is not linked into bare metal.** `seats`, `ident`, `terms`, `zui_lock` and `zui_present_fb` are not in Makefile.x86_64. There is no persistent store for the roster or the terms-acceptance record. The "boot vault" referenced by seats/terms/ZXVD.md does not exist. | missing (pieces exist, host-tested) | Makefile.x86_64 sources; seats.h:4; terms.h:11; docs/ZXVD.md:765 | ~300 glue | 9, 11 |
| 11 | **No writable data partition.** zxvfs v2 at fixed geometry is enough for a roster and settings (4 MB) but not for /home. | partial | zxvfs.h:59-65 | milestone: ~40 (zxvfs v2 inside a partition); later zxvfs v3 ~900 | 5 |
| 12 | **The disc's models cannot run on bare metal.** The kernel never opens them. `zt_gguf_open` wants the whole file in one buffer. The FAT32 driver cannot read a 1.1 GB file. Kernels are built `-mgeneral-regs-only`, so only the C reference runs: about 0.6 tok/s single-thread for the 1.5B. There is no SMP: no SIPI, MADT or PSCI code. | missing | zt_gguf.h:5-7; zt_model.h:254-258; fat32.h:88-90; Makefile.x86_64:28; bench/.../REPORT.md:267; no SIPI/MADT in kernel/arch | stub load ~120, kernel glue ~200, SIMD enable ~80, SMP ~600 | 2, 4 |
| 13 | **Model check is integrity, not authenticity.** `MODELS.SHA` sits unsigned on the same FAT volume as the models, so anyone who edits one can edit both. `MANIFEST.SHA256` is never checked on the device. | partial | efi_models.h:221-300; mk_golden_disc.sh:132-136 | ~40 (pins compiled into the stub) | 2 |
| 14 | **No boot entries, menu, recovery entry or safe mode.** Only the removable path `\EFI\BOOT\BOOT*.EFI` is used. There is no Boot#### variable, no key-hold, no timeout and no recovery image. | missing | mk_efi_disc.sh:87; mk_golden_disc.sh:170; no SetVariable in kernel/arch | ~250 | 8 |
| 15 | **No system-image A/B or rollback.** `abupdate` is app-level: files in zxvfs, seeded with `hello.zsp`. | partial (app-level only) | abupdate.h:17-20; kernel_main_arm64.c:2137-2142 | ~500 | 8, 14 |
| 16 | **No on-device installer.** `install_zxv.py` and `transactional_disk.py` are host Python that copy files into a directory or mount. There is no GPT write, mkfs, ESP install or Boot#### step. | missing | install/README.md:1-5; install_zxv.py:260-353; transactional_disk.py:1-10 | ~900 + UI | 5, 8, 11, 14, 15 |
| 17 | **No crash dump, panic persistence or safe mode** (audit confirmed). `recover/` on 8jvjng is a component rollback framework. | missing | x86: "ring-3 self-test faulted" only (kernel_main_x86_64.c:233); recover.h (branch) | ~350 | 8, 14 |
| 18 | **No Secure Boot or measured boot.** The stub is unsigned (the doc says to turn SB off). bootattest is branch-only and unwired. There is no bare-metal TPM2 (CRB/TIS) driver. | missing | BAREMETAL.md:114-116; ba.h (688qg0); hwkey.h:31-38 | ~400 wiring + ~700 TPM2 CRB; signing is a process | 2, 15 |
| 19 | **Disk encryption.** None. ChaCha20-Poly1305, HKDF, Argon2id and Shamir already exist and are freestanding. | missing | tls/aead.h:1-47; ident/ident.h; ident/id_shamir.c | ~600 | 11, 18 for TPM sealing |
| 20 | **fsck.** zxvfs replays its journal on mount and nothing else. There is no cross-check of bitmap against extents. | partial | zxvfs.c:338 | ~300 | 11 |
| 21 | **Processes and apps on x86 bare metal.** Ring 3 is a self-test; file and process syscalls return ENOSYS. arm64 has EL0 processes, an ELF loader and signed .zsp. zxvd is hosted C on libc + `select()` + AF_UNIX/AF_INET sockets + `mmap`. The 144 apps run in the hosted browser UI. | missing on x86 / partial on arm64 | kernel_main_x86_64.c:216-230; el0_userspace.c:691; kernel_main_arm64.c:3804; zxv_host.c:448, :2619, :2724; zxv_model_host.c:92 | large (separate track) | 7, 9 |
| 22 | **Time source.** There is no RTC driver, so timesvc gets no RTC sample and TLS certificate expiry has no clock until SNTP succeeds. | missing | timesvc.h:7-10 | ~20 (EFI GetTime in the handoff) | 2 |
| 23 | **ACPI on UEFI-only PCs.** `acpi.c` scans only the BIOS area (EBDA, 0xE0000) for the RSDP. With CSM off it may not be there; it should come from the EFI config table. | partial | acpi.c:25-45 | part of #2 | 2 |
| 24 | **DVD / ISO9660 golden disc.** There is none. On a DVD the firmware's SimpleFileSystem sees only the El Torito FAT image, so `efi_check_models` would find no models. Kernel-side optical reads would need ATAPI, which AHCI leaves alone. | missing | mkuniversal_disc.sh:144-154 (plain disc only); HW_DRIVERS.md:173 | ~100 shell (USB-first); DVD reads later | 8 |

**Size budget.** Today the image is 1,632,632,832 bytes, of which the models
are 1,608,720,768. The layout in D3 adds a system image (tens of MB) and a
16 MiB data partition. Targets:

- stay within 3.7 GB, so it fits a "4 GB" stick (4.0e9 bytes) and a DVD-5;
- ship **one** universal golden image carrying both BOOTX64.EFI and
  BOOTAA64.EFI. The models are arch-independent, so this saves 1.6 GB of
  distribution and one CI artifact.

---

## 2. Two primitives the designs share

### 2.1 Knowledge join (promote from fwadapt into lpres)

`lpres_conjoin` and `lpres_disjoin` are truth-order operators (min and max on
F < N < B < T). They are the right tools for *"all of these must hold"*. They
are the wrong tools for *"what do my sources say"*.

Combining two measurements needs the knowledge join:

| told | T | F | B | N |
|---|---|---|---|---|
| **T** | T | B | B | T |
| **F** | B | F | B | F |
| **B** | B | B | B | B |
| **N** | T | F | B | N |

`fwadapt` already implements this as a static (`fwadapt.c:86 join_value`).
Expose it as `lpres_state_t lpres_kjoin(lpres_state_t, lpres_state_t)` in
lpres.c, about 15 LOC plus a table test, and use it everywhere below.

**Rule of thumb.** kjoin across *sources about one subject*, then
`lpres_conjoin` across *subjects that must all hold*. Note that conjoin's
N-absorbs-B rule (lpres.c:74-79) means a gap plus a contradiction is N, so it
WAITs and never PROCEEDs.

### 2.2 The verdict, carried as an lpx tag

Every decision below produces one `lpx_t`. The action comes from
`lpx_action()` alone: one table lookup, never if-chains in callers.

- **Polarity:** from `lpx_polarity()` evidence counts, or set FORBIDDEN when a
  real check caught tampering: a signature failure, a version below the
  anti-rollback floor, or a cross-linked extent.
- **Phase:** advances potential -> emerging -> manifest -> dissolving with
  `lpx_advance`. That gives each boot slot, mount, service and install step
  the same lifecycle vocabulary.
- **FORBIDDEN:** always goes to an `lpx_qlog`, kept on the boot-control
  partition (D3) so it survives reboots. Per X6, a full log halts.

---

## 3. Designs for the top gaps

### D1. Never format what you did not recognise (gap 1)

The mount verdict for an unknown superblock is **N**, not "blank disk".

```c
/* kernel_main_arm64.c:2113 today: mrc == -2 -> zxvfs_format().  Replace with: */
lpres_state_t v = (mrc == 0) ? LPRES_STATE_TRUE
                : (mrc == -2) ? LPRES_STATE_NEITHER   /* not ours: unknown */
                : LPRES_STATE_FALSE;                  /* ours but broken   */
/* N -> WAIT: leave the disk alone, log "[STORAGE HELD] not ZXVFS, untouched".
 * Formatting is an install step (D7) that needs a person's confirmed tag. */
```

Treat a sector-0 `0x55AA` signature or LBA-1 `"EFI PART"` as positive evidence
of *someone else's* data. Then `kjoin(N, F_ours)` = F, which means REFUSE,
which is never a format. For the QEMU development flow, keep an explicit
`zxv.format=1` handoff flag (D2) that the dev scripts pass. Nothing formats by
default.

### D2. Handoff v2: one record, everything the kernel needs (gaps 2, 3, 6, 13, 22, 23)

The stub already walks the firmware for GOP and models. Collect the rest while
boot services exist, into one page-aligned EfiLoaderData buffer allocated
*before* the final GetMemoryMap. Point to it from the existing fixed page
(x86 0x80000, arm64 0x40070000), so neither entry path changes.

```c
#define ZXH_MAGIC 0x3248585Au            /* "ZXH2" */
typedef struct { uint64_t base, pages; uint32_t kind, _r; } zxh_mem_t;
/* kind: USABLE, RESERVED, ACPI_RECLAIM, ACPI_NVS, MMIO, ZXV_MODEL, ZXV_HANDOFF,
 * RUNTIME. Compacted and merged from the EFI map: ~40 entries on a real PC. */
typedef struct {
    uint64_t phys, bytes;                 /* loaded copy, or 0 if not loaded   */
    uint8_t  sha256[32];
    lpx_t    tag;                         /* D5 verdict                         */
    uint8_t  name_len; char name[54];
} zxh_model_t;
typedef struct {
    uint32_t magic, version, bytes, crc32;     /* crc over the whole record  */
    uint64_t rsdp, smbios3, dtb;               /* EFI configuration table    */
    uint64_t fb; uint32_t w, h, stride, pixfmt;
    uint8_t  disk_guid[16], part_guid[16];     /* LoadedImage device path HD node */
    uint32_t part_index;
    int64_t  efi_time_s;                       /* GetTime before EBS, UTC    */
    uint8_t  slot, slot_tag, flags, _r;        /* D4; flags: SAFE, RECOVERY, NOMODELS, FORMAT_OK */
    uint32_t n_mem, n_models;
    zxh_mem_t   mem[128];
    zxh_model_t models[4];
} zxh_t;
```

**Kernel acceptance is itself a verdict:**

| Check | Verdict | What the kernel does |
|---|---|---|
| magic + CRC + version good | T | uses the record |
| CRC bad | F | serial-only, no disk writes, `[HANDOFF_BAD]` |
| absent (the `-kernel` path) | N | today's behaviour |

**Stub fixes that come with it (gap 3):**

- Refuse to hand off (print and halt) when the 0x100000 reservation or the
  second ExitBootServices fails. Do not copy over firmware memory.
- `SetWatchdogTimer(0,0,0,0)` before hashing.
- On arm64, take the UART from the SPCR table (or stay silent) instead of the
  hardcoded `0x09000000`.

### D3. Golden disc v2 layout (gaps 7, 8, 11, 14, 24)

| # | GPT name | Type | Size on disc | Contents | Who reads it |
|---|---|---|---|---|---|
| 1 | ESP | EF00 | 64 MiB | `\EFI\BOOT\BOOTX64.EFI` + `BOOTAA64.EFI` (one universal image), `\EFI\ZXV\RECOVERY.FLG`, `MEGAROM.TVL`, README, licences | firmware |
| 2 | ZXV-BOOTCTL | ZXV GUID | 1 MiB | two bootctl copies (D4), the quarantine log, the crash record (D8) | stub (BlockIo), kernel |
| 3 | ZXV-SYS-A | ZXV GUID | ~64-256 MiB | **zxsys** signed read-only image: kernel, services, apps, fonts, music, icons | stub (kernel blob), kernel (root) |
| 4 | ZXV-MODELS | ZXV GUID | 1.61 GB | **zxmodels**: a 4 KiB header (name, 4 KiB-aligned offset, bytes, sha256 per model) then raw GGUF bytes | stub (BlockIo), kernel (raw extents) |
| 5 | ZXV-DATA | ZXV GUID | 16 MiB | zxvfs v2: seat roster, terms acceptance, settings (live-stick persistence) | kernel |

SYS-B is not on the disc. The installer creates it on the target, and an
update writes it.

**Why each choice:**

- **Raw models partition, not FAT.** The stub hashes and loads through
  `EFI_BLOCK_IO` (about 80 LOC to find the sibling partition by
  disk_guid+index). The kernel reads contiguous extents with the 64-bit
  multi-block `hw_blk_t.read` it already has (hwdrv.h:149). So no large-file
  filesystem code is needed in either stage. This is the cheapest correct
  path, and it is how Android and ChromeOS ship big read-only payloads.
- **zxsys** (see D6). It is read-only, so A/B is a partition copy and
  integrity is a signature plus per-file hashes.
- **Convention is best for:**
  - GPT and the FAT32 ESP;
  - the removable fallback path for the live stick;
  - `\EFI\ZXV\ZXVX64.EFI` + a `Boot####` entry + fallback on an installed
    disk, written by the stub's SetVariable while boot services are up.
- **Do not add an ISO9660 build yet.** A 1.6 GB El Torito ESP is fragile across
  firmware. USB sticks first, with DVD only after an ATAPI read path exists.
  If a DVD is needed, xorriso
  `-append_partition 2 0xef esp.img -e --interval:appended_partition_2:all::`
  (the Debian/Ubuntu layout) can carry the same GPT disc.

### D4. Boot-slot selection by knowledge join (gaps 14, 15, 17)

`bootctl` is a 512-byte record (CRC32 + generation), stored twice in
ZXV-BOOTCTL:

```c
typedef struct {
    uint32_t magic, crc32; uint64_t generation;
    struct { uint32_t version; uint8_t img_sha[32]; uint8_t tries_left, confirmed,
             crashes, _r; } slot[2];
    uint32_t rollback_floor;          /* from abupdate.h's ab_state_t, same meaning */
    uint8_t  safe_next, recovery_next, _r[2];
} zxbc_t;
```

**Step 1: choose which bootctl copy to trust.** Each copy is a source.

| Copies | Verdict | Action |
|---|---|---|
| both CRC-good and equal | T | use it |
| one CRC-bad | T, neutral | use the good copy |
| both good, generations differ by exactly 1 | T, neutral | use the newer copy; this is the expected torn write |
| both good, contents differ otherwise | **B** | ESCALATE: boot recovery and show both |
| both bad | N | recovery |

This is the A/B superblock convention, with the impossible case named instead
of guessed.

**Step 2: a verdict per slot.** The stub reads the slot's zxsys header and
measures the kernel blob. Each source contributes one claim:

| Source | Claim |
|---|---|
| e1 signature: ML-DSA-87 over the zxsys header (`zx_upcheck_mldsa.c` is freestanding; verify only, about 15-20 KB of code) | T / F / N (no signature) |
| e2 measured `img_sha` vs `bootctl.slot.img_sha` | T / F |
| e3 version vs `rollback_floor` | T, or **FORBIDDEN** below the floor |
| e4 health: `confirmed` gives T; `tries_left > 0` and unconfirmed gives T with polarity entropic, falling as tries decrement; `tries_left == 0` gives F | T / F |

```c
uint8_t base = lpx_base_from_lpres(lpres_conjoin(lpres_kjoin(e1, e2), e4));
uint8_t pol  = sig_bad || below_floor ? LPX_P_FORBIDDEN
             : confirmed ? LPX_P_NEGENTROPIC : on_probation ? LPX_P_NEUTRAL
             : LPX_P_ENTROPIC;              /* crashes > 0 this generation */
lpx_t tag = lpx_make(base, pol, LPX_PH_EMERGING);
```

**Step 3: choose between the two slots.**

| Slot A | Slot B | Boot |
|---|---|---|
| PROCEED | PROCEED | the higher version |
| PROCEED | anything else | A; record B's tag |
| WAIT (probation, entropic) | not PROCEED | A, `tries_left--` |
| ESCALATE (B: signature good but hash disagrees, i.e. *both claim validity but disagree*) | — | the other slot if PROCEED, else **recovery + ask** |
| REFUSE (F) or N (unsigned, unverified) | — | never booted |
| ROLLBACK (FORBIDDEN) | — | `lpx_qlog_append(slot img digest)`; the slot is dead until a fresh install. X4 absorbing: only new work clears it |
| nothing bootable | | the ESP's own embedded kernel as **recovery** (the golden-disc kernel *is* the recovery image) |

- **Safe mode:** `bootctl.safe_next`, set by the kernel after two crashes in
  the same generation, or by holding Shift/Esc during a 1 s `ReadKeyStroke`
  window in the stub.
- **Confirmation:** the kernel confirms a slot only after `[LOGIN_READY]`
  (D7), not after `[BOOT_OK]`.

This is the Android/ChromeOS A/B convention: the counter scheme is standard.
lpres adds what the convention lacks, a named state for "valid signature, wrong
bytes", which escalates instead of silently burning a try.

### D5. Models: hash while loading, in the stub (gaps 12, 13)

The stub already reads every model byte to hash it (efi_models.h:282-287).
Read into the final pages instead of a 4 MiB scratch buffer, and the
"loading" costs nothing extra.

1. **Size from the map.** From the memory map, `usable` = the sum of
   EfiConventional pages. Load models in `golden.models` order while
   `usable - loaded - bytes >= RESERVE` (512 MiB for OS + KV). A 4 GB PC gets
   both models; a 2 GB PC gets the 0.5B only. A model that is skipped gets
   tag N with no address, so the kernel WAITs and can stream it later from
   the raw partition.
2. **Load and hash.** `AllocatePages(AllocateAnyPages, EfiLoaderData or OEM
   type 0x80000001, ...)`, read in 4 MiB chunks straight into place, and
   SHA-256 as it reads.
3. **Verdict by kjoin of two sources:**
   - S1 is the sha256 list **compiled into the stub** from `golden.models` at
     build time. Once the stub is Secure Boot-signed (gap 18), these pins are
     authenticated with zero extra crypto code.
   - S2 is `MODELS.SHA` (or the zxmodels header) on the disc.

| S1 | S2 | Verdict | Action |
|---|---|---|---|
| match | match | T, negentropic | handed to the engine |
| match | differs | **B** | handed, but tagged ESCALATE: the engine withholds it until a person accepts. The disc manifest was edited, which is evidence of tampering even though these bytes are good |
| differs | match | **FORBIDDEN** | the bytes match an unauthenticated list but not the build's pins: quarantine and free the pages |
| differs | differs | F | REFUSE: corruption |
| no pin (dev build) | — | S2 alone gives T, neutral | handed |

4. **Kernel side** (about 200 LOC). PMM marks the ZXV_MODEL ranges reserved
   and maps them. Then `zt_gguf_open(phys, bytes)`, an arena from the PMM
   (`zt_model_arena_bytes`), `zt_model_load` in place (Q4_K/Q6_K are used in
   place: zt_model.h:254-262), a KV state, and an 8-token greedy run printed
   as `[MODEL_RUN] <name> <fnv>`. That FNV is comparable with the hosted
   bench's per-prompt `fnv` (REPORT.md:232).
5. **What still blocks useful speed.** In order of payoff:
   - **(a)** enable SSE/AVX: CR4.OSFXSR|OSXMMEXCPT|OSXSAVE plus XCR0, and
     build only the tensor TU without `-mgeneral-regs-only`. The kernel is
     single-threaded and has no preemption, so no FPU context switch is needed
     yet. This gives about 2.4x, from REPORT.md:92 (C vs AVX2).
   - **(b)** start the APs (INIT-SIPI from the MADT, from the RSDP in D2) as
     tensor workers through the existing `zt_dist_t` hook.
   - **(c)** the 0.5B as the default interactive model on bare metal.

   Honest: before (a) and (b), the 1.5B runs at about 0.6 tok/s
   (REPORT.md:267). It works, but it is not usable for chat.

### D6. Root = zxsys signed read-only image, plus a mount verdict (gap 7)

Design the format for the smallest correct reader:

```text
header (4 KiB): "ZXS1" | u32 ver | u32 image_version | u32 n | u64 table_off |
                u64 data_off | u64 total | sha256(table) | ML-DSA-87 sig[4627]
                (signature over the header minus sig, + table hash)
table: n x { u64 path_hash (FNV-1a 64), u32 path_off, u32 flags/mode,
             u64 data_off (4 KiB aligned), u64 size, u8 sha256[32] }  sorted by path_hash
strings: NUL-terminated paths; data: file bytes, 4 KiB aligned
```

**Reader:** `zxs_open` is a binary search plus a string compare. `zxs_pread`
is offset arithmetic onto the partition's `hw_blk_t`. About 300 LOC with no
allocator and no write path.

**Integrity is file-granular and lazy:**

- verify the header signature once at mount;
- verify each file's sha256 on its first full read, keeping a bit per entry;
- a mismatch gives FORBIDDEN on *that file*: qlog, and the open fails with no
  kernel panic.

Honest: the conventional answer is erofs/squashfs + dm-verity (block-granular
Merkle). File-granular hashing reads each file once more, but the code is a
third of the size. If big files need random access, block hashes can be added
per file later (the model partition already has its own path, D5).

**Mount verdict for root** (kjoin per source, then conjoin across subjects):

| Subject | Sources | Verdict |
|---|---|---|
| this partition is the booted system | handoff `disk_guid` + `slot` (D2/D4); GPT type GUID ZXV-SYS; bootctl's `img_sha` vs the header hash | T on agreement. **B** when two disks carry the same disk GUID (a cloned stick next to its installed copy): ask which one |
| the image is genuine | ML-DSA signature; `image_version >= floor` | F gives REFUSE; below the floor gives FORBIDDEN |
| readable | a header read succeeds | F gives REFUSE |

| Action | Result |
|---|---|
| PROCEED | mount at `/` read-only, `[ROOT_OK]` |
| WAIT or N | no root: recovery console from the kernel's built-ins |
| ESCALATE | prompt on the framebuffer: "two ZXV systems claim to be this boot" |

**The data partition** (zxvfs v2 inside ZXV-DATA through `hw_bdev_init`, which
already windows a partition: hw_storage.c:93) gets its own verdict:

| Mount result | Verdict | Action |
|---|---|---|
| mount 0 | T | mount read-write |
| journal replayed | T, neutral | mount read-write, logged |
| superblock not ours | N | D1: untouched |
| fsck B (D9) | B | mount read-only + ESCALATE |

### D7. zxinit: services as tagged units (gaps 9, 10, 21)

```c
typedef struct {
    const char *name; uint16_t deps_bitmap_idx; uint8_t critical, max_restarts;
    int  (*start)(void *);                 /* returns 0 = launched           */
    lpres_state_t (*probe)(void *);        /* health: T ok, F dead, B degraded, N not yet */
    void (*stop)(void *); void *ctx;
    lpx_t tag; uint8_t restarts; uint32_t deadline_ms;
} zxu_t;
```

- **Order.** The unit table is generated at build time and topologically
  sorted. The existing declaration gate (`zxv_decl_boot_gate`) checks it, so
  there is no solver at runtime. Units start in order on one core. Each start
  moves the tag potential -> emerging. The first probe then sets the base.
- **Tick.** A LAPIC/PIT tick (about 100 Hz) drives probes and deadlines. This
  is the minimal timer-interrupt work x86 lacks today.

| Probe result | Action |
|---|---|
| T | PROCEED to manifest |
| N past its deadline | F |
| F | restart with backoff 1/2/4 s up to `max_restarts`; after that REFUSE: the unit stays down, the Witness/Wake journal gets a line, and a critical unit sets `bootctl.safe_next` |
| B (alive but its probe fails, e.g. listening but answers wrong) | WAIT while negentropic, else ESCALATE to the notification centre. Never kill-loop it |
| FORBIDDEN (a bounds or sanitizer trap, or a capability violation in that unit) | qlog, never restarted this boot |

**Milestone unit list, all kernel-resident and needing no user processes:**
`storage` (root + data mount), `time` (EFI time from D2 + timesvc), `input`
(usbkbd), `display` (`zui_present_fb` on the GOP fb), `seats` (roster from
ZXV-DATA), `terms` (acceptance record on ZXV-DATA), `login` (`zui_lock`, auth
via seats/ident Argon2id). `[LOGIN_READY]` confirms the boot slot (D4).

**After the milestone:**

- `zxvd` becomes one unit, either as a kernel task with its RPC transport
  swapped from AF_UNIX to an in-kernel queue, or as a ring-3 process once x86
  has a process table.
- Apps start as zxsys entries (`.zxapp` manifests) loaded on demand through
  the arm64 ELF/zsp path ported to x86.

That is the separate track of gap 21. Do not block the login milestone on it.

### D8. Recovery, safe mode, crash record (gaps 14, 17)

- **Recovery** is the kernel embedded in the ESP loader, booted with
  `flags=RECOVERY`. On an installed disk it is a second Boot#### entry,
  "ZXV Recovery". It mounts nothing read-write and offers: reinstall a slot
  from the disc, roll back, fsck, copy out data.
- **Safe mode** is `flags=SAFE`: core units only, no models, no third-party
  apps, GOP text console.
- **Crash record.** The polled drivers are the right shape for panic-time I/O.
  The panic handler writes the last 64 KiB of the boot log, the registers and
  the tag of the running unit to the crash area of ZXV-BOOTCTL, then bumps
  `slot.crashes`. The stub turns crashes into entropic polarity (D4), so the
  next boot falls back on its own. The next good boot shows the record in
  Lifeboat (#11).

### D9. Install as tagged units of work, holding on contradiction (gap 16)

The plan is a fixed list of steps. Each step has an lpx tag and a record in an
install journal written to the **stick's** ZXV-DATA (the target is not
trustworthy until the end), so a power cut resumes:

| Step | Evidence | Holds when |
|---|---|---|
| 1 choose target | disk model and size; GPT present?; any partition without a ZXV type (someone's data) | N until the person types the disk's size to confirm; "erase whole disk" or "use free space" only. NTFS resize is out of scope (honest) |
| 2 write GPT (backup the old header + array into the journal first) | read-back CRC | read-back sometimes matches, sometimes not -> **B** (flaky disk): ESCALATE, stop, never retry blindly |
| 3 ESP: FAT32 mkfs (~250 LOC; FAT write is needed only here) + loader + Boot#### | read-back sha | F -> retry once, then REFUSE |
| 4 copy SYS-A (partition copy) and models (partition copy) | read-back sha vs signed header and pins | as 2 |
| 5 zxvfs format of the data partition (only now, on a partition this plan created) | mount T | — |
| 6 bootctl: A tries=3, unconfirmed | both copies read back equal | — |
| 7 first boot from disk confirms at `[LOGIN_READY]` | D4 | — |

- **First-boot setup** is the login milestone plus: create the OWNER seat,
  show terms (the read gate in `kernel/src/terms`), and write the acceptance
  record on the data partition. Terms s2.2 says records stay on the device,
  which this satisfies.
- `install_zxv.py` stays as the host-side "prepare a stick" tool.

### D10. fsck as an ownership join (gap 20) and encryption (gap 19), briefly

- **fsck.** Every data sector has up to two kinds of claimant: the bitmap
  ("used/free") and the inodes' extents ("file X owns it"). kjoin per sector:

| Bitmap | Inodes | Verdict | Action |
|---|---|---|---|
| used | one owner | T | — |
| free | no owner | F | — |
| used | no owner | B, a harmless leak | reclaim automatically, log |
| free | an owner | **B** | mark used, ESCALATE |
| any | two owners (cross-link) | **FORBIDDEN** | both files go read-only + qlog |

  This is about 300 LOC over zxvfs's existing load_inode and bitmap helpers.
- **Encryption.** Encrypt only ZXV-DATA. Root and models are public and
  signed, so encrypting them buys nothing.
  - **Cipher:** ChaCha20-Poly1305 (tls/aead.c) per 4 KiB block, with
    nonce = (64-bit write generation, block index). zxvfs is copy-on-write, so
    every write lands on a fresh (generation, block) pair.
  - **Tags:** a tag area of 16 B per block (0.4%).
  - **Key:** HKDF(Argon2id(passphrase) || TPM-sealed secret) once a TPM2 CRB
    driver exists; until then, passphrase only.
  - **Recovery key:** a Shamir share set (ident/id_shamir.c). This *is* the
    "boot vault" the seats and terms docs refer to.
  - **Honest:** the industry default is XTS (no integrity). AEAD with
    copy-on-write gives integrity for 0.4% space, so it is the better
    convention here.

---

## 4. What I would build first

Target: **golden disc on a PC -> root from disk -> services -> login**, every
step gated by a new serial marker in `uefi_boot_check.sh` and run in CI with
`-m 4G`.

| Order | Work | Gap | Size | CI marker |
|---|---|---|---|---|
| 0 | No auto-format (D1); stub hardening (refuse a failed 0x100000 reservation or EBS, disarm the watchdog) | 1, 3 | ~75 | existing |
| 1 | Handoff v2 (D2) in both stubs + kernel parse; `lpres_kjoin` | 2, 6, 22, 23 | ~330 | `[HANDOFF_OK]` |
| 2 | x86 PMM from the map; map all RAM with 1 GB pages; DMA pool from the PMM | 4 | ~300 | `[MEM_OK] <MiB>` |
| 3 | blockdev v2 (64-bit LBA, multi-block, 4Kn); GPT enumerate by type GUID; match the boot disk by `disk_guid` | 5, 6 | ~220 | `[PARTS_OK]` |
| 4 | mk_golden_disc v2 layout (D3, universal x86+arm64) + host `mkzxsys` + signing in `sign_release.sh` | 8 | ~450 | image builds |
| 5 | zxsys reader + root mount verdict (D6); zxvfs v2 on ZXV-DATA | 7, 11 | ~500 | `[ROOT_OK]`, `[DATA_OK]` |
| 6 | Timer tick + zxinit (D7) with the 7 kernel-resident units | 9 | ~570 | `[SERVICES_OK] 7` |
| 7 | Link seats/ident/terms/zui_lock/zui_present_fb; roster + terms record on ZXV-DATA; QEMU monitor types a passphrase | 10 | ~300 | `[LOGIN_READY]` |

That is about 2,700 LOC for a PC that boots from the stick, mounts a signed
root from the disc, starts supervised services and shows a login. The
installed-disk A/B (D4), installer (D9), recovery (D8) and models (D5) then
build on exactly these pieces. Do models first among them (D5 + SSE enable is
about 400 LOC, and the hashing pass already exists). Then D4/D8, and D9 last,
because it needs everything else to exist.

**Not on this path, and owned elsewhere:** Secure Boot signing (a process
decision: shim + Microsoft UEFI CA vs. own-key MOK enrolment), the TPM2 driver,
the x86 process model and zxvd port (gap 21), and DVD.
