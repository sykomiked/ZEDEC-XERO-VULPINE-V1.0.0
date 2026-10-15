#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Convert Kokoro-82M (kokoro-v1_0.pth + config.json + voice packs) to the
GGUF that kernel/src/tensor/zt_kokoro.c loads. No torch: pth_read.py.

    python3 -I kokoro2gguf.py DIR out.gguf [voice ...]

DIR holds config.json, kokoro-v1_0.pth and voices/<name>.pt (or <name>.pt).
Weight norm is folded in; convolutions become [C_out][K * C_in] matrices with
column j * C_in + c (tap-major, kokoro.conv_order 1, so each tap's input row
is copied whole); transposed convolutions are split into their stride
phases, so each phase is a plain matrix; LSTM biases are
summed (b_ih + b_hh). Voices are stored as voice.<name> [510][256].
Weights stay out of git: this file is the recipe.
"""
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pth_read  # noqa: E402
import gguf  # noqa: E402

F32 = np.float32


def wn(p, name):
    v = p[name + '.weight_v'].astype(np.float64)
    g = p[name + '.weight_g'].astype(np.float64)
    n = np.sqrt((v * v).reshape(v.shape[0], -1).sum(1)).reshape((-1,) + (1,) * (v.ndim - 1))
    return v * g / n


class Out:
    def __init__(self, path):
        self.w = gguf.GGUFWriter(path, 'kokoro')
        self.names = set()

    def t(self, name, arr):
        assert name not in self.names, name
        self.names.add(name)
        self.w.add_tensor(name, np.ascontiguousarray(np.asarray(arr, F32)))


def conv(o, name, w, b=None):  # w [O, C, K] -> [O][K*C], column j * C + c
    o.t(name + '.weight', np.transpose(w, (0, 2, 1)).reshape(w.shape[0], -1))
    if b is not None:
        o.t(name + '.bias', b)


def lstm(o, name, p, src):
    for tag, suf in (('fw', ''), ('bw', '_reverse')):
        o.t('%s.%s.wi' % (name, tag), p[src + '.weight_ih_l0' + suf])
        o.t('%s.%s.wh' % (name, tag), p[src + '.weight_hh_l0' + suf])
        o.t('%s.%s.b' % (name, tag), p[src + '.bias_ih_l0' + suf].astype(np.float64)
            + p[src + '.bias_hh_l0' + suf])


def fc(o, name, p, src):
    o.t(name + '.weight', p[src + '.weight'])
    o.t(name + '.bias', p[src + '.bias'])


def resblk(o, name, p, src, upsample=False):
    fc(o, name + '.norm1', p, src + '.norm1.fc')
    fc(o, name + '.norm2', p, src + '.norm2.fc')
    conv(o, name + '.conv1', wn(p, src + '.conv1'), p[src + '.conv1.bias'])
    conv(o, name + '.conv2', wn(p, src + '.conv2'), p[src + '.conv2.bias'])
    if src + '.conv1x1.weight_v' in p:
        conv(o, name + '.conv1x1', wn(p, src + '.conv1x1'))
    if upsample:  # depthwise ConvTranspose1d k3 s2 p1 op1, weight [C, 1, 3]
        o.t(name + '.pool.weight', wn(p, src + '.pool')[:, 0, :])
        o.t(name + '.pool.bias', p[src + '.pool.bias'])


def resblock1(o, name, p, src):
    for j in range(3):
        for a in (1, 2):
            fc(o, '%s.%d.adain%d' % (name, j, a), p, '%s.adain%d.%d.fc' % (src, a, j))
            o.t('%s.%d.alpha%d' % (name, j, a), p['%s.alpha%d.%d' % (src, a, j)].reshape(-1))
            conv(o, '%s.%d.convs%d' % (name, j, a), wn(p, '%s.convs%d.%d' % (src, a, j)),
                 p['%s.convs%d.%d.bias' % (src, a, j)])


def conv_t_phases(o, name, w, b, stride, pad):
    """ConvTranspose1d weight [C_in, C_out, K] -> stride phases. Output t
    (phase p = t mod stride) uses taps k0 + m * stride (k0 = (p + pad) mod
    stride) on inputs ib - m, ib = (t + pad - k0) / stride. Phase p is the
    matrix [C_out][taps * C_in], column m * C_in + c."""
    Cin, Cout, K = w.shape
    taps = K // stride
    assert taps * stride == K
    for ph in range(stride):
        k0 = (ph + pad) % stride
        m = np.zeros((Cout, taps, Cin))
        for t in range(taps):
            m[:, t, :] = w[:, :, k0 + t * stride].T
        o.t('%s.p%d.weight' % (name, ph), m.reshape(Cout, taps * Cin))
    o.t(name + '.bias', b)


def main():
    d, out = sys.argv[1], sys.argv[2]
    voices = sys.argv[3:] or ['af_heart']
    cfg = json.load(open(os.path.join(d, 'config.json'), encoding='utf-8'))
    m = pth_read.load(os.path.join(d, 'kokoro-v1_0.pth'))
    o = Out(out)
    w = o.w
    w.add_name('Kokoro-82M')
    w.add_string('general.license', 'apache-2.0')
    w.add_string('general.source.url', 'https://huggingface.co/hexgrad/Kokoro-82M')
    w.add_string('kokoro.credits', 'Kokoro-82M by hexgrad, Apache-2.0. Training data includes '
                 'Koniwa (CC BY 3.0) and SIWIS (CC BY 4.0).')
    w.add_uint32('kokoro.n_token', cfg['n_token'])
    w.add_uint32('kokoro.style_dim', cfg['style_dim'])
    w.add_uint32('kokoro.hidden_dim', cfg['hidden_dim'])
    w.add_uint32('kokoro.max_dur', cfg['max_dur'])
    w.add_uint32('kokoro.sample_rate', 24000)
    w.add_uint32('kokoro.conv_order', 1)
    sym = [''] * cfg['n_token']
    for k, v in cfg['vocab'].items():
        sym[v] = k
    w.add_array('kokoro.vocab', sym)
    w.add_array('kokoro.voices', voices)

    b = m['bert']
    B = 'module.'
    L = B + 'encoder.albert_layer_groups.0.albert_layers.0.'
    o.t('bert.word_embd', b[B + 'embeddings.word_embeddings.weight'])
    o.t('bert.pos_embd', b[B + 'embeddings.position_embeddings.weight'])
    o.t('bert.type_embd', b[B + 'embeddings.token_type_embeddings.weight'][0])
    fc(o, 'bert.emb_norm', b, B + 'embeddings.LayerNorm')
    fc(o, 'bert.map', b, B + 'encoder.embedding_hidden_mapping_in')
    for nm, src in (('q', 'attention.query'), ('k', 'attention.key'), ('v', 'attention.value'),
                    ('o', 'attention.dense'), ('attn_norm', 'attention.LayerNorm'),
                    ('ffn_up', 'ffn'), ('ffn_down', 'ffn_output'),
                    ('ffn_norm', 'full_layer_layer_norm')):
        fc(o, 'bert.' + nm, b, L + src)
    fc(o, 'bert_enc', m['bert_encoder'], 'module')

    p = m['predictor']
    P = 'module.'
    for l in range(3):
        lstm(o, 'pred.dur_enc.%d.lstm' % l, p, P + 'text_encoder.lstms.%d' % (2 * l))
        fc(o, 'pred.dur_enc.%d.norm' % l, p, P + 'text_encoder.lstms.%d.fc' % (2 * l + 1))
    lstm(o, 'pred.lstm', p, P + 'lstm')
    fc(o, 'pred.dur_proj', p, P + 'duration_proj.linear_layer')
    lstm(o, 'pred.shared', p, P + 'shared')
    for br in ('F0', 'N'):
        for k in range(3):
            resblk(o, 'pred.%s.%d' % (br, k), p, P + '%s.%d' % (br, k), upsample=(k == 1))
        o.t('pred.%s_proj.weight' % br, p[P + '%s_proj.weight' % br].reshape(1, -1))
        o.t('pred.%s_proj.bias' % br, p[P + '%s_proj.bias' % br])

    t = m['text_encoder']
    o.t('text.embd', t['module.embedding.weight'])
    for c in range(3):
        conv(o, 'text.cnn.%d.conv' % c, wn(t, 'module.cnn.%d.0' % c), t['module.cnn.%d.0.bias' % c])
        o.t('text.cnn.%d.norm.weight' % c, t['module.cnn.%d.1.gamma' % c])
        o.t('text.cnn.%d.norm.bias' % c, t['module.cnn.%d.1.beta' % c])
    lstm(o, 'text.lstm', t, 'module.lstm')

    q = m['decoder']
    D = 'module.'
    for nm in ('F0_conv', 'N_conv'):
        o.t('dec.%s.weight' % nm, wn(q, D + nm).reshape(-1))
        o.t('dec.%s.bias' % nm, q[D + nm + '.bias'])
    conv(o, 'dec.asr_res', wn(q, D + 'asr_res.0'), q[D + 'asr_res.0.bias'])
    resblk(o, 'dec.encode', q, D + 'encode')
    for k in range(4):
        resblk(o, 'dec.decode.%d' % k, q, D + 'decode.%d' % k, upsample=(k == 3))
    G = D + 'generator.'
    o.t('gen.l_linear.weight', q[G + 'm_source.l_linear.weight'].reshape(-1))
    o.t('gen.l_linear.bias', q[G + 'm_source.l_linear.bias'])
    conv(o, 'gen.noise_conv.0', q[G + 'noise_convs.0.weight'], q[G + 'noise_convs.0.bias'])
    conv(o, 'gen.noise_conv.1', q[G + 'noise_convs.1.weight'], q[G + 'noise_convs.1.bias'])
    for i in range(2):
        resblock1(o, 'gen.noise_res.%d' % i, q, G + 'noise_res.%d' % i)
    for i, (u, k) in enumerate(zip([10, 6], [20, 12])):
        conv_t_phases(o, 'gen.ups.%d' % i, wn(q, G + 'ups.%d' % i), q[G + 'ups.%d.bias' % i], u,
                      (k - u) // 2)
    for r in range(6):
        resblock1(o, 'gen.res.%d' % r, q, G + 'resblocks.%d' % r)
    conv(o, 'gen.post', wn(q, G + 'conv_post'), q[G + 'conv_post.bias'])

    for v in voices:
        vp = os.path.join(d, 'voices', v + '.pt')
        if not os.path.exists(vp):
            vp = os.path.join(d, v + '.pt')
        pack = pth_read.load(vp)
        o.t('voice.' + v, pack.reshape(pack.shape[0], -1))

    w.write_header_to_file()
    w.write_kv_data_to_file()
    w.write_tensors_to_file()
    w.close()
    print('%d tensors -> %s' % (len(o.names), out))


if __name__ == '__main__':
    main()
