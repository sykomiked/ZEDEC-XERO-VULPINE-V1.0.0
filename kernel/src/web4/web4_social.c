/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* web4_social.c — one text post between the platform feed and the
 * ActivityPub / AT Protocol record shapes. See web4_web2.h for the limits. */
#include "web4_web2.h"

#define SOC_TOKS  128u
#define AS_PUBLIC "https://www.w3.org/ns/activitystreams#Public"

/* ===== RFC 3339 (UTC, years 1970..9999) ===== */
/* Howard Hinnant's days_from_civil / civil_from_days, 32-bit only. */
static int32_t days_from_civil(int32_t y, uint32_t m, uint32_t d)
{
    y -= m <= 2;
    int32_t era = (y >= 0 ? y : y - 399) / 400;
    uint32_t yoe = (uint32_t) (y - era * 400);
    uint32_t doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int32_t) doe - 719468;
}

static void civil_from_days(int32_t z, int32_t *y, uint32_t *m, uint32_t *d)
{
    z += 719468;
    int32_t era = (z >= 0 ? z : z - 146096) / 146097;
    uint32_t doe = (uint32_t) (z - era * 146097);
    uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int32_t yy = (int32_t) yoe + era * 400;
    uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    uint32_t mp = (5 * doy + 2) / 153;
    *d = doy - (153 * mp + 2) / 5 + 1;
    *m = mp < 10 ? mp + 3 : mp - 9;
    *y = yy + (*m <= 2);
}

static void put2(w4_w *w, uint32_t v)
{
    w4_w_byte(w, (uint8_t) ('0' + v / 10));
    w4_w_byte(w, (uint8_t) ('0' + v % 10));
}

int32_t w4_rfc3339_format(uint64_t unix_s, char *out, uint32_t cap)
{
    uint64_t sod;
    uint64_t days = w4_udiv64(unix_s, 86400u, &sod);
    if (days > 2932896u) return W4_ERR_RANGE; /* past 9999-12-31 */
    int32_t y;
    uint32_t m, d, s = (uint32_t) sod;
    civil_from_days((int32_t) days, &y, &m, &d);
    w4_w w;
    w4_w_init(&w, out, cap);
    put2(&w, (uint32_t) y / 100);
    put2(&w, (uint32_t) y % 100);
    w4_w_byte(&w, '-');
    put2(&w, m);
    w4_w_byte(&w, '-');
    put2(&w, d);
    w4_w_byte(&w, 'T');
    put2(&w, s / 3600);
    w4_w_byte(&w, ':');
    put2(&w, (s / 60) % 60);
    w4_w_byte(&w, ':');
    put2(&w, s % 60);
    w4_w_byte(&w, 'Z');
    return w4_w_cstr(&w);
}

static bool digits(const char *s, uint32_t n, uint32_t *v)
{
    uint32_t r = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (!w4_is_digit(s[i])) return false;
        r = r * 10 + (uint32_t) (s[i] - '0');
    }
    *v = r;
    return true;
}

int w4_rfc3339_parse(const char *s, uint32_t len, uint64_t *unix_s)
{
    uint32_t y, mo, d, h, mi, se;
    if (len < 20 || s[4] != '-' || s[7] != '-' || (s[10] != 'T' && s[10] != 't') || s[13] != ':' ||
        s[16] != ':')
        return W4_ERR_PARSE;
    if (!digits(s, 4, &y) || !digits(s + 5, 2, &mo) || !digits(s + 8, 2, &d) ||
        !digits(s + 11, 2, &h) || !digits(s + 14, 2, &mi) || !digits(s + 17, 2, &se))
        return W4_ERR_PARSE;
    uint32_t i = 19;
    if (i < len && s[i] == '.') {
        i++;
        uint32_t fs = i;
        while (i < len && w4_is_digit(s[i])) i++;
        if (i == fs || i - fs > 9) return W4_ERR_PARSE;
    }
    if (i + 1 != len || (s[i] != 'Z' && s[i] != 'z')) return W4_ERR_PARSE; /* UTC only */
    static const uint8_t mdays[12] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (y < 1970 || mo < 1 || mo > 12 || d < 1 || d > mdays[mo - 1] || h > 23 || mi > 59 || se > 59)
        return W4_ERR_PARSE;
    bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    if (mo == 2 && d == 29 && !leap) return W4_ERR_PARSE;
    int32_t days = days_from_civil((int32_t) y, mo, d);
    *unix_s = (uint64_t) (uint32_t) days * 86400u + h * 3600u + mi * 60u + se;
    return W4_OK;
}

/* ===== text <-> HTML for ActivityPub content ===== */
static void html_escape(w4_w *w, const char *s)
{
    for (; *s; s++) {
        switch (*s) {
        case '&':
            w4_w_str(w, "&amp;");
            break;
        case '<':
            w4_w_str(w, "&lt;");
            break;
        case '>':
            w4_w_str(w, "&gt;");
            break;
        case '"':
            w4_w_str(w, "&quot;");
            break;
        case '\'':
            w4_w_str(w, "&#39;");
            break;
        case '\n':
            w4_w_str(w, "<br>");
            break;
        default:
            w4_w_byte(w, (uint8_t) *s);
        }
    }
}

/* Drop tags (<br> and </p><p> become newlines), decode five entities. */
static int html_to_text(const char *h, uint32_t n, char *out, uint32_t cap)
{
    w4_w w;
    w4_w_init(&w, out, cap);
    bool started = false;
    for (uint32_t i = 0; i < n;) {
        if (h[i] == '<') {
            uint32_t e = i;
            while (e < n && h[e] != '>') e++;
            if (e >= n) return W4_ERR_PARSE;
            uint32_t tl = e - i - 1;
            const char *tg = h + i + 1;
            bool br = (tl >= 2 && w4_lower(tg[0]) == 'b' && w4_lower(tg[1]) == 'r');
            bool popen = (tl >= 1 && w4_lower(tg[0]) == 'p' && (tl == 1 || tg[1] == ' '));
            if (br || (popen && started)) w4_w_byte(&w, '\n');
            i = e + 1;
            continue;
        }
        if (h[i] == '&') {
            static const char *ent[6] = {"&amp;", "&lt;", "&gt;", "&quot;", "&#39;", "&apos;"};
            static const char val[6] = {'&', '<', '>', '"', '\'', '\''};
            int k;
            for (k = 0; k < 6; k++) {
                uint32_t L = (uint32_t) w4_strnlen(ent[k], 8);
                if (n - i >= L && w4_memeq(h + i, ent[k], L)) {
                    w4_w_byte(&w, (uint8_t) val[k]);
                    i += L;
                    break;
                }
            }
            if (k < 6) {
                started = true;
                continue;
            }
        }
        w4_w_byte(&w, (uint8_t) h[i++]);
        started = true;
    }
    return w4_w_cstr(&w) < 0 ? W4_ERR_SPACE : W4_OK;
}

int32_t w4_ap_note_build(const w4_post_t *p, char *out, uint32_t cap)
{
    if (!p || !p->id[0] || !p->author[0]) return W4_ERR_ARG;
    char ts[24], html[2 * W4_POST_TEXT_MAX + 64];
    if (w4_rfc3339_format(p->created_s, ts, sizeof ts) < 0) return W4_ERR_RANGE;
    w4_w hw;
    w4_w_init(&hw, html, sizeof html);
    w4_w_str(&hw, "<p>");
    html_escape(&hw, p->text);
    w4_w_str(&hw, "</p>");
    if (w4_w_cstr(&hw) < 0) return W4_ERR_SPACE;
    w4_jw j;
    w4_jw_init(&j, out, cap);
    w4_jw_obj(&j);
    w4_jw_key(&j, "@context");
    w4_jw_str(&j, "https://www.w3.org/ns/activitystreams");
    w4_jw_key(&j, "type");
    w4_jw_str(&j, "Create");
    w4_jw_key(&j, "actor");
    w4_jw_str(&j, p->author);
    w4_jw_key(&j, "published");
    w4_jw_str(&j, ts);
    w4_jw_key(&j, "to");
    w4_jw_arr(&j);
    w4_jw_str(&j, AS_PUBLIC);
    w4_jw_arr_end(&j);
    w4_jw_key(&j, "object");
    w4_jw_obj(&j);
    w4_jw_key(&j, "type");
    w4_jw_str(&j, "Note");
    w4_jw_key(&j, "id");
    w4_jw_str(&j, p->id);
    w4_jw_key(&j, "attributedTo");
    w4_jw_str(&j, p->author);
    w4_jw_key(&j, "content");
    w4_jw_str(&j, html);
    w4_jw_key(&j, "published");
    w4_jw_str(&j, ts);
    w4_jw_key(&j, "to");
    w4_jw_arr(&j);
    w4_jw_str(&j, AS_PUBLIC);
    w4_jw_arr_end(&j);
    if (p->reply_to[0]) {
        w4_jw_key(&j, "inReplyTo");
        w4_jw_str(&j, p->reply_to);
    }
    if (p->lang[0]) {
        w4_jw_key(&j, "contentMap");
        w4_jw_obj(&j);
        w4_jw_key(&j, p->lang);
        w4_jw_str(&j, html);
        w4_jw_obj_end(&j);
    }
    w4_jw_obj_end(&j);
    w4_jw_obj_end(&j);
    return w4_jw_finish(&j);
}

static int opt(const char *js, const w4_jtok_t *t, int32_t n, int32_t o, const char *k, char *out,
               uint32_t cap)
{
    int32_t v = w4_json_get(js, t, n, o, k);
    if (v == W4_ERR_NOTFOUND) return W4_OK;
    if (v < 0) return W4_ERR_PARSE;
    if (t[v].type == W4_J_NULL) return W4_OK;
    if (t[v].type != W4_J_STR) return W4_ERR_PARSE;
    return w4_json_str(js, &t[v], out, cap) < 0 ? W4_ERR_SPACE : W4_OK;
}

int w4_ap_note_parse(const char *json, uint32_t len, w4_post_t *p)
{
    if (!json || !p) return W4_ERR_ARG;
    w4_memset(p, 0, sizeof *p);
    w4_jtok_t t[SOC_TOKS];
    int32_t n = w4_json_parse(json, len, t, SOC_TOKS, 8);
    if (n <= 0 || t[0].type != W4_J_OBJ) return W4_ERR_PARSE;
    int32_t ty = w4_json_get(json, t, n, 0, "type");
    if (ty < 0 || t[ty].type != W4_J_STR) return W4_ERR_PARSE;
    int32_t note = 0;
    if (w4_json_str_eq(json, &t[ty], "Create")) {
        if (opt(json, t, n, 0, "actor", p->author, sizeof p->author)) return W4_ERR_PARSE;
        note = w4_json_get(json, t, n, 0, "object");
        if (note < 0) return W4_ERR_PARSE;
        if (t[note].type != W4_J_OBJ) return W4_ERR_UNSUPP; /* object by reference */
        int32_t nt = w4_json_get(json, t, n, note, "type");
        if (nt < 0 || !w4_json_str_eq(json, &t[nt], "Note")) return W4_ERR_UNSUPP;
    } else if (!w4_json_str_eq(json, &t[ty], "Note")) {
        return W4_ERR_UNSUPP;
    }
    if (w4_json_get_str(json, t, n, note, "id", p->id, sizeof p->id)) return W4_ERR_PARSE;
    if (opt(json, t, n, note, "attributedTo", p->author, sizeof p->author)) return W4_ERR_PARSE;
    if (!p->author[0]) return W4_ERR_PARSE;
    if (opt(json, t, n, note, "inReplyTo", p->reply_to, sizeof p->reply_to)) return W4_ERR_PARSE;
    int32_t c = w4_json_get(json, t, n, note, "content");
    if (c < 0 || t[c].type != W4_J_STR) return W4_ERR_PARSE;
    char html[2 * W4_POST_TEXT_MAX + 64];
    int32_t hl = w4_json_str(json, &t[c], html, sizeof html);
    if (hl < 0) return W4_ERR_SPACE;
    if (html_to_text(html, (uint32_t) hl, p->text, sizeof p->text)) return W4_ERR_SPACE;
    char ts[48];
    ts[0] = 0;
    if (opt(json, t, n, note, "published", ts, sizeof ts)) return W4_ERR_PARSE;
    if (ts[0] && w4_rfc3339_parse(ts, (uint32_t) w4_strnlen(ts, sizeof ts), &p->created_s))
        return W4_ERR_PARSE;
    int32_t cm = w4_json_get(json, t, n, note, "contentMap");
    if (cm >= 0 && t[cm].type == W4_J_OBJ && t[cm].size > 0)
        w4_json_str(json, &t[cm + 1], p->lang, sizeof p->lang);
    p->origin = W4_POST_ACTIVITYPUB;
    return W4_OK;
}

static uint32_t codepoints(const char *s)
{
    uint32_t n = 0;
    for (; *s; s++)
        if (((uint8_t) *s & 0xC0u) != 0x80u) n++;
    return n;
}

int32_t w4_atp_post_build(const w4_post_t *p, const char *reply_cid, char *out, uint32_t cap)
{
    if (!p) return W4_ERR_ARG;
    uint32_t bl = (uint32_t) w4_strnlen(p->text, sizeof p->text);
    if (bl > 3000 || codepoints(p->text) > 300) return W4_ERR_RANGE; /* lexicon limits */
    char ts[24];
    if (w4_rfc3339_format(p->created_s, ts, sizeof ts) < 0) return W4_ERR_RANGE;
    if (p->reply_to[0]) {
        const char *r = p->reply_to;
        bool at = r[0] == 'a' && r[1] == 't' && r[2] == ':' && r[3] == '/' && r[4] == '/';
        if (!at || !reply_cid || !reply_cid[0]) return W4_ERR_ARG;
    }
    w4_jw j;
    w4_jw_init(&j, out, cap);
    w4_jw_obj(&j);
    w4_jw_key(&j, "$type");
    w4_jw_str(&j, "app.bsky.feed.post");
    w4_jw_key(&j, "text");
    w4_jw_str(&j, p->text);
    w4_jw_key(&j, "createdAt");
    w4_jw_str(&j, ts);
    if (p->lang[0]) {
        w4_jw_key(&j, "langs");
        w4_jw_arr(&j);
        w4_jw_str(&j, p->lang);
        w4_jw_arr_end(&j);
    }
    if (p->reply_to[0]) {
        w4_jw_key(&j, "reply");
        w4_jw_obj(&j);
        const char *keys[2] = {"root", "parent"};
        for (int k = 0; k < 2; k++) {
            w4_jw_key(&j, keys[k]);
            w4_jw_obj(&j);
            w4_jw_key(&j, "uri");
            w4_jw_str(&j, p->reply_to);
            w4_jw_key(&j, "cid");
            w4_jw_str(&j, reply_cid);
            w4_jw_obj_end(&j);
        }
        w4_jw_obj_end(&j);
    }
    w4_jw_obj_end(&j);
    return w4_jw_finish(&j);
}

int w4_atp_post_parse(const char *json, uint32_t len, w4_post_t *p)
{
    if (!json || !p) return W4_ERR_ARG;
    w4_memset(p, 0, sizeof *p);
    w4_jtok_t t[SOC_TOKS];
    int32_t n = w4_json_parse(json, len, t, SOC_TOKS, 8);
    if (n <= 0 || t[0].type != W4_J_OBJ) return W4_ERR_PARSE;
    int32_t ty = w4_json_get(json, t, n, 0, "$type");
    if (ty < 0 || !w4_json_str_eq(json, &t[ty], "app.bsky.feed.post")) return W4_ERR_UNSUPP;
    if (w4_json_get_str(json, t, n, 0, "text", p->text, sizeof p->text)) return W4_ERR_PARSE;
    if (codepoints(p->text) > 300) return W4_ERR_RANGE;
    char ts[48];
    if (w4_json_get_str(json, t, n, 0, "createdAt", ts, sizeof ts)) return W4_ERR_PARSE;
    if (w4_rfc3339_parse(ts, (uint32_t) w4_strnlen(ts, sizeof ts), &p->created_s))
        return W4_ERR_PARSE;
    int32_t lg = w4_json_get(json, t, n, 0, "langs");
    if (lg >= 0 && t[lg].type == W4_J_ARR && t[lg].size > 0) {
        int32_t e = w4_json_at(t, n, lg, 0);
        if (t[e].type != W4_J_STR || w4_json_str(json, &t[e], p->lang, sizeof p->lang) < 0)
            return W4_ERR_PARSE;
    }
    int32_t rp = w4_json_get(json, t, n, 0, "reply");
    if (rp >= 0) {
        if (t[rp].type != W4_J_OBJ) return W4_ERR_PARSE;
        int32_t par = w4_json_get(json, t, n, rp, "parent");
        if (par < 0 || t[par].type != W4_J_OBJ) return W4_ERR_PARSE;
        if (w4_json_get_str(json, t, n, par, "uri", p->reply_to, sizeof p->reply_to))
            return W4_ERR_PARSE;
    }
    p->origin = W4_POST_ATPROTO;
    return W4_OK;
}
