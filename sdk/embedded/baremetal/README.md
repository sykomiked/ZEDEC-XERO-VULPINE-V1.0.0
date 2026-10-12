<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# ZXV-BareMetal: boot packaging for board makers

This folder is for OEMs and hardware makers who want to ship the ZXV
kernel on their own machine. You describe the board once in a **board
manifest** (JSON); one command checks it and packages the kernel for it:

```sh
python3 sdk/embedded/baremetal/tools/zxv_board.py validate my-board.json
python3 sdk/embedded/baremetal/tools/zxv_board.py plan     my-board.json   # what would run
python3 sdk/embedded/baremetal/tools/zxv_board.py package  my-board.json   # build + collect
```

Kit version 0.1 (preview), manifest format version 1.

Not to be confused with `PORT=baremetal` in [`../port`](../port), which is
the libzxv port for microcontrollers. This folder packages the whole ZXV
**kernel** for application-class machines that boot through UEFI.

## What it builds on (nothing duplicated)

The disc build is the one merged in PR #25 and documented in
[`docs/BAREMETAL.md`](../../../docs/BAREMETAL.md). This kit only calls it:

| step | script (unchanged) |
|---|---|
| toolchain | [`build_system/Dockerfile.baremetal`](../../../build_system/Dockerfile.baremetal), the pinned Ubuntu 22.04 image |
| kernel + EFI loader + GPT/FAT32 disc | [`build_system/baremetal_images.sh`](../../../build_system/baremetal_images.sh) `<arch>` (which runs `mk_efi_disc.sh`) |
| boot check (`--boot-check`) | [`build_system/uefi_boot_check.sh`](../../../build_system/uefi_boot_check.sh), OVMF / QEMU_EFI, fail-closed on `[BOOT_OK]` |
| device-tree targets | the arch Makefiles, `build_system/Makefile.arm64` / `Makefile.riscv` |

`package` runs those in Docker by default (the same image CI uses), or
with this machine's toolchain under `--host`. It then copies the result
into `dist/boards/<board id>/` (or `--out DIR`):

```
zxv-<board id>-<arch>-uefi.img   the disc, checked against dist/discs/SHA256SUMS first
board.json                       the manifest as validated
BOOT.txt                         console, success marker, Secure Boot note, profile digest
SHA256SUMS                       of the three files above
```

`plan` and `package --dry-run` print the exact commands and write nothing.
No step signs anything or writes to a physical disk.

## The board manifest

Format: [`schema/board-manifest-1.schema.json`](schema/board-manifest-1.schema.json)
(JSON Schema 2020-12) plus the cross-field rules in
[`tools/zxv_board.py`](tools/zxv_board.py). Examples: [`boards/`](boards).

```json
{
  "format": "zxv-board-manifest",
  "version": 1,
  "board": {"id": "generic-x86-64-uefi-pc", "vendor": "Generic", "model": "x86-64 UEFI PC"},
  "arch": "x86_64",
  "firmware": {"type": "uefi", "secure_boot": "off"},
  "console": {"serial": {"uart": "16550", "base": "0x3f8", "baud": 38400},
              "framebuffer": "uefi-gop"},
  "memory": {"ram_mib": 4096},
  "hw_profile": {"digest": "34ec0e82...90d4", "file": "../profiles/r1.zhp"}
}
```

| field | meaning |
|---|---|
| `format`, `version` | `"zxv-board-manifest"`, `1`. A later incompatible format gets a new version and a new schema file. |
| `board.id` | lower-case slug, 1-64 characters; names the output folder |
| `board.vendor`, `.model`, `.revision` | free text, 1-128 characters, no control characters |
| `arch` | `x86_64`, `arm64` or `riscv64` |
| `firmware.type` | `uefi` or `devicetree` |
| `firmware.secure_boot` | UEFI only: `off`, or `owner-signs` (the machine owner signs the loader with their own key after packaging) |
| `firmware.machine` | device tree only: `qemu-virt` (see the support table) |
| `console.serial` | optional: `uart`, `base` (lower-case hex string) and `baud` |
| `console.framebuffer` | `uefi-gop` or `none`; there must be at least one console |
| `memory.ram_mib` | installed RAM in MiB |
| `hw_profile.digest` | the board's **ZHP-1 hardware profile digest** ([HW_PROFILE.md](../../docs/HW_PROFILE.md)): SHA-256 of the canonical profile encoding, the value `zxv_hw_profile_digest` returns |
| `hw_profile.file` | optional: the `.zhp` file, relative to the manifest; then its header is checked and its SHA-256 must equal `digest` |
| `image.esp_mib` | UEFI only, optional: ESP size, 33-4096 MiB (default 48) |
| `notes` | optional free text |

The validator is strict: unknown keys, duplicate keys, floating-point
numbers, `NaN`/`Infinity`, non-UTF-8 input and files over 1 MiB are all
errors. Integers only, as everywhere in ZXV.

**The hardware profile.** [`tools/zhp_tool.c`](tools/zhp_tool.c) uses the
public `zxv_hw_profile_*` ABI (libzxv 1.4):

```sh
make -C sdk/embedded/baremetal          # builds build/zhp_tool against libzxv
build/zhp_tool host my-board.zhp        # profile the machine it runs on; prints the digest
build/zhp_tool reference r1.zhp         # the public reference machine R1
build/zhp_tool digest my-board.zhp      # canonical decode + digest (refuses non-canonical)
```

Run `zhp_tool host` on the board itself (under Linux, say), or write the
profile with `zxv_hw_profile_node_add`/`cap_set`, and pin its digest. The
example boards pin R1 ([`profiles/r1.zhp`](profiles/r1.zhp)), the
reference machine ZXV software targets; `make check-profile` proves that
file is still libzxv's R1.

## What boots where (honest support table)

The rules come from the kernel's own constants, and the validator refuses
anything else.

| arch / firmware | console the kernel drives | min RAM | status |
|---|---|---|---|
| `x86_64` / `uefi` | 16550 COM1 at `0x3f8`, 38400 baud; and/or the UEFI GOP framebuffer | 512 MiB | built and booted under OVMF in CI (`ci.yml` `uefi-disc`); physical PCs by the steps in docs/BAREMETAL.md |
| `arm64` / `uefi` | PL011 at `0x9000000`, 115200 baud | 512 MiB | built and booted under QEMU virt + QEMU_EFI in CI; no physical arm64 board validated |
| `arm64` / `devicetree` | PL011 at `0x9000000`, 115200 baud | 256 MiB | QEMU virt only: a kernel ELF for `-kernel`; the kernel does not parse the device tree, its addresses are QEMU virt's |
| `riscv64` / `devicetree` | NS16550A at `0x10000000`, 115200 baud | 256 MiB | QEMU virt only, through OpenSBI (`-kernel`); the DTB pointer is captured but not parsed; host build only (`--host`, needs `gcc-riscv64-linux-gnu`) |
| `riscv64` / `uefi` | | | **not yet: no RISC-V UEFI boot path in the kernel** |
| `x86_64` / `devicetree` | | | not applicable: x86_64 boots through UEFI only |

The arm64 kernel has compile-time board profiles
(`kernel/arch/arm64/board_profile.h`) for some MediaTek boards, but their
addresses are marked unverified there and the build does not select them,
so this kit does not offer them.

## Tests

```sh
make -C sdk/libzxv
make -C sdk/embedded/baremetal test     # no Docker, QEMU or cross compiler needed
make -C sdk/embedded/baremetal smoke    # optional: real x86_64 disc + OVMF boot on this host
```

`make test` (CI: the `baremetal` job in
[`sdk-embedded.yml`](../../../.github/workflows/sdk-embedded.yml)):

- [`test/test_zxv_board.py`](test/test_zxv_board.py): 24 tests covering
  122 manifest cases (24 valid, 98 invalid, each invalid one checked for
  the right message), the exact commands of every plan, dry runs that
  write nothing, CLI exit codes, and `package --host` against a fake
  repository whose `baremetal_images.sh` writes a stand-in disc (bundle
  contents, SHA256SUMS, BOOT.txt text, a failing build, a checksum
  mismatch);
- every example manifest validated and dry-run planned;
- `check-profile`: `profiles/r1.zhp` byte-equal to libzxv's reference R1,
  decoded canonically, and pinned by every example board;
- R11 no-float check on `zhp_tool.c`.

`make smoke` packages `boards/qemu-x86-64-ovmf.json` with `--host
--boot-check`: it needs the PR #25 host toolchain (`x86_64-linux-gnu-gcc`,
`nasm`, `mtools`, `gdisk`), `qemu-system-x86_64` and OVMF. It is not in CI
here; the same build and boot run in `ci.yml`'s `uefi-disc` job.

## Limits

- **No Secure Boot signing.** Images are unsigned. Signing the loader with
  a db key and enrolling that key is the machine owner's step
  (`secure_boot: "owner-signs"` only records that intent and prints the
  instructions in BOOT.txt).
- **No vendor keys.** The kit holds, generates and expects no platform,
  KEK, db or vendor key of any kind.
- **Not validated on physical boards.** Every boot this kit's tests or CI
  perform is in QEMU. The x86_64 disc's physical-PC steps are documented
  in docs/BAREMETAL.md, but no physical board has been validated by this
  kit, and a valid manifest is not a statement that the board works.
- **The manifest describes; it does not configure.** The kernel's console
  addresses, baud rates and memory map are compiled in. The validator
  refuses a manifest that disagrees with them; it cannot make the kernel
  match a different board.
- **The profile digest is pinned, not attested.** The kit checks that a
  `.zhp` file matches its digest. It does not prove the board is what the
  profile says.
- **RISC-V:** no UEFI path in the kernel, so no RISC-V disc; QEMU virt via
  OpenSBI only.
