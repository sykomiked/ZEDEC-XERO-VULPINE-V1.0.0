#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""datapack.py's tests: the licence classes, the source table's rules, no fetch
without approval, nothing outside build/, and packs that are the same every time and
hold only what their class allows. Fixtures live under build/test_datapack."""
import gzip
import hashlib
import io
import os
import shutil
import sys
import tarfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import datapack as dp  # noqa: E402

checks = fails = 0


def CHECK(ok, what):
    global checks, fails
    checks += 1
    if not ok:
        fails += 1
        print(f"  FAIL {what}")


def refused(fn, *args, has=""):
    try:
        fn(*args)
    except SystemExit as e:
        return has in str(e)
    except Exception:  # noqa: BLE001 - anything but the refusal is a failure
        return False
    return False


HEAD = "# fixture\n"


def table(rows):
    path = os.path.join(T, "SOURCES.tsv")
    with open(path, "w", encoding="utf-8") as f:
        f.write(HEAD + "".join("\t".join(r) + "\n" for r in rows))
    return dp.read_sources(path)


def lexicon(lid, lic):
    xml = f'<?xml version="1.0"?>\n<LexicalResource>\n<Lexicon id="{lid}"\n  language="xx"\n'
    if lic:
        xml += f'  license="{lic}"\n'
    return (xml + '  version="1">\n</Lexicon>\n</LexicalResource>\n').encode()


def members(path):
    with tarfile.open(path) as t:
        return {m.name: t.extractfile(m).read() for m in t.getmembers()}


T = os.path.join(dp.BUILD, "test_datapack")
shutil.rmtree(T, ignore_errors=True)
os.makedirs(T)

# the classes
for lic, want in (("CC0-1.0", dp.TRUE), ("CC-BY-4.0", dp.TRUE), ("NLOD-2.0", dp.TRUE),
                  ("LicenseRef-US-Government-PD", dp.TRUE), ("CC-BY-SA-4.0", dp.BOTH),
                  ("ODbL-1.0", dp.BOTH), ("CC-BY-NC-4.0", dp.FALSE), ("CC-BY-ND-4.0", dp.FALSE),
                  ("https://creativecommons.org/licenses/by/4.0/", dp.TRUE),
                  ("https://creativecommons.org/licenses/by-sa/3.0/", dp.BOTH),
                  ("https://creativecommons.org/licenses/by-nc-sa/3.0/", dp.FALSE),
                  ("https://wordnet.princeton.edu/license-and-commercial-use", dp.TRUE),
                  ("CC-BY-NC-SA-2.5-XX", dp.FALSE), ("For research only", dp.NEITHER),
                  ("Non-commercial use only", dp.FALSE), ("", dp.NEITHER), ("CC-BY-2.0", dp.NEITHER)):
    CHECK(dp.classify(lic) == want, f"{lic!r} is {dp.NAME[want]}")

# the real table holds
CHECK(dp.check(dp.read_sources()) == [], "SOURCES.tsv is sound: " + "; ".join(dp.check(dp.read_sources())))
apps = {r["app"] for r in dp.read_sources()}
CHECK(apps == {"weatherglass", "wordhoard", "fieldguide"}, f"the three apps have sources: {apps}")

# each rule refuses
GOOD = ["kit", "62", "core", "words", "file", "CC-BY-4.0", "https://example.org/w.txt", "Words, CC BY"]
for cell, value, says in ((5, "CC-BY-SA-4.0", "core pack takes only TRUE"),
                          (5, "CC-BY-NC-4.0", "never used"), (5, "Ask us", "refused"),
                          (6, "http://example.org/w.txt", "not TLS"), (2, "spare", "core or optional"),
                          (4, "torrent", "kind"), (1, "408", "slot"), (7, " ", "no attribution"),
                          (3, "Words", "fresh short name")):
    row = list(GOOD)
    row[cell] = value
    errs = dp.check(table([row]))
    CHECK(any(says in e for e in errs), f"column {cell} = {value!r} is refused ({says}): {errs}")
row = list(GOOD)
row[2] = "optional"
CHECK(any("optional pack takes only BOTH" in e for e in dp.check(table([row]))), "a TRUE source in optional")
row = list(GOOD)
row[4] = "lmf"
CHECK(any("per lexicon" in e for e in dp.check(table([row]))), "an lmf source with one licence")
row = list(GOOD)
row[4] = "sparql"
CHECK(any(".rq" in e for e in dp.check(table([row]))), "a query with no .rq file")
CHECK(any("columns" in e for e in dp.check(table([GOOD[:7]]))), "a short row")

# no fetch without the owner's go, none outside build/
rows = table([GOOD])
CHECK(refused(dp.fetch, "kit", rows, os.path.join(T, "raw"), False, has="--approved"), "fetch needs --approved")
CHECK(refused(dp.fetch, "kit", rows, os.path.join(dp.ROOT, "kernel"), True, has="outside build/"),
      "fetch only into build/")

# a pack from fixtures
rows = table([
    GOOD,
    ["kit", "62", "core", "nets", "lmf", "per-lexicon", "https://example.org/nets.tar.xz", "Nets"],
    ["kit", "62", "core", "now", "live", "CC-BY-4.0", "https://example.org/now?lat={lat}", "Now, CC BY"],
    ["kit", "62", "optional", "wiki", "file", "CC-BY-SA-4.0", "https://example.org/wiki.zim", "Wiki, CC BY-SA"],
])
raw = os.path.join(T, "raw")
os.makedirs(os.path.join(raw, "words"), exist_ok=True)
os.makedirs(os.path.join(raw, "nets"), exist_ok=True)
os.makedirs(os.path.join(raw, "wiki"), exist_ok=True)
open(os.path.join(raw, "words", "w.txt"), "wb").write(b"alpha\nbeta\n")
open(os.path.join(raw, "wiki", "wiki.zim"), "wb").write(b"ZIM-fixture")
buf = io.BytesIO()
with tarfile.open(fileobj=buf, mode="w:xz") as t:
    for name, data in (("nets/by.xml", lexicon("by", "https://creativecommons.org/licenses/by/4.0/")),
                       ("nets/sa.xml.gz", gzip.compress(lexicon("sa", "https://creativecommons.org/licenses/by-sa/4.0/"))),
                       ("nets/nc.xml", lexicon("nc", "https://creativecommons.org/licenses/by-nc/3.0/")),
                       ("nets/none.xml", lexicon("none", "")),
                       ("nets/README", b"not a lexicon")):
        info = tarfile.TarInfo(name)
        info.size = len(data)
        t.addfile(info, io.BytesIO(data))
open(os.path.join(raw, "nets", "nets.tar.xz"), "wb").write(buf.getvalue())
out = os.path.join(T, "out")
core, d1 = dp.build("kit", "core", rows, raw, out)
_, d2 = dp.build("kit", "core", rows, raw, out)
CHECK(d1 == d2 and d1 == hashlib.sha256(open(core, "rb").read()).hexdigest(), "the core pack is the same twice")
os.utime(os.path.join(raw, "words", "w.txt"), (1, 1))
_, d3 = dp.build("kit", "core", rows, raw, out)
CHECK(d3 == d1, "a file's time does not change the pack")
m = members(core)
CHECK(m.get("data/words/w.txt") == b"alpha\nbeta\n", "the core pack holds the CC BY file")
CHECK("data/nets/nets/by.xml" in m and not any("sa.xml" in k or "nc.xml" in k or "none.xml" in k for k in m),
      f"the core pack holds only the CC BY lexicon: {sorted(m)}")
CHECK(not any("wiki" in k for k in m), "no share-alike file in the core pack")
ex = m.get("EXCLUDED.tsv", b"").decode()
CHECK("nets/nets/nc.xml" in ex and "FALSE" in ex and "nets/nets/none.xml" in ex and "NEITHER" in ex,
      f"the NC and unlicensed lexicons are named as excluded: {ex!r}")
CHECK(b"https://example.org/now?lat={lat}" in m.get("ENDPOINTS.tsv", b""), "the live endpoint's terms ride along")
CHECK(b"Now, CC BY" in m["ATTRIBUTION.txt"] and b"Words, CC BY" in m["ATTRIBUTION.txt"], "attribution")
CHECK(b"words\tCC-BY-4.0" in m["LICENSES.tsv"] and b"nets/nets/by.xml\tCC-BY-4.0" in m["LICENSES.tsv"], "licences")
CHECK(b"state\tTRUE" in m["PACK.tsv"], "the core pack says TRUE")
man = m["MANIFEST.tsv"].decode().splitlines()
CHECK([x.split("\t")[2] for x in man] == sorted(x.split("\t")[2] for x in man), "the manifest is sorted by name")
CHECK(f"{hashlib.sha256(b'alpha' + bytes([10]) + b'beta' + bytes([10])).hexdigest()}\t11\tdata/words/w.txt" in man,
      "the manifest gives each file's sha256 and size")
with tarfile.open(core) as t:
    CHECK(all(i.mtime == 0 and i.uid == 0 and i.mode == 0o644 and i.uname == "" for i in t.getmembers()),
          "times, owners and modes are fixed")
    CHECK([i.name for i in t.getmembers()] == sorted(i.name for i in t.getmembers()), "names sorted")
opt, _ = dp.build("kit", "optional", rows, raw, out)
m = members(opt)
CHECK("data/wiki/wiki.zim" in m and "data/nets/nets/sa.xml.gz" in m, f"the optional pack holds the BY-SA parts: {sorted(m)}")
CHECK("data/words/w.txt" not in m and "data/nets/nets/by.xml" not in m, "and nothing TRUE")
CHECK(b"state\tBOTH" in m["PACK.tsv"], "the optional pack says BOTH")

# refusals at build time
CHECK(refused(dp.build, "kit", "core", rows, raw, os.path.join(dp.ROOT, "kernel"), has="outside build/"),
      "a pack only under build/")
with open(os.path.join(raw, "FETCHED.tsv"), "w") as f:
    f.write("words\tw.txt\t" + "0" * 64 + "\n")
CHECK(refused(dp.build, "kit", "core", rows, raw, out, has="changed since"), "a file changed since its fetch")
os.remove(os.path.join(raw, "FETCHED.tsv"))
shutil.rmtree(os.path.join(raw, "words"))
CHECK(refused(dp.build, "kit", "core", rows, raw, out, has="was not fetched"), "a source never fetched")
bad = table([GOOD[:5] + ["CC-BY-SA-4.0"] + GOOD[6:]])
CHECK(refused(dp.build, "kit", "core", bad, raw, out, has="not sound"), "an unsound table builds nothing")

shutil.rmtree(T, ignore_errors=True)
if fails:
    print(f"[FAIL] test_datapack: {fails} of {checks} checks failed")
    sys.exit(1)
print(f"[PASS] test_datapack: {checks} checks")
