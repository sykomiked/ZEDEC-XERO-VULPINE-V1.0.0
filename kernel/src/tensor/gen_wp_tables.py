# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# Generate zt_wp_tables.h: the character classes and maps of the BERT
# WordPiece tokenizer (zt_bert.c), read from the Hugging Face `tokenizers`
# library itself, one code point at a time, so the tables are what that
# library does and not a re-reading of the Unicode standard (its Rust crates
# carry their own Unicode versions; Python's unicodedata is newer).
#
#   CTRL     removed by BertNormalizer's clean_text (NUL, U+FFFD, controls)
#   CLEANWS  turned into ' ' by clean_text
#   CJK      surrounded by spaces by handle_chinese_chars
#   NFD      canonical decompositions (Hangul syllables excepted: zt_bert.c
#            decomposes them arithmetically), and CCC, the nonzero canonical
#            combining classes, for strip_accents: NFD with canonical
#            reordering, then the nonspacing marks (MN) dropped
#   LOWER    code points lowercase turns into other code points
#   PUNCT    split off as their own word by BertPreTokenizer
#   PREWS    split on (and dropped) by BertPreTokenizer
# NFD and CCC come from Python's unicodedata; every table is then checked
# against the library: each code point alone, every pair of code points with
# a nonzero combining class, and each kept mark with every mark between two
# (where canonical reordering shows).
#
#   python3 -m pip install tokenizers==0.23.3; python3 gen_wp_tables.py zt_wp_tables.h
import sys
import unicodedata
import tokenizers
from tokenizers.normalizers import BertNormalizer
from tokenizers.pre_tokenizers import BertPreTokenizer

K = 3  # most code points lowercase gives one code point (checked below)
KD = 4  # most code points a canonical decomposition gives (checked below)


def norm(**kw):
    base = dict(clean_text=False, handle_chinese_chars=False, strip_accents=False, lowercase=False)
    base.update(kw)
    return BertNormalizer(**base)


clean, cjk, strip, lower = norm(clean_text=True), norm(handle_chinese_chars=True), \
    norm(strip_accents=True), norm(lowercase=True)
pre = BertPreTokenizer()
SCALARS = [cp for cp in range(0x110000) if not 0xD800 <= cp <= 0xDFFF]

sets = {k: [] for k in ('CTRL', 'CLEANWS', 'CJK', 'MN', 'PUNCT', 'PREWS')}
lower_map, nfd_map, ccc, OLDER_CCC = [], [], {}, []


def hangul(cp):
    s = cp - 0xAC00
    out = [0x1100 + s // 588, 0x1161 + (s % 588) // 28]
    if s % 28:
        out.append(0x11A7 + s % 28)
    return out


OLDER = set()


def decomp(cp):
    if 0xAC00 <= cp <= 0xD7A3:
        return hangul(cp)
    if cp in OLDER:
        return [cp]
    return [ord(x) for x in unicodedata.normalize('NFD', chr(cp))]


# the library's normalisation crate can carry an older Unicode than Python:
# a decomposition it does not apply (the code point comes out unchanged) is
# left out, so decomp() below is the library's
OLDER = set()
for cp in SCALARS:
    d = decomp(cp)
    if d != [cp] and not 0xAC00 <= cp <= 0xD7A3:
        if strip.normalize_str('a' + chr(cp) + 'a') == 'a' + chr(cp) + 'a':
            OLDER.add(cp)
        else:
            nfd_map.append((cp, d))
    if unicodedata.combining(chr(cp)):
        ccc[cp] = unicodedata.combining(chr(cp))

# MN: a code point with no decomposition that strip_accents removes
for cp in SCALARS:
    if decomp(cp) == [cp] and strip.normalize_str('a' + chr(cp) + 'a') == 'aa':
        sets['MN'].append(cp)
MN = set(sets['MN'])


def model_strip(cps):
    """what zt_bert.c does: decompose, reorder each run of nonzero classes
    stably by class (insertion as each code point arrives), drop MN"""
    out = []
    for cp in cps:
        for d in decomp(cp):
            c = ccc.get(d, 0)
            i = len(out)
            out.append(d)
            while c and i > 0 and ccc.get(out[i - 1], 0) > c:
                out[i - 1], out[i] = out[i], out[i - 1]
                i -= 1
    return [x for x in out if x not in MN]


def check_strip(cps):
    s = 'a' + ''.join(map(chr, cps)) + 'a'
    got = [ord(x) for x in strip.normalize_str(s)]
    want = [ord('a')] + model_strip(cps) + [ord('a')]
    if got != want:
        sys.exit('strip_accents model differs on %s: %s vs %s' %
                 (' '.join('U+%04X' % c for c in cps), got, want))


for cp in SCALARS:
    c = chr(cp)
    s = 'a' + c + 'a'
    o = clean.normalize_str(s)
    if o == 'aa':
        sets['CTRL'].append(cp)
    elif o == 'a a' and c != ' ':
        sets['CLEANWS'].append(cp)
    elif o != s:
        sys.exit('clean_text does something else to U+%04X: %r' % (cp, o))
    o = cjk.normalize_str(s)
    if o == 'a ' + c + ' a':
        sets['CJK'].append(cp)
    elif o != s:
        sys.exit('handle_chinese_chars does something else to U+%04X' % cp)
    check_strip([cp])
    mid = [ord(x) for x in lower.normalize_str(s)[1:-1]]
    if mid != [cp]:
        lower_map.append((cp, mid))
    p = [w for w, _ in pre.pre_tokenize_str(s)]
    if p == ['a', c, 'a']:
        sets['PUNCT'].append(cp)
    elif p == ['a', 'a']:
        sets['PREWS'].append(cp)
    elif p != [s]:
        sys.exit('BertPreTokenizer does something else to U+%04X: %r' % (cp, p))

# a combining class the library does not know (a mark newer than its Unicode)
# is 0 there: find them where a pair of marks is not reordered as Python's
# classes say, and take the class out
def pair_ok(x, y):
    s = 'a' + chr(x) + chr(y) + 'a'
    return [ord(c) for c in strip.normalize_str(s)] == [ord('a')] + model_strip([x, y]) + [ord('a')]


marks = sorted(ccc)
for x in marks:
    for y in marks:
        if pair_ok(x, y):
            continue
        for z in (x, y):
            keep = ccc.pop(z)
            if all(pair_ok(z, w) and pair_ok(w, z) for w in marks if w in ccc):
                OLDER_CCC.append((z, keep))
                break
            ccc[z] = keep
        else:
            sys.exit('no class fits U+%04X U+%04X' % (x, y))
marks = sorted(ccc)
for x in marks:
    for y in marks:
        check_strip([x, y])
kept = [cp for cp in marks if cp not in MN]
between = sorted(set(sets['MN']) | set(marks))
for x in kept:
    for z in between:
        for y in kept:
            check_strip([x, z, y])

for cp, out in lower_map:
    if not 1 <= len(out) <= K or 0 in out:
        sys.exit('lowercase U+%04X gives %d code points' % (cp, len(out)))
for cp, out in nfd_map:
    if not 1 <= len(out) <= KD or 0 in out:
        sys.exit('NFD U+%04X gives %d code points' % (cp, len(out)))


def ranges(cps, val=None):
    out = []
    for cp in cps:
        v = val(cp) if val else None
        if out and out[-1][1] == cp - 1 and out[-1][2] == v:
            out[-1][1] = cp
        else:
            out.append([cp, cp, v])
    return out


def emit_ranges(name, cps):
    rs = ranges(cps)
    s = 'static const uint32_t ZT_WP_%s[%d][2] = {\n' % (name, len(rs))
    for i in range(0, len(rs), 4):
        s += '    ' + ' '.join('{0x%x, 0x%x},' % tuple(r[:2]) for r in rs[i:i + 4]) + '\n'
    return s + '};\n'


def emit_ccc():
    rs = ranges(marks, ccc.get)
    s = 'static const uint32_t ZT_WP_CCC[%d][3] = {\n' % len(rs)
    for i in range(0, len(rs), 3):
        s += '    ' + ' '.join('{0x%x, 0x%x, %d},' % tuple(r) for r in rs[i:i + 3]) + '\n'
    return s + '};\n'


def emit_map(name, m, k):
    s = 'static const uint32_t ZT_WP_%s[%d][%d] = {\n' % (name, len(m), k + 1)
    for i in range(0, len(m), 3):
        s += '    ' + ' '.join('{' + ', '.join('0x%x' % v for v in [cp] + out + [0] * (k - len(out))) + '},'
                               for cp, out in m[i:i + 3]) + '\n'
    return s + '};\n'


h = ['/* zt_wp_tables.h — generated by gen_wp_tables.py, checked against the Hugging Face',
     ' * tokenizers %s library (BertNormalizer, BertPreTokenizer); NFD and combining classes'
     % tokenizers.__version__,
     ' * from Python %s unicodedata (Unicode %s). Do not edit.' % (sys.version.split()[0], unicodedata.unidata_version),
     ' * Ranges are [first, last] (CCC: [first, last, class]); map rows are {code point,',
     ' * its code points, 0 padded}. */', '#include <stdint.h>',
     '#define ZT_WP_LOWER_K %du' % K, '#define ZT_WP_NFD_K %du' % KD]
for name in ('CTRL', 'CLEANWS', 'CJK', 'MN', 'PUNCT', 'PREWS'):
    h.append(emit_ranges(name, sets[name]))
h.append(emit_ccc())
h.append(emit_map('NFD', nfd_map, KD))
h.append(emit_map('LOWER', lower_map, K))
open(sys.argv[1], 'w').write('\n'.join(h))
print('decompositions left out (newer than the library):', ' '.join('U+%04X' % c for c in sorted(OLDER)))
print('classes left out (newer than the library):', ' '.join('U+%04X' % c for c, _ in sorted(OLDER_CCC)))
print(' '.join('%s %d' % (k, len(ranges(v))) for k, v in sets.items()),
      'CCC %d NFD %d LOWER %d' % (len(ranges(marks, ccc.get)), len(nfd_map), len(lower_map)))
