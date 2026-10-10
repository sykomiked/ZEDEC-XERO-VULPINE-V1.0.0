#!/usr/bin/env bash
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# Run every machine check in proofs/. See proofs/README.md.
#
#   TLA2TOOLS  path to tla2tools.jar (pinned version: see README.md)
#   LEAN       path to the lean binary (pinned toolchain: lean-toolchain)
#
# Usage: proofs/check.sh [tla|lean|all] [model-name ...]
# Exit status is non-zero if any check fails, including an expected
# counterexample that TLC did NOT find.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WHAT="${1:-all}"
shift || true
ONLY=("$@")
TLA2TOOLS="${TLA2TOOLS:-$HERE/.tools/tla2tools.jar}"
LEAN="${LEAN:-lean}"
META="${TLC_METADIR:-$(mktemp -d)}"
LOGDIR="${PROOFS_LOGDIR:-$META/logs}"
mkdir -p "$LOGDIR"
fail=0

want() {
    [ "${#ONLY[@]}" -eq 0 ] && return 0
    local m
    for m in "${ONLY[@]}"; do [ "$m" = "$1" ] && return 0; done
    return 1
}

# tlc_ok MODULE CFG: TLC must finish with no error.
tlc_ok() {
    local mod="$1" cfg="$2" log="$LOGDIR/$2.log"
    want "$cfg" || return 0
    echo "== TLC $mod -config $cfg"
    if (cd "$HERE" && java -XX:+UseParallelGC -cp "$TLA2TOOLS" tlc2.TLC -workers auto \
        -metadir "$META/$cfg" -config "$cfg" "$mod") >"$log" 2>&1 &&
        grep -q "Model checking completed. No error has been found." "$log"; then
        grep -E "states generated|distinct states found|depth of the complete state graph" "$log"
        echo "[PASS] $cfg"
    else
        tail -40 "$log"
        echo "[FAIL] $cfg"
        fail=1
    fi
}

# tlc_violates MODULE CFG INVARIANT: TLC must report a violation of INVARIANT
# (a known, documented limitation of the C code that the model demonstrates).
tlc_violates() {
    local mod="$1" cfg="$2" inv="$3" log="$LOGDIR/$2.log"
    want "$cfg" || return 0
    echo "== TLC $mod -config $cfg (expects $inv to be violated)"
    (cd "$HERE" && java -XX:+UseParallelGC -cp "$TLA2TOOLS" tlc2.TLC -workers auto \
        -metadir "$META/$cfg" -config "$cfg" "$mod") >"$log" 2>&1 || true
    if grep -q "Invariant $inv is violated" "$log"; then
        grep -E "states generated|distinct states found" "$log" || true
        echo "[PASS] $cfg (counterexample found, as documented)"
    else
        tail -40 "$log"
        echo "[FAIL] $cfg: expected a violation of $inv"
        fail=1
    fi
}

lean_ok() {
    local f="$1"
    want "$f" || return 0
    echo "== lean $f"
    if grep -nE '\b(sorry|admit)\b|^\s*axiom\b' "$HERE/$f"; then
        echo "[FAIL] $f: contains sorry/admit/axiom"
        fail=1
        return
    fi
    if (cd "$HERE" && "$LEAN" "$f") >"$LOGDIR/$(basename "$f").log" 2>&1 &&
        ! grep -q "declaration uses 'sorry'" "$LOGDIR/$(basename "$f").log"; then
        echo "[PASS] $f"
    else
        cat "$LOGDIR/$(basename "$f").log"
        echo "[FAIL] $f"
        fail=1
    fi
}

if [ "$WHAT" = tla ] || [ "$WHAT" = all ]; then
    [ -f "$TLA2TOOLS" ] || { echo "tla2tools.jar not found at $TLA2TOOLS"; exit 2; }
    tlc_ok MC_ledger_conservation.tla MC_ledger_conservation.cfg
    tlc_ok MC_ledger_fee.tla MC_ledger_fee.cfg
    tlc_ok ledger_replay.tla MC_ledger_replay.cfg
    tlc_violates ledger_replay.tla MC_ledger_replay_wrap.cfg ReplayNeverApplied
    tlc_ok ledger_replay.tla MC_ledger_replay_nonce.cfg
    tlc_ok triple_ledger.tla MC_triple_ledger.cfg
    tlc_violates triple_ledger.tla MC_triple_ledger_trial.cfg TrialBalanced
fi

if [ "$WHAT" = lean ] || [ "$WHAT" = all ]; then
    "$LEAN" --version
    lean_ok headroom_convergence/HeadroomConvergence.lean
    lean_ok rational_bounds/RationalBounds.lean
fi

exit "$fail"
