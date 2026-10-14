#!/usr/bin/env bash
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# ai_cross.sh -- grammar-constrained decoding and the evidence head give the
# same bits on every CPU.
#
# Builds test_zt_gram (grammar compiler, matcher, token masks) and
# test_zt_evh (evidence head, fixed-point fit, paradox bridge) for the host
# and as static binaries for aarch64, riscv64, armhf (32-bit), i686 (32-bit)
# and s390x (64-bit big-endian), runs the cross builds under qemu-user, and
# compares the digest lines each test prints (every mask, every fitted
# weight) with the host's. Same toolchains as kat_cross.sh; a missing one is
# a SKIP unless KAT_CROSS_STRICT=1 (CI sets it).
set -u

here=$(cd "$(dirname "$0")" && pwd)
K="$here/../kernel"
cd "$K" || exit 2
OUT=${AI_CROSS_OUT:-/tmp/zxv_ai_cross}
STRICT=${KAT_CROSS_STRICT:-0}
mkdir -p "$OUT"

AI_SRCS="src/tensor/zt_gram.c src/tensor/zt_gram_lib.c src/tensor/zt_evh.c src/tensor/zt_tok.c \
src/tensor/zt_gguf.c src/tensor/zt.c"
GRAM="-Isrc/tensor -Isrc/treaty src/tensor/test_zt_gram.c $AI_SRCS src/treaty/zxt.c"
EVH="-Iinclude -Isrc/tensor -Isrc/paradox -Isrc/lpres -Isrc/event_space -Isrc/modbind -Isrc/surplus \
src/tensor/test_zt_evh.c $AI_SRCS src/paradox/paradox.c src/lpres/lpres_hex.c src/lpres/lpres.c \
src/robin_debanks/sha256.c"
CFLAGS="-std=c11 -Wall -Werror -Wextra -O2 -DTEST_HOST"

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

# build and run one test; prints its digest lines into $OUT/<test>_<target>.dig
run_one() { # cc qemu test srcs target
    local cc=$1 qemu=$2 test=$3 srcs=$4 t=$5 static=
    [ -n "$qemu" ] && static=-static
    local bin="$OUT/${test}_$t" log="$OUT/${test}_$t.log"
    # shellcheck disable=SC2086
    if ! "$cc" $CFLAGS $static $srcs -lm -o "$bin" > "$log" 2>&1; then
        tail -30 "$log"
        echo "[FAIL] ai-cross $t $test: does not build"
        return 1
    fi
    # shellcheck disable=SC2086
    if ! $qemu "$bin" > "$log" 2>&1; then
        tail -30 "$log"
        echo "[FAIL] ai-cross $t $test: $(tail -1 "$log")"
        return 1
    fi
    grep "digest" "$log" > "$OUT/${test}_$t.dig"
    [ -s "$OUT/${test}_$t.dig" ] || {
        echo "[FAIL] ai-cross $t $test: no digest line"
        return 1
    }
}

fail=0
for test in test_zt_gram test_zt_evh; do
    srcs=$GRAM
    [ $test = test_zt_evh ] && srcs=$EVH
    run_one gcc "" "$test" "$srcs" host || fail=1
done
[ $fail = 0 ] || exit 1
echo "[PASS] ai-cross host: $(cat "$OUT"/test_zt_gram_host.dig "$OUT"/test_zt_evh_host.dig | tr '\n' ' ')"

targets=("$@")
[ ${#targets[@]} -eq 0 ] && targets=(aarch64 riscv64 armhf i686 s390x)
skip=0
for t in "${targets[@]}"; do
    cc=$(target_cc "$t") || {
        echo "[FAIL] ai-cross: unknown target '$t'"
        fail=1
        continue
    }
    qemu=$(target_qemu "$t")
    if ! command -v "$cc" > /dev/null || ! command -v "$qemu" > /dev/null; then
        if [ "$STRICT" = 1 ]; then
            echo "[FAIL] ai-cross $t: $cc or $qemu not installed"
            fail=1
        else
            echo "[SKIP] ai-cross $t: $cc or $qemu not installed"
            skip=$((skip + 1))
        fi
        continue
    fi
    ok=1
    for test in test_zt_gram test_zt_evh; do
        srcs=$GRAM
        [ $test = test_zt_evh ] && srcs=$EVH
        if ! run_one "$cc" "$qemu" "$test" "$srcs" "$t"; then
            ok=0
            continue
        fi
        if ! cmp -s "$OUT/${test}_host.dig" "$OUT/${test}_$t.dig"; then
            echo "[FAIL] ai-cross $t $test: digest differs from the host"
            diff "$OUT/${test}_host.dig" "$OUT/${test}_$t.dig"
            ok=0
        fi
    done
    if [ $ok = 1 ]; then
        echo "[PASS] ai-cross $t: same masks and fitted weights as the host"
    else
        fail=1
    fi
done
[ $fail = 0 ] || exit 1
echo "[PASS] ai-cross: grammar masks and evidence-head fits identical on host and ${#targets[@]} targets ($skip skipped)"
