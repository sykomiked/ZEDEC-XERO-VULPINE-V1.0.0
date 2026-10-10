#!/bin/sh
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# test_hosted.sh — build the hosted app with the host compiler and test it:
# the API guard unit test (under ASan/UBSan where the compiler has them),
# then the end-to-end socket test with the tokenizer test model installed.
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

# the app itself: the same source list build_desktop.sh uses
python3 "$HERE/gen_ui.py" "$HERE/zxv_ui.html" "$T/zxv_ui.c"
set -- "$HERE/zxv_host.c" "$HERE/zxv_http_guard.c" "$HERE/zxv_model_host.c" "$T/zxv_ui.c"
for f in "$K"/src/swarm/swarm_*.c; do set -- "$@" "$f"; done
# every tensor-engine source (so zt_model*.c is compiled in once it exists)
for f in "$K"/src/tensor/zt*.c; do set -- "$@" "$f"; done
DEFS=""
if [ -f "$K/src/tensor/zt_model.c" ] && [ -f "$HERE/zxv_zt_glue.c" ]; then
    set -- "$@" "$HERE/zxv_zt_glue.c"
    DEFS="-DZXV_HAVE_ZT_GLUE"
fi
"$CC" -std=c11 -O1 -Wall -Wextra -Werror $SAN $DEFS -I"$HERE" -I"$K/src/swarm" -I"$K/src/tensor" \
    -I"$K/src/zcapital" -I"$K/src/surplus" "$@" -o "$T/zxv-host"

# the forward-pass glue against the tensor engine's reference chain
if [ -n "$DEFS" ]; then
    set -- "$HERE/test_zxv_zt_glue.c" "$HERE/zxv_zt_glue.c"
    for f in "$K"/src/tensor/zt*.c; do set -- "$@" "$f"; done
    "$CC" -std=c11 -O1 -Wall -Wextra -Werror $SAN -I"$HERE" -I"$K/src/tensor" "$@" -o "$T/test_glue"
    "$T/test_glue"
fi

"$CC" -std=c11 -Wall -Wextra -Werror -I"$K/src/tensor" "$HERE/test_write_gguf.c" -o "$T/write_gguf"
"$T/write_gguf" "$T/tiny-test.gguf"

ASAN_OPTIONS=detect_leaks=0 python3 "$HERE/test_host_api.py" "$T/zxv-host"
ASAN_OPTIONS=detect_leaks=0 python3 "$HERE/test_host_api.py" "$T/zxv-host" "$T/tiny-test.gguf"
echo "[PASS] hosted app: API guard, forward-pass glue, end-to-end API, model slot"
