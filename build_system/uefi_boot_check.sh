#!/usr/bin/env bash
# uefi_boot_check.sh — boot a UEFI disc image under QEMU firmware once and fail
# closed unless the serial log reached [BOOT_OK] with no fault marker.
#
#   build_system/uefi_boot_check.sh <arm64|x86_64> [image] [seconds] [marker...]
#
#   image    default dist/discs/zxv-<arch>-uefi.img (from baremetal_images.sh)
#   seconds  how long QEMU runs before timeout stops it (default 60)
#   marker   extra literal lines the log must also contain, e.g. [AI_OK]
#
# Unlike ci_boot_check.sh (qemu -kernel), this is a real media boot: the
# firmware (OVMF for x86_64, QEMU_EFI for arm64) finds /EFI/BOOT/BOOT<arch>.EFI
# on the disc's ESP by itself. QEMU opens a COPY of the image with snapshot=on,
# so the disc file is never modified. Same exit and fault-marker rules as
# ci_boot_check.sh: only a timeout (124) is a normal QEMU exit.
#
# UEFI_SCREENSHOT=<file.png> also saves the screen a few seconds before the
# timeout (qemu_screendump.py over a monitor socket; arm64 gets a ramfb
# display for it). The serial log stays the evidence; the picture is a
# courtesy and never fails the check.
#
# UEFI_DISK=virtio|ahci|nvme picks how the disc is attached (default virtio).
# The x86_64 kernel's own storage drivers read AHCI and NVMe, not virtio, so a
# golden disc, whose models the kernel reads after boot, boots with ahci or
# nvme. UEFI_MEM sets the guest's RAM (default 512M; golden discs are also
# booted with 4G and 8G). UEFI_EXTRA="<img> ..." attaches more discs the
# same way after the boot disc, read-only snapshots too: the swarm discs that
# go with a golden core disc.
set -u

ARCH=${1:?usage: uefi_boot_check.sh <arm64|x86_64> [image] [seconds] [marker...]}
IMG=${2:-dist/discs/zxv-${ARCH}-uefi.img}
SECS=${3:-60}
shift $(( $# < 3 ? $# : 3 ))
LOG="uefi_boot_${ARCH}.log"
# Both discs boot through the stub's handoff v2 (memory map, ACPI, clock,
# kernel window); the kernel must have accepted it, not merely booted. x86_64
# must also have run its ring-3 processes under the preemptive scheduler, with
# the deliberately faulting ones ended alone ([SCHED_OK], x86_userspace.c).
set -- "$@" "[HANDOFF_OK]"
[ "$ARCH" = x86_64 ] && set -- "$@" "[SCHED_OK]"
FAULT_RE='\[FAULT\]|\[EL0 FAULT\]|CPU exception vector=|PANIC|[Kk]ernel panic'

firmware(){ for f in "$@"; do [ -f "$f" ] && { echo "$f"; return; }; done; }
case "$ARCH" in
  x86_64)
    FW=$(firmware "${OVMF:-}" /usr/share/ovmf/OVMF.fd /usr/share/OVMF/OVMF_CODE.fd \
                  /opt/homebrew/share/qemu/edk2-x86_64-code.fd /usr/local/share/qemu/edk2-x86_64-code.fd)
    QEMU=(qemu-system-x86_64 -machine q35 -m "${UEFI_MEM:-512M}") ;;
  arm64)
    FW=$(firmware "${AAVMF:-}" /usr/share/qemu-efi-aarch64/QEMU_EFI.fd /usr/share/AAVMF/AAVMF_CODE.fd \
                  /opt/homebrew/share/qemu/edk2-aarch64-code.fd /usr/local/share/qemu/edk2-aarch64-code.fd)
    QEMU=(qemu-system-aarch64 -M virt,gic-version=3 -cpu cortex-a57 -m "${UEFI_MEM:-512M}") ;;
  *) echo "::error::unknown arch $ARCH (arm64|x86_64)"; exit 2 ;;
esac
[ -n "$FW" ] || { echo "::error::$ARCH: no UEFI firmware found (install ovmf / qemu-efi-aarch64)"; exit 1; }
[ -f "$IMG" ] || { echo "::error::$ARCH: disc image $IMG missing (run baremetal_images.sh)"; exit 1; }

# edk2 *-code.fd builds are flash images and must go in as pflash, padded to
# 64 MiB on arm64; a plain -bios works for the distro .fd files.
case "$FW" in
  *edk2-aarch64-code.fd)
    PF="$(mktemp)"; cp "$FW" "$PF"; truncate -s 64M "$PF"
    FWARGS=(-drive "if=pflash,format=raw,readonly=on,file=$PF") ;;
  *edk2-x86_64-code.fd|*OVMF_CODE.fd)
    FWARGS=(-drive "if=pflash,format=raw,readonly=on,file=$FW") ;;
  *) FWARGS=(-bios "$FW") ;;
esac

# attach <drive id> <image> <index>: the boot disc is index 0
attach(){
  DISK+=(-drive "if=none,id=$1,format=raw,snapshot=on,file=$2")
  local boot=""; [ "$3" = 0 ] && boot=",bootindex=0"
  case "${UEFI_DISK:-virtio}" in
    virtio) DISK+=(-device "virtio-blk-pci,drive=$1$boot") ;;
    ahci)   [ "$3" = 0 ] && DISK+=(-device ich9-ahci,id=zahci)
            DISK+=(-device "ide-hd,drive=$1,bus=zahci.$3$boot") ;;
    nvme)   DISK+=(-device "nvme,serial=ZXVDISC$3,drive=$1$boot") ;;
    *) echo "::error::UEFI_DISK=${UEFI_DISK} (virtio|ahci|nvme)"; exit 2 ;;
  esac
}
DISK=(); attach disc "$IMG" 0; i=1
for x in ${UEFI_EXTRA:-}; do
  [ -f "$x" ] || { echo "::error::$ARCH: extra disc $x missing"; exit 1; }
  [ "$i" -le 5 ] || { echo "::error::at most 5 extra discs (six AHCI ports)"; exit 2; }
  attach "disc$i" "$x" "$i"; i=$((i + 1))
done

MON=(-monitor none); SHOT_PID=
if [ -n "${UEFI_SCREENSHOT:-}" ]; then
  SOCK="$(mktemp -u)"; MON=(-monitor "unix:$SOCK,server,nowait")
  [ "$ARCH" = arm64 ] && MON+=(-device ramfb)
  python3 "$(dirname "$0")/qemu_screendump.py" "$SOCK" "$(( SECS > 8 ? SECS - 5 : SECS / 2 ))" \
    "$UEFI_SCREENSHOT" & SHOT_PID=$!
fi

rc=0
timeout "$SECS" "${QEMU[@]}" "${FWARGS[@]}" \
  "${DISK[@]}" \
  -net none -display none -serial stdio "${MON[@]}" \
  < /dev/null > "$LOG" 2>&1 || rc=$?
[ -n "$SHOT_PID" ] && { wait "$SHOT_PID" || true; rm -f "$SOCK"; }
[ -n "${PF:-}" ] && rm -f "$PF"
cat "$LOG"
echo "[uefi-boot] $ARCH: QEMU exited $rc after at most ${SECS}s (firmware $FW, disc on ${UEFI_DISK:-virtio}, ${UEFI_MEM:-512M} RAM)"

fail=0
if [ "$rc" -ne 0 ] && [ "$rc" -ne 124 ]; then
  echo "::error::$ARCH UEFI boot: QEMU failed with exit $rc (not a timeout)"; fail=1
fi
if ! grep -aq '\[BOOT_OK\]' "$LOG"; then
  echo "::error::$ARCH UEFI boot did not reach [BOOT_OK]"; fail=1
fi
for m in "$@"; do
  grep -aqF -- "$m" "$LOG" || { echo "::error::$ARCH UEFI boot did not print required marker $m"; fail=1; }
done
if grep -aqE "$FAULT_RE" "$LOG"; then
  echo "::error::$ARCH UEFI boot printed a fault marker:"; grep -anE "$FAULT_RE" "$LOG" | head -5; fail=1
fi
[ "$fail" -eq 0 ] || exit 1
echo "[uefi-boot] $ARCH: [BOOT_OK] reached from the UEFI disc, no fault marker"
