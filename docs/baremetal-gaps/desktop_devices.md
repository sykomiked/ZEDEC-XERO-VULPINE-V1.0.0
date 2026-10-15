<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# Bare-metal desktop session and device/service stack: gap audit

Audited 2026-10-14 against origin/main `4bea0160` (after merge batches 1-3 and
PR #126; PRs #103, #104, #109, #110, #113, #115, #117 and #118 are in). This
covers **bare metal only** (UEFI disc, x86_64 and arm64), meaning what a person
sees and uses after the firmware hands over. The hosted app is out of scope.
Evidence is `file:line`. I ran `make test-hwdrv`, `make test-fwadapt` and
`make test-terms` in a scratch copy, and all passed.

## The headline: the parts exist, but the bare-metal session does not

On main, most of the consumer-OS user layer is real, freestanding and tested
on the host. It lives in `kernel/src/zui`: about 39,900 lines with roughly 25
test files. It includes a window manager (`zui_wm`), lock screen (`zui_lock`),
IME, CLDR keymaps, touchpad gestures, on-screen keyboard, notification center
(`zui_notice`), layered per-seat settings (`zui_settings`), privacy indicators
(`zui_privacy`), screen reader, capture and a GOP framebuffer present layer
(`zui_present_fb.c`). Other pieces are also there: `seats/` (login roster),
`terms/` (per-seat licence acceptance) and `audio/` (mixer).

**None of these are linked into either bare-metal image.** Checks:

- `grep zui|seats|terms|ident|fwadapt build_system/Makefile.{x86_64,arm64}`
  returns nothing.
- `audio/`, `acpi/`, `wifi/` and `bluetooth/` are only in `Makefile.arm64`
  (`:477`, `:480`), not in x86_64.

What a person actually sees today:

- **x86_64 (the first physical target):** a grey upper-case text log on the
  GOP framebuffer, `[BOOT_OK]`, then `while(1){ hlt(); }`
  (`kernel/arch/x86_64/kernel_main_x86_64.c:323-335`). There is no desktop and
  no input after the 2-second keyboard self-test. There is no interrupt
  controller or timer either: the only IDT is the ring-3 test's
  (`ring3.c:95,311`), and `pic`, `keyboard` and `mouse` are compiled
  (`Makefile.x86_64`) but never initialised. The image has no shutdown or
  reboot.
- **arm64:** the legacy `desktop/zxv_shell.c` "Dimensional Desktop" on GOP or
  ramfb (`kernel_main_arm64.c:2009-2045`). Input is virtio-input only
  (`:2047-2050`), so on real arm64 hardware there is no input at all.

So for this area, a consumer OS is mostly **integration plus a handful of
missing drivers**, not new UI.

## Corrections to the 2026-10-12 documents

| Doc says | Actually (main, today) | Evidence |
|---|---|---|
| TLS does not verify certificates (`OS_FEATURE_AUDIT` gap 1) | **Verifies.** `TLS_VERIFY_REQUIRED` checks the chain to a trust store (expiry, hostname, EKU, name constraints) plus CertificateVerify. It fails closed with `TLS_ERR_NO_VERIFIER` when there is no trust store or no time. Revocation is not checked. | `tls/handshake.h:4-30`, `handshake.c:281,330,360`, `tls/trust.c`, `trust_roots.h` |
| On bare metal TLS is therefore fine | **No caller anywhere** (`grep tls_client_init` finds only the definition). There is no time source on bare metal: no RTC driver, and SNTP is linked but never called (`grep sntp_ kernel/arch` finds nothing). A bare-metal TLS client would hit `NO_VERIFIER`. | as stated |
| Window manager, lock, IME, gestures, OSK, notices, privacy indicator: on branches | **On main**, tested on the host, not on bare metal | `kernel/src/zui/*.h` |
| Keyboard layouts: hardcoded US at `zxv_shell.c:1008` | Still true for the bare-metal shell (`zxv_shell.c:1009-1022`) and `usbkbd.c:34-42`. `zui_keymap` (CLDR model) exists but is not wired in. | as stated |
| `seats/` on branch `86si57` | On main (`seats/seats.h`), with per-seat licence acceptance in `terms/terms.h:165-193`. Neither is in an image. | as stated |
| HW drivers "done in QEMU" | Still true, with three caveats. The keyboard is polled only inside `hw_selftest_all` (`hwdrv_probe.c:242,309`); nothing consumes it afterwards. xHCI has no hot-plug: "ports are scanned, not event driven" (`xhci.c:263`). HDA plays one buffer at a time (`hda.c:15-18`). | as stated |
| Display "partial" | GOP mode is the firmware default. The stub reads `Mode->Info` only and never calls QueryMode or SetMode (`x86_64/efi_stub.c:98-110`). There is no EDID anywhere in the tree. Pixel formats `PixelBitMask` and `BltOnly` are rejected (`efi_stub.c` `PixFmt <= 1`), giving a black screen on such firmware. | as stated |

## Ranked gaps

Sizes: S is under 500 lines, M is 500-2,000, L is 2,000-6,000, XL is more.
"Sec" means the **Security and system services gaps** thread. "GPU" means the
**GPU-first architecture** thread.

| Rank | Gap | Status | Evidence | Size | Depends on | Owner |
|---|---|---|---|---|---|---|
| 1 | **x86_64 session spine**: interrupts and timer (LAPIC/IOAPIC via MADT, or a deadline-free poll loop first), one event loop, zui linked in | missing | `kernel_main_x86_64.c:329-335` hlt loop; no pic/APIC init; zui absent from both Makefiles | M | boot handoff (rank 3) for MADT | none (Coordinator) |
| 2 | **zui on bare metal**: link `zui_canvas/layout/widget/input/wm/present_fb/lock/settings/notice/privacy` into both images and retire the `zxv_shell` path | missing (code exists and is tested) | `zui_present.h:15` names the GOP backend; `kernel/Makefile:1197-1206` host tests only | M | 1 | UI thread |
| 3 | **Boot handoff carries only a framebuffer**: no RSDP, memory map, UEFI runtime services, EDID or GOP mode list | missing | `efifb_con.h:21-31`, `arm64/zxv_bootinfo.h:26-34` (`reserved[3/4]` unused) | S | none | none (Coordinator) |
| 4 | **No shutdown or reboot** on either arch (no ResetSystem, FADT reset register, 0xCF9, `_S5` or PSCI SYSTEM_OFF) | missing | grep for all of these in `kernel/arch` and `src/acpi` is empty | S (runtime services) / L (AML `_S5`) | 3 | Drivers |
| 5 | **Input on real PCs.** USB HID is the boot-protocol keyboard only. There is no HID report-descriptor parser, so no USB mouse, consumer keys or precision touchpad. No I2C-HID, which is how most laptop touchpads and keyboards attach. PS/2 is never initialised on x86. No xHCI hot-plug. The keymap is fixed to US. | partial | `usbkbd.c:6,126`; `xhci.c:263,457`; `keyboard.c`/`mouse.c` not called; `zui_keymap.h`, `zui_gesture.h` unwired | M (HID + PS/2 + hot-plug) / L (I2C-HID: DesignWare I2C, ACPI `_CRS`, GPIO interrupts) | 1, AML for I2C-HID | Drivers |
| 6 | **Login, lock and first-boot licence per seat** on screen, persisted | missing on bare metal; logic exists | `seats.h:3-28`, `zui_lock.h:4-30`, `terms.h:165-193`; x86 never writes a disk by design (`BAREMETAL.md` "First physical boot") | M | 2; a writable data store on the boot medium (decision) | UI + Coordinator |
| 7 | **Display mode selection**: EDID, GOP mode set before ExitBootServices, user mode | missing | no EDID; no SetMode; `display.c:75` negotiates from GOP geometry only; `views/display.h` settings panel exists | S-M | 3 | Drivers + UI; GPU for anything past GOP |
| 8 | **arm64 present path**: three full-frame copies per frame, and a **suspected stride mismatch**. The shell draws at `g_disp.w` stride (`kernel_main_arm64.c:2026`, `vbe.c:133`). The wallpaper (`pb_init(...ZXV_FB_W...)` at `:1991`) and the GOP blit (`:446-456`) read rows as 2560 wide. When GOP width is not 2560 the image should come out sheared or squeezed. Needs a screenshot to confirm. Replaced by zui damage-only present (P1). | bug (suspected) | lines cited | S | 2 | UI |
| 9 | **NIC to IP stack on x86**: hwdrv e1000/igb/r8169 NICs feed nothing (no `hw_nic` consumer outside `hwdrv/`). arm64 virtio-net does reach DHCP, DNS and TCP (`kernel_main_arm64.c:2525-2710`). | missing (x86) | grep `hw_nic` | S | 1 | Drivers |
| 10 | **Time on bare metal**: RTC (CMOS, PL031, or UEFI GetTime via runtime services) plus SNTP called. TLS verification, licence timestamps and lock backoff all need it. | missing | no RTC driver; `timesvc/sntp.c` linked, never called | S | 3, 9 | Sec |
| 11 | **Audio path**: HDA as a continuous ring (BDL cycling), then `audio_bind_ops` to the mixer, per-app gain, launch cues | partial | `hda.c:15-18` one-shot 64 KB; `audio.h:24-33` "NO DRIVER" boundary; `zui_wm.h:23-28,130-132` cue hook; `launch/zxl_launch.h` sequence | M | 1, 2 | Drivers + UI |
| 12 | **ACPI AML interpreter**: battery (`_BST`/`_BIX`), thermal (`_TMP`/`_PSV`/`_CRT`), lid, power button, `_S3`, `_BCM` backlight, I2C-HID `_CRS` | missing | `acpi/acpi.c` is 163 lines, tables only (MADT/HPET flags), arm64 only | L | 3 | Drivers |
| 13 | **Interrupts for devices** (MSI/MSI-X) and an **IOMMU**; DMA is unfenced | missing | `HW_DRIVERS.md` "Not done yet"; `xhci.c:981` "IE stays 0: polled" | M (MSI) / L (VT-d) | 1, 3 | Drivers |
| 14 | **Hypervisor and Linux driver VM** for Wi-Fi, GPU and the long tail (VMX/SVM with EPT, or EL2 with stage-2). arm64 drops EL2 without leaving a hyp stub. | missing | no vmxon/VMCS/vttbr in tree; `arm64/boot.s:15-51` drops to EL1; `efi_tramp.S:59` enters at EL1 | XL | 13 (IOMMU) | Drivers; GPU thread for GPU passthrough |
| 15 | **Per-app sandbox and device capabilities at runtime**: caps are 5 bits (write, proc, exec, ipc, fs). There is no camera, mic, net, display or input cap, no app loaded from disk, and no surface syscall for EL0 apps to draw into the compositor. | partial | `syscall.h:87-92`; `zui_privacy.h` exists with no driver bracketing | M-L | 2 | Sec (sandbox); UI (prompt) |
| 16 | **USB device authorisation** on plug | missing | none in `xhci.c` | S | 5 | Drivers + UI; Sec |
| 17 | **Secure boot chain**: image unsigned, and the docs tell people to turn Secure Boot off | missing | `BAREMETAL.md` step 3; `hwkey/` (soft) on main; `bootattest` not on main | M | none | Sec |
| 18 | **fwadapt not in the boot path**: `hwdrv_probe.c` matches with its own table, so BOTH or stale evidence never reaches a person | partial | `fwadapt.h:21-41`; no non-test caller of `fwa_init`/`fwa_decide` | S | none | Drivers |
| 19 | Notifications, settings, file manager, search on bare metal: zui_notice, zui_settings and Burrow exist, but there is no store to persist to; search is missing | partial / missing | `zui_notice.h`, `zui_settings.h`, `views/burrow.c`; no indexer | M (wire) + L (search) | 2, 6 | Apps + UI |
| 20 | Multi-monitor, vsync, GPU acceleration | missing (impossible on GOP: one output, no vblank) | `virtio_gpu.h:5-19` 2D scanout over MMIO only; compiled into arm64, never called | XL | GPU driver or driver VM | **GPU-first architecture** |
| 21 | Wi-Fi radio, Bluetooth HCI | logic only | `wifi/wifi.c` WPA2 handshake; `bluetooth/` host stack, no HCI transport | L (BT USB) / via 14 (Wi-Fi) | 5, 14 | Drivers |
| 22 | IPv6, firewall, VPN, disk encryption, crash dump and recovery | missing | see the OS feature audit | — | — | **Sec** (status only here) |

## Designs for the top gaps

Throughout this section, the shorthand works like this. **T/F/B/N** are the
lpres states (`lpres.h:39-45`). A **tag** is `lpx_t`. The **action** comes
from `lpx_action` (`lpres_hex.h:22-28`):

- FORBIDDEN gives ROLLBACK.
- T gives PROCEED, or WAIT if the evidence is entropic (weakening or stale).
- F gives REFUSE.
- B gives WAIT if negentropic (converging), otherwise ESCALATE.
- N gives WAIT.

I reuse `fwadapt`'s claim-and-join shape everywhere instead of inventing new
machinery.

### D1. Session spine and handoff (ranks 1-4): mostly convention, and that is right

**Handoff.** Extend both bootinfo records within their existing `reserved[]`
slots, so the layout is unchanged and the `check` field still covers the
original fields:

- `rsdp`: from `EFI_ACPI_20_TABLE_GUID` in `ConfigurationTable`.
- `mmap`: a copy of the final map below 1 MB.
- `rt`: the `RuntimeSvc` pointer.
- `edid`: `EFI_EDID_ACTIVE_PROTOCOL` read before ExitBootServices, falling
  back to `EDID_DISCOVERED`. This gives EDID **without a GPU driver**.

`efi_stub.c` already has these pointers in hand (`:46`, `:89-90`).

**Shutdown and reboot.** Use an ordered fallback chain, recording each step's
verdict as evidence:

1. `RT->ResetSystem`: map the `EfiRuntimeServicesCode/Data` ranges from the
   map, identity, no SetVirtualAddressMap.
2. FADT `RESET_REG` and the `_S5` package. A tiny `_S5` scanner (a byte
   pattern over the DSDT, which is what Linux's early code did before ACPICA)
   is about 60 lines, needs no interpreter, and gives power-off on most PCs.
3. `0xCF9`, then the 8042 `0xFE` pulse.
4. On arm64, PSCI `SYSTEM_OFF`/`SYSTEM_RESET` via `hvc`/`smc`, chosen by the
   FADT ARM boot flags.

Each attempt that returns is an F claim. If every path is F, the result is N,
and the screen says "safe to power off" instead of hanging silently.
**Convention is best here.** The paraconsistent part is only the honest final
state.

**Event loop.** A single cooperative loop: poll inputs, then the `zui`
queue, then `zui_wm`, then `zui_present_fb` with damage only. For a first
cut, keep it polled with no interrupts; the TSC is already calibrated
(`hwplat_x86.c`, `delay_us`). Add a LAPIC timer from the MADT afterwards for
idle `hlt` and power. This uses less code than finishing PIC, PIT and IDT
first, and it puts a usable desktop on hardware sooner.

### D2. Input arbitration (rank 5): evidence joins where hardware really lies

Each input *source* is an fwadapt device on bus USB, platform (PS/2) or I2C,
with claims from ACPI (FADT `IAPC_BOOT_ARCH` "8042 present" bit; PNP0303 or
PNP0F13 in the DSDT), the live probe (8042 self-test returns `0x55`), and USB
descriptors.

- **8042 presence.** FADT says no, but the probe answers: BOTH, so poll it
  without depending on it, and flag it. FADT yes and probe silent: F, so
  skip it. Real laptops misreport this bit both ways, so the join replaces a
  pile of DMI quirk tables.
- **Duplicate keystrokes from USB legacy emulation.** If BIOS handoff fails,
  one physical key arrives through both xHCI and the emulated 8042. Join per
  usage within an 8 ms window. Two sources reporting the same edge is
  corroboration (negentropic T): emit once, and record the duplicate source
  as an echo for the rest of the boot, so the dedupe becomes O(1). If the
  sources disagree on state (stuck key on one, release on the other), the
  result is BOTH: release the key (fail safe, no runaway repeat) and show it
  in input settings.
- **Secure input.** For the lock screen and secret fields, `zui_lock` only
  accepts events whose source tag is T and *physical* (USB/PS/2/I2C, or the
  system OSK). Synthetic input from apps, remote desktop or automation is N
  for secret fields and is refused there, while still working everywhere
  else. This is what Windows' secure attention sequence does, written as data
  instead of a special key.
- **Pointer from touchpad and mouse together.** Sum them, as every OS does.
  Convention is best.

Build order: (a) a generic HID report-descriptor parser (about 600 lines)
for keyboard, mouse, consumer page and the Windows Precision Touchpad
digitizer collection, feeding `zui_gesture`; (b) PS/2 init and runtime
polling; (c) xHCI port-status-change events driving re-enumeration; (d) a
`zui_keymap` layout chosen in the seat's settings (CLDR data is already in
`i18n/`); (e) I2C-HID last, because it needs AML `_CRS` (D5).

### D3. Display mode from three witnesses (ranks 7-8)

Each candidate mode gets one lpres value from three sources:

- **EDID**: preferred timing and the modes it lists. It must pass its
  checksum; a bad checksum gives N.
- **GOP**: `QueryMode` list, read before ExitBootServices.
- **The person**: the `display.mode` value in `zui_settings` (per seat),
  persisted as `ZXV/DISPLAY.CFG` on the ESP. The stub reads it, because the
  mode can only change before ExitBootServices.

The scanout budget (`display.h:90-95`) can only add F.

| EDID | GOP lists it | User | Result | Action |
|---|---|---|---|---|
| preferred | yes | none or same | T, negentropic | SetMode in the stub |
| preferred | no | none | B | stay on the GOP mode with matching aspect; flag in Display settings |
| none or bad | yes | asked for it | T, neutral | apply; `views/display.h` D2 keep-or-revert timer runs |
| any | yes | previously *reverted* | FORBIDDEN | never auto-apply that mode again (the qlog entry is the memory) |
| any | any | over budget | F | refused, not clamped (`display.h` rule) |

This costs one table in the stub, about 200 lines including an EDID
base-block parser.

Also fix the arm64 present path:

- Compose directly into one back buffer at the negotiated stride.
- Present damage rectangles straight into the GOP. Drop `g_scanout` and the
  scaling blit, and keep the holo effect as an opt-in pass over damage.

This removes two full-frame copies per frame and the suspected stride bug.

**Honest limit.** GOP has one output and no vblank, so there is no tearing
control, no multi-monitor and no brightness without a GPU driver or ACPI
`_BCM`. Those belong to the GPU-first thread and the driver VM.

### D4. Login, lock and licence per seat (rank 6)

Boot sequence:

1. Boot vault animation.
2. `seat_auto_enter`, or the login grid.
3. `terms_status(seat)` returns NEEDED or OUTDATED: the reader must reach
   the end (`TERMS_ERR_GATE`), then `terms_accept`.
4. The desktop, with `zui_lock` armed (auth callback is `seat_unlock`).

The paraconsistent angle is in **where the roster lives**. The x86 disc never
writes internal disks (`BAREMETAL.md`), so the roster and acceptance records
need a writable store on the boot medium: a `ZXVDATA` FAT32 or zxvfs
partition on the stick. Several copies can then disagree: the stick copy, a
copy on the installed disk later, and a devmesh peer. Join them per seat
record using the hash and a monotonic counter:

- One newer copy with a valid chain: T.
- Two different copies with the same counter: BOTH. Escalate to the **owner**
  seat; never auto-merge credentials.
- A broken hash chain: FORBIDDEN. Quarantine that copy and sign in from the
  other.

A guest seat's wipe on sign-out is a ROLLBACK phase transition
(manifest to dissolving to potential).

### D5. ACPI AML as evidence (rank 12)

Write a small AML interpreter, as an evaluator rather than a full ACPICA:
namespace, the main opcodes, OperationRegions for SystemIO, SystemMemory, PCI
config and EmbeddedControl. The design choice is that **every method result
is a claim, not a fact**:

- A method that faults (unknown opcode, loop budget exceeded, EC timeout)
  gives a FAULT claim. Fwadapt rule A4 then quarantines *that device's AML*
  for the boot. It never panics, and it never retries in a loop. This is the
  main reason to build our own: it degrades per device.
- **Battery**: `_BST` remaining capacity versus `_BIX` design capacity versus
  the power ledger's integrated drain (`power/power_ledger.h`, which already
  has "conflict detection between meters"). If they disagree, BOTH: show a
  range ("2-3 h") and ask to recalibrate. Never shut down on a BOTH reading
  unless it is under `_BIX` "critical" on two sources.
- **Thermal**: `_TMP` versus the CPU's digital thermal sensor
  (`IA32_THERM_STATUS`, which needs fault-safe `rdmsr`, called out in the
  audit). For `_CRT`, T (both over, or one over and rising) shuts down. B
  (one says critical, the other says 45 C) gives passive throttle and an
  alert, with no shutdown. N means the firmware has no thermal zone, so trust
  the CPU's own PROCHOT. This avoids both false shutdowns from a broken EC
  and silent overheating.
- **Sleep (S3 or s2idle)**: PROCEED only if every bound driver reports T for
  "can resume". Any N, such as a driver with no resume path, gives REFUSE
  sleep: the screen locks and the machine stays on. Convention (Linux)
  suspends anyway and hangs. Here a refusal is correct.

### D6. Capability prompts (ranks 15-16), sketched only because Sec owns sandboxing

Inputs: policy (the zxapp manifest plus owner or MDM locks, using
`zui_settings` S4 locks), the person's stored grant, context (seat locked?
app in front?) and hardware (privacy switch or jack state). The join:

- Policy F: REFUSE with no prompt. Do not ask people for what they cannot
  grant.
- Policy T and grant T and context T: PROCEED. `zui_privacy_begin` runs, and
  the indicator cannot be hidden (PV4).
- Policy T and grant N: ESCALATE (the prompt).
- Grant T but context F (a background app, or the seat is locked): BOTH.
  Re-prompt when the app is next in front. A stale "always" grant (unused for
  N days) is entropic, so it ESCALATEs again.
- Hardware switch off while the grant is T: BOTH. The indicator shows
  "blocked by switch", and the app gets silence, not an error.

**USB authorisation** works the same way. A device's interface set is a
claim keyed by VID:PID and serial against the last sighting. A known
storage device that now also presents HID (BadUSB) gives identity BOTH:
SAFE_GENERIC binds storage read-only, and HID binding waits for confirmation
from an already-trusted keyboard. Any new HID while the seat is locked gives
N, which is WAIT.

### D7. Audio (rank 11): convention plus one evidence point

Turn HDA into a cycling 2-entry BDL ring that is refilled from
`audio_mixer_process`, and register it through `audio_bind_ops`
(`audio.h` boundary). Per-app volume is the per-stream gain that already
exists. Launch cues are the `zui_wm.cue` callback into the mixer.

The one evidence point is output routing. Pin sense (`_PD` jack detect)
versus the `_DCB` default config versus the person's choice gives a join per
output. Headphones sensed but the codec reporting no headphone pin is BOTH:
keep the speakers muted and ask, rather than blasting the speaker. Capture
(mic) stays off until D6 grants it.

### D8. Driver VM (rank 14): XL, after IOMMU

Run a minimal type-1 loop: VMX with EPT on x86 (SVM later), and on arm64
**stay at EL2** (install a hyp stub before `efi_tramp`'s drop, so the option
is not lost). The guest is a stock Linux with Wi-Fi and GPU drivers that
exposes virtio-net and virtio-gpu back to ZXV through shared vrings
(`virtio/vring.c` exists).

Paraconsistent angle: the guest's device claims are one fwadapt source and
ZXV's own PCI probe is another. A guest crash or watchdog miss is FAULT,
which leads to ROLLBACK: reset the IOMMU group, restart the VM from the
signed image, and quarantine after three tries. Wi-Fi depends on this. GPU
acceleration is the GPU-first thread's decision (native driver versus
passthrough).

## What I would build first, in order

1. **Handoff plus power-off (S, about 400 lines).** Add `rsdp`, `mmap`,
   `rt` and `edid` to bootinfo, map runtime services, and add ResetSystem
   with the `_S5`, 0xCF9 and PSCI fallbacks. Test: a QEMU check that
   `[POWER_OFF]` exits QEMU with status 0.
2. **x86 and arm64 session spine with zui (M, about 1,500 lines of glue).**
   Link zui core, `zui_wm` and `zui_present_fb` into both images. Poll loop:
   usbkbd plus PS/2, then the zui queue, `zui_wm`, and a GOP damage present.
   Fix or replace the arm64 present path (rank 8). Test: extend
   `hw_drivers_check.sh` to type into a zui text field and screenshot-hash
   the GOP through the QEMU monitor.
3. **Generic HID, PS/2 runtime and xHCI hot-plug (M).** Mouse and touchpad
   reach `zui_gesture`; the keymap comes from settings. Wire `fwadapt`
   between `hw_pci_scan` and bind (rank 18) as part of this, so input
   arbitration (D2) uses it.
4. **Seats, terms and lock on screen, with a `ZXVDATA` partition on the
   stick (M).** This gives a real first-boot flow per person.
5. **x86 NIC to `net/`, plus RTC/UEFI GetTime and SNTP (S).** This unblocks
   verified TLS. Coordinate with Sec.
6. **HDA ring to the mixer, and launch cues (M).**
7. **AML evaluator (L)**, then battery, thermal, lid and sleep, then
   I2C-HID touchpads.
8. MSI-X and VT-d, then the driver VM (XL), which brings Wi-Fi.

Steps 1-4 turn the golden disc from "boot log, then halt" into a
login-to-desktop session on a real PC with keyboard, mouse, power-off and
per-seat licence acceptance. Almost all of that code already exists.
