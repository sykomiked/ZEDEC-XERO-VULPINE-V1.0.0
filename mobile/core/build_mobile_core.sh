#!/usr/bin/env bash
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# build_mobile_core.sh — compile the ZEDEC mobile core into a static library
# for 64-bit ARM phones.
#
#   usage: build_mobile_core.sh [TARGET] [OUTDIR]
#     TARGET  linux-arm64     clang --target=aarch64-linux-gnu (size check, CI)
#             android-arm64   Android NDK clang, aarch64-linux-android26
#             ios-arm64       Xcode clang, iphoneos SDK, arm64, iOS 15+
#             ios-sim-arm64   Xcode clang, iphonesimulator SDK, arm64
#             ios             both iOS slices + ZXVCore.xcframework
#     OUTDIR  default mobile/core/build/<TARGET>
#
#   environment:
#     ZXV_PROFILE   lean (default) or full (adds the Carracho DHT)
#     ZXV_OPT       optimisation flag, default -Os
#     ANDROID_NDK_HOME / ANDROID_NDK_ROOT / ANDROID_HOME (NDK lookup)
#     ZXV_ANDROID_API  default 26
#     ZXV_NO_OPTIONAL=1  leave out the optional modules
#
# Output: OUTDIR/libzxvcore.a, OUTDIR/include/zxv_mobile.h,
#         OUTDIR/core_sources.txt (what went in), OUTDIR/size.txt
#
# The module set (kernel/src unless noted) and why each is there:
#   devmesh/        personal device mesh: pairing, roster, sessions, sync,
#                   routing, money confirmation, capacity-market glue
#   capmkt/         capacity market (double auction, escrow, pay on delivery)
#   pqsec/          pq_matrix: ML-KEM-1024 + HQC-5 + X25519, ML-DSA-87 +
#                   SLH-DSA-256s; ML-DSA-65 (STANDARD level, Carracho)
#   mlkem/          SHA3/SHAKE (keccak) and ML-KEM-768
#   tls/            ChaCha20-Poly1305 and X25519
#   ehop/           private-network channels (a devmesh carrier)
#   pay/            wallet ledger on the 555/777/888 rails, 0.08889% fee
#   swarm/          swarm_market (constants and splits pay_assure uses)
#   tensor/         zt (integer helpers), GGUF reader, tokenizer: local
#                   small-model files
#   mobile/core/    zxv_mobile.c, the flat C API for Kotlin and Swift
# Optional, compiled when present and when they build for the target:
#   tensor/zt_model*.c (on-device inference), i18n/*.c
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
K="$ROOT/kernel"
TARGET=${1:-linux-arm64}
OUT=${2:-$HERE/build/$TARGET}
PROFILE=${ZXV_PROFILE:-lean}
OPT=${ZXV_OPT:--Os}
API=${ZXV_ANDROID_API:-26}

if [ "$TARGET" = "ios" ]; then
    "$0" ios-arm64 "$HERE/build/ios-arm64"
    "$0" ios-sim-arm64 "$HERE/build/ios-sim-arm64"
    command -v xcodebuild >/dev/null || { echo "xcodebuild not found: cannot make the xcframework" >&2; exit 1; }
    rm -rf "$HERE/build/ZXVCore.xcframework"
    xcodebuild -create-xcframework \
        -library "$HERE/build/ios-arm64/libzxvcore.a" -headers "$HERE/build/ios-arm64/include" \
        -library "$HERE/build/ios-sim-arm64/libzxvcore.a" -headers "$HERE/build/ios-sim-arm64/include" \
        -output "$HERE/build/ZXVCore.xcframework"
    echo "wrote $HERE/build/ZXVCore.xcframework"
    exit 0
fi

# ---------------------------------------------------------------- sources
CORE=(
    devmesh/devmesh.c devmesh/dm_sync.c devmesh/dm_remote.c devmesh/dm_capmkt.c
    capmkt/capmkt.c
    pqsec/pq_matrix.c pqsec/pq_mlkem1024.c pqsec/pq_mldsa87.c pqsec/pq_hqc5.c pqsec/pq_slh256s.c
    pqsec/pq_mldsa65.c pqsec/pq_slhdsa.c
    mlkem/keccak.c mlkem/mlkem768.c mlkem/mlkem_encode.c mlkem/mlkem_sample.c mlkem/mlkem_ntt.c
    mlkem/mlkem_kpe.c
    tls/aead.c tls/x25519.c
    ehop/ehop_frame.c ehop/ehop_net.c ehop/ehop_sched.c
    pay/pay_util.c pay/pay_ledger.c pay/pay_tables.c pay/pay_assure.c
    swarm/swarm_market.c swarm/swarm_budget.c swarm/swarm_emotion.c
    tensor/zt.c tensor/zt_gguf.c tensor/zt_tok.c
)
# vendored ML-DSA / SLH-DSA reference files (mlkem1024/ and hqc5/ are
# #included by their wrappers and must not be compiled on their own)
for f in "$K"/src/pqsec/mldsa/*.c "$K"/src/pqsec/slhdsa/*.c; do CORE+=("${f#"$K"/src/}"); done
if [ "$PROFILE" = "full" ]; then
    CORE+=(carracho/carr_agree.c carracho/carr_cid.c carracho/carr_cmd.c carracho/carr_common.c
           carracho/carr_econ.c carracho/carr_file.c carracho/carr_frame.c carracho/carr_id.c
           carracho/carr_kad.c carracho/carr_node.c carracho/carr_schema.c carracho/carr_session.c
           carracho/carr_wire.c tls/hkdf.c robin_debanks/sha256.c swarm/swarm_hk.c ubh/ubh.c
           event_space/event_envelope.c zcapital/zcapital.c)
fi
OPTIONAL=()
[ "${ZXV_NO_OPTIONAL:-0}" = 1 ] || for f in "$K"/src/tensor/zt_model*.c "$K"/src/i18n/*.c; do
    [ -f "$f" ] || continue
    case "$(basename "$f")" in test_*|gen_*) continue ;; esac
    OPTIONAL+=("${f#"$K"/src/}")
done

INC=(-I"$K/include" -I"$K/src/pqsec" -I"$K/src/mlkem" -I"$K/src/lpres" -I"$K/src/surplus"
     -I"$K/src/edp_risk" -I"$K/src/event_space" -I"$K/src/modbind" -I"$K/src/trispace"
     -I"$K/src/pay" -I"$K/src/tensor" -I"$K/src/swarm" -I"$K/src/zcapital" -I"$K/src/ehop"
     -I"$K/src/robin_debanks" -I"$K/src/carracho" -I"$K/src/devmesh" -I"$K/src/capmkt")

# ---------------------------------------------------------------- toolchain
AR=""
case "$TARGET" in
linux-arm64)
    CC=${CC:-clang}
    TFLAGS=(--target=aarch64-linux-gnu)
    [ -n "${ZXV_SYSROOT:-}" ] && TFLAGS+=(--sysroot="$ZXV_SYSROOT")
    AR=$(command -v llvm-ar || command -v ar)
    ;;
android-arm64)
    NDK=${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-}}
    if [ -z "$NDK" ] && [ -n "${ANDROID_HOME:-}" ] && [ -d "$ANDROID_HOME/ndk" ]; then
        NDK=$(ls -d "$ANDROID_HOME"/ndk/* 2>/dev/null | sort -V | tail -1)
    fi
    [ -n "$NDK" ] && [ -d "$NDK" ] || { echo "Android NDK not found: set ANDROID_NDK_HOME" >&2; exit 1; }
    case "$(uname -s)" in Darwin) HOSTTAG=darwin-x86_64 ;; *) HOSTTAG=linux-x86_64 ;; esac
    BIN="$NDK/toolchains/llvm/prebuilt/$HOSTTAG/bin"
    CC="$BIN/clang"
    TFLAGS=(--target=aarch64-linux-android"$API")
    AR="$BIN/llvm-ar"
    ;;
ios-arm64|ios-sim-arm64)
    command -v xcrun >/dev/null || { echo "xcrun not found: iOS builds need Xcode on macOS" >&2; exit 1; }
    if [ "$TARGET" = ios-arm64 ]; then SDK=iphoneos; TRIPLE=arm64-apple-ios15.0; else SDK=iphonesimulator; TRIPLE=arm64-apple-ios15.0-simulator; fi
    CC=$(xcrun -sdk "$SDK" --find clang)
    TFLAGS=(-target "$TRIPLE" -isysroot "$(xcrun -sdk "$SDK" --show-sdk-path)")
    AR=$(xcrun -sdk "$SDK" --find ar)
    ;;
*) echo "unknown target $TARGET" >&2; exit 2 ;;
esac

CFLAGS=(-std=c11 "$OPT" -fPIC -ffunction-sections -fdata-sections -DZXV_MOBILE=1
        -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare)
STRICT=(-Werror)

rm -rf "$OUT"
mkdir -p "$OUT/obj" "$OUT/include"
: > "$OUT/core_sources.txt"

compile() { # src_path obj strict
    local src=$1 obj=$2 strict=$3
    if [ "$strict" = 1 ]; then
        "$CC" "${TFLAGS[@]}" "${CFLAGS[@]}" "${STRICT[@]}" "${INC[@]}" -c "$src" -o "$obj"
    else
        "$CC" "${TFLAGS[@]}" "${CFLAGS[@]}" "${INC[@]}" -c "$src" -o "$obj" 2>"$obj.log"
    fi
}

n=0
for s in "${CORE[@]}"; do
    obj="$OUT/obj/$(echo "$s" | tr '/' '_' | sed 's/\.c$/.o/')"
    case "$s" in devmesh/*|capmkt/*) strict=1 ;; *) strict=0 ;; esac
    if ! compile "$K/src/$s" "$obj" "$strict"; then
        [ -f "$obj.log" ] && cat "$obj.log" >&2
        echo "FAILED: $s" >&2
        exit 1
    fi
    echo "kernel/src/$s" >> "$OUT/core_sources.txt"
    n=$((n + 1))
done
compile "$HERE/zxv_mobile.c" "$OUT/obj/mobile_zxv_mobile.o" 1
echo "mobile/core/zxv_mobile.c" >> "$OUT/core_sources.txt"
n=$((n + 1))
skipped=0
for s in "${OPTIONAL[@]}"; do
    obj="$OUT/obj/opt_$(echo "$s" | tr '/' '_' | sed 's/\.c$/.o/')"
    if compile "$K/src/$s" "$obj" 0; then
        echo "kernel/src/$s (optional)" >> "$OUT/core_sources.txt"
        n=$((n + 1))
    else
        echo "skipped optional $s (does not build for $TARGET)" >&2
        rm -f "$obj"
        skipped=$((skipped + 1))
    fi
done
rm -f "$OUT"/obj/*.log

"$AR" rcs "$OUT/libzxvcore.a" "$OUT"/obj/*.o
cp "$HERE/zxv_mobile.h" "$OUT/include/"
cat > "$OUT/include/module.modulemap" <<'EOF'
module ZXVCore {
    header "zxv_mobile.h"
    link "zxvcore"
    export *
}
EOF

bytes=$(wc -c < "$OUT/libzxvcore.a")
{
    echo "target:   $TARGET ($PROFILE profile, $OPT)"
    echo "objects:  $n (optional skipped: $skipped)"
    echo "archive:  $bytes bytes"
    SIZE=$(command -v llvm-size || command -v size || true)
    if [ -n "$SIZE" ]; then
        "$SIZE" -t "$OUT"/obj/*.o 2>/dev/null | tail -1 | awk '{print "sections: text " $1 " data " $2 " bss " $3 " (all objects, before dead-code elimination)"}'
    fi
    # linked size: a probe app that calls every API, dead code removed
    if [ "$TARGET" = linux-arm64 ] && command -v aarch64-linux-gnu-gcc >/dev/null; then
        printf 'int main(void){return 0;}\n' > "$OUT/empty.c"
        aarch64-linux-gnu-gcc -Os -Wl,--gc-sections "$OUT/empty.c" -o "$OUT/empty"
        aarch64-linux-gnu-gcc -Os -I"$HERE" -Wl,--gc-sections "$HERE/size_probe.c" \
            "$OUT/libzxvcore.a" -o "$OUT/size_probe"
        if [ -n "$SIZE" ]; then
            e=$("$SIZE" "$OUT/empty" | tail -1 | awk '{print $1}')
            p=$("$SIZE" "$OUT/size_probe" | tail -1 | awk '{print $1 " " $2 " " $3}')
            set -- $p
            echo "linked:   text $(($1 - e)) bytes added to an app (data $2, bss $3, incl. one dm_mesh_t)"
        fi
        rm -f "$OUT/empty" "$OUT/empty.c"
    fi
} | tee "$OUT/size.txt"
