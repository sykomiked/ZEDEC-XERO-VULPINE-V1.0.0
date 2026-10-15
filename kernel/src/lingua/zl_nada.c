/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zl_nada.c — Śikṣā classes and earcons (zl_nada.h). */
#include "zl_nada.h"
#include "zl_sutra.h"

static const zl_varna_t V[ZL_NADA_CONSONANTS] = {
    {"k", 0, 0, 1},  {"kh", 0, 1, 2}, {"g", 0, 2, 3},  {"gh", 0, 3, 4}, {"\xe1\xb9\x85", 0, 4, 5},
    {"c", 1, 0, 6},  {"ch", 1, 1, 7}, {"j", 1, 2, 8},  {"jh", 1, 3, 9}, {"\xc3\xb1", 1, 4, 0},
    {"\xe1\xb9\xad", 2, 0, 1}, {"\xe1\xb9\xadh", 2, 1, 2}, {"\xe1\xb8\x8d", 2, 2, 3},
    {"\xe1\xb8\x8dh", 2, 3, 4}, {"\xe1\xb9\x87", 2, 4, 5},
    {"t", 3, 0, 6},  {"th", 3, 1, 7}, {"d", 3, 2, 8},  {"dh", 3, 3, 9}, {"n", 3, 4, 0},
    {"p", 4, 0, 1},  {"ph", 4, 1, 2}, {"b", 4, 2, 3},  {"bh", 4, 3, 4}, {"m", 4, 4, 5},
    {"y", 1, 5, 1},  {"r", 2, 5, 2},  {"l", 3, 5, 3},  {"v", 5, 5, 4},
    {"\xc5\x9b", 1, 6, 5}, {"\xe1\xb9\xa3", 2, 6, 6}, {"s", 3, 6, 7}, {"h", 0, 7, 8},
};

/* just-intonation ratios over 144 Hz, one per sthāna: 1/1, 9/8, 5/4, 4/3,
 * 3/2, 5/3 */
static const uint32_t NUM[6] = {1, 9, 5, 4, 3, 5}, DEN[6] = {1, 8, 4, 3, 2, 3};

const zl_varna_t *zl_nada_consonant(uint32_t i)
{
    return i < ZL_NADA_CONSONANTS ? &V[i] : 0;
}

const zl_varna_t *zl_nada_match(const char *s, uint32_t *len)
{
    const zl_varna_t *best = 0;
    uint32_t bl = 0;
    for (uint32_t i = 0; s && i < ZL_NADA_CONSONANTS; i++) {
        uint32_t n = 0;
        while (V[i].iast[n] && s[n] == V[i].iast[n]) n++;
        if (!V[i].iast[n] && n > bl) best = &V[i], bl = n;
    }
    if (len) *len = bl;
    return best;
}

int32_t zl_nada_tone(uint64_t cell, zl_tone_t *t)
{
    int32_t op = zl_sutra_op(cell);
    if (op < 0 || !t) return -1;
    const char *s = zl_sutra_root((uint32_t) op);
    const zl_varna_t *first = 0, *last = 0;
    while (*s) {
        uint32_t n;
        const zl_varna_t *v = zl_nada_match(s, &n);
        if (v) {
            if (!first) first = v;
            last = v;
            s += n;
        } else { /* a vowel or a mark: skip one UTF-8 character */
            s++;
            while ((*s & 0xC0) == 0x80) s++;
        }
    }
    if (!first) return -1;
    t->carrier_hz = 144u * NUM[first->sthana] / DEN[first->sthana];
    t->formant_hz = 288u * NUM[last->sthana] / DEN[last->sthana];
    t->envelope = first->prayatna;
    t->phase_deg = 90u * (uint32_t) ((cell >> 60) & 3u);
    return 0;
}
