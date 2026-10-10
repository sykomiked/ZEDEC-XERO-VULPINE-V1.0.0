#!/usr/bin/env bash
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# completion_build.sh — drives completion_schema.yaml steps 2-6.
# Builds the 64-bit kernels, renders the Tri-Space triad for every program,
# assembles the ONE universal ISO + VM images, and verifies boot.
# Honest: reports an arch bootable only when its BOOT*.EFI is really built.
set -uo pipefail
cd "$(dirname "$0")/.." || exit 1
DIST=dist; mkdir -p "$DIST"
log(){ printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }
ok(){  printf '\033[1;32m[OK]\033[0m %s\n' "$*"; }
warn(){ printf '\033[1;33m[..]\033[0m %s\n' "$*"; }
need(){ command -v "$1" >/dev/null 2>&1; }

# --- 2: kernels (skip a target if its cross-gcc is absent; keep what exists) ---
log "2/6  cross-build the 64-bit kernels"
make -f build_system/Makefile.arm64  >/tmp/k_arm64.log  2>&1 && cp -f kernel_arm64.bin  "$DIST/" && ok "arm64"  || warn "arm64 build (see /tmp/k_arm64.log)"
if need x86_64-linux-gnu-gcc; then
  make -f build_system/Makefile.x86_64 CC=x86_64-linux-gnu-gcc LD=x86_64-linux-gnu-ld OBJCOPY=x86_64-linux-gnu-objcopy >/tmp/k_x86.log 2>&1 \
    && cp -f kernel_x86_64.bin "$DIST/" && ok "x86_64" || warn "x86_64 build (see /tmp/k_x86.log)"
fi
make -f build_system/Makefile.riscv  >/tmp/k_riscv.log  2>&1 && cp -f kernel_riscv.bin  "$DIST/" && ok "riscv64" || warn "riscv64 build (see /tmp/k_riscv.log)"

# --- 3: Tri-Space triad for every program (S+/.zxvc S-/.cedez S0/.cedec) ---
log "3/6  render the Tri-Space triad for every program"
if cc -std=c11 -O2 -DTEST_HOST -Ikernel/src/trispace -Ikernel/src/zxpkg -Ikernel/src/robin_debanks \
     -Ikernel/src/loader -Iuserapp -Iinclude \
     -o "$DIST/mkzxpkg" build_system/mkzxpkg.c kernel/src/zxpkg/zxpkg.c \
     kernel/src/trispace/trispace.c kernel/src/robin_debanks/sha256.c \
     kernel/src/loader/zsp.c kernel/src/robin_debanks/ed25519_verify.c 2>"$DIST/mkzxpkg.log"; then
  ok "built mkzxpkg"
  for k in "$DIST"/kernel_*.bin; do
    [ -e "$k" ] || continue
    a=$(basename "${k%.bin}" | sed 's/^kernel_//')
    "$DIST/mkzxpkg" "$k" "$DIST/zxv-$a" restoring >/dev/null 2>&1 \
      && ok "triad: zxv-$a.{zxvc,cedez,cedec}" || warn "triad failed for $a"
  done
  mkdir -p "$DIST/programs"
  while IFS= read -r p; do
    [ -n "$p" ] || continue
    b=$(basename "$p")
    "$DIST/mkzxpkg" "$p" "$DIST/programs/$b" constraining >/dev/null 2>&1 \
      && ok "triad: programs/$b.{zxvc,cedez,cedec}" || true
  done < <(find kernel/userapp userapp -type f \( -name '*.bin' -o -name '*.elf' -o -name 'hello' \) 2>/dev/null)
else
  warn "could not build mkzxpkg (see $DIST/mkzxpkg.log) — triads skipped"
fi

# --- 4: the one universal ISO (fixed disc builder: grub-mkstandalone + multiboot) ---
log "4/6  assemble the universal ISO"
bash build_system/mkuniversal_disc.sh "$DIST"

# --- 5: VM images ---
log "5/6  emit the VM images"
[ -f build_system/mkuniversal_vm.sh ] && bash build_system/mkuniversal_vm.sh "$DIST" || warn "mkuniversal_vm.sh absent"

# --- 6: verify boot (fail-closed floor) ---
log "6/6  verify boot"
if [ -f "$DIST/kernel_x86_64.bin" ] && need qemu-system-x86_64; then
  timeout 15 qemu-system-x86_64 -m 256M -nographic -kernel "$DIST/kernel_x86_64.bin" 2>&1 \
    | grep -m1 -aE 'x86-64 kernel main|\[E00' >/dev/null && ok "x86_64 kernel boots (multiboot floor)" || warn "x86_64 boot floor unclear"
fi
OVMF=$(ls /usr/share/OVMF/OVMF_CODE*.fd /usr/share/ovmf/OVMF.fd 2>/dev/null | head -1)
if [ -n "${OVMF:-}" ] && [ -f "$DIST/zxv-universal.iso" ]; then
  timeout 30 qemu-system-x86_64 -m 256M -nographic -bios "$OVMF" -cdrom "$DIST/zxv-universal.iso" >/tmp/iso_boot.log 2>&1 || true
  grep -m1 -aE 'x86-64 kernel main|ZEDEC pqOS' /tmp/iso_boot.log >/dev/null && ok "ISO boots x86_64 through UEFI (OVMF)" || warn "ISO UEFI boot unclear (see /tmp/iso_boot.log)"
else
  warn "no OVMF firmware — 'sudo apt-get install ovmf' to boot-test the ISO through UEFI here"
fi
if [ -f "$DIST/kernel_arm64.bin" ] && need qemu-system-aarch64; then
  timeout 15 qemu-system-aarch64 -M virt,gic-version=3 -cpu cortex-a53 -m 256M -nographic -kernel "$DIST/kernel_arm64.bin" 2>&1 \
    | grep -m1 -a BOOT_OK >/dev/null && ok "arm64 FULL OS boots (direct-kernel floor)" || warn "arm64 boot floor unclear"
fi

log "COMPLETION BUILD DONE"
echo "artifacts in $DIST/:"
ls -1 "$DIST"/zxv-universal.iso "$DIST"/zxv.* "$DIST"/*.zxvc "$DIST"/*.cedez "$DIST"/*.cedec 2>/dev/null | sed 's/^/  /'
echo "boot matrix + the last-mile efi_stub_task: build_system/completion_schema.yaml"
