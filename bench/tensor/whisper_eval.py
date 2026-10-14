#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""whisper_eval.py -- measure zt_asr (integer Whisper, T25) on real clips.

    python3 bench/tensor/whisper_eval.py --zt-asr /tmp/zxv_bench/zt_asr \
        --model whisper-base-q8_0.gguf --clips clips.json [--whisper-cli PATH --ggml ggml-base.bin]
        [--threads 2] [--ref] [--json out.json]

clips.json: [{"id": ..., "wav": path, "text": reference transcript or null}, ...]
(16 kHz mono 16-bit WAV; audio is never stored in the repository).
Per clip: zt_asr's transcript and timings; with --ref, ref_whisper.py's
float64 gaps (mel, encoder, logits); with --whisper-cli, whisper.cpp's
greedy transcript (-bs 1 -bo 1 -nf -nt -sns -l en, the same decoding
policy) and timings; word error rates of zt_asr against the reference text
and against whisper.cpp, after a simple normalisation (lower case, letters,
digits and apostrophes only). It measures; it claims nothing by itself.
"""
import argparse
import json
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))


def norm(s):
    s = s.lower().replace('-', ' ')
    s = re.sub(r"[^a-z0-9' ]+", ' ', s)
    return s.split()


def wer(ref, hyp):
    r, h = norm(ref), norm(hyp)
    d = list(range(len(h) + 1))
    for i in range(1, len(r) + 1):
        prev, d[0] = d[0], i
        for j in range(1, len(h) + 1):
            cur = d[j]
            d[j] = min(d[j] + 1, d[j - 1] + 1, prev + (r[i - 1] != h[j - 1]))
            prev = cur
    return d[len(h)] / max(len(r), 1), d[len(h)], len(r)


def run_zt(a, wav, tmp):
    dm, de, dl = (os.path.join(tmp, x) for x in ('mel.bin', 'enc.bin', 'logits.bin'))
    cmd = [a.zt_asr, a.model, wav, '--threads', str(a.threads), '--dump-mel', dm, '--dump-enc', de,
           '--dump-logits', dl]
    if a.kern:
        cmd += ['--kern', a.kern]
    out = subprocess.run(cmd, capture_output=True, text=True, check=True).stdout
    lines = out.split('\n')
    res = {'text': lines[0].strip()}
    for ln in lines[1:]:
        if ln.startswith('zt.') and '=' in ln:
            k, v = ln[3:].split('=', 1)
            res[k] = v
    return res, dm, de, dl


def run_wcpp(a, wav):
    cmd = [a.whisper_cli, '-m', a.ggml, '-f', wav, '-bs', '1', '-bo', '1', '-nf', '-nt', '-sns',
           '-l', 'en', '-t', str(a.threads)]
    p = subprocess.run(cmd, capture_output=True, text=True, check=True)
    res = {'text': ' '.join(x.strip() for x in p.stdout.split('\n') if x.strip())}
    for key in ('mel', 'encode', 'decode', 'total'):
        m = re.search(r'%s time =\s*([\d.]+) ms' % key, p.stderr)
        if m:
            res[key + '_ms'] = float(m.group(1))
    m = re.search(r'decode time =\s*[\d.]+ ms /\s*(\d+) runs', p.stderr)
    if m:
        res['decode_runs'] = int(m.group(1))
    return res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--zt-asr', required=True)
    ap.add_argument('--model', required=True)
    ap.add_argument('--clips', required=True)
    ap.add_argument('--threads', type=int, default=2)
    ap.add_argument('--kern')
    ap.add_argument('--ref', action='store_true', help='run the float64 reference (slow)')
    ap.add_argument('--whisper-cli')
    ap.add_argument('--ggml')
    ap.add_argument('--json')
    a = ap.parse_args()
    clips = json.load(open(a.clips))
    rows = []
    with tempfile.TemporaryDirectory() as tmp:
        for c in clips:
            row = {'id': c['id']}
            z, dm, de, dl = run_zt(a, c['wav'], tmp)
            row['zt'] = z
            if c.get('text'):
                row['wer_zt_vs_truth'] = wer(c['text'], z['text'])
            if a.whisper_cli:
                w = run_wcpp(a, c['wav'])
                row['wcpp'] = w
                row['wer_zt_vs_wcpp'] = wer(w['text'], z['text'])
                row['exact_vs_wcpp'] = norm(w['text']) == norm(z['text'])
                if c.get('text'):
                    row['wer_wcpp_vs_truth'] = wer(c['text'], w['text'])
            if a.ref:
                rj = os.path.join(tmp, 'ref.json')
                subprocess.run([sys.executable, os.path.join(HERE, 'compare', 'ref_whisper.py'), a.model,
                                c['wav'], dm, de, dl, '--ids', z.get('ids', ''), '--json', rj],
                               check=True, capture_output=True)
                row['ref'] = json.load(open(rj))
            rows.append(row)
            print(json.dumps(row), flush=True)
    if a.json:
        json.dump(rows, open(a.json, 'w'), indent=1)


if __name__ == '__main__':
    main()
