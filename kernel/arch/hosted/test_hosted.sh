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
python3 "$HERE/gen_ui.py" "$HERE/zxv_ui.html" "$T/zxv_ui.c" \
    --table-out "$T/ui_table.json" --samples-out "$T/ui_samples.json" --html-out "$T/ui_page.html"
# the window's words all come from kernel/src/i18n (and its script matches the C module)
python3 "$HERE/check_ui_i18n.py" "$HERE/zxv_ui.html" "$T/ui_table.json" "$T/ui_samples.json" \
    "$T/ui_page.html"
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
    # zt_zxvc.c checks a .zxvc package's digest: SHA-256 links with the engine
    set -- "$HERE/test_zxv_zt_glue.c" "$HERE/zxv_zt_glue.c" "$K/src/robin_debanks/sha256.c"
    for f in "$K"/src/tensor/zt*.c; do set -- "$@" "$f"; done
    "$CC" -std=c11 -O1 -Wall -Wextra -Werror $SAN -I"$HERE" -I"$K/src/tensor" -I"$K/include" -I"$K/src/modbind" "$@" -o "$T/test_glue"
    "$T/test_glue"
    # several models at once: slots, agents, a LoRA adapter per agent (M6, G6)
    set -- "$HERE/test_zxv_swarm_slots.c" "$HERE/zxv_model_host.c" "$HERE/zxv_zt_glue.c" \
        "$K/src/robin_debanks/sha256.c"
    for f in "$K"/src/tensor/zt*.c; do set -- "$@" "$f"; done
    "$CC" -std=c11 -O1 -Wall -Wextra -Werror $SAN $DEFS -I"$HERE" -I"$K/src/tensor" -I"$K/include" -I"$K/src/modbind" "$@" -lpthread \
        -o "$T/test_swarm_slots"
    "$T/test_swarm_slots" "$T"
    # the models on the GPU this machine has (G7: Metal on a Mac; CUDA, HIP
    # or Vulkan elsewhere), every reply equal to the CPU's; with no GPU both
    # runs are on the CPU and the test says so
    set -- "$HERE/test_zxv_zt_gpu.c" "$HERE/zxv_zt_glue.c" "$K/src/robin_debanks/sha256.c"
    for f in "$K"/src/tensor/zt*.c; do set -- "$@" "$f"; done
    GPUF="-ldl"
    case "$("$CC" -dumpmachine 2>/dev/null)" in
        *darwin*|*apple*)
            set -- "$@" "$K/src/tensor/zt_metal.m"
            GPUF="-DZT_HAVE_METAL -framework Metal -framework Foundation" ;;
        *)
            if command -v glslangValidator >/dev/null 2>&1 &&
                echo '#include <vulkan/vulkan.h>' | "$CC" -E -x c - >/dev/null 2>&1; then
                sh "$K/src/tensor/gen_zt_vk_spv.sh" "$T/zt_vk_spv.h"
                GPUF="$GPUF -DZT_VK_SPV_H=\"$T/zt_vk_spv.h\""
            fi ;;
    esac
    # shellcheck disable=SC2086 # GPUF is a list of flags
    "$CC" -std=c11 -O1 -Wall -Wextra -Werror $SAN $DEFS -I"$HERE" -I"$K/src/tensor" -I"$K/include" -I"$K/src/modbind" "$@" $GPUF -lpthread \
        -o "$T/test_zt_gpu"
    "$T/test_zt_gpu" > "$T/test_zt_gpu.log" 2>&1 || { cat "$T/test_zt_gpu.log"; exit 1; }
    echo "[PASS] app models on the GPU == the CPU ($(tail -1 "$T/test_zt_gpu.log"))"
fi

# the 64-state tag on one answer (zxv_answer_tag.h)
printf '%s\n' arch/hosted/test_zxv_answer_tag.c arch/hosted/zxv_answer_tag.c src/lpres/lpres_hex.c \
    src/lpres/lpres.c src/robin_debanks/sha256.c > "$T/tag"
args "$T/tag"
run_cc "$T/test_tag"
"$T/test_tag"

# talking to several Tiamat cells at once (zxv_tiamat.h, harness H1)
printf '%s\n' arch/hosted/test_zxv_tiamat.c arch/hosted/zxv_tiamat.c > "$T/tiamat"
args "$T/tiamat"
run_cc "$T/test_tiamat"
"$T/test_tiamat" "$T"

# R24: idle self-work in slices and one sealed receipt per interaction
printf '%s\n' arch/hosted/test_zxv_idle.c arch/hosted/zxv_idle.c arch/hosted/zxv_receipt.c \
    arch/hosted/zxv_tiamat.c src/robin_debanks/sha256.c > "$T/idle"
args "$T/idle"
run_cc "$T/test_idle"
mkdir -p "$T/idle_dir"
"$T/test_idle" "$T/idle_dir"

# the swarm budget governs generation (zxv_budget_gate.h)
printf '%s\n' arch/hosted/test_zxv_budget_gate.c arch/hosted/zxv_budget_gate.c \
    src/swarm/swarm_budget.c > "$T/gate"
if [ -n "$DEFS" ]; then
    echo arch/hosted/zxv_zt_glue.c >> "$T/gate"
    echo src/robin_debanks/sha256.c >> "$T/gate"
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

# remote inference between two nodes: PQ TCP session, paid request, fee settlement
# (zxv_peer_host.h), the server in a forked child as on a second machine
printf '%s\n' arch/hosted/test_zxv_peer_host.c arch/hosted/zxv_peer_host.c \
    arch/hosted/zxv_http_guard.c src/pay/pay_ledger.c src/pay/pay_util.c src/pay/pay_assure.c \
    src/swarm/swarm_budget.c src/swarm/swarm_emotion.c src/swarm/swarm_market.c \
    src/swarm/swarm_hk.c src/tensor/zt.c > "$T/peer"
args "$T/peer" "$T/vinea"
run_cc "$T/test_peer"
"$T/test_peer"

"$CC" -std=c11 -Wall -Wextra -Werror -I"$K/src/tensor" "$HERE/test_write_gguf.c" -o "$T/write_gguf"
"$T/write_gguf" "$T/tiny-test.gguf"

ASAN_OPTIONS=detect_leaks=0 python3 "$HERE/test_host_api.py" "$T/zxv-host"
ASAN_OPTIONS=detect_leaks=0 python3 "$HERE/test_host_api.py" "$T/zxv-host" "$T/tiny-test.gguf"
ASAN_OPTIONS=detect_leaks=0 python3 "$HERE/test_host_net.py" "$T/zxv-host"
echo "[PASS] hosted app: window strings from i18n, API guard, forward-pass glue, budget gate, net host, peer host, end-to-end API, model slot, peers, updates, notifications"
