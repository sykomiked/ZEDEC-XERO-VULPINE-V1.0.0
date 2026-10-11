#!/usr/bin/env bash
# baremetal_images.sh — build the arm64 and x86_64 bare-metal UEFI discs.
#
#   bash build_system/baremetal_images.sh [arm64|x86_64 ...]   (default: both)
#
# Meant to run inside build_system/Dockerfile.baremetal so the toolchain is the
# same on a Mac and in CI. For each arch: clean-build the kernel (the swarm and
# tensor engine are part of the arch Makefile's source list), build the
# self-contained EFI stub that embeds it, then mk_efi_disc.sh wraps it in a
# GPT disc with one FAT32 ESP. Output: dist/discs/zxv-<arch>-uefi.img plus a
# SHA256SUMS file. Nothing here writes to a physical disk; the discs are
# opt-in images for QEMU or for a later, separately confirmed flash.
set -euo pipefail
cd "$(dirname "$0")/.."

ARCHES=("$@"); [ ${#ARCHES[@]} -gt 0 ] || ARCHES=(arm64 x86_64)
log(){ printf '\033[1;36m==> %s\033[0m\n' "$*"; }

# The swarm/tensor engine must be in the image, not just in the tree.
need_linked(){ # <elf> <symbol>
  # grep the saved list, not a pipe: under pipefail, grep -q exiting early
  # would SIGPIPE nm and fail the check even when the symbol is there.
  local syms; syms=$("$NM" "$1")
  grep -q " [TtDdBbRr] $2\$" <<<"$syms" || { echo "ERR: $2 not linked into $1"; exit 1; }
}

for arch in "${ARCHES[@]}"; do
  case "$arch" in
    arm64)
      MK=build_system/Makefile.arm64
      VARS=(CROSS_COMPILE=aarch64-linux-gnu-)
      ELF=kernel_arm64.elf EFI=BOOTAA64.EFI NM=aarch64-linux-gnu-nm ;;
    x86_64)
      MK=build_system/Makefile.x86_64
      VARS=(CC=x86_64-linux-gnu-gcc LD=x86_64-linux-gnu-ld OBJCOPY=x86_64-linux-gnu-objcopy)
      ELF=kernel_x86_64.elf EFI=BOOTX64.EFI NM=x86_64-linux-gnu-nm ;;
    *) echo "unknown arch $arch (arm64|x86_64)"; exit 2 ;;
  esac
  log "$arch: clean build ($MK)"
  make -f "$MK" "${VARS[@]}" clean >/dev/null
  make -f "$MK" "${VARS[@]}" all
  make -f "$MK" "${VARS[@]}" "$EFI"
  need_linked "$ELF" swarm_boot_selfcheck
  need_linked "$ELF" zt_boot_selfcheck
  log "$arch: swarm + tensor engine linked into $ELF"
  bash build_system/mk_efi_disc.sh "$arch"
done

( cd dist/discs && sha256sum zxv-*-uefi.img MEGAROM.TVL > SHA256SUMS && cat SHA256SUMS )
