/* ipfsn_net.c — pulling content from the network, verified.
 *
 * Two sources, one rule. The IPFS network is reached through a trustless
 * HTTP gateway (the host performs the HTTPS GET; this file builds the request
 * and checks the answer). ZXV peers in the Carracho economy are reached
 * through a function-pointer hook. Neither is trusted: a block is accepted
 * only after it hashes to the CID that was asked for, and a CAR is accepted
 * only after EVERY block in it does, before any of them is stored.
 *
 * A private CID is never put in a gateway URL or a peer request: asking for
 * it would tell the other side what this node holds.
 *
 * NOT IMPLEMENTED (stated plainly): libp2p, Bitswap, the public IPFS DHT. This
 * node is not a libp2p peer; it speaks to IPFS through gateways and CIDs.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#include "ipfs_node.h"
#include "ipfsn_util.h"

/* ---- CAR v1 ------------------------------------------------------------------ */

/* One canonical CBOR head (dag-cbor: shortest form only). */
static int cbor_head(const uint8_t *p, uint32_t len, uint32_t *pos, uint32_t *major, uint32_t *val)
{
    uint32_t ai, v;
    if (*pos >= len) return IPFSN_ERR_MALFORMED;
    *major = p[*pos] >> 5;
    ai = p[*pos] & 31u;
    (*pos)++;
    if (ai < 24) {
        v = ai;
    } else if (ai == 24) {
        if (*pos + 1 > len) return IPFSN_ERR_MALFORMED;
        v = p[*pos];
        *pos += 1;
        if (v < 24) return IPFSN_ERR_MALFORMED;
    } else if (ai == 25) {
        if (*pos + 2 > len) return IPFSN_ERR_MALFORMED;
        v = ((uint32_t) p[*pos] << 8) | p[*pos + 1];
        *pos += 2;
        if (v < 256) return IPFSN_ERR_MALFORMED;
    } else if (ai == 26) {
        if (*pos + 4 > len) return IPFSN_ERR_MALFORMED;
        v = ((uint32_t) p[*pos] << 24) | ((uint32_t) p[*pos + 1] << 16) |
            ((uint32_t) p[*pos + 2] << 8) | p[*pos + 3];
        *pos += 4;
        if (v < 65536) return IPFSN_ERR_MALFORMED;
    } else {
        return IPFSN_ERR_MALFORMED; /* 64-bit lengths, indefinite lengths */
    }
    *val = v;
    return IPFSN_OK;
}

static bool cbor_key_is(const uint8_t *p, uint32_t len, uint32_t *pos, const char *key,
                        uint32_t klen)
{
    if (*pos + klen > len) return false;
    if (!ipfsn__eq(p + *pos, key, klen)) return false;
    *pos += klen;
    return true;
}

int ipfsn_car_open(ipfsn_car_t *c, const uint8_t *buf, uint32_t len)
{
    uint64_t hl;
    uint32_t pos, end, mj, v, n;
    bool have_roots = false, have_version = false;
    int k;
    if (!c || !buf) return IPFSN_ERR_ARG;
    ipfsn__set(c, 0, (uint32_t) sizeof(*c));
    if ((k = ipfsn_varint_get(buf, len, &hl)) < 0) return k;
    if (hl == 0 || hl > len - (uint32_t) k) return IPFSN_ERR_MALFORMED;
    pos = (uint32_t) k;
    end = pos + (uint32_t) hl;
    if (cbor_head(buf, end, &pos, &mj, &n) || mj != 5 || n != 2) return IPFSN_ERR_MALFORMED;
    for (uint32_t e = 0; e < 2; e++) {
        if (cbor_head(buf, end, &pos, &mj, &v) || mj != 3) return IPFSN_ERR_MALFORMED;
        if (v == 5 && !have_roots && cbor_key_is(buf, end, &pos, "roots", 5)) {
            have_roots = true;
            if (cbor_head(buf, end, &pos, &mj, &n) || mj != 4 || n == 0) return IPFSN_ERR_MALFORMED;
            if (n > 4) return IPFSN_ERR_UNSUPP;
            for (uint32_t i = 0; i < n; i++) {
                if (cbor_head(buf, end, &pos, &mj, &v) || mj != 6 || v != 42)
                    return IPFSN_ERR_MALFORMED;
                if (cbor_head(buf, end, &pos, &mj, &v) || mj != 2 || v < 2 || v > end - pos)
                    return IPFSN_ERR_MALFORMED;
                if (buf[pos] != 0x00) return IPFSN_ERR_MALFORMED; /* identity multibase */
                if (ipfsn_cid_decode_exact(buf + pos + 1, v - 1, &c->roots[i]))
                    return IPFSN_ERR_MALFORMED;
                pos += v;
            }
            c->nroots = n;
        } else if (v == 7 && !have_version && cbor_key_is(buf, end, &pos, "version", 7)) {
            have_version = true;
            if (cbor_head(buf, end, &pos, &mj, &v) || mj != 0) return IPFSN_ERR_MALFORMED;
            if (v != 1) return IPFSN_ERR_UNSUPP; /* CARv2 is not read here */
        } else {
            return IPFSN_ERR_MALFORMED;
        }
    }
    if (pos != end) return IPFSN_ERR_MALFORMED;
    c->buf = buf;
    c->len = len;
    c->first = end;
    c->pos = end;
    return IPFSN_OK;
}

/* Section at *pos, unverified. */
static int car_section(const ipfsn_car_t *c, uint32_t *pos, ipfsn_cid_t *cid, const uint8_t **block,
                       uint32_t *blen)
{
    uint64_t sl;
    int k;
    if (*pos >= c->len) return IPFSN_ERR_NOTFOUND;
    if ((k = ipfsn_varint_get(c->buf + *pos, c->len - *pos, &sl)) < 0) return k;
    if (sl == 0 || sl > c->len - *pos - (uint32_t) k) return IPFSN_ERR_MALFORMED;
    uint32_t s = *pos + (uint32_t) k, sl32 = (uint32_t) sl;
    if ((k = ipfsn_cid_decode(c->buf + s, sl32, cid)) < 0) return IPFSN_ERR_MALFORMED;
    *block = c->buf + s + (uint32_t) k;
    *blen = sl32 - (uint32_t) k;
    if (*blen > IPFSN_BLOCK_MAX) return IPFSN_ERR_SPACE;
    *pos = s + sl32;
    return IPFSN_OK;
}

int ipfsn_car_next(ipfsn_car_t *c, ipfsn_cid_t *cid, const uint8_t **block, uint32_t *len)
{
    int r;
    if (!c || !c->buf || !cid || !block || !len) return IPFSN_ERR_ARG;
    if ((r = car_section(c, &c->pos, cid, block, len)) != 0) return r;
    return ipfsn_cid_verify(cid, *block, *len);
}

int ipfsn_car_source(void *ctx, const ipfsn_cid_t *cid, uint8_t *buf, uint32_t cap, uint32_t *len)
{
    const ipfsn_car_t *c = (const ipfsn_car_t *) ctx;
    uint32_t pos;
    ipfsn_cid_t k, s;
    const uint8_t *b;
    uint32_t bl;
    int r;
    if (!c || !cid || !buf || !len) return IPFSN_ERR_ARG;
    ipfsn_cid_to_v1(cid, &k);
    pos = c->first;
    while ((r = car_section(c, &pos, &s, &b, &bl)) == IPFSN_OK) {
        ipfsn_cid_to_v1(&s, &s);
        if (!ipfsn_cid_equal(&s, &k)) continue;
        if (bl > cap) return IPFSN_ERR_SPACE;
        ipfsn__cpy(buf, b, bl);
        *len = bl;
        return IPFSN_OK; /* the caller (ipfsn_cat) verifies */
    }
    return r;
}

int ipfsn_car_begin(const ipfsn_cid_t *root, uint8_t *out, uint32_t cap)
{
    uint8_t h[24 + IPFSN_CID_BIN_MAX];
    uint8_t cb[IPFSN_CID_BIN_MAX];
    uint32_t n = 0, k;
    int cl;
    if (!root || !out) return IPFSN_ERR_ARG;
    if ((cl = ipfsn_cid_encode(root, cb, sizeof cb)) < 0) return cl;
    h[n++] = 0xa2; /* map(2): keys in dag-cbor order (length, then bytes) */
    h[n++] = 0x65;
    ipfsn__cpy(h + n, "roots", 5);
    n += 5;
    h[n++] = 0x81;
    h[n++] = 0xd8;
    h[n++] = 0x2a;
    if (cl + 1 < 24) {
        h[n++] = (uint8_t) (0x40 + cl + 1);
    } else {
        h[n++] = 0x58;
        h[n++] = (uint8_t) (cl + 1);
    }
    h[n++] = 0x00;
    ipfsn__cpy(h + n, cb, (uint32_t) cl);
    n += (uint32_t) cl;
    h[n++] = 0x67;
    ipfsn__cpy(h + n, "version", 7);
    n += 7;
    h[n++] = 0x01;
    if (!(k = ipfsn_varint_put(n, out, cap))) return IPFSN_ERR_SPACE;
    if (cap - k < n) return IPFSN_ERR_SPACE;
    ipfsn__cpy(out + k, h, n);
    return (int) (k + n);
}

int ipfsn_car_put(const ipfsn_cid_t *cid, const uint8_t *block, uint32_t len, uint8_t *out,
                  uint32_t cap)
{
    uint8_t cb[IPFSN_CID_BIN_MAX];
    uint32_t k;
    int cl;
    if (!cid || !out || (!block && len)) return IPFSN_ERR_ARG;
    if ((cl = ipfsn_cid_encode(cid, cb, sizeof cb)) < 0) return cl;
    if (len > IPFSN_BLOCK_MAX) return IPFSN_ERR_SPACE;
    if (!(k = ipfsn_varint_put((uint32_t) cl + len, out, cap))) return IPFSN_ERR_SPACE;
    if (cap - k < (uint32_t) cl + len) return IPFSN_ERR_SPACE;
    ipfsn__cpy(out + k, cb, (uint32_t) cl);
    if (len) ipfsn__cpy(out + k + (uint32_t) cl, block, len);
    return (int) (k + (uint32_t) cl + len);
}

/* ---- trustless gateway ----------------------------------------------------------- */

int ipfsn_gw_url(const char *base, const ipfsn_cid_t *cid, uint32_t fmt, char *out, uint32_t cap)
{
    char cs[IPFSN_CID_STR_MAX];
    ipfsn_cid_t v1;
    const char *q = fmt == IPFSN_FMT_CAR ? "?format=car" : "?format=raw";
    uint32_t bl, n = 0;
    int cl;
    if (!base || !cid || !out || fmt > IPFSN_FMT_CAR) return IPFSN_ERR_ARG;
    bl = ipfsn__strlen(base, 256);
    if (bl == 0 || bl >= 256 || base[bl - 1] == '/') return IPFSN_ERR_ARG;
    ipfsn_cid_to_v1(cid, &v1); /* gateways accept v1 base32 everywhere (subdomain-safe) */
    if ((cl = ipfsn_cid_to_string(&v1, cs, sizeof cs)) < 0) return cl;
    if (bl + 6 + (uint32_t) cl + 11 + 1 > cap) return IPFSN_ERR_SPACE;
    ipfsn__cpy(out, base, bl);
    n = bl;
    ipfsn__cpy(out + n, "/ipfs/", 6);
    n += 6;
    ipfsn__cpy(out + n, cs, (uint32_t) cl);
    n += (uint32_t) cl;
    ipfsn__cpy(out + n, q, 11);
    n += 11;
    out[n] = 0;
    return (int) n;
}

int ipfsn_gw_fetch_block(ipfsn_node_t *n, const ipfsn_cid_t *cid, uint8_t *buf, uint32_t cap,
                         uint32_t *len)
{
    char url[512];
    int r;
    if (!n || !cid || !buf || !len) return IPFSN_ERR_ARG;
    if (ipfsn_is_private(n, cid)) return IPFSN_ERR_PRIVATE;
    if (!n->gw.https_get || !n->gw.base) return IPFSN_ERR_TRANSPORT;
    if ((r = ipfsn_gw_url(n->gw.base, cid, IPFSN_FMT_RAW, url, sizeof url)) < 0) return r;
    *len = 0;
    if (n->gw.https_get(n->gw.ctx, url, "application/vnd.ipld.raw", buf, cap, len) != 0)
        return IPFSN_ERR_TRANSPORT;
    if (*len > cap) return IPFSN_ERR_SPACE;
    if ((r = ipfsn_cid_verify(cid, buf, *len)) != 0) return r;
    return ipfsn_bs_put(n->bs, cid, buf, *len, IPFSN_VIS_PUBLIC, 0);
}

int ipfsn_gw_fetch_car(ipfsn_node_t *n, const ipfsn_cid_t *root, uint8_t *carbuf, uint32_t cap,
                       uint32_t *nblocks)
{
    char url[512];
    ipfsn_car_t car;
    ipfsn_cid_t cid, r1, k;
    const uint8_t *b;
    uint32_t len = 0, bl, count = 0;
    bool listed = false;
    int r;
    if (!n || !root || !carbuf || !nblocks) return IPFSN_ERR_ARG;
    *nblocks = 0;
    if (ipfsn_is_private(n, root)) return IPFSN_ERR_PRIVATE;
    if (!n->gw.https_get || !n->gw.base) return IPFSN_ERR_TRANSPORT;
    if ((r = ipfsn_gw_url(n->gw.base, root, IPFSN_FMT_CAR, url, sizeof url)) < 0) return r;
    if (n->gw.https_get(n->gw.ctx, url, "application/vnd.ipld.car", carbuf, cap, &len) != 0)
        return IPFSN_ERR_TRANSPORT;
    if (len > cap) return IPFSN_ERR_SPACE;
    if ((r = ipfsn_car_open(&car, carbuf, len)) != 0) return r;
    ipfsn_cid_to_v1(root, &r1);
    for (uint32_t i = 0; i < car.nroots; i++) {
        ipfsn_cid_to_v1(&car.roots[i], &k);
        if (ipfsn_cid_equal(&k, &r1)) listed = true;
    }
    if (!listed) return IPFSN_ERR_MALFORMED;
    /* Pass 1: every block must verify before any is kept. */
    while ((r = ipfsn_car_next(&car, &cid, &b, &bl)) == IPFSN_OK) count++;
    if (r != IPFSN_ERR_NOTFOUND) return r;
    /* Pass 2: store. */
    car.pos = car.first;
    while ((r = ipfsn_car_next(&car, &cid, &b, &bl)) == IPFSN_OK) {
        int p = ipfsn_bs_put(n->bs, &cid, b, bl, IPFSN_VIS_PUBLIC, 0);
        if (p) return p;
        (*nblocks)++;
    }
    return r == IPFSN_ERR_NOTFOUND && *nblocks == count ? IPFSN_OK : IPFSN_ERR_MALFORMED;
}

/* ---- peers (Carracho hook) --------------------------------------------------------- */

int ipfsn_peer_fetch(ipfsn_node_t *n, const ipfsn_cid_t *cid, uint8_t *buf, uint32_t cap,
                     uint32_t *len)
{
    uint8_t cb[IPFSN_CID_BIN_MAX];
    const uint8_t *blk;
    uint32_t got = 0, blen;
    int cl, r;
    if (!n || !cid || !buf || !len) return IPFSN_ERR_ARG;
    if (ipfsn_is_private(n, cid)) return IPFSN_ERR_PRIVATE;
    if (!n->peer.want || !n->net_buf) return IPFSN_ERR_TRANSPORT;
    if ((cl = ipfsn_cid_encode(cid, cb, sizeof cb)) < 0) return cl;
    if (n->peer.want(n->peer.ctx, cb, (uint32_t) cl, n->net_buf, n->net_cap, &got) != 0)
        return IPFSN_ERR_NOTFOUND;
    if (got > n->net_cap) return IPFSN_ERR_SPACE;
    if ((r = ipfsn_wire_decode_block(n->peer.wire, cid, n->net_buf, got, &blk, &blen)) != 0)
        return r;
    if (blen > cap) return IPFSN_ERR_SPACE;
    ipfsn__cpy(buf, blk, blen);
    *len = blen;
    return ipfsn_bs_put(n->bs, cid, buf, blen, IPFSN_VIS_PUBLIC, 0);
}

int ipfsn_node_source(void *ctx, const ipfsn_cid_t *cid, uint8_t *buf, uint32_t cap, uint32_t *len)
{
    ipfsn_node_t *n = (ipfsn_node_t *) ctx;
    int r;
    if (!n || !cid) return IPFSN_ERR_ARG;
    r = ipfsn_bs_get(n->bs, cid, buf, cap, len);
    if (r != IPFSN_ERR_NOTFOUND) return r;
    if (n->local_only || ipfsn_is_private(n, cid)) return IPFSN_ERR_NOTFOUND; /* stay local */
    if (n->peer.want && ipfsn_peer_fetch(n, cid, buf, cap, len) == IPFSN_OK) return IPFSN_OK;
    if (n->gw.https_get) return ipfsn_gw_fetch_block(n, cid, buf, cap, len);
    return IPFSN_ERR_NOTFOUND;
}

int ipfsn_node_cat(ipfsn_node_t *n, ipfsn_walk_t *w, const ipfsn_cid_t *root, ipfsn_write_fn out,
                   void *out_ctx, uint64_t *size)
{
    int r;
    if (!n || !root) return IPFSN_ERR_ARG;
    n->local_only = ipfsn_is_private(n, root);
    r = ipfsn_cat(w, root, ipfsn_node_source, n, out, out_ctx, size);
    n->local_only = false;
    return r;
}
