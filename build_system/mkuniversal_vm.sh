#!/usr/bin/env bash
# mkuniversal_vm.sh — ONE VM file that every hypervisor can run.
#
# THE HONEST MECHANISM
# --------------------
# Hypervisors disagree on disk format (QEMU qcow2, VMware vmdk, Hyper-V vhdx,
# VirtualBox vdi), but nearly all of them accept a RAW disk image, and the
# cross-vendor portable-appliance standard is the OVA (a tar of an OVF
# descriptor + a disk). So the single most-universal artifact is the raw image
# the universal disc already is; on top of that we emit:
#   - zxv.raw          the one file every hypervisor can attach directly
#   - zxv.ova          the DMTF-standard appliance most platforms can import
#   - zxv.qcow2/.vmdk/.vhdx/.vdi   convenience conversions from the one raw,
#                                  produced only if qemu-img is present
#
# The raw image IS the universal disc from mkuniversal_disc.sh, so a VM boots
# exactly what a machine installs. One source of truth, many envelopes.
#
#   bash build_system/mkuniversal_vm.sh [DIST_DIR]
set -uo pipefail
DIST="${1:-dist}"
cd "$(dirname "$0")/.." || exit 1

log()  { printf '\033[1;36m==> %s\033[0m\n' "$*"; }
warn() { printf '\033[1;33m    %s\033[0m\n' "$*"; }
need() { command -v "$1" >/dev/null 2>&1; }

SRC="$DIST/zxv-universal.img"
[ -f "$SRC" ] || { warn "no universal disc at $SRC — run mkuniversal_disc.sh first"; exit 1; }

RAW="$DIST/zxv.raw"
cp "$SRC" "$RAW"
log "raw disk: $RAW ($(wc -c < "$RAW") bytes) — attachable by every hypervisor"

if need qemu-img; then
  for fmt in qcow2 vmdk vhdx vdi; do
    if qemu-img convert -O "$fmt" "$RAW" "$DIST/zxv.$fmt" 2>/dev/null; then
      log "converted -> zxv.$fmt"
    else warn "conversion to $fmt skipped"; fi
  done
else
  warn "qemu-img not present — only the raw image was produced (apt install qemu-utils)"
fi

# OVA: a tar of an OVF descriptor + the disk (use vmdk if we made one, else raw)
disk="zxv.raw"; [ -f "$DIST/zxv.vmdk" ] && disk="zxv.vmdk"
bytes=$(wc -c < "$DIST/$disk")
cat > "$DIST/zxv.ovf" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<Envelope xmlns="http://schemas.dmtf.org/ovf/envelope/1"
          xmlns:rasd="http://schemas.dmtf.org/wbem/wscim/1/cim-schema/2/CIM_ResourceAllocationSettingData">
  <References><File ovf:href="$disk" ovf:id="disk1" ovf:size="$bytes"
    xmlns:ovf="http://schemas.dmtf.org/ovf/envelope/1"/></References>
  <DiskSection xmlns:ovf="http://schemas.dmtf.org/ovf/envelope/1">
    <Info>ZXV universal disk</Info>
    <Disk ovf:diskId="vmdisk1" ovf:fileRef="disk1" ovf:capacity="$bytes"/>
  </DiskSection>
  <VirtualSystem ovf:id="ZXV" xmlns:ovf="http://schemas.dmtf.org/ovf/envelope/1">
    <Info>ZEDEC XERO VULPINE — universal appliance</Info>
    <Name>ZXV</Name>
  </VirtualSystem>
</Envelope>
EOF
if need tar; then
  ( cd "$DIST" && tar -cf zxv.ova zxv.ovf "$disk" ) && log "wrote $DIST/zxv.ova (portable appliance)"
else warn "tar not present — OVA skipped"; fi

log "VM SUMMARY"
( cd "$DIST" && ls -1 zxv.raw zxv.ova zxv.qcow2 zxv.vmdk zxv.vhdx zxv.vdi 2>/dev/null | sed 's/^/  /' )
echo "  attach zxv.raw anywhere; import zxv.ova on OVF-aware platforms."
exit 0
