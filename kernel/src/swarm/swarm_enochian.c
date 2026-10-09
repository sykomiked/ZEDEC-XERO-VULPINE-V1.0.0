/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* swarm_enochian.c — Enochian core language. See swarm_enochian.h. */
#include "swarm_enochian.h"
#include "swarm_budget.h" /* swarm_muldiv */

/* E1, E3: in index order. */
static const struct {
    char letter;
    uint8_t value;
    const char *deva;
    const char *iast;
} LETTERS[SWARM_EN_LETTERS] = {
    {'A', 6, "\xE0\xA4\x85", "a"},   /* अ */
    {'B', 5, "\xE0\xA4\xAC", "ba"},  /* ब */
    {'C', 3, "\xE0\xA4\x9A", "ca"},  /* च */
    {'D', 4, "\xE0\xA4\xA6", "da"},  /* द */
    {'E', 9, "\xE0\xA4\x8F", "e"},   /* ए */
    {'F', 6, "\xE0\xA4\xAB", "pha"}, /* फ */
    {'G', 1, "\xE0\xA4\x97", "ga"},  /* ग */
    {'H', 5, "\xE0\xA4\xB9", "ha"},  /* ह */
    {'I', 3, "\xE0\xA4\x87", "i"},   /* इ */
    {'L', 8, "\xE0\xA4\xB2", "la"},  /* ल */
    {'M', 9, "\xE0\xA4\xAE", "ma"},  /* म */
    {'N', 8, "\xE0\xA4\xA8", "na"},  /* न */
    {'O', 6, "\xE0\xA4\x93", "o"},   /* ओ */
    {'P', 5, "\xE0\xA4\xAA", "pa"},  /* प */
    {'Q', 10, "\xE0\xA4\x95", "ka"}, /* क */
    {'R', 3, "\xE0\xA4\xB0", "ra"},  /* र */
    {'S', 3, "\xE0\xA4\xB8", "sa"},  /* स */
    {'T', 9, "\xE0\xA4\xA4", "ta"},  /* त */
    {'U', 4, "\xE0\xA4\x89", "u"},   /* उ */
    {'V', 0, "", ""},                /* glide */
    {'X', 1, "\xE0\xA4\x95\xE0\xA5\x8D\xE0\xA4\xB7",
     "k\xE1\xB9\xA3"
     "a"},                                      /* क्ष, kṣa */
    {'Y', 0, "", ""},                           /* glide */
    {'Z', 7, "\xE0\xA4\x9C\xE0\xA4\xBC", "za"}, /* ज़ */
};

int swarm_en_index(char c)
{
    if (c >= 'a' && c <= 'z') c = (char) (c - 'a' + 'A');
    if (c == 'J')
        c = 'I';
    else if (c == 'K')
        c = 'C';
    else if (c == 'W')
        c = 'U';
    for (uint32_t i = 0; i < SWARM_EN_LETTERS; i++)
        if (LETTERS[i].letter == c) return (int) i;
    return -1;
}

char swarm_en_letter(uint32_t index)
{
    return index < SWARM_EN_LETTERS ? LETTERS[index].letter : 0;
}

uint32_t swarm_en_value(char c)
{
    int i = swarm_en_index(c);
    return i < 0 ? 0u : LETTERS[i].value;
}

uint32_t swarm_en_gematria(const char *text, uint32_t len)
{
    uint32_t sum = 0;
    for (uint32_t i = 0; i < len && text[i]; i++) sum += swarm_en_value(text[i]);
    return sum;
}

/* E2 */
uint32_t swarm_en_root(uint64_t n)
{
    uint64_t rem;
    if (n == 0) return 9;
    (void) swarm_muldiv(n - 1, 1, 9, &rem);
    return (uint32_t) rem + 1u;
}

static const char *const DOMAINS[10] = {"",         "MONADIC", "DYADIC",   "TRIADIC", "TETRADIC",
                                        "PENTADIC", "HEXADIC", "HEPTADIC", "OCTADIC", "ENNEADIC"};

const char *swarm_en_domain(uint32_t root)
{
    return (root >= 1 && root <= 9) ? DOMAINS[root] : "";
}

/* E3 */
static int32_t spell(const char *word, uint32_t len, char *buf, uint32_t cap, bool deva)
{
    uint32_t n = 0;
    if (cap == 0) return -1;
    for (uint32_t i = 0; i < len && word[i]; i++) {
        int k = swarm_en_index(word[i]);
        if (k < 0) continue;
        const char *s = deva ? LETTERS[k].deva : LETTERS[k].iast;
        for (; *s; s++) {
            if (n + 1 >= cap) return -1;
            buf[n++] = *s;
        }
    }
    buf[n] = 0;
    return (int32_t) n;
}

int32_t swarm_en_sanskrit(const char *word, uint32_t len, char *buf, uint32_t cap)
{
    return spell(word, len, buf, cap, true);
}

int32_t swarm_en_iast(const char *word, uint32_t len, char *buf, uint32_t cap)
{
    return spell(word, len, buf, cap, false);
}

/* E4, E5: the same readings the dictionary's annotations carry. */
static const char *const DIGIT_MEANING[10] = {
    "",
    "Genesis / unity / monad / will",
    "Polarity / mirror / cooperation / dyad",
    "Synthesis / triad / creative expression / vortex anode",
    "Foundation / matter / quaternary / order",
    "Change / liberty / bridge between heaven and earth",
    "Harmony / love / hexagram / domestic equilibrium",
    "Mystery / contemplation / spiritual sevenfold",
    "Power / mastery / infinite cycle / octave",
    "Completion / wisdom / triple-triad / vortex cathode",
};

static const char *const MASTER_MEANING[9] = {
    "",
    "Illumination \xE2\x80\x94 gateway, intuitive clairvoyance",     /* 11 */
    "Master Builder \xE2\x80\x94 the architect of form",             /* 22 */
    "Master Teacher \xE2\x80\x94 the Christic / Bodhisattva octave", /* 33 */
    "Master Healer \xE2\x80\x94 anchored compassion in matter",      /* 44 */
    "Master Liberator \xE2\x80\x94 radical transformation",          /* 55 */
    "Master Lover \xE2\x80\x94 universal devotion",                  /* 66 */
    "Master Adept \xE2\x80\x94 sealed mystery",                      /* 77 */
    "Master Sovereign \xE2\x80\x94 eternal abundance",               /* 88 */
};

static const char *const PLANET[7] = {"Saturn (boundary)", "Sun (vitality)", "Moon (reflection)",
                                      "Mars (action)",     "Mercury (mind)", "Jupiter (expansion)",
                                      "Venus (relation)"};

static const char *const ELEMENT[3] = {"Sulphur (soul)", "Mercury (mind)", "Salt (body)"};

static const uint8_t LOOP[6] = {1, 2, 4, 8, 7, 5};

static bool is_prime(uint32_t n)
{
    if (n < 2) return false;
    if (n % 2u == 0) return n == 2;
    for (uint32_t d = 3; d <= n / d; d += 2)
        if (n % d == 0) return false;
    return true;
}

static bool is_fibonacci(uint32_t n)
{
    uint32_t a = 0, b = 1;
    while (a < n) {
        uint32_t t = a + b;
        if (t < b) return false; /* overflow: n is past the last 32-bit term */
        a = b;
        b = t;
    }
    return a == n;
}

swarm_numen_t swarm_numen(uint32_t n)
{
    swarm_numen_t r;
    r.n = n;
    r.root = (uint8_t) swarm_en_root(n);
    r.mod9 = (uint8_t) (n % 9u);
    r.mod7 = (uint8_t) (n % 7u);
    r.mod3 = (uint8_t) (n % 3u);
    r.master = n >= 11 && n <= 88 && n % 11u == 0;
    r.prime = is_prime(n);
    r.fibonacci = is_fibonacci(n);
    r.vortex = (r.root % 3u == 0) ? SWARM_VORTEX_AXIS : SWARM_VORTEX_LOOP;
    r.loop_step = -1;
    for (int8_t i = 0; i < 6; i++)
        if (LOOP[i] == r.root) r.loop_step = i;
    r.digit_meaning = DIGIT_MEANING[r.root];
    r.master_meaning = r.master ? MASTER_MEANING[n / 11u] : "";
    r.planet = PLANET[r.mod7];
    r.element = ELEMENT[r.mod3];
    return r;
}

uint32_t swarm_vortex_next(uint32_t root)
{
    return swarm_en_root((uint64_t) root * 2u);
}

/* E6 */
static void unit_fix_root(swarm_en_unit_t *u)
{
    u->root = (uint8_t) swarm_en_root(u->gematria);
}

void swarm_en_unit_init(swarm_en_unit_t *u, uint32_t dim, uint64_t *units, uint32_t cap)
{
    u->gematria = 0;
    u->dim = dim;
    u->units = units;
    u->cap = units ? cap : 0;
    for (uint32_t k = 0; k < u->cap; k++) units[k] = 0;
    u->mod7 = u->mod3 = u->mod49 = 0;
    unit_fix_root(u);
}

static void unit_add_value(swarm_en_unit_t *u, uint64_t g, uint8_t m7, uint8_t m3, uint8_t m49)
{
    u->gematria += g;
    u->mod7 = (uint8_t) ((u->mod7 + m7) % 7u);
    u->mod3 = (uint8_t) ((u->mod3 + m3) % 3u);
    u->mod49 = (uint8_t) ((u->mod49 + m49) % 49u);
    unit_fix_root(u);
}

bool swarm_en_unit_letter(swarm_en_unit_t *u, char c)
{
    if (u->dim != 1 || swarm_en_index(c) < 0) return false;
    uint32_t v = swarm_en_value(c);
    u->gematria = 0;
    u->mod7 = u->mod3 = u->mod49 = 0;
    unit_add_value(u, v, (uint8_t) (v % 7u), (uint8_t) (v % 3u), (uint8_t) (v % 49u));
    return true;
}

bool swarm_en_unit_word(swarm_en_unit_t *u, const char *w, uint32_t len)
{
    if (u->dim != 2 || u->cap < 1) return false;
    for (uint32_t i = 0; i < len && w[i]; i++) {
        if (swarm_en_index(w[i]) < 0) return false;
        uint32_t v = swarm_en_value(w[i]);
        unit_add_value(u, v, (uint8_t) (v % 7u), (uint8_t) (v % 3u), (uint8_t) (v % 49u));
        u->units[0]++;
    }
    return true;
}

bool swarm_en_unit_add(swarm_en_unit_t *parent, const swarm_en_unit_t *child)
{
    if (child->dim == 0 || parent->dim != child->dim + 1) return false;
    if (parent->cap < child->dim) return false; /* out of granted memory */
    if (child->cap < child->dim - 1) return false;
    unit_add_value(parent, child->gematria, child->mod7, child->mod3, child->mod49);
    for (uint32_t k = 0; k + 1 < child->dim; k++) parent->units[k] += child->units[k];
    parent->units[child->dim - 1]++;
    return true;
}

uint32_t swarm_en_unit_coords(const swarm_en_unit_t *u, uint8_t *out, uint32_t cap)
{
    uint32_t n = u->dim < cap ? u->dim : cap;
    if (n == 0) return 0;
    out[0] = u->mod49;
    for (uint32_t k = 1; k < n; k++) {
        uint64_t rem = 0;
        if (k - 1 < u->cap) (void) swarm_muldiv(u->units[k - 1], 1, SWARM_EN_LATTICE, &rem);
        out[k] = (uint8_t) rem;
    }
    return n;
}

/* E7: base-25 digits in 16-bit limbs, so every division is 32-bit. */
#define LIMBS     11u /* 176 bits >= 168 */
#define SYM_END   0u
#define SYM_SPACE 24u

static int sym_of(char c)
{
    if (c == ' ') return (int) SYM_SPACE;
    for (uint32_t i = 0; i < SWARM_EN_LETTERS; i++)
        if (LETTERS[i].letter == c) return (int) i + 1;
    return -1; /* lowercase and allographs are refused: packing is lossless */
}

int32_t swarm_en_pack(const char *text, uint32_t len, uint8_t *out, uint32_t cap)
{
    uint32_t frames = (len + SWARM_EN_FRAME_SYMBOLS - 1) / SWARM_EN_FRAME_SYMBOLS;
    if (frames * SWARM_EN_FRAME_BYTES > cap) return -1;
    for (uint32_t f = 0; f < frames; f++) {
        uint32_t limb[LIMBS] = {0};
        for (int32_t i = (int32_t) SWARM_EN_FRAME_SYMBOLS - 1; i >= 0; i--) {
            uint32_t pos = f * SWARM_EN_FRAME_SYMBOLS + (uint32_t) i;
            uint32_t d = SYM_END;
            if (pos < len) {
                int s = sym_of(text[pos]);
                if (s < 0) return -1;
                d = (uint32_t) s;
            }
            uint32_t carry = d;
            for (uint32_t k = 0; k < LIMBS; k++) {
                uint32_t t = limb[k] * 25u + carry;
                limb[k] = t & 0xFFFFu;
                carry = t >> 16;
            }
        }
        uint8_t *o = out + f * SWARM_EN_FRAME_BYTES;
        for (uint32_t b = 0; b < SWARM_EN_FRAME_BYTES; b++)
            o[b] = (uint8_t) (b & 1u ? limb[b >> 1] >> 8 : limb[b >> 1]);
    }
    return (int32_t) (frames * SWARM_EN_FRAME_BYTES);
}

int32_t swarm_en_unpack(const uint8_t *in, uint32_t len, char *out, uint32_t cap)
{
    uint32_t n = 0;
    if (len % SWARM_EN_FRAME_BYTES) return -1;
    for (uint32_t f = 0; f < len / SWARM_EN_FRAME_BYTES; f++) {
        const uint8_t *p = in + f * SWARM_EN_FRAME_BYTES;
        uint32_t limb[LIMBS] = {0};
        for (uint32_t b = 0; b < SWARM_EN_FRAME_BYTES; b++)
            limb[b >> 1] |= (uint32_t) p[b] << ((b & 1u) * 8u);
        for (uint32_t i = 0; i < SWARM_EN_FRAME_SYMBOLS; i++) {
            uint32_t rem = 0;
            for (int32_t k = (int32_t) LIMBS - 1; k >= 0; k--) {
                uint32_t t = (rem << 16) | limb[k];
                limb[k] = t / 25u;
                rem = t % 25u;
            }
            if (rem == SYM_END) {
                if (n >= cap) return -1;
                out[n] = 0;
                return (int32_t) n;
            }
            if (n + 1 >= cap) return -1;
            out[n++] = rem == SYM_SPACE ? ' ' : LETTERS[rem - 1].letter;
        }
    }
    if (n >= cap) return -1;
    out[n] = 0;
    return (int32_t) n;
}

/* E8 */
static bool str_eq(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static void str_copy(char *dst, const char *src, uint32_t cap)
{
    uint32_t i = 0;
    for (; src[i] && i + 1 < cap; i++) dst[i] = src[i];
    dst[i] = 0;
}

void swarm_lang_init(swarm_lang_table_t *t)
{
    t->count = 0;
    t->operator_lang = 0;
    (void) swarm_lang_install(t, "en", "English", 0);
}

int32_t swarm_lang_install(swarm_lang_table_t *t, const char *code, const char *name,
                           uint32_t translator)
{
    for (uint32_t i = 0; i < t->count; i++)
        if (str_eq(t->lang[i].code, code)) {
            t->lang[i].translator = translator;
            return (int32_t) i;
        }
    if (t->count >= SWARM_LANG_MAX || !code[0]) return -1;
    swarm_lang_t *l = &t->lang[t->count];
    str_copy(l->code, code, sizeof l->code);
    str_copy(l->name, name, sizeof l->name);
    l->translator = translator;
    return (int32_t) t->count++;
}

bool swarm_lang_set_operator(swarm_lang_table_t *t, const char *code)
{
    for (uint32_t i = 0; i < t->count; i++)
        if (str_eq(t->lang[i].code, code)) {
            t->operator_lang = i;
            return true;
        }
    return false;
}

const char *swarm_lang_core(void)
{
    return "enochian";
}
