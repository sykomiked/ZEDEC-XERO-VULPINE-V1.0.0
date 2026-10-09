#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Turn zxv_ui.html into zxv_ui.c (the window, compiled into the binary)."""
import sys
src, dst = sys.argv[1], sys.argv[2]
data = open(src, 'rb').read()
with open(dst, 'w') as f:
    f.write('/* Generated from zxv_ui.html by gen_ui.py. Do not edit. */\n')
    f.write('const char zxv_ui_html[] =\n')
    for i in range(0, len(data), 64):
        chunk = data[i:i + 64]
        f.write('    "' + ''.join('\\x%02x' % b for b in chunk) + '"\n')
    f.write(';\n')
