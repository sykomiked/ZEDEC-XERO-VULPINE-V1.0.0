/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* xlate_langid.c — the language identifier. Mirrors Model.classify() in
 * gen_langid_model.py exactly; see xlate_langid.h. */
#include "xlate_langid.h"
#include "xlate_langid_model.h"

typedef struct {
    uint32_t lo, hi;
} xl_range_t;
typedef struct {
    const xl_range_t *r;
    uint32_t n;
} xl_script_t;

/* Same order as SCRIPTS in gen_langid_model.py (index = script id). */
static const xl_range_t r_latn[] = {{0x61, 0x7A}, {0xDF, 0x24F}, {0x1E00, 0x1EFF}};
static const xl_range_t r_cyrl[] = {{0x400, 0x52F}};
static const xl_range_t r_grek[] = {{0x370, 0x3FF}, {0x1F00, 0x1FFF}};
static const xl_range_t r_armn[] = {{0x530, 0x58F}};
static const xl_range_t r_hebr[] = {{0x590, 0x5FF}};
static const xl_range_t r_arab[] = {
    {0x600, 0x6FF}, {0x750, 0x77F}, {0xFB50, 0xFDFF}, {0xFE70, 0xFEFF}};
static const xl_range_t r_deva[] = {{0x900, 0x97F}};
static const xl_range_t r_beng[] = {{0x980, 0x9FF}};
static const xl_range_t r_guru[] = {{0xA00, 0xA7F}};
static const xl_range_t r_gujr[] = {{0xA80, 0xAFF}};
static const xl_range_t r_orya[] = {{0xB00, 0xB7F}};
static const xl_range_t r_taml[] = {{0xB80, 0xBFF}};
static const xl_range_t r_telu[] = {{0xC00, 0xC7F}};
static const xl_range_t r_knda[] = {{0xC80, 0xCFF}};
static const xl_range_t r_mlym[] = {{0xD00, 0xD7F}};
static const xl_range_t r_sinh[] = {{0xD80, 0xDFF}};
static const xl_range_t r_thai[] = {{0xE00, 0xE7F}};
static const xl_range_t r_laoo[] = {{0xE80, 0xEFF}};
static const xl_range_t r_mymr[] = {{0x1000, 0x109F}};
static const xl_range_t r_geor[] = {{0x10A0, 0x10FF}, {0x1C90, 0x1CBF}};
static const xl_range_t r_hang[] = {{0x1100, 0x11FF}, {0x3130, 0x318F}, {0xAC00, 0xD7AF}};
static const xl_range_t r_ethi[] = {{0x1200, 0x139F}};
static const xl_range_t r_khmr[] = {{0x1780, 0x17FF}};
static const xl_range_t r_kana[] = {{0x3040, 0x30FF}, {0x31F0, 0x31FF}, {0xFF66, 0xFF9F}};
static const xl_range_t r_hani[] = {
    {0x3400, 0x4DBF}, {0x4E00, 0x9FFF}, {0xF900, 0xFAFF}, {0x20000, 0x2FFFF}};

#define R(x) {x, sizeof x / sizeof x[0]}
static const xl_script_t k_scripts[] = {R(r_latn), R(r_cyrl), R(r_grek), R(r_armn), R(r_hebr),
                                        R(r_arab), R(r_deva), R(r_beng), R(r_guru), R(r_gujr),
                                        R(r_orya), R(r_taml), R(r_telu), R(r_knda), R(r_mlym),
                                        R(r_sinh), R(r_thai), R(r_laoo), R(r_mymr), R(r_geor),
                                        R(r_hang), R(r_ethi), R(r_khmr), R(r_kana), R(r_hani)};
#undef R
#define XL_NSCRIPT (sizeof k_scripts / sizeof k_scripts[0])
#define XL_KANA    23u
#define XL_HAN     24u

uint32_t xlate_utf8_next(const char *s, uint32_t len, uint32_t *i)
{
    const uint8_t *u = (const uint8_t *) s;
    uint32_t p = *i;
    uint8_t c = u[p];
    uint32_t need, cp, min;
    if (c < 0x80) {
        *i = p + 1;
        return c;
    }
    if ((c & 0xE0) == 0xC0) {
        need = 1;
        cp = c & 0x1F;
        min = 0x80;
    } else if ((c & 0xF0) == 0xE0) {
        need = 2;
        cp = c & 0x0F;
        min = 0x800;
    } else if ((c & 0xF8) == 0xF0) {
        need = 3;
        cp = c & 0x07;
        min = 0x10000;
    } else {
        *i = p + 1;
        return 0xFFFD;
    }
    if (p + need >= len) {
        *i = p + 1;
        return 0xFFFD;
    }
    for (uint32_t k = 1; k <= need; k++) {
        uint8_t b = u[p + k];
        if ((b & 0xC0) != 0x80) {
            *i = p + 1;
            return 0xFFFD;
        }
        cp = (cp << 6) | (b & 0x3F);
    }
    if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
        *i = p + 1;
        return 0xFFFD;
    }
    *i = p + need + 1;
    return cp;
}

static uint32_t xl_fold(uint32_t cp)
{
    if (cp >= 0x41 && cp <= 0x5A) return cp + 32;
    if (cp >= 0xC0 && cp <= 0xDE && cp != 0xD7) return cp + 32;
    if (cp >= 0x100 && cp <= 0x17F) {
        if ((cp >= 0x139 && cp <= 0x148) || (cp >= 0x179 && cp <= 0x17E))
            return (cp & 1) ? cp + 1 : cp;
        if (cp == 0x130) return 0x69;
        if (cp == 0x178) return 0xFF;
        if (cp == 0x131 || cp == 0x138 || cp == 0x149 || cp == 0x17F) return cp;
        return cp | 1;
    }
    if (cp >= 0x391 && cp <= 0x3A9 && cp != 0x3A2) return cp + 32;
    if (cp >= 0x410 && cp <= 0x42F) return cp + 32;
    if (cp >= 0x400 && cp <= 0x40F) return cp + 80;
    if (cp >= 0x531 && cp <= 0x556) return cp + 48;
    return cp;
}

static const uint16_t k_sep_single[] = {0x60C,  0x61B,  0x61F,  0x6D4, 0x964,  0x965,
                                        0x589,  0x5BE,  0x5C0,  0x5C3, 0x1361, 0x1362,
                                        0x1363, 0x104A, 0x104B, 0xE5A, 0xE5B};

static bool xl_is_sep(uint32_t cp)
{
    if (cp < 0x80) return !((cp >= 0x61 && cp <= 0x7A) || (cp >= 0x41 && cp <= 0x5A));
    if (cp <= 0xBF || cp == 0xD7 || cp == 0xF7) return true;
    if ((cp >= 0x2000 && cp <= 0x206F) || (cp >= 0x3000 && cp <= 0x303F)) return true;
    if ((cp >= 0xFF00 && cp <= 0xFF20) || (cp >= 0xFF5B && cp <= 0xFF65)) return true;
    for (uint32_t i = 0; i < sizeof k_sep_single / sizeof k_sep_single[0]; i++)
        if (cp == k_sep_single[i]) return true;
    if ((cp >= 0x660 && cp <= 0x669) || (cp >= 0x6F0 && cp <= 0x6F9) ||
        (cp >= 0x966 && cp <= 0x96F))
        return true;
    return false;
}

static bool xl_is_drop(uint32_t cp)
{
    if (cp >= 0x300 && cp <= 0x36F) return true;
    if (cp >= 0x591 && cp <= 0x5C7 && cp != 0x5BE && cp != 0x5C0 && cp != 0x5C3 && cp != 0x5C6)
        return true;
    if ((cp >= 0x64B && cp <= 0x65F) || cp == 0x670 || cp == 0x640) return true;
    return cp == 0x200B || cp == 0x200C || cp == 0x200D || cp == 0xFEFF;
}

/* Normalise into out (cap XL_MAX_CPS); returns the count. Mirrors norm(). */
static uint32_t xl_normalize(const char *s, uint32_t len, uint32_t *out)
{
    uint32_t n = 0, i = 0;
    bool prev_sep = true;
    while (i < len && s[i] != '\0') {
        uint32_t cp = xlate_utf8_next(s, len, &i);
        if (xl_is_drop(cp)) continue;
        if (xl_is_sep(cp)) {
            if (!prev_sep) {
                if (n >= XL_MAX_CPS) break;
                out[n++] = 0x20;
                prev_sep = true;
            }
            continue;
        }
        if (n >= XL_MAX_CPS) break;
        out[n++] = xl_fold(cp);
        prev_sep = false;
    }
    while (n > 0 && out[n - 1] == 0x20) n--;
    return n;
}

static int32_t xl_script_of(uint32_t cp)
{
    for (uint32_t i = 0; i < XL_NSCRIPT; i++)
        for (uint32_t k = 0; k < k_scripts[i].n; k++)
            if (cp >= k_scripts[i].r[k].lo && cp <= k_scripts[i].r[k].hi) return (int32_t) i;
    return -1;
}

static int32_t xl_dom_script(const uint32_t *cps, uint32_t n, uint32_t *letters)
{
    uint32_t cnt[XL_NSCRIPT];
    for (uint32_t i = 0; i < XL_NSCRIPT; i++) cnt[i] = 0;
    *letters = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (cps[i] == 0x20) continue;
        (*letters)++;
        int32_t sc = xl_script_of(cps[i]);
        if (sc >= 0) cnt[sc]++;
    }
    if (cnt[XL_KANA] > 0 && cnt[XL_KANA] * 8 >= cnt[XL_KANA] + cnt[XL_HAN]) return XL_KANA;
    uint32_t best = 0;
    int32_t bi = -1;
    for (uint32_t i = 0; i < XL_NSCRIPT; i++) {
        if (cnt[i] > best) {
            best = cnt[i];
            bi = (int32_t) i;
        }
    }
    return bi;
}

/* Binary search: index of h in XL_HASH, or -1. */
static int32_t xl_find(uint32_t h)
{
    uint32_t lo = 0, hi = XL_NHASH;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (XL_HASH[mid] < h)
            lo = mid + 1;
        else
            hi = mid;
    }
    return (lo < XL_NHASH && XL_HASH[lo] == h) ? (int32_t) lo : -1;
}

static void xl_set_und(xlate_langid_result_t *out, uint32_t letters)
{
    out->lang = -1;
    out->tag = "und";
    out->confidence = 0;
    out->letters = letters;
    out->reliable = false;
}

void xlate_langid(const char *utf8, uint32_t len, xlate_langid_result_t *out)
{
    if (!out) return;
    xl_set_und(out, 0);
    if (!utf8 || len == 0) return;
    uint32_t cps[XL_MAX_CPS];
    uint32_t n = xl_normalize(utf8, len, cps);
    uint32_t letters = 0;
    int32_t dom = xl_dom_script(cps, n, &letters);
    out->letters = letters;
    if (dom < 0) return;

    uint8_t cand[XL_NLANG];
    uint32_t nc = 0;
    for (uint32_t i = 0; i < XL_NLANG; i++)
        if (XL_SCRIPT[i] == (uint32_t) dom || (dom == XL_HAN && XL_SCRIPT[i] == XL_KANA))
            cand[nc++] = (uint8_t) i; /* Han-only text may be Japanese */
    if (nc == 0) return;
    if (nc == 1) {
        out->lang = cand[0];
        out->tag = XL_TAG[cand[0]];
        out->confidence = 100;
        out->reliable = letters >= 1;
        return;
    }

    uint32_t score[XL_NLANG];
    uint8_t mark[XL_NLANG];
    for (uint32_t i = 0; i < nc; i++) score[i] = 0;
    for (uint32_t i = 0; i < XL_NLANG; i++) mark[i] = 0xFF;
    for (uint32_t i = 0; i < nc; i++) mark[cand[i]] = (uint8_t) i;
    uint32_t scored = 0;

    /* Features of [space] cps [space], n = 1..3, in the generator's order. */
    for (uint32_t g = 1; g <= 3; g++) {
        uint32_t total = n + 2;
        for (uint32_t st = 0; st + g <= total; st++) {
            /* position total-1 is the trailing space, not cps[n] (which is
             * past the normalized text, and past the array when n is full) */
            uint32_t first = (st == 0 || st == total - 1) ? 0x20 : cps[st - 1];
            if (g == 1 && first == 0x20) continue;
            uint32_t h = 2166136261u ^ g;
            for (uint32_t k = 0; k < g; k++) {
                uint32_t p = st + k;
                uint32_t c = (p == 0 || p == total - 1) ? 0x20 : cps[p - 1];
                h = (h ^ c) * 16777619u;
            }
            int32_t at = xl_find(h);
            if (at < 0) continue;
            uint32_t a = XL_START[at], b = XL_START[at + 1];
            bool any = false;
            for (uint32_t e = a; e < b; e++)
                if (mark[XL_ENTRY[e] >> 8] != 0xFF) any = true;
            if (!any) continue;
            scored++;
            uint8_t hit[XL_NLANG];
            for (uint32_t i = 0; i < nc; i++) hit[i] = 0;
            for (uint32_t e = a; e < b; e++) {
                uint8_t m = mark[XL_ENTRY[e] >> 8];
                if (m == 0xFF) continue;
                score[m] += XL_ENTRY[e] & 0xFFu;
                hit[m] = 1;
            }
            for (uint32_t i = 0; i < nc; i++)
                if (!hit[i]) score[i] += XL_UNSEEN[cand[i]];
        }
    }
    uint32_t best = 0;
    for (uint32_t i = 1; i < nc; i++)
        if (score[i] < score[best]) best = i;
    uint32_t second = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < nc; i++)
        if (i != best && score[i] < second) second = score[i];
    out->lang = cand[best];
    out->tag = XL_TAG[cand[best]];
    /* Margin in cost units per scored n-gram; 4 units/n-gram -> 100. */
    if (scored > 0) {
        uint32_t margin = second - score[best];
        uint32_t c = (margin * 25u) / scored;
        out->confidence = c > 100 ? 100 : c;
    }
    out->reliable = out->confidence >= 30 && letters >= 12;
}

uint32_t xlate_langid_count(void)
{
    return XL_NLANG;
}

const char *xlate_langid_tag(uint32_t i)
{
    return i < XL_NLANG ? XL_TAG[i] : "";
}

const char *xlate_langid_name(uint32_t i)
{
    return i < XL_NLANG ? XL_NAME[i] : "";
}

static char xl_lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char) (c - 'A' + 'a') : c;
}

int32_t xlate_langid_find(const char *tag)
{
    if (!tag) return -1;
    for (uint32_t i = 0; i < XL_NLANG; i++) {
        const char *t = XL_TAG[i];
        uint32_t k = 0;
        while (t[k] && tag[k] && xl_lower(t[k]) == xl_lower(tag[k])) k++;
        if (t[k] == '\0' && tag[k] == '\0') return (int32_t) i;
    }
    return -1;
}
