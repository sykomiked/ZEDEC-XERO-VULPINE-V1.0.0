#!/bin/bash
# ci_gate.sh — fail-closed release gate for the ZXV ARM64 kernel.
#
# Runs, in order, and STOPS on the first failure:
#   1. clean ARM64 kernel build (default config)
#   2. host verification suite (make verify-all)
#   3. adversarial parser fuzzers under ASan+UBSan (ELF, ZXVFS)
#   4. N-boot QEMU harness (every boot must reach BOOT_OK, no faults)
#
# A required step that is absent or errors fails the gate — it never
# silently skips (audit: "a release target must fail closed when
# required tests are missing").
#
#   build_system/ci_gate.sh [BOOTS]     (default 100)
#
# Run from 05_KERNEL/.
set -u
BOOTS=${1:-100}
FUZZ_ELF_ITERS=${FUZZ_ELF_ITERS:-200000}
FUZZ_ZXVFS_ITERS=${FUZZ_ZXVFS_ITERS:-60000}
here=$(cd "$(dirname "$0")/.." && pwd)   # 05_KERNEL/
cd "$here" || exit 1

fail() { echo ""; echo "*** CI GATE FAILED: $1 ***"; exit 1; }

echo "=== [1/4] clean ARM64 build ==="
make -f build_system/Makefile.arm64 clean >/dev/null 2>&1
make -f build_system/Makefile.arm64 all  >/tmp/ci_build.log 2>&1 \
    || { tail -20 /tmp/ci_build.log; fail "ARM64 build"; }
echo "  build OK"

echo "=== [2/4] host verify-all ==="
( cd kernel && make verify-all >/tmp/ci_verify.log 2>&1 ) \
    || { tail -20 /tmp/ci_verify.log; fail "verify-all"; }
grep -q "ALL STAGE-1 CHECKS PASSED" /tmp/ci_verify.log \
    || fail "verify-all did not report ALL STAGE-1 CHECKS PASSED"
echo "  verify-all OK ($(grep -c '\[PASS\]' /tmp/ci_verify.log) checks)"

echo "=== [3/4] adversarial parser fuzzers (ASan+UBSan) ==="
SAN="-O1 -g -fsanitize=address,undefined"
cd kernel || fail "kernel dir"
cc $SAN -Isrc/loader tests/fuzz/fuzz_elf.c src/loader/elf.c -o /tmp/ci_fuzz_elf \
    2>/tmp/ci_fuzz_elf_build.log || { cat /tmp/ci_fuzz_elf_build.log; fail "fuzz_elf build"; }
/tmp/ci_fuzz_elf "$FUZZ_ELF_ITERS" || fail "fuzz_elf found a defect"
cc $SAN -DZXVFS_HOST -Iinclude -Isrc/zxvfs tests/fuzz/fuzz_zxvfs.c src/zxvfs/zxvfs.c \
    -o /tmp/ci_fuzz_zxvfs 2>/tmp/ci_fuzz_zxvfs_build.log \
    || { cat /tmp/ci_fuzz_zxvfs_build.log; fail "fuzz_zxvfs build"; }
/tmp/ci_fuzz_zxvfs "$FUZZ_ZXVFS_ITERS" || fail "fuzz_zxvfs found a defect"
cd "$here" || exit 1
echo "  fuzzers OK"

echo "=== [4/4] $BOOTS-boot QEMU harness ==="
bash build_system/ci_boot_harness.sh "$BOOTS" || fail "boot harness"

echo ""
echo "*** CI GATE PASSED (build + verify-all + fuzz + $BOOTS/$BOOTS boots) ***"
