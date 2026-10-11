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
