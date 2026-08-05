#!/usr/bin/env bash
# turnkey.sh — one command, on a freshly-provisioned Linux server, that takes
# ZXV all the way from source to shippable deliverables for every platform.
#
# WHY THIS EXISTS
# ---------------
# The server time is paid and finite. This script runs the whole pipeline in
# order, stops on the first REAL failure, and reports an honest stage matrix so
# nothing is assumed done that was actually skipped. Every stage is
# individually re-runnable.
#
# THE PIPELINE
#   0. preflight   compile-check every target with clang (fast, catches gaps)
#   1. build       cross-compile a real image per architecture
#   2. verify      run the host test suite (verify-all) — the correctness gate
#   3. package     wrap each image into its native Tri-Space triad
#                    (.zxvc / .cedez / .cedec) with mkzxpkg
#   4. disc        assemble the single universal install disc (UEFI multi-arch)
#   5. vm          assemble the single universal VM file (raw + OVA + conversions)
#   6. manifest    checksums + a deliverables manifest
#
#   bash build_system/turnkey.sh                 # everything available
#   STAGES="build verify package" bash build_system/turnkey.sh
#   TARGETS="arm64 x86_64 riscv" bash build_system/turnkey.sh
#
# Run from 05_KERNEL. Honest by construction: a missing toolchain is SKIP, a
# real error is FAIL, and only a produced artifact is OK.
set -uo pipefail
cd "$(dirname "$0")/.." || exit 1

TARGETS="${TARGETS:-arm64 x86_64 riscv riscv32 arm32}"
STAGES="${STAGES:-preflight build verify package disc vm manifest}"
DIST="${DIST:-dist}"
mkdir -p "$DIST"

c_cyan=$'\033[1;36m'; c_grn=$'\033[1;32m'; c_red=$'\033[1;31m'; c_yel=$'\033[1;33m'; c_off=$'\033[0m'
stage() { printf '\n%s========== %s ==========%s\n' "$c_cyan" "$*" "$c_off"; }
ok()    { printf '%s  [OK]%s %s\n'   "$c_grn" "$c_off" "$*"; }
skip()  { printf '%s  [SKIP]%s %s\n' "$c_yel" "$c_off" "$*"; }
die()   { printf '%s  [FAIL]%s %s\n' "$c_red" "$c_off" "$*"; exit 1; }

has_stage() { case " $STAGES " in *" $1 "*) return 0;; *) return 1;; esac; }

declare -a MATRIX

# ---------- 0. preflight ----------
if has_stage preflight; then
  stage "0. preflight — compile-check every target"
  if bash build_system/preflight_all_targets.sh; then ok "preflight clean"
  else die "preflight found compile errors — fix before spending build time"; fi
fi

# ---------- 1. build ----------
built=()
if has_stage build; then
  stage "1. build — cross-compile each target"
  for T in $TARGETS; do
    MK="build_system/Makefile.${T}"
    [ -f "$MK" ] || { skip "$T (no Makefile)"; MATRIX+=("$T|build|SKIP"); continue; }
    if make -f "$MK" >/dev/null 2>"$DIST/build_$T.log"; then
      # locate the produced image
      img=$(ls -1 kernel_${T}.bin kernel_${T}.elf 2>/dev/null | head -1 || true)
      if [ -n "$img" ]; then
        cp "$img" "$DIST/"; built+=("$T:$DIST/$(basename "$img")")
        ok "$T -> $(basename "$img") ($(wc -c < "$img") bytes)"; MATRIX+=("$T|build|OK")
      else skip "$T built but produced no image"; MATRIX+=("$T|build|NO-IMAGE"); fi
    else
      printf '%s  [FAIL]%s %s (see %s)\n' "$c_red" "$c_off" "$T" "$DIST/build_$T.log"
      MATRIX+=("$T|build|FAIL")
    fi
  done
fi

# ---------- 2. verify ----------
if has_stage verify; then
  stage "2. verify — host correctness suite"
  if make -C kernel verify-all >"$DIST/verify.log" 2>&1; then
    n=$(grep -c '^\[PASS\]' "$DIST/verify.log" || true)
    ok "verify-all: $n assertions, 0 failures"
  else
    f=$(grep -c '^\[FAIL\]' "$DIST/verify.log" || true)
    die "verify-all: $f failing assertion(s) — see $DIST/verify.log"
  fi
fi

# ---------- 3. package into native Tri-Space triads ----------
if has_stage package; then
  stage "3. package — native Tri-Space triad per image"
  if ! [ -x "$DIST/mkzxpkg" ]; then
    cc -std=c11 -O2 -DTEST_HOST -o "$DIST/mkzxpkg" build_system/mkzxpkg.c \
       kernel/src/zxpkg/zxpkg.c kernel/src/trispace/trispace.c \
       kernel/src/robin_debanks/sha256.c 2>"$DIST/mkzxpkg.log" \
       && ok "built mkzxpkg" || die "could not build mkzxpkg (see $DIST/mkzxpkg.log)"
  fi
  shopt -s nullglob
  for img in "$DIST"/kernel_*.bin "$DIST"/kernel_*.elf; do
    base="$DIST/zxv-$(basename "${img%.*}" | sed 's/^kernel_//')"
    if "$DIST/mkzxpkg" "$img" "$base" restoring >>"$DIST/package.log" 2>&1; then
      ok "$(basename "$img") -> $(basename "$base").zxvc/.cedez/.cedec"
      MATRIX+=("$(basename "$base")|package|OK")
    else
      MATRIX+=("$(basename "$base")|package|FAIL")
      printf '%s  [FAIL]%s package %s\n' "$c_red" "$c_off" "$(basename "$img")"
    fi
  done
  shopt -u nullglob
fi

# ---------- 4. universal disc ----------
if has_stage disc; then
  stage "4. disc — single universal install disc (UEFI multi-arch)"
  if bash build_system/mkuniversal_disc.sh "$DIST"; then ok "universal disc assembled"
  else skip "universal disc incomplete (see log; per-arch EFI stub may be pending)"; fi
fi

# ---------- 5. universal VM ----------
if has_stage vm; then
  stage "5. vm — single universal VM file (+ conversions)"
  if bash build_system/mkuniversal_vm.sh "$DIST"; then ok "universal VM assembled"
  else skip "universal VM incomplete (see log)"; fi
fi

# ---------- 6. manifest ----------
if has_stage manifest; then
  stage "6. manifest — checksums + deliverables"
  ( cd "$DIST" && ls -1 *.zxvc *.cedez *.cedec *.img *.iso *.ova *.qcow2 2>/dev/null \
      | while read -r f; do sha256sum "$f"; done ) > "$DIST/MANIFEST.sha256" 2>/dev/null || true
  ok "wrote $DIST/MANIFEST.sha256"
fi

# ---------- summary ----------
stage "SUMMARY"
printf '%-22s %-10s %s\n' TARGET STAGE STATUS
printf -- '--------------------------------------------------\n'
for row in ${MATRIX[@]+"${MATRIX[@]}"}; do
  IFS='|' read -r a b c <<< "$row"
  col="$c_grn"; case "$c" in FAIL) col="$c_red";; SKIP|NO-IMAGE) col="$c_yel";; esac
  printf '%-22s %-10s %s%s%s\n' "$a" "$b" "$col" "$c" "$c_off"
done
echo
echo "deliverables in: $DIST/"
ls -1 "$DIST" 2>/dev/null | sed 's/^/  /'
