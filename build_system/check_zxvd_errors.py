#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""check_zxvd_errors.py: zxvd's JSON-RPC error codes stay one registry.

Every ZXVD_E_* code defined under kernel/arch/hosted must be:
  - in -32099..-32000 (zxvd_rpc.h X4; -32000 is the libzxv status code),
  - used by one name only (two features on one number is a collision),
  - listed in zxvd_rpc.h's X4 registry,
  - listed in docs/ZXVD.md's error list and in api/zxvd.openrpc.json's
    info.description, which clients read.
Two features shared -32012 twice before this check existed (play, then the
module methods), and power used -32100, outside the range.
"""
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HOSTED = os.path.join(ROOT, 'kernel', 'arch', 'hosted')
# JSON-RPC 2.0's own codes: parse error, invalid request, unknown method,
# bad params, internal error. Not zxvd's to allocate.
STANDARD = {-32700, -32600, -32601, -32602, -32603}
DEF = re.compile(r'#\s*define\s+(ZXVD_E_[A-Z0-9_]+)\s+\(?\s*(-\d+)\s*\)?')


def main():
    codes = {}
    for f in sorted(os.listdir(HOSTED)):
        if not f.endswith(('.c', '.h')) or f.startswith('test_'):
            continue
        for m in DEF.finditer(open(os.path.join(HOSTED, f), encoding='utf-8').read()):
            if int(m.group(2)) not in STANDARD:
                codes.setdefault(int(m.group(2)), set()).add((m.group(1), f))
    bad = []
    names = {}
    for code, defs in sorted(codes.items()):
        for name, f in defs:
            names.setdefault(name, set()).add(code)
        if not -32099 <= code <= -32000:
            bad.append('%d (%s) is outside -32099..-32000' % (code, ', '.join(sorted(n for n, _ in defs))))
        if len({n for n, _ in defs}) > 1:
            bad.append('%d is used by %s' % (code, ', '.join(sorted({n for n, _ in defs}))))
    for name, cs in names.items():
        if len(cs) > 1:
            bad.append('%s has two values: %s' % (name, sorted(cs)))
    h = open(os.path.join(HOSTED, 'zxvd_rpc.h'), encoding='utf-8').read()
    x4 = h[h.index(' *   X4 '):h.index(' *   X5 ')]
    doc = open(os.path.join(ROOT, 'docs', 'ZXVD.md'), encoding='utf-8').read()
    spec = json.load(open(os.path.join(ROOT, 'api', 'zxvd.openrpc.json'), encoding='utf-8'))
    desc = spec['info']['description']
    for code in sorted(codes):
        s = str(code)
        if s not in x4:
            bad.append('%s is not in zxvd_rpc.h X4' % s)
        if s not in doc:
            bad.append('%s is not in docs/ZXVD.md' % s)
        if s not in desc:
            bad.append('%s is not in api/zxvd.openrpc.json info.description' % s)
    if bad:
        print('\n'.join(bad))
        sys.exit('check_zxvd_errors: fix the codes above (zxvd_rpc.h X4 is the registry)')
    print('check_zxvd_errors: %d zxvd error codes, each in range, unique and documented' % len(codes))


if __name__ == '__main__':
    main()
