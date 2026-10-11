/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* aipi.c — AIPI: content-addressed functions and data. See aipi.h and
 * docs/AIPI.md. Freestanding C11, integer only, no allocation. */
#include "aipi.h"
#include "../robin_debanks/sha256.h"

/* ===== small helpers (no libc) ============================================ */

static void a_cpy(void *d, const void *s, uint32_t n)
{
    uint8_t *x = (uint8_t *) d;
    const uint8_t *y = (const uint8_t *) s;
    for (uint32_t i = 0; i < n; i++) x[i] = y[i];
}

static void a_zero(void *d, uint32_t n)
{
    volatile uint8_t *x = (volatile uint8_t *) d;
    for (uint32_t i = 0; i < n; i++) x[i] = 0;
}

static bool a_eq(const void *a, const void *b, uint32_t n)
{
    const uint8_t *x = (const uint8_t *) a;
    const uint8_t *y = (const uint8_t *) b;
    uint8_t acc = 0;
    for (uint32_t i = 0; i < n; i++) acc |= (uint8_t) (x[i] ^ y[i]);
    return acc == 0;
}

static void a_wr16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t) v;
    p[1] = (uint8_t) (v >> 8);
}

static uint32_t a_rd16(const uint8_t *p)
{
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8);
}

/* u64 / u32 by shift-and-subtract: no 64-bit division instruction needed on
 * 32-bit targets. */
static uint64_t a_divmod(uint64_t n, uint32_t d, uint32_t *rem)
{
    uint64_t q = 0, r = 0;
    for (int i = 63; i >= 0; i--) {
        r = (r << 1) | ((n >> i) & 1u);
        if (r >= d) {
            r -= d;
            q |= (uint64_t) 1u << i;
        }
    }
    *rem = (uint32_t) r;
    return q;
}

static int a_map(int r)
{
    switch (r) {
    case IPFSN_OK:
        return AIPI_OK;
    case IPFSN_ERR_ARG:
        return AIPI_ERR_ARG;
    case IPFSN_ERR_MALFORMED:
        return AIPI_ERR_MALFORMED;
    case IPFSN_ERR_UNSUPP:
        return AIPI_ERR_UNSUPP;
    case IPFSN_ERR_SPACE:
        return AIPI_ERR_SPACE;
    case IPFSN_ERR_HASH:
        return AIPI_ERR_HASH;
    case IPFSN_ERR_CRYPTO:
        return AIPI_ERR_HASH; /* sealed record altered */
    case IPFSN_ERR_NOTFOUND:
        return AIPI_ERR_NOTFOUND;
    case IPFSN_ERR_PRIVATE:
        return AIPI_ERR_PRIVATE;
    case IPFSN_ERR_NOKEY:
        return AIPI_ERR_PRIVATE;
    case IPFSN_ERR_FULL:
        return AIPI_ERR_FULL;
    case IPFSN_ERR_CONFLICT:
        return AIPI_ERR_STATE;
    case IPFSN_ERR_DEPTH:
        return AIPI_ERR_UNSUPP;
    default:
        return AIPI_ERR_TRANSPORT;
    }
}

/* ===== codecs, names, CIDs ================================================ */

uint32_t aipi_intent_codec(uint8_t intent)
{
    return intent < AIPI_INTENT_COUNT ? AIPI_MC_FN + intent : 0u;
}

bool aipi_is_fn_codec(uint32_t codec)
{
    return codec >= AIPI_MC_FN && codec < AIPI_MC_FN + AIPI_INTENT_COUNT;
}

static bool raw_cid_ok(const ipfsn_cid_t *c)
{
    return c->version == 1 && c->codec == IPFSN_MC_RAW && c->mh_code == IPFSN_MH_SHA2_256 &&
           c->digest_len == 32u;
}

static bool cid_is_v1_sha256(const ipfsn_cid_t *c)
{
    return c->version == 1 && c->mh_code == IPFSN_MH_SHA2_256 && c->digest_len == 32u;
}

/* Length of a valid export name, or 0 if it is not one. */
static uint32_t name_len(const char *s)
{
    uint32_t n = 0;
    while (n <= AIPI_NAME_MAX && s[n]) {
        char ch = s[n];
        bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                  (ch >= '0' && ch <= '9') || ch == '_';
        if (!ok) return 0;
        n++;
    }
    return n <= AIPI_NAME_MAX ? n : 0;
}

static bool name_empty(const char *s)
{
    return s[0] == 0;
}

static bool name_same(const char *a, const char *b)
{
    for (uint32_t i = 0; i <= AIPI_NAME_MAX; i++) {
        if (a[i] != b[i]) return false;
        if (!a[i]) return true;
    }
    return true;
}

int aipi_fn_check(const aipi_fn_t *fn)
{
    if (!fn) return AIPI_ERR_ARG;
    if (fn->intent >= AIPI_INTENT_COUNT) return AIPI_ERR_MALFORMED;
    if (fn->privacy > AIPI_PRIV_LOCAL || fn->determinism > AIPI_DET_DETERMINISTIC)
        return AIPI_ERR_MALFORMED;
    if (fn->verify < AIPI_VERIFY_CHECKER || fn->verify > AIPI_VERIFY_LOCAL)
        return AIPI_ERR_MALFORMED;
    if (fn->fuel_max == 0) return AIPI_ERR_MALFORMED;
    if (!raw_cid_ok(&fn->module)) return AIPI_ERR_MALFORMED;
    if (fn->has_contract && !raw_cid_ok(&fn->contract)) return AIPI_ERR_MALFORMED;
    if (!name_len(fn->entry)) return AIPI_ERR_MALFORMED;

    bool want_checker = fn->verify == AIPI_VERIFY_CHECKER || fn->verify == AIPI_VERIFY_CERT;
    if (want_checker) {
        if (!name_len(fn->checker) || name_same(fn->checker, fn->entry)) return AIPI_ERR_MALFORMED;
    } else if (!name_empty(fn->checker)) {
        return AIPI_ERR_MALFORMED;
    }
    if (fn->verify == AIPI_VERIFY_CERT) {
        if (!name_len(fn->certifier) || name_same(fn->certifier, fn->entry) ||
            name_same(fn->certifier, fn->checker))
            return AIPI_ERR_MALFORMED;
    } else if (!name_empty(fn->certifier)) {
        return AIPI_ERR_MALFORMED;
    }

    if (fn->verify == AIPI_VERIFY_QUORUM) {
        /* Redundant execution only proves anything if honest nodes agree
         * bit for bit; k > n/2 means two disagreeing groups cannot both win. */
        if (fn->determinism != AIPI_DET_DETERMINISTIC) return AIPI_ERR_MALFORMED;
        if (fn->quorum_n < 2u || fn->quorum_n > AIPI_CLAIMS_MAX) return AIPI_ERR_MALFORMED;
        if (fn->quorum_k > fn->quorum_n || 2u * fn->quorum_k <= fn->quorum_n)
            return AIPI_ERR_MALFORMED;
    } else if (fn->quorum_n || fn->quorum_k) {
        return AIPI_ERR_MALFORMED;
    }
    return AIPI_OK;
}

static const uint8_t FN_MAGIC[4] = {'A', 'I', 'P', 'F'};
static const uint8_t CALL_MAGIC[4] = {'A', 'I', 'P', 'C'};
static const uint8_t CLAIM_MAGIC[4] = {'A', 'I', 'P', 'R'};
#define AIPI_WIRE_V 1u

static int put_cid(const ipfsn_cid_t *c, uint8_t *out, uint32_t cap, uint32_t *pos)
{
    uint8_t b[IPFSN_CID_BIN_MAX];
    int n = ipfsn_cid_encode(c, b, sizeof b);
    if (n < 0) return a_map(n);
    if (*pos + 1u + (uint32_t) n > cap) return AIPI_ERR_SPACE;
    out[(*pos)++] = (uint8_t) n;
    a_cpy(out + *pos, b, (uint32_t) n);
    *pos += (uint32_t) n;
    return AIPI_OK;
}

static int get_cid(const uint8_t *in, uint32_t len, uint32_t *pos, ipfsn_cid_t *c)
{
    uint32_t n;
    if (*pos >= len) return AIPI_ERR_MALFORMED;
    n = in[(*pos)++];
    if (!n || n > len - *pos) return AIPI_ERR_MALFORMED;
    if (ipfsn_cid_decode_exact(in + *pos, n, c) != IPFSN_OK) return AIPI_ERR_MALFORMED;
    *pos += n;
    return AIPI_OK;
}

static int put_name(const char *s, uint8_t *out, uint32_t cap, uint32_t *pos)
{
    uint32_t n = name_empty(s) ? 0u : name_len(s);
    if (!name_empty(s) && !n) return AIPI_ERR_MALFORMED;
    if (*pos + 1u + n > cap) return AIPI_ERR_SPACE;
    out[(*pos)++] = (uint8_t) n;
    a_cpy(out + *pos, s, n);
    *pos += n;
    return AIPI_OK;
}

static int get_name(const uint8_t *in, uint32_t len, uint32_t *pos, char *s)
{
    uint32_t n;
    if (*pos >= len) return AIPI_ERR_MALFORMED;
    n = in[(*pos)++];
    if (n > AIPI_NAME_MAX || n > len - *pos) return AIPI_ERR_MALFORMED;
    a_cpy(s, in + *pos, n);
    s[n] = 0;
    *pos += n;
    if (n && name_len(s) != n) return AIPI_ERR_MALFORMED; /* bad char or embedded NUL */
    return AIPI_OK;
}

int aipi_fn_encode(const aipi_fn_t *fn, uint8_t *out, uint32_t cap)
{
    uint32_t pos = 0;
    int r;
    if (!fn || !out) return AIPI_ERR_ARG;
    if ((r = aipi_fn_check(fn)) != AIPI_OK) return r;
    if (cap < 19u) return AIPI_ERR_SPACE;
    a_cpy(out, FN_MAGIC, 4);
    out[4] = AIPI_WIRE_V;
    out[5] = fn->intent;
    out[6] = fn->verify;
    out[7] = fn->privacy;
    out[8] = fn->determinism;
    out[9] = fn->quorum_n;
    out[10] = fn->quorum_k;
    for (uint32_t i = 0; i < 8u; i++) out[11u + i] = (uint8_t) (fn->fuel_max >> (8u * i));
    pos = 19u;
    if ((r = put_cid(&fn->module, out, cap, &pos)) != AIPI_OK) return r;
    if (fn->has_contract) {
        if ((r = put_cid(&fn->contract, out, cap, &pos)) != AIPI_OK) return r;
    } else {
        if (pos + 1u > cap) return AIPI_ERR_SPACE;
        out[pos++] = 0;
    }
    if ((r = put_name(fn->entry, out, cap, &pos)) != AIPI_OK) return r;
    if ((r = put_name(fn->checker, out, cap, &pos)) != AIPI_OK) return r;
    if ((r = put_name(fn->certifier, out, cap, &pos)) != AIPI_OK) return r;
    return (int) pos;
}

int aipi_fn_decode(const uint8_t *in, uint32_t len, aipi_fn_t *fn)
{
    uint32_t pos;
    int r;
    if (!in || !fn) return AIPI_ERR_ARG;
    a_zero(fn, (uint32_t) sizeof(*fn));
    if (len < 19u || !a_eq(in, FN_MAGIC, 4) || in[4] != AIPI_WIRE_V) return AIPI_ERR_MALFORMED;
    fn->intent = in[5];
    fn->verify = in[6];
    fn->privacy = in[7];
    fn->determinism = in[8];
    fn->quorum_n = in[9];
    fn->quorum_k = in[10];
    for (uint32_t i = 0; i < 8u; i++) fn->fuel_max |= (uint64_t) in[11u + i] << (8u * i);
    pos = 19u;
    if ((r = get_cid(in, len, &pos, &fn->module)) != AIPI_OK) return r;
    if (pos >= len) return AIPI_ERR_MALFORMED;
    if (in[pos] == 0) {
        pos++;
    } else {
        fn->has_contract = true;
        if ((r = get_cid(in, len, &pos, &fn->contract)) != AIPI_OK) return r;
    }
    if ((r = get_name(in, len, &pos, fn->entry)) != AIPI_OK) return r;
    if ((r = get_name(in, len, &pos, fn->checker)) != AIPI_OK) return r;
    if ((r = get_name(in, len, &pos, fn->certifier)) != AIPI_OK) return r;
    if (pos != len) return AIPI_ERR_MALFORMED;
    if ((r = aipi_fn_check(fn)) != AIPI_OK) return r;
    /* One function, one CID: the bytes must be the canonical encoding. */
    {
        uint8_t again[AIPI_FN_ENC_MAX];
        int n = aipi_fn_encode(fn, again, sizeof again);
        if (n < 0 || (uint32_t) n != len || !a_eq(again, in, len)) return AIPI_ERR_MALFORMED;
    }
    return AIPI_OK;
}

int aipi_fn_cid(const aipi_fn_t *fn, ipfsn_cid_t *cid)
{
    uint8_t b[AIPI_FN_ENC_MAX];
    int n;
    if (!fn || !cid) return AIPI_ERR_ARG;
    if ((n = aipi_fn_encode(fn, b, sizeof b)) < 0) return n;
    return a_map(ipfsn_cid_sha256(aipi_intent_codec(fn->intent), b, (uint32_t) n, cid));
}

int aipi_call_encode(const ipfsn_cid_t *fn_cid, const uint8_t *in, uint32_t in_len, uint8_t *out,
                     uint32_t cap)
{
    uint32_t pos = 6u;
    int r;
    if (!fn_cid || !out || (!in && in_len)) return AIPI_ERR_ARG;
    if (!aipi_is_fn_codec(fn_cid->codec) || !cid_is_v1_sha256(fn_cid)) return AIPI_ERR_UNSUPP;
    if (in_len > AIPI_IO_MAX) return AIPI_ERR_SPACE;
    if (cap < 6u) return AIPI_ERR_SPACE;
    a_cpy(out, CALL_MAGIC, 4);
    out[4] = AIPI_WIRE_V;
    out[5] = 0; /* flags, reserved */
    if ((r = put_cid(fn_cid, out, cap, &pos)) != AIPI_OK) return r;
    if (pos + 2u + in_len > cap) return AIPI_ERR_SPACE;
    a_wr16(out + pos, in_len);
    pos += 2u;
    a_cpy(out + pos, in, in_len);
    return (int) (pos + in_len);
}

int aipi_call_decode(const uint8_t *rec, uint32_t len, ipfsn_cid_t *fn_cid, const uint8_t **in,
                     uint32_t *in_len)
{
    uint32_t pos = 6u, n;
    if (!rec || !fn_cid || !in || !in_len) return AIPI_ERR_ARG;
    if (len < 6u || !a_eq(rec, CALL_MAGIC, 4) || rec[4] != AIPI_WIRE_V || rec[5] != 0)
        return AIPI_ERR_MALFORMED;
    if (get_cid(rec, len, &pos, fn_cid) != AIPI_OK) return AIPI_ERR_MALFORMED;
    if (!aipi_is_fn_codec(fn_cid->codec) || !cid_is_v1_sha256(fn_cid)) return AIPI_ERR_MALFORMED;
    if (len - pos < 2u) return AIPI_ERR_MALFORMED;
    n = a_rd16(rec + pos);
    pos += 2u;
    if (n > AIPI_IO_MAX || n != len - pos) return AIPI_ERR_MALFORMED;
    *in = rec + pos;
    *in_len = n;
    return AIPI_OK;
}

int aipi_call_cid(const ipfsn_cid_t *fn_cid, const uint8_t *in, uint32_t in_len, ipfsn_cid_t *cid)
{
    uint8_t b[AIPI_CALL_ENC_MAX];
    int n;
    if (!cid) return AIPI_ERR_ARG;
    if ((n = aipi_call_encode(fn_cid, in, in_len, b, sizeof b)) < 0) return n;
    return a_map(ipfsn_cid_sha256(AIPI_MC_CALL, b, (uint32_t) n, cid));
}

int aipi_claim_encode(const ipfsn_cid_t *call, const uint8_t executor[32], const uint8_t *out,
                      uint32_t out_len, const uint8_t *cert, uint32_t cert_len, uint8_t *buf,
                      uint32_t cap)
{
    uint32_t pos = 6u;
    int r;
    if (!call || !executor || !buf || (!out && out_len) || (!cert && cert_len)) return AIPI_ERR_ARG;
    if (call->codec != AIPI_MC_CALL || !cid_is_v1_sha256(call)) return AIPI_ERR_UNSUPP;
    if (out_len > AIPI_IO_MAX || cert_len > AIPI_CERT_MAX) return AIPI_ERR_SPACE;
    if (cap < 6u) return AIPI_ERR_SPACE;
    a_cpy(buf, CLAIM_MAGIC, 4);
    buf[4] = AIPI_WIRE_V;
    buf[5] = 0;
    if ((r = put_cid(call, buf, cap, &pos)) != AIPI_OK) return r;
    if (pos + 32u + 2u + out_len + 2u + cert_len > cap) return AIPI_ERR_SPACE;
    a_cpy(buf + pos, executor, 32u);
    pos += 32u;
    a_wr16(buf + pos, out_len);
    pos += 2u;
    a_cpy(buf + pos, out, out_len);
    pos += out_len;
    a_wr16(buf + pos, cert_len);
    pos += 2u;
    a_cpy(buf + pos, cert, cert_len);
    return (int) (pos + cert_len);
}

int aipi_claim_decode(const uint8_t *buf, uint32_t len, aipi_claim_t *c)
{
    uint32_t pos = 6u, n;
    if (!buf || !c) return AIPI_ERR_ARG;
    a_zero(c, (uint32_t) sizeof(*c));
    if (len < 6u || !a_eq(buf, CLAIM_MAGIC, 4) || buf[4] != AIPI_WIRE_V || buf[5] != 0)
        return AIPI_ERR_MALFORMED;
    if (get_cid(buf, len, &pos, &c->call) != AIPI_OK) return AIPI_ERR_MALFORMED;
    if (c->call.codec != AIPI_MC_CALL || !cid_is_v1_sha256(&c->call)) return AIPI_ERR_MALFORMED;
    if (len - pos < 34u) return AIPI_ERR_MALFORMED;
    a_cpy(c->executor, buf + pos, 32u);
    pos += 32u;
    n = a_rd16(buf + pos);
    pos += 2u;
    if (n > AIPI_IO_MAX || n > len - pos) return AIPI_ERR_MALFORMED;
    c->out = buf + pos;
    c->out_len = n;
    pos += n;
    if (len - pos < 2u) return AIPI_ERR_MALFORMED;
    n = a_rd16(buf + pos);
    pos += 2u;
    if (n > AIPI_CERT_MAX || n != len - pos) return AIPI_ERR_MALFORMED;
    c->cert = buf + pos;
    c->cert_len = n;
    return AIPI_OK;
}

int aipi_cid_to_text(const ipfsn_cid_t *cid, char *out, uint32_t cap)
{
    uint8_t b[IPFSN_CID_BIN_MAX];
    int n, k;
    if (!cid || !out) return AIPI_ERR_ARG;
    if (cid->version != 1) return AIPI_ERR_UNSUPP;
    if ((n = ipfsn_cid_encode(cid, b, sizeof b)) < 0) return a_map(n);
    if (cap < 2u) return AIPI_ERR_SPACE;
    out[0] = 'z';
    if ((k = ipfsn_base58_encode(b, (uint32_t) n, out + 1, cap - 1u)) < 0) return a_map(k);
    return k + 1;
}

int aipi_cid_from_text(const char *s, uint32_t len, ipfsn_cid_t *cid)
{
    uint8_t b[IPFSN_CID_BIN_MAX];
    int n;
    if (!s || !cid) return AIPI_ERR_ARG;
    if (len < 2u) return AIPI_ERR_MALFORMED;
    if (s[0] == 'z') {
        if ((n = ipfsn_base58_decode(s + 1, len - 1u, b, sizeof b)) < 0) return AIPI_ERR_MALFORMED;
        if (ipfsn_cid_decode_exact(b, (uint32_t) n, cid) != IPFSN_OK || cid->version != 1)
            return AIPI_ERR_MALFORMED;
        /* reject non-canonical text (e.g. extra leading '1's) */
        {
            char again[AIPI_TEXT_MAX];
            int m = aipi_cid_to_text(cid, again, sizeof again);
            if (m < 0 || (uint32_t) m != len || !a_eq(again, s, len)) return AIPI_ERR_MALFORMED;
        }
        return AIPI_OK;
    }
    if (s[0] == 'b') {
        if (ipfsn_cid_parse(s, len, cid) != IPFSN_OK || cid->version != 1)
            return AIPI_ERR_MALFORMED;
        return AIPI_OK;
    }
    return AIPI_ERR_UNSUPP;
}

int aipi_provider_key(const ipfsn_cid_t *cid, uint8_t key[32])
{
    static const char pfx[] = "zxv/aipi/v1/provider";
    uint8_t b[IPFSN_CID_BIN_MAX];
    sha256_ctx_t h;
    int n;
    if (!cid || !key) return AIPI_ERR_ARG;
    if ((n = ipfsn_cid_encode(cid, b, sizeof b)) < 0) return a_map(n);
    sha256_init(&h);
    sha256_update(&h, (const uint8_t *) pfx, sizeof pfx - 1u);
    sha256_update(&h, b, (uint32_t) n);
    sha256_final(&h, key);
    return AIPI_OK;
}

/* ===== context, GET, publish ============================================== */

int aipi_init(aipi_ctx_t *c, ipfsn_node_t *node, aipi_job_t *jobs, uint32_t job_cap,
              aipi_memo_t *memo, uint32_t memo_cap, uint8_t *module_buf, uint8_t *contract_buf)
{
    if (!c || !node || !node->bs || !jobs || !job_cap || (!memo && memo_cap) || !module_buf ||
        !contract_buf)
        return AIPI_ERR_ARG;
    a_zero(c, (uint32_t) sizeof(*c));
    c->node = node;
    c->jobs = jobs;
    c->job_cap = job_cap;
    c->memo = memo;
    c->memo_cap = memo_cap;
    c->module_buf = module_buf;
    c->contract_buf = contract_buf;
    for (uint32_t i = 0; i < job_cap; i++) jobs[i].status = AIPI_JOB_FREE;
    for (uint32_t i = 0; i < memo_cap; i++) memo[i].used = 0;
    return AIPI_OK;
}

#define AIPI_Q_GET_HASH 1u /* qlog reason: bytes did not hash to their CID */
enum { SRC_LOCAL = 1, SRC_PEER = 2, SRC_GATEWAY = 3 };

static void forbid(aipi_ctx_t *c, const ipfsn_cid_t *cid, uint32_t source, lpx_t *tag)
{
    lpx_t t = lpx_forbid(lpx_make(LPX_B_FALSE, LPX_P_NEUTRAL, LPX_PH_EMERGING));
    c->forbidden++;
    if (c->qlog) lpx_qlog_append(c->qlog, t, AIPI_Q_GET_HASH, source, cid->digest);
    if (tag) *tag = t;
}

static lpx_t tag_verified(void)
{
    return lpx_make(LPX_B_TRUE, LPX_P_NEGENTROPIC, LPX_PH_MANIFEST);
}

int aipi_get(aipi_ctx_t *c, const ipfsn_cid_t *cid, uint8_t *buf, uint32_t cap, uint32_t *len,
             lpx_t *tag)
{
    bool lied = false;
    int r;
    if (tag) *tag = lpx_make(LPX_B_NEITHER, LPX_P_NEUTRAL, LPX_PH_POTENTIAL);
    if (!c || !cid || !buf || !len) return AIPI_ERR_ARG;
    if (cid->mh_code != IPFSN_MH_SHA2_256) return AIPI_ERR_UNSUPP; /* identity proves nothing */

    r = ipfsn_bs_get(c->node->bs, cid, buf, cap, len);
    if (r == IPFSN_OK) {
        c->gets_verified++;
        if (tag) *tag = tag_verified();
        return AIPI_OK;
    }
    if (r == IPFSN_ERR_HASH || r == IPFSN_ERR_CRYPTO) {
        forbid(c, cid, SRC_LOCAL, tag);
        return AIPI_ERR_HASH;
    }
    if (r != IPFSN_ERR_NOTFOUND) return a_map(r);
    if (ipfsn_is_private(c->node, cid)) return AIPI_ERR_NOTFOUND; /* A1: stays here */
    if (!c->online) return AIPI_ERR_OFFLINE;

    if (c->node->peer.want) {
        r = ipfsn_peer_fetch(c->node, cid, buf, cap, len);
        if (r == IPFSN_OK) {
            c->gets_verified++;
            if (tag) *tag = tag_verified();
            return AIPI_OK;
        }
        if (r == IPFSN_ERR_HASH) {
            forbid(c, cid, SRC_PEER, tag);
            lied = true;
        }
    }
    if (c->node->gw.https_get) {
        r = ipfsn_gw_fetch_block(c->node, cid, buf, cap, len);
        if (r == IPFSN_OK) {
            c->gets_verified++;
            if (tag) *tag = tag_verified();
            return AIPI_OK;
        }
        if (r == IPFSN_ERR_HASH) {
            forbid(c, cid, SRC_GATEWAY, tag);
            lied = true;
        }
    }
    return lied ? AIPI_ERR_HASH : AIPI_ERR_NOTFOUND;
}

static int store_block(aipi_ctx_t *c, const ipfsn_cid_t *cid, const uint8_t *data, uint32_t len,
                       uint32_t vis, bool pin)
{
    ipfsn_cid_t pcid;
    int r;
    /* A1: bytes this node holds sealed never get a plaintext copy. The
     * blockstore would accept a public put over a sealed entry (and write
     * the clear bytes before any pin conflict), so refuse it here first. */
    if (vis == IPFSN_VIS_PUBLIC &&
        (ipfsn_is_private(c->node, cid) ||
         ipfsn_bs_visibility(c->node->bs, cid) == (int) IPFSN_VIS_PRIVATE))
        return AIPI_ERR_STATE;
    r = ipfsn_bs_put(c->node->bs, cid, data, len, vis, &pcid);
    if (r != IPFSN_OK) return a_map(r);
    if (pin) {
        r = ipfsn_pin(c->node, cid, vis, vis == IPFSN_VIS_PRIVATE ? &pcid : 0);
        if (r != IPFSN_OK) return a_map(r);
    }
    return AIPI_OK;
}

int aipi_put_blob(aipi_ctx_t *c, const uint8_t *data, uint32_t len, uint32_t vis, ipfsn_cid_t *cid)
{
    int r;
    if (!c || !cid || (!data && len)) return AIPI_ERR_ARG;
    if (vis != IPFSN_VIS_PUBLIC && vis != IPFSN_VIS_PRIVATE) return AIPI_ERR_ARG;
    if ((r = ipfsn_cid_sha256(IPFSN_MC_RAW, data, len, cid)) != IPFSN_OK) return a_map(r);
    return store_block(c, cid, data, len, vis, true);
}

int aipi_publish_fn(aipi_ctx_t *c, const aipi_fn_t *fn, ipfsn_cid_t *fn_cid)
{
    int n, r;
    if (!c || !fn || !fn_cid) return AIPI_ERR_ARG;
    if ((n = aipi_fn_encode(fn, c->fn_buf, sizeof c->fn_buf)) < 0) return n;
    r = ipfsn_cid_sha256(aipi_intent_codec(fn->intent), c->fn_buf, (uint32_t) n, fn_cid);
    if (r != IPFSN_OK) return a_map(r);
    return store_block(c, fn_cid, c->fn_buf, (uint32_t) n,
                       fn->privacy == AIPI_PRIV_LOCAL ? IPFSN_VIS_PRIVATE : IPFSN_VIS_PUBLIC, true);
}

int aipi_load_fn(aipi_ctx_t *c, const ipfsn_cid_t *fn_cid, aipi_fn_t *fn)
{
    uint32_t len = 0;
    int r;
    if (!c || !fn_cid || !fn) return AIPI_ERR_ARG;
    if (!aipi_is_fn_codec(fn_cid->codec)) return AIPI_ERR_UNSUPP;
    if ((r = aipi_get(c, fn_cid, c->fn_buf, sizeof c->fn_buf, &len, 0)) != AIPI_OK) return r;
    if ((r = aipi_fn_decode(c->fn_buf, len, fn)) != AIPI_OK) return r;
    if (aipi_intent_codec(fn->intent) != fn_cid->codec) return AIPI_ERR_CODEC;
    return AIPI_OK;
}

static int load_code(aipi_ctx_t *c, const aipi_fn_t *fn, aipi_code_t *code)
{
    uint32_t len = 0;
    int r;
    a_zero(code, (uint32_t) sizeof(*code));
    code->fn = fn;
    r = aipi_get(c, &fn->module, c->module_buf, AIPI_MODULE_MAX, &len, 0);
    if (r != AIPI_OK) return r;
    code->module = c->module_buf;
    code->module_len = len;
    if (fn->has_contract) {
        r = aipi_get(c, &fn->contract, c->contract_buf, AIPI_CONTRACT_MAX, &len, 0);
        if (r != AIPI_OK) return r;
        code->contract = c->contract_buf;
        code->contract_len = len;
    }
    return AIPI_OK;
}

static bool have_code(aipi_ctx_t *c, const aipi_fn_t *fn)
{
    return ipfsn_bs_has(c->node->bs, &fn->module) &&
           (!fn->has_contract || ipfsn_bs_has(c->node->bs, &fn->contract));
}

/* ===== memo (verified results of deterministic public calls) =============== */

static int memo_find(const aipi_ctx_t *c, const ipfsn_cid_t *call)
{
    for (uint32_t i = 0; i < c->memo_cap; i++)
        if (c->memo[i].used && ipfsn_cid_equal(&c->memo[i].call, call)) return (int) i;
    return -1;
}

static void memo_put(aipi_ctx_t *c, const aipi_job_t *j)
{
    ipfsn_cid_t out;
    uint32_t i;
    if (!c->memo_cap || j->fn.determinism != AIPI_DET_DETERMINISTIC || j->input_private ||
        j->fn.privacy == AIPI_PRIV_LOCAL)
        return;
    if (ipfsn_cid_sha256(IPFSN_MC_RAW, j->out, j->out_len, &out) != IPFSN_OK) return;
    if (store_block(c, &out, j->out, j->out_len, IPFSN_VIS_PUBLIC, false) != AIPI_OK) return;
    if (memo_find(c, &j->call_cid) >= 0) return;
    i = c->memo_next;
    c->memo_next = (c->memo_next + 1u == c->memo_cap) ? 0u : c->memo_next + 1u;
    c->memo[i].call = j->call_cid;
    c->memo[i].output = out;
    c->memo[i].used = 1;
}

/* ===== EXECUTE ============================================================== */

int aipi_plan(aipi_ctx_t *c, const aipi_fn_t *fn, const ipfsn_cid_t *call_cid, bool input_private)
{
    bool priv, code;
    if (!c || !fn || !call_cid) return AIPI_ERR_ARG;
    priv = fn->privacy == AIPI_PRIV_LOCAL || input_private;
    if (!priv && fn->determinism == AIPI_DET_DETERMINISTIC) {
        int m = memo_find(c, call_cid);
        if (m >= 0 && ipfsn_bs_has(c->node->bs, &c->memo[m].output)) return AIPI_ROUTE_CACHE;
    }
    code = have_code(c, fn);
    if (code && (priv || fn->verify == AIPI_VERIFY_LOCAL || c->prefer_local || !c->online ||
                 !c->mesh.broadcast))
        return AIPI_ROUTE_LOCAL;
    if (!c->online) return AIPI_ROUTE_UNAVAILABLE; /* A3 */
    if (priv || fn->verify == AIPI_VERIFY_LOCAL || c->prefer_local || !c->mesh.broadcast)
        return AIPI_ROUTE_FETCH_LOCAL; /* public code by CID, run here */
    return AIPI_ROUTE_MESH;
}

static void job_tag(aipi_job_t *j, uint8_t base, uint8_t pol)
{
    j->tag = lpx_set(j->tag, base, pol);
}

static void job_phase(aipi_job_t *j, uint8_t phase)
{
    (void) lpx_advance(&j->tag, phase);
}

static void job_accept(aipi_ctx_t *c, aipi_job_t *j, const uint8_t *out, uint32_t out_len)
{
    a_cpy(j->out, out, out_len);
    j->out_len = (uint16_t) out_len;
    j->status = AIPI_JOB_DONE;
    job_tag(j, LPX_B_TRUE, LPX_P_NEGENTROPIC);
    job_phase(j, LPX_PH_MANIFEST);
    memo_put(c, j);
}

static void job_refund(aipi_ctx_t *c, aipi_job_t *j)
{
    uint64_t left = j->bounty - j->paid - j->refunded;
    if (left && c->mesh.refund) c->mesh.refund(c->mesh.ctx, left);
    j->refunded += left;
}

/* A2: pay the verified winners, equal shares, the remainder one unit each to
 * the earliest winners; exactly the bounty in total. */
static void job_pay(aipi_ctx_t *c, aipi_job_t *j)
{
    uint32_t w = 0, rem = 0, k = 0;
    uint64_t share;
    for (uint32_t i = 0; i < j->nclaims; i++)
        if (j->slot[i].verdict == AIPI_V_ACCEPTED) w++;
    if (!w || !j->bounty) return;
    share = a_divmod(j->bounty, w, &rem);
    for (uint32_t i = 0; i < j->nclaims; i++) {
        uint64_t amt;
        if (j->slot[i].verdict != AIPI_V_ACCEPTED) continue;
        amt = share + (k < rem ? 1u : 0u);
        k++;
        if (amt && c->mesh.pay) c->mesh.pay(c->mesh.ctx, j->slot[i].executor, amt);
        j->paid += amt;
    }
}

static int run_local(aipi_ctx_t *c, aipi_job_t *j)
{
    aipi_code_t code;
    uint32_t out_len = 0;
    int r;
    if (!c->exec.run) return AIPI_ERR_EXEC;
    if ((r = load_code(c, &j->fn, &code)) != AIPI_OK) return r;
    r = c->exec.run(c->exec.ctx, &code, j->in, j->in_len, j->out, AIPI_IO_MAX, &out_len);
    if (r != AIPI_OK || out_len > AIPI_IO_MAX) return r != AIPI_OK ? r : AIPI_ERR_EXEC;
    job_accept(c, j, j->out, out_len);
    return AIPI_OK;
}

int aipi_execute(aipi_ctx_t *c, const ipfsn_cid_t *fn_cid, const uint8_t *in, uint32_t in_len,
                 bool input_private, uint64_t bounty, uint64_t now, uint64_t ttl, uint32_t *job)
{
    aipi_job_t *j = 0;
    uint32_t idx = 0;
    int r, route;
    if (!c || !fn_cid || !job || (!in && in_len)) return AIPI_ERR_ARG;
    if (in_len > AIPI_IO_MAX) return AIPI_ERR_SPACE;
    for (uint32_t i = 0; i < c->job_cap; i++)
        if (c->jobs[i].status == AIPI_JOB_FREE) {
            j = &c->jobs[i];
            idx = i;
            break;
        }
    if (!j) return AIPI_ERR_FULL;
    a_zero(j, (uint32_t) sizeof(*j));
    if ((r = aipi_load_fn(c, fn_cid, &j->fn)) != AIPI_OK) return r;
    if ((r = aipi_call_cid(fn_cid, in, in_len, &j->call_cid)) != AIPI_OK) return r;
    j->fn_cid = *fn_cid;
    j->input_private = input_private;
    a_cpy(j->in, in, in_len);
    j->in_len = (uint16_t) in_len;
    j->tag = lpx_make(LPX_B_NEITHER, LPX_P_NEUTRAL, LPX_PH_POTENTIAL);

    route = aipi_plan(c, &j->fn, &j->call_cid, input_private);
    if (route < 0) return route;
    if (route == AIPI_ROUTE_UNAVAILABLE) return AIPI_ERR_OFFLINE;
    j->route = (uint8_t) route;
    j->status = AIPI_JOB_PENDING;
    j->deadline = now + ttl;
    *job = idx;

    if (route == AIPI_ROUTE_CACHE) {
        int m = memo_find(c, &j->call_cid);
        uint32_t len = 0;
        job_phase(j, LPX_PH_EMERGING);
        r = m < 0 ? AIPI_ERR_NOTFOUND
                  : aipi_get(c, &c->memo[m].output, j->out, AIPI_IO_MAX, &len, 0);
        if (r == AIPI_OK) {
            job_accept(c, j, j->out, len);
            return AIPI_OK;
        }
        route = AIPI_ROUTE_LOCAL; /* cache entry lost: compute it */
        j->route = (uint8_t) route;
    }
    if (route == AIPI_ROUTE_LOCAL || route == AIPI_ROUTE_FETCH_LOCAL) {
        if (lpx_phase(j->tag) == LPX_PH_POTENTIAL) job_phase(j, LPX_PH_EMERGING);
        r = run_local(c, j);
        if (r != AIPI_OK) {
            j->status = AIPI_JOB_FAILED;
            job_tag(j, LPX_B_FALSE, LPX_P_NEUTRAL);
        }
        return r;
    }

    /* MESH. CHECKER/CERT results are judged here, so the checker's code must
     * be obtainable before anyone is paid to work. */
    if (j->fn.verify == AIPI_VERIFY_CHECKER || j->fn.verify == AIPI_VERIFY_CERT) {
        aipi_code_t code;
        if (!c->exec.check) {
            j->status = AIPI_JOB_FREE;
            return AIPI_ERR_EXEC;
        }
        if ((r = load_code(c, &j->fn, &code)) != AIPI_OK) {
            j->status = AIPI_JOB_FREE;
            return r;
        }
    }
    if (bounty) {
        if (!c->mesh.reserve || !c->mesh.pay || !c->mesh.refund) {
            j->status = AIPI_JOB_FREE;
            return AIPI_ERR_ARG;
        }
        if (c->mesh.reserve(c->mesh.ctx, bounty) != 0) {
            j->status = AIPI_JOB_FREE;
            return AIPI_ERR_FUNDS;
        }
        j->bounty = bounty;
    }
    r = aipi_call_encode(fn_cid, in, in_len, c->call_buf, sizeof c->call_buf);
    if (r < 0 || c->mesh.broadcast(c->mesh.ctx, &j->call_cid, c->call_buf, (uint32_t) r, bounty,
                                   j->fn.verify == AIPI_VERIFY_QUORUM ? j->fn.quorum_n : 1u) != 0) {
        job_refund(c, j);
        j->status = AIPI_JOB_FREE;
        return r < 0 ? r : AIPI_ERR_TRANSPORT;
    }
    c->broadcasts++;
    job_phase(j, LPX_PH_EMERGING);
    return AIPI_OK;
}

static bool same_out(const aipi_slot_t *a, const aipi_slot_t *b)
{
    return a->out_len == b->out_len && a_eq(a->out, b->out, a->out_len);
}

static int submit_checker(aipi_ctx_t *c, aipi_job_t *j, aipi_slot_t *s, const aipi_claim_t *cl,
                          uint8_t *verdict)
{
    aipi_code_t code;
    int r, ok;
    if (j->fn.verify == AIPI_VERIFY_CERT && cl->cert_len == 0) {
        ok = 0;
    } else {
        if (!c->exec.check) return AIPI_ERR_EXEC;
        if ((r = load_code(c, &j->fn, &code)) != AIPI_OK) return r;
        ok = c->exec.check(c->exec.ctx, &code, j->in, j->in_len, cl->out, cl->out_len, cl->cert,
                           cl->cert_len);
        if (ok < 0) return ok; /* could not check: the claim is not counted */
    }
    j->nclaims++;
    if (ok == 1) {
        s->verdict = AIPI_V_ACCEPTED;
        j->naccepted++;
        job_accept(c, j, cl->out, cl->out_len);
        job_pay(c, j);
        *verdict = AIPI_V_ACCEPTED;
        return AIPI_OK;
    }
    /* Wrong answer: not paid. The job holds a claim and a refutation, BOTH,
     * until a valid answer arrives or it expires. */
    s->verdict = AIPI_V_REJECTED;
    j->nrejected++;
    job_tag(j, LPX_B_BOTH, lpx_polarity(0, j->nrejected - 1u, 0, j->nrejected, false));
    *verdict = AIPI_V_REJECTED;
    return AIPI_OK;
}

static int submit_quorum(aipi_ctx_t *c, aipi_job_t *j, aipi_slot_t *s, uint8_t *verdict)
{
    uint32_t n = j->fn.quorum_n, k = j->fn.quorum_k, match = 0, best = 0;
    bool split = false;
    if (j->nclaims >= n) return AIPI_ERR_FULL;
    s->verdict = AIPI_V_COUNTED;
    j->nclaims++;
    for (uint32_t i = 0; i < j->nclaims; i++) {
        uint32_t g = 0;
        for (uint32_t m = 0; m < j->nclaims; m++)
            if (same_out(&j->slot[i], &j->slot[m])) g++;
        if (g > best) best = g;
        if (!same_out(&j->slot[i], s))
            split = true;
        else
            match++;
    }
    if (match >= k) {
        for (uint32_t i = 0; i < j->nclaims; i++) {
            bool win = same_out(&j->slot[i], s);
            j->slot[i].verdict = win ? AIPI_V_ACCEPTED : AIPI_V_REJECTED;
            if (win)
                j->naccepted++;
            else
                j->nrejected++;
        }
        job_accept(c, j, s->out, s->out_len);
        job_pay(c, j);
        *verdict = AIPI_V_ACCEPTED;
        return AIPI_OK;
    }
    if (best + (n - j->nclaims) < k) {
        /* No output can reach k any more: a dispute for a person (A4). */
        for (uint32_t i = 0; i < j->nclaims; i++) j->slot[i].verdict = AIPI_V_REJECTED;
        j->nrejected = j->nclaims;
        j->status = AIPI_JOB_DISPUTED;
        job_tag(j, LPX_B_BOTH, LPX_P_ENTROPIC);
        job_refund(c, j);
        *verdict = AIPI_V_DISPUTED;
        return AIPI_OK;
    }
    /* Still decidable: a split vote is a dispute that can converge (BOTH,
     * negentropic: WAIT); an unbroken vote is pending (NEITHER: WAIT). */
    job_tag(j, split ? LPX_B_BOTH : LPX_B_NEITHER, LPX_P_NEGENTROPIC);
    *verdict = AIPI_V_COUNTED;
    return AIPI_OK;
}

int aipi_submit(aipi_ctx_t *c, uint32_t job, const uint8_t *claim, uint32_t len, uint8_t *verdict)
{
    aipi_claim_t cl;
    aipi_job_t *j;
    aipi_slot_t *s;
    int r;
    if (!c || !claim || !verdict || job >= c->job_cap) return AIPI_ERR_ARG;
    j = &c->jobs[job];
    if (j->status != AIPI_JOB_PENDING || j->route != AIPI_ROUTE_MESH) return AIPI_ERR_STATE;
    if ((r = aipi_claim_decode(claim, len, &cl)) != AIPI_OK) return r;
    if (!ipfsn_cid_equal(&cl.call, &j->call_cid)) return AIPI_ERR_ARG; /* another call */
    for (uint32_t i = 0; i < j->nclaims; i++)
        if (a_eq(j->slot[i].executor, cl.executor, 32u)) return AIPI_ERR_DUP;
    if (j->nclaims >= AIPI_CLAIMS_MAX) return AIPI_ERR_FULL;
    s = &j->slot[j->nclaims];
    a_cpy(s->executor, cl.executor, 32u);
    a_cpy(s->out, cl.out, cl.out_len);
    s->out_len = (uint16_t) cl.out_len;
    if (j->fn.verify == AIPI_VERIFY_QUORUM) return submit_quorum(c, j, s, verdict);
    if (j->fn.verify == AIPI_VERIFY_CHECKER || j->fn.verify == AIPI_VERIFY_CERT)
        return submit_checker(c, j, s, &cl, verdict);
    return AIPI_ERR_STATE; /* LOCAL never goes to the mesh */
}

uint32_t aipi_tick(aipi_ctx_t *c, uint64_t now)
{
    uint32_t n = 0;
    if (!c) return 0;
    for (uint32_t i = 0; i < c->job_cap; i++) {
        aipi_job_t *j = &c->jobs[i];
        if (j->status != AIPI_JOB_PENDING || now < j->deadline) continue;
        job_refund(c, j);
        j->status = AIPI_JOB_EXPIRED;
        job_tag(j, LPX_B_NEITHER, LPX_P_ENTROPIC);
        n++;
    }
    return n;
}

int aipi_result(const aipi_ctx_t *c, uint32_t job, uint8_t *out, uint32_t cap, uint32_t *len)
{
    const aipi_job_t *j;
    if (!c || !out || !len || job >= c->job_cap) return AIPI_ERR_ARG;
    j = &c->jobs[job];
    if (j->status != AIPI_JOB_DONE) return AIPI_ERR_STATE;
    if (j->out_len > cap) return AIPI_ERR_SPACE;
    a_cpy(out, j->out, j->out_len);
    *len = j->out_len;
    return AIPI_OK;
}

const aipi_job_t *aipi_job(const aipi_ctx_t *c, uint32_t job)
{
    if (!c || job >= c->job_cap || c->jobs[job].status == AIPI_JOB_FREE) return 0;
    return &c->jobs[job];
}

int aipi_release(aipi_ctx_t *c, uint32_t job)
{
    if (!c || job >= c->job_cap) return AIPI_ERR_ARG;
    if (c->jobs[job].status == AIPI_JOB_PENDING) return AIPI_ERR_STATE;
    a_zero(&c->jobs[job], (uint32_t) sizeof(c->jobs[job]));
    c->jobs[job].status = AIPI_JOB_FREE;
    return AIPI_OK;
}

/* ===== executor side ======================================================== */

int aipi_serve(aipi_ctx_t *c, const uint8_t *call_rec, uint32_t len, uint8_t *claim, uint32_t cap)
{
    ipfsn_cid_t fn_cid, call;
    aipi_fn_t fn;
    aipi_code_t code;
    const uint8_t *in;
    uint32_t in_len, out_len = 0, cert_len = 0;
    uint8_t out[AIPI_IO_MAX];
    uint8_t cert[AIPI_CERT_MAX];
    int r;
    if (!c || !call_rec || !claim) return AIPI_ERR_ARG;
    if ((r = aipi_call_decode(call_rec, len, &fn_cid, &in, &in_len)) != AIPI_OK) return r;
    if ((r = aipi_load_fn(c, &fn_cid, &fn)) != AIPI_OK) return r;
    if (fn.privacy == AIPI_PRIV_LOCAL) return AIPI_ERR_PRIVATE;
    if (fn.verify == AIPI_VERIFY_LOCAL) return AIPI_ERR_PRIVATE; /* never run remotely */
    if (!c->exec.run) return AIPI_ERR_EXEC;
    if ((r = load_code(c, &fn, &code)) != AIPI_OK) return r;
    if ((r = c->exec.run(c->exec.ctx, &code, in, in_len, out, sizeof out, &out_len)) != AIPI_OK)
        return r;
    if (out_len > AIPI_IO_MAX) return AIPI_ERR_EXEC;
    if (fn.verify == AIPI_VERIFY_CERT) {
        if (!c->exec.certify) return AIPI_ERR_EXEC;
        r = c->exec.certify(c->exec.ctx, &code, in, in_len, out, out_len, cert, sizeof cert,
                            &cert_len);
        if (r != AIPI_OK) return r;
        if (cert_len > AIPI_CERT_MAX) return AIPI_ERR_EXEC;
    }
    if ((r = aipi_call_cid(&fn_cid, in, in_len, &call)) != AIPI_OK) return r;
    return aipi_claim_encode(&call, c->self, out, out_len, cert, cert_len, claim, cap);
}

const char *aipi_status_name(int status)
{
    switch (status) {
    case AIPI_OK:
        return "AIPI_OK";
    case AIPI_ERR_ARG:
        return "AIPI_ERR_ARG";
    case AIPI_ERR_MALFORMED:
        return "AIPI_ERR_MALFORMED";
    case AIPI_ERR_UNSUPP:
        return "AIPI_ERR_UNSUPP";
    case AIPI_ERR_SPACE:
        return "AIPI_ERR_SPACE";
    case AIPI_ERR_HASH:
        return "AIPI_ERR_HASH";
    case AIPI_ERR_NOTFOUND:
        return "AIPI_ERR_NOTFOUND";
    case AIPI_ERR_PRIVATE:
        return "AIPI_ERR_PRIVATE";
    case AIPI_ERR_OFFLINE:
        return "AIPI_ERR_OFFLINE";
    case AIPI_ERR_STATE:
        return "AIPI_ERR_STATE";
    case AIPI_ERR_DUP:
        return "AIPI_ERR_DUP";
    case AIPI_ERR_FULL:
        return "AIPI_ERR_FULL";
    case AIPI_ERR_FUNDS:
        return "AIPI_ERR_FUNDS";
    case AIPI_ERR_EXEC:
        return "AIPI_ERR_EXEC";
    case AIPI_ERR_CODEC:
        return "AIPI_ERR_CODEC";
    case AIPI_ERR_TRANSPORT:
        return "AIPI_ERR_TRANSPORT";
    default:
        return "AIPI_ERR_?";
    }
}
