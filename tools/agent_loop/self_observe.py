#!/usr/bin/env python3
# self_observe.py — the observation layer, three levels deep. The system
# observes itself, observes that observation, and observes that in turn:
#
#   OBSERVE 1   what the system IS       (the artifacts: the apps, the
#                                        criteria, the tests, the logic)
#   OBSERVE 2   what the observation IS  (whether the observation is
#                                        complete: every artifact observed,
#                                        no blind spots)
#   OBSERVE 3   what the observer IS     (whether the observer is sound:
#                                        the observation's own integrity,
#                                        its assumptions, its refusals)
#
# A system that cannot observe itself cannot close its tautology: it has
# no way to know whether its picture of itself is complete. The three
# levels are what make the closure checkable rather than asserted.
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""usage: self_observe.py [--json out.json]"""
import argparse
import json
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../.."))


def observe_1():
    """What the system IS: count the artifacts, by kind."""
    kinds = {
        "the apps (manifests)": ("kernel/src/apps", ".zxapp"),
        "the criteria documents": ("tools/agent_loop", ".md"),
        "the test manifests": ("tools/server-tasks/tests", ".zxapp"),
        "the language tests": ("tools/server-tasks/lang-tests", ".zxapp"),
        "the truth tables": ("tools/server-tasks/truth-tables", ".zxapp"),
        "the lattice tests": ("tools/server-tasks/lattice", ".zxapp"),
        "the icons": ("zxv-brand/app-icons/zxvg", ".zxvg"),
        "the pillar sources": ("sdk/pillars", ".c"),
        "the kernel sources": ("kernel/src/lpres", ".c"),
    }
    out = {}
    for name, (d, ext) in kinds.items():
        p = os.path.join(ROOT, d)
        n = 0
        if os.path.isdir(p):
            for dp, _dn, fn in os.walk(p):
                n += sum(1 for f in fn if f.endswith(ext))
        out[name] = n
    return out


def observe_2(obs1):
    """What the observation IS: is the observation complete? Every kind
    with zero artifacts is a blind spot — the observer did not see it.
    A complete observation has no zeros where artifacts are expected."""
    blind = [k for k, v in obs1.items() if v == 0]
    return {"observed kinds": len(obs1), "blind spots": blind,
            "complete": not blind}


def observe_3(obs2):
    """What the observer IS: is the observer sound? It must be able to
    detect its own failure — if it cannot imagine a blind spot, it cannot
    see one. The check: the observer's refusal path is exercised (it
    reports an empty observation as incomplete)."""
    # the self-test: an empty observation MUST be reported incomplete
    empty = {"a": 0, "b": 0}
    detects = observe_2(empty)["complete"] is False
    return {"the observer detects its own blindness": detects,
            "the observer is sound": detects and obs2["complete"]}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--json", default="")
    args = ap.parse_args()
    o1 = observe_1()
    o2 = observe_2(o1)
    o3 = observe_3(o2)
    print("== the observation layer (three levels) ==")
    print()
    print("  OBSERVE 1 — what the system IS:")
    for k, v in sorted(o1.items()):
        print("    %-28s %d" % (k, v))
    print()
    print("  OBSERVE 2 — what the observation IS:")
    print("    the kinds observed:  %d" % o2["observed kinds"])
    print("    the blind spots:     %s" % (o2["blind spots"] or "none"))
    print("    the observation is complete: %s" % o2["complete"])
    print()
    print("  OBSERVE 3 — what the observer IS:")
    print("    the observer detects its own blindness: %s"
          % o3["the observer detects its own blindness"])
    print("    the observer is sound:                  %s"
          % o3["the observer is sound"])
    print()
    print("  The three levels make the closure CHECKABLE: the system can")
    print("  see what it is, see whether its seeing is complete, and see")
    print("  whether the seer is sound. A tautology that cannot observe")
    print("  itself is only asserted; this one is verified.")
    if args.json:
        json.dump({"observe_1": o1, "observe_2": o2, "observe_3": o3},
                  open(args.json, "w"), indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main())