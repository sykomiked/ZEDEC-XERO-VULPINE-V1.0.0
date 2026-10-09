/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* call_fec.c — XOR parity FEC across groups of media packets. */
#include "call_fec.h"
#include "call_common.h"

bool call_fec_enc_init(call_fec_enc_t *e, uint32_t protected_ssrc, uint32_t fec_ssrc, uint8_t k,
                       uint8_t *parity, uint32_t parity_cap, uint16_t first_seq)
{
    if (!e || !parity || k < 2 || k > CALL_FEC_MAX_K) return false;
    if (parity_cap <= CALL_FEC_HDR_LEN + CALL_RTP_HDR_LEN || parity_cap > 0xffffu - 64u)
        return false;
    call_fill(e, 0, sizeof(*e));
    e->protected_ssrc = protected_ssrc;
    e->k = k;
    e->parity = parity;
    e->parity_cap = parity_cap;
    call_packetizer_init(&e->pk, fec_ssrc, CALL_PT_FEC, 0,
                         (uint16_t) (parity_cap + CALL_RTP_HDR_LEN), first_seq);
    return true;
}

static int emit_parity(call_fec_enc_t *e, uint8_t *scratch, uint32_t scap, call_emit_fn emit,
                       void *ctx)
{
    if (e->n == 0) return 0;
    uint8_t *p = e->parity;
    call_put32(p, e->protected_ssrc);
    call_put16(p + 4, e->base_seq);
    p[6] = e->n;
    p[7] = CALL_FEC_VERSION;
    call_put16(p + 8, e->len_xor);
    uint32_t plen = CALL_FEC_HDR_LEN + e->maxlen;
    e->n = 0;
    int r = call_packetize(&e->pk, p, plen, e->base_seq, false, scratch, scap, emit, ctx);
    if (r < 0) return r;
    e->emitted++;
    return 1;
}

int call_fec_enc_flush(call_fec_enc_t *e, uint8_t *scratch, uint32_t scratch_cap, call_emit_fn emit,
                       void *ctx)
{
    if (!e || !scratch || !emit) return CALL_ERR_ARG;
    return emit_parity(e, scratch, scratch_cap, emit, ctx);
}

int call_fec_enc_add(call_fec_enc_t *e, const uint8_t *pkt, uint32_t len, uint8_t *scratch,
                     uint32_t scratch_cap, call_emit_fn emit, void *ctx)
{
    if (!e || !pkt || !scratch || !emit) return CALL_ERR_ARG;
    call_rtp_hdr_t h;
    int pl = call_rtp_parse(pkt, len, &h, NULL);
    if (pl < 0) return pl;
    if (h.ssrc != e->protected_ssrc) return CALL_ERR_ARG;
    if (CALL_FEC_HDR_LEN + len > e->parity_cap) return CALL_ERR_SPACE;
    int out = 0;
    if (e->n && (uint16_t) (e->base_seq + e->n) != h.seq) {
        int r = emit_parity(e, scratch, scratch_cap, emit, ctx); /* gap: close early */
        if (r < 0) return r;
        out += r;
    }
    if (e->n == 0) {
        call_fill(e->parity, 0, e->parity_cap);
        e->base_seq = h.seq;
        e->len_xor = 0;
        e->maxlen = 0;
    }
    uint8_t *x = e->parity + CALL_FEC_HDR_LEN;
    for (uint32_t i = 0; i < len; i++) x[i] ^= pkt[i];
    e->len_xor ^= (uint16_t) len;
    if (len > e->maxlen) e->maxlen = (uint16_t) len;
    e->n++;
    if (e->n == e->k) {
        int r = emit_parity(e, scratch, scratch_cap, emit, ctx);
        if (r < 0) return r;
        out += r;
    }
    return out;
}

/* ---- decoder ---- */

int call_fec_hdr_parse(const uint8_t *payload, uint32_t len, call_fec_hdr_t *h)
{
    if (!payload || !h) return CALL_ERR_ARG;
    if (len < CALL_FEC_HDR_LEN + 1) return CALL_ERR_SHORT;
    h->protected_ssrc = call_get32(payload);
    h->base_seq = call_get16(payload + 4);
    h->n = payload[6];
    if (payload[7] != CALL_FEC_VERSION) return CALL_ERR_FORMAT;
    if (h->n == 0 || h->n > CALL_FEC_MAX_K) return CALL_ERR_FORMAT;
    h->len_xor = call_get16(payload + 8);
    h->plen = len - CALL_FEC_HDR_LEN;
    if (h->plen < CALL_RTP_HDR_LEN + 1) return CALL_ERR_FORMAT;
    return CALL_FEC_HDR_LEN;
}

bool call_fec_dec_init(call_fec_dec_t *d, uint32_t protected_ssrc, call_fec_mslot_t *m,
                       uint8_t *mmem, uint32_t nmedia, uint32_t mcap, call_fec_fslot_t *f,
                       uint8_t *fmem, uint32_t nfec, uint32_t fcap, uint8_t *work)
{
    if (!d || !m || !mmem || !f || !fmem || !work) return false;
    if (nmedia < CALL_FEC_MAX_K || (nmedia & (nmedia - 1)) || nmedia > 32768 || nfec == 0)
        return false;
    if (mcap == 0 || mcap > 0xffffu || fcap < mcap) return false;
    call_fill(d, 0, sizeof(*d));
    d->protected_ssrc = protected_ssrc;
    d->m = m;
    d->mmem = mmem;
    d->nmedia = nmedia;
    d->mcap = mcap;
    d->f = f;
    d->fmem = fmem;
    d->nfec = nfec;
    d->fcap = fcap;
    d->work = work;
    for (uint32_t i = 0; i < nmedia; i++) call_fill(&m[i], 0, sizeof(m[i]));
    for (uint32_t i = 0; i < nfec; i++) call_fill(&f[i], 0, sizeof(f[i]));
    return true;
}

static call_fec_mslot_t *mslot(call_fec_dec_t *d, uint16_t seq, uint8_t **buf)
{
    uint32_t i = seq & (d->nmedia - 1);
    if (buf) *buf = d->mmem + i * d->mcap;
    return &d->m[i];
}

static bool have(call_fec_dec_t *d, uint16_t seq)
{
    call_fec_mslot_t *s = mslot(d, seq, NULL);
    return s->used && s->seq == seq;
}

static void store(call_fec_dec_t *d, uint16_t seq, const uint8_t *pkt, uint32_t len)
{
    uint8_t *b;
    call_fec_mslot_t *s = mslot(d, seq, &b);
    s->used = 1;
    s->seq = seq;
    s->len = (uint16_t) len;
    if (b != pkt) call_copy(b, pkt, len);
}

/* Try to finish parity slot fi. Returns 1 if a packet was recovered. */
static int try_group(call_fec_dec_t *d, uint32_t fi, call_emit_fn emit, void *ctx)
{
    call_fec_fslot_t *f = &d->f[fi];
    if (!f->used) return 0;
    uint32_t missing = 0;
    uint16_t miss_seq = 0;
    for (uint32_t i = 0; i < f->n; i++) {
        uint16_t q = (uint16_t) (f->base_seq + i);
        if (!have(d, q)) {
            missing++;
            miss_seq = q;
        }
    }
    if (missing == 0) {
        f->used = 0;
        d->fec_useless++;
        return 0;
    }
    if (missing > 1) return 0;
    const uint8_t *par = d->fmem + fi * d->fcap;
    uint8_t *w = d->work;
    if (f->plen > d->mcap) {
        f->used = 0;
        d->bad_recovery++;
        return 0;
    }
    call_copy(w, par, f->plen);
    uint16_t lx = f->len_xor;
    for (uint32_t i = 0; i < f->n; i++) {
        uint16_t q = (uint16_t) (f->base_seq + i);
        if (q == miss_seq) continue;
        uint8_t *b;
        call_fec_mslot_t *s = mslot(d, q, &b);
        lx ^= s->len;
        uint32_t L = s->len < f->plen ? s->len : f->plen;
        for (uint32_t j = 0; j < L; j++) w[j] ^= b[j];
    }
    f->used = 0;
    call_rtp_hdr_t h;
    if (lx == 0 || lx > f->plen || call_rtp_parse(w, lx, &h, NULL) < 0 || h.seq != miss_seq ||
        h.ssrc != d->protected_ssrc) {
        d->bad_recovery++;
        return 0;
    }
    store(d, miss_seq, w, lx);
    d->recovered++;
    if (emit) {
        uint8_t *b;
        mslot(d, miss_seq, &b);
        int e = emit(ctx, b, lx);
        if (e < 0) return e;
    }
    return 1;
}

static int sweep(call_fec_dec_t *d, uint16_t seq, call_emit_fn emit, void *ctx)
{
    int total = 0;
    for (uint32_t i = 0; i < d->nfec; i++) {
        call_fec_fslot_t *f = &d->f[i];
        if (!f->used) continue;
        if ((uint32_t) (uint16_t) (seq - f->base_seq) >= f->n) continue;
        int r = try_group(d, i, emit, ctx);
        if (r < 0) return r;
        total += r;
    }
    return total;
}

int call_fec_dec_media(call_fec_dec_t *d, const uint8_t *pkt, uint32_t len, call_emit_fn emit,
                       void *ctx)
{
    if (!d || !pkt) return CALL_ERR_ARG;
    call_rtp_hdr_t h;
    int pl = call_rtp_parse(pkt, len, &h, NULL);
    if (pl < 0) return pl;
    if (h.ssrc != d->protected_ssrc) return CALL_ERR_ARG;
    if (len > d->mcap) return CALL_ERR_SPACE;
    if (have(d, h.seq)) return 0;
    store(d, h.seq, pkt, len);
    return sweep(d, h.seq, emit, ctx);
}

int call_fec_dec_parity(call_fec_dec_t *d, const uint8_t *pkt, uint32_t len, call_emit_fn emit,
                        void *ctx)
{
    if (!d || !pkt) return CALL_ERR_ARG;
    call_rtp_hdr_t h;
    const uint8_t *pay;
    int pl = call_rtp_parse(pkt, len, &h, &pay);
    if (pl < 0) return pl;
    if (h.pt != CALL_PT_FEC) return CALL_ERR_FORMAT;
    call_fec_hdr_t fh;
    int r = call_fec_hdr_parse(pay, (uint32_t) pl, &fh);
    if (r < 0) return r;
    if (fh.protected_ssrc != d->protected_ssrc) return CALL_ERR_ARG;
    if (fh.plen > d->fcap || fh.plen > d->mcap) return CALL_ERR_SPACE;
    d->fec_in++;
    d->tick++;
    /* free slot or oldest */
    uint32_t best = 0, best_age = 0;
    bool found = false;
    for (uint32_t i = 0; i < d->nfec; i++) {
        if (d->f[i].used && d->f[i].base_seq == fh.base_seq && d->f[i].n == fh.n) return 0;
    }
    for (uint32_t i = 0; i < d->nfec; i++) {
        if (!d->f[i].used) {
            best = i;
            found = true;
            break;
        }
        uint32_t age = d->tick - d->f[i].age;
        if (i == 0 || age > best_age) {
            best = i;
            best_age = age;
        }
    }
    if (!found) d->fec_overrun++;
    call_fec_fslot_t *f = &d->f[best];
    f->used = 1;
    f->base_seq = fh.base_seq;
    f->n = fh.n;
    f->len_xor = fh.len_xor;
    f->plen = (uint16_t) fh.plen;
    f->age = d->tick;
    call_copy(d->fmem + best * d->fcap, pay + CALL_FEC_HDR_LEN, fh.plen);
    return try_group(d, best, emit, ctx);
}
