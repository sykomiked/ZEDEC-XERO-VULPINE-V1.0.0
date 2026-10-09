/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* vna_file.c — chunking, Merkle proofs, streaming verification. See vna_file.h. */
#include "vna_file.h"
#include "../robin_debanks/sha256.h"

static const vna_field_t hash_fields[] = {VNA_FFIX(vna_hash_el_t, h, VNA_AXIS_PROVENANCE)};
static const vna_schema_t hash_schema = {"hash", 0x0030, 3, hash_fields, 1};

static const vna_field_t req_fields[] = {
    VNA_FCONST(vna_chunk_req_t, magic, VNA_CHUNKREQ_MAGIC),
    VNA_FFIX(vna_chunk_req_t, root, VNA_AXIS_NONE),
    VNA_FU32(vna_chunk_req_t, index, VNA_FILE_MAX_CHUNKS - 1u, VNA_AXIS_NONE),
};
/* UBH class 3 = CONTENT_OBJECT */
const vna_schema_t vna_chunk_req_schema = {"chunk_req", 0x0031, 3, req_fields, 3};

#define C vna_chunk_t
static const vna_field_t chunk_fields[] = {
    VNA_FCONST(C, magic, VNA_CHUNK_MAGIC),
    VNA_FFIX(C, root, VNA_AXIS_NONE),
    VNA_FU64(C, file_size, 0, VNA_AXIS_NONE),
    VNA_FU32(C, chunk_size, VNA_CHUNK_SIZE, VNA_AXIS_NONE),
    VNA_FU32(C, index, VNA_FILE_MAX_CHUNKS - 1u, VNA_AXIS_NONE),
    VNA_FARR(C, proof, proof_n, &hash_schema, vna_hash_el_t, VNA_AXIS_PROVENANCE),
    VNA_FVAR(C, data, data_len, VNA_AXIS_NONE),
};
#undef C
const vna_schema_t vna_chunk_schema = {"chunk", 0x0032, 3, chunk_fields, 7};

static int32_t chunk_shift(uint32_t cs)
{
    if (cs < VNA_CHUNK_MIN || cs > VNA_CHUNK_SIZE || (cs & (cs - 1u))) return -1;
    int32_t s = 0;
    while ((1u << s) != cs) s++;
    return s;
}

uint32_t vna_file_nchunks(uint64_t size, uint32_t chunk_size)
{
    int32_t s = chunk_shift(chunk_size);
    if (s < 0) return 0;
    uint64_t n = (size + chunk_size - 1u) >> s; /* power-of-two divisor: a shift */
    if (n == 0) n = 1;
    return n > VNA_FILE_MAX_CHUNKS ? 0 : (uint32_t) n;
}

void vna_file_leaf(const uint8_t *chunk, uint32_t len, uint8_t out[32])
{
    uint8_t buf[1 + VNA_CID_RAW_LEN];
    buf[0] = 0x00;
    vna_cid_raw(chunk, len, buf + 1);
    sha256(buf, sizeof buf, out);
}

static void node_hash(const uint8_t l[32], const uint8_t r[32], uint8_t out[32])
{
    uint8_t buf[65];
    buf[0] = 0x01;
    vna_copy(buf + 1, l, 32);
    vna_copy(buf + 33, r, 32);
    sha256(buf, 65, out);
}

static uint32_t split_point(uint32_t n) /* largest power of two < n, n >= 2 */
{
    uint32_t k = 1;
    while ((k << 1) < n) k <<= 1;
    return k;
}

void vna_file_mth(const uint8_t (*leaves)[32], uint32_t n, uint8_t out[32])
{
    if (n == 1) {
        vna_copy(out, leaves[0], 32);
        return;
    }
    uint8_t l[32], r[32];
    uint32_t k = split_point(n);
    vna_file_mth(leaves, k, l);
    vna_file_mth(leaves + k, n - k, r);
    node_hash(l, r, out);
}

void vna_file_root(const uint8_t mth[32], uint64_t size, uint32_t chunk_size, vna_id_t *root)
{
    uint8_t buf[1 + 8 + 4 + 32];
    buf[0] = 0x02;
    vna_put64(buf + 1, size);
    vna_put32(buf + 9, chunk_size);
    vna_copy(buf + 13, mth, 32);
    sha256(buf, sizeof buf, root->b);
}

uint32_t vna_file_build(const uint8_t *data, uint64_t size, uint32_t chunk_size,
                        uint8_t (*leaves)[32], uint32_t max, vna_id_t *root)
{
    uint32_t n = vna_file_nchunks(size, chunk_size);
    if (n == 0 || n > max || (!data && size)) return 0;
    for (uint32_t i = 0; i < n; i++) {
        uint64_t off = (uint64_t) i * chunk_size;
        uint64_t rem = size - (off < size ? off : size);
        uint32_t len = rem < chunk_size ? (uint32_t) rem : chunk_size;
        vna_file_leaf(data ? data + off : 0, len, leaves[i]);
    }
    uint8_t mth[32];
    vna_file_mth((const uint8_t(*)[32]) leaves, n, mth);
    vna_file_root(mth, size, chunk_size, root);
    return n;
}

static uint32_t path(const uint8_t (*leaves)[32], uint32_t n, uint32_t m, vna_hash_el_t *out)
{
    if (n <= 1) return 0;
    uint32_t k = split_point(n);
    uint32_t d;
    if (m < k) {
        d = path(leaves, k, m, out);
        vna_file_mth(leaves + k, n - k, out[d].h);
    } else {
        d = path(leaves + k, n - k, m - k, out);
        vna_file_mth(leaves, k, out[d].h);
    }
    return d + 1;
}

uint32_t vna_file_proof(const uint8_t (*leaves)[32], uint32_t n, uint32_t index,
                        vna_hash_el_t *proof)
{
    if (!leaves || index >= n) return 0;
    return path(leaves, n, index, proof);
}

vna_status_t vna_file_verify_chunk(const vna_id_t *root, uint64_t size, uint32_t chunk_size,
                                   uint32_t index, const uint8_t *data, uint32_t len,
                                   const vna_hash_el_t *proof, uint32_t proof_n)
{
    uint32_t n = vna_file_nchunks(size, chunk_size);
    if (n == 0 || index >= n || proof_n > VNA_PROOF_MAX || (!data && len)) return VNA_ERR_PARSE;
    uint64_t off = (uint64_t) index * chunk_size;
    uint64_t rem = size - off;
    uint32_t expect = rem < chunk_size ? (uint32_t) rem : chunk_size;
    if (len != expect) return VNA_ERR_PARSE;
    uint8_t r[32];
    vna_file_leaf(data, len, r);
    /* RFC 9162 section 2.1.3.2 */
    uint32_t fn = index, sn = n - 1u;
    for (uint32_t i = 0; i < proof_n; i++) {
        if (sn == 0) return VNA_ERR_MERKLE;
        if ((fn & 1u) || fn == sn) {
            node_hash(proof[i].h, r, r);
            if (!(fn & 1u))
                while (fn != 0 && !(fn & 1u)) {
                    fn >>= 1;
                    sn >>= 1;
                }
        } else {
            node_hash(r, proof[i].h, r);
        }
        fn >>= 1;
        sn >>= 1;
    }
    if (sn != 0) return VNA_ERR_MERKLE;
    vna_id_t got;
    vna_file_root(r, size, chunk_size, &got);
    return vna_ct_eq(got.b, root->b, 32) ? VNA_OK : VNA_ERR_MERKLE;
}

vna_status_t vna_fetch_init(vna_fetch_t *f, const vna_id_t *root, uint64_t size,
                            uint32_t chunk_size, uint8_t *bitmap, uint32_t bitmap_len)
{
    uint32_t n = vna_file_nchunks(size, chunk_size);
    if (!f || !root || !bitmap || n == 0 || bitmap_len < (n + 7u) / 8u) return VNA_ERR_ARG;
    f->root = *root;
    f->size = size;
    f->chunk_size = chunk_size;
    f->n = n;
    f->bitmap = bitmap;
    vna_zero(bitmap, (n + 7u) / 8u);
    f->received = f->rejected = 0;
    return VNA_OK;
}

vna_status_t vna_fetch_accept(vna_fetch_t *f, const uint8_t *bytes, uint32_t len, vna_chunk_t *c)
{
    if (!f || !bytes || !c) return VNA_ERR_ARG;
    if (vna_schema_unpack(&vna_chunk_schema, bytes, len, c, 0) < 0) {
        f->rejected++;
        return VNA_ERR_PARSE;
    }
    if (!vna_id_eq(&c->root, &f->root) || c->file_size != f->size ||
        c->chunk_size != f->chunk_size || c->index >= f->n) {
        f->rejected++;
        return VNA_ERR_PARSE;
    }
    if (f->bitmap[c->index >> 3] & (1u << (c->index & 7u))) return VNA_ERR_DUP;
    vna_status_t st = vna_file_verify_chunk(&f->root, f->size, f->chunk_size, c->index, c->data,
                                            c->data_len, c->proof, c->proof_n);
    if (st != VNA_OK) {
        f->rejected++;
        return st;
    }
    f->bitmap[c->index >> 3] = (uint8_t) (f->bitmap[c->index >> 3] | (1u << (c->index & 7u)));
    f->received++;
    return VNA_OK;
}

bool vna_fetch_complete(const vna_fetch_t *f)
{
    return f->received == f->n;
}

int32_t vna_fetch_next(const vna_fetch_t *f)
{
    for (uint32_t i = 0; i < f->n; i++)
        if (!(f->bitmap[i >> 3] & (1u << (i & 7u)))) return (int32_t) i;
    return -1;
}

int32_t vna_file_serve(const vna_shared_file_t *sf, const vna_agreement_t *agr, vna_usage_t *us,
                       const vna_id_t *peer, uint64_t trust, const uint8_t *req, uint32_t req_len,
                       uint64_t now, vna_chunk_t *c, uint8_t *out, uint32_t cap)
{
    vna_chunk_req_t rq;
    if (!sf || !peer || !req || !c || !out) return VNA_ERR_ARG;
    if (vna_schema_unpack(&vna_chunk_req_schema, req, req_len, &rq, 0) < 0) return VNA_ERR_PARSE;
    if (!vna_id_eq(&rq.root, &sf->root) || rq.index >= sf->n) return VNA_ERR_PARSE;
    vna_status_t st =
        vna_agree_check(agr, us, peer, trust, VNA_RES_FILE, &rq.root, 1, 0, now, true);
    if (st != VNA_OK) return st;
    c->magic = VNA_CHUNK_MAGIC;
    c->root = sf->root;
    c->file_size = sf->size;
    c->chunk_size = sf->chunk_size;
    c->index = rq.index;
    c->proof_n = (uint16_t) vna_file_proof(sf->leaves, sf->n, rq.index, c->proof);
    uint64_t off = (uint64_t) rq.index * sf->chunk_size;
    uint64_t rem = sf->size - off;
    uint32_t len = rem < sf->chunk_size ? (uint32_t) rem : sf->chunk_size;
    vna_copy(c->data, sf->data + off, len);
    c->data_len = (uint16_t) len;
    return vna_schema_pack(&vna_chunk_schema, c, out, cap, true);
}
