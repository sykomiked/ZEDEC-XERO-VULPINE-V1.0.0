#!/bin/sh
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# run.sh: build and run the B2 unit tests for kernel/src/emu, desktop,
# sdk_bridge and panopticon (mutation-killing tests, tests/MUTATION_REPORT.md).
#
#   sh tests/host/b2/run.sh NAME     one test (names below)
#   sh tests/host/b2/run.sh all      every test
#
# Exit status: 0 pass, 1 test failure, 3 build failure. The mutation harness
# (tests/harness/mutator/mutate.py, --build-code 3) counts 3 as a mutant that
# did not compile instead of a kill. Run from anywhere; ZXV_TMP picks the
# scratch directory (default /tmp).
set -u
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
K="$ROOT/kernel"
T="$ROOT/tests/host/b2"
TMP="${ZXV_TMP:-/tmp}"
CC="${CC:-gcc}"
CFLAGS="-std=c11 -O1 -g -Wall -Werror -Wextra -fsanitize=address,undefined -fno-sanitize-recover=all -DTEST_HOST"

INC="-I$K/include -I$K/src"
for d in sdk_bridge sdk zcapital lpres surplus event_space modbind edp_risk rational orbital_compat crypto_wallet financial_fabric bridge constellation count_house finance pay finance_markets hardware identity interspace ipfs mesh_net mesh_token net onepolicy orbital_elevator orbital_fabric porter_house predictive update vena vino yantra desktop panopticon emu; do INC="$INC -I$K/src/$d"; done

NAMES="emu_cpu6502 emu_cpu_z80 emu_relate emu_game_runner desktop sdk_bridge sdk_bridge_lang polyglot_matrix panopticon panopticon_vpn render_boot"

# srcs NAME: prints the include flags and sources for one test
srcs() {
    case "$1" in
    emu_cpu6502) echo "-I$K/src/emu $T/test_emu_cpu6502.c $K/src/emu/cpu6502.c" ;;
    emu_cpu_z80) echo "$INC $T/test_emu_cpu_z80.c $K/src/emu/cpu_z80.c" ;;
    emu_relate) echo "$INC $T/test_emu_relate.c $K/src/emu/emu_relate.c $K/src/emu/cpu6502.c $K/src/emu/cpu_z80.c" ;;
    emu_game_runner) echo "-Wno-misleading-indentation $INC $T/test_emu_game_runner.c $K/src/emu/game_runner.c $K/src/emu/cpu6502.c $K/src/emu/cpu_z80.c $K/src/emu/nes.c $K/src/emu/gb.c $K/src/emu/cpu_lr35902.c $K/src/emu/gba.c $K/src/emu/cpu_arm7.c $K/src/emu/genesis.c $K/src/emu/cpu_m68k.c $K/src/emu/pce.c $K/src/emu/cpu_huc6280.c $K/src/emu/snes.c $K/src/emu/cpu65816.c $K/src/emu/zxv_cover.c" ;;
    desktop) echo "$INC $T/test_desktop_units.c $K/src/desktop/desktop.c $K/src/event_space/event_envelope.c $K/src/robin_debanks/sha256.c" ;;
    sdk_bridge) echo "$INC $T/test_sdk_bridge_units.c $K/src/sdk_bridge/sdk_bridge.c $K/src/lpres/lpres.c" ;;
    sdk_bridge_lang) echo "-Wno-unused-parameter $INC $T/test_sdk_bridge_lang.c $K/src/sdk_bridge/sdk_bridge_lang.c $K/src/sdk_bridge/sdk_bridge.c $K/src/lpres/lpres.c $K/src/rational/rational.c" ;;
    polyglot_matrix) echo "-Wno-unused-parameter $INC $T/test_polyglot_matrix.c $K/src/sdk_bridge/polyglot_matrix.c $K/src/lpres/lpres.c" ;;
    panopticon) echo "$INC $T/test_panopticon.c $K/src/panopticon/panopticon.c" ;;
    panopticon_vpn) echo "$INC $T/test_panopticon_vpn.c $K/src/panopticon/panopticon_vpn.c" ;;
    render_boot) echo "$INC -I$K/src/prism_break -I$K/src/vbe -I$K/src/display -I$K/src/video $T/test_render_boot.c $K/src/desktop/render_boot.c" ;;
    *) return 1 ;;
    esac
}

one() {
    s="$(srcs "$1")" || { echo "unknown test: $1 (one of: $NAMES)" >&2; return 2; }
    bin="$TMP/b2_$1"
    # shellcheck disable=SC2086
    if ! $CC $CFLAGS $s -o "$bin" -lm 2> "$bin.build.log"; then
        cat "$bin.build.log"
        echo "[FAIL] b2 $1 (build)"
        return 3
    fi
    if "$bin" > "$bin.log" 2>&1; then
        echo "[PASS] b2 $1 ($(tail -1 "$bin.log"))"
        return 0
    fi
    tail -40 "$bin.log"
    echo "[FAIL] b2 $1"
    return 1
}

[ $# -eq 1 ] || { echo "usage: $0 NAME|all" >&2; exit 2; }
if [ "$1" = all ]; then
    rc=0
    for n in $NAMES; do one "$n" || rc=1; done
    exit $rc
fi
one "$1"
