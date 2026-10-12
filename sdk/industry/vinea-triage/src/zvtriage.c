/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zvtriage.c — vinea-triage: triage records, signed hash-chained bundles
 * for store-and-forward links, a station that deduplicates, reorders,
 * detects forks and holds disagreeing records as BOTH for a reviewer, and
 * signed acknowledgements. No encryption (see zvtriage.h). Integer code
 * only (R11). */
#include <stdlib.h>
#include <string.h>

#include "zvtriage.h"

#define REC_MAX_BYTES 400u /* an encoded record is at most ~370 bytes */
#define STAGE_MAX     1024u
#define MAX_REVIEWERS 16u

const char *zvt_status_name(int32_t status) {
    switch (status) {
    case ZVT_E_RESPONDER: return "ZVT_E_RESPONDER";
    case ZVT_E_GAP: return "ZVT_E_GAP";
    case ZVT_E_FORK: return "ZVT_E_FORK";
    case ZVT_E_RECORD: return "ZVT_E_RECORD";
    case ZVT_E_PATIENT: return "ZVT_E_PATIENT";
    case ZVT_E_REVIEWER: return "ZVT_E_REVIEWER";
    default: return zind_status_name(status);
    }
}

/* ===== records ===== */

static const struct {
    uint8_t unit;
    int32_t lo, hi;
} k_vital[8] = {{0, 0, 0},
                {ZVT_U_PER_MIN, 0, 150},
                {ZVT_U_PER_MIN, 0, 350},
                {ZVT_U_MMHG, 0, 400},
                {ZVT_U_PERCENT, 0, 100},
                {ZVT_U_MS, 0, 20000},
                {ZVT_U_SCORE, 3, 15},
                {ZVT_U_DECI_C, 150, 460}};

/* Well-formed UTF-8 without control characters (C0, DEL, C1). */
static int text_ok(const char *s) {
    const uint8_t *p = (const uint8_t *) s;
    while (*p) {
        uint32_t c;
        int n;
        if (*p < 0x80) {
            if (*p < 0x20 || *p == 0x7f) return 0;
            p++;
            continue;
        }
        if ((*p & 0xe0) == 0xc0) {
            c = *p & 0x1f;
            n = 1;
        } else if ((*p & 0xf0) == 0xe0) {
            c = *p & 0x0f;
            n = 2;
        } else if ((*p & 0xf8) == 0xf0) {
            c = *p & 0x07;
            n = 3;
        } else {
            return 0;
        }
        for (int i = 1; i <= n; i++) {
            if ((p[i] & 0xc0) != 0x80) return 0;
            c = (c << 6) | (p[i] & 0x3f);
        }
        if ((n == 1 && c < 0x80) || (n == 2 && c < 0x800) || (n == 3 && c < 0x10000) ||
            c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff) || (c >= 0x80 && c <= 0x9f))
            return 0;
        p += n + 1;
    }
    return 1;
}

static int32_t rec_fail(const char *why) {
    zind_set_error("zvt_record: %s", why);
    return ZVT_E_RECORD;
}

int32_t zvt_record_check(const zvt_record *r) {
    uint8_t seen = 0;
    if (!r) return ZXV_E_ARG;
    if (!memchr(r->patient, 0, sizeof r->patient) || !zind_ident_ok(r->patient, ZVT_ID_MAX))
        return rec_fail("the patient ID is 1..32 of [A-Za-z0-9._:/-]");
    if (r->system != ZVT_SYS_START && r->system != ZVT_SYS_SALT)
        return rec_fail("the system is START or SALT");
    if (r->category < ZVT_CAT_MINOR || r->category > ZVT_CAT_DEAD)
        return rec_fail("the category is MINOR, DELAYED, IMMEDIATE, EXPECTANT or DEAD");
    if (r->category == ZVT_CAT_EXPECTANT && r->system != ZVT_SYS_SALT)
        return rec_fail("EXPECTANT exists only in SALT");
    if (r->n_vitals > ZVT_VITALS_MAX) return rec_fail("at most 8 vitals");
    for (uint32_t i = 0; i < r->n_vitals; i++) {
        const zvt_vital *v = &r->vitals[i];
        if (v->kind < ZVT_V_RESP || v->kind > ZVT_V_TEMP) return rec_fail("an unknown vital kind");
        if (seen & (1u << v->kind)) return rec_fail("a vital kind given twice");
        seen = (uint8_t) (seen | (1u << v->kind));
        if (v->unit != k_vital[v->kind].unit) return rec_fail("a vital in the wrong unit");
        if (v->value < k_vital[v->kind].lo || v->value > k_vital[v->kind].hi)
            return rec_fail("a vital outside its plausible range");
    }
    if (!memchr(r->note, 0, sizeof r->note) || !text_ok(r->note))
        return rec_fail("the note is up to 200 bytes of UTF-8 without control characters");
    return ZXV_OK;
}

static int is_zero(const uint8_t h[32]) {
    uint8_t acc = 0;
    for (int i = 0; i < 32; i++) acc |= h[i];
    return acc == 0;
}

/* u8 1, responder, record_no, patient, system, category, observed,
 * based_on, vitals, note */
static void enc_record(zind_wr *w, const char *responder, uint64_t record_no,
                       const zvt_record *r) {
    zind_wr_u8(w, 1);
    zind_wr_str(w, responder);
    zind_wr_u64(w, record_no);
    zind_wr_str(w, r->patient);
    zind_wr_u8(w, r->system);
    zind_wr_u8(w, r->category);
    zind_wr_u64(w, r->observed);
    zind_wr_bytes(w, r->based_on, 32);
    zind_wr_u8(w, r->n_vitals);
    for (uint32_t i = 0; i < r->n_vitals; i++) {
        zind_wr_u8(w, r->vitals[i].kind);
        zind_wr_u8(w, r->vitals[i].unit);
        zind_wr_u32(w, (uint32_t) r->vitals[i].value);
    }
    zind_wr_str(w, r->note);
}

static int32_t dec_record(const uint8_t *buf, size_t len, char responder[ZVT_ID_MAX + 1],
                          uint64_t *record_no, zvt_record *r) {
    zind_rd rd;
    char tmp[256];
    memset(r, 0, sizeof *r);
    zind_rd_init(&rd, buf, len);
    if (zind_rd_u8(&rd) != 1) return ZXV_E_FORMAT;
    zind_rd_str(&rd, tmp, sizeof tmp);
    if (rd.bad || !zind_ident_ok(tmp, ZVT_ID_MAX)) return ZXV_E_FORMAT;
    memcpy(responder, tmp, strlen(tmp) + 1);
    *record_no = zind_rd_u64(&rd);
    zind_rd_str(&rd, tmp, sizeof tmp);
    if (rd.bad || strlen(tmp) > ZVT_ID_MAX) return ZXV_E_FORMAT;
    memcpy(r->patient, tmp, strlen(tmp) + 1);
    r->system = zind_rd_u8(&rd);
    r->category = zind_rd_u8(&rd);
    r->observed = zind_rd_u64(&rd);
    for (int i = 0; i < 32; i++) r->based_on[i] = zind_rd_u8(&rd);
    r->n_vitals = zind_rd_u8(&rd);
    if (rd.bad || r->n_vitals > ZVT_VITALS_MAX) return ZXV_E_FORMAT;
    for (uint32_t i = 0; i < r->n_vitals; i++) {
        r->vitals[i].kind = zind_rd_u8(&rd);
        r->vitals[i].unit = zind_rd_u8(&rd);
        r->vitals[i].value = (int32_t) zind_rd_u32(&rd);
    }
    zind_rd_str(&rd, tmp, sizeof tmp);
    if (rd.bad || rd.pos != len || strlen(tmp) > ZVT_NOTE_MAX) return ZXV_E_FORMAT;
    memcpy(r->note, tmp, strlen(tmp) + 1);
    return zvt_record_check(r) == ZXV_OK ? ZXV_OK : ZXV_E_FORMAT;
}

/* ===== bundles =====
 * "ZVTB" u8 version(1) u8 enc(0) str responder u64 seq prev[32] u64 created
 * u16 n { u16 len, record } x n, then the ML-DSA-87 signature of
 * zind_subject("vinea-triage/bundle", everything before it). */

typedef struct {
    char responder[ZVT_ID_MAX + 1];
    uint64_t seq, created;
    uint8_t prev[32], hash[32];
    uint16_t n;
    const uint8_t *rec[ZVT_BUNDLE_RECORDS];
    uint16_t rec_len[ZVT_BUNDLE_RECORDS];
    size_t body_len;
    const uint8_t *sig;
} bundle_t;

static int32_t parse_bundle(const uint8_t *buf, size_t len, bundle_t *b) {
    zind_rd rd;
    char tmp[256];
    uint8_t enc;
    memset(b, 0, sizeof *b);
    if (!buf || len < 4 + ZIND_SIG_BYTES || memcmp(buf, "ZVTB", 4) != 0) goto bad;
    zind_rd_init(&rd, buf, len - ZIND_SIG_BYTES);
    rd.pos = 4;
    if (zind_rd_u8(&rd) != 1) goto bad;
    enc = zind_rd_u8(&rd);
    if (!rd.bad && enc != 0) {
        zind_set_error("zvt: the bundle claims an encrypted payload; this kit has no cipher "
                       "(encryption belongs to the transport)");
        return ZXV_E_NOTIMPL;
    }
    zind_rd_str(&rd, tmp, sizeof tmp);
    if (rd.bad || !zind_ident_ok(tmp, ZVT_ID_MAX)) goto bad;
    memcpy(b->responder, tmp, strlen(tmp) + 1);
    b->seq = zind_rd_u64(&rd);
    for (int i = 0; i < 32; i++) b->prev[i] = zind_rd_u8(&rd);
    b->created = zind_rd_u64(&rd);
    b->n = zind_rd_u16(&rd);
    if (rd.bad || b->n == 0 || b->n > ZVT_BUNDLE_RECORDS) goto bad;
    for (uint32_t i = 0; i < b->n; i++) {
        uint16_t l = zind_rd_u16(&rd);
        if (rd.bad || l == 0 || l > REC_MAX_BYTES || l > rd.len - rd.pos) goto bad;
        b->rec[i] = rd.buf + rd.pos;
        b->rec_len[i] = l;
        rd.pos += l;
    }
    if (rd.pos != rd.len || rd.len > ZVT_BUNDLE_MAX) goto bad;
    b->body_len = rd.len;
    b->sig = buf + rd.len;
    return zind_subject("vinea-triage/bundle", buf, b->body_len, b->hash);
bad:
    zind_set_error("zvt: malformed bundle");
    return ZXV_E_FORMAT;
}

/* ===== outbox ===== */

typedef struct {
    uint8_t *bytes;
    size_t len;
    uint64_t seq;
    uint8_t hash[32];
} obundle_t;

struct zvt_outbox {
    const zxv_cap *cap;
    const zind_signer *key;
    char id[ZVT_ID_MAX + 1];
    uint64_t next_record_no, next_seq;
    uint8_t head[32];
    struct {
        uint8_t *bytes;
        size_t len;
    } staged[STAGE_MAX];
    uint32_t n_staged;
    obundle_t out[ZVT_OUTBOX_MAX];
    uint32_t n_out;
    uint8_t (*hashes)[32]; /* every bundle made, by seq */
    uint64_t n_hashes, a_hashes;
};

int32_t zvt_outbox_create(const zxv_cap *cap, const char *responder, const zind_signer *key,
                          zvt_outbox **out) {
    zvt_outbox *o;
    int32_t st;
    if (!cap || !key || !out) return ZXV_E_ARG;
    *out = NULL;
    if (!zind_ident_ok(responder, ZVT_ID_MAX)) {
        zind_set_error("zvt_outbox_create: a responder ID is 1..32 of [A-Za-z0-9._:/-]");
        return ZXV_E_ARG;
    }
    st = zxv_cap_check(cap, ZXV_CAP_CRYPTO_SIGN);
    if (st != ZXV_OK) {
        zind_set_error("zvt_outbox_create: the cap needs CRYPTO_SIGN");
        return st;
    }
    o = (zvt_outbox *) calloc(1, sizeof *o);
    if (!o) return ZXV_E_NOMEM;
    o->cap = cap;
    o->key = key;
    memcpy(o->id, responder, strlen(responder) + 1);
    *out = o;
    return ZXV_OK;
}

void zvt_outbox_destroy(zvt_outbox *o) {
    if (!o) return;
    for (uint32_t i = 0; i < o->n_staged; i++) free(o->staged[i].bytes);
    for (uint32_t i = 0; i < o->n_out; i++) free(o->out[i].bytes);
    free(o->hashes);
    free(o);
}

int32_t zvt_outbox_record(zvt_outbox *o, const zvt_record *r, uint8_t out_hash[32]) {
    uint8_t buf[REC_MAX_BYTES];
    zind_wr w;
    int32_t st;
    if (!o || !r) return ZXV_E_ARG;
    st = zvt_record_check(r);
    if (st != ZXV_OK) return st;
    if (o->n_staged >= STAGE_MAX) {
        zind_set_error("zvt_outbox_record: %u records staged; seal a bundle first", STAGE_MAX);
        return ZXV_E_CAPACITY;
    }
    zind_wr_init(&w, buf, sizeof buf);
    enc_record(&w, o->id, o->next_record_no, r);
    if (w.overflow) return ZXV_E_INTERNAL;
    if (out_hash) {
        st = zind_subject("vinea-triage/rec", buf, w.len, out_hash);
        if (st != ZXV_OK) return st;
    }
    o->staged[o->n_staged].bytes = (uint8_t *) malloc(w.len);
    if (!o->staged[o->n_staged].bytes) return ZXV_E_NOMEM;
    memcpy(o->staged[o->n_staged].bytes, buf, w.len);
    o->staged[o->n_staged].len = w.len;
    o->n_staged++;
    o->next_record_no++;
    return ZXV_OK;
}

int32_t zvt_outbox_seal(zvt_outbox *o, uint64_t now, uint64_t *out_seq) {
    uint8_t *buf;
    uint32_t take = 0;
    size_t hdr, body;
    zind_wr w;
    obundle_t *ob;
    int32_t st;
    if (!o) return ZXV_E_ARG;
    if (o->n_staged == 0) {
        zind_set_error("zvt_outbox_seal: nothing staged");
        return ZXV_E_STATE;
    }
    if (o->n_out >= ZVT_OUTBOX_MAX) {
        zind_set_error("zvt_outbox_seal: %u bundles unacknowledged; deliver them first",
                       ZVT_OUTBOX_MAX);
        return ZXV_E_CAPACITY;
    }
    hdr = 4 + 1 + 1 + 1 + strlen(o->id) + 8 + 32 + 8 + 2;
    body = hdr;
    while (take < o->n_staged && take < ZVT_BUNDLE_RECORDS &&
           body + 2 + o->staged[take].len <= ZVT_BUNDLE_MAX) {
        body += 2 + o->staged[take].len;
        take++;
    }
    if (o->n_hashes == o->a_hashes) {
        uint64_t a = o->a_hashes ? o->a_hashes * 2 : 16;
        uint8_t (*nh)[32] = (uint8_t (*)[32]) realloc(o->hashes, (size_t) a * 32);
        if (!nh) return ZXV_E_NOMEM;
        o->hashes = nh;
        o->a_hashes = a;
    }
    buf = (uint8_t *) malloc(body + ZIND_SIG_BYTES);
    if (!buf) return ZXV_E_NOMEM;
    zind_wr_init(&w, buf, body);
    zind_wr_bytes(&w, "ZVTB", 4);
    zind_wr_u8(&w, 1);
    zind_wr_u8(&w, 0); /* not encrypted: see zvtriage.h */
    zind_wr_str(&w, o->id);
    zind_wr_u64(&w, o->next_seq);
    zind_wr_bytes(&w, o->head, 32);
    zind_wr_u64(&w, now);
    zind_wr_u16(&w, (uint16_t) take);
    for (uint32_t i = 0; i < take; i++) {
        zind_wr_u16(&w, (uint16_t) o->staged[i].len);
        zind_wr_bytes(&w, o->staged[i].bytes, o->staged[i].len);
    }
    if (w.overflow || w.len != body) {
        free(buf);
        return ZXV_E_INTERNAL;
    }
    ob = &o->out[o->n_out];
    st = zind_subject("vinea-triage/bundle", buf, body, ob->hash);
    if (st == ZXV_OK) st = zind_sign(o->cap, o->key, ob->hash, 32, ZVT_BUNDLE_CTX, buf + body);
    if (st != ZXV_OK) {
        free(buf);
        zind_set_error("zvt_outbox_seal: signing failed (%s)", zxv_status_name(st));
        return st;
    }
    ob->bytes = buf;
    ob->len = body + ZIND_SIG_BYTES;
    ob->seq = o->next_seq;
    memcpy(o->head, ob->hash, 32);
    memcpy(o->hashes[o->n_hashes++], ob->hash, 32);
    o->n_out++;
    if (out_seq) *out_seq = o->next_seq;
    o->next_seq++;
    for (uint32_t i = 0; i < take; i++) free(o->staged[i].bytes);
    memmove(o->staged, o->staged + take, (o->n_staged - take) * sizeof o->staged[0]);
    o->n_staged -= take;
    return ZXV_OK;
}

int32_t zvt_outbox_staged(const zvt_outbox *o, uint32_t *out_count) {
    if (!o || !out_count) return ZXV_E_ARG;
    *out_count = o->n_staged;
    return ZXV_OK;
}

int32_t zvt_outbox_pending(const zvt_outbox *o, uint32_t *out_count) {
    if (!o || !out_count) return ZXV_E_ARG;
    *out_count = o->n_out;
    return ZXV_OK;
}

int32_t zvt_outbox_bundle(const zvt_outbox *o, uint32_t i, uint8_t *buf, size_t cap,
                          size_t *out_len) {
    if (!o || !out_len) return ZXV_E_ARG;
    if (i >= o->n_out) return ZXV_E_NOT_FOUND;
    *out_len = o->out[i].len;
    if (!buf || cap < o->out[i].len) return ZXV_E_BUFFER;
    memcpy(buf, o->out[i].bytes, o->out[i].len);
    return ZXV_OK;
}

/* "ZVTA" u8 1 str responder u64 next_seq head[32], then the signature of
 * zind_subject("vinea-triage/ack", body). */
int32_t zvt_outbox_ack(zvt_outbox *o, const uint8_t station_pk[ZIND_PK_BYTES],
                       const uint8_t *ack, size_t len) {
    zind_rd rd;
    char who[256];
    uint64_t next;
    uint8_t head[32], h[32];
    uint32_t keep = 0;
    int32_t st;
    if (!o || !station_pk || !ack) return ZXV_E_ARG;
    if (len < 4 + ZIND_SIG_BYTES || memcmp(ack, "ZVTA", 4) != 0) return ZXV_E_FORMAT;
    zind_rd_init(&rd, ack, len - ZIND_SIG_BYTES);
    rd.pos = 4;
    if (zind_rd_u8(&rd) != 1) return ZXV_E_FORMAT;
    zind_rd_str(&rd, who, sizeof who);
    next = zind_rd_u64(&rd);
    for (int i = 0; i < 32; i++) head[i] = zind_rd_u8(&rd);
    if (rd.bad || rd.pos != rd.len) return ZXV_E_FORMAT;
    st = zind_subject("vinea-triage/ack", ack, rd.len, h);
    if (st == ZXV_OK) st = zind_verify(o->cap, station_pk, h, 32, ZVT_ACK_CTX, ack + rd.len);
    if (st != ZXV_OK) {
        zind_set_error("zvt_outbox_ack: the acknowledgement is not signed by this station");
        return st;
    }
    if (strcmp(who, o->id) != 0) {
        zind_set_error("zvt_outbox_ack: the acknowledgement is for responder %s", who);
        return ZVT_E_RESPONDER;
    }
    if (next > o->n_hashes ||
        (next > 0 && memcmp(o->hashes[next - 1], head, 32) != 0)) {
        zind_set_error("zvt_outbox_ack: the station holds a different history (seq %llu)",
                       (unsigned long long) next);
        return ZIND_E_CHAIN;
    }
    for (uint32_t i = 0; i < o->n_out; i++) {
        if (o->out[i].seq < next) {
            free(o->out[i].bytes);
        } else {
            o->out[keep++] = o->out[i];
        }
    }
    o->n_out = keep;
    return ZXV_OK;
}

/* ===== station ===== */

typedef struct {
    char id[ZVT_ID_MAX + 1];
    uint8_t pk[ZIND_PK_BYTES];
    uint64_t next_seq;
    uint8_t head[32];
    uint8_t (*hashes)[32];
    uint64_t a_hashes;
    struct {
        uint8_t *bytes;
        size_t len;
        uint64_t seq;
        uint8_t hash[32];
    } pend[ZVT_PENDING_MAX];
    uint32_t n_pend;
    int forked;
} resp_t;

typedef struct {
    char id[ZVT_ID_MAX + 1];
    uint8_t pk[ZIND_PK_BYTES];
    uint8_t key_id[32];
} reviewer_t;

typedef struct {
    uint8_t is_decision;
    uint8_t category;
    uint32_t idx; /* record index, or decision index */
    uint32_t who; /* responder index, or reviewer index */
    uint8_t hash[32];
} head_t;

typedef struct {
    char id[ZVT_ID_MAX + 1];
    head_t *heads;
    uint32_t n_heads, a_heads;
    uint8_t (*refs)[32];
    uint32_t n_refs, a_refs;
    uint32_t records;
    int32_t lp;
} patient_t;

typedef struct {
    zvt_record rec;
    uint32_t resp;
    uint64_t record_no;
    uint8_t hash[32];
} srec_t;

#define MAP_SLOTS (2u * ZVT_MAX_RECORDS) /* power of two, >= 2 x every table */

struct zvt_station {
    const zxv_cap *cap;
    const zind_signer *key;
    zind_qsink *qs;
    zind_chain *journal;
    resp_t *resp[ZVT_MAX_RESPONDERS];
    uint32_t n_resp;
    reviewer_t rev[MAX_REVIEWERS];
    uint32_t n_rev;
    patient_t *pat;
    uint32_t n_pat, a_pat;
    srec_t *recs;
    uint32_t n_recs, a_recs;
    uint32_t n_decisions;
    uint32_t *pat_map, *rec_map; /* index + 1, 0 = empty */
    zind_seen *bundles;
    uint64_t st_bundles, st_replays, st_dups, st_badsig;
};

static uint32_t fnv32(const void *p, size_t n) {
    const uint8_t *b = (const uint8_t *) p;
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 16777619u;
    return h;
}

static uint32_t rec_key(uint32_t resp, uint64_t no) {
    uint8_t k[12];
    for (int i = 0; i < 4; i++) k[i] = (uint8_t) (resp >> (8 * i));
    for (int i = 0; i < 8; i++) k[4 + i] = (uint8_t) (no >> (8 * i));
    return fnv32(k, sizeof k);
}

static int find_patient(const zvt_station *s, const char *id) {
    uint32_t h;
    if (!id) return -1;
    h = fnv32(id, strlen(id)) & (MAP_SLOTS - 1);
    while (s->pat_map[h]) {
        if (strcmp(s->pat[s->pat_map[h] - 1].id, id) == 0) return (int) (s->pat_map[h] - 1);
        h = (h + 1) & (MAP_SLOTS - 1);
    }
    return -1;
}

static int find_rec(const zvt_station *s, uint32_t resp, uint64_t no) {
    uint32_t h = rec_key(resp, no) & (MAP_SLOTS - 1);
    while (s->rec_map[h]) {
        const srec_t *r = &s->recs[s->rec_map[h] - 1];
        if (r->resp == resp && r->record_no == no) return (int) (s->rec_map[h] - 1);
        h = (h + 1) & (MAP_SLOTS - 1);
    }
    return -1;
}

static int find_resp(const zvt_station *s, const char *id) {
    if (!id) return -1;
    for (uint32_t i = 0; i < s->n_resp; i++)
        if (strcmp(s->resp[i]->id, id) == 0) return (int) i;
    return -1;
}

static int find_rev(const zvt_station *s, const char *id) {
    if (!id) return -1;
    for (uint32_t i = 0; i < s->n_rev; i++)
        if (strcmp(s->rev[i].id, id) == 0) return (int) i;
    return -1;
}

int32_t zvt_station_create(const zxv_cap *cap, const zind_signer *station_key, zxv_hexlog *qlog,
                           zvt_station **out) {
    zvt_station *s;
    int32_t st;
    if (!cap || !station_key || !out) return ZXV_E_ARG;
    *out = NULL;
    st = zxv_cap_check(cap, ZXV_CAP_CRYPTO_SIGN);
    if (st != ZXV_OK) {
        zind_set_error("zvt_station_create: the cap needs CRYPTO_SIGN");
        return st;
    }
    s = (zvt_station *) calloc(1, sizeof *s);
    if (!s) return ZXV_E_NOMEM;
    s->cap = cap;
    s->key = station_key;
    s->pat_map = (uint32_t *) calloc(MAP_SLOTS, sizeof *s->pat_map);
    s->rec_map = (uint32_t *) calloc(MAP_SLOTS, sizeof *s->rec_map);
    st = (!s->pat_map || !s->rec_map) ? ZXV_E_NOMEM : ZXV_OK;
    if (st == ZXV_OK) st = zind_seen_create(1u << 16, &s->bundles);
    if (st == ZXV_OK) st = zind_chain_create(ZVT_JOURNAL, ZIND_CHAIN_MAX, &s->journal);
    if (st == ZXV_OK && qlog) st = zind_qsink_create(cap, qlog, NULL, &s->qs);
    if (st != ZXV_OK) {
        zvt_station_destroy(s);
        return st;
    }
    *out = s;
    return ZXV_OK;
}

void zvt_station_destroy(zvt_station *s) {
    if (!s) return;
    for (uint32_t i = 0; i < s->n_resp; i++) {
        for (uint32_t k = 0; k < s->resp[i]->n_pend; k++) free(s->resp[i]->pend[k].bytes);
        free(s->resp[i]->hashes);
        free(s->resp[i]);
    }
    for (uint32_t i = 0; i < s->n_pat; i++) {
        free(s->pat[i].heads);
        free(s->pat[i].refs);
    }
    free(s->pat);
    free(s->recs);
    free(s->pat_map);
    free(s->rec_map);
    zind_seen_destroy(s->bundles);
    zind_chain_destroy(s->journal);
    zind_qsink_destroy(s->qs);
    free(s);
}

static int32_t key_dup(const zvt_station *s, const uint8_t pk[ZIND_PK_BYTES]) {
    for (uint32_t i = 0; i < s->n_resp; i++)
        if (memcmp(s->resp[i]->pk, pk, ZIND_PK_BYTES) == 0) return 1;
    for (uint32_t i = 0; i < s->n_rev; i++)
        if (memcmp(s->rev[i].pk, pk, ZIND_PK_BYTES) == 0) return 1;
    return 0;
}

int32_t zvt_responder_add(zvt_station *s, const char *responder, const uint8_t pk[ZIND_PK_BYTES]) {
    resp_t *r;
    if (!s || !pk) return ZXV_E_ARG;
    if (!zind_ident_ok(responder, ZVT_ID_MAX)) {
        zind_set_error("zvt_responder_add: a responder ID is 1..32 of [A-Za-z0-9._:/-]");
        return ZXV_E_ARG;
    }
    if (find_resp(s, responder) >= 0 || find_rev(s, responder) >= 0 || key_dup(s, pk))
        return ZXV_E_DUPLICATE;
    if (s->n_resp >= ZVT_MAX_RESPONDERS) return ZXV_E_CAPACITY;
    r = (resp_t *) calloc(1, sizeof *r);
    if (!r) return ZXV_E_NOMEM;
    memcpy(r->id, responder, strlen(responder) + 1);
    memcpy(r->pk, pk, ZIND_PK_BYTES);
    s->resp[s->n_resp++] = r;
    return ZXV_OK;
}

int32_t zvt_reviewer_add(zvt_station *s, const char *reviewer, const uint8_t pk[ZIND_PK_BYTES]) {
    reviewer_t *v;
    int32_t st;
    if (!s || !pk) return ZXV_E_ARG;
    if (!zind_ident_ok(reviewer, ZVT_ID_MAX)) {
        zind_set_error("zvt_reviewer_add: a reviewer ID is 1..32 of [A-Za-z0-9._:/-]");
        return ZXV_E_ARG;
    }
    if (find_resp(s, reviewer) >= 0 || find_rev(s, reviewer) >= 0 || key_dup(s, pk))
        return ZXV_E_DUPLICATE;
    if (s->n_rev >= MAX_REVIEWERS) return ZXV_E_CAPACITY;
    v = &s->rev[s->n_rev];
    st = zind_key_id(pk, v->key_id);
    if (st != ZXV_OK) return st;
    memcpy(v->id, reviewer, strlen(reviewer) + 1);
    memcpy(v->pk, pk, ZIND_PK_BYTES);
    s->n_rev++;
    return ZXV_OK;
}

static void quarantine(zvt_station *s, int32_t lp, uint8_t pol, uint8_t reason, uint32_t who,
                       const char *dom, const void *data, size_t len) {
    uint8_t tag = 0, subj[32];
    if (!s->qs) return;
    if (zind_tag(lp, pol, ZXV_HEX_PHASE_EMERGING, &tag) != ZXV_OK) return;
    if (zind_subject(dom, (const uint8_t *) data, len, subj) != ZXV_OK) return;
    (void) zind_qsink_event(s->qs, tag, reason, who, subj, NULL);
}

static int32_t fork_resp(zvt_station *s, uint32_t ri, uint64_t seq, const uint8_t held[32],
                         const uint8_t other[32]) {
    resp_t *r = s->resp[ri];
    uint8_t p[ZVT_ID_MAX + 1 + 8 + 64];
    zind_wr w;
    if (!r->forked) {
        r->forked = 1;
        zind_wr_init(&w, p, sizeof p);
        zind_wr_str(&w, r->id);
        zind_wr_u64(&w, seq);
        zind_wr_bytes(&w, held, 32);
        zind_wr_bytes(&w, other, 32);
        (void) zind_chain_append(s->cap, s->journal, s->key, ZVT_J_FORK, p, w.len, NULL, NULL);
        quarantine(s, ZXV_LP_BOTH, ZXV_HEX_POL_NEUTRAL, ZIND_REASON_HELD_BOTH, ri,
                   "vinea-triage/responder", r->id, strlen(r->id));
    }
    zind_set_error("zvt_station_receive: responder %s has two different bundles numbered %llu; "
                   "held for a person (a cloned device or key?)",
                   r->id, (unsigned long long) seq);
    return ZVT_E_FORK;
}

static int has_ref(const patient_t *p, const uint8_t h[32]) {
    for (uint32_t i = 0; i < p->n_refs; i++)
        if (memcmp(p->refs[i], h, 32) == 0) return 1;
    return 0;
}

static int32_t add_ref(patient_t *p, const uint8_t h[32]) {
    if (has_ref(p, h)) return ZXV_OK;
    if (p->n_refs == p->a_refs) {
        uint32_t a = p->a_refs ? p->a_refs * 2 : 4;
        uint8_t (*n)[32] = (uint8_t (*)[32]) realloc(p->refs, (size_t) a * 32);
        if (!n) return ZXV_E_NOMEM;
        p->refs = n;
        p->a_refs = a;
    }
    memcpy(p->refs[p->n_refs++], h, 32);
    return ZXV_OK;
}

static int32_t add_head(patient_t *p, const head_t *h) {
    if (p->n_heads == p->a_heads) {
        uint32_t a = p->a_heads ? p->a_heads * 2 : 4;
        head_t *n = (head_t *) realloc(p->heads, (size_t) a * sizeof *n);
        if (!n) return ZXV_E_NOMEM;
        p->heads = n;
        p->a_heads = a;
    }
    p->heads[p->n_heads++] = *h;
    return ZXV_OK;
}

static uint8_t cat_mask(const patient_t *p) {
    uint8_t m = 0;
    for (uint32_t i = 0; i < p->n_heads; i++) m = (uint8_t) (m | (1u << p->heads[i].category));
    return m;
}

static int32_t lp_of(const patient_t *p) {
    uint8_t m = cat_mask(p);
    if (m == 0) return ZXV_LP_NEITHER;
    return (m & (m - 1)) ? ZXV_LP_BOTH : ZXV_LP_TRUE;
}

/* One record into its patient's picture. *conflict = 1 if the patient
 * became BOTH. */
static int32_t apply_record(zvt_station *s, uint32_t ri, uint32_t idx, int *conflict) {
    srec_t *sr = &s->recs[idx];
    patient_t *p;
    int pi = find_patient(s, sr->rec.patient);
    int32_t old, st;
    uint32_t keep = 0;
    head_t h;
    *conflict = 0;
    if (pi < 0) {
        uint32_t slot;
        if (s->n_pat == s->a_pat) {
            uint32_t a = s->a_pat ? s->a_pat * 2 : 64;
            patient_t *np = (patient_t *) realloc(s->pat, (size_t) a * sizeof *np);
            if (!np) return ZXV_E_NOMEM;
            s->pat = np;
            s->a_pat = a;
        }
        pi = (int) s->n_pat++;
        memset(&s->pat[pi], 0, sizeof s->pat[pi]);
        memcpy(s->pat[pi].id, sr->rec.patient, strlen(sr->rec.patient) + 1);
        slot = fnv32(sr->rec.patient, strlen(sr->rec.patient)) & (MAP_SLOTS - 1);
        while (s->pat_map[slot]) slot = (slot + 1) & (MAP_SLOTS - 1);
        s->pat_map[slot] = (uint32_t) pi + 1;
    }
    p = &s->pat[pi];
    old = lp_of(p);
    p->records++;
    if (!is_zero(sr->rec.based_on)) {
        st = add_ref(p, sr->rec.based_on);
        if (st != ZXV_OK) return st;
    }
    /* a record replaces what it builds on and its own responder's earlier
     * record; everything else stands beside it */
    for (uint32_t i = 0; i < p->n_heads; i++) {
        const head_t *x = &p->heads[i];
        if (memcmp(x->hash, sr->rec.based_on, 32) == 0) continue;
        if (!x->is_decision && x->who == ri) continue;
        p->heads[keep++] = *x;
    }
    p->n_heads = keep;
    if (!has_ref(p, sr->hash)) { /* else a later record already built on it */
        memset(&h, 0, sizeof h);
        h.is_decision = 0;
        h.category = sr->rec.category;
        h.idx = idx;
        h.who = ri;
        memcpy(h.hash, sr->hash, 32);
        st = add_head(p, &h);
        if (st != ZXV_OK) return st;
    }
    p->lp = lp_of(p);
    if (p->lp == ZXV_LP_BOTH && old != ZXV_LP_BOTH) {
        *conflict = 1;
        quarantine(s, ZXV_LP_BOTH, ZXV_HEX_POL_NEUTRAL, ZIND_REASON_HELD_BOTH, ri,
                   "vinea-triage/patient", p->id, strlen(p->id));
    }
    return ZXV_OK;
}

/* Apply one in-order bundle: every record or none. */
static int32_t apply_bundle(zvt_station *s, uint32_t ri, const bundle_t *b, zvt_receipt *rc) {
    resp_t *r = s->resp[ri];
    zvt_record recs[ZVT_BUNDLE_RECORDS];
    uint64_t nos[ZVT_BUNDLE_RECORDS];
    uint8_t hashes[ZVT_BUNDLE_RECORDS][32], jp[128];
    int dup[ZVT_BUNDLE_RECORDS];
    uint32_t fresh = 0, dups = 0, conflicts = 0;
    zind_wr w;
    int32_t st;
    for (uint32_t i = 0; i < b->n; i++) {
        char who[ZVT_ID_MAX + 1];
        int k;
        st = dec_record(b->rec[i], b->rec_len[i], who, &nos[i], &recs[i]);
        if (st != ZXV_OK || strcmp(who, r->id) != 0) {
            zind_set_error("zvt_station_receive: record %u of the bundle is malformed or not "
                           "by %s",
                           i, r->id);
            return ZXV_E_FORMAT;
        }
        st = zind_subject("vinea-triage/rec", b->rec[i], b->rec_len[i], hashes[i]);
        if (st != ZXV_OK) return st;
        dup[i] = 0;
        k = find_rec(s, ri, nos[i]);
        if (k >= 0) {
            if (memcmp(s->recs[k].hash, hashes[i], 32) != 0)
                return fork_resp(s, ri, b->seq, s->recs[k].hash, hashes[i]);
            dup[i] = 1;
        }
        for (uint32_t j = 0; j < i && !dup[i]; j++)
            if (nos[j] == nos[i]) {
                if (memcmp(hashes[j], hashes[i], 32) != 0)
                    return fork_resp(s, ri, b->seq, hashes[j], hashes[i]);
                dup[i] = 1;
            }
        if (dup[i]) dups++;
        else fresh++;
    }
    if (s->n_recs + fresh > ZVT_MAX_RECORDS || s->n_pat + fresh > ZVT_MAX_PATIENTS ||
        s->st_bundles >= (1u << 16)) {
        zind_set_error("zvt_station_receive: the station is full");
        return ZXV_E_CAPACITY;
    }
    if (s->n_recs + fresh > s->a_recs) {
        uint32_t a = s->a_recs ? s->a_recs : 64;
        srec_t *nr;
        while (a < s->n_recs + fresh) a *= 2;
        nr = (srec_t *) realloc(s->recs, (size_t) a * sizeof *nr);
        if (!nr) return ZXV_E_NOMEM;
        s->recs = nr;
        s->a_recs = a;
    }
    if (r->next_seq == r->a_hashes) {
        uint64_t a = r->a_hashes ? r->a_hashes * 2 : 16;
        uint8_t (*nh)[32] = (uint8_t (*)[32]) realloc(r->hashes, (size_t) a * 32);
        if (!nh) return ZXV_E_NOMEM;
        r->hashes = nh;
        r->a_hashes = a;
    }
    /* the journal first: if it cannot be signed, nothing changes */
    zind_wr_init(&w, jp, sizeof jp);
    zind_wr_str(&w, r->id);
    zind_wr_u64(&w, b->seq);
    zind_wr_bytes(&w, b->hash, 32);
    zind_wr_u16(&w, (uint16_t) fresh);
    zind_wr_u16(&w, (uint16_t) dups);
    st = zind_chain_append(s->cap, s->journal, s->key, ZVT_J_BUNDLE, jp, w.len, NULL, NULL);
    if (st != ZXV_OK) return st;
    for (uint32_t i = 0; i < b->n; i++) {
        uint32_t idx, slot;
        int conflict = 0;
        if (dup[i]) continue;
        idx = s->n_recs++;
        s->recs[idx].rec = recs[i];
        s->recs[idx].resp = ri;
        s->recs[idx].record_no = nos[i];
        memcpy(s->recs[idx].hash, hashes[i], 32);
        slot = rec_key(ri, nos[i]) & (MAP_SLOTS - 1);
        while (s->rec_map[slot]) slot = (slot + 1) & (MAP_SLOTS - 1);
        s->rec_map[slot] = idx + 1;
        st = apply_record(s, ri, idx, &conflict);
        if (st != ZXV_OK) return st;
        conflicts += (uint32_t) conflict;
    }
    memcpy(r->hashes[r->next_seq], b->hash, 32);
    memcpy(r->head, b->hash, 32);
    r->next_seq++;
    (void) zind_seen_add(s->bundles, b->hash);
    s->st_bundles++;
    s->st_dups += dups;
    if (rc) {
        rc->bundles++;
        rc->records += fresh;
        rc->duplicates += dups;
        rc->new_conflicts += conflicts;
    }
    return ZXV_OK;
}

int32_t zvt_station_receive(zvt_station *s, const uint8_t *buf, size_t len, zvt_receipt *out) {
    bundle_t b;
    resp_t *r;
    int ri;
    int32_t st, present = 0;
    zvt_receipt rc;
    memset(&rc, 0, sizeof rc);
    if (out) memset(out, 0, sizeof *out);
    if (!s || !buf) return ZXV_E_ARG;
    st = parse_bundle(buf, len, &b);
    if (st != ZXV_OK) return st;
    memcpy(rc.responder, b.responder, sizeof rc.responder);
    rc.seq = b.seq;
    if (out) *out = rc;
    ri = find_resp(s, b.responder);
    if (ri < 0) {
        zind_set_error("zvt_station_receive: unknown responder %s", b.responder);
        return ZVT_E_RESPONDER;
    }
    r = s->resp[ri];
    st = zind_seen_has(s->bundles, b.hash, &present);
    if (st != ZXV_OK) return st;
    if (present) {
        s->st_replays++;
        zind_set_error("zvt_station_receive: bundle %llu of %s is already held",
                       (unsigned long long) b.seq, b.responder);
        return ZIND_E_REPLAY;
    }
    st = zind_verify(s->cap, r->pk, b.hash, 32, ZVT_BUNDLE_CTX, b.sig);
    if (st != ZXV_OK) {
        if (st == ZXV_E_VERIFY) {
            s->st_badsig++;
            quarantine(s, ZXV_LP_FALSE, ZXV_HEX_POL_FORBIDDEN, ZIND_REASON_TAMPER, (uint32_t) ri,
                       "vinea-triage/bundle", b.hash, 32);
            zind_set_error("zvt_station_receive: bundle %llu is not signed by %s's key",
                           (unsigned long long) b.seq, b.responder);
            return ZIND_E_CHAIN;
        }
        return st;
    }
    if (r->forked) {
        zind_set_error("zvt_station_receive: responder %s is held after a fork", r->id);
        return ZIND_E_HELD;
    }
    if (b.seq < r->next_seq) return fork_resp(s, (uint32_t) ri, b.seq, r->hashes[b.seq], b.hash);
    if (b.seq > r->next_seq) {
        for (uint32_t k = 0; k < r->n_pend; k++)
            if (r->pend[k].seq == b.seq) {
                if (memcmp(r->pend[k].hash, b.hash, 32) == 0) {
                    s->st_replays++;
                    return ZIND_E_REPLAY;
                }
                return fork_resp(s, (uint32_t) ri, b.seq, r->pend[k].hash, b.hash);
            }
        if (r->n_pend >= ZVT_PENDING_MAX) {
            zind_set_error("zvt_station_receive: %u bundles of %s already wait behind a gap",
                           ZVT_PENDING_MAX, r->id);
            return ZXV_E_CAPACITY;
        }
        r->pend[r->n_pend].bytes = (uint8_t *) malloc(len);
        if (!r->pend[r->n_pend].bytes) return ZXV_E_NOMEM;
        memcpy(r->pend[r->n_pend].bytes, buf, len);
        r->pend[r->n_pend].len = len;
        r->pend[r->n_pend].seq = b.seq;
        memcpy(r->pend[r->n_pend].hash, b.hash, 32);
        r->n_pend++;
        zind_set_error("zvt_station_receive: bundle %llu of %s waits for %llu",
                       (unsigned long long) b.seq, r->id, (unsigned long long) r->next_seq);
        return ZVT_E_GAP;
    }
    if (memcmp(b.prev, r->head, 32) != 0)
        return fork_resp(s, (uint32_t) ri, b.seq, r->head, b.prev);
    st = apply_bundle(s, (uint32_t) ri, &b, &rc);
    if (st != ZXV_OK) {
        if (out) *out = rc;
        return st;
    }
    /* drain what waited behind the gap */
    for (;;) {
        uint32_t k;
        bundle_t pb;
        for (k = 0; k < r->n_pend; k++)
            if (r->pend[k].seq == r->next_seq) break;
        if (k == r->n_pend) break;
        st = parse_bundle(r->pend[k].bytes, r->pend[k].len, &pb);
        if (st == ZXV_OK && memcmp(pb.prev, r->head, 32) != 0)
            st = fork_resp(s, (uint32_t) ri, pb.seq, r->head, pb.prev);
        if (st == ZXV_OK) st = apply_bundle(s, (uint32_t) ri, &pb, &rc);
        free(r->pend[k].bytes);
        r->pend[k] = r->pend[--r->n_pend];
        if (st != ZXV_OK) break;
    }
    if (out) *out = rc;
    return st == ZXV_OK ? ZXV_OK : st;
}

int32_t zvt_station_ack(const zvt_station *s, const char *responder, uint8_t *buf, size_t cap,
                        size_t *out_len) {
    const resp_t *r;
    uint8_t h[32];
    size_t need;
    zind_wr w;
    int ri;
    int32_t st;
    if (!s || !out_len) return ZXV_E_ARG;
    ri = find_resp(s, responder);
    if (ri < 0) return ZVT_E_RESPONDER;
    r = s->resp[ri];
    need = 4 + 1 + 1 + strlen(r->id) + 8 + 32;
    *out_len = need + ZIND_SIG_BYTES;
    if (!buf || cap < need + ZIND_SIG_BYTES) return ZXV_E_BUFFER;
    zind_wr_init(&w, buf, need);
    zind_wr_bytes(&w, "ZVTA", 4);
    zind_wr_u8(&w, 1);
    zind_wr_str(&w, r->id);
    zind_wr_u64(&w, r->next_seq);
    zind_wr_bytes(&w, r->head, 32);
    if (w.overflow) return ZXV_E_INTERNAL;
    st = zind_subject("vinea-triage/ack", buf, need, h);
    if (st == ZXV_OK) st = zind_sign(s->cap, s->key, h, 32, ZVT_ACK_CTX, buf + need);
    return st;
}

int32_t zvt_responder_state(const zvt_station *s, const char *responder, zvt_responder_view *out) {
    int ri;
    if (!s || !out) return ZXV_E_ARG;
    ri = find_resp(s, responder);
    if (ri < 0) return ZVT_E_RESPONDER;
    out->next_seq = s->resp[ri]->next_seq;
    out->pending = s->resp[ri]->n_pend;
    out->lp = s->resp[ri]->forked ? ZXV_LP_BOTH : ZXV_LP_TRUE;
    return ZXV_OK;
}

int32_t zvt_patient_count(const zvt_station *s, uint32_t *out_count) {
    if (!s || !out_count) return ZXV_E_ARG;
    *out_count = s->n_pat;
    return ZXV_OK;
}

int32_t zvt_patient_id(const zvt_station *s, uint32_t i, char *buf, size_t cap) {
    size_t n;
    if (!s || !buf) return ZXV_E_ARG;
    if (i >= s->n_pat) return ZXV_E_NOT_FOUND;
    n = strlen(s->pat[i].id);
    if (cap < n + 1) return ZXV_E_BUFFER;
    memcpy(buf, s->pat[i].id, n + 1);
    return ZXV_OK;
}

int32_t zvt_patient_state(const zvt_station *s, const char *patient, zvt_patient_view *out) {
    const patient_t *p;
    int pi;
    uint8_t phase;
    int32_t st;
    if (!s || !out) return ZXV_E_ARG;
    memset(out, 0, sizeof *out);
    pi = find_patient(s, patient);
    if (pi < 0) return ZVT_E_PATIENT;
    p = &s->pat[pi];
    out->lp = lp_of(p);
    out->categories = cat_mask(p);
    out->heads = p->n_heads;
    out->records = p->records;
    out->resolved = p->n_heads == 1 && p->heads[0].is_decision;
    if (out->lp == ZXV_LP_TRUE) out->category = p->heads[0].category;
    phase = out->resolved ? ZXV_HEX_PHASE_MANIFEST
            : p->records <= 1 ? ZXV_HEX_PHASE_POTENTIAL
                              : ZXV_HEX_PHASE_EMERGING;
    st = zind_tag(out->lp, ZXV_HEX_POL_NEUTRAL, phase, &out->tag);
    return st;
}

int32_t zvt_patient_head(const zvt_station *s, const char *patient, uint32_t i, zvt_record *rec,
                         char *responder, size_t responder_cap, uint8_t out_hash[32],
                         uint8_t *out_category, int *out_is_decision) {
    const patient_t *p;
    const head_t *h;
    const char *who;
    int pi;
    if (!s) return ZXV_E_ARG;
    pi = find_patient(s, patient);
    if (pi < 0) return ZVT_E_PATIENT;
    p = &s->pat[pi];
    if (i >= p->n_heads) return ZXV_E_NOT_FOUND;
    h = &p->heads[i];
    who = h->is_decision ? s->rev[h->who].id : s->resp[h->who]->id;
    if (responder) {
        if (responder_cap < strlen(who) + 1) return ZXV_E_BUFFER;
        memcpy(responder, who, strlen(who) + 1);
    }
    if (rec && !h->is_decision) *rec = s->recs[h->idx].rec;
    if (out_hash) memcpy(out_hash, h->hash, 32);
    if (out_category) *out_category = h->category;
    if (out_is_decision) *out_is_decision = h->is_decision;
    return ZXV_OK;
}

/* "zvt/resolve/v1" str patient u32 n hashes[n] u8 category str reviewer */
static int32_t resolve_msg(const patient_t *p, uint8_t category, const char *reviewer,
                           uint8_t *buf, size_t cap, size_t *out_len) {
    zind_wr w;
    size_t need = 14 + 1 + strlen(p->id) + 4 + 32 * (size_t) p->n_heads + 1 + 1 + strlen(reviewer);
    *out_len = need;
    if (!buf || cap < need) return ZXV_E_BUFFER;
    zind_wr_init(&w, buf, cap);
    zind_wr_bytes(&w, "zvt/resolve/v1", 14);
    zind_wr_str(&w, p->id);
    zind_wr_u32(&w, p->n_heads);
    for (uint32_t i = 0; i < p->n_heads; i++) zind_wr_bytes(&w, p->heads[i].hash, 32);
    zind_wr_u8(&w, category);
    zind_wr_str(&w, reviewer);
    return w.overflow || w.len != need ? ZXV_E_INTERNAL : ZXV_OK;
}

static int32_t resolve_args(const zvt_station *s, const char *patient, uint8_t category,
                            const char *reviewer, int *pi, int *vi) {
    *pi = find_patient(s, patient);
    if (*pi < 0) return ZVT_E_PATIENT;
    if (category < ZVT_CAT_MINOR || category > ZVT_CAT_DEAD) return ZXV_E_ARG;
    *vi = find_rev(s, reviewer);
    if (*vi < 0) {
        zind_set_error("zvt_resolve: %s is not a registered reviewer", reviewer ? reviewer : "-");
        return ZVT_E_REVIEWER;
    }
    if (lp_of(&s->pat[*pi]) != ZXV_LP_BOTH) {
        zind_set_error("zvt_resolve: patient %s is not held", patient);
        return ZXV_E_STATE;
    }
    return ZXV_OK;
}

int32_t zvt_resolve_message(const zvt_station *s, const char *patient, uint8_t category,
                            const char *reviewer, uint8_t *buf, size_t cap, size_t *out_len) {
    int pi, vi;
    int32_t st;
    if (!s || !out_len) return ZXV_E_ARG;
    st = resolve_args(s, patient, category, reviewer, &pi, &vi);
    if (st != ZXV_OK) return st;
    return resolve_msg(&s->pat[pi], category, reviewer, buf, cap, out_len);
}

int32_t zvt_resolve(zvt_station *s, const char *patient, uint8_t category, const char *reviewer,
                    const uint8_t sig[ZIND_SIG_BYTES]) {
    patient_t *p;
    uint8_t *msg = NULL, *rec = NULL;
    size_t len = 0;
    int pi, vi;
    head_t h;
    int32_t st;
    if (!s || !sig) return ZXV_E_ARG;
    st = resolve_args(s, patient, category, reviewer, &pi, &vi);
    if (st != ZXV_OK) return st;
    st = zxv_cap_check(s->cap, ZXV_CAP_ATTEST);
    if (st != ZXV_OK) {
        zind_set_error("zvt_resolve: the station cap needs ATTEST to apply a decision");
        return st;
    }
    p = &s->pat[pi];
    (void) resolve_msg(p, category, reviewer, NULL, 0, &len);
    if (len + 32 + ZIND_SIG_BYTES > ZIND_PAYLOAD_MAX) {
        zind_set_error("zvt_resolve: too many heads to record in one decision");
        return ZXV_E_CAPACITY;
    }
    msg = (uint8_t *) malloc(len + 32 + ZIND_SIG_BYTES);
    if (!msg) return ZXV_E_NOMEM;
    st = resolve_msg(p, category, reviewer, msg, len, &len);
    if (st == ZXV_OK) st = zind_verify(s->cap, s->rev[vi].pk, msg, len, ZVT_RESOLVE_CTX, sig);
    if (st != ZXV_OK) {
        if (st == ZXV_E_VERIFY) {
            quarantine(s, ZXV_LP_FALSE, ZXV_HEX_POL_FORBIDDEN, ZIND_REASON_TAMPER, (uint32_t) vi,
                       "vinea-triage/forged", sig, ZIND_SIG_BYTES);
            zind_set_error("zvt_resolve: the decision is not signed by %s", reviewer);
        }
        free(msg);
        return st;
    }
    memset(&h, 0, sizeof h);
    h.is_decision = 1;
    h.category = category;
    h.idx = s->n_decisions;
    h.who = (uint32_t) vi;
    st = zind_subject("vinea-triage/decision", msg, len, h.hash);
    rec = msg;
    memcpy(rec + len, s->rev[vi].key_id, 32);
    memcpy(rec + len + 32, sig, ZIND_SIG_BYTES);
    if (st == ZXV_OK)
        st = zind_chain_append(s->cap, s->journal, s->key, ZVT_J_RESOLVE, rec,
                               len + 32 + ZIND_SIG_BYTES, NULL, NULL);
    free(msg);
    if (st != ZXV_OK) return st;
    for (uint32_t i = 0; i < p->n_heads; i++) {
        st = add_ref(p, p->heads[i].hash);
        if (st != ZXV_OK) return st;
    }
    p->n_heads = 0;
    st = add_head(p, &h);
    if (st != ZXV_OK) return st;
    s->n_decisions++;
    p->lp = lp_of(p);
    return ZXV_OK;
}

const zind_chain *zvt_station_journal(const zvt_station *s) { return s ? s->journal : NULL; }

int32_t zvt_station_stats(const zvt_station *s, uint64_t *bundles, uint64_t *replays,
                          uint64_t *dup_records, uint64_t *bad_sigs) {
    if (!s) return ZXV_E_ARG;
    if (bundles) *bundles = s->st_bundles;
    if (replays) *replays = s->st_replays;
    if (dup_records) *dup_records = s->st_dups;
    if (bad_sigs) *bad_sigs = s->st_badsig;
    return ZXV_OK;
}
