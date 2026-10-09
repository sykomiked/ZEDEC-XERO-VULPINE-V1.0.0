/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* freight.c — GF(2^8), the systematic Cauchy Reed-Solomon row code, the
 * freight header and multi-freight split/join. See freight.h for the rules
 * and the HONEST LIMITS. Freestanding: no libc, no division, caller buffers. */
#include "freight.h"
#include "sha256.h"

/* ---- small helpers (freestanding: no libc) ---- */

static void fr_copy(uint8_t *d, const uint8_t *s, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) d[i] = s[i];
}

static void fr_zero(uint8_t *d, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) d[i] = 0;
}

static bool fr_eq(const uint8_t *a, const uint8_t *b, uint32_t n)
{
    uint8_t acc = 0;
    for (uint32_t i = 0; i < n; i++) acc |= (uint8_t) (a[i] ^ b[i]);
    return acc == 0;
}

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t) v;
    p[1] = (uint8_t) (v >> 8);
}

static uint16_t get16(const uint8_t *p)
{
    return (uint16_t) (p[0] | (p[1] << 8));
}

static void put64(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; i++) p[i] = (uint8_t) (v >> (8 * i));
}

static uint64_t get64(const uint8_t *p)
{
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--) v = (v << 8) | p[i];
    return v;
}

/* ---- GF(2^8) ---- */

static const uint8_t gf_exp[512] = {
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

static const uint8_t gf_log[256] = {
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

uint8_t freight_gf_mul(uint8_t a, uint8_t b)
{
    if (a == 0 || b == 0) return 0;
    return gf_exp[gf_log[a] + gf_log[b]];
}

uint8_t freight_gf_inv(uint8_t a)
{
    if (a == 0) return 0;
    return gf_exp[255 - gf_log[a]];
}

uint8_t freight_gf_div(uint8_t a, uint8_t b)
{
    if (a == 0 || b == 0) return 0;
    return gf_exp[gf_log[a] + 255 - gf_log[b]];
}

/* Systematic generator: identity on the k data rows, Cauchy 1 / (r ^ j) on
 * the parity rows. With x_r = r (r >= k) and y_j = j (j < k) all x and y are
 * distinct field elements, so r ^ j != 0 and every square submatrix of the
 * parity block is a Cauchy matrix, hence invertible: any k rows decode. */
uint8_t freight_gen_coef(uint8_t k, uint32_t r, uint32_t j)
{
    if (r < k) return r == j ? 1 : 0;
    return freight_gf_inv((uint8_t) (r ^ j));
}

/* ---- header ---- */

int freight_header_check(const freight_header_t *h)
{
    if (!h) return FREIGHT_ERR_ARG;
    if (h->version != FREIGHT_VERSION) return FREIGHT_ERR_FORMAT;
    if (h->k < 1 || h->k > FREIGHT_ROWS) return FREIGHT_ERR_FORMAT;
    if (h->flags & ~FREIGHT_F_OPSTREAM) return FREIGHT_ERR_FORMAT;
    if (h->payload_len > FREIGHT_CAPACITY(h->k)) return FREIGHT_ERR_FORMAT;
    if (h->seq_count < 1 || h->seq_count > FREIGHT_MAX_SET) return FREIGHT_ERR_FORMAT;
    if (h->seq >= h->seq_count) return FREIGHT_ERR_FORMAT;
    if (!(h->flags & FREIGHT_F_OPSTREAM) && h->expand_limit != 0) return FREIGHT_ERR_FORMAT;
    return FREIGHT_OK;
}

int freight_header_init(freight_header_t *h, uint64_t freight_id, uint8_t k, uint8_t flags,
                        uint64_t expand_limit, const uint8_t *payload, uint32_t len)
{
    if (!h || (len && !payload)) return FREIGHT_ERR_ARG;
    if (k < 1 || k > FREIGHT_ROWS || len > FREIGHT_CAPACITY(k)) return FREIGHT_ERR_ARG;
    h->version = FREIGHT_VERSION;
    h->k = k;
    h->flags = flags;
    h->payload_len = (uint16_t) len;
    h->seq = 0;
    h->seq_count = 1;
    h->freight_id = freight_id;
    h->expand_limit = expand_limit;
    sha256(payload, len, h->payload_sha256);
    return freight_header_check(h) == FREIGHT_OK ? FREIGHT_OK : FREIGHT_ERR_ARG;
}

int freight_header_serialize(const freight_header_t *h, uint8_t out[FREIGHT_HEADER_BYTES])
{
    int rc = freight_header_check(h);
    if (rc) return rc;
    if (!out) return FREIGHT_ERR_ARG;
    out[0] = 'Z';
    out[1] = 'X';
    out[2] = 'F';
    out[3] = 'H';
    out[4] = h->version;
    out[5] = h->k;
    out[6] = h->flags;
    out[7] = 0;
    put16(out + 8, h->payload_len);
    put16(out + 10, h->seq);
    put16(out + 12, h->seq_count);
    put16(out + 14, 0);
    put64(out + 16, h->freight_id);
    put64(out + 24, h->expand_limit);
    fr_copy(out + 32, h->payload_sha256, FREIGHT_SHA256_BYTES);
    return FREIGHT_OK;
}

int freight_header_parse(const uint8_t in[FREIGHT_HEADER_BYTES], freight_header_t *h)
{
    if (!in || !h) return FREIGHT_ERR_ARG;
    if (in[0] != 'Z' || in[1] != 'X' || in[2] != 'F' || in[3] != 'H') return FREIGHT_ERR_FORMAT;
    if (in[7] != 0 || in[14] != 0 || in[15] != 0) return FREIGHT_ERR_FORMAT;
    freight_header_t t;
    t.version = in[4];
    t.k = in[5];
    t.flags = in[6];
    t.payload_len = get16(in + 8);
    t.seq = get16(in + 10);
    t.seq_count = get16(in + 12);
    t.freight_id = get64(in + 16);
    t.expand_limit = get64(in + 24);
    fr_copy(t.payload_sha256, in + 32, FREIGHT_SHA256_BYTES);
    int rc = freight_header_check(&t);
    if (rc) return rc;
    /* Field by field: a struct assignment may become a memcpy call. */
    h->version = t.version;
    h->k = t.k;
    h->flags = t.flags;
    h->payload_len = t.payload_len;
    h->seq = t.seq;
    h->seq_count = t.seq_count;
    h->freight_id = t.freight_id;
    h->expand_limit = t.expand_limit;
    fr_copy(h->payload_sha256, t.payload_sha256, FREIGHT_SHA256_BYTES);
    return FREIGHT_OK;
}

/* ---- erasure code ---- */

int freight_encode(const freight_header_t *h, const uint8_t *payload, uint8_t out[FREIGHT_BYTES])
{
    int rc = freight_header_check(h);
    if (rc) return rc;
    if (!out || (h->payload_len && !payload)) return FREIGHT_ERR_ARG;
    uint8_t d[SHA256_DIGEST_LEN];
    sha256(payload, h->payload_len, d);
    if (!fr_eq(d, h->payload_sha256, SHA256_DIGEST_LEN)) return FREIGHT_ERR_HASH;

    const uint32_t k = h->k, len = h->payload_len;
    /* Data rows: the payload verbatim, zero padded. */
    for (uint32_t r = 0; r < k; r++) {
        uint8_t *row = out + r * FREIGHT_PACKET_BYTES;
        row[0] = (uint8_t) r;
        for (uint32_t b = 0; b < FREIGHT_ROW_PAYLOAD; b++) {
            uint32_t off = r * FREIGHT_ROW_PAYLOAD + b;
            row[1 + b] = off < len ? payload[off] : 0;
        }
    }
    /* Parity rows: parity[r][b] = sum_j coef(r, j) * data[j][b]. */
    for (uint32_t r = k; r < FREIGHT_ROWS; r++) {
        uint8_t *row = out + r * FREIGHT_PACKET_BYTES;
        row[0] = (uint8_t) r;
        fr_zero(row + 1, FREIGHT_ROW_PAYLOAD);
        for (uint32_t j = 0; j < k; j++) {
            uint8_t c = freight_gen_coef((uint8_t) k, r, j);
            const uint8_t *src = out + j * FREIGHT_PACKET_BYTES + 1;
            if (c == 0) continue;
            uint32_t lc = gf_log[c];
            for (uint32_t b = 0; b < FREIGHT_ROW_PAYLOAD; b++)
                if (src[b]) row[1 + b] ^= gf_exp[lc + gf_log[src[b]]];
        }
    }
    return FREIGHT_OK;
}

/* Row op: dst ^= c * src over n bytes. */
static void row_axpy(uint8_t *dst, const uint8_t *src, uint8_t c, uint32_t n)
{
    if (c == 0) return;
    uint32_t lc = gf_log[c];
    for (uint32_t i = 0; i < n; i++)
        if (src[i]) dst[i] ^= gf_exp[lc + gf_log[src[i]]];
}

static void row_scale(uint8_t *row, uint8_t c, uint32_t n)
{
    uint32_t lc = gf_log[c];
    for (uint32_t i = 0; i < n; i++)
        if (row[i]) row[i] = gf_exp[lc + gf_log[row[i]]];
}

int freight_decode(const freight_header_t *h, const uint8_t *packets, uint32_t npackets,
                   uint8_t *out, uint32_t out_cap, uint8_t *work, uint32_t work_len)
{
    int rc = freight_header_check(h);
    if (rc) return rc;
    if ((npackets && !packets) || (h->payload_len && !out) || !work) return FREIGHT_ERR_ARG;
    const uint32_t k = h->k, w = k + FREIGHT_ROW_PAYLOAD;
    if (work_len < FREIGHT_DECODE_WORK(k)) return FREIGHT_ERR_SPACE;
    if (out_cap < h->payload_len) return FREIGHT_ERR_SPACE;

    /* Pick the first k distinct valid rows. */
    uint8_t seen[(FREIGHT_ROWS + 7) / 8];
    fr_zero(seen, sizeof seen);
    uint32_t have = 0;
    for (uint32_t p = 0; p < npackets && have < k; p++) {
        const uint8_t *pk = packets + (uint64_t) p * FREIGHT_PACKET_BYTES;
        uint32_t r = pk[0];
        if (r >= FREIGHT_ROWS) continue;
        if (seen[r >> 3] & (1u << (r & 7))) continue;
        seen[r >> 3] |= (uint8_t) (1u << (r & 7));
        uint8_t *a = work + have * w;
        for (uint32_t j = 0; j < k; j++) a[j] = freight_gen_coef((uint8_t) k, r, j);
        fr_copy(a + k, pk + 1, FREIGHT_ROW_PAYLOAD);
        have++;
    }
    if (have < k) return FREIGHT_ERR_TOO_FEW;

    /* Gauss-Jordan on [M | Y]: afterwards row i holds data row i. */
    for (uint32_t c = 0; c < k; c++) {
        uint32_t p = c;
        while (p < k && work[p * w + c] == 0) p++;
        if (p == k) return FREIGHT_ERR_SINGULAR;
        if (p != c) {
            uint8_t *x = work + p * w, *y = work + c * w;
            for (uint32_t i = c; i < w; i++) {
                uint8_t t = x[i];
                x[i] = y[i];
                y[i] = t;
            }
        }
        uint8_t *pr = work + c * w;
        if (pr[c] != 1) row_scale(pr + c, freight_gf_inv(pr[c]), w - c);
        for (uint32_t r = 0; r < k; r++) {
            if (r == c) continue;
            uint8_t *rr = work + r * w;
            if (rr[c]) row_axpy(rr + c, pr + c, rr[c], w - c);
        }
    }

    /* Emit the payload; the padding past payload_len must be zero. */
    const uint32_t len = h->payload_len;
    uint8_t pad = 0;
    for (uint32_t r = 0; r < k; r++) {
        const uint8_t *y = work + r * w + k;
        for (uint32_t b = 0; b < FREIGHT_ROW_PAYLOAD; b++) {
            uint32_t off = r * FREIGHT_ROW_PAYLOAD + b;
            if (off < len)
                out[off] = y[b];
            else
                pad |= y[b];
        }
    }
    uint8_t d[SHA256_DIGEST_LEN];
    sha256(out, len, d);
    if (pad || !fr_eq(d, h->payload_sha256, SHA256_DIGEST_LEN)) return FREIGHT_ERR_HASH;
    return FREIGHT_OK;
}

/* ---- multi-freight ---- */

uint32_t freight_set_count(uint64_t len, uint8_t k)
{
    if (k < 1 || k > FREIGHT_ROWS) return 0;
    const uint64_t per = FREIGHT_CAPACITY(k);
    /* ceil(len / per) without division: the bound check keeps the loop short. */
    if (len > per * FREIGHT_MAX_SET) return 0;
    uint32_t n = 1;
    while (len > per) {
        len -= per;
        n++;
    }
    return n;
}

int freight_split(uint64_t freight_id, uint8_t k, uint8_t flags, uint64_t expand_limit,
                  const uint8_t *data, uint64_t len, uint32_t seq, freight_header_t *h,
                  uint8_t packets[FREIGHT_BYTES])
{
    if (!h || !packets || (len && !data)) return FREIGHT_ERR_ARG;
    uint32_t n = freight_set_count(len, k);
    if (n == 0 || seq >= n) return FREIGHT_ERR_ARG;
    const uint64_t per = FREIGHT_CAPACITY(k);
    const uint64_t off = per * seq;
    uint64_t take = len - off;
    if (take > per) take = per;
    int rc =
        freight_header_init(h, freight_id, k, flags, expand_limit, data + off, (uint32_t) take);
    if (rc) return rc;
    h->seq = (uint16_t) seq;
    h->seq_count = (uint16_t) n;
    return freight_encode(h, data + off, packets);
}

void freight_join_init(freight_join_t *j, uint8_t *out, uint64_t cap)
{
    if (!j) return;
    j->out = out;
    j->cap = out ? cap : 0;
    j->started = false;
    j->k = 0;
    j->flags = 0;
    j->count = 0;
    j->received = 0;
    j->freight_id = 0;
    j->expand_limit = 0;
    j->total = 0;
    fr_zero(j->seen, sizeof j->seen);
}

int freight_join_add(freight_join_t *j, const freight_header_t *h, const uint8_t *packets,
                     uint32_t npackets, uint8_t *work, uint32_t work_len)
{
    if (!j) return FREIGHT_ERR_ARG;
    int rc = freight_header_check(h);
    if (rc) return rc;
    const uint64_t per = FREIGHT_CAPACITY(h->k);
    if (!j->started) {
        /* The whole set must fit before anything is accepted. */
        uint64_t need = per * (uint64_t) (h->seq_count - 1u);
        if (need > j->cap) return FREIGHT_ERR_SPACE;
        j->started = true;
        j->freight_id = h->freight_id;
        j->k = h->k;
        j->flags = h->flags;
        j->count = h->seq_count;
        j->expand_limit = h->expand_limit;
    } else if (h->freight_id != j->freight_id || h->k != j->k || h->flags != j->flags ||
               h->seq_count != j->count || h->expand_limit != j->expand_limit) {
        return FREIGHT_ERR_SEQ;
    }
    const uint32_t s = h->seq;
    const bool last = s + 1u == j->count;
    if (!last && h->payload_len != per) return FREIGHT_ERR_SEQ;
    if (j->seen[s >> 3] & (1u << (s & 7))) return FREIGHT_OK;
    const uint64_t off = per * s;
    if (off + h->payload_len > j->cap) return FREIGHT_ERR_SPACE;
    rc = freight_decode(h, packets, npackets, j->out + off, h->payload_len, work, work_len);
    if (rc) return rc;
    j->seen[s >> 3] |= (uint8_t) (1u << (s & 7));
    j->received++;
    if (last) j->total = off + h->payload_len;
    return FREIGHT_OK;
}

bool freight_join_complete(const freight_join_t *j, uint64_t *total)
{
    if (!j || !j->started || j->received != j->count) return false;
    if (total) *total = j->total;
    return true;
}
