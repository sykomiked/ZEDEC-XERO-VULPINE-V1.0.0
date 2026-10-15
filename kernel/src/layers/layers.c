/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* layers.c — layer manifests read in their one layout, and the lineage
 * check (layers.h). */
#include "layers.h"
#include "../robin_debanks/sha256.h"

static const char *const NAMES[5] = {"HARDWARE_ENVELOPE", "CORE_BOOT_ENGINE", "SWARM", "ALEXANDRIA",
                                     "COMMUNITY_OVERLAY"};
static const char B32[] = "abcdefghijklmnopqrstuvwxyz234567";

static uint32_t slen(const char *s)
{
    uint32_t n = 0;
    while (s[n]) n++;
    return n;
}

/* Does line [p, p+n) equal s exactly? */
static int is(const char *p, uint32_t n, const char *s)
{
    uint32_t k = slen(s);
    if (k != n) return 0;
    for (uint32_t i = 0; i < n; i++)
        if (p[i] != s[i]) return 0;
    return 1;
}

/* Does [p, p+n) start with s? */
static int starts(const char *p, uint32_t n, const char *s)
{
    uint32_t k = slen(s);
    if (k > n) return 0;
    for (uint32_t i = 0; i < k; i++)
        if (p[i] != s[i]) return 0;
    return 1;
}

void zxv_layer_cid_text(const uint8_t cid[ZXV_LAYER_CID_BYTES], char out[61])
{
    uint32_t o = 0, acc = 0, bits = 0;
    out[o++] = 'b';
    for (uint32_t i = 0; i < ZXV_LAYER_CID_BYTES; i++) {
        acc = (acc << 8) | cid[i];
        bits += 8;
        while (bits >= 5) {
            out[o++] = B32[(acc >> (bits - 5)) & 31u];
            bits -= 5;
        }
    }
    if (bits) out[o++] = B32[(acc << (5 - bits)) & 31u];
    out[o] = 0;
}

/* The 59 characters at p ("bafkrei...") -> a raw sha2-256 CIDv1, or 0. */
static int cid_parse(const char *p, uint8_t cid[ZXV_LAYER_CID_BYTES])
{
    uint32_t acc = 0, bits = 0, o = 0;
    if (p[0] != 'b') return 0;
    for (uint32_t i = 1; i < 59; i++) {
        uint32_t v = 32;
        for (uint32_t k = 0; k < 32; k++)
            if (B32[k] == p[i]) v = k;
        if (v == 32) return 0;
        acc = (acc << 5) | v;
        bits += 5;
        if (bits >= 8) {
            if (o == ZXV_LAYER_CID_BYTES) return 0;
            cid[o++] = (uint8_t) (acc >> (bits - 8));
            bits -= 8;
        }
    }
    if (o != ZXV_LAYER_CID_BYTES || (acc & ((1u << bits) - 1u))) return 0;
    return cid[0] == 0x01 && cid[1] == 0x55 && cid[2] == 0x12 && cid[3] == 0x20;
}

static void cid_of(const char *t, uint32_t n, uint8_t cid[ZXV_LAYER_CID_BYTES])
{
    cid[0] = 0x01;
    cid[1] = 0x55;
    cid[2] = 0x12;
    cid[3] = 0x20;
    sha256((const uint8_t *) t, n, cid + 4);
}

/* ---- one object line: {"key": value, ...} with string or number values --- */

typedef struct {
    const char *k;
    uint32_t kn;
    const char *v; /* a string's characters, or a number's digits */
    uint32_t vn;
    int str;
} kv_t;

/* The next pair from *q in [.., end); 1 got, 0 at "}", -1 malformed. */
static int pair(const char **q, const char *end, kv_t *kv, int first)
{
    const char *p = *q;
    if (p < end && *p == '}') return 0;
    if (!first) {
        if (end - p < 2 || p[0] != ',' || p[1] != ' ') return -1;
        p += 2;
    }
    if (p >= end || *p++ != '"') return -1;
    kv->k = p;
    while (p < end && *p != '"') {
        if (!((*p >= 'a' && *p <= 'z') || *p == '_' || (*p >= '0' && *p <= '9'))) return -1;
        p++;
    }
    kv->kn = (uint32_t) (p - kv->k);
    if (end - p < 3 || p[0] != '"' || p[1] != ':' || p[2] != ' ') return -1;
    p += 3;
    if (p < end && *p == '"') {
        kv->str = 1;
        kv->v = ++p;
        while (p < end && *p != '"') {
            if (*p < 0x20 || *p > 0x7e || *p == '\\') return -1;
            p++;
        }
        if (p >= end) return -1;
        kv->vn = (uint32_t) (p - kv->v);
        p++;
    } else {
        kv->str = 0;
        kv->v = p;
        while (p < end && *p >= '0' && *p <= '9') p++;
        kv->vn = (uint32_t) (p - kv->v);
        if (!kv->vn || kv->vn > 19 || (kv->vn > 1 && kv->v[0] == '0')) return -1;
    }
    *q = p;
    return 1;
}

static int key(const kv_t *kv, const char *s)
{
    return is(kv->k, kv->kn, s);
}

static int copy(char *dst, uint32_t cap, const kv_t *kv)
{
    if (!kv->str || kv->vn >= cap) return -1;
    for (uint32_t i = 0; i < kv->vn; i++) dst[i] = kv->v[i];
    dst[kv->vn] = 0;
    return 0;
}

static uint64_t num(const kv_t *kv)
{
    uint64_t v = 0;
    for (uint32_t i = 0; i < kv->vn; i++) v = v * 10u + (uint64_t) (kv->v[i] - '0');
    return v;
}

static int hexv(char c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

/* One object line [p, p+n) (without the leading two spaces or trailing
 * comma) into *o. 0 ok, -1 malformed. Keys must come in mk_layers.py's
 * order, each once. */
static int obj(const char *p, uint32_t n, zxv_layer_obj_t *o)
{
    static const char *const ORDER[] = {"kind",        "arch",         "group",      "name",
                                        "firmware",    "link_address", "page_bytes", "sector_bytes",
                                        "min_ram_mib", "bytes",        "sha256",     "cid"};
    const char *q = p, *end = p + n;
    uint32_t next = 0;
    kv_t kv;
    for (uint32_t i = 0; i < sizeof *o; i++) ((uint8_t *) o)[i] = 0;
    if (n < 2 || *q++ != '{' || end[-1] != '}') return -1;
    end--;
    for (int first = 1;; first = 0) {
        if (q == end) break;
        if (pair(&q, end, &kv, first) != 1) return -1;
        uint32_t k = next;
        while (k < sizeof ORDER / sizeof ORDER[0] && !key(&kv, ORDER[k])) k++;
        if (k == sizeof ORDER / sizeof ORDER[0]) return -1; /* unknown, repeated or out of order */
        next = k + 1;
        int bad = 0;
        if (key(&kv, "kind"))
            bad = copy(o->kind, sizeof o->kind, &kv);
        else if (key(&kv, "arch"))
            bad = copy(o->arch, sizeof o->arch, &kv);
        else if (key(&kv, "group"))
            bad = copy(o->group, sizeof o->group, &kv);
        else if (key(&kv, "name"))
            bad = copy(o->name, sizeof o->name, &kv);
        else if (key(&kv, "firmware"))
            bad = copy(o->firmware, sizeof o->firmware, &kv);
        else if (key(&kv, "link_address") || key(&kv, "cid"))
            bad = !kv.str;
        else if (key(&kv, "page_bytes") || key(&kv, "sector_bytes") || key(&kv, "min_ram_mib") ||
                 key(&kv, "bytes")) {
            if (kv.str) return -1;
            if (key(&kv, "page_bytes")) o->page_bytes = num(&kv);
            if (key(&kv, "sector_bytes")) o->sector_bytes = num(&kv);
            if (key(&kv, "bytes")) {
                o->bytes = num(&kv);
                o->has_bytes = 1;
            }
        } else { /* sha256 */
            if (!kv.str || kv.vn != 64) return -1;
            for (uint32_t i = 0; i < 32; i++) {
                int h = hexv(kv.v[2 * i]), l = hexv(kv.v[2 * i + 1]);
                if (h < 0 || l < 0) return -1;
                o->sha[i] = (uint8_t) (h << 4 | l);
            }
            o->has_sha = 1;
        }
        if (bad) return -1;
    }
    return o->kind[0] ? 0 : -1;
}

/* ---- the manifest ------------------------------------------------------------ */

static int fail(const char **why, const char *s)
{
    *why = s;
    return -1;
}

int zxv_layer_parse(const char *t, uint32_t n, zxv_layer_t *L, const char **why)
{
    const char *line[7];
    uint32_t len[7], pos = 0;
    for (uint32_t i = 0; i < sizeof *L; i++) ((uint8_t *) L)[i] = 0;
    if (n > ZXV_LAYER_BYTES_MAX) return fail(why, "over 1 MiB");
    if (!n || t[n - 1] != '\n') return fail(why, "does not end with a newline");
    for (uint32_t i = 0; i < 7; i++) { /* the head: seven lines */
        line[i] = t + pos;
        while (pos < n && t[pos] != '\n') pos++;
        if (pos >= n) return fail(why, "too short");
        len[i] = (uint32_t) (t + pos - line[i]);
        pos++;
    }
    if (!is(line[0], len[0], "{") || !is(line[1], len[1], " \"format\": \"zxv-layer 1\","))
        return fail(why, "not a 'zxv-layer 1' manifest");
    if (len[2] != 18 || !starts(line[2], len[2], " \"layer_depth\": ") || line[2][16] < '0' ||
        line[2][16] > '4' || line[2][17] != ',')
        return fail(why, "layer_depth is not 0..4");
    L->depth = (uint8_t) (line[2][16] - '0');
    {
        char want[48];
        const char *a = " \"layer_name\": \"", *nm = NAMES[L->depth];
        uint32_t k = 0;
        while (*a) want[k++] = *a++;
        while (*nm) want[k++] = *nm++;
        want[k++] = '"';
        want[k++] = ',';
        want[k] = 0;
        if (!is(line[3], len[3], want)) return fail(why, "layer_name does not match its depth");
    }
    if (L->depth == 0) {
        if (!is(line[4], len[4], " \"parent_layer_cid\": null,"))
            return fail(why, "L0 names a parent");
    } else {
        const char *a = " \"parent_layer_cid\": \"";
        uint32_t k = slen(a);
        if (len[4] != k + 59 + 2 || !starts(line[4], len[4], a) || line[4][k + 59] != '"' ||
            line[4][k + 60] != ',' || !cid_parse(line[4] + k, L->parent))
            return fail(why, "parent_layer_cid is not a layer CID (bafkrei...)");
    }
    if (is(line[5], len[5], " \"parent_compat\": \"evo_comm\",") && L->depth >= 3)
        L->compat = 1;
    else if (!is(line[5], len[5], " \"parent_compat\": \"exact\","))
        return fail(why, "parent_compat is not exact (or evo_comm at L3 and L4)");
    if (!is(line[6], len[6], " \"objects\": [")) return fail(why, "no objects list");

    /* the objects, then " ]", "}" and the end */
    L->objs = t + pos;
    for (;;) {
        const char *l = t + pos;
        while (pos < n && t[pos] != '\n') pos++;
        if (pos >= n) return fail(why, "the objects list does not close");
        uint32_t ln = (uint32_t) (t + pos - l);
        pos++;
        if (is(l, ln, " ]")) {
            if (!L->nobj) return fail(why, "the layer holds nothing");
            break;
        }
        zxv_layer_obj_t o;
        int comma = ln > 3 && l[ln - 1] == ',';
        if (ln < 4 || l[0] != ' ' || l[1] != ' ' || obj(l + 2, ln - 2 - (uint32_t) comma, &o) != 0)
            return fail(why, "an object line is malformed");
        /* every line but the last ends with a comma */
        const char *nx = t + pos;
        int last = (uint32_t) (n - pos) >= 3 && nx[0] == ' ' && nx[1] == ']' && nx[2] == '\n';
        if (comma == last) return fail(why, "commas between objects are wrong");
        L->nobj++;
        L->objs_len = (uint32_t) (t + pos - L->objs);
    }
    if (n - pos != 2 || t[pos] != '}' || t[pos + 1] != '\n')
        return fail(why, "bytes after the closing brace");
    cid_of(t, n, L->cid);
    return 0;
}

int zxv_layer_next(const zxv_layer_t *L, uint32_t *pos, zxv_layer_obj_t *o)
{
    if (*pos >= L->objs_len) return 0;
    const char *l = L->objs + *pos;
    uint32_t ln = 0;
    while (*pos + ln < L->objs_len && l[ln] != '\n') ln++;
    *pos += ln + 1;
    int comma = l[ln - 1] == ',';
    (void) obj(l + 2, ln - 2 - (uint32_t) comma, o);
    return 1;
}

static int eq(const uint8_t *a, const uint8_t *b)
{
    uint8_t d = 0;
    for (uint32_t i = 0; i < ZXV_LAYER_CID_BYTES; i++) d |= (uint8_t) (a[i] ^ b[i]);
    return d == 0;
}

int zxv_layer_chain(const zxv_layer_t *const *L, uint32_t n,
                    int (*admit)(const uint8_t parent[ZXV_LAYER_CID_BYTES], void *ctx), void *ctx,
                    uint32_t *at, const char **why)
{
    for (uint32_t i = 0; i < n; i++) {
        *at = i;
        if (i == 0) {
            if (L[0]->depth != 0) return fail(why, "the chain does not start at L0");
            continue;
        }
        const zxv_layer_t *p = L[i - 1], *c = L[i];
        if (!(c->depth == p->depth + 1 || (c->depth == 4 && p->depth == 4)))
            return fail(why, "depth does not rise by one");
        if (eq(c->parent, p->cid)) continue;
        if (c->compat && admit && admit(c->parent, ctx)) continue;
        return fail(why, c->compat ? "names a parent that is neither the layer below nor one "
                                     "evo_comm admits"
                                   : "names a parent that is not the layer below");
    }
    return 0;
}
