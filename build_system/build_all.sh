#!/usr/bin/env bash
# build_all.sh — the single reproducible build+verify entrypoint for the ZXV /
# ZEDEC pqOS five-architecture multikernel deliverable.
#
# ONE command, on the Linux build box, that:
#   1. discovers the toolchain (via `which`) and the out-of-tree object dirs
#      (from each Makefile's own OBJDIR) -- nothing discoverable is hardcoded;
#   2. cleans (rm -rf build/, plus any stray in-tree .o);
#   3. builds all five arches CONCURRENTLY with -j (nonlinear: every arch at once);
#   4. runs BOTH static gates -- DRC (verify_layers.sh) and LVS (verify_banners.sh);
#   5. boots all six rows in QEMU (30s; rc=124 == still-running == PASS) and reads
#      the runtime gate (modbind_verify_graph), [BOOT_OK], and any REAL fault;
#   6. prints the PASS/FAIL verification matrix and an overall verdict.
#
# Supported families: ARM64 (ARMv8-A), x86-64, RISC-V RV64 (Sv39),
# RISC-V RV32 (Sv32), ARMv7-A (arm32). Emulation-verified only.
#
# Honest accounting: "builds" != "links" != "boots" != "graph sound". Each column
# is measured from a real artifact (make rc, nm-backed ELF, QEMU exit, boot log),
# never inferred from a prior column.
set -u

# --- ROOT: the tree is the parent of build_system/ ---------------------------
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT" || { echo "cannot cd to tree root"; exit 2; }

JOBS=$(nproc 2>/dev/null || echo 4)
LOG="$ROOT/_build_all_logs"
rm -rf "$LOG"; mkdir -p "$LOG"

# arch -> (Makefile, ELF artifact, .bin artifact or "-", QEMU boot rows...)
ARCHES=(arm64 x86_64 riscv riscv32 arm32)
declare -A MK ELF BIN
MK[arm64]=build_system/Makefile.arm64 ;   ELF[arm64]=kernel_arm64.elf ;        BIN[arm64]=kernel_arm64.bin
MK[x86_64]=build_system/Makefile.x86_64 ; ELF[x86_64]=kernel_x86_64.elf ;      BIN[x86_64]=kernel_x86_64.bin
MK[riscv]=build_system/Makefile.riscv ;   ELF[riscv]=kernel_riscv.elf ;        BIN[riscv]=kernel_riscv.bin
MK[riscv32]=build_system/Makefile.riscv32;ELF[riscv32]=kernel_riscv32.elf ;    BIN[riscv32]=kernel_riscv32.bin
MK[arm32]=build_system/Makefile.arm32 ;   ELF[arm32]=zedec_xero_arm32.elf ;    BIN[arm32]=-

# boot rows: label -> qemu command (six rows; arm64 booted on both GICv3 and v2)
declare -A BOOT_ELF
BOOT_ROWS=(arm64v3 arm64v2 x86_64 riscv64 riscv32 arm32)
boot_cmd() { # echoes the qemu argv for a boot-row label
  case "$1" in
    arm64v3) echo "qemu-system-aarch64 -M virt,gic-version=3 -cpu cortex-a72 -m 512 -nographic -kernel kernel_arm64.elf" ;;
    arm64v2) echo "qemu-system-aarch64 -M virt -cpu cortex-a72 -m 512 -nographic -kernel kernel_arm64.elf" ;;
    x86_64)  echo "qemu-system-x86_64 -m 512 -nographic -kernel kernel_x86_64.elf" ;;
    riscv64) echo "qemu-system-riscv64 -M virt -m 512 -nographic -kernel kernel_riscv.elf" ;;
    riscv32)
      # SHIP OUR OWN OpenSBI FOR RV32. Debian's `opensbi` package installs the
      # generic (riscv64) firmware ONLY -- there is no riscv32 blob in it -- so
      # qemu-system-riscv32 dies instantly with
      #   "Unable to load the RISC-V firmware opensbi-riscv32-generic-fw_dynamic.bin"
      # and the row reports boot_rc=1 with an empty log. MEASURED on a fresh
      # Debian 12 / QEMU 7.2 node 2026-09; the previous hand-built box happened to
      # have the blob lying around, which is why this never surfaced before.
      # The kernel is fine: entry is 0x80200000 and it reaches [BOOT_OK] the
      # moment real firmware is supplied. Prefer a bundled blob, fall back to
      # QEMU's default so a machine that DOES package it still works.
      # Paths are ROOT-ABSOLUTE on purpose. They used to be cwd-relative, which
      # made the row's verdict depend on where the harness happened to be
      # invoked from: a run whose cwd was not ROOT found no blob, silently
      # dropped -bios, and reported boot_rc=1 -- a FALSE FAILURE for a kernel
      # that boots. MEASURED 2026-09-02: same tree, same ELF, launched by hand
      # with the bundled blob -> rc=124 (still running) and [BOOT_OK] present.
      # If this row still fails, the blob is genuinely MISSING from the box --
      # which is a provisioning fact, and the row now says so out loud.
      _rv32fw=""
      for _c in "$ROOT/opensbi-riscv32-generic-fw_dynamic.bin" \
                "$ROOT/deliverable/discs/firmware/opensbi-riscv32-generic-fw_dynamic.bin" \
                "/usr/share/qemu/opensbi-riscv32-generic-fw_dynamic.bin" \
                "/usr/lib/riscv64-linux-gnu/opensbi/generic/fw_dynamic.bin"; do
        [ -f "$_c" ] && { _rv32fw="-bios $_c"; break; }
      done
      [ -z "$_rv32fw" ] && echo "WARN: no rv32 OpenSBI blob found; row will fail for want of FIRMWARE, not kernel" >&2
      echo "qemu-system-riscv32 -M virt -m 512 -nographic $_rv32fw -kernel kernel_riscv32.elf" ;;
    arm32)   echo "qemu-system-arm -M virt -cpu cortex-a15 -m 512 -nographic -kernel zedec_xero_arm32.elf" ;;
  esac
}

# discover each arch's default toolchain prefix from its Makefile (?= or =),
# then resolve the compiler with `which`. Never hardcode a path.
mk_prefix() { # <makefile> -> toolchain prefix (may be empty for native)
  # Capture the RHS of the first assignment, tolerant of ?=/:=/= and spacing.
  # Precedence: CROSS_COMPILE, then CROSS, then CROSS_PREFIX.
  awk '
    function rhs(l){ sub(/^[^=]*=[[:space:]]*/,"",l); sub(/[[:space:]].*$/,"",l); return l }
    /^CROSS_COMPILE[[:space:]]*[?:]?=/ { print rhs($0); exit }
    /^CROSS[[:space:]]*[?:]?=/         { if(c=="") c=rhs($0) }
    /^CROSS_PREFIX[[:space:]]*[?:]?=/  { if(p=="") p=rhs($0) }
    END{ if(c!="") print c; else print p }' "$1"
}

echo "############################################################"
echo "# ZXV / ZEDEC pqOS  --  build_all.sh"
echo "# root : $ROOT"
echo "# host : $(uname -m) $(uname -s)  jobs=-j$JOBS"
echo "# date : $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
echo "# git  : $(git -C "$ROOT" rev-parse --short HEAD 2>/dev/null || echo 'no-git')"
echo "############################################################"

echo
echo "=== [0] TOOLCHAIN + EMULATOR DISCOVERY (which) ==="
PREFLIGHT_OK=1
for a in "${ARCHES[@]}"; do
  pfx=$(mk_prefix "${MK[$a]}")
  cc=$(which "${pfx}gcc" 2>/dev/null)
  # x86_64 falls back to native gcc if the cross prefix is absent (host is x86_64)
  if [ -z "$cc" ] && [ "$a" = x86_64 ]; then pfx=""; cc=$(which gcc 2>/dev/null); fi
  objd=$(awk -F':= *' '/^OBJDIR[[:space:]]*:=/{print $2; exit}' "${MK[$a]}")
  if [ -n "$cc" ]; then
    printf "  %-8s prefix=%-22s cc=%s  objdir=%s\n" "$a" "${pfx:-<native>}" "$cc" "${objd:-?}"
  else
    printf "  %-8s prefix=%-22s cc=MISSING\n" "$a" "${pfx:-<native>}"; PREFLIGHT_OK=0
  fi
done
for q in qemu-system-aarch64 qemu-system-x86_64 qemu-system-riscv64 qemu-system-riscv32 qemu-system-arm; do
  w=$(which "$q" 2>/dev/null)
  [ -n "$w" ] || { printf "  QEMU MISSING: %s\n" "$q"; PREFLIGHT_OK=0; }
done
[ "$PREFLIGHT_OK" = 1 ] || { echo "PREFLIGHT FAILED -- missing toolchain/emulator above"; exit 3; }
echo "  all toolchains + emulators resolved."

echo
echo "=== [1] CLEAN ==="
rm -rf "$ROOT/build"
find "$ROOT/kernel" -name '*.o' -delete 2>/dev/null
for a in "${ARCHES[@]}"; do rm -f "$ROOT/${ELF[$a]}"; [ "${BIN[$a]}" != - ] && rm -f "$ROOT/${BIN[$a]}"; done
echo "  removed build/, stray kernel/**/*.o, and prior ELF/.bin artifacts."

echo
echo "=== [2] BUILD (all five concurrently, -j$JOBS, out-of-tree OBJDIR) ==="
declare -A BRC
pids=""
for a in "${ARCHES[@]}"; do
  ( make -f "${MK[$a]}" -j"$JOBS" all >"$LOG/build_$a.log" 2>&1; echo $? >"$LOG/build_$a.rc" ) &
  pids="$pids $!"
done
wait $pids
BUILD_OK=1
for a in "${ARCHES[@]}"; do
  BRC[$a]=$(cat "$LOG/build_$a.rc" 2>/dev/null || echo 99)
  errs=$(grep -ciE '(^|[: ])error:' "$LOG/build_$a.log" 2>/dev/null); errs=${errs:-0}
  printf "  build %-8s rc=%s  errors=%s\n" "$a" "${BRC[$a]}" "$errs"
  [ "${BRC[$a]}" = 0 ] || BUILD_OK=0
done

echo
echo "=== [2b] ELF ARTIFACTS ==="
for a in "${ARCHES[@]}"; do
  if [ -f "$ROOT/${ELF[$a]}" ]; then
    printf "  %-8s %-24s %8s bytes" "$a" "${ELF[$a]}" "$(stat -c%s "$ROOT/${ELF[$a]}")"
    [ "${BIN[$a]}" != - ] && [ -f "$ROOT/${BIN[$a]}" ] && printf "   %s %s bytes" "${BIN[$a]}" "$(stat -c%s "$ROOT/${BIN[$a]}")"
    echo
  else
    printf "  %-8s %-24s MISSING\n" "$a" "${ELF[$a]}"
  fi
done

echo
echo "=== [3] GATE 1 -- DRC (verify_layers.sh, static layer/acyclic/provided/contract) ==="
DRC=FAIL
if bash "$ROOT/build_system/verify_layers.sh" >"$LOG/drc.log" 2>&1; then DRC=PASS; fi
grep -E 'PASS:|FAIL:|unassigned|no module includes' "$LOG/drc.log" | head -6 | sed 's/^/  /'
echo "  DRC verdict: $DRC"

echo
echo "=== [4] GATE 2 -- LVS (verify_banners.sh per ELF, banner-backed-by-symbols) ==="
declare -A LVS
for a in "${ARCHES[@]}"; do
  if [ -f "$ROOT/${ELF[$a]}" ] && bash "$ROOT/build_system/verify_banners.sh" "$ROOT/${ELF[$a]}" >"$LOG/lvs_$a.log" 2>&1; then
    LVS[$a]=PASS
  else
    LVS[$a]=FAIL
  fi
  printf "  LVS %-8s %s\n" "$a" "${LVS[$a]}"
done

echo
echo "=== [5] BOOT (six rows, 30s each; rc=124 == still-running == PASS) ==="
declare -A QRC BOK GATE FAULT
for r in "${BOOT_ROWS[@]}"; do
  cmd=$(boot_cmd "$r")
  ( cd "$ROOT" && timeout 30 $cmd >"$LOG/boot_$r.log" 2>&1; echo $? >"$LOG/boot_$r.rc" )
  QRC[$r]=$(cat "$LOG/boot_$r.rc")
  BOK[$r]=$(grep -qE '\[BOOT_OK\]' "$LOG/boot_$r.log" && echo yes || echo no)
  if grep -qE '\[OK\] GATE modbind_verify_graph' "$LOG/boot_$r.log"; then GATE[$r]=OK
  elif grep -qE '\[FAIL\] GATE modbind_verify_graph' "$LOG/boot_$r.log"; then GATE[$r]=FAIL
  else GATE[$r]="n/a"; fi
  # a REAL fault dumps registers or panics; [FAULT CONTAINED] is a PASSING self-test
  if grep -qE 'PANIC|ESR_EL|FAR_EL|ELR_EL|scause|Unhandled|General Protection|#PF' "$LOG/boot_$r.log"; then
    FAULT[$r]=FAULT
  else
    FAULT[$r]=none
  fi
  bytes=$(wc -c <"$LOG/boot_$r.log")
  printf "  boot %-8s qemu_rc=%-4s bootok=%-3s gate=%-4s fault=%-5s log=%sB\n" \
     "$r" "${QRC[$r]}" "${BOK[$r]}" "${GATE[$r]}" "${FAULT[$r]}" "$bytes"
done

echo
echo "=== [6] VERIFICATION MATRIX ==="
printf "%-9s | %-7s | %-24s | %-4s | %-4s | %-8s | %-7s | %-5s | %-6s | %s\n" \
  arch buildrc ELF DRC LVS "boot_rc" BOOT_OK gate fault VERDICT
echo "----------|---------|--------------------------|------|------|----------|---------|------|--------|--------"
elf_for_row() { case "$1" in arm64v3|arm64v2) echo arm64;; x86_64) echo x86_64;; riscv64) echo riscv;; riscv32) echo riscv32;; arm32) echo arm32;; esac; }
ALL_OK=1
for r in "${BOOT_ROWS[@]}"; do
  a=$(elf_for_row "$r")
  esz=$( [ -f "$ROOT/${ELF[$a]}" ] && stat -c%s "$ROOT/${ELF[$a]}" || echo MISSING )
  # per-ROW verdict: build rc0, ELF present, DRC pass, LVS pass, qemu 124, BOOT_OK yes,
  # gate OK (or n/a where the arch prints no runtime gate), no real fault.
  v=PASS
  [ "${BRC[$a]}" = 0 ] || v=FAIL
  [ "$esz" = MISSING ] && v=FAIL
  [ "$DRC" = PASS ] || v=FAIL
  [ "${LVS[$a]}" = PASS ] || v=FAIL
  [ "${QRC[$r]}" = 124 ] || v=FAIL
  [ "${BOK[$r]}" = yes ] || v=FAIL
  [ "${GATE[$r]}" = FAIL ] && v=FAIL
  [ "${FAULT[$r]}" = none ] || v=FAIL
  [ "$v" = PASS ] || ALL_OK=0
  printf "%-9s | rc%-5s | %-24s | %-4s | %-4s | %-8s | %-7s | %-4s | %-6s | %s\n" \
     "$r" "${BRC[$a]}" "${ELF[$a]}($esz)" "$DRC" "${LVS[$a]}" "${QRC[$r]}(124=up)" "${BOK[$r]}" "${GATE[$r]}" "${FAULT[$r]}" "$v"
done
echo
if [ "$ALL_OK" = 1 ] && [ "$BUILD_OK" = 1 ]; then
  echo "OVERALL: PASS -- all five arches build, both gates green, all six boot rows up with BOOT_OK, no real fault."
  echo "ALLDONE PASS"
else
  echo "OVERALL: FAIL -- see the matrix above; at least one row did not meet every column."
  echo "ALLDONE FAIL"
fi
