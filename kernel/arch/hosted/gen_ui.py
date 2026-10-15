#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Turn zxv_ui.html into zxv_ui.c (the window, compiled into the binary).

  gen_ui.py zxv_ui.html zxv_ui.c [--table-out T.json] [--samples-out S.json]
                                 [--html-out PAGE.html]

Every word the window shows is a message id of kernel/src/i18n (catalog/
en.msg). This script collects the ids the page uses (data-i18n="KEY",
data-i18n-title, data-i18n-placeholder, T('KEY', ...)), compiles
gen_ui_strings.c with the i18n sources using the host C compiler ($HOSTCC,
else cc, gcc or clang: the build host's, also when $CC cross-compiles), runs
it to get the string table for every locale, and embeds that table in the
page's <script id="zxv-i18n">. It also fills the English text into the
markup, so the page reads in English before its script runs. An id that is
not in the catalog stops the build. The --*-out files are for
check_ui_i18n.py.
"""
import html
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
I18N = os.path.join(HERE, '..', '..', 'src', 'i18n')
I18N_SRCS = ['i18n_text.c', 'i18n_locale.c', 'i18n_format.c', 'i18n_tables.c', 'i18n_catalog.c']
SLOT = '<script id="zxv-i18n" type="application/json">null</script>'

KEY = r'([A-Z][A-Z0-9_]*)'
USES = [re.compile(r'\bdata-i18n(?:-title|-placeholder)?="' + KEY + '"'),
        re.compile(r"\bT\('" + KEY + "'")]


def die(msg):
    sys.stderr.write('gen_ui.py: %s\n' % msg)
    sys.exit(1)


def keys_used(page):
    seen = []
    for rx in USES:
        for k in rx.findall(page):
            if k not in seen:
                seen.append(k)
    return seen


def host_cc():
    if os.environ.get('HOSTCC'):
        return os.environ['HOSTCC'].split()
    for c in ('cc', 'gcc', 'clang'):
        if shutil.which(c):
            return [c]
    die('no host C compiler (set HOSTCC) to build gen_ui_strings')


def string_table(keys, tmp):
    exe = os.path.join(tmp, 'gen_ui_strings' + ('.exe' if os.name == 'nt' else ''))
    cmd = host_cc() + ['-std=c11', '-O1', '-Wall', '-Wextra', '-Werror', '-I' + I18N,
                       os.path.join(HERE, 'gen_ui_strings.c')]
    cmd += [os.path.join(I18N, f) for f in I18N_SRCS] + ['-o', exe]
    if subprocess.call(cmd) != 0:
        die('building gen_ui_strings failed')
    table, samples = os.path.join(tmp, 'table.json'), os.path.join(tmp, 'samples.json')
    if subprocess.call([exe, table, samples] + keys) != 0:
        die('gen_ui_strings failed')
    with open(table, 'rb') as f:
        t = f.read()
    with open(samples, 'rb') as f:
        s = f.read()
    return t, s


def prerender(page, tb):
    en = dict(zip(tb['keys'], tb['en']))
    filled = [0]

    def text(m):
        filled[0] += 1
        return m.group(1) + html.escape(en[m.group(3)], quote=False) + m.group(4)

    out = re.sub(r'(<([a-z0-9]+)\b[^<>]*\bdata-i18n="' + KEY + r'"[^<>]*>)(</\2>)', text, page)
    if filled[0] != len(re.findall(r'\bdata-i18n="', page)):
        die('every data-i18n element must be empty in zxv_ui.html')
    for attr in ('title', 'placeholder'):
        out = re.sub(r'\bdata-i18n-%s="%s"' % (attr, KEY),
                     lambda m, a=attr: '%s %s="%s"' % (m.group(0), a,
                                                        html.escape(en[m.group(1)], quote=True)),
                     out)
    return out


def main():
    args = sys.argv[1:]
    outs = {}
    for opt in ('--table-out', '--samples-out', '--html-out'):
        if opt in args:
            i = args.index(opt)
            outs[opt] = args[i + 1]
            del args[i:i + 2]
    if len(args) != 2:
        die('usage: gen_ui.py zxv_ui.html zxv_ui.c [--table-out F] [--samples-out F] [--html-out F]')
    src, dst = args
    with open(src, encoding='utf-8') as f:
        page = f.read()
    if page.count(SLOT) != 1:
        die('zxv_ui.html must hold exactly one ' + SLOT)
    keys = keys_used(page)
    with tempfile.TemporaryDirectory() as tmp:
        table, samples = string_table(keys, tmp)
    tb = json.loads(table.decode('utf-8'))
    page = prerender(page, tb)
    # the table escapes < > & (gen_ui_strings.c), so it cannot end the element
    page = page.replace(SLOT, SLOT.replace('>null<', '>' + table.decode('utf-8').strip() + '<'))
    data = page.encode('utf-8')
    for opt, blob in (('--table-out', table), ('--samples-out', samples), ('--html-out', data)):
        if opt in outs:
            with open(outs[opt], 'wb') as f:
                f.write(blob)
    with open(dst, 'w') as f:
        f.write('/* Generated from zxv_ui.html and the i18n catalog by gen_ui.py. Do not edit. */\n')
        f.write('const char zxv_ui_html[] =\n')
        for i in range(0, len(data), 64):
            chunk = data[i:i + 64]
            f.write('    "' + ''.join('\\x%02x' % b for b in chunk) + '"\n')
        f.write(';\n')


main()
