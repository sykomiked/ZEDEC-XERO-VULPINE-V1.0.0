/* script.c — multi-language text itemization. See script.h. */
#include "script.h"

bool font_utf8_next(const uint8_t *s, uint32_t len, uint32_t *cp, uint32_t *adv) {
    if (!s || !cp || !adv || len == 0) { if (cp) *cp = 0xFFFD; if (adv) *adv = len ? 1 : 0; return false; }
    uint8_t b0 = s[0];
    if (b0 < 0x80) { *cp = b0; *adv = 1; return true; }

    uint32_t need, min, c;
    if ((b0 & 0xE0) == 0xC0) { need = 1; c = b0 & 0x1F; min = 0x80; }
    else if ((b0 & 0xF0) == 0xE0) { need = 2; c = b0 & 0x0F; min = 0x800; }
    else if ((b0 & 0xF8) == 0xF0) { need = 3; c = b0 & 0x07; min = 0x10000; }
    else { *cp = 0xFFFD; *adv = 1; return false; }     /* stray continuation / >4-byte */

    if (len < need + 1) { *cp = 0xFFFD; *adv = 1; return false; }  /* truncated */
    for (uint32_t i = 1; i <= need; i++) {
        if ((s[i] & 0xC0) != 0x80) { *cp = 0xFFFD; *adv = 1; return false; }
        c = (c << 6) | (uint32_t)(s[i] & 0x3F);
    }
    if (c < min) { *cp = 0xFFFD; *adv = 1; return false; }          /* overlong */
    if (c > 0x10FFFF) { *cp = 0xFFFD; *adv = 1; return false; }
    if (c >= 0xD800 && c <= 0xDFFF) { *cp = 0xFFFD; *adv = 1; return false; } /* surrogate */
    *cp = c; *adv = need + 1;
    return true;
}

/* Range table. Ordered, non-overlapping; a linear walk is fine for the size. */
struct rng { uint32_t lo, hi; font_script_t s; };
static const struct rng RANGES[] = {
    { 0x0030, 0x0039, SCRIPT_COMMON },      /* digits            */
    { 0x0041, 0x005A, SCRIPT_LATIN },
    { 0x0061, 0x007A, SCRIPT_LATIN },
    { 0x00C0, 0x024F, SCRIPT_LATIN },       /* Latin-1 supp + ext A/B */
    { 0x0370, 0x03FF, SCRIPT_GREEK },
    { 0x0400, 0x04FF, SCRIPT_CYRILLIC },
    { 0x0500, 0x052F, SCRIPT_CYRILLIC },
    { 0x0530, 0x058F, SCRIPT_ARMENIAN },
    { 0x0590, 0x05FF, SCRIPT_HEBREW },
    { 0x0600, 0x06FF, SCRIPT_ARABIC },
    { 0x0700, 0x074F, SCRIPT_SYRIAC },      /* covers Aramaic-as-Syriac too    */
    { 0x0750, 0x077F, SCRIPT_ARABIC },      /* Arabic supplement               */
    { 0x0780, 0x07BF, SCRIPT_THAANA },
    { 0x07C0, 0x07FF, SCRIPT_NKO },
    { 0x0900, 0x097F, SCRIPT_DEVANAGARI },
    { 0x0980, 0x09FF, SCRIPT_BENGALI },
    { 0x0B80, 0x0BFF, SCRIPT_TAMIL },
    { 0x0E00, 0x0E7F, SCRIPT_THAI },
    { 0x10A0, 0x10FF, SCRIPT_GEORGIAN },
    { 0x1100, 0x11FF, SCRIPT_HANGUL },      /* Jamo                            */
    { 0x1200, 0x137F, SCRIPT_ETHIOPIC },
    { 0x13A0, 0x13FF, SCRIPT_CHEROKEE },
    { 0x1400, 0x167F, SCRIPT_CANADIAN_ABORIGINAL },
    { 0x2D30, 0x2D7F, SCRIPT_TIFINAGH },
    { 0x3040, 0x309F, SCRIPT_HIRAGANA },
    { 0x30A0, 0x30FF, SCRIPT_KATAKANA },
    { 0x3400, 0x4DBF, SCRIPT_HAN },         /* CJK ext A                       */
    { 0x4E00, 0x9FFF, SCRIPT_HAN },         /* CJK unified                     */
    { 0xA500, 0xA63F, SCRIPT_VAI },
    { 0xAC00, 0xD7AF, SCRIPT_HANGUL },      /* Hangul syllables                */
    { 0xE000, 0xE0FF, SCRIPT_ENOCHIAN },    /* PUA slice the ASCW Enochian uses */
    { 0x10B00,0x10B3F, SCRIPT_AVESTAN },
    { 0x1E900,0x1E95F, SCRIPT_ADLAM },
    { 0x20000,0x2A6DF, SCRIPT_HAN },        /* CJK ext B                       */
};

font_script_t font_script_of(uint32_t cp) {
    /* whitespace and ASCII punctuation are COMMON */
    if (cp == 0x20 || cp == 0x09 || cp == 0x0A || cp == 0x0D) return SCRIPT_COMMON;
    if ((cp >= 0x21 && cp <= 0x2F) || (cp >= 0x3A && cp <= 0x40) ||
        (cp >= 0x5B && cp <= 0x60) || (cp >= 0x7B && cp <= 0x7E)) return SCRIPT_COMMON;
    if (cp == 0x00A0 || (cp >= 0x2000 && cp <= 0x206F)) return SCRIPT_COMMON; /* general punct */
    uint32_t n = (uint32_t)(sizeof RANGES / sizeof RANGES[0]);
    for (uint32_t i = 0; i < n; i++)
        if (cp >= RANGES[i].lo && cp <= RANGES[i].hi) return RANGES[i].s;
    return SCRIPT_UNKNOWN;
}

font_dir_t font_script_dir(font_script_t s) {
    switch (s) {
    case SCRIPT_HEBREW: case SCRIPT_ARABIC: case SCRIPT_SYRIAC:
    case SCRIPT_THAANA: case SCRIPT_NKO: case SCRIPT_ADLAM:
    case SCRIPT_AVESTAN:
        return DIR_RTL;
    default:
        return DIR_LTR;
    }
}

const char *font_script_name(font_script_t s) {
    switch (s) {
    case SCRIPT_COMMON: return "Common";
    case SCRIPT_LATIN: return "Latin";
    case SCRIPT_GREEK: return "Greek";
    case SCRIPT_CYRILLIC: return "Cyrillic";
    case SCRIPT_ARMENIAN: return "Armenian";
    case SCRIPT_HEBREW: return "Hebrew";
    case SCRIPT_ARABIC: return "Arabic";
    case SCRIPT_SYRIAC: return "Syriac";
    case SCRIPT_THAANA: return "Thaana";
    case SCRIPT_NKO: return "NKo";
    case SCRIPT_DEVANAGARI: return "Devanagari";
    case SCRIPT_BENGALI: return "Bengali";
    case SCRIPT_TAMIL: return "Tamil";
    case SCRIPT_THAI: return "Thai";
    case SCRIPT_GEORGIAN: return "Georgian";
    case SCRIPT_HANGUL: return "Hangul";
    case SCRIPT_ETHIOPIC: return "Ethiopic";
    case SCRIPT_CHEROKEE: return "Cherokee";
    case SCRIPT_CANADIAN_ABORIGINAL: return "CanadianAboriginal";
    case SCRIPT_TIFINAGH: return "Tifinagh";
    case SCRIPT_VAI: return "Vai";
    case SCRIPT_ADLAM: return "Adlam";
    case SCRIPT_HAN: return "Han";
    case SCRIPT_HIRAGANA: return "Hiragana";
    case SCRIPT_KATAKANA: return "Katakana";
    case SCRIPT_AVESTAN: return "Avestan";
    case SCRIPT_ENOCHIAN: return "Enochian";
    default: return "Unknown";
    }
}

font_script_t font_script_from_name(const char *name) {
    if (!name) return SCRIPT_UNKNOWN;
    for (int s = 0; s < SCRIPT__COUNT; s++) {
        const char *n = font_script_name((font_script_t)s);
        const char *a = name, *b = n;
        while (*a && *b && *a == *b) { a++; b++; }
        if (*a == 0 && *b == 0) return (font_script_t)s;
    }
    /* a few ASCW tokens that differ from our canonical name */
    const char *a = name;
    if (a[0]=='A'&&a[1]=='r'&&a[2]=='a'&&a[3]=='m') return SCRIPT_SYRIAC;   /* Aramaic */
    if (a[0]=='K'&&a[1]=='a'&&a[2]=='n'&&a[3]=='a') return SCRIPT_HIRAGANA; /* Kana */
    if (a[0]=='S'&&a[1]=='a'&&a[2]=='n'&&a[3]=='s') return SCRIPT_DEVANAGARI; /* Sanskrit */
    if (a[0]=='R'&&a[1]=='o'&&a[2]=='m'&&a[3]=='a') return SCRIPT_LATIN;    /* Roman */
    return SCRIPT_UNKNOWN;
}

uint32_t font_itemize(const uint8_t *text, uint32_t len,
                      font_run_t *runs, uint32_t max, bool *truncated) {
    if (truncated) *truncated = false;
    if (!text || !runs || max == 0) { if (truncated && max == 0) *truncated = (len > 0); return 0; }

    uint32_t nruns = 0;
    uint32_t at = 0;
    /* current run being accumulated */
    bool have = false;
    uint32_t run_start = 0;
    font_script_t run_script = SCRIPT_UNKNOWN;

    while (at < len) {
        uint32_t cp, adv;
        font_utf8_next(text + at, len - at, &cp, &adv);
        font_script_t sc = font_script_of(cp);

        if (!have) {
            run_start = at;
            run_script = sc;                 /* may be COMMON; resolved when a real script arrives */
            have = true;
        } else if (sc == SCRIPT_COMMON) {
            /* COMMON joins the current run, whatever it is */
        } else if (run_script == SCRIPT_COMMON) {
            /* leading COMMON adopts the first real script it meets */
            run_script = sc;
        } else if (sc != run_script) {
            /* close the current run, open a new one */
            if (nruns < max) {
                runs[nruns].start = run_start;
                runs[nruns].len = at - run_start;
                runs[nruns].script = run_script;
                runs[nruns].dir = font_script_dir(run_script);
                nruns++;
            } else { if (truncated) *truncated = true; }
            run_start = at;
            run_script = sc;
        }
        at += adv ? adv : 1;
    }

    if (have) {
        if (nruns < max) {
            runs[nruns].start = run_start;
            runs[nruns].len = len - run_start;
            runs[nruns].script = run_script;
            runs[nruns].dir = font_script_dir(run_script);
            nruns++;
        } else if (truncated) *truncated = true;
    }
    return nruns;
}
