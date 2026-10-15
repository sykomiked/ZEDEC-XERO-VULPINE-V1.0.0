# Bare-metal UEFI discs

ZXV can boot on its own as a separate, opt-in disc image. It never replaces or
modifies the host OS: the build only writes files under `dist/discs/`.

## Build (Mac or Linux, needs Docker)

```bash
docker build -f build_system/Dockerfile.baremetal -t zxv-baremetal .
docker run --rm -v "$PWD:/src" -w /src zxv-baremetal bash build_system/baremetal_images.sh
```

Output: `dist/discs/zxv-arm64-uefi.img`, `dist/discs/zxv-x86_64-uefi.img`
(GPT disk, one FAT32 ESP holding `/EFI/BOOT/BOOTAA64.EFI` or `BOOTX64.EFI`
plus `/MEGAROM.TVL`) and `SHA256SUMS`. The build fails if the swarm and tensor
engine (`swarm_boot_selfcheck`, `zt_boot_selfcheck`) are not linked in.

## Boot check

```bash
bash build_system/uefi_boot_check.sh arm64  "" 90 "[AI_OK]"
bash build_system/uefi_boot_check.sh x86_64 "" 90 "[AI_OK]"
```

Runs inside the container (OVMF / QEMU_EFI) or on a Mac with Homebrew QEMU
(edk2 firmware). The firmware loads the EFI app from the disc itself; QEMU uses
`snapshot=on`, so the image file is never modified. It exits non-zero unless the
serial log reaches `[BOOT_OK]` and `[AI_OK]` with no fault marker. CI runs the
same check in the `uefi-disc` job.

## Golden discs: models and assets on the discs

The golden discs carry everything, so nothing is downloaded: the AI models in
`build_system/golden.models` (Qwen2.5-1.5B-Instruct and Qwen2.5-0.5B-Instruct,
Q4_K_M, both Apache-2.0) and the asset folders in `build_system/golden.contents`
(licences, music, and fonts, boot sounds, brand and app icons once those land
in `assets/`). Machine code and model weights are kept apart, the way Debian
keeps architecture packages apart from `Architecture: all` ones:

| Disc | Architecture | Holds |
|---|---|---|
| `zxv-<arch>-disc1-core.img` | x86_64 or arm64 | the loader, the kernel, the assets and the core models (`build_system/golden.core`) on one FAT32 EFI System Partition |
| `zxv-swarm-disc<N>-<group>.img` | all | one model group (`build_system/golden.swarm`) on one FAT32 partition named `ZXV_SWARM_<GROUP>`, GPT type `5A585653-5741-4D53-9000-000000000001` |

A kernel fix rebuilds only the small core discs; the swarm discs keep their
SHA-256 sums and serve every architecture. Today the core is Qwen2.5-0.5B
(core disc 546 MB) and swarm disc 2, `reason`, holds Qwen2.5-1.5B (1,139 MB).

```bash
docker run --rm -v "$PWD:/src" -w /src zxv-baremetal bash -c \
  'bash build_system/baremetal_images.sh x86_64 && bash build_system/mk_golden_disc.sh x86_64 && bash build_system/mk_golden_disc.sh swarm'
UEFI_DISK=ahci UEFI_EXTRA=dist/discs/zxv-swarm-disc2-reason.img UEFI_MEM=4G \
  bash build_system/uefi_boot_check.sh x86_64 dist/discs/zxv-x86_64-disc1-core.img 420 "[AI_OK]" "[MODELS_OK] 2 verified"
```

Each model is downloaded once from its pinned URL into `dist/models` (or
`ZXV_MODEL_CACHE`) and refused unless its size and SHA-256 match the list.
Every model is on exactly one disc. Each disc has its own `MODELS.SHA` (one
line per model: sha256, size, name, `esp` or `swarm`, and the file's 8.3
name, `MDL000.GGU` in `golden.models` order, since the kernel's FAT32 driver
reads short names only), its own `MANIFEST.SHA256` for `sha256sum -c` from
another computer, and a `README.TXT`. The build fails if any disc would pass
1.8 GB (so each fits a DVD-R or a small stick; split a group that grows past
it) or any file is 3.8 GiB or larger (FAT32 stops at 4 GiB per file). The
discs are reproducible: the same commit and models give the same bytes.

**x86_64.** The loader does not read the models. After ExitBootServices the
kernel finds the core disc and every attached `ZXV_SWARM_*` disc with its own
AHCI or NVMe driver, reads their manifests and prints `[SWARM_MOUNTED]
ZXV_SWARM_REASON on sata1: 1 model` and `[MODELS_INDEXED] 2 listed (1 on the
boot partition, 1 on 1 swarm disc)`. A swarm disc with a bad manifest drops
only its own models. Each model is hashed when something first asks for it,
and the rest in the idle loop after `[BOOT_OK]`, 1 MiB per pass with the timer
still running (`kernel/src/hwdrv/hw_models.h`). It prints `[MODEL_OK]` per
model and `[MODELS_OK] 2 verified`, or `[MODEL_BAD]` and `[MODELS_FAIL]`; a bad
model is reported, it does not stop the OS. Under QEMU emulation (TCG) both
models were verified about 85 s after `[BOOT_OK]`, with 4 GB and with 8 GB of
RAM. The kernel's drivers do not cover virtio, so boot golden discs with
`UEFI_DISK=ahci` or `nvme`.

**arm64.** Its kernel cannot reach a disc yet (no PCI storage driver), so the
loader checks the core disc's models before ExitBootServices, streamed 4 MiB
at a time (`kernel/arch/efi_models.h`), and prints `[MODELS_OK] 1 verified`.

`build_system/test_golden_disc.sh <arch>` proves the check in about two
minutes per arch with small test files as the models, one on a core disc and
one on a swarm disc: intact, they must verify; with one byte changed on a
disc, it must be caught. CI runs it in the `uefi-disc` job. The Golden disc
workflow builds the real discs and boots them with 4 GB and with 8 GB of RAM.

The kernel does not yet run the discs' models on bare metal: it checks them,
and runs its built-in swarm and tensor self-check (`[AI_OK]`). A riscv64 core
disc needs a riscv64 UEFI loader first; the riscv64 kernel boots through
OpenSBI only.

## Run it on any OS (Docker only)

```bash
bash build_system/run_baremetal.sh x86_64 --screen
bash build_system/run_baremetal.sh arm64 --screen
```

Builds the toolchain image and the disc on first use, then boots the disc in
QEMU inside the container. The terminal is the serial console (Ctrl-A then X
quits); `--screen` serves the virtual monitor at `vnc://127.0.0.1:5900`. On
Windows run it from Git Bash or WSL with Docker Desktop. The same commands
without the script:

```bash
docker build -f build_system/Dockerfile.baremetal -t zxv-baremetal .
docker run --rm -v "$PWD:/src" -w /src zxv-baremetal bash build_system/baremetal_images.sh x86_64
docker run --rm -it -v "$PWD:/src:ro" -w /src zxv-baremetal qemu-system-x86_64 -machine q35 -m 1G -bios /usr/share/ovmf/OVMF.fd -display none -net none -drive if=none,id=disc,format=raw,snapshot=on,file=dist/discs/zxv-x86_64-uefi.img -device virtio-blk-pci,drive=disc,bootindex=0 -serial mon:stdio
```

## First physical boot: an old x86-64 UEFI PC

The x86_64 disc is the first hardware target: almost any 64-bit PC or laptop
from about 2012 onward boots it. It runs entirely from the USB stick. The
x86_64 boot path has no code that opens or writes an internal disk (the ATA
driver is compiled in but never initialized), so the PC's own OS is untouched;
unplug the stick and reboot to get it back.

You need: the PC, a USB stick of 64 MB or more **that will be erased**, and
`dist/discs/zxv-x86_64-uefi.img`.

1. Check the image: `shasum -a 256 dist/discs/zxv-x86_64-uefi.img` must match
   the line in `dist/discs/SHA256SUMS` (or the CI artifact).
2. Write it to the stick. Easiest on any OS: balenaEtcher, choose
   "Flash from file", pick the `.img`, then pick the USB stick and **check the
   drive name and size before pressing Flash**. On macOS by hand:
   ```bash
   diskutil list external
   ```
   find the stick (say `/dev/disk4`; confirm by its size), then
   ```bash
   diskutil unmountDisk /dev/disk4
   sudo dd if=dist/discs/zxv-x86_64-uefi.img of=/dev/rdisk4 bs=4m
   diskutil eject /dev/disk4
   ```
   A wrong disk number erases that disk. No script in this repo ever runs this
   step.
3. In the PC's firmware setup (usually F2 or Del at power-on): turn **Secure
   Boot off** (the image is not signed yet) and leave UEFI mode on (CSM/legacy
   is not needed).
4. Plug in the stick, power on, open the one-time boot menu (F12, F11, F8 or
   Esc depending on the maker) and pick the entry named "UEFI: <your stick>".
5. Expected: grey capital-letter boot log on screen, ending in
   `[AI_OK] SWARM+TENSOR SELFCHECK ...` and
   `[BOOT_OK] PHASE E0082 COMPLETE`, then the event cycle running. The same
   text goes to COM1 at 38400 baud if the PC has a serial port.

If the screen stays black after the firmware logo, the firmware offered no
32-bit linear framebuffer (GOP); the log is still on COM1. A photo of the
screen at the point it stops is enough to debug from.

## Later: arm64 hardware

The arm64 disc boots on UEFI arm64 machines (QEMU virt today). A Raspberry Pi
needs a UEFI firmware layer on its SD card first plus Pi-specific UART and
interrupt-controller support in the kernel, so it comes after the PC boot.
