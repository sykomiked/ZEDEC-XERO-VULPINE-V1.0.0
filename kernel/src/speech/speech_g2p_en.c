/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* speech_g2p_en.c — English text to Kokoro's phonemes. See speech_g2p_en.h. */
#include "speech_g2p_en.h"

const char speech_g2p_en_attribution[] =
    "English pronunciations from misaki by hexgrad (Apache-2.0); letter-to-sound "
    "rules after the US Naval Research Laboratory's (Elovitz et al., 1976).";

/* ---- bytes and code points ---- */

static uint32_t slen(const char *s)
{
    uint32_t n = 0;
    while (s[n]) n++;
    return n;
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t) p[0] | (uint32_t) p[1] << 8 | (uint32_t) p[2] << 16 | (uint32_t) p[3] << 24;
}

/* decode one code point at s[*i] (n bytes); 0 on a bad sequence (skipped) */
static uint32_t cp_next(const uint8_t *s, uint32_t n, uint32_t *i)
{
    uint32_t c = s[*i], w = 1, cp;
    if (c < 0x80)
        cp = c;
    else if ((c & 0xE0) == 0xC0)
        w = 2, cp = c & 0x1F;
    else if ((c & 0xF0) == 0xE0)
        w = 3, cp = c & 0x0F;
    else if ((c & 0xF8) == 0xF0)
        w = 4, cp = c & 0x07;
    else {
        (*i)++;
        return 0;
    }
    if (*i + w > n) {
        *i = n;
        return 0;
    }
    for (uint32_t k = 1; k < w; k++) {
        uint8_t b = s[*i + k];
        if ((b & 0xC0) != 0x80) {
            *i += k;
            return 0;
        }
        cp = cp << 6 | (b & 0x3F);
    }
    *i += w;
    return cp;
}

/* the last code point of s[0..n) and where it starts */
static uint32_t cp_last(const char *s, uint32_t n, uint32_t *at)
{
    if (!n) return 0;
    uint32_t i = n - 1;
    while (i > 0 && ((uint8_t) s[i] & 0xC0) == 0x80) i--;
    if (at) *at = i;
    uint32_t j = i;
    return cp_next((const uint8_t *) s, n, &j);
}

static bool in_set(uint32_t cp, const char *set)
{
    uint32_t n = slen(set), i = 0;
    while (i < n)
        if (cp_next((const uint8_t *) set, n, &i) == cp) return true;
    return false;
}

#define PRIMARY   0x2C8u /* ˈ */
#define SECONDARY 0x2CCu /* ˌ */
static const char VOWELS[] = "AIOQWYaiuæɑɒɔəɛɜɪʊʌᵻɐ";
static const char CONSONANTS[] = "bdfhjklmnpstvwzðŋɡɹɾʃʒʤʧθʔ";
/* Kokoro's US symbols, the stress marks and the punctuation G6 keeps */
static const char PHONES[] = "AIOWYbdfhijklmnpstuvwzæðŋɑɔəɛɜɡɪɹɾʃʊʌʒʤʧˈˌθᵊᵻʔɐ";

/* ---- an output buffer ---- */

typedef struct {
    char *p;
    uint32_t n, cap;
    bool over;
} buf_t;

static void put(buf_t *b, const char *s, uint32_t n)
{
    if (b->over) return;
    if (b->n + n + 1 > b->cap) {
        b->over = true;
        return;
    }
    for (uint32_t i = 0; i < n; i++) b->p[b->n++] = s[i];
    b->p[b->n] = 0;
}

static void puts_(buf_t *b, const char *s)
{
    put(b, s, slen(s));
}

/* ---- the dictionary file (G1) ---- */

static int32_t cmp_word(const char *a, uint32_t an, const uint8_t *b)
{
    for (uint32_t i = 0;; i++) {
        uint32_t x = i < an ? (uint8_t) a[i] : 0, y = b[i];
        if (x != y) return x < y ? -1 : 1;
        if (!x) return 0;
    }
}

speech_lex_status_t speech_lex_parse(speech_lex_t *l, const uint8_t *b, uint64_t n)
{
    if (!l) return SPEECH_LEX_EARG;
    l->count = l->blob_len = 0;
    l->offs = l->blob = 0;
    if (!b) return SPEECH_LEX_EARG;
    if (n < 16) return SPEECH_LEX_ESIZE;
    if (b[0] != 'Z' || b[1] != 'X' || b[2] != 'L' || b[3] != 'X') return SPEECH_LEX_EMAGIC;
    if (b[4] != SPEECH_LEX_VERSION || b[5] || b[6] || b[7]) return SPEECH_LEX_EVERSION;
    uint32_t count = rd32(b + 8), blen = rd32(b + 12);
    if (!count || (uint64_t) 16 + (uint64_t) count * 4 + blen != n) return SPEECH_LEX_ESIZE;
    const uint8_t *offs = b + 16, *blob = offs + (uint64_t) count * 4;
    uint32_t prev = 0, expect = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t o = rd32(offs + (uint64_t) i * 4);
        if (o != expect || o >= blen) return SPEECH_LEX_EOFFSET;
        uint32_t w = o;
        while (w < blen && blob[w]) {
            if (blob[w] <= 0x20) return SPEECH_LEX_ERECORD;
            w++;
        }
        if (w == o || w >= blen || w - o >= SPEECH_G2P_WORD) return SPEECH_LEX_ERECORD;
        uint32_t p = w + 1, q = p;
        while (q < blen && blob[q]) q++;
        if (q == p || q >= blen) return SPEECH_LEX_ERECORD;
        for (uint32_t k = p; k < q;) {
            uint32_t cp = cp_next(blob, q, &k);
            if (!cp || !in_set(cp, PHONES)) return SPEECH_LEX_EPHONE;
        }
        if (i &&
            cmp_word((const char *) blob + prev, slen((const char *) blob + prev), blob + o) >= 0)
            return SPEECH_LEX_EORDER;
        prev = o;
        expect = q + 1;
    }
    if (expect != blen) return SPEECH_LEX_ERECORD;
    l->count = count;
    l->blob_len = blen;
    l->offs = offs;
    l->blob = blob;
    return SPEECH_LEX_OK;
}

const char *speech_lex_find(const speech_lex_t *l, const char *w, uint32_t len)
{
    if (!l || !l->count || !w || !len) return 0;
    uint32_t lo = 0, hi = l->count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        const uint8_t *r = l->blob + rd32(l->offs + (uint64_t) mid * 4);
        int32_t c = cmp_word(w, len, r);
        if (!c) return (const char *) r + slen((const char *) r) + 1;
        if (c < 0)
            hi = mid;
        else
            lo = mid + 1;
    }
    return 0;
}

/* ---- the built-in words (G3) ---- */

typedef struct {
    const char *w, *p;
} word_t;

static const word_t WORDS[] = {
#include "speech_g2p_words.inc"
};
#define NWORDS (sizeof WORDS / sizeof WORDS[0])

static const char *builtin(const char *w, uint32_t len)
{
    uint32_t lo = 0, hi = NWORDS;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        int32_t c = cmp_word(w, len, (const uint8_t *) WORDS[mid].w);
        if (!c) return WORDS[mid].p;
        if (c < 0)
            hi = mid;
        else
            lo = mid + 1;
    }
    return 0;
}

typedef struct {
    const speech_lex_t *lex;
    speech_g2p_stats_t *st;
} ctx_t;

/* our words, then the file: lower-case w */
static const char *known(const ctx_t *c, const char *w, uint32_t len, bool count)
{
    const char *p = builtin(w, len);
    if (p) {
        if (count && c->st) c->st->builtin++;
        return p;
    }
    p = speech_lex_find(c->lex, w, len);
    if (p && count && c->st) c->st->lexicon++;
    return p;
}

/* ---- stress (misaki's apply_stress, the cases used here) ---- */

/* every primary to secondary, earlier secondaries dropped */
static void demote(buf_t *b, const char *s)
{
    uint32_t n = slen(s);
    bool prim = false;
    for (uint32_t i = 0; i < n;) {
        uint32_t cp = cp_next((const uint8_t *) s, n, &i);
        if (cp == PRIMARY) prim = true;
    }
    for (uint32_t i = 0; i < n;) {
        uint32_t at = i, cp = cp_next((const uint8_t *) s, n, &i);
        if (cp == SECONDARY && prim) continue;
        if (cp == PRIMARY)
            puts_(b, "ˌ");
        else
            put(b, s + at, i - at);
    }
}

/* ---- suffixes by sound (misaki's _s, _ed, _ing) ---- */

static void suffix_s(buf_t *b)
{
    uint32_t c = cp_last(b->p, b->n, 0);
    if (in_set(c, "ptkfθ"))
        puts_(b, "s");
    else if (in_set(c, "szʃʒʧʤ"))
        puts_(b, "ᵻz");
    else
        puts_(b, "z");
}

static void suffix_ed(buf_t *b)
{
    uint32_t at = 0, c = cp_last(b->p, b->n, &at);
    if (in_set(c, "pkfθʃsʧ"))
        puts_(b, "t");
    else if (c == 'd')
        puts_(b, "ᵻd");
    else if (c != 't')
        puts_(b, "d");
    else {
        uint32_t c2 = cp_last(b->p, at, 0);
        if (at && in_set(c2, "AIOWYiuæɑəɛɪɹʊʌ")) {
            b->n = at;
            puts_(b, "ɾᵻd");
        } else
            puts_(b, "ᵻd");
    }
}

static void suffix_ing(buf_t *b)
{
    uint32_t at = 0, c = cp_last(b->p, b->n, &at);
    if (c == 't' && at && in_set(cp_last(b->p, at, 0), "AIOWYiuæɑəɛɪɹʊʌ")) {
        b->n = at;
        puts_(b, "ɾɪŋ");
    } else
        puts_(b, "ɪŋ");
}

static bool ends(const char *w, uint32_t n, const char *s)
{
    uint32_t k = slen(s);
    if (k > n) return false;
    for (uint32_t i = 0; i < k; i++)
        if (w[n - k + i] != s[i]) return false;
    return true;
}

static bool is_cons_letter(char c)
{
    return c >= 'a' && c <= 'z' && c != 'a' && c != 'e' && c != 'i' && c != 'o' && c != 'u';
}

/* G4: a known stem plus -s / -'s / -ed / -ing */
static bool stemmed(const ctx_t *c, const char *w, uint32_t n, buf_t *b)
{
    char t[SPEECH_G2P_WORD + 2];
    const char *p = 0;
    int kind = 0; /* 1 s, 2 ed, 3 ing */
    if (n >= 3 && w[n - 1] == 's') {
        kind = 1;
        if (!ends(w, n, "ss")) p = known(c, w, n - 1, false);
        if (!p && (ends(w, n, "'s") || (n > 4 && ends(w, n, "es") && !ends(w, n, "ies"))))
            p = known(c, w, n - 2, false);
        if (!p && n > 4 && ends(w, n, "ies")) {
            for (uint32_t i = 0; i < n - 3; i++) t[i] = w[i];
            t[n - 3] = 'y';
            p = known(c, t, n - 2, false);
        }
    } else if (n >= 2 && ends(w, n, "s'")) {
        kind = 1;
        p = known(c, w, n - 2, false);
    }
    if (!p && n >= 4 && w[n - 1] == 'd') {
        kind = 2;
        if (!ends(w, n, "dd")) p = known(c, w, n - 1, false);
        if (!p && n > 4 && ends(w, n, "ed") && !ends(w, n, "eed")) p = known(c, w, n - 2, false);
        if (!p && n > 5 && ends(w, n, "ed") && w[n - 3] == w[n - 4] && is_cons_letter(w[n - 3]))
            p = known(c, w, n - 3, false);
    }
    if (!p && n >= 5 && ends(w, n, "ing")) {
        kind = 3;
        if (n > 5) p = known(c, w, n - 3, false);
        if (!p) {
            for (uint32_t i = 0; i < n - 3; i++) t[i] = w[i];
            t[n - 3] = 'e';
            p = known(c, t, n - 2, false);
        }
        if (!p && n > 5 && w[n - 4] == w[n - 5] && is_cons_letter(w[n - 4]))
            p = known(c, w, n - 4, false);
        if (!p && ends(w, n, "cking")) p = known(c, w, n - 4, false);
    }
    if (!p) return false;
    puts_(b, p);
    if (kind == 1)
        suffix_s(b);
    else if (kind == 2)
        suffix_ed(b);
    else
        suffix_ing(b);
    if (c->st) c->st->stemmed++;
    return true;
}

/* G4: two known words, the second one's stress made secondary */
static bool compound(const ctx_t *c, const char *w, uint32_t n, buf_t *b)
{
    if (n < 6) return false;
    for (uint32_t k = n - 3; k >= 3; k--) {
        const char *a = known(c, w, k, false), *z = a ? known(c, w + k, n - k, false) : 0;
        if (a && z) {
            puts_(b, a);
            demote(b, z);
            if (c->st) c->st->compound++;
            return true;
        }
    }
    return false;
}

/* spell letters (lower case a-z) by name, the last stress primary */
static void spell(const char *w, uint32_t n, buf_t *b)
{
    uint32_t start = b->n;
    for (uint32_t i = 0; i < n; i++) {
        char k[2] = {'#', (char) (w[i] | 0x20)};
        const char *p = builtin(k, 2);
        if (p) demote(b, p);
    }
    /* the last secondary becomes primary (misaki's get_NNP) */
    uint32_t last = UINT32_MAX;
    for (uint32_t i = start; i < b->n;) {
        uint32_t at = i, cp = cp_next((const uint8_t *) b->p, b->n, &i);
        if (cp == SECONDARY) last = at;
    }
    if (last != UINT32_MAX && !b->over) {
        /* ˌ (CB 8C) -> ˈ (CB 88): same length */
        b->p[last + 1] = (char) 0x88;
    }
}

/* ---- letter-to-sound rules (G4) ---- */

typedef struct {
    const char *l, *m, *r, *p;
} rule_t;

/* Context classes: '#' one or more vowels; ':' zero or more consonants;
 * '^' one consonant; '.' one voiced consonant; '+' E, I or Y; '%' a suffix
 * (ER E ES ED ING ELY); '&' S C G Z X J CH SH; '@' T S R D L Z N J TH CH SH;
 * ' ' not a letter. Rules for a letter are tried in order. */
static const rule_t RULES[] = {
    {"", "A", " ", "ə"},        {" ", "ARE", " ", "ɑɹ"},   {" ", "AR", "O", "əɹ"},
    {"", "AR", "#", "ɛɹ"},      {"^", "AS", "#", "As"},    {"", "A", "WA", "ə"},
    {"", "AW", "", "ɔ"},        {" :", "ANY", "", "ɛni"},  {"", "A", "^+#", "A"},
    {"#:", "ALLY", "", "əli"},  {" ", "AL", "#", "əl"},    {"", "AGAIN", "", "əɡɛn"},
    {"#:", "AG", "E", "ɪʤ"},    {"", "A", "^+:#", "æ"},    {" :", "A", "^+ ", "A"},
    {"", "A", "^%", "A"},       {" ", "ARR", "", "əɹ"},    {"", "ARR", "", "æɹ"},
    {" :", "AR", " ", "ɑɹ"},    {"", "AR", " ", "ɜɹ"},     {"", "AR", "", "ɑɹ"},
    {"", "AIR", "", "ɛɹ"},      {"", "AI", "", "A"},       {"", "AY", "", "A"},
    {"", "AU", "", "ɔ"},        {"#:", "AL", " ", "əl"},   {"#:", "ALS", " ", "əlz"},
    {"", "ALK", "", "ɔk"},      {"", "AL", "^", "ɔl"},     {" :", "ABLE", "", "Abəl"},
    {"", "ABLE", "", "əbəl"},   {"", "ANG", "+", "Anʤ"},   {"", "A", "", "æ"},

    {" ", "BE", "^#", "bɪ"},    {"", "BEING", "", "biɪŋ"}, {" ", "BOTH", " ", "bOθ"},
    {" ", "BUS", "#", "bɪz"},   {"", "BUIL", "", "bɪl"},   {"", "BB", "", "b"},
    {"", "B", "", "b"},

    {" ", "CH", "^", "k"},      {"^E", "CH", "", "k"},     {"", "CH", "", "ʧ"},
    {" S", "CI", "#", "sI"},    {"", "CI", "A", "ʃ"},      {"", "CI", "O", "ʃ"},
    {"", "CI", "EN", "ʃ"},      {"", "CC", "+", "ks"},     {"", "C", "+", "s"},
    {"", "CK", "", "k"},        {"", "COM", "%", "kʌm"},   {"", "CC", "", "k"},
    {"", "C", "", "k"},

    {"#:", "DED", " ", "dɪd"},  {".E", "D", " ", "d"},     {"#:^E", "D", " ", "t"},
    {" ", "DE", "^#", "dɪ"},    {" ", "DO", " ", "du"},    {" ", "DOES", "", "dʌz"},
    {" ", "DOING", "", "duɪŋ"}, {" ", "DOW", "", "dW"},    {"", "DU", "A", "ʤu"},
    {"", "DD", "", "d"},        {"", "D", "", "d"},

    {"#:", "E", " ", ""},       {"':^", "E", " ", ""},     {" :", "E", " ", "i"},
    {"#", "ED", " ", "d"},      {"#:", "E", "D ", ""},     {"", "EV", "ER", "ɛv"},
    {"", "E", "^%", "i"},       {"", "ERI", "#", "iɹi"},   {"", "ERI", "", "ɛɹɪ"},
    {"#:", "ER", "#", "ɜɹ"},    {"", "ER", "#", "ɛɹ"},     {"", "ER", "", "ɜɹ"},
    {" ", "EVEN", "", "ivɛn"},  {"#:", "E", "W", ""},      {"@", "EW", "", "u"},
    {"", "EW", "", "ju"},       {"", "E", "O", "i"},       {"#:&", "ES", " ", "ɪz"},
    {"#:", "E", "S ", ""},      {"#:", "ELY", " ", "li"},  {"#:", "EMENT", "", "mɛnt"},
    {"", "EFUL", "", "fʊl"},    {"", "EE", "", "i"},       {"", "EARN", "", "ɜɹn"},
    {" ", "EAR", "^", "ɜɹ"},    {"", "EAD", "", "ɛd"},     {"#:", "EA", " ", "iə"},
    {"", "EA", "SU", "ɛ"},      {"", "EA", "", "i"},       {"", "EIGH", "", "A"},
    {"", "EI", "", "i"},        {" ", "EYE", "", "I"},     {"", "EY", "", "i"},
    {"", "EU", "", "ju"},       {"", "E", "", "ɛ"},

    {"", "FUL", "", "fʊl"},     {"", "FF", "", "f"},       {"", "F", "", "f"},

    {"", "GIV", "", "ɡɪv"},     {" ", "G", "I^", "ɡ"},     {"", "GE", "T", "ɡɛ"},
    {"SU", "GGES", "", "ɡʤɛs"}, {"", "GG", "", "ɡ"},       {" B#", "G", "", "ɡ"},
    {"", "G", "+", "ʤ"},        {"", "GREAT", "", "ɡɹAt"}, {"#", "GH", "", ""},
    {"", "G", "", "ɡ"},

    {" ", "HAV", "", "hæv"},    {" ", "HERE", "", "hiɹ"},  {" ", "HOUR", "", "Wəɹ"},
    {"", "HOW", "", "hW"},      {"", "H", "#", "h"},       {"", "H", "", ""},

    {" ", "IN", "", "ɪn"},      {" ", "I", " ", "I"},      {"", "IN", "D", "In"},
    {"", "IER", "", "iɜɹ"},     {"#:R", "IED", "", "id"},  {"", "IED", " ", "Id"},
    {"", "IEN", "", "iɛn"},     {"", "IE", "T", "Iɛ"},     {" :", "I", "%", "I"},
    {"", "I", "%", "i"},        {"", "IE", "", "i"},       {"", "I", "^+:#", "ɪ"},
    {"", "IR", "#", "Iɹ"},      {"", "IZ", "%", "Iz"},     {"", "IS", "%", "Iz"},
    {"", "I", "D%", "I"},       {"+^", "I", "^+", "ɪ"},    {"", "I", "T%", "I"},
    {"#:^", "I", "^+", "ɪ"},    {"", "IR", "", "ɜɹ"},      {"", "IGH", "", "I"},
    {"", "ILD", "", "Ild"},     {"", "IGN", " ", "In"},    {"", "IGN", "^", "In"},
    {"", "IGN", "%", "In"},     {"", "IQUE", "", "ik"},    {"", "I", "", "ɪ"},

    {"", "J", "", "ʤ"},

    {" ", "K", "N", ""},        {"", "KK", "", "k"},       {"", "K", "", "k"},

    {"", "LO", "C#", "lO"},     {"L", "L", "", ""},        {"#:^", "L", "%", "əl"},
    {"", "LEAD", "", "lid"},    {"", "L", "", "l"},

    {"", "MOV", "", "muv"},     {"", "MM", "", "m"},       {"", "M", "", "m"},

    {"E", "NG", "+", "nʤ"},     {"", "NG", "R", "ŋɡ"},     {"", "NG", "#", "ŋɡ"},
    {"", "NGL", "%", "ŋɡəl"},   {"", "NG", "", "ŋ"},       {"", "NK", "", "ŋk"},
    {" ", "NOW", " ", "nW"},    {"", "NN", "", "n"},       {"", "N", "", "n"},

    {"", "OF", " ", "əv"},      {"", "OROUGH", "", "ɜɹO"}, {"#:", "OR", " ", "ɜɹ"},
    {"#:", "ORS", " ", "ɜɹz"},  {"", "OR", "", "ɔɹ"},      {" ", "ONE", "", "wʌn"},
    {"", "OW", "", "O"},        {" ", "OVER", "", "Ovɜɹ"}, {"", "OV", "", "ʌv"},
    {"", "O", "^%", "O"},       {"", "O", "^EN", "O"},     {"", "O", "^I#", "O"},
    {"", "OL", "D", "Ol"},      {"", "OUGHT", "", "ɔt"},   {"", "OUGH", "", "ʌf"},
    {" ", "OU", "", "W"},       {"H", "OU", "S#", "W"},    {"", "OUS", "", "əs"},
    {"", "OUR", "", "ɔɹ"},      {"", "OULD", "", "ʊd"},    {"^", "OU", "^L", "ʌ"},
    {"", "OUP", "", "up"},      {"", "OU", "", "W"},       {"", "OY", "", "Y"},
    {"", "OING", "", "Oɪŋ"},    {"", "OI", "", "Y"},       {"", "OOR", "", "ɔɹ"},
    {"", "OOK", "", "ʊk"},      {"", "OOD", "", "ʊd"},     {"", "OO", "", "u"},
    {"", "O", "E", "O"},        {"", "O", " ", "O"},       {"", "OA", "", "O"},
    {" ", "ONLY", "", "Onli"},  {" ", "ONCE", "", "wʌns"}, {"", "ON'T", "", "Ont"},
    {"C", "O", "N", "ɑ"},       {"", "O", "NG", "ɔ"},      {" :^", "O", "N", "ʌ"},
    {"I", "ON", "", "ən"},      {"#:", "ON", " ", "ən"},   {"#^", "ON", "", "ən"},
    {"", "O", "ST ", "O"},      {"", "OF", "^", "ɔf"},     {"", "OTHER", "", "ʌðɜɹ"},
    {"", "OSS", " ", "ɔs"},     {"#:^", "OM", "", "ʌm"},   {"", "O", "", "ɑ"},

    {"", "PH", "", "f"},        {"", "PEOP", "", "pip"},   {"", "POW", "", "pW"},
    {"", "PUT", " ", "pʊt"},    {"", "PP", "", "p"},       {"", "P", "", "p"},

    {"", "QUAR", "", "kwɔɹ"},   {"", "QU", "", "kw"},      {"", "Q", "", "k"},

    {" ", "RE", "^#", "ɹi"},    {"", "RR", "", "ɹ"},       {"", "R", "", "ɹ"},

    {"", "SH", "", "ʃ"},        {"#", "SION", "", "ʒən"},  {"", "SOME", "", "sʌm"},
    {"#", "SUR", "#", "ʒɜɹ"},   {"", "SUR", "#", "ʃɜɹ"},   {"#", "SU", "#", "ʒu"},
    {"#", "SSU", "#", "ʃu"},    {"#", "SED", " ", "zd"},   {"#", "S", "#", "z"},
    {"", "SAID", "", "sɛd"},    {"^", "SION", "", "ʃən"},  {"", "S", "S", ""},
    {".", "S", " ", "z"},       {"#:.E", "S", " ", "z"},   {"#:^##", "S", " ", "z"},
    {"#:^#", "S", " ", "s"},    {"U", "S", " ", "s"},      {" :#", "S", " ", "z"},
    {" ", "SCH", "", "sk"},     {"", "S", "C+", ""},       {"#", "SM", "", "zm"},
    {"#", "SN", "'", "zən"},    {"", "S", "", "s"},

    {" ", "THE", " ", "ðə"},    {"", "TO", " ", "tu"},     {"", "THAT", " ", "ðæt"},
    {" ", "THIS", " ", "ðɪs"},  {" ", "THEY", "", "ðA"},   {" ", "THERE", "", "ðɛɹ"},
    {"", "THER", "", "ðɜɹ"},    {"", "THEIR", "", "ðɛɹ"},  {" ", "THAN", " ", "ðæn"},
    {" ", "THEM", " ", "ðɛm"},  {"", "THESE", " ", "ðiz"}, {" ", "THEN", "", "ðɛn"},
    {"", "THROUGH", "", "θɹu"}, {"", "THOSE", "", "ðOz"},  {"", "THOUGH", " ", "ðO"},
    {" ", "THUS", "", "ðʌs"},   {"", "TH", "", "θ"},       {"#:", "TED", " ", "tɪd"},
    {"S", "TI", "#N", "ʧ"},     {"", "TI", "O", "ʃ"},      {"", "TI", "A", "ʃ"},
    {"", "TIEN", "", "ʃən"},    {"", "TUR", "#", "ʧɜɹ"},   {"", "TU", "A", "ʧu"},
    {" ", "TWO", "", "tu"},     {"", "TT", "", "t"},       {"", "T", "", "t"},

    {" ", "UN", "I", "jun"},    {" ", "UN", "", "ʌn"},     {" ", "UPON", "", "əpɔn"},
    {"@", "UR", "#", "ʊɹ"},     {"", "UR", "#", "jʊɹ"},    {"", "UR", "", "ɜɹ"},
    {"", "U", "^ ", "ʌ"},       {"", "U", "^^", "ʌ"},      {"", "UY", "", "I"},
    {" G", "U", "#", ""},       {"G", "U", "%", ""},       {"G", "U", "#", "w"},
    {"#N", "U", "", "ju"},      {"@", "U", "", "u"},       {"", "U", "", "ju"},

    {"", "VIEW", "", "vju"},    {"", "V", "", "v"},

    {" ", "WERE", "", "wɜɹ"},   {"", "WA", "S", "wɑ"},     {"", "WA", "T", "wɑ"},
    {"", "WHERE", "", "wɛɹ"},   {"", "WHAT", "", "wʌt"},   {"", "WHOL", "", "hOl"},
    {"", "WHO", "", "hu"},      {"", "WH", "", "w"},       {"", "WAR", "", "wɔɹ"},
    {"", "WOR", "^", "wɜɹ"},    {"", "WR", "", "ɹ"},       {"", "W", "", "w"},

    {" ", "X", "", "z"},        {"", "X", "", "ks"},

    {"", "YOUNG", "", "jʌŋ"},   {" ", "YOU", "", "ju"},    {" ", "YES", "", "jɛs"},
    {" ", "Y", "", "j"},        {"#:^", "Y", " ", "i"},    {"#:^", "Y", "I", "i"},
    {" :", "Y", " ", "I"},      {" :", "Y", "#", "I"},     {" :", "Y", "^+:#", "ɪ"},
    {" :", "Y", "^#", "I"},     {"", "Y", "", "ɪ"},

    {"", "ZZ", "", "z"},        {"", "Z", "", "z"},

    {"", "'", "", ""},
};
#define NRULES (sizeof RULES / sizeof RULES[0])

static bool is_letter(char c)
{
    return c >= 'A' && c <= 'Z';
}

static bool is_vowel_l(char c)
{
    return c == 'A' || c == 'E' || c == 'I' || c == 'O' || c == 'U';
}

static bool is_cons_l(char c)
{
    return is_letter(c) && !is_vowel_l(c);
}

static bool is_voiced(char c)
{
    return c == 'B' || c == 'D' || c == 'V' || c == 'G' || c == 'J' || c == 'L' || c == 'M' ||
           c == 'N' || c == 'R' || c == 'W' || c == 'Z';
}

/* w is " WORD " (padded); match the right context from i forward */
static bool right_ok(const char *w, uint32_t n, uint32_t i, const char *r)
{
    for (; *r; r++) {
        char c = i < n ? w[i] : ' ';
        switch (*r) {
        case ' ':
            if (is_letter(c)) return false;
            i++;
            break;
        case '#':
            if (!is_vowel_l(c)) return false;
            while (i < n && is_vowel_l(w[i])) i++;
            break;
        case ':':
            while (i < n && is_cons_l(w[i])) i++;
            break;
        case '^':
            if (!is_cons_l(c)) return false;
            i++;
            break;
        case '.':
            if (!is_voiced(c)) return false;
            i++;
            break;
        case '+':
            if (c != 'E' && c != 'I' && c != 'Y') return false;
            i++;
            break;
        case '%': {
            static const char *const sfx[] = {"ING", "ELY", "ER", "ES", "ED", "E"};
            bool hit = false;
            for (uint32_t k = 0; k < 6 && !hit; k++) {
                uint32_t L = slen(sfx[k]), j = 0;
                while (j < L && i + j < n && w[i + j] == sfx[k][j]) j++;
                if (j == L) {
                    i += L;
                    hit = true;
                }
            }
            if (!hit) return false;
            break;
        }
        default:
            if (c != *r) return false;
            i++;
        }
    }
    return true;
}

/* the left context, matched backward from i - 1 (pattern read right to left) */
static bool left_ok(const char *w, int32_t i, const char *l)
{
    int32_t k = (int32_t) slen(l) - 1;
    for (; k >= 0; k--) {
        char c = i >= 0 ? w[i] : ' ';
        switch (l[k]) {
        case ' ':
            if (is_letter(c)) return false;
            i--;
            break;
        case '#':
            if (!is_vowel_l(c)) return false;
            while (i >= 0 && is_vowel_l(w[i])) i--;
            break;
        case ':':
            while (i >= 0 && is_cons_l(w[i])) i--;
            break;
        case '^':
            if (!is_cons_l(c)) return false;
            i--;
            break;
        case '.':
            if (!is_voiced(c)) return false;
            i--;
            break;
        case '+':
            if (c != 'E' && c != 'I' && c != 'Y') return false;
            i--;
            break;
        case '&':
            if (c == 'H' && i >= 1 && (w[i - 1] == 'C' || w[i - 1] == 'S'))
                i -= 2;
            else if (c == 'S' || c == 'C' || c == 'G' || c == 'Z' || c == 'X' || c == 'J')
                i--;
            else
                return false;
            break;
        case '@':
            if (c == 'H' && i >= 1 && (w[i - 1] == 'T' || w[i - 1] == 'C' || w[i - 1] == 'S'))
                i -= 2;
            else if (c == 'T' || c == 'S' || c == 'R' || c == 'D' || c == 'L' || c == 'Z' ||
                     c == 'N' || c == 'J')
                i--;
            else
                return false;
            break;
        default:
            if (c != l[k]) return false;
            i--;
        }
    }
    return true;
}

static bool has_prefix(const char *w, uint32_t n, const char *p)
{
    uint32_t k = slen(p);
    if (k >= n) return false;
    for (uint32_t i = 0; i < k; i++)
        if (w[i] != p[i]) return false;
    return true;
}

/* rules for w (lower case a-z and '), then the stress */
static void by_rules(const char *w, uint32_t n, buf_t *b)
{
    char u[SPEECH_G2P_WORD + 2];
    uint32_t start = b->n;
    for (uint32_t base = 0; base < n; base += SPEECH_G2P_WORD) {
        uint32_t m = n - base < SPEECH_G2P_WORD ? n - base : SPEECH_G2P_WORD;
        u[0] = ' ';
        for (uint32_t i = 0; i < m; i++) {
            char c = w[base + i];
            u[i + 1] = c >= 'a' && c <= 'z' ? (char) (c - 32) : c;
        }
        u[m + 1] = ' ';
        uint32_t L = m + 2;
        for (uint32_t i = 1; i < L - 1;) {
            bool done = false;
            for (uint32_t r = 0; r < NRULES && !done; r++) {
                const rule_t *R = &RULES[r];
                if (R->m[0] != u[i]) continue;
                uint32_t k = slen(R->m), j = 0;
                while (j < k && i + j < L && u[i + j] == R->m[j]) j++;
                if (j < k) continue;
                if (!left_ok(u, (int32_t) i - 1, R->l) || !right_ok(u, L, i + k, R->r)) continue;
                puts_(b, R->p);
                i += k;
                done = true;
            }
            if (!done) i++; /* a letter with no rule (none: every letter has a default) */
        }
    }
    if (b->over) return;
    /* G4: primary stress on the first full vowel; past a weak prefix when
     * there is more than one vowel */
    uint32_t nuc[16], nn = 0;
    bool prev_v = false;
    for (uint32_t i = start; i < b->n;) {
        uint32_t at = i, cp = cp_next((const uint8_t *) b->p, b->n, &i);
        bool v = in_set(cp, VOWELS);
        if (v && !prev_v && nn < 16) nuc[nn++] = at;
        prev_v = v;
    }
    if (!nn) return;
    uint32_t pick = 0;
    if (nn > 1 && (has_prefix(w, n, "be") || has_prefix(w, n, "de") || has_prefix(w, n, "re") ||
                   has_prefix(w, n, "com") || has_prefix(w, n, "con") || has_prefix(w, n, "ex") ||
                   has_prefix(w, n, "pre") || has_prefix(w, n, "un")))
        pick = 1;
    else {
        /* skip a schwa when a full vowel follows */
        uint32_t j = nuc[0];
        if (cp_next((const uint8_t *) b->p, b->n, &j) == 0x259 && nn > 1) pick = 1;
    }
    /* insert ˈ at nuc[pick] */
    uint32_t at = nuc[pick];
    if (b->n + 3 > b->cap) {
        b->over = true;
        return;
    }
    for (uint32_t i = b->n; i > at; i--) b->p[i + 1] = b->p[i - 1];
    b->p[at] = (char) 0xCB;
    b->p[at + 1] = (char) 0x88;
    b->n += 2;
    b->p[b->n] = 0;
    /* unstressed ɜɹ is əɹ */
    for (uint32_t i = start; i + 1 < b->n;) {
        uint32_t a0 = i, cp = cp_next((const uint8_t *) b->p, b->n, &i);
        if (cp == 0x25C &&
            !(a0 >= 2 && (uint8_t) b->p[a0 - 2] == 0xCB && (uint8_t) b->p[a0 - 1] == 0x88)) {
            /* ɜ (C9 9C) -> ə (C9 99) */
            b->p[a0 + 1] = (char) 0x99;
        }
    }
}

/* ---- one word (G4) ---- */

static bool all_caps(const char *w, uint32_t n)
{
    if (n < 2) return false;
    for (uint32_t i = 0; i < n; i++)
        if (!(w[i] >= 'A' && w[i] <= 'Z')) return false;
    return true;
}

static bool has_vowel_letter(const char *w, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        char c = (char) (w[i] | 0x20);
        if (c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u' || c == 'y') return true;
    }
    return false;
}

static void word_ph(const ctx_t *c, const char *w, uint32_t n, buf_t *b)
{
    char lw[SPEECH_G2P_WORD];
    if (c->st) c->st->words++;
    if (n >= SPEECH_G2P_WORD) {
        if (c->st) c->st->rules++;
        by_rules(w, n, b);
        return;
    }
    for (uint32_t i = 0; i < n; i++) lw[i] = w[i] >= 'A' && w[i] <= 'Z' ? (char) (w[i] + 32) : w[i];
    if (n == 1 && lw[0] != 'a' && lw[0] != 'i' && lw[0] != '\'') {
        spell(lw, 1, b);
        if (c->st) c->st->spelled++;
        return;
    }
    const char *p = builtin(lw, n);
    if (p) {
        if (c->st) c->st->builtin++;
        puts_(b, p);
        return;
    }
    p = speech_lex_find(c->lex, w, n);
    if (!p) p = speech_lex_find(c->lex, lw, n);
    if (p) {
        if (c->st) c->st->lexicon++;
        puts_(b, p);
        return;
    }
    if (all_caps(w, n) && (n <= 5 || !has_vowel_letter(w, n))) {
        spell(lw, n, b);
        if (c->st) c->st->spelled++;
        return;
    }
    if (stemmed(c, lw, n, b)) return;
    if (compound(c, lw, n, b)) return;
    if (c->st) c->st->rules++;
    by_rules(lw, n, b);
}

int32_t speech_g2p_word(const speech_lex_t *lex, const char *w, uint32_t len, char *out,
                        uint32_t cap)
{
    if (!w || !out || !cap) return -1;
    buf_t b = {out, 0, cap, false};
    out[0] = 0;
    ctx_t c = {lex, 0};
    word_ph(&c, w, len, &b);
    return b.over ? -1 : (int32_t) b.n;
}

/* ---- numbers (G5) ---- */

static const char *const ONES[] = {"zero",    "one",     "two",       "three",    "four",
                                   "five",    "six",     "seven",     "eight",    "nine",
                                   "ten",     "eleven",  "twelve",    "thirteen", "fourteen",
                                   "fifteen", "sixteen", "seventeen", "eighteen", "nineteen"};
static const char *const TENS[] = {"",      "",      "twenty",  "thirty", "forty",
                                   "fifty", "sixty", "seventy", "eighty", "ninety"};
static const char *const SCALE[] = {"", "thousand", "million", "billion", "trillion"};

typedef struct {
    const char *w[48];
    uint32_t n;
} words_t;

static void add(words_t *ws, const char *w)
{
    if (ws->n < 48) ws->w[ws->n++] = w;
}

static void say_999(words_t *ws, uint32_t v)
{
    if (v >= 100) {
        add(ws, ONES[v / 100]);
        add(ws, "hundred");
        v %= 100;
        if (!v) return;
    }
    if (v < 20)
        add(ws, ONES[v]);
    else {
        add(ws, TENS[v / 10]);
        if (v % 10) add(ws, ONES[v % 10]);
    }
}

/* digits d[0..n) (no leading zeros unless n == 1) as a cardinal; n <= 15 */
static void say_int(words_t *ws, const char *d, uint32_t n)
{
    uint32_t g[5] = {0}, ng = (n + 2) / 3;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t from_right = n - 1 - i;
        g[from_right / 3] = g[from_right / 3] * 10 + (uint32_t) (d[i] - '0');
    }
    bool any = false;
    for (int32_t k = (int32_t) ng - 1; k >= 0; k--) {
        if (!g[k]) continue;
        say_999(ws, g[k]);
        if (k) add(ws, SCALE[k]);
        any = true;
    }
    if (!any) add(ws, "zero");
}

static const char *ordinal_of(const char *w)
{
    static const char *const map[][2] = {{"one", "first"},
                                         {"two", "second"},
                                         {"three", "third"},
                                         {"four", "fourth"},
                                         {"five", "fifth"},
                                         {"six", "sixth"},
                                         {"seven", "seventh"},
                                         {"eight", "eighth"},
                                         {"nine", "ninth"},
                                         {"ten", "tenth"},
                                         {"eleven", "eleventh"},
                                         {"twelve", "twelfth"},
                                         {"thirteen", "thirteenth"},
                                         {"fourteen", "fourteenth"},
                                         {"fifteen", "fifteenth"},
                                         {"sixteen", "sixteenth"},
                                         {"seventeen", "seventeenth"},
                                         {"eighteen", "eighteenth"},
                                         {"nineteen", "nineteenth"},
                                         {"twenty", "twentieth"},
                                         {"thirty", "thirtieth"},
                                         {"forty", "fortieth"},
                                         {"fifty", "fiftieth"},
                                         {"sixty", "sixtieth"},
                                         {"seventy", "seventieth"},
                                         {"eighty", "eightieth"},
                                         {"ninety", "ninetieth"},
                                         {"hundred", "hundredth"},
                                         {"thousand", "thousandth"},
                                         {"million", "millionth"},
                                         {"billion", "billionth"}};
    for (uint32_t i = 0; i < sizeof map / sizeof map[0]; i++)
        if (!cmp_word(w, slen(w), (const uint8_t *) map[i][0])) return map[i][1];
    return w;
}

static void say_words(const words_t *ws, buf_t *b)
{
    for (uint32_t i = 0; i < ws->n; i++) {
        const char *p = builtin(ws->w[i], slen(ws->w[i]));
        if (!p) continue;
        if (i) puts_(b, " ");
        puts_(b, p);
    }
}

static bool is_digit(char c)
{
    return c >= '0' && c <= '9';
}

/* a number token at t[*i]: $? -? digits(,ddd)* (.digits)? suffix? %? */
static void number(const ctx_t *c, const char *t, uint32_t len, uint32_t *i, buf_t *b)
{
    uint32_t k = *i;
    bool dollar = false, minus = false, pct = false;
    if (t[k] == '$') dollar = true, k++;
    if (t[k] == '-') minus = true, k++;
    char d[16];
    uint32_t nd = 0;
    bool commas = false, too_long = false;
    while (k < len &&
           (is_digit(t[k]) || (t[k] == ',' && k + 1 < len && is_digit(t[k + 1]) && nd))) {
        if (t[k] == ',')
            commas = true;
        else {
            if (nd < 15)
                d[nd++] = t[k];
            else
                too_long = true;
        }
        k++;
    }
    /* a time: h:mm ("four oh five", "nine thirty", "ten o'clock" as "ten") */
    if (!dollar && !minus && !commas && nd >= 1 && nd <= 2 && k + 2 < len && t[k] == ':' &&
        is_digit(t[k + 1]) && is_digit(t[k + 2]) && !(k + 3 < len && is_digit(t[k + 3]))) {
        words_t tw;
        tw.n = 0;
        say_int(&tw, d, nd);
        char m0 = t[k + 1], m1 = t[k + 2];
        if (m0 == '0' && m1 != '0') {
            add(&tw, "oh");
            add(&tw, ONES[m1 - '0']);
        } else if (m0 != '0') {
            char mm[2] = {m0, m1};
            say_int(&tw, mm, 2);
        }
        *i = k + 3;
        if (c->st) c->st->numbers++;
        say_words(&tw, b);
        return;
    }
    const char *frac = 0;
    uint32_t nfrac = 0;
    if (k + 1 < len && t[k] == '.' && is_digit(t[k + 1])) {
        frac = t + k + 1;
        k++;
        while (k < len && is_digit(t[k])) k++, nfrac++;
    }
    char sfx[3] = {0};
    if (k + 1 < len &&
        ((t[k] == 's' && t[k + 1] == 't') || (t[k] == 'n' && t[k + 1] == 'd') ||
         (t[k] == 'r' && t[k + 1] == 'd') || (t[k] == 't' && t[k + 1] == 'h')) &&
        !(k + 2 < len && ((t[k + 2] | 0x20) >= 'a' && (t[k + 2] | 0x20) <= 'z'))) {
        sfx[0] = t[k];
        sfx[1] = t[k + 1];
        k += 2;
    } else if (k < len && t[k] == 's' &&
               !(k + 1 < len && (t[k + 1] | 0x20) >= 'a' && (t[k + 1] | 0x20) <= 'z')) {
        sfx[0] = 's';
        k++;
    }
    while (k < len && t[k] == ' ' && k + 1 < len && t[k + 1] == '%') k++;
    if (k < len && t[k] == '%') pct = true, k++;
    *i = k;
    if (c->st) c->st->numbers++;

    words_t ws;
    ws.n = 0;
    if (minus) add(&ws, "minus");
    bool year = !dollar && !minus && !pct && !commas && !frac &&
                (!sfx[0] || (sfx[0] == 's' && !sfx[1])) && nd == 4 && d[0] != '0';
    uint32_t v4 = year ? (uint32_t) ((d[0] - '0') * 1000 + (d[1] - '0') * 100 + (d[2] - '0') * 10 +
                                     (d[3] - '0'))
                       : 0;
    if (year && v4 >= 1100 && v4 <= 2099 && !(v4 >= 2000 && v4 <= 2009)) {
        say_999(&ws, v4 / 100);
        uint32_t lo = v4 % 100;
        if (!lo)
            add(&ws, "hundred");
        else if (lo < 10) {
            add(&ws, "oh");
            add(&ws, ONES[lo]);
        } else
            say_999(&ws, lo);
    } else if (too_long || (nd > 1 && d[0] == '0')) {
        for (uint32_t j = 0; j < nd; j++) add(&ws, ONES[d[j] - '0']);
    } else {
        say_int(&ws, d, nd ? nd : 1);
        if (!nd) ws.n = minus ? 1 : 0; /* ".5" */
    }
    bool cents = dollar && frac && nfrac == 2;
    if (frac && !cents) {
        if (!nd) add(&ws, "zero");
        add(&ws, "point");
        for (uint32_t j = 0; j < nfrac && j < 12; j++) add(&ws, ONES[frac[j] - '0']);
    }
    if (sfx[0] && !(sfx[0] == 's' && !sfx[1]) && ws.n) ws.w[ws.n - 1] = ordinal_of(ws.w[ws.n - 1]);
    say_words(&ws, b);
    if ((sfx[0] == 's' && !sfx[1])) suffix_s(b);
    if (dollar) {
        bool one = nd == 1 && d[0] == '1';
        puts_(b, " ");
        puts_(b, builtin("dollar", 6));
        if (!one) suffix_s(b);
        if (cents && !(frac[0] == '0' && frac[1] == '0')) {
            words_t cw;
            cw.n = 0;
            char cd[2] = {frac[0], frac[1]};
            if (cd[0] == '0')
                say_int(&cw, cd + 1, 1);
            else
                say_int(&cw, cd, 2);
            puts_(b, " ");
            puts_(b, builtin("and", 3));
            puts_(b, " ");
            say_words(&cw, b);
            puts_(b, " ");
            puts_(b, builtin("cent", 4));
            if (!(cd[0] == '0' && cd[1] == '1')) suffix_s(b);
        }
    }
    if (pct) {
        puts_(b, " ");
        puts_(b, builtin("percent", 7));
    }
}

/* ---- the sentence (G2, G6) ---- */

enum { SP_NONE = 0, SP_THE, SP_TO, SP_A, SP_AM };

typedef struct {
    buf_t *out;
    int pending;
    bool open; /* the last thing written opens (no space after it) */
} sent_t;

/* true / false: the phonemes start with a vowel / consonant; -1: none */
static int starts_vowel(const char *p)
{
    uint32_t n = slen(p);
    for (uint32_t i = 0; i < n;) {
        uint32_t cp = cp_next((const uint8_t *) p, n, &i);
        if (in_set(cp, VOWELS)) return 1;
        if (in_set(cp, CONSONANTS)) return 0;
    }
    return -1;
}

static void space(sent_t *s)
{
    buf_t *b = s->out;
    if (b->n && !s->open && b->p[b->n - 1] != ' ') puts_(b, " ");
    s->open = false;
}

/* settle a held "the", "to", "a" or "am" by what follows: 1 vowel, 0
 * consonant, -1 a pause */
static void settle(sent_t *s, int next)
{
    if (!s->pending) return;
    const char *p = "";
    switch (s->pending) {
    case SP_THE:
        p = next == 1 ? "ði" : "ðə";
        break;
    case SP_TO:
        p = next == 1 ? "tʊ" : next == 0 ? "tə" : "tu";
        break;
    case SP_A:
        p = next == -1 ? "ˈA" : "ɐ";
        break;
    case SP_AM:
        p = next == 1 ? "ɐm" : "æm";
        break;
    }
    s->pending = SP_NONE;
    space(s);
    puts_(s->out, p);
}

static void emit_word(sent_t *s, const char *p)
{
    settle(s, starts_vowel(p));
    space(s);
    puts_(s->out, p);
}

static void emit_punct(sent_t *s, const char *p, bool opening)
{
    settle(s, -1);
    buf_t *b = s->out;
    if (opening) {
        space(s);
        puts_(b, p);
        s->open = true;
        return;
    }
    while (b->n && b->p[b->n - 1] == ' ') b->p[--b->n] = 0;
    if (p[0] == (char) 0xE2 && p[1] == (char) 0x80 && p[2] == (char) 0x94) puts_(b, " ");
    puts_(b, p);
}

static bool is_alpha(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

int32_t speech_g2p_en(const speech_lex_t *lex, const char *text, uint32_t len, char *out,
                      uint32_t cap, speech_g2p_stats_t *st)
{
    if (!text || !out || !cap) return -1;
    if (st) {
        speech_g2p_stats_t z = {0, 0, 0, 0, 0, 0, 0, 0};
        *st = z;
    }
    buf_t b = {out, 0, cap, false};
    out[0] = 0;
    ctx_t c = {lex, st};
    sent_t s = {&b, SP_NONE, false};
    char w[SPEECH_G2P_WORD * 2];
    char tmp[512];
    const uint8_t *u = (const uint8_t *) text;
    for (uint32_t i = 0; i < len && !b.over;) {
        char ch = text[i];
        bool lead_num =
            is_digit(ch) ||
            ((ch == '$' || ch == '-' || ch == '.') && i + 1 < len && is_digit(text[i + 1]) &&
             (ch != '-' || i == 0 || !is_alpha(text[i - 1]))) ||
            (ch == '$' && i + 2 < len && text[i + 1] == '-' && is_digit(text[i + 2]));
        if (is_alpha(ch)) {
            uint32_t n = 0;
            while (i < len) {
                char x = text[i];
                bool apos = x == '\'' ||
                            (u[i] == 0xE2 && i + 2 < len && u[i + 1] == 0x80 && u[i + 2] == 0x99);
                uint32_t aw = x == '\'' ? 1 : 3;
                if (is_alpha(x)) {
                    if (n < sizeof w) w[n++] = x;
                    i++;
                } else if (apos && n &&
                           ((i + aw < len && is_alpha(text[i + aw])) || (w[n - 1] | 0x20) == 's')) {
                    if (n < sizeof w) w[n++] = '\'';
                    i += aw;
                } else
                    break;
            }
            /* G2: the context words */
            bool lower = w[0] >= 'a';
            if (n == 3 && (w[0] | 0x20) == 't' && (w[1] | 0x20) == 'h' && (w[2] | 0x20) == 'e') {
                settle(&s, 0);
                s.pending = SP_THE;
                if (st) st->words++;
                continue;
            }
            if (n == 2 && (w[0] | 0x20) == 't' && (w[1] | 0x20) == 'o' && (lower || w[1] == 'o')) {
                settle(&s, 0);
                s.pending = SP_TO;
                if (st) st->words++;
                continue;
            }
            if (n == 1 && (w[0] == 'a' || w[0] == 'A')) {
                settle(&s, 1);
                s.pending = SP_A;
                if (st) st->words++;
                continue;
            }
            if (n == 2 && w[0] == 'a' && w[1] == 'm') {
                settle(&s, 1);
                s.pending = SP_AM;
                if (st) st->words++;
                continue;
            }
            if (n == 2 && (w[0] == 'a' || w[0] == 'A') && w[1] == 'n') {
                emit_word(&s, "ɐn");
                if (st) st->words++;
                continue;
            }
            if (n == 1 && w[0] == 'I') {
                emit_word(&s, "ˌI");
                if (st) st->words++;
                continue;
            }
            buf_t wb = {tmp, 0, sizeof tmp, false};
            tmp[0] = 0;
            word_ph(&c, w, n, &wb);
            if (wb.over) {
                b.over = true;
                break;
            }
            if (wb.n) emit_word(&s, tmp);
        } else if (lead_num) {
            buf_t nb = {tmp, 0, sizeof tmp, false};
            tmp[0] = 0;
            number(&c, text, len, &i, &nb);
            if (nb.over) {
                b.over = true;
                break;
            }
            if (nb.n) emit_word(&s, tmp);
        } else if (ch == '.' && i && is_alpha(text[i - 1]) && i + 1 < len &&
                   is_alpha(text[i + 1])) {
            /* github.com: a dot inside a name is said */
            emit_word(&s, "dˈɑt");
            if (st) st->words++;
            i++;
        } else if (ch == ',' || ch == '.' || ch == '!' || ch == '?' || ch == ';' || ch == ':') {
            char p[2] = {ch, 0};
            /* "..." is an ellipsis */
            if (ch == '.' && i + 2 < len && text[i + 1] == '.' && text[i + 2] == '.') {
                emit_punct(&s, "…", false);
                i += 3;
                continue;
            }
            emit_punct(&s, p, false);
            i++;
        } else if (ch == '(' || ch == ')') {
            char p[2] = {ch, 0};
            emit_punct(&s, p, ch == '(');
            i++;
        } else if (ch == '-') {
            /* " - " and "--" are a dash; a hyphen joins nothing (a space) */
            bool dash = (i + 1 < len && text[i + 1] == '-') ||
                        ((i == 0 || text[i - 1] == ' ') && (i + 1 >= len || text[i + 1] == ' '));
            if (dash) emit_punct(&s, "—", false);
            while (i < len && text[i] == '-') i++;
        } else if (ch == '&' || ch == '+' || ch == '@' || ch == '%') {
            const char *wd = ch == '&' ? "and" : ch == '+' ? "plus" : ch == '@' ? "at" : "percent";
            emit_word(&s, builtin(wd, slen(wd)));
            if (st) st->words++;
            i++;
        } else if (u[i] >= 0x80) {
            uint32_t j = i, cp = cp_next(u, len, &j);
            if (cp == 0x2014 || cp == 0x2013)
                emit_punct(&s, "—", false);
            else if (cp == 0x2026)
                emit_punct(&s, "…", false);
            else if (cp == 0x201C)
                emit_punct(&s, "“", true);
            else if (cp == 0x201D)
                emit_punct(&s, "”", false);
            i = j > i ? j : i + 1;
        } else {
            i++; /* spaces and symbols with no sound */
        }
    }
    settle(&s, -1);
    while (b.n && b.p[b.n - 1] == ' ') b.p[--b.n] = 0;
    return b.over ? -1 : (int32_t) b.n;
}
