/* ascw_golden.c — the ASCW Golden face map. See ascw_golden.h. */
#include "ascw_golden_internal.h"

static bool str_eq(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static char fold(char c)
{
    if (c >= 'A' && c <= 'Z') return (char) (c + 32);
    if (c == '_') return '-';
    return c;
}

/* tag[0..n) equals t, ignoring case and '_' vs '-' */
static bool tag_eq(const char *tag, uint32_t n, const char *t)
{
    uint32_t i = 0;
    for (; i < n; i++)
        if (!t[i] || fold(tag[i]) != fold(t[i])) return false;
    return t[i] == 0;
}

uint32_t ascw_face_count(void)
{
    return ASCW_FACE_N;
}

const ascw_face_t *ascw_face_at(int32_t i)
{
    return (i >= 0 && (uint32_t) i < ASCW_FACE_N) ? &ASCW_FACES[i] : 0;
}

int32_t ascw_face_find(const char *file)
{
    if (!file) return -1;
    for (uint32_t i = 0; i < ASCW_FACE_N; i++)
        if (str_eq(ASCW_FACES[i].file, file)) return (int32_t) i;
    return -1;
}

int32_t ascw_primary(void)
{
    for (uint32_t i = 0; i < ASCW_FACE_N; i++)
        if (ASCW_FACES[i].role == ASCW_ROLE_PRIMARY) return (int32_t) i;
    return -1;
}

int32_t ascw_face_for_script(const char *iso15924)
{
    if (!iso15924) return -1;
    for (uint32_t i = 0; i < ASCW_SCRIPT_N; i++)
        if (str_eq(ASCW_SCRIPTS[i].script, iso15924)) return ASCW_SCRIPTS[i].face;
    return -1;
}

int32_t ascw_face_for_locale(const char *tag, const char *script)
{
    if (tag) {
        uint32_t n = 0;
        while (tag[n]) n++;
        while (n > 0) {
            for (uint32_t i = 0; i < ASCW_LOCALE_N; i++)
                if (tag_eq(tag, n, ASCW_LOCALES[i].tag)) return ASCW_LOCALES[i].face;
            while (n > 0 && tag[n - 1] != '-' && tag[n - 1] != '_') n--; /* drop a subtag */
            if (n > 0) n--;
        }
    }
    return ascw_face_for_script(script);
}

uint32_t ascw_face_chain(const char *tag, const char *script, int32_t *out, uint32_t max)
{
    if (!out || max == 0) return 0;
    uint32_t n = 0;
    int32_t f = ascw_face_for_locale(tag, script), p = ascw_primary();
    if (f >= 0) out[n++] = f;
    if (p >= 0 && p != f && n < max) out[n++] = p;
    return n;
}

int32_t ascw_face_for_codepoint(uint32_t cp)
{
    uint32_t lo = 0, hi = ASCW_RANGE_N;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        if (cp < ASCW_RANGES[mid].lo)
            hi = mid;
        else if (cp > ASCW_RANGES[mid].hi)
            lo = mid + 1u;
        else
            return ASCW_RANGES[mid].face;
    }
    return -1;
}

/* script.h's enum, for the scripts it names */
static font_script_t enum_script(const char *iso)
{
    static const struct {
        const char *iso;
        font_script_t s;
    } M[] = {
        {"Latn", SCRIPT_LATIN},    {"Grek", SCRIPT_GREEK},
        {"Cyrl", SCRIPT_CYRILLIC}, {"Armn", SCRIPT_ARMENIAN},
        {"Hebr", SCRIPT_HEBREW},   {"Arab", SCRIPT_ARABIC},
        {"Syrc", SCRIPT_SYRIAC},   {"Thaa", SCRIPT_THAANA},
        {"Nkoo", SCRIPT_NKO},      {"Deva", SCRIPT_DEVANAGARI},
        {"Beng", SCRIPT_BENGALI},  {"Taml", SCRIPT_TAMIL},
        {"Thai", SCRIPT_THAI},     {"Geor", SCRIPT_GEORGIAN},
        {"Hang", SCRIPT_HANGUL},   {"Ethi", SCRIPT_ETHIOPIC},
        {"Cher", SCRIPT_CHEROKEE}, {"Cans", SCRIPT_CANADIAN_ABORIGINAL},
        {"Tfng", SCRIPT_TIFINAGH}, {"Vaii", SCRIPT_VAI},
        {"Adlm", SCRIPT_ADLAM},    {"Hani", SCRIPT_HAN},
        {"Hira", SCRIPT_HIRAGANA}, {"Kana", SCRIPT_KATAKANA},
        {"Avst", SCRIPT_AVESTAN},
    };
    for (uint32_t i = 0; i < sizeof M / sizeof M[0]; i++)
        if (str_eq(M[i].iso, iso)) return M[i].s;
    return SCRIPT_UNKNOWN;
}

/* register face i once per script.h script it serves; at least once */
static int32_t install_one(font_registry_t *r, const char *dir, uint32_t i)
{
    const ascw_face_t *f = &ASCW_FACES[i];
    char path[FONT_ASSET_LEN];
    uint32_t n = 0;
    for (const char *s = dir; *s; s++) {
        if (n + 1 >= FONT_ASSET_LEN) return -1;
        path[n++] = *s;
    }
    if (n > 0 && path[n - 1] != '/') {
        if (n + 1 >= FONT_ASSET_LEN) return -1;
        path[n++] = '/';
    }
    for (const char *s = f->file; *s; s++) {
        if (n + 1 >= FONT_ASSET_LEN) return -1;
        path[n++] = *s;
    }
    path[n] = 0;

    font_style_t style = f->role == ASCW_ROLE_ALTERNATE ? FONT_STYLE_DISPLAY : FONT_STYLE_REGULAR;
    int32_t count = 0;
    const char *p = f->scripts;
    while (*p) {
        char iso[8];
        uint32_t k = 0;
        while (*p && *p != ' ') {
            if (k + 1 < sizeof iso) iso[k++] = *p;
            p++;
        }
        iso[k] = 0;
        while (*p == ' ') p++;
        font_script_t s = enum_script(iso);
        if (s == SCRIPT_UNKNOWN) continue;
        if (font_register(r, f->family, s, style, path) < 0) return -1;
        count++;
    }
    if (count == 0) {
        if (font_register(r, f->family, SCRIPT_UNKNOWN, style, path) < 0) return -1;
        count = 1;
    }
    return count;
}

int32_t ascw_install(font_registry_t *r, const char *dir)
{
    if (!r || !dir) return -1;
    int32_t total = 0, p = ascw_primary();
    if (p < 0) return -1;
    int32_t k = install_one(r, dir, (uint32_t) p);
    if (k < 0) return -1;
    total += k;
    for (uint32_t i = 0; i < ASCW_FACE_N; i++) {
        if ((int32_t) i == p) continue;
        k = install_one(r, dir, i);
        if (k < 0) return -1;
        total += k;
    }
    return total;
}
