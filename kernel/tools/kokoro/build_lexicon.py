#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Build the English pronouncing lexicon (.zxlx) for speech_g2p_en.

    python3 -I build_lexicon.py us_gold.json us_silver.json out.zxlx

The sources are misaki's American English dictionaries (hexgrad/misaki,
Apache-2.0); the gold entries win over the silver ones. An entry that depends
on the part of speech keeps its DEFAULT. The file is data only and is not in
git; ship misaki's licence notice with it.

Format (little-endian): "ZXLX", u8 version 1, u8 flags 0, u16 0, u32 count,
u32 blob bytes, u32 offsets[count] into the blob, then the blob: for each
entry "word\\0phonemes\\0", sorted by the word's bytes, no duplicates.
"""
import json
import struct
import sys

# what Kokoro's US voices know (misaki US_VOCAB plus the reduced 'a', ɐ)
PHONES = set('AIOWYbdfhijklmnpstuvwzæðŋɑɔəɛɜɡɪɹɾʃʊʌʒʤʧˈˌθᵊᵻʔɐ')


def entries(path):
    d = json.load(open(path, encoding='utf-8'))
    for w, v in d.items():
        if isinstance(v, dict):
            v = v.get('DEFAULT')
        if not isinstance(v, str) or not v:
            continue
        if not all(c in PHONES for c in v):
            continue
        b = w.encode('utf-8')
        if not b or len(b) > 63 or any(c < 0x20 for c in b) or b' ' in b:
            continue
        yield b, v.encode('utf-8')


def main():
    gold, silver, out = sys.argv[1:4]
    lex = {}
    for w, p in entries(silver):
        lex[w] = p
    for w, p in entries(gold):
        lex[w] = p
    keys = sorted(lex)
    blob = bytearray()
    offs = []
    for k in keys:
        offs.append(len(blob))
        blob += k + b'\0' + lex[k] + b'\0'
    with open(out, 'wb') as f:
        f.write(b'ZXLX' + struct.pack('<BBHII', 1, 0, 0, len(keys), len(blob)))
        f.write(struct.pack('<%dI' % len(offs), *offs))
        f.write(blob)
    print('%d words, %d bytes' % (len(keys), 16 + 4 * len(offs) + len(blob)))


if __name__ == '__main__':
    main()
