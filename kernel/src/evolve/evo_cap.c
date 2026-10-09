/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* evo_cap.c — CIDs, capability descriptors, structural cores, the
 * descriptor registry, capability sets and session negotiation (C1, C2). */
#include "evo_util.h"
#include "keccak.h"

const char *evo_strerror(evo_status_t s)
{
    switch (s) {
    case EVO_OK:
        return "ok";
    case EVO_ERR_ARG:
        return "bad argument";
    case EVO_ERR_FULL:
        return "capacity exceeded";
    case EVO_ERR_PARSE:
        return "not a canonical encoding";
    case EVO_ERR_CID_MISMATCH:
        return "content does not match its CID";
    case EVO_ERR_MISSING_REQUIRED:
        return "required field missing";
    case EVO_ERR_MUST_UNDERSTAND:
        return "unknown must-understand field";
    case EVO_ERR_CONFLICT:
        return "conflict";
    case EVO_ERR_NOT_FOUND:
        return "not found";
    case EVO_ERR_DUP:
        return "already present";
    case EVO_ERR_UNKNOWN_PARENT:
        return "parent record unknown";
    case EVO_ERR_BAD_SIG:
        return "bad signature";
    case EVO_ERR_UNTRUSTED:
        return "not trusted";
    case EVO_ERR_UNSATISFIED:
        return "requirement unsatisfied";
    case EVO_ERR_NONCONFORMANT:
        return "safety core not conformant";
    }
    return "unknown";
}

/* ===== CIDs ===== */
void evo_cid_of(const uint8_t *data, uint32_t len, evo_cid_t *out)
{
    out->b[0] = 0x01; /* CIDv1 */
    out->b[1] = 0x55; /* raw */
    out->b[2] = 0x16; /* sha3-256 */
    out->b[3] = 0x20;
    sha3_256(data, len, out->b + 4);
}

bool evo_cid_eq(const evo_cid_t *a, const evo_cid_t *b)
{
    return evo_cmp(a->b, b->b, EVO_CID_LEN) == 0;
}

int evo_cid_cmp(const evo_cid_t *a, const evo_cid_t *b)
{
    return evo_cmp(a->b, b->b, EVO_CID_LEN);
}

bool evo_cid_is_zero(const evo_cid_t *a)
{
    for (uint32_t i = 0; i < EVO_CID_LEN; i++)
        if (a->b[i]) return false;
    return true;
}

void evo_key_id(const uint8_t *pk, uint32_t pk_len, uint8_t out[32])
{
    sha3_256(pk, pk_len, out);
}

/* ===== Descriptors ===== */
static uint16_t natural_max(uint8_t type)
{
    switch (type) {
    case EVO_T_UINT:
        return 8;
    case EVO_T_CID:
        return EVO_CID_LEN;
    case EVO_T_BYTES:
    case EVO_T_TEXT:
        return EVO_VAL_MAX;
    }
    return 0;
}

static bool field_ok(const evo_field_t *f)
{
    uint16_t nm = natural_max(f->type);
    if (!nm || f->tag == 0 || f->tag > EVO_TAG_MAX) return false;
    if (f->flags & ~(EVO_FF_REQUIRED | EVO_FF_MUST_UNDERSTAND)) return false;
    if (f->max_len == 0 || f->max_len > nm) return false;
    if (f->type == EVO_T_UINT && f->max_len != 8) return false;
    if (f->type == EVO_T_CID && f->max_len != EVO_CID_LEN) return false;
    if (f->type != EVO_T_UINT && f->def) return false;
    if ((f->flags & EVO_FF_REQUIRED) && f->def) return false;
    return true;
}

evo_status_t evo_desc_init(evo_desc_t *d, const char *family, evo_kind_t kind, evo_class_t klass)
{
    if (!d) return EVO_ERR_ARG;
    evo_zero(d, sizeof(*d));
    if (kind < EVO_KIND_MESSAGE || kind > EVO_KIND_MODULE_IFACE) return EVO_ERR_ARG;
    if (klass != EVO_CLASS_GENERAL && klass != EVO_CLASS_MONEY) return EVO_ERR_ARG;
    if (!evo_set_name(d->name, &d->name_len, family, EVO_NAME_MAX)) return EVO_ERR_ARG;
    d->kind = (uint8_t) kind;
    d->klass = (uint8_t) klass;
    return EVO_OK;
}

evo_status_t evo_desc_add(evo_desc_t *d, uint16_t tag, evo_type_t type, uint8_t flags,
                          uint16_t max_len, uint64_t def)
{
    if (!d) return EVO_ERR_ARG;
    evo_field_t f = {tag, (uint8_t) type, flags, max_len ? max_len : natural_max((uint8_t) type),
                     def};
    if (!field_ok(&f)) return EVO_ERR_ARG;
    if (d->n_fields >= EVO_MAX_FIELDS) return EVO_ERR_FULL;
    uint32_t i = d->n_fields;
    while (i > 0 && d->f[i - 1].tag > tag) {
        d->f[i] = d->f[i - 1];
        i--;
    }
    if (i > 0 && d->f[i - 1].tag == tag) {
        /* undo the shift */
        for (uint32_t j = i; j < d->n_fields; j++) d->f[j] = d->f[j + 1];
        return EVO_ERR_DUP;
    }
    d->f[i] = f;
    d->n_fields++;
    return EVO_OK;
}

const evo_field_t *evo_desc_field(const evo_desc_t *d, uint16_t tag)
{
    for (uint32_t i = 0; i < d->n_fields; i++)
        if (d->f[i].tag == tag) return &d->f[i];
    return 0;
}

#define DESC_MAGIC EVO_MAGIC('E', 'V', 'C', '1')

uint32_t evo_desc_encode(const evo_desc_t *d, uint8_t *out, uint32_t cap)
{
    if (!d || !out || d->n_fields > EVO_MAX_FIELDS) return 0;
    evo_w_t w = {out, cap, 0, false};
    evo_w_u32(&w, DESC_MAGIC);
    evo_w_u8(&w, d->kind);
    evo_w_u8(&w, d->klass);
    evo_w_u8(&w, d->name_len);
    evo_w_bytes(&w, d->name, d->name_len);
    evo_w_u8(&w, d->n_fields);
    for (uint32_t i = 0; i < d->n_fields; i++) {
        const evo_field_t *f = &d->f[i];
        if (!field_ok(f) || (i && d->f[i - 1].tag >= f->tag)) return 0;
        evo_w_u16(&w, f->tag);
        evo_w_u8(&w, f->type);
        evo_w_u8(&w, f->flags);
        evo_w_u16(&w, f->max_len);
        evo_w_u64(&w, f->def);
    }
    return w.err ? 0 : w.len;
}

evo_status_t evo_desc_decode(const uint8_t *in, uint32_t len, evo_desc_t *d)
{
    if (!in || !d) return EVO_ERR_ARG;
    evo_zero(d, sizeof(*d));
    evo_r_t r = {in, len, 0, false};
    if (evo_r_le(&r, 4) != DESC_MAGIC) return EVO_ERR_PARSE;
    d->kind = (uint8_t) evo_r_le(&r, 1);
    d->klass = (uint8_t) evo_r_le(&r, 1);
    d->name_len = (uint8_t) evo_r_le(&r, 1);
    if (r.err || d->name_len == 0 || d->name_len > EVO_NAME_MAX) return EVO_ERR_PARSE;
    if (d->kind < EVO_KIND_MESSAGE || d->kind > EVO_KIND_MODULE_IFACE || d->klass > 1)
        return EVO_ERR_PARSE;
    evo_r_copy(&r, d->name, d->name_len);
    d->n_fields = (uint8_t) evo_r_le(&r, 1);
    if (d->n_fields > EVO_MAX_FIELDS) return EVO_ERR_PARSE;
    for (uint32_t i = 0; i < d->n_fields; i++) {
        evo_field_t *f = &d->f[i];
        f->tag = (uint16_t) evo_r_le(&r, 2);
        f->type = (uint8_t) evo_r_le(&r, 1);
        f->flags = (uint8_t) evo_r_le(&r, 1);
        f->max_len = (uint16_t) evo_r_le(&r, 2);
        f->def = evo_r_le(&r, 8);
        if (r.err || !field_ok(f) || (i && d->f[i - 1].tag >= f->tag)) return EVO_ERR_PARSE;
    }
    if (r.err || r.pos != len) return EVO_ERR_PARSE;
    for (uint32_t i = 0; i < d->name_len; i++)
        if (d->name[i] == 0) return EVO_ERR_PARSE;
    return EVO_OK;
}

evo_status_t evo_desc_cid(const evo_desc_t *d, evo_cid_t *out)
{
    uint8_t buf[EVO_DESC_ENC_MAX];
    uint32_t n = evo_desc_encode(d, buf, sizeof(buf));
    if (!n) return EVO_ERR_ARG;
    evo_cid_of(buf, n, out);
    return EVO_OK;
}

evo_status_t evo_desc_core(const evo_desc_t *a, const evo_desc_t *b, evo_desc_t *core)
{
    if (!a || !b || !core) return EVO_ERR_ARG;
    if (evo_name_cmp(a->name, a->name_len, b->name, b->name_len) != 0) return EVO_ERR_ARG;
    if (a->kind != b->kind) return EVO_ERR_CONFLICT;
    evo_desc_t c;
    evo_zero(&c, sizeof(c));
    c.kind = a->kind;
    c.klass = a->klass > b->klass ? a->klass : b->klass;
    c.name_len = a->name_len;
    evo_cpy(c.name, a->name, a->name_len);
    uint32_t i = 0, j = 0;
    while (i < a->n_fields || j < b->n_fields) {
        const evo_field_t *fa = i < a->n_fields ? &a->f[i] : 0;
        const evo_field_t *fb = j < b->n_fields ? &b->f[j] : 0;
        if (fa && (!fb || fa->tag < fb->tag)) {
            if (fa->flags & EVO_FF_REQUIRED) return EVO_ERR_CONFLICT;
            i++;
        } else if (fb && (!fa || fb->tag < fa->tag)) {
            if (fb->flags & EVO_FF_REQUIRED) return EVO_ERR_CONFLICT;
            j++;
        } else {
            if (fa->type == fb->type && fa->def == fb->def) {
                evo_field_t f = *fa;
                f.flags = (uint8_t) (fa->flags | fb->flags);
                f.max_len = fa->max_len < fb->max_len ? fa->max_len : fb->max_len;
                c.f[c.n_fields++] = f;
            } else if ((fa->flags | fb->flags) & EVO_FF_REQUIRED) {
                return EVO_ERR_CONFLICT;
            }
            i++;
            j++;
        }
    }
    evo_cpy(core, &c, sizeof(c));
    return EVO_OK;
}

bool evo_desc_satisfies(const evo_desc_t *have, const evo_desc_t *want)
{
    evo_desc_t c;
    if (!have || !want) return false;
    if (evo_name_cmp(have->name, have->name_len, want->name, want->name_len) != 0) return false;
    if (evo_desc_core(have, want, &c) != EVO_OK) return false;
    return c.n_fields == want->n_fields;
}

uint16_t evo_ext_tag(const char *ext_name)
{
    uint8_t h[32];
    uint32_t n = ext_name ? evo_strlen(ext_name, 255) : 0;
    sha3_256((const uint8_t *) ext_name, n, h);
    uint32_t span = (uint32_t) EVO_TAG_MAX - EVO_EXT_TAG_FIRST + 1u; /* 0x7000 */
    uint32_t v = ((uint32_t) h[0] | ((uint32_t) h[1] << 8)) & 0x7fffu;
    while (v >= span) v -= span; /* at most one step: 0x7fff < 2 * 0x7000 */
    return (uint16_t) (EVO_EXT_TAG_FIRST + v);
}

/* ===== Registry ===== */
void evo_reg_init(evo_registry_t *r)
{
    evo_zero(r, sizeof(*r));
}

static evo_status_t reg_insert(evo_registry_t *r, const evo_desc_t *d, const evo_cid_t *cid)
{
    if (evo_reg_find(r, cid)) return EVO_ERR_DUP;
    if (r->n >= EVO_REG_MAX) return EVO_ERR_FULL;
    evo_cpy(&r->cid[r->n], cid, sizeof(*cid));
    evo_cpy(&r->d[r->n], d, sizeof(*d));
    r->n++;
    return EVO_OK;
}

evo_status_t evo_reg_add(evo_registry_t *r, const evo_desc_t *d, evo_cid_t *cid_out)
{
    evo_cid_t cid;
    if (!r || !d) return EVO_ERR_ARG;
    if (evo_desc_cid(d, &cid) != EVO_OK) return EVO_ERR_ARG;
    if (cid_out) evo_cpy(cid_out, &cid, sizeof(cid));
    return reg_insert(r, d, &cid);
}

evo_status_t evo_reg_add_bytes(evo_registry_t *r, const uint8_t *in, uint32_t len,
                               const evo_cid_t *expect)
{
    evo_desc_t d;
    evo_cid_t cid;
    if (!r || !in || !expect) return EVO_ERR_ARG;
    evo_cid_of(in, len, &cid);
    if (!evo_cid_eq(&cid, expect)) return EVO_ERR_CID_MISMATCH;
    evo_status_t s = evo_desc_decode(in, len, &d);
    if (s != EVO_OK) return s;
    return reg_insert(r, &d, &cid);
}

const evo_desc_t *evo_reg_find(const evo_registry_t *r, const evo_cid_t *cid)
{
    for (uint32_t i = 0; i < r->n; i++)
        if (evo_cid_eq(&r->cid[i], cid)) return &r->d[i];
    return 0;
}

/* ===== Capability sets ===== */
void evo_capset_init(evo_capset_t *s)
{
    evo_zero(s, sizeof(*s));
}

evo_status_t evo_capset_put(evo_capset_t *s, const evo_desc_t *d)
{
    evo_cid_t cid;
    if (!s || !d || evo_desc_cid(d, &cid) != EVO_OK) return EVO_ERR_ARG;
    uint32_t i = 0;
    while (i < s->n && evo_name_cmp(s->c[i].name, s->c[i].name_len, d->name, d->name_len) < 0) i++;
    if (i < s->n && evo_name_cmp(s->c[i].name, s->c[i].name_len, d->name, d->name_len) == 0) {
        evo_cpy(&s->c[i].cid, &cid, sizeof(cid));
        return EVO_OK;
    }
    if (s->n >= EVO_CAPSET_MAX) return EVO_ERR_FULL;
    for (uint32_t j = s->n; j > i; j--) evo_cpy(&s->c[j], &s->c[j - 1], sizeof(evo_cap_t));
    evo_zero(&s->c[i], sizeof(evo_cap_t));
    s->c[i].name_len = d->name_len;
    evo_cpy(s->c[i].name, d->name, d->name_len);
    evo_cpy(&s->c[i].cid, &cid, sizeof(cid));
    s->n++;
    return EVO_OK;
}

#define CAPSET_MAGIC EVO_MAGIC('E', 'V', 'S', '1')

uint32_t evo_capset_encode(const evo_capset_t *s, uint8_t *out, uint32_t cap)
{
    if (!s || !out || s->n > EVO_CAPSET_MAX) return 0;
    evo_w_t w = {out, cap, 0, false};
    evo_w_u32(&w, CAPSET_MAGIC);
    evo_w_u8(&w, (uint8_t) s->n);
    for (uint32_t i = 0; i < s->n; i++) {
        evo_w_u8(&w, s->c[i].name_len);
        evo_w_bytes(&w, s->c[i].name, s->c[i].name_len);
        evo_w_bytes(&w, s->c[i].cid.b, EVO_CID_LEN);
    }
    return w.err ? 0 : w.len;
}

evo_status_t evo_capset_decode(const uint8_t *in, uint32_t len, evo_capset_t *s)
{
    if (!in || !s) return EVO_ERR_ARG;
    evo_capset_init(s);
    evo_r_t r = {in, len, 0, false};
    if (evo_r_le(&r, 4) != CAPSET_MAGIC) return EVO_ERR_PARSE;
    uint32_t n = (uint32_t) evo_r_le(&r, 1);
    if (n > EVO_CAPSET_MAX) return EVO_ERR_PARSE;
    for (uint32_t i = 0; i < n; i++) {
        evo_cap_t *c = &s->c[i];
        c->name_len = (uint8_t) evo_r_le(&r, 1);
        if (r.err || c->name_len == 0 || c->name_len > EVO_NAME_MAX) return EVO_ERR_PARSE;
        evo_r_copy(&r, c->name, c->name_len);
        evo_r_copy(&r, c->cid.b, EVO_CID_LEN);
        if (r.err) return EVO_ERR_PARSE;
        if (i && evo_name_cmp(s->c[i - 1].name, s->c[i - 1].name_len, c->name, c->name_len) >= 0)
            return EVO_ERR_PARSE;
    }
    s->n = n;
    if (r.err || r.pos != len) return EVO_ERR_PARSE;
    return EVO_OK;
}

/* ===== Negotiation ===== */
evo_status_t evo_negotiate(const evo_registry_t *reg, const evo_capset_t *local,
                           const evo_capset_t *remote, evo_session_t *s)
{
    if (!reg || !local || !remote || !s) return EVO_ERR_ARG;
    evo_zero(s, sizeof(*s));
    uint32_t i = 0, j = 0;
    while (i < local->n && j < remote->n) {
        const evo_cap_t *a = &local->c[i], *b = &remote->c[j];
        int c = evo_name_cmp(a->name, a->name_len, b->name, b->name_len);
        if (c < 0) {
            i++;
            continue;
        }
        if (c > 0) {
            j++;
            continue;
        }
        i++;
        j++;
        const evo_desc_t *ld = evo_reg_find(reg, &a->cid);
        if (!ld) return EVO_ERR_NOT_FOUND; /* our own descriptor must be held */
        const evo_desc_t *rd = evo_reg_find(reg, &b->cid);
        if (!rd) {
            evo_cpy(&s->need[s->n_need++], &b->cid, sizeof(evo_cid_t));
            continue;
        }
        evo_sess_cap_t *e = &s->e[s->n++];
        e->name_len = a->name_len;
        evo_cpy(e->name, a->name, a->name_len);
        evo_cpy(&e->local_cid, &a->cid, sizeof(evo_cid_t));
        evo_cpy(&e->remote_cid, &b->cid, sizeof(evo_cid_t));
        evo_status_t st = evo_desc_core(ld, rd, &e->core);
        if (st == EVO_OK && evo_desc_cid(&e->core, &e->core_cid) != EVO_OK) st = EVO_ERR_CONFLICT;
        e->status = (uint8_t) (st == EVO_OK ? EVO_OK : EVO_ERR_CONFLICT);
        e->money = st == EVO_OK && e->core.klass == EVO_CLASS_MONEY;
        e->allowed = st == EVO_OK && !e->money;
    }
    return EVO_OK;
}

const evo_sess_cap_t *evo_session_find(const evo_session_t *s, const char *family)
{
    uint32_t n = evo_strlen(family, EVO_NAME_MAX);
    for (uint32_t i = 0; i < s->n; i++)
        if (evo_name_cmp(s->e[i].name, s->e[i].name_len, family, n) == 0) return &s->e[i];
    return 0;
}

uint32_t evo_session_gate(evo_session_t *s, uint32_t local_conform, uint32_t peer_conform)
{
    bool ok = local_conform == EVO_CHK_ALL && peer_conform == EVO_CHK_ALL;
    uint32_t allowed = 0;
    for (uint32_t i = 0; i < s->n; i++) {
        evo_sess_cap_t *e = &s->e[i];
        if (e->status != EVO_OK)
            e->allowed = false;
        else if (e->money)
            e->allowed = ok;
        else
            e->allowed = true;
        allowed += e->allowed ? 1u : 0u;
    }
    return allowed;
}
