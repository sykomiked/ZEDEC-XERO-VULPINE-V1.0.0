/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* cisex_idx.c — the CISEX global IPFS index; see cisex_idx.h. */

#include "cisex_idx.h"
#include "../ipfs_node/ipfsn_dir.h" /* IPFSN_UFS_HAMT */
#include "../robin_debanks/sha256.h"

static void c6x_copy(void *dst, const void *src, uint32_t n)
{
    uint8_t *d = (uint8_t *) dst;
    const uint8_t *s = (const uint8_t *) src;
    for (uint32_t i = 0; i < n; i++) d[i] = s[i];
}

static void c6x_zero(void *dst, uint32_t n)
{
    uint8_t *d = (uint8_t *) dst;
    for (uint32_t i = 0; i < n; i++) d[i] = 0;
}

static bool c6x_eq(const void *a, const void *b, uint32_t n)
{
    const uint8_t *x = (const uint8_t *) a;
    const uint8_t *y = (const uint8_t *) b;
    uint8_t acc = 0;
    for (uint32_t i = 0; i < n; i++) acc |= (uint8_t) (x[i] ^ y[i]);
    return acc == 0;
}

static uint8_t c6x_lower(uint8_t c)
{
    return (c >= 'A' && c <= 'Z') ? (uint8_t) (c + 32) : c;
}

static bool c6x_alnum(uint8_t c)
{
    c = c6x_lower(c);
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
}

static void put_be16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t) (v >> 8);
    p[1] = (uint8_t) v;
}

static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t) (v >> 24);
    p[1] = (uint8_t) (v >> 16);
    p[2] = (uint8_t) (v >> 8);
    p[3] = (uint8_t) v;
}

static void put_be64(uint8_t *p, uint64_t v)
{
    put_be32(p, (uint32_t) (v >> 32));
    put_be32(p + 4, (uint32_t) v);
}

static uint16_t get_be16(const uint8_t *p)
{
    return (uint16_t) (((uint16_t) p[0] << 8) | p[1]);
}

static uint32_t get_be32(const uint8_t *p)
{
    return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | p[3];
}

static uint64_t get_be64(const uint8_t *p)
{
    return ((uint64_t) get_be32(p) << 32) | get_be32(p + 4);
}

static uint32_t popcount8(uint8_t v)
{
    uint32_t n = 0;
    while (v) {
        n += (uint32_t) (v & 1u);
        v = (uint8_t) (v >> 1);
    }
    return n;
}

void cisex_init(cisex_t *x, cisex_ent_t *ents, uint32_t cap)
{
    if (!x) return;
    x->e = ents;
    x->cap = ents ? cap : 0;
    x->count = 0;
    x->n_indexed = x->n_wants = x->n_obs = x->n_merged = 0;
    for (uint32_t i = 0; i < x->cap; i++) ents[i].used = 0;
}

cisex_ent_t *cisex_find(const cisex_t *x, const uint8_t digest[32])
{
    if (!x || !digest) return 0;
    for (uint32_t i = 0; i < x->cap; i++)
        if (x->e[i].used && c6x_eq(x->e[i].digest, digest, 32)) return &x->e[i];
    return 0;
}

const cisex_ent_t *cisex_at(const cisex_t *x, uint32_t idx)
{
    if (!x || idx >= x->cap || !x->e[idx].used) return 0;
    return &x->e[idx];
}

static cisex_ent_t *ent_get(cisex_t *x, const uint8_t digest[32], bool create, uint8_t codec)
{
    cisex_ent_t *e = cisex_find(x, digest);
    uint32_t i;
    if (e) {
        if (e->codec == 0 && codec) e->codec = codec;
        return e;
    }
    if (!create) return 0;
    for (i = 0; i < x->cap && x->e[i].used; i++)
        ;
    if (i == x->cap) return 0;
    e = &x->e[i];
    c6x_zero(e, sizeof *e);
    e->used = 1;
    c6x_copy(e->digest, digest, 32);
    e->codec = codec;
    x->count++;
    e->lpx = lpx_make(LPX_B_NEITHER, LPX_P_NEUTRAL, LPX_PH_POTENTIAL);
    return e;
}

/* Recompute armada mask, joined base, polarity and walk the phase. `stale`
 * is caller-supplied (the region's own freshness), never sniffed here. */
static void tag_update(cisex_ent_t *e, bool stale)
{
    lpres_state_t joined = LPRES_STATE_NEITHER;
    uint8_t base;
    for (uint32_t r = 0; r < CISEX_REGIONS; r++)
        joined = lpres_kjoin(joined, (lpres_state_t) e->rst[r]);
    e->armada = 0;
    for (uint32_t r = 0; r < CISEX_REGIONS; r++)
        if (e->rst[r] == LPRES_STATE_TRUE) e->armada |= (uint8_t) (1u << r);
    base = lpx_base_from_lpres(joined);
    e->lpx = lpx_set(e->lpx, base,
                     lpx_polarity(e->pos_ev, e->neg_ev, e->pos_ev, e->neg_ev, stale));
    if (lpx_phase(e->lpx) == LPX_PH_POTENTIAL && joined != LPRES_STATE_NEITHER)
        (void) lpx_advance(&e->lpx, LPX_PH_EMERGING);
    if (lpx_phase(e->lpx) == LPX_PH_EMERGING && popcount8(e->armada) >= 2u)
        (void) lpx_advance(&e->lpx, LPX_PH_MANIFEST);
    if (lpx_phase(e->lpx) == LPX_PH_MANIFEST &&
        (joined == LPRES_STATE_FALSE || joined == LPRES_STATE_NEITHER))
        (void) lpx_advance(&e->lpx, LPX_PH_DISSOLVING);
}

int cisex_want(cisex_t *x, const uint8_t digest[32], uint8_t region)
{
    cisex_ent_t *e;
    if (!x || !digest || region >= CISEX_REGIONS) return IPFSN_ERR_ARG;
    e = ent_get(x, digest, true, 0);
    if (!e) return IPFSN_ERR_FULL;
    if (e->wants < 0xFFFFFFFFu) e->wants++;
    x->n_wants++;
    return IPFSN_OK;
}

/* Harvest lowercase alnum tokens (len 3..24) out of a filename into the
 * entry's keyword slots; extension feeds the kind guess. */
static void harvest_name(cisex_ent_t *e, const uint8_t *name, uint32_t len)
{
    uint32_t i = 0;
    while (i < len && e->n_kw < CISEX_KW_MAX) {
        uint8_t tok[CISEX_KW_MAXTK];
        uint32_t tl = 0, j = i;
        while (j < len && !c6x_alnum(name[j])) j++;
        i = j;
        while (j < len && c6x_alnum(name[j]) && tl < CISEX_KW_MAXTK) tok[tl++] = c6x_lower(name[j++]);
        while (j < len && c6x_alnum(name[j])) j++; /* over-long token: skip rest */
        i = j;
        if (tl < CISEX_KW_MIN) continue;
        uint64_t h = cisex_kw_hash((const char *) tok, tl);
        bool dup = false;
        for (uint8_t k = 0; k < e->n_kw; k++) dup |= e->kw[k] == h;
        if (!dup) e->kw[e->n_kw++] = h;
    }
}

static void set_name(cisex_ent_t *e, const uint8_t *name, uint32_t len)
{
    if (e->name_len || !name || !len) return; /* keep the first name seen */
    if (len >= CISEX_NAME_MAX) len = CISEX_NAME_MAX - 1;
    for (uint32_t i = 0; i < len; i++) e->name[i] = (char) name[i];
    e->name[len] = 0;
    e->name_len = (uint8_t) len;
    harvest_name(e, name, len);
}

int cisex_index_dagpb(cisex_t *x, const ipfsn_cid_t *cid, const uint8_t *block, uint32_t len,
                      uint8_t region, uint32_t *n_links)
{
    ipfsn_pbnode_t pb;
    ipfsn_pblink_t l;
    ipfsn_unixfs_t fs;
    cisex_ent_t *parent, *child;
    uint32_t pos = 0, nl = 0;
    int r;

    if (!x || !cid || !block || region >= CISEX_REGIONS) return IPFSN_ERR_ARG;
    (void) region; /* indexing says what exists, not who serves it */
    if (n_links) *n_links = 0;
    if (ipfsn_cid_verify(cid, block, len) != IPFSN_OK) return IPFSN_ERR_HASH;
    if (ipfsn_dagpb_parse(block, len, &pb) != IPFSN_OK) return IPFSN_ERR_MALFORMED;

    parent = ent_get(x, cid->digest, true, (uint8_t) cid->codec);
    if (!parent) return IPFSN_ERR_FULL;

    /* Classify the node itself from its UnixFS Data. */
    if (parent->kind == CISEX_K_UNKNOWN && pb.data && pb.data_len &&
        ipfsn_unixfs_parse(pb.data, pb.data_len, &fs) == IPFSN_OK) {
        if (fs.type == IPFSN_UFS_DIR || fs.type == IPFSN_UFS_HAMT)
            parent->kind = CISEX_K_DIR;
        else if (fs.type == IPFSN_UFS_FILE)
            parent->kind = CISEX_K_FILE;
    }
    if (parent->kind == CISEX_K_UNKNOWN && pb.nlinks == 0) parent->kind = CISEX_K_FILE;

    /* Every link is a citation of the child CID by this DAG. A block already
     * harvested (`crawled`) is not re-counted, so re-scanning a directory is
     * idempotent. */
    bool first = !parent->crawled;
    while ((r = ipfsn_dagpb_next_link(&pb, &pos, &l)) == IPFSN_OK) {
        child = ent_get(x, l.cid.digest, true, (uint8_t) l.cid.codec);
        if (!child) return IPFSN_ERR_FULL;
        if (first) {
            if (child->cites < 0xFFFFFFFFu) child->cites++;
            if (l.has_tsize && child->size == 0) child->size = l.tsize;
        }
        set_name(child, l.name, l.name_len);
        nl++;
    }
    if (r != IPFSN_ERR_NOTFOUND) return IPFSN_ERR_MALFORMED;
    parent->crawled = 1;
    if (lpx_phase(parent->lpx) == LPX_PH_POTENTIAL) (void) lpx_advance(&parent->lpx, LPX_PH_EMERGING);
    x->n_indexed++;
    if (n_links) *n_links = nl;
    return IPFSN_OK;
}

/* Magic-byte and extension classification over the first bytes. */
static uint8_t magic_kind(const uint8_t *h, uint32_t n)
{
    if (n >= 4 && c6x_eq(h, "%PDF", 4)) return CISEX_K_PDF;
    if (n >= 4 && h[0] == 0x7F && c6x_eq(h + 1, "ELF", 3)) return CISEX_K_ELF;
    if (n >= 4 && h[0] == 0x00 && c6x_eq(h + 1, "asm", 3)) return CISEX_K_WASM;
    if (n >= 4 && c6x_eq(h, "GGUF", 4)) return CISEX_K_GGUF;
    if (n >= 16 && c6x_eq(h, "SQLite format 3", 15)) return CISEX_K_SQLITE;
    if (n >= 4 && h[0] == 'P' && h[1] == 'K' && h[2] == 3 && h[3] == 4) return CISEX_K_ZIP;
    if (n >= 2 && h[0] == 0x1F && h[1] == 0x8B) return CISEX_K_GZ;
    if (n >= 8 && h[0] <= 0x38 && h[1] == 0xA2 && h[2] == 0x65 && c6x_eq(h + 3, "roots", 5))
        return CISEX_K_CAR;
    if (n >= 262 && c6x_eq(h + 257, "ustar", 5)) return CISEX_K_TAR;
    if (n >= 4 && h[0] == 0x89 && c6x_eq(h + 1, "PNG", 3)) return CISEX_K_IMG;
    if (n >= 3 && h[0] == 0xFF && h[1] == 0xD8 && h[2] == 0xFF) return CISEX_K_IMG;
    if (n >= 8 && c6x_eq(h, "#include", 8)) return CISEX_K_C;
    if (n >= 2 && h[0] == '#' && h[1] == ' ') return CISEX_K_MD;
    if (n >= 2 && (h[0] == '\\' || (h[0] == '%' && h[1] == '!'))) return CISEX_K_TEX;
    return CISEX_K_UNKNOWN;
}

/* Case-insensitive check that the name ends ".ext". */
static bool ext_eq(const char *name, uint32_t len, const char *ext, uint32_t el)
{
    uint32_t i;
    if (len <= el || name[len - el - 1] != '.') return false;
    for (i = 0; i < el; i++)
        if (c6x_lower((uint8_t) name[len - el + i]) != (uint8_t) ext[i]) return false;
    return true;
}

/* A filename extension guess, used before any bytes are seen. */
static uint8_t ext_kind(const char *name, uint32_t len)
{
    if (ext_eq(name, len, "pdf", 3)) return CISEX_K_PDF;
    if (ext_eq(name, len, "car", 3)) return CISEX_K_CAR;
    if (ext_eq(name, len, "gguf", 4)) return CISEX_K_GGUF;
    if (ext_eq(name, len, "zip", 3)) return CISEX_K_ZIP;
    if (ext_eq(name, len, "gz", 2)) return CISEX_K_GZ;
    if (ext_eq(name, len, "tar", 3)) return CISEX_K_TAR;
    if (ext_eq(name, len, "png", 3)) return CISEX_K_IMG;
    if (ext_eq(name, len, "jpg", 3)) return CISEX_K_IMG;
    if (ext_eq(name, len, "jpeg", 4)) return CISEX_K_IMG;
    if (ext_eq(name, len, "md", 2)) return CISEX_K_MD;
    if (ext_eq(name, len, "markdown", 8)) return CISEX_K_MD;
    if (ext_eq(name, len, "c", 1)) return CISEX_K_C;
    if (ext_eq(name, len, "h", 1)) return CISEX_K_C;
    if (ext_eq(name, len, "tex", 3)) return CISEX_K_TEX;
    if (ext_eq(name, len, "elf", 3)) return CISEX_K_ELF;
    if (ext_eq(name, len, "wasm", 4)) return CISEX_K_WASM;
    if (ext_eq(name, len, "sqlite", 6)) return CISEX_K_SQLITE;
    return CISEX_K_UNKNOWN;
}

int cisex_peek(cisex_t *x, const uint8_t digest[32], const uint8_t *hdr, uint32_t len,
               uint8_t region, uint8_t *kind_out, uint16_t *entropy_out)
{
    cisex_ent_t *e;
    uint8_t k;
    if (!x || !digest || !hdr || len == 0 || len > CISEX_HDR_PEEK || region >= CISEX_REGIONS)
        return IPFSN_ERR_ARG;
    (void) region; /* a header peek classifies bytes; it is not a provider claim */
    e = ent_get(x, digest, true, 0);
    if (!e) return IPFSN_ERR_FULL;
    e->entropy_x1024 = cisex_entropy_x1024(hdr, len);
    k = magic_kind(hdr, len);
    if (e->kind == CISEX_K_UNKNOWN && k != CISEX_K_UNKNOWN) e->kind = k;
    if (e->kind == CISEX_K_UNKNOWN && e->name_len) e->kind = ext_kind(e->name, e->name_len);
    if (lpx_phase(e->lpx) == LPX_PH_POTENTIAL) (void) lpx_advance(&e->lpx, LPX_PH_EMERGING);
    if (kind_out) *kind_out = e->kind;
    if (entropy_out) *entropy_out = e->entropy_x1024;
    return IPFSN_OK;
}

int cisex_observe(cisex_t *x, const uint8_t digest[32], uint8_t region, lpres_state_t st)
{
    cisex_ent_t *e;
    if (!x || !digest || region >= CISEX_REGIONS) return IPFSN_ERR_ARG;
    if (st != LPRES_STATE_TRUE && st != LPRES_STATE_FALSE && st != LPRES_STATE_BOTH)
        return IPFSN_OK; /* NEITHER adds nothing */
    e = ent_get(x, digest, true, 0);
    if (!e) return IPFSN_ERR_FULL;
    e->rst[region] = (uint8_t) lpres_kjoin((lpres_state_t) e->rst[region], st);
    if (st == LPRES_STATE_TRUE || st == LPRES_STATE_BOTH) e->pos_ev++;
    if (st == LPRES_STATE_FALSE || st == LPRES_STATE_BOTH) e->neg_ev++;
    tag_update(e, false);
    x->n_obs++;
    return IPFSN_OK;
}

int cisex_provide(cisex_t *x, const uint8_t digest[32], uint8_t region, const uint8_t addr[6])
{
    cisex_ent_t *e;
    uint32_t i;
    int r;
    if (!x || !digest || !addr || region >= CISEX_REGIONS) return IPFSN_ERR_ARG;
    e = ent_get(x, digest, true, 0);
    if (!e) return IPFSN_ERR_FULL;
    for (i = 0; i < e->n_prov; i++) {
        if (c6x_eq(e->prov[i].addr, addr, 6)) {
            e->prov[i].region = region;
            e->prov[i].state = LPRES_STATE_TRUE;
            goto observed;
        }
    }
    if (e->n_prov < CISEX_PROV_MAX) {
        c6x_copy(e->prov[e->n_prov].addr, addr, 6);
        e->prov[e->n_prov].region = region;
        e->prov[e->n_prov].state = LPRES_STATE_TRUE;
        e->n_prov++;
    }
observed:
    r = cisex_observe(x, digest, region, LPRES_STATE_TRUE);
    return r;
}

uint64_t cisex_rank(const cisex_ent_t *e)
{
    uint64_t r = 0;
    if (!e || !e->used) return 0;
    r += (uint64_t) e->cites * 13u;
    r += (uint64_t) e->wants * 8u;
    r += (uint64_t) popcount8(e->armada) * 5u;
    if (lpx_base(e->lpx) == LPX_B_TRUE) r += 21u;
    return r;
}

uint64_t cisex_kw_hash(const char *tok, uint32_t len)
{
    uint64_t h = 1469598103934665603ULL;
    for (uint32_t i = 0; i < len; i++) {
        h ^= c6x_lower((uint8_t) tok[i]);
        h *= 1099511628211ULL;
    }
    return h;
}

/* Take the leading alnum token of a query, lowercase it, hash it. */
static uint64_t query_hash(const char *kw, uint32_t len)
{
    uint8_t tok[CISEX_KW_MAXTK];
    uint32_t tl = 0;
    while (tl < len && tl < CISEX_KW_MAXTK && c6x_alnum((uint8_t) kw[tl])) {
        tok[tl] = c6x_lower((uint8_t) kw[tl]);
        tl++;
    }
    return tl ? cisex_kw_hash((const char *) tok, tl) : 0;
}

/* Insertion into a rank-descending top-cap index list. */
static void top_insert(uint32_t *out, uint64_t *rk, uint32_t *n, uint32_t cap, uint32_t idx,
                       uint64_t rank)
{
    uint32_t i = *n, j;
    if (i == cap) {
        if (cap == 0 || rank <= rk[cap - 1]) return;
        i = cap - 1;
    } else {
        (*n)++;
    }
    for (j = i; j > 0 && rk[j - 1] < rank; j--) {
        rk[j] = rk[j - 1];
        out[j] = out[j - 1];
    }
    rk[j] = rank;
    out[j] = idx;
}

uint32_t cisex_search(const cisex_t *x, const char *kw, uint32_t kw_len, uint32_t *out_idx,
                      uint32_t cap)
{
    uint64_t h, rk[32];
    uint32_t n = 0, max = cap < 32u ? cap : 32u;
    if (!x || !kw || !kw_len || !out_idx) return 0;
    h = query_hash(kw, kw_len);
    if (!h) return 0;
    for (uint32_t i = 0; i < x->cap; i++) {
        const cisex_ent_t *e = &x->e[i];
        bool m = false;
        if (!e->used) continue;
        for (uint8_t k = 0; k < e->n_kw; k++) m |= e->kw[k] == h;
        if (m) top_insert(out_idx, rk, &n, max, i, cisex_rank(e));
    }
    return n;
}

uint32_t cisex_bounty(const cisex_t *x, uint32_t *out_idx, uint32_t cap)
{
    uint64_t rk[32];
    uint32_t n = 0, max = cap < 32u ? cap : 32u;
    if (!x || !out_idx) return 0;
    for (uint32_t i = 0; i < x->cap; i++) {
        const cisex_ent_t *e = &x->e[i];
        bool stale;
        if (!e->used || e->cites == 0) continue;
        /* Cited but not healthy: nobody verified, joined not TRUE, or the
         * evidence trend is entropic (fading provider). */
        stale = e->armada == 0 || lpx_base(e->lpx) != LPX_B_TRUE ||
                lpx_pol(e->lpx) == LPX_P_ENTROPIC;
        if (stale) top_insert(out_idx, rk, &n, max, i, cisex_rank(e));
    }
    return n;
}

/* ---- fixed-point Shannon entropy (x1024 bits) ----
 * log2(n) ~ floor + linear interpolation between powers of two: exact at
 * powers of two, at most ~0.086 bit low elsewhere. n <= 4096 so every
 * intermediate fits in u32; only the final /n is 32-bit. */
static uint32_t lg2_x1024(uint32_t n)
{
    uint32_t f = 0, base, frac;
    if (n <= 1) return 0;
    while ((n >> f) > 1u) f++;
    base = 1u << f;
    frac = ((n - base) << 10) >> f;
    return (f << 10) + frac;
}

uint16_t cisex_entropy_x1024(const uint8_t *d, uint32_t n)
{
    uint32_t cnt[256];
    uint64_t s = 0;
    uint32_t e;
    if (!d || !n) return 0;
    c6x_zero(cnt, sizeof cnt);
    for (uint32_t i = 0; i < n; i++) cnt[d[i]]++;
    for (uint32_t b = 0; b < 256; b++)
        if (cnt[b]) s += (uint64_t) cnt[b] * lg2_x1024(cnt[b]);
    e = n * lg2_x1024(n) - (uint32_t) s;
    return (uint16_t) (e / n);
}

/* ---- the 168-byte UBH-168 index envelope ---- */
int cisex_pack_idx(const cisex_ent_t *e, uint8_t region, uint8_t out[CISEX_ENV_BYTES])
{
    ubh_168_header_t h;
    uint8_t d[SHA256_DIGEST_LEN];
    uint8_t *pl;
    uint32_t i;

    if (!e || !e->used || !out || region >= CISEX_REGIONS) return IPFSN_ERR_ARG;
    if (e->lpx >= LPX_STATES) return IPFSN_ERR_ARG;

    ubh_168_header_init(&h, UBH_FRAME_CONTENT_OBJECT);
    h.source_format_id = UBH_FMT_UBH_168;
    h.target_type = CISEX_T_INDEX;
    h.flags = 0x05u; /* has_payload | canonical */
    h.payload_length = CISEX_PAYLOAD;
    h.schema_id = CISEX_SCHEMA;
    for (i = 0; i < 3; i++) h.integrity_ref[i] = e->digest[i];
    ubh_168_header_pack(&h, out);

    pl = out + CISEX_FRAME;
    c6x_zero(pl, CISEX_PAYLOAD);
    c6x_copy(pl + 0, e->digest, 32);
    c6x_copy(pl + 32, e->name, CISEX_NAME_MAX);
    for (i = 0; i < CISEX_KW_MAX; i++) put_be64(pl + 64 + 8 * i, e->kw[i]);
    put_be64(pl + 88, e->size);
    put_be32(pl + 96, e->cites);
    put_be32(pl + 100, e->wants);
    if (e->n_prov) c6x_copy(pl + 104, e->prov[0].addr, 6);
    pl[110] = e->n_prov;
    pl[111] = e->kind;
    pl[112] = e->lpx;
    pl[113] = e->armada;
    pl[114] = region;
    pl[115] = e->codec;
    put_be16(pl + 116, e->entropy_x1024);

    sha256(out, CISEX_FRAME + CISEX_PAYLOAD, d);
    ubh_168_header_init(&h, UBH_FRAME_INTEGRITY);
    h.source_format_id = UBH_FMT_UBH_168;
    h.target_type = CISEX_T_INDEX;
    h.payload_length = CISEX_PAYLOAD;
    h.schema_id = CISEX_PAYLOAD / CISEX_FRAME; /* 6 frames */
    for (i = 0; i < 3; i++) h.integrity_ref[i] = d[i];
    ubh_168_header_pack(&h, out + CISEX_FRAME + CISEX_PAYLOAD);
    return IPFSN_OK;
}

int cisex_unpack_idx(const uint8_t in[CISEX_ENV_BYTES], cisex_wire_t *out)
{
    ubh_168_header_t h, ref, t;
    uint8_t d[SHA256_DIGEST_LEN];
    const uint8_t *pl;
    uint32_t i;

    if (!in || !out) return IPFSN_ERR_ARG;
    ubh_168_header_init(&ref, UBH_FRAME_CONTENT_OBJECT);
    if (!ubh_168_header_unpack(&h, in)) return IPFSN_ERR_MALFORMED;
    if (h.version != ref.version || h.frame_class != UBH_FRAME_CONTENT_OBJECT ||
        h.source_format_id != UBH_FMT_UBH_168 || h.target_type != CISEX_T_INDEX ||
        h.order_flags != ref.order_flags || h.flags != 0x05u ||
        h.payload_length != CISEX_PAYLOAD || h.schema_id != CISEX_SCHEMA)
        return IPFSN_ERR_MALFORMED;

    if (!ubh_168_header_unpack(&t, in + CISEX_FRAME + CISEX_PAYLOAD))
        return IPFSN_ERR_MALFORMED;
    if (t.version != ref.version || t.frame_class != UBH_FRAME_INTEGRITY ||
        t.source_format_id != UBH_FMT_UBH_168 || t.target_type != h.target_type ||
        t.order_flags != ref.order_flags || t.flags != ref.flags ||
        t.payload_length != CISEX_PAYLOAD || t.schema_id != CISEX_PAYLOAD / CISEX_FRAME)
        return IPFSN_ERR_MALFORMED;

    sha256(in, CISEX_FRAME + CISEX_PAYLOAD, d);
    if (!c6x_eq(t.integrity_ref, d, 3)) return IPFSN_ERR_MALFORMED;

    pl = in + CISEX_FRAME;
    if (!c6x_eq(h.integrity_ref, pl, 3)) return IPFSN_ERR_MALFORMED;
    if (pl[112] >= LPX_STATES || pl[114] >= CISEX_REGIONS || pl[110] > CISEX_PROV_MAX)
        return IPFSN_ERR_MALFORMED;

    c6x_zero(out, sizeof *out);
    c6x_copy(out->digest, pl + 0, 32);
    c6x_copy(out->name, pl + 32, CISEX_NAME_MAX);
    out->name[CISEX_NAME_MAX - 1] = 0;
    for (i = 0; i < CISEX_NAME_MAX && out->name[i]; i++) out->name_len = (uint8_t) (i + 1);
    for (i = 0; i < CISEX_KW_MAX; i++) out->kw[i] = get_be64(pl + 64 + 8 * i);
    out->size = get_be64(pl + 88);
    out->cites = get_be32(pl + 96);
    out->wants = get_be32(pl + 100);
    c6x_copy(out->addr, pl + 104, 6);
    out->n_prov = pl[110];
    out->kind = pl[111];
    out->lpx = pl[112];
    out->armada = pl[113];
    out->region = pl[114];
    out->codec = pl[115];
    out->entropy_x1024 = get_be16(pl + 116);
    return IPFSN_OK;
}

int cisex_merge(cisex_t *x, const uint8_t in[CISEX_ENV_BYTES])
{
    cisex_wire_t w;
    cisex_ent_t *e;
    lpres_state_t sender_view;
    int r;

    r = cisex_unpack_idx(in, &w);
    if (r != IPFSN_OK) return r;
    if (!x) return IPFSN_ERR_ARG;
    e = ent_get(x, w.digest, true, w.codec);
    if (!e) return IPFSN_ERR_FULL;

    /* Every region the sender verifies is kjoin'ed TRUE locally; the
     * sender's own joined base is kjoin'ed into its region slot. Negative
     * evidence stays region-scoped: a mask cannot express it. */
    for (uint32_t i = 0; i < CISEX_REGIONS; i++)
        if (w.armada & (1u << i)) e->rst[i] = (uint8_t) lpres_kjoin((lpres_state_t) e->rst[i],
                                                                   LPRES_STATE_TRUE);
    sender_view = lpx_base_to_lpres(lpx_base(w.lpx));
    e->rst[w.region] =
        (uint8_t) lpres_kjoin((lpres_state_t) e->rst[w.region], sender_view);

    if (w.cites > e->cites) e->cites = w.cites;
    if (w.wants > e->wants) e->wants = w.wants;
    if (w.size > e->size) e->size = w.size;
    if (e->kind == CISEX_K_UNKNOWN && w.kind != CISEX_K_UNKNOWN) e->kind = w.kind;
    if (!e->entropy_x1024 && w.entropy_x1024) e->entropy_x1024 = w.entropy_x1024;
    set_name(e, (const uint8_t *) w.name, w.name_len);
    for (uint8_t k = 0; k < CISEX_KW_MAX && e->n_kw < CISEX_KW_MAX; k++) {
        bool dup = false;
        if (!w.kw[k]) continue;
        for (uint8_t j = 0; j < e->n_kw; j++) dup |= e->kw[j] == w.kw[k];
        if (!dup) e->kw[e->n_kw++] = w.kw[k];
    }
    if (w.n_prov && e->n_prov < CISEX_PROV_MAX) {
        bool have = false;
        for (uint8_t i = 0; i < e->n_prov; i++) have |= c6x_eq(e->prov[i].addr, w.addr, 6);
        if (!have) {
            c6x_copy(e->prov[e->n_prov].addr, w.addr, 6);
            e->prov[e->n_prov].region = w.region;
            e->prov[e->n_prov].state = (uint8_t) sender_view;
            e->n_prov++;
        }
    }
    tag_update(e, false);
    x->n_merged++;
    return IPFSN_OK;
}
