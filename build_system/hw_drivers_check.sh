#!/usr/bin/env bash
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# hw_drivers_check.sh — boot the x86_64 UEFI disc in QEMU with one of every
# standard device the class drivers cover (kernel/src/hwdrv), and fail closed
# unless each driver binds and passes its boot self-test.
#
#   build_system/hw_drivers_check.sh [image] [seconds]
#
#   image    default dist/discs/zxv-x86_64-uefi.img (baremetal_images.sh x86_64)
#   seconds  QEMU time limit (default 120)
#
# Devices (q35 machine, OVMF firmware):
#   virtio-blk   -device virtio-blk-pci  the boot disk
#   virtio-scsi  -device virtio-scsi-pci + scsi-hd (the disc again), a KVM cloud's disk
#   NVMe         -device nvme            the disc image again, read-only copy
#   AHCI         q35's built-in ICH9 AHCI with the disc as a SATA disk
#   xHCI         qemu-xhci + usb-kbd + usb-storage (the disc again)
#   e1000/e1000e/igb/virtio-net  four NICs on QEMU user networking, whose DHCP server answers
#   HD Audio     intel-hda + hda-duplex on a null audio backend
# Keys are typed into the USB keyboard through the QEMU monitor while the
# input self-test polls, so the log has to show the typed text.
#
# Every disk is the same image opened with snapshot=on: nothing is written to
# the file. Every self-test reads LBA 1 and must find the GPT header.
set -u
cd "$(dirname "$0")/.."

IMG=${1:-dist/discs/zxv-x86_64-uefi.img}
SECS=${2:-120}
LOG=hw_drivers_x86_64.log
MON=$(mktemp -u /tmp/zxv-hwmon.XXXXXX)
FAULT_RE='\[FAULT\]|\[EL0 FAULT\]|CPU exception vector=|PANIC|[Kk]ernel panic'

firmware(){ for f in "$@"; do [ -f "$f" ] && { echo "$f"; return; }; done; }
FW=$(firmware "${OVMF:-}" /usr/share/ovmf/OVMF.fd /usr/share/OVMF/OVMF_CODE.fd \
              /opt/homebrew/share/qemu/edk2-x86_64-code.fd /usr/local/share/qemu/edk2-x86_64-code.fd)
[ -n "$FW" ] || { echo "::error::no OVMF firmware found (install ovmf)"; exit 1; }
[ -f "$IMG" ] || { echo "::error::disc image $IMG missing (run baremetal_images.sh x86_64)"; exit 1; }
case "$FW" in
  *edk2-x86_64-code.fd|*OVMF_CODE.fd) FWARGS=(-drive "if=pflash,format=raw,readonly=on,file=$FW") ;;
  *) FWARGS=(-bios "$FW") ;;
esac

# QEMU grew an igb (82576) model in 8.0; older QEMU (Ubuntu 22.04 has 6.2)
# runs the check with the two e1000-family NICs only.
IGB=(); NICS=3
if qemu-system-x86_64 -device help 2>/dev/null | grep -q '"igb"'; then
  IGB=(-netdev user,id=n2 -device igb,netdev=n2,mac=52:54:00:5a:58:03); NICS=4
fi

disk(){ echo "if=none,id=$1,format=raw,snapshot=on,file=$IMG"; }

: > "$LOG"
timeout "$SECS" qemu-system-x86_64 -machine q35 -m 512M "${FWARGS[@]}" \
  -drive "$(disk boot)" -device virtio-blk-pci,drive=boot,bootindex=0 \
  -device virtio-scsi-pci,id=vs \
  -drive "$(disk vsd)"  -device scsi-hd,drive=vsd,bus=vs.0 \
  -drive "$(disk nv)"   -device nvme,drive=nv,serial=ZXVNVME0 \
  -drive "$(disk sata)" -device ide-hd,drive=sata,bus=ide.0 \
  -device qemu-xhci,id=xhci \
  -device usb-kbd,bus=xhci.0 \
  -drive "$(disk usb)"  -device usb-storage,bus=xhci.0,drive=usb \
  -netdev user,id=n0 -device e1000,netdev=n0,mac=52:54:00:5a:58:01 \
  -netdev user,id=n1 -device e1000e,netdev=n1,mac=52:54:00:5a:58:02 \
  -netdev user,id=n3 -device virtio-net-pci,netdev=n3,mac=52:54:00:5a:58:04 \
  "${IGB[@]}" \
  -audiodev none,id=snd0 -device intel-hda -device hda-duplex,audiodev=snd0 \
  -display none -serial stdio -monitor "unix:$MON,server,nowait" \
  < /dev/null > "$LOG" 2>&1 &
QPID=$!

# Type "zxv" into the USB keyboard while the input self-test is polling: wait
# for the probe summary line, then send the keys a few times (the first may
# land before the keyboard's interrupt endpoint is armed).
python3 -I - "$MON" "$LOG" "$SECS" <<'PY' &
import os, socket, sys, time
mon, log, secs = sys.argv[1], sys.argv[2], int(sys.argv[3])
end = time.time() + secs
def seen(s):
    try:
        with open(log, 'rb') as f: return s in f.read()
    except OSError: return False
while time.time() < end and not seen(b'class drivers:'):
    time.sleep(0.05)
for _ in range(40):
    if time.time() > end or seen(b'key press'): break
    try:
        c = socket.socket(socket.AF_UNIX); c.connect(mon)
        for k in ('z', 'x', 'v'):
            c.sendall(('sendkey %s\n' % k).encode()); time.sleep(0.05)
        c.close()
    except OSError:
        pass
    time.sleep(0.25)
PY
KPID=$!

rc=0; wait "$QPID" || rc=$?
kill "$KPID" 2>/dev/null; wait "$KPID" 2>/dev/null
rm -f "$MON"
grep -aE '\[MSR|\[DRIVER|\[SELFTEST|\[STORAGE|\[SED\]|class drivers|hardware self-tests|\[BOOT_OK\]' "$LOG"
echo "[hw-drivers] QEMU exited $rc after at most ${SECS}s"

fail=0
need(){ grep -aqF -- "$1" "$LOG" || { echo "::error::missing: $1"; fail=1; }; }
if [ "$rc" -ne 0 ] && [ "$rc" -ne 124 ]; then echo "::error::QEMU failed with exit $rc"; fail=1; fi
need '[BOOT_OK]'
# Fault-safe MSR layer (kernel/arch/x86_64/msr_safe.h): valid MSRs read, a
# rejected MSR access #GPs, is recovered through the exception-fixup table, and
# the boot goes on. QEMU TCG never faults an RDMSR at CPL0 (unknown MSRs read
# as 0), so the read line accepts either outcome and the reserved-bit WRMSR
# (IA32_PKRS, which TCG and every real CPU fault) is what must recover here.
need '[MSR OK] safe rdmsr: valid read ok'
need '[MSR OK] safe wrmsr: invalid MSR write faulted and recovered'
need '[MSR OK] fixup table: rdmsr and wrmsr registered with the #GP handler'
need '[MSR] RAPL '
if grep -aq '\[MSR FAIL\]' "$LOG"; then echo "::error::MSR self-test failed:"; grep -a '\[MSR FAIL\]' "$LOG"; fail=1; fi
need '[DRIVER ONLINE] nvme'
need '[DRIVER ONLINE] ahci'
need '[DRIVER ONLINE] xhci'
need '[DRIVER ONLINE] e1000'
[ "$NICS" -eq 3 ] || need '[DRIVER ONLINE] igb'
need '[DRIVER ONLINE] vblk'
need '[DRIVER ONLINE] vscsi'
need '[DRIVER ONLINE] vnet'
need '[DRIVER ONLINE] hda'
# QEMU's NVMe has no Opal: the SED unlock must say so and move on (hw_sed.c).
need 'not an SED (Security Receive refused), nothing to unlock'
# Five disks (virtio-blk, virtio-scsi, NVMe, SATA, USB) each read the disc's GPT header.
n=$(grep -ac "read LBA 1: GPT header 'EFI PART'" "$LOG")
[ "$n" -ge 5 ] || { echo "::error::only $n of 5 disks read the GPT header"; fail=1; }
# Every NIC got a DHCP offer from QEMU's user-mode network.
n=$(grep -ac 'DHCP offer received' "$LOG")
[ "$n" -ge "$NICS" ] || { echo "::error::only $n of $NICS NICs got a DHCP offer"; fail=1; }
# ...and every disk mounted the ESP as FAT32 and read the MegaROM file off it.
n=$(grep -ac '\[STORAGE OK\].*read /MEGAROM.TVL' "$LOG")
[ "$n" -ge 5 ] || { echo "::error::only $n of 5 disks mounted the ESP and read /MEGAROM.TVL"; fail=1; }
need 'output stream DMA position'
grep -aE '\[SELFTEST OK\].*key press\(es\): ".*z' "$LOG" >/dev/null || { echo "::error::USB keyboard did not deliver the typed keys"; fail=1; }
if grep -aq '\[DRIVER FAILED\]\|\[SELFTEST FAILED\]\|\[STORAGE FAILED\]' "$LOG"; then
  echo "::error::a driver or self-test failed:"; grep -a '\[DRIVER FAILED\]\|\[SELFTEST FAILED\]\|\[STORAGE FAILED\]' "$LOG"; fail=1
fi
if grep -aqE "$FAULT_RE" "$LOG"; then
  echo "::error::fault marker:"; grep -anE "$FAULT_RE" "$LOG" | head -5; fail=1
fi
[ "$fail" -eq 0 ] || exit 1
echo "[hw-drivers] every class driver bound and passed its self-test"
