# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""convert_whisper.py -- openai/whisper-base (Hugging Face safetensors) to the
GGUF file kernel/src/tensor/zt_whisper.c reads (T25 SPEECH IN).

    python3 bench/tensor/convert_whisper.py ~/.cache/zxv/models/openai_whisper-base \
        ~/.cache/zxv/models/openai_whisper-base/whisper-base-q8_0.gguf

The input directory holds the Hugging Face files of openai/whisper-base
(model.safetensors, config.json, generation_config.json, vocab.json,
added_tokens.json, tokenizer.json). model.safetensors is checked against the
pinned SHA-256 below first (--any-sha skips the check, for other Whisper
sizes; the layout is the same).

Storage:
  * every matrix whose rows are a multiple of 32 long: Q8_0 (gguf.quantize),
    used in place by the engine. That is all attention and MLP matrices, the
    token embedding (also the tied output matrix) and conv2 (its kernel
    [d, d, 3] flattened to rows of d*3 values, column c*3 + j).
  * conv1 (rows of n_mels*3 = 240 values, not a multiple of 32): F32. The
    engine rounds it once to its own 8-bit blocks when it loads (zero-padded
    rows of 256), so it is rounded once either way.
  * norms, biases, the two position tables and the mel filterbank: F32.
The mel filterbank is computed here (librosa's slaney mel, as whisper's
mel_filters.npz and transformers' WhisperFeatureExtractor), not read from a
file: float64, stored as float32. The conversion is deterministic (no
timestamps or random state in the output): the same input gives the same
bytes.
"""
import argparse
import hashlib
import json
import os
import sys

import numpy as np
import gguf
from gguf import GGMLQuantizationType as T

# SHA-256 of openai/whisper-base model.safetensors (the Hugging Face LFS oid,
# x-linked-etag of https://huggingface.co/openai/whisper-base/resolve/main/model.safetensors)
WHISPER_BASE_SHA256 = '07cadb9f25677c8d50df603e66a98fbd842cce45047139baeb16e6219a1e807b'


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


# ---- librosa.filters.mel(sr, n_fft, n_mels, htk=False, norm='slaney') in float64 ----
def hz_to_mel(f):
    f = np.asanyarray(f, dtype=np.float64)
    f_sp = 200.0 / 3
    mels = f / f_sp
    min_log_hz, min_log_mel, logstep = 1000.0, 1000.0 / f_sp, np.log(6.4) / 27.0
    return np.where(f >= min_log_hz, min_log_mel + np.log(np.maximum(f, 1e-300) / min_log_hz) / logstep, mels)


def mel_to_hz(m):
    m = np.asanyarray(m, dtype=np.float64)
    f_sp = 200.0 / 3
    freqs = f_sp * m
    min_log_hz, min_log_mel, logstep = 1000.0, 1000.0 / f_sp, np.log(6.4) / 27.0
    return np.where(m >= min_log_mel, min_log_hz * np.exp(logstep * (m - min_log_mel)), freqs)


def mel_filters(sr=16000, n_fft=400, n_mels=80):
    fftfreqs = np.linspace(0, sr / 2, 1 + n_fft // 2)
    mel_f = mel_to_hz(np.linspace(hz_to_mel(0.0), hz_to_mel(sr / 2.0), n_mels + 2))
    fdiff = np.diff(mel_f)
    ramps = np.subtract.outer(mel_f, fftfreqs)
    w = np.zeros((n_mels, 1 + n_fft // 2))
    for i in range(n_mels):
        lower = -ramps[i] / fdiff[i]
        upper = ramps[i + 2] / fdiff[i + 1]
        w[i] = np.maximum(0, np.minimum(lower, upper))
    enorm = 2.0 / (mel_f[2:n_mels + 2] - mel_f[:n_mels])
    return w * enorm[:, None]


def q8(w, name, t):
    """add a float matrix as Q8_0"""
    w.add_tensor(name, gguf.quantize(np.ascontiguousarray(t, dtype=np.float32), T.Q8_0), raw_dtype=T.Q8_0)


def f32(w, name, t):
    w.add_tensor(name, np.ascontiguousarray(t, dtype=np.float32))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('src', help='directory with the Hugging Face openai/whisper-* files')
    ap.add_argument('out', help='output GGUF path (keep it out of git)')
    ap.add_argument('--any-sha', action='store_true', help='skip the pinned SHA-256 check')
    a = ap.parse_args()
    st_path = os.path.join(a.src, 'model.safetensors')
    sha = sha256_file(st_path)
    print('model.safetensors sha256', sha)
    if sha != WHISPER_BASE_SHA256 and not a.any_sha:
        sys.exit('SHA-256 mismatch: expected %s (openai/whisper-base); use --any-sha for another size' %
                 WHISPER_BASE_SHA256)
    from safetensors.numpy import load_file
    W = load_file(st_path)
    cfg = json.load(open(os.path.join(a.src, 'config.json')))
    gen = json.load(open(os.path.join(a.src, 'generation_config.json')))
    vocab = json.load(open(os.path.join(a.src, 'vocab.json')))
    added = json.load(open(os.path.join(a.src, 'added_tokens.json')))
    tokj = json.load(open(os.path.join(a.src, 'tokenizer.json')))

    n_mels, d = cfg['num_mel_bins'], cfg['d_model']
    n_vocab = cfg['vocab_size']
    n_actx, n_tctx = cfg['max_source_positions'], cfg['max_target_positions']
    ne_l, nd_l = cfg['encoder_layers'], cfg['decoder_layers']

    # ---- tokenizer: GPT-2 byte-alphabet strings, specials CONTROL ----
    tokens = [None] * n_vocab
    for s, i in list(vocab.items()) + list(added.items()):
        if i < n_vocab:
            if tokens[i] is not None and tokens[i] != s:
                sys.exit('token id %d has two strings' % i)
            tokens[i] = s
    if any(t is None for t in tokens):
        sys.exit('vocabulary has holes')
    eot = vocab['<|endoftext|>'] if '<|endoftext|>' in vocab else added['<|endoftext|>']
    ttype = [gguf.TokenType.CONTROL if i >= eot else gguf.TokenType.NORMAL for i in range(n_vocab)]
    merges = tokj['model']['merges']
    merges = [m if isinstance(m, str) else ' '.join(m) for m in merges]
    tid = {s: i for i, s in enumerate(tokens)}
    langs = sorted(gen['lang_to_id'].items(), key=lambda kv: kv[1])
    lang_ids = [i for _, i in langs]
    if lang_ids != list(range(lang_ids[0], lang_ids[0] + len(lang_ids))):
        sys.exit('language tokens are not contiguous')
    lang_codes = [s[2:-2] for s, _ in langs]

    w = gguf.GGUFWriter(a.out, 'whisper')
    w.add_name('whisper-base')
    w.add_file_type(gguf.LlamaFileType.MOSTLY_Q8_0)
    w.add_uint32('whisper.sample_rate', 16000)
    w.add_uint32('whisper.n_fft', 400)
    w.add_uint32('whisper.hop_length', 160)
    w.add_uint32('whisper.n_mels', n_mels)
    w.add_uint32('whisper.vocab_size', n_vocab)
    w.add_float32('whisper.attention.layer_norm_epsilon', 1e-5)
    w.add_uint32('whisper.audio.context_length', n_actx)
    w.add_uint32('whisper.audio.embedding_length', d)
    w.add_uint32('whisper.audio.feed_forward_length', cfg['encoder_ffn_dim'])
    w.add_uint32('whisper.audio.head_count', cfg['encoder_attention_heads'])
    w.add_uint32('whisper.audio.block_count', ne_l)
    w.add_uint32('whisper.text.context_length', n_tctx)
    w.add_uint32('whisper.text.embedding_length', d)
    w.add_uint32('whisper.text.feed_forward_length', cfg['decoder_ffn_dim'])
    w.add_uint32('whisper.text.head_count', cfg['decoder_attention_heads'])
    w.add_uint32('whisper.text.block_count', nd_l)
    for key, s in [('eot', '<|endoftext|>'), ('sot', '<|startoftranscript|>'), ('translate', '<|translate|>'),
                   ('transcribe', '<|transcribe|>'), ('sot_lm', '<|startoflm|>'), ('sot_prev', '<|startofprev|>'),
                   ('no_speech', '<|nocaptions|>'), ('no_timestamps', '<|notimestamps|>'),
                   ('timestamp_begin', '<|0.00|>')]:
        w.add_uint32('whisper.token.' + key, tid[s])
    w.add_uint32('whisper.token.lang_first', lang_ids[0])
    w.add_uint32('whisper.token.lang_count', len(lang_ids))
    w.add_array('whisper.languages', lang_codes)
    w.add_array('whisper.suppress_tokens', [int(x) for x in gen['suppress_tokens']])
    w.add_array('whisper.begin_suppress_tokens', [int(x) for x in gen['begin_suppress_tokens']])
    w.add_tokenizer_model('gpt2')
    w.add_tokenizer_pre('whisper')
    w.add_token_list(tokens)
    w.add_token_types(ttype)
    w.add_token_merges(merges)
    w.add_eos_token_id(eot)
    w.add_bos_token_id(tid['<|startoftranscript|>'])
    w.add_add_bos_token(False)

    # ---- tensors ----
    mf = mel_filters(16000, 400, n_mels)
    f32(w, 'mel_filters', mf)
    e, dd = 'model.encoder.', 'model.decoder.'
    f32(w, 'enc.conv1.weight', W[e + 'conv1.weight'].reshape(d, -1))
    f32(w, 'enc.conv1.bias', W[e + 'conv1.bias'])
    q8(w, 'enc.conv2.weight', W[e + 'conv2.weight'].reshape(d, -1))
    f32(w, 'enc.conv2.bias', W[e + 'conv2.bias'])
    f32(w, 'enc.pos', W[e + 'embed_positions.weight'])

    def block(src, dst, cross):
        f32(w, dst + 'attn_ln.weight', W[src + 'self_attn_layer_norm.weight'])
        f32(w, dst + 'attn_ln.bias', W[src + 'self_attn_layer_norm.bias'])
        parts = [('attn_', 'self_attn.')] + ([('cross_', 'encoder_attn.')] if cross else [])
        for p, s in parts:
            if p == 'cross_':
                f32(w, dst + 'cross_ln.weight', W[src + 'encoder_attn_layer_norm.weight'])
                f32(w, dst + 'cross_ln.bias', W[src + 'encoder_attn_layer_norm.bias'])
            q8(w, dst + p + 'q.weight', W[src + s + 'q_proj.weight'])
            f32(w, dst + p + 'q.bias', W[src + s + 'q_proj.bias'])
            q8(w, dst + p + 'k.weight', W[src + s + 'k_proj.weight'])
            q8(w, dst + p + 'v.weight', W[src + s + 'v_proj.weight'])
            f32(w, dst + p + 'v.bias', W[src + s + 'v_proj.bias'])
            q8(w, dst + p + 'out.weight', W[src + s + 'out_proj.weight'])
            f32(w, dst + p + 'out.bias', W[src + s + 'out_proj.bias'])
        f32(w, dst + 'mlp_ln.weight', W[src + 'final_layer_norm.weight'])
        f32(w, dst + 'mlp_ln.bias', W[src + 'final_layer_norm.bias'])
        q8(w, dst + 'fc1.weight', W[src + 'fc1.weight'])
        f32(w, dst + 'fc1.bias', W[src + 'fc1.bias'])
        q8(w, dst + 'fc2.weight', W[src + 'fc2.weight'])
        f32(w, dst + 'fc2.bias', W[src + 'fc2.bias'])

    for l in range(ne_l):
        block(e + 'layers.%d.' % l, 'enc.blk.%d.' % l, False)
    f32(w, 'enc.ln.weight', W[e + 'layer_norm.weight'])
    f32(w, 'enc.ln.bias', W[e + 'layer_norm.bias'])
    q8(w, 'dec.token_embd.weight', W[dd + 'embed_tokens.weight'])
    f32(w, 'dec.pos', W[dd + 'embed_positions.weight'])
    for l in range(nd_l):
        block(dd + 'layers.%d.' % l, 'dec.blk.%d.' % l, True)
    f32(w, 'dec.ln.weight', W[dd + 'layer_norm.weight'])
    f32(w, 'dec.ln.bias', W[dd + 'layer_norm.bias'])
    w.write_header_to_file()
    w.write_kv_data_to_file()
    w.write_tensors_to_file()
    w.close()
    print('wrote', a.out, os.path.getsize(a.out), 'bytes, sha256', sha256_file(a.out))


if __name__ == '__main__':
    main()
