#!/bin/sh
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# test_hosted.sh — build the hosted app with the host compiler and test it:
# the API guard unit test (under ASan/UBSan where the compiler has them),
# the forward-pass glue, the budget gate and the Vinea net host, then the
# end-to-end socket tests (with the tokenizer test model installed, and two
# app instances that find each other over UDP).
# Linux and macOS. Meant for kernel/Makefile verify-all and for CI.
#
#   kernel/arch/hosted/test_hosted.sh            (from anywhere)
#   CC=clang kernel/arch/hosted/test_hosted.sh
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
K="$(cd "$HERE/../.." && pwd)"           # kernel/
CC="${CC:-cc}"
T="$(mktemp -d "${TMPDIR:-/tmp}/zxv-hosted.XXXXXX")"
trap 'rm -rf "$T"' EXIT INT TERM

SAN=""
if echo 'int main(void){return 0;}' > "$T/s.c" &&
   "$CC" -fsanitize=address,undefined "$T/s.c" -o "$T/s" 2>/dev/null && "$T/s"; then
    SAN="-fsanitize=address,undefined -fno-omit-frame-pointer"
fi

"$CC" -std=c11 -Wall -Wextra -Werror $SAN -I"$HERE" \
    "$HERE/test_zxv_http_guard.c" "$HERE/zxv_http_guard.c" -o "$T/test_guard"
"$T/test_guard"

# the app itself: the same source list build_desktop.sh uses (app_sources.sh)
python3 "$HERE/gen_ui.py" "$HERE/zxv_ui.html" "$T/zxv_ui.c"
sh "$HERE/app_sources.sh" incs > "$T/incs"
sh "$HERE/app_sources.sh" srcs > "$T/srcs"
sh "$HERE/app_sources.sh" vinea > "$T/vinea"
DEFS=""
if [ -f "$K/src/tensor/zt_model.c" ] && [ -f "$HERE/zxv_zt_glue.c" ]; then
    echo arch/hosted/zxv_zt_glue.c >> "$T/srcs"
    DEFS="-DZXV_HAVE_ZT_GLUE"
fi
# args LIST...: the -I flags and the sources named in each LIST file, into $T/args
args() {
    : > "$T/args"
    while IFS= read -r d; do printf '%s\n' "-I$K/$d" >> "$T/args"; done < "$T/incs"
    for list in "$@"; do
        while IFS= read -r f; do printf '%s\n' "$K/$f" >> "$T/args"; done < "$list"
    done
}
run_cc() { # OUTPUT: compile $T/args (with $SAN and $DEFS)
    out="$1"
    set --
    while IFS= read -r a; do set -- "$@" "$a"; done < "$T/args"
    "$CC" -std=c11 -O1 -Wall -Wextra -Werror $SAN $DEFS "$@" -o "$out"
}
args "$T/srcs"
printf '%s\n' "$T/zxv_ui.c" >> "$T/args"
run_cc "$T/zxv-host"

# the forward-pass glue against the tensor engine's reference chain
if [ -n "$DEFS" ]; then
    set -- "$HERE/test_zxv_zt_glue.c" "$HERE/zxv_zt_glue.c"
    for f in "$K"/src/tensor/zt*.c; do set -- "$@" "$f"; done
    "$CC" -std=c11 -O1 -Wall -Wextra -Werror $SAN -I"$HERE" -I"$K/src/tensor" "$@" -o "$T/test_glue"
    "$T/test_glue"
fi

# the swarm budget governs generation (zxv_budget_gate.h)
printf '%s\n' arch/hosted/test_zxv_budget_gate.c arch/hosted/zxv_budget_gate.c \
    src/swarm/swarm_budget.c > "$T/gate"
if [ -n "$DEFS" ]; then
    echo arch/hosted/zxv_zt_glue.c >> "$T/gate"
    (cd "$K" && for f in src/tensor/zt*.c; do echo "$f"; done) >> "$T/gate"
fi
args "$T/gate"
run_cc "$T/test_gate"
"$T/test_gate"

# two Vinea net hosts on 127.0.0.1 over real UDP sockets (zxv_net_host.h)
printf '%s\n' arch/hosted/test_zxv_net_host.c arch/hosted/zxv_net_host.c \
    arch/hosted/zxv_http_guard.c src/swarm/swarm_budget.c src/swarm/swarm_emotion.c \
    src/swarm/swarm_market.c src/swarm/swarm_hk.c > "$T/net"
args "$T/net" "$T/vinea"
run_cc "$T/test_net"
"$T/test_net"

"$CC" -std=c11 -Wall -Wextra -Werror -I"$K/src/tensor" "$HERE/test_write_gguf.c" -o "$T/write_gguf"
"$T/write_gguf" "$T/tiny-test.gguf"

ASAN_OPTIONS=detect_leaks=0 python3 "$HERE/test_host_api.py" "$T/zxv-host"
ASAN_OPTIONS=detect_leaks=0 python3 "$HERE/test_host_api.py" "$T/zxv-host" "$T/tiny-test.gguf"
ASAN_OPTIONS=detect_leaks=0 python3 "$HERE/test_host_net.py" "$T/zxv-host"
echo "[PASS] hosted app: API guard, forward-pass glue, budget gate, net host, end-to-end API, model slot, peers, updates, notifications"
