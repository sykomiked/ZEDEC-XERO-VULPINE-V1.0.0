# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Train the compact language identifier used by xlate_langid.c.

DATA (public domain only, so the trained tables carry no licence terms):
  * Mozilla Common Voice sentence collection, CC0-1.0 (public domain
    dedication): server/data/<locale>/sentence-collector.txt from
    github.com/common-voice/common-voice at commit CV_COMMIT.
  * Public-domain Bibles from the eBible corpus (github.com/BibleNLP/ebible at
    commit EB_COMMIT, metadata/licences.tsv lists them as "Public Domain"),
    used only to top up three languages whose Common Voice text is small:
    Croatian (hrv), Hebrew (heb) and Swahili (swh1850).

MODEL: a naive-Bayes score over hashed character 1-, 2- and 3-grams of
case-folded text, after routing by Unicode script. A language that is the only
one in its script (Greek, Thai, Korean, ...) needs no n-gram table at all;
Han-only text is scored against the Han languages and Japanese. Each other
language keeps its K most frequent n-grams (K_HAN for Han and kana scripts) with a quantised cost round(8 * -ln p); an n-gram missing from a
language costs that language's rarest kept n-gram plus UNSEEN_PENALTY. The C
classifier reproduces score() below exactly (integer arithmetic only), and
test_xlate.c checks it against this script's own predictions.

SPLIT: a sentence is held out when (131-hash of its UTF-8 bytes) % 10 == 0;
the held-out sentences are never trained on. Accuracy is reported on up to
HELD_OUT_MAX held-out sentences per language.

Usage:
    python3 gen_langid_model.py fetch DATA_DIR      # download the pinned text
    python3 gen_langid_model.py build DATA_DIR OUT_DIR
        -> OUT_DIR/xlate_langid_model.h, OUT_DIR/test_langid_fixture.h and an
           accuracy report on stdout
"""
import collections
import math
import os
import shutil
import subprocess
import sys
import urllib.request

CV_COMMIT = 'a3acd695856c3dacdfe6fe52597692c9be2aac04'
EB_COMMIT = 'c531ff2da02843ded6d09afbe29a197ab844981f'
CV_URL = ('https://raw.githubusercontent.com/common-voice/common-voice/%s/server/data/%s/'
          'sentence-collector.txt')
EB_URL = 'https://raw.githubusercontent.com/BibleNLP/ebible/%s/corpus/%s.txt'

NMAX = 3
K = 600
K_HAN = 1500
COST_SCALE = 8.0
UNSEEN_PENALTY = 12
TRAIN_CHARS = 300000
HELD_OUT_MAX = 1000
MAX_CPS = 512          # normalised code points scored per text (C: XL_MAX_CPS)
FIXTURE_PER_LANG = 4

# (BCP 47 tag, English name, Common Voice locales, public-domain eBible files)
LANGS = [
    ('en', 'English', ['en'], []), ('de', 'German', ['de'], []), ('fr', 'French', ['fr'], []),
    ('es', 'Spanish', ['es'], []), ('it', 'Italian', ['it'], []), ('pt', 'Portuguese', ['pt'], []),
    ('ru', 'Russian', ['ru'], []), ('uk', 'Ukrainian', ['uk'], []), ('pl', 'Polish', ['pl'], []),
    ('cs', 'Czech', ['cs'], []), ('sk', 'Slovak', ['sk'], []),
    ('hr', 'Croatian', ['hr'], ['hrv-hrv']), ('sr', 'Serbian', ['sr'], []),
    ('bg', 'Bulgarian', ['bg'], []), ('mk', 'Macedonian', ['mk'], []),
    ('be', 'Belarusian', ['be'], []), ('lt', 'Lithuanian', ['lt'], []),
    ('lv', 'Latvian', ['lv'], []), ('fi', 'Finnish', ['fi'], []), ('hu', 'Hungarian', ['hu'], []),
    ('ro', 'Romanian', ['ro'], []), ('nl', 'Dutch', ['nl'], []), ('da', 'Danish', ['da'], []),
    ('sv', 'Swedish', ['sv-SE'], []), ('nb', 'Norwegian Bokmal', ['nb-NO'], []),
    ('is', 'Icelandic', ['is'], []), ('cy', 'Welsh', ['cy'], []), ('ca', 'Catalan', ['ca'], []),
    ('gl', 'Galician', ['gl'], []), ('el', 'Greek', ['el'], []), ('tr', 'Turkish', ['tr'], []),
    ('kk', 'Kazakh', ['kk'], []), ('uz', 'Uzbek', ['uz'], []), ('tt', 'Tatar', ['tt'], []),
    ('ar', 'Arabic', ['ar'], []), ('he', 'Hebrew', ['he'], ['heb-heb']),
    ('fa', 'Persian', ['fa'], []), ('ur', 'Urdu', ['ur'], []), ('hi', 'Hindi', ['hi'], []),
    ('bn', 'Bengali', ['bn'], []), ('mr', 'Marathi', ['mr'], []),
    ('pa', 'Punjabi', ['pa-IN'], []), ('or', 'Odia', ['or'], []), ('as', 'Assamese', ['as'], []),
    ('ja', 'Japanese', ['ja'], []), ('zh-Hans', 'Chinese (Simplified)', ['zh-CN'], []),
    ('zh-Hant', 'Chinese (Traditional)', ['zh-TW'], []),
    ('yue', 'Cantonese', ['yue', 'zh-HK'], []), ('ko', 'Korean', ['ko'], []),
    ('vi', 'Vietnamese', ['vi'], []), ('th', 'Thai', ['th'], []),
    ('id', 'Indonesian', ['id'], []), ('af', 'Afrikaans', ['af'], []),
    ('eo', 'Esperanto', ['eo'], []), ('sq', 'Albanian', ['sq'], []),
    ('ka', 'Georgian', ['ka'], []), ('mn', 'Mongolian', ['mn'], []),
    ('my', 'Burmese', ['my'], []), ('lo', 'Lao', ['lo'], []), ('am', 'Amharic', ['am'], []),
    ('mt', 'Maltese', ['mt'], []), ('fy', 'Western Frisian', ['fy-NL'], []),
    ('ha', 'Hausa', ['ha'], []), ('yo', 'Yoruba', ['yo'], []), ('ig', 'Igbo', ['ig'], []),
    ('zu', 'Zulu', ['zu'], []), ('xh', 'Xhosa', ['xh'], []),
    ('ckb', 'Central Kurdish', ['ckb'], []), ('kmr', 'Northern Kurdish', ['kmr'], []),
    ('ps', 'Pashto', ['ps'], []), ('ug', 'Uyghur', ['ug'], []), ('ba', 'Bashkir', ['ba'], []),
    ('cv', 'Chuvash', ['cv'], []), ('sah', 'Yakut', ['sah'], []), ('tk', 'Turkmen', ['tk'], []),
    ('ta', 'Tamil', ['ta'], []), ('ml', 'Malayalam', ['ml'], []), ('te', 'Telugu', ['te'], []),
    ('kn', 'Kannada', ['kn'], []), ('sw', 'Swahili', ['sw'], ['swh-swh1850']),
    ('tl', 'Tagalog', ['tl'], []), ('ne', 'Nepali', ['ne-NP'], []),
]

# Script ranges, in xlate_langid.c's k_scripts order (index = script id).
SCRIPTS = [
    ('Latn', [(0x61, 0x7A), (0xDF, 0x24F), (0x1E00, 0x1EFF)]),
    ('Cyrl', [(0x400, 0x52F)]), ('Grek', [(0x370, 0x3FF), (0x1F00, 0x1FFF)]),
    ('Armn', [(0x530, 0x58F)]), ('Hebr', [(0x590, 0x5FF)]),
    ('Arab', [(0x600, 0x6FF), (0x750, 0x77F), (0xFB50, 0xFDFF), (0xFE70, 0xFEFF)]),
    ('Deva', [(0x900, 0x97F)]), ('Beng', [(0x980, 0x9FF)]), ('Guru', [(0xA00, 0xA7F)]),
    ('Gujr', [(0xA80, 0xAFF)]), ('Orya', [(0xB00, 0xB7F)]), ('Taml', [(0xB80, 0xBFF)]),
    ('Telu', [(0xC00, 0xC7F)]), ('Knda', [(0xC80, 0xCFF)]), ('Mlym', [(0xD00, 0xD7F)]),
    ('Sinh', [(0xD80, 0xDFF)]), ('Thai', [(0xE00, 0xE7F)]), ('Laoo', [(0xE80, 0xEFF)]),
    ('Mymr', [(0x1000, 0x109F)]), ('Geor', [(0x10A0, 0x10FF), (0x1C90, 0x1CBF)]),
    ('Hang', [(0x1100, 0x11FF), (0x3130, 0x318F), (0xAC00, 0xD7AF)]),
    ('Ethi', [(0x1200, 0x139F)]), ('Khmr', [(0x1780, 0x17FF)]),
    ('Kana', [(0x3040, 0x30FF), (0x31F0, 0x31FF), (0xFF66, 0xFF9F)]),
    ('Hani', [(0x3400, 0x4DBF), (0x4E00, 0x9FFF), (0xF900, 0xFAFF), (0x20000, 0x2FFFF)]),
]
KANA = 23
HAN = 24
M32 = 0xFFFFFFFF


def fold(cp):
    if 0x41 <= cp <= 0x5A:
        return cp + 32
    if 0xC0 <= cp <= 0xDE and cp != 0xD7:
        return cp + 32
    if 0x100 <= cp <= 0x17F:
        if 0x139 <= cp <= 0x148 or 0x179 <= cp <= 0x17E:
            return cp + 1 if cp % 2 == 1 else cp
        if cp == 0x130:
            return 0x69
        if cp == 0x178:
            return 0xFF
        if cp in (0x131, 0x138, 0x149, 0x17F):
            return cp
        return cp | 1
    if 0x391 <= cp <= 0x3A9 and cp != 0x3A2:
        return cp + 32
    if 0x410 <= cp <= 0x42F:
        return cp + 32
    if 0x400 <= cp <= 0x40F:
        return cp + 80
    if 0x531 <= cp <= 0x556:
        return cp + 48
    return cp


SEP_SINGLE = (0x60C, 0x61B, 0x61F, 0x6D4, 0x964, 0x965, 0x589, 0x5BE, 0x5C0, 0x5C3, 0x1361, 0x1362,
              0x1363, 0x104A, 0x104B, 0xE5A, 0xE5B)


def is_sep(cp):
    if cp < 0x80:
        return not (0x61 <= cp <= 0x7A or 0x41 <= cp <= 0x5A)
    if cp <= 0xBF or cp == 0xD7 or cp == 0xF7:
        return True
    if 0x2000 <= cp <= 0x206F or 0x3000 <= cp <= 0x303F:
        return True
    if 0xFF00 <= cp <= 0xFF20 or 0xFF5B <= cp <= 0xFF65:
        return True
    if cp in SEP_SINGLE:
        return True
    if 0x660 <= cp <= 0x669 or 0x6F0 <= cp <= 0x6F9 or 0x966 <= cp <= 0x96F:
        return True
    return False


def is_drop(cp):
    if 0x300 <= cp <= 0x36F:
        return True
    if 0x591 <= cp <= 0x5C7 and cp not in (0x5BE, 0x5C0, 0x5C3, 0x5C6):
        return True
    if 0x64B <= cp <= 0x65F or cp == 0x670 or cp == 0x640:
        return True
    return cp in (0x200B, 0x200C, 0x200D, 0xFEFF)


def norm(s):
    """Mirror of xl_normalize(): drop marks, fold case, collapse separators."""
    out = []
    prev_sep = True
    for ch in s:
        cp = ord(ch)
        if is_drop(cp):
            continue
        if is_sep(cp):
            if not prev_sep:
                if len(out) >= MAX_CPS:
                    break
                out.append(0x20)
                prev_sep = True
            continue
        if len(out) >= MAX_CPS:
            break
        out.append(fold(cp))
        prev_sep = False
    while out and out[-1] == 0x20:
        out.pop()
    return out


def feats(cps):
    seq = [0x20] + cps + [0x20]
    res = []
    for n in range(1, NMAX + 1):
        for i in range(len(seq) - n + 1):
            g = seq[i:i + n]
            if n == 1 and g[0] == 0x20:
                continue
            h = (2166136261 ^ n) & M32
            for c in g:
                h = ((h ^ c) * 16777619) & M32
            res.append(h)
    return res


def script_of(cp):
    for i, (_, rs) in enumerate(SCRIPTS):
        for a, b in rs:
            if a <= cp <= b:
                return i
    return -1


def dom_script(cps):
    cnt = [0] * len(SCRIPTS)
    for cp in cps:
        if cp == 0x20:
            continue
        s = script_of(cp)
        if s >= 0:
            cnt[s] += 1
    if cnt[KANA] > 0 and cnt[KANA] * 8 >= cnt[KANA] + cnt[HAN]:
        return KANA
    best, bi = 0, -1
    for i, c in enumerate(cnt):
        if c > best:
            best, bi = c, i
    return bi


def held_out(line):
    h = 0
    for b in line.encode('utf-8'):
        h = (h * 131 + b) & M32
    return h % 10 == 0


def read_lines(path):
    out = []
    with open(path, encoding='utf-8', errors='replace') as f:
        for ln in f:
            ln = ln.strip()
            if ln and ln != '<range>':
                out.append(ln)
    return out


def fetch(data):
    os.makedirs(os.path.join(data, 'cv'), exist_ok=True)
    os.makedirs(os.path.join(data, 'eb'), exist_ok=True)
    for _, _, cvs, ebs in LANGS:
        for loc in cvs:
            dst = os.path.join(data, 'cv', loc + '.txt')
            if not os.path.exists(dst):
                print('fetch cv', loc)
                urllib.request.urlretrieve(CV_URL % (CV_COMMIT, loc), dst)
        for name in ebs:
            dst = os.path.join(data, 'eb', name + '.txt')
            if not os.path.exists(dst):
                print('fetch ebible', name)
                urllib.request.urlretrieve(EB_URL % (EB_COMMIT, name), dst)


class Model:
    def __init__(self, data):
        self.tags = [t for t, _, _, _ in LANGS]
        self.train, self.test = {}, {}
        for tag, _, cvs, ebs in LANGS:
            cv = [ln for loc in cvs for ln in read_lines(os.path.join(data, 'cv', loc + '.txt'))]
            eb = [ln for nm in ebs for ln in read_lines(os.path.join(data, 'eb', nm + '.txt'))]
            tr = [ln for ln in cv if not held_out(ln)] + [ln for ln in eb if not held_out(ln)]
            te = [ln for ln in cv if held_out(ln)] + [ln for ln in eb if held_out(ln)]
            t, c = [], 0
            for ln in tr:
                if c > TRAIN_CHARS:
                    break
                t.append(ln)
                c += len(ln)
            self.train[tag] = t
            self.test[tag] = te[:HELD_OUT_MAX]
        # Each language's script: the majority over its training sentences.
        self.script = []
        for tag in self.tags:
            c = collections.Counter(dom_script(norm(ln)) for ln in self.train[tag][:3000])
            self.script.append(c.most_common(1)[0][0])
        self.table = {}                       # hash -> {lang index: cost}
        self.unseen = [0] * len(self.tags)
        for li, tag in enumerate(self.tags):
            if self.script.count(self.script[li]) == 1 and self.script[li] != KANA:
                continue                      # alone in its script: no table
            cnt = collections.Counter()
            for ln in self.train[tag]:
                cnt.update(feats(norm(ln)))
            tot = sum(cnt.values())
            top = cnt.most_common(K_HAN if self.script[li] in (HAN, KANA) else K)
            self.unseen[li] = min(255, int(round(-math.log(top[-1][1] / tot) * COST_SCALE))
                                  + UNSEEN_PENALTY)
            for h, c in top:
                cost = min(255, int(round(-math.log(c / tot) * COST_SCALE)))
                self.table.setdefault(h, {})[li] = cost

    def classify(self, text):
        cps = norm(text)
        d = dom_script(cps)
        if d < 0:
            return -1
        # Han-only text may be Japanese (kanji place names): kana-script
        # languages compete in the Han group too.
        cand = [i for i, s in enumerate(self.script) if s == d or (d == HAN and s == KANA)]
        if not cand:
            return -1
        if len(cand) == 1:
            return cand[0]
        score = {i: 0 for i in cand}
        for h in feats(cps):
            row = self.table.get(h)
            if row is None or not any(i in row for i in cand):
                continue
            for i in cand:
                score[i] += row.get(i, self.unseen[i])
        best = cand[0]
        for i in cand:
            if score[i] < score[best]:
                best = i
        return best


def c_str(s):
    out = '"'
    for b in s.encode('utf-8'):
        if b in (0x22, 0x5C):
            out += '\\' + chr(b)
        elif 0x20 <= b < 0x7F and chr(b) != '?':
            out += chr(b)
        else:
            out += '\\%03o' % b
    return out + '"'


HEADER = ['/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */',
          '/* SPDX-License-Identifier: Apache-2.0 */']


def write_model(m, path):
    hashes = sorted(m.table)
    starts, entries = [], []
    for h in hashes:
        starts.append(len(entries))
        for li in sorted(m.table[h]):
            entries.append((li << 8) | m.table[h][li])
    starts.append(len(entries))
    assert len(entries) < 65536 and len(m.tags) < 128
    o = HEADER + ['/* xlate_langid_model.h — generated by gen_langid_model.py. Do not edit.',
                  ' * Trained on public-domain text only: Mozilla Common Voice sentences',
                  ' * (CC0-1.0, commit %s) and public-domain Bibles' % CV_COMMIT[:12],
                  ' * from the eBible corpus (commit %s). See docs/SPEECH_AND_TRANSLATION.md.'
                  % EB_COMMIT[:12],
                  ' * %d languages, %d hashed n-grams, %d (language, cost) entries. */'
                  % (len(m.tags), len(hashes), len(entries)),
                  '#ifndef ZXV_XLATE_LANGID_MODEL_H', '#define ZXV_XLATE_LANGID_MODEL_H',
                  '#include <stdint.h>',
                  '#define XL_NLANG   %du' % len(m.tags),
                  '#define XL_NHASH   %du' % len(hashes),
                  '#define XL_NENTRY  %du' % len(entries),
                  '#define XL_MAX_CPS %du' % MAX_CPS,
                  'static const char *const XL_TAG[XL_NLANG] = {']
    o += ['    %s,' % c_str(t) for t in m.tags]
    o += ['};', 'static const char *const XL_NAME[XL_NLANG] = {']
    o += ['    %s,' % c_str(n) for _, n, _, _ in LANGS]
    o += ['};', 'static const uint8_t XL_SCRIPT[XL_NLANG] = {']
    o += ['    ' + ', '.join(str(s) for s in m.script) + ',', '};']
    o += ['static const uint8_t XL_UNSEEN[XL_NLANG] = {']
    o += ['    ' + ', '.join(str(s) for s in m.unseen) + ',', '};']
    o += ['static const uint32_t XL_HASH[XL_NHASH] = {']
    for i in range(0, len(hashes), 6):
        o.append('    ' + ', '.join('0x%08xu' % h for h in hashes[i:i + 6]) + ',')
    o += ['};', 'static const uint16_t XL_START[XL_NHASH + 1] = {']
    for i in range(0, len(starts), 12):
        o.append('    ' + ', '.join(str(v) for v in starts[i:i + 12]) + ',')
    o += ['};', 'static const uint16_t XL_ENTRY[XL_NENTRY] = {']
    for i in range(0, len(entries), 12):
        o.append('    ' + ', '.join(str(v) for v in entries[i:i + 12]) + ',')
    o += ['};', '#endif']
    open(path, 'w').write('\n'.join(o) + '\n')


def write_fixture(m, path):
    o = HEADER + ['/* test_langid_fixture.h — generated by gen_langid_model.py. Do not edit.',
                  ' * Held-out sentences (never trained on) with the language they are',
                  ' * written in and the generator\'s own prediction, which the C',
                  ' * classifier must reproduce exactly. Sentence text: Mozilla Common',
                  ' * Voice (CC0-1.0) and public-domain eBible verses. */',
                  '#ifndef ZXV_TEST_LANGID_FIXTURE_H', '#define ZXV_TEST_LANGID_FIXTURE_H',
                  'typedef struct {', '    const char *text;', '    const char *lang;',
                  '    const char *predicted;', '} langid_case_t;',
                  'static const langid_case_t LANGID_CASES[] = {']
    n = 0
    for li, tag in enumerate(m.tags):
        k = 0
        for s in m.test[tag]:
            if k >= FIXTURE_PER_LANG:
                break
            if not (20 <= len(s) and len(s.encode('utf-8')) <= 160):
                continue
            p = m.classify(s)
            o.append('    {%s, %s, %s},' % (c_str(s), c_str(tag),
                                            c_str(m.tags[p] if p >= 0 else 'und')))
            k += 1
            n += 1
    o += ['};', '#define LANGID_NCASES %du' % n, '#endif']
    open(path, 'w').write('\n'.join(o) + '\n')


def report(m):
    tot = ok = 0
    by_len = collections.defaultdict(lambda: [0, 0])
    per, conf = {}, collections.Counter()
    for li, tag in enumerate(m.tags):
        good = 0
        for s in m.test[tag]:
            p = m.classify(s)
            b = '<20' if len(s) < 20 else ('20-49' if len(s) < 50 else '>=50')
            by_len[b][1] += 1
            tot += 1
            if p == li:
                ok += 1
                good += 1
                by_len[b][0] += 1
            else:
                conf[(tag, m.tags[p] if p >= 0 else 'und')] += 1
        per[tag] = (good, len(m.test[tag]))
    print('languages: %d   held-out sentences: %d   accuracy: %.2f%%' % (len(m.tags), tot,
                                                                         100.0 * ok / tot))
    for b in ('<20', '20-49', '>=50'):
        g, n = by_len[b]
        print('  length %-5s chars: %.2f%% of %d' % (b, 100.0 * g / n, n))
    print('per language (accuracy, held-out count):')
    print('  ' + ', '.join('%s %.1f%% (%d)' % (t, 100.0 * g / n, n) for t, (g, n) in per.items()))
    print('most common confusions (true -> predicted: count):')
    print('  ' + ', '.join('%s->%s: %d' % (a, b, c) for (a, b), c in conf.most_common(15)))


def clang_format(path):
    cf = shutil.which('clang-format-18') or shutil.which('clang-format')
    if cf:
        subprocess.run([cf, '-i', path], check=True)
        subprocess.run([cf, '-i', path], check=True)


def main():
    if len(sys.argv) >= 3 and sys.argv[1] == 'fetch':
        fetch(sys.argv[2])
        return
    if len(sys.argv) < 4 or sys.argv[1] != 'build':
        print(__doc__)
        sys.exit(2)
    m = Model(sys.argv[2])
    mp = os.path.join(sys.argv[3], 'xlate_langid_model.h')
    fp = os.path.join(sys.argv[3], 'test_langid_fixture.h')
    write_model(m, mp)
    write_fixture(m, fp)
    clang_format(mp)
    clang_format(fp)
    report(m)


if __name__ == '__main__':
    main()
