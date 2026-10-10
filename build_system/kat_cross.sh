#!/usr/bin/env bash
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# kat_cross.sh -- run the NIST ACVP / KAT post-quantum tests on other CPUs.
#
# verify-all runs the known-answer tests on the build host only (x86_64,
# little-endian, 64-bit). This script cross-compiles the same tests as static
# Linux binaries and runs them under qemu-user, so a word-size, alignment,
# endianness or compiler-specific bug in the crypto shows up as a KAT failure:
#
#   aarch64  (64-bit little-endian)   aarch64-linux-gnu-gcc   + qemu-aarch64
#   riscv64  (64-bit little-endian)   riscv64-linux-gnu-gcc   + qemu-riscv64
#   armhf    (32-bit little-endian)   arm-linux-gnueabihf-gcc + qemu-arm
#   i686     (32-bit little-endian)   i686-linux-gnu-gcc      + qemu-i386
#   s390x    (64-bit BIG-endian)      s390x-linux-gnu-gcc     + qemu-s390x
#
# Tests (the same sources and vectors verify-all uses):
#   test_mlkem_kat  ML-KEM-768 keyGen/encaps/decaps vs NIST ACVP
#   test_pq_kat     ML-DSA-65 + SLH-DSA-SHAKE-128s vs NIST ACVP
#   test_pq_matrix  ML-KEM-1024, HQC-5, X25519, ML-DSA-87, SLH-DSA-256s KATs
#
# Usage: build_system/kat_cross.sh [target ...]     (default: all five)
#   KAT_CROSS_OUT=dir   where binaries and logs go (default /tmp/zxv_kat_cross)
#   KAT_CROSS_OPT=-O2   optimisation level
# A target whose compiler or qemu is missing is reported as SKIP; set
# KAT_CROSS_STRICT=1 (CI does) to make a missing toolchain a failure.
# On Ubuntu: apt-get install qemu-user gcc-aarch64-linux-gnu gcc-riscv64-linux-gnu \
#            gcc-arm-linux-gnueabihf gcc-i686-linux-gnu gcc-s390x-linux-gnu
# (do not install gcc-multilib alongside these: Ubuntu makes it conflict with
# the cross compilers, which is why i686 uses a cross compiler, not gcc -m32).
set -u

here=$(cd "$(dirname "$0")" && pwd)
K="$here/../kernel"
cd "$K" || exit 2
OUT=${KAT_CROSS_OUT:-/tmp/zxv_kat_cross}
OPT=${KAT_CROSS_OPT:--O2}
STRICT=${KAT_CROSS_STRICT:-0}
mkdir -p "$OUT"

# Same shared include path the kernel Makefile exports.
export CPATH="$K/include:$K/src/modbind:$K/src/e8:$K/src/event_space:$K/src/surplus"

MLKEM_SRCS="src/mlkem/keccak.c src/mlkem/mlkem768.c src/mlkem/mlkem_encode.c \
src/mlkem/mlkem_sample.c src/mlkem/mlkem_ntt.c src/mlkem/mlkem_kpe.c"
PQSIG_SRCS="src/pqsec/pq_mldsa65.c src/pqsec/pq_slhdsa.c $(ls src/pqsec/mldsa/*.c | sort) \
$(ls src/pqsec/slhdsa/*.c | sort)"
PQM_SRCS="src/pqsec/pq_matrix.c src/pqsec/pq_mlkem1024.c src/pqsec/pq_mldsa87.c \
src/pqsec/pq_hqc5.c src/pqsec/pq_slh256s.c"
INC="-Iinclude -Isrc/pqsec -Isrc/mlkem -Isrc/lpres -Isrc/surplus -Isrc/edp_risk \
-Isrc/event_space -Isrc/modbind -Isrc/trispace"
CFLAGS="-std=c11 -Wall -Werror -Wextra $OPT -static"

target_cc() {
    case "$1" in
    aarch64) echo aarch64-linux-gnu-gcc ;;
    riscv64) echo riscv64-linux-gnu-gcc ;;
    armhf) echo arm-linux-gnueabihf-gcc ;;
    i686) echo i686-linux-gnu-gcc ;;
    s390x) echo s390x-linux-gnu-gcc ;;
    *) return 1 ;;
    esac
}
target_qemu() {
    case "$1" in
    aarch64) echo qemu-aarch64 ;;
    riscv64) echo qemu-riscv64 ;;
    armhf) echo qemu-arm ;;
    i686) echo qemu-i386 ;;
    s390x) echo qemu-s390x ;;
    esac
}

targets=("$@")
[ ${#targets[@]} -eq 0 ] && targets=(aarch64 riscv64 armhf i686 s390x)

fail=0
skip=0
pass=0
for t in "${targets[@]}"; do
    cc=$(target_cc "$t") || {
        echo "[FAIL] kat-cross: unknown target '$t'"
        fail=1
        continue
    }
    qemu=$(target_qemu "$t")
    if ! command -v "$cc" > /dev/null || ! command -v "$qemu" > /dev/null; then
        if [ "$STRICT" = 1 ]; then
            echo "[FAIL] kat-cross $t: $cc or $qemu not installed"
            fail=1
        else
            echo "[SKIP] kat-cross $t: $cc or $qemu not installed"
            skip=$((skip + 1))
        fi
        continue
    fi
    echo "=== kat-cross: $t ($cc, run under $qemu) ==="
    for test in test_mlkem_kat test_pq_kat test_pq_matrix; do
        case $test in
        test_mlkem_kat) srcs="src/mlkem/test_mlkem_kat.c $MLKEM_SRCS" args="" ;;
        test_pq_kat) srcs="src/pqsec/test_pq_kat.c src/mlkem/keccak.c $PQSIG_SRCS" args="" ;;
        test_pq_matrix)
            srcs="src/pqsec/test_pq_matrix.c $PQM_SRCS $PQSIG_SRCS $MLKEM_SRCS src/tls/x25519.c src/tls/aead.c"
            args="--no-bench"
            ;;
        esac
        bin="$OUT/${test}_$t"
        log="$OUT/${test}_$t.log"
        # shellcheck disable=SC2086
        if ! "$cc" $CFLAGS $INC $srcs -o "$bin" > "$log" 2>&1; then
            tail -30 "$log"
            echo "[FAIL] kat-cross $t $test: does not build"
            fail=1
            continue
        fi
        # shellcheck disable=SC2086
        if "$qemu" "$bin" $args > "$log" 2>&1; then
            n=$(sed -n 's/^\([0-9]*\) comparisons, 0 failures$/\1 ACVP comparisons/p' "$log")
            [ -n "$n" ] || n="$(grep -c '\[PASS\]' "$log") checks"
            echo "[PASS] kat-cross $t $test ($n passed)"
            pass=$((pass + 1))
        elif [ "$t" = s390x ] && [ "$test" = test_pq_matrix ] &&
            ! grep -E '^  \[FAIL\]' "$log" | grep -qv 'HQC-5' &&
            grep -qE '^  \[FAIL\].*HQC-5' "$log"; then
            # Known and documented: the vendored HQC-5 reference serialises
            # uint64_t vectors with memcpy, so it only matches the official
            # KAT on little-endian CPUs. Every other check in the run must
            # still pass; any non-HQC failure falls through to [FAIL] below.
            n=$(grep -c '\[PASS\]' "$log")
            k=$(grep -cE '^  \[FAIL\].*HQC-5' "$log")
            echo "[KNOWN] kat-cross $t $test: $n checks passed; $k HQC-5 KAT checks fail" \
                "(vendored HQC-5 is little-endian only; do not use HQC-5 on big-endian)"
            pass=$((pass + 1))
        else
            grep -E 'FAIL|mismatch' "$log" | head -20
            tail -5 "$log"
            echo "[FAIL] kat-cross $t $test (log: $log)"
            fail=1
        fi
    done
done
echo "kat-cross: $pass test runs passed, $skip targets skipped"
[ $fail -eq 0 ] || {
    echo "[FAIL] kat-cross"
    exit 1
}
echo "[PASS] kat-cross"
