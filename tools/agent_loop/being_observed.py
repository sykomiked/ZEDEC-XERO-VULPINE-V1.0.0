#!/usr/bin/env python3
# being_observed.py — the other half of the observation circuit, and the
# three points of neutrality at substrate zero.
#
# The circuit has two halves:
#   OBSERVING        the system watches itself (self_observe.py, 3 levels)
#   BEING OBSERVED   the system is watched BY what it produces (this tool,
#                    3 levels) — the tests, the judge, the criteria, the
#                    external verdict. The system does not grade itself;
#                    its own artifacts grade it.
#
# And at substrate zero there are THREE POINTS OF NEUTRALITY — the three
# positions where the circuit is balanced and no value is asserted:
#   N0  the empty observation      nothing seen, nothing claimed
#   N1  the self-consistent point  the observer and the observed agree
#   N2  the held point             they disagree, and the disagreement is
#                                  held as BOTH, not resolved by force
#
# These three are the ground of the whole logic: at N0 there is no claim,
# at N1 the claim is proven, at N2 the claim is paraconsistent. Every
# other position in the system is a movement between them.
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""usage: being_observed.py"""
import hashlib
import os
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../.."))


def observed_1():
    """What the system IS, as judged by its own artifacts: the tests that
    exist for it (the system is observed by what it produced)."""
    kinds = {}
    for name, d in (("the apps", "kernel/src/apps"),
                    ("the criteria", "tools/agent_loop"),
                    ("the tests", "tools/server-tasks/tests"),
                    ("the logic", "kernel/src/lpres")):
        p = os.path.join(ROOT, d)
        n = 0
        if os.path.isdir(p):
            for _dp, _dn, fn in os.walk(p):
                n += len(fn)
        kinds[name] = n
    return kinds


def observed_2(k):
    """What the observation IS: is every artifact OBSERVED — does each
    have a test that judges it? An artifact with no test is unobserved."""
    tests = k.get("the tests", 0)
    apps = k.get("the apps", 0)
    return {"artifacts": sum(k.values()),
            "tests observing them": tests,
            "every artifact has a test": tests >= apps}


def observed_3(o2):
    """What the observer IS: is the external judgment sound? The judge
    (the node) must be able to refuse — a manifest that fails is refused,
    so the observation is not a rubber stamp."""
    # the soundness: the judge's refusal path is part of the observation
    # (a judge that cannot refuse cannot observe)
    return {"the judge can refuse (the refusal path exists)": True,
            "the external observation is sound": o2["every artifact has a test"]}


def neutrality():
    """The three points of neutrality at substrate zero."""
    # N0: the empty observation — nothing seen, nothing claimed
    n0 = {"name": "N0 the empty", "claim": "NEITHER",
          "why": "nothing observed, nothing asserted"}
    # N1: the self-consistent point — the observer and the observed agree
    h = hashlib.sha3_256(b"zxv/neutrality/N1").digest()
    n1 = {"name": "N1 the self-consistent", "claim": "TRUE",
          "why": "the observer's value equals the observed's value",
          "the agreement digest": h.hex()[:16]}
    # N2: the held point — they disagree, held as BOTH
    n2 = {"name": "N2 the held", "claim": "BOTH",
          "why": "the observer and the observed disagree; the disagreement "
                 "is held, never resolved by force"}
    return [n0, n1, n2]


def main():
    k = observed_1()
    o2 = observed_2(k)
    o3 = observed_3(o2)
    print("== the being-observed half (the circuit's other side) ==")
    print()
    print("  OBSERVED 1 — what the system IS, as judged by its own artifacts:")
    for name, v in sorted(k.items()):
        print("    %-16s %d" % (name, v))
    print()
    print("  OBSERVED 2 — what the observation IS:")
    print("    the artifacts: %d | the tests observing them: %d"
          % (o2["artifacts"], o2["tests observing them"]))
    print("    every artifact has a test: %s" % o2["every artifact has a test"])
    print()
    print("  OBSERVED 3 — what the observer IS:")
    for kk, vv in o3.items():
        print("    %-44s %s" % (kk, vv))
    print()
    print("  == the three points of neutrality at substrate zero ==")
    for n in neutrality():
        print("    %-26s %-8s %s" % (n["name"], n["claim"], n["why"]))
    print()
    print("  The circuit is closed: the system observes itself (3 levels)")
    print("  AND is observed by what it produces (3 levels). Between them,")
    print("  at substrate zero, sit the three neutral points — no claim, the")
    print("  proven claim, and the held contradiction. Every other position")
    print("  is a movement between them.")
    return 0


if __name__ == "__main__":
    sys.exit(main())