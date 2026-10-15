#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Data packs for the apps that need outside data (docs/DATAPACKS.md).

SOURCES.tsv lists every source. Each licence is classed in the kernel's
four values (kernel/src/lpres: NEITHER 0, TRUE 1, FALSE 2, BOTH 3):

  TRUE     commercial use with attribution (CC0, CC BY, NLOD, public domain,
           the WordNet licence, MIT, BSD, Apache): the core pack.
  BOTH     commercial use, share-alike (CC BY-SA, ODbL, GFDL): a separate
           optional pack, never mixed with the Apache-2.0 code.
  FALSE    non-commercial or no derivatives: never used.
  NEITHER  a licence this table does not know: refused, never guessed.

  datapack.py check                     SOURCES.tsv is sound (test_zxapp.sh runs it)
  datapack.py classify <licence>        a licence's value
  datapack.py fetch <app> --approved    download its sources into build/datapacks/raw/<app>
  datapack.py build <app> core|optional the pack, from what was fetched, into build/datapacks

Nothing is fetched without --approved (the owner's go), nothing is written outside
build/, and a pack is a release asset, never committed. The same fetched files give
the same pack, byte for byte: names sorted, times, owners and modes fixed."""
import argparse
import gzip
import hashlib
import io
import os
import re
import sys
import tarfile
import urllib.parse
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
SOURCES = os.path.join(HERE, "SOURCES.tsv")
BUILD = os.path.join(ROOT, "build")
AGENT = "ZXV-datapack/1.0 (+https://github.com/sykomiked/ZEDEC-XERO-VULPINE-V1.0.0)"
COLUMNS = ("app", "slot", "pack", "id", "kind", "licence", "url", "attribution")
KINDS = ("file", "sparql", "lmf", "live")

NEITHER, TRUE, FALSE, BOTH = 0, 1, 2, 3
NAME = {NEITHER: "NEITHER", TRUE: "TRUE", FALSE: "FALSE", BOTH: "BOTH"}
PACK_STATE = {"core": TRUE, "optional": BOTH}

# SPDX ids and the licence URLs wordnets carry, each to its value
LICENCES = {
    "CC0-1.0": TRUE, "CC-BY-3.0": TRUE, "CC-BY-4.0": TRUE, "NLOD-2.0": TRUE,
    "PDDL-1.0": TRUE, "ODC-By-1.0": TRUE, "LicenseRef-US-Government-PD": TRUE,
    "LicenseRef-WordNet-3.0": TRUE, "MIT": TRUE, "BSD-2-Clause": TRUE, "BSD-3-Clause": TRUE,
    "Apache-2.0": TRUE,
    "CC-BY-SA-3.0": BOTH, "CC-BY-SA-4.0": BOTH, "ODbL-1.0": BOTH, "GFDL-1.3-or-later": BOTH,
    "CC-BY-NC-4.0": FALSE, "CC-BY-NC-3.0": FALSE, "CC-BY-NC-SA-3.0": FALSE,
    "CC-BY-NC-SA-4.0": FALSE, "CC-BY-ND-4.0": FALSE, "CC-BY-NC-ND-4.0": FALSE,
}
URLS = (
    (r"creativecommons\.org/publicdomain/zero/1\.0", "CC0-1.0"),
    (r"creativecommons\.org/licenses/by/3\.0", "CC-BY-3.0"),
    (r"creativecommons\.org/licenses/by/4\.0", "CC-BY-4.0"),
    (r"creativecommons\.org/licenses/by-sa/3\.0", "CC-BY-SA-3.0"),
    (r"creativecommons\.org/licenses/by-sa/4\.0", "CC-BY-SA-4.0"),
    (r"creativecommons\.org/licenses/by-nc", "CC-BY-NC-4.0"),
    (r"creativecommons\.org/licenses/by-nd", "CC-BY-ND-4.0"),
    (r"wordnet\.princeton\.edu/(wordnet/)?license", "LicenseRef-WordNet-3.0"),
    (r"opensource\.org/licenses/MIT", "MIT"),
    (r"opensource\.org/licenses/Apache-2\.0|apache\.org/licenses/LICENSE-2\.0", "Apache-2.0"),
    (r"opendatacommons\.org/licenses/odbl", "ODbL-1.0"),
)


def spdx(licence):
    """A licence (SPDX id or URL) as its SPDX id, or "" when unknown."""
    if licence in LICENCES:
        return licence
    for pat, sid in URLS:
        if re.search(pat, licence, re.I):
            return sid
    return ""


def classify(licence):
    """The four-valued class of a licence: TRUE, BOTH, FALSE, or NEITHER (unknown)."""
    sid = spdx(licence)
    if sid:
        return LICENCES[sid]
    if re.search(r"(^|[-/])n[cd]([-/]|$)|non-?commercial", licence, re.I):
        return FALSE
    return NEITHER


def read_sources(path=SOURCES):
    rows = []
    with open(path, encoding="utf-8") as f:
        for n, line in enumerate(f, 1):
            if not line.strip() or line.startswith("#"):
                continue
            cells = line.rstrip("\n").split("\t")
            row = dict(zip(COLUMNS, cells))
            row["line"], row["ncells"] = n, len(cells)
            rows.append(row)
    return rows


def check(rows):
    """Every error in the source table."""
    errors, seen = [], set()
    for r in rows:
        at = f"SOURCES.tsv:{r['line']}"
        if r["ncells"] != len(COLUMNS):
            errors.append(f"{at}: {r['ncells']} columns, not {len(COLUMNS)}")
            continue
        if r["id"] in seen or not re.fullmatch(r"[a-z][a-z0-9_]*", r["id"]):
            errors.append(f"{at}: id {r['id']!r} is not a fresh short name")
        seen.add(r["id"])
        if not r["slot"].isdigit() or not 1 <= int(r["slot"]) <= 407:
            errors.append(f"{at}: slot {r['slot']} is not 1 to 407")
        if r["pack"] not in PACK_STATE:
            errors.append(f"{at}: pack {r['pack']} is not core or optional")
        if r["kind"] not in KINDS:
            errors.append(f"{at}: kind {r['kind']} is not one of {', '.join(KINDS)}")
        if not r["url"].startswith("https://"):
            errors.append(f"{at}: {r['url']} is not TLS (https://)")
        if not r["attribution"].strip():
            errors.append(f"{at}: no attribution")
        if r["kind"] == "lmf":
            if r["licence"] != "per-lexicon":
                errors.append(f"{at}: an lmf source is classed per lexicon (licence per-lexicon)")
            continue
        state = classify(r["licence"])
        want = PACK_STATE.get(r["pack"])
        if state == NEITHER:
            errors.append(f"{at}: licence {r['licence']} is unknown (NEITHER): refused")
        elif state == FALSE:
            errors.append(f"{at}: licence {r['licence']} is non-commercial or no-derivatives "
                          "(FALSE): never used")
        elif want is not None and state != want:
            errors.append(f"{at}: licence {r['licence']} is {NAME[state]}, but the {r['pack']} "
                          f"pack takes only {NAME[want]}")
        if r["kind"] == "sparql" and not os.path.isfile(os.path.join(HERE, f"{r['id']}.rq")):
            errors.append(f"{at}: no query {r['id']}.rq beside SOURCES.tsv")
    return errors


def inside_build(path):
    full = os.path.realpath(path)
    return full == os.path.realpath(BUILD) or full.startswith(os.path.realpath(BUILD) + os.sep)


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def download(url, dest, data=None, accept=None):
    req = urllib.request.Request(url, data=data, headers={"User-Agent": AGENT})
    if accept:
        req.add_header("Accept", accept)
    tmp = dest + ".part"
    with urllib.request.urlopen(req, timeout=300) as resp, open(tmp, "wb") as out:
        if not resp.geturl().startswith("https://"):
            raise OSError(f"{url} left TLS: {resp.geturl()}")
        for chunk in iter(lambda: resp.read(1 << 20), b""):
            out.write(chunk)
    os.replace(tmp, dest)


def fetch(app, rows, to, approved):
    """Download an app's sources (not the live ones) into to/<id>/; FETCHED.tsv names each
    file with its sha256. Refused without the owner's approval."""
    if not approved:
        raise SystemExit("datapack: fetch needs --approved (the owner's go): nothing fetched")
    if not inside_build(to):
        raise SystemExit(f"datapack: {to} is outside build/: refused")
    mine = [r for r in rows if r["app"] == app and r["kind"] != "live"]
    if not mine:
        raise SystemExit(f"datapack: {app} has no source to fetch")
    log = []
    for r in mine:
        d = os.path.join(to, r["id"])
        os.makedirs(d, exist_ok=True)
        if r["kind"] == "sparql":
            q = open(os.path.join(HERE, f"{r['id']}.rq"), encoding="utf-8").read()
            for digit in "123456789":
                body = urllib.parse.urlencode({"query": q.replace("{digit}", digit)}).encode()
                dest = os.path.join(d, f"part{digit}.tsv")
                download(r["url"], dest, data=body, accept="text/tab-separated-values")
                log.append((r["id"], f"part{digit}.tsv", sha256_file(dest)))
        else:
            name = os.path.basename(urllib.parse.urlparse(r["url"]).path)
            dest = os.path.join(d, name)
            download(r["url"], dest)
            log.append((r["id"], name, sha256_file(dest)))
        print(f"fetched {r['id']}")
    with open(os.path.join(to, "FETCHED.tsv"), "w", encoding="utf-8") as f:
        for row in log:
            f.write("\t".join(row) + "\n")


def lmf_members(path):
    """An LMF wordnet archive's lexicon files: [(member name, licence, bytes)]."""
    out = []
    with tarfile.open(path, "r:*") as t:
        for m in sorted(t.getmembers(), key=lambda m: m.name):
            if not m.isfile() or not re.search(r"\.xml(\.gz)?$", m.name):
                continue
            data = t.extractfile(m).read()
            text = gzip.decompress(data) if m.name.endswith(".gz") else data
            lic = re.search(rb'<Lexicon\b[^>]*?\blicense="([^"]*)"', text, re.S)
            out.append((m.name, lic.group(1).decode() if lic else "", data))
    return out


class Pack:
    """A tar written the same way every time: sorted names, fixed times, owners, modes."""

    def __init__(self):
        self.files = {}

    def add_bytes(self, name, data):
        self.files[name] = ("bytes", data)

    def add_path(self, name, path):
        self.files[name] = ("path", path)

    def size(self, name):
        kind, v = self.files[name]
        return len(v) if kind == "bytes" else os.path.getsize(v)

    def write(self, dest):
        manifest = []
        for name in sorted(self.files):
            kind, v = self.files[name]
            digest = hashlib.sha256(v).hexdigest() if kind == "bytes" else sha256_file(v)
            manifest.append(f"{digest}\t{self.size(name)}\t{name}\n")
        self.add_bytes("MANIFEST.tsv", "".join(manifest).encode())
        tmp = dest + ".part"
        with tarfile.open(tmp, "w", format=tarfile.PAX_FORMAT) as t:
            for name in sorted(self.files):
                kind, v = self.files[name]
                info = tarfile.TarInfo(name)
                info.size, info.mtime, info.mode = self.size(name), 0, 0o644
                info.uid = info.gid = 0
                info.uname = info.gname = ""
                if kind == "bytes":
                    t.addfile(info, io.BytesIO(v))
                else:
                    with open(v, "rb") as f:
                        t.addfile(info, f)
        os.replace(tmp, dest)
        return sha256_file(dest)


def build(app, pack, rows, raw, out):
    """The app's pack from what fetch() wrote into raw; returns (path, sha256)."""
    if pack not in PACK_STATE:
        raise SystemExit("datapack: the pack is core or optional")
    if not inside_build(out):
        raise SystemExit(f"datapack: {out} is outside build/: refused")
    errors = check(rows)
    if errors:
        raise SystemExit("datapack: SOURCES.tsv is not sound:\n  " + "\n  ".join(errors))
    want = PACK_STATE[pack]
    mine = [r for r in rows if r["app"] == app and (r["pack"] == pack or r["kind"] == "lmf")]
    if not mine:
        raise SystemExit(f"datapack: {app} has no {pack} source")
    p, licences, attribution, excluded, endpoints = Pack(), [], [], [], []
    slot = mine[0]["slot"]
    for r in mine:
        if r["kind"] == "live":
            endpoints.append(f"{r['id']}\t{r['url']}\t{r['licence']}\t{AGENT}\n")
            attribution.append(r["attribution"])
            licences.append((r["id"], r["licence"]))
            continue
        d = os.path.join(raw, r["id"])
        fetched = {}
        log = os.path.join(raw, "FETCHED.tsv")
        if os.path.exists(log):
            for line in open(log, encoding="utf-8"):
                sid, name, digest = line.rstrip("\n").split("\t")
                fetched[(sid, name)] = digest
        names = sorted(f for f in os.listdir(d)) if os.path.isdir(d) else []
        names = [f for f in names if not f.endswith(".part")]
        if not names:
            raise SystemExit(f"datapack: {r['id']} was not fetched (datapack.py fetch {app} --approved)")
        for name in names:
            path = os.path.join(d, name)
            if fetched.get((r["id"], name)) not in (None, sha256_file(path)):
                raise SystemExit(f"datapack: {r['id']}/{name} changed since it was fetched")
            if r["kind"] != "lmf":
                p.add_path(f"data/{r['id']}/{name}", path)
                continue
            took = 0
            for member, lic, data in lmf_members(path):
                state = classify(lic)
                if state == want:
                    p.add_bytes(f"data/{r['id']}/{member}", data)
                    licences.append((f"{r['id']}/{member}", spdx(lic) or lic))
                    took += 1
                elif state in (FALSE, NEITHER):
                    excluded.append(f"{r['id']}/{member}\t{lic or '(none)'}\t{NAME[state]}\n")
            if took:
                attribution.append(r["attribution"])
        if r["kind"] != "lmf":
            attribution.append(r["attribution"])
            licences.append((r["id"], r["licence"]))
    if not licences:
        raise SystemExit(f"datapack: nothing in {app}'s {pack} pack holds {NAME[want]}")
    p.add_bytes("PACK.tsv", f"app\t{app}\nslot\t{slot}\npack\t{pack}\nstate\t{NAME[want]}\n".encode())
    p.add_bytes("ATTRIBUTION.txt", ("".join(f"{a}\n" for a in sorted(set(attribution)))).encode())
    p.add_bytes("LICENSES.tsv", "".join(f"{w}\t{lic}\n" for w, lic in sorted(licences)).encode())
    if excluded:
        p.add_bytes("EXCLUDED.tsv", "".join(sorted(excluded)).encode())
    if endpoints:
        p.add_bytes("ENDPOINTS.tsv", "".join(sorted(endpoints)).encode())
    os.makedirs(out, exist_ok=True)
    dest = os.path.join(out, f"{app}-{pack}.tar")
    return dest, p.write(dest)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("check")
    c = sub.add_parser("classify")
    c.add_argument("licence")
    f = sub.add_parser("fetch")
    f.add_argument("app")
    f.add_argument("--approved", action="store_true", help="the owner said go")
    f.add_argument("--to", help="default build/datapacks/raw/<app>")
    b = sub.add_parser("build")
    b.add_argument("app")
    b.add_argument("pack", choices=sorted(PACK_STATE))
    b.add_argument("--raw", help="default build/datapacks/raw/<app>")
    b.add_argument("--out", default=os.path.join(BUILD, "datapacks"))
    a = ap.parse_args()
    if a.cmd == "classify":
        print(NAME[classify(a.licence)])
        return 0
    rows = read_sources()
    if a.cmd == "check":
        errors = check(rows)
        for e in errors:
            print(f"datapack: {e}", file=sys.stderr)
        return 1 if errors else 0
    raw = os.path.join(BUILD, "datapacks", "raw", a.app)
    if a.cmd == "fetch":
        fetch(a.app, rows, a.to or raw, a.approved)
        return 0
    dest, digest = build(a.app, a.pack, rows, a.raw or raw, a.out)
    print(f"{digest}  {os.path.relpath(dest, ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
