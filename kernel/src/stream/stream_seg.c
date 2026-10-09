/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* stream_seg.c — segments, signed manifests, row generation (MDS ids 0..255
 * and RLNC ids beyond), the online decoder and the live/VOD cutter. See
 * stream_seg.h for the format and the HONEST LIMITS. Freestanding. */
#include "stream_seg.h"
#include "sha256.h"

/* ---- helpers ---- */

static void sg_copy(uint8_t *d, const uint8_t *s, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) d[i] = s[i];
}

static void sg_zero(uint8_t *d, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) d[i] = 0;
}

static bool sg_eq(const uint8_t *a, const uint8_t *b, uint32_t n)
{
    uint8_t acc = 0;
    for (uint32_t i = 0; i < n; i++) acc |= (uint8_t) (a[i] ^ b[i]);
    return acc == 0;
}

static void put16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t) v;
    p[1] = (uint8_t) (v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) p[i] = (uint8_t) (v >> (8 * i));
}

static void put64(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; i++) p[i] = (uint8_t) (v >> (8 * i));
}

static uint32_t get16(const uint8_t *p)
{
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8);
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) |
           ((uint32_t) p[3] << 24);
}

static uint64_t get64(const uint8_t *p)
{
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--) v = (v << 8) | p[i];
    return v;
}

/* ---- GF(2^8), poly 0x11d, the same tables as freight.c ---- */

static const uint8_t sg_exp[512] = {
    0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1d, 0x3a, 0x74, 0xe8, 0xcd, 0x87, 0x13, 0x26,
    0x4c, 0x98, 0x2d, 0x5a, 0xb4, 0x75, 0xea, 0xc9, 0x8f, 0x03, 0x06, 0x0c, 0x18, 0x30, 0x60, 0xc0,
    0x9d, 0x27, 0x4e, 0x9c, 0x25, 0x4a, 0x94, 0x35, 0x6a, 0xd4, 0xb5, 0x77, 0xee, 0xc1, 0x9f, 0x23,
    0x46, 0x8c, 0x05, 0x0a, 0x14, 0x28, 0x50, 0xa0, 0x5d, 0xba, 0x69, 0xd2, 0xb9, 0x6f, 0xde, 0xa1,
    0x5f, 0xbe, 0x61, 0xc2, 0x99, 0x2f, 0x5e, 0xbc, 0x65, 0xca, 0x89, 0x0f, 0x1e, 0x3c, 0x78, 0xf0,
    0xfd, 0xe7, 0xd3, 0xbb, 0x6b, 0xd6, 0xb1, 0x7f, 0xfe, 0xe1, 0xdf, 0xa3, 0x5b, 0xb6, 0x71, 0xe2,
    0xd9, 0xaf, 0x43, 0x86, 0x11, 0x22, 0x44, 0x88, 0x0d, 0x1a, 0x34, 0x68, 0xd0, 0xbd, 0x67, 0xce,
    0x81, 0x1f, 0x3e, 0x7c, 0xf8, 0xed, 0xc7, 0x93, 0x3b, 0x76, 0xec, 0xc5, 0x97, 0x33, 0x66, 0xcc,
    0x85, 0x17, 0x2e, 0x5c, 0xb8, 0x6d, 0xda, 0xa9, 0x4f, 0x9e, 0x21, 0x42, 0x84, 0x15, 0x2a, 0x54,
    0xa8, 0x4d, 0x9a, 0x29, 0x52, 0xa4, 0x55, 0xaa, 0x49, 0x92, 0x39, 0x72, 0xe4, 0xd5, 0xb7, 0x73,
    0xe6, 0xd1, 0xbf, 0x63, 0xc6, 0x91, 0x3f, 0x7e, 0xfc, 0xe5, 0xd7, 0xb3, 0x7b, 0xf6, 0xf1, 0xff,
    0xe3, 0xdb, 0xab, 0x4b, 0x96, 0x31, 0x62, 0xc4, 0x95, 0x37, 0x6e, 0xdc, 0xa5, 0x57, 0xae, 0x41,
    0x82, 0x19, 0x32, 0x64, 0xc8, 0x8d, 0x07, 0x0e, 0x1c, 0x38, 0x70, 0xe0, 0xdd, 0xa7, 0x53, 0xa6,
    0x51, 0xa2, 0x59, 0xb2, 0x79, 0xf2, 0xf9, 0xef, 0xc3, 0x9b, 0x2b, 0x56, 0xac, 0x45, 0x8a, 0x09,
    0x12, 0x24, 0x48, 0x90, 0x3d, 0x7a, 0xf4, 0xf5, 0xf7, 0xf3, 0xfb, 0xeb, 0xcb, 0x8b, 0x0b, 0x16,
    0x2c, 0x58, 0xb0, 0x7d, 0xfa, 0xe9, 0xcf, 0x83, 0x1b, 0x36, 0x6c, 0xd8, 0xad, 0x47, 0x8e, 0x01,
    0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1d, 0x3a, 0x74, 0xe8, 0xcd, 0x87, 0x13, 0x26, 0x4c,
    0x98, 0x2d, 0x5a, 0xb4, 0x75, 0xea, 0xc9, 0x8f, 0x03, 0x06, 0x0c, 0x18, 0x30, 0x60, 0xc0, 0x9d,
    0x27, 0x4e, 0x9c, 0x25, 0x4a, 0x94, 0x35, 0x6a, 0xd4, 0xb5, 0x77, 0xee, 0xc1, 0x9f, 0x23, 0x46,
    0x8c, 0x05, 0x0a, 0x14, 0x28, 0x50, 0xa0, 0x5d, 0xba, 0x69, 0xd2, 0xb9, 0x6f, 0xde, 0xa1, 0x5f,
    0xbe, 0x61, 0xc2, 0x99, 0x2f, 0x5e, 0xbc, 0x65, 0xca, 0x89, 0x0f, 0x1e, 0x3c, 0x78, 0xf0, 0xfd,
    0xe7, 0xd3, 0xbb, 0x6b, 0xd6, 0xb1, 0x7f, 0xfe, 0xe1, 0xdf, 0xa3, 0x5b, 0xb6, 0x71, 0xe2, 0xd9,
    0xaf, 0x43, 0x86, 0x11, 0x22, 0x44, 0x88, 0x0d, 0x1a, 0x34, 0x68, 0xd0, 0xbd, 0x67, 0xce, 0x81,
    0x1f, 0x3e, 0x7c, 0xf8, 0xed, 0xc7, 0x93, 0x3b, 0x76, 0xec, 0xc5, 0x97, 0x33, 0x66, 0xcc, 0x85,
    0x17, 0x2e, 0x5c, 0xb8, 0x6d, 0xda, 0xa9, 0x4f, 0x9e, 0x21, 0x42, 0x84, 0x15, 0x2a, 0x54, 0xa8,
    0x4d, 0x9a, 0x29, 0x52, 0xa4, 0x55, 0xaa, 0x49, 0x92, 0x39, 0x72, 0xe4, 0xd5, 0xb7, 0x73, 0xe6,
    0xd1, 0xbf, 0x63, 0xc6, 0x91, 0x3f, 0x7e, 0xfc, 0xe5, 0xd7, 0xb3, 0x7b, 0xf6, 0xf1, 0xff, 0xe3,
    0xdb, 0xab, 0x4b, 0x96, 0x31, 0x62, 0xc4, 0x95, 0x37, 0x6e, 0xdc, 0xa5, 0x57, 0xae, 0x41, 0x82,
    0x19, 0x32, 0x64, 0xc8, 0x8d, 0x07, 0x0e, 0x1c, 0x38, 0x70, 0xe0, 0xdd, 0xa7, 0x53, 0xa6, 0x51,
    0xa2, 0x59, 0xb2, 0x79, 0xf2, 0xf9, 0xef, 0xc3, 0x9b, 0x2b, 0x56, 0xac, 0x45, 0x8a, 0x09, 0x12,
    0x24, 0x48, 0x90, 0x3d, 0x7a, 0xf4, 0xf5, 0xf7, 0xf3, 0xfb, 0xeb, 0xcb, 0x8b, 0x0b, 0x16, 0x2c,
    0x58, 0xb0, 0x7d, 0xfa, 0xe9, 0xcf, 0x83, 0x1b, 0x36, 0x6c, 0xd8, 0xad, 0x47, 0x8e, 0x01, 0x02,
};

static const uint8_t sg_log[256] = {
    0x00, 0x00, 0x01, 0x19, 0x02, 0x32, 0x1a, 0xc6, 0x03, 0xdf, 0x33, 0xee, 0x1b, 0x68, 0xc7, 0x4b,
    0x04, 0x64, 0xe0, 0x0e, 0x34, 0x8d, 0xef, 0x81, 0x1c, 0xc1, 0x69, 0xf8, 0xc8, 0x08, 0x4c, 0x71,
    0x05, 0x8a, 0x65, 0x2f, 0xe1, 0x24, 0x0f, 0x21, 0x35, 0x93, 0x8e, 0xda, 0xf0, 0x12, 0x82, 0x45,
    0x1d, 0xb5, 0xc2, 0x7d, 0x6a, 0x27, 0xf9, 0xb9, 0xc9, 0x9a, 0x09, 0x78, 0x4d, 0xe4, 0x72, 0xa6,
    0x06, 0xbf, 0x8b, 0x62, 0x66, 0xdd, 0x30, 0xfd, 0xe2, 0x98, 0x25, 0xb3, 0x10, 0x91, 0x22, 0x88,
    0x36, 0xd0, 0x94, 0xce, 0x8f, 0x96, 0xdb, 0xbd, 0xf1, 0xd2, 0x13, 0x5c, 0x83, 0x38, 0x46, 0x40,
    0x1e, 0x42, 0xb6, 0xa3, 0xc3, 0x48, 0x7e, 0x6e, 0x6b, 0x3a, 0x28, 0x54, 0xfa, 0x85, 0xba, 0x3d,
    0xca, 0x5e, 0x9b, 0x9f, 0x0a, 0x15, 0x79, 0x2b, 0x4e, 0xd4, 0xe5, 0xac, 0x73, 0xf3, 0xa7, 0x57,
    0x07, 0x70, 0xc0, 0xf7, 0x8c, 0x80, 0x63, 0x0d, 0x67, 0x4a, 0xde, 0xed, 0x31, 0xc5, 0xfe, 0x18,
    0xe3, 0xa5, 0x99, 0x77, 0x26, 0xb8, 0xb4, 0x7c, 0x11, 0x44, 0x92, 0xd9, 0x23, 0x20, 0x89, 0x2e,
    0x37, 0x3f, 0xd1, 0x5b, 0x95, 0xbc, 0xcf, 0xcd, 0x90, 0x87, 0x97, 0xb2, 0xdc, 0xfc, 0xbe, 0x61,
    0xf2, 0x56, 0xd3, 0xab, 0x14, 0x2a, 0x5d, 0x9e, 0x84, 0x3c, 0x39, 0x53, 0x47, 0x6d, 0x41, 0xa2,
    0x1f, 0x2d, 0x43, 0xd8, 0xb7, 0x7b, 0xa4, 0x76, 0xc4, 0x17, 0x49, 0xec, 0x7f, 0x0c, 0x6f, 0xf6,
    0x6c, 0xa1, 0x3b, 0x52, 0x29, 0x9d, 0x55, 0xaa, 0xfb, 0x60, 0x86, 0xb1, 0xbb, 0xcc, 0x3e, 0x5a,
    0xcb, 0x59, 0x5f, 0xb0, 0x9c, 0xa9, 0xa0, 0x51, 0x0b, 0xf5, 0x16, 0xeb, 0x7a, 0x75, 0x2c, 0xd7,
    0x4f, 0xae, 0xd5, 0xe9, 0xe6, 0xe7, 0xad, 0xe8, 0x74, 0xd6, 0xf4, 0xea, 0xa8, 0x50, 0x58, 0xaf,
};

static uint8_t gf_inv(uint8_t a)
{
    if (a == 0) return 0;
    return sg_exp[255 - sg_log[a]];
}

/* dst ^= c * src */
static void gf_axpy(uint8_t *dst, const uint8_t *src, uint8_t c, uint32_t n)
{
    if (c == 0) return;
    uint32_t lc = sg_log[c];
    for (uint32_t i = 0; i < n; i++)
        if (src[i]) dst[i] ^= sg_exp[lc + sg_log[src[i]]];
}

static void gf_scale(uint8_t *v, uint8_t c, uint32_t n)
{
    uint32_t lc = sg_log[c];
    for (uint32_t i = 0; i < n; i++)
        if (v[i]) v[i] = sg_exp[lc + sg_log[v[i]]];
}

/* ---- geometry ---- */

uint32_t stream_seg_nfreights(uint32_t len, uint8_t k)
{
    if (k < 1 || k > STREAM_K_MAX) return 0;
    const uint32_t per = FREIGHT_CAPACITY(k);
    if (len > per * STREAM_SEG_MAX_FREIGHTS) return 0;
    uint32_t n = 1;
    while (len > per) {
        len -= per;
        n++;
    }
    return n;
}

uint32_t stream_freight_len(const stream_manifest_t *m, uint32_t fr)
{
    if (!m || fr >= m->nfreights) return 0;
    const uint32_t per = FREIGHT_CAPACITY(m->k);
    const uint32_t off = per * fr;
    if (off >= m->data_len) return 0;
    uint32_t take = m->data_len - off;
    return take > per ? per : take;
}

static uint64_t freight_id_from_cid(const ipfsn_cid_t *cid)
{
    return get64(cid->digest);
}

int stream_manifest_build(stream_manifest_t *m, uint64_t stream_id, uint32_t seq,
                          uint64_t t_start_us, uint32_t dur_us, uint8_t flags, uint8_t k,
                          const uint8_t prev_sha256[STREAM_SHA_BYTES], const uint8_t *data,
                          uint32_t len)
{
    if (!m || (len && !data) || (flags & ~STREAM_MF_ALL)) return STREAM_ERR_ARG;
    uint32_t n = stream_seg_nfreights(len, k);
    if (n == 0) return STREAM_ERR_ARG;
    if (ipfsn_cid_sha256(IPFSN_MC_RAW, data, len, &m->cid) != IPFSN_OK) return STREAM_ERR_ARG;
    m->stream_id = stream_id;
    m->seq = seq;
    m->t_start_us = t_start_us;
    m->dur_us = dur_us;
    m->data_len = len;
    m->flags = flags;
    m->k = k;
    m->nfreights = (uint8_t) n;
    m->freight_id = freight_id_from_cid(&m->cid);
    if (prev_sha256)
        sg_copy(m->prev_sha256, prev_sha256, STREAM_SHA_BYTES);
    else
        sg_zero(m->prev_sha256, STREAM_SHA_BYTES);
    const uint32_t per = FREIGHT_CAPACITY(k);
    for (uint32_t f = 0; f < n; f++)
        sha256(data + per * f, stream_freight_len(m, f), m->freight_sha256[f]);
    return STREAM_OK;
}

static int manifest_body(const stream_manifest_t *m, uint8_t *out, uint32_t cap)
{
    if (m->k < 1 || m->k > STREAM_K_MAX || m->nfreights < 1 ||
        m->nfreights > STREAM_SEG_MAX_FREIGHTS || (m->flags & ~STREAM_MF_ALL))
        return STREAM_ERR_ARG;
    if (stream_seg_nfreights(m->data_len, m->k) != m->nfreights) return STREAM_ERR_ARG;
    uint8_t cidb[IPFSN_CID_BIN_MAX];
    int cl = ipfsn_cid_encode(&m->cid, cidb, sizeof cidb);
    if (cl <= 0) return STREAM_ERR_ARG;
    uint32_t need = STREAM_MANIFEST_FIXED + (uint32_t) cl + m->nfreights * STREAM_SHA_BYTES;
    if (need > cap) return STREAM_ERR_SPACE;
    out[0] = 'Z';
    out[1] = 'X';
    out[2] = 'S';
    out[3] = 'M';
    out[4] = STREAM_MANIFEST_VER;
    out[5] = m->flags;
    out[6] = m->k;
    out[7] = m->nfreights;
    put64(out + 8, m->stream_id);
    put32(out + 16, m->seq);
    put32(out + 20, m->dur_us);
    put64(out + 24, m->t_start_us);
    put32(out + 32, m->data_len);
    put64(out + 36, m->freight_id);
    sg_copy(out + 44, m->prev_sha256, STREAM_SHA_BYTES);
    out[76] = (uint8_t) cl;
    sg_copy(out + 77, cidb, (uint32_t) cl);
    uint32_t p = 77 + (uint32_t) cl;
    for (uint32_t f = 0; f < m->nfreights; f++, p += STREAM_SHA_BYTES)
        sg_copy(out + p, m->freight_sha256[f], STREAM_SHA_BYTES);
    return (int) p;
}

int stream_manifest_serialize(const stream_manifest_t *m, uint8_t *out, uint32_t cap,
                              stream_sign_fn sign, void *sign_ctx,
                              uint8_t body_sha[STREAM_SHA_BYTES])
{
    if (!m || !out) return STREAM_ERR_ARG;
    int bl = manifest_body(m, out, cap);
    if (bl < 0) return bl;
    uint32_t p = (uint32_t) bl;
    if (p + 2 > cap) return STREAM_ERR_SPACE;
    uint32_t sl = 0;
    if (sign) {
        uint32_t room = cap - p - 2;
        if (room > STREAM_SIG_MAX) room = STREAM_SIG_MAX;
        if (sign(sign_ctx, out, p, out + p + 2, room, &sl) != 0) return STREAM_ERR_SIG;
        if (sl > room) return STREAM_ERR_SIG;
    }
    put16(out + p, sl);
    if (body_sha) sha256(out, p, body_sha);
    return (int) (p + 2 + sl);
}

int stream_manifest_parse(const uint8_t *in, uint32_t len, stream_manifest_t *m,
                          stream_verify_fn verify, void *verify_ctx,
                          uint8_t body_sha[STREAM_SHA_BYTES])
{
    if (!in || !m || !verify) return STREAM_ERR_ARG;
    if (len < STREAM_MANIFEST_FIXED + 2) return STREAM_ERR_FORMAT;
    if (in[0] != 'Z' || in[1] != 'X' || in[2] != 'S' || in[3] != 'M') return STREAM_ERR_FORMAT;
    if (in[4] != STREAM_MANIFEST_VER || (in[5] & ~STREAM_MF_ALL)) return STREAM_ERR_FORMAT;
    const uint8_t k = in[6], nfr = in[7];
    if (k < 1 || k > STREAM_K_MAX || nfr < 1 || nfr > STREAM_SEG_MAX_FREIGHTS)
        return STREAM_ERR_FORMAT;
    const uint32_t dlen = get32(in + 32);
    if (stream_seg_nfreights(dlen, k) != nfr) return STREAM_ERR_FORMAT;
    const uint32_t cl = in[76];
    if (cl < 1 || cl > IPFSN_CID_BIN_MAX) return STREAM_ERR_FORMAT;
    const uint32_t body = STREAM_MANIFEST_FIXED + cl + (uint32_t) nfr * STREAM_SHA_BYTES;
    if (body + 2 > len) return STREAM_ERR_FORMAT;
    ipfsn_cid_t cid;
    if (ipfsn_cid_decode_exact(in + 77, cl, &cid) != IPFSN_OK) return STREAM_ERR_FORMAT;
    if (cid.version != 1 || cid.codec != IPFSN_MC_RAW || cid.mh_code != IPFSN_MH_SHA2_256 ||
        cid.digest_len != 32)
        return STREAM_ERR_FORMAT;
    if (get64(in + 36) != freight_id_from_cid(&cid)) return STREAM_ERR_FORMAT;
    const uint32_t sl = get16(in + body);
    if (sl > STREAM_SIG_MAX || body + 2 + sl != len) return STREAM_ERR_FORMAT;
    if (!verify(verify_ctx, in, body, in + body + 2, sl)) return STREAM_ERR_SIG;

    m->flags = in[5];
    m->k = k;
    m->nfreights = nfr;
    m->stream_id = get64(in + 8);
    m->seq = get32(in + 16);
    m->dur_us = get32(in + 20);
    m->t_start_us = get64(in + 24);
    m->data_len = dlen;
    m->freight_id = get64(in + 36);
    sg_copy(m->prev_sha256, in + 44, STREAM_SHA_BYTES);
    m->cid.version = cid.version;
    m->cid.codec = cid.codec;
    m->cid.mh_code = cid.mh_code;
    m->cid.digest_len = cid.digest_len;
    sg_copy(m->cid.digest, cid.digest, IPFSN_DIGEST_MAX);
    for (uint32_t f = 0; f < nfr; f++)
        sg_copy(m->freight_sha256[f], in + 77 + cl + f * STREAM_SHA_BYTES, STREAM_SHA_BYTES);
    if (body_sha) sha256(in, body, body_sha);
    return STREAM_OK;
}

bool stream_manifest_chains(const stream_manifest_t *m, const uint8_t prev[STREAM_SHA_BYTES])
{
    if (!m || !prev) return false;
    return sg_eq(m->prev_sha256, prev, STREAM_SHA_BYTES);
}

int stream_seg_verify(const stream_manifest_t *m, const uint8_t *data, uint32_t len)
{
    if (!m || (len && !data)) return STREAM_ERR_ARG;
    if (len != m->data_len) return STREAM_ERR_HASH;
    return ipfsn_cid_verify(&m->cid, data, len) == IPFSN_OK ? STREAM_OK : STREAM_ERR_HASH;
}

int stream_freight_header(const stream_manifest_t *m, uint32_t fr, freight_header_t *h)
{
    if (!m || !h || fr >= m->nfreights) return STREAM_ERR_ARG;
    h->version = FREIGHT_VERSION;
    h->k = m->k;
    h->flags = 0;
    h->payload_len = (uint16_t) stream_freight_len(m, fr);
    h->seq = (uint16_t) fr;
    h->seq_count = m->nfreights;
    h->freight_id = m->freight_id;
    h->expand_limit = 0;
    sg_copy(h->payload_sha256, m->freight_sha256[fr], STREAM_SHA_BYTES);
    return freight_header_check(h) == FREIGHT_OK ? STREAM_OK : STREAM_ERR_ARG;
}

/* ---- coefficient vectors ---- */

/* RLNC coefficients: xorshift64* seeded from (freight id, freight, id),
 * eight coefficients per output word, zero mapped to one. */
static uint64_t rl_seed(const stream_manifest_t *m, uint32_t fr, uint32_t id)
{
    uint64_t z =
        m->freight_id ^ ((uint64_t) fr << 40) ^ ((uint64_t) id << 8) ^ 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    return z ? z : 0x2545F4914F6CDD1Dull;
}

static void coef_vector(const stream_manifest_t *m, uint32_t fr, uint32_t id, uint8_t *v)
{
    const uint32_t k = m->k;
    if (id < k) {
        for (uint32_t j = 0; j < k; j++) v[j] = j == id ? 1 : 0;
    } else if (id < STREAM_MDS_IDS) {
        for (uint32_t j = 0; j < k; j++) v[j] = gf_inv((uint8_t) (id ^ j));
    } else {
        uint64_t s = rl_seed(m, fr, id), w = 0;
        for (uint32_t j = 0; j < k; j++) {
            if ((j & 7) == 0) {
                s ^= s >> 12;
                s ^= s << 25;
                s ^= s >> 27;
                w = s * 0x2545F4914F6CDD1Dull;
            }
            uint8_t b = (uint8_t) (w >> (8 * (j & 7)));
            v[j] = b ? b : 1;
        }
    }
}

uint8_t stream_row_coef(const stream_manifest_t *m, uint32_t fr, uint32_t id, uint32_t j)
{
    if (!m || m->k < 1 || m->k > STREAM_K_MAX || j >= m->k || id > 0xFFFFu) return 0;
    uint8_t v[STREAM_K_MAX];
    coef_vector(m, fr, id, v);
    return v[j];
}

int stream_seg_row(const stream_manifest_t *m, const uint8_t *data, uint32_t len, uint32_t fr,
                   uint32_t id, uint8_t out[STREAM_ROW_BYTES])
{
    if (!m || !out || (len && !data) || fr >= m->nfreights || id > 0xFFFFu) return STREAM_ERR_ARG;
    if (m->k < 1 || m->k > STREAM_K_MAX || len != m->data_len) return STREAM_ERR_ARG;
    const uint32_t k = m->k, per = FREIGHT_CAPACITY(m->k), base = per * fr;
    const uint32_t flen = stream_freight_len(m, fr);
    sg_zero(out, STREAM_ROW_BYTES);
    uint8_t v[STREAM_K_MAX];
    coef_vector(m, fr, id, v);
    for (uint32_t j = 0; j < k; j++) {
        if (!v[j]) continue;
        uint8_t row[STREAM_ROW_BYTES];
        for (uint32_t b = 0; b < STREAM_ROW_BYTES; b++) {
            uint32_t off = j * STREAM_ROW_BYTES + b;
            row[b] = off < flen ? data[base + off] : 0;
        }
        gf_axpy(out, row, v[j], STREAM_ROW_BYTES);
    }
    return STREAM_OK;
}

/* ---- online decoder ---- */

int stream_dec_init(stream_dec_t *d, const stream_manifest_t *m, uint32_t fr, uint8_t *work,
                    uint32_t work_len)
{
    if (!d || !m || !work || fr >= m->nfreights || m->k < 1 || m->k > STREAM_K_MAX)
        return STREAM_ERR_ARG;
    if (work_len < STREAM_DEC_WORK(m->k)) return STREAM_ERR_SPACE;
    d->m = m;
    d->fr = fr;
    d->k = m->k;
    d->rank = 0;
    d->work = work;
    sg_zero(d->piv, sizeof d->piv);
    return STREAM_OK;
}

int stream_dec_add(stream_dec_t *d, uint32_t id, const uint8_t row[STREAM_ROW_BYTES])
{
    if (!d || !row || id > 0xFFFFu) return STREAM_ERR_ARG;
    const uint32_t k = d->k, w = k + STREAM_ROW_BYTES;
    if (d->rank >= k) return 0;
    uint8_t v[STREAM_K_MAX + STREAM_ROW_BYTES];
    coef_vector(d->m, d->fr, id, v);
    sg_copy(v + k, row, STREAM_ROW_BYTES);
    for (uint32_t c = 0; c < k; c++) {
        if (!v[c] || !d->piv[c]) continue;
        const uint8_t *pr = d->work + (uint32_t) (d->piv[c] - 1) * w;
        gf_axpy(v + c, pr + c, v[c], w - c);
    }
    uint32_t c = 0;
    while (c < k && v[c] == 0) c++;
    if (c == k) return 0;
    if (v[c] != 1) gf_scale(v + c, gf_inv(v[c]), w - c);
    uint8_t *dst = d->work + (uint32_t) d->rank * w;
    sg_copy(dst, v, w);
    d->piv[c] = (uint8_t) (d->rank + 1);
    d->rank++;
    return 1;
}

bool stream_dec_ready(const stream_dec_t *d)
{
    return d && d->rank == d->k;
}

int stream_dec_finish(stream_dec_t *d, uint8_t *out, uint32_t cap)
{
    if (!d) return STREAM_ERR_ARG;
    if (d->rank < d->k) return STREAM_ERR_TOO_FEW;
    const uint32_t k = d->k, w = k + STREAM_ROW_BYTES;
    const uint32_t flen = stream_freight_len(d->m, d->fr);
    if (flen && !out) return STREAM_ERR_ARG;
    if (cap < flen) return STREAM_ERR_SPACE;
    /* Back-substitution on the payload columns only. */
    for (uint32_t c = k; c-- > 0;) {
        uint8_t *r = d->work + (uint32_t) (d->piv[c] - 1) * w;
        for (uint32_t c2 = c + 1; c2 < k; c2++) {
            if (!r[c2]) continue;
            const uint8_t *x = d->work + (uint32_t) (d->piv[c2] - 1) * w + k;
            gf_axpy(r + k, x, r[c2], STREAM_ROW_BYTES);
            r[c2] = 0;
        }
    }
    uint8_t pad = 0;
    for (uint32_t j = 0; j < k; j++) {
        const uint8_t *x = d->work + (uint32_t) (d->piv[j] - 1) * w + k;
        for (uint32_t b = 0; b < STREAM_ROW_BYTES; b++) {
            uint32_t off = j * STREAM_ROW_BYTES + b;
            if (off < flen)
                out[off] = x[b];
            else
                pad |= x[b];
        }
    }
    uint8_t h[STREAM_SHA_BYTES];
    sha256(out, flen, h);
    if (pad || !sg_eq(h, d->m->freight_sha256[d->fr], STREAM_SHA_BYTES)) return STREAM_ERR_HASH;
    return STREAM_OK;
}

/* ---- segmentation ---- */

int stream_cut_init(stream_cutter_t *c, uint8_t *buf, uint32_t cap, uint32_t target_us,
                    uint32_t max_us)
{
    if (!c || !buf || cap == 0 || target_us == 0 || max_us < target_us) return STREAM_ERR_ARG;
    c->buf = buf;
    c->cap = cap;
    c->len = 0;
    c->target_us = target_us;
    c->max_us = max_us;
    c->t0 = 0;
    c->last_pts = 0;
    c->seq = 0;
    c->open = false;
    c->key_start = false;
    return STREAM_OK;
}

static void cut_emit(stream_cutter_t *c, uint64_t end, stream_seg_out_t *out)
{
    uint64_t d = end - c->t0;
    out->data = c->buf;
    out->len = c->len;
    out->seq = c->seq;
    out->t_start_us = c->t0;
    out->dur_us = d > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t) d;
    out->key_start = c->key_start;
}

int stream_cut_push(stream_cutter_t *c, const uint8_t *unit, uint32_t len, uint64_t pts_us,
                    bool keyframe, stream_seg_out_t *out)
{
    if (!c || !out || (len && !unit)) return STREAM_ERR_ARG;
    if (!c->open) {
        if (c->seq && pts_us < c->last_pts) return STREAM_ERR_ARG;
        if (len > c->cap) return STREAM_ERR_SPACE;
        c->open = true;
        c->t0 = pts_us;
        c->last_pts = pts_us;
        c->key_start = keyframe;
        c->len = 0;
    } else {
        if (pts_us < c->last_pts) return STREAM_ERR_ARG;
        const uint64_t el = pts_us - c->t0;
        if ((keyframe && el >= c->target_us) || el >= c->max_us || len > c->cap - c->len) {
            cut_emit(c, pts_us, out);
            return STREAM_CUT_READY;
        }
        c->last_pts = pts_us;
    }
    sg_copy(c->buf + c->len, unit, len);
    c->len += len;
    return STREAM_CUT_NONE;
}

void stream_cut_next(stream_cutter_t *c)
{
    if (!c || !c->open) return;
    c->open = false;
    c->seq++;
    c->len = 0;
}

int stream_cut_flush(stream_cutter_t *c, uint32_t tail_us, stream_seg_out_t *out)
{
    if (!c || !out) return STREAM_ERR_ARG;
    if (!c->open) return STREAM_CUT_NONE;
    cut_emit(c, c->last_pts + tail_us, out);
    c->open = false;
    c->seq++;
    return STREAM_CUT_READY;
}

/* field copies: a struct assignment may become a memcpy call */
static void vod_put(stream_vod_seg_t *d, const stream_vod_seg_t *c)
{
    d->first_unit = c->first_unit;
    d->nunits = c->nunits;
    d->offset = c->offset;
    d->len = c->len;
    d->t_start_us = c->t_start_us;
    d->dur_us = c->dur_us;
    d->key_start = c->key_start;
}

static void vod_zero(stream_vod_seg_t *d)
{
    d->first_unit = d->nunits = d->offset = d->len = d->dur_us = 0;
    d->t_start_us = 0;
    d->key_start = false;
}

int stream_vod_plan(const stream_unit_t *units, uint32_t n, uint32_t target_us, uint32_t max_us,
                    uint32_t max_bytes, uint32_t tail_us, stream_vod_seg_t *segs, uint32_t cap)
{
    if ((n && !units) || (cap && !segs) || target_us == 0 || max_us < target_us || !max_bytes)
        return STREAM_ERR_ARG;
    uint32_t ns = 0;
    bool open = false;
    stream_vod_seg_t cur;
    vod_zero(&cur);
    uint64_t last = 0;
    for (uint32_t i = 0; i < n; i++) {
        const stream_unit_t *u = &units[i];
        if (u->len > max_bytes) return STREAM_ERR_SPACE;
        if (i) {
            if (u->offset != units[i - 1].offset + units[i - 1].len) return STREAM_ERR_FORMAT;
            if (u->offset < units[i - 1].offset) return STREAM_ERR_FORMAT; /* wrapped */
            if (u->pts_us < last) return STREAM_ERR_FORMAT;
        }
        if (open) {
            const uint64_t el = u->pts_us - cur.t_start_us;
            if ((u->keyframe && el >= target_us) || el >= max_us || u->len > max_bytes - cur.len) {
                if (ns >= cap) return STREAM_ERR_SPACE;
                cur.dur_us = el > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t) el;
                vod_put(&segs[ns++], &cur);
                open = false;
            }
        }
        if (!open) {
            open = true;
            cur.first_unit = i;
            cur.nunits = 0;
            cur.offset = u->offset;
            cur.len = 0;
            cur.t_start_us = u->pts_us;
            cur.key_start = u->keyframe;
        }
        cur.nunits++;
        cur.len += u->len;
        last = u->pts_us;
    }
    if (open) {
        if (ns >= cap) return STREAM_ERR_SPACE;
        uint64_t el = last + tail_us - cur.t_start_us;
        cur.dur_us = el > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t) el;
        vod_put(&segs[ns++], &cur);
    }
    return (int) ns;
}
