#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Compile templates/<id>/* into zp_templates.c, so the zxv command, zxvd
and the Playground tab all carry the same templates.

Usage: gen_templates.py TEMPLATES_DIR OUT.c
"""
import os
import re
import sys

MAX_FILE = 256 * 1024
NAME = re.compile(r'^[A-Za-z0-9][A-Za-z0-9._-]{0,62}$')
TID = re.compile(r'^[a-z0-9][a-z0-9-]{0,62}$')


def c_bytes(data):
    out = []
    for i in range(0, len(data), 48):
        chunk = data[i:i + 48]
        out.append('    "' + ''.join('\\x%02x' % b for b in chunk) + '"')
    return '\n'.join(out) if out else '    ""'


def main():
    src, dst = sys.argv[1], sys.argv[2]
    ids = sorted(d for d in os.listdir(src) if os.path.isdir(os.path.join(src, d)))
    lines = ['/* Generated from sdk/playground/templates by gen_templates.py. Do not edit. */',
             '#include "zp_internal.h"', '']
    tables = []
    for n, tid in enumerate(ids):
        if not TID.match(tid):
            sys.exit('bad template id: %r' % tid)
        d = os.path.join(src, tid)
        names = sorted(f for f in os.listdir(d) if not f.startswith('.'))
        if 'template.zxv' not in names:
            sys.exit('%s: no template.zxv' % tid)
        manifest = open(os.path.join(d, 'template.zxv'), encoding='utf-8').read()
        m = re.search(r'^id\s+(\S+)\s*$', manifest, re.M)
        if not m or m.group(1) != tid:
            sys.exit('%s: template.zxv id must match the folder name' % tid)
        if len(names) > 16:
            sys.exit('%s: more than 16 files' % tid)
        for k, f in enumerate(names):
            if not NAME.match(f):
                sys.exit('%s: bad file name %r' % (tid, f))
            data = open(os.path.join(d, f), 'rb').read()
            if len(data) > MAX_FILE:
                sys.exit('%s/%s: larger than 256 KiB' % (tid, f))
            data.decode('utf-8')
            lines.append('static const char t%d_f%d[] =\n%s;' % (n, k, c_bytes(data)))
        lines.append('static const zp_file t%d_files[] = {' % n)
        for k, f in enumerate(names):
            lines.append('    {"%s", t%d_f%d, sizeof t%d_f%d - 1u},' % (f, n, k, n, k))
        lines.append('};')
        tables.append('    {"%s", t%d_files, %du},' % (tid, n, len(names)))
    lines.append('const zp_template zp_builtin_templates[] = {')
    lines.extend(tables if tables else ['    {"", 0, 0u},'])
    lines.append('};')
    lines.append('const uint32_t zp_builtin_template_count = %du;' % len(ids))
    with open(dst, 'w') as f:
        f.write('\n'.join(lines) + '\n')


if __name__ == '__main__':
    main()
