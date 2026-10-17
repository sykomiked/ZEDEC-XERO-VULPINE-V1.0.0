#!/usr/bin/env python3
# patterns.py — the canonical logic patterns behind the 407 apps: each
# pattern is one shape of exact arithmetic (a limit, a conservation, a
# share, a modular draw...), authored once with grammar-correct bodies and
# checks with computed answers. mk_criteria_v2 instantiates the patterns
# each app's wave spec names, so every app's criteria carry the exact
# entries, languages and checks.
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""The pattern table. Each entry:
  name, kind, args, langs, logic (the body), checks (the exact line),
  matches (the spec-clause keywords that select this pattern).
"""

PATTERNS = [
    {
        "name": "limit_excess",
        "kind": "num",
        "args": "value,limit",
        "langs": "trebago, eve",
        "logic": "0 when value <= limit, else value - limit (the ceiling's excess)",
        "checks": "check limit_excess value=0,1000,1001,2000 limit=1000,1000,1000,1000 = 0,0,1,1000",
        "matches": ["limit", "limits", "ceiling", "cap", "maximum", "budget"],
    },
    {
        "name": "within_range",
        "kind": "truth",
        "args": "value,min,max",
        "langs": "eve, kron",
        "logic": "true when min <= value <= max",
        "checks": "check within_range value=0,50,100,101 min=0,0,0,0 max=100,100,100,100 = true,true,true,false",
        "matches": ["range check", "within range", "in range", "probe readings", "brightness", "volume"],
    },
    {
        "name": "conserved_sum",
        "kind": "truth",
        "args": "whole,a,b,c,d",
        "langs": "trebago, sutra",
        "logic": "true when a + b + c + d == whole exactly (nothing created or destroyed)",
        "checks": "check conserved_sum whole=100,100,100,101 a=25,0,0,25 b=25,0,0,25 c=25,0,0,25 d=25,100,99,25 = true,true,false,false",
        "matches": ["conserved", "conservation", "losses", "through the stages", "nothing created"],
    },
    {
        "name": "exact_total",
        "kind": "num",
        "args": "n,a,b,c,d",
        "langs": "trebago",
        "logic": "the exact sum of the first n of a,b,c,d (the accumulator law)",
        "checks": "check exact_total n=2,4,1,0 a=10,1,7,5 b=20,2,7,5 c=0,3,7,5 d=0,4,7,5 = 30,10,7,0",
        "matches": ["total size", "total exact", "sum", "totals", "accumulat"],
    },
    {
        "name": "share_x100",
        "kind": "num",
        "args": "part,whole",
        "langs": "kron, trebago",
        "logic": "the part's share of the whole in hundredths of a percent, floor: part * 10000 / whole",
        "checks": "check share_x100 part=1,1,1,50 whole=3,4,100,100 = 3333,2500,100,5000",
        "matches": ["share", "shares", "proportion", "coverage x100", "split", "fraction"],
    },
    {
        "name": "largest_remainder",
        "kind": "num",
        "args": "pool,w1,w2,w3,w4",
        "langs": "sutra, trebago",
        "logic": "the first recipient's share of the pool by weight (floor), plus one while the largest remainders last",
        "checks": "check largest_remainder pool=100,10,7,100 w1=1,1,1,1 w2=1,1,1,1 w3=1,1,1,1 w4=1,0,0,0 = 25,4,3,34",
        "matches": ["largest remainder", "split by weight", "by weight", "distribute", "distribution"],
    },
    {
        "name": "modular_draw",
        "kind": "num",
        "args": "seed,members,step",
        "langs": "trebago, sutra",
        "logic": "the sortition's modular walk: (seed * 1103515245 + 12345 + step * 7) mod members, integers only",
        "checks": "check modular_draw seed=1,1,1,2 members=10,10,10,7 step=0,1,2,0 = 5,2,9,6",
        "matches": ["draw", "sortition", "seed", "lottery", "raffle", "shuffle"],
    },
    {
        "name": "difference",
        "kind": "num",
        "args": "a,b",
        "langs": "trebago, kron",
        "logic": "a - b exactly (never negative in this app's domain)",
        "checks": "check difference a=10,5,0,100 b=3,5,0,99 = 7,0,0,1",
        "matches": ["difference", "remaining", "left", "minus", "net"],
    },
    {
        "name": "product",
        "kind": "num",
        "args": "a,b",
        "langs": "sutra, kron",
        "logic": "a * b exactly in minor units",
        "checks": "check product a=1,12,0,999 b=1250,999,7,1000 = 1250,11988,0,999000",
        "matches": ["value", "price", "sale", "product", "cost", "amount", "fee"],
    },
    {
        "name": "count_ok",
        "kind": "truth",
        "args": "count,max",
        "langs": "eve, kron",
        "logic": "true when 0 < count <= max (the registry's law)",
        "checks": "check count_ok count=1,100,101,0 max=100,100,100,100 = true,true,false,false",
        "matches": ["count", "number of", "file-count", "at most", "registry"],
    },
    {
        "name": "composite_key",
        "kind": "num",
        "args": "high,low",
        "langs": "myra, kron",
        "logic": "the record's index: high * 1000 + low (the cellar's composite key)",
        "checks": "check composite_key high=2024,0,1,999 low=7,0,999,999 = 2024007,0,1999,999999",
        "matches": ["index", "key", "record", "records", "label", "lookup"],
    },
    {
        "name": "grade_band",
        "kind": "num",
        "args": "value,b1,b2,b3,b4",
        "langs": "kron, trebago",
        "logic": "the band index 0..4 of the value across the four ascending bounds",
        "checks": "check grade_band value=0,5,15,25,100 b1=10,b2=20,b3=30,b4=40 = 0,0,1,2,4",
        "matches": ["band", "grade", "tier", "level", "bracket"],
    },
    {
        "name": "ratio_x1000",
        "kind": "num",
        "args": "num,den",
        "langs": "kron, trebago",
        "logic": "the ratio in thousandths, floor: num * 1000 / den",
        "checks": "check ratio_x1000 num=1,1,1,3 den=3,8,1000,4 = 333,125,1,750",
        "matches": ["ratio", "rate", "per thousand", "x1000", "yield", "concentration"],
    },
    {
        "name": "running_state",
        "kind": "truth",
        "args": "state,event",
        "langs": "eve, sutra",
        "logic": "the transition's legality: state + event -> true when the move is allowed",
        "checks": "check running_state state=0,0,1,1 event=1,2,1,2 = true,false,true,false",
        "matches": ["state", "transition", "lifecycle", "status", "move"],
    },
    {
        "name": "flags_none",
        "kind": "truth",
        "args": "bits,forbidden",
        "langs": "eve",
        "logic": "true when (bits & forbidden) == 0 (no refused bit set)",
        "checks": "check flags_none bits=0,1,2,3 forbidden=1,1,1,1 = true,false,true,false",
        "matches": ["bits", "flags", "standing", "unappealed", "permission", "mask"],
    },
    {
        "name": "clamp",
        "kind": "num",
        "args": "value,min,max",
        "langs": "kron, trebago",
        "logic": "value clamped into [min,max]",
        "checks": "check clamp value=-5,0,50,150 min=0,0,0,0 max=100,100,100,100 = 0,0,50,100",
        "matches": ["clamp", "bound", "saturate", "cap at"],
    },
]

INDEX = {}
for p in PATTERNS:
    for kw in p["matches"]:
        INDEX.setdefault(kw.lower(), []).append(p)


def match_patterns(clause):
    """The patterns a spec clause names (the keyword hits, best first)."""
    hits = []
    c = clause.lower()
    for kw, ps in INDEX.items():
        if kw in c:
            for p in ps:
                if p not in hits:
                    hits.append(p)
    return hits
