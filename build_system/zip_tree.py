#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3
"""zip_tree.py OUT.zip PATH... — zip files and folders keeping Unix
permissions and symlinks, so a .app still runs after unzipping."""
import os
import stat
import sys
import zipfile


def add(z, path):
    st = os.lstat(path)
    info = zipfile.ZipInfo(path + ('/' if stat.S_ISDIR(st.st_mode) else ''))
    info.date_time = (2026, 1, 1, 0, 0, 0)        # reproducible
    info.external_attr = (st.st_mode & 0xFFFF) << 16
    info.create_system = 3                         # Unix
    if stat.S_ISLNK(st.st_mode):
        z.writestr(info, os.readlink(path))
    elif stat.S_ISDIR(st.st_mode):
        z.writestr(info, b'')
        for name in sorted(os.listdir(path)):
            add(z, os.path.join(path, name))
    else:
        info.compress_type = zipfile.ZIP_DEFLATED
        with open(path, 'rb') as f:
            z.writestr(info, f.read())


with zipfile.ZipFile(sys.argv[1], 'w') as z:
    for p in sys.argv[2:]:
        add(z, p)
