# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# Generate test_bert_fixture.h for test_zt_bert.c: three tiny random encoder
# GGUF files written by gguf-py with convert_bert.py's keys and llama.cpp's
# tensor names, the token ids the Hugging Face `tokenizers` library gives
# for a set of texts under each file's normaliser flags, and the float64
# numpy forward pass on the dequantised weights (Q16, rounded).
#   F0  bert, classifier (pooler tanh), every normaliser flag on, a short
#       max_chars (12) so the long-word rule shows; Q8_0 matrices, F16 fc2
#   F1  electra, classifier (GELU head), cased: no lowercase, no accent
#       stripping, no CJK spacing; F16 token embedding
#   F2  bert, mean pooling then divided by the length, no clean_text
# The tokenizer cases run with special-token text parsed and not, and at a
# full and a tight length limit (the truncation rules).
# Usage: python3 -m pip install gguf numpy tokenizers; python3 gen_bert_fixture.py test_bert_fixture.h
import math
import os
import random
import sys
import tempfile

import numpy as np
import gguf
import tokenizers
from gguf import GGMLQuantizationType as T
from gguf.quants import quantize, dequantize
from tokenizers import Tokenizer, models, normalizers, pre_tokenizers, processors

rng = np.random.default_rng(20261015)
NE, NF, NH, NL, CTX, NT = 32, 64, 2, 2, 40, 2
HD = NE // NH
EPS = 1e-12

SPECIAL = ['[PAD]', '[UNK]', '[CLS]', '[SEP]', '[MASK]']
WORDS = ['the', 'quick', 'brown', 'fox', 'jump', 'over', 'lazy', 'dog', 'hello', 'world', 'play',
         'cafe', 'resume', 'strasse', 'Hello', 'The', 'World', 'Café', 'naïve', 'istanbul', 'a',
         'i', 'x', 'y', 'un', 'super', 'long', 'pair', 'word', 'ab', 'abc', 'ου', 'σ', 'ο', 'δ',
         'Ο', 'Δ', 'Σ', 'a\U0001d165\U0001d16db', 'a\U0001d16d\U0001d165b', '\U0001d158\U0001d165',
         '\u1100\u1161\u11c2', '\u1112\u1161\u11ab', '##\u1100\u116e\u11a8', '##\u110b\u1165']
PIECES = ['##s', '##ing', '##ed', '##er', '##ly', '##a', '##b', '##c', '##d', '##e', '##f', '##é',
          '##σ', '##ς', '##ο']
CHARS = list('abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.,!?\'"-;:()[]') + \
    ['日', '本', '語', '中', '文', '。', '，', 'ᄒ', 'ᅡ', 'ᆫ', 'ᄀ', 'ᅳ',
     'é', 'ı', 'ß', '😀', '∑', '«', '»', '—', '…', 'ﬁ', '①']
vocab = []
for t in SPECIAL + WORDS + PIECES + CHARS + ['##' + c for c in 'abcdefghijklmnopqrstuvwxyz']:
    if t not in vocab:
        vocab.append(t)
NV = len(vocab)


def phantom(tok):
    if tok.startswith('[') and tok.endswith(']'):
        return tok
    if tok.startswith('##'):
        return tok[2:]
    return '\u2581' + tok


assert len(set(map(phantom, vocab))) == NV

FILES = [
    dict(arch='bert', pooling=gguf.PoolingType.CLS, labels=['entailment', 'neutral', 'contradiction'],
         clean_text=True, chinese_chars=True, strip_accents=True, lowercase=True, max_chars=12,
         normalize=False, embd='q8', fc2_f16=True),
    dict(arch='electra', pooling=gguf.PoolingType.CLS, labels=['ad hominem', 'circular', 'none', 'other'],
         clean_text=True, chinese_chars=False, strip_accents=False, lowercase=False, max_chars=100,
         normalize=False, embd='f16', fc2_f16=False),
    dict(arch='bert', pooling=gguf.PoolingType.MEAN, labels=[], clean_text=False, chinese_chars=True,
         strip_accents=True, lowercase=True, max_chars=100, normalize=True, embd='q8', fc2_f16=False),
]

HARD = ['Hello, world!', 'The quick brown fox jumps over the lazy dog.', "Don't stop; it's 3.14!",
        'Caf\u00e9 na\u00efve r\u00e9sum\u00e9 co\u00f6perate', '\u039f\u0394\u039f\u03a3 \u03bf\u03b4\u03bf\u03c2', '\u0130stanbul \u0131i', 'Stra\u00dfe', '\ud55c\uad6d\uc5b4 \uac1b', '\u65e5\u672c\u8a9e\u4e2d\u6587\u3002',
        'e\u0301 a\u0308 o\u0323\u0302', 'x\u0315\u0300y', '\u0f73', '\U0001d15f',
        'a\U0001d16d\U0001d165b a\U0001d16d\u0301\U0001d165b a\U0001d16d\u0f39\U0001d165b a\U0001d16d\u034f\U0001d165b',
        'tab\there\nnew\rline\x0bvt\u3000ideo\u00a0nbsp\u200bzw', 'ctrl\x00nul\x01\x7f\ufffd\u200d',
        '\U0001f600 \u2211 \u00abx\u00bb \u2014 \u2026 \ufb01 \u2460', 'abcdefghijkl', 'abcdefghijklm', 'superplayingly unplayed',
        '[CLS] in [SEP] text [UNK] [cls]', 'x[SEP]y', '', '   ', 'zzzz qqq', 'Hello World THE',
        ' '.join(['word'] * 30)]
BLOCKS = [(0x20, 0x7E), (0xA0, 0x24F), (0x300, 0x36F), (0x370, 0x3FF), (0x1100, 0x11FF),
          (0x2000, 0x206F), (0x3000, 0x30FF), (0x4E00, 0x4E3F), (0xAC00, 0xAC3F), (0x0, 0x1F),
          (0x1F600, 0x1F60F)]
rnd = random.Random(20261015)
FUZZ = []
for _ in range(40):
    s = []
    for _ in range(rnd.randint(1, 16)):
        if rnd.random() < 0.3:
            s.append(' ')
        lo, hi = rnd.choice(BLOCKS)
        s.append(chr(rnd.randint(lo, hi)))
    FUZZ.append(''.join(s))
CUT = ['the quick brown fox jumps over the lazy dog', 'hello world', 'hello', 'a b c d e f', 'x y z w',
       'a b c', 'x y z', 'a b c d e f g h i j']
TEXTS = HARD + FUZZ + CUT
CASES = [(t, None, 40) for t in TEXTS] + [(TEXTS[i], TEXTS[-1 - i], 40) for i in range(10)] + \
    [('the quick brown fox jumps over the lazy dog', 'hello world', 9),
     ('hello', 'the quick brown fox jumps over the lazy dog', 9),
     ('a b c d e f', 'x y z w', 8), ('a b c', 'x y z', 8), ('a b c d e f g h i j', None, 6)]


def make_tokenizer(f, max_tokens):
    tk = Tokenizer(models.WordPiece({t: i for i, t in enumerate(vocab)}, unk_token='[UNK]',
                                    max_input_chars_per_word=f['max_chars']))
    tk.normalizer = normalizers.BertNormalizer(clean_text=f['clean_text'],
                                               handle_chinese_chars=f['chinese_chars'],
                                               strip_accents=f['strip_accents'], lowercase=f['lowercase'])
    tk.pre_tokenizer = pre_tokenizers.BertPreTokenizer()
    tk.post_processor = processors.TemplateProcessing(
        single='[CLS] $A [SEP]', pair='[CLS] $A [SEP] $B:1 [SEP]:1',
        special_tokens=[('[CLS]', vocab.index('[CLS]')), ('[SEP]', vocab.index('[SEP]'))])
    tk.add_special_tokens(SPECIAL)
    tk.enable_truncation(max_length=max_tokens, strategy='longest_first')
    return tk


def write_gguf(f, path):
    w = gguf.GGUFWriter(path, f['arch'])
    vals = {}

    def q8(name, rows, cols, scale):
        a = (rng.standard_normal((rows, cols)) * scale).astype(np.float32)
        raw = quantize(a, T.Q8_0)
        w.add_tensor(name, raw, raw_dtype=T.Q8_0)
        vals[name] = dequantize(raw, T.Q8_0).astype(np.float64).reshape(rows, cols)

    def f16(name, rows, cols, scale):
        a = (rng.standard_normal((rows, cols)) * scale).astype(np.float16)
        w.add_tensor(name, a)
        vals[name] = a.astype(np.float64)

    def f32(name, shape, scale, offset=0.0):
        a = (offset + rng.standard_normal(shape) * scale).astype(np.float32)
        w.add_tensor(name, a)
        vals[name] = a.astype(np.float64)

    w.add_context_length(CTX)
    w.add_embedding_length(NE)
    w.add_feed_forward_length(NF)
    w.add_block_count(NL)
    w.add_head_count(NH)
    w.add_layer_norm_eps(EPS)
    w.add_causal_attention(False)
    w.add_pooling_type(f['pooling'])
    if f['labels']:
        w.add_classifier_output_labels(f['labels'])
    w.add_tokenizer_model('bert')
    w.add_token_list([phantom(t) for t in vocab])
    w.add_token_types([gguf.TokenType.UNKNOWN if t == '[UNK]' else gguf.TokenType.CONTROL
                       if t in SPECIAL else gguf.TokenType.NORMAL for t in vocab])
    w.add_token_type_count(NT)
    w.add_bos_token_id(vocab.index('[CLS]'))
    w.add_sep_token_id(vocab.index('[SEP]'))
    w.add_unk_token_id(vocab.index('[UNK]'))
    w.add_pad_token_id(vocab.index('[PAD]'))
    for k in ('clean_text', 'chinese_chars', 'strip_accents', 'lowercase'):
        w.add_bool('zxv.wordpiece.' + k, f[k])
    w.add_uint32('zxv.wordpiece.max_chars', f['max_chars'])
    w.add_uint32('zxv.wordpiece.max_tokens', CTX)
    w.add_bool('zxv.pooling.normalize', f['normalize'])
    (q8 if f['embd'] == 'q8' else f16)('token_embd.weight', NV, NE, 0.5)
    f32('position_embd.weight', (CTX, NE), 0.3)
    f32('token_types.weight', (NT, NE), 0.3)
    f32('token_embd_norm.weight', (NE,), 0.2, 1.0)
    f32('token_embd_norm.bias', (NE,), 0.1)
    for l in range(NL):
        p = 'blk.%d.' % l
        for m in ('attn_q', 'attn_k', 'attn_v', 'attn_output'):
            q8(p + m + '.weight', NE, NE, 0.25)
            f32(p + m + '.bias', (NE,), 0.05)
        q8(p + 'ffn_up.weight', NF, NE, 0.2)
        f32(p + 'ffn_up.bias', (NF,), 0.05)
        (f16 if f['fc2_f16'] and l == 1 else q8)(p + 'ffn_down.weight', NE, NF, 0.15)
        f32(p + 'ffn_down.bias', (NE,), 0.05)
        for n in ('attn_output_norm', 'layer_output_norm'):
            f32(p + n + '.weight', (NE,), 0.2, 1.0)
            f32(p + n + '.bias', (NE,), 0.1)
    if f['labels']:
        q8('cls.weight', NE, NE, 0.25)
        f32('cls.bias', (NE,), 0.1)
        q8('cls.output.weight', len(f['labels']), NE, 0.4)
        f32('cls.output.bias', (len(f['labels']),), 0.1)
    w.write_header_to_file()
    w.write_kv_data_to_file()
    w.write_tensors_to_file()
    w.close()
    return vals


def ln(v, g, b):
    c = v - v.mean(-1, keepdims=True)
    return c / np.sqrt((c * c).mean(-1, keepdims=True) + EPS) * g + b


erf = np.vectorize(math.erf)


def gelu(v):
    return 0.5 * v * (1 + erf(v / math.sqrt(2)))


def forward(f, V, ids, types):
    n = len(ids)
    h = V['token_embd.weight'][ids] + V['position_embd.weight'][:n] + V['token_types.weight'][types]
    h = ln(h, V['token_embd_norm.weight'], V['token_embd_norm.bias'])
    for l in range(NL):
        P = lambda s: V['blk.%d.%s' % (l, s)]
        q = (h @ P('attn_q.weight').T + P('attn_q.bias')).reshape(n, NH, HD).transpose(1, 0, 2)
        k = (h @ P('attn_k.weight').T + P('attn_k.bias')).reshape(n, NH, HD).transpose(1, 0, 2)
        v = (h @ P('attn_v.weight').T + P('attn_v.bias')).reshape(n, NH, HD).transpose(1, 0, 2)
        s = q @ k.transpose(0, 2, 1) / math.sqrt(HD)
        s = np.exp(s - s.max(-1, keepdims=True))
        s /= s.sum(-1, keepdims=True)
        o = (s @ v).transpose(1, 0, 2).reshape(n, NE)
        h = ln(h + o @ P('attn_output.weight').T + P('attn_output.bias'),
               P('attn_output_norm.weight'), P('attn_output_norm.bias'))
        m = gelu(h @ P('ffn_up.weight').T + P('ffn_up.bias'))
        h = ln(h + m @ P('ffn_down.weight').T + P('ffn_down.bias'),
               P('layer_output_norm.weight'), P('layer_output_norm.bias'))
    if f['labels']:
        d = h[0] @ V['cls.weight'].T + V['cls.bias']
        d = gelu(d) if f['arch'] == 'electra' else np.tanh(d)
        return d @ V['cls.output.weight'].T + V['cls.output.bias']
    e = h.mean(0)
    return e / np.linalg.norm(e) if f['normalize'] else e


def arr(name, ctype, a, per=16):
    a = list(a)
    s = ['static const %s %s[%d] = {' % (ctype, name, max(len(a), 1))]
    for i in range(0, len(a), per):
        s.append('    ' + ', '.join(str(int(v)) for v in a[i:i + per]) + ',')
    if not a:
        s.append('    0,')
    s.append('};')
    return '\n'.join(s)


def cstr(b):
    return '"' + ''.join('\\x%02x' % c for c in b) + '"'


out = ['/* test_bert_fixture.h -- generated by gen_bert_fixture.py with gguf-py, numpy %s and'
       % np.__version__,
       ' * tokenizers %s. Three tiny random encoder GGUF files, the library\'s token ids for'
       % tokenizers.__version__,
       ' * a set of texts under each file\'s normaliser, and float64 reference outputs (Q16,',
       ' * rounded). Do not edit. */', '#include <stdint.h>',
       '#define BF_NFILES %d' % len(FILES), '#define BF_CTX %d' % CTX, '#define BF_NE %d' % NE]
# the texts, shared by every file
blob, offs = bytearray(), []
for t in TEXTS:
    e = t.encode()
    offs.append((len(blob), len(e)))
    blob += e
out.append(arr('BF_TEXT', 'uint8_t', blob, 24))
out.append('#define BF_NTEXT %d' % len(TEXTS))
out.append(arr('BF_TEXT_OFF', 'uint32_t', [o for o, _ in offs]))
out.append(arr('BF_TEXT_LEN', 'uint32_t', [n for _, n in offs]))
# cases: text A index, text B index or -1, max tokens
case_rows = []
for a, b, m in CASES:
    case_rows += [TEXTS.index(a) if a in TEXTS else -1, -1 if b is None else
                  (TEXTS.index(b) if b in TEXTS else -1), m]
extra = [t for a, b, _ in CASES for t in (a, b) if t is not None and t not in TEXTS]
assert not extra, extra
out.append('#define BF_NCASE %d' % len(CASES))
out.append(arr('BF_CASE', 'int32_t', case_rows, 12))
ids_all, ids_off = [], []
fwd_ids, fwd_types, fwd_off, fwd_expect, fwd_eoff, mean_expect = [], [], [], [], [], []
ggufs, gsizes = [], []
FWD = [0, 1, len(HARD) - 1, len(TEXTS) + 2, len(TEXTS) + 10]  # two singles, the long one, a pair, a cut pair
for fi, f in enumerate(FILES):
    path = os.path.join(tempfile.mkdtemp(), 'f%d.gguf' % fi)
    V = write_gguf(f, path)
    g = open(path, 'rb').read()
    out.append(arr('BF_GGUF%d' % fi, 'uint8_t', g, 24))
    gsizes.append(len(g))
    for special in (1, 0):
        for ci, (a, b, m) in enumerate(CASES):
            tk = make_tokenizer(f, m)
            tk.encode_special_tokens = not special
            enc = tk.encode(a, b) if b is not None else tk.encode(a)
            ids_off.append(len(ids_all))
            ids_all += enc.ids
            if special and ci in FWD:
                fwd_off.append(len(fwd_ids))
                fwd_ids += enc.ids
                fwd_types += enc.type_ids
                fwd_eoff.append(len(fwd_expect))
                fwd_expect += list(np.round(forward(f, V, enc.ids, enc.type_ids) * 65536).astype(np.int64))
                if f['normalize']:
                    raw = dict(f, normalize=False)
                    mean_expect.extend(np.round(forward(raw, V, enc.ids, enc.type_ids) * 65536).astype(np.int64))
    ids_off.append(len(ids_all))
fwd_off.append(len(fwd_ids))
fwd_eoff.append(len(fwd_expect))
out.append('static const uint8_t *const BF_GGUF[BF_NFILES] = {%s};' %
           ', '.join('BF_GGUF%d' % i for i in range(len(FILES))))
out.append(arr('BF_GGUF_SIZE', 'uint32_t', gsizes))
out.append(arr('BF_NLABELS', 'uint32_t', [len(f['labels']) for f in FILES]))
out.append('/* ids of file f, special s (1 parsed, 0 as text), case c: BF_IDS[BF_IDS_OFF[r]] ..'
           ' BF_IDS[BF_IDS_OFF[r + 1]] with r = (f * 2 + (1 - s)) * BF_NCASE + c, and one end'
           ' offset per file */')
out.append(arr('BF_IDS', 'int32_t', ids_all))
# make the offsets a single run: one end per file was appended; rebuild plainly
flat = []
k = 0
per = 2 * len(CASES)
for fi in range(len(FILES)):
    flat += ids_off[k:k + per]
    k += per + 1
flat.append(len(ids_all))
out.append(arr('BF_IDS_OFF', 'uint32_t', flat))
out.append('#define BF_NFWD %d' % len(FWD))
out.append(arr('BF_FWD_CASE', 'int32_t', FWD))
out.append(arr('BF_FWD_IDS', 'int32_t', fwd_ids))
out.append(arr('BF_FWD_TYPES', 'uint8_t', fwd_types))
out.append(arr('BF_FWD_OFF', 'uint32_t', fwd_off))
out.append(arr('BF_FWD_EXPECT', 'int32_t', fwd_expect, 8))
out.append(arr('BF_FWD_EOFF', 'uint32_t', fwd_eoff))
out.append('/* file 2 read with zxv.pooling.normalize off: the plain mean, per forward input */')
out.append(arr('BF_FWD_MEAN', 'int32_t', mean_expect, 8))
open(sys.argv[1], 'w').write('\n'.join(out) + '\n')
print('vocab', NV, 'gguf bytes', gsizes, 'ids', len(ids_all), 'forward values', len(fwd_expect))
