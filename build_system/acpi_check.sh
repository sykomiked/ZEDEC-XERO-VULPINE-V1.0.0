#!/usr/bin/env bash
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# acpi_check.sh — boot the x86_64 UEFI disc in QEMU (q35 + OVMF) and fail
# closed unless the ACPI layer (kernel/src/acpi: tables, AML interpreter,
# power management) works on QEMU's real firmware tables:
#
#   build_system/acpi_check.sh [image] [seconds]
#
#   image    default dist/discs/zxv-x86_64-uefi.img (baremetal_images.sh x86_64)
#   seconds  QEMU time limit per boot (default 120)
#
# Three boots, each selected by a QEMU fw_cfg file the kernel reads only when
# the ACPI OEM is QEMU's ("BOCHS ") and the fw_cfg signature is present:
#
#   1. opt/zxv/acpi_pwrbtn=1   the boot report ([ACPI OK] namespace, \_S5_,
#      S3, power button, processors), then the kernel polls the fixed power
#      button; the monitor's system_powerdown presses it and the kernel must
#      power off through S5 (QEMU exits on its own).
#   2. opt/zxv/acpi_poweroff=1 S5 straight after the report: QEMU must exit.
#   3. opt/zxv/acpi_s3=1 with ICH9-LPC.disable_s3=0: the kernel enters S3 and
#      the monitor must report the guest "suspended". system_wakeup is then
#      sent; resuming into the kernel is NOT implemented (no FACS waking
#      vector), so what happens after the wake is logged, not required.
#
# Batteries, lids, thermal zones, backlight and CPPC are absent from QEMU's
# q35 tables; the host tests (make -C kernel test-aml) cover those.
set -u
cd "$(dirname "$0")/.."

IMG=${1:-dist/discs/zxv-x86_64-uefi.img}
SECS=${2:-120}
LOGDIR=${ACPI_LOGDIR:-build/acpi_check}
mkdir -p "$LOGDIR"
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

fail=0
need(){ grep -aqF -- "$2" "$1" || { echo "::error::$1: missing: $2"; fail=1; }; }
nofault(){
  if grep -aqE "$FAULT_RE" "$1"; then
    echo "::error::$1: fault marker:"; grep -anE "$FAULT_RE" "$1" | head -5; fail=1
  fi
}

# boot <log> <monitor-script> <qemu args...>: run QEMU with a monitor socket
# and a Python helper that drives it; sets RC to QEMU's exit status.
boot(){
  local log=$1 script=$2; shift 2
  local mon; mon=$(mktemp -u /tmp/zxv-acpimon.XXXXXX)
  : > "$log"
  timeout "$SECS" qemu-system-x86_64 -machine q35 -smp 2 -m 512M "${FWARGS[@]}" \
    -drive "if=none,id=boot,format=raw,snapshot=on,file=$IMG" \
    -device virtio-blk-pci,drive=boot,bootindex=0 \
    -display none -serial stdio -monitor "unix:$mon,server,nowait" "$@" \
    < /dev/null > "$log" 2>&1 &
  local qpid=$!
  python3 -I -c "$script" "$mon" "$log" "$SECS" &
  local hpid=$!
  RC=0; wait "$qpid" || RC=$?
  wait "$hpid" 2>/dev/null
  rm -f "$mon"
}

# Shared helper text: wait for a log line, talk to the monitor.
PYLIB='
import socket, sys, time
mon, log, secs = sys.argv[1], sys.argv[2], int(sys.argv[3])
end = time.time() + secs
def seen(s):
    try:
        with open(log, "rb") as f: return s in f.read()
    except OSError: return False
def wait_for(s):
    while time.time() < end and not seen(s): time.sleep(0.05)
    return seen(s)
def cmd(c):
    try:
        k = socket.socket(socket.AF_UNIX); k.settimeout(2); k.connect(mon)
        k.sendall((c + "\n").encode()); time.sleep(0.3)
        out = b""
        try:
            while True:
                d = k.recv(4096)
                if not d: break
                out += d
        except OSError: pass
        k.close(); return out.decode("latin-1")
    except OSError: return ""
def note(s):
    with open(log, "ab") as f: f.write(("\n[acpi-check] " + s + "\n").encode())
'

# ---- 1: report, then the power button powers the machine off -------------
L1=$LOGDIR/acpi_x86_64_pwrbtn.log
boot "$L1" "$PYLIB
if wait_for(b'waiting for the power button'):
    time.sleep(0.5)
    for _ in range(20):
        cmd('system_powerdown'); time.sleep(1)
        if seen(b'power button pressed'): break
" -fw_cfg name=opt/zxv/acpi_pwrbtn,string=1
grep -aE '^\[ACPI' "$L1"
echo "[acpi-check] boot 1 (power button): QEMU exited $RC"
need "$L1" '[ACPI OK] namespace: '
need "$L1" '[ACPI] \_S5_ SLP_TYPa='
grep -aqE '^\[ACPI\] S3 (supported|absent)' "$L1" || { echo "::error::no S3 line"; fail=1; }
need "$L1" '[ACPI] power button: fixed feature'
grep -aqE '^\[ACPI\] processors: [2-9]' "$L1" || { echo "::error::-smp 2 but fewer than 2 processors found"; fail=1; }
need "$L1" '[ACPI] power button pressed -> S5'
need "$L1" '[ACPI] entering S5'
[ "$RC" -eq 0 ] || { echo "::error::QEMU did not power off on the power button (exit $RC)"; fail=1; }
grep -aqF 'S5 did not take effect' "$L1" && { echo "::error::S5 did not take effect"; fail=1; }
grep -aq 'load errors' "$L1" && ! grep -aq ' 0 load errors' "$L1" && { echo "::error::AML load errors on QEMU's tables"; fail=1; }
nofault "$L1"

# ---- 2: S5 straight after the report -------------------------------------
L2=$LOGDIR/acpi_x86_64_s5.log
boot "$L2" "$PYLIB" -fw_cfg name=opt/zxv/acpi_poweroff,string=1
echo "[acpi-check] boot 2 (S5): QEMU exited $RC"
need "$L2" '[ACPI] fw_cfg acpi_poweroff: entering S5'
[ "$RC" -eq 0 ] || { echo "::error::QEMU did not power off (exit $RC)"; fail=1; }
grep -aqF 'S5 did not take effect' "$L2" && { echo "::error::S5 did not take effect"; fail=1; }
nofault "$L2"

# ---- 3: S3 entry ---------------------------------------------------------
L3=$LOGDIR/acpi_x86_64_s3.log
boot "$L3" "$PYLIB
if wait_for(b'entering S3'):
    st = ''
    for _ in range(50):
        st = cmd('info status')
        if 'suspended' in st: break
        time.sleep(0.2)
    note('monitor after S3 entry: ' + ('suspended' if 'suspended' in st else 'NOT suspended'))
    if 'suspended' in st:
        cmd('system_wakeup'); time.sleep(5)
        st = cmd('info status')
        note('monitor 5 s after system_wakeup: ' + ('running' if 'running' in st else 'other'))
cmd('quit')
" -global ICH9-LPC.disable_s3=0 -fw_cfg name=opt/zxv/acpi_s3,string=1
echo "[acpi-check] boot 3 (S3): QEMU exited $RC"
grep -a '\[acpi-check\]\|S3' "$L3" | grep -av '^\[E' | tail -5
need "$L3" '[ACPI] S3 supported'
need "$L3" '[ACPI] fw_cfg acpi_s3: entering S3'
need "$L3" '[acpi-check] monitor after S3 entry: suspended'
nofault "$L3"

[ "$fail" -eq 0 ] || exit 1
echo "[acpi-check] ACPI namespace, S5 (power button and direct) and S3 entry verified in QEMU"
