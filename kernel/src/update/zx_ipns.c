/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* zx_ipns.c — IPNS names and record verification for zx_upcheck.
 *
 * Implements "IPNS Record and Protocol" (specs.ipfs.tech/ipns/ipns-record,
 * the 2025-02-28 revision, read from github.com/ipfs/specs), Record
 * Verification steps 1-8, for Ed25519 names, cross-checked against boxo
 * v0.24.3 ipns/validation.go:
 *   1. record <= 10 KiB;
 *   2. signatureV2 and data present and non-empty;
 *   3. the key is the one inlined in the name (identity multihash of the
 *      libp2p PublicKey protobuf {Type=Ed25519, Data=32 bytes}); a pubKey
 *      field, if present, must be that same protobuf;
 *   4. data is strict DAG-CBOR (definite lengths, shortest heads, map keys in
 *      length-then-bytes order, no duplicates, only tag 42); unknown keys are
 *      skipped as the spec requires;
 *   5-6. Ed25519 over "ipns-signature:" || data;
 *   7. if signatureV1 or value is present, value, validity, validityType,
 *      sequence and ttl in the protobuf must equal the CBOR ones (a missing
 *      protobuf field counts as empty/zero, as in boxo);
 *   8. ValidityType 0 and Validity (RFC 3339) later than now.
 * signatureV1 is never used for verification.
 */
#include "zx_upcheck.h"
#include "zx_upcheck_util.h"
#include "../robin_debanks/ed25519_verify.h"

static const uint8_t SIG_PREFIX[15] = {'i', 'p', 'n', 's', '-', 's', 'i', 'g',
                                       'n', 'a', 't', 'u', 'r', 'e', ':'};
#define LIBP2P_KEY 0x72u

/* ---- base36 (multibase 'k', lowercase) ------------------------------------- */

static const char B36[] = "0123456789abcdefghijklmnopqrstuvwxyz";

static int b36_decode(const char *in, uint32_t len, uint8_t *out, uint32_t cap)
{
    uint8_t b[96];
    uint32_t nb = 0, zeros = 0;
    if (len > 150) return ZXU_ERR_SPACE;
    while (zeros < len && in[zeros] == '0') zeros++;
    for (uint32_t i = zeros; i < len; i++) {
        uint32_t v, carry;
        char c = in[i];
        if (c >= '0' && c <= '9')
            v = (uint32_t) (c - '0');
        else if (c >= 'a' && c <= 'z')
            v = (uint32_t) (c - 'a') + 10u;
        else
            return ZXU_ERR_MALFORMED;
        carry = v;
        for (uint32_t j = 0; j < nb; j++) {
            carry += (uint32_t) b[j] * 36u;
            b[j] = (uint8_t) carry;
            carry >>= 8;
        }
        while (carry) {
            if (nb >= sizeof b) return ZXU_ERR_SPACE;
            b[nb++] = (uint8_t) carry;
            carry >>= 8;
        }
    }
    if (zeros + nb > cap) return ZXU_ERR_SPACE;
    for (uint32_t i = 0; i < zeros; i++) out[i] = 0;
    for (uint32_t i = 0; i < nb; i++) out[zeros + i] = b[nb - 1u - i];
    return (int) (zeros + nb);
}

static int b36_encode(const uint8_t *in, uint32_t len, char *out, uint32_t cap)
{
    uint8_t dig[160];
    uint8_t num[64];
    uint32_t nd = 0, zeros = 0, n = 0, start;
    if (len > sizeof num) return ZXU_ERR_SPACE;
    while (zeros < len && in[zeros] == 0) zeros++;
    zxu__cpy(num, in, len);
    start = zeros;
    while (start < len) { /* long division by 36, most significant byte first */
        uint32_t rem = 0;
        for (uint32_t i = start; i < len; i++) {
            uint32_t x = (rem << 8) | num[i]; /* < 36*256 */
            uint32_t q = (x * 7282u) >> 18;   /* == x / 36 for x < 9216 */
            num[i] = (uint8_t) q;
            rem = x - q * 36u;
        }
        if (nd >= sizeof dig) return ZXU_ERR_SPACE;
        dig[nd++] = (uint8_t) rem;
        while (start < len && num[start] == 0) start++;
    }
    if (zeros + nd + 1u > cap) return ZXU_ERR_SPACE;
    for (uint32_t i = 0; i < zeros; i++) out[n++] = '0';
    for (uint32_t i = 0; i < nd; i++) out[n++] = B36[dig[nd - 1u - i]];
    out[n] = 0;
    return (int) n;
}

/* ---- names ------------------------------------------------------------------ */

/* identity multihash digest = protobuf PublicKey{Type=1 (Ed25519), Data=32 bytes} */
static int key_from_pb(const uint8_t *d, uint32_t len, uint8_t pk[32])
{
    if (len != 36 || d[0] != 0x08 || d[1] != 0x01 || d[2] != 0x12 || d[3] != 0x20)
        return ZXU_ERR_UNSUPP; /* RSA, secp256k1, ECDSA keys or junk */
    zxu__cpy(pk, d + 4, 32);
    return ZXU_OK;
}

int zxu_ipns_name_parse(const char *s, uint32_t len, uint8_t pubkey[32])
{
    uint8_t bin[96];
    ipfsn_cid_t cid;
    int n;
    if (!s || !pubkey || len < 2) return ZXU_ERR_ARG;
    if (s[0] == 'k') {
        if ((n = b36_decode(s + 1, len - 1u, bin, sizeof bin)) < 0) return ZXU_ERR_MALFORMED;
        if (ipfsn_cid_decode_exact(bin, (uint32_t) n, &cid)) return ZXU_ERR_MALFORMED;
    } else if (s[0] == 'b') {
        if (ipfsn_cid_parse(s, len, &cid)) return ZXU_ERR_MALFORMED;
    } else if (len > 2 && s[0] == '1' && s[1] == '2') { /* legacy peer ID: base58 multihash */
        if ((n = ipfsn_base58_decode(s, len, bin, sizeof bin)) < 0) return ZXU_ERR_MALFORMED;
        if (n != 38 || bin[0] != 0x00 || bin[1] != 36) return ZXU_ERR_UNSUPP;
        return key_from_pb(bin + 2, 36, pubkey);
    } else {
        return ZXU_ERR_MALFORMED;
    }
    if (cid.version != 1 || cid.codec != LIBP2P_KEY) return ZXU_ERR_MALFORMED;
    if (cid.mh_code != IPFSN_MH_IDENTITY) return ZXU_ERR_UNSUPP; /* hashed (RSA) key */
    return key_from_pb(cid.digest, cid.digest_len, pubkey);
}

int zxu_ipns_name_string(const uint8_t pubkey[32], char *out, uint32_t cap)
{
    static const uint8_t pre[8] = {0x01, LIBP2P_KEY, 0x00, 36, 0x08, 0x01, 0x12, 0x20};
    uint8_t bin[40];
    int n;
    if (!pubkey || !out || cap < 2) return ZXU_ERR_ARG;
    zxu__cpy(bin, pre, 8);
    zxu__cpy(bin + 8, pubkey, 32);
    out[0] = 'k';
    if ((n = b36_encode(bin, sizeof bin, out + 1, cap - 1u)) < 0) return n;
    return n + 1;
}

/* ---- strict DAG-CBOR ---------------------------------------------------------- */

static int cbor_head(const uint8_t *p, uint32_t len, uint32_t *pos, uint32_t *major, uint64_t *val)
{
    uint32_t ai, nb;
    uint64_t v = 0;
    if (*pos >= len) return ZXU_ERR_MALFORMED;
    *major = p[*pos] >> 5;
    ai = p[*pos] & 31u;
    (*pos)++;
    if (ai < 24) {
        *val = ai;
        return ZXU_OK;
    }
    if (ai > 27) return ZXU_ERR_MALFORMED; /* indefinite lengths, reserved */
    nb = 1u << (ai - 24u);
    if (len - *pos < nb) return ZXU_ERR_MALFORMED;
    for (uint32_t i = 0; i < nb; i++) v = (v << 8) | p[*pos + i];
    *pos += nb;
    if (*major == 7) { /* floats: DAG-CBOR allows only 64-bit ones */
        if (ai != 27) return ZXU_ERR_MALFORMED;
        *val = v;
        return ZXU_OK;
    }
    /* shortest form only */
    if ((nb == 1 && v < 24) || (nb == 2 && v < 0x100u) || (nb == 4 && v < 0x10000u) ||
        (nb == 8 && v < 0x100000000ull))
        return ZXU_ERR_MALFORMED;
    *val = v;
    return ZXU_OK;
}

static int cbor_skip(const uint8_t *p, uint32_t len, uint32_t *pos, uint32_t depth)
{
    uint32_t major, at = *pos;
    uint64_t v;
    if (depth > 16) return ZXU_ERR_MALFORMED;
    if (cbor_head(p, len, pos, &major, &v)) return ZXU_ERR_MALFORMED;
    switch (major) {
    case 0:
    case 1:
        return ZXU_OK;
    case 2:
    case 3:
        if (v > len - *pos) return ZXU_ERR_MALFORMED;
        *pos += (uint32_t) v;
        return ZXU_OK;
    case 4:
        if (v > len) return ZXU_ERR_MALFORMED;
        for (uint64_t i = 0; i < v; i++)
            if (cbor_skip(p, len, pos, depth + 1)) return ZXU_ERR_MALFORMED;
        return ZXU_OK;
    case 5:
        if (v > len) return ZXU_ERR_MALFORMED;
        for (uint64_t i = 0; i < 2u * v; i++)
            if (cbor_skip(p, len, pos, depth + 1)) return ZXU_ERR_MALFORMED;
        return ZXU_OK; /* (key order inside nested maps is not checked) */
    case 6:
        if (v != 42) return ZXU_ERR_MALFORMED; /* DAG-CBOR: only CID links */
        return cbor_skip(p, len, pos, depth + 1);
    default:
        /* major 7: only false, true, null and float64 (head already consumed) */
        if (p[at] == 0xf4u || p[at] == 0xf5u || p[at] == 0xf6u || p[at] == 0xfbu) return ZXU_OK;
        return ZXU_ERR_MALFORMED;
    }
}

static bool key_is(const uint8_t *k, uint32_t kl, const char *s)
{
    uint32_t n = zxu__strlen(s, 32);
    return kl == n && zxu__eq(k, s, n);
}

typedef struct {
    const uint8_t *value, *validity;
    uint32_t value_len, validity_len;
    uint64_t vtype, seq, ttl;
    uint32_t have; /* bit per required key */
} ipns_data_t;

static int parse_data(const uint8_t *p, uint32_t len, ipns_data_t *d)
{
    uint32_t pos = 0, major;
    uint64_t n;
    const uint8_t *pk = 0;
    uint32_t pkl = 0;
    zxu__set(d, 0, (uint32_t) sizeof(*d));
    if (cbor_head(p, len, &pos, &major, &n) || major != 5 || n > 64) return ZXU_ERR_MALFORMED;
    for (uint64_t i = 0; i < n; i++) {
        uint64_t kl, v;
        const uint8_t *k;
        if (cbor_head(p, len, &pos, &major, &kl) || major != 3 || kl > len - pos)
            return ZXU_ERR_MALFORMED;
        k = p + pos;
        pos += (uint32_t) kl;
        if (pk) { /* canonical order: length first, then bytes; no duplicates */
            int c = 0;
            if (pkl != (uint32_t) kl) {
                c = pkl < (uint32_t) kl ? -1 : 1;
            } else {
                for (uint32_t j = 0; j < pkl && !c; j++)
                    if (pk[j] != k[j]) c = pk[j] < k[j] ? -1 : 1;
            }
            if (c >= 0) return ZXU_ERR_MALFORMED;
        }
        pk = k;
        pkl = (uint32_t) kl;
        if (key_is(k, pkl, "Value") || key_is(k, pkl, "Validity")) {
            if (cbor_head(p, len, &pos, &major, &v) || major != 2 || v > len - pos)
                return ZXU_ERR_MALFORMED;
            if (key_is(k, pkl, "Value")) {
                d->value = p + pos;
                d->value_len = (uint32_t) v;
                d->have |= 1u;
            } else {
                d->validity = p + pos;
                d->validity_len = (uint32_t) v;
                d->have |= 2u;
            }
            pos += (uint32_t) v;
        } else if (key_is(k, pkl, "ValidityType") || key_is(k, pkl, "Sequence") ||
                   key_is(k, pkl, "TTL")) {
            if (cbor_head(p, len, &pos, &major, &v) || major != 0) return ZXU_ERR_MALFORMED;
            if (key_is(k, pkl, "ValidityType")) {
                d->vtype = v;
                d->have |= 4u;
            } else if (key_is(k, pkl, "Sequence")) {
                d->seq = v;
                d->have |= 8u;
            } else {
                d->ttl = v;
                d->have |= 16u;
            }
        } else if (cbor_skip(p, len, &pos, 1)) {
            return ZXU_ERR_MALFORMED;
        }
    }
    if (pos != len) return ZXU_ERR_MALFORMED; /* trailing bytes */
    if (d->have != 31u) return ZXU_ERR_MALFORMED;
    return ZXU_OK;
}

/* ---- RFC 3339 ------------------------------------------------------------------- */

static int digits(const uint8_t *s, uint32_t n, uint32_t *v)
{
    uint32_t x = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '9') return ZXU_ERR_MALFORMED;
        x = x * 10u + (uint32_t) (s[i] - '0');
    }
    *v = x;
    return ZXU_OK;
}

static bool leap(uint32_t y)
{
    /* division-free: y % 4, then y % 100 and y % 400 by subtraction */
    uint32_t r100 = y, r400 = y;
    if (y & 3u) return false;
    while (r100 >= 100u) r100 -= 100u;
    if (r100) return true;
    while (r400 >= 400u) r400 -= 400u;
    return r400 == 0;
}

int zxu_rfc3339_parse(const uint8_t *s, uint32_t len, uint64_t *secs, uint32_t *ns)
{
    static const uint8_t mdays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    uint32_t Y, M, D, h, m, sec, frac = 0, pos = 19, fd = 0;
    uint64_t days = 0, t;
    int64_t off = 0;
    if (!s || !secs || !ns || len < 20) return ZXU_ERR_MALFORMED;
    if (digits(s, 4, &Y) || s[4] != '-' || digits(s + 5, 2, &M) || s[7] != '-' ||
        digits(s + 8, 2, &D) || s[10] != 'T' || digits(s + 11, 2, &h) || s[13] != ':' ||
        digits(s + 14, 2, &m) || s[16] != ':' || digits(s + 17, 2, &sec))
        return ZXU_ERR_MALFORMED;
    if (Y < 1970 || M < 1 || M > 12 || D < 1 || h > 23 || m > 59 || sec > 59)
        return ZXU_ERR_MALFORMED; /* (leap seconds are refused) */
    if (D > (uint32_t) mdays[M - 1] + ((M == 2 && leap(Y)) ? 1u : 0u)) return ZXU_ERR_MALFORMED;
    if (pos < len && s[pos] == '.') {
        pos++;
        while (pos < len && s[pos] >= '0' && s[pos] <= '9') {
            if (fd == 9) return ZXU_ERR_MALFORMED;
            frac = frac * 10u + (uint32_t) (s[pos] - '0');
            fd++;
            pos++;
        }
        if (fd == 0) return ZXU_ERR_MALFORMED;
        while (fd < 9) {
            frac *= 10u;
            fd++;
        }
    }
    if (pos + 1u == len && s[pos] == 'Z') {
        off = 0;
    } else if (pos + 6u == len && (s[pos] == '+' || s[pos] == '-') && s[pos + 3] == ':') {
        uint32_t oh, om;
        if (digits(s + pos + 1, 2, &oh) || digits(s + pos + 4, 2, &om) || oh > 23 || om > 59)
            return ZXU_ERR_MALFORMED;
        off = (int64_t) (oh * 3600u + om * 60u);
        if (s[pos] == '-') off = -off;
    } else {
        return ZXU_ERR_MALFORMED;
    }
    for (uint32_t y = 1970; y < Y; y++) days += leap(y) ? 366u : 365u;
    for (uint32_t i = 1; i < M; i++) days += mdays[i - 1] + ((i == 2 && leap(Y)) ? 1u : 0u);
    days += D - 1u;
    t = days * 86400u + h * 3600u + m * 60u + sec;
    if (off > 0 && (uint64_t) off > t) return ZXU_ERR_MALFORMED; /* before 1970 */
    *secs = (uint64_t) ((int64_t) t - off);
    *ns = frac;
    return ZXU_OK;
}

/* ---- protobuf IpnsEntry ------------------------------------------------------- */

static int pb_varint(const uint8_t *p, uint32_t len, uint32_t *pos, uint64_t *v)
{
    uint64_t x = 0;
    for (uint32_t i = 0; i < 10; i++) {
        uint8_t b;
        if (*pos >= len) return ZXU_ERR_MALFORMED;
        b = p[(*pos)++];
        if (i == 9 && b > 1) return ZXU_ERR_MALFORMED;
        x |= (uint64_t) (b & 0x7fu) << (7u * i);
        if (!(b & 0x80u)) {
            if (i > 0 && b == 0) return ZXU_ERR_MALFORMED; /* not minimal */
            *v = x;
            return ZXU_OK;
        }
    }
    return ZXU_ERR_MALFORMED;
}

typedef struct {
    const uint8_t *f[10]; /* bytes fields by number */
    uint32_t fl[10];
    uint64_t iv[10]; /* varint fields by number */
    uint32_t seen;   /* bit per field number */
} ipns_pb_t;

static int parse_pb(const uint8_t *p, uint32_t len, ipns_pb_t *e)
{
    uint32_t pos = 0;
    zxu__set(e, 0, (uint32_t) sizeof(*e));
    while (pos < len) {
        uint64_t tag, v;
        uint32_t f, wt;
        if (pb_varint(p, len, &pos, &tag)) return ZXU_ERR_MALFORMED;
        if (tag > 0xFFFFFFFFu || (tag >> 3) == 0) return ZXU_ERR_MALFORMED;
        f = (uint32_t) (tag >> 3);
        wt = (uint32_t) (tag & 7u);
        if (wt == 0) {
            if (pb_varint(p, len, &pos, &v)) return ZXU_ERR_MALFORMED;
        } else if (wt == 2) {
            if (pb_varint(p, len, &pos, &v) || v > len - pos) return ZXU_ERR_MALFORMED;
        } else {
            return ZXU_ERR_MALFORMED; /* no fixed32/64/groups in an IpnsEntry */
        }
        if (f <= 9) {
            bool bytes_field = (f == 1 || f == 2 || f == 4 || f == 7 || f == 8 || f == 9);
            if (e->seen & (1u << f)) return ZXU_ERR_MALFORMED; /* duplicate: no merging */
            if (bytes_field != (wt == 2)) return ZXU_ERR_MALFORMED;
            e->seen |= 1u << f;
            if (wt == 2) {
                e->f[f] = p + pos;
                e->fl[f] = (uint32_t) v;
            } else {
                e->iv[f] = v;
            }
        }
        if (wt == 2) pos += (uint32_t) v; /* unknown fields are skipped */
    }
    return ZXU_OK;
}

static bool bytes_eq(const uint8_t *a, uint32_t al, const uint8_t *b, uint32_t bl)
{
    return al == bl && (al == 0 || zxu__eq(a, b, al));
}

int zxu_ipns_verify(const uint8_t *rec, uint32_t len, const uint8_t pubkey[32], uint64_t now,
                    uint8_t *scratch, uint32_t scap, zxu_ipns_t *out)
{
    ipns_pb_t e;
    ipns_data_t d;
    uint64_t eol;
    uint32_t eol_ns;
    if (!rec || !pubkey || !scratch || !out) return ZXU_ERR_ARG;
    zxu__set(out, 0, (uint32_t) sizeof(*out));
    if (len > ZXU_IPNS_RECORD_MAX) return ZXU_ERR_MALFORMED; /* step 1 */
    if (parse_pb(rec, len, &e)) return ZXU_ERR_MALFORMED;
    if (!e.fl[8] || !e.fl[9]) return ZXU_ERR_MALFORMED; /* step 2 */
    if (e.seen & (1u << 7)) { /* step 3: an embedded key must be the name's key */
        uint8_t k[32];
        if (key_from_pb(e.f[7], e.fl[7], k) || !zxu__eq(k, pubkey, 32)) return ZXU_ERR_NAME;
    }
    if (parse_data(e.f[9], e.fl[9], &d)) return ZXU_ERR_MALFORMED; /* step 4 */
    if (e.fl[8] != 64) return ZXU_ERR_SIG;                         /* Ed25519 */
    if (scap < 15u + e.fl[9]) return ZXU_ERR_SPACE;
    zxu__cpy(scratch, SIG_PREFIX, 15); /* step 5 */
    zxu__cpy(scratch + 15, e.f[9], e.fl[9]);
    if (!ed25519_verify(scratch, 15u + e.fl[9], e.f[8], pubkey)) return ZXU_ERR_SIG; /* 6 */
    if (e.fl[2] || e.fl[1]) {                                                        /* step 7 */
        if (!bytes_eq(e.f[1], e.fl[1], d.value, d.value_len) ||
            !bytes_eq(e.f[4], e.fl[4], d.validity, d.validity_len) || e.iv[3] != d.vtype ||
            e.iv[5] != d.seq || e.iv[6] != d.ttl)
            return ZXU_ERR_MALFORMED;
        out->has_v1 = true;
    }
    if (d.vtype != 0) return ZXU_ERR_UNSUPP; /* step 8 */
    if (zxu_rfc3339_parse(d.validity, d.validity_len, &eol, &eol_ns)) return ZXU_ERR_MALFORMED;
    /* the Value we accept: exactly "/ipfs/<cid>" */
    if (d.value_len < 7 || !zxu__eq(d.value, "/ipfs/", 6)) return ZXU_ERR_UNSUPP;
    if (ipfsn_cid_parse((const char *) d.value + 6, d.value_len - 6u, &out->value))
        return ZXU_ERR_UNSUPP;
    out->sequence = d.seq;
    out->eol = eol;
    out->eol_ns = eol_ns;
    out->ttl_ns = d.ttl;
    if (now == 0) return ZXU_ERR_ARG;
    if (!(eol > now || (eol == now && eol_ns > 0))) return ZXU_ERR_EXPIRED;
    return ZXU_OK;
}
