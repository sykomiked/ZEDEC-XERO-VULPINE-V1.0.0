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
# where a prebuilt GRUB EFI for that arch might already live on the build host
grub_efi() { case "$1" in
  x86_64) echo /usr/lib/grub/x86_64-efi/monolithic/grubx64.efi;;
  arm64)  echo /usr/lib/grub/arm64-efi/monolithic/grubaa64.efi;;
  *) echo "";; esac; }

log "staging the EFI System Partition tree"
rm -rf "$ESP"; mkdir -p "$ESP/EFI/BOOT" "$ESP/ZXV"

bootable=0; carried=0
cat > "$ESP/EFI/BOOT/grub.cfg" <<'EOF'
# ZXV universal disc — GRUB picks the entry for the firmware that loaded it.
set timeout=5
set default=0
EOF

shopt -s nullglob
for img in "$DIST"/kernel_*.bin "$DIST"/kernel_*.elf; do
  arch=$(basename "${img%.*}" | sed 's/^kernel_//')
  en=$(efi_name "$arch"); [ -n "$en" ] || { warn "$arch: no known EFI name, skipped"; continue; }
  mkdir -p "$ESP/ZXV/$arch"
  cp "$img" "$ESP/ZXV/$arch/"
  # carry the native triad if it was produced
  for ext in zxvc cedez cedec; do
    f="$DIST/zxv-$arch.$ext"; [ -f "$f" ] && cp "$f" "$ESP/ZXV/$arch/"
  done
  # install a boot payload if we have one for this arch
  ge=$(grub_efi "$arch")
  if [ -n "$ge" ] && [ -f "$ge" ]; then
    cp "$ge" "$ESP/EFI/BOOT/$en"
    printf 'menuentry "ZXV (%s)" { linux /ZXV/%s/%s }\n' "$arch" "$arch" "$(basename "$img")" \
      >> "$ESP/EFI/BOOT/grub.cfg"
    bootable=$((bootable+1)); log "$arch: $en installed (GRUB) + kernel + triad staged"
  else
    carried=$((carried+1))
    warn "$arch: kernel + triad staged, but NO EFI boot payload present ($en) — CARRIED, not yet bootable"
    warn "        provide grub-efi-$arch or an EFI stub, then re-run"
  fi
done
shopt -u nullglob

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
  xorriso -as mkisofs -R -J -e "$(basename "$OUT")" -no-emul-boot \
    -o "$DIST/zxv-universal.iso" "$DIST" >/dev/null 2>&1 \
    && log "also wrote $DIST/zxv-universal.iso" || warn "iso wrap skipped"
fi

log "DISC SUMMARY: $bootable arch(es) bootable, $carried carried-not-yet-bootable"
echo "  image: $OUT ($(wc -c < "$OUT" 2>/dev/null || echo 0) bytes)"
echo "  this file is BOTH an install disc and a raw VM disk."
[ "$bootable" -gt 0 ] || warn "no arch is bootable yet — install per-arch EFI payloads (see header)"
rm -rf "$ESP"
exit 0
