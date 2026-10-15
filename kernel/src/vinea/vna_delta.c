/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* vna_delta.c — the L2 state bus's payload: see vna_delta.h. */
#include "vna_delta.h"

int vna_delta_encode(const vna_delta_t *d, uint8_t *out, uint32_t cap)
{
    if (!d || !out || d->len > VNA_DELTA_PAYLOAD_MAX || d->lattice > LPRES_STATE_BOTH ||
        cap < VNA_DELTA_HDR + d->len)
        return VNA_ERR_ARG;
    uint32_t n = 0;
    out[n++] = VNA_DELTA_MAGIC0;
    out[n++] = VNA_DELTA_MAGIC1;
    out[n++] = VNA_DELTA_MAGIC2;
    vna_copy(out + n, d->parent, VNA_HASH_LEN);
    n += VNA_HASH_LEN;
    vna_put16(out + n, d->cell);
    n += 2;
    out[n++] = d->lattice;
    vna_put16(out + n, d->len);
    n += 2;
    if (d->len) {
        if (!d->payload) return VNA_ERR_ARG;
        vna_copy(out + n, d->payload, d->len);
        n += d->len;
    }
    return (int) n;
}

int vna_delta_parse(const uint8_t *buf, uint32_t len, vna_delta_t *d)
{
    if (!buf || !d || len < VNA_DELTA_HDR || buf[0] != VNA_DELTA_MAGIC0 ||
        buf[1] != VNA_DELTA_MAGIC1 || buf[2] != VNA_DELTA_MAGIC2)
        return VNA_ERR_PARSE;
    uint16_t plen = vna_get16(buf + 3 + 32 + 2 + 1);
    if (plen > VNA_DELTA_PAYLOAD_MAX || len != VNA_DELTA_HDR + plen) return VNA_ERR_PARSE;
    if (buf[3 + 32 + 2] > LPRES_STATE_BOTH) return VNA_ERR_PARSE;
    vna_copy(d->parent, buf + 3, VNA_HASH_LEN);
    d->cell = vna_get16(buf + 3 + 32);
    d->lattice = buf[3 + 32 + 2];
    d->len = plen;
    d->payload = buf + VNA_DELTA_HDR;
    return VNA_OK;
}

void vna_delta_digest(const uint8_t *buf, uint32_t len, uint8_t out[VNA_HASH_LEN])
{
    vna_htag("vinea/v2/delta", buf, len, out);
}

void vna_delta_page_init(vna_delta_page_t *p)
{
    if (p) vna_zero(p, sizeof *p);
}

const vna_delta_cell_t *vna_delta_cell(const vna_delta_page_t *p, uint16_t tag)
{
    if (!p) return 0;
    for (uint32_t i = 0; i < p->ncell; i++)
        if (p->cell[i].used && p->cell[i].tag == tag) return &p->cell[i];
    return 0;
}

static vna_delta_cell_t *cell_of(vna_delta_page_t *p, uint16_t tag)
{
    for (uint32_t i = 0; i < p->ncell; i++)
        if (p->cell[i].used && p->cell[i].tag == tag) return &p->cell[i];
    if (p->ncell == VNA_DELTA_CELLS) return 0;
    vna_delta_cell_t *c = &p->cell[p->ncell++];
    vna_zero(c, sizeof *c);
    c->tag = tag;
    c->used = 1;
    c->lattice = (uint8_t) LPRES_STATE_NEITHER;
    return c;
}

int vna_delta_apply(vna_delta_page_t *p, const uint8_t *buf, uint32_t len, uint64_t tick)
{
    if (!p || !buf) return VNA_ERR_ARG;
    vna_delta_t d;
    int st = vna_delta_parse(buf, len, &d);
    if (st != VNA_OK) return st;
    vna_delta_cell_t *c = cell_of(p, d.cell);
    if (!c) return VNA_ERR_SPACE;
    uint8_t dig[VNA_HASH_LEN];
    vna_delta_digest(buf, len, dig);
    if (c->napp && !vna_eq(c->head, d.parent, VNA_HASH_LEN)) c->forks++;
    c->lattice = (uint8_t) lpres_kjoin((lpres_state_t) c->lattice, (lpres_state_t) d.lattice);
    vna_copy(c->head, dig, VNA_HASH_LEN);
    c->napp++;
    if (tick > c->tick) c->tick = tick;
    if (tick > p->clock) p->clock = tick;
    return c->lattice;
}

int vna_delta_issue(vna_delta_page_t *p, uint16_t cell, lpres_state_t lattice,
                    const uint8_t *payload, uint16_t n, uint8_t *out, uint32_t cap,
                    uint64_t *tick)
{
    if (!p || !out) return VNA_ERR_ARG;
    const vna_delta_cell_t *prev = vna_delta_cell(p, cell);
    vna_delta_t d;
    vna_zero(&d, sizeof d);
    if (prev) vna_copy(d.parent, prev->head, VNA_HASH_LEN);
    d.cell = cell;
    d.lattice = (uint8_t) lattice;
    d.len = n;
    d.payload = payload;
    int len = vna_delta_encode(&d, out, cap);
    if (len < 0) return len;
    uint64_t t = p->clock + 1;
    int st = vna_delta_apply(p, out, (uint32_t) len, t);
    if (st < 0) return st;
    if (tick) *tick = t;
    return len;
}
