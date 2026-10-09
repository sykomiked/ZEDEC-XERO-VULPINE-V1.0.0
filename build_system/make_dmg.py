#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3
"""make_dmg.py FOLDER VOLUME OUT.dmg — a disk image that macOS opens with a
double-click, built without a Mac. It is ISO 9660 with Rock Ridge (which
keeps the app's execute permission) and Joliet. On a Mac, build_desktop.sh
uses hdiutil instead and makes a native compressed image."""
import io
import os
import sys

import pycdlib

src, volume, out = sys.argv[1], sys.argv[2], sys.argv[3]
iso = pycdlib.PyCdlib()
iso.new(interchange_level=4, rock_ridge='1.09', joliet=3, vol_ident=volume[:32].upper().replace(' ', '_'))
# pycdlib needs ISO 9660 names for every entry; we give each a short unique
# one and the real name through Rock Ridge and Joliet.
dirs = {'': '/'}
n = 0
for root, dnames, fnames in os.walk(src):
    dnames.sort()
    fnames.sort()
    rel_root = os.path.relpath(root, src)
    rel_root = '' if rel_root == '.' else rel_root
    for d in dnames:
        n += 1
        rel = os.path.join(rel_root, d) if rel_root else d
        isop = dirs[rel_root].rstrip('/') + '/D%06d' % n
        iso.add_directory(isop, rr_name=d, joliet_path='/' + rel)
        dirs[rel] = isop
    for f in fnames:
        n += 1
        rel = os.path.join(rel_root, f) if rel_root else f
        p = os.path.join(src, rel)
        isop = dirs[rel_root].rstrip('/') + '/F%06d.;1' % n
        if os.path.islink(p):
            continue
        data = open(p, 'rb').read()
        mode = os.stat(p).st_mode & 0o777
        iso.add_fp(io.BytesIO(data), len(data), isop, rr_name=f, joliet_path='/' + rel,
                   file_mode=0o100000 | mode)
iso.write(out)
iso.close()
