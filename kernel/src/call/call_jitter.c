/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* call_jitter.c — adaptive jitter buffer (RFC 3550 jitter estimator). */
#include "call_jitter.h"
#include "call_common.h"

#define JB_MAX_D_MS 2000u /* |D| is clamped to 2 s so J*16 stays in 32 bits */

bool call_jb_init(call_jb_t *jb, call_jb_slot_t *slots, uint32_t nslots, uint8_t *mem,
                  uint32_t slot_cap, uint32_t clock_khz, uint32_t min_delay_ms,
                  uint32_t max_delay_ms)
{
    if (!jb || !slots || !mem || nslots == 0 || (nslots & (nslots - 1)) || nslots > 16384)
        return false;
    if (clock_khz == 0 || clock_khz > 1000 || min_delay_ms > max_delay_ms) return false;
    jb->slots = slots;
    jb->mem = mem;
    jb->nslots = nslots;
    jb->slot_cap = slot_cap;
    jb->clock_khz = clock_khz;
    jb->min_delay_ms = min_delay_ms;
    jb->max_delay_ms = max_delay_ms;
    call_jb_reset(jb);
    return true;
}

void call_jb_reset(call_jb_t *jb)
{
    for (uint32_t i = 0; i < jb->nslots; i++) call_fill(&jb->slots[i], 0, sizeof(jb->slots[i]));
    jb->delay_ms = jb->min_delay_ms;
    jb->jitter_q4 = 0;
    jb->ts0 = 0;
    jb->base_ms = 0;
    jb->last_transit = 0;
    jb->next_seq = 0;
    jb->started = 0;
    jb->have_transit = 0;
    jb->depth = 0;
    call_fill(&jb->st, 0, sizeof(jb->st));
}

uint32_t call_jb_jitter_us(const call_jb_t *jb)
{
    /* J*16 * 1000 / (16 * clock_khz) */
    return (jb->jitter_q4 * 125u) / (2u * jb->clock_khz);
}

static int32_t rel_ms(const call_jb_t *jb, uint32_t ts)
{
    return (int32_t) (ts - jb->ts0) / (int32_t) jb->clock_khz;
}

static uint32_t due_ms(const call_jb_t *jb, uint32_t ts)
{
    return jb->base_ms + (uint32_t) rel_ms(jb, ts) + jb->delay_ms;
}

static void update_delay(call_jb_t *jb)
{
    uint32_t target = jb->min_delay_ms + (3u * call_jb_jitter_us(jb) + 999u) / 1000u;
    if (target > jb->max_delay_ms) target = jb->max_delay_ms;
    if (target > jb->delay_ms)
        jb->delay_ms = target;
    else if (target < jb->delay_ms)
        jb->delay_ms -= (jb->delay_ms - target + 15u) / 16u;
}

int call_jb_push(call_jb_t *jb, uint16_t seq, uint32_t ts, bool marker, const uint8_t *data,
                 uint32_t len, uint32_t now_ms)
{
    if (!jb || (!data && len)) return 0;
    jb->st.pushed++;
    if (len > jb->slot_cap || len > 0xffffu) {
        jb->st.overflow++;
        return 0;
    }
    if (!jb->started) {
        jb->started = 1;
        jb->next_seq = seq;
        jb->ts0 = ts;
        jb->base_ms = now_ms;
    }
    int32_t d = call_seq_diff(jb->next_seq, seq);
    if (d < 0) {
        jb->st.late++;
        return 0;
    }
    /* too far ahead: slide the window forward, dropping what falls out */
    while ((uint32_t) call_seq_diff(jb->next_seq, seq) >= jb->nslots) {
        call_jb_slot_t *o = &jb->slots[jb->next_seq & (jb->nslots - 1)];
        if (o->used && o->seq == jb->next_seq) {
            o->used = 0;
            jb->depth--;
            jb->st.overflow++;
        }
        jb->next_seq++;
    }
    call_jb_slot_t *s = &jb->slots[seq & (jb->nslots - 1)];
    if (s->used && s->seq == seq) {
        jb->st.duplicate++;
        return 0;
    }
    s->used = 1;
    s->seq = seq;
    s->ts = ts;
    s->arrival = now_ms;
    s->len = (uint16_t) len;
    s->marker = marker ? 1 : 0;
    call_copy(jb->mem + (uint32_t) (seq & (jb->nslots - 1)) * jb->slot_cap, data, len);
    jb->depth++;
    if (jb->depth > jb->st.max_depth) jb->st.max_depth = jb->depth;

    /* RFC 3550 interarrival jitter, in media clock units, J kept as J*16 */
    int32_t transit = (int32_t) (now_ms * jb->clock_khz - ts);
    if (jb->have_transit) {
        uint32_t ad = call_abs32(transit - jb->last_transit);
        uint32_t cap = JB_MAX_D_MS * jb->clock_khz;
        if (ad > cap) ad = cap;
        jb->jitter_q4 = jb->jitter_q4 - (jb->jitter_q4 >> 4) + ad;
    }
    jb->last_transit = transit;
    jb->have_transit = 1;

    /* the fastest transit seen anchors media time to local time */
    uint32_t cand = now_ms - (uint32_t) rel_ms(jb, ts);
    if ((int32_t) (cand - jb->base_ms) < 0) jb->base_ms = cand;
    update_delay(jb);
    return 1;
}

call_jb_result_t call_jb_pop(call_jb_t *jb, uint32_t now_ms, call_jb_out_t *out)
{
    if (!jb || !jb->started || jb->depth == 0) return CALL_JB_EMPTY;
    uint32_t mask = jb->nslots - 1;
    call_jb_slot_t *s = &jb->slots[jb->next_seq & mask];
    if (s->used && s->seq == jb->next_seq) {
        if (!call_time_ge(now_ms, due_ms(jb, s->ts))) return CALL_JB_EMPTY;
        if (out) {
            out->data = jb->mem + (uint32_t) (jb->next_seq & mask) * jb->slot_cap;
            out->len = s->len;
            out->ts = s->ts;
            out->seq = s->seq;
            out->marker = s->marker;
        }
        s->used = 0;
        jb->depth--;
        jb->next_seq++;
        jb->st.played++;
        return CALL_JB_PACKET;
    }
    /* missing: conceal it once the next buffered packet is due */
    for (uint32_t k = 1; k < jb->nslots; k++) {
        uint16_t q = (uint16_t) (jb->next_seq + k);
        call_jb_slot_t *c = &jb->slots[q & mask];
        if (!c->used || c->seq != q) continue;
        if (!call_time_ge(now_ms, due_ms(jb, c->ts))) return CALL_JB_EMPTY;
        if (out) {
            out->data = NULL;
            out->len = 0;
            out->ts = 0;
            out->seq = jb->next_seq;
            out->marker = 0;
        }
        jb->next_seq++;
        jb->st.concealed++;
        return CALL_JB_CONCEAL;
    }
    return CALL_JB_EMPTY;
}
