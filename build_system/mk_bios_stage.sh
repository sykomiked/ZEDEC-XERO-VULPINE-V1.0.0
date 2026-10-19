#!/usr/bin/env bash
# mk_bios_stage.sh — give a ZXV x86_64 UEFI disc a legacy-BIOS boot path too.
#
#   bash build_system/mk_bios_stage.sh <disc.img> [kernel_x86_64.elf]
#
# The discs from mk_efi_disc.sh / mk_golden_disc.sh are GPT with one FAT32 ESP
# starting at LBA 2048, so LBA 34..2047 is unused. This script, in place:
#
#   1. names that gap a BIOS boot partition (GPT type EF02, partition 2);
#      partition 1, the ESP, is not moved or touched;
#   2. builds a GRUB i386-pc core image whose embedded config is just
#      "multiboot (hd0,gpt1)/ZXV64.ELF; boot" — the same multiboot1 kernel
#      (kernel/arch/x86_64/boot.asm) the golden disc puts on the ESP — and
#      writes it at LBA 34. The kernel rides the FAT, not a memdisk: the
#      memdisk made the core image outgrow the gap the day the doorway was
#      embedded;
#   3. writes GRUB's boot.img into the MBR's 440 code bytes, pointed at LBA 34,
#      and keeps the protective MBR's partition table and signature.
#
# UEFI firmware still finds /EFI/BOOT/BOOTX64.EFI on the ESP and never looks
# at the MBR code; BIOS firmware (SeaBIOS on KVM hosts) runs the MBR, which
# loads the core image, which loads the same kernel the EFI stub embeds. The
# kernel then reads the disc with its own drivers either way.
#
# Needs grub-mkimage and the i386-pc modules (grub-pc-bin), sgdisk, dd and
# python3. grub-pc-bin exists only for x86 hosts: on an arm64 host run this in
# build_system/Dockerfile.bios (linux/amd64). Same inputs, same bytes out:
# grub-mkimage's output is fixed for a given GRUB build and module list.
set -euo pipefail
cd "$(dirname "$0")/.."

IMG="${1:?usage: mk_bios_stage.sh <disc.img> [kernel_x86_64.elf]}"
ELF="${2:-kernel_x86_64.elf}"
GRUB_DIR="${GRUB_DIR:-/usr/lib/grub/i386-pc}"
die(){ echo "mk_bios_stage: $*" >&2; exit 1; }

fsize(){ stat -c %s "$1" 2>/dev/null || stat -f %z "$1"; }
for t in sgdisk dd python3 tar; do command -v "$t" >/dev/null || die "needs $t"; done
[ -f "$IMG" ] || die "no disc $IMG"
[ -f "$ELF" ] || die "no kernel $ELF"
# the kernel must carry the multiboot1 header in its first 8 KiB
python3 - "$ELF" <<'PY' || die "$ELF has no multiboot1 header"
import struct, sys
d = open(sys.argv[1], "rb").read(8192)
ok = any(struct.unpack_from("<I", d, i)[0] == 0x1BADB002 and
         (sum(struct.unpack_from("<III", d, i)) & 0xffffffff) == 0 for i in range(0, len(d) - 11, 4))
sys.exit(0 if ok else 1)
PY

FIRST=34
ESP_START=$(sgdisk -i 1 "$IMG" | awk '/^First sector:/ {print $3}')
[ "$ESP_START" = 2048 ] || die "ESP starts at LBA $ESP_START, expected 2048"
LAST=$((ESP_START - 1))
GAP=$((ESP_START - FIRST))

# the kernel the config names must be on the ESP — mk_golden_disc.sh writes
# it as ZXV64.ELF; update it in-place with mcopy whenever available so a
# rebuilt kernel_x86_64.elf always lands on the BIOS boot path too.
if command -v mcopy >/dev/null 2>&1; then
  mcopy -i "$IMG@@$((ESP_START * 512))" -o "$ELF" ::/ZXV64.ELF
fi
dd if="$IMG" bs=512 skip="$ESP_START" count=131072 status=none 2>/dev/null \
  | grep -a "ZXV64   ELF" >/dev/null || die "$IMG does not carry ZXV64.ELF on its ESP"

# partition 2 = the gap, type BIOS boot; re-adding is a no-op if it is there
if ! sgdisk -i 2 "$IMG" 2>/dev/null | grep -q '21686148-6449-6E6F-744E-656564454649'; then
  sgdisk -a 1 -n "2:$FIRST:$LAST" -t 2:EF02 -c 2:"BIOS boot" "$IMG" >/dev/null || die "sgdisk failed"
fi

if command -v grub-mkimage >/dev/null 2>&1 && [ -f "$GRUB_DIR/boot.img" ]; then
  W=$(mktemp -d); trap 'rm -rf "$W"' EXIT
  cat > "$W/early.cfg" <<'CFG'
echo "ZXV: legacy BIOS boot, loading the multiboot kernel"
multiboot (hd0,gpt1)/ZXV64.ELF
boot
CFG
  grub-mkimage -d "$GRUB_DIR" -O i386-pc -o "$W/core.img" -p '(hd0,gpt1)/' \
    -c "$W/early.cfg" \
    biosdisk part_gpt fat multiboot echo boot
  CORE=$(fsize "$W/core.img")
  NSEC=$(( (CORE + 511) / 512 ))
  [ "$NSEC" -le "$GAP" ] || die "core image is $NSEC sectors, the gap holds $GAP"
  python3 - "$IMG" "$GRUB_DIR/boot.img" "$W/core.img" "$FIRST" "$NSEC" <<'PY'
import struct, sys
img, bootp, corep, first, nsec = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]), int(sys.argv[5])
boot = open(bootp, "rb").read()
core = bytearray(open(corep, "rb").read())
core += b"\0" * (nsec * 512 - len(core))
assert len(boot) == 512
# diskboot (core sector 0) blocklist: the rest of core, from the next sector
struct.pack_into("<QHH", core, 0x1F4, first + 1, nsec - 1, 0x820)
with open(img, "r+b") as f:
    mbr = bytearray(f.read(512))
    mbr[0:440] = boot[0:440]                    # code only; table + 0x55AA stay
    struct.pack_into("<Q", mbr, 0x5C, first)    # boot.img: LBA of the core image
    f.seek(0); f.write(mbr)
    f.seek(first * 512); f.write(core)
PY
else
  [ -f build_system/grub_bios_mbr.bin ] && [ -f build_system/grub_bios_core.bin ] \
    || die "no grub-mkimage and no prebuilt build_system/grub_bios_{mbr,core}.bin"
  CORE=$(fsize build_system/grub_bios_core.bin)
  NSEC=$(( (CORE + 511) / 512 ))
  python3 - "$IMG" build_system/grub_bios_mbr.bin build_system/grub_bios_core.bin "$FIRST" <<'PY'
import sys
img, mbrp, corep, first = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4])
boot = open(mbrp, "rb").read(440)
core = open(corep, "rb").read()
with open(img, "r+b") as f:
    mbr = bytearray(f.read(512))
    mbr[0:440] = boot
    f.seek(0); f.write(mbr)
    f.seek(first * 512); f.write(core)
PY
fi
sgdisk -v "$IMG" >/dev/null || die "GPT no longer verifies"
echo "mk_bios_stage: $IMG: BIOS boot partition LBA $FIRST..$LAST, core image $NSEC sectors ($CORE bytes), kernel $(fsize "$ELF") bytes"
