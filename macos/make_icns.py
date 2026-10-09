#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""make_icns.py LOGO.png OUT.icns — the app icon: the logo on a white rounded
square, written as an .icns of PNG entries (128, 256, 512 and 1024 px).
Needs Pillow; works on any OS (no iconutil)."""
import io
import struct
import sys

from PIL import Image, ImageDraw

src, out = sys.argv[1], sys.argv[2]
logo = Image.open(src).convert('RGBA')
entries = []
for kind, px in ((b'ic07', 128), (b'ic08', 256), (b'ic09', 512), (b'ic10', 1024)):
    img = Image.new('RGBA', (px, px), (0, 0, 0, 0))
    pad = px // 10                                   # macOS icon grid margin
    ImageDraw.Draw(img).rounded_rectangle((pad, pad, px - pad, px - pad), radius=px // 5,
                                          fill=(255, 255, 255, 255))
    inner = px - 4 * pad
    img.alpha_composite(logo.resize((inner, inner), Image.LANCZOS), (2 * pad, 2 * pad))
    buf = io.BytesIO()
    img.save(buf, 'PNG')
    data = buf.getvalue()
    entries.append(kind + struct.pack('>I', 8 + len(data)) + data)
body = b''.join(entries)
with open(out, 'wb') as f:
    f.write(b'icns' + struct.pack('>I', 8 + len(body)) + body)
