#!/bin/sh
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# build_zxvd.sh OUTDIR — build zxvd: the hosted swarm (the same sources as
# zxv-host, from app_sources.sh) with -DZXVD, its JSON-RPC socket
# (zxvd_rpc.c) and the web4 JSON tokenizer, linked against libzxv's shared
# library through its frozen C ABI (sdk/libzxv/include/zxv_api.h). The module
# methods (zxvd_rpc_modules.c: freight, harmonic, stream, ehop, web4) add
# their kernel sources below; Linux, macOS and Windows link the same list.
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
        OSLIBS="-lws2_32 -lshell32 -lbcrypt -ladvapi32 -lpowrprof -static-libgcc"
        OSDEFS="-D__USE_MINGW_ANSI_STDIO=1 -D_WIN32_WINNT=0x0A00"
        ;;
    # Both rpaths at link time (next to the binary, and Contents/Frameworks
    # in ZXV.app), with header room: the x86_64 slice has none left for a
    # later install_name_tool -add_rpath.
    *darwin*|*apple*)
        LIBF=libzxv.dylib
        OSLIBS="-framework IOKit -framework CoreFoundation" # power.* (zxv_power_host.c)
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
for d in freight harmonic stream ehop; do set -- "$@" "-I$K/src/$d"; done
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
# 4. The Playground (sdk/playground): the templates and the runner for ZXV's
# languages, behind /api/play/* in the window and play.* on the socket. Not
# in the Windows build yet (ZXVD_PLAY=0 leaves it out anywhere).
PLAY_LIBS=""
if [ -z "$EXE" ] && [ "${ZXVD_PLAY:-1}" != 0 ]; then
    P="$OUT/play-build"
    # The Playground's deps (libzxv, the pillars, libzxv-econ) build into
    # fixed folders, and make does not see a compiler change: a universal
    # macOS build runs this once per arch. Build them again when CC changed.
    STAMP="$R/sdk/libzxv/build/.play-deps-cc"
    if [ "$(cat "$STAMP" 2>/dev/null)" != "$CC" ]; then
        rm -rf "$R/sdk/libzxv/build" "$R/packages/build/libzxv-econ"
        for p in cobol-sutra fortran-trebago ada-eve apl-kron mumps-myra; do
            rm -rf "$R/sdk/pillars/$p/build"
        done
    fi
    make -C "$R/sdk/playground" CC="$CC" OUT="$P" ${SANITIZE:+SANITIZE=1} deps "$P/libzxvplay.a" \
        >"$OUT/play-build.log" 2>&1 ||
        { tail -30 "$OUT/play-build.log"; echo "[FAIL] playground build"; exit 1; }
    printf '%s\n' "$CC" >"$STAMP"
    PL="$R/sdk/pillars"
    PLAY_LIBS="$P/libzxvplay.a $PL/cobol-sutra/build/libzsutra.a $PL/fortran-trebago/build/libztrebago.a"
    PLAY_LIBS="$PLAY_LIBS $PL/ada-eve/build/libzeve.a $PL/apl-kron/build/libzkron.a $PL/mumps-myra/build/libzmyra.a"
    PLAY_LIBS="$PLAY_LIBS $R/packages/build/libzxv-econ/libzxv-econ.a"
    set -- "$@" "-I$R/sdk/playground/include"
    DEFS="$DEFS -DZXV_HAVE_PLAY"
fi
# 5. method groups behind feature switches (zxvd_ext.h): netting, cards, ...
for d in $(sh "$HERE/app_sources.sh" groupincs); do set -- "$@" "-I$K/$d"; done
for f in $(sh "$HERE/app_sources.sh" groups); do set -- "$@" "$K/$f"; done
set -- "$@" "$HERE/zxvd_rpc.c" "$HERE/zxv_files.c" "$HERE/zxv_spaces.c" "$HERE/zxvd_phone.c" "$K/src/web4/web4_json.c" "$K/src/web4/web4_util.c" "$T/zxv_ui.c"
# the module methods (app_sources.sh modules): each source not already on
# the line (srcs and the method groups link several of them) is added once
for f in $(sh "$HERE/app_sources.sh" modules); do
    case " $* " in *" $K/$f "*) ;; *) set -- "$@" "$K/$f" ;; esac
done
# the owner's social chat (zxvd_chat.h): signed records, store, ISF feed
set -- "$@" "$HERE/zxvd_chat.c" "$K/src/reputation/reputation.c"
for f in social_post social_store social_feed social_bucket social_group social_sign_mldsa; do
    set -- "$@" "$K/src/social/$f.c"
done
# payments (zxvd_pay.h): ident's passkey check and the post-quantum code it
# links (app_sources.sh already has ML-DSA-65/87, ML-KEM-1024, X25519), and
# its Argon2id for seats.* (zxvd_seats.c)
set -- "$@" "-I$K/src/ident" "$HERE/zxvd_pay.c" "$K/src/ident/id_passkey.c" "$K/src/ident/id_core.c" \
    "$K/src/ident/id_argon2.c"
for f in pq_matrix pq_hqc5 pq_slh256s; do set -- "$@" "$K/src/pqsec/$f.c"; done
for f in "$K"/src/pqsec/slhdsa/[a-z]*.c; do set -- "$@" "$f"; done
for f in mlkem768 mlkem_encode mlkem_sample mlkem_ntt mlkem_kpe; do set -- "$@" "$K/src/mlkem/$f.c"; done
# 6. The people features (zxvd_people.c: social, calls, translation, the
# device mesh, simple mode): kernel libraries compiled here. Not in the
# Windows build yet (ZXVD_PEOPLE=0 leaves them out anywhere). The chat,
# payments and method groups above already link part of what they need
# (social, reputation, pq_matrix, ML-KEM-768, ...): a file is added once.
if [ -z "$EXE" ] && [ "${ZXVD_PEOPLE:-1}" != 0 ]; then
    for d in $(sh "$HERE/app_sources.sh" people_incs); do set -- "$@" "-I$K/$d"; done
    for f in $(sh "$HERE/app_sources.sh" people); do
        dup=0
        for a in "$@"; do [ "$a" = "$K/$f" ] && { dup=1; break; }; done
        [ "$dup" = 1 ] || set -- "$@" "$K/$f"
    done
    DEFS="$DEFS -DZXV_HAVE_PEOPLE"
fi
# shellcheck disable=SC2086 # CC may carry flags ("xcrun clang -arch arm64")
$CC -std=c11 -O2 -Wall -Wextra -Werror $SAN $DEFS "$@" $AIPI_LIBS $PLAY_LIBS -L"$OUT" -lzxv ${RP:+"$RP"} -lm $OSLIBS \
    -o "$OUT/zxvd$EXE"
echo "[PASS] built $OUT/zxvd$EXE against $OUT/$LIBF"
