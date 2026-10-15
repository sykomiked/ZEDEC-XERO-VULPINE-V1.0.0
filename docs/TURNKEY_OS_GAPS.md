<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# What ZXV needs to be a full turnkey OS

This list (2026-10-12) compares ZXV with what macOS, Windows and a desktop
Linux ship out of the box. Status is one of **exists**, **partial**, **stub**
or **missing**. "Partial" covers a lot of ground here. Most kernel modules are
freestanding C with host tests, but the only verified boot is QEMU
(`BOOT_LOG_E0082.md`, `kernel/arch/arm64/board_profile.h`: only
`BOARD_QEMU_VIRT` is verified). Until ZXV boots on physical machines, nothing
below that touches hardware counts as "exists".

**See also `OS_FEATURE_AUDIT_2026-10-12.md`.** It corrects some rows below
(notably row 5: `kernel/src/hwdrv` is not in any pushed ref as of
2026-10-12 10:40Z) and covers about 50 more areas (thermal, sleep, TLS
certificate checks, disk encryption, search, printing and others).

| # | Area | macOS / Windows / Linux have | ZXV today | Evidence | Next step |
|---|---|---|---|---|---|
| 1 | Boot | firmware boot on real machines, secure boot | partial | arm64 + x86_64 EFI stubs (`kernel/arch/*/efi_stub.c`), `kernel/grub.cfg`; QEMU only | boot one physical machine (bare-metal thread, PR #25 line) |
| 2 | Installer | partition, format, install, first-run setup | partial | `install/install_zxv.py`, `install/transactional_disk.py` (host Python, copy + rollback journal) | on-device installer with partitioning, then first-run setup |
| 3 | Users, accounts, login | multi-user, login screen, per-user homes | partial | `kernel/src/ident/` (argon2, passkeys, Shamir recovery, `id_rp_login`); no uid model or login UI | uid/session model on top of ident, login screen |
| 4 | Filesystem | journaled FS, permissions, ACLs | partial | `zxvfs/zxvfs.c` (journaled, tests + fuzz), `fat32/`, `vfs/`; no permissions | owner/mode/ACL in zxvfs, fsck |
| 5 | Drivers | USB, GPU, storage, Wi-Fi, BT, audio for real chips | partial (QEMU) | virtio blk/net/input/snd/gpu; standard class drivers in `kernel/src/hwdrv` (NVMe, AHCI, xHCI + USB keyboard and mass storage, e1000/e1000e, igb, r8169, igc, HD Audio), all but r8169 and igc passing in QEMU (those two: host tests only), none yet on real hardware; Wi-Fi/BT are protocol logic only | USB CDC/audio/UVC, BT HCI, interrupts, IOMMU, then the Linux driver VM |
| 6 | Networking | TCP/IP, DHCP, DNS, TLS, Wi-Fi | partial (strongest area) | `net/tcp.c`, `dhcp.c`, `dns.c`, `src/tls/` all tested; virtio-net | IPv6, NTP, real NIC drivers |
| 7 | Packages and updates | package manager, signed OTA, A/B rollback | partial | `update/` (signed content-addressed updates, ML-DSA), `zxpkg/`; app-level A/B with probation and auto-rollback in `loader/abupdate.c` (tested); hwemu `hwr` has A/B slots for hardware images | system-image A/B slots + apply-on-device |
| 8 | Security and sandboxing | app sandbox, MAC, secure boot chain, disk encryption | partial | `ZSC_CAP_*` syscall caps, EL0 per-process page tables + W^X (`el0_userspace.c`), x86 `ring3.c`; no secure boot, no disk encryption | measured boot, encrypted zxvfs volumes |
| 9 | Accessibility | screen reader, zoom, contrast, keyboard navigation | partial (branches) | widget roles/names, `zui_a11y_text`, Tab focus (`sfklol`); reduce-motion and WCAG contrast check (`98r3n6`); captions/read-aloud in `speech/`; no screen reader, zoom or colour filters | screen reader walking the zui tree with `speech/`, magnifier, high contrast theme |
| 10 | Languages (i18n) | Unicode, fonts, input methods | partial (strong data) | `i18n/` CLDR 48.2 tables + tests, `font/` TrueType + shaping | input methods (IME) |
| 11 | Backups and snapshots | Time Machine / File History / snapshots | partial | `fs/fs.c` snapshots (untested), `tripartite_fs/` | user-facing backup + restore, tested |
| 12 | Power | power use per app, battery, sleep, CPU frequency | partial | **power tracking built 2026-10-12** (`kernel/src/power`, `docs/POWER.md`): energy per app and per agent, battery health and time left, budgets, on Linux/macOS/Windows hosts, Android/iOS and x86 RAPL; `acpi/acpi.c` still parses tables only | zxvd agent wiring + Brazier app; ACPI AML for battery, sleep states, thermal zones, cpufreq |
| 13 | Printing | print to local and network printers | **missing** | nothing | IPP Everywhere client (driverless printing) |
| 14 | Display server and windows | compositor, window manager, multi-app | partial | `desktop/zxv_shell.c` framebuffer compositor + dock (untested); hosted app uses a browser window | window protocol between apps and the compositor |
| 15 | Shell and terminal | Terminal + bash/zsh/PowerShell | partial, three unconnected shells | `kernel_shell_exec` (arm64), `pterm/`, `desktop/zxv_shell.c` | **zxsh (this PR)**: bash-familiar syntax, line editing, sub-terminals (`split`, `tab`, `together`); later make pterm call zxsh's core |
| 16 | Processes | load programs from disk, scheduler | partial | `sched/`, EL0 preemptive scheduler; processes are built into the kernel (`user_procs.c`) | ELF loading from zxvfs, spawn/wait syscalls |
| 17 | Time | RTC, NTP sync, time zones | partial | `clock/event_clock.c` (tested); no RTC driver or NTP | NTP client, RTC drivers |
| 18 | System log | unified journal | partial | `chronicle/`, hash-chained logs in lpres_hex / paradox / hwa | one system journal service with a viewer |
| 19 | Settings app | System Settings / Control Panel | stub | settings panel mentioned in `desktop/desktop.h`; panels in the hosted window | owned by the Built-in apps thread |
| 20 | App store | store with install flow | partial (model only) | `community_chest/`, `market/` storefront model; no install path through zxpkg | store -> zxpkg install |
| 21 | Browser | full web browser | partial | `browser/browser.c` (HTML tokenizer, layout, HTTP/1.1, tested); no JS/CSS | CSS, then a small JS engine; window view in the Built-in apps thread |
| 22 | Audio | mixer, devices, Bluetooth audio | partial | `audio/audio.c` (tested), `virtio_snd.c`, HD Audio (`kernel/src/hwdrv/hda.c`, bound at boot in QEMU) | USB audio; HDA on a physical codec |
| 23 | Virtualization and containers | Hypervisor/WSL/Docker | **missing** as an OS feature | `emu/` is retro-CPU emulation for ROM analysis | run Linux containers in a small VM next to ZXV (decided direction) |
| 24 | Crash reporting and recovery mode | crash reports, safe mode, recovery partition | **missing** | only fail-closed logs and update rollback | panic dump to disk, recovery boot entry |
| 25 | File manager | Finder / Explorer | stub | `LS` in the desktop shell | owned by the Built-in apps thread (Files) |
| 26 | Notifications | notification center | partial | `social/zx_notify.h` (do-not-disturb with breakthrough priority, tested), `zx_notify_host.c` | notification center UI |
| 27 | Search | Spotlight / Windows Search | **missing** | nothing | local indexer over zxvfs |
| 28 | Multi-device sync | iCloud / OneDrive | partial (adjacent) | `devmesh/`, `ipfs_node/`, `decent/` | file + settings sync on devmesh |

## Where paraconsistent logic changes the design

- **Shell:** commands return true / false / both / neither (zxsh, `tools/zxsh/`), with the commands programmers already know and sub-terminals whose verdicts combine.
- **Drivers:** the smart firmware adapter takes evidence from several sources
  and joins it with lpres. It binds a matched driver on true, falls back to a
  safe generic mode on both, and uses class-generic drivers only on neither.
  It cannot make truly unknown hardware work without a spec or driver; what it
  does is pick the right driver and fail safe.
- **Updates, installer, backups:** each step is a unit of work with a
  hexagram tag. A contradiction (two mirrors disagree on a hash) is held for
  a person rather than retried until it happens to pass.

## Drivers: the least code that covers the most hardware

The approach is to write drivers against **standards, not chips**, in three
layers.

1. **Standard class drivers** (about 20, each written once):
   - Boot and firmware: UEFI GOP framebuffer, ACPI.
   - PCI and storage: PCI enumeration, NVMe, AHCI.
   - USB: xHCI, plus the USB classes HID, mass storage, CDC (network and
     serial), audio and video (UVC).
   - Virtual hardware: virtio (already in the tree).
   - Audio: Intel HD Audio.
   - Bluetooth: Bluetooth HCI over USB.
   - Wired network: three NIC families, Intel e1000e/igc and Realtek r8169.
2. **The paraconsistent adapter** (`kernel/src/fwadapt`) picks among them and
   fails safe on conflicts or unknown devices.
3. **The long tail goes to a Linux driver VM.** Wi-Fi chips, GPU
   acceleration, fingerprint readers and vendor-specific devices are handed to
   a small Linux VM through the IOMMU (VT-d / AMD-Vi). ZXV sees each of them
   as a virtio device, so its existing virtio drivers serve them. This is the
   same VM as the earlier Docker-in-VM decision.

**Coverage estimate (inferred from market structure, not measured).**
- On x86 UEFI PCs from roughly the last ten years, layer 1 should give a
  working machine on about 80-90% of models: framebuffer display, storage,
  USB input and media, wired network and audio.
- Wi-Fi and GPU acceleration need layer 3 on nearly all of them.
- Layer 3 needs an IOMMU, which most recent PCs have.
- Apple Silicon Macs have no ACPI or standard PCI firmware interface, so ZXV
  stays hosted (zxvd + app) on them.

**Build order** (progress 2026-10-12, Hardware drivers build thread).
1. xHCI + USB HID + mass storage: done in QEMU.
2. NVMe and AHCI: done in QEMU.
3. GOP framebuffer: done (`efifb_con.c`).
4. HDA: done in QEMU.
5. e1000e and r8169: e1000/e1000e and igb done in QEMU; r8169 (with the
   RTL8125 2.5 GbE family) and igc (I225/I226 2.5 GbE) pass host tests
   only (QEMU models neither).
6. The driver VM: not started.
