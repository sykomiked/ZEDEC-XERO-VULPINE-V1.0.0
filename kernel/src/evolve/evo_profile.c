/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* evo_profile.c — module manifests and device profiles: what to run, where,
 * from which fork, shareable as one CID. */
#include "evo_util.h"

#define MOD_MAGIC  EVO_MAGIC('E', 'V', 'M', '1')
#define PROF_MAGIC EVO_MAGIC('E', 'V', 'P', '1')

uint32_t evo_module_encode(const evo_module_t *m, uint8_t *out, uint32_t cap)
{
    if (!m || !out || !m->name_len || m->name_len > EVO_NAME_MAX || m->n_prov > EVO_MOD_MAX_CAPS ||
        m->n_req > EVO_MOD_MAX_CAPS)
        return 0;
    evo_w_t w = {out, cap, 0, false};
    evo_w_u32(&w, MOD_MAGIC);
    evo_w_bytes(&w, m->code.b, EVO_CID_LEN);
    evo_w_u8(&w, m->name_len);
    evo_w_bytes(&w, m->name, m->name_len);
    evo_w_u8(&w, m->n_prov);
    for (uint32_t i = 0; i < m->n_prov; i++) evo_w_bytes(&w, m->prov[i].b, EVO_CID_LEN);
    evo_w_u8(&w, m->n_req);
    for (uint32_t i = 0; i < m->n_req; i++) evo_w_bytes(&w, m->req[i].b, EVO_CID_LEN);
    return w.err ? 0 : w.len;
}

evo_status_t evo_module_decode(const uint8_t *in, uint32_t len, evo_module_t *m)
{
    if (!in || !m) return EVO_ERR_ARG;
    evo_zero(m, sizeof(*m));
    evo_r_t r = {in, len, 0, false};
    if (evo_r_le(&r, 4) != MOD_MAGIC) return EVO_ERR_PARSE;
    evo_r_copy(&r, m->code.b, EVO_CID_LEN);
    m->name_len = (uint8_t) evo_r_le(&r, 1);
    if (r.err || !m->name_len || m->name_len > EVO_NAME_MAX) return EVO_ERR_PARSE;
    evo_r_copy(&r, m->name, m->name_len);
    m->n_prov = (uint8_t) evo_r_le(&r, 1);
    if (m->n_prov > EVO_MOD_MAX_CAPS) return EVO_ERR_PARSE;
    for (uint32_t i = 0; i < m->n_prov; i++) evo_r_copy(&r, m->prov[i].b, EVO_CID_LEN);
    m->n_req = (uint8_t) evo_r_le(&r, 1);
    if (m->n_req > EVO_MOD_MAX_CAPS) return EVO_ERR_PARSE;
    for (uint32_t i = 0; i < m->n_req; i++) evo_r_copy(&r, m->req[i].b, EVO_CID_LEN);
    return r.err || r.pos != len ? EVO_ERR_PARSE : EVO_OK;
}

evo_status_t evo_module_cid(const evo_module_t *m, evo_cid_t *out)
{
    uint8_t buf[EVO_MOD_ENC_MAX];
    uint32_t n = evo_module_encode(m, buf, sizeof(buf));
    if (!n) return EVO_ERR_ARG;
    evo_cid_of(buf, n, out);
    return EVO_OK;
}

evo_status_t evo_profile_init(evo_profile_t *p, const char *label)
{
    if (!p) return EVO_ERR_ARG;
    evo_zero(p, sizeof(*p));
    if (label) {
        uint32_t n = evo_strlen(label, EVO_LABEL_MAX);
        if (n > EVO_LABEL_MAX) return EVO_ERR_ARG;
        evo_cpy(p->label, label, n);
        p->label_len = (uint8_t) n;
    }
    return EVO_OK;
}

evo_status_t evo_profile_put(evo_profile_t *p, const evo_cid_t *module, const evo_cid_t *lineage,
                             evo_place_t place, const uint8_t peer[32])
{
    if (!p || !module || place > EVO_PLACE_PEER || (place == EVO_PLACE_PEER && !peer))
        return EVO_ERR_ARG;
    uint32_t i = 0;
    while (i < p->n && evo_cid_cmp(&p->e[i].module, module) < 0) i++;
    if (!(i < p->n && evo_cid_eq(&p->e[i].module, module))) {
        if (p->n >= EVO_PROFILE_MAX) return EVO_ERR_FULL;
        for (uint32_t j = p->n; j > i; j--) evo_cpy(&p->e[j], &p->e[j - 1], sizeof(p->e[0]));
        p->n++;
    }
    evo_prof_entry_t *e = &p->e[i];
    evo_zero(e, sizeof(*e));
    evo_cpy(&e->module, module, sizeof(evo_cid_t));
    if (lineage) evo_cpy(&e->lineage, lineage, sizeof(evo_cid_t));
    e->place = (uint8_t) place;
    if (place == EVO_PLACE_PEER) evo_cpy(e->peer, peer, 32);
    return EVO_OK;
}

uint32_t evo_profile_encode(const evo_profile_t *p, uint8_t *out, uint32_t cap)
{
    if (!p || !out || p->n > EVO_PROFILE_MAX || p->label_len > EVO_LABEL_MAX) return 0;
    evo_w_t w = {out, cap, 0, false};
    evo_w_u32(&w, PROF_MAGIC);
    evo_w_u8(&w, p->label_len);
    evo_w_bytes(&w, p->label, p->label_len);
    evo_w_u8(&w, p->n);
    for (uint32_t i = 0; i < p->n; i++) {
        const evo_prof_entry_t *e = &p->e[i];
        if (e->place > EVO_PLACE_PEER || (i && evo_cid_cmp(&p->e[i - 1].module, &e->module) >= 0))
            return 0;
        evo_w_bytes(&w, e->module.b, EVO_CID_LEN);
        evo_w_bytes(&w, e->lineage.b, EVO_CID_LEN);
        evo_w_u8(&w, e->place);
        if (e->place == EVO_PLACE_PEER) evo_w_bytes(&w, e->peer, 32);
    }
    return w.err ? 0 : w.len;
}

evo_status_t evo_profile_decode(const uint8_t *in, uint32_t len, evo_profile_t *p)
{
    if (!in || !p) return EVO_ERR_ARG;
    evo_zero(p, sizeof(*p));
    evo_r_t r = {in, len, 0, false};
    if (evo_r_le(&r, 4) != PROF_MAGIC) return EVO_ERR_PARSE;
    p->label_len = (uint8_t) evo_r_le(&r, 1);
    if (p->label_len > EVO_LABEL_MAX) return EVO_ERR_PARSE;
    evo_r_copy(&r, p->label, p->label_len);
    uint32_t n = (uint32_t) evo_r_le(&r, 1);
    if (r.err || n > EVO_PROFILE_MAX) return EVO_ERR_PARSE;
    for (uint32_t i = 0; i < n; i++) {
        evo_prof_entry_t *e = &p->e[i];
        evo_r_copy(&r, e->module.b, EVO_CID_LEN);
        evo_r_copy(&r, e->lineage.b, EVO_CID_LEN);
        e->place = (uint8_t) evo_r_le(&r, 1);
        if (r.err || e->place > EVO_PLACE_PEER) return EVO_ERR_PARSE;
        if (e->place == EVO_PLACE_PEER) evo_r_copy(&r, e->peer, 32);
        if (i && evo_cid_cmp(&p->e[i - 1].module, &e->module) >= 0) return EVO_ERR_PARSE;
    }
    p->n = (uint8_t) n;
    return r.err || r.pos != len ? EVO_ERR_PARSE : EVO_OK;
}

evo_status_t evo_profile_cid(const evo_profile_t *p, evo_cid_t *out)
{
    uint8_t buf[EVO_PROFILE_ENC_MAX];
    uint32_t n = evo_profile_encode(p, buf, sizeof(buf));
    if (!n) return EVO_ERR_ARG;
    evo_cid_of(buf, n, out);
    return EVO_OK;
}

static const evo_module_t *find_module(const evo_module_t *mods, uint32_t n, const evo_cid_t *cid)
{
    evo_cid_t c;
    for (uint32_t i = 0; i < n; i++)
        if (evo_module_cid(&mods[i], &c) == EVO_OK && evo_cid_eq(&c, cid)) return &mods[i];
    return 0;
}

static bool state_holds(const evo_state_t *st, const evo_cid_t *v)
{
    for (uint32_t i = 0; i < st->n; i++)
        if (evo_cid_eq(&st->s[i].val, v)) return true;
    return false;
}

evo_status_t evo_profile_check(const evo_profile_t *p, const evo_module_t *mods, uint32_t n_mods,
                               const evo_registry_t *reg, const evo_dag_t *g, uint32_t *bad)
{
    static evo_state_t st; /* 4 KB: off the stack */
    if (!p || !mods || !reg) return EVO_ERR_ARG;
    for (uint32_t i = 0; i < p->n; i++) {
        const evo_prof_entry_t *e = &p->e[i];
        if (e->place == EVO_PLACE_OFF) continue;
        if (bad) *bad = i;
        const evo_module_t *m = find_module(mods, n_mods, &e->module);
        if (!m) return EVO_ERR_NOT_FOUND;
        if (g && !evo_cid_is_zero(&e->lineage)) {
            int32_t k = evo_dag_find(g, &e->lineage);
            if (k < 0) return EVO_ERR_NOT_FOUND;
            if (evo_dag_state(g, (uint32_t) k, &st) != EVO_OK || !state_holds(&st, &e->module))
                return EVO_ERR_UNTRUSTED; /* the named build does not contain it */
        }
        for (uint32_t q = 0; q < m->n_req; q++) {
            const evo_desc_t *want = evo_reg_find(reg, &m->req[q]);
            if (!want) return EVO_ERR_NOT_FOUND;
            bool sat = false;
            for (uint32_t j = 0; j < p->n && !sat; j++) {
                if (p->e[j].place == EVO_PLACE_OFF) continue;
                const evo_module_t *pm = find_module(mods, n_mods, &p->e[j].module);
                for (uint32_t r = 0; pm && r < pm->n_prov && !sat; r++) {
                    const evo_desc_t *have = evo_reg_find(reg, &pm->prov[r]);
                    sat = have && evo_desc_satisfies(have, want);
                }
            }
            if (!sat) return EVO_ERR_UNSATISFIED;
        }
    }
    return EVO_OK;
}
