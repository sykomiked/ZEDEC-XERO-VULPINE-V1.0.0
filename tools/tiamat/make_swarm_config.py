#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Assemble the disc's text swarm for zxv-host (kernel/arch/hosted/zxv_tiamat.h).

Reads the approved roster (docs/release/swarm_model_manifest.json), the
organs and witness rule (tiamat_swarm.json) and the server's adapter work
(extract_adapters.py reports and build_disc_models.sh's GGUF adapters), and
writes a --tiamat config with every disc cell the integer engine runs today:

- a cell with no trunk runs whole on its own slot (its shipped GGUF);
- a cell on a trunk runs on the trunk's slot with its GGUF LoRA when S3
  passed it (ADAPTER or UPSTREAM_ADAPTER);
- a cell S3 kept whole (FULL) runs on its own slot from a whole GGUF (the
  server's F16 conversion, <cell>-f16.gguf, or an upstream GGUF in src/);
- every other cell is left out, and the assembly report says why.

Lineage is the developer of the cell's pretrained family (its trunk's, for a
cell on a trunk). tiamat_swarm.json's witness rule would accept another
trunk of the same developer, but Qwen2.5's instruct, coder and maths models
share one pretraining, so this takes the stricter reading: a witness comes
from another developer's model, and a fine-tune never witnesses its family. The voice_of_tiamat lead comes
first (H2 takes the first cell on a tie). Only cells already in the approved
manifest can be assembled: the swarm grows through the roster (a new cell is
admitted, checked and approved there), never by this script.

    python3 make_swarm_config.py --models /srv/tiamat/models --adapters /srv/tiamat/adapters \\
        --out swarm.conf --report assembly.json
"""
import argparse
import hashlib
import json
import pathlib
import re
import sys

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
MANIFEST = ROOT / "docs/release/swarm_model_manifest.json"
SWARM = HERE / "tiamat_swarm.json"
SLOTS, CELLS, KEYWORDS, WORD = 12, 16, 24, 24  # zxv_tiamat.h ZXV_TIAMAT_SLOTS, _CELLS, _KEYWORDS, _WORD

# words that route a question to an organ (H2 counts them as substrings, so
# none is short enough to sit inside a common word); the lead's organ has none
ORGAN_WORDS = {
    "smith": ["code", "python", "function", "program", "script", "bug", "compile", "javascript",
              "rust", "regex", "c++", "refactor"],
    "warden": ["security", "vulnerability", "vulnerabilities", "exploit", "injection", "unsafe",
               "audit", "review"],
    "scribe": ["extract", "summarise", "summarize", "summary", "sql", "query", "entities"],
    "reckoner": ["solve", "equation", "prove", "proof", "integral", "derivative", "algebra",
                 "probability", "theorem", "puzzle"],
    "arbiter": ["check", "verify", "argument", "valid", "fallacy", "contradiction", "contradicts",
                "premise"],
    "clerk": ["regulation", "legal", "compliance", "statute", "gdpr", "directive"],
}
PASSED = ("ADAPTER", "UPSTREAM_ADAPTER")


def sha256(p):
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for b in iter(lambda: f.read(1 << 22), b""):
            h.update(b)
    return h.hexdigest()


def quote(s):
    s = str(s)
    if '"' in s:
        raise SystemExit(f"a path with a double quote is not supported: {s}")
    return f'"{s}"' if any(c in s for c in " \t#") else s


def lineage_of(m):
    """The developer's first word ("qwen", "hugging"): short enough for the app's word field."""
    return re.sub(r"[^a-z0-9]", "", m["developer"].split()[0].lower())[:WORD - 1]


def organ_of(cfg):
    out = {}
    for organ, o in cfg["organs"].items():
        for c in o["cells"]:
            out.setdefault(c, organ)
    return out


def s3_results(adapters):
    res = {}
    for rp in sorted(adapters.glob("report_*.json")):
        for cell, e in json.loads(rp.read_text())["cells"].items():
            res[cell] = e
    return res


def assemble(man, cfg, models, adapters, pin):
    by = {m["dragon_name"]: m for m in man["models"]}
    trunk_of = {base: t["trunk"] for base, t in cfg["tissues"].items()}
    organs = organ_of(cfg)
    s3 = s3_results(adapters)
    lead = cfg["organs"]["voice_of_tiamat"]["lead"]
    slots, slot_of, cells, report = [], {}, [], {}

    def slot(key, path):
        if key not in slot_of:
            if len(slots) >= SLOTS:
                return None
            slot_of[key] = len(slots)
            slots.append((path, sha256(path) if pin else None))
        return slot_of[key]

    def shipped(m):
        for f in m["shipped_files"]:
            p = models / m["dragon_name"] / "ship" / f["file"]
            if p.is_file() and p.name.endswith(".gguf"):
                return p
        return None

    order = [lead] + [m["dragon_name"] for m in man["models"] if m["dragon_name"] != lead]
    for name in order:
        m = by[name]
        if m["placement"] not in ("disc", "disc_gated"):
            continue
        organ = organs.get(name, m["organ"])
        if not m["engine"].startswith("runs now"):
            report[name] = {"assembled": False, "organ": organ, "why": f"engine: {m['engine']}"}
            continue
        if organ == "trunk" and name != lead:
            report[name] = {"assembled": False, "organ": organ, "why": "a trunk only: its cells run on it"}
            continue
        entry = {"organ": organ}
        if not m["trunk"]:
            p = shipped(m)
            if not p:
                report[name] = {"assembled": False, "organ": organ, "why": "its shipped GGUF is not in --models"}
                continue
            s, lineage, adapter, how = slot(name, p), lineage_of(m), None, "whole (shipped GGUF)"
        else:
            tname = trunk_of.get(m["trunk"])
            tm = by.get(tname)
            r = s3.get(name)
            lineage = lineage_of(tm) if tm else lineage_of(m)
            if not r:
                report[name] = {"assembled": False, "organ": organ, "why": "no S3 result for it in --adapters"}
                continue
            if r["result"] in PASSED:
                ad = adapters / f"{name.lower()}-lora-f16.gguf"
                tp = shipped(tm) if tm else None
                if not ad.is_file() or not tp:
                    report[name] = {"assembled": False, "organ": organ, "s3": r["result"],
                                    "why": f"{'its GGUF adapter' if not ad.is_file() else 'its trunk GGUF'} is missing"}
                    continue
                s, adapter, how = slot(tname, tp), ad, f"{r['result'].lower()} on {tname}"
                entry["adapter_sha256"], entry["adapter_bytes"] = sha256(ad), ad.stat().st_size
            elif r["result"] == "FULL":
                cands = [models / name / f"{name.lower()}-f16.gguf"] + sorted((models / name / "src").glob("*.gguf"))
                p = next((c for c in cands if c.is_file()), None)
                if not p:
                    report[name] = {"assembled": False, "organ": organ, "s3": "FULL",
                                    "why": "kept whole by S3, and no whole GGUF of it yet"}
                    continue
                s, adapter, how = slot(name, p), None, f"whole ({p.name}): S3 kept it FULL"
            else:
                report[name] = {"assembled": False, "organ": organ, "s3": r["result"],
                                "why": r.get("why", "S3 did not pass it")}
                continue
        if s is None or len(cells) >= CELLS:
            report[name] = {"assembled": False, "organ": organ, "why": "the app's slot or cell limit is full"}
            continue
        kws = ORGAN_WORDS.get(organ, [])[:KEYWORDS]
        cells.append((name, s, lineage, adapter, kws))
        report[name] = {"assembled": True, **entry, "slot": s, "lineage": lineage, "how": how}
    return slots, cells, report


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--models", required=True, help="fetch_pinned.py's --dest (<cell>/ship, <cell>/src)")
    ap.add_argument("--adapters", required=True, help="extract_adapters.py's --out, with the GGUF adapters")
    ap.add_argument("--out", required=True)
    ap.add_argument("--report", required=True)
    ap.add_argument("--log", help="default: swarm_log.jsonl beside --out")
    ap.add_argument("--manifest", default=str(MANIFEST))
    ap.add_argument("--no-pin", action="store_true", help="leave slot SHA-256 pins out (tests)")
    a = ap.parse_args()
    man = json.loads(pathlib.Path(a.manifest).read_text())
    cfg = json.loads(SWARM.read_text())
    slots, cells, report = assemble(man, cfg, pathlib.Path(a.models).resolve(),
                                    pathlib.Path(a.adapters).resolve(), not a.no_pin)
    lead = cfg["organs"]["voice_of_tiamat"]["lead"]
    if not cells or cells[0][0] != lead:
        raise SystemExit(f"the lead {lead} could not be assembled: {report.get(lead)}")
    out = pathlib.Path(a.out)
    log = a.log or str(out.with_name("swarm_log.jsonl"))
    lines = ["# Tiamat disc text swarm (kernel/arch/hosted/zxv_tiamat.h), written by",
             "# tools/tiamat/make_swarm_config.py; the assembly report says what is left out and why"]
    for i, (p, h) in enumerate(slots):
        lines.append(f"slot {i} {quote(p)}" + (f" {h}" if h else ""))
    for name, s, lineage, adapter, kws in cells:
        lines.append(f"cell {name} {s} {lineage} {quote(adapter) if adapter else '-'}"
                     + "".join(f" {k}" for k in kws))
    lines.append(f"log {quote(log)}")
    out.write_text("\n".join(lines) + "\n")
    organs = {}
    for name, e in report.items():
        organs.setdefault(e["organ"], {"assembled": [], "left_out": []})[
            "assembled" if e["assembled"] else "left_out"].append(name)
    lineages = {c[2] for c in cells}
    doc = {"config": str(out), "slots": len(slots), "cells": len(cells), "lineages": sorted(lineages),
           "witness_possible": len(lineages) > 1, "organs": organs, "per_cell": report}
    pathlib.Path(a.report).write_text(json.dumps(doc, indent=2) + "\n")
    print(f"{len(cells)} cells on {len(slots)} slots, {len(lineages)} lineages; "
          f"{sum(not e['assembled'] for e in report.values())} left out -> {a.report}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
