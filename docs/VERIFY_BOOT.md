# Verify the boot yourself

You don't have to take the project's word that the bare-metal disc boots. This
page shows you how to build the disc from source and boot it on your own
computer. Then you can read what it prints and compare the disc's fingerprint
with the one CI got. It takes one command and about ten to fifteen minutes the
first time.

Everything runs in QEMU, a machine emulator. Nothing is written to a real disk:
QEMU boots a throwaway copy of the disc image, and the image file itself is
never changed.

## 1. Get the code

```bash
git clone https://github.com/sykomiked/ZEDEC-XERO-VULPINE-V1.0.0.git
cd ZEDEC-XERO-VULPINE-V1.0.0
```

## 2. Install the tools

**Easiest, on any OS: Docker only.** Install
[Docker Desktop](https://www.docker.com/products/docker-desktop/) (macOS,
Windows) or Docker Engine (Linux). The compilers, QEMU and the UEFI firmware
all live inside the container. On Windows, run the commands below from WSL or
Git Bash. A disc built this way matches CI's byte for byte (step 4).

**Or natively.**

Ubuntu or Debian on an x86_64 PC:

```bash
sudo apt-get install -y gcc make nasm python3 git mtools gdisk dosfstools \
  qemu-system-x86 qemu-system-arm ovmf qemu-efi-aarch64 \
  gcc-x86-64-linux-gnu binutils-x86-64-linux-gnu \
  gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu
```

macOS: the native cross compilers are awkward to install on a Mac, so use
Docker. Homebrew's `qemu` is enough to boot a disc you already have, with
`build_system/uefi_boot_check.sh`.

## 3. Run the check

```bash
bash build_system/verify_boot.sh            # both discs, x86_64 and arm64
bash build_system/verify_boot.sh x86_64     # one disc
bash build_system/verify_boot.sh --docker   # force Docker even if tools exist
```

For each disc, the script:

1. builds the kernel and the UEFI disc from your checkout,
2. prints the disc's SHA-256,
3. boots it headless under UEFI firmware (OVMF for x86_64, QEMU_EFI for
   arm64) for 150 seconds, the way a PC boots from a USB stick: the firmware
   finds the boot loader on the disc by itself,
4. checks the serial console for every marker below, and fails if any is
   missing or a fault marker appears.

It ends with `VERIFIED` or `NOT VERIFIED` for each disc. Everything it saw is
in `dist/verify/`:

| File | What it is |
|---|---|
| `REPORT.txt` | commit, disc hashes, each marker PASS or MISSING |
| `serial-<arch>.log` | the complete serial console output |
| `screen-<arch>.png` | the virtual screen a few seconds before the end |
| `check-<arch>.txt`, `build-<arch>.log` | the boot check and build output |

## What each marker proves

| Marker | Disc | What it proves |
|---|---|---|
| `[HANDOFF_OK]` | both | The boot loader passed the kernel the firmware's memory map, ACPI tables and clock in a sealed record, and the kernel accepted it. The loader also checked that the memory the kernel occupies was free. |
| `[BOOTMEM]` | both | The kernel's large buffers came from memory the firmware called usable. It never guessed at memory. |
| `[SCHED_OK]` | x86_64 | Several user programs ran in their own address spaces under the preemptive scheduler. The ones built to crash were ended alone, and the kernel carried on. |
| `[AI_OK]` | both | The built-in swarm and tensor engine passed their self-check (fixed inputs, known answer). |
| `All systems online` | both | Every subsystem's self-check passed. |
| `[BOOT_OK]` | both | The kernel reached its main loop with no fault. |
| `heartbeat: ticks=` | x86_64 | Timer interrupts keep arriving in the idle loop, every ten seconds of ticks. |

The check fails if any of these appear: `[FAULT]`, `[EL0 FAULT]`,
`CPU exception vector=`, `PANIC`, `kernel panic`. One line,
`[FAULT CONTAINED] arm64-io fail-stopped`, is expected on arm64. It comes from a
deliberate self-test showing that a failed component is stopped and the rest
keeps running.

What this does not prove: the disc has not been booted on a physical PC yet
(docs/BAREMETAL.md covers that first boot). This plain disc also carries no AI
model weights; the golden disc with models is `golden-disc.yml`.

## 4. Check the disc's fingerprint

The disc build is reproducible: the same commit gives the same bytes. Every
timestamp, volume serial and partition GUID is fixed. So:

- **Your build against CI's.** Run the
  [Verify boot workflow](https://github.com/sykomiked/ZEDEC-XERO-VULPINE-V1.0.0/actions/workflows/verify-boot.yml)
  on a commit (Actions tab, "Run workflow"), or open a past run. Its summary
  and `REPORT.txt` list each disc's SHA-256. Build the same commit with
  `--docker` and compare the `sha256` lines. Docker matters here. A native
  build uses your system's compiler and mtools, which can differ from CI's.
  The disc then still boots, but its hash can differ.
- **A disc you downloaded.** Check it against the hash published next to it:

  ```bash
  sha256sum -c zxv-x86_64-uefi.img.gz.sha256       # Linux
  shasum -a 256 -c zxv-x86_64-uefi.img.gz.sha256   # macOS
  ```

  A locally built disc is listed in `dist/discs/SHA256SUMS`.

## Run it in CI instead

The same check runs on GitHub with no setup. In the repository, open
**Actions**, choose **Verify boot**, press **Run workflow**, and pick a disc.
When it finishes, the run page shows the report. The `verify-boot-*` artifact
holds the serial logs, the screenshots and the hashes.

## Watch it boot interactively

```bash
bash build_system/run_baremetal.sh x86_64 --screen    # VNC on 127.0.0.1:5900
```

The terminal becomes the serial console (press Ctrl-A then X to quit). With
`--screen` you can watch the virtual monitor in any VNC viewer.

## If something fails

Please open an issue at
<https://github.com/sykomiked/ZEDEC-XERO-VULPINE-V1.0.0/issues> with:

- the first lines of `dist/verify/REPORT.txt` (commit and hashes),
- your OS, and whether you used Docker or native tools,
- `dist/verify/check-<arch>.txt` and `dist/verify/serial-<arch>.log`
  attached (or `build-<arch>.log` if the build failed),
- the screenshot, if there is one.

A missing marker together with the last lines of the serial log is usually
enough to find the cause.
