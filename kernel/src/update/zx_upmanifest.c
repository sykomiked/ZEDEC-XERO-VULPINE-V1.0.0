/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* zx_upmanifest.c — the strict zxv-update.manifest parser and its signature
 * check. The format is documented in zx_upcheck.h and written by
 * build_system/zxv_publish_update.py; anything that tool would not write is
 * refused.
 */
#include "zx_upcheck.h"
#include "zx_upcheck_util.h"

static const char *const KINDS[] = {"kernel", "app", "module", "firmware", "data", "doc"};
static const char *const ARCHES[] = {"any",     "x86_64", "aarch64", "riscv64",
                                     "riscv32", "arm32",  "x86"};

/* ---- versions ------------------------------------------------------------------ */

/* 0..999, no leading zeros. */
static int num999(const char *s, uint32_t len, uint32_t *v)
{
    uint32_t x = 0;
    if (len == 0 || len > 3 || (len > 1 && s[0] == '0')) return ZXU_ERR_MALFORMED;
    for (uint32_t i = 0; i < len; i++) {
        if (s[i] < '0' || s[i] > '9') return ZXU_ERR_MALFORMED;
        x = x * 10u + (uint32_t) (s[i] - '0');
    }
    *v = x;
    return ZXU_OK;
}

int zxu_version_parse(const char *s, uint32_t len, uint32_t *v)
{
    uint32_t part[3], k = 0, start = 0;
    if (!s || !v) return ZXU_ERR_ARG;
    for (uint32_t i = 0; i <= len; i++) {
        if (i == len || s[i] == '.') {
            if (k == 3 || num999(s + start, i - start, &part[k])) return ZXU_ERR_MALFORMED;
            k++;
            start = i + 1u;
        }
    }
    if (k != 3) return ZXU_ERR_MALFORMED;
    *v = ZXU_VERSION(part[0], part[1], part[2]);
    return ZXU_OK;
}

/* x < 1000: decimal digits without division. */
static uint32_t put999(uint32_t x, char *o)
{
    uint32_t n = 0, h = 0, t = 0;
    while (x >= 100u) {
        x -= 100u;
        h++;
    }
    while (x >= 10u) {
        x -= 10u;
        t++;
    }
    if (h) o[n++] = (char) ('0' + h);
    if (h || t) o[n++] = (char) ('0' + t);
    o[n++] = (char) ('0' + x);
    return n;
}

int zxu_version_format(uint32_t v, char *out, uint32_t cap)
{
    uint32_t a = 0, b = 0, n = 0;
    char tmp[16];
    if (!out || v > ZXU_VERSION(999, 999, 999)) return ZXU_ERR_ARG;
    while (v >= 1000000u) {
        v -= 1000000u;
        a++;
    }
    while (v >= 1000u) {
        v -= 1000u;
        b++;
    }
    n += put999(a, tmp + n);
    tmp[n++] = '.';
    n += put999(b, tmp + n);
    tmp[n++] = '.';
    n += put999(v, tmp + n);
    if (n + 1u > cap) return ZXU_ERR_SPACE;
    zxu__cpy(out, tmp, n);
    out[n] = 0;
    return (int) n;
}

/* ---- tokens ---------------------------------------------------------------------- */

typedef struct {
    const char *p;
    uint32_t len, pos;
} cur_t;

/* The next line (without its '\n'); the line must end in '\n'. */
static int line(cur_t *c, const char **l, uint32_t *ll)
{
    uint32_t s = c->pos;
    while (c->pos < c->len && c->p[c->pos] != '\n') c->pos++;
    if (c->pos >= c->len) return ZXU_ERR_MALFORMED; /* missing final LF */
    *l = c->p + s;
    *ll = c->pos - s;
    c->pos++;
    return ZXU_OK;
}

/* Split on single spaces into at most `max` fields. */
static int fields(const char *l, uint32_t ll, const char **f, uint32_t *fl, uint32_t max,
                  uint32_t *n)
{
    uint32_t k = 0, s = 0;
    for (uint32_t i = 0; i <= ll; i++) {
        if (i == ll || l[i] == ' ') {
            if (i == s || k == max) return ZXU_ERR_MALFORMED; /* empty field, too many */
            f[k] = l + s;
            fl[k] = i - s;
            k++;
            s = i + 1u;
        }
    }
    *n = k;
    return ZXU_OK;
}

static bool tok_is(const char *t, uint32_t tl, const char *s)
{
    uint32_t n = zxu__strlen(s, 64);
    return tl == n && zxu__eq(t, s, n);
}

static int u64dec(const char *s, uint32_t len, uint64_t max, uint64_t *v)
{
    uint64_t x = 0;
    if (len == 0 || len > 20 || (len > 1 && s[0] == '0')) return ZXU_ERR_MALFORMED;
    for (uint32_t i = 0; i < len; i++) {
        uint64_t d;
        if (s[i] < '0' || s[i] > '9') return ZXU_ERR_MALFORMED;
        d = (uint64_t) (s[i] - '0');
        if (x > 1844674407370955161ull) return ZXU_ERR_MALFORMED; /* x*10 would wrap */
        x *= 10u;
        if (x > 0xFFFFFFFFFFFFFFFFull - d) return ZXU_ERR_MALFORMED;
        x += d;
        if (x > max) return ZXU_ERR_MALFORMED;
    }
    *v = x;
    return ZXU_OK;
}

static int hexnib(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static bool path_ok(const char *p, uint32_t len)
{
    uint32_t s = 0;
    if (len == 0 || len > ZXU_PATH_MAX) return false;
    if (tok_is(p, len, ZXU_MANIFEST_NAME) || tok_is(p, len, ZXU_SIG_NAME)) return false;
    for (uint32_t i = 0; i <= len; i++) {
        if (i == len || p[i] == '/') {
            uint32_t cl = i - s;
            if (cl == 0) return false;
            if (cl == 1 && p[s] == '.') return false;
            if (cl == 2 && p[s] == '.' && p[s + 1] == '.') return false;
            s = i + 1u;
            continue;
        }
        {
            char c = p[i];
            bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                      c == '.' || c == '_' || c == '+' || c == '-';
            if (!ok) return false;
        }
    }
    return true;
}

static int path_cmp(const zxu_entry_t *a, const char *p, uint32_t pl)
{
    uint32_t n = a->path_len < pl ? a->path_len : pl;
    for (uint32_t i = 0; i < n; i++)
        if ((uint8_t) a->path[i] != (uint8_t) p[i])
            return (uint8_t) a->path[i] < (uint8_t) p[i] ? -1 : 1;
    return a->path_len == pl ? 0 : (a->path_len < pl ? -1 : 1);
}

/* ---- the parser -------------------------------------------------------------------- */

int zxu_manifest_parse(const uint8_t *text, uint32_t len, zxu_manifest_t *m)
{
    cur_t c;
    const char *l, *f[8];
    uint32_t ll, fl[8], nf;
    uint64_t v64;
    if (!text || !m) return ZXU_ERR_ARG;
    zxu__set(m, 0, (uint32_t) sizeof(*m));
    if (len == 0 || len > ZXU_MANIFEST_MAX) return ZXU_ERR_MALFORMED;
    for (uint32_t i = 0; i < len; i++) /* printable ASCII and LF only */
        if (text[i] != '\n' && (text[i] < 0x20 || text[i] > 0x7e)) return ZXU_ERR_MALFORMED;
    c.p = (const char *) text;
    c.len = len;
    c.pos = 0;

    if (line(&c, &l, &ll) || !tok_is(l, ll, "zxv-update-manifest 1")) return ZXU_ERR_MALFORMED;
    if (line(&c, &l, &ll) || fields(l, ll, f, fl, 2, &nf) || nf != 2 ||
        !tok_is(f[0], fl[0], "release") || zxu_version_parse(f[1], fl[1], &m->release))
        return ZXU_ERR_MALFORMED;
    if (line(&c, &l, &ll) || fields(l, ll, f, fl, 2, &nf) || nf != 2 ||
        !tok_is(f[0], fl[0], "min-version") || zxu_version_parse(f[1], fl[1], &m->min_version))
        return ZXU_ERR_MALFORMED;
    if (m->min_version > m->release) return ZXU_ERR_MALFORMED;
    if (line(&c, &l, &ll) || fields(l, ll, f, fl, 2, &nf) || nf != 2 ||
        !tok_is(f[0], fl[0], "issued") || u64dec(f[1], fl[1], 0x7fffffffffffffffull, &m->issued))
        return ZXU_ERR_MALFORMED;

    for (;;) {
        zxu_entry_t *e;
        uint32_t k;
        if (line(&c, &l, &ll)) return ZXU_ERR_MALFORMED;
        if (tok_is(l, ll, "end")) break;
        if (fields(l, ll, f, fl, 8, &nf) || nf != 8 || !tok_is(f[0], fl[0], "entry"))
            return ZXU_ERR_MALFORMED;
        if (m->n == ZXU_MAX_ENTRIES) return ZXU_ERR_MALFORMED;
        e = &m->e[m->n];
        for (k = 0; k < sizeof KINDS / sizeof KINDS[0]; k++)
            if (tok_is(f[1], fl[1], KINDS[k])) break;
        if (k == sizeof KINDS / sizeof KINDS[0]) return ZXU_ERR_MALFORMED;
        e->kind = (uint8_t) k;
        for (k = 0; k < sizeof ARCHES / sizeof ARCHES[0]; k++)
            if (tok_is(f[2], fl[2], ARCHES[k])) break;
        if (k == sizeof ARCHES / sizeof ARCHES[0]) return ZXU_ERR_MALFORMED;
        e->arch = (uint8_t) k; /* ARCHES[] is in ZXU_ARCH_* order */
        if (zxu_version_parse(f[3], fl[3], &e->version) || e->version > m->release)
            return ZXU_ERR_MALFORMED;
        if (u64dec(f[4], fl[4], 0xFFFFFFFFull, &v64)) return ZXU_ERR_MALFORMED;
        e->size = (uint32_t) v64;
        if (fl[5] < 2 || f[5][0] != 'b' || ipfsn_cid_parse(f[5], fl[5], &e->cid) ||
            e->cid.version != 1 || e->cid.mh_code != IPFSN_MH_SHA2_256 ||
            (e->cid.codec != IPFSN_MC_RAW && e->cid.codec != IPFSN_MC_DAG_PB))
            return ZXU_ERR_MALFORMED;
        if (fl[6] != 64) return ZXU_ERR_MALFORMED;
        for (uint32_t i = 0; i < 32; i++) {
            int hi = hexnib(f[6][2 * i]), lo = hexnib(f[6][2 * i + 1]);
            if (hi < 0 || lo < 0) return ZXU_ERR_MALFORMED;
            e->sha256[i] = (uint8_t) ((hi << 4) | lo);
        }
        if (!path_ok(f[7], fl[7])) return ZXU_ERR_MALFORMED;
        if (m->n && path_cmp(&m->e[m->n - 1], f[7], fl[7]) >= 0)
            return ZXU_ERR_MALFORMED; /* unsorted or duplicate */
        zxu__cpy(e->path, f[7], fl[7]);
        e->path[fl[7]] = 0;
        e->path_len = fl[7];
        m->n++;
    }
    if (c.pos != len) return ZXU_ERR_MALFORMED; /* nothing after "end\n" */
    if (m->n == 0) return ZXU_ERR_MALFORMED;
    return ZXU_OK;
}

int zxu_manifest_verify(zxu_sigverify_fn fn, void *ctx, const uint8_t (*keys)[ZXU_PK_BYTES],
                        uint32_t nkeys, const uint8_t *text, uint32_t len,
                        const uint8_t sig[ZXU_SIG_BYTES], uint32_t *which)
{
    const uint8_t *dom = (const uint8_t *) ZXU_SIG_CONTEXT;
    uint32_t dl = zxu__strlen(ZXU_SIG_CONTEXT, 64);
    if (!fn || !keys || !text || !sig) return ZXU_ERR_ARG;
    for (uint32_t i = 0; i < nkeys && i < ZXU_MAX_KEYS; i++) {
        if (fn(ctx, keys[i], text, len, dom, dl, sig)) {
            if (which) *which = i;
            return ZXU_OK;
        }
    }
    return ZXU_ERR_SIG;
}
