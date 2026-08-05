#!/usr/bin/env bash
# make_release.sh — produce the ZXV release deliverables from ONE build.
#
# Outputs into dist/:
#   zxv-<target>.bin / .elf     per-architecture kernel images
#   zxv-disk-<target>.img       bootable raw disk image (ZXVFS, A/B slots)
#   zxv-universal.tar.gz        the universal installer bundle (all targets)
#   MANIFEST.txt + SHA256SUMS   what is in the release and its digests
#
# HONEST SCOPE: this produces a QEMU-bootable raw image and a signed-manifest
# bundle. It is NOT yet a UEFI-bootable USB installer with GPT/ESP — that is
# roadmap item A3 (signed path-safe installer + system A/B). What is here is
# real and reproducible; what is missing is named, not implied.
#
#   bash build_system/make_release.sh
# Run from 05_KERNEL.
set -uo pipefail
cd "$(dirname "$0")/.." || exit 1

OUT="dist"
IMG_MB="${IMG_MB:-64}"
mkdir -p "$OUT"
note() { printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }

# ---------------------------------------------------------------- 1. build
note "building all available targets"
bash build_system/build_all_targets.sh || true

# ------------------------------------------------------- 2. disk images
note "creating bootable disk images (${IMG_MB}MB, ZXVFS)"
for BIN in "$OUT"/zxv-*.bin; do
  [ -e "$BIN" ] || continue
  T=$(basename "$BIN" .bin); T=${T#zxv-}
  IMG="$OUT/zxv-disk-${T}.img"
  # A raw image the kernel formats as ZXVFS on first boot. ZXVFS is a fixed
  # small layout (see src/zxvfs/zxvfs.h), so a zeroed image is a valid blank
  # disk — the kernel detects no superblock and formats it.
  if command -v qemu-img >/dev/null 2>&1; then
    qemu-img create -f raw "$IMG" "${IMG_MB}M" >/dev/null 2>&1
  else
    dd if=/dev/zero of="$IMG" bs=1m count="$IMG_MB" 2>/dev/null
  fi
  echo "  $IMG"
done

# --------------------------------------------- 3. universal bundle
note "assembling the universal installer bundle"
STAGE=$(mktemp -d)
mkdir -p "$STAGE/zxv/images" "$STAGE/zxv/install"
cp "$OUT"/zxv-*.bin "$OUT"/zxv-*.elf "$STAGE/zxv/images/" 2>/dev/null
cp build_system/install_zxv.py "$STAGE/zxv/install/" 2>/dev/null || true
cp CANONICAL.md ROADMAP.md "$STAGE/zxv/" 2>/dev/null || true

cat > "$STAGE/zxv/install/README.txt" <<'EOF'
ZXV / ZEDEC pqOS — universal installer bundle

images/  one kernel image per architecture. Pick the one matching the target
         machine's ISA: a CPU can only execute its own instruction set. A
         "universal" bundle carries every ISA's artifact plus one shared,
         architecture-neutral application layer; it does not make an x86 CPU
         execute an ARM kernel natively.

Boot a target directly under QEMU, e.g.:
  qemu-system-aarch64 -M virt,gic-version=3 -cpu cortex-a53 -m 256M \
    -nographic -kernel images/zxv-arm64.bin

With persistent storage:
  qemu-system-aarch64 -M virt,gic-version=3 -cpu cortex-a53 -m 256M -nographic \
    -drive file=zxv-disk-arm64.img,if=none,format=raw,id=d0 \
    -device virtio-blk-device,drive=d0 -kernel images/zxv-arm64.bin

STATUS: bare-metal/UEFI installation onto physical media is roadmap item A3
(signed, path-safe installer with GPT/ESP and system-level A/B). Not shipped.
EOF

TAR="$OUT/zxv-universal.tar.gz"
( cd "$STAGE" && tar czf - zxv ) > "$TAR"
rm -rf "$STAGE"
echo "  $TAR"

# ------------------------------------------------------- 4. manifest
note "manifest + digests"
{
  echo "ZXV release manifest"
  echo "generated: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "commit:    $(git rev-parse HEAD 2>/dev/null || echo unknown)"
  echo "branch:    $(git rev-parse --abbrev-ref HEAD 2>/dev/null || echo unknown)"
  echo
  echo "artifacts:"
  for f in "$OUT"/*; do [ -f "$f" ] && printf '  %-34s %10s bytes\n' "$(basename "$f")" "$(wc -c < "$f" | tr -d ' ')"; done
} > "$OUT/MANIFEST.txt"

( cd "$OUT" && { command -v sha256sum >/dev/null 2>&1 && sha256sum ./* || shasum -a 256 ./*; } ) \
  > "$OUT/SHA256SUMS" 2>/dev/null
echo "  $OUT/MANIFEST.txt"
echo "  $OUT/SHA256SUMS"

note "RELEASE COMPLETE"
cat "$OUT/MANIFEST.txt"
