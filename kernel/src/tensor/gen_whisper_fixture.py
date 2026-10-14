# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# Generate test_whisper_fixture.h for test_zt_whisper.c: a tiny random Whisper
# (80 mel bins with the real slaney filterbank, width 32, 2 heads, 2 encoder
# and 2 decoder layers, audio context 50 = 1 s, text context 16, a 276-token
# byte-level vocabulary with Whisper's special tokens) written as a GGUF by
# gguf-py in the layout of bench/tensor/convert_whisper.py, and the float64
# reference of bench/tensor/compare/ref_whisper.py (whisper/audio.py and
# whisper/model.py semantics) on its dequantised weights:
#   the log-mel of a test signal (rebuilt by the test from an integer recipe:
#   a 400 Hz square wave with an envelope, splitmix64 noise, a silent gap and
#   zero padding), the encoder output, teacher-forced logits for a token
#   sequence, the language detected and the greedy transcript (with the
#   suppression rules of zt_whisper_greedy). Values are Q16, rounded.
# Matrices are Q8_0 except conv1 (rows of 240: F32) and the second decoder
# layer's fc2 (F16), which zt_nn converts.
# Usage: python3 -m pip install gguf numpy
#        python3 gen_whisper_fixture.py test_whisper_fixture.h
import os
import sys
import tempfile

import numpy as np
import gguf
from gguf import GGMLQuantizationType as T

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', '..', 'bench', 'tensor'))
sys.path.insert(0, os.path.join(HERE, '..', '..', '..', 'bench', 'tensor', 'compare'))
import convert_whisper as CW  # noqa: E402
import ref_whisper as R  # noqa: E402
from gen_model_fixture import mix  # noqa: E402

rng = np.random.default_rng(20261012)
NM, D, NH, NL, FF, ACTX, TCTX = 80, 32, 2, 2, 64, 50, 16
NS = 2 * ACTX * 160  # samples in the window
N_PCM = 12000        # samples given (the rest is zero padding)
LANGS = ['en', 'de', 'fr']


def bytes_to_unicode():
    bs = list(range(ord('!'), ord('~') + 1)) + list(range(ord('¡'), ord('¬') + 1)) + list(range(ord('®'), ord('ÿ') + 1))
    cs = bs[:]
    n = 0
    for b in range(256):
        if b not in bs:
            bs.append(b)
            cs.append(256 + n)
            n += 1
    return [chr(c) for c in cs]


toks = bytes_to_unicode()  # ids 0..255, GPT-2's order (' ' -> 'Ġ' is 220)
merges = ['Ġ t', 'h e', 'Ġt he', 'i s']
toks += ['Ġt', 'he', 'Ġthe', 'is']
specials = ['<|endoftext|>', '<|startoftranscript|>'] + ['<|%s|>' % l for l in LANGS] + \
    ['<|translate|>', '<|transcribe|>', '<|startoflm|>', '<|startofprev|>', '<|nocaptions|>',
     '<|notimestamps|>'] + ['<|%.2f|>' % (0.02 * i) for i in range(5)]
EOT = len(toks)
toks += specials
NV = len(toks)
tid = {s: i for i, s in enumerate(toks)}

path = os.path.join(tempfile.mkdtemp(), 'whisper-tiny-random.gguf')
w = gguf.GGUFWriter(path, 'whisper')
for k, v in [('sample_rate', 16000), ('n_fft', 400), ('hop_length', 160), ('n_mels', NM), ('vocab_size', NV),
             ('audio.context_length', ACTX), ('audio.embedding_length', D), ('audio.feed_forward_length', FF),
             ('audio.head_count', NH), ('audio.block_count', NL), ('text.context_length', TCTX),
             ('text.embedding_length', D), ('text.feed_forward_length', FF), ('text.head_count', NH),
             ('text.block_count', NL)]:
    w.add_uint32('whisper.' + k, v)
w.add_float32('whisper.attention.layer_norm_epsilon', 1e-5)
for key, s in [('eot', '<|endoftext|>'), ('sot', '<|startoftranscript|>'), ('translate', '<|translate|>'),
               ('transcribe', '<|transcribe|>'), ('sot_lm', '<|startoflm|>'), ('sot_prev', '<|startofprev|>'),
               ('no_speech', '<|nocaptions|>'), ('no_timestamps', '<|notimestamps|>'),
               ('timestamp_begin', '<|0.00|>')]:
    w.add_uint32('whisper.token.' + key, tid[s])
w.add_uint32('whisper.token.lang_first', tid['<|en|>'])
w.add_uint32('whisper.token.lang_count', len(LANGS))
w.add_array('whisper.languages', LANGS)
w.add_array('whisper.suppress_tokens', [1, 2, 7, 259, tid['<|translate|>']])
w.add_array('whisper.begin_suppress_tokens', [220, EOT])
w.add_tokenizer_model('gpt2')
w.add_tokenizer_pre('whisper')
w.add_token_list(toks)
w.add_token_types([gguf.TokenType.CONTROL if i >= EOT else gguf.TokenType.NORMAL for i in range(NV)])
w.add_token_merges(merges)
w.add_eos_token_id(EOT)


def q8(name, rows, cols, scale):
    CW.q8(w, name, rng.standard_normal((rows, cols)) * scale)


def f32(name, shape, scale, offset=0.0):
    CW.f32(w, name, offset + rng.standard_normal(shape) * scale)


CW.f32(w, 'mel_filters', CW.mel_filters(16000, 400, NM))
f32('enc.conv1.weight', (D, NM * 3), 0.08)
f32('enc.conv1.bias', (D,), 0.1)
q8('enc.conv2.weight', D, D * 3, 0.1)
f32('enc.conv2.bias', (D,), 0.1)
f32('enc.pos', (ACTX, D), 0.3)


def block(p, cross, l):
    for ln, parts in [('attn_', True)] + ([('cross_', True)] if cross else []):
        if ln == 'cross_':
            f32(p + 'cross_ln.weight', (D,), 0.2, 1.0)
            f32(p + 'cross_ln.bias', (D,), 0.1)
        else:
            f32(p + 'attn_ln.weight', (D,), 0.2, 1.0)
            f32(p + 'attn_ln.bias', (D,), 0.1)
        for m in ('q', 'k', 'v', 'out'):
            q8(p + ln + m + '.weight', D, D, 0.25)
            if m != 'k':
                f32(p + ln + m + '.bias', (D,), 0.05)
    f32(p + 'mlp_ln.weight', (D,), 0.2, 1.0)
    f32(p + 'mlp_ln.bias', (D,), 0.1)
    q8(p + 'fc1.weight', FF, D, 0.2)
    f32(p + 'fc1.bias', (FF,), 0.05)
    if cross and l == 1:
        w.add_tensor(p + 'fc2.weight', (rng.standard_normal((D, FF)) * 0.15).astype(np.float16))
    else:
        q8(p + 'fc2.weight', D, FF, 0.15)
    f32(p + 'fc2.bias', (D,), 0.05)


for l in range(NL):
    block('enc.blk.%d.' % l, False, l)
f32('enc.ln.weight', (D,), 0.2, 1.0)
f32('enc.ln.bias', (D,), 0.1)
q8('dec.token_embd.weight', NV, D, 0.5)
f32('dec.pos', (TCTX, D), 0.2)
for l in range(NL):
    block('dec.blk.%d.' % l, True, l)
f32('dec.ln.weight', (D,), 0.2, 1.0)
f32('dec.ln.bias', (D,), 0.1)
w.write_header_to_file()
w.write_kv_data_to_file()
w.write_tensors_to_file()
w.close()
blob = open(path, 'rb').read()


def pcm_recipe():
    """the test signal (test_zt_whisper.c rebuilds it): integers only"""
    z = mix(0x5157, N_PCM)
    out = []
    for n in range(N_PCM):
        if 8000 <= n < 9200:
            out.append(0)
            continue
        env = n if n < 4000 else (4000 if n < 8000 else 12000 - n)
        a = env * 6000 // 4000
        noise = int(z[n] % np.uint64(2001)) - 1000
        out.append((a if (n // 20) & 1 else -a) + noise)
    return np.array(out, dtype=np.int64)


pcm = pcm_recipe()
assert np.abs(pcm).max() < 32768
kv, W = R.load(path)
mel = R.log_mel(pcm.astype(np.float64), W('mel_filters'), NS)
enc = R.encoder(kv, W, mel)
prompt = [tid['<|startoftranscript|>'], tid['<|en|>'], tid['<|transcribe|>'], tid['<|notimestamps|>']]
seq = prompt + [72, 256, 258, 40, 259, 7]
logits = R.decoder(kv, W, enc, np.array(seq))
lang_logits = R.decoder(kv, W, enc, np.array(prompt[:1]))[0]
lang = int(np.argmax(lang_logits[tid['<|en|>']:tid['<|en|>'] + len(LANGS)]))
lsort = np.sort(lang_logits[tid['<|en|>']:tid['<|en|>'] + len(LANGS)])
lang_gap = lsort[-1] - lsort[-2]
# greedy, the suppression of zt_whisper_greedy; prompt with the detected language
sup = [1, 2, 7, 259, tid['<|translate|>']]
gprompt = [prompt[0], tid['<|en|>'] + lang, prompt[2], prompt[3]]
chain, gaps = [], []
while True:
    L = R.decoder(kv, W, enc, np.array(gprompt + chain))[-1].copy()
    L[EOT + 1:] = -np.inf
    L[sup] = -np.inf
    if not chain:
        L[[220, EOT]] = -np.inf
    o = np.argsort(-L, kind='stable')
    gaps.append(L[o[0]] - L[o[1]])
    t = int(o[0])
    if t == EOT:
        break
    chain.append(t)
    if len(chain) >= TCTX // 2:
        break


def cstr(b):
    lines, cur, prev_oct = [], '', False
    for x in b:
        ch = chr(x)
        if 32 <= x < 127 and ch not in '"\\?' and not (prev_oct and ch in '01234567'):
            cur += ch
            prev_oct = False
        else:
            cur += '\\%o' % x
            prev_oct = True
        if len(cur) >= 96:
            lines.append('    "%s"' % cur)
            cur, prev_oct = '', False
    if cur:
        lines.append('    "%s"' % cur)
    return lines


def arr(name, a, per=12):
    a = [int(v) for v in a]
    s = ['static const int32_t %s[%d] = {' % (name, len(a))]
    for i in range(0, len(a), per):
        s.append('    ' + ', '.join(str(v) for v in a[i:i + per]) + ',')
    s.append('};')
    return s


q16 = lambda a: np.round(np.asarray(a) * 65536).astype(np.int64).reshape(-1)
out = ['/* test_whisper_fixture.h -- generated by gen_whisper_fixture.py with gguf-py and numpy %s.' % np.__version__,
       ' * A tiny random Whisper GGUF (convert_whisper.py layout), and float64 reference values',
       ' * (ref_whisper.py semantics, Q16 rounded) for a test signal. Do not edit. */',
       '#include <stdint.h>',
       '#define WF_N_PCM %du' % N_PCM, '#define WF_PCM_SEED 0x5157ull',
       '#define WF_NM %du' % NM, '#define WF_D %du' % D, '#define WF_ACTX %du' % ACTX,
       '#define WF_NV %du' % NV, '#define WF_EOT %d' % EOT, '#define WF_LANG %d' % lang,
       '#define WF_GGUF_LEN %du' % len(blob),
       'static const char WF_GGUF[WF_GGUF_LEN + 1] =']
out += cstr(blob)
out[-1] += ';'
out += arr('WF_MEL', q16(mel))
out += arr('WF_ENC', q16(enc))
out += arr('WF_SEQ', seq)
out += arr('WF_LOGITS', q16(logits[len(prompt) - 1:]))
out += arr('WF_GREEDY', chain + [EOT])
open(sys.argv[1], 'w').write('\n'.join(out) + '\n')
print('gguf %d bytes; mel rms %.3f, enc rms %.3f, logit rms %.3f; lang %d (gap %.3f); greedy %s, min gap %.4f'
      % (len(blob), np.sqrt((mel ** 2).mean()), np.sqrt((enc ** 2).mean()), np.sqrt((logits ** 2).mean()),
         lang, lang_gap, chain, min(gaps)))
print(sys.argv[1], os.path.getsize(sys.argv[1]), 'bytes')
