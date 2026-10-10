/* ipfsn_store.c — content-addressed blockstore, sealed private blocks, pins,
 * and the provider announcement gate.
 *
 * STORAGE FORMAT (append-only log over host storage ops)
 *     "ZXIB" | ver=1 | flags | cid_len | 0 | payload_len (u32 LE) | cid | payload
 * PUBLIC:  cid = the block's CID, payload = the block.
 * PRIVATE: cid = the PRIVATE CID (CIDv1 raw sha2-256 of the payload), payload =
 *          nonce(12) | ChaCha20-Poly1305( cid_len | plaintext CID | block ) | tag(16).
 * So the plaintext CID of a private block is never written in the clear: a
 * stolen disk does not reveal that it holds a copy of some public file.
 *
 * SEALING. seal_key = HMAC(node_key, "zxv-ipfs-seal"), and the nonce is
 * HMAC(nonce_key, plaintext CID) cut to 96 bits. That is deterministic, which
 * is safe here because a nonce can only repeat for the same CID, and one CID
 * names exactly one plaintext (so the same key never encrypts two different
 * messages under one nonce, barring a 96-bit nonce collision between two
 * different CIDs: ~2^48 private blocks). The payoff: the same private block
 * always seals to the same bytes, so private copies deduplicate.
 *
 * Every get re-hashes. A record whose bytes no longer match its CID (bit rot,
 * a tampered disk) is reported as IPFSN_ERR_HASH, never returned.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#include "ipfs_node.h"
#include "ipfsn_util.h"
#include "../robin_debanks/sha256.h"
#include "../tls/aead.h"
#include "../tls/hkdf.h"

static const uint8_t REC_MAGIC[4] = {'Z', 'X', 'I', 'B'};
static const uint8_t SEAL_AAD[5] = {'Z', 'X', 'I', 'B', 1};

/* ---- hash table -------------------------------------------------------------- */

static uint32_t cid_hash(const ipfsn_cid_t *c)
{
    uint32_t h = 2166136261u;
    h = (h ^ c->codec) * 16777619u;
    h = (h ^ c->mh_code) * 16777619u;
    for (uint32_t i = 0; i < c->digest_len; i++) h = (h ^ c->digest[i]) * 16777619u;
    return h;
}

static ipfsn_bs_entry_t *bs_find(ipfsn_bs_t *bs, const ipfsn_cid_t *cid)
{
    ipfsn_cid_t k;
    ipfsn_cid_to_v1(cid, &k); /* v0 and v1 name the same dag-pb bytes */
    uint32_t m = bs->cap - 1, i = cid_hash(&k) & m;
    for (uint32_t probe = 0; probe < bs->cap; probe++, i = (i + 1) & m) {
        if (!bs->tab[i].used) return 0;
        if (ipfsn_cid_equal(&bs->tab[i].cid, &k)) return &bs->tab[i];
    }
    return 0;
}

static ipfsn_bs_entry_t *bs_insert(ipfsn_bs_t *bs, const ipfsn_cid_t *cid, uint64_t off,
                                   uint32_t len, uint8_t flags)
{
    ipfsn_cid_t k;
    ipfsn_cid_to_v1(cid, &k);
    if ((bs->count + 1) * 4u > bs->cap * 3u) return 0; /* keep load <= 3/4 */
    uint32_t m = bs->cap - 1, i = cid_hash(&k) & m;
    while (bs->tab[i].used) i = (i + 1) & m;
    ipfsn__cid_copy(&bs->tab[i].cid, &k);
    bs->tab[i].off = off;
    bs->tab[i].len = len;
    bs->tab[i].flags = flags;
    bs->tab[i].used = 1;
    bs->count++;
    return &bs->tab[i];
}

/* ---- init / key ---------------------------------------------------------------- */

int ipfsn_bs_init(ipfsn_bs_t *bs, const ipfsn_storage_ops_t *ops, ipfsn_bs_entry_t *table,
                  uint32_t cap_pow2, uint8_t *scratch, uint32_t scratch_cap)
{
    if (!bs || !ops || !ops->read || !ops->write || !ops->size || !table) return IPFSN_ERR_ARG;
    if (cap_pow2 < 4 || (cap_pow2 & (cap_pow2 - 1))) return IPFSN_ERR_ARG;
    ipfsn__set(bs, 0, (uint32_t) sizeof(*bs));
    ipfsn__cpy(&bs->ops, ops, (uint32_t) sizeof(*ops));
    bs->tab = table;
    bs->cap = cap_pow2;
    for (uint32_t i = 0; i < cap_pow2; i++) table[i].used = 0;
    bs->scratch = scratch;
    bs->scratch_cap = scratch ? scratch_cap : 0;
    bs->end = ops->size(ops->ctx);
    return IPFSN_OK;
}

void ipfsn_bs_set_key(ipfsn_bs_t *bs, const uint8_t key[IPFSN_KEY_LEN])
{
    static const uint8_t L1[] = "zxv-ipfs-seal";
    static const uint8_t L2[] = "zxv-ipfs-nonce";
    if (!bs || !key) return;
    hmac_sha256(key, IPFSN_KEY_LEN, L1, sizeof L1 - 1, bs->seal_key);
    hmac_sha256(key, IPFSN_KEY_LEN, L2, sizeof L2 - 1, bs->nonce_key);
    bs->have_key = true;
}

/* ---- records ------------------------------------------------------------------- */

static int write_record(ipfsn_bs_t *bs, const ipfsn_cid_t *key, uint8_t flags,
                        const uint8_t *payload, uint32_t plen, uint64_t *off)
{
    uint8_t hdr[IPFSN_REC_HDR + IPFSN_CID_BIN_MAX];
    int cl = ipfsn_cid_encode(key, hdr + IPFSN_REC_HDR, IPFSN_CID_BIN_MAX);
    if (cl < 0) return cl;
    ipfsn__cpy(hdr, REC_MAGIC, 4);
    hdr[4] = 1;
    hdr[5] = flags;
    hdr[6] = (uint8_t) cl;
    hdr[7] = 0;
    ipfsn__wr32le(hdr + 8, plen);
    *off = bs->end;
    if (bs->ops.write(bs->ops.ctx, bs->end, hdr, IPFSN_REC_HDR + (uint32_t) cl))
        return IPFSN_ERR_IO;
    if (plen && bs->ops.write(bs->ops.ctx, bs->end + IPFSN_REC_HDR + (uint32_t) cl, payload, plen))
        return IPFSN_ERR_IO;
    bs->end += IPFSN_REC_HDR + (uint32_t) cl + plen;
    return IPFSN_OK;
}

/* Read a record header; *payload_off and *plen describe the payload. */
static int read_header(ipfsn_bs_t *bs, uint64_t off, ipfsn_cid_t *key, uint8_t *flags,
                       uint64_t *payload_off, uint32_t *plen)
{
    uint8_t hdr[IPFSN_REC_HDR + IPFSN_CID_BIN_MAX];
    uint64_t size = bs->ops.size(bs->ops.ctx);
    if (off + IPFSN_REC_HDR > size) return IPFSN_ERR_MALFORMED;
    if (bs->ops.read(bs->ops.ctx, off, hdr, IPFSN_REC_HDR)) return IPFSN_ERR_IO;
    if (!ipfsn__eq(hdr, REC_MAGIC, 4) || hdr[4] != 1 || hdr[7] != 0 ||
        (hdr[5] & ~IPFSN_BSE_PRIVATE) || hdr[6] == 0 || hdr[6] > IPFSN_CID_BIN_MAX)
        return IPFSN_ERR_MALFORMED;
    if (off + IPFSN_REC_HDR + hdr[6] > size) return IPFSN_ERR_MALFORMED;
    if (bs->ops.read(bs->ops.ctx, off + IPFSN_REC_HDR, hdr + IPFSN_REC_HDR, hdr[6]))
        return IPFSN_ERR_IO;
    if (ipfsn_cid_decode_exact(hdr + IPFSN_REC_HDR, hdr[6], key)) return IPFSN_ERR_MALFORMED;
    *flags = hdr[5];
    *plen = ipfsn__rd32le(hdr + 8);
    *payload_off = off + IPFSN_REC_HDR + hdr[6];
    if (*plen > IPFSN_SCRATCH_MIN || *payload_off + *plen > size) return IPFSN_ERR_MALFORMED;
    return IPFSN_OK;
}

/* Open a sealed payload sitting in bs->scratch[0..plen). On success the
 * plaintext CID is in *inner and the block is at *blk. */
static int unseal(ipfsn_bs_t *bs, uint32_t plen, ipfsn_cid_t *inner, const uint8_t **blk,
                  uint32_t *blen)
{
    uint8_t *s = bs->scratch;
    uint32_t ctlen;
    if (!bs->have_key) return IPFSN_ERR_NOKEY;
    if (plen < 12u + 16u + 2u) return IPFSN_ERR_MALFORMED;
    ctlen = plen - 12u - 16u;
    if (!aead_open(bs->seal_key, s, SEAL_AAD, sizeof SEAL_AAD, s + 12, s + 12, ctlen,
                   s + 12 + ctlen))
        return IPFSN_ERR_CRYPTO;
    uint32_t cl = s[12];
    if (cl == 0 || 1u + cl > ctlen) return IPFSN_ERR_MALFORMED;
    if (ipfsn_cid_decode_exact(s + 13, cl, inner)) return IPFSN_ERR_MALFORMED;
    *blk = s + 13 + cl;
    *blen = ctlen - 1u - cl;
    return ipfsn_cid_verify(inner, *blk, *blen);
}

/* Seal (cid, block) into bs->scratch; returns sealed length via *plen. */
static int seal(ipfsn_bs_t *bs, const ipfsn_cid_t *cid, const uint8_t *block, uint32_t len,
                uint32_t *plen)
{
    uint8_t *s = bs->scratch, mac[32];
    int cl;
    if (!bs->have_key) return IPFSN_ERR_NOKEY;
    if (!s || bs->scratch_cap < 12u + 1u + IPFSN_CID_BIN_MAX + 16u ||
        len > bs->scratch_cap - (12u + 1u + IPFSN_CID_BIN_MAX + 16u))
        return IPFSN_ERR_SPACE;
    if ((cl = ipfsn_cid_encode(cid, s + 13, IPFSN_CID_BIN_MAX)) < 0) return cl;
    s[12] = (uint8_t) cl;
    hmac_sha256(bs->nonce_key, IPFSN_KEY_LEN, s + 13, (uint32_t) cl, mac);
    ipfsn__cpy(s, mac, 12);
    if (len) ipfsn__cpy(s + 13 + cl, block, len);
    uint32_t ptlen = 1u + (uint32_t) cl + len;
    aead_seal(bs->seal_key, s, SEAL_AAD, sizeof SEAL_AAD, s + 12, s + 12, ptlen, s + 12 + ptlen);
    *plen = 12u + ptlen + 16u;
    return IPFSN_OK;
}

/* ---- public API ---------------------------------------------------------------- */

int ipfsn_bs_mount(ipfsn_bs_t *bs)
{
    uint64_t off = 0, size, poff;
    ipfsn_cid_t key, inner;
    uint8_t flags;
    uint32_t plen, blen;
    const uint8_t *blk;
    if (!bs) return IPFSN_ERR_ARG;
    size = bs->ops.size(bs->ops.ctx);
    for (uint32_t i = 0; i < bs->cap; i++) bs->tab[i].used = 0;
    bs->count = 0;
    while (off < size) {
        if (read_header(bs, off, &key, &flags, &poff, &plen)) break; /* torn tail */
        if (!bs_find(bs, &key)) {
            uint8_t ef =
                (flags & IPFSN_BSE_PRIVATE) ? (IPFSN_BSE_PRIVATE | IPFSN_BSE_SEALED_KEY) : 0u;
            if (!bs_insert(bs, &key, off, plen, ef)) return IPFSN_ERR_FULL;
        } else if (!(flags & IPFSN_BSE_PRIVATE)) { /* a later public copy wins */
            ipfsn_bs_entry_t *e = bs_find(bs, &key);
            e->off = off;
            e->len = plen;
            e->flags = 0;
        }
        if ((flags & IPFSN_BSE_PRIVATE) && bs->have_key && bs->scratch && plen <= bs->scratch_cap) {
            if (bs->ops.read(bs->ops.ctx, poff, bs->scratch, plen)) return IPFSN_ERR_IO;
            if (unseal(bs, plen, &inner, &blk, &blen) == IPFSN_OK && !bs_find(bs, &inner))
                if (!bs_insert(bs, &inner, off, plen, IPFSN_BSE_PRIVATE)) return IPFSN_ERR_FULL;
        }
        off = poff + plen;
    }
    bs->end = off; /* a torn final record is overwritten by the next append */
    return IPFSN_OK;
}

int ipfsn_bs_put(ipfsn_bs_t *bs, const ipfsn_cid_t *cid, const uint8_t *block, uint32_t len,
                 uint32_t vis, ipfsn_cid_t *pcid)
{
    ipfsn_bs_entry_t *e;
    uint64_t off;
    int r;
    if (!bs || !cid || (!block && len)) return IPFSN_ERR_ARG;
    if (vis != IPFSN_VIS_PUBLIC && vis != IPFSN_VIS_PRIVATE) return IPFSN_ERR_ARG;
    if (len > IPFSN_BLOCK_MAX) return IPFSN_ERR_SPACE;
    if ((r = ipfsn_cid_verify(cid, block, len)) != 0) return r; /* refuse a lying put */
    e = bs_find(bs, cid);
    if (vis == IPFSN_VIS_PUBLIC) {
        if (e && !(e->flags & IPFSN_BSE_PRIVATE)) return IPFSN_OK; /* dedup */
        if ((r = write_record(bs, cid, 0, block, len, &off)) != 0) return r;
        if (e) { /* was held sealed; now public too. Its private CID stays valid. */
            e->off = off;
            e->len = len;
            e->flags = 0;
            return IPFSN_OK;
        }
        return bs_insert(bs, cid, off, len, 0) ? IPFSN_OK : IPFSN_ERR_FULL;
    }
    uint32_t plen;
    ipfsn_cid_t pc;
    if ((r = seal(bs, cid, block, len, &plen)) != 0) return r;
    ipfsn_cid_sha256(IPFSN_MC_RAW, bs->scratch, plen, &pc);
    ipfsn_bs_entry_t *pe = bs_find(bs, &pc);
    if (!pe) {
        if ((r = write_record(bs, &pc, IPFSN_BSE_PRIVATE, bs->scratch, plen, &off)) != 0) return r;
        if (!(pe = bs_insert(bs, &pc, off, plen, IPFSN_BSE_PRIVATE | IPFSN_BSE_SEALED_KEY)))
            return IPFSN_ERR_FULL;
    }
    if (!e && !bs_insert(bs, cid, pe->off, plen, IPFSN_BSE_PRIVATE)) return IPFSN_ERR_FULL;
    if (pcid) ipfsn__cid_copy(pcid, &pc);
    return IPFSN_OK;
}

int ipfsn_bs_get(ipfsn_bs_t *bs, const ipfsn_cid_t *cid, uint8_t *buf, uint32_t cap, uint32_t *len)
{
    ipfsn_bs_entry_t *e;
    ipfsn_cid_t key, inner, want;
    uint8_t flags;
    uint64_t poff;
    uint32_t plen, blen;
    const uint8_t *blk;
    int r;
    if (!bs || !cid || !buf || !len) return IPFSN_ERR_ARG;
    if (!(e = bs_find(bs, cid))) return IPFSN_ERR_NOTFOUND;
    if ((r = read_header(bs, e->off, &key, &flags, &poff, &plen)) != 0) return r;
    if (plen != e->len) return IPFSN_ERR_MALFORMED;
    ipfsn_cid_to_v1(cid, &want);
    if (!(e->flags & IPFSN_BSE_PRIVATE)) {
        if (flags & IPFSN_BSE_PRIVATE) return IPFSN_ERR_MALFORMED;
        if (plen > cap) return IPFSN_ERR_SPACE;
        if (bs->ops.read(bs->ops.ctx, poff, buf, plen)) return IPFSN_ERR_IO;
        if ((r = ipfsn_cid_verify(&want, buf, plen)) != 0) return r;
        *len = plen;
        return IPFSN_OK;
    }
    if (!(flags & IPFSN_BSE_PRIVATE)) return IPFSN_ERR_MALFORMED;
    if (!bs->scratch || plen > bs->scratch_cap) return IPFSN_ERR_SPACE;
    if (bs->ops.read(bs->ops.ctx, poff, bs->scratch, plen)) return IPFSN_ERR_IO;
    if ((r = ipfsn_cid_verify(&key, bs->scratch, plen)) != 0) return r; /* sealed bytes intact */
    if ((r = unseal(bs, plen, &inner, &blk, &blen)) != 0) return r;
    if (!(e->flags & IPFSN_BSE_SEALED_KEY)) {
        ipfsn_cid_t iv1;
        ipfsn_cid_to_v1(&inner, &iv1);
        if (!ipfsn_cid_equal(&iv1, &want)) return IPFSN_ERR_HASH;
    }
    if (blen > cap) return IPFSN_ERR_SPACE;
    ipfsn__cpy(buf, blk, blen);
    ipfsn__set(bs->scratch, 0, plen); /* do not leave plaintext in scratch */
    *len = blen;
    return IPFSN_OK;
}

bool ipfsn_bs_has(ipfsn_bs_t *bs, const ipfsn_cid_t *cid)
{
    return bs && cid && bs_find(bs, cid) != 0;
}

int ipfsn_bs_visibility(ipfsn_bs_t *bs, const ipfsn_cid_t *cid)
{
    ipfsn_bs_entry_t *e;
    if (!bs || !cid) return IPFSN_ERR_ARG;
    if (!(e = bs_find(bs, cid))) return IPFSN_ERR_NOTFOUND;
    return (e->flags & IPFSN_BSE_PRIVATE) ? (int) IPFSN_VIS_PRIVATE : (int) IPFSN_VIS_PUBLIC;
}

int ipfsn_bs_source(void *ctx, const ipfsn_cid_t *cid, uint8_t *buf, uint32_t cap, uint32_t *len)
{
    return ipfsn_bs_get((ipfsn_bs_t *) ctx, cid, buf, cap, len);
}

/* ---- memory storage -------------------------------------------------------------- */

static int ms_read(void *ctx, uint64_t off, uint8_t *buf, uint32_t len)
{
    ipfsn_memstore_t *m = (ipfsn_memstore_t *) ctx;
    if (off > m->len || len > m->len - off) return -1;
    ipfsn__cpy(buf, m->buf + off, len);
    return 0;
}

static int ms_write(void *ctx, uint64_t off, const uint8_t *buf, uint32_t len)
{
    ipfsn_memstore_t *m = (ipfsn_memstore_t *) ctx;
    if (off > m->cap || len > m->cap - off) return -1;
    ipfsn__cpy(m->buf + off, buf, len);
    if (off + len > m->len) m->len = off + len;
    return 0;
}

static uint64_t ms_size(void *ctx)
{
    return ((ipfsn_memstore_t *) ctx)->len;
}

void ipfsn_memstore_ops(ipfsn_memstore_t *m, ipfsn_storage_ops_t *ops)
{
    if (!m || !ops) return;
    ops->ctx = m;
    ops->read = ms_read;
    ops->write = ms_write;
    ops->size = ms_size;
}

/* ---- node: pins and announcements -------------------------------------------- */

int ipfsn_node_init(ipfsn_node_t *n, ipfsn_bs_t *bs, ipfsn_pin_t *pins, uint32_t pin_cap,
                    uint8_t *net_buf, uint32_t net_cap)
{
    if (!n || !bs || (!pins && pin_cap)) return IPFSN_ERR_ARG;
    ipfsn__set(n, 0, (uint32_t) sizeof(*n));
    n->bs = bs;
    n->pins = pins;
    n->pin_cap = pin_cap;
    for (uint32_t i = 0; i < pin_cap; i++) pins[i].used = 0;
    n->net_buf = net_buf;
    n->net_cap = net_buf ? net_cap : 0;
    n->peer.wire = IPFSN_WIRE_UBH168;
    return IPFSN_OK;
}

static ipfsn_pin_t *pin_find(ipfsn_node_t *n, const ipfsn_cid_t *cid)
{
    ipfsn_cid_t k;
    ipfsn_cid_to_v1(cid, &k);
    for (uint32_t i = 0; i < n->pin_cap; i++)
        if (n->pins[i].used && ipfsn_cid_equal(&n->pins[i].cid, &k)) return &n->pins[i];
    return 0;
}

int ipfsn_pin(ipfsn_node_t *n, const ipfsn_cid_t *cid, uint32_t vis, const ipfsn_cid_t *handle)
{
    ipfsn_pin_t *p;
    if (!n || !cid) return IPFSN_ERR_ARG;
    if (vis != IPFSN_VIS_PUBLIC && vis != IPFSN_VIS_PRIVATE) return IPFSN_ERR_ARG;
    if (vis == IPFSN_VIS_PUBLIC && ipfsn_is_private(n, cid)) return IPFSN_ERR_CONFLICT;
    if ((p = pin_find(n, cid)) != 0) {
        if (p->vis != vis) return IPFSN_ERR_CONFLICT;
    } else {
        for (uint32_t i = 0; i < n->pin_cap && !p; i++)
            if (!n->pins[i].used) p = &n->pins[i];
        if (!p) return IPFSN_ERR_FULL;
        ipfsn_cid_to_v1(cid, &p->cid);
        p->vis = (uint8_t) vis;
        p->used = 1;
    }
    ipfsn_cid_to_v1(handle ? handle : cid, &p->handle);
    p->private_cid = (uint8_t) (handle && !ipfsn_cid_equal(&p->handle, &p->cid));
    return IPFSN_OK;
}

int ipfsn_unpin(ipfsn_node_t *n, const ipfsn_cid_t *cid)
{
    ipfsn_pin_t *p;
    if (!n || !cid) return IPFSN_ERR_ARG;
    if (!(p = pin_find(n, cid))) return IPFSN_ERR_NOTFOUND;
    p->used = 0;
    return IPFSN_OK;
}

bool ipfsn_is_private(ipfsn_node_t *n, const ipfsn_cid_t *cid)
{
    ipfsn_cid_t k;
    if (!n || !cid) return true; /* fail closed */
    ipfsn_cid_to_v1(cid, &k);
    for (uint32_t i = 0; i < n->pin_cap; i++) {
        const ipfsn_pin_t *p = &n->pins[i];
        if (p->used && p->vis == IPFSN_VIS_PRIVATE &&
            (ipfsn_cid_equal(&p->cid, &k) || ipfsn_cid_equal(&p->handle, &k)))
            return true;
    }
    return ipfsn_bs_visibility(n->bs, &k) == (int) IPFSN_VIS_PRIVATE;
}

int ipfsn_provide(ipfsn_node_t *n, const ipfsn_cid_t *cid)
{
    ipfsn_pin_t *p;
    if (!n || !cid) return IPFSN_ERR_ARG;
    if (ipfsn_is_private(n, cid)) return IPFSN_ERR_PRIVATE; /* never announced */
    if (!(p = pin_find(n, cid)) || p->vis != IPFSN_VIS_PUBLIC) return IPFSN_ERR_NOTFOUND;
    if (!n->prov.announce) return IPFSN_ERR_TRANSPORT;
    if (n->prov.announce(n->prov.ctx, &p->cid) != 0) return IPFSN_ERR_TRANSPORT;
    n->announced++;
    return IPFSN_OK;
}

int ipfsn_provide_all(ipfsn_node_t *n)
{
    int count = 0;
    if (!n) return IPFSN_ERR_ARG;
    for (uint32_t i = 0; i < n->pin_cap; i++) {
        if (!n->pins[i].used || n->pins[i].vis != IPFSN_VIS_PUBLIC) continue;
        int r = ipfsn_provide(n, &n->pins[i].cid);
        if (r == IPFSN_OK)
            count++;
        else if (r != IPFSN_ERR_PRIVATE)
            return r;
    }
    return count;
}

int ipfsn_serve_block(ipfsn_node_t *n, const ipfsn_cid_t *cid, uint32_t wire, uint8_t *out,
                      uint32_t cap, uint32_t *len)
{
    uint32_t blen;
    int r;
    if (!n || !cid || !out || !len || !n->net_buf) return IPFSN_ERR_ARG;
    if (ipfsn_is_private(n, cid)) return IPFSN_ERR_PRIVATE; /* never served to peers */
    if ((r = ipfsn_bs_get(n->bs, cid, n->net_buf, n->net_cap, &blen)) != 0) return r;
    return ipfsn_wire_encode_block(wire, cid, n->net_buf, blen, out, cap, len);
}

/* ---- add a buffer as a UnixFS file --------------------------------------------- */

typedef struct {
    ipfsn_node_t *n;
    uint32_t vis;
    ipfsn_cid_t last_pcid;
} add_ctx_t;

static int add_sink(void *ctx, const ipfsn_cid_t *cid, const uint8_t *block, uint32_t len)
{
    add_ctx_t *a = (add_ctx_t *) ctx;
    return ipfsn_bs_put(a->n->bs, cid, block, len, a->vis, &a->last_pcid);
}

int ipfsn_add(ipfsn_node_t *n, ipfsn_ufs_t *u, uint8_t *chunk_buf, const uint8_t *data,
              uint32_t len, uint32_t vis, bool private_cid, ipfsn_cid_t *root, ipfsn_cid_t *handle)
{
    add_ctx_t a;
    int r;
    if (!n || !u || !chunk_buf || !root || (!data && len)) return IPFSN_ERR_ARG;
    if (vis == IPFSN_VIS_PRIVATE && !n->bs->have_key) return IPFSN_ERR_NOKEY;
    if (vis == IPFSN_VIS_PUBLIC && private_cid) return IPFSN_ERR_ARG;
    a.n = n;
    a.vis = vis;
    ipfsn__set(&a.last_pcid, 0, (uint32_t) sizeof(a.last_pcid));
    if ((r = ipfsn_ufs_init(u, chunk_buf, 0, 0, add_sink, &a)) != 0) return r;
    if ((r = ipfsn_ufs_update(u, data, len)) != 0) return r;
    if ((r = ipfsn_ufs_final(u, root, 0)) != 0) return r;
    /* The root is the last block the builder emits, so last_pcid is its private CID. */
    const ipfsn_cid_t *h = private_cid ? &a.last_pcid : root;
    if ((r = ipfsn_pin(n, root, vis, h)) != 0) return r;
    if (handle) ipfsn__cid_copy(handle, h);
    return IPFSN_OK;
}
