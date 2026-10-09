/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* zx_upcheck.c — the update-bucket checker: configuration, the per-bucket
 * check, resumable downloads, and the bridge into update.h. See zx_upcheck.h.
 */
#include "zx_upcheck.h"
#include "zx_upcheck_util.h"
#include "../robin_debanks/sha256.h"

/* ===== small helpers ======================================================== */

static void cid_copy(ipfsn_cid_t *d, const ipfsn_cid_t *s)
{
    zxu__cpy(d, s, (uint32_t) sizeof(*d));
}

static bool cid_same(const ipfsn_cid_t *a, const ipfsn_cid_t *b)
{
    ipfsn_cid_t x, y;
    ipfsn_cid_to_v1(a, &x);
    ipfsn_cid_to_v1(b, &y);
    return ipfsn_cid_equal(&x, &y);
}

typedef struct {
    char *p;
    uint32_t cap, n;
} sb_t;

static void sb_add(sb_t *s, const char *t)
{
    while (t && *t && s->n + 1u < s->cap) s->p[s->n++] = *t++;
    s->p[s->n] = 0;
}

static void sb_ver(sb_t *s, uint32_t v)
{
    char t[16];
    if (zxu_version_format(v, t, sizeof t) > 0) sb_add(s, t);
}

static void sb_u32(sb_t *s, uint32_t v)
{
    static const uint32_t p10[10] = {1000000000u, 100000000u, 10000000u, 1000000u, 100000u,
                                     10000u,      1000u,      100u,      10u,      1u};
    char t[12];
    uint32_t n = 0;
    bool lead = true;
    for (uint32_t i = 0; i < 10; i++) {
        uint32_t d = 0;
        while (v >= p10[i]) {
            v -= p10[i];
            d++;
        }
        if (d || !lead || i == 9) {
            t[n++] = (char) ('0' + d);
            lead = false;
        }
    }
    t[n] = 0;
    sb_add(s, t);
}

static void label_copy(char *dst, const char *src)
{
    uint32_t n = zxu__strlen(src, ZXU_LABEL_MAX - 1u);
    for (uint32_t i = 0; i < n; i++) {
        char c = src[i];
        dst[i] = (c < 0x20 || c == 0x7f) ? '?' : c;
    }
    if (n == 0) {
        const char *d = "bucket";
        for (n = 0; d[n]; n++) dst[n] = d[n];
    }
    dst[n] = 0;
}

/* ===== configuration ========================================================= */

static void reset_state(zxu_bucket_t *b)
{
    b->have_seq = false;
    b->ipns_seq = 0;
    zxu__set(&b->ipns_value, 0, (uint32_t) sizeof b->ipns_value);
    b->have_issued = false;
    b->issued = 0;
    zxu__set(b->manifest_digest, 0, 32);
    b->next_due = 0;
    b->have_notified = false;
    zxu__set(&b->notified_root, 0, (uint32_t) sizeof b->notified_root);
}

static int parse_bucket_cid(const char *s, ipfsn_cid_t *cid)
{
    uint32_t n = zxu__strlen(s, IPFSN_CID_STR_MAX + 1u);
    if (!s || n == 0 || n > IPFSN_CID_STR_MAX) return ZXU_ERR_ARG;
    if (ipfsn_cid_parse(s, n, cid)) return ZXU_ERR_MALFORMED;
    if (cid->codec != IPFSN_MC_DAG_PB) return ZXU_ERR_UNSUPP; /* a bucket is a directory */
    return ZXU_OK;
}

static bool same_source(const zxu_bucket_t *b, uint8_t src, const ipfsn_cid_t *cid,
                        const uint8_t *pk)
{
    if (!b->used || b->source != src) return false;
    if (src == ZXU_SRC_CID) return cid_same(&b->cid, cid);
    return zxu__eq(b->ipns_pk, pk, 32);
}

static int add_bucket(zxu_config_t *c, const char *label, uint8_t src, const ipfsn_cid_t *cid,
                      const uint8_t *pk, bool builtin, uint32_t *idx)
{
    for (uint32_t i = 0; i < ZXU_MAX_BUCKETS; i++)
        if (same_source(&c->b[i], src, cid, pk)) return ZXU_ERR_EXISTS;
    for (uint32_t i = 0; i < ZXU_MAX_BUCKETS; i++) {
        zxu_bucket_t *b = &c->b[i];
        if (b->used) continue;
        zxu__set(b, 0, (uint32_t) sizeof(*b));
        b->used = true;
        b->enabled = true;
        b->builtin = builtin;
        b->source = src;
        label_copy(b->label, label);
        if (src == ZXU_SRC_CID)
            cid_copy(&b->cid, cid);
        else
            zxu__cpy(b->ipns_pk, pk, 32);
        if (idx) *idx = i;
        return ZXU_OK;
    }
    return ZXU_ERR_FULL;
}

int zxu_config_init(zxu_config_t *c, bool enabled, uint8_t arch, uint32_t installed,
                    const uint8_t *release_pk)
{
    ipfsn_cid_t cid;
    uint32_t idx;
    int r;
    if (!c || arch > ZXU_ARCH_X86) return ZXU_ERR_ARG;
    zxu__set(c, 0, (uint32_t) sizeof(*c));
    c->enabled = enabled;
    c->arch = arch;
    c->installed = installed;
    c->interval_s = ZXU_DEFAULT_INTERVAL;
    if ((r = parse_bucket_cid(ZXU_BUILTIN_CID, &cid)) != 0) return r;
    if ((r = add_bucket(c, ZXU_BUILTIN_LABEL, ZXU_SRC_CID, &cid, 0, true, &idx)) != 0) return r;
    if (release_pk) return zxu_bucket_trust_key(c, idx, release_pk);
    return ZXU_OK;
}

int zxu_bucket_add_cid(zxu_config_t *c, const char *label, const char *cid, uint32_t *idx)
{
    ipfsn_cid_t k;
    int r;
    if (!c) return ZXU_ERR_ARG;
    if ((r = parse_bucket_cid(cid, &k)) != 0) return r;
    return add_bucket(c, label, ZXU_SRC_CID, &k, 0, false, idx);
}

int zxu_bucket_add_ipns(zxu_config_t *c, const char *label, const char *name, uint32_t *idx)
{
    uint8_t pk[32];
    int r;
    if (!c || !name) return ZXU_ERR_ARG;
    if ((r = zxu_ipns_name_parse(name, zxu__strlen(name, ZXU_NAME_MAX + 1u), pk)) != 0) return r;
    return add_bucket(c, label, ZXU_SRC_IPNS, 0, pk, false, idx);
}

static zxu_bucket_t *slot(zxu_config_t *c, uint32_t idx)
{
    if (!c || idx >= ZXU_MAX_BUCKETS || !c->b[idx].used) return 0;
    return &c->b[idx];
}

int zxu_bucket_remove(zxu_config_t *c, uint32_t idx)
{
    zxu_bucket_t *b = slot(c, idx);
    if (!b) return ZXU_ERR_NOTFOUND;
    if (b->builtin) return ZXU_ERR_BUILTIN; /* it can be disabled instead */
    zxu__set(b, 0, (uint32_t) sizeof(*b));
    return ZXU_OK;
}

int zxu_bucket_enable(zxu_config_t *c, uint32_t idx, bool on)
{
    zxu_bucket_t *b = slot(c, idx);
    if (!b) return ZXU_ERR_NOTFOUND;
    b->enabled = on;
    return ZXU_OK;
}

int zxu_bucket_set_cid(zxu_config_t *c, uint32_t idx, const char *cid)
{
    zxu_bucket_t *b = slot(c, idx);
    ipfsn_cid_t k;
    int r;
    if (!b) return ZXU_ERR_NOTFOUND;
    if ((r = parse_bucket_cid(cid, &k)) != 0) return r;
    for (uint32_t i = 0; i < ZXU_MAX_BUCKETS; i++)
        if (i != idx && same_source(&c->b[i], ZXU_SRC_CID, &k, 0)) return ZXU_ERR_EXISTS;
    b->source = ZXU_SRC_CID;
    cid_copy(&b->cid, &k);
    zxu__set(b->ipns_pk, 0, 32);
    reset_state(b);
    return ZXU_OK;
}

int zxu_bucket_set_ipns(zxu_config_t *c, uint32_t idx, const char *name)
{
    zxu_bucket_t *b = slot(c, idx);
    uint8_t pk[32];
    int r;
    if (!b || !name) return b ? ZXU_ERR_ARG : ZXU_ERR_NOTFOUND;
    if ((r = zxu_ipns_name_parse(name, zxu__strlen(name, ZXU_NAME_MAX + 1u), pk)) != 0) return r;
    for (uint32_t i = 0; i < ZXU_MAX_BUCKETS; i++)
        if (i != idx && same_source(&c->b[i], ZXU_SRC_IPNS, 0, pk)) return ZXU_ERR_EXISTS;
    b->source = ZXU_SRC_IPNS;
    zxu__cpy(b->ipns_pk, pk, 32);
    zxu__set(&b->cid, 0, (uint32_t) sizeof b->cid);
    reset_state(b);
    return ZXU_OK;
}

int zxu_bucket_trust_key(zxu_config_t *c, uint32_t idx, const uint8_t pk[ZXU_PK_BYTES])
{
    zxu_bucket_t *b = slot(c, idx);
    if (!b) return ZXU_ERR_NOTFOUND;
    if (!pk) return ZXU_ERR_ARG;
    for (uint32_t i = 0; i < b->nkeys; i++)
        if (zxu__eq(b->keys[i], pk, ZXU_PK_BYTES)) return ZXU_OK;
    if (b->nkeys >= ZXU_MAX_KEYS) return ZXU_ERR_FULL;
    zxu__cpy(b->keys[b->nkeys++], pk, ZXU_PK_BYTES);
    return ZXU_OK;
}

int zxu_bucket_untrust_key(zxu_config_t *c, uint32_t idx, const uint8_t pk[ZXU_PK_BYTES])
{
    zxu_bucket_t *b = slot(c, idx);
    if (!b) return ZXU_ERR_NOTFOUND;
    if (!pk) return ZXU_ERR_ARG;
    for (uint32_t i = 0; i < b->nkeys; i++) {
        if (!zxu__eq(b->keys[i], pk, ZXU_PK_BYTES)) continue;
        for (uint32_t j = i + 1u; j < b->nkeys; j++)
            zxu__cpy(b->keys[j - 1u], b->keys[j], ZXU_PK_BYTES);
        b->nkeys--;
        zxu__set(b->keys[b->nkeys], 0, ZXU_PK_BYTES);
        return ZXU_OK;
    }
    return ZXU_ERR_NOTFOUND;
}

uint32_t zxu_bucket_count(const zxu_config_t *c)
{
    uint32_t n = 0;
    for (uint32_t i = 0; c && i < ZXU_MAX_BUCKETS; i++) n += c->b[i].used ? 1u : 0u;
    return n;
}

int zxu_init(zxu_t *u, zxu_config_t *cfg, ipfsn_node_t *node, zxu_work_t *work,
             zxu_sigverify_fn verify, void *verify_ctx, zxu_notify_fn notify, void *notify_ctx)
{
    if (!u || !cfg || !node || !work || !verify || !work->blk || !work->walk ||
        work->blk_cap < IPFSN_NODE_MAX || !work->walk->leaf)
        return ZXU_ERR_ARG;
    zxu__set(u, 0, (uint32_t) sizeof(*u));
    u->cfg = cfg;
    u->node = node;
    u->work = work;
    u->verify = verify;
    u->verify_ctx = verify_ctx;
    u->notify = notify;
    u->notify_ctx = notify_ctx;
    return ZXU_OK;
}

/* ===== block source that remembers network failures ========================== */

typedef struct {
    ipfsn_node_t *node;
    int fail; /* first fetch error (not "found nothing in a directory") */
} src_t;

static int src_get(void *ctx, const ipfsn_cid_t *cid, uint8_t *buf, uint32_t cap, uint32_t *len)
{
    src_t *s = (src_t *) ctx;
    int r = ipfsn_node_source(s->node, cid, buf, cap, len);
    if (r == IPFSN_OK) r = ipfsn_cid_verify(cid, buf, *len); /* belt and braces */
    if (r && !s->fail) s->fail = r;
    return r;
}

typedef struct {
    uint8_t *buf;
    uint32_t cap, n;
    bool over;
} membuf_t;

static int mem_write(void *ctx, const uint8_t *d, uint32_t len)
{
    membuf_t *m = (membuf_t *) ctx;
    if (len > m->cap - m->n) {
        m->over = true;
        return 1;
    }
    zxu__cpy(m->buf + m->n, d, len);
    m->n += len;
    return 0;
}

static int list_cb(void *ctx, const ipfsn_dirent_t *e)
{
    zxu_result_t *r = (zxu_result_t *) ctx;
    zxu_listent_t *l;
    if (r->n_list >= ZXU_LIST_MAX) {
        r->list_truncated = true;
        return 1;
    }
    l = &r->list[r->n_list++];
    zxu__cpy(l->name, e->name, e->name_len);
    l->name[e->name_len] = 0;
    l->name_len = e->name_len;
    cid_copy(&l->cid, &e->cid);
    l->tsize = e->tsize;
    return 0;
}

/* Read one small file of the bucket into buf. */
static int read_file(zxu_t *u, src_t *s, const ipfsn_cid_t *cid, uint8_t *buf, uint32_t cap,
                     uint32_t *len)
{
    membuf_t m = {buf, cap, 0, false};
    uint64_t size = 0;
    int r = ipfsn_cat(u->work->walk, cid, src_get, s, mem_write, &m, &size);
    if (m.over) return ZXU_ERR_SPACE;
    if (r) return r;
    *len = m.n;
    return ZXU_OK;
}

/* ===== the check ============================================================== */

static bool is_net(int r)
{
    return r == IPFSN_ERR_TRANSPORT || r == IPFSN_ERR_NOTFOUND || r == IPFSN_ERR_HASH ||
           r == IPFSN_ERR_IO;
}

static void notify_once(zxu_t *u, zxu_bucket_t *b, zxu_result_t *r)
{
    sb_t t = {r->title, sizeof r->title, 0}, d = {r->body, sizeof r->body, 0};
    int kind;
    r->title[0] = 0;
    r->body[0] = 0;
    sb_add(&d, b->label);
    sb_add(&d, ": ");
    if (r->status == ZXU_ST_UPDATE_AVAILABLE) {
        kind = ZXU_NOTIFY_UPDATE;
        sb_add(&t, "Update available");
        sb_add(&d, "ZXV ");
        sb_ver(&d, r->manifest.release);
        sb_add(&d, " is available (");
        sb_u32(&d, r->n_applicable);
        sb_add(&d, r->n_applicable == 1 ? " file)." : " files).");
    } else if (r->status == ZXU_ST_INCOMPATIBLE) {
        kind = ZXU_NOTIFY_INCOMPATIBLE;
        sb_add(&t, "Update needs an earlier step");
        sb_add(&d, "ZXV ");
        sb_ver(&d, r->manifest.release);
        sb_add(&d, " needs ");
        sb_ver(&d, r->manifest.min_version);
        sb_add(&d, " or later installed first.");
    } else if (r->status == ZXU_ST_UNSIGNED) {
        kind = ZXU_NOTIFY_UNSIGNED;
        sb_add(&t, "Unsigned update bucket");
        sb_add(&d, "its contents are not signed by a release key you trust, so nothing from "
                   "it will be installed.");
    } else {
        return;
    }
    if (b->have_notified && cid_same(&b->notified_root, &r->root)) return; /* already told */
    b->have_notified = true;
    cid_copy(&b->notified_root, &r->root);
    if (u->notify) u->notify(u->notify_ctx, kind, r->title, r->body);
}

/* Resolve the bucket root. ZXU_ST_UP_TO_DATE here means "carry on". */
static zxu_status_t resolve_root(zxu_t *u, zxu_bucket_t *b, uint64_t now, zxu_result_t *r,
                                 bool *seq_ok, zxu_ipns_t *rec)
{
    char url[512], name[ZXU_NAME_MAX];
    ipfsn_gateway_ops_t *gw = &u->node->gw;
    uint32_t n = 0, len = 0;
    int nl, e;
    *seq_ok = false;
    if (b->source == ZXU_SRC_CID) {
        cid_copy(&r->root, &b->cid);
        return ZXU_ST_UP_TO_DATE;
    }
    if (now == 0) return ZXU_ST_NO_CLOCK;
    if (!gw->https_get || !gw->base) {
        r->detail = ZXU_ERR_NETWORK;
        return ZXU_ST_NETWORK;
    }
    if ((nl = zxu_ipns_name_string(b->ipns_pk, name, sizeof name)) < 0) return ZXU_ST_IPNS_INVALID;
    {
        uint32_t bl = zxu__strlen(gw->base, 256);
        const char *q = "?format=ipns-record";
        uint32_t ql = zxu__strlen(q, 64);
        if (bl == 0 || bl >= 256 || gw->base[bl - 1u] == '/' ||
            bl + 6u + (uint32_t) nl + ql + 1u > sizeof url) {
            r->detail = ZXU_ERR_ARG;
            return ZXU_ST_NETWORK;
        }
        zxu__cpy(url, gw->base, bl);
        n = bl;
        zxu__cpy(url + n, "/ipns/", 6);
        n += 6;
        zxu__cpy(url + n, name, (uint32_t) nl);
        n += (uint32_t) nl;
        zxu__cpy(url + n, q, ql);
        n += ql;
        url[n] = 0;
    }
    u->requests++;
    if (gw->https_get(gw->ctx, url, "application/vnd.ipfs.ipns-record", u->work->rec,
                      sizeof u->work->rec, &len) != 0) {
        r->detail = ZXU_ERR_NETWORK;
        return ZXU_ST_NETWORK;
    }
    if (len > sizeof u->work->rec) {
        r->detail = ZXU_ERR_SPACE;
        return ZXU_ST_IPNS_INVALID;
    }
    e = zxu_ipns_verify(u->work->rec, len, b->ipns_pk, now, u->work->msg, sizeof u->work->msg, rec);
    r->detail = e;
    if (e == ZXU_ERR_EXPIRED) return ZXU_ST_IPNS_EXPIRED;
    if (e) return ZXU_ST_IPNS_INVALID;
    r->ipns_seq = rec->sequence;
    if (b->have_seq) {
        if (rec->sequence < b->ipns_seq) {
            r->detail = ZXU_ERR_ROLLBACK;
            return ZXU_ST_IPNS_ROLLBACK;
        }
        if (rec->sequence == b->ipns_seq && !cid_same(&rec->value, &b->ipns_value)) {
            r->detail = ZXU_ERR_ROLLBACK; /* two values under one sequence */
            return ZXU_ST_IPNS_ROLLBACK;
        }
    }
    *seq_ok = true;
    cid_copy(&r->root, &rec->value);
    return ZXU_ST_UP_TO_DATE;
}

static zxu_status_t check_contents(zxu_t *u, zxu_bucket_t *b, zxu_result_t *r, bool *man_ok,
                                   uint8_t digest[32])
{
    src_t s = {u->node, 0};
    ipfsn_dirent_t me, se;
    uint32_t cnt = 0, mlen = 0, slen = 0;
    int e;
    *man_ok = false;
    if (r->root.codec != IPFSN_MC_DAG_PB) {
        r->detail = IPFSN_ERR_UNSUPP;
        return ZXU_ST_BAD_BUCKET;
    }
    /* 1. the listing (also fetches and verifies the root) */
    e = ipfsn_dir_list(&u->work->dwalk, &r->root, src_get, &s, u->work->blk, u->work->blk_cap,
                       list_cb, r, &cnt);
    if (e != IPFSN_OK && !(e == 1 && r->list_truncated)) {
        r->detail = e;
        if (s.fail && is_net(s.fail)) return ZXU_ST_NETWORK;
        return ZXU_ST_BAD_BUCKET;
    }
    /* 2. the manifest and its signature */
    s.fail = 0;
    e = ipfsn_dir_lookup(&r->root, src_get, &s, (const uint8_t *) ZXU_MANIFEST_NAME,
                         zxu__strlen(ZXU_MANIFEST_NAME, 64), u->work->blk, u->work->blk_cap, &me);
    if (e) {
        r->detail = e;
        if (s.fail) return is_net(s.fail) ? ZXU_ST_NETWORK : ZXU_ST_BAD_BUCKET;
        if (e != IPFSN_ERR_NOTFOUND) return ZXU_ST_BAD_BUCKET;
        r->unsigned_reason = ZXU_UNS_NO_MANIFEST;
        return ZXU_ST_UNSIGNED;
    }
    e = ipfsn_dir_lookup(&r->root, src_get, &s, (const uint8_t *) ZXU_SIG_NAME,
                         zxu__strlen(ZXU_SIG_NAME, 64), u->work->blk, u->work->blk_cap, &se);
    if (e) {
        r->detail = e;
        if (s.fail) return is_net(s.fail) ? ZXU_ST_NETWORK : ZXU_ST_BAD_BUCKET;
        if (e != IPFSN_ERR_NOTFOUND) return ZXU_ST_BAD_BUCKET;
        r->unsigned_reason = ZXU_UNS_NO_SIG;
        return ZXU_ST_UNSIGNED;
    }
    if (me.tsize > (uint64_t) ZXU_MANIFEST_MAX + 4096u) {
        r->unsigned_reason = ZXU_UNS_TOO_BIG;
        return ZXU_ST_UNSIGNED;
    }
    e = read_file(u, &s, &me.cid, u->work->manifest, sizeof u->work->manifest, &mlen);
    if (e == ZXU_ERR_SPACE) {
        r->unsigned_reason = ZXU_UNS_TOO_BIG;
        return ZXU_ST_UNSIGNED;
    }
    if (e) {
        r->detail = e;
        return (s.fail && is_net(s.fail)) ? ZXU_ST_NETWORK : ZXU_ST_BAD_BUCKET;
    }
    e = read_file(u, &s, &se.cid, u->work->sig, sizeof u->work->sig, &slen);
    if (e && e != ZXU_ERR_SPACE) {
        r->detail = e;
        return (s.fail && is_net(s.fail)) ? ZXU_ST_NETWORK : ZXU_ST_BAD_BUCKET;
    }
    if (e == ZXU_ERR_SPACE || slen != ZXU_SIG_BYTES) {
        r->unsigned_reason = ZXU_UNS_NO_SIG;
        return ZXU_ST_UNSIGNED;
    }
    if (b->nkeys == 0) {
        r->unsigned_reason = ZXU_UNS_NO_KEYS;
        return ZXU_ST_UNSIGNED;
    }
    if (zxu_manifest_verify(u->verify, u->verify_ctx, (const uint8_t(*)[ZXU_PK_BYTES]) b->keys,
                            b->nkeys, u->work->manifest, mlen, u->work->sig, &r->key_index)) {
        r->unsigned_reason = ZXU_UNS_BAD_SIG;
        return ZXU_ST_UNSIGNED;
    }
    /* 3. signed: now it must also be well-formed and not older than before */
    if (zxu_manifest_parse(u->work->manifest, mlen, &r->manifest)) {
        r->detail = ZXU_ERR_MALFORMED;
        return ZXU_ST_MANIFEST_INVALID;
    }
    sha256(u->work->manifest, mlen, digest);
    if (b->have_issued &&
        (r->manifest.issued < b->issued ||
         (r->manifest.issued == b->issued && !zxu__eq(digest, b->manifest_digest, 32)))) {
        r->detail = ZXU_ERR_ROLLBACK;
        return ZXU_ST_MANIFEST_ROLLBACK;
    }
    *man_ok = true;
    r->have_manifest = true;
    /* 4. is each listed file where the manifest says? (informational) */
    for (uint32_t i = 0; i < r->manifest.n; i++) {
        zxu_entry_t *en = &r->manifest.e[i];
        ipfsn_dirent_t pe;
        s.fail = 0;
        if (ipfsn_dir_resolve(&r->root, src_get, &s, en->path, en->path_len, u->work->blk,
                              u->work->blk_cap, &pe) == IPFSN_OK)
            en->in_bucket = cid_same(&pe.cid, &en->cid);
    }
    /* 5. what applies to this system */
    if (r->manifest.release <= u->cfg->installed) return ZXU_ST_UP_TO_DATE;
    if (u->cfg->installed < r->manifest.min_version) return ZXU_ST_INCOMPATIBLE;
    for (uint32_t i = 0; i < r->manifest.n; i++) {
        const zxu_entry_t *en = &r->manifest.e[i];
        if ((en->arch == ZXU_ARCH_ANY || en->arch == u->cfg->arch) &&
            en->version > u->cfg->installed)
            r->applicable[r->n_applicable++] = (uint8_t) i;
    }
    return r->n_applicable ? ZXU_ST_UPDATE_AVAILABLE : ZXU_ST_UP_TO_DATE;
}

zxu_status_t zxu_check(zxu_t *u, uint32_t idx, uint64_t now, bool force, zxu_result_t *r)
{
    zxu_bucket_t *b;
    zxu_ipns_t rec;
    bool seq_ok = false, man_ok = false;
    uint8_t digest[32];
    zxu_status_t st;
    if (!r) return ZXU_ST_DISABLED;
    zxu__set(r, 0, (uint32_t) sizeof(*r));
    r->bucket = idx;
    r->status = ZXU_ST_DISABLED;
    if (!u || !u->cfg || !(b = slot(u->cfg, idx))) {
        r->detail = ZXU_ERR_NOTFOUND;
        return r->status;
    }
    if (!u->cfg->enabled || !b->enabled) return r->status; /* nothing is fetched */
    if (!force && (u->cfg->interval_s == 0 || (now && now < b->next_due))) {
        r->status = ZXU_ST_NOT_DUE;
        return r->status;
    }
    st = resolve_root(u, b, now, r, &seq_ok, &rec);
    if (st == ZXU_ST_UP_TO_DATE) {
        r->have_root = true;
        st = check_contents(u, b, r, &man_ok, digest);
    }
    r->status = st;
    if (st == ZXU_ST_NETWORK || st == ZXU_ST_NO_CLOCK) return st; /* no state changes */
    /* commit: a verified record and a verified manifest move this bucket forward */
    if (seq_ok) {
        b->have_seq = true;
        b->ipns_seq = rec.sequence;
        cid_copy(&b->ipns_value, &rec.value);
    }
    if (man_ok) {
        b->have_issued = true;
        b->issued = r->manifest.issued;
        zxu__cpy(b->manifest_digest, digest, 32);
    }
    if (now) {
        uint32_t iv = u->cfg->interval_s;
        if (iv && iv < ZXU_MIN_INTERVAL) iv = ZXU_MIN_INTERVAL;
        b->next_due = now + iv;
    }
    if (r->have_root) notify_once(u, b, r);
    return st;
}

uint32_t zxu_check_all(zxu_t *u, uint64_t now, bool force, zxu_result_t *rs, uint32_t cap)
{
    uint32_t n = 0;
    if (!u || !u->cfg || !rs) return 0;
    for (uint32_t i = 0; i < ZXU_MAX_BUCKETS && n < cap; i++) {
        if (!u->cfg->b[i].used) continue;
        zxu_check(u, i, now, force, &rs[n++]);
    }
    return n;
}

bool zxu_installable(const zxu_result_t *r)
{
    return r && r->status == ZXU_ST_UPDATE_AVAILABLE && r->have_manifest && r->n_applicable > 0;
}

const char *zxu_status_str(zxu_status_t s)
{
    switch (s) {
    case ZXU_ST_DISABLED:
        return "disabled";
    case ZXU_ST_NOT_DUE:
        return "not due";
    case ZXU_ST_NETWORK:
        return "network unavailable";
    case ZXU_ST_NO_CLOCK:
        return "no clock for IPNS";
    case ZXU_ST_IPNS_INVALID:
        return "IPNS record invalid";
    case ZXU_ST_IPNS_EXPIRED:
        return "IPNS record expired";
    case ZXU_ST_IPNS_ROLLBACK:
        return "IPNS record older than one already seen";
    case ZXU_ST_BAD_BUCKET:
        return "bucket is not a valid directory";
    case ZXU_ST_UNSIGNED:
        return "unsigned bucket contents";
    case ZXU_ST_MANIFEST_INVALID:
        return "signed manifest is malformed";
    case ZXU_ST_MANIFEST_ROLLBACK:
        return "signed manifest older than one already seen";
    case ZXU_ST_UP_TO_DATE:
        return "up to date";
    case ZXU_ST_INCOMPATIBLE:
        return "update needs a newer installed version";
    case ZXU_ST_UPDATE_AVAILABLE:
        return "update available";
    }
    return "unknown";
}

/* ===== downloads ================================================================ */

static bool add_missing(ipfsn_cid_t *list, uint32_t cap, uint32_t *n, const ipfsn_cid_t *cid)
{
    for (uint32_t i = 0; i < *n; i++)
        if (cid_same(&list[i], cid)) return true;
    if (*n >= cap) return false;
    cid_copy(&list[(*n)++], cid);
    return true;
}

int zxu_plan(zxu_t *u, const ipfsn_cid_t *root, ipfsn_cid_t *missing, uint32_t cap, uint32_t *n,
             bool *complete)
{
    ipfsn_walk_t *w;
    ipfsn_bs_t *bs;
    uint32_t pos[IPFSN_DAG_MAX_DEPTH], d = 0, len;
    bool full = false;
    if (!u || !root || !missing || !n || !complete) return ZXU_ERR_ARG;
    w = u->work->walk;
    bs = u->node->bs;
    *n = 0;
    *complete = false;
    if (root->mh_code == IPFSN_MH_IDENTITY) {
        *complete = true;
        return ZXU_OK;
    }
    if (!ipfsn_bs_has(bs, root)) {
        add_missing(missing, cap, n, root);
        return ZXU_OK;
    }
    if (root->codec == IPFSN_MC_RAW) {
        *complete = true;
        return ZXU_OK;
    }
    if (root->codec != IPFSN_MC_DAG_PB) return ZXU_ERR_UNSUPP;
    /* frames reuse the walker's node buffers: the planner and ipfsn_cat never run together */
    if (ipfsn_bs_get(bs, root, w->f[0].node, IPFSN_NODE_MAX, &len) ||
        ipfsn_dagpb_parse(w->f[0].node, len, &w->f[0].pb))
        return ZXU_ERR_MALFORMED;
    pos[0] = 0;
    for (;;) {
        ipfsn_pblink_t l;
        int r = ipfsn_dagpb_next_link(&w->f[d].pb, &pos[d], &l);
        if (r == IPFSN_ERR_NOTFOUND) {
            if (d == 0) break;
            d--;
            continue;
        }
        if (r) return ZXU_ERR_MALFORMED;
        if (l.cid.mh_code == IPFSN_MH_IDENTITY) continue;
        if (!ipfsn_bs_has(bs, &l.cid)) {
            if (!add_missing(missing, cap, n, &l.cid)) {
                full = true;
                break;
            }
            continue;
        }
        if (l.cid.codec == IPFSN_MC_RAW) continue;
        if (l.cid.codec != IPFSN_MC_DAG_PB) return ZXU_ERR_UNSUPP;
        if (d + 1u >= IPFSN_DAG_MAX_DEPTH) return ZXU_ERR_MALFORMED;
        if (ipfsn_bs_get(bs, &l.cid, w->f[d + 1u].node, IPFSN_NODE_MAX, &len) ||
            ipfsn_dagpb_parse(w->f[d + 1u].node, len, &w->f[d + 1u].pb))
            return ZXU_ERR_MALFORMED;
        d++;
        pos[d] = 0;
    }
    *complete = (*n == 0 && !full);
    return ZXU_OK;
}

int zxu_fetch(zxu_t *u, const ipfsn_cid_t *missing, uint32_t n, uint32_t *got)
{
    uint32_t ok = 0, len;
    if (!u || (!missing && n) || !got) return ZXU_ERR_ARG;
    *got = 0;
    if (!u->cfg->enabled) return ZXU_ERR_DISABLED;
    for (uint32_t i = 0; i < n; i++) {
        if (ipfsn_bs_has(u->node->bs, &missing[i]) ||
            ipfsn_node_source(u->node, &missing[i], u->work->blk, u->work->blk_cap, &len) ==
                IPFSN_OK)
            ok++;
    }
    *got = ok;
    return ok == n ? ZXU_OK : ZXU_ERR_NETWORK;
}

typedef struct {
    sha256_ctx_t h;
    uint64_t n;
} hashout_t;

static int hash_write(void *ctx, const uint8_t *d, uint32_t len)
{
    hashout_t *o = (hashout_t *) ctx;
    sha256_update(&o->h, d, len);
    o->n += len;
    return 0;
}

int zxu_download(zxu_t *u, const zxu_result_t *r, uint32_t entry, ipfsn_cid_t *list,
                 uint32_t list_cap, uint32_t max_rounds)
{
    const zxu_entry_t *e;
    hashout_t o;
    uint8_t dg[32];
    uint64_t size = 0;
    bool complete = false;
    int x;
    if (!u || !r || !list || list_cap == 0) return ZXU_ERR_ARG;
    if (!zxu_installable(r)) return ZXU_ERR_NOTSIGNED;
    if (entry >= r->manifest.n) return ZXU_ERR_ARG;
    if (!u->cfg->enabled) return ZXU_ERR_DISABLED;
    e = &r->manifest.e[entry];
    for (uint32_t round = 0; round <= max_rounds; round++) {
        uint32_t n = 0, got = 0;
        if ((x = zxu_plan(u, &e->cid, list, list_cap, &n, &complete)) != 0) return x;
        if (complete) break;
        if (round == max_rounds) break;
        zxu_fetch(u, list, n, &got);
        if (got == 0) return ZXU_ERR_NETWORK; /* resumable: call again later */
    }
    if (!complete) return ZXU_ERR_NETWORK;
    sha256_init(&o.h);
    o.n = 0;
    x = ipfsn_cat(u->work->walk, &e->cid, ipfsn_bs_source, u->node->bs, hash_write, &o, &size);
    if (x) return x == IPFSN_ERR_HASH ? ZXU_ERR_HASH : ZXU_ERR_MALFORMED;
    sha256_final(&o.h, dg);
    if (o.n != e->size || size != e->size || !zxu__eq(dg, e->sha256, 32)) return ZXU_ERR_HASH;
    return ZXU_OK;
}

/* ===== update.h composition ======================================================= */

#define ZXU_ATT_MAX 64u

typedef struct {
    bool used;
    uint8_t fp[32];   /* SHA-256 of the ML-DSA key that signed */
    uint8_t sha[32];  /* SHA-256 of the file */
    uint8_t mdig[32]; /* SHA-256 of the manifest */
    ipfsn_cid_t cid;
    uint32_t size;
} zxu_att_t;

static zxu_att_t g_att[ZXU_ATT_MAX];

void zxu_catalog_reset(void)
{
    zxu__set(g_att, 0, (uint32_t) sizeof g_att);
}

static void attest(const uint8_t fp[32], const uint8_t sha[32], const uint8_t mdig[32],
                   uint8_t sig[64])
{
    sha256_ctx_t h;
    sha256_init(&h);
    sha256_update(&h, (const uint8_t *) "zxu-attest-v1", 13);
    sha256_update(&h, fp, 32);
    sha256_update(&h, sha, 32);
    sha256_update(&h, mdig, 32);
    sha256_final(&h, sig);
    zxu__cpy(sig + 32, mdig, 32);
}

static zxu_att_t *att_find(const uint8_t *fp, const uint8_t *sha)
{
    for (uint32_t i = 0; i < ZXU_ATT_MAX; i++)
        if (g_att[i].used && (!fp || zxu__eq(g_att[i].fp, fp, 32)) &&
            zxu__eq(g_att[i].sha, sha, 32))
            return &g_att[i];
    return 0;
}

static const char HEX[] = "0123456789abcdef";

int zxu_to_catalog(const zxu_result_t *r, const zxu_config_t *cfg, upd_catalog_t *c,
                   uint32_t *published)
{
    const zxu_bucket_t *b;
    uint8_t fp[32], sig[64];
    if (published) *published = 0;
    if (!r || !cfg || !c) return ZXU_ERR_ARG;
    if (!zxu_installable(r)) return ZXU_ERR_NOTSIGNED;
    if (r->bucket >= ZXU_MAX_BUCKETS || !cfg->b[r->bucket].used) return ZXU_ERR_NOTFOUND;
    b = &cfg->b[r->bucket];
    if (r->key_index >= b->nkeys) return ZXU_ERR_ARG;
    sha256(b->keys[r->key_index], ZXU_PK_BYTES, fp);
    if (!upd_trust_author(c, fp)) return ZXU_ERR_FULL;
    for (uint32_t k = 0; k < r->n_applicable; k++) {
        const zxu_entry_t *e = &r->manifest.e[r->applicable[k]];
        char id[UPD_ID_LEN], name[UPD_NAME_LEN];
        uint8_t ph[32];
        zxu_att_t *a = att_find(fp, e->sha256);
        uint32_t n = 0, j;
        if (!a) {
            for (j = 0; j < ZXU_ATT_MAX && g_att[j].used; j++) {
            }
            if (j == ZXU_ATT_MAX) return ZXU_ERR_FULL;
            a = &g_att[j];
        }
        a->used = true;
        zxu__cpy(a->fp, fp, 32);
        zxu__cpy(a->sha, e->sha256, 32);
        zxu__cpy(a->mdig, b->manifest_digest, 32);
        cid_copy(&a->cid, &e->cid);
        a->size = e->size;
        attest(fp, e->sha256, b->manifest_digest, sig);
        /* id: "zxu<slot>-<12 hex of SHA-256(path)>" */
        sha256((const uint8_t *) e->path, e->path_len, ph);
        id[n++] = 'z';
        id[n++] = 'x';
        id[n++] = 'u';
        id[n++] = (char) ('0' + r->bucket);
        id[n++] = '-';
        for (j = 0; j < 6; j++) {
            id[n++] = HEX[ph[j] >> 4];
            id[n++] = HEX[ph[j] & 15u];
        }
        id[n] = 0;
        n = 0;
        for (j = 0; j < e->path_len && n + 1u < UPD_NAME_LEN; j++) name[n++] = e->path[j];
        name[n] = 0;
        if (upd_publish(c, id, name, e->version, e->sha256, fp, sig, e->size, 0, 0) != UPD_OK)
            return ZXU_ERR_FULL;
        if (published) (*published)++;
    }
    return ZXU_OK;
}

bool zxu_catalog_verify(const uint8_t *msg, uint32_t len, const uint8_t sig[64],
                        const uint8_t pubkey[32])
{
    uint8_t want[64];
    const zxu_att_t *a;
    if (!msg || len != 32 || !sig || !pubkey) return false;
    if (!(a = att_find(pubkey, msg))) return false;
    attest(a->fp, a->sha, a->mdig, want);
    return zxu__eq(want, sig, 64);
}

static int tr_fetch(const uint8_t cid[UPD_CID_LEN], uint8_t *buf, uint32_t cap, uint32_t *out_len,
                    void *ctx)
{
    zxu_t *u = (zxu_t *) ctx;
    const zxu_att_t *a = att_find(0, cid);
    membuf_t m = {buf, cap, 0, false};
    uint64_t size = 0;
    if (!u || !a || !buf || !out_len || a->size > cap) return -1;
    if (!u->cfg->enabled) return -1;
    if (ipfsn_node_cat(u->node, u->work->walk, &a->cid, mem_write, &m, &size) || m.over) return -1;
    *out_len = m.n;
    return 0; /* upd_fetch_verify re-hashes it against the SHA-256 */
}

upd_transport_t zxu_catalog_transport(zxu_t *u)
{
    upd_transport_t t;
    t.fetch = tr_fetch;
    t.ctx = u;
    return t;
}
