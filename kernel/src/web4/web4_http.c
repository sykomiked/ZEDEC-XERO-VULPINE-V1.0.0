/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* web4_http.c — bounded HTTP/1.1 message parse/build, URLs, form encoding. */
#include "web4_web2.h"

#define HEAD_MAX 65536u

static bool is_tchar(uint8_t c)
{
    if (c >= '0' && c <= '9') return true;
    if ((c | 0x20) >= 'a' && (c | 0x20) <= 'z') return true;
    switch (c) {
    case '!':
    case '#':
    case '$':
    case '%':
    case '&':
    case '\'':
    case '*':
    case '+':
    case '-':
    case '.':
    case '^':
    case '_':
    case '`':
    case '|':
    case '~':
        return true;
    default:
        return false;
    }
}

/* field-value octets: VCHAR, SP, HTAB, obs-text. */
static bool is_fchar(uint8_t c)
{
    return c == '\t' || (c >= 0x20 && c != 0x7f);
}

/* Locate the end of the header section. Returns index just past CRLFCRLF,
 * 0 if not yet present, -1 on a bare LF or oversized head. */
static int64_t head_end(const uint8_t *b, uint32_t len)
{
    uint32_t lim = len < HEAD_MAX ? len : HEAD_MAX;
    for (uint32_t i = 0; i < lim; i++) {
        if (b[i] == '\n' && (i == 0 || b[i - 1] != '\r')) return -1;
        if (i >= 3 && b[i] == '\n' && b[i - 1] == '\r' && b[i - 2] == '\n' && b[i - 3] == '\r')
            return i + 1;
    }
    return len >= HEAD_MAX ? -1 : 0;
}

static int parse_version(const uint8_t *p, uint32_t n, uint8_t *minor)
{
    if (n != 8 || !w4_memeq(p, "HTTP/1.", 7)) return W4_ERR_PARSE;
    if (p[7] != '0' && p[7] != '1') return W4_ERR_PARSE;
    *minor = (uint8_t) (p[7] - '0');
    return W4_OK;
}

/* Parse header lines from b[i..end-2). */
static int parse_headers(const uint8_t *b, uint32_t i, uint32_t end, w4_http_msg_t *m)
{
    m->nh = 0;
    while (i + 2 <= end) {
        if (b[i] == '\r' && b[i + 1] == '\n') return (i + 2 == end) ? W4_OK : W4_ERR_PARSE;
        if (b[i] == ' ' || b[i] == '\t') return W4_ERR_PARSE; /* obs-fold */
        uint32_t ns = i;
        while (i < end && is_tchar(b[i])) i++;
        if (i == ns || i >= end || b[i] != ':') return W4_ERR_PARSE; /* incl. space before ':' */
        uint32_t ne = i++;
        while (i < end && (b[i] == ' ' || b[i] == '\t')) i++;
        uint32_t vs = i;
        while (i < end && b[i] != '\r') {
            if (!is_fchar(b[i])) return W4_ERR_PARSE;
            i++;
        }
        if (i + 1 >= end || b[i + 1] != '\n') return W4_ERR_PARSE;
        uint32_t ve = i;
        while (ve > vs && (b[ve - 1] == ' ' || b[ve - 1] == '\t')) ve--;
        if (m->nh >= W4_HTTP_MAX_HDRS) return W4_ERR_PARSE;
        m->h[m->nh].name = (const char *) b + ns;
        m->h[m->nh].name_len = ne - ns;
        m->h[m->nh].val = (const char *) b + vs;
        m->h[m->nh].val_len = ve - vs;
        m->nh++;
        i += 2;
    }
    return W4_ERR_PARSE;
}

const w4_hdr_t *w4_http_hdr(const w4_http_msg_t *m, const char *name)
{
    uint32_t nl = (uint32_t) w4_strnlen(name, 256);
    for (uint32_t i = 0; i < m->nh; i++)
        if (w4_casecmp_eq(m->h[i].name, m->h[i].name_len, name, nl)) return &m->h[i];
    return NULL;
}

/* Content-Length / Transfer-Encoding framing. *kind: 0 none, 1 length, 2 chunked. */
static int framing(const w4_http_msg_t *m, int *kind, uint32_t *clen)
{
    bool have_cl = false, have_te = false;
    uint64_t cl = 0;
    for (uint32_t i = 0; i < m->nh; i++) {
        const w4_hdr_t *h = &m->h[i];
        if (w4_casecmp_eq(h->name, h->name_len, "content-length", 14)) {
            if (h->val_len == 0 || h->val_len > 10) return W4_ERR_PARSE;
            uint64_t v = 0;
            for (uint32_t k = 0; k < h->val_len; k++) {
                if (!w4_is_digit(h->val[k])) return W4_ERR_PARSE;
                v = v * 10u + (uint64_t) (h->val[k] - '0');
            }
            if (have_cl && v != cl) return W4_ERR_PARSE;
            have_cl = true;
            cl = v;
        } else if (w4_casecmp_eq(h->name, h->name_len, "transfer-encoding", 17)) {
            if (have_te || !w4_casecmp_eq(h->val, h->val_len, "chunked", 7)) return W4_ERR_PARSE;
            have_te = true;
        }
    }
    if (have_cl && have_te) return W4_ERR_PARSE;
    if (cl > W4_HTTP_MAX_BODY) return W4_ERR_PARSE;
    *kind = have_te ? 2 : (have_cl ? 1 : 0);
    *clen = (uint32_t) cl;
    return W4_OK;
}

/* De-chunk b[i..len) into out. *used gets the input bytes consumed. */
static int dechunk(const uint8_t *b, uint32_t i, uint32_t len, uint8_t *out, uint32_t cap,
                   uint32_t *olen, uint32_t *used)
{
    uint32_t o = 0;
    if (!out) return W4_ERR_PARSE;
    while (1) {
        uint32_t s = i;
        uint64_t sz = 0;
        while (i < len && w4_is_hex((char) b[i])) {
            if (i - s >= 8) return W4_ERR_PARSE;
            sz = (sz << 4) | (uint64_t) w4_hexval((char) b[i]);
            i++;
        }
        if (i >= len) return W4_ERR_PENDING;
        if (i == s) return W4_ERR_PARSE;
        if (b[i] == ';') { /* chunk-ext: skipped, but must be clean */
            while (i < len && b[i] != '\r') {
                if (!is_fchar(b[i])) return W4_ERR_PARSE;
                i++;
            }
        }
        if (i + 1 >= len) return W4_ERR_PENDING;
        if (b[i] != '\r' || b[i + 1] != '\n') return W4_ERR_PARSE;
        i += 2;
        if (sz == 0) {
            if (i + 1 >= len) return W4_ERR_PENDING;
            if (b[i] != '\r' || b[i + 1] != '\n') return W4_ERR_PARSE; /* trailers refused */
            *olen = o;
            *used = i + 2;
            return W4_OK;
        }
        if (sz > W4_HTTP_MAX_BODY || sz > cap - o) return W4_ERR_PARSE;
        if (len - i < sz + 2) return W4_ERR_PENDING;
        w4_memcpy(out + o, b + i, (size_t) sz);
        o += (uint32_t) sz;
        i += (uint32_t) sz;
        if (b[i] != '\r' || b[i + 1] != '\n') return W4_ERR_PARSE;
        i += 2;
    }
}

static int body(const uint8_t *buf, uint32_t len, uint32_t he, int kind, uint32_t cl,
                w4_http_msg_t *m, uint8_t *cb, uint32_t ccap)
{
    if (kind == 1) {
        if (len - he < cl) return W4_ERR_PENDING;
        m->body = buf + he;
        m->body_len = cl;
        m->consumed = he + cl;
    } else if (kind == 2) {
        uint32_t used = 0, ol = 0;
        int r = dechunk(buf, he, len, cb, ccap, &ol, &used);
        if (r) return r;
        m->chunked = true;
        m->body = cb;
        m->body_len = ol;
        m->consumed = used;
    } else {
        m->body = buf + he;
        m->body_len = 0;
        m->consumed = he;
    }
    return W4_OK;
}

int w4_http_parse_request(const uint8_t *buf, uint32_t len, w4_http_msg_t *m, uint8_t *chunk_buf,
                          uint32_t chunk_cap)
{
    if (!buf || !m) return W4_ERR_ARG;
    w4_memset(m, 0, sizeof *m);
    int64_t he = head_end(buf, len);
    if (he < 0) return W4_ERR_PARSE;
    if (he == 0) return W4_ERR_PENDING;
    uint32_t i = 0, e = (uint32_t) he;
    while (i < e && is_tchar(buf[i])) i++;
    if (i == 0 || buf[i] != ' ') return W4_ERR_PARSE;
    m->method = (const char *) buf;
    m->method_len = i++;
    uint32_t ts = i;
    while (i < e && buf[i] > 0x20 && buf[i] != 0x7f) i++;
    if (i == ts || buf[i] != ' ') return W4_ERR_PARSE;
    m->target = (const char *) buf + ts;
    m->target_len = i - ts;
    i++;
    uint32_t vs = i;
    while (i < e && buf[i] != '\r') i++;
    if (parse_version(buf + vs, i - vs, &m->minor)) return W4_ERR_PARSE;
    i += 2;
    int r = parse_headers(buf, i, e, m);
    if (r) return r;
    if (m->minor == 1 && !w4_http_hdr(m, "host")) return W4_ERR_PARSE; /* RFC 9112 3.2 */
    int kind;
    uint32_t cl;
    if (framing(m, &kind, &cl)) return W4_ERR_PARSE;
    return body(buf, len, e, kind, cl, m, chunk_buf, chunk_cap);
}

int w4_http_parse_response(const uint8_t *buf, uint32_t len, bool head_request, w4_http_msg_t *m,
                           uint8_t *chunk_buf, uint32_t chunk_cap)
{
    if (!buf || !m) return W4_ERR_ARG;
    w4_memset(m, 0, sizeof *m);
    int64_t he = head_end(buf, len);
    if (he < 0) return W4_ERR_PARSE;
    if (he == 0) return W4_ERR_PENDING;
    uint32_t e = (uint32_t) he;
    if (e < 14 || parse_version(buf, 8, &m->minor) || buf[8] != ' ') return W4_ERR_PARSE;
    if (!w4_is_digit((char) buf[9]) || !w4_is_digit((char) buf[10]) ||
        !w4_is_digit((char) buf[11]) || buf[9] < '1' || buf[9] > '5')
        return W4_ERR_PARSE;
    m->status = (uint16_t) ((buf[9] - '0') * 100 + (buf[10] - '0') * 10 + (buf[11] - '0'));
    uint32_t i = 12;
    if (buf[i] == ' ')
        i++;
    else if (buf[i] != '\r')
        return W4_ERR_PARSE;
    uint32_t rs = i;
    while (i < e && buf[i] != '\r') {
        if (!is_fchar(buf[i])) return W4_ERR_PARSE;
        i++;
    }
    m->reason = (const char *) buf + rs;
    m->reason_len = i - rs;
    i += 2;
    int r = parse_headers(buf, i, e, m);
    if (r) return r;
    int kind;
    uint32_t cl;
    if (framing(m, &kind, &cl)) return W4_ERR_PARSE;
    if (head_request || m->status < 200 || m->status == 204 || m->status == 304) {
        m->body = buf + e;
        m->body_len = 0;
        m->consumed = e;
        return W4_OK;
    }
    if (kind == 0) {
        if (len - e > W4_HTTP_MAX_BODY) return W4_ERR_PARSE;
        m->until_close = true;
        m->body = buf + e;
        m->body_len = len - e;
        m->consumed = len;
        return W4_OK;
    }
    return body(buf, len, e, kind, cl, m, chunk_buf, chunk_cap);
}

w4_hdr_t w4_hdr(const char *name, const char *val)
{
    w4_hdr_t h;
    h.name = name;
    h.name_len = (uint32_t) w4_strnlen(name, 256);
    h.val = val;
    h.val_len = (uint32_t) w4_strnlen(val, 8192);
    return h;
}

static bool hdr_ok(const w4_hdr_t *h)
{
    if (h->name_len == 0) return false;
    for (uint32_t i = 0; i < h->name_len; i++)
        if (!is_tchar((uint8_t) h->name[i])) return false;
    for (uint32_t i = 0; i < h->val_len; i++) {
        uint8_t c = (uint8_t) h->val[i];
        if (c == '\r' || c == '\n' || c == 0 || !is_fchar(c)) return false;
    }
    return true;
}

static bool str_safe(const char *s, bool allow_space)
{
    if (!s || !*s) return false;
    for (; *s; s++) {
        uint8_t c = (uint8_t) *s;
        if (c < 0x20 || c == 0x7f || (!allow_space && c == ' ')) return false;
    }
    return true;
}

static void put_headers(w4_w *w, const w4_hdr_t *hdrs, uint32_t nh)
{
    for (uint32_t i = 0; i < nh; i++) {
        if (!hdr_ok(&hdrs[i])) {
            w->err = true;
            return;
        }
        w4_w_strn(w, hdrs[i].name, hdrs[i].name_len);
        w4_w_str(w, ": ");
        w4_w_strn(w, hdrs[i].val, hdrs[i].val_len);
        w4_w_str(w, "\r\n");
    }
}

int32_t w4_http_build_request(w4_w *w, const char *method, const char *host, const char *target,
                              const w4_hdr_t *hdrs, uint32_t nh, const uint8_t *body,
                              uint32_t body_len)
{
    if (!str_safe(method, false) || !str_safe(host, false) || !str_safe(target, false))
        return W4_ERR_ARG;
    for (const char *p = method; *p; p++)
        if (!is_tchar((uint8_t) *p)) return W4_ERR_ARG;
    w4_w_str(w, method);
    w4_w_byte(w, ' ');
    w4_w_str(w, target);
    w4_w_str(w, " HTTP/1.1\r\nHost: ");
    w4_w_str(w, host);
    w4_w_str(w, "\r\n");
    put_headers(w, hdrs, nh);
    bool want_len = body_len > 0 || w4_streq(method, "POST") || w4_streq(method, "PUT") ||
                    w4_streq(method, "PATCH");
    if (want_len) {
        w4_w_str(w, "Content-Length: ");
        w4_w_u64(w, body_len);
        w4_w_str(w, "\r\n");
    }
    w4_w_str(w, "\r\n");
    if (body_len) w4_w_bytes(w, body, body_len);
    return w4_w_done(w);
}

int32_t w4_http_build_response(w4_w *w, uint16_t status, const char *reason, const w4_hdr_t *hdrs,
                               uint32_t nh, const uint8_t *body, uint32_t body_len)
{
    if (status < 100 || status > 599) return W4_ERR_ARG;
    if (reason && *reason && !str_safe(reason, true)) return W4_ERR_ARG;
    w4_w_str(w, "HTTP/1.1 ");
    w4_w_u64(w, status);
    w4_w_byte(w, ' ');
    if (reason) w4_w_str(w, reason);
    w4_w_str(w, "\r\n");
    put_headers(w, hdrs, nh);
    if (status >= 200 && status != 204 && status != 304) {
        w4_w_str(w, "Content-Length: ");
        w4_w_u64(w, body_len);
        w4_w_str(w, "\r\n");
    }
    w4_w_str(w, "\r\n");
    if (body_len) w4_w_bytes(w, body, body_len);
    return w4_w_done(w);
}

/* ===== URLs ===== */
int w4_url_parse(const char *s, uint32_t len, w4_url_t *u)
{
    if (!s || !u) return W4_ERR_ARG;
    w4_memset(u, 0, sizeof *u);
    uint32_t i = 0;
    if (len == 0 || !w4_is_alpha(s[0])) return W4_ERR_PARSE;
    while (i < len &&
           (w4_is_alpha(s[i]) || w4_is_digit(s[i]) || s[i] == '+' || s[i] == '-' || s[i] == '.'))
        i++;
    if (len - i < 3 || s[i] != ':' || s[i + 1] != '/' || s[i + 2] != '/') return W4_ERR_PARSE;
    u->scheme = s;
    u->scheme_len = i;
    i += 3;
    uint32_t hs = i;
    if (i < len && s[i] == '[') {
        while (i < len && s[i] != ']') i++;
        if (i >= len) return W4_ERR_PARSE;
        i++;
    } else {
        while (i < len && s[i] != ':' && s[i] != '/' && s[i] != '?' && s[i] != '#') {
            char c = s[i];
            if (c == '@' || (uint8_t) c <= 0x20 || c == 0x7f || c == '\\') return W4_ERR_PARSE;
            i++;
        }
    }
    if (i == hs) return W4_ERR_PARSE;
    u->host = s + hs;
    u->host_len = i - hs;
    if (w4_casecmp_eq(u->scheme, u->scheme_len, "https", 5) ||
        w4_casecmp_eq(u->scheme, u->scheme_len, "wss", 3))
        u->port = 443;
    else if (w4_casecmp_eq(u->scheme, u->scheme_len, "http", 4) ||
             w4_casecmp_eq(u->scheme, u->scheme_len, "ws", 2))
        u->port = 80;
    if (i < len && s[i] == ':') {
        i++;
        uint32_t ps = i, pv = 0;
        while (i < len && w4_is_digit(s[i])) {
            pv = pv * 10u + (uint32_t) (s[i] - '0');
            if (pv > 65535u || i - ps >= 5) return W4_ERR_PARSE;
            i++;
        }
        if (i == ps || pv == 0) return W4_ERR_PARSE;
        u->port = (uint16_t) pv;
    }
    uint32_t ps = i;
    while (i < len && s[i] != '?' && s[i] != '#') {
        if ((uint8_t) s[i] <= 0x20 || s[i] == 0x7f) return W4_ERR_PARSE;
        i++;
    }
    if (i == ps) {
        u->path = "/";
        u->path_len = 1;
    } else {
        if (s[ps] != '/') return W4_ERR_PARSE;
        u->path = s + ps;
        u->path_len = i - ps;
    }
    if (i < len && s[i] == '?') {
        uint32_t qs = ++i;
        while (i < len && s[i] != '#') {
            if ((uint8_t) s[i] <= 0x20 || s[i] == 0x7f) return W4_ERR_PARSE;
            i++;
        }
        u->query = s + qs;
        u->query_len = i - qs;
    }
    return W4_OK;
}

/* ===== Percent-encoding ===== */
static bool unreserved(char c)
{
    return w4_is_alpha(c) || w4_is_digit(c) || c == '-' || c == '.' || c == '_' || c == '~';
}

void w4_w_pct(w4_w *w, const char *s)
{
    static const char hx[] = "0123456789ABCDEF";
    for (; s && *s; s++) {
        uint8_t c = (uint8_t) *s;
        if (unreserved((char) c)) {
            w4_w_byte(w, c);
        } else {
            w4_w_byte(w, '%');
            w4_w_byte(w, (uint8_t) hx[c >> 4]);
            w4_w_byte(w, (uint8_t) hx[c & 15]);
        }
    }
}

void w4_w_form(w4_w *w, bool *first, const char *k, const char *v)
{
    if (!*first) w4_w_byte(w, '&');
    *first = false;
    w4_w_pct(w, k);
    w4_w_byte(w, '=');
    w4_w_pct(w, v);
}

/* Decode q[s..e) into w. Returns false on a bad escape. */
static bool pct_decode(const char *q, uint32_t s, uint32_t e, w4_w *w)
{
    for (uint32_t i = s; i < e; i++) {
        char c = q[i];
        if (c == '+') {
            w4_w_byte(w, ' ');
        } else if (c == '%') {
            if (i + 2 >= e) return false;
            int hi = w4_hexval(q[i + 1]), lo = w4_hexval(q[i + 2]);
            if (hi < 0 || lo < 0) return false;
            w4_w_byte(w, (uint8_t) ((hi << 4) | lo));
            i += 2;
        } else {
            w4_w_byte(w, (uint8_t) c);
        }
    }
    return true;
}

int32_t w4_form_get(const char *q, uint32_t qlen, const char *key, char *out, uint32_t cap)
{
    int32_t result = W4_ERR_NOTFOUND;
    uint32_t i = 0;
    while (i <= qlen) {
        uint32_t ps = i;
        while (i < qlen && q[i] != '&') i++;
        uint32_t pe = i;
        uint32_t eq = ps;
        while (eq < pe && q[eq] != '=') eq++;
        char kb[128];
        w4_w kw;
        w4_w_init(&kw, kb, sizeof kb);
        if (pe > ps) {
            if (!pct_decode(q, ps, eq, &kw)) return W4_ERR_PARSE;
            if (w4_w_cstr(&kw) >= 0 && w4_streq(kb, key)) {
                if (result != W4_ERR_NOTFOUND) return W4_ERR_PARSE; /* duplicated */
                w4_w ow;
                w4_w_init(&ow, out, cap);
                if (!pct_decode(q, eq < pe ? eq + 1 : pe, pe, &ow)) return W4_ERR_PARSE;
                result = w4_w_cstr(&ow);
                if (result < 0) return W4_ERR_SPACE;
            }
        }
        i = pe + 1;
    }
    return result;
}
