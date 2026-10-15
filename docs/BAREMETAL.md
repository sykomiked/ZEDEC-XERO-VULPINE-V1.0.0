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
bash build_system/uefi_boot_check.sh arm64  "" 90 "[AI_SELFCHECK_OK]" "[AI_NO_MODEL]"
bash build_system/uefi_boot_check.sh x86_64 "" 90 "[AI_SELFCHECK_OK]" "[AI_NO_MODEL]"
```

Runs inside the container (OVMF / QEMU_EFI) or on a Mac with Homebrew QEMU
(edk2 firmware). The firmware loads the EFI app from the disc itself; QEMU uses
`snapshot=on`, so the image file is never modified. It exits non-zero unless the
serial log reaches `[BOOT_OK]` and the markers given, with no fault marker. CI
runs the same check in the `uefi-disc` job. A plain disc carries no model:
`[AI_SELFCHECK_OK]` is the swarm and tensor arithmetic self-check, and
`[AI_NO_MODEL]` says the AI is not running. `[AI_OK]` appears only on a disc
whose model answered (next section).

## Golden disc: models and assets on the disc

The golden disc is the same disc with everything on it, so nothing is
downloaded: the AI models in `build_system/golden.models` (Qwen2.5-1.5B-Instruct
and Qwen2.5-0.5B-Instruct, Q4_K_M, both Apache-2.0) and the asset folders in
`build_system/golden.contents` (licences, music, and fonts, boot sounds, brand
and app icons once those land in `assets/`).

```bash
docker run --rm -v "$PWD:/src" -w /src zxv-baremetal bash -c \
  'bash build_system/baremetal_images.sh x86_64 && bash build_system/mk_golden_disc.sh x86_64'
ZXV_UEFI_MEM=3G bash build_system/uefi_boot_check.sh x86_64 dist/discs/zxv-x86_64-uefi-golden.img 1200 \
  "[MODELS_OK] 2 verified" "[AI_OK] qwen2.5-0.5b-instruct-q4_k_m.gguf answered"
```

Each model is downloaded once from its pinned URL into `dist/models` (or
`ZXV_MODEL_CACHE`) and refused unless its size and SHA-256 match the list.
On the disc: `/ZXV/MODELS/*.gguf`, `/ZXV/MODELS.SHA` (sha256, size, name),
`/ZXV/MANIFEST.SHA256` over every file (`sha256sum -c` from the mounted disc)
and `/ZXV/README.TXT`. Output: `dist/discs/zxv-<arch>-uefi-golden.img` and a
size breakdown in `.sizes`.

On every boot the EFI loader re-hashes each model before it starts the kernel
(`kernel/arch/efi_models.h`) and prints `[MODEL_OK]` per model and
`[MODELS_OK] 2 verified`, or `[MODEL_BAD]` and `[MODELS_FAIL]`; a bad model is
reported, it does not stop the boot. A plain disc has no `MODELS.SHA` and
prints nothing. Under QEMU emulation the check takes about 36 s for 1.6 GB; on
real hardware it is bounded by the disk's read speed.

### The kernel runs the boot model: what `[AI_OK]` means

The smallest model on the disc (Qwen2.5-0.5B-Instruct) is the boot model. The
loader reads it into its own pages, hashes it there, and when it verifies
allocates a scratch region as large as the model and leaves a record for the
kernel (`kernel/arch/zxv_aimodel.h`, `[AI_HANDOFF]`). The kernel
(`kernel/src/swarm/swarm_bootmodel.c`) checks that both regions are inside
the memory it maps and outside its own image, hashes the model a second time
and joins the two digests (`lpres_kjoin`: they must agree), loads it into the
integer engine (`zt_model`, `zt_tok`) and answers a fixed prompt greedily:

```
[AI_MODEL] qwen2.5-0.5b-instruct-q4_k_m.gguf 491400032 bytes, kernel sha256 74a4da8c9fdbcd15 agrees with the loader
[AI_OK] qwen2.5-0.5b-instruct-q4_k_m.gguf answered "Hello! It's nice to meet you!" (32 prompt + 9 tokens, digest 0x7af4f2fc)
```

Both lines are from QEMU boots of the golden disc on x86_64 (OVMF) and arm64
(QEMU_EFI) with 2 GB of guest RAM, 2026-10-15; a host run of the same file
gives the same answer and digest. Under emulation the answer takes a few
minutes; with hardware virtualisation or on a real machine, seconds. Any
refusal prints `[AI_FAULT] <name> <why>` and the boot goes on; a disc without
a model prints `[AI_NO_MODEL]`. Neither prints `[AI_OK]`.

`build_system/test_golden_disc.sh <arch>` proves the whole path in about two
minutes with a tiny generated model (`build_system/mk_boot_test_model.py`,
120 KB, a real qwen2 GGUF with random weights, so its answer is noise): intact,
it must verify, be handed over and answer with the token digest
`test_swarm_bootmodel` pins on the host; with one byte changed on the disc, it
must be caught and the kernel must say `[AI_NO_MODEL]`. CI runs it in the
`uefi-disc` job.

### Log in and ask (arm64 console)

After boot the EL0 shell (`P-TERM/EL0`, prompt `> `) takes a passport login
and questions for the boot model:

```
> login <seed text>        [LOGIN_OK] did:zxv:...      (or [LOGIN_FAIL] <why>)
> whoami                   [WHOAMI] did:zxv:...        (or nobody)
> ask <question>           [AI_ANSWER] "<text>" (<p> prompt + <g> tokens, digest 0x...)
> logout                   [LOGOUT] did:zxv:...
> create                   [NEW_SEED] <seed text>      (first time: write it down)
> confirm <seed text>      [CREATED] did:zxv:..., [LOGIN_OK] did:zxv:...
```

A first-time person makes a passport from nothing, with no server: `create`
takes 32 bytes from the CPU's random source (RNDR; a CPU without one refuses
rather than make a weak seed), shows the seed text once, and nobody is logged
in until `confirm` types back that same seed.

`login` rebuilds the whole identity from the seed text at the matrix level
(`kernel/src/passport/pp_console.h`, ML-DSA-87 + SLH-DSA-256s); the shell
echoes typed seeds (`login`, `confirm`) as `*`, and both the shell's and the kernel's copies of the
line are wiped after it runs. `ask` is refused with `[AI_REFUSED]` until
someone is logged in, takes up to 200 bytes, and treats a `<` in the question
as text, never as a chat-template token (`swarm_bootmodel_ask`). Passphrase
login is not offered here: Argon2id needs a 64 MiB work area the kernel does
not set aside.

`build_system/golden_login_check.sh` boots the tiny-model disc and types the
whole session over the serial line: refused before login, a one-character
typo refused by the checksum, logged in as the DID the host's
`test_pp_console` builds from the same seed, the boot question answered with
the digest `test_swarm_bootmodel` pins, logged out and refused again, then a new passport created, a
wrong confirmation refused and the right one logged in, with no typed seed
in the log (QEMU `-cpu max`, for RNDR). CI runs it in the `uefi-disc` job. x86_64 has no

The same check then stresses it: hostile login lines (empty, overlong,
punctuation, chat-template markup, a seed one character short or long), every
fourth seed position mistyped, five logins and ten asks in a row (the same
digest every time), the plain disc with no model (login works, `ask` says
there is no model), 256 MB of RAM (the boot reaches the shell and says why
the AI is off) and 128 MB.

**RAM floor.** The arm64 kernel's image, BSS and stack run from 0x40000000 to
`__kernel_end` (about 235 MB). The loader checks the firmware's memory map
covers that window before anything else; when it does not it prints
`[BOOT_FAIL] this kernel needs RAM from 0x40000000 to ...` and stops, where it
used to hand off and hang without a word. It then reserves the window, so the
boot model and its scratch are placed above it. The x86_64 loader already
refuses a window it cannot own (`[BOOT_REFUSED]`) and returns to the firmware.
console input yet, so its disc answers at boot but takes no login.

Measured 2026-10-12: 1,632,632,832 bytes raw (1.63 GB) on both arches,
1,569,507,209 bytes gzipped (x86_64). The models are 1,608,720,768 bytes of
that. The kernel runs the 0.5B boot model at boot (above); the 1.5B is checked
and carried but not yet run on bare metal.

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
   `[AI_SELFCHECK_OK] SWARM+TENSOR SELFCHECK ...` (and, on a golden disc,
   `[AI_OK] ... ANSWERED "..."`) and
   `[BOOT_OK] PHASE E0082 COMPLETE`, then the event cycle running. The same
   text goes to COM1 at 38400 baud if the PC has a serial port.

If the screen stays black after the firmware logo, the firmware offered no
32-bit linear framebuffer (GOP); the log is still on COM1. A photo of the
screen at the point it stops is enough to debug from.

## Later: arm64 hardware

The arm64 disc boots on UEFI arm64 machines (QEMU virt today). A Raspberry Pi
needs a UEFI firmware layer on its SD card first plus Pi-specific UART and
interrupt-controller support in the kernel, so it comes after the PC boot.
