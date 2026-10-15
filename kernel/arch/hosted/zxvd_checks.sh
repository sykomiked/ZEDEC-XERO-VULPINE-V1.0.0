#!/bin/sh
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# zxvd_checks.sh OUT: zxvd's unit tests and its end-to-end run, as one step.
# ci.yml's zxvd job runs this, so the merge gate (ci-gate) covers zxvd; the
# zxvd.yml workflow runs the same tests per platform, sanitizer and display
# layer. Run from the repository root; CC and CFLAGS pass through.
#
# Every kernel/arch/hosted/test_zxvd_*.sh runs: the list is the files, so a
# new unit test is gated the day it lands without editing this script.
set -eu
OUT=${1:?usage: zxvd_checks.sh OUT}
CC=${CC:-cc}
export CC
H=kernel/arch/hosted
mkdir -p "$OUT"

for t in "$H"/test_zxvd_*.sh; do
    echo "== $t"
    sh "$t"
done

echo "== zxv_files"
$CC -std=c11 -O1 -Wall -Wextra -Werror ${CFLAGS:-} -I"$H" "$H/test_zxv_files.c" "$H/zxv_files.c" -o "$OUT/test_zxv_files"
"$OUT/test_zxv_files"

for t in kernel/src/apps/test_apps.sh kernel/src/apps/test_zxapp.sh kernel/src/zui/views/test_views.sh \
    kernel/src/zui/test_zui_toolkit.sh "$H/test_zxpower.sh"; do
    echo "== $t"
    sh "$t"
done

echo "== build zxvd"
sh "$H/build_zxvd.sh" "$OUT/zxvd"
D="$OUT/zxvd/zxvd"
python3 "$H/check_zxvd_spec.py" "$D"
python3 "$H/test_zxvd.py" "$D"
python3 "$H/test_staple_apps.py" "$D"
python3 "$H/test_zxvd_phone.py" "$D"
$CC -std=c11 -Wall -Wextra -Werror -Ikernel/src/tensor "$H/test_write_gguf.c" -o "$OUT/write_gguf"
"$OUT/write_gguf" "$OUT/tiny-test.gguf"
python3 "$H/test_zxvd.py" "$D" "$OUT/tiny-test.gguf"
python3 tools/zxv_two_node_demo.py --zxvd "$D" local --check
$CC -std=c11 -Wall -Wextra -Werror macos/zxv_rpc_client.c macos/zxv_rpc_call.c -o "$OUT/zxv-rpc-call"
python3 macos/test_rpc_client.py "$D" "$OUT/zxv-rpc-call"
python3 macos/test_model_fetch.py
echo "zxvd_checks: all passed"
