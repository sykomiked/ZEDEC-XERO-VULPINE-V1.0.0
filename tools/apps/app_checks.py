#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""The reviewed check lines (zxapp.h K1): kernel/src/apps/CHECKS.tsv.

A manifest's `check` lines are its proof, but a builder writes the manifest, so it
could write its own exam. CHECKS.tsv holds the check lines a reviewer accepted, app by
app, verbatim. It is the trusted copy: the fabricator accepts a manifest's checks only
when they equal it, the wave loop's builder may not change it (wave_loop.py restores it
after every attempt), and --check fails when a frozen app's manifest says anything else.
An app not yet in the file is unreviewed: its checks run, but nothing trusts them yet.

  app_checks.py --freeze ID ...   copy these apps' check lines in (a reviewer's act)
  app_checks.py --check           every frozen app's manifest has exactly its lines
  app_checks.py --list            the apps with unreviewed checks"""
import argparse
import os
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
APPS = os.path.join(ROOT, "kernel", "src", "apps")
TABLE = os.path.join(APPS, "CHECKS.tsv")
HEAD = ("# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC\n"
        "# SPDX-License-Identifier: Apache-2.0\n"
        "#\n"
        "# The reviewed check lines of each app (tools/apps/app_checks.py): the trusted\n"
        "# copy of its proof. Written only by app_checks.py --freeze, after review.\n"
        "# app\tcheck line, verbatim\n")


def manifest_checks(app_id):
    path = os.path.join(APPS, f"{app_id}.zxapp")
    if not os.path.exists(path):
        return None
    out = []
    for line in open(path, encoding="utf-8"):
        s = " ".join(line.split("#", 1)[0].split())
        if s.startswith("check "):
            out.append(s)
    return out


def read_table():
    frozen = {}
    if os.path.exists(TABLE):
        for line in open(TABLE, encoding="utf-8"):
            if line.startswith("#") or not line.strip():
                continue
            app_id, text = line.rstrip("\n").split("\t", 1)
            frozen.setdefault(app_id, []).append(text)
    return frozen


def write_table(frozen):
    with open(TABLE, "w", encoding="utf-8") as f:
        f.write(HEAD)
        for app_id in sorted(frozen):
            for text in frozen[app_id]:
                f.write(f"{app_id}\t{text}\n")


def errors(frozen):
    out = []
    for app_id in sorted(frozen):
        have = manifest_checks(app_id)
        rel = f"kernel/src/apps/{app_id}.zxapp"
        if have is None:
            out.append(f"{rel}: in CHECKS.tsv but not there")
        elif have != frozen[app_id]:
            gone = [x for x in frozen[app_id] if x not in have]
            new = [x for x in have if x not in frozen[app_id]]
            what = (f"{len(gone)} reviewed check line(s) changed or gone" if gone else "") + \
                   (", " if gone and new else "") + (f"{len(new)} not reviewed" if new else "") or \
                   "the reviewed lines in another order"
            out.append(f"{rel}: its check lines are not the reviewed ones in CHECKS.tsv ({what}); "
                       "a reviewer re-freezes them (app_checks.py --freeze)")
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--freeze", nargs="+", metavar="ID")
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--list", action="store_true")
    a = ap.parse_args()
    frozen = read_table()
    if a.freeze:
        for app_id in a.freeze:
            have = manifest_checks(app_id)
            if not have:
                print(f"app_checks: {app_id} has no check lines to freeze", file=sys.stderr)
                return 1
            frozen[app_id] = have
        write_table(frozen)
    if a.list:
        for f in sorted(os.listdir(APPS)):
            if f.endswith(".zxapp") and f[:-6] not in frozen and manifest_checks(f[:-6]):
                print(f[:-6])
    if a.check or not (a.freeze or a.list):
        bad = errors(frozen)
        for e in bad:
            print(f"app_checks: {e}", file=sys.stderr)
        if bad:
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
