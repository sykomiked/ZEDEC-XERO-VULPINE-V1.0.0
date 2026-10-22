#!/bin/sh
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# test_browser.sh OUT [SHOTS_DIR] — the browser path end to end on Linux:
# boot -> passport login -> a space -> the companion's answer from a real
# model. Builds zxvd, the smallest model zxvd can run
# (kernel/arch/hosted/write_test_model.c) and the browser root
# (tools/web/build_web_root.sh), then drives headless Chromium with
# Playwright (tests/browser/test_browser.cjs). With SHOTS_DIR it saves one
# screenshot per step there. Then the same path under abuse
# (tests/browser/test_browser_stress.cjs): bad credentials, a broken boot,
# zxvd killed mid-ask, no or corrupt models, tabs at once, a 100-round soak
# and the seed looked for everywhere after it is confirmed.
#
#   ZXVD=path/to/zxvd        use this zxvd instead of building one
#   ZXV_BRAND_DIR=DIR        the gold brand SVGs, for the boot screen's logos
#   ZXV_MODELS_DIR=DIR       ask this folder's model instead of the test model
#                            (a downloaded GGUF; the test needs one that runs)
#   PLAYWRIGHT_BROWSERS_PATH where Chromium is (never installed from here)
#   SOAK_ROUNDS=N            the soak's rounds (default 100)
#   ZXV_STRESS=0             skip the stress test
#
# Needs cc, clang with the wasm32 target, wasm-ld, python3, node and the
# playwright package (found through `npm root -g` when not on NODE_PATH).
set -eu
R=$(cd "$(dirname "$0")/../.." && pwd)
OUT=${1:?usage: test_browser.sh OUT [SHOTS_DIR]}
SHOTS=${2:-}
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
CC=${CC:-cc}

if [ -z "${ZXVD:-}" ]; then
    sh "$R/kernel/arch/hosted/build_zxvd.sh" "$OUT/zxvd" > "$OUT/zxvd-build.log" 2>&1 ||
        { tail -30 "$OUT/zxvd-build.log"; echo "[FAIL] zxvd build"; exit 1; }
    ZXVD="$OUT/zxvd/zxvd"
fi

rm -rf "$OUT/root"
MODELS=${ZXV_MODELS_DIR:-}
if [ -z "$MODELS" ]; then
    rm -rf "$OUT/models"
    mkdir -p "$OUT/models"
    "$CC" -std=c11 -O2 -Wall -Wextra -Werror -I"$R/kernel/src/tensor" -I"$R/kernel/arch/hosted" \
        "$R/kernel/arch/hosted/write_test_model.c" "$R/kernel/src/tensor/zt_gguf.c" \
        "$R/kernel/src/tensor/zt.c" -o "$OUT/write_test_model"
    "$OUT/write_test_model" "$OUT/models/tiny-test.gguf"
    MODELS="$OUT/models"
fi

if [ -n "${ZXV_BRAND_DIR:-}" ]; then
    sh "$R/tools/web/build_web_root.sh" "$OUT/root" --brand "$ZXV_BRAND_DIR"
else
    sh "$R/tools/web/build_web_root.sh" "$OUT/root"
fi

NODE_PATH=${NODE_PATH:-$(npm root -g)}
export NODE_PATH
node "$R/tests/browser/test_browser.cjs" "$ZXVD" "$MODELS" "$OUT/root" $SHOTS
if [ "${ZXV_STRESS:-1}" != 0 ]; then
    mkdir -p "$OUT/stress"
    node "$R/tests/browser/test_browser_stress.cjs" "$ZXVD" "$MODELS" "$OUT/root" "$OUT/stress"
fi
