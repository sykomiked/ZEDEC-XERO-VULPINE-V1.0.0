#!/bin/bash
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# adversarial-audit.sh — the adversarial self-audit (docs/ADVERSARIAL_AUDIT.md).
# Attacks the tree the way a hostile peer, a hostile model file, a hostile
# local app, a malicious contributor and a sceptical reviewer would, and
# checks that the normal gate is itself honest. Everything runs locally;
# nothing is pushed or published.
#
#   scripts/adversarial-audit.sh            stage S: static audit (seconds)
#   scripts/adversarial-audit.sh --fuzz     + stage F: every fuzz and property
#                                           harness, corpus replay and seeded
#                                           random runs under ASan/UBSan
#   scripts/adversarial-audit.sh --mutate   + stage M: mutation testing of the
#                                           security code listed in
#                                           build_system/adversarial_targets.json
#   scripts/adversarial-audit.sh --deep     S + F + M + stage D (the full host
#                                           suite, make -C kernel verify-all)
#
# Options:
#   --seeds "A B C"   seeds for the random property runs (default 3 seeds)
#   --iters N         random inputs per seed and harness (default 20000)
#   --jobs N          mutation workers (default: CPU count)
#   --out DIR         where logs and the JSON findings go (default: a temp dir)
#
# Exit status: 0 when no stage reported FAIL, 1 otherwise. The summary at the
# end lists each stage, its verdict and its log.
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

FUZZ=0 MUT=0 DEEP=0 SEEDS="20261012 7 1618033988" ITERS=20000 JOBS="" OUT=""
while [ $# -gt 0 ]; do
    case "$1" in
        --fuzz) FUZZ=1 ;;
        --mutate) MUT=1 ;;
        --deep) FUZZ=1 MUT=1 DEEP=1 ;;
        --seeds) SEEDS="${2:?--seeds needs a list}"; shift ;;
        --iters) ITERS="${2:?--iters needs a number}"; shift ;;
        --jobs) JOBS="${2:?--jobs needs a number}"; shift ;;
        --out) OUT="${2:?--out needs a directory}"; shift ;;
        -h|--help) sed -n '4,27p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
    shift
done
if [ -z "$OUT" ]; then OUT="$(mktemp -d "${TMPDIR:-/tmp}/zxv-adv.XXXXXX")"; fi
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"
# A private directory for every binary the stages build, so a fixed /tmp
# path can never be raced or pre-planted by another user of the machine.
export ZXV_TMP="$OUT/bin"
mkdir -p "$ZXV_TMP"
chmod 700 "$OUT" "$ZXV_TMP"

declare -a NAMES VERDICTS LOGS
RC=0
stage() { # stage NAME LOGFILE command...
    local name="$1" log="$2"; shift 2
    printf '\n=== %s ===\n' "$name"
    local t0=$SECONDS
    if "$@" >"$log" 2>&1; then v=PASS; else v=FAIL; RC=1; fi
    tail -25 "$log"
    NAMES+=("$name"); VERDICTS+=("$v ($((SECONDS - t0)) s)"); LOGS+=("$log")
}

# ---- S: static -------------------------------------------------------------
stage "S static audit" "$OUT/static.log" \
    python3 build_system/adversarial_audit.py --json "$OUT/static.json"

# ---- F: fuzz and property harnesses ----------------------------------------
fuzz_all() {
    make -C fuzz OUT="$OUT/fuzz" replay || return 1
    local h s
    for h in $(make -s -C fuzz list); do
        for s in $SEEDS; do
            # crash-random-* inputs land in $OUT, not in the tree.
            (cd "$OUT" && "$OUT/fuzz/replay/$h" --random "$s" "$ITERS" 512 >/dev/null) ||
                { echo "[FAIL] $h --random $s $ITERS 512 (input saved as $OUT/crash-random-$s-*)"; return 1; }
        done
        echo "[PASS] $h: corpus + $(echo $SEEDS | wc -w) x $ITERS random inputs"
    done
    if command -v clang >/dev/null && echo 'int LLVMFuzzerTestOneInput(const char*d,long n){return 0;}' |
        clang -x c -fsanitize=fuzzer - -o "$OUT/probe" 2>/dev/null; then
        make -C fuzz OUT="$OUT/fuzz" smoke SMOKE_SECS="${SMOKE_SECS:-30}"
    else
        echo "note: clang's libFuzzer runtime is missing; coverage-guided runs skipped (CI runs them)"
    fi
}
[ "$FUZZ" = 1 ] && stage "F fuzz + property harnesses" "$OUT/fuzz.log" fuzz_all

# ---- M: mutation testing of the security code ------------------------------
mutate_sec() {
    python3 tests/harness/mutator/mutate.py --selftest &&
        python3 tests/harness/mutator/mutate.py --config build_system/adversarial_targets.json \
            --ci --jobs "${JOBS:-$(nproc 2>/dev/null || sysctl -n hw.ncpu)}" --out "$OUT/mutation"
}
[ "$MUT" = 1 ] && stage "M mutation of security code" "$OUT/mutation.log" mutate_sec

# ---- D: the full host suite --------------------------------------------------
[ "$DEEP" = 1 ] && stage "D host suite (verify-all)" "$OUT/verify-all.log" make -C kernel verify-all

printf '\n=== adversarial self-audit summary (logs in %s) ===\n' "$OUT"
for i in "${!NAMES[@]}"; do
    printf '  %-32s %-14s %s\n' "${NAMES[$i]}" "${VERDICTS[$i]}" "${LOGS[$i]}"
done
exit $RC
