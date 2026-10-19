/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* meta.h — the Tier 3 metamorphic runner (hosted only; include after tier.h).
 *
 * A metamorphic relation is a function that drives the code under test with
 * related inputs, CHECKs the relation between the outputs, and returns a
 * digest of every output it observed. meta_run() runs the whole table three
 * times in one process:
 *     forward, forward again, and in reverse order,
 * and requires each relation's digest to be identical in all three runs. A
 * module that keeps hidden static state (a counter, a cache, a table that a
 * previous relation filled) changes some output between runs, and the digest
 * comparison names the relation it leaked into. Building the same program
 * with -fsanitize=address (LeakSanitizer on) is how tests/harness/tiers.py
 * proves the relations leak no memory across runs either.
 */
#ifndef ZXV_META_H
#define ZXV_META_H

typedef uint64_t (*meta_fn)(void);
typedef struct {
    const char *name;
    meta_fn fn;
} meta_rel_t;

/* order-sensitive digest step (FNV-1a over the 8 bytes of v) */
static inline uint64_t meta_mix(uint64_t h, uint64_t v)
{
    if (!h) h = 0xCBF29CE484222325ull;
    for (int i = 0; i < 8; i++) {
        h ^= (v >> (8 * i)) & 0xFF;
        h *= 0x100000001B3ull;
    }
    return h;
}

#define META_MAX_RELS 64
static inline void meta_run(const meta_rel_t *rels, unsigned n)
{
    uint64_t d[3][META_MAX_RELS];
    if (n > META_MAX_RELS) n = META_MAX_RELS;
    for (unsigned i = 0; i < n; i++) d[0][i] = rels[i].fn();
    for (unsigned i = 0; i < n; i++) d[1][i] = rels[i].fn();
    for (unsigned i = n; i-- > 0;) d[2][i] = rels[i].fn();
    for (unsigned i = 0; i < n; i++)
        CHECK(d[0][i] == d[1][i] && d[0][i] == d[2][i],
              "relation %s gives the same outputs forward, again and in reverse order "
              "(no hidden state): %016llx %016llx %016llx",
              rels[i].name, (unsigned long long) d[0][i], (unsigned long long) d[1][i],
              (unsigned long long) d[2][i]);
}

#endif /* ZXV_META_H */
