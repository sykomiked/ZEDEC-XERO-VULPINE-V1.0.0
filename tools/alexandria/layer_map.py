#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""layer_map.py — write docs/spec/LAYER_MAP.md from MATRIX.tsv and a pack.

    layer_map.py FILES.TSV > docs/spec/LAYER_MAP.md

FILES.TSV is the one pack_matrix.py --out-dir writes (path, layer, tier,
CID). The page lists the placement rules, booklet 000, and every layer's
app, homes, file count and the modules the glue pulled in."""
import collections
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pack_matrix as pm  # noqa: E402


def main():
    lay = pm.layers()
    files = [l.rstrip("\n").split("\t") for l in open(sys.argv[1]) if l.strip() and not l.startswith("#")]
    rules = [pm.home_path(h) for v in lay.values() for h in v["homes"]]
    cnt, loose = collections.Counter(), collections.defaultdict(set)
    for f in files:
        p, k = f[0], int(f[1])
        cnt[k] += 1
        if p.startswith("docs/") or "/" not in p:
            continue
        if not any(p == h or p.startswith(h + "/") for h in rules):
            loose[k].add(p.split("/")[2] if p.startswith("kernel/src/") else p.split("/")[0])
    used = sum(1 for k in range(pm.LAYERS) if cnt[k])
    w = sys.stdout.write
    w("<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->\n")
    w("<!-- SPDX-License-Identifier: Apache-2.0 -->\n\n")
    w("# Layer map: what goes in each of the 145 layers\n\n")
    w("This page writes out the mapping `tools/alexandria/pack_matrix.py` applies. The source of "
      "truth is `kernel/src/alexandria/MATRIX.tsv` (one line per layer) and the rules below; "
      "`tools/alexandria/layer_map.py` generated this page from that file and one pack of the "
      "current tree, so the counts change as the tree does. MATRIX.md has the formats, and its "
      "section 8 how a layer is addressed.\n\n")
    w("## Rules, in order\n\n")
    w("1. **The corpus is every tracked file of the repository** (`git ls-files`): the kernel, "
      "the host app and daemon, boot, the SDK, tools, tests, assets and documents.\n")
    w("2. **Booklet 000** is the first booklet of layer 0 and the first thing read after boot. It "
      "holds, in this order: " + ", ".join("`%s`" % p for p in pm.BOOKLET_000) +
      ". The packer refuses a tree where any of them is outside layer 0.\n")
    w("3. **Layer N, for N from 1 to 144, is L1 system app N**, in the order of "
      "`zxv-ui/APP_CATALOG_407.csv`, with that app's vector icon.\n")
    w("4. **A file goes to the layer whose home matches it.** A home is a path under "
      "`kernel/src`, or from the top of the repository when it starts with `/`; the longest "
      "match wins.\n")
    w("5. **`docs/`, and files at the top of the repository that no home names, go to layer "
      "130**, the developer documentation.\n")
    w("6. **A module no home names** (its directory under `kernel/src`, or its top-level "
      "directory) goes to the layer its glue links pull hardest, app layers preferred over the "
      "core; if nothing pulls it, to the core. The table lists where these landed.\n\n")
    w("## The layers\n\n")
    w("Files: how many files the current tree puts there. Pulled in: modules placed by rule 6.\n\n")
    w("| Layer | App | Group | Homes | Files | Pulled in |\n|---|---|---|---|---|---|\n")
    for k in range(pm.LAYERS):
        v = lay[k]
        h = " ".join("`%s`" % x for x in v["homes"]) or "none yet"
        if k == 130:
            h += " and `docs/`"
        w("| %d | %s | %s | %s | %d | %s |\n" % (k, v["app"], v["group"], h, cnt[k],
                                                 " ".join("`%s`" % x for x in sorted(loose[k]))))
    w("\n%d files in %d layers that hold content; %d layers are empty until their apps' code "
      "lands.\n" % (len(files), used, pm.LAYERS - used))


if __name__ == "__main__":
    main()
