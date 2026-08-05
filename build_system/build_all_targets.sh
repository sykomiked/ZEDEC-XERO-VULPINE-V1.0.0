#!/usr/bin/env bash
# build_all_targets.sh — cross-compile ZXV for every supported target and
# report an HONEST build matrix.
#
# Each target is attempted independently: a missing toolchain is reported as
# SKIP (not a failure), a real compile error is reported as FAIL, and only a
# produced artifact counts as OK. The matrix at the end is the deliverable —
# it says exactly which targets exist, which build, and which boot.
#
#   bash build_system/build_all_targets.sh            # build everything available
#   TARGETS="arm64 x86_64" bash build_system/build_all_targets.sh
#   BOOT=1 bash build_system/build_all_targets.sh     # also boot-test in QEMU
#
# Run from 05_KERNEL.
set -uo pipefail
cd "$(dirname "$0")/.." || exit 1

TARGETS="${TARGETS:-arm64 x86_64 riscv riscv32 arm32}"
BOOT="${BOOT:-0}"
OUT="dist"
mkdir -p "$OUT"

declare -a ROWS
note() { printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }

# target -> cross prefix (used only to test toolchain presence)
prefix_for() {
  case "$1" in
    arm64)   echo "aarch64-linux-gnu-" ;;
    x86_64)  echo "x86_64-linux-gnu-" ;;
    riscv)   echo "riscv64-linux-gnu-" ;;
    riscv32) echo "riscv64-linux-gnu-" ;;
    arm32)   echo "arm-none-eabi-" ;;
    *)       echo "" ;;
  esac
}

# On macOS the aarch64 toolchain is often the -unknown- triple; accept either.
resolve_cc() {
  local p="$1"
  if command -v "${p}gcc" >/dev/null 2>&1; then echo "${p}gcc"; return; fi
  case "$p" in
    aarch64-linux-gnu-)
      command -v aarch64-unknown-linux-gnu-gcc >/dev/null 2>&1 && \
        echo "aarch64-unknown-linux-gnu-gcc" && return ;;
    riscv64-linux-gnu-)
      command -v riscv64-unknown-elf-gcc >/dev/null 2>&1 && \
        echo "riscv64-unknown-elf-gcc" && return ;;
  esac
  echo ""
}

for T in $TARGETS; do
  MK="build_system/Makefile.${T}"
  if [ ! -f "$MK" ]; then
    ROWS+=("$T|NO-MAKEFILE|-|-"); continue
  fi
  PFX="$(prefix_for "$T")"
  CC="$(resolve_cc "$PFX")"
  if [ -z "$CC" ]; then
    note "$T: toolchain ${PFX}gcc not installed — SKIP"
    ROWS+=("$T|SKIP(no toolchain)|-|-"); continue
  fi

  note "$T: building with $CC"
  # clean only this target's objects so targets do not clobber each other
  find kernel -name '*.o' -delete 2>/dev/null
  LOG="/tmp/zxv_build_${T}.log"
  # allow the Makefile's own CROSS_COMPILE default to be overridden when the
  # local toolchain uses a different triple
  CROSS="${CC%gcc}"
  if make -f "$MK" CROSS_COMPILE="$CROSS" > "$LOG" 2>&1; then
    ART=$(ls -t *.bin *.elf 2>/dev/null | head -1)
    WARN=$(grep -c 'warning:' "$LOG" | tr -d ' ')
    if [ -n "$ART" ]; then
      cp "$ART" "$OUT/zxv-${T}$(echo "$ART" | sed 's/.*\(\.[a-z]*\)$/\1/')" 2>/dev/null
      SZ=$(wc -c < "$ART" | tr -d ' ')
      ROWS+=("$T|OK|${SZ}B|warn=${WARN}")
    else
      ROWS+=("$T|BUILT-NO-ARTIFACT|-|warn=${WARN}")
    fi
  else
    ERR=$(grep -m1 -E 'error|Error' "$LOG" | cut -c1-58)
    ROWS+=("$T|FAIL|-|${ERR:-see $LOG}")
  fi
done

# ---------------------------------------------------------------- boot test
if [ "$BOOT" = "1" ]; then
  note "boot tests"
  for T in $TARGETS; do
    case "$T" in
      arm64)
        [ -f "$OUT/zxv-arm64.bin" ] || continue
        if timeout 20 qemu-system-aarch64 -M virt,gic-version=3 -cpu cortex-a53 \
             -m 256M -nographic -kernel "$OUT/zxv-arm64.bin" 2>&1 | grep -q BOOT_OK
        then echo "  arm64 BOOT_OK"; else echo "  arm64 boot FAILED"; fi ;;
      x86_64)
        [ -f "$OUT/zxv-x86_64.bin" ] || continue
        if timeout 20 qemu-system-x86_64 -nographic \
             -kernel "$OUT/zxv-x86_64.bin" 2>&1 | grep -q BOOT_OK
        then echo "  x86_64 BOOT_OK"; else echo "  x86_64 boot FAILED"; fi ;;
      riscv)
        [ -f "$OUT/zxv-riscv.bin" ] || continue
        if timeout 20 qemu-system-riscv64 -M virt -nographic \
             -kernel "$OUT/zxv-riscv.bin" 2>&1 | grep -q BOOT_OK
        then echo "  riscv BOOT_OK"; else echo "  riscv boot FAILED"; fi ;;
    esac
  done
fi

# ------------------------------------------------------------------ matrix
echo
printf '\033[1m%-10s %-22s %-12s %s\033[0m\n' TARGET STATUS SIZE NOTES
printf '%.0s-' {1..70}; echo
OKC=0
for r in "${ROWS[@]}"; do
  IFS='|' read -r t st sz nt <<< "$r"
  [ "$st" = "OK" ] && OKC=$((OKC+1))
  printf '%-10s %-22s %-12s %s\n' "$t" "$st" "$sz" "$nt"
done
echo
echo "built ${OKC} target(s) into ./${OUT}/"
echo "NOTE: SKIP means the cross toolchain is not installed on this host —"
echo "      run build_system/provision_server.sh to install them."
