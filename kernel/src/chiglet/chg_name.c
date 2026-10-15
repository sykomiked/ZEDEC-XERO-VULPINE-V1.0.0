/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* chg_name.c — naming the chiglet (chg_name.h N1-N7). */
#include "chg_name.h"
#include "../robin_debanks/sha256.h"

static const char SEED_TAG[] = "zxv/chiglet/name/v1";
static const char CONFIRM_TAG[] = "zxv/chiglet/name/confirm/v1";
static const uint8_t MAGIC[4] = {'Z', 'X', 'C', 'N'};

/* N2: compared after folding (lower case, no separators) */
static const char *const RESERVED[] = {
    "chiglet", "system",  "root",   "admin",   "kernel",  "owner", "guest",
    "tiamat",  "zxv",     "siri",   "alexa",   "cortana", "bixby", "copilot",
    "gemini",  "chatgpt", "claude", "netnavi", "megaman", "pet",
};

static bool is_alpha(uint8_t c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static bool is_sep(uint8_t c)
{
    return c == ' ' || c == '-' || c == '\'';
}

static uint8_t lower(uint8_t c)
{
    return c >= 'A' && c <= 'Z' ? (uint8_t) (c - 'A' + 'a') : c;
}

/* the length of the well-formed UTF-8 character at s[i], or 0 */
static size_t utf8_len(const uint8_t *s, size_t i, size_t n)
{
    uint8_t c = s[i];
    if (c < 0x80u) return 1;
    size_t k;
    uint32_t cp, min;
    if (c >= 0xC2u && c <= 0xDFu) {
        k = 2;
        cp = c & 0x1Fu;
        min = 0x80u;
    } else if (c >= 0xE0u && c <= 0xEFu) {
        k = 3;
        cp = c & 0x0Fu;
        min = 0x800u;
    } else if (c >= 0xF0u && c <= 0xF4u) {
        k = 4;
        cp = c & 0x07u;
        min = 0x10000u;
    } else {
        return 0;
    }
    if (n - i < k) return 0;
    for (size_t j = 1; j < k; j++) {
        if ((s[i + j] & 0xC0u) != 0x80u) return 0;
        cp = (cp << 6) | (s[i + j] & 0x3Fu);
    }
    if (cp < min || cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu)) return 0;
    return k;
}

static bool reserved(const uint8_t *s, size_t n)
{
    char f[CHGN_NAME_MAX];
    size_t m = 0;
    for (size_t i = 0; i < n; i++)
        if (!is_sep(s[i])) f[m++] = (char) lower(s[i]);
    for (size_t r = 0; r < sizeof RESERVED / sizeof RESERVED[0]; r++) {
        const char *w = RESERVED[r];
        size_t i = 0;
        while (i < m && w[i] && w[i] == f[i]) i++;
        if (i == m && w[i] == '\0') return true;
    }
    return false;
}

int chgn_check(const char *name, size_t len)
{
    if (!name) return CHGN_E_ARG;
    if (len == 0 || len > CHGN_NAME_MAX) return CHGN_E_EMPTY;
    const uint8_t *s = (const uint8_t *) name;
    for (size_t i = 0; i < len;) {
        size_t k = utf8_len(s, i, len);
        if (k == 0) return CHGN_E_UTF8;
        i += k;
    }
    if (!(is_alpha(s[0]) || s[0] >= 0x80u)) return CHGN_E_START;
    bool prev_sep = false;
    for (size_t i = 0; i < len;) {
        size_t k = utf8_len(s, i, len);
        uint8_t c = s[i];
        if (k == 1 && is_sep(c)) {
            if (prev_sep || i + 1 == len) return CHGN_E_SEP;
            prev_sep = true;
        } else {
            if (k == 1 && !is_alpha(c) && !(c >= '0' && c <= '9')) return CHGN_E_CHAR;
            prev_sep = false;
        }
        i += k;
    }
    return reserved(s, len) ? CHGN_E_RESERVED : CHGN_OK;
}

const char *chgn_reason(int code)
{
    static const char *const R[] = {
        "ok",
        "bad arguments",
        "a name has 1 to 32 bytes",
        "a name is well-formed UTF-8",
        "a name starts with a letter",
        "a name holds letters, digits and single spaces, hyphens or apostrophes",
        "a space, hyphen or apostrophe goes between two letters or digits",
        "that name is reserved",
        "this chiglet already has its name; a new name means a new chiglet",
        "that is not the name you were shown",
        "the stored chiglet record is damaged",
    };
    return code <= 0 && code >= CHGN_E_RECORD ? R[-code] : "unknown";
}

void chgn_init(chgn_t *n, const uint8_t salt[CHGN_SALT])
{
    if (!n) return;
    for (uint32_t i = 0; i < CHGN_SALT; i++) n->salt[i] = salt ? salt[i] : 0;
    n->locked = false;
    n->len = 0;
    for (uint32_t i = 0; i <= CHGN_NAME_MAX; i++) n->name[i] = 0;
    n->seed = 0;
}

static void tagged(const char *tag, size_t tlen, const uint8_t salt[CHGN_SALT], const char *name,
                   size_t len, uint8_t out[32])
{
    sha256_ctx_t c;
    sha256_init(&c);
    sha256_update(&c, (const uint8_t *) tag, tlen);
    sha256_update(&c, salt, CHGN_SALT);
    sha256_update(&c, (const uint8_t *) name, len);
    sha256_final(&c, out);
}

static uint64_t seed_of(const uint8_t salt[CHGN_SALT], const char *name, size_t len)
{
    uint8_t h[32];
    tagged(SEED_TAG, sizeof SEED_TAG - 1, salt, name, len, h);
    uint64_t v = 0;
    for (uint32_t i = 0; i < 8; i++) v |= (uint64_t) h[i] << (8 * i);
    return v;
}

static bool is_vowel_or_soft(uint8_t c)
{
    switch (c) {
    case 'a':
    case 'e':
    case 'i':
    case 'o':
    case 'u':
    case 'l':
    case 'm':
    case 'n':
    case 'r':
    case 'w':
    case 'y':
        return true;
    default:
        return false;
    }
}

static bool is_hard(uint8_t c)
{
    switch (c) {
    case 'b':
    case 'd':
    case 'g':
    case 'k':
    case 'p':
    case 'q':
    case 't':
    case 'x':
    case 'z':
        return true;
    default:
        return false;
    }
}

/* N4 */
static swarm_dna_t personality(uint64_t seed, const char *name, size_t len)
{
    swarm_dna_t d = swarm_dna_from_seed(seed);
    uint32_t letters = 0, soft = 0, hard = 0;
    for (size_t i = 0; i < len; i++) {
        uint8_t c = lower((uint8_t) name[i]);
        if (!is_alpha(c)) continue;
        letters++;
        if (is_vowel_or_soft(c)) soft++;
        if (is_hard(c)) hard++;
    }
    if (letters == 0) return d;
    uint32_t warmth = soft * 255u / letters;
    uint32_t temper = hard * 255u / letters;
    uint32_t pace = letters >= 16u ? 0u : (16u - letters) * 255u / 15u;
    d.gene[SWARM_GENE_WARMTH] = (uint8_t) ((d.gene[SWARM_GENE_WARMTH] + warmth) / 2u);
    d.gene[SWARM_GENE_TEMPERAMENT] = (uint8_t) ((d.gene[SWARM_GENE_TEMPERAMENT] + temper) / 2u);
    d.gene[SWARM_GENE_PACE] = (uint8_t) ((d.gene[SWARM_GENE_PACE] + pace) / 2u);
    return d;
}

int chgn_preview(const chgn_t *n, const char *name, size_t len, chgn_preview_t *out)
{
    if (!n || !name || !out) return CHGN_E_ARG;
    if (n->locked) return CHGN_E_LOCKED;
    int rc = chgn_check(name, len);
    if (rc != CHGN_OK) return rc;
    out->seed = seed_of(n->salt, name, len);
    out->dna = personality(out->seed, name, len);
    tagged(CONFIRM_TAG, sizeof CONFIRM_TAG - 1, n->salt, name, len, out->confirm);
    return CHGN_OK;
}

int chgn_confirm(chgn_t *n, const char *name, size_t len, const uint8_t confirm[CHGN_DIGEST])
{
    if (!n || !name || !confirm) return CHGN_E_ARG;
    if (n->locked) return CHGN_E_LOCKED;
    int rc = chgn_check(name, len);
    if (rc != CHGN_OK) return rc;
    uint8_t want[CHGN_DIGEST];
    tagged(CONFIRM_TAG, sizeof CONFIRM_TAG - 1, n->salt, name, len, want);
    uint8_t diff = 0;
    for (uint32_t i = 0; i < CHGN_DIGEST; i++) diff |= (uint8_t) (want[i] ^ confirm[i]);
    if (diff) return CHGN_E_DIGEST;
    for (size_t i = 0; i < len; i++) n->name[i] = name[i];
    n->name[len] = 0;
    n->len = (uint8_t) len;
    n->seed = seed_of(n->salt, name, len);
    n->locked = true;
    return CHGN_OK;
}

bool chgn_dna(const chgn_t *n, swarm_dna_t *out)
{
    if (!n || !out || !n->locked) return false;
    *out = personality(n->seed, n->name, n->len);
    return true;
}

bool chgn_addressed(const chgn_t *n, const char *text, size_t len, size_t *rest)
{
    if (!n || !text || !n->locked || len < n->len) return false;
    for (size_t i = 0; i < n->len; i++) {
        uint8_t a = (uint8_t) text[i], b = (uint8_t) n->name[i];
        if (lower(a) != lower(b)) return false;
    }
    size_t i = n->len;
    if (i < len) {
        uint8_t c = (uint8_t) text[i];
        if (!(c == ' ' || c == ',' || c == ':' || c == ';' || c == '!' || c == '?' || c == '.'))
            return false;
    }
    while (i < len) {
        uint8_t c = (uint8_t) text[i];
        if (!(c == ' ' || c == ',' || c == ':' || c == ';' || c == '!' || c == '?' || c == '.'))
            break;
        i++;
    }
    if (rest) *rest = i;
    return true;
}

void chgn_encode(const chgn_t *n, uint8_t out[CHGN_RECORD])
{
    size_t k = 0;
    for (uint32_t i = 0; i < 4; i++) out[k++] = MAGIC[i];
    out[k++] = 1;
    out[k++] = n->locked ? 1u : 0u;
    out[k++] = n->len;
    for (uint32_t i = 0; i < CHGN_NAME_MAX; i++) out[k++] = i < n->len ? (uint8_t) n->name[i] : 0u;
    for (uint32_t i = 0; i < CHGN_SALT; i++) out[k++] = n->salt[i];
    uint8_t h[32];
    sha256(out, k, h);
    for (uint32_t i = 0; i < 8; i++) out[k++] = h[i];
}

int chgn_decode(chgn_t *n, const uint8_t in[CHGN_RECORD])
{
    if (!n || !in) return CHGN_E_ARG;
    const size_t body = CHGN_RECORD - 8u;
    uint8_t h[32];
    sha256(in, body, h);
    uint8_t diff = 0;
    for (uint32_t i = 0; i < 8; i++) diff |= (uint8_t) (h[i] ^ in[body + i]);
    for (uint32_t i = 0; i < 4; i++) diff |= (uint8_t) (in[i] ^ MAGIC[i]);
    if (diff || in[4] != 1u || in[5] > 1u || in[6] > CHGN_NAME_MAX) return CHGN_E_RECORD;
    bool locked = in[5] == 1u;
    uint8_t len = in[6];
    const uint8_t *name = in + 7;
    for (uint32_t i = len; i < CHGN_NAME_MAX; i++)
        if (name[i] != 0) return CHGN_E_RECORD;
    if (locked ? chgn_check((const char *) name, len) != CHGN_OK : len != 0) return CHGN_E_RECORD;
    chgn_init(n, in + 7 + CHGN_NAME_MAX);
    if (locked) {
        for (uint32_t i = 0; i < len; i++) n->name[i] = (char) name[i];
        n->len = len;
        n->seed = seed_of(n->salt, n->name, len);
        n->locked = true;
    }
    return CHGN_OK;
}
