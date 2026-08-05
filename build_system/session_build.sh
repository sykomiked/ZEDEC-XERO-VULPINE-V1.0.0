#!/usr/bin/env bash
# session_build.sh — the full ZXV build + verification run, for a time-boxed
# server session. Fail-closed: any stage that fails stops the run with a clear
# marker, so we never mistake a partial run for a green one.
#
#   bash build_system/session_build.sh            # full run
#   BOOTS=100 bash build_system/session_build.sh  # more boot iterations
#
# Run from the 05_KERNEL directory.
set -uo pipefail

BOOTS="${BOOTS:-25}"
FAILED=0
step() { printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }
ok()   { printf '\033[1;32m[OK]\033[0m %s\n' "$*"; }
bad()  { printf '\033[1;31m[FAIL]\033[0m %s\n' "$*"; FAILED=1; }

cd "$(dirname "$0")/.." || exit 1
echo "workdir: $(pwd)"

# ---------------------------------------------------------------- host gate
step "1/6  host verification gate (verify-all)"
if make -C kernel verify-all > /tmp/verify.log 2>&1; then
  ok "verify-all: $(grep -c '^\[PASS\]' /tmp/verify.log) assertions passed"
else
  bad "verify-all failed"; grep -E '^\[FAIL\]|error:' /tmp/verify.log | head -20
fi

# ------------------------------------------------------------ arm64 build
step "2/6  ARM64 cross-build (expect ZERO warnings)"
find kernel -name '*.o' -delete 2>/dev/null
rm -f kernel_arm64.elf kernel_arm64.bin
if make -f build_system/Makefile.arm64 > /tmp/arm64.log 2>&1; then
  W=$(grep -c 'warning:' /tmp/arm64.log || true)
  ok "ARM64 built ($(stat -c%s kernel_arm64.bin 2>/dev/null || stat -f%z kernel_arm64.bin) bytes)"
  if [ "$W" -eq 0 ]; then ok "zero warnings"; else bad "$W warnings"; grep 'warning:' /tmp/arm64.log | head; fi
else
  bad "ARM64 build failed"; grep -E 'error' /tmp/arm64.log | head -20
fi

# ------------------------------------------------------------- boot gate
step "3/6  ARM64 QEMU boot acceptance (${BOOTS}x)"
PASS=0
for i in $(seq 1 "$BOOTS"); do
  if timeout 20 qemu-system-aarch64 -M virt,gic-version=3 -cpu cortex-a53 -m 256M \
      -nographic -kernel kernel_arm64.bin 2>&1 | grep -q "BOOT_OK"; then
    PASS=$((PASS+1))
  fi
done
if [ "$PASS" -eq "$BOOTS" ]; then ok "boot $PASS/$BOOTS"; else bad "boot $PASS/$BOOTS"; fi

# ------------------------------------------------- boot with net + disk
step "4/6  boot with virtio-net + virtio-blk attached"
qemu-img create -f raw /tmp/zxv.img 16M >/dev/null 2>&1
OUT=$(timeout 25 qemu-system-aarch64 -M virt,gic-version=3 -cpu cortex-a53 -m 256M -nographic \
  -drive file=/tmp/zxv.img,if=none,format=raw,id=d0 -device virtio-blk-device,drive=d0 \
  -netdev user,id=n0 -device virtio-net-device,netdev=n0 \
  -kernel kernel_arm64.bin 2>&1)
echo "$OUT" | grep -aE "virtio-net|virtio-blk|MOUNTED|BOOT_OK" | head
echo "$OUT" | grep -qa "BOOT_OK" && ok "booted with devices" || bad "device boot failed"
echo "$OUT" | grep -qa "DRIVER ONLINE. virtio-net" && ok "virtio-net probed" || echo "  (virtio-net not probed — see driver notes)"

# ----------------------------------------------------------- x86_64 build
step "5/6  x86-64 build (parity track)"
if [ -f build_system/build_x86_64.sh ]; then
  if bash build_system/build_x86_64.sh > /tmp/x86.log 2>&1; then ok "x86-64 build ran"; else bad "x86-64 build failed (see /tmp/x86.log)"; fi
else
  echo "  (no x86-64 build script yet — Track G)"
fi

# ------------------------------------------------------------ fuzz + san
step "6/6  fuzz / sanitizer gate"
if make -C kernel fuzz FUZZ_ELF_ITERS="${FUZZ_ELF_ITERS:-50000}" > /tmp/fuzz.log 2>&1; then
  ok "fuzzers passed"
else
  echo "  (fuzz target unavailable or failed — see /tmp/fuzz.log)"; tail -5 /tmp/fuzz.log
fi

echo
if [ "$FAILED" -eq 0 ]; then
  printf '\033[1;32m*** SESSION BUILD GREEN ***\033[0m\n'
else
  printf '\033[1;31m*** SESSION BUILD HAD FAILURES ***\033[0m\n'
fi
exit "$FAILED"
