/* chronicle.c — the hash-chained, self-resolving ledger. See chronicle.h. */
#include "chronicle.h"
#include "../robin_debanks/sha256.h"

/* ---- tiny byte-buffer serialiser (deterministic within a platform) ---- */
typedef struct { uint8_t b[1024]; uint32_t n; } buf_t;
static void put_bytes(buf_t *o, const void *p, uint32_t len) {
    const uint8_t *s = (const uint8_t *)p;
    for (uint32_t i = 0; i < len && o->n < sizeof(o->b); i++) o->b[o->n++] = s[i];
}
static void put_u64(buf_t *o, uint64_t v) { put_bytes(o, &v, sizeof(v)); }
static void put_u32(buf_t *o, uint32_t v) { put_bytes(o, &v, sizeof(v)); }
static void put_u8(buf_t *o, uint8_t v)   { put_bytes(o, &v, 1); }

/* Digest of an event: a stable serialisation of every field that determines
 * the verdict. surplus_real_t is hashed in its native width (int64 on target,
 * double on host) — deterministic within a platform. */
static void event_digest(const wyrm_event_t *e, uint8_t out[CHRON_HASH_LEN]) {
    buf_t o; o.n = 0;
    put_u64(&o, e->ordinal);
    put_u64(&o, e->parent_ordinal);
    put_u8(&o, e->parent_committed ? 1u : 0u);
    uint32_t nd = (e->n_deltas <= WYRM_MAX_DELTAS) ? e->n_deltas : WYRM_MAX_DELTAS;
    put_u32(&o, nd);
    for (uint32_t i = 0; i < nd; i++) {
        put_u64(&o, e->delta[i].numerator);
        put_u64(&o, e->delta[i].denominator);
        put_u8(&o, e->delta[i].negative ? 1u : 0u);
    }
    put_u8(&o, (uint8_t)e->evidence);
    uint32_t ne = (e->n_ev <= CHG_MAX_EXPERTS) ? e->n_ev : CHG_MAX_EXPERTS;
    put_u32(&o, ne);
    for (uint32_t i = 0; i < ne; i++)
        for (uint32_t d = 0; d < CHG_DIM; d++)
            put_bytes(&o, &e->ev[i][d], sizeof(e->ev[i][d]));
    put_bytes(&o, &e->r_min, sizeof(e->r_min));
    put_u8(&o, e->route_available ? 1u : 0u);
    put_u8(&o, e->phases_healthy ? 1u : 0u);
    sha256(o.b, o.n, out);
}

static void copy_hash(uint8_t *d, const uint8_t *s) {
    for (uint32_t i = 0; i < CHRON_HASH_LEN; i++) d[i] = s[i];
}

/* entry_hash = SHA256(prev || seq || verdict || reason || event_digest) */
static void link_hash(const uint8_t prev[CHRON_HASH_LEN], uint64_t seq,
                      uint8_t verdict, uint8_t reason,
                      const uint8_t ev_digest[CHRON_HASH_LEN],
                      uint8_t out[CHRON_HASH_LEN]) {
    buf_t o; o.n = 0;
    put_bytes(&o, prev, CHRON_HASH_LEN);
    put_u64(&o, seq);
    put_u8(&o, verdict);
    put_u8(&o, reason);
    put_bytes(&o, ev_digest, CHRON_HASH_LEN);
    sha256(o.b, o.n, out);
}

void chronicle_init(chronicle_t *c) {
    if (!c) return;
    for (uint32_t i = 0; i < CHRON_HASH_LEN; i++) c->head[i] = 0;   /* genesis */
    c->n_entries = 0;
    c->next_seq = 1;
    c->n_pending = 0;
    for (uint32_t i = 0; i < CHRON_MAX_PENDING; i++) c->pending[i].occupied = false;
}

/* Append a resolved (S+ or S-) verdict to the chain. */
static uint64_t append_entry(chronicle_t *c, const wyrm_event_t *e,
                             const wyrm_result_t *r) {
    if (c->n_entries >= CHRON_MAX_ENTRIES) return 0;
    chronicle_entry_t *en = &c->entry[c->n_entries];
    en->seq = c->next_seq++;
    en->verdict = (uint8_t)r->verdict;
    en->reason = (uint8_t)r->reason;
    event_digest(e, en->event_digest);
    link_hash(c->head, en->seq, en->verdict, en->reason, en->event_digest, en->entry_hash);
    copy_hash(c->head, en->entry_hash);
    c->n_entries++;
    return en->seq;
}

static int32_t enqueue_pending(chronicle_t *c, const wyrm_event_t *e, uint8_t reason) {
    for (uint32_t i = 0; i < CHRON_MAX_PENDING; i++) {
        if (c->pending[i].occupied) continue;
        c->pending[i].event = *e;
        c->pending[i].reason = reason;
        c->pending[i].occupied = true;
        c->n_pending++;
        return (int32_t)i;
    }
    return -1;   /* pending set full */
}

chronicle_result_t chronicle_submit(chronicle_t *c, const wyrm_event_t *e) {
    chronicle_result_t out;
    out.verdict = WYRM_DEFER; out.reason = WYRM_R_PHASE_UNHEALTHY;
    out.seq = 0; out.pending_id = -1; out.chained = false;
    if (!c || !e) return out;

    wyrm_result_t r = wyrm_judge(e);
    out.verdict = r.verdict; out.reason = r.reason;
    if (r.verdict == WYRM_DEFER) {
        out.pending_id = enqueue_pending(c, e, (uint8_t)r.reason);
        return out;
    }
    /* S+ or S- both become permanent, tamper-evident history */
    out.seq = append_entry(c, e, &r);
    out.chained = true;
    return out;
}

wyrm_event_t *chronicle_pending_event(chronicle_t *c, uint32_t pending_id) {
    if (!c || pending_id >= CHRON_MAX_PENDING || !c->pending[pending_id].occupied)
        return 0;
    return &c->pending[pending_id].event;
}

uint32_t chronicle_poke(chronicle_t *c) {
    if (!c) return 0;
    uint32_t resolved = 0;
    for (uint32_t i = 0; i < CHRON_MAX_PENDING; i++) {
        if (!c->pending[i].occupied) continue;
        wyrm_result_t r = wyrm_judge(&c->pending[i].event);
        if (r.verdict == WYRM_DEFER) {
            c->pending[i].reason = (uint8_t)r.reason;   /* maybe a new reason */
            continue;
        }
        append_entry(c, &c->pending[i].event, &r);      /* now resolved */
        c->pending[i].occupied = false;
        c->n_pending--;
        resolved++;
    }
    return resolved;
}

const uint8_t *chronicle_head(const chronicle_t *c) { return c ? c->head : 0; }
uint32_t chronicle_length(const chronicle_t *c) { return c ? c->n_entries : 0; }
uint32_t chronicle_pending_count(const chronicle_t *c) { return c ? c->n_pending : 0; }

bool chronicle_verify(const chronicle_t *c) {
    if (!c) return false;
    uint8_t h[CHRON_HASH_LEN];
    for (uint32_t i = 0; i < CHRON_HASH_LEN; i++) h[i] = 0;   /* genesis */
    for (uint32_t i = 0; i < c->n_entries; i++) {
        const chronicle_entry_t *en = &c->entry[i];
        uint8_t expect[CHRON_HASH_LEN];
        link_hash(h, en->seq, en->verdict, en->reason, en->event_digest, expect);
        for (uint32_t k = 0; k < CHRON_HASH_LEN; k++)
            if (expect[k] != en->entry_hash[k]) return false;   /* tampered */
        copy_hash(h, en->entry_hash);
    }
    for (uint32_t k = 0; k < CHRON_HASH_LEN; k++)
        if (h[k] != c->head[k]) return false;                   /* head diverged */
    return true;
}
