#!/bin/sh
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# build_zxvd.sh OUTDIR — build zxvd: the hosted swarm (the same sources as
# zxv-host, from app_sources.sh) with -DZXVD, its JSON-RPC socket
# (zxvd_rpc.c) and the web4 JSON tokenizer, linked against libzxv's shared
# library through its frozen C ABI (sdk/libzxv/include/zxv_api.h).
#
# libzxv is linked as a shared library on purpose: it is built from kernel
# sources with its own flags (TEST_HOST number types, hidden visibility), so
# a static link beside the app's own copies of the same kernel files could
# mix two layouts of one struct. Behind the ABI, each side keeps its own.
#
# Writes OUTDIR/zxvd and OUTDIR/libzxv.{so,dylib} (found next to zxvd via
# rpath), or OUTDIR/zxvd.exe and OUTDIR/libzxv.dll for a Windows target
# (CC=x86_64-w64-mingw32-gcc, from Linux or from MSYS2).
#
#   CC=clang SANITIZE=1 kernel/arch/hosted/build_zxvd.sh /tmp/zxvd
#   CC="xcrun clang -arch x86_64 -mmacosx-version-min=10.15" ... (one slice of
#   the Mac app's universal engine; build_desktop.sh joins them with lipo)
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
K="$(cd "$HERE/../.." && pwd)"
R="$(cd "$K/.." && pwd)"
OUT="${1:?usage: build_zxvd.sh OUTDIR}"
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"
CC="${CC:-cc}"

SAN=""
if [ -n "${SANITIZE:-}" ]; then
    SAN="-fsanitize=address,undefined -fno-omit-frame-pointer"
fi

# 1. libzxv, the ABI neck (its own Makefile; SANITIZE passes through)
# (built under OUTDIR, so a sanitized and a plain build never share objects)
make -C "$R/sdk/libzxv" CC="$CC" OUT="$OUT/libzxv-build" ${SANITIZE:+SANITIZE=1} \
    >"$OUT/libzxv-build.log" 2>&1 ||
    { tail -30 "$OUT/libzxv-build.log"; echo "[FAIL] libzxv build"; exit 1; }
# The compiler's target decides (a MinGW cross compiler builds for Windows
# from Linux): zxvd.exe finds libzxv.dll beside it, Windows' own rule.
EXE=""
OSLIBS=""
OSDEFS=""
case "$($CC -dumpmachine 2>/dev/null)" in
    *mingw*|*windows*)
        LIBF=libzxv.dll; RP=""; EXE=.exe
        OSLIBS="-lws2_32 -lshell32 -lbcrypt -ladvapi32 -static-libgcc"
        OSDEFS="-D__USE_MINGW_ANSI_STDIO=1 -D_WIN32_WINNT=0x0A00"
        ;;
    # Both rpaths at link time (next to the binary, and Contents/Frameworks
    # in ZXV.app), with header room: the x86_64 slice has none left for a
    # later install_name_tool -add_rpath.
    *darwin*|*apple*)
        LIBF=libzxv.dylib
        RP='-Wl,-rpath,@loader_path,-rpath,@executable_path/../Frameworks,-headerpad_max_install_names' ;;
    *)                LIBF=libzxv.so;    RP='-Wl,-rpath,$ORIGIN' ;;
esac
cp "$OUT/libzxv-build/$LIBF" "$OUT/$LIBF"

# 2. zxvd: app_sources.sh + the RPC layer
T="$(mktemp -d "${TMPDIR:-/tmp}/zxvd-build.XXXXXX")"
trap 'rm -rf "$T"' EXIT INT TERM
python3 "$HERE/gen_ui.py" "$HERE/zxv_ui.html" "$T/zxv_ui.c"
set --
for d in $(sh "$HERE/app_sources.sh" incs); do set -- "$@" "-I$K/$d"; done
set -- "$@" "-I$K/src/web4" "-I$R/sdk/libzxv/include"
for f in $(sh "$HERE/app_sources.sh" srcs); do set -- "$@" "$K/$f"; done
DEFS="-DZXVD $OSDEFS"
if [ -f "$K/src/tensor/zt_model.c" ] && [ -f "$HERE/zxv_zt_glue.c" ]; then
    set -- "$@" "$HERE/zxv_zt_glue.c"
    DEFS="$DEFS -DZXV_HAVE_ZT_GLUE"
fi
[ -n "$EXE" ] && set -- "$@" "$HERE/zxvd_rpc_win.c"
# 3. AIPI on Z-Wasm (zxvd_aipi.c): the sandbox libraries from the aipi kit;
# the AIPI core and the blockstore are kernel sources already in the app's
# source list (app_sources.sh vinea, for AIPI over Vinea). Not in the Windows build yet (ZXVD_AIPI=0 leaves it out anywhere).
AIPI_LIBS=""
if [ -z "$EXE" ] && [ "${ZXVD_AIPI:-1}" != 0 ]; then
    A="$OUT/aipi-build"
    make -C "$R/sdk/kits/aipi" CC="$CC" OUT="$A" ${SANITIZE:+SANITIZE=1} \
        "$A/libaipi_zwasm.a" "$A/zcontract/libzcontract_wasm.a" "$A/zcontract/libzcontract.a" \
        "$A/zwasm/libzwasm.a" >"$OUT/aipi-build.log" 2>&1 ||
        { tail -30 "$OUT/aipi-build.log"; echo "[FAIL] aipi kit build"; exit 1; }
    AIPI_LIBS="$A/libaipi_zwasm.a $A/zcontract/libzcontract_wasm.a $A/zcontract/libzcontract.a $A/zwasm/libzwasm.a"
    set -- "$@" "-I$K/src/aipi" "-I$R/sdk/kits/aipi/include" "$HERE/zxvd_aipi.c"
    DEFS="$DEFS -DZXV_HAVE_AIPI"
fi
set -- "$@" "$HERE/zxvd_rpc.c" "$K/src/web4/web4_json.c" "$K/src/web4/web4_util.c" "$T/zxv_ui.c"
# shellcheck disable=SC2086 # CC may carry flags ("xcrun clang -arch arm64")
$CC -std=c11 -O2 -Wall -Wextra -Werror $SAN $DEFS "$@" $AIPI_LIBS -L"$OUT" -lzxv ${RP:+"$RP"} -lm $OSLIBS \
    -o "$OUT/zxvd$EXE"
echo "[PASS] built $OUT/zxvd$EXE against $OUT/$LIBF"
