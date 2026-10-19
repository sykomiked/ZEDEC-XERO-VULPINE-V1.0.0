/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* escl.c — see escl.h */
#include "escl.h"

#include <stdio.h>
#include <string.h>

#define A4_W         2480u  /* 210 mm in 1/300 inch */
#define A4_H         3508u  /* 297 mm */
#define SIZE_MAX_300 10200u /* 34 inches: larger than any flatbed or feeder */

int escl_settings(const escl_settings_t *s, char *out, uint32_t cap, uint32_t *len)
{
    static const char *const SRC[] = {"Platen", "Feeder"};
    static const char *const COLOR[] = {"RGB24", "Grayscale8", "BlackAndWhite1"};
    static const char *const FMT[] = {"image/jpeg", "application/pdf", "image/png"};
    bool fmt_ok = false;
    for (uint32_t i = 0; i < sizeof FMT / sizeof *FMT && s->format; i++)
        fmt_ok = fmt_ok || !strcmp(s->format, FMT[i]);
    uint32_t w = s->width ? s->width : A4_W, h = s->height ? s->height : A4_H;
    if (!fmt_ok || s->source > ESCL_SRC_FEEDER || s->color > ESCL_COLOR_BW || s->dpi < 75 ||
        s->dpi > 1200 || w > SIZE_MAX_300 || h > SIZE_MAX_300)
        return ESCL_E_ARG;
    int n =
        snprintf(out, cap,
                 "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                 "<scan:ScanSettings xmlns:scan=\"http://schemas.hp.com/imaging/escl/2011/05/03\" "
                 "xmlns:pwg=\"http://www.pwg.org/schemas/2010/12/sm\">\n"
                 "<pwg:Version>2.63</pwg:Version>\n"
                 "<pwg:ScanRegions><pwg:ScanRegion>"
                 "<pwg:ContentRegionUnits>escl:ThreeHundredthsOfInches</pwg:ContentRegionUnits>"
                 "<pwg:XOffset>0</pwg:XOffset><pwg:YOffset>0</pwg:YOffset>"
                 "<pwg:Width>%u</pwg:Width><pwg:Height>%u</pwg:Height>"
                 "</pwg:ScanRegion></pwg:ScanRegions>\n"
                 "<pwg:InputSource>%s</pwg:InputSource>\n"
                 "<scan:ColorMode>%s</scan:ColorMode>\n"
                 "<scan:XResolution>%u</scan:XResolution><scan:YResolution>%u</scan:YResolution>\n"
                 "<pwg:DocumentFormat>%s</pwg:DocumentFormat>"
                 "<scan:DocumentFormatExt>%s</scan:DocumentFormatExt>\n"
                 "</scan:ScanSettings>\n",
                 w, h, SRC[s->source], COLOR[s->color], s->dpi, s->dpi, s->format, s->format);
    if (n < 0 || (uint32_t) n >= cap) return ESCL_E_ROOM;
    *len = (uint32_t) n;
    return ESCL_OK;
}

/* ===================== reading ===================== */

static bool namech(char c)
{
    return c != ' ' && c != '\t' && c != '\r' && c != '\n' && c != '>' && c != '/';
}

/* the five XML entities; anything else is kept as written */
static uint32_t entity(const char *p, uint32_t n, char *c)
{
    static const struct {
        const char *e;
        char c;
    } E[] = {{"&amp;", '&'}, {"&lt;", '<'}, {"&gt;", '>'}, {"&quot;", '"'}, {"&apos;", '\''}};
    for (uint32_t i = 0; i < sizeof E / sizeof *E; i++) {
        uint32_t l = (uint32_t) strlen(E[i].e);
        if (l <= n && !memcmp(p, E[i].e, l)) {
            *c = E[i].c;
            return l;
        }
    }
    *c = '&';
    return 1;
}

/* n, or less so the bytes end on a whole UTF-8 character */
static uint32_t whole(const char *s, uint32_t n)
{
    uint32_t i = n;
    while (i > 0 && n - i < 4 && ((uint8_t) s[i - 1] & 0xC0) == 0x80) i--;
    if (i == 0 || ((uint8_t) s[i - 1] & 0xC0) != 0xC0) return n; /* ASCII last, or no lead */
    uint8_t lead = (uint8_t) s[i - 1];
    uint32_t want = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : 2;
    return n - (i - 1) < want ? i - 1 : n;
}

bool escl_text(const char *xml, uint32_t len, uint32_t *from, const char *name, char *out,
               uint32_t cap)
{
    size_t nl = strlen(name);
    for (uint32_t i = *from; i < len; i++) {
        if (xml[i] != '<' || i + 1 >= len) continue;
        char c = xml[i + 1];
        if (c == '/' || c == '?' || c == '!') continue;
        uint32_t s = i + 1, e = s;
        while (e < len && namech(xml[e])) e++;
        uint32_t l = s; /* the local name: after the last ':' */
        for (uint32_t k = s; k < e; k++)
            if (xml[k] == ':') l = k + 1;
        if (e - l != nl || memcmp(xml + l, name, nl)) continue;
        uint32_t gt = e;
        while (gt < len && xml[gt] != '>') gt++;
        if (gt >= len) return false;
        uint32_t o = 0;
        if (xml[gt - 1] != '/') { /* not <Name/>: the text up to the next tag */
            uint32_t t = gt + 1;
            while (t < len && xml[t] != '<') {
                char ch = xml[t];
                uint32_t step = 1;
                if (ch == '&') step = entity(xml + t, len - t, &ch);
                if (o + 1 < cap) out[o++] = ch;
                t += step;
            }
            gt = t;
        } else
            gt++;
        if (cap) {
            /* trim the white space around it, and a cut character */
            uint32_t a = 0;
            while (a < o && (out[a] == ' ' || out[a] == '\n' || out[a] == '\r' || out[a] == '\t'))
                a++;
            while (o > a && (out[o - 1] == ' ' || out[o - 1] == '\n' || out[o - 1] == '\r' ||
                             out[o - 1] == '\t'))
                o--;
            memmove(out, out + a, o - a);
            o -= a;
            o = whole(out, o);
            out[o] = 0;
        }
        *from = gt;
        return true;
    }
    return false;
}

static bool has(const char *xml, uint32_t len, const char *name)
{
    char tmp[2];
    uint32_t at = 0;
    return escl_text(xml, len, &at, name, tmp, sizeof tmp);
}

static void words(const char *xml, uint32_t len, const char *name, char (*list)[ESCL_WORD],
                  uint32_t *n)
{
    char v[ESCL_WORD];
    uint32_t at = 0;
    while (*n < ESCL_LIST && escl_text(xml, len, &at, name, v, sizeof v)) {
        bool seen = !v[0];
        for (uint32_t i = 0; i < *n && !seen; i++) seen = !strcmp(list[i], v);
        if (!seen) memcpy(list[(*n)++], v, sizeof v);
    }
}

static bool number(const char *s, uint32_t *v)
{
    uint64_t x = 0;
    if (!*s) return false;
    for (; *s; s++) {
        if (*s < '0' || *s > '9') return false;
        x = x * 10u + (uint32_t) (*s - '0');
        if (x > 0xFFFFFFFFu) return false;
    }
    *v = (uint32_t) x;
    return true;
}

int escl_parse_caps(const char *xml, uint32_t len, escl_caps_t *c)
{
    memset(c, 0, sizeof *c);
    uint32_t at = 0;
    if (!has(xml, len, "ScannerCapabilities")) return ESCL_E_FORMAT;
    escl_text(xml, len, &at, "MakeAndModel", c->make, sizeof c->make);
    c->platen = has(xml, len, "Platen");
    c->feeder = has(xml, len, "Adf");
    char v[ESCL_WORD];
    at = 0;
    while (c->ndpi < ESCL_LIST && escl_text(xml, len, &at, "XResolution", v, sizeof v)) {
        uint32_t d;
        if (!number(v, &d)) continue;
        bool seen = false;
        for (uint32_t i = 0; i < c->ndpi && !seen; i++) seen = c->dpi[i] == d;
        if (!seen) c->dpi[c->ndpi++] = d;
    }
    words(xml, len, "ColorMode", c->colors, &c->ncolors);
    words(xml, len, "DocumentFormatExt", c->formats, &c->nformats);
    if (!c->nformats) words(xml, len, "DocumentFormat", c->formats, &c->nformats);
    at = 0;
    if (escl_text(xml, len, &at, "MaxWidth", v, sizeof v) && !number(v, &c->max_width))
        c->max_width = 0;
    at = 0;
    if (escl_text(xml, len, &at, "MaxHeight", v, sizeof v) && !number(v, &c->max_height))
        c->max_height = 0;
    return ESCL_OK;
}

int escl_parse_status(const char *xml, uint32_t len, escl_status_t *s)
{
    memset(s, 0, sizeof *s);
    uint32_t at = 0;
    if (!has(xml, len, "ScannerStatus") ||
        !escl_text(xml, len, &at, "State", s->state, sizeof s->state))
        return ESCL_E_FORMAT;
    at = 0;
    escl_text(xml, len, &at, "AdfState", s->adf_state, sizeof s->adf_state);
    char tmp[2];
    at = 0;
    while (escl_text(xml, len, &at, "JobInfo", tmp, sizeof tmp)) s->jobs++;
    return ESCL_OK;
}

bool escl_job_path(const char *location, char *out, uint32_t cap)
{
    const char *p = location;
    if (!strncmp(p, "http://", 7) || !strncmp(p, "https://", 8)) {
        p = strchr(strstr(p, "//") + 2, '/'); /* the scheme and host are dropped: */
        if (!p) return false;                 /* the job lives on the scanner asked */
    }
    if (*p != '/') return false;
    const char *id = strstr(p, "/ScanJobs/");
    if (!id) return false;
    id += 10;
    if (!*id) return false;
    for (const char *q = id; *q; q++)
        if (!((*q >= '0' && *q <= '9') || (*q >= 'a' && *q <= 'z') || (*q >= 'A' && *q <= 'Z') ||
              *q == '-' || *q == '_'))
            return false;
    for (const char *q = p; q < id; q++)
        if (*q < 0x21 || *q > 0x7e || *q == '?' || *q == '#') return false;
    size_t n = strlen(p);
    if (n + 1 > cap) return false;
    memcpy(out, p, n + 1);
    return true;
}
