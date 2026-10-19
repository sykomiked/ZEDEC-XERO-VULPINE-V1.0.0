#!/bin/bash
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# coverage.sh — clang source-based coverage (line, branch and MC/DC) of the
# host test suite (`make -C kernel verify-all`) plus the committed fuzz
# corpora, summarised per module.
#
#   fuzz/tools/coverage.sh                 (or: make -C fuzz coverage)
#   COV_OUT=dir  COV_CC=clang-18  LLVM_SUFFIX=-18  fuzz/tools/coverage.sh
#
# Every host-test compile goes through tools/san-cc with clang's coverage
# flags (-fprofile-instr-generate -fcoverage-mapping -fcoverage-mcdc), each
# linked program is kept, the raw profiles are merged with llvm-profdata,
# and llvm-cov exports the summary that tools/coverage_table.py turns into
# docs/COVERAGE.md. Outputs (in $COV_OUT, default fuzz/build/cov):
#   summary.json  report.txt  COVERAGE.table.md  verify-all.log
set -euo pipefail
FUZZ="$(cd "$(dirname "$0")/.." && pwd)"
ROOT="$(dirname "$FUZZ")"
OUT="${COV_OUT:-$FUZZ/build/cov}"
CCX="${COV_CC:-clang}"
SFX="${LLVM_SUFFIX:-}"
rm -rf "$OUT"
mkdir -p "$OUT/bin" "$OUT/prof" "$OUT/bins"
ln -s "$FUZZ/tools/san-cc" "$OUT/bin/gcc"
ln -s "$FUZZ/tools/san-cc" "$OUT/bin/cc"

FLAGS="-fprofile-instr-generate -fcoverage-mapping -fcoverage-mcdc -Wno-error"
FLAGS="$FLAGS -Wno-unknown-warning-option -Wno-unused-command-line-argument"
export LLVM_PROFILE_FILE="$OUT/prof/%m-%p.profraw"

echo "=== coverage: host test suite (make -C kernel verify-all) ==="
if ! env PATH="$OUT/bin:$PATH" SANCC_CC="$CCX" SANCC_FLAGS="$FLAGS" SANCC_COV_DIR="$OUT/bins" \
    make -C "$ROOT/kernel" verify-all > "$OUT/verify-all.log" 2>&1; then
    tail -40 "$OUT/verify-all.log"
    echo "[FAIL] verify-all under coverage instrumentation"
    exit 1
fi
grep -c '^\[PASS\]' "$OUT/verify-all.log" | sed 's/^/verify-all [PASS] lines: /'

echo "=== coverage: fuzz corpus replay ==="
make -C "$FUZZ" --no-print-directory OUT="$OUT/fuzz" CC_REPLAY="$CCX" REPLAY_SAN="$FLAGS" \
    ED25519_GCC= replay > "$OUT/fuzz-replay.log" 2>&1 || {
    tail -40 "$OUT/fuzz-replay.log"
    exit 1
}
cp "$OUT"/fuzz/replay/* "$OUT/bins/"

echo "=== coverage: merge and export ==="
"llvm-profdata$SFX" merge -sparse "$OUT"/prof/*.profraw -o "$OUT/merged.profdata"
objs=()
for b in "$OUT"/bins/*; do
    # keep only instrumented programs (some recipes build helpers we skip)
    if "llvm-cov$SFX" report --summary-only -instr-profile "$OUT/merged.profdata" "$b" \
        > /dev/null 2>&1; then
        objs+=(-object "$b")
    fi
done
[ ${#objs[@]} -gt 0 ] || { echo "no instrumented programs"; exit 1; }
first="${objs[1]}"
rest=("${objs[@]:2}")
IGN='(^|/)(test_[^/]*|third_party/.*)\.c$|/fuzz/|^/usr/'
"llvm-cov$SFX" export -summary-only -instr-profile "$OUT/merged.profdata" "$first" \
    "${rest[@]}" > "$OUT/summary.json"
"llvm-cov$SFX" report -show-mcdc-summary -instr-profile "$OUT/merged.profdata" \
    -ignore-filename-regex="$IGN" "$first" "${rest[@]}" > "$OUT/report.txt" 2>/dev/null ||
    "llvm-cov$SFX" report -instr-profile "$OUT/merged.profdata" \
        -ignore-filename-regex="$IGN" "$first" "${rest[@]}" > "$OUT/report.txt"
python3 -I "$FUZZ/tools/coverage_table.py" "$OUT/summary.json" "$ROOT" > "$OUT/COVERAGE.table.md"
echo "programs: $(( ${#objs[@]} / 2 ))  profiles: $(ls "$OUT"/prof | wc -l)"
tail -1 "$OUT/COVERAGE.table.md"
