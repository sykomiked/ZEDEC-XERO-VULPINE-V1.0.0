#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""ref_whisper.py -- float64 numpy reference of Whisper (whisper/audio.py and
whisper/model.py semantics) on the SAME GGUF zt_whisper reads, and the gap
between it and zt_asr's dumps, stage by stage.

    zt_asr MODEL.gguf in.wav --dump-mel m.bin --dump-enc e.bin --dump-logits l.bin > out.txt
    python3 bench/tensor/compare/ref_whisper.py MODEL.gguf in.wav m.bin e.bin l.bin \
        --ids $(grep zt.ids= out.txt | cut -d= -f2) [--lang en] [--json r.json]

Weights are dequantised exactly from the file (gguf-py: Q8_0 is f16 scale
times int8, exact in float32), all arithmetic is float64: the STFT with
numpy's FFT, GELU with math.erf, softmax and LayerNorm exactly. Stages:
  mel     ZXV's Q16 log-mel vs float64 log-mel of the same PCM
  enc     ZXV's encoder output vs float64 encoder on the float64 mel (end to end)
  logits  ZXV's logits at every decoding step vs the float64 decoder fed the
          same tokens (prompt + ZXV's transcript), on the float64 encoder output
For each: max |error|, relative RMS (RMS of the error / RMS of the reference),
and for logits the share of steps whose argmax (after the same suppression)
agrees and the smallest top-2 gap of the float64 logits.
"""
import argparse
import json
import math
import wave

import numpy as np
from gguf import GGUFReader
from gguf.quants import dequantize


def load(path):
    r = GGUFReader(path)

    def kv(k):
        f = r.fields[k]
        if f.types[0].name == 'ARRAY':
            if f.types[-1].name == 'STRING':
                return [bytes(f.parts[i]).decode() for i in f.data]
            return [f.parts[i][0] for i in f.data]
        return f.parts[f.data[0]][0]
    T = {t.name: t for t in r.tensors}

    def W(name):
        t = T[name]
        a = dequantize(t.data, t.tensor_type).astype(np.float64)
        return a.reshape([int(x) for x in reversed(t.shape)])
    return kv, W


def read_wav(path):
    with wave.open(path, 'rb') as f:
        assert f.getnchannels() == 1 and f.getsampwidth() == 2 and f.getframerate() == 16000, \
            'need 16 kHz mono 16-bit PCM'
        return np.frombuffer(f.readframes(f.getnframes()), dtype='<i2').astype(np.float64)


def log_mel(pcm, filters, n_samples):
    x = np.zeros(n_samples)
    n = min(len(pcm), n_samples)
    x[:n] = pcm[:n] / 32768.0
    xp = np.pad(x, (200, 200), mode='reflect')
    win = 0.5 - 0.5 * np.cos(2 * np.pi * np.arange(400) / 400)
    n_frames = n_samples // 160
    idx = np.arange(n_frames)[:, None] * 160 + np.arange(400)[None, :]
    spec = np.fft.rfft(xp[idx] * win, axis=1)
    power = spec.real ** 2 + spec.imag ** 2  # [frames][201]
    mel = power @ filters.T  # [frames][n_mels]
    lg = np.log10(np.maximum(mel, 1e-10))
    lg = np.maximum(lg, lg.max() - 8.0)
    return (lg + 4.0) / 4.0


_erf = np.frompyfunc(math.erf, 1, 1)


def gelu(x):
    return 0.5 * x * (1.0 + _erf(x / math.sqrt(2.0)).astype(np.float64))


def ln(x, g, b, eps):
    m = x.mean(-1, keepdims=True)
    v = ((x - m) ** 2).mean(-1, keepdims=True)
    return (x - m) / np.sqrt(v + eps) * g + b


def attention(q, k, v, nh, causal):
    T, d = q.shape
    hd = d // nh
    out = np.zeros_like(q)
    for h in range(nh):
        s = slice(h * hd, (h + 1) * hd)
        sc = q[:, s] @ k[:, s].T / math.sqrt(hd)
        if causal:
            off = k.shape[0] - T
            mask = np.arange(k.shape[0])[None, :] > (np.arange(T)[:, None] + off)
            sc = np.where(mask, -np.inf, sc)
        p = np.exp(sc - sc.max(-1, keepdims=True))
        p /= p.sum(-1, keepdims=True)
        out[:, s] = p @ v[:, s]
    return out


def conv1d(x, w, b, stride):
    """x [T][C], w [Cout][C*3] (column c*3 + j), padding 1"""
    T, C = x.shape
    xp = np.pad(x, ((1, 1), (0, 0)))
    To = (T + 2 - 3) // stride + 1
    cols = np.stack([xp[t * stride:t * stride + 3].T.reshape(-1) for t in range(To)])
    return cols @ w.T + b


def encoder(kv, W, mel):
    eps = float(kv('whisper.attention.layer_norm_epsilon'))
    nh = int(kv('whisper.audio.head_count'))
    x = gelu(conv1d(mel, W('enc.conv1.weight'), W('enc.conv1.bias'), 1))
    x = gelu(conv1d(x, W('enc.conv2.weight'), W('enc.conv2.bias'), 2))
    x = x + W('enc.pos')
    for l in range(int(kv('whisper.audio.block_count'))):
        p = 'enc.blk.%d.' % l
        h = ln(x, W(p + 'attn_ln.weight'), W(p + 'attn_ln.bias'), eps)
        q = h @ W(p + 'attn_q.weight').T + W(p + 'attn_q.bias')
        k = h @ W(p + 'attn_k.weight').T
        v = h @ W(p + 'attn_v.weight').T + W(p + 'attn_v.bias')
        x = x + attention(q, k, v, nh, False) @ W(p + 'attn_out.weight').T + W(p + 'attn_out.bias')
        h = ln(x, W(p + 'mlp_ln.weight'), W(p + 'mlp_ln.bias'), eps)
        h = gelu(h @ W(p + 'fc1.weight').T + W(p + 'fc1.bias'))
        x = x + h @ W(p + 'fc2.weight').T + W(p + 'fc2.bias')
    return ln(x, W('enc.ln.weight'), W('enc.ln.bias'), eps)


def decoder(kv, W, enc, ids):
    """logits at every position for the token sequence ids (teacher forcing)"""
    eps = float(kv('whisper.attention.layer_norm_epsilon'))
    nh = int(kv('whisper.text.head_count'))
    E = W('dec.token_embd.weight')
    x = E[ids] + W('dec.pos')[:len(ids)]
    for l in range(int(kv('whisper.text.block_count'))):
        p = 'dec.blk.%d.' % l
        h = ln(x, W(p + 'attn_ln.weight'), W(p + 'attn_ln.bias'), eps)
        q = h @ W(p + 'attn_q.weight').T + W(p + 'attn_q.bias')
        k = h @ W(p + 'attn_k.weight').T
        v = h @ W(p + 'attn_v.weight').T + W(p + 'attn_v.bias')
        x = x + attention(q, k, v, nh, True) @ W(p + 'attn_out.weight').T + W(p + 'attn_out.bias')
        h = ln(x, W(p + 'cross_ln.weight'), W(p + 'cross_ln.bias'), eps)
        q = h @ W(p + 'cross_q.weight').T + W(p + 'cross_q.bias')
        k = enc @ W(p + 'cross_k.weight').T
        v = enc @ W(p + 'cross_v.weight').T + W(p + 'cross_v.bias')
        x = x + attention(q, k, v, nh, False) @ W(p + 'cross_out.weight').T + W(p + 'cross_out.bias')
        h = ln(x, W(p + 'mlp_ln.weight'), W(p + 'mlp_ln.bias'), eps)
        h = gelu(h @ W(p + 'fc1.weight').T + W(p + 'fc1.bias'))
        x = x + h @ W(p + 'fc2.weight').T + W(p + 'fc2.bias')
    return ln(x, W('dec.ln.weight'), W('dec.ln.bias'), eps) @ E.T


def gap(z, ref):
    e = z - ref
    return {'max_abs': float(np.abs(e).max()),
            'rel_rms': float(np.sqrt((e * e).mean()) / max(np.sqrt((ref * ref).mean()), 1e-300)),
            'ref_rms': float(np.sqrt((ref * ref).mean())), 'ref_max': float(np.abs(ref).max())}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('model')
    ap.add_argument('wav')
    ap.add_argument('mel')
    ap.add_argument('enc')
    ap.add_argument('logits')
    ap.add_argument('--ids', default='', help="zt_asr's zt.ids (comma-separated)")
    ap.add_argument('--lang', default='en')
    ap.add_argument('--json')
    a = ap.parse_args()
    kv, W = load(a.model)
    n_mels, n_actx = int(kv('whisper.n_mels')), int(kv('whisper.audio.context_length'))
    d, n_vocab = int(kv('whisper.audio.embedding_length')), int(kv('whisper.vocab_size'))
    n_samples = 2 * n_actx * 160
    pcm = read_wav(a.wav)
    res = {}
    mel = log_mel(pcm, W('mel_filters'), n_samples)
    zm = np.fromfile(a.mel, dtype='<i4').astype(np.float64).reshape(-1, n_mels) / 65536
    res['mel'] = gap(zm, mel)
    enc = encoder(kv, W, mel)
    ze = np.fromfile(a.enc, dtype='<i4').astype(np.float64).reshape(n_actx, d) / 65536
    res['enc'] = gap(ze, enc)
    langs = kv('whisper.languages')
    tok = lambda k: int(kv('whisper.token.' + k))
    prompt = [tok('sot'), tok('lang_first') + langs.index(a.lang), tok('transcribe'), tok('no_timestamps')]
    ids = [int(t) for t in a.ids.split(',') if t.strip()]
    zl = np.fromfile(a.logits, dtype='<i4').astype(np.float64).reshape(-1, n_vocab) / 65536
    steps = zl.shape[0]
    seq = prompt + ids[:steps - 1]
    L = decoder(kv, W, enc, np.array(seq))[len(prompt) - 1:]
    res['logits'] = gap(zl, L)
    sup = set(int(x) for x in kv('whisper.suppress_tokens'))
    sup0 = set(int(x) for x in kv('whisper.begin_suppress_tokens'))
    eot = tok('eot')
    agree, mingap = 0, 1e300
    for s in range(steps):
        mask = np.zeros(n_vocab, bool)
        mask[eot + 1:] = True
        mask[list(sup)] = True
        if s == 0:
            mask[list(sup0)] = True
        lr = np.where(mask, -np.inf, L[s])
        lz = np.where(mask, -np.inf, zl[s])
        o = np.argsort(-lr, kind='stable')
        mingap = min(mingap, lr[o[0]] - lr[o[1]])
        agree += int(np.argmax(lz) == o[0])
    res['logits']['argmax_agree'] = '%d/%d' % (agree, steps)
    res['logits']['min_top2_gap_f64'] = float(mingap)
    for k, v in res.items():
        print('%-7s ' % k + '  '.join('%s=%s' % (kk, ('%.4g' % vv) if isinstance(vv, float) else vv)
                                      for kk, vv in v.items()))
    if a.json:
        json.dump(res, open(a.json, 'w'), indent=1)


if __name__ == '__main__':
    main()
