#!/usr/bin/env bash
# mk_efi_disc.sh — assemble a REAL UEFI boot disc (GPT + FAT32 ESP) for one arch.
#
# THE HONEST MECHANISM
# --------------------
# A GPT disk carries one FAT32 EFI System Partition (type EF00). Inside the ESP,
# at the fixed removable-media path the firmware itself looks for, sits the
# arch's EFI-stub'd kernel:
#     x86_64 -> /EFI/BOOT/BOOTX64.EFI
#     arm64  -> /EFI/BOOT/BOOTAA64.EFI
# Alongside it, /MEGAROM.TVL — the valid TVUL container the kernel also embeds
# and registers at boot (identical bytes; see build_system/mk_megarom.c). UEFI
# firmware (OVMF for x86_64, QEMU_EFI for arm64) loads BOOT<arch>.EFI straight
# off this disc — a -drive/-cdrom media boot, NOT qemu -kernel.
#
#   bash build_system/mk_efi_disc.sh <x86_64|arm64>
#
# Produces dist/discs/zxv-<arch>-uefi.img (raw GPT disk) and MEGAROM.TVL.
set -uo pipefail
cd "$(dirname "$0")/.." || exit 1

ARCH="${1:?usage: mk_efi_disc.sh <x86_64|arm64>}"
case "$ARCH" in
  x86_64) EFI_SRC="BOOTX64.EFI";  EFI_DST="BOOTX64.EFI";;
  arm64)  EFI_SRC="BOOTAA64.EFI"; EFI_DST="BOOTAA64.EFI";;
  *) echo "mk_efi_disc: unknown arch '$ARCH' (x86_64|arm64 only)"; exit 2;;
esac

OUTDIR="dist/discs"
OUT="$OUTDIR/zxv-${ARCH}-uefi.img"
ROM="$OUTDIR/MEGAROM.TVL"
ESP="$OUTDIR/.esp-${ARCH}.img"
mkdir -p "$OUTDIR"

log(){ printf '\033[1;36m==> %s\033[0m\n' "$*"; }
die(){ printf '\033[1;31mERR: %s\033[0m\n' "$*"; exit 1; }

for t in dd mformat mmd mcopy sgdisk cc; do
  command -v "$t" >/dev/null 2>&1 || die "missing tool: $t"
done
[ -f "$EFI_SRC" ] || die "$EFI_SRC not present — build it first (make -f build_system/Makefile.$ARCH $EFI_SRC)"

# ---- 1. author MEGAROM.TVL once, from the REAL kernel container source -------
if [ ! -f "$ROM" ]; then
  log "building MEGAROM.TVL (host tool linking the kernel TVUL container source)"
  STUB="$(mktemp -d)"
  # sha256.c file-scope module declaration needs zxv_decl.h; the host tool does
  # not run the module graph, so the stub declares nothing. It still takes the
  # bring-up's address, so the function stays referenced (no unused warning).
  cat > "$STUB/zxv_decl.h" <<'EOF'
#ifndef ZXV_DECL_STUB_H
#define ZXV_DECL_STUB_H
#define ZXV_DECLARE(mname, PROV, REQ, BRING) \
    static int (*const zxv_host_bringup_##mname)(void) __attribute__((unused)) = BRING
#define ZXV_PROVIDES(...)
#define ZXV_REQUIRES(...)
#define ZXV_REQUIRES_NONE
#define ZXV_BRINGUP(fn) (fn)
#define ZXV_NO_BRINGUP ((int (*)(void))0)
#endif
EOF
  INCS=(-I kernel/src/tolvovina -I kernel/src/emu -I kernel/src/fractal \
        -I kernel/src/e8 -I kernel/src/robin_debanks -I kernel/src/trispace \
        -I "$STUB")
  cc -std=gnu11 -O2 -Wall "${INCS[@]}" -o "$OUTDIR/mk_megarom" \
     build_system/mk_megarom.c \
     kernel/src/tolvovina/tvl_rom.c \
     kernel/src/emu/dimfold.c \
     kernel/src/emu/megarom.c \
     kernel/src/robin_debanks/sha256.c \
     kernel/src/fractal/zorder.c \
     || die "host megarom tool failed to compile"
  "$OUTDIR/mk_megarom" "$ROM" || die "mk_megarom refused to write the ROM"
  rm -rf "$STUB"
fi
ROM_BYTES=$(wc -c < "$ROM")
log "MEGAROM.TVL = $ROM_BYTES bytes"

# ---- 2. stage the ESP (FAT32) -----------------------------------------------
ESP_MB="${ESP_MB:-300}"                   # >= 300 with 4KiB clusters: the bank blob EFI (59 MB) overflows the 512B-cluster FAT32 cap (~32 MB)
ESP_SECTORS=$((ESP_MB * 2048))            # 512-byte sectors
PART_START=2048                           # 1 MiB alignment
PART_END=$((PART_START + ESP_SECTORS - 1))
DISK_SECTORS=$((PART_END + 1 + 2048))     # + room for the secondary GPT

# Reproducible: the same commit gives the same disc bytes, so anyone can
# rebuild it and compare its SHA-256 with CI's (docs/VERIFY_BOOT.md). The
# kernel and stub are already deterministic; what varied was the wrapping:
# FAT timestamps (mtools honours SOURCE_DATE_EPOCH), the FAT volume serial
# and the GPT GUIDs, which are fixed here per arch.
export SOURCE_DATE_EPOCH="${SOURCE_DATE_EPOCH:-1767225600}"
case "$ARCH" in
  x86_64) SERIAL=0x5A580086 DISK_GUID=5A58560A-0000-4000-8000-000000086064 ;;
  arm64)  SERIAL=0x5A580064 DISK_GUID=5A58560A-0000-4000-8000-0000000A6400 ;;
esac
PART_GUID="${DISK_GUID%????????????}00000000E5F1"

log "formatting a ${ESP_MB} MiB FAT32 ESP"
dd if=/dev/zero of="$ESP" bs=1M count="$ESP_MB" status=none
mformat -i "$ESP" -F -c 8 -N "${SERIAL#0x}" :: 2>/dev/null || die "mformat (FAT32) failed"
mmd -i "$ESP" ::/EFI ::/EFI/BOOT 2>/dev/null || die "mmd failed"
mcopy -i "$ESP" "$EFI_SRC" "::/EFI/BOOT/$EFI_DST" || die "mcopy EFI failed"
mcopy -i "$ESP" "$ROM"    "::/MEGAROM.TVL"        || die "mcopy ROM failed"
log "ESP contents:"
mdir -i "$ESP" -/ :: 2>/dev/null | sed 's/^/    /'

# ---- 3. wrap the ESP in a GPT disk ------------------------------------------
log "building GPT disk ($((DISK_SECTORS/2048)) MiB) with one EF00 ESP"
dd if=/dev/zero of="$OUT" bs=512 count="$DISK_SECTORS" status=none
sgdisk -o \
  -n "1:${PART_START}:${PART_END}" -t 1:EF00 \
  -c 1:"EFI System Partition" -U "$DISK_GUID" -u "1:$PART_GUID" "$OUT" >/dev/null 2>&1 \
  || die "sgdisk partitioning failed"
dd if="$ESP" of="$OUT" bs=512 seek="$PART_START" conv=notrunc status=none
rm -f "$ESP"

log "DISC READY: $OUT ($(wc -c < "$OUT") bytes)"
sgdisk -p "$OUT" 2>/dev/null | sed 's/^/    /'
echo "  MegaROM on ESP: /MEGAROM.TVL ($ROM_BYTES bytes)"
echo "  EFI loader:     /EFI/BOOT/$EFI_DST"
exit 0
