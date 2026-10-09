/* ipfsn_unixfs.c — UnixFS files over dag-pb, built the way Kubo builds them,
 * and read back with every size and hash checked.
 *
 * KUBO COMPATIBILITY (`ipfs add --cid-version=1`, defaults)
 * ---------------------------------------------------------
 *  - chunker size-262144: fixed 256 KiB chunks, the last one short;
 *  - raw leaves (implied by --cid-version=1): each chunk is a raw (0x55) block,
 *    and a file of at most one chunk IS that raw block (no dag-pb wrapper);
 *  - balanced layout, at most 174 links per node: a node that is full and
 *    receives one more link is closed and becomes the first child of the
 *    level above, so every leaf sits at the same depth;
 *  - each dag-pb node is Links then Data; a link is Hash, an empty Name and
 *    Tsize (the bytes of the whole sub-DAG); Data is UnixFS{Type=File,
 *    filesize, blocksizes...}.
 * The streaming builder holds one pending node per level and one chunk, so
 * memory is bounded by IPFSN_DAG_MAX_DEPTH * IPFSN_UFS_LINKS links.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#include "ipfs_node.h"
#include "ipfsn_util.h"

/* ---- protobuf writer ------------------------------------------------------ */

typedef struct {
    uint8_t *p;
    uint32_t cap, n;
    bool bad;
} pbw_t;

static void pbw_varint(pbw_t *w, uint64_t v)
{
    uint32_t k = ipfsn_varint_put(v, w->p + w->n, w->cap - w->n);
    if (!k) w->bad = true;
    w->n += k;
}

static void pbw_bytes(pbw_t *w, const uint8_t *b, uint32_t len)
{
    if (w->cap - w->n < len) {
        w->bad = true;
        return;
    }
    ipfsn__cpy(w->p + w->n, b, len);
    w->n += len;
}

static uint32_t varint_len(uint64_t v)
{
    uint32_t n = 1;
    while (v >= 0x80u) {
        v >>= 7;
        n++;
    }
    return n;
}

/* ---- builder --------------------------------------------------------------- */

int ipfsn_ufs_init(ipfsn_ufs_t *u, uint8_t *chunk_buf, uint32_t chunk_size, uint32_t max_links,
                   ipfsn_block_sink_fn sink, void *sink_ctx)
{
    if (!u || !chunk_buf || !sink) return IPFSN_ERR_ARG;
    if (!chunk_size) chunk_size = IPFSN_UFS_CHUNK;
    if (!max_links) max_links = IPFSN_UFS_LINKS;
    if (chunk_size > IPFSN_BLOCK_MAX || max_links < 2 || max_links > IPFSN_UFS_LINKS)
        return IPFSN_ERR_ARG;
    u->chunk = chunk_buf;
    u->chunk_size = chunk_size;
    u->fill = 0;
    u->max_links = max_links;
    u->sink = sink;
    u->sink_ctx = sink_ctx;
    for (uint32_t i = 0; i < IPFSN_DAG_MAX_DEPTH; i++) u->nlinks[i] = 0;
    u->top = 0;
    u->total = 0;
    u->leaves = 0;
    u->err = IPFSN_OK;
    return IPFSN_OK;
}

static int add_link(ipfsn_ufs_t *u, uint32_t lvl, const ipfsn_ufs_link_t *l);

/* Close level `lvl` into one dag-pb node; its link goes to *out. */
static int build_node(ipfsn_ufs_t *u, uint32_t lvl, ipfsn_ufs_link_t *out)
{
    pbw_t w = {u->node, IPFSN_NODE_MAX, 0, false};
    uint8_t cb[IPFSN_CID_BIN_MAX];
    uint64_t fsize = 0, tsize = 0;
    uint32_t n = u->nlinks[lvl], dlen;
    for (uint32_t i = 0; i < n; i++) {
        const ipfsn_ufs_link_t *l = &u->level[lvl][i];
        int cl = ipfsn_cid_encode(&l->cid, cb, sizeof cb);
        if (cl < 0) return cl;
        uint32_t ll = 1 + varint_len((uint32_t) cl) + (uint32_t) cl + 2 + 1 + varint_len(l->tsize);
        pbw_varint(&w, (2u << 3) | 2u); /* PBNode.Links */
        pbw_varint(&w, ll);
        pbw_varint(&w, (1u << 3) | 2u); /* PBLink.Hash */
        pbw_varint(&w, (uint32_t) cl);
        pbw_bytes(&w, cb, (uint32_t) cl);
        pbw_varint(&w, (2u << 3) | 2u); /* PBLink.Name = "" */
        pbw_varint(&w, 0);
        pbw_varint(&w, (3u << 3) | 0u); /* PBLink.Tsize */
        pbw_varint(&w, l->tsize);
        fsize += l->fsize;
        tsize += l->tsize;
    }
    dlen = 2 + 1 + varint_len(fsize);
    for (uint32_t i = 0; i < n; i++) dlen += 1 + varint_len(u->level[lvl][i].fsize);
    pbw_varint(&w, (1u << 3) | 2u); /* PBNode.Data */
    pbw_varint(&w, dlen);
    pbw_varint(&w, (1u << 3) | 0u); /* UnixFS.Type = File */
    pbw_varint(&w, IPFSN_UFS_FILE);
    pbw_varint(&w, (3u << 3) | 0u); /* UnixFS.filesize */
    pbw_varint(&w, fsize);
    for (uint32_t i = 0; i < n; i++) {
        pbw_varint(&w, (4u << 3) | 0u); /* UnixFS.blocksizes (repeated, unpacked) */
        pbw_varint(&w, u->level[lvl][i].fsize);
    }
    if (w.bad) return IPFSN_ERR_SPACE;
    ipfsn_cid_sha256(IPFSN_MC_DAG_PB, u->node, w.n, &out->cid);
    out->tsize = tsize + w.n;
    out->fsize = fsize;
    u->nlinks[lvl] = 0;
    if (u->sink(u->sink_ctx, &out->cid, u->node, w.n) != 0) return IPFSN_ERR_IO;
    return IPFSN_OK;
}

static int add_link(ipfsn_ufs_t *u, uint32_t lvl, const ipfsn_ufs_link_t *l)
{
    if (lvl >= IPFSN_DAG_MAX_DEPTH) return IPFSN_ERR_DEPTH;
    if (u->nlinks[lvl] == u->max_links) { /* full and one more arrives: close it */
        ipfsn_ufs_link_t up;
        int r = build_node(u, lvl, &up);
        if (r) return r;
        if ((r = add_link(u, lvl + 1, &up)) != 0) return r;
    }
    ipfsn__cpy(&u->level[lvl][u->nlinks[lvl]++], l, (uint32_t) sizeof(*l));
    if (lvl > u->top) u->top = lvl;
    return IPFSN_OK;
}

static int emit_leaf(ipfsn_ufs_t *u)
{
    ipfsn_ufs_link_t l;
    ipfsn_cid_sha256(IPFSN_MC_RAW, u->chunk, u->fill, &l.cid);
    l.tsize = u->fill;
    l.fsize = u->fill;
    if (u->sink(u->sink_ctx, &l.cid, u->chunk, u->fill) != 0) return IPFSN_ERR_IO;
    u->fill = 0;
    u->leaves++;
    return add_link(u, 0, &l);
}

int ipfsn_ufs_update(ipfsn_ufs_t *u, const uint8_t *data, uint32_t len)
{
    if (!u || (!data && len)) return IPFSN_ERR_ARG;
    if (u->err) return u->err;
    while (len) {
        uint32_t room = u->chunk_size - u->fill, take = len < room ? len : room;
        ipfsn__cpy(u->chunk + u->fill, data, take);
        u->fill += take;
        u->total += take;
        data += take;
        len -= take;
        if (u->fill == u->chunk_size && (u->err = emit_leaf(u)) != 0) return u->err;
    }
    return IPFSN_OK;
}

int ipfsn_ufs_final(ipfsn_ufs_t *u, ipfsn_cid_t *root, uint64_t *file_size)
{
    ipfsn_ufs_link_t l;
    int r;
    if (!u || !root) return IPFSN_ERR_ARG;
    if (u->err) return u->err;
    if ((u->fill || u->leaves == 0) && (r = emit_leaf(u)) != 0) return u->err = r;
    if (u->top == 0 && u->nlinks[0] == 1) { /* one chunk: the raw leaf is the file */
        ipfsn__cpy(&l, &u->level[0][0], (uint32_t) sizeof(l));
    } else {
        for (uint32_t i = 0; i < u->top; i++) {
            if (!u->nlinks[i]) continue;
            if ((r = build_node(u, i, &l)) != 0) return u->err = r;
            if ((r = add_link(u, i + 1, &l)) != 0) return u->err = r;
        }
        if ((r = build_node(u, u->top, &l)) != 0) return u->err = r;
    }
    ipfsn__cid_copy(root, &l.cid);
    if (file_size) *file_size = u->total;
    u->err = IPFSN_ERR_ARG; /* finished: further use is an error */
    return IPFSN_OK;
}

/* ---- protobuf reader -------------------------------------------------------- */

static int rd_varint(const uint8_t *p, uint32_t len, uint32_t *pos, uint64_t *v)
{
    int k = ipfsn_varint_get(p + *pos, len - *pos, v);
    if (k < 0) return IPFSN_ERR_MALFORMED;
    *pos += (uint32_t) k;
    return IPFSN_OK;
}

static int rd_tag(const uint8_t *p, uint32_t len, uint32_t *pos, uint32_t *field, uint32_t *wt)
{
    uint64_t t;
    if (rd_varint(p, len, pos, &t)) return IPFSN_ERR_MALFORMED;
    if (t > 0xFFFFFFFFu || (t >> 3) == 0) return IPFSN_ERR_MALFORMED;
    *field = (uint32_t) (t >> 3);
    *wt = (uint32_t) (t & 7u);
    return IPFSN_OK;
}

static int rd_bytes(const uint8_t *p, uint32_t len, uint32_t *pos, const uint8_t **b,
                    uint32_t *blen)
{
    uint64_t l;
    if (rd_varint(p, len, pos, &l)) return IPFSN_ERR_MALFORMED;
    if (l > len - *pos) return IPFSN_ERR_MALFORMED;
    *b = p + *pos;
    *blen = (uint32_t) l;
    *pos += (uint32_t) l;
    return IPFSN_OK;
}

static int parse_link(const uint8_t *p, uint32_t len, ipfsn_pblink_t *l)
{
    uint32_t pos = 0, last = 0, f, wt, blen;
    const uint8_t *b;
    bool have_hash = false;
    ipfsn__set(l, 0, (uint32_t) sizeof(*l));
    while (pos < len) {
        if (rd_tag(p, len, &pos, &f, &wt)) return IPFSN_ERR_MALFORMED;
        if (f <= last) return IPFSN_ERR_MALFORMED; /* out of order or duplicate */
        last = f;
        if (f == 1 && wt == 2) {
            if (rd_bytes(p, len, &pos, &b, &blen)) return IPFSN_ERR_MALFORMED;
            if (ipfsn_cid_decode_exact(b, blen, &l->cid)) return IPFSN_ERR_MALFORMED;
            have_hash = true;
        } else if (f == 2 && wt == 2) {
            if (rd_bytes(p, len, &pos, &l->name, &l->name_len)) return IPFSN_ERR_MALFORMED;
        } else if (f == 3 && wt == 0) {
            if (rd_varint(p, len, &pos, &l->tsize)) return IPFSN_ERR_MALFORMED;
            l->has_tsize = true;
        } else {
            return IPFSN_ERR_MALFORMED;
        }
    }
    return have_hash ? IPFSN_OK : IPFSN_ERR_MALFORMED;
}

int ipfsn_dagpb_parse(const uint8_t *block, uint32_t len, ipfsn_pbnode_t *n)
{
    uint32_t pos = 0, f, wt, blen;
    const uint8_t *b;
    ipfsn_pblink_t l;
    if (!block || !n) return IPFSN_ERR_ARG;
    ipfsn__set(n, 0, (uint32_t) sizeof(*n));
    n->block = block;
    n->len = len;
    while (pos < len) {
        uint32_t at = pos;
        if (rd_tag(block, len, &pos, &f, &wt)) return IPFSN_ERR_MALFORMED;
        if (f == 2 && wt == 2) {
            if (n->data) return IPFSN_ERR_MALFORMED; /* Links after Data */
            if (rd_bytes(block, len, &pos, &b, &blen)) return IPFSN_ERR_MALFORMED;
            if (parse_link(b, blen, &l)) return IPFSN_ERR_MALFORMED;
            n->nlinks++;
        } else if (f == 1 && wt == 2) {
            if (n->data) return IPFSN_ERR_MALFORMED; /* duplicate Data */
            if (rd_bytes(block, len, &pos, &n->data, &n->data_len)) return IPFSN_ERR_MALFORMED;
            n->links_end = at;
        } else {
            return IPFSN_ERR_MALFORMED;
        }
    }
    if (!n->data) n->links_end = len;
    return IPFSN_OK;
}

int ipfsn_dagpb_next_link(const ipfsn_pbnode_t *n, uint32_t *pos, ipfsn_pblink_t *l)
{
    uint32_t f, wt, blen;
    const uint8_t *b;
    if (!n || !pos || !l) return IPFSN_ERR_ARG;
    if (*pos >= n->links_end) return IPFSN_ERR_NOTFOUND;
    if (rd_tag(n->block, n->links_end, pos, &f, &wt) || f != 2 || wt != 2)
        return IPFSN_ERR_MALFORMED;
    if (rd_bytes(n->block, n->links_end, pos, &b, &blen)) return IPFSN_ERR_MALFORMED;
    return parse_link(b, blen, l);
}

int ipfsn_unixfs_parse(const uint8_t *msg, uint32_t len, ipfsn_unixfs_t *u)
{
    uint32_t pos = 0, last = 0, f, wt;
    uint64_t v;
    const uint8_t *b;
    uint32_t blen;
    bool have_type = false;
    if (!msg || !u) return IPFSN_ERR_ARG;
    ipfsn__set(u, 0, (uint32_t) sizeof(*u));
    u->msg = msg;
    u->msg_len = len;
    while (pos < len) {
        if (rd_tag(msg, len, &pos, &f, &wt)) return IPFSN_ERR_MALFORMED;
        if (f < last || (f == last && f != 4)) return IPFSN_ERR_MALFORMED;
        last = f;
        if (wt == 0 && (f == 1 || f == 3 || f == 4 || f == 5 || f == 6 || f == 7)) {
            if (rd_varint(msg, len, &pos, &v)) return IPFSN_ERR_MALFORMED;
            if (f == 1) {
                if (v > 5) return IPFSN_ERR_MALFORMED;
                u->type = (uint32_t) v;
                have_type = true;
            } else if (f == 3) {
                u->filesize = v;
                u->has_filesize = true;
            } else if (f == 4) {
                u->nblocksizes++;
            }
        } else if (wt == 2 && (f == 2 || f == 8)) {
            if (rd_bytes(msg, len, &pos, &b, &blen)) return IPFSN_ERR_MALFORMED;
            if (f == 2) {
                u->data = b;
                u->data_len = blen;
            }
        } else {
            return IPFSN_ERR_MALFORMED;
        }
    }
    return have_type ? IPFSN_OK : IPFSN_ERR_MALFORMED;
}

int ipfsn_unixfs_next_blocksize(const ipfsn_unixfs_t *u, uint32_t *pos, uint64_t *bs)
{
    uint32_t f, wt, blen;
    uint64_t v;
    const uint8_t *b;
    if (!u || !pos || !bs) return IPFSN_ERR_ARG;
    while (*pos < u->msg_len) {
        if (rd_tag(u->msg, u->msg_len, pos, &f, &wt)) return IPFSN_ERR_MALFORMED;
        if (wt == 0) {
            if (rd_varint(u->msg, u->msg_len, pos, &v)) return IPFSN_ERR_MALFORMED;
            if (f == 4) {
                *bs = v;
                return IPFSN_OK;
            }
        } else if (wt == 2) {
            if (rd_bytes(u->msg, u->msg_len, pos, &b, &blen)) return IPFSN_ERR_MALFORMED;
        } else {
            return IPFSN_ERR_MALFORMED;
        }
    }
    return IPFSN_ERR_NOTFOUND;
}

/* ---- DAG walk ------------------------------------------------------------- */

static int fetch(ipfsn_walk_t *w, const ipfsn_cid_t *cid, ipfsn_get_fn get, void *ctx,
                 uint32_t *len)
{
    int r;
    if (cid->codec != IPFSN_MC_RAW && cid->codec != IPFSN_MC_DAG_PB) return IPFSN_ERR_UNSUPP;
    if (cid->mh_code == IPFSN_MH_IDENTITY) { /* inline: the CID carries the block */
        if (cid->digest_len > w->leaf_cap) return IPFSN_ERR_SPACE;
        ipfsn__cpy(w->leaf, cid->digest, cid->digest_len);
        *len = cid->digest_len;
        return IPFSN_OK;
    }
    *len = 0;
    if ((r = get(ctx, cid, w->leaf, w->leaf_cap, len)) != 0) return r < 0 ? r : IPFSN_ERR_NOTFOUND;
    if (*len > w->leaf_cap) return IPFSN_ERR_SPACE;
    return ipfsn_cid_verify(cid, w->leaf, *len); /* never trust the source */
}

static int open_frame(ipfsn_walk_t *w, uint32_t d, uint32_t len, uint64_t total, ipfsn_write_fn out,
                      void *octx)
{
    ipfsn_walk_frame_t *F = &w->f[d];
    if (len > IPFSN_NODE_MAX) return IPFSN_ERR_SPACE;
    ipfsn__cpy(F->node, w->leaf, len);
    if (ipfsn_dagpb_parse(F->node, len, &F->pb)) return IPFSN_ERR_MALFORMED;
    if (!F->pb.data) return IPFSN_ERR_MALFORMED; /* dag-pb but not UnixFS */
    if (ipfsn_unixfs_parse(F->pb.data, F->pb.data_len, &F->fs)) return IPFSN_ERR_MALFORMED;
    if (F->fs.type != IPFSN_UFS_FILE && F->fs.type != IPFSN_UFS_RAW) return IPFSN_ERR_UNSUPP;
    if (F->fs.nblocksizes != F->pb.nlinks) return IPFSN_ERR_MALFORMED;
    F->link_pos = 0;
    F->bs_pos = 0;
    F->start = total;
    if (F->fs.data_len && out(octx, F->fs.data, F->fs.data_len) != 0) return IPFSN_ERR_IO;
    return IPFSN_OK;
}

int ipfsn_cat(ipfsn_walk_t *w, const ipfsn_cid_t *root, ipfsn_get_fn get, void *get_ctx,
              ipfsn_write_fn out, void *out_ctx, uint64_t *size)
{
    uint32_t len, d = 0;
    uint64_t total = 0;
    ipfsn_pblink_t l;
    int r;
    if (!w || !root || !get || !out || !w->leaf) return IPFSN_ERR_ARG;
    if ((r = fetch(w, root, get, get_ctx, &len)) != 0) return r;
    if (root->codec == IPFSN_MC_RAW) {
        if (len && out(out_ctx, w->leaf, len) != 0) return IPFSN_ERR_IO;
        if (size) *size = len;
        return IPFSN_OK;
    }
    if ((r = open_frame(w, 0, len, 0, out, out_ctx)) != 0) return r;
    total = w->f[0].fs.data_len;
    for (;;) {
        ipfsn_walk_frame_t *F = &w->f[d];
        uint64_t bs;
        r = ipfsn_dagpb_next_link(&F->pb, &F->link_pos, &l);
        if (r == IPFSN_ERR_NOTFOUND) { /* node done: its sizes must add up */
            if (F->fs.has_filesize && total - F->start != F->fs.filesize)
                return IPFSN_ERR_MALFORMED;
            if (d == 0) break;
            d--;
            if (total - w->f[d].child_start != w->f[d].child_expect) return IPFSN_ERR_MALFORMED;
            continue;
        }
        if (r) return r;
        if (ipfsn_unixfs_next_blocksize(&F->fs, &F->bs_pos, &bs)) return IPFSN_ERR_MALFORMED;
        F->child_start = total;
        F->child_expect = bs;
        if ((r = fetch(w, &l.cid, get, get_ctx, &len)) != 0) return r;
        if (l.cid.codec == IPFSN_MC_RAW) {
            if (len != bs) return IPFSN_ERR_MALFORMED;
            if (len && out(out_ctx, w->leaf, len) != 0) return IPFSN_ERR_IO;
            total += len;
            continue;
        }
        if (d + 1 >= IPFSN_DAG_MAX_DEPTH) return IPFSN_ERR_DEPTH;
        d++;
        if ((r = open_frame(w, d, len, total, out, out_ctx)) != 0) return r;
        total += w->f[d].fs.data_len;
    }
    if (size) *size = total;
    return IPFSN_OK;
}
