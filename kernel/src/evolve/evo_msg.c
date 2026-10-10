/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* evo_msg.c — tag-length-value messages that preserve what they do not
 * understand (C3) and refuse what they must understand but do not (C4). */
#include "evo_util.h"

#define MU_BIT 0x8000u

static int32_t field_index(const evo_desc_t *d, uint16_t tag)
{
    for (uint32_t i = 0; i < d->n_fields; i++)
        if (d->f[i].tag == tag) return (int32_t) i;
    return -1;
}

void evo_msg_init(evo_msg_t *m, const evo_desc_t *d)
{
    evo_zero(m, sizeof(*m));
    m->d = d;
}

static evo_status_t put_data(evo_msg_t *m, int32_t k, const uint8_t *p, uint32_t len)
{
    if (len > (uint32_t) EVO_MSG_MAX - m->data_len) return EVO_ERR_FULL;
    m->v[k].off = m->data_len;
    m->v[k].len = (uint16_t) len;
    if (len) evo_cpy(m->data + m->data_len, p, len);
    m->data_len = (uint16_t) (m->data_len + len);
    return EVO_OK;
}

evo_status_t evo_msg_set_uint(evo_msg_t *m, uint16_t tag, uint64_t v)
{
    if (!m || !m->d) return EVO_ERR_ARG;
    int32_t k = field_index(m->d, tag);
    if (k < 0 || m->d->f[k].type != EVO_T_UINT) return EVO_ERR_ARG;
    m->v[k].state = EVO_V_PRESENT;
    m->v[k].mu = (m->d->f[k].flags & EVO_FF_MUST_UNDERSTAND) ? 1 : 0;
    m->v[k].u = v;
    m->v[k].len = 0;
    return EVO_OK;
}

evo_status_t evo_msg_set_bytes(evo_msg_t *m, uint16_t tag, const uint8_t *p, uint32_t len)
{
    if (!m || !m->d || (len && !p)) return EVO_ERR_ARG;
    int32_t k = field_index(m->d, tag);
    if (k < 0 || m->d->f[k].type == EVO_T_UINT) return EVO_ERR_ARG;
    if (len > m->d->f[k].max_len) return EVO_ERR_ARG;
    if (m->d->f[k].type == EVO_T_CID && len != EVO_CID_LEN) return EVO_ERR_ARG;
    evo_status_t s = put_data(m, k, p, len); /* data[] is append-only */
    if (s != EVO_OK) return s;
    m->v[k].state = EVO_V_PRESENT;
    m->v[k].mu = (m->d->f[k].flags & EVO_FF_MUST_UNDERSTAND) ? 1 : 0;
    return EVO_OK;
}

evo_status_t evo_msg_clear(evo_msg_t *m, uint16_t tag)
{
    if (!m || !m->d) return EVO_ERR_ARG;
    int32_t k = field_index(m->d, tag);
    if (k < 0) return EVO_ERR_NOT_FOUND;
    evo_zero(&m->v[k], sizeof(evo_val_t));
    return EVO_OK;
}

bool evo_msg_get_uint(const evo_msg_t *m, uint16_t tag, uint64_t *v)
{
    int32_t k = field_index(m->d, tag);
    if (k < 0 || m->d->f[k].type != EVO_T_UINT || m->v[k].state == EVO_V_ABSENT) return false;
    *v = m->v[k].u;
    return true;
}

bool evo_msg_get_bytes(const evo_msg_t *m, uint16_t tag, const uint8_t **p, uint32_t *len)
{
    int32_t k = field_index(m->d, tag);
    if (k < 0 || m->d->f[k].type == EVO_T_UINT || m->v[k].state == EVO_V_ABSENT) return false;
    *p = m->data + m->v[k].off;
    *len = m->v[k].len;
    return true;
}

uint8_t evo_msg_state(const evo_msg_t *m, uint16_t tag)
{
    int32_t k = field_index(m->d, tag);
    return k < 0 ? EVO_V_ABSENT : m->v[k].state;
}

static uint32_t uint_len(uint64_t v)
{
    uint32_t n = 0;
    while (v) {
        n++;
        v >>= 8;
    }
    return n;
}

/* Next preserved unknown TLV at position *pos: its tag and total length. */
static bool unk_next(const evo_msg_t *m, uint32_t pos, uint16_t *tag, uint32_t *tot)
{
    if (pos + 5 > m->unk_len) return false;
    *tag = (uint16_t) ((m->unk[pos] | (m->unk[pos + 1] << 8)) & ~MU_BIT);
    *tot = 5u + (uint32_t) (m->unk[pos + 3] | (m->unk[pos + 4] << 8));
    return true;
}

evo_status_t evo_msg_encode(const evo_msg_t *m, const evo_desc_t *core, uint8_t *out, uint32_t cap,
                            uint32_t *out_len)
{
    if (!m || !m->d || !out || !out_len) return EVO_ERR_ARG;
    const evo_desc_t *d = m->d;
    if (core) {
        for (uint32_t i = 0; i < core->n_fields; i++) {
            const evo_field_t *cf = &core->f[i];
            int32_t k = field_index(d, cf->tag);
            bool set = k >= 0 && m->v[k].state == EVO_V_PRESENT;
            if ((cf->flags & EVO_FF_REQUIRED) && !set) return EVO_ERR_MISSING_REQUIRED;
            if (set && d->f[k].type != EVO_T_UINT && m->v[k].len > cf->max_len) return EVO_ERR_ARG;
        }
        for (uint32_t k = 0; k < d->n_fields; k++)
            if (m->v[k].state == EVO_V_PRESENT && m->v[k].mu && !evo_desc_field(core, d->f[k].tag))
                return EVO_ERR_MUST_UNDERSTAND;
    }
    evo_w_t w = {out, cap, 0, false};
    uint32_t k = 0, upos = 0;
    int32_t last = -1;
    for (;;) {
        while (k < d->n_fields && m->v[k].state != EVO_V_PRESENT) k++;
        uint16_t ut = 0;
        uint32_t utot = 0;
        bool have_u = unk_next(m, upos, &ut, &utot);
        bool have_k = k < d->n_fields;
        if (!have_u && !have_k) break;
        if (have_k && (!have_u || d->f[k].tag < ut)) {
            const evo_field_t *f = &d->f[k];
            const evo_val_t *v = &m->v[k];
            if ((int32_t) f->tag <= last) return EVO_ERR_CONFLICT;
            evo_w_u16(&w, (uint16_t) (f->tag | (v->mu ? MU_BIT : 0)));
            evo_w_u8(&w, f->type);
            if (f->type == EVO_T_UINT) {
                uint32_t n = uint_len(v->u);
                evo_w_u16(&w, (uint16_t) n);
                for (uint32_t b = 0; b < n; b++) evo_w_u8(&w, (uint8_t) (v->u >> (8 * b)));
            } else {
                evo_w_u16(&w, v->len);
                evo_w_bytes(&w, m->data + v->off, v->len);
            }
            last = f->tag;
            k++;
        } else {
            if ((int32_t) ut <= last) return EVO_ERR_CONFLICT; /* set over a preserved tag */
            evo_w_bytes(&w, m->unk + upos, utot);
            last = ut;
            upos += utot;
        }
    }
    if (w.err) return EVO_ERR_FULL;
    *out_len = w.len;
    return EVO_OK;
}

evo_status_t evo_msg_decode(const evo_desc_t *d, const uint8_t *in, uint32_t len, evo_msg_t *m)
{
    if (!d || !in || !m) return EVO_ERR_ARG;
    evo_msg_init(m, d);
    if (len > EVO_MSG_MAX) return EVO_ERR_FULL;
    evo_r_t r = {in, len, 0, false};
    int32_t last = -1;
    while (r.pos < len) {
        uint32_t start = r.pos;
        uint16_t raw = (uint16_t) evo_r_le(&r, 2);
        uint8_t type = (uint8_t) evo_r_le(&r, 1);
        uint32_t vlen = (uint32_t) evo_r_le(&r, 2);
        const uint8_t *val = evo_r_bytes(&r, vlen);
        if (r.err) return EVO_ERR_PARSE;
        uint16_t tag = (uint16_t) (raw & ~MU_BIT);
        bool mu = (raw & MU_BIT) != 0;
        if (tag == 0 || (int32_t) tag <= last) return EVO_ERR_PARSE;
        if (type < EVO_T_UINT || type > EVO_T_CID) return EVO_ERR_PARSE;
        if (type == EVO_T_UINT && (vlen > 8 || (vlen && val[vlen - 1] == 0))) return EVO_ERR_PARSE;
        if (type == EVO_T_CID && vlen != EVO_CID_LEN) return EVO_ERR_PARSE;
        last = tag;
        int32_t k = field_index(d, tag);
        bool fits =
            k >= 0 && d->f[k].type == type && (type == EVO_T_UINT || vlen <= d->f[k].max_len);
        if (k >= 0 && !fits && (d->f[k].flags & EVO_FF_REQUIRED)) return EVO_ERR_PARSE;
        if (fits) {
            evo_val_t *v = &m->v[k];
            v->state = EVO_V_PRESENT;
            v->mu = mu ? 1 : 0;
            if (type == EVO_T_UINT) {
                for (uint32_t b = 0; b < vlen; b++) v->u |= (uint64_t) val[b] << (8 * b);
            } else if (put_data(m, k, val, vlen) != EVO_OK) {
                return EVO_ERR_FULL;
            }
            continue;
        }
        /* not ours to interpret */
        if (mu) return EVO_ERR_MUST_UNDERSTAND;
        uint32_t tot = r.pos - start;
        if (tot > (uint32_t) EVO_UNKNOWN_MAX - m->unk_len) return EVO_ERR_FULL;
        evo_cpy(m->unk + m->unk_len, in + start, tot);
        m->unk_len = (uint16_t) (m->unk_len + tot);
        m->n_unknown++;
    }
    for (uint32_t k = 0; k < d->n_fields; k++) {
        if (m->v[k].state != EVO_V_ABSENT) continue;
        if (d->f[k].flags & EVO_FF_REQUIRED) return EVO_ERR_MISSING_REQUIRED;
        m->v[k].state = EVO_V_DEFAULTED;
        m->v[k].u = d->f[k].def;
        m->v[k].off = m->data_len;
        m->v[k].len = 0;
    }
    return EVO_OK;
}
