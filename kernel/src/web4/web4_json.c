/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* web4_json.c — bounded RFC 8259 tokenizer and writer (see web4_web2.h). */
#include "web4_web2.h"

/* Length of the valid UTF-8 sequence starting at s[i] (1..4), or 0 if the
 * bytes there are not well-formed UTF-8 (overlong, surrogate, > U+10FFFF,
 * truncated). */
static uint32_t utf8_seq(const uint8_t *s, uint32_t i, uint32_t len)
{
    uint8_t c = s[i];
    if (c < 0x80) return 1;
    uint32_t n;
    uint32_t cp;
    if (c >= 0xC2 && c <= 0xDF) {
        n = 2;
        cp = c & 0x1Fu;
    } else if (c >= 0xE0 && c <= 0xEF) {
        n = 3;
        cp = c & 0x0Fu;
    } else if (c >= 0xF0 && c <= 0xF4) {
        n = 4;
        cp = c & 0x07u;
    } else {
        return 0;
    }
    if (len - i < n) return 0;
    for (uint32_t k = 1; k < n; k++) {
        if ((s[i + k] & 0xC0u) != 0x80u) return 0;
        cp = (cp << 6) | (s[i + k] & 0x3Fu);
    }
    if (n == 3 && (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF))) return 0;
    if (n == 4 && (cp < 0x10000 || cp > 0x10FFFF)) return 0;
    return n;
}

static bool is_ws(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static int hex4(const char *p, uint32_t *v)
{
    uint32_t r = 0;
    for (int i = 0; i < 4; i++) {
        int h = w4_hexval(p[i]);
        if (h < 0) return -1;
        r = (r << 4) | (uint32_t) h;
    }
    *v = r;
    return 0;
}

/* Scan a string starting at the opening quote js[i]. Returns the index of the
 * closing quote, or -1. */
static int64_t scan_string(const char *js, uint32_t len, uint32_t i)
{
    const uint8_t *u = (const uint8_t *) js;
    i++;
    while (i < len) {
        uint8_t c = u[i];
        if (c == '"') return i;
        if (c < 0x20) return -1;
        if (c == '\\') {
            if (i + 1 >= len) return -1;
            char e = js[i + 1];
            if (e == 'u') {
                uint32_t cp;
                if (i + 6 > len || hex4(js + i + 2, &cp) < 0) return -1;
                i += 6;
                if (cp >= 0xDC00 && cp <= 0xDFFF) return -1; /* lone low surrogate */
                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    uint32_t lo;
                    if (i + 6 > len || js[i] != '\\' || js[i + 1] != 'u' ||
                        hex4(js + i + 2, &lo) < 0 || lo < 0xDC00 || lo > 0xDFFF)
                        return -1;
                    i += 6;
                }
                continue;
            }
            if (e != '"' && e != '\\' && e != '/' && e != 'b' && e != 'f' && e != 'n' && e != 'r' &&
                e != 't')
                return -1;
            i += 2;
            continue;
        }
        uint32_t n = utf8_seq(u, i, len);
        if (!n) return -1;
        i += n;
    }
    return -1;
}

/* Scan a number starting at js[i]. Returns the index one past it, or -1. */
static int64_t scan_number(const char *js, uint32_t len, uint32_t i)
{
    if (i < len && js[i] == '-') i++;
    if (i >= len) return -1;
    if (js[i] == '0') {
        i++;
    } else if (js[i] >= '1' && js[i] <= '9') {
        while (i < len && w4_is_digit(js[i])) i++;
    } else {
        return -1;
    }
    if (i < len && js[i] == '.') {
        i++;
        if (i >= len || !w4_is_digit(js[i])) return -1;
        while (i < len && w4_is_digit(js[i])) i++;
    }
    if (i < len && (js[i] == 'e' || js[i] == 'E')) {
        i++;
        if (i < len && (js[i] == '+' || js[i] == '-')) i++;
        if (i >= len || !w4_is_digit(js[i])) return -1;
        while (i < len && w4_is_digit(js[i])) i++;
    }
    if (i < len && !is_ws(js[i]) && js[i] != ',' && js[i] != ']' && js[i] != '}') return -1;
    return i;
}

enum {
    EXP_VALUE,
    EXP_KEY_OR_CLOSE,
    EXP_KEY,
    EXP_COLON,
    EXP_COMMA_OR_CLOSE,
    EXP_ARR_FIRST,
    EXP_END
};

int32_t w4_json_parse(const char *js, uint32_t len, w4_jtok_t *t, uint32_t max, uint32_t max_depth)
{
    int32_t stack[W4_JSON_MAX_DEPTH];
    uint32_t depth = 0;
    uint32_t n = 0;
    int32_t pending_parent = -1; /* parent for the next value */
    int st = EXP_VALUE;
    uint32_t i = 0;
    if (!js || !t) return W4_ERR_ARG;
    if (max_depth == 0 || max_depth > W4_JSON_MAX_DEPTH) max_depth = W4_JSON_MAX_DEPTH;

    while (1) {
        while (i < len && is_ws(js[i])) i++;
        if (i >= len) break;
        char c = js[i];
        if (st == EXP_END) return W4_ERR_PARSE; /* trailing garbage */

        if (st == EXP_COLON) {
            if (c != ':') return W4_ERR_PARSE;
            i++;
            st = EXP_VALUE;
            continue;
        }
        if (st == EXP_COMMA_OR_CLOSE || st == EXP_KEY_OR_CLOSE || st == EXP_ARR_FIRST) {
            if (c == '}' || c == ']') {
                if (depth == 0) return W4_ERR_PARSE;
                int32_t top = stack[depth - 1];
                uint8_t want = (c == '}') ? W4_J_OBJ : W4_J_ARR;
                if (t[top].type != want) return W4_ERR_PARSE;
                if (st == EXP_KEY_OR_CLOSE && want != W4_J_OBJ) return W4_ERR_PARSE;
                if (st == EXP_ARR_FIRST && want != W4_J_ARR) return W4_ERR_PARSE;
                t[top].end = i + 1;
                depth--;
                i++;
                st = depth ? EXP_COMMA_OR_CLOSE : EXP_END;
                continue;
            }
            if (st == EXP_COMMA_OR_CLOSE) {
                if (c != ',') return W4_ERR_PARSE;
                i++;
                st = (t[stack[depth - 1]].type == W4_J_OBJ) ? EXP_KEY : EXP_VALUE;
                if (st == EXP_VALUE) pending_parent = stack[depth - 1];
                continue;
            }
            if (st == EXP_KEY_OR_CLOSE) st = EXP_KEY;
            if (st == EXP_ARR_FIRST) st = EXP_VALUE;
        }

        if (st == EXP_KEY) {
            if (c != '"') return W4_ERR_PARSE;
            int64_t e = scan_string(js, len, i);
            if (e < 0) return W4_ERR_PARSE;
            if (n >= max) return W4_ERR_SPACE;
            int32_t obj = stack[depth - 1];
            t[n].type = W4_J_STR;
            t[n].start = i + 1;
            t[n].end = (uint32_t) e;
            t[n].size = 1;
            t[n].parent = obj;
            t[obj].size++;
            pending_parent = (int32_t) n;
            n++;
            i = (uint32_t) e + 1;
            st = EXP_COLON;
            continue;
        }

        /* st == EXP_VALUE */
        if (n >= max) return W4_ERR_SPACE;
        int32_t par = (depth == 0) ? -1 : pending_parent;
        if (depth > 0 && t[stack[depth - 1]].type == W4_J_ARR) t[stack[depth - 1]].size++;
        t[n].parent = par;
        t[n].size = 0;
        if (c == '{' || c == '[') {
            if (depth >= max_depth) return W4_ERR_PARSE;
            t[n].type = (c == '{') ? W4_J_OBJ : W4_J_ARR;
            t[n].start = i;
            t[n].end = 0;
            stack[depth++] = (int32_t) n;
            pending_parent = (int32_t) n;
            n++;
            i++;
            st = (c == '{') ? EXP_KEY_OR_CLOSE : EXP_ARR_FIRST;
            continue;
        }
        if (c == '"') {
            int64_t e = scan_string(js, len, i);
            if (e < 0) return W4_ERR_PARSE;
            t[n].type = W4_J_STR;
            t[n].start = i + 1;
            t[n].end = (uint32_t) e;
            i = (uint32_t) e + 1;
        } else if (c == '-' || w4_is_digit(c)) {
            int64_t e = scan_number(js, len, i);
            if (e < 0) return W4_ERR_PARSE;
            t[n].type = W4_J_NUM;
            t[n].start = i;
            t[n].end = (uint32_t) e;
            i = (uint32_t) e;
        } else {
            static const char *lits[3] = {"true", "false", "null"};
            static const uint8_t types[3] = {W4_J_TRUE, W4_J_FALSE, W4_J_NULL};
            int which = -1;
            for (int k = 0; k < 3; k++) {
                uint32_t L = (uint32_t) w4_strnlen(lits[k], 8);
                if (len - i >= L && w4_memeq(js + i, lits[k], L)) {
                    which = k;
                    break;
                }
            }
            if (which < 0) return W4_ERR_PARSE;
            uint32_t L = (uint32_t) w4_strnlen(lits[which], 8);
            if (i + L < len && !is_ws(js[i + L]) && js[i + L] != ',' && js[i + L] != ']' &&
                js[i + L] != '}')
                return W4_ERR_PARSE;
            t[n].type = types[which];
            t[n].start = i;
            t[n].end = i + L;
            i += L;
        }
        n++;
        st = depth ? EXP_COMMA_OR_CLOSE : EXP_END;
        if (depth && t[stack[depth - 1]].type == W4_J_ARR) pending_parent = stack[depth - 1];
    }
    if (st != EXP_END) return W4_ERR_PARSE;
    return (int32_t) n;
}

int32_t w4_json_skip(const w4_jtok_t *t, int32_t n, int32_t i)
{
    int32_t j = i + 1;
    while (j < n && t[j].start < t[i].end) j++;
    return j;
}

int32_t w4_json_at(const w4_jtok_t *t, int32_t n, int32_t arr, uint32_t idx)
{
    if (arr < 0 || arr >= n || t[arr].type != W4_J_ARR || idx >= t[arr].size)
        return W4_ERR_NOTFOUND;
    int32_t i = arr + 1;
    for (uint32_t k = 0; k < idx; k++) i = w4_json_skip(t, n, i);
    return i;
}

static void put_utf8(w4_w *w, uint32_t cp)
{
    if (cp < 0x80) {
        w4_w_byte(w, (uint8_t) cp);
    } else if (cp < 0x800) {
        w4_w_byte(w, (uint8_t) (0xC0 | (cp >> 6)));
        w4_w_byte(w, (uint8_t) (0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        w4_w_byte(w, (uint8_t) (0xE0 | (cp >> 12)));
        w4_w_byte(w, (uint8_t) (0x80 | ((cp >> 6) & 0x3F)));
        w4_w_byte(w, (uint8_t) (0x80 | (cp & 0x3F)));
    } else {
        w4_w_byte(w, (uint8_t) (0xF0 | (cp >> 18)));
        w4_w_byte(w, (uint8_t) (0x80 | ((cp >> 12) & 0x3F)));
        w4_w_byte(w, (uint8_t) (0x80 | ((cp >> 6) & 0x3F)));
        w4_w_byte(w, (uint8_t) (0x80 | (cp & 0x3F)));
    }
}

int32_t w4_json_str(const char *js, const w4_jtok_t *tok, char *out, uint32_t cap)
{
    if (!tok || tok->type != W4_J_STR) return W4_ERR_ARG;
    w4_w w;
    w4_w_init(&w, out, cap);
    for (uint32_t i = tok->start; i < tok->end;) {
        char c = js[i];
        if (c != '\\') {
            w4_w_byte(&w, (uint8_t) c);
            i++;
            continue;
        }
        char e = js[i + 1];
        if (e == 'u') {
            uint32_t cp = 0, lo = 0;
            hex4(js + i + 2, &cp);
            i += 6;
            if (cp >= 0xD800 && cp <= 0xDBFF) {
                hex4(js + i + 2, &lo);
                i += 6;
                cp = 0x10000u + ((cp - 0xD800u) << 10) + (lo - 0xDC00u);
            }
            put_utf8(&w, cp);
            continue;
        }
        char d = e;
        if (e == 'b') d = '\b';
        if (e == 'f') d = '\f';
        if (e == 'n') d = '\n';
        if (e == 'r') d = '\r';
        if (e == 't') d = '\t';
        w4_w_byte(&w, (uint8_t) d);
        i += 2;
    }
    return w4_w_cstr(&w);
}

bool w4_json_str_eq(const char *js, const w4_jtok_t *tok, const char *s)
{
    char buf[512];
    int32_t n = w4_json_str(js, tok, buf, sizeof buf);
    if (n < 0) return false;
    return w4_streq(buf, s);
}

int32_t w4_json_get(const char *js, const w4_jtok_t *t, int32_t n, int32_t obj, const char *key)
{
    if (obj < 0 || obj >= n || t[obj].type != W4_J_OBJ) return W4_ERR_NOTFOUND;
    int32_t found = W4_ERR_NOTFOUND;
    int32_t i = obj + 1;
    for (uint32_t k = 0; k < t[obj].size && i < n; k++) {
        if (w4_json_str_eq(js, &t[i], key)) {
            if (found >= 0) return W4_ERR_PARSE; /* duplicate key */
            found = i + 1;
        }
        i = w4_json_skip(t, n, i + 1);
    }
    return found;
}

static int parse_int(const char *js, const w4_jtok_t *tok, bool *neg, uint64_t *mag)
{
    if (!tok || tok->type != W4_J_NUM) return W4_ERR_ARG;
    uint32_t i = tok->start;
    *neg = false;
    if (js[i] == '-') {
        *neg = true;
        i++;
    }
    uint64_t v = 0;
    for (; i < tok->end; i++) {
        char c = js[i];
        if (!w4_is_digit(c)) return W4_ERR_RANGE; /* fraction / exponent */
        uint64_t d = (uint64_t) (c - '0');
        if (v > 1844674407370955161u || (v == 1844674407370955161u && d > 5u))
            return W4_ERR_RANGE; /* would pass 2^64-1 */
        v = v * 10u + d;
    }
    *mag = v;
    return W4_OK;
}

int w4_json_u64(const char *js, const w4_jtok_t *tok, uint64_t *v)
{
    bool neg;
    uint64_t m;
    int r = parse_int(js, tok, &neg, &m);
    if (r) return r;
    if (neg && m) return W4_ERR_RANGE;
    *v = m;
    return W4_OK;
}

int w4_json_i64(const char *js, const w4_jtok_t *tok, int64_t *v)
{
    bool neg;
    uint64_t m;
    int r = parse_int(js, tok, &neg, &m);
    if (r) return r;
    if (!neg && m > (uint64_t) INT64_MAX) return W4_ERR_RANGE;
    if (neg && m > (uint64_t) INT64_MAX + 1u) return W4_ERR_RANGE;
    *v = neg ? (int64_t) (0 - m) : (int64_t) m;
    return W4_OK;
}

int w4_json_bool(const w4_jtok_t *tok, bool *v)
{
    if (!tok) return W4_ERR_ARG;
    if (tok->type == W4_J_TRUE)
        *v = true;
    else if (tok->type == W4_J_FALSE)
        *v = false;
    else
        return W4_ERR_ARG;
    return W4_OK;
}

int w4_json_get_str(const char *js, const w4_jtok_t *t, int32_t n, int32_t obj, const char *key,
                    char *out, uint32_t cap)
{
    int32_t v = w4_json_get(js, t, n, obj, key);
    if (v < 0) return v;
    if (t[v].type != W4_J_STR) return W4_ERR_PARSE;
    int32_t r = w4_json_str(js, &t[v], out, cap);
    return r < 0 ? r : W4_OK;
}

int w4_json_get_u64(const char *js, const w4_jtok_t *t, int32_t n, int32_t obj, const char *key,
                    uint64_t *v)
{
    int32_t i = w4_json_get(js, t, n, obj, key);
    if (i < 0) return i;
    return w4_json_u64(js, &t[i], v);
}

/* ===== Writer ===== */
void w4_jw_init(w4_jw *j, char *buf, uint32_t cap)
{
    w4_w_init(&j->w, buf, cap);
    j->depth = 0;
    j->after_key = false;
}

static void before_value(w4_jw *j)
{
    if (j->depth == 0) return;
    uint8_t d = (uint8_t) (j->depth - 1);
    if (j->kind[d] == W4_J_ARR) {
        if (j->count[d]) w4_w_byte(&j->w, ',');
        j->count[d] = 1;
    } else {
        if (!j->after_key) j->w.err = true; /* value in an object needs a key */
        j->after_key = false;
    }
}

static void put_str(w4_jw *j, const char *s, uint32_t n)
{
    const uint8_t *u = (const uint8_t *) s;
    w4_w_byte(&j->w, '"');
    for (uint32_t i = 0; i < n;) {
        uint8_t c = u[i];
        if (c >= 0x80) {
            uint32_t L = utf8_seq(u, i, n);
            if (!L) {
                j->w.err = true; /* refuse to emit invalid UTF-8 */
                return;
            }
            w4_w_bytes(&j->w, u + i, L);
            i += L;
            continue;
        }
        if (c == '"' || c == '\\') {
            w4_w_byte(&j->w, '\\');
            w4_w_byte(&j->w, c);
        } else if (c == '\n') {
            w4_w_str(&j->w, "\\n");
        } else if (c == '\r') {
            w4_w_str(&j->w, "\\r");
        } else if (c == '\t') {
            w4_w_str(&j->w, "\\t");
        } else if (c < 0x20) {
            static const char hx[] = "0123456789abcdef";
            w4_w_str(&j->w, "\\u00");
            w4_w_byte(&j->w, (uint8_t) hx[c >> 4]);
            w4_w_byte(&j->w, (uint8_t) hx[c & 15]);
        } else {
            w4_w_byte(&j->w, c);
        }
        i++;
    }
    w4_w_byte(&j->w, '"');
}

static void open_c(w4_jw *j, uint8_t kind, char ch)
{
    before_value(j);
    if (j->depth >= W4_JSON_MAX_DEPTH) {
        j->w.err = true;
        return;
    }
    j->kind[j->depth] = kind;
    j->count[j->depth] = 0;
    j->depth++;
    w4_w_byte(&j->w, (uint8_t) ch);
}

static void close_c(w4_jw *j, uint8_t kind, char ch)
{
    if (j->depth == 0 || j->kind[j->depth - 1] != kind || j->after_key) {
        j->w.err = true;
        return;
    }
    j->depth--;
    w4_w_byte(&j->w, (uint8_t) ch);
}

void w4_jw_obj(w4_jw *j)
{
    open_c(j, W4_J_OBJ, '{');
}

void w4_jw_obj_end(w4_jw *j)
{
    close_c(j, W4_J_OBJ, '}');
}

void w4_jw_arr(w4_jw *j)
{
    open_c(j, W4_J_ARR, '[');
}

void w4_jw_arr_end(w4_jw *j)
{
    close_c(j, W4_J_ARR, ']');
}

void w4_jw_key(w4_jw *j, const char *k)
{
    if (j->depth == 0 || j->kind[j->depth - 1] != W4_J_OBJ || j->after_key) {
        j->w.err = true;
        return;
    }
    if (j->count[j->depth - 1]) w4_w_byte(&j->w, ',');
    j->count[j->depth - 1] = 1;
    put_str(j, k, (uint32_t) w4_strnlen(k, 4096));
    w4_w_byte(&j->w, ':');
    j->after_key = true;
}

void w4_jw_strn(w4_jw *j, const char *s, uint32_t n)
{
    before_value(j);
    put_str(j, s, n);
}

void w4_jw_str(w4_jw *j, const char *s)
{
    w4_jw_strn(j, s, (uint32_t) w4_strnlen(s, W4_HTTP_MAX_BODY));
}

void w4_jw_u64(w4_jw *j, uint64_t v)
{
    before_value(j);
    w4_w_u64(&j->w, v);
}

void w4_jw_i64(w4_jw *j, int64_t v)
{
    before_value(j);
    w4_w_i64(&j->w, v);
}

void w4_jw_bool(w4_jw *j, bool v)
{
    before_value(j);
    w4_w_str(&j->w, v ? "true" : "false");
}

void w4_jw_null(w4_jw *j)
{
    before_value(j);
    w4_w_str(&j->w, "null");
}

void w4_jw_hexstr(w4_jw *j, const uint8_t *p, uint32_t n)
{
    before_value(j);
    w4_w_str(&j->w, "\"0x");
    w4_w_hex(&j->w, p, n);
    w4_w_byte(&j->w, '"');
}

void w4_jw_qty(w4_jw *j, const w4_u256 *v)
{
    before_value(j);
    w4_w_byte(&j->w, '"');
    w4_w_u256_qty(&j->w, v);
    w4_w_byte(&j->w, '"');
}

void w4_jw_qty64(w4_jw *j, uint64_t v)
{
    w4_u256 u;
    w4_u256_from_u64(&u, v);
    w4_jw_qty(j, &u);
}

int32_t w4_jw_finish(w4_jw *j)
{
    if (j->depth != 0 || j->after_key) j->w.err = true;
    return w4_w_cstr(&j->w);
}
