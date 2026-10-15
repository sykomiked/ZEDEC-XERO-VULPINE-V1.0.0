#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Test make_swarm_config.py on a stand-in server layout (no real weights).

Every disc cell gets a small file in its place; the adapter reports cover
each S3 outcome (adapter, upstream adapter, kept whole with and without a
whole GGUF, error, missing). The config written must parse with the app's
own parser (zxv_tiamat.c, compiled here with cc) and match the rules: the
lead first, adapters on their trunk's slot, lineage by trunk, every cell
the engine cannot run left out with its reason.

    python3 tools/tiamat/test_make_swarm_config.py
"""
import json
import pathlib
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
FAILS = 0

PARSE_C = r"""
#include <stdio.h>
#include "zxv_tiamat.h"
int main(int argc, char **argv)
{
    static zxv_tiamat_conf_t c;
    char err[1200];
    if (argc != 2 || zxv_tiamat_load(argv[1], &c, err, sizeof err) != 0) {
        printf("ERR %s\n", err);
        return 1;
    }
    for (unsigned s = 0; s < ZXV_TIAMAT_SLOTS; s++)
        if (c.slot_path[s][0]) printf("slot %u %s\n", s, c.slot_path[s]);
    for (unsigned i = 0; i < c.n_cells; i++)
        printf("cell %s %u %s %s %u\n", c.cell[i].name, c.cell[i].slot, c.cell[i].lineage,
               c.cell[i].adapter[0] ? c.cell[i].adapter : "-", c.cell[i].n_kw);
    return 0;
}
"""


def check(ok, what):
    global FAILS
    print(("  ok   " if ok else "  FAIL ") + what)
    FAILS += not ok


def touch(p, data=b"stand-in"):
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_bytes(data)


def main():
    man = json.loads((ROOT / "docs/release/swarm_model_manifest.json").read_text())
    by = {m["dragon_name"]: m for m in man["models"]}
    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        models, adapters = tmp / "models", tmp / "adapters"
        for m in man["models"]:
            if m["placement"] in ("disc", "disc_gated"):
                for f in m["shipped_files"]:
                    touch(models / m["dragon_name"] / "ship" / f["file"])
        touch(models / "Statutewyrmling" / "src" / "euaiact-qwen2.5-0.5b-q4_k_m.gguf")
        touch(models / "Wardendrake" / "wardendrake-f16.gguf")
        reports = {
            "Spinewyrm": {"Wardendrake": "FULL", "Reviewdrake": "UPSTREAM_ADAPTER", "Debatelindworm": "UPSTREAM_ADAPTER"},
            "Numberwyrm": {"Theoremwyrm": "ADAPTER", "Puzzlewyrm": "ERROR"},
            "Kindlewyrm": {"Statutewyrmling": "FULL"},
            "Awllindworm": {"Mendlindworm": "FULL", "Recordlindworm": "ADAPTER"},
        }
        for trunk, cells in reports.items():
            body = {"cells": {c: {"result": r, **({"why": "planted"} if r == "ERROR" else {})}
                              for c, r in cells.items()}}
            touch(adapters / f"report_{trunk}.json", json.dumps(body).encode())
            for c, r in cells.items():
                if r in ("ADAPTER", "UPSTREAM_ADAPTER") and c != "Debatelindworm":
                    touch(adapters / f"{c.lower()}-lora-f16.gguf", c.encode())
        conf, rep = tmp / "swarm.conf", tmp / "assembly.json"
        r = subprocess.run([sys.executable, str(HERE / "make_swarm_config.py"), "--models", str(models),
                            "--adapters", str(adapters), "--out", str(conf), "--report", str(rep)],
                           capture_output=True, text=True)
        print(r.stdout + r.stderr, end="")
        check(r.returncode == 0, "the config is written")
        doc = json.loads(rep.read_text())
        pc = doc["per_cell"]
        asm = {n for n, e in pc.items() if e["assembled"]}
        print("  assembled:", " ".join(sorted(asm)))

        exe = tmp / "parse"
        (tmp / "parse.c").write_text(PARSE_C)
        h = ROOT / "kernel/arch/hosted"
        cc = subprocess.run(["cc", "-std=c11", "-O1", "-Wall", "-Werror", f"-I{h}", str(tmp / "parse.c"),
                             str(h / "zxv_tiamat.c"), "-o", str(exe)], capture_output=True, text=True)
        check(cc.returncode == 0, "the app's parser builds" + (": " + cc.stderr if cc.returncode else ""))
        p = subprocess.run([str(exe), str(conf)], capture_output=True, text=True)
        check(p.returncode == 0, "the app's own parser reads the config: " + p.stdout.strip().splitlines()[0]
              if p.stdout else "the app's own parser reads the config")
        parsed = [l.split() for l in p.stdout.splitlines()]
        pslots = {int(l[1]): l[2] for l in parsed if l[0] == "slot"}
        pcells = [l for l in parsed if l[0] == "cell"]
        names = [c[1] for c in pcells]
        check(names and names[0] == "Spinewyrm", "the lead comes first")
        check(set(names) == asm, "the parsed cells are the report's assembled cells")
        cell = {c[1]: c for c in pcells}
        check(cell["Reviewdrake"][2] == cell["Spinewyrm"][2] and cell["Reviewdrake"][4].endswith("reviewdrake-lora-f16.gguf"),
              "an upstream adapter runs on its trunk's slot with its GGUF LoRA")
        check(cell["Recordlindworm"][3] == cell["Spinewyrm"][3] == cell["Codelindworm"][3] == "qwen"
              and "Awllindworm" not in names,
              "an adapter takes its trunk's lineage, one per developer; a bare trunk is not a cell")
        check(cell["Theoremwyrm"][2] != cell["Recordlindworm"][2], "two trunks, two slots")
        check(cell["Wardendrake"][4] == "-" and pslots[int(cell["Wardendrake"][2])].endswith("wardendrake-f16.gguf")
              and cell["Wardendrake"][3] == cell["Spinewyrm"][3],
              "a cell S3 kept whole runs whole from its F16 GGUF, still in its trunk's lineage")
        check(pslots[int(cell["Statutewyrmling"][2])].endswith("euaiact-qwen2.5-0.5b-q4_k_m.gguf"),
              "a kept-whole cell with an upstream GGUF uses it")
        check(not pc["Mendlindworm"]["assembled"] and "no whole GGUF" in pc["Mendlindworm"]["why"],
              "a kept-whole cell with no whole GGUF is left out, saying so")
        check(not pc["Puzzlewyrm"]["assembled"] and pc["Puzzlewyrm"]["why"] == "planted", "an S3 error is left out with its reason")
        check(not pc["Debatelindworm"]["assembled"] and "adapter is missing" in pc["Debatelindworm"]["why"],
              "a passed adapter without its GGUF is left out")
        check(all(not pc[n]["assembled"] and pc[n]["why"].startswith("engine:")
                  for n in pc if not by[n]["engine"].startswith("runs now")),
              "every cell the engine cannot run is left out, naming the engine path")
        check(cell["Vouchwyvern"][3] != cell["Spinewyrm"][3] and doc["witness_possible"],
              "a witness of another lineage is in the swarm")
        check(int(cell["Codelindworm"][5]) > 0 and int(cell["Spinewyrm"][5]) == 0,
              "organ cells carry routing words; the lead carries none")
        check(all(len(l) == 4 and len(l[3]) == 64 for l in (x.split() for x in conf.read_text().splitlines())
                  if l[:1] == ["slot"]), "every slot is pinned with its SHA-256")
    print(f"[{'PASS' if FAILS == 0 else 'FAIL'}] make_swarm_config: {FAILS} failed")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
