#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""ref_bert.py -- an encoder cell on the integer engine (bench/tensor/zt_enc)
against the Hugging Face stack it was converted from.

    python3 ref_bert.py HF_DIR MODEL.gguf ZT_ENC [--fuzz N] [--json OUT]

1. Tokenization, exactly: the `tokenizers` library (the checkpoint's
   tokenizer.json, padding off, truncation longest_first at the file's
   max_tokens) and zt_bert_tokenize give the same ids on every case: fixed
   hard cases (accents, Hangul, CJK, combining marks, controls, white space
   of every kind, punctuation, emoji, long words, special-token text, text
   past the length limit) and N seeded random strings drawn from the scripts
   and symbol blocks; with special-token text parsed (the library's default)
   and not (encode_special_tokens).
2. The forward pass: transformers in float64 on the same ids (so this step
   measures the network alone) against the engine. Classifiers: the largest
   logit difference and whether the top label agrees; a disagreement counts
   only when the reference's top two are closer than the difference (a near
   tie). Embedders: the cosine of each embedding with the reference.
The bounds are written here before the run: logits within 0.15, every top
label agrees outside near ties; embeddings cosine >= 0.999. Exit 1 when one
fails.
"""
import argparse
import json
import os
import random
import struct
import subprocess
import sys

import numpy as np

HARD = [
    'Hello, world!', 'The quick brown fox jumps over the lazy dog.', "Don't stop-believing; it's 3.14!",
    'Caf\u00e9 na\u00efve r\u00e9sum\u00e9 co\u00f6perate \u00c5ngstr\u00f6m', '\u039f\u0394\u039f\u03a3 \u03a3\u03af\u03c3\u03c5\u03c6\u03bf\u03c2', '\u0130stanbul \u0131i', 'Stra\u00dfe MASSE',
    '\ud55c\uad6d\uc5b4 \ud14d\uc2a4\ud2b8\uc785\ub2c8\ub2e4', '\u65e5\u672c\u8a9e\u306e\u30c6\u30ad\u30b9\u30c8\u3068\u6f22\u5b57', '\u4e2d\u6587\u5206\u8bcd\u6d4b\u8bd5\uff0c\u6807\u70b9\u3002', '\u0e44\u0e17\u0e22 \u0e20\u0e32\u0e29\u0e32', '\u05e2\u05b4\u05d1\u05b0\u05e8\u05b4\u05d9\u05ea', '\u0627\u0644\u0639\u0631\u0628\u064a\u0629',
    'e\u0301 a\u0308 o\u0323\u0302 \u1e69', 'x\u0315\u0300y', '\u0f73\u0f75', '\U0001d15f\U0001d160',
    'tab\there\nnew\rline\x0bvt\x0cff\x85nel\u2028ls\u3000ideo\u00a0nbsp\u200bzwsp',
    'ctrl\x00nul\x01\x7f\ufffd\u200d\ufeff',
    'emoji \U0001f600\U0001f44d\U0001f3fd \U0001f1ee\U0001f1f9 \u2764\ufe0f', 'math \u2211\u222b\u221a\u221e \u2260 \u2264', '\u00abquoted\u00bb \u201ccurly\u201d \u2018single\u2019 \u2014 dash \u2013 en \u2026 ellipsis',
    'a' * 99, 'b' * 100, 'c' * 101, 'supercalifragilisticexpialidocious antidisestablishmentarianism',
    '[CLS] inside [SEP] text [UNK] [MASK] [PAD] [cls]', 'x[SEP]y', '[SEP][SEP]',
    '', '   ', '!!!', 'Mixed CASE Words', 'URLs http://example.com/a?b=c&d=e#f',
    'numbers 1,234,567.89 and 2026-10-15', '\u210c\U0001d522\U0001d529\U0001d529\U0001d52c \ufb01 \ufb00 \u2460 \u00b2', '\u216b \u01c5 \u01c8 \u01cb', '\ua66e \U00010437 \U0001d400',
    ' '.join(['word%d' % i for i in range(400)]),
]

PAIRS = [
    ('A man is playing a guitar on stage.', 'A person is making music.'),
    ('A man is playing a guitar on stage.', 'Nobody is playing any instrument.'),
    ('The cat sleeps on the sofa.', 'The weather in Paris is warm today.'),
    ('All birds can fly.', 'Penguins are birds that cannot fly.'),
    ('She finished the report before noon.', 'The report was finished in the morning.'),
    ('The store is closed on Sundays.', 'You can shop there every day of the week.'),
    ('Two children are building a sandcastle.', 'Kids are at the beach.'),
    ('The price rose by ten percent.', 'The price fell.'),
    (' '.join(['long'] * 300), ' '.join(['pair'] * 300)),
]

SENTENCES = [
    'Everyone is buying this phone, so it must be the best one.',
    'You cannot trust his argument about taxes because he was once arrested.',
    'If we allow students to redo one test, soon they will demand to redo every assignment.',
    'Either you support this policy completely or you hate our country.',
    'I wore my lucky socks and we won, so the socks caused the win.',
    'My grandfather smoked every day and lived to ninety, so smoking is harmless.',
    'This medicine works because the doctor who sells it says it works.',
    'The Bible is true because it says so in the Bible.',
    'Think of the poor children who will suffer if you vote against this.',
    'A feather is light; what is light cannot be dark; so a feather cannot be dark.',
    'Water boils at one hundred degrees Celsius at sea level.',
    'The meeting was moved to Thursday afternoon.',
    'A cat is resting on the windowsill.', 'Stock markets fell sharply after the announcement.',
]

BLOCKS = [(0x20, 0x7E), (0xA0, 0x24F), (0x300, 0x36F), (0x370, 0x3FF), (0x400, 0x4FF),
          (0x590, 0x6FF), (0x900, 0x97F), (0xE00, 0xE7F), (0x1100, 0x11FF), (0x1E00, 0x1FFF),
          (0x2000, 0x206F), (0x2100, 0x22FF), (0x3000, 0x30FF), (0x4E00, 0x4FFF), (0xAC00, 0xAD00),
          (0xF900, 0xFAFF), (0xFE00, 0xFE6F), (0xFF00, 0xFFEF), (0x1D100, 0x1D1FF),
          (0x1F300, 0x1F6FF), (0x20000, 0x200FF), (0x0, 0x1F)]


def fuzz(n, seed=20261015):
    rnd = random.Random(seed)
    out = []
    for _ in range(n):
        s = []
        for _ in range(rnd.randint(1, 40)):
            if rnd.random() < 0.3:
                s.append(' ')
            lo, hi = rnd.choice(BLOCKS)
            cp = rnd.randint(lo, hi)
            if not 0xD800 <= cp <= 0xDFFF:
                s.append(chr(cp))
        out.append(''.join(s))
    return out


def cases_blob(cases):
    b = bytearray()
    for a, bt in cases:
        ea = a.encode()
        b += struct.pack('<I', len(ea)) + ea
        if bt is None:
            b += struct.pack('<I', 0)
        else:
            eb = bt.encode()
            b += struct.pack('<II', 1, len(eb)) + eb
    return bytes(b)


def run_engine(zt_enc, gguf_path, cases, tmp, *flags):
    p = os.path.join(tmp, 'cases.bin')
    with open(p, 'wb') as f:
        f.write(cases_blob(cases))
    r = subprocess.run([zt_enc, gguf_path, p, *flags], capture_output=True, text=True)
    if r.returncode:
        sys.exit('zt_enc failed: ' + r.stderr)
    rows, meta = [], {}
    for line in r.stdout.splitlines():
        if line.startswith('zt.'):
            k, v = line.split('=', 1)
            meta[k] = v
            continue
        parts = line.split(' | ')
        ids = [int(x) for x in parts[0].split()[1:]]
        vals = [int(x) for x in parts[1].split()[1:]] if len(parts) > 1 else None
        rows.append((ids, vals))
    return rows, meta


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('hf_dir')
    ap.add_argument('gguf')
    ap.add_argument('zt_enc')
    ap.add_argument('--fuzz', type=int, default=2000)
    ap.add_argument('--json', help='write the results here')
    a = ap.parse_args()
    import tempfile
    import torch
    from tokenizers import Tokenizer
    from gguf import GGUFReader
    from transformers import AutoConfig, AutoModel, AutoModelForSequenceClassification

    rd = GGUFReader(a.gguf)
    f = rd.fields['zxv.wordpiece.max_tokens']
    max_tokens = int(f.parts[f.data[0]][0])
    cfg = AutoConfig.from_pretrained(a.hf_dir)
    classifier = any(x.endswith('ForSequenceClassification') for x in (cfg.architectures or []))
    tk = Tokenizer.from_file(os.path.join(a.hf_dir, 'tokenizer.json'))
    tk.no_padding()
    tk.enable_truncation(max_length=max_tokens, strategy='longest_first')

    singles = HARD + fuzz(a.fuzz)
    tok_cases = [(s, None) for s in singles] + PAIRS + [(singles[i], singles[-1 - i]) for i in range(40)]
    report = {'model': a.gguf, 'max_tokens': max_tokens}
    fails = 0
    with tempfile.TemporaryDirectory() as tmp:
        for special in (True, False):
            tk.encode_special_tokens = not special
            want = [tk.encode(x, y).ids if y is not None else tk.encode(x).ids for x, y in tok_cases]
            got, _ = run_engine(a.zt_enc, a.gguf, tok_cases, tmp, '--tok', *(['--special'] if special else []))
            bad = [i for i, (w, (g, _)) in enumerate(zip(want, got)) if w != g]
            key = 'tokens_special_parsed' if special else 'tokens_special_as_text'
            report[key] = {'cases': len(tok_cases), 'differ': len(bad)}
            print('%s: %d cases, %d differ' % (key, len(tok_cases), len(bad)))
            for i in bad[:5]:
                print('  case %r / %r\n    library %s\n    engine  %s' % (tok_cases[i][0][:80], tok_cases[i][1],
                                                                       want[i][:40], got[i][0][:40]))
            fails += len(bad) > 0
        tk.encode_special_tokens = False

        fwd_cases = ([(s, None) for s in SENTENCES] + (PAIRS if classifier else []) +
                     [(s, None) for s in HARD[:12]])
        got, meta = run_engine(a.zt_enc, a.gguf, fwd_cases, tmp, '--special')
        report['engine'] = meta
        torch.set_grad_enabled(False)
        if classifier:
            m = AutoModelForSequenceClassification.from_pretrained(a.hf_dir, dtype=torch.float64).eval()
        else:
            m = AutoModel.from_pretrained(a.hf_dir, dtype=torch.float64).eval()
        rows = []
        for (x, y), (ids, vals) in zip(fwd_cases, got):
            enc = tk.encode(x, y) if y is not None else tk.encode(x)
            if enc.ids != ids:
                sys.exit('token ids differ on a forward case: %r' % x)
            ii = torch.tensor([ids])
            tt = torch.tensor([enc.type_ids])
            out = m(input_ids=ii, token_type_ids=tt, attention_mask=torch.ones_like(ii))
            e = np.array(vals, dtype=np.float64) / 65536.0
            if classifier:
                ref = out.logits[0].numpy()
                diff = float(np.abs(ref - e).max())
                srt = np.sort(ref)[::-1]
                agree = int(np.argmax(ref)) == int(np.argmax(e))
                tie = float(srt[0] - srt[1]) < 2 * diff
                rows.append({'text': x[:60], 'pair': y is not None and y[:60], 'max_diff': diff,
                             'ref_top': int(np.argmax(ref)), 'engine_top': int(np.argmax(e)),
                             'agree': agree, 'near_tie': tie})
            else:
                h = out.last_hidden_state[0].numpy().mean(0)
                h = h / np.linalg.norm(h)
                cos = float(h @ e / np.linalg.norm(e))
                rows.append({'text': x[:60], 'cosine': cos})
        report['forward'] = rows
        if classifier:
            md = max(r['max_diff'] for r in rows)
            dis = [r for r in rows if not r['agree'] and not r['near_tie']]
            ties = [r for r in rows if not r['agree'] and r['near_tie']]
            report['summary'] = {'cases': len(rows), 'max_logit_diff': md, 'top_agree':
                                 sum(r['agree'] for r in rows), 'disagree_outside_ties': len(dis),
                                 'disagree_near_ties': len(ties)}
            print('forward: %d cases, max logit difference %.4f, top label agrees on %d, '
                  'disagrees outside near ties on %d (near ties %d)' %
                  (len(rows), md, report['summary']['top_agree'], len(dis), len(ties)))
            fails += md > 0.15 or len(dis) > 0
        else:
            cmin = min(r['cosine'] for r in rows)
            report['summary'] = {'cases': len(rows), 'min_cosine': cmin,
                                 'mean_cosine': sum(r['cosine'] for r in rows) / len(rows)}
            print('forward: %d cases, cosine min %.6f mean %.6f' % (len(rows), cmin,
                                                                   report['summary']['mean_cosine']))
            fails += cmin < 0.999
        print('engine:', ' '.join('%s=%s' % kv for kv in meta.items()))
    report['pass'] = fails == 0
    if a.json:
        with open(a.json, 'w') as f:
            json.dump(report, f, indent=1, ensure_ascii=False)
    print('[%s] %s' % ('PASS' if fails == 0 else 'FAIL', os.path.basename(a.gguf)))
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
