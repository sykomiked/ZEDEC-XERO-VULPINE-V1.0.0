#!/usr/bin/env bash
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# verify_boot.sh — build the bare-metal UEFI disc and watch it boot, so anyone
# can check the claims in docs/VERIFY_BOOT.md for themselves.
#
#   bash build_system/verify_boot.sh [x86_64|arm64|all] [--docker|--native] [--seconds N]
#
# For each arch: build the disc from this checkout (baremetal_images.sh),
# print its SHA-256, boot it headless under UEFI firmware in QEMU
# (uefi_boot_check.sh: a snapshot copy, the disc file is never written), and
# require every marker the boot is meant to print. Results land in
# dist/verify/: the serial log, a screenshot and REPORT.txt.
#
# --docker builds and boots inside build_system/Dockerfile.baremetal (only
# Docker needed, and the disc then matches CI byte for byte). --native uses
# the tools on this machine. Without either, native is used when the tools
# are all present, else Docker. Nothing here touches a physical disk.
set -uo pipefail
cd "$(dirname "$0")/.." || exit 1

WHAT=all MODE=auto SECS=150
while [ $# -gt 0 ]; do
  case "$1" in
    x86_64|arm64|all) WHAT=$1 ;;
    --docker) MODE=docker ;;
    --native) MODE=native ;;
    --seconds) SECS=${2:?--seconds needs a number}; shift ;;
    -h|--help) sed -n '5,20p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "unknown argument: $1 (see --help)"; exit 2 ;;
  esac
  shift
done
[ "$WHAT" = all ] && ARCHES=(x86_64 arm64) || ARCHES=("$WHAT")

missing_native(){ # the tools baremetal_images.sh, mk_efi_disc.sh and the boot use
  local t miss=()
  for t in make python3 sha256sum mformat mcopy sgdisk timeout; do
    command -v "$t" >/dev/null 2>&1 || miss+=("$t")
  done
  for a in "${ARCHES[@]}"; do
    case "$a" in
      x86_64) for t in x86_64-linux-gnu-gcc nasm qemu-system-x86_64; do
                command -v "$t" >/dev/null 2>&1 || miss+=("$t"); done ;;
      arm64)  for t in aarch64-linux-gnu-gcc qemu-system-aarch64; do
                command -v "$t" >/dev/null 2>&1 || miss+=("$t"); done ;;
    esac
  done
  echo "${miss[@]+"${miss[@]}"}"
}

if [ "$MODE" = auto ]; then
  if [ -z "$(missing_native)" ]; then MODE=native
  elif command -v docker >/dev/null 2>&1; then MODE=docker
  else
    echo "Missing tools: $(missing_native)"
    echo "Install Docker (simplest), or the native tools listed in docs/VERIFY_BOOT.md."
    exit 1
  fi
fi

if [ "$MODE" = docker ]; then
  command -v docker >/dev/null 2>&1 || { echo "Docker is not installed (docs/VERIFY_BOOT.md)"; exit 1; }
  echo "==> building the toolchain image (first run only takes a while)"
  docker build -q -f build_system/Dockerfile.baremetal -t zxv-baremetal . >/dev/null || exit 1
  # Run as this user so dist/ stays yours; git inside the container may not
  # trust the mount, so the commit is passed in.
  DIRTY=$(git status --porcelain 2>/dev/null | grep -qv '^??' && echo " (with local changes)")
  exec docker run --rm -u "$(id -u):$(id -g)" -e HOME=/tmp \
    -e ZXV_COMMIT="$(git rev-parse HEAD 2>/dev/null || echo unknown)${DIRTY:-}" \
    -v "$PWD:/src" -w /src zxv-baremetal \
    bash build_system/verify_boot.sh "$WHAT" --native --seconds "$SECS"
fi

m=$(missing_native)
[ -z "$m" ] || { echo "Missing tools: $m (or rerun with --docker)"; exit 1; }

OUT=dist/verify
mkdir -p "$OUT"
REPORT="$OUT/REPORT.txt"
if [ -n "${ZXV_COMMIT:-}" ]; then COMMIT=$ZXV_COMMIT; else
  COMMIT=$(git rev-parse HEAD 2>/dev/null || echo unknown)
  DIRTY=$(git status --porcelain 2>/dev/null | grep -qv '^??' && echo " (with local changes)")
fi
{
  echo "ZXV boot verification"
  echo "commit:  $COMMIT${DIRTY:-}"
  echo "date:    $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "host:    $(uname -sm), $(qemu-system-x86_64 --version 2>/dev/null | head -1)"
} > "$REPORT"

# marker|what it proves (docs/VERIFY_BOOT.md explains each)
MARKERS_COMMON=(
  "[HANDOFF_OK]|the EFI stub handed over the firmware memory map, ACPI and clock, and the kernel window was free"
  "[BOOTMEM]|large buffers came from memory the firmware called usable"
  "[AI_OK]|the built-in swarm and tensor engine passed their self-check"
  "All systems online|every subsystem self-check passed"
  "[BOOT_OK]|the kernel reached its main loop with no fault"
)
MARKERS_X86=(
  "[SCHED_OK]|ring-3 processes ran under the preemptive scheduler; faulting ones ended alone"
  "heartbeat: ticks=|timer interrupts keep arriving in the idle loop"
)

fail=0
for arch in "${ARCHES[@]}"; do
  echo "==> $arch: building the disc"
  bash build_system/baremetal_images.sh "$arch" > "$OUT/build-$arch.log" 2>&1 \
    || { echo "build failed: see $OUT/build-$arch.log"; tail -20 "$OUT/build-$arch.log"; fail=1; continue; }
  DISC=dist/discs/zxv-$arch-uefi.img
  SUM=$(sha256sum "$DISC" | cut -d' ' -f1)
  echo "    $DISC"
  echo "    sha256 $SUM"
  M=("${MARKERS_COMMON[@]}"); [ "$arch" = x86_64 ] && M+=("${MARKERS_X86[@]}")
  need=(); for e in "${M[@]}"; do need+=("${e%%|*}"); done

  echo "==> $arch: booting it under UEFI firmware for ${SECS}s (headless)"
  UEFI_SCREENSHOT="$OUT/screen-$arch.png" \
    bash build_system/uefi_boot_check.sh "$arch" "$DISC" "$SECS" "${need[@]}" > "$OUT/check-$arch.txt" 2>&1
  rc=$?
  cp -f "uefi_boot_$arch.log" "$OUT/serial-$arch.log" 2>/dev/null
  {
    echo
    echo "[$arch] disc $DISC"
    echo "[$arch] sha256 $SUM"
  } >> "$REPORT"
  for e in "${M[@]}"; do
    k=${e%%|*} why=${e#*|}
    if grep -aqF -- "$k" "$OUT/serial-$arch.log" 2>/dev/null; then r=PASS; else r=MISSING; fi
    printf '    %-8s %-22s %s\n' "$r" "$k" "$why"
    printf '[%s] %-8s %-22s %s\n' "$arch" "$r" "$k" "$why" >> "$REPORT"
  done
  if [ "$rc" -eq 0 ]; then
    echo "    VERIFIED: $arch booted with every marker and no fault"
    echo "[$arch] VERIFIED" >> "$REPORT"
  else
    echo "    NOT VERIFIED: $arch (details in $OUT/check-$arch.txt)"
    grep -a '::error::' "$OUT/check-$arch.txt" | sed 's/^/    /'
    echo "[$arch] NOT VERIFIED" >> "$REPORT"
    fail=1
  fi
done

echo
echo "Report: $REPORT; serial logs and screenshots in $OUT/"
[ "$fail" -eq 0 ] || echo "Something failed. docs/VERIFY_BOOT.md says how to report it."
exit "$fail"
