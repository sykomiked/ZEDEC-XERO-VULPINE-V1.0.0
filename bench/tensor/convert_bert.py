# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""convert_bert.py -- a Hugging Face BERT or ELECTRA checkpoint (safetensors)
to the GGUF file kernel/src/tensor/zt_bert.c reads (T26 ENCODER CELLS).

    python3 bench/tensor/convert_bert.py <hf dir> <out.gguf> [--outtype q8_0|f16|f32]

The input directory holds the Hugging Face files of one of:
  * BertForSequenceClassification (the pooler and classifier become the head:
    dense, tanh, out);
  * ElectraForSequenceClassification (classifier.dense and out_proj become
    the head: dense, GELU, out);
  * a sentence-transformers BertModel (modules.json with mean pooling, and
    optionally Normalize: the file says to divide by the length).
config.json, tokenizer.json and model.safetensors are required (plus
modules.json, 1_Pooling/config.json and sentence_bert_config.json for
sentence-transformers). model.safetensors must match a pinned SHA-256 below
(the disc cells), unless --any-sha.

What is checked, so the engine does what the Hugging Face stack does or the
conversion stops: absolute positions; embedding_size == hidden_size;
hidden_act gelu (the erf form); the normaliser is one BertNormalizer, the
pre-tokenizer BertPreTokenizer, the model WordPiece with "##" pieces, and the
post-processor the [CLS] A [SEP] (B [SEP]) template with type ids 0 and 1;
every added token is special; the vocabulary has no holes and its llama.cpp
form (U+2581 before word starts, "##" removed, [BRACKETED] kept) has no
repeats.

Storage: every matrix (the token embedding too) in --outtype (Q8_0, the
default, is used in place by the engine; F16 and F32 are rounded once by the
engine to its 8-bit blocks plus a residual block when it loads); position
and token-type tables, norms and biases F32. Keys: llama.cpp's BERT keys
under the architecture ("bert" or "electra"), the tokenizer's normaliser
flags and length limits under zxv.wordpiece.*, zxv.pooling.normalize, and
the source (repository, revision, SHA-256) under zxv.source.*. The output is
deterministic: the same input gives the same bytes.
"""
import argparse
import hashlib
import json
import os
import sys

import numpy as np
import gguf
from gguf import GGMLQuantizationType as T

# model.safetensors of the disc's encoder cells (docs/release/swarm_model_manifest.json)
PINNED = {
    '9dff0513eaa351ef0c83106709b393e17f5d8954e0f75efe574d1dfa1a67dd0a':
        ('Fallacydrakeling', 'heavyhelium/electra-base-logic-fallacy', '8327c1fac323b5155f3c09a4b51dc71b2e99ba3b'),
    'ba50008343c42acccb77eeab9cf0dd1c26db76e2cbb1a226129f1bccb53e4700':
        ('Verdictwyrmling', 'Pankaj8922/bert-small-nli', 'cd578a157edb3b785730005b94dd29bafccc4e39'),
    '53aa51172d142c89d9012cce15ae4d6cc0ca6895895114379cacb4fab128d9db':
        ('Indexserpent', 'sentence-transformers/all-MiniLM-L6-v2', '1110a243fdf4706b3f48f1d95db1a4f5529b4d41'),
}


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def load_json(d, name, required=True):
    p = os.path.join(d, name)
    if not os.path.exists(p):
        if required:
            sys.exit('missing ' + name)
        return None
    with open(p) as f:
        return json.load(f)


def phantom(tok):
    """llama.cpp's WordPiece vocabulary form"""
    if tok.startswith('[') and tok.endswith(']'):
        return tok
    if tok.startswith('##'):
        return tok[2:]
    return '\u2581' + tok


def check_tokenizer(tj):
    n = tj.get('normalizer') or {}
    if n.get('type') != 'BertNormalizer':
        sys.exit('normaliser is not one BertNormalizer: %s' % n.get('type'))
    if (tj.get('pre_tokenizer') or {}).get('type') != 'BertPreTokenizer':
        sys.exit('pre-tokenizer is not BertPreTokenizer')
    m = tj['model']
    if m.get('type') != 'WordPiece' or m.get('continuing_subword_prefix') != '##':
        sys.exit('model is not WordPiece with ## pieces')
    pp = tj.get('post_processor') or {}
    want_single = [('[CLS]', 0), ('A', 0), ('[SEP]', 0)]
    want_pair = want_single + [('B', 1), ('[SEP]', 1)]

    def seq(items):
        out = []
        for it in items:
            (kind, v), = it.items()
            out.append((v['id'], v['type_id']))
        return out
    if pp.get('type') != 'TemplateProcessing' or seq(pp['single']) != want_single or \
            seq(pp['pair']) != want_pair:
        sys.exit('post-processor is not the [CLS] A [SEP] (B [SEP]) template')
    for a in tj.get('added_tokens', []):
        if not a.get('special') or a.get('lstrip') or a.get('rstrip') or a.get('single_word') or \
                a.get('normalized'):
            sys.exit('added token %r is not a plain special token' % a['content'])
    strip = n.get('strip_accents')
    return dict(clean_text=bool(n['clean_text']), chinese_chars=bool(n['handle_chinese_chars']),
                lowercase=bool(n['lowercase']),
                strip_accents=bool(n['lowercase'] if strip is None else strip),
                max_chars=int(m.get('max_input_chars_per_word', 100)), unk=m['unk_token'])


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('src', help='directory with the Hugging Face files')
    ap.add_argument('out', help='output GGUF path (keep it out of git)')
    ap.add_argument('--outtype', choices=('q8_0', 'f16', 'f32'), default='f16')
    ap.add_argument('--any-sha', action='store_true', help='skip the pinned SHA-256 check')
    a = ap.parse_args()
    st_path = os.path.join(a.src, 'model.safetensors')
    sha = sha256_file(st_path)
    print('model.safetensors sha256', sha)
    if sha not in PINNED and not a.any_sha:
        sys.exit('SHA-256 is not a pinned disc cell; use --any-sha for another checkpoint')
    cell, repo, rev = PINNED.get(sha, ('', '', ''))
    cfg = load_json(a.src, 'config.json')
    tj = load_json(a.src, 'tokenizer.json')
    modules = load_json(a.src, 'modules.json', required=False)
    from safetensors.numpy import load_file
    W = load_file(st_path)

    mt = cfg.get('model_type')
    if mt not in ('bert', 'electra'):
        sys.exit('model_type %r is not bert or electra' % mt)
    ne, nl, nh, nf = cfg['hidden_size'], cfg['num_hidden_layers'], cfg['num_attention_heads'], \
        cfg['intermediate_size']
    if cfg.get('position_embedding_type', 'absolute') != 'absolute':
        sys.exit('positions are not absolute')
    if cfg.get('embedding_size', ne) != ne:
        sys.exit('embedding_size %d != hidden_size %d (a projection the engine does not have)' %
                 (cfg['embedding_size'], ne))
    if cfg.get('hidden_act') != 'gelu':
        sys.exit('hidden_act %r is not gelu' % cfg.get('hidden_act'))
    archs = cfg.get('architectures') or []
    tok = check_tokenizer(tj)

    # ---- the head ----
    pooling, normalize, labels, max_tokens = None, False, [], None
    if mt == 'bert' and 'BertForSequenceClassification' in archs:
        pooling, head = gguf.PoolingType.CLS, ('bert.pooler.dense', 'classifier')
    elif mt == 'electra' and 'ElectraForSequenceClassification' in archs:
        pooling, head = gguf.PoolingType.CLS, ('classifier.dense', 'classifier.out_proj')
    elif mt == 'bert' and modules:
        types = [m['type'] for m in modules]
        if types[:2] != ['sentence_transformers.models.Transformer', 'sentence_transformers.models.Pooling'] \
                or types[2:] not in ([], ['sentence_transformers.models.Normalize']):
            sys.exit('sentence-transformers modules are not Transformer, Pooling (, Normalize): %s' % types)
        pc = load_json(a.src, os.path.join(modules[1]['path'], 'config.json'))
        on = [k for k, v in pc.items() if k.startswith('pooling_mode') and v]
        if on != ['pooling_mode_mean_tokens']:
            sys.exit('pooling is not the mean of the tokens: %s' % on)
        pooling, head, normalize = gguf.PoolingType.MEAN, None, len(types) == 3
        sb = load_json(a.src, 'sentence_bert_config.json')
        max_tokens = sb['max_seq_length']
    else:
        sys.exit('no head: not a sequence classifier and not a sentence-transformers model (%s)' % archs)
    if head:
        n_lab = cfg['id2label'] and len(cfg['id2label'])
        labels = [cfg['id2label'][str(i)] for i in range(n_lab)]
    if max_tokens is None:
        trunc = tj.get('truncation') or {}
        max_tokens = trunc.get('max_length') or cfg['max_position_embeddings']
    if not 3 <= max_tokens <= cfg['max_position_embeddings']:
        sys.exit('max_tokens %d out of range' % max_tokens)

    # ---- the vocabulary ----
    nv = cfg['vocab_size']
    tokens = [None] * nv
    for s, i in tj['model']['vocab'].items():
        if i >= nv or tokens[i] is not None:
            sys.exit('vocabulary id %d out of range or repeated' % i)
        tokens[i] = s
    added = {x['content']: x['id'] for x in tj.get('added_tokens', [])}
    for s, i in added.items():
        if i >= nv or (tokens[i] is not None and tokens[i] != s):
            sys.exit('added token %r clashes with the vocabulary' % s)
        tokens[i] = s
    if any(t is None for t in tokens):
        sys.exit('vocabulary has holes')
    stored = [phantom(t) for t in tokens]
    if len(set(stored)) != nv:
        sys.exit('the llama.cpp vocabulary form repeats an entry')
    ttype = [gguf.TokenType.UNKNOWN if t == tok['unk'] else
             gguf.TokenType.CONTROL if t in added else gguf.TokenType.NORMAL for t in tokens]
    ids = {t: i for i, t in enumerate(tokens)}
    for s in ('[CLS]', '[SEP]', tok['unk']):
        if s not in ids:
            sys.exit('no %s token' % s)

    # ---- the weights ----
    pre = {'bert': 'bert.', 'electra': 'electra.'}[mt]
    if not any(k.startswith(pre) for k in W):
        pre = ''
    used = set()

    def get(name):
        for cand in (name, name.replace('.weight', '.gamma').replace('.bias', '.beta')):
            if cand in W:
                used.add(cand)
                return W[cand].astype(np.float32)
        sys.exit('missing tensor ' + name)

    qt = {'q8_0': T.Q8_0, 'f16': T.F16, 'f32': T.F32}[a.outtype]
    arch = mt
    w = gguf.GGUFWriter(a.out, arch)
    w.add_name(cell or os.path.basename(os.path.normpath(a.src)))
    w.add_file_type({'q8_0': gguf.LlamaFileType.MOSTLY_Q8_0, 'f16': gguf.LlamaFileType.MOSTLY_F16,
                     'f32': gguf.LlamaFileType.ALL_F32}[a.outtype])
    w.add_context_length(cfg['max_position_embeddings'])
    w.add_embedding_length(ne)
    w.add_feed_forward_length(nf)
    w.add_block_count(nl)
    w.add_head_count(nh)
    w.add_layer_norm_eps(cfg['layer_norm_eps'])
    w.add_causal_attention(False)
    w.add_pooling_type(pooling)
    if labels:
        w.add_classifier_output_labels(labels)
    w.add_tokenizer_model('bert')
    w.add_token_list(stored)
    w.add_token_types(ttype)
    w.add_token_type_count(cfg['type_vocab_size'])
    w.add_bos_token_id(ids['[CLS]'])  # llama.cpp keeps BERT's [CLS] as BOS
    w.add_sep_token_id(ids['[SEP]'])
    w.add_unk_token_id(ids[tok['unk']])
    if '[PAD]' in ids:
        w.add_pad_token_id(ids['[PAD]'])
    if '[MASK]' in ids:
        w.add_mask_token_id(ids['[MASK]'])
    for k in ('clean_text', 'chinese_chars', 'strip_accents', 'lowercase'):
        w.add_bool('zxv.wordpiece.' + k, tok[k])
    w.add_uint32('zxv.wordpiece.max_chars', tok['max_chars'])
    w.add_uint32('zxv.wordpiece.max_tokens', max_tokens)
    w.add_bool('zxv.pooling.normalize', normalize)
    w.add_string('zxv.source.sha256', sha)
    if repo:
        w.add_string('zxv.source.repo', repo)
        w.add_string('zxv.source.revision', rev)

    def mat(name, t):
        t = np.ascontiguousarray(t, dtype=np.float32)
        if qt == T.Q8_0 and t.shape[-1] % 32 == 0:
            w.add_tensor(name, gguf.quantize(t, T.Q8_0), raw_dtype=T.Q8_0)
        elif qt == T.F16:
            w.add_tensor(name, t.astype(np.float16))
        else:
            w.add_tensor(name, t)

    def f32(name, t):
        w.add_tensor(name, np.ascontiguousarray(t, dtype=np.float32))

    e = pre + 'embeddings.'
    mat('token_embd.weight', get(e + 'word_embeddings.weight'))
    f32('position_embd.weight', get(e + 'position_embeddings.weight'))
    f32('token_types.weight', get(e + 'token_type_embeddings.weight'))
    f32('token_embd_norm.weight', get(e + 'LayerNorm.weight'))
    f32('token_embd_norm.bias', get(e + 'LayerNorm.bias'))
    for l in range(nl):
        p, o = '%sencoder.layer.%d.' % (pre, l), 'blk.%d.' % l
        for src, dst in (('attention.self.query', 'attn_q'), ('attention.self.key', 'attn_k'),
                         ('attention.self.value', 'attn_v'), ('attention.output.dense', 'attn_output'),
                         ('intermediate.dense', 'ffn_up'), ('output.dense', 'ffn_down')):
            mat(o + dst + '.weight', get(p + src + '.weight'))
            f32(o + dst + '.bias', get(p + src + '.bias'))
        for src, dst in (('attention.output.LayerNorm', 'attn_output_norm'),
                         ('output.LayerNorm', 'layer_output_norm')):
            f32(o + dst + '.weight', get(p + src + '.weight'))
            f32(o + dst + '.bias', get(p + src + '.bias'))
    if head:
        mat('cls.weight', get(head[0] + '.weight'))
        f32('cls.bias', get(head[0] + '.bias'))
        mat('cls.output.weight', get(head[1] + '.weight'))
        f32('cls.output.bias', get(head[1] + '.bias'))
    # what is left must be what no forward pass reads
    left = sorted(set(W) - used)
    skip = [k for k in left if k.endswith('position_ids') or (pooling == gguf.PoolingType.MEAN and
                                                             k.startswith(pre + 'pooler.'))]
    if set(left) - set(skip):
        sys.exit('tensors the conversion does not use: %s' % sorted(set(left) - set(skip)))
    w.write_header_to_file()
    w.write_kv_data_to_file()
    w.write_tensors_to_file()
    w.close()
    print('wrote', a.out, 'arch', arch, 'layers', nl, 'width', ne, 'labels', len(labels),
          'pooling', pooling.name, 'normalize', normalize, 'max_tokens', max_tokens, 'tokenizer', tok)


if __name__ == '__main__':
    main()
