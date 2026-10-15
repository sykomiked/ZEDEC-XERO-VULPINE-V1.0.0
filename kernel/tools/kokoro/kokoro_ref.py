#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Float reference for Kokoro-82M (StyleTTS2 + ISTFTNet) in numpy, no torch.

The integer port (kernel/src/speech/speech_kokoro.c) is checked against this.
Adapted from hexgrad/kokoro (Apache-2.0): model.py, modules.py, istftnet.py.
Randomness in the source (initial harmonic phases, noise) is replaced by a
fixed generator so the same input gives the same audio.

    python3 -I kokoro_ref.py WEIGHTS.pth VOICE.pt "phonemes" out.wav
"""
import json
import math
import os
import sys
import wave

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pth_read  # noqa: E402

F = np.float64


def wn(p, name):
    v = p[name + '.weight_v'].astype(F)
    g = p[name + '.weight_g'].astype(F)
    n = np.sqrt((v * v).reshape(v.shape[0], -1).sum(1)).reshape((-1,) + (1,) * (v.ndim - 1))
    return v * g / n


def conv1d(x, w, b=None, stride=1, pad=0, dil=1, groups=1):
    C, T = x.shape
    O, Cg, K = w.shape
    xp = np.pad(x, ((0, 0), (pad, pad)))
    Tout = (T + 2 * pad - dil * (K - 1) - 1) // stride + 1
    y = np.zeros((O, Tout), F)
    og = O // groups
    for g in range(groups):
        xs = xp[g * Cg:(g + 1) * Cg]
        for k in range(K):
            cols = xs[:, k * dil: k * dil + stride * (Tout - 1) + 1: stride]
            y[g * og:(g + 1) * og] += w[g * og:(g + 1) * og, :, k] @ cols
    if b is not None:
        y += b[:, None]
    return y


def conv_t1d(x, w, b=None, stride=1, pad=0, out_pad=0, groups=1):
    C, T = x.shape
    Cin, Og, K = w.shape
    Tfull = (T - 1) * stride + K
    O = Og * groups
    y = np.zeros((O, Tfull + out_pad), F)
    cg = Cin // groups
    for g in range(groups):
        xs = x[g * cg:(g + 1) * cg]
        for k in range(K):
            contrib = w[g * cg:(g + 1) * cg, :, k].T @ xs  # [Og, T]
            y[g * Og:(g + 1) * Og, k: k + stride * (T - 1) + 1: stride] += contrib
    Tout = (T - 1) * stride - 2 * pad + K + out_pad
    y = y[:, pad: pad + Tout]
    if b is not None:
        y += b[:, None]
    return y


def lrelu(x, a):
    return np.where(x > 0, x, a * x)


def sig(x):
    return 1 / (1 + np.exp(-x))


def lstm1(x, Wi, Wh, bi, bh):
    T = x.shape[0]
    H = Wh.shape[1]
    h = np.zeros(H, F)
    c = np.zeros(H, F)
    out = np.zeros((T, H), F)
    xi = x @ Wi.T + bi + bh
    for t in range(T):
        z = xi[t] + Wh @ h
        i, f, g, o = sig(z[:H]), sig(z[H:2 * H]), np.tanh(z[2 * H:3 * H]), sig(z[3 * H:])
        c = f * c + i * g
        h = o * np.tanh(c)
        out[t] = h
    return out


def bilstm(p, name, x):
    g = lambda s: p[name + s].astype(F)
    fw = lstm1(x, g('.weight_ih_l0'), g('.weight_hh_l0'), g('.bias_ih_l0'), g('.bias_hh_l0'))
    bw = lstm1(x[::-1], g('.weight_ih_l0_reverse'), g('.weight_hh_l0_reverse'),
               g('.bias_ih_l0_reverse'), g('.bias_hh_l0_reverse'))[::-1]
    return np.concatenate([fw, bw], 1)


def lnorm(x, w=None, b=None, eps=1e-5):  # over the last axis
    m = x.mean(-1, keepdims=True)
    v = ((x - m) ** 2).mean(-1, keepdims=True)
    y = (x - m) / np.sqrt(v + eps)
    if w is not None:
        y = y * w + b
    return y


def adain(p, name, x, s):  # x [C,T]
    h = p[name + '.fc.weight'].astype(F) @ s + p[name + '.fc.bias'].astype(F)
    C = x.shape[0]
    gamma, beta = h[:C], h[C:]
    m = x.mean(1, keepdims=True)
    v = ((x - m) ** 2).mean(1, keepdims=True)
    return (1 + gamma[:, None]) * (x - m) / np.sqrt(v + 1e-5) + beta[:, None]


def adain_resblk(p, name, x, s, upsample=False):
    def has(k):
        return name + k in p
    r = adain(p, name + '.norm1', x, s)
    r = lrelu(r, 0.2)
    if upsample:
        r = conv_t1d(r, wn(p, name + '.pool'), p[name + '.pool.bias'].astype(F), 2, 1, 1,
                     groups=r.shape[0])
    r = conv1d(r, wn(p, name + '.conv1'), p[name + '.conv1.bias'].astype(F), pad=1)
    r = adain(p, name + '.norm2', r, s)
    r = lrelu(r, 0.2)
    r = conv1d(r, wn(p, name + '.conv2'), p[name + '.conv2.bias'].astype(F), pad=1)
    sc = np.repeat(x, 2, axis=1) if upsample else x
    if has('.conv1x1.weight_v'):
        sc = conv1d(sc, wn(p, name + '.conv1x1'))
    return (r + sc) / math.sqrt(2)


def gelu_new(x):
    return 0.5 * x * (1 + np.tanh(math.sqrt(2 / math.pi) * (x + 0.044715 * x ** 3)))


def albert(p, ids):
    g = lambda n: p['module.' + n].astype(F)
    T = len(ids)
    e = g('embeddings.word_embeddings.weight')[ids] + g('embeddings.position_embeddings.weight')[:T] \
        + g('embeddings.token_type_embeddings.weight')[0]
    e = lnorm(e, g('embeddings.LayerNorm.weight'), g('embeddings.LayerNorm.bias'), 1e-12)
    x = e @ g('encoder.embedding_hidden_mapping_in.weight').T + g('encoder.embedding_hidden_mapping_in.bias')
    L = 'encoder.albert_layer_groups.0.albert_layers.0.'
    for _ in range(12):
        q = x @ g(L + 'attention.query.weight').T + g(L + 'attention.query.bias')
        k = x @ g(L + 'attention.key.weight').T + g(L + 'attention.key.bias')
        v = x @ g(L + 'attention.value.weight').T + g(L + 'attention.value.bias')
        q, k, v = (a.reshape(T, 12, 64).transpose(1, 0, 2) for a in (q, k, v))
        sc = q @ k.transpose(0, 2, 1) / 8.0
        sc = np.exp(sc - sc.max(-1, keepdims=True))
        sc /= sc.sum(-1, keepdims=True)
        ctx = (sc @ v).transpose(1, 0, 2).reshape(T, 768)
        a = ctx @ g(L + 'attention.dense.weight').T + g(L + 'attention.dense.bias')
        x = lnorm(x + a, g(L + 'attention.LayerNorm.weight'), g(L + 'attention.LayerNorm.bias'), 1e-12)
        f = gelu_new(x @ g(L + 'ffn.weight').T + g(L + 'ffn.bias'))
        f = f @ g(L + 'ffn_output.weight').T + g(L + 'ffn_output.bias')
        x = lnorm(x + f, g(L + 'full_layer_layer_norm.weight'), g(L + 'full_layer_layer_norm.bias'), 1e-12)
    return x  # [T, 768]


class Rng:
    """xorshift64* and a sum of 12 uniforms: the fixed stand-in for torch's
    random numbers, the same integers as speech_kokoro's."""
    def __init__(self, seed=0x9E3779B97F4A7C15):
        self.s = seed

    def u24(self):
        s = self.s
        s ^= (s >> 12)
        s ^= (s << 25) & 0xFFFFFFFFFFFFFFFF
        s ^= (s >> 27)
        self.s = s
        return ((s * 0x2545F4914F6CDD1D) & 0xFFFFFFFFFFFFFFFF) >> 40

    def normal_q16(self, n):  # (sum of 12 uniforms) - 6, in Q16, exactly
        out = np.zeros(n, np.int64)
        for i in range(n):
            t = 0
            for _ in range(12):
                t += self.u24()
            out[i] = (t >> 8) - (6 << 16)
        return out


def interp_linear(x, out_len, scale):  # F.interpolate(mode=linear, align_corners=False), x [C,T]
    T = x.shape[1]
    i = np.arange(out_len, dtype=F)
    src = (i + 0.5) / scale - 0.5
    src = np.maximum(src, 0)
    i0 = np.floor(src).astype(int)
    i0 = np.minimum(i0, T - 1)
    i1 = np.minimum(i0 + 1, T - 1)
    w = src - i0
    return x[:, i0] * (1 - w) + x[:, i1] * w


def hann(n):
    return 0.5 - 0.5 * np.cos(2 * math.pi * np.arange(n) / n)


def stft(x, n_fft=20, hop=5):
    pad = n_fft // 2
    xp = np.pad(x, (pad, pad), mode='reflect')
    frames = 1 + (len(xp) - n_fft) // hop
    w = hann(n_fft)
    idx = np.arange(n_fft)[None, :] + hop * np.arange(frames)[:, None]
    spec = np.fft.rfft(xp[idx] * w, axis=1)  # [frames, 11]
    return np.abs(spec).T, np.angle(spec).T


def istft(mag, phase, n_fft=20, hop=5):
    spec = (mag * np.exp(1j * phase)).T  # [frames, 11]
    frames = spec.shape[0]
    fr = np.fft.irfft(spec, n=n_fft, axis=1)
    w = hann(n_fft)
    L = n_fft + hop * (frames - 1)
    y = np.zeros(L, F)
    env = np.zeros(L, F)
    for t in range(frames):
        y[t * hop:t * hop + n_fft] += fr[t] * w
        env[t * hop:t * hop + n_fft] += w * w
    pad = n_fft // 2
    y = y[pad:L - pad]
    env = env[pad:L - pad]
    return y / np.where(env > 1e-11, env, 1)


def generator(p, x, s, f0_curve, rng):
    G = 'module.generator.'
    up = 300
    f0 = np.repeat(f0_curve, up)  # nearest
    L = len(f0)
    harm = np.arange(1, 10)[:, None] * f0[None, :]  # [9, L]
    rad = (harm / 24000.0) % 1
    # torch.rand initial phases (0 for the fundamental) -> fixed: zeros
    down = interp_linear(rad, L // up, 1 / up)
    phase = np.cumsum(down, axis=1) * 2 * math.pi
    phase = interp_linear(phase * up, L, up)
    sines = np.sin(phase) * 0.1
    uv = (f0 > 10).astype(F)
    noise_amp = uv * 0.003 + (1 - uv) * 0.1 / 3
    noise = noise_amp[None, :] * (rng.normal_q16(9 * L).reshape(L, 9).T / 65536.0)
    sines = sines * uv[None, :] + noise
    har_src = np.tanh(p[G + 'm_source.l_linear.weight'].astype(F) @ sines
                      + p[G + 'm_source.l_linear.bias'].astype(F))[0]
    mag, ph = stft(har_src)
    har = np.concatenate([mag, ph], 0)  # [22, frames]
    rates, kernels = [10, 6], [20, 12]
    for i in range(2):
        x = lrelu(x, 0.1)
        if i == 0:
            xs = conv1d(har, p[G + 'noise_convs.0.weight'].astype(F), p[G + 'noise_convs.0.bias'].astype(F),
                        stride=6, pad=(6 + 1) // 2)
        else:
            xs = conv1d(har, p[G + 'noise_convs.1.weight'].astype(F), p[G + 'noise_convs.1.bias'].astype(F))
        xs = resblock1(p, G + 'noise_res.%d' % i, xs, s, [7, 11][i])
        u, k = rates[i], kernels[i]
        x = conv_t1d(x, wn(p, G + 'ups.%d' % i), p[G + 'ups.%d.bias' % i].astype(F), u, (k - u) // 2)
        if i == 1:
            x = np.concatenate([x[:, 1:2], x], 1)  # ReflectionPad1d((1, 0))
        x = x + xs
        acc = None
        for j, ks in enumerate([3, 7, 11]):
            r = resblock1(p, G + 'resblocks.%d' % (i * 3 + j), x, s, ks)
            acc = r if acc is None else acc + r
        x = acc / 3
    x = lrelu(x, 0.01)
    x = conv1d(x, wn(p, G + 'conv_post'), p[G + 'conv_post.bias'].astype(F), pad=3)
    spec = np.exp(x[:11])
    phase = np.sin(x[11:])
    return istft(spec, phase)


def resblock1(p, name, x, s, k):
    for j, d in enumerate([1, 3, 5]):
        xt = adain(p, name + '.adain1.%d' % j, x, s)
        a1 = p[name + '.alpha1.%d' % j].astype(F).reshape(-1, 1)
        xt = xt + (1 / a1) * np.sin(a1 * xt) ** 2
        xt = conv1d(xt, wn(p, name + '.convs1.%d' % j), p[name + '.convs1.%d.bias' % j].astype(F),
                    pad=(k * d - d) // 2, dil=d)
        xt = adain(p, name + '.adain2.%d' % j, xt, s)
        a2 = p[name + '.alpha2.%d' % j].astype(F).reshape(-1, 1)
        xt = xt + (1 / a2) * np.sin(a2 * xt) ** 2
        xt = conv1d(xt, wn(p, name + '.convs2.%d' % j), p[name + '.convs2.%d.bias' % j].astype(F),
                    pad=(k - 1) // 2)
        x = xt + x
    return x


def synth(m, vocab, phonemes, ref_s, speed=1.0, trace=None):
    ids = [0] + [vocab[c] for c in phonemes if c in vocab] + [0]
    T = len(ids)
    bert = m['bert']
    pr = m['predictor']
    te = m['text_encoder']
    dec = m['decoder']
    s_pro = ref_s[128:].astype(F)
    s_dec = ref_s[:128].astype(F)
    b = albert(bert, ids)
    d_en = b @ m['bert_encoder']['module.weight'].astype(F).T + m['bert_encoder']['module.bias'].astype(F)
    # DurationEncoder
    x = np.concatenate([d_en, np.repeat(s_pro[None], T, 0)], 1)  # [T, 640]
    for l in range(3):
        h = bilstm(pr, 'module.text_encoder.lstms.%d' % (2 * l), x)  # [T,512]
        nm = 'module.text_encoder.lstms.%d' % (2 * l + 1)
        hh = pr[nm + '.fc.weight'].astype(F) @ s_pro + pr[nm + '.fc.bias'].astype(F)
        h = (1 + hh[:512]) * lnorm(h) + hh[512:]
        x = np.concatenate([h, np.repeat(s_pro[None], T, 0)], 1)
    d = x  # [T, 640]
    xl = bilstm(pr, 'module.lstm', d)
    dur = xl @ pr['module.duration_proj.linear_layer.weight'].astype(F).T + \
        pr['module.duration_proj.linear_layer.bias'].astype(F)
    dur = sig(dur).sum(-1) / speed
    pred = np.maximum(np.round(dur), 1).astype(int)
    Fr = int(pred.sum())
    aln = np.zeros((T, Fr), F)
    pos = 0
    for i, n in enumerate(pred):
        aln[i, pos:pos + n] = 1
        pos += n
    en = d.T @ aln  # [640, Fr]
    sh = bilstm(pr, 'module.shared', en.T).T  # [512, Fr]
    f0 = sh
    for k, upd in ((0, False), (1, True), (2, False)):
        f0 = adain_resblk(pr, 'module.F0.%d' % k, f0, s_pro, upd)
    f0 = conv1d(f0, pr['module.F0_proj.weight'].astype(F), pr['module.F0_proj.bias'].astype(F))[0]
    nn_ = sh
    for k, upd in ((0, False), (1, True), (2, False)):
        nn_ = adain_resblk(pr, 'module.N.%d' % k, nn_, s_pro, upd)
    nn_ = conv1d(nn_, pr['module.N_proj.weight'].astype(F), pr['module.N_proj.bias'].astype(F))[0]
    # text encoder
    e = te['module.embedding.weight'].astype(F)[ids].T  # [512, T]
    for c in range(3):
        nm = 'module.cnn.%d' % c
        e = conv1d(e, wn(te, nm + '.0'), te[nm + '.0.bias'].astype(F), pad=2)
        e = lnorm(e.T, te[nm + '.1.gamma'].astype(F), te[nm + '.1.beta'].astype(F)).T
        e = lrelu(e, 0.2)
    t_en = bilstm(te, 'module.lstm', e.T).T  # [512, T]
    asr = t_en @ aln  # [512, Fr]
    # decoder
    D = 'module.'
    F0 = conv1d(f0[None], wn(dec, D + 'F0_conv'), dec[D + 'F0_conv.bias'].astype(F), 2, 1)
    N = conv1d(nn_[None], wn(dec, D + 'N_conv'), dec[D + 'N_conv.bias'].astype(F), 2, 1)
    x = np.concatenate([asr, F0, N], 0)
    x = adain_resblk(dec, D + 'encode', x, s_dec)
    asr_res = conv1d(asr, wn(dec, D + 'asr_res.0'), dec[D + 'asr_res.0.bias'].astype(F))
    res = True
    for k in range(4):
        if res:
            x = np.concatenate([x, asr_res, F0, N], 0)
        x = adain_resblk(dec, D + 'decode.%d' % k, x, s_dec, upsample=(k == 3))
        if k == 3:
            res = False
    audio = generator(dec, x, s_dec, f0, Rng())
    if trace is not None:
        trace.update(ids=ids, dur=pred, f0=f0, n=nn_, asr=asr)
    return audio


def write_wav(path, audio, rate=24000):
    a = np.clip(audio, -1, 1)
    pcm = (a * 32767).astype('<i2').tobytes()
    with wave.open(path, 'wb') as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(pcm)


def main():
    wpath, vpath, ph, out = sys.argv[1:5]
    cfg = json.load(open(os.path.join(os.path.dirname(wpath), 'config.json'), encoding='utf-8'))
    m = pth_read.load(wpath)
    pack = pth_read.load(vpath)  # [510, 1, 256]
    ref = pack[len(ph) - 1, 0]
    tr = {}
    audio = synth(m, cfg['vocab'], ph, ref, trace=tr)
    print('tokens', len(tr['ids']), 'frames', int(tr['dur'].sum()), 'samples', len(audio),
          'peak %.3f' % np.abs(audio).max())
    print('dur', ' '.join(str(int(d)) for d in tr['dur']))
    print('f0', ' '.join('%.0f' % v for v in tr['f0'][::4]))
    write_wav(out, audio)


if __name__ == '__main__':
    main()
