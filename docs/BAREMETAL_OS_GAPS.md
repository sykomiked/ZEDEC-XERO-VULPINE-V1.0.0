<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# From golden disc to a consumer OS: gaps and design (2026-10-14)

The owner asked (2026-10-14 23:12Z) for ZXV to mount on bare metal from the
golden disc image, with separated user space and everything a consumer OS
has, fully vertically integrated, with the desktop apps and the bare-metal
OS as one universal platform. They asked for the missing pieces to be
designed around the paraconsistent logic, not convention, and as efficient as
possible.

Four audits ran in parallel against `main` at 4bea0160. Their full reports,
with file:line evidence for every row, are in `docs/baremetal-gaps/`:

| Report | Area |
|---|---|
| [kernel_core.md](baremetal-gaps/kernel_core.md) | memory, address spaces, processes, syscalls, scheduler, interrupts, SMP, IPC |
| [boot_storage.md](baremetal-gaps/boot_storage.md) | EFI stub, handoff, disc layout, root filesystem, services, install, A/B, recovery, models |
| [userland_apps.md](baremetal-gaps/userland_apps.md) | user-space runtime, app container, sandbox, language runtimes, zxvd, swarm, blocks, tiers |
| [desktop_devices.md](baremetal-gaps/desktop_devices.md) | session, display, input, login, audio, network, ACPI, driver VM |

This file merges them into one ranked list, one set of design rules and one
build order. `TURNKEY_OS_GAPS.md` and `OS_FEATURE_AUDIT_2026-10-12.md` stay
as the feature-by-feature tables; the corrections to them are in section 6.

## 1. Where ZXV stands

The libraries are much further along than the bare-metal images. Most of a
consumer OS exists as freestanding, host-tested C on `main`: the zui toolkit
(window manager, lock screen, IME, gestures, notifications, settings), seats
and licence terms, the class drivers, TLS with certificate checks, SNTP, the
app manifests and blocks, and the tensor engine. Almost none of it is linked
into either bare-metal image.

What a person gets today from the golden disc:

- **x86_64** (the first physical target): the EFI stub hashes the two models,
  the kernel prints a boot log, runs one 34-byte ring-3 self-test, and halts
  in a `hlt` loop with interrupts off. There is no timer, scheduler, memory
  manager, user process model, session or input after the self-test.
- **arm64**: a real but small user space (per-process page tables, SVC
  syscalls, timer preemption, signed `.zsp` apps loaded from disk). It cannot
  see the golden disc (no PCI), runs out of process slots and user pages after
  about 15 launches, and a faulting app usually halts the kernel.
- **Apple Silicon** stays hosted (zxvd + app), as decided before.

So "separated user space" is the core missing piece, and it is missing on
the architecture the golden disc mostly targets.

## 2. Ranked gaps

Ranked for the milestone **boot the golden disc on a PC, mount from disk,
log in, run an app as its own process**, then the consumer features. Size: S
under 500 lines, M 500-2k, L 2-6k, XL over 6k. "Report" points at the row
with the evidence.

| # | Gap | Status | Size | Report |
|---|---|---|---|---|
| 1 | arm64 boot formatted any non-ZXVFS disk, which would wipe a GPT disc | **fixed in this PR** | S | boot_storage #1 |
| 2 | Boot handoff carries only the framebuffer: no memory map, RSDP, boot disk identity, model verdicts, time or flags; x86 stub copies over memory it failed to reserve | **x86_64 done in this PR** (handoff v2); arm64 stub and boot disk identity next | S | boot_storage #2-3, kernel_core #1 |
| 3 | Physical frame allocator fed by the UEFI map; kernel heap with `free`. The x86 kernel's 90 MB BSS overwrites OVMF's ACPI NVS at 8 MB: move the big buffers onto map frames | allocator built (arm64 user pool); x86 big buffers now on map memory (**this PR**); heap missing | M | kernel_core #2, #13 |
| 4 | Per-process address spaces (full lower half, VMAs, guard pages); x86 has one CR3 for everything | partial arm64, missing x86 | M | kernel_core #3 |
| 5 | Process lifecycle: slot reuse, spawn, wait, kill; a user fault must never halt the kernel | slot reuse and fault fix **done in this PR** (arm64); spawn/wait/kill missing | S | kernel_core #5-6 |
| 6 | x86_64 process runtime: LAPIC timer, scheduler, context switch, x86 ELF, FP save | missing | L | kernel_core #4, #9 |
| 7 | Syscall ABI 1.1: handles, pointer+length I/O, verdict register, correct error codes | partial | M | kernel_core #8 |
| 8 | Loader trust: `run` uses the weak v1 check, runs unsigned code, ignores the signed caps | live hole | S | userland_apps #5 |
| 9 | Root and data filesystems from disk; 64-bit block layer; partition discovery by type | missing | M | boot_storage #5-7, #11 |
| 10 | Golden disc v2 layout: ESP, boot control, signed system image, raw models partition, data | partial (one FAT32 partition) | M | boot_storage #8, D3 |
| 11 | Service manager on bare metal | missing | M | boot_storage #9 |
| 12 | Session spine: zui linked into both images, one event loop, damage-only present | missing (code exists) | M | desktop_devices #1-2 |
| 13 | Login, lock and per-seat licence on screen, persisted to the data partition | missing (code exists) | M | desktop_devices #6, boot_storage #10 |
| 14 | Input on real PCs: HID report parser (mouse, touchpad), PS/2, hot-plug, keymaps | partial | M | desktop_devices #5 |
| 15 | Power-off and reboot | missing | S | desktop_devices #4 |
| 16 | Channel IPC with shared memory; window protocol between apps and the compositor | partial | M | kernel_core #12, userland_apps #3, #8 |
| 17 | Userland runtime `zxrt`: crt0, small libc, syscall wrappers, libzxv for user space | missing | M | userland_apps #4 |
| 18 | zxvd and the AI swarm as user-space services on bare metal (models mapped read-only) | missing | L | userland_apps #7, #11; boot_storage #12 |
| 19 | One app container (`.zxa` = zxpkg triad + ZSP2), `needs` lines, sandbox from manifest | missing | M | userland_apps #6, D1, D4 |
| 20 | Language runtimes (Sutra, Eve, KRON, Trebago, MYRA, wasm) in user space; one Sutra | missing | M | userland_apps #9 |
| 21 | System A/B, boot entries, recovery, safe mode, crash record | missing | M | boot_storage #14-15, #17 |
| 22 | On-device installer | missing | L | boot_storage #16 |
| 23 | NIC to IP stack on x86, RTC + SNTP so TLS can verify | missing | S | desktop_devices #9-10 |
| 24 | Device interrupts (MSI/MSI-X), SMP, then IOMMU | missing | L | kernel_core #16-17, desktop_devices #13 |
| 25 | Audio ring to the mixer and launch sound sequences | partial | M | desktop_devices #11 |
| 26 | ACPI AML (battery, thermal, lid, sleep, backlight, I2C-HID) | missing | L | desktop_devices #12 |
| 27 | Secure Boot signing, measured boot, TPM, disk encryption | missing | L | boot_storage #18-19 |
| 28 | No-code builder, typed inputs, block catalogue by CID; building the 407 catalogued apps | partial | L-XL | userland_apps #13-15 |
| 29 | Hypervisor and Linux driver VM (Wi-Fi, GPU long tail) | missing | XL | desktop_devices #14 |

## 3. Design rules: where the paraconsistent logic changes behaviour

Every design in the reports reduces to the same three steps, which keeps the
cost at a few integer operations and one table lookup per decision:

1. **Join the sources about one subject with the knowledge join** (T+F = B,
   anything + N = itself). `fwadapt` already has it as a static; it moves into
   `lpres` as `lpres_kjoin` so every subsystem uses one table.
2. **Conjoin the subjects that must all hold** with `lpres_conjoin` (min on
   F < N < B < T), so a gap plus a contradiction waits and never proceeds.
3. **Act on `lpx_action(tag)` alone**: PROCEED, WAIT, REFUSE, ESCALATE or
   ROLLBACK. FORBIDDEN goes to the hash-chained quarantine log. The phase
   cycle (potential, emerging, manifest, dissolving) is the lifecycle of a
   boot slot, mount, service, process, install step or app.

Tags go on decisions and units of work, never on data bytes or interrupts.
Where the reports found convention simply best, they say so: interrupt
controllers, locks, TLB tagging, FP save, NX/SMEP/SMAP/PAN, guard pages,
copy-user fixup tables, GPT, the FAT32 ESP, `Boot####` entries, A/B try
counters, AEAD for disk encryption.

The decisions that change:

| Decision | T | F | B | N |
|---|---|---|---|---|
| Disk with no ZXVFS superblock (built) | | blank: format allowed | | unrecognised data: left alone |
| Frame state (2 bits/frame) | owned | unusable | double free or overlap: fenced for life, logged | free |
| Out of memory | | refuse (ISR, reserved class) | | caller waits for a frame; no OOM kill |
| Page fault | map and retry | kill only this process, verdict to parent | page tables contradict the VMA list: hold the process, escalate | wait for a frame |
| Capability (grant mask G, deny mask D) | granted | denied | granted and denied: held, shown to the owner | undecided: first-use consent prompt |
| Process exit | clean | refused or faulted | held conflict, abandoned | never ran |
| Scheduler | run queue | dissolve | `held` queue: no CPU, listed as "waiting for you" | `wait` queue keyed by token, no polling |
| Syscall result (tag in a second register) | value | error | `EHELD` + handle to the conflict | parked, re-issued on wake |
| Boot slot (signature, hash, health, version) | boot it | never boot | signature good but hash disagrees: other slot or recovery + ask | unsigned or unverified: never boot |
| Display mode (EDID, GOP, user) | set it | over budget: refused | EDID and GOP disagree: keep a safe mode, flag it | |
| App permission (declared vs used caps) | grant | refuse | declared and derived disagree: ask | undeclared: consent |
| Two-language critical feature | agree: proceed | | differ and converging: wait; stuck: a person | |

A mode the person reverted, a slot below the rollback floor, or a forged
signature is FORBIDDEN: rolled back, logged, and never retried by itself.

## 4. Vertical integration and the universal platform

The owner's model: the code bases are precompiled into the apps, the three
tiers of initial apps are modular, and people build apps without knowing the
languages. The design that gets there with the least new code:

- **One app container** `.zxa`: the existing zxpkg triad with a ZSP2 release
  signature. No new format. The same file installs on bare metal and hosted.
- **One runtime ABI `zxrt`** built from the same source for bare metal (EL0 /
  ring 3 over syscalls) and hosted (over libc). libzxv reaches user space
  through the existing `sdk/embedded/port` layer, which already builds it
  unmodified. WebAssembly is the portable block format; convention is right
  there.
- **Tiers.** L1 (001-144) are base modules, L2 (145-271) compose 2-5 L1
  apps, L3 (272-407) are apps compiled from L2 modules plus L1. Composition
  only goes down, each L2/L3 app uses all five pillars, and every app uses
  `use <block>` by content id (the project's `zxv-ui/APP_CATALOG_407.csv` and
  `APP_STACK_L1_L2_L3.md` define all 407; the Built-in apps and toolbox thread
  builds them). On bare metal this maps to: L1 precompiled as signed native
  blocks in the system image; L2 and L3 shipped as `.zxa` containers whose
  blocks resolve by CID against L1. Nothing in the container format changes
  per tier; the tier only sets the Immigration visa and which blocks may be
  `use`d.
- **No-code.** `zxapp` already composes blocks and joins two languages with
  lpres. Missing: a builder app, `flow` lines that feed one block into
  another, typed inputs beyond whole numbers, triggers, and one CID catalogue
  shared with AIPI (two registries today).
- **zxvd** keeps one RPC surface: its request handler is already
  transport-independent, so on bare metal it runs as a user-space service
  over channel IPC with the same methods the desktop apps call.

## 5. Build order

Each wave ends in a serial marker the CI QEMU job checks.

**Wave 1, foundation (this PR starts it).**
- Done here: the disk-format hazard (#1); the four-state frame allocator
  (`kernel/src/pmm`) under the arm64 user pool, with page freeing, slot reuse
  and the fault-halt fix (#3, #5; 25 launches in QEMU where 15 used to fail);
  syscall error codes; `lpres_kjoin`; x86_64 handoff v2 (#2,
  `kernel/arch/zxv_handoff.h`): memory map, RSDP, SMBIOS 3, firmware clock,
  the kernel's full window and the disc model verdict, sealed and checked.
  The stub refuses to boot (returns to firmware) when the window covers
  runtime or reserved memory, and boots with ACPI held when it covers ACPI
  memory. CI's UEFI boot now requires `[HANDOFF_OK]`.
- Found doing it: the x86 kernel's fixed window (1 MB to ~91 MB, mostly BSS:
  vino 39 MB, tl 23 MB, ramdisk 16 MB, DMA 8 MB) covers OVMF's ACPI NVS at
  8 MB, so today's kernel overwrites firmware ACPI memory even under QEMU.
- Fixed after that: those buffers now come from firmware-usable memory past
  the kernel (`kernel/arch/zxv_bootmem.h`, `bootmem_x86.c`), fed by the
  handoff map or, on a direct boot, the Multiboot map. The fixed window
  shrank from about 91 MB to under 5 MB, the stub's reservation now succeeds,
  and nothing overlaps ACPI. The window also counts the boot stack now (it
  sat after `__kernel_end`, outside what the stub reserved), and the
  Multiboot magic reaches the kernel (boot.asm used to clobber it).
- Next: arm64 stub parity; power-off (#15); `lpres_hex` linked into both images; loader
  hardening (#8, with the Security thread).

**Wave 2, x86_64 parity and the ABI.** Address spaces on both arches (#4),
the x86 process runtime (#6), ABI 1.1 with verdicts (#7), loading apps from
the disc.

**Wave 3, login.** Disc v2 layout with root and data partitions (#9, #10),
service manager (#11), zui on bare metal (#12), login and licence on screen
(#13), input (#14). Marker: `[LOGIN_READY]`.

**Wave 4, apps as processes.** Channels and the window protocol (#16), zxrt
(#17), the `.zxa` container and sandbox (#19), zxvd and the swarm in user space
(#18), then Burrow end to end as the first real bare-metal app.

**Then:** system A/B and recovery (#21), installer (#22), network + time
(#23), interrupts and SMP (#24), audio (#25), AML (#26), secure boot and
encryption (#27), the no-code builder (#28), the driver VM (#29).

Rough total for waves 1-3: about 7,000 lines of C, most of it glue around code
that already exists.

### Owned elsewhere

- **Security and system services gaps**: TLS, SNTP, crash/recovery, disk
  encryption, A/B, firewall, WireGuard, IPv6, find-my-device, sandboxing.
  Rows 8, 19, 21, 23, 27 coordinate with it.
- **GPU-first architecture**: multi-monitor, vsync, acceleration (past GOP).
- **Every legacy language plus AI twin**: new language runtimes (row 20 only
  moves the existing ones into user space).
- **Your long message**: the repo-wide docs-vs-code gap map.
- **Native GUI and display**, **Hardware drivers build**, **Paraconsistent
  terminal and OS gaps**: zui, class drivers and zxsh respectively.

## 6. Corrections to existing docs

- `TURNKEY_OS_GAPS.md` row 16: arm64 also loads separate signed ELF apps from
  ZXVFS; only boot test programs and the shell are built in. x86 cannot load
  a user binary (`elf.c` accepts AArch64 only).
- `TURNKEY_OS_GAPS.md` row 1: `kernel/grub.cfg` is the legacy i386 Multiboot
  path, not the UEFI boot.
- `OS_FEATURE_AUDIT` gap 1: TLS now verifies certificates, but nothing on bare
  metal calls it and there is no clock, so it would fail closed.
- `OS_FEATURE_AUDIT`: seats, hwkey and timesvc (with SNTP) are on main, not
  branches; none is linked into a bare-metal image.
- `BAREMETAL.md`: x86 does open internal disks now (NVMe/AHCI read the GPT and
  mount the ESP read-only); it still never writes. The x86 "heartbeat" never
  runs, because interrupts are never enabled.
- `ARCHITECTURE.md:212` ("syscalls, scheduler, memory ✅") overstates it:
  partial on arm64 only.
