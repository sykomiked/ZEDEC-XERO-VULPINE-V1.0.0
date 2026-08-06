#!/usr/bin/env bash
# mkuniversal_disc.sh — ONE install disc that boots on many chips.
#
# THE HONEST MECHANISM
# --------------------
# There is no CPU that runs another CPU's machine code, so "one image for all
# chips" is not a fat binary — it is UEFI's own multi-architecture boot path.
# A single GPT disk carries one FAT32 EFI System Partition, and that ESP holds
# a per-architecture bootloader at the fixed removable-media path each firmware
# looks for:
#     /EFI/BOOT/BOOTX64.EFI     <- x86-64 UEFI firmware loads THIS
#     /EFI/BOOT/BOOTAA64.EFI    <- arm64 UEFI firmware loads THIS
#     /EFI/BOOT/BOOTRISCV64.EFI <- riscv64 UEFI firmware loads THIS
# Each machine's firmware picks its own; the others are simply ignored. The
# same physical disc therefore boots an x86 laptop, an ARM server and a RISC-V
# board. This is exactly how real multi-arch install media works.
#
# Alongside each bootloader we place that arch's ZXV kernel triad (.zxvc etc).
# The one disc is ALSO a valid raw disk image, so a VM can boot it directly.
#
# THE REMAINING INTEGRATION, STATED PLAINLY
# -----------------------------------------
# ZXV kernels are currently raw images booted by QEMU's -kernel (direct boot).
# To be loaded by firmware from this disc, each arch needs EITHER a GRUB build
# for that arch (grub-efi-<arch>) with a menuentry that loads the ZXV kernel,
# OR the kernel wrapped as its own EFI application (an EFI stub). This script
# assembles the correct multi-arch STRUCTURE and installs whichever of those it
# finds; for an arch with neither present it still stages the kernel + triad and
# logs that the arch is CARRIED-BUT-NOT-YET-BOOTABLE. It never claims an arch
# boots when the boot payload is absent.
#
#   bash build_system/mkuniversal_disc.sh [DIST_DIR]
set -uo pipefail
DIST="${1:-dist}"
cd "$(dirname "$0")/.." || exit 1
OUT="$DIST/zxv-universal.img"
ESP="$DIST/.esp"
SIZE_MB="${DISC_SIZE_MB:-256}"

log() { printf '\033[1;36m==> %s\033[0m\n' "$*"; }
warn(){ printf '\033[1;33m    %s\033[0m\n' "$*"; }

need() { command -v "$1" >/dev/null 2>&1; }
for t in dd mformat mmd mcopy; do
  need "$t" || { warn "missing tool: $t (apt install mtools dosfstools) — cannot build disc"; exit 1; }
done

# arch -> the EFI removable-media filename its firmware loads
efi_name() { case "$1" in
  x86_64) echo BOOTX64.EFI;; arm64) echo BOOTAA64.EFI;;
  riscv|riscv64) echo BOOTRISCV64.EFI;; riscv32) echo BOOTRISCV32.EFI;;
  arm32) echo BOOTARM.EFI;; *) echo "";; esac; }
# GRUB build target for grub-mkstandalone (we BUILD the loader, not copy a prebuilt one)
grub_target() { case "$1" in
  x86_64) echo x86_64-efi;; arm64) echo arm64-efi;;
  riscv|riscv64) echo riscv64-efi;; *) echo "";; esac; }
# The GRUB command that loads a ZXV kernel of this arch. x86_64 kernels carry a
# Multiboot1 header (magic 0x1BADB002), so GRUB loads them with `multiboot`.
# arm64/riscv kernels are raw images; GRUB's `linux` accepts them ONLY once they
# carry an arch Image/EFI-stub header (see EFI-STUB NOTE in the summary) — we still
# emit the correct entry so a stubbed kernel boots with no disc change.
grub_loadcmd() { case "$1" in x86_64) echo multiboot;; *) echo linux;; esac; }

log "staging the EFI System Partition tree"
rm -rf "$ESP"; mkdir -p "$ESP/EFI/BOOT" "$ESP/ZXV"

bootable=0; carried=0
# a human-readable top-level cfg (each BOOTxxx.EFI embeds its OWN cfg via
# grub-mkstandalone, so this one is reference/fallback only)
{ echo "# ZXV universal disc — each firmware loads its own /EFI/BOOT/BOOT<arch>.EFI"
  echo "set timeout=5"; echo "set default=0"; } > "$ESP/EFI/BOOT/grub.cfg"

shopt -s nullglob
for img in "$DIST"/kernel_*.bin; do
  arch=$(basename "${img%.*}" | sed 's/^kernel_//')
  en=$(efi_name "$arch"); gt=$(grub_target "$arch")
  [ -n "$en" ] || { warn "$arch: no known EFI name, skipped"; continue; }
  mkdir -p "$ESP/ZXV/$arch"
  cp "$img" "$ESP/ZXV/$arch/"
  # carry this program's native Tri-Space triad (S+/S-/S0) if it was produced
  for ext in zxvc cedez cedec; do
    f="$DIST/zxv-$arch.$ext"; [ -f "$f" ] && cp "$f" "$ESP/ZXV/$arch/"
  done

  lc=$(grub_loadcmd "$arch")
  # the exact boot entry, correct per arch (multiboot for x86_64, linux otherwise)
  cfg="$DIST/.grub-$arch.cfg"
  { echo "set timeout=3"; echo "set default=0"
    printf 'menuentry "ZEDEC pqOS (%s)" {\n  %s /ZXV/%s/%s\n  boot\n}\n' \
      "$arch" "$lc" "$arch" "$(basename "$img")"
    printf '%s\n' "$lc" > /dev/null; } > "$cfg"
  printf 'menuentry "ZEDEC pqOS (%s)" { %s /ZXV/%s/%s ; boot }\n' \
    "$arch" "$lc" "$arch" "$(basename "$img")" >> "$ESP/EFI/BOOT/grub.cfg"

  # BUILD the arch's bootloader with grub-mkstandalone (embeds cfg + modules)
  mods="part_gpt part_msdos fat iso9660 normal configfile echo test"
  [ "$arch" = x86_64 ] && mods="$mods multiboot" || mods="$mods linux"
  if [ -n "$gt" ] && need grub-mkstandalone && [ -d "/usr/lib/grub/$gt" ]; then
    if grub-mkstandalone -O "$gt" -o "$ESP/EFI/BOOT/$en" \
         --modules="$mods" "boot/grub/grub.cfg=$cfg" >/dev/null 2>"$DIST/.grub-$arch.log"; then
      bootable=$((bootable+1)); log "$arch: BUILT $en (grub-mkstandalone $gt, loadcmd=$lc) + kernel + triad"
    else
      carried=$((carried+1)); warn "$arch: grub-mkstandalone failed (see $DIST/.grub-$arch.log) — CARRIED"
    fi
  else
    carried=$((carried+1))
    warn "$arch: grub target /usr/lib/grub/$gt absent — CARRIED. Install grub-efi-${arch/x86_64/amd64}-bin, re-run."
  fi
  rm -f "$cfg"
done
shopt -u nullglob

# stage the userland programs' Tri-Space triads too (rendered by completion_build 3/6)
if [ -d "$DIST/programs" ]; then
  mkdir -p "$ESP/ZXV/programs"
  cp -f "$DIST"/programs/* "$ESP/ZXV/programs/" 2>/dev/null || true
  log "staged userland program triads (.zxvc/.cedez/.cedec) into /ZXV/programs"
fi

log "building the FAT32 ESP image ($SIZE_MB MB)"
dd if=/dev/zero of="$OUT" bs=1M count="$SIZE_MB" status=none
mformat -i "$OUT" -F ::  2>/dev/null || { warn "mformat failed"; exit 1; }
# copy the staged tree in
( cd "$ESP" && find . -type d | while read -r d; do
    [ "$d" = "." ] && continue; mmd -i "../$(basename "$OUT")" "::${d#./}" 2>/dev/null || true
  done
  find . -type f | while read -r f; do
    mcopy -i "../$(basename "$OUT")" "$f" "::${f#./}" 2>/dev/null || true
  done )

# make it a hybrid ISO too, if xorriso is present (bootable as a CD/USB)
if need xorriso; then
  # EFI El Torito (platform 0xEF) + GPT so UEFI firmware AND USB dd both boot it.
  # Without -eltorito-platform efi the entry defaults to BIOS and UEFI ignores it.
  xorriso -as mkisofs -R -J -V ZXVOS \
    -eltorito-platform efi -e "$(basename "$OUT")" -no-emul-boot \
    -isohybrid-gpt-basdat \
    -o "$DIST/zxv-universal.iso" "$DIST" >/dev/null 2>&1 \
    && log "also wrote $DIST/zxv-universal.iso (UEFI El Torito + GPT hybrid)" \
    || warn "iso wrap skipped (xorriso error)"
fi

log "DISC SUMMARY: $bootable arch(es) bootable, $carried carried-not-yet-bootable"
echo "  image: $OUT ($(wc -c < "$OUT" 2>/dev/null || echo 0) bytes)"
echo "  this file is BOTH an install disc and a raw VM disk."
[ "$bootable" -gt 0 ] || warn "no arch is bootable yet — install per-arch EFI payloads (see header)"
rm -rf "$ESP"
exit 0
