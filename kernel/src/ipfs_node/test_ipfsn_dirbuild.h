/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* test_ipfsn_dirbuild.h — TEST-ONLY helpers: an in-memory block store and a
 * small UnixFS directory builder (plain and HAMT, fanout 256) that writes the
 * same bytes Kubo 0.32.1 writes. Hosted C (malloc, qsort). Never linked into
 * the kernel: the kernel only READS directories (ipfsn_dir.c).
 *
 * The builder is trusted only because test_ipfsn_dir.c checks the root CIDs
 * it produces against Kubo's for a plain directory tree, a HAMT at a lowered
 * threshold, and a HAMT at Kubo's default threshold.
 *
 * HONEST LIMITS: no mode/mtime, no symlinks, fanout fixed at 256, and the
 * caller decides plain vs HAMT (Kubo decides by its size estimate).
 */
#ifndef TEST_IPFSN_DIRBUILD_H
#define TEST_IPFSN_DIRBUILD_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ipfs_node.h"
#include "ipfsn_dir.h"

#pragma GCC diagnostic ignored "-Wunused-function"

typedef struct {
    ipfsn_cid_t cid;
    uint8_t *data;
    uint32_t len;
} tb_block_t;

typedef struct {
    tb_block_t *b;
    uint32_t n, cap;
    uint32_t gets; /* calls to tb_get, for spies */
} tb_store_t;

static void tb_init(tb_store_t *s)
{
    memset(s, 0, sizeof *s);
}

static void tb_free(tb_store_t *s)
{
    for (uint32_t i = 0; i < s->n; i++) free(s->b[i].data);
    free(s->b);
    memset(s, 0, sizeof *s);
}

static tb_block_t *tb_find(tb_store_t *s, const ipfsn_cid_t *cid)
{
    ipfsn_cid_t a, b;
    ipfsn_cid_to_v1(cid, &a);
    for (uint32_t i = 0; i < s->n; i++) {
        ipfsn_cid_to_v1(&s->b[i].cid, &b);
        if (ipfsn_cid_equal(&a, &b)) return &s->b[i];
    }
    return NULL;
}

static void tb_put(tb_store_t *s, const ipfsn_cid_t *cid, const uint8_t *data, uint32_t len)
{
    if (tb_find(s, cid)) return;
    if (s->n == s->cap) {
        s->cap = s->cap ? s->cap * 2 : 64;
        s->b = (tb_block_t *) realloc(s->b, s->cap * sizeof(tb_block_t));
    }
    s->b[s->n].cid = *cid;
    s->b[s->n].data = (uint8_t *) malloc(len ? len : 1);
    if (len) memcpy(s->b[s->n].data, data, len);
    s->b[s->n].len = len;
    s->n++;
}

/* ipfsn_get_fn over the store (ctx = tb_store_t *). Does not verify: callers must. */
static int tb_get(void *ctx, const ipfsn_cid_t *cid, uint8_t *buf, uint32_t cap, uint32_t *len)
{
    tb_store_t *s = (tb_store_t *) ctx;
    tb_block_t *b;
    s->gets++;
    b = tb_find(s, cid);
    if (!b) return IPFSN_ERR_NOTFOUND;
    if (b->len > cap) return IPFSN_ERR_SPACE;
    memcpy(buf, b->data, b->len);
    *len = b->len;
    return IPFSN_OK;
}

/* ---- protobuf writer ---- */

typedef struct {
    uint8_t *p;
    uint32_t n, cap;
} tb_pb_t;

static void tb_byte(tb_pb_t *w, uint8_t c)
{
    if (w->n == w->cap) {
        w->cap = w->cap ? w->cap * 2 : 1024;
        w->p = (uint8_t *) realloc(w->p, w->cap);
    }
    w->p[w->n++] = c;
}

static void tb_varint(tb_pb_t *w, uint64_t v)
{
    while (v >= 0x80) {
        tb_byte(w, (uint8_t) (v | 0x80));
        v >>= 7;
    }
    tb_byte(w, (uint8_t) v);
}

static void tb_raw(tb_pb_t *w, const uint8_t *b, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) tb_byte(w, b[i]);
}

typedef struct {
    char name[260];
    uint32_t name_len;
    ipfsn_cid_t cid;
    uint64_t tsize;
} tb_ent_t;

static void tb_link(tb_pb_t *w, const ipfsn_cid_t *cid, const uint8_t *name, uint32_t nlen,
                    uint64_t tsize)
{
    uint8_t cb[IPFSN_CID_BIN_MAX];
    tb_pb_t l = {0, 0, 0};
    int cl = ipfsn_cid_encode(cid, cb, sizeof cb);
    tb_varint(&l, (1u << 3) | 2u);
    tb_varint(&l, (uint32_t) cl);
    tb_raw(&l, cb, (uint32_t) cl);
    tb_varint(&l, (2u << 3) | 2u);
    tb_varint(&l, nlen);
    tb_raw(&l, name, nlen);
    tb_varint(&l, (3u << 3) | 0u);
    tb_varint(&l, tsize);
    tb_varint(w, (2u << 3) | 2u);
    tb_varint(w, l.n);
    tb_raw(w, l.p, l.n);
    free(l.p);
}

/* Finish a node: Data, CID, store; *tsize = node bytes + sum of link Tsizes. */
static void tb_finish(tb_store_t *s, tb_pb_t *w, const uint8_t *data, uint32_t dlen,
                      uint64_t link_sum, ipfsn_cid_t *cid, uint64_t *tsize)
{
    tb_varint(w, (1u << 3) | 2u);
    tb_varint(w, dlen);
    tb_raw(w, data, dlen);
    ipfsn_cid_sha256(IPFSN_MC_DAG_PB, w->p, w->n, cid);
    tb_put(s, cid, w->p, w->n);
    if (tsize) *tsize = link_sum + w->n;
    free(w->p);
    w->p = 0;
}

/* ---- files: Kubo defaults (256 KiB, raw leaves, 174 links) ---- */

typedef struct {
    tb_store_t *s;
    uint64_t total;
} tb_sink_t;

static int tb_sink(void *ctx, const ipfsn_cid_t *cid, const uint8_t *block, uint32_t len)
{
    tb_sink_t *k = (tb_sink_t *) ctx;
    tb_put(k->s, cid, block, len);
    k->total += len;
    return 0;
}

/* chunk 0 = Kubo default; other chunk sizes give multi-block DAGs cheaply. */
static void tb_file_chunked(tb_store_t *s, const uint8_t *data, uint32_t len, uint32_t chunk,
                            ipfsn_cid_t *cid, uint64_t *tsize)
{
    static ipfsn_ufs_t u;
    static uint8_t cbuf[IPFSN_UFS_CHUNK];
    tb_sink_t k = {s, 0};
    uint64_t fs;
    ipfsn_ufs_init(&u, cbuf, chunk, 0, tb_sink, &k);
    ipfsn_ufs_update(&u, data, len);
    ipfsn_ufs_final(&u, cid, &fs);
    if (tsize) *tsize = k.total;
}

static void tb_file(tb_store_t *s, const uint8_t *data, uint32_t len, ipfsn_cid_t *cid,
                    uint64_t *tsize)
{
    tb_file_chunked(s, data, len, 0, cid, tsize);
}

static int tb_ent_cmp(const void *a, const void *b)
{
    const tb_ent_t *x = (const tb_ent_t *) a, *y = (const tb_ent_t *) b;
    uint32_t n = x->name_len < y->name_len ? x->name_len : y->name_len;
    int c = memcmp(x->name, y->name, n);
    if (c) return c;
    return x->name_len < y->name_len ? -1 : (x->name_len > y->name_len);
}

static void tb_ent(tb_ent_t *e, const char *name, const ipfsn_cid_t *cid, uint64_t tsize)
{
    memset(e, 0, sizeof *e);
    e->name_len = (uint32_t) strlen(name);
    memcpy(e->name, name, e->name_len);
    e->cid = *cid;
    e->tsize = tsize;
}

/* Plain directory: links sorted by name bytes, Data = UnixFS{Type=Directory}. */
static void tb_dir(tb_store_t *s, tb_ent_t *ents, uint32_t n, ipfsn_cid_t *cid, uint64_t *tsize)
{
    static const uint8_t dd[2] = {0x08, 0x01};
    tb_pb_t w = {0, 0, 0};
    uint64_t sum = 0;
    qsort(ents, n, sizeof *ents, tb_ent_cmp);
    for (uint32_t i = 0; i < n; i++) {
        tb_link(&w, &ents[i].cid, (const uint8_t *) ents[i].name, ents[i].name_len, ents[i].tsize);
        sum += ents[i].tsize;
    }
    tb_finish(s, &w, dd, 2, sum, cid, tsize);
}

/* HAMT shard over ents[idx[0..n)] at `depth`. */
static void tb_shard(tb_store_t *s, tb_ent_t *ents, uint32_t *idx, uint32_t n, uint32_t depth,
                     ipfsn_cid_t *cid, uint64_t *tsize)
{
    uint8_t bf[32];
    uint8_t data[64];
    uint32_t dl = 0, bfs = 0;
    uint32_t *slot = (uint32_t *) malloc((n ? n : 1) * sizeof(uint32_t));
    uint32_t *sub = (uint32_t *) malloc((n ? n : 1) * sizeof(uint32_t));
    tb_pb_t w = {0, 0, 0};
    uint64_t sum = 0;
    memset(bf, 0, sizeof bf);
    for (uint32_t i = 0; i < n; i++) {
        uint64_t h = ipfsn_murmur3_64((const uint8_t *) ents[idx[i]].name, ents[idx[i]].name_len);
        slot[i] = (uint32_t) ((h << (8 * depth)) >> 56);
    }
    for (uint32_t sl = 0; sl < 256; sl++) {
        uint32_t k = 0;
        char pre[3];
        for (uint32_t i = 0; i < n; i++)
            if (slot[i] == sl) sub[k++] = idx[i];
        if (!k) continue;
        bf[31 - sl / 8] |= (uint8_t) (1u << (sl % 8));
        snprintf(pre, sizeof pre, "%02X", sl);
        if (k == 1) {
            uint8_t nm[260];
            tb_ent_t *e = &ents[sub[0]];
            memcpy(nm, pre, 2);
            memcpy(nm + 2, e->name, e->name_len);
            tb_link(&w, &e->cid, nm, 2 + e->name_len, e->tsize);
            sum += e->tsize;
        } else {
            ipfsn_cid_t c;
            uint64_t ts;
            tb_shard(s, ents, sub, k, depth + 1, &c, &ts);
            tb_link(&w, &c, (const uint8_t *) pre, 2, ts);
            sum += ts;
        }
    }
    /* UnixFS{Type=HAMTShard, Data=bitfield (leading zero bytes stripped), hashType, fanout} */
    while (bfs < 32 && bf[bfs] == 0) bfs++;
    data[dl++] = 0x08;
    data[dl++] = 0x05;
    if (bfs < 32) {
        data[dl++] = 0x12;
        data[dl++] = (uint8_t) (32 - bfs);
        memcpy(data + dl, bf + bfs, 32 - bfs);
        dl += 32 - bfs;
    }
    data[dl++] = 0x28;
    data[dl++] = 0x22;
    data[dl++] = 0x30;
    data[dl++] = 0x80;
    data[dl++] = 0x02;
    tb_finish(s, &w, data, dl, sum, cid, tsize);
    free(slot);
    free(sub);
}

static void tb_hamt(tb_store_t *s, tb_ent_t *ents, uint32_t n, ipfsn_cid_t *cid, uint64_t *tsize)
{
    uint32_t *idx = (uint32_t *) malloc((n ? n : 1) * sizeof(uint32_t));
    for (uint32_t i = 0; i < n; i++) idx[i] = i;
    tb_shard(s, ents, idx, n, 0, cid, tsize);
    free(idx);
}

/* A CAR of every block in the store, rooted at `root`. Caller frees *out. */
static uint32_t tb_car(tb_store_t *s, const ipfsn_cid_t *root, uint8_t **out)
{
    uint32_t cap = 1024, n;
    int r;
    for (uint32_t i = 0; i < s->n; i++) cap += s->b[i].len + 128;
    *out = (uint8_t *) malloc(cap);
    r = ipfsn_car_begin(root, *out, cap);
    if (r < 0) return 0;
    n = (uint32_t) r;
    for (uint32_t i = 0; i < s->n; i++) {
        r = ipfsn_car_put(&s->b[i].cid, s->b[i].data, s->b[i].len, *out + n, cap - n);
        if (r < 0) return 0;
        n += (uint32_t) r;
    }
    return n;
}

#endif /* TEST_IPFSN_DIRBUILD_H */
