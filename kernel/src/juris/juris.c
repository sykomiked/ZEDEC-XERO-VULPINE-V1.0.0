/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* juris.c — the jurisdiction plug-in ABI (juris.h J1-J7). */
#include "juris.h"
#include "../robin_debanks/sha256.h"

static lpx_t tag(uint8_t base)
{
    return lpx_make(base, LPX_P_NEUTRAL, LPX_PH_POTENTIAL);
}

static void zero(void *p, size_t n)
{
    uint8_t *b = p;
    while (n--) *b++ = 0;
}

static bool upper2(const char *c)
{
    return c[0] >= 'A' && c[0] <= 'Z' && c[1] >= 'A' && c[1] <= 'Z' && c[2] == '\0';
}

static bool same_country(const char *a, const char *b)
{
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2];
}

/* ---- J1: settings ---- */
void juris_settings_default(juris_settings_t *s)
{
    if (!s) return;
    s->market = JURIS_MARKET_CLOSED;
    s->standard = ACCT_STD_NONE;
    s->country[0] = s->country[1] = s->country[2] = '\0';
    s->in_effect = false;
}

typedef struct {
    const char *p;
    size_t i, n;
} jr_t;

static void ws(jr_t *r)
{
    while (r->i < r->n &&
           (r->p[r->i] == ' ' || r->p[r->i] == '\t' || r->p[r->i] == '\n' || r->p[r->i] == '\r'))
        r->i++;
}

static bool lit(jr_t *r, const char *s)
{
    size_t k = 0;
    while (s[k]) {
        if (r->i + k >= r->n || r->p[r->i + k] != s[k]) return false;
        k++;
    }
    r->i += k;
    return true;
}

static int hexv(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* A JSON string into out (cap bytes, NUL-terminated); *trunc set when it
 * did not fit. out may be NULL (skip). */
static bool jstr(jr_t *r, char *out, size_t cap, bool *trunc)
{
    size_t o = 0;
    if (trunc) *trunc = false;
    if (r->i >= r->n || r->p[r->i] != '"') return false;
    r->i++;
    for (;;) {
        if (r->i >= r->n) return false;
        unsigned char c = (unsigned char) r->p[r->i++];
        if (c == '"') break;
        if (c < 0x20) return false;
        if (c == '\\') {
            if (r->i >= r->n) return false;
            char e = r->p[r->i++];
            switch (e) {
            case '"': c = '"'; break;
            case '\\': c = '\\'; break;
            case '/': c = '/'; break;
            case 'b': c = '\b'; break;
            case 'f': c = '\f'; break;
            case 'n': c = '\n'; break;
            case 'r': c = '\r'; break;
            case 't': c = '\t'; break;
            case 'u': {
                int v = 0;
                for (int k = 0; k < 4; k++) {
                    if (r->i >= r->n) return false;
                    int h = hexv(r->p[r->i++]);
                    if (h < 0) return false;
                    v = v * 16 + h;
                }
                c = v < 0x80 ? (unsigned char) v : '?';
                break;
            }
            default: return false;
            }
        }
        if (out) {
            if (o + 1 < cap) out[o++] = (char) c;
            else if (trunc) *trunc = true;
        }
    }
    if (out && cap) out[o] = '\0';
    return true;
}

static bool skip_value(jr_t *r, int depth);

static bool skip_container(jr_t *r, int depth, char open, char close)
{
    if (depth > 16 || r->i >= r->n || r->p[r->i] != open) return false;
    r->i++;
    ws(r);
    if (r->i < r->n && r->p[r->i] == close) {
        r->i++;
        return true;
    }
    for (;;) {
        ws(r);
        if (open == '{') {
            if (!jstr(r, NULL, 0, NULL)) return false;
            ws(r);
            if (r->i >= r->n || r->p[r->i] != ':') return false;
            r->i++;
            ws(r);
        }
        if (!skip_value(r, depth + 1)) return false;
        ws(r);
        if (r->i >= r->n) return false;
        if (r->p[r->i] == ',') {
            r->i++;
            continue;
        }
        if (r->p[r->i] == close) {
            r->i++;
            return true;
        }
        return false;
    }
}

static bool skip_value(jr_t *r, int depth)
{
    if (r->i >= r->n) return false;
    char c = r->p[r->i];
    if (c == '"') return jstr(r, NULL, 0, NULL);
    if (c == '{') return skip_container(r, depth, '{', '}');
    if (c == '[') return skip_container(r, depth, '[', ']');
    if (lit(r, "true") || lit(r, "false") || lit(r, "null")) return true;
    if (c == '-' || (c >= '0' && c <= '9')) {
        size_t s = r->i;
        if (c == '-') r->i++;
        while (r->i < r->n) {
            char d = r->p[r->i];
            if ((d >= '0' && d <= '9') || d == '.' || d == 'e' || d == 'E' || d == '+' || d == '-')
                r->i++;
            else
                break;
        }
        return r->i > s + (c == '-' ? 1u : 0u);
    }
    return false;
}

/* an array of strings (the "standards" list) */
static bool str_array(jr_t *r)
{
    if (r->i >= r->n || r->p[r->i] != '[') return false;
    r->i++;
    ws(r);
    if (r->i < r->n && r->p[r->i] == ']') {
        r->i++;
        return true;
    }
    for (;;) {
        ws(r);
        if (!jstr(r, NULL, 0, NULL)) return false;
        ws(r);
        if (r->i >= r->n) return false;
        if (r->p[r->i] == ',') {
            r->i++;
            continue;
        }
        if (r->p[r->i] == ']') {
            r->i++;
            return true;
        }
        return false;
    }
}

static bool keyeq(const char *k, const char *s)
{
    while (*k && *k == *s) k++, s++;
    return *k == *s;
}

juris_status_t juris_settings_parse(const char *json, size_t len, juris_settings_t *out)
{
    if (!out) return JURIS_E_ARG;
    juris_settings_default(out);
    if (!json) return JURIS_E_ARG;
    juris_settings_t s;
    juris_settings_default(&s);
    jr_t r = {json, 0, len};
    unsigned seen = 0;
    ws(&r);
    if (r.i >= r.n || r.p[r.i] != '{') return JURIS_E_PARSE;
    r.i++;
    ws(&r);
    if (r.i < r.n && r.p[r.i] == '}') {
        r.i++;
        goto tail;
    }
    for (;;) {
        char key[24], val[24];
        bool trunc;
        ws(&r);
        if (!jstr(&r, key, sizeof key, &trunc)) return JURIS_E_PARSE;
        if (trunc) key[0] = '\0'; /* a long key is not one of ours: skip it */
        ws(&r);
        if (r.i >= r.n || r.p[r.i] != ':') return JURIS_E_PARSE;
        r.i++;
        ws(&r);
        unsigned bit = keyeq(key, "market")      ? 1u
                       : keyeq(key, "standard")  ? 2u
                       : keyeq(key, "country")   ? 4u
                       : keyeq(key, "in_effect") ? 8u
                       : keyeq(key, "standards") ? 16u
                                                 : 0u;
        if (bit & seen) return JURIS_E_PARSE; /* repeated key */
        seen |= bit;
        switch (bit) {
        case 1u:
            if (!jstr(&r, val, sizeof val, &trunc) || trunc) return JURIS_E_PARSE;
            if (keyeq(val, "closed")) s.market = JURIS_MARKET_CLOSED;
            else if (keyeq(val, "open")) s.market = JURIS_MARKET_OPEN;
            else return JURIS_E_PARSE;
            break;
        case 2u: {
            if (!jstr(&r, val, sizeof val, &trunc) || trunc) return JURIS_E_PARSE;
            acct_std_t st = acct_std_from_name(val);
            if (st >= ACCT_STD_COUNT) return JURIS_E_PARSE;
            s.standard = (uint8_t) st;
            break;
        }
        case 4u:
            if (!jstr(&r, val, sizeof val, &trunc) || trunc) return JURIS_E_PARSE;
            if (val[0] == '\0') {
                s.country[0] = '\0';
            } else if (upper2(val)) {
                s.country[0] = val[0];
                s.country[1] = val[1];
                s.country[2] = '\0';
            } else {
                return JURIS_E_PARSE;
            }
            break;
        case 8u:
            if (lit(&r, "true")) s.in_effect = true;
            else if (lit(&r, "false")) s.in_effect = false;
            else return JURIS_E_PARSE;
            break;
        case 16u:
            if (!str_array(&r)) return JURIS_E_PARSE;
            break;
        default:
            if (!skip_value(&r, 0)) return JURIS_E_PARSE;
        }
        ws(&r);
        if (r.i >= r.n) return JURIS_E_PARSE;
        if (r.p[r.i] == ',') {
            r.i++;
            continue;
        }
        if (r.p[r.i] == '}') {
            r.i++;
            break;
        }
        return JURIS_E_PARSE;
    }
tail:
    ws(&r);
    if (r.i != r.n) return JURIS_E_PARSE;
    *out = s;
    return JURIS_OK;
}

/* ---- J3: manifest ---- */
static void put_le(uint8_t *o, uint64_t v, unsigned n)
{
    for (unsigned i = 0; i < n; i++) o[i] = (uint8_t) (v >> (8 * i));
}

void juris_manifest_msg(const juris_manifest_t *m, uint8_t out[JURIS_MANIFEST_MSG])
{
    static const char dom[19] = {'z', 'x', 'v', '/', 'j', 'u', 'r', 'i', 's', '-',
                                 'p', 'l', 'u', 'g', 'i', 'n', '/', 'v', '1'};
    size_t o = 0;
    for (unsigned i = 0; i < 19; i++) out[o++] = (uint8_t) dom[i];
    for (unsigned i = 0; i < JURIS_ID_LEN; i++) out[o++] = (uint8_t) m->id[i];
    out[o++] = (uint8_t) m->country[0];
    out[o++] = (uint8_t) m->country[1];
    put_le(out + o, m->hooks, 4);
    o += 4;
    put_le(out + o, m->version, 8);
    o += 8;
    for (unsigned i = 0; i < 32; i++) out[o++] = m->image_hash[i];
}

static bool id_eq(const char *a, const char *b)
{
    for (unsigned i = 0; i < JURIS_ID_LEN; i++)
        if (a[i] != b[i]) return false;
    return true;
}

/* ---- the host ---- */
void juris_host_init(juris_host_t *h, const juris_settings_t *s, juris_verify_fn verify, void *vctx,
                     lpx_qrec_t *qstore, uint32_t qcap)
{
    if (!h) return;
    zero(h, sizeof *h);
    juris_settings_default(&h->set);
    if (s) h->set = *s;
    h->verify = verify;
    h->vctx = vctx;
    lpx_qlog_init(&h->qlog, qstore, qcap);
    h->last_tag = tag(LPX_B_NEITHER);
}

static bool settings_ok(const juris_settings_t *s)
{
    if (s->market > JURIS_MARKET_OPEN || s->standard >= ACCT_STD_COUNT) return false;
    return s->country[0] == '\0' ? s->country[1] == '\0' : upper2(s->country);
}

juris_status_t juris_host_apply_settings(juris_host_t *h, const juris_settings_t *s)
{
    if (!h || !s || !settings_ok(s)) return JURIS_E_ARG;
    bool same = same_country(h->set.country, s->country);
    h->set = *s;
    if (!same) {
        uint32_t k = 0;
        for (uint32_t i = 0; i < h->n; i++)
            if (h->set.country[0] && same_country(h->plug[i]->country, h->set.country))
                h->plug[k++] = h->plug[i];
        for (uint32_t i = k; i < JURIS_MAX_PLUGINS; i++) h->plug[i] = NULL;
        h->n = k;
    }
    return JURIS_OK;
}

juris_status_t juris_host_register(juris_host_t *h, const juris_plugin_t *p,
                                   const juris_manifest_t *m, const uint8_t *image,
                                   size_t image_len)
{
    if (!h || !p || !m || (!image && image_len)) return JURIS_E_ARG;
    if (h->halted) return JURIS_E_HALTED;
    /* J2 */
    if ((p->abi >> 16) != JURIS_PLUGIN_ABI_MAJOR || p->size < sizeof(juris_plugin_t))
        return JURIS_E_ABI;
    if ((p->hooks & ~JURIS_HOOK_ALL) != 0) return JURIS_E_ABI;
    if (((p->hooks & JURIS_HOOK_TRANSFER) && !p->transfer) ||
        ((p->hooks & JURIS_HOOK_MARKET) && !p->market) ||
        ((p->hooks & JURIS_HOOK_TAX) && !p->tax_rate) ||
        ((p->hooks & JURIS_HOOK_STANDARD) && !p->standard) ||
        ((p->hooks & JURIS_HOOK_COMMIT) && !p->commit))
        return JURIS_E_ABI;
    if (!upper2(p->country)) return JURIS_E_ARG;
    /* J3 */
    if (!h->verify || !m->sig) return JURIS_E_SIG;
    if (!id_eq(m->id, p->id) || !same_country(m->country, p->country)) return JURIS_E_SIG;
    uint8_t msg[JURIS_MANIFEST_MSG];
    juris_manifest_msg(m, msg);
    if (!h->verify(h->vctx, msg, JURIS_MANIFEST_MSG, m->sig, m->sig_len)) return JURIS_E_SIG;
    uint8_t dig[32];
    sha256(image, image_len, dig);
    uint8_t diff = 0;
    for (unsigned i = 0; i < 32; i++) diff |= (uint8_t) (dig[i] ^ m->image_hash[i]);
    if (diff) return JURIS_E_HASH;
    if (m->hooks != p->hooks) return JURIS_E_HOOKS;
    /* J4 */
    if (!h->set.country[0] || !same_country(p->country, h->set.country)) return JURIS_E_COUNTRY;
    int vi = -1;
    for (uint32_t i = 0; i < h->n_ver; i++)
        if (id_eq(h->ver_id[i], p->id)) vi = (int) i;
    if (vi >= 0 && m->version <= h->ver[vi]) return JURIS_E_VERSION;
    /* the same id replaces its older version in place */
    int slot = -1;
    for (uint32_t i = 0; i < h->n; i++)
        if (id_eq(h->plug[i]->id, p->id)) slot = (int) i;
    if (slot < 0 && h->n >= JURIS_MAX_PLUGINS) return JURIS_E_FULL;
    if (vi < 0 && h->n_ver >= JURIS_MAX_IDS) return JURIS_E_FULL;
    if (vi < 0) {
        vi = (int) h->n_ver++;
        for (unsigned i = 0; i < JURIS_ID_LEN; i++) h->ver_id[vi][i] = p->id[i];
    }
    h->ver[vi] = m->version;
    if (slot < 0) h->plug[h->n++] = p;
    else h->plug[slot] = p;
    return JURIS_OK;
}

static void transfer_digest(const juris_transfer_t *t, uint8_t out[32])
{
    uint8_t b[3 + 3 + 4 + 8 + 4 + 4 + 8];
    size_t o = 0;
    for (unsigned i = 0; i < 3; i++) b[o++] = (uint8_t) t->from_country[i];
    for (unsigned i = 0; i < 3; i++) b[o++] = (uint8_t) t->to_country[i];
    for (unsigned i = 0; i < 4; i++) b[o++] = (uint8_t) t->ccy[i];
    put_le(b + o, t->amount, 8);
    o += 8;
    put_le(b + o, t->sender, 4);
    o += 4;
    put_le(b + o, t->receiver, 4);
    o += 4;
    put_le(b + o, t->epoch, 8);
    o += 8;
    sha256(b, o, out);
}

/* J5: quarantine a forbidden verdict; a full log halts the host */
static lpx_t settle(juris_host_t *h, lpx_t t, uint32_t who, const uint8_t subject[32])
{
    h->last_tag = t;
    if (lpx_forbidden(t)) {
        if (!lpx_qlog_append(&h->qlog, t, 1, who, subject)) h->halted = true;
    }
    return t;
}

lpx_t juris_check_transfer(juris_host_t *h, const juris_transfer_t *t)
{
    if (!h || !t) return tag(LPX_B_NEITHER);
    if (h->halted) return lpx_forbid(tag(LPX_B_FALSE));
    if (!h->set.country[0]) return h->last_tag = tag(LPX_B_NEITHER);
    lpx_t acc = tag(LPX_B_TRUE);
    bool asked = false;
    for (uint32_t i = 0; i < h->n; i++) {
        const juris_plugin_t *p = h->plug[i];
        if (!(p->hooks & JURIS_HOOK_TRANSFER)) continue;
        acc = lpx_and(acc, p->transfer(p->ctx, t) & (LPX_STATES - 1u));
        asked = true;
    }
    if (!asked) acc = tag(LPX_B_NEITHER);
    uint8_t d[32];
    transfer_digest(t, d);
    return settle(h, acc, t->sender, d);
}

void juris_commit_transfer(juris_host_t *h, const juris_transfer_t *t)
{
    if (!h || !t || h->halted) return;
    for (uint32_t i = 0; i < h->n; i++) {
        const juris_plugin_t *p = h->plug[i];
        if (p->hooks & JURIS_HOOK_COMMIT) p->commit(p->ctx, t);
    }
}

lpx_t juris_check_market(juris_host_t *h, juris_market_t m)
{
    if (!h || (unsigned) m > JURIS_MARKET_OPEN) return tag(LPX_B_NEITHER);
    if (h->halted) return lpx_forbid(tag(LPX_B_FALSE));
    if (!h->set.country[0]) return h->last_tag = tag(LPX_B_NEITHER);
    if (m == JURIS_MARKET_OPEN && h->set.market != JURIS_MARKET_OPEN)
        return h->last_tag = tag(LPX_B_FALSE);
    lpx_t acc = tag(LPX_B_TRUE);
    bool asked = false;
    for (uint32_t i = 0; i < h->n; i++) {
        const juris_plugin_t *p = h->plug[i];
        if (!(p->hooks & JURIS_HOOK_MARKET)) continue;
        acc = lpx_and(acc, p->market(p->ctx, m) & (LPX_STATES - 1u));
        asked = true;
    }
    /* closed clearing is the default: it needs no plug-in to say yes */
    if (!asked && m == JURIS_MARKET_OPEN) acc = tag(LPX_B_NEITHER);
    uint8_t d[32] = {(uint8_t) m};
    return settle(h, acc, 0, d);
}

juris_status_t juris_host_tax_rate(juris_host_t *h, juris_tax_kind_t k, uint64_t *num,
                                   uint64_t *den)
{
    if (num) *num = 0;
    if (den) *den = 0;
    if (!h || !num || !den || (unsigned) k >= JURIS_TAX_COUNT) return JURIS_E_ARG;
    if (h->halted) return JURIS_E_HALTED;
    bool have = false;
    uint64_t n0 = 0, d0 = 1;
    for (uint32_t i = 0; i < h->n; i++) {
        const juris_plugin_t *p = h->plug[i];
        if (!(p->hooks & JURIS_HOOK_TAX)) continue;
        uint64_t n = 0, d = 0;
        lpx_t t = p->tax_rate(p->ctx, k, &n, &d) & (LPX_STATES - 1u);
        if (lpx_action(t) != LPX_ACT_PROCEED) {
            if (lpx_base(t) == LPX_B_NEITHER) continue; /* this one does not know */
            h->last_tag = t;
            return lpx_base(t) == LPX_B_BOTH ? JURIS_E_CONFLICT : JURIS_E_NO_RULE;
        }
        if (d == 0 || n > d) return JURIS_E_NO_RULE; /* a rate outside [0, 1] */
        if (!have) {
            n0 = n;
            d0 = d;
            have = true;
        } else {
            __extension__ unsigned __int128 a = (unsigned __int128) n * d0,
                                            b = (unsigned __int128) n0 * d;
            if (a != b) {
                h->last_tag = tag(LPX_B_BOTH);
                return JURIS_E_CONFLICT;
            }
        }
    }
    if (!have) return JURIS_E_NO_RULE;
    *num = n0;
    *den = d0;
    return JURIS_OK;
}

acct_std_t juris_host_standard(juris_host_t *h)
{
    if (!h || h->halted || !h->set.country[0]) return ACCT_STD_NONE;
    acct_std_t s = (acct_std_t) h->set.standard;
    if (!acct_std_supported(s)) return ACCT_STD_NONE;
    for (uint32_t i = 0; i < h->n; i++) {
        const juris_plugin_t *p = h->plug[i];
        if (!(p->hooks & JURIS_HOOK_STANDARD)) continue;
        if (lpx_action(p->standard(p->ctx, s) & (LPX_STATES - 1u)) != LPX_ACT_PROCEED)
            return ACCT_STD_NONE;
    }
    return s;
}
