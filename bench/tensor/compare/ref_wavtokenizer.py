#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# Float64 reference of the WavTokenizer decoder (llama.cpp "wavtokenizer-dec" GGUF) and
# llama.cpp's OuteTTS iSTFT, compared with the integer engine (zt_tts, T26) on the SAME
# code ids. Weights are dequantized exactly from the file (gguf-py); all arithmetic is
# float64. GELU is the tanh form llama.cpp uses; the inverse DFT is llama.cpp's irfft
# (sum over the 641 bins of Re(X e^{i theta}), divided by 641); PCM is round(32767 x),
# clipped, with the first SKIP samples zeroed, as zt_tts writes it.
#
# usage: ref_wavtokenizer.py DECODER.gguf CODES ZXV.wav [--spec ZXV.spec] [--skip 6000]
#                            [--llama LLAMA.wav] [--out REF.wav] [--json FILE]
#   CODES     whitespace-separated code ids (zt_tts --codes-out)
#   ZXV.wav   zt_tts's output for those codes
#   --spec    zt_tts --spec-out: also compare the 1282-value decoder output per frame
#   --llama   llama.cpp's WAV for the same codes: compared with the float64 PCM too
# Prints the spectrogram error (log-magnitude and phase halves), and for PCM: max |error|
# in int16 steps, RMS error, and SNR in dB (signal = the float64 PCM), over samples
# after SKIP.
import sys, json, argparse, numpy as np
from gguf import GGUFReader
from gguf.quants import dequantize

NFFT, HOP, NBIN = 1280, 320, 641


def load(path):
    r = GGUFReader(path)
    T = {t.name: t for t in r.tensors}

    def kv(k):
        f = r.fields[k]
        v = [f.parts[i][0] for i in f.data]
        return v[0] if len(v) == 1 else v

    def W(name):
        t = T[name]
        a = dequantize(t.data, t.tensor_type).astype(np.float64)
        return a.reshape([int(x) for x in reversed(t.shape)])

    P = 'wavtokenizer-dec.'
    cfg = dict(n_conv=int(kv(P + 'convnext.block_count')), groups=int(kv(P + 'attention.group_norm_groups')),
               eps_ln=float(kv(P + 'attention.layer_norm_epsilon')),
               eps_gn=float(kv(P + 'attention.group_norm_epsilon')))
    return W, cfg


def gelu_tanh(v): return 0.5 * v * (1 + np.tanh(np.sqrt(2 / np.pi) * (v + 0.044715 * v ** 3)))
def silu(v): return v / (1 + np.exp(-v))


def decoder(W, cfg, codes):
    """codes -> [T][1282] float64, llama.cpp's wavtokenizer-dec graph (channels last)."""
    G, e_ln, e_gn = cfg['groups'], cfg['eps_ln'], cfg['eps_gn']

    def conv(x, name, K):  # x [T][C]; weight [C_out][C_in][K] after reshape
        w = W(name + '.weight'); b = W(name + '.bias').reshape(-1)
        Tn, C = x.shape; p = K // 2
        xp = np.concatenate([np.zeros((p, C)), x, np.zeros((p, C))])
        out = np.zeros((Tn, w.shape[0]))
        for j in range(K): out += xp[j:j + Tn] @ w[:, :, j].T
        return out + b

    def gn(x, name):
        g = W(name + '.weight').reshape(-1); b = W(name + '.bias').reshape(-1)
        Tn, C = x.shape; xg = x.reshape(Tn, G, C // G)
        mu = xg.mean(axis=(0, 2), keepdims=True); var = ((xg - mu) ** 2).mean(axis=(0, 2), keepdims=True)
        return ((xg - mu) / np.sqrt(var + e_gn)).reshape(Tn, C) * g + b

    def ln(x, name):
        g = W(name + '.weight').reshape(-1); b = W(name + '.bias').reshape(-1)
        mu = x.mean(-1, keepdims=True); c = x - mu
        return c / np.sqrt((c * c).mean(-1, keepdims=True) + e_ln) * g + b

    E = W('token_embd.weight')
    x = conv(E[np.asarray(codes)], 'conv1d', 7)
    for i in range(6):
        p = 'posnet.%d.' % i
        if i in (0, 1, 3, 4):
            h = conv(silu(gn(x, p + 'norm1')), p + 'conv1', 3)
            x = x + conv(silu(gn(h, p + 'norm2')), p + 'conv2', 3)
        elif i == 2:
            h = gn(x, p + 'attn_norm')
            q, k, v = (conv(h, p + n, 1) for n in ('attn_q', 'attn_k', 'attn_v'))
            s = q @ k.T / np.sqrt(x.shape[1])
            s = np.exp(s - s.max(-1, keepdims=True)); s /= s.sum(-1, keepdims=True)
            x = x + conv(s @ v, p + 'attn_output', 1)
        else:
            x = gn(x, p + 'attn_norm')
    x = ln(x, 'token_embd_norm')
    for l in range(cfg['n_conv']):
        p = 'convnext.%d.' % l
        w = W(p + 'dw.weight').reshape(-1, 7); b = W(p + 'dw.bias').reshape(-1)
        Tn = x.shape[0]; xp = np.concatenate([np.zeros((3, x.shape[1])), x, np.zeros((3, x.shape[1]))])
        h = sum(xp[j:j + Tn] * w[:, j] for j in range(7)) + b
        h = ln(h, p + 'norm')
        h = gelu_tanh(h @ W(p + 'pw1.weight').T + W(p + 'pw1.bias'))
        h = h @ W(p + 'pw2.weight').T + W(p + 'pw2.bias')
        x = x + h * W(p + 'gamma.weight')
    x = ln(x, 'output_norm')
    return x @ W('output.weight').T + W('output.bias')


def istft(spec, skip):
    """llama.cpp's embd_to_audio, then round(32767 x) clipped to int16."""
    n = spec.shape[0]
    mag = np.minimum(np.exp(spec[:, :NBIN]), 100.0)
    X = mag * np.exp(1j * spec[:, NBIN:])
    k = np.arange(NFFT)[:, None]; m = np.arange(NBIN)[None, :]
    tw = np.exp(2j * np.pi * ((k * m) % NFFT) / NFFT)      # [NFFT][NBIN]
    y = (X @ tw.T).real / NBIN                              # [n][NFFT]
    hann = 0.5 * (1 - np.cos(2 * np.pi * np.arange(NFFT) / NFFT))
    L = (n - 1) * HOP + NFFT
    acc = np.zeros(L); env = np.zeros(L)
    for f in range(n):
        acc[f * HOP:f * HOP + NFFT] += y[f] * hann
        env[f * HOP:f * HOP + NFFT] += hann * hann
    pad = (NFFT - HOP) // 2
    a = acc[pad:L - pad] / env[pad:L - pad]
    a[:skip] = 0
    return a, np.clip(np.round(a * 32767), -32768, 32767)


def read_wav(path):
    b = open(path, 'rb').read()
    i = b.index(b'data')
    return np.frombuffer(b[i + 8:], dtype='<i2').astype(np.float64)


def write_wav(path, pcm):
    d = pcm.astype('<i2').tobytes()
    h = b'RIFF' + (36 + len(d)).to_bytes(4, 'little') + b'WAVEfmt ' + (16).to_bytes(4, 'little')
    h += (1).to_bytes(2, 'little') + (1).to_bytes(2, 'little') + (24000).to_bytes(4, 'little')
    h += (48000).to_bytes(4, 'little') + (2).to_bytes(2, 'little') + (16).to_bytes(2, 'little')
    open(path, 'wb').write(h + b'data' + len(d).to_bytes(4, 'little') + d)


def cmp_pcm(got, ref, skip):
    n = min(len(got), len(ref)); g, r = got[skip:n], ref[skip:n]
    e = g - r
    snr = 10 * np.log10((r * r).sum() / max((e * e).sum(), 1e-30))
    return dict(n=int(n - skip), max_abs=float(np.abs(e).max()), rms=float(np.sqrt((e * e).mean())),
                ref_rms=float(np.sqrt((r * r).mean())), snr_db=float(snr))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('dec'); ap.add_argument('codes'); ap.add_argument('wav')
    ap.add_argument('--spec'); ap.add_argument('--llama'); ap.add_argument('--out')
    ap.add_argument('--json'); ap.add_argument('--skip', type=int, default=6000)
    a = ap.parse_args()
    codes = [int(c) for c in open(a.codes).read().split()]
    W, cfg = load(a.dec)
    spec = decoder(W, cfg, codes)
    res = dict(n_codes=len(codes))
    if a.spec:
        z = np.fromfile(a.spec, dtype='<i4').astype(np.float64).reshape(len(codes), -1) / 65536
        d = z - spec
        res['spec_logmag_max_abs'] = float(np.abs(d[:, :NBIN]).max())
        res['spec_logmag_rms'] = float(np.sqrt((d[:, :NBIN] ** 2).mean()))
        res['spec_phase_max_abs'] = float(np.abs(d[:, NBIN:]).max())
        res['spec_phase_rms'] = float(np.sqrt((d[:, NBIN:] ** 2).mean()))
        res['spec_ref_rms'] = float(np.sqrt((spec ** 2).mean()))
    _, ref = istft(spec, a.skip)
    if a.out: write_wav(a.out, ref)
    res['zxv_vs_f64'] = cmp_pcm(read_wav(a.wav), ref, a.skip)
    if a.llama: res['llama_vs_f64'] = cmp_pcm(read_wav(a.llama), ref, a.skip)
    if a.spec:  # the integer iSTFT alone: float64 iSTFT of ZXV's own spectrogram
        _, refz = istft(z, a.skip)
        res['istft_only'] = cmp_pcm(read_wav(a.wav), refz, a.skip)
    print(json.dumps(res, indent=1))
    if a.json: open(a.json, 'w').write(json.dumps(res, indent=1))


if __name__ == '__main__':
    main()
