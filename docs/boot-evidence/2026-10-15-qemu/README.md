# 2026-10-15: both UEFI discs under QEMU

- **Built from:** commit `7d93e2bd`, in this repository's history, with
  `bash build_system/baremetal_images.sh` (native toolchain).
- **Booted with:** QEMU 8.2.2 under UEFI firmware (OVMF for x86_64, QEMU_EFI
  for arm64). Each disc ran headless for 120 seconds with 512 MB of RAM;
  arm64 had a `ramfb` display.
- **Disc SHA-256 (native build):**
  - x86_64 `bdcfb809d5528b65c598f0a9411d714efaf2e33b18f83ce5a60c300029db7d1e`
  - arm64 `bf2fbfc118fe22a5d8297dd8b8b7f4c584a750e4c9142839ac4eb6db2f9b058f`

  A Docker build of the same commit gives different hashes, because the
  compilers differ. VERIFY_BOOT.md step 4 says how to compare builds.

## Markers

| Marker | x86_64 | arm64 |
|---|---|---|
| `[HANDOFF_OK]` handoff v2 accepted, kernel window verified free | pass | pass |
| `[BOOTMEM]` buffers from firmware-usable memory | pass (8 ranges, 500 MiB) | pass (3 ranges, 440 MiB) |
| `[SCHED_OK]` ring-3 processes under the preemptive scheduler | pass | not built on arm64 |
| `[AI_OK]` swarm and tensor self-check | pass | pass |
| `All systems online` | pass | pass |
| `[BOOT_OK]` main loop reached, no fault | pass | pass |
| `heartbeat: ticks=` timer interrupts in the idle loop | pass (11 beats) | not built on arm64 |
| fault markers (`[FAULT]`, `PANIC`, exception) | none | none |

## Files

| File | What it shows |
|---|---|
| [x86_64-serial.log](x86_64-serial.log) | full x86_64 serial console |
| [x86_64-1-boot-log.png](x86_64-1-boot-log.png) | the boot log on screen, 8 s in |
| [x86_64-2-heartbeat.png](x86_64-2-heartbeat.png) | the end of boot and the heartbeat lines, 110 s in |
| [arm64-serial.log](arm64-serial.log) | full arm64 serial console |
| [arm64-1-firmware.png](arm64-1-firmware.png) | UEFI firmware starting the disc, 10 s in |
| [arm64-2-desktop.png](arm64-2-desktop.png) | the desktop on the firmware framebuffer, 25 s in |
| [arm64-3-desktop-running.png](arm64-3-desktop-running.png) | the desktop still running, 110 s in |

The x86_64 disc shows its boot log on screen and has no desktop yet. Linking
the desktop session into both discs is the next gap
([BAREMETAL_OS_GAPS.md](../../BAREMETAL_OS_GAPS.md), row 12).
