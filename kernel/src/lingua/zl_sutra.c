/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zl_sutra.c — the Dhātu view of the Lingua IR (zl_sutra.h). */
#include "zl_sutra.h"

/* the roots, in IAST, and their senses; a cell's dhātu is the index here
 * plus 1, so dhātu 0 means no root and no operation's cell is 0 */
static const char *const ROOT[] = {"sthā", "dhā", "grah", "dhṛ", "kal", "guṇ", "hṛ", "vṛdh", "vṛt", "mā", "lakṣ", "kṣi", "bṛh", "tul", "yuj", "kḷp", "sidh", "kṛ", "tyaj", "mī", "gam", "hve", "likh", "śru", "viś", "kram", "bandh", "diś", "yu", "jñā", "sṛj", "chid", "ṇam", "śri", "kṣip", "cal", "veṣṭ"};
static const char *const GLOSS[] = {"stand / stop", "place", "take", "hold", "reckon", "multiply", "take away", "grow", "turn", "measure", "mark", "diminish", "grow great", "weigh", "yoke", "arrange", "ward off", "make", "abandon", "exchange", "go", "call", "write", "hear", "enter", "step", "bind", "point out", "join", "know", "emit", "cut", "bend", "resort", "throw", "move", "wrap"};
static const char *const UPASARGA[] = {"", "pra", "parā", "apa", "sam", "anu", "ava", "nis", "nir", "dus", "dur", "vi", "ā", "ni", "adhi", "api", "ati", "su", "ud", "abhi", "prati", "pari", "upa"};
static const char *const PRATYAYA[] = {"", "slot", "array", "address", "frame", "pair-first", "pair-second", "vector", "string", "const", "trunc-quotient", "trunc-remainder", "floor-quotient", "floor-remainder", "exact-quotient", "eq", "ne", "lt", "le", "gt", "ge", "zero", "nonzero", "indirect", "indirect-tail", "integer", "byte", "value", "fixed-point", "newline", "tab", "end-of-input", "length", "numeric", "bitwise", "second", "three", "kind", "range", "width", "status", "magnitude", "index"};
static const char KCHAR[16] = {'-', '1', '2', '3', '*', 't', 'k', 's', 'a', 'p', 'f', 'c', 'i', 'o', 'h', 'x'};

/* one row per IR operation, in opcode order (alphabet.tsv): root,
 * upasarga, pratyaya, and the kāraka codes (kartṛ, karman, karaṇa,
 * sampradāna, apādāna, adhikaraṇa) */
typedef struct {
    uint8_t root, upasarga, pratyaya, karaka[6];
} row_t;

static const row_t OPS[] = {
    {0, 0, 0, {0, 0, 0, 0, 0, 0}}, /* HALT */
    {1, 0, 9, {0, 5, 6, 0, 0, 0}}, /* PUSH */
    {2, 0, 1, {0, 5, 0, 0, 7, 0}}, /* LOAD */
    {3, 0, 1, {0, 7, 0, 0, 1, 0}}, /* STORE */
    {2, 0, 2, {0, 5, 0, 0, 8, 1}}, /* LOADX */
    {3, 0, 2, {0, 8, 0, 0, 1, 2}}, /* STOREX */
    {4, 4, 0, {0, 5, 1, 0, 2, 0}}, /* ADD */
    {4, 11, 0, {0, 5, 1, 0, 2, 0}}, /* SUB */
    {5, 0, 0, {0, 5, 1, 0, 2, 0}}, /* MUL */
    {6, 0, 10, {0, 5, 1, 0, 2, 0}}, /* DIVT */
    {6, 0, 11, {0, 5, 1, 0, 2, 0}}, /* MODT */
    {6, 0, 12, {0, 5, 1, 0, 2, 0}}, /* DIVF */
    {6, 0, 13, {0, 5, 1, 0, 2, 0}}, /* MODF */
    {6, 0, 14, {0, 5, 1, 0, 2, 0}}, /* DIVX */
    {7, 0, 0, {0, 5, 1, 0, 2, 0}}, /* POW */
    {8, 21, 33, {0, 5, 0, 0, 1, 0}}, /* NEG */
    {9, 0, 41, {0, 5, 0, 0, 1, 0}}, /* ABS */
    {10, 0, 0, {0, 5, 0, 0, 1, 0}}, /* SGN */
    {11, 0, 0, {0, 5, 1, 0, 2, 0}}, /* MIN */
    {12, 0, 0, {0, 5, 1, 0, 2, 0}}, /* MAX */
    {13, 0, 15, {0, 5, 1, 0, 2, 0}}, /* EQ */
    {13, 0, 16, {0, 5, 1, 0, 2, 0}}, /* NE */
    {13, 0, 17, {0, 5, 1, 0, 2, 0}}, /* LT */
    {13, 0, 18, {0, 5, 1, 0, 2, 0}}, /* LE */
    {13, 0, 19, {0, 5, 1, 0, 2, 0}}, /* GT */
    {13, 0, 20, {0, 5, 1, 0, 2, 0}}, /* GE */
    {14, 4, 0, {0, 5, 1, 0, 2, 0}}, /* AND */
    {15, 11, 0, {0, 5, 1, 0, 2, 0}}, /* OR */
    {14, 11, 0, {0, 5, 1, 0, 2, 0}}, /* XOR */
    {8, 21, 34, {0, 5, 0, 0, 1, 0}}, /* INVERT */
    {16, 13, 0, {0, 5, 0, 0, 1, 0}}, /* NOT */
    {17, 5, 0, {0, 5, 0, 0, 1, 0}}, /* DUP */
    {18, 0, 0, {0, 1, 0, 0, 0, 0}}, /* DROP */
    {19, 11, 0, {0, 5, 1, 0, 2, 0}}, /* SWAP */
    {17, 5, 35, {0, 5, 1, 0, 2, 0}}, /* OVER */
    {8, 12, 36, {0, 5, 1, 0, 2, 3}}, /* ROT */
    {20, 0, 0, {0, 11, 0, 0, 0, 0}}, /* JMP */
    {20, 0, 21, {0, 11, 1, 0, 0, 0}}, /* JZ */
    {20, 0, 22, {0, 11, 1, 0, 0, 0}}, /* JNZ */
    {21, 12, 0, {0, 11, 0, 0, 0, 0}}, /* CALL */
    {20, 20, 0, {0, 4, 0, 0, 0, 0}}, /* RET */
    {22, 0, 25, {0, 1, 0, 13, 0, 0}}, /* OUTI */
    {22, 0, 8, {0, 6, 0, 13, 0, 0}}, /* OUTS */
    {22, 0, 26, {0, 1, 0, 13, 0, 0}}, /* OUTC */
    {22, 0, 29, {0, 0, 0, 13, 0, 0}}, /* NL */
    {22, 0, 30, {0, 0, 0, 13, 0, 0}}, /* TAB */
    {23, 0, 0, {0, 5, 0, 0, 12, 0}}, /* IN */
    {2, 0, 3, {0, 5, 0, 0, 9, 1}}, /* LOADA */
    {3, 0, 3, {0, 9, 0, 0, 1, 2}}, /* STOREA */
    {0, 6, 40, {0, 0, 6, 0, 0, 0}}, /* TRAP */
    {24, 1, 0, {0, 10, 4, 0, 0, 0}}, /* ENTER */
    {25, 7, 0, {0, 10, 0, 0, 0, 0}}, /* LEAVE */
    {26, 0, 0, {0, 5, 0, 0, 10, 0}}, /* LINK */
    {2, 0, 4, {0, 5, 0, 0, 10, 4}}, /* LOADF */
    {3, 0, 4, {0, 10, 0, 0, 4, 4}}, /* STOREF */
    {27, 0, 4, {0, 5, 0, 0, 10, 4}}, /* ADDRF */
    {9, 21, 42, {0, 5, 4, 0, 0, 0}}, /* BOUND */
    {28, 0, 0, {0, 14, 4, 0, 0, 0}}, /* CONS */
    {2, 0, 5, {0, 5, 0, 0, 14, 0}}, /* CAR */
    {2, 0, 6, {0, 5, 0, 0, 14, 0}}, /* CDR */
    {3, 0, 5, {0, 14, 0, 4, 0, 0}}, /* SETCAR */
    {3, 0, 6, {0, 14, 0, 4, 0, 0}}, /* SETCDR */
    {29, 0, 37, {0, 5, 0, 0, 4, 0}}, /* ISA */
    {22, 0, 27, {0, 4, 0, 13, 0, 0}}, /* OUTV */
    {30, 0, 7, {0, 14, 4, 0, 0, 0}}, /* VNEW */
    {9, 0, 32, {0, 5, 0, 0, 14, 0}}, /* VLEN */
    {2, 0, 7, {0, 5, 0, 0, 14, 4}}, /* VREF */
    {3, 0, 7, {0, 14, 0, 0, 4, 4}}, /* VSET */
    {28, 0, 8, {0, 14, 4, 0, 4, 0}}, /* SCAT */
    {31, 0, 8, {0, 14, 4, 0, 14, 0}}, /* SSUB */
    {13, 0, 8, {0, 5, 4, 0, 4, 0}}, /* SCMP */
    {32, 21, 0, {0, 5, 4, 0, 4, 0}}, /* SCONV */
    {21, 12, 23, {0, 11, 4, 0, 0, 0}}, /* CALLI */
    {20, 0, 24, {0, 11, 4, 0, 0, 0}}, /* JMPI */
    {33, 12, 0, {0, 15, 4, 0, 0, 0}}, /* CATCH */
    {33, 3, 0, {0, 15, 0, 0, 0, 0}}, /* UNCATCH */
    {34, 0, 0, {0, 15, 4, 0, 0, 0}}, /* THROW */
    {9, 0, 38, {0, 5, 0, 0, 1, 0}}, /* RANGE */
    {23, 0, 31, {0, 5, 0, 0, 12, 0}}, /* INEOF */
    {35, 1, 0, {0, 5, 1, 0, 2, 0}}, /* SHL */
    {35, 3, 0, {0, 5, 1, 0, 2, 0}}, /* SHR */
    {36, 0, 39, {0, 5, 0, 0, 1, 0}}, /* WRAP */
    {22, 0, 28, {0, 1, 0, 13, 0, 0}}, /* OUTD */
};
#define NOPS ((uint32_t) (sizeof OPS / sizeof OPS[0]))
#define NROOTS ((uint32_t) (sizeof ROOT / sizeof ROOT[0]))
#define NPRAT ((uint32_t) (sizeof PRATYAYA / sizeof PRATYAYA[0]))

uint32_t zl_sutra_count(void)
{
    return NOPS;
}

uint64_t zl_sutra_pack(const zl_sutra_t *s)
{
    if (!s || s->dhatu > 0xFFFu || s->upasarga > ZL_SUTRA_UPASARGAS || s->pratyaya > 0xFFu ||
        s->sandhi > 0x7FFu || s->lpres > 3u || s->svara > 3u)
        return 0;
    uint64_t c = (uint64_t) s->dhatu | (uint64_t) s->upasarga << 12 | (uint64_t) s->pratyaya << 17;
    for (uint32_t r = 0; r < 6; r++) {
        if (s->karaka[r] > 15u) return 0;
        c |= (uint64_t) s->karaka[r] << (25u + 4u * r);
    }
    return c | (uint64_t) s->sandhi << 49 | (uint64_t) s->lpres << 60 | (uint64_t) s->svara << 62;
}

void zl_sutra_unpack(uint64_t c, zl_sutra_t *s)
{
    s->dhatu = (uint32_t) (c & 0xFFFu);
    s->upasarga = (uint32_t) (c >> 12) & 0x1Fu;
    s->pratyaya = (uint32_t) (c >> 17) & 0xFFu;
    for (uint32_t r = 0; r < 6; r++) s->karaka[r] = (uint8_t) ((c >> (25u + 4u * r)) & 0xFu);
    s->sandhi = (uint32_t) (c >> 49) & 0x7FFu;
    s->lpres = (uint32_t) (c >> 60) & 3u;
    s->svara = (uint32_t) (c >> 62) & 3u;
}

uint64_t zl_sutra_cell(uint32_t op, uint32_t lpres)
{
    if (op >= NOPS || lpres > 3u) return 0;
    zl_sutra_t s = {OPS[op].root + 1u, OPS[op].upasarga, OPS[op].pratyaya, 0, lpres, 0, {0}};
    for (uint32_t r = 0; r < 6; r++) s.karaka[r] = OPS[op].karaka[r];
    return zl_sutra_pack(&s);
}

int32_t zl_sutra_op(uint64_t cell)
{
    zl_sutra_t s;
    zl_sutra_unpack(cell, &s);
    if (s.sandhi || s.svara) return -1;
    for (uint32_t op = 0; op < NOPS; op++) {
        const row_t *o = &OPS[op];
        uint32_t r = 0;
        if (o->root + 1u != s.dhatu || o->upasarga != s.upasarga || o->pratyaya != s.pratyaya) continue;
        while (r < 6 && o->karaka[r] == s.karaka[r]) r++;
        if (r == 6) return (int32_t) op;
    }
    return -1;
}

const char *zl_sutra_root(uint32_t op)
{
    return op < NOPS ? ROOT[OPS[op].root] : "";
}

const char *zl_sutra_gloss(uint32_t op)
{
    return op < NOPS ? GLOSS[OPS[op].root] : "";
}

const char *zl_sutra_upasarga(uint32_t u)
{
    return u <= ZL_SUTRA_UPASARGAS ? UPASARGA[u] : "";
}

const char *zl_sutra_pratyaya(uint32_t p)
{
    return p < NPRAT ? PRATYAYA[p] : "";
}

char zl_sutra_kchar(uint32_t code)
{
    return code < 16u ? KCHAR[code] : '?';
}

uint32_t zl_sutra_evidence(uint32_t a, uint32_t b)
{
    return (a | b) & 3u;
}

#define TAG_SHIFT 60
#define TAG_MASK (3ull << TAG_SHIFT)
#define RESERVED (0x7FFull << 49 | 3ull << 62)

int32_t zl_sutra_fuse(uint64_t a, uint64_t b, uint64_t out[2])
{
    if (!out || (a & RESERVED) || (b & RESERVED)) return -1;
    uint32_t ta = (uint32_t) (a >> TAG_SHIFT) & 3u, tb = (uint32_t) (b >> TAG_SHIFT) & 3u;
    if ((a & ~TAG_MASK) == (b & ~TAG_MASK)) {
        out[0] = (a & ~TAG_MASK) | (uint64_t) zl_sutra_evidence(ta, tb) << TAG_SHIFT;
        return 1;
    }
    if (!ta != !tb) { /* only one side made a claim */
        out[0] = ta ? a : b;
        return 1;
    }
    uint64_t t = ta ? 3ull << TAG_SHIFT : 0;
    out[0] = (a & ~TAG_MASK) | t;
    out[1] = (b & ~TAG_MASK) | t;
    return 2;
}
