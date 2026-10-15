/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* allot_view.c — app #075 Crown's view of the allotment. See allot_view.h. */
#include "allot_view.h"
#include "pay_util.h"

allot_view_mode_t allot_view_mode(const allot_config_t *c)
{
    return c && c->jurisdiction_permits ? ALLOT_VIEW_ENTRY : ALLOT_VIEW_HISTORY;
}

allot_status_t allot_history_init(allot_history_t *h, allot_stewardship_t *rec, uint32_t cap)
{
    if (!h || !rec || cap == 0) return ALLOT_ERR_ARG;
    pay_memset(h, 0, sizeof *h);
    h->rec = rec;
    h->cap = cap;
    return ALLOT_OK;
}

allot_status_t allot_history_note(allot_history_t *h, const allot_settlement_t *s)
{
    if (!h || !h->rec || !s) return ALLOT_ERR_ARG;
    uint32_t slot;
    if (h->len < h->cap) {
        slot = h->head + h->len; /* head stays 0 until the ring fills */
        h->len++;
    } else {
        slot = h->head; /* overwrite the oldest */
        h->head = h->head + 1 == h->cap ? 0 : h->head + 1;
    }
    allot_stewardship_t *e = &h->rec[slot];
    e->epoch = s->epoch;
    pay_memcpy(e->steward, s->steward, ALLOT_ID_LEN);
    e->endowment = s->crown_endowment;
    pay_memcpy(e->link, s->link, ALLOT_HASH_LEN);
    return ALLOT_OK;
}

const allot_stewardship_t *allot_history_at(const allot_history_t *h, uint32_t i)
{
    if (!h || !h->rec || i >= h->len) return 0;
    uint32_t k = h->head + i;
    if (k >= h->cap) k -= h->cap;
    return &h->rec[k];
}

allot_status_t allot_view_build(const allot_config_t *c, const allot_t *r, const allot_history_t *h,
                                const uint8_t *who, allot_view_t *out)
{
    if (!c || !out) return ALLOT_ERR_ARG;
    pay_memset(out, 0, sizeof *out);
    out->mode = allot_view_mode(c);
    out->header = ALLOT_UI_HEADER;
    out->subtext = ALLOT_UI_SUBTEXT;
    out->history_len = h ? h->len : 0;
    if (out->mode == ALLOT_VIEW_HISTORY) { /* V2: never an entry prompt */
        out->note = ALLOT_UI_HISTORY_NOTE;
        return ALLOT_OK;
    }
    if (!r) return ALLOT_ERR_ARG;
    out->action = ALLOT_UI_ACTION;
    out->note = ALLOT_UI_NO_COST;
    out->epoch = r->epoch;
    out->ballots = r->ballots;
    out->threshold = r->threshold;
    out->endowment = r->pool;
    out->can_register = who && r->phase == ALLOT_OPEN && r->cfg.jurisdiction_permits &&
                        allot_room(r, who) > 0 && allot_mint_check(r, who, 0, 1) == ALLOT_OK;
    return ALLOT_OK;
}
