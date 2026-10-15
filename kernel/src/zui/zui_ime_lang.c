/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Tables derived from the Unicode Han Database (Unihan), Copyright (c)
 * Unicode, Inc., under the Unicode License v3
 * (https://www.unicode.org/license.txt). */
/* zui_ime_lang.c — the input methods of languages a keyboard cannot type
 * directly (zui_ime.h M6): Chinese, Cantonese, Japanese, Korean and the
 * Ge'ez syllables of Amharic and Tigrinya. */
#include "zui_ime.h"

static uint32_t slen(const char *s)
{
    uint32_t n = 0;
    while (s && s[n]) n++;
    return n;
}

static bool sequal(const char *a, const char *b)
{
    uint32_t i = 0;
    for (; a[i] && a[i] == b[i]; i++) {
    }
    return a[i] == b[i];
}

/* append cp to a NUL-terminated buffer of cap bytes */
static void put_cp(char *buf, uint32_t cap, uint32_t cp)
{
    uint32_t n = slen(buf);
    char b[5];
    uint32_t k = zui_km_utf8_put(b, cp);
    if (n + k >= cap) return;
    for (uint32_t i = 0; i < k; i++) buf[n + i] = b[i];
    buf[n + k] = 0;
}

static void put_str(char *buf, uint32_t cap, const char *s)
{
    uint32_t n = slen(buf), k = slen(s);
    if (n + k >= cap) return;
    for (uint32_t i = 0; i <= k; i++) buf[n + i] = s[i];
}

/* the candidates of a space-separated list */
static void add_list(zui_ime_t *ime, const char *list, uint8_t used)
{
    uint32_t at = 0, n = slen(list);
    while (at < n) {
        while (at < n && list[at] == ' ') at++;
        uint32_t s = at;
        while (at < n && list[at] != ' ') at++;
        if (at > s) zui_ime_cand_add(ime, list + s, at - s, used);
    }
}

/* ---- Chinese and Cantonese: the table engine over Unihan ------------------------- */

#include "zui_ime_han.inc"

#define DICT(t) {t, (uint32_t) (sizeof t / sizeof t[0])}
const zui_ime_dict_t zui_pinyin_dict = DICT(HAN_PINYIN);
const zui_ime_dict_t zui_pinyin_hant_dict = DICT(HAN_PINYIN_HANT);
const zui_ime_dict_t zui_jyutping_dict = DICT(HAN_JYUTPING);
const zui_ime_dict_t zui_kana_dict = DICT(HAN_KANA);

const zui_ime_engine_t zui_ime_pinyin = {"pinyin",
                                         "Pinyin (Simplified Chinese)",
                                         &zui_pinyin_dict,
                                         zui_ime_table_reset,
                                         zui_ime_table_key,
                                         zui_ime_table_text,
                                         zui_ime_table_commit,
                                         zui_ime_table_cancel,
                                         zui_ime_table_select};
const zui_ime_engine_t zui_ime_pinyin_hant = {"pinyin-hant",         "Pinyin (Traditional Chinese)",
                                              &zui_pinyin_hant_dict, zui_ime_table_reset,
                                              zui_ime_table_key,     zui_ime_table_text,
                                              zui_ime_table_commit,  zui_ime_table_cancel,
                                              zui_ime_table_select};
const zui_ime_engine_t zui_ime_jyutping = {
    "jyutping",           "Jyutping (Cantonese)", &zui_jyutping_dict,
    zui_ime_table_reset,  zui_ime_table_key,      zui_ime_table_text,
    zui_ime_table_commit, zui_ime_table_cancel,   zui_ime_table_select};

/* ---- Korean: two-set Hangul ----------------------------------------------------- */

/* compatibility jamo typed by the letters a..z (base) and their shifted
 * forms (Q W E R T O P give the double consonants and two vowels) */
static const uint16_t HG_BASE[26] = {0x3141, 0x3160, 0x314A, 0x3147, 0x3137, 0x3139, 0x314E,
                                     0x3157, 0x3151, 0x3153, 0x314F, 0x3163, 0x3161, 0x315C,
                                     0x3150, 0x3154, 0x3142, 0x3131, 0x3134, 0x3145, 0x3155,
                                     0x314D, 0x3148, 0x314C, 0x315B, 0x314B};
static uint32_t hg_jamo(uint32_t cp)
{
    switch (cp) {
    case 'Q':
        return 0x3143;
    case 'W':
        return 0x3149;
    case 'E':
        return 0x3138;
    case 'R':
        return 0x3132;
    case 'T':
        return 0x3146;
    case 'O':
        return 0x3152;
    case 'P':
        return 0x3156;
    default:
        break;
    }
    if (cp >= 'A' && cp <= 'Z') cp += 32;
    return cp >= 'a' && cp <= 'z' ? HG_BASE[cp - 'a'] : 0;
}

static const uint16_t HG_L[19] = {0x3131, 0x3132, 0x3134, 0x3137, 0x3138, 0x3139, 0x3141,
                                  0x3142, 0x3143, 0x3145, 0x3146, 0x3147, 0x3148, 0x3149,
                                  0x314A, 0x314B, 0x314C, 0x314D, 0x314E};
static const uint16_t HG_T[28] = {0,      0x3131, 0x3132, 0x3133, 0x3134, 0x3135, 0x3136,
                                  0x3137, 0x3139, 0x313A, 0x313B, 0x313C, 0x313D, 0x313E,
                                  0x313F, 0x3140, 0x3141, 0x3142, 0x3144, 0x3145, 0x3146,
                                  0x3147, 0x3148, 0x314A, 0x314B, 0x314C, 0x314D, 0x314E};
/* two jamo that make one: vowels, then final consonants */
static const uint16_t HG_PAIR[][3] = {
    {0x3157, 0x314F, 0x3158}, {0x3157, 0x3150, 0x3159}, {0x3157, 0x3163, 0x315A},
    {0x315C, 0x3153, 0x315D}, {0x315C, 0x3154, 0x315E}, {0x315C, 0x3163, 0x315F},
    {0x3161, 0x3163, 0x3162}, {0x3131, 0x3145, 0x3133}, {0x3134, 0x3148, 0x3135},
    {0x3134, 0x314E, 0x3136}, {0x3139, 0x3131, 0x313A}, {0x3139, 0x3141, 0x313B},
    {0x3139, 0x3142, 0x313C}, {0x3139, 0x3145, 0x313D}, {0x3139, 0x314C, 0x313E},
    {0x3139, 0x314D, 0x313F}, {0x3139, 0x314E, 0x3140}, {0x3142, 0x3145, 0x3144}};

static bool hg_vowel(uint32_t j)
{
    return j >= 0x314F && j <= 0x3163;
}
static int32_t hg_index(const uint16_t *t, uint32_t n, uint32_t j)
{
    for (uint32_t i = 0; i < n; i++)
        if (t[i] == j) return (int32_t) i;
    return -1;
}
static uint32_t hg_pair(uint32_t a, uint32_t b)
{
    for (uint32_t i = 0; i < sizeof HG_PAIR / sizeof HG_PAIR[0]; i++)
        if (HG_PAIR[i][0] == a && HG_PAIR[i][1] == b) return HG_PAIR[i][2];
    return 0;
}

typedef struct {
    uint32_t l, v, t, t1, t2; /* jamo, 0 none; t1, t2 the final's parts */
    uint32_t start, t2_at;    /* key index of the syllable and of t2 (or t1) */
} hg_syl_t;

static uint32_t hg_cp(const hg_syl_t *s)
{
    if (s->l && s->v) {
        int32_t L = hg_index(HG_L, 19, s->l), T = s->t ? hg_index(HG_T, 28, s->t) : 0;
        return 0xAC00u + ((uint32_t) L * 21u + (s->v - 0x314Fu)) * 28u + (uint32_t) T;
    }
    return s->l ? s->l : s->v;
}

/* Run the two-set automaton over the jamo typed: every finished syllable is
 * written to done (when not 0) and the open one is left in *cur. */
static void hg_run(const uint16_t *k, uint32_t n, char *done, uint32_t cap, hg_syl_t *cur)
{
    hg_syl_t s = {0, 0, 0, 0, 0, 0, 0};
    for (uint32_t i = 0; i < n; i++) {
        uint32_t j = k[i], p;
        hg_syl_t nx = {0, 0, 0, 0, 0, i, 0};
        if (hg_vowel(j)) {
            if (s.v && !s.t && (p = hg_pair(s.v, j)) != 0) {
                s.v = p;
                continue;
            }
            if (s.l && !s.v) {
                s.v = j;
                continue;
            }
            if (s.t) { /* the last final consonant starts the next syllable */
                if (s.t2) {
                    nx.l = s.t2;
                    s.t = s.t1;
                    s.t2 = 0;
                } else {
                    nx.l = s.t1;
                    s.t = s.t1 = 0;
                }
                nx.start = s.t2_at;
            }
            nx.v = j;
        } else {
            if (s.l && s.v && !s.t && hg_index(HG_T, 28, j) > 0) {
                s.t = s.t1 = j;
                s.t2_at = i;
                continue;
            }
            if (s.t && !s.t2 && (p = hg_pair(s.t, j)) != 0) {
                s.t = p;
                s.t2 = j;
                s.t2_at = i;
                continue;
            }
            nx.l = j;
        }
        if (s.l || s.v) {
            if (done) put_cp(done, cap, hg_cp(&s));
        }
        s = nx;
    }
    *cur = s;
}

typedef struct {
    uint16_t k[16];
    uint32_t n;
} hg_state_t;

static void hg_show(zui_ime_t *ime)
{
    hg_state_t *st = (hg_state_t *) (void *) ime->st.raw;
    char done[48] = {0}, pre[8] = {0};
    hg_syl_t cur;
    hg_run(st->k, st->n, done, sizeof done, &cur);
    if (done[0]) { /* finished syllables go to the app; keep the open one */
        zui_ime_send(ime, done);
        uint32_t from = cur.start;
        for (uint32_t i = from; i < st->n; i++) st->k[i - from] = st->k[i];
        st->n -= from;
    }
    if (cur.l || cur.v) put_cp(pre, sizeof pre, hg_cp(&cur));
    zui_ime_set_preedit(ime, pre);
}

static void hg_reset(zui_ime_t *ime)
{
    ((hg_state_t *) (void *) ime->st.raw)->n = 0;
}

static void hg_commit(zui_ime_t *ime)
{
    zui_ime_send(ime, ime->pre);
    hg_reset(ime);
}

static bool hg_key(zui_ime_t *ime, uint16_t key, uint8_t mods)
{
    (void) mods;
    hg_state_t *st = (hg_state_t *) (void *) ime->st.raw;
    if (key == ZUI_KEY_BACKSPACE && st->n) {
        st->n--;
        hg_show(ime);
        return true;
    }
    return false; /* anything else commits the syllable and goes on */
}

static bool hg_text(zui_ime_t *ime, uint32_t cp)
{
    hg_state_t *st = (hg_state_t *) (void *) ime->st.raw;
    uint32_t j = hg_jamo(cp);
    if (!j) return false;
    if (st->n == 16) { /* cannot happen with the automaton; stay safe */
        hg_commit(ime);
        zui_ime_set_preedit(ime, "");
    }
    st->k[st->n++] = (uint16_t) j;
    hg_show(ime);
    return true;
}

const zui_ime_engine_t zui_ime_hangul = {"hangul", "Hangul (two-set)", 0,        hg_reset, hg_key,
                                         hg_text,  hg_commit,          hg_reset, 0};

/* ---- Japanese: romaji to kana, then kanji ---------------------------------------- */

static const struct {
    const char *ro, *kana;
} KANA[] = {
    {"a", "あ"},     {"i", "い"},     {"u", "う"},     {"e", "え"},     {"o", "お"},
    {"ka", "か"},    {"ki", "き"},    {"ku", "く"},    {"ke", "け"},    {"ko", "こ"},
    {"ga", "が"},    {"gi", "ぎ"},    {"gu", "ぐ"},    {"ge", "げ"},    {"go", "ご"},
    {"sa", "さ"},    {"shi", "し"},   {"si", "し"},    {"su", "す"},    {"se", "せ"},
    {"so", "そ"},    {"za", "ざ"},    {"ji", "じ"},    {"zi", "じ"},    {"zu", "ず"},
    {"ze", "ぜ"},    {"zo", "ぞ"},    {"ta", "た"},    {"chi", "ち"},   {"ti", "ち"},
    {"tsu", "つ"},   {"tu", "つ"},    {"te", "て"},    {"to", "と"},    {"da", "だ"},
    {"di", "ぢ"},    {"du", "づ"},    {"de", "で"},    {"do", "ど"},    {"na", "な"},
    {"ni", "に"},    {"nu", "ぬ"},    {"ne", "ね"},    {"no", "の"},    {"ha", "は"},
    {"hi", "ひ"},    {"fu", "ふ"},    {"hu", "ふ"},    {"he", "へ"},    {"ho", "ほ"},
    {"ba", "ば"},    {"bi", "び"},    {"bu", "ぶ"},    {"be", "べ"},    {"bo", "ぼ"},
    {"pa", "ぱ"},    {"pi", "ぴ"},    {"pu", "ぷ"},    {"pe", "ぺ"},    {"po", "ぽ"},
    {"ma", "ま"},    {"mi", "み"},    {"mu", "む"},    {"me", "め"},    {"mo", "も"},
    {"ya", "や"},    {"yu", "ゆ"},    {"yo", "よ"},    {"ra", "ら"},    {"ri", "り"},
    {"ru", "る"},    {"re", "れ"},    {"ro", "ろ"},    {"wa", "わ"},    {"wo", "を"},
    {"nn", "ん"},    {"n'", "ん"},    {"vu", "ゔ"},    {"kya", "きゃ"}, {"kyu", "きゅ"},
    {"kyo", "きょ"}, {"gya", "ぎゃ"}, {"gyu", "ぎゅ"}, {"gyo", "ぎょ"}, {"sha", "しゃ"},
    {"shu", "しゅ"}, {"sho", "しょ"}, {"she", "しぇ"}, {"sya", "しゃ"}, {"syu", "しゅ"},
    {"syo", "しょ"}, {"ja", "じゃ"},  {"ju", "じゅ"},  {"jo", "じょ"},  {"je", "じぇ"},
    {"jya", "じゃ"}, {"jyu", "じゅ"}, {"jyo", "じょ"}, {"zya", "じゃ"}, {"zyu", "じゅ"},
    {"zyo", "じょ"}, {"cha", "ちゃ"}, {"chu", "ちゅ"}, {"cho", "ちょ"}, {"che", "ちぇ"},
    {"tya", "ちゃ"}, {"tyu", "ちゅ"}, {"tyo", "ちょ"}, {"nya", "にゃ"}, {"nyu", "にゅ"},
    {"nyo", "にょ"}, {"hya", "ひゃ"}, {"hyu", "ひゅ"}, {"hyo", "ひょ"}, {"bya", "びゃ"},
    {"byu", "びゅ"}, {"byo", "びょ"}, {"pya", "ぴゃ"}, {"pyu", "ぴゅ"}, {"pyo", "ぴょ"},
    {"mya", "みゃ"}, {"myu", "みゅ"}, {"myo", "みょ"}, {"rya", "りゃ"}, {"ryu", "りゅ"},
    {"ryo", "りょ"}, {"fa", "ふぁ"},  {"fi", "ふぃ"},  {"fe", "ふぇ"},  {"fo", "ふぉ"},
    {"thi", "てぃ"}, {"dhi", "でぃ"}, {"xa", "ぁ"},    {"xi", "ぃ"},    {"xu", "ぅ"},
    {"xe", "ぇ"},    {"xo", "ぉ"},    {"la", "ぁ"},    {"li", "ぃ"},    {"lu", "ぅ"},
    {"le", "ぇ"},    {"lo", "ぉ"},    {"xya", "ゃ"},   {"xyu", "ゅ"},   {"xyo", "ょ"},
    {"lya", "ゃ"},   {"lyu", "ゅ"},   {"lyo", "ょ"},   {"xtu", "っ"},   {"ltu", "っ"},
    {"xtsu", "っ"},  {"xwa", "ゎ"},   {"-", "ー"},     {".", "。"},     {",", "、"},
    {"[", "「"},     {"]", "」"},
};
#define NKANA (sizeof KANA / sizeof KANA[0])

typedef struct {
    char ro[40]; /* what was typed */
    uint32_t n;
} kana_state_t;

static bool kana_vowel(char c)
{
    return c == 'a' || c == 'i' || c == 'u' || c == 'e' || c == 'o';
}

/* romaji -> kana; *pending is the start of a tail that may still grow into
 * a syllable (n when finish is false) */
static void kana_convert(const char *ro, uint32_t n, bool finish, char *out, uint32_t cap,
                         uint32_t *pending)
{
    out[0] = 0;
    uint32_t i = 0;
    while (i < n) {
        uint32_t best = 0, bi = 0;
        for (uint32_t k = 0; k < NKANA; k++) {
            uint32_t L = slen(KANA[k].ro);
            if (L <= best || i + L > n) continue;
            uint32_t m = 0;
            while (m < L && ro[i + m] == KANA[k].ro[m]) m++;
            if (m == L) best = L, bi = k;
        }
        bool prefix = false; /* the rest starts a longer syllable */
        for (uint32_t k = 0; k < NKANA && !prefix; k++) {
            uint32_t L = slen(KANA[k].ro), m = 0;
            if (L <= n - i) continue;
            while (i + m < n && ro[i + m] == KANA[k].ro[m]) m++;
            prefix = i + m == n;
        }
        char c = ro[i];
        if (prefix && !finish) break;
        if (best) {
            put_str(out, cap, KANA[bi].kana);
            i += best;
        } else if (i + 1 < n && c == ro[i + 1] && c != 'n' && c >= 'a' && c <= 'z' &&
                   !kana_vowel(c)) {
            put_str(out, cap, "っ");
            i++;
        } else if (c == 'n') {
            put_str(out, cap, "ん");
            i++;
        } else {
            put_cp(out, cap, (uint8_t) c);
            i++;
        }
    }
    *pending = i;
}

static void kana_show(zui_ime_t *ime)
{
    kana_state_t *st = (kana_state_t *) (void *) ime->st.raw;
    char kana[ZUI_IME_PRE_MAX], kata[ZUI_IME_PRE_MAX];
    uint32_t pend;
    kana_convert(st->ro, st->n, false, kana, sizeof kana, &pend);
    char pre[ZUI_IME_PRE_MAX];
    pre[0] = 0;
    put_str(pre, sizeof pre, kana);
    for (uint32_t i = pend; i < st->n; i++) put_cp(pre, sizeof pre, (uint8_t) st->ro[i]);
    zui_ime_set_preedit(ime, pre);
    zui_ime_cands_clear(ime);
    if (!st->n) return;
    kana_convert(st->ro, st->n, true, kana, sizeof kana, &pend);
    /* hiragana, katakana, then the kanji read that way */
    uint32_t n = slen(kana), at = 0;
    kata[0] = 0;
    while (at < n) {
        uint32_t cp;
        at += zui_km_utf8_get(kana + at, n - at, &cp);
        put_cp(kata, sizeof kata, cp >= 0x3041 && cp <= 0x3096 ? cp + 0x60 : cp);
    }
    zui_ime_cand_add(ime, kana, n, 0);
    if (!sequal(kata, kana)) zui_ime_cand_add(ime, kata, slen(kata), 0);
    for (uint32_t i = 0; i < zui_kana_dict.n; i++)
        if (sequal(zui_kana_dict.e[i].keys, kana)) add_list(ime, zui_kana_dict.e[i].cands, 0);
}

static void kana_reset(zui_ime_t *ime)
{
    kana_state_t *st = (kana_state_t *) (void *) ime->st.raw;
    st->n = 0;
    st->ro[0] = 0;
}

static void kana_commit(zui_ime_t *ime)
{
    kana_state_t *st = (kana_state_t *) (void *) ime->st.raw;
    char kana[ZUI_IME_PRE_MAX];
    uint32_t pend;
    kana_convert(st->ro, st->n, true, kana, sizeof kana, &pend);
    zui_ime_send(ime, kana);
    kana_reset(ime);
}

static void kana_select(zui_ime_t *ime, uint32_t i)
{
    if (i >= ime->ncand) return;
    zui_ime_send(ime, ime->cand[i].text);
    kana_reset(ime);
    zui_ime_set_preedit(ime, "");
}

static bool kana_key(zui_ime_t *ime, uint16_t key, uint8_t mods)
{
    (void) mods;
    kana_state_t *st = (kana_state_t *) (void *) ime->st.raw;
    switch (key) {
    case ZUI_KEY_BACKSPACE:
        if (st->n) st->n--;
        kana_show(ime);
        return true;
    case ZUI_KEY_ESCAPE:
        kana_reset(ime);
        zui_ime_set_preedit(ime, "");
        return true;
    case ZUI_KEY_ENTER:
        kana_commit(ime);
        zui_ime_set_preedit(ime, "");
        return true;
    default:
        return zui_ime_cand_nav(ime, key);
    }
}

static bool kana_text(zui_ime_t *ime, uint32_t cp)
{
    kana_state_t *st = (kana_state_t *) (void *) ime->st.raw;
    if (cp >= 'A' && cp <= 'Z') cp += 32;
    bool typed = (cp >= 'a' && cp <= 'z') || cp == '-' || cp == '.' || cp == ',' || cp == '[' ||
                 cp == ']' || (cp == '\'' && st->n && st->ro[st->n - 1] == 'n');
    if (typed) {
        if (st->n < sizeof st->ro - 1) {
            st->ro[st->n++] = (char) cp;
            st->ro[st->n] = 0;
            kana_show(ime);
        }
        return true;
    }
    if (!zui_ime_composing(ime)) return false;
    if (cp == ' ') {
        kana_select(ime, ime->sel);
        return true;
    }
    int32_t d = zui_ime_cand_digit(ime, cp);
    if (d >= 0) {
        kana_select(ime, (uint32_t) d);
        return true;
    }
    return false;
}

const zui_ime_engine_t zui_ime_kana = {"kana",      "Romaji to kana and kanji",
                                       0,           kana_reset,
                                       kana_key,    kana_text,
                                       kana_commit, kana_reset,
                                       kana_select};

/* ---- Amharic and Tigrinya: Ge'ez syllables from Latin letters (SERA) ------------- */

/* the first form of each consonant's row (orders 1..7 then the wa form follow) */
static uint32_t geez_base(uint32_t c, bool tick)
{
    if (tick) {
        switch (c) {
        case 's':
            return 0x1220; /* ሠ */
        case 'h':
            return 0x1280; /* ኀ */
        case 'S':
            return 0x1340; /* ፀ */
        default:
            return 0;
        }
    }
    switch (c) {
    case 'h':
        return 0x1200;
    case 'l':
        return 0x1208;
    case 'H':
        return 0x1210;
    case 'm':
        return 0x1218;
    case 'r':
        return 0x1228;
    case 's':
        return 0x1230;
    case 'x':
        return 0x1238;
    case 'q':
        return 0x1240;
    case 'Q':
        return 0x1250;
    case 'b':
        return 0x1260;
    case 'v':
        return 0x1268;
    case 't':
        return 0x1270;
    case 'c':
        return 0x1278;
    case 'n':
        return 0x1290;
    case 'N':
        return 0x1298;
    case 'k':
        return 0x12A8;
    case 'K':
        return 0x12B8;
    case 'w':
        return 0x12C8;
    case 'z':
        return 0x12D8;
    case 'Z':
        return 0x12E0;
    case 'y':
        return 0x12E8;
    case 'd':
        return 0x12F0;
    case 'D':
        return 0x12F8;
    case 'j':
        return 0x1300;
    case 'g':
        return 0x1308;
    case 'G':
        return 0x1318;
    case 'T':
        return 0x1320;
    case 'C':
        return 0x1328;
    case 'P':
        return 0x1330;
    case 'S':
        return 0x1338;
    case 'f':
        return 0x1348;
    case 'p':
        return 0x1350;
    default:
        return 0;
    }
}

/* the order a vowel letter selects: e ä, u, i, a, E é, I ə, o, W wa (W then
 * the vowel a, as in lWa) */
static int32_t geez_order(uint32_t c)
{
    switch (c) {
    case 'e':
        return 0;
    case 'u':
    case 'U':
        return 1;
    case 'i':
        return 2;
    case 'a':
    case 'A':
        return 3;
    case 'E':
        return 4;
    case 'I':
        return 5;
    case 'o':
    case 'O':
        return 6;
    case 'W':
        return 7;
    default:
        return -1;
    }
}

typedef struct {
    uint32_t base; /* the consonant waiting for its vowel, 0 none */
    bool tick;     /* ` typed: the next letter is ሠ ኀ ፀ or the ዐ row */
    bool colon;    /* : typed: a second : makes ። */
    bool wa;       /* W typed after the consonant: its wa form, a vowel ends it */
} geez_state_t;

static void geez_show(zui_ime_t *ime)
{
    geez_state_t *st = (geez_state_t *) (void *) ime->st.raw;
    char pre[8] = {0};
    if (st->base) put_cp(pre, sizeof pre, st->base + (st->wa ? 7u : 5u));
    if (st->tick) put_cp(pre, sizeof pre, '`');
    if (st->colon) put_cp(pre, sizeof pre, 0x1361); /* ፡ */
    zui_ime_set_preedit(ime, pre);
}

static void geez_reset(zui_ime_t *ime)
{
    geez_state_t *st = (geez_state_t *) (void *) ime->st.raw;
    st->base = 0;
    st->tick = st->colon = st->wa = false;
}

static void geez_flush(zui_ime_t *ime)
{
    geez_state_t *st = (geez_state_t *) (void *) ime->st.raw;
    if (st->base) zui_ime_send_cp(ime, st->base + (st->wa ? 7u : 5u));
    if (st->tick) zui_ime_send(ime, "`");
    if (st->colon) zui_ime_send_cp(ime, 0x1361);
    geez_reset(ime);
}

static void geez_commit(zui_ime_t *ime)
{
    geez_flush(ime);
}

static bool geez_key(zui_ime_t *ime, uint16_t key, uint8_t mods)
{
    (void) mods;
    geez_state_t *st = (geez_state_t *) (void *) ime->st.raw;
    if (key == ZUI_KEY_BACKSPACE) {
        if (st->colon)
            st->colon = false;
        else if (st->wa)
            st->wa = false;
        else if (st->tick)
            st->tick = false;
        else
            st->base = 0;
        geez_show(ime);
        return true;
    }
    if (key == ZUI_KEY_ESCAPE) {
        geez_reset(ime);
        geez_show(ime);
        return true;
    }
    return false;
}

static bool geez_text(zui_ime_t *ime, uint32_t cp)
{
    geez_state_t *st = (geez_state_t *) (void *) ime->st.raw;
    if (cp == ':') {
        if (st->colon) {
            st->colon = false;
            geez_flush(ime);
            zui_ime_send_cp(ime, 0x1362); /* ። */
        } else {
            geez_flush(ime);
            st->colon = true;
        }
        geez_show(ime);
        return true;
    }
    if (cp == ',' || cp == ';') {
        geez_flush(ime);
        zui_ime_send_cp(ime, cp == ',' ? 0x1363 : 0x1364); /* ፣ ፤ */
        geez_show(ime);
        return true;
    }
    if (cp == '`') {
        geez_flush(ime);
        st->tick = true;
        geez_show(ime);
        return true;
    }
    int32_t ord = geez_order(cp);
    if (ord >= 0) {
        if (st->base && ord == 7) {
            st->wa = true;
        } else if (st->base) {
            zui_ime_send_cp(ime, st->base + (st->wa ? 7u : (uint32_t) ord));
            st->base = 0;
            st->wa = false;
        } else {
            bool tick = st->tick;
            st->tick = false;
            geez_flush(ime);
            zui_ime_send_cp(ime, (tick ? 0x12D0u : 0x12A0u) + (uint32_t) (ord == 7 ? 3 : ord));
        }
        geez_show(ime);
        return true;
    }
    uint32_t b = geez_base(cp, st->tick);
    if (!b && st->tick) b = geez_base(cp, false);
    if (b) {
        st->tick = false;
        geez_flush(ime);
        st->base = b;
        geez_show(ime);
        return true;
    }
    return false;
}

const zui_ime_engine_t zui_ime_geez = {
    "geez", "Ge'ez syllables (SERA)", 0, geez_reset, geez_key, geez_text, geez_commit, geez_reset,
    0};

/* ---- which input method a language uses ------------------------------------------- */

static const struct {
    const char *tag;
    const zui_ime_engine_t *eng;
} LOCALE_IME[] = {
    {"zh", &zui_ime_pinyin},
    {"zh-Hans", &zui_ime_pinyin},
    {"zh-Hant", &zui_ime_pinyin_hant},
    {"zh-TW", &zui_ime_pinyin_hant},
    {"zh-HK", &zui_ime_pinyin_hant},
    {"zh-MO", &zui_ime_pinyin_hant},
    {"yue", &zui_ime_jyutping},
    {"ja", &zui_ime_kana},
    {"ko", &zui_ime_hangul},
    {"am", &zui_ime_geez},
    {"ti", &zui_ime_geez},
};

static char low(char c)
{
    return c >= 'A' && c <= 'Z' ? (char) (c + 32) : c == '_' ? '-' : c;
}

/* a and the first n bytes of b are the same tag */
static bool tag_is(const char *a, const char *b, uint32_t n)
{
    uint32_t i = 0;
    for (; i < n && a[i] && low(a[i]) == low(b[i]); i++) {
    }
    return i == n && !a[i];
}

const zui_ime_engine_t *zui_ime_for_locale(const char *tag)
{
    if (!tag) return 0;
    uint32_t n = slen(tag), lang = 0;
    while (lang < n && tag[lang] != '-' && tag[lang] != '_') lang++;
    for (uint32_t pass = 0; pass < 2; pass++)
        for (uint32_t i = 0; i < sizeof LOCALE_IME / sizeof LOCALE_IME[0]; i++)
            if (tag_is(LOCALE_IME[i].tag, tag, pass ? lang : n)) return LOCALE_IME[i].eng;
    return 0;
}
