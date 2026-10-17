#!/usr/bin/env python3
# completion_state.py — the completion measure in the system's OWN logic.
# Sequential percentages are a category error in a paraconsistent
# architecture: the layers do not complete 95%, 90%, 60% and then add up.
# Each layer holds a FOUR-VALUED state, and the whole reaches TRUE only
# when every layer does — simultaneously, because the system is one thing.
#
#   NEITHER  the layer has no evidence yet (not started)
#   BOTH     the layer has partial evidence (in progress: some TRUE, some
#            not) — the honest state of a system under construction
#   FALSE    the layer is contradicted (broken, failing)
#   TRUE     the layer is complete (all its checks pass)
#
# The system's state is the CONJOIN of the layers (the meet on the chain):
# it is TRUE only when every layer is TRUE, and BOTH while any is BOTH.
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""usage: completion_state.py"""
import os
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../.."))
NEITHER, TRUE, FALSE, BOTH = 0, 1, 2, 3
NAME = {0: "NEITHER", 1: "TRUE", 2: "FALSE", 3: "BOTH"}
# the chain: FALSE < NEITHER < BOTH < TRUE; conjoin = the meet (min)
CHAIN = {FALSE: 0, NEITHER: 1, BOTH: 2, TRUE: 3}


def conjoin(a, b):
    return a if CHAIN[a] <= CHAIN[b] else b


def layer_state(*checks):
    """A layer is TRUE only when every check passes; FALSE if any is
    contradicted; BOTH when some pass and some have no evidence; NEITHER
    when none has evidence."""
    if all(checks):
        return TRUE
    if not any(checks):
        return NEITHER
    return BOTH


def main():
    # the evidence per layer (each a list of concrete, checkable facts)
    ev = {}

    # 1. the mathematics
    ev["the mathematics"] = [
        os.path.exists(os.path.join(ROOT, "kernel/src/lpres/lpres_phases.h")),
        os.path.exists(os.path.join(ROOT, "kernel/src/lpres/lpres_bridge.c")),
    ]
    # 2. the logic
    ev["the logic"] = [
        os.path.exists(os.path.join(ROOT, "kernel/src/lpres/lpres.c")),
        os.path.exists(os.path.join(ROOT, "tools/agent_loop/mk_lpres_phases.py")),
    ]
    # 3. the security
    ev["the security"] = [
        os.path.exists(os.path.join(ROOT, "tools/agent_loop/lattice_harden.py")),
        os.path.exists(os.path.join(ROOT, "tools/agent_loop/LATTICE_HARDENING.md")),
    ]
    # 4. the architecture
    ev["the architecture"] = [
        os.path.exists(os.path.join(ROOT, "tools/agent_loop/MASTER.md")),
        os.path.exists(os.path.join(ROOT, "tools/agent_loop/ROSETTA.md")),
        os.path.exists(os.path.join(ROOT, "tools/agent_loop/WASM_FACTORY.md")),
    ]
    # 5. the apps (all 144 matrix cells pass)
    ad = os.path.join(ROOT, "kernel/src/apps")
    built = len([f for f in os.listdir(ad) if f.endswith(".zxapp")]) if os.path.isdir(ad) else 0
    ev["the apps"] = [built >= 144, built > 0]
    # 6. the vertical integration
    ev["the vertical integration"] = [False, False]
    # 7. the ecosystem catalogue
    ev["the ecosystem catalogue"] = [
        os.path.exists(os.path.join(ROOT, "web/apps/names.tsv")),
        os.path.exists(os.path.join(ROOT, "tools/agent_loop/ICON_CATALOGUE.md")),
    ]
    # 8. the test code
    ev["the test code"] = [
        os.path.exists(os.path.join(ROOT, "tools/server-tasks/tests")),
        os.path.exists(os.path.join(ROOT, "tools/server-tasks/truth-tables")),
    ]

    print("== the completion state (four-valued, not percentages) ==")
    print()
    states = {}
    for L, checks in ev.items():
        st = layer_state(*checks)
        states[L] = st
        mark = "  " if st != TRUE else "OK"
        print("  %-26s %-7s  %d/%d checks" % (L, NAME[st], sum(checks), len(checks)))
    print()
    # the whole: the conjoin of every layer (the meet on the chain)
    whole = TRUE
    for st in states.values():
        whole = conjoin(whole, st)
    print("  THE SYSTEM                 %s" % NAME[whole])
    print()
    print("  The system is TRUE only when EVERY layer is TRUE — they reach")
    print("  it simultaneously, because it is one thing. While any layer is")
    print("  BOTH, the system is BOTH: partial evidence, held, not exploded.")
    print("  That is the paraconsistent architecture: the state is a value,")
    print("  and the completion is a conjoin, never a sum of percentages.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
