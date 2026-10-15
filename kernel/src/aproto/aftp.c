/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* aftp.c — the FTP twin: signed listings, consented offers, multi-source
 * Merkle-verified gets, logged serving. See aftp.h. */
#include "aftp.h"

#define EN aftp_entry_t
static const vna_field_t entry_fields[] = {
    VNA_FVAR(EN, name, name_len, VNA_AXIS_NONE),
    VNA_FFIX(EN, root, VNA_AXIS_PROVENANCE),
    VNA_FU64(EN, size, 0, VNA_AXIS_NONE),
    VNA_FU32(EN, chunk_size, VNA_CHUNK_SIZE, VNA_AXIS_NONE),
    VNA_FU8(EN, kind, AFTP_KIND_MAX, VNA_AXIS_NONE),
    VNA_FU8(EN, tag, AP_TAG_B, VNA_AXIS_PROVENANCE),
};
#undef EN
/* UBH class 3 = CONTENT_OBJECT */
const vna_schema_t aftp_entry_schema = {"aftp_entry", 0x0150, 3, entry_fields, 6};

#define D aftp_dir_t
static const vna_field_t dir_fields[] = {
    VNA_FCONST(D, magic, AFTP_DIR_MAGIC),
    VNA_FFIX(D, publisher, VNA_AXIS_PROVENANCE),
    VNA_FFIX(D, share, VNA_AXIS_NONE),
    VNA_FU64(D, seq, 0, VNA_AXIS_NONE),
    VNA_FU64(D, created, 0, VNA_AXIS_NONE),
    VNA_FU64(D, expires, 0, VNA_AXIS_NONE),
    VNA_FU8(D, tag, AP_TAG_B, VNA_AXIS_PROVENANCE),
    VNA_FARR(D, e, n, &aftp_entry_schema, aftp_entry_t, VNA_AXIS_NONE),
    VNA_FSIG(D, sig),
};
#undef D
const vna_schema_t aftp_dir_schema = {"aftp_dir", 0x0151, 3, dir_fields, 9};

#define Q aftp_dir_req_t
static const vna_field_t dir_req_fields[] = {
    VNA_FCONST(Q, magic, AFTP_DIRREQ_MAGIC),
    VNA_FFIX(Q, publisher, VNA_AXIS_NONE),
    VNA_FFIX(Q, share, VNA_AXIS_NONE),
    VNA_FU64(Q, have_seq, 0, VNA_AXIS_NONE),
};
#undef Q
const vna_schema_t aftp_dir_req_schema = {"aftp_dir_req", 0x0152, 3, dir_req_fields, 4};

#define O aftp_offer_t
static const vna_field_t offer_fields[] = {
    VNA_FCONST(O, magic, AFTP_OFFER_MAGIC),
    VNA_FFIX(O, from, VNA_AXIS_PROVENANCE),
    VNA_FFIX(O, to, VNA_AXIS_NONE),
    VNA_FU64(O, nonce, 0, VNA_AXIS_NONE),
    VNA_FU64(O, time, 0, VNA_AXIS_NONE),
    VNA_FU8(O, tag, AP_TAG_B, VNA_AXIS_PROVENANCE),
    VNA_FVAR(O, name, name_len, VNA_AXIS_NONE),
    VNA_FFIX(O, root, VNA_AXIS_PROVENANCE),
    VNA_FU64(O, size, 0, VNA_AXIS_NONE),
    VNA_FU32(O, chunk_size, VNA_CHUNK_SIZE, VNA_AXIS_NONE),
    VNA_FU8(O, kind, AFTP_KIND_MAX, VNA_AXIS_NONE),
    VNA_FVAR(O, note, note_len, VNA_AXIS_EXTERNALITY),
    VNA_FSIG(O, sig),
};
#undef O
const vna_schema_t aftp_offer_schema = {"aftp_offer", 0x0153, 3, offer_fields, 13};

/* ---- D2: text and paths ---- */

/* Length of the UTF-8 sequence at p (1..4), or 0 if it is not well formed or
 * is a control (C0, DEL, C1) or a bidirectional override, which can make a
 * name display as something it is not. */
static uint32_t utf8_char(const uint8_t *p, uint32_t rem, bool newline_ok)
{
    uint8_t c = p[0];
    if (c < 0x80) {
        if (c == '\n' && newline_ok) return 1;
        return (c < 0x20 || c == 0x7f) ? 0 : 1;
    }
    uint32_t n;
    uint8_t lo = 0x80, hi = 0xBF;
    if (c >= 0xC2 && c <= 0xDF)
        n = 2;
    else if (c == 0xE0)
        n = 3, lo = 0xA0;
    else if ((c >= 0xE1 && c <= 0xEC) || c == 0xEE || c == 0xEF)
        n = 3;
    else if (c == 0xED)
        n = 3, hi = 0x9F; /* no surrogates */
    else if (c == 0xF0)
        n = 4, lo = 0x90;
    else if (c >= 0xF1 && c <= 0xF3)
        n = 4;
    else if (c == 0xF4)
        n = 4, hi = 0x8F; /* nothing above U+10FFFF */
    else
        return 0; /* stray continuation, overlong lead, F5.. */
    if (rem < n || p[1] < lo || p[1] > hi) return 0;
    for (uint32_t k = 2; k < n; k++)
        if (p[k] < 0x80 || p[k] > 0xBF) return 0;
    if (c == 0xC2 && p[1] <= 0x9F) return 0; /* C1 controls U+0080..U+009F */
    if (c == 0xE2 && p[1] == 0x80 && p[2] >= 0xAA && p[2] <= 0xAE) return 0; /* U+202A..E */
    if (c == 0xE2 && p[1] == 0x81 && p[2] >= 0xA6 && p[2] <= 0xA9) return 0; /* U+2066..9 */
    return n;
}

static bool text_ok(const uint8_t *p, uint32_t len, bool newline_ok)
{
    for (uint32_t i = 0; i < len;) {
        uint32_t k = utf8_char(p + i, len - i, newline_ok);
        if (!k) return false;
        i += k;
    }
    return true;
}

bool aftp_path_ok(const uint8_t *p, uint32_t len)
{
    if (!p || len == 0 || len > AFTP_NAME_MAX || !text_ok(p, len, false)) return false;
    uint32_t seg = 0;
    for (uint32_t i = 0; i <= len; i++) {
        if (i < len && p[i] == '\\') return false;
        if (i < len && p[i] != '/') continue;
        uint32_t sl = i - seg;
        if (sl == 0) return false;
        if (p[seg] == '.' && (sl == 1 || (sl == 2 && p[seg + 1] == '.'))) return false;
        seg = i + 1;
    }
    return true;
}

/* -1, 0, +1: byte order, a prefix sorting first. */
static int name_cmp(const aftp_entry_t *x, const aftp_entry_t *y)
{
    uint32_t m = x->name_len < y->name_len ? x->name_len : y->name_len;
    for (uint32_t i = 0; i < m; i++)
        if (x->name[i] != y->name[i]) return x->name[i] < y->name[i] ? -1 : 1;
    return x->name_len == y->name_len ? 0 : (x->name_len < y->name_len ? -1 : 1);
}

static bool entry_ok(const aftp_entry_t *e)
{
    return e->name_len <= AFTP_NAME_MAX && aftp_path_ok(e->name, e->name_len) &&
           vna_file_nchunks(e->size, e->chunk_size) != 0 && e->kind <= AFTP_KIND_MAX &&
           e->tag <= AP_TAG_B;
}

vna_status_t aftp_dir_check(const aftp_dir_t *d)
{
    if (!d || d->n > AFTP_DIR_MAX || d->tag > AP_TAG_B) return VNA_ERR_ARG;
    for (uint32_t i = 0; i < d->n; i++) {
        if (!entry_ok(&d->e[i])) return VNA_ERR_PARSE;                        /* D2, D4 */
        if (i && name_cmp(&d->e[i - 1], &d->e[i]) >= 0) return VNA_ERR_PARSE; /* D3 */
    }
    return VNA_OK;
}

vna_status_t aftp_init(aftp_t *a, const vna_identity_t *self, ap_log_t *log)
{
    if (!a || !self || !log || !vna_id_eq(&log->owner, &self->id)) return VNA_ERR_ARG;
    vna_zero(a, sizeof *a);
    a->self = self;
    a->log = log;
    return VNA_OK;
}

static vna_status_t logev(aftp_t *a, uint16_t ev, uint8_t tag, const vna_id_t *peer,
                          const vna_id_t *subject, uint64_t amount, int32_t st, uint64_t now)
{
    return ap_log_append(a->log, AP_TWIN_AFTP, ev, tag, peer, subject, amount, st, now) == VNA_OK
               ? VNA_OK
               : VNA_ERR_SPACE;
}

static void digest_id(const uint8_t *p, uint32_t len, vna_id_t *out)
{
    vna_sha3(p, len, out->b);
}

/* ---- listings ---- */

int32_t aftp_dir_sign(aftp_t *a, aftp_dir_t *d, const uint8_t rnd[32], uint64_t now, uint8_t *out,
                      uint32_t cap)
{
    if (!a || !d || !rnd || !out) return VNA_ERR_ARG;
    d->magic = AFTP_DIR_MAGIC;
    d->publisher = a->self->id;
    vna_status_t st = aftp_dir_check(d);
    if (st != VNA_OK) return st;
    if (d->expires <= d->created) return VNA_ERR_ARG;
    int32_t m = vna_schema_pack(&aftp_dir_schema, d, out, cap, false);
    if (m < 0) return VNA_ERR_SPACE;
    vna_id_t dg;
    digest_id(out, (uint32_t) m, &dg);
    vna_sign(a->self->sk, AFTP_CTX_DIR, out, (uint32_t) m, rnd, d->sig);
    int32_t n = vna_schema_pack(&aftp_dir_schema, d, out, cap, true);
    if (n < 0) return VNA_ERR_SPACE;
    if (logev(a, AFTP_EV_DIR_OUT, AP_TAG_T, 0, &dg, d->seq, 0, now) != VNA_OK) return VNA_ERR_SPACE;
    return n;
}

static vna_status_t dir_open(const uint8_t *in, uint32_t len, const uint8_t pk[VNA_PK_LEN],
                             uint64_t now, aftp_dir_t *d, uint32_t *sig_off)
{
    if (!in || !pk || !d) return VNA_ERR_ARG;
    if (vna_schema_unpack(&aftp_dir_schema, in, len, d, sig_off) < 0) return VNA_ERR_PARSE;
    vna_id_t id;
    vna_node_id(pk, &id);
    if (!vna_id_eq(&id, &d->publisher)) return VNA_ERR_BINDING;
    if (!vna_verify(pk, AFTP_CTX_DIR, in, *sig_off, d->sig)) return VNA_ERR_SIG;
    if (now >= d->expires) return VNA_ERR_EXPIRED;
    if (d->created > now + AFTP_SKEW_MS) return VNA_ERR_STALE;
    return aftp_dir_check(d) == VNA_OK ? VNA_OK : VNA_ERR_PARSE;
}

vna_status_t aftp_dir_open(const uint8_t *in, uint32_t len, const uint8_t pk[VNA_PK_LEN],
                           uint64_t now, aftp_dir_t *out)
{
    uint32_t so = 0;
    return dir_open(in, len, pk, now, out, &so);
}

static aftp_known_t *known_find(aftp_t *a, const vna_id_t *pub, const vna_id_t *share)
{
    for (uint32_t i = 0; i < AFTP_KNOWN; i++)
        if (a->known[i].used && vna_id_eq(&a->known[i].publisher, pub) &&
            vna_id_eq(&a->known[i].share, share))
            return &a->known[i];
    return 0;
}

bool aftp_dir_equivocated(const aftp_t *a, const vna_id_t *publisher, const vna_id_t *share)
{
    const aftp_known_t *k = known_find((aftp_t *) a, publisher, share);
    return k && k->equivocated;
}

vna_status_t aftp_dir_learn(aftp_t *a, const uint8_t *in, uint32_t len,
                            const uint8_t pk[VNA_PK_LEN], uint64_t now, aftp_dir_t *out,
                            uint8_t *tag)
{
    uint8_t t = AP_TAG_F;
    if (tag) *tag = AP_TAG_F;
    if (!a || !in || !pk || !out) return VNA_ERR_ARG;
    uint32_t so = 0;
    vna_status_t st = dir_open(in, len, pk, now, out, &so);
    vna_id_t dg, pub;
    vna_zero(&dg, sizeof dg);
    vna_node_id(pk, &pub); /* the key that was offered, whether or not it signed */
    aftp_known_t *k = 0, *slot = 0;
    if (st == VNA_OK) {
        digest_id(in, so, &dg);
        k = known_find(a, &out->publisher, &out->share);
        if (k) {
            if (out->seq < k->seq)
                st = VNA_ERR_REPLAY;
            else if (out->seq == k->seq)
                st = vna_eq(dg.b, k->digest, 32) ? VNA_ERR_DUP : VNA_ERR_FORK;
        } else {
            for (uint32_t i = 0; i < AFTP_KNOWN && !slot; i++)
                if (!a->known[i].used) slot = &a->known[i];
            if (!slot) st = VNA_ERR_SPACE;
        }
        t = st == VNA_OK || st == VNA_ERR_DUP ? AP_TAG_T
            : st == VNA_ERR_FORK              ? AP_TAG_B
            : st == VNA_ERR_SPACE             ? AP_TAG_N
                                              : AP_TAG_F;
    }
    if (logev(a, AFTP_EV_DIR_IN, t, &pub, &dg, st == VNA_OK ? out->seq : 0, st, now) != VNA_OK)
        return VNA_ERR_SPACE; /* no audit, no action */
    if (tag) *tag = t;
    if (st == VNA_ERR_FORK) {
        k->equivocated = true;
        a->equivocations++;
    } else if (st == VNA_OK) {
        if (!k) {
            k = slot;
            k->used = true;
            k->equivocated = false;
            k->publisher = out->publisher;
            k->share = out->share;
        }
        k->seq = out->seq;
        vna_copy(k->digest, dg.b, 32);
    }
    if (st == VNA_OK || st == VNA_ERR_DUP)
        a->dirs_in++;
    else
        a->dirs_refused++;
    return st;
}

int32_t aftp_dir_req_encode(const vna_id_t *publisher, const vna_id_t *share, uint64_t have_seq,
                            uint8_t *out, uint32_t cap)
{
    if (!publisher || !share || !out) return VNA_ERR_ARG;
    aftp_dir_req_t q = {AFTP_DIRREQ_MAGIC, *publisher, *share, have_seq};
    int32_t n = vna_schema_pack(&aftp_dir_req_schema, &q, out, cap, true);
    return n < 0 ? VNA_ERR_SPACE : n;
}

vna_status_t aftp_dir_req_decode(const uint8_t *in, uint32_t len, aftp_dir_req_t *out)
{
    if (!in || !out) return VNA_ERR_ARG;
    return vna_schema_unpack(&aftp_dir_req_schema, in, len, out, 0) < 0 ? VNA_ERR_PARSE : VNA_OK;
}

/* ---- offers ---- */

int32_t aftp_offer_make(aftp_t *a, const vna_id_t *to, const aftp_entry_t *file,
                        const uint8_t *note, uint16_t note_len, uint64_t nonce,
                        const uint8_t rnd[32], uint64_t now, uint8_t *out, uint32_t cap)
{
    static aftp_offer_t o;
    if (!a || !to || !file || !rnd || !out || (note_len && !note)) return VNA_ERR_ARG;
    if (!entry_ok(file) || note_len > AFTP_NOTE_MAX || !text_ok(note, note_len, true))
        return VNA_ERR_PARSE;
    vna_zero(&o, sizeof o);
    o.magic = AFTP_OFFER_MAGIC;
    o.from = a->self->id;
    o.to = *to;
    o.nonce = nonce;
    o.time = now;
    o.tag = file->tag;
    o.name_len = file->name_len;
    vna_copy(o.name, file->name, file->name_len);
    o.root = file->root;
    o.size = file->size;
    o.chunk_size = file->chunk_size;
    o.kind = file->kind;
    o.note_len = note_len;
    if (note_len) vna_copy(o.note, note, note_len);
    int32_t m = vna_schema_pack(&aftp_offer_schema, &o, out, cap, false);
    if (m < 0) return VNA_ERR_SPACE;
    vna_sign(a->self->sk, AFTP_CTX_OFFER, out, (uint32_t) m, rnd, o.sig);
    int32_t n = vna_schema_pack(&aftp_offer_schema, &o, out, cap, true);
    if (n < 0) return VNA_ERR_SPACE;
    if (logev(a, AFTP_EV_OFFER_OUT, AP_TAG_N, to, &file->root, file->size, 0, now) != VNA_OK)
        return VNA_ERR_SPACE;
    return n;
}

vna_status_t aftp_offer_receive(aftp_t *a, const uint8_t *in, uint32_t len,
                                const uint8_t pk[VNA_PK_LEN], uint64_t now, int32_t *slot)
{
    static aftp_offer_t o;
    if (slot) *slot = -1;
    if (!a || !in || !pk || !slot) return VNA_ERR_ARG;
    vna_id_t from, root;
    vna_node_id(pk, &from);
    vna_zero(&root, sizeof root);
    uint32_t so = 0;
    vna_status_t st = VNA_OK;
    aftp_nonce_t *nn = 0;
    aftp_pending_t *pd = 0;
    if (vna_schema_unpack(&aftp_offer_schema, in, len, &o, &so) < 0)
        st = VNA_ERR_PARSE;
    else {
        root = o.root;
        aftp_entry_t f;
        f.name_len = o.name_len;
        vna_copy(f.name, o.name, o.name_len);
        f.size = o.size;
        f.chunk_size = o.chunk_size;
        f.kind = o.kind;
        f.tag = o.tag;
        if (!vna_id_eq(&o.to, &a->self->id))
            st = VNA_ERR_DST;
        else if (!vna_id_eq(&o.from, &from))
            st = VNA_ERR_BINDING;
        else if (!vna_verify(pk, AFTP_CTX_OFFER, in, so, o.sig))
            st = VNA_ERR_SIG;
        else if (vna_absdiff(o.time, now) > AFTP_SKEW_MS)
            st = VNA_ERR_STALE;
        else if (!entry_ok(&f) || !text_ok(o.note, o.note_len, true))
            st = VNA_ERR_PARSE;
        else {
            for (uint32_t i = 0; i < AFTP_NONCES; i++) {
                aftp_nonce_t *x = &a->nonces[i];
                if (x->used && x->time + AFTP_SKEW_MS < now) x->used = false; /* past replay */
                if (x->used && x->nonce == o.nonce && vna_id_eq(&x->from, &from))
                    st = VNA_ERR_REPLAY;
                if (!x->used && !nn) nn = x;
            }
            for (uint32_t i = 0; i < AFTP_INBOX && !pd; i++)
                if (!a->inbox[i].used) pd = &a->inbox[i];
            if (st == VNA_OK && (!nn || !pd)) st = VNA_ERR_CAP;
        }
    }
    uint8_t t = st == VNA_OK ? AP_TAG_N : AP_TAG_F;
    if (logev(a, AFTP_EV_OFFER_IN, t, &from, &root, st == VNA_OK ? o.size : 0, st, now) != VNA_OK)
        return VNA_ERR_SPACE;
    if (st != VNA_OK) {
        a->offers_refused++;
        return st;
    }
    nn->used = true;
    nn->from = from;
    nn->nonce = o.nonce;
    nn->time = o.time;
    vna_zero(pd, sizeof *pd);
    pd->used = true;
    pd->from = from;
    pd->nonce = o.nonce;
    pd->time = o.time;
    pd->file.name_len = o.name_len;
    vna_copy(pd->file.name, o.name, o.name_len);
    pd->file.root = o.root;
    pd->file.size = o.size;
    pd->file.chunk_size = o.chunk_size;
    pd->file.kind = o.kind;
    pd->file.tag = o.tag;
    pd->note_len = o.note_len;
    vna_copy(pd->note, o.note, o.note_len);
    a->offers_in++;
    *slot = (int32_t) (pd - a->inbox);
    return VNA_OK;
}

vna_status_t aftp_offer_decline(aftp_t *a, uint32_t slot, uint64_t now)
{
    if (!a || slot >= AFTP_INBOX || !a->inbox[slot].used) return VNA_ERR_ARG;
    aftp_pending_t *p = &a->inbox[slot];
    if (logev(a, AFTP_EV_OFFER_DECLINE, AP_TAG_F, &p->from, &p->file.root, p->file.size, 0, now) !=
        VNA_OK)
        return VNA_ERR_SPACE;
    p->used = false;
    return VNA_OK;
}

/* ---- gets ---- */

static bool bit(const vna_fetch_t *f, uint32_t i)
{
    return (f->bitmap[i >> 3] >> (i & 7u)) & 1u;
}

vna_status_t aftp_get_start(aftp_t *a, aftp_get_t *g, const vna_id_t *root, uint64_t size,
                            uint32_t chunk_size, uint8_t *bitmap, uint32_t bitmap_len, bool resume,
                            uint32_t window, uint64_t timeout, uint64_t now)
{
    uint32_t n = vna_file_nchunks(size, chunk_size);
    if (!a || !g || !root || !bitmap || n == 0 || bitmap_len < (n + 7u) / 8u || window == 0 ||
        window > AFTP_WINDOW || timeout == 0)
        return VNA_ERR_ARG;
    uint32_t have = 0;
    if (resume) {
        if ((n & 7u) && (bitmap[n >> 3] >> (n & 7u))) return VNA_ERR_ARG; /* bits past the end */
        for (uint32_t i = 0; i < n; i++) have += (bitmap[i >> 3] >> (i & 7u)) & 1u;
    }
    if (logev(a, AFTP_EV_GET_START, AP_TAG_N, 0, root, size, 0, now) != VNA_OK)
        return VNA_ERR_SPACE;
    vna_zero(g, sizeof *g);
    g->a = a;
    g->f.root = *root;
    g->f.size = size;
    g->f.chunk_size = chunk_size;
    g->f.n = n;
    g->f.bitmap = bitmap;
    if (!resume) vna_zero(bitmap, (n + 7u) / 8u);
    g->f.received = have;
    g->window = window;
    g->timeout = timeout;
    if (have == n && logev(a, AFTP_EV_GET_DONE, AP_TAG_T, 0, root, size, 0, now) == VNA_OK)
        g->done = true;
    return VNA_OK;
}

vna_status_t aftp_get_add_source(aftp_get_t *g, const vna_id_t *id)
{
    if (!g || !id) return VNA_ERR_ARG;
    for (uint32_t i = 0; i < g->nsrc; i++)
        if (vna_id_eq(&g->src[i].id, id)) return VNA_ERR_DUP;
    if (g->nsrc >= AFTP_SOURCES) return VNA_ERR_SPACE;
    vna_zero(&g->src[g->nsrc], sizeof g->src[0]);
    g->src[g->nsrc++].id = *id;
    return VNA_OK;
}

vna_status_t aftp_offer_accept(aftp_t *a, uint32_t slot, aftp_get_t *g, uint8_t *bitmap,
                               uint32_t bitmap_len, uint32_t window, uint64_t timeout, uint64_t now)
{
    if (!a || slot >= AFTP_INBOX || !a->inbox[slot].used || !g || !bitmap) return VNA_ERR_ARG;
    aftp_pending_t *p = &a->inbox[slot];
    uint32_t n = vna_file_nchunks(p->file.size, p->file.chunk_size);
    if (bitmap_len < (n + 7u) / 8u || window == 0 || window > AFTP_WINDOW || timeout == 0)
        return VNA_ERR_ARG;
    if (logev(a, AFTP_EV_OFFER_TAKE, AP_TAG_T, &p->from, &p->file.root, p->file.size, 0, now) !=
        VNA_OK)
        return VNA_ERR_SPACE;
    vna_status_t st = aftp_get_start(a, g, &p->file.root, p->file.size, p->file.chunk_size, bitmap,
                                     bitmap_len, false, window, timeout, now);
    if (st != VNA_OK) return st;
    aftp_get_add_source(g, &p->from);
    p->used = false;
    return VNA_OK;
}

static uint32_t live_sources(const aftp_get_t *g)
{
    uint32_t k = 0;
    for (uint32_t i = 0; i < g->nsrc; i++) k += !g->src[i].dropped;
    return k;
}

static void drop_source(aftp_get_t *g, uint32_t j, int32_t why, uint64_t now)
{
    aftp_src_t *s = &g->src[j];
    s->dropped = true;
    for (uint32_t i = 0; i < AFTP_WINDOW; i++)
        if (g->win[i].used && g->win[i].src == j) g->win[i].used = false;
    s->inflight = 0;
    /* Dropping protects the transfer, so it happens even if the log is full;
     * the log counts the failure (ap_log_t.sink_failures). */
    (void) logev(g->a, AFTP_EV_SOURCE_DROP, AP_TAG_F, &s->id, &g->f.root, s->good, why, now);
}

static vna_status_t finish(aftp_get_t *g, uint64_t now)
{
    if (g->done || g->f.received != g->f.n) return VNA_OK;
    if (logev(g->a, AFTP_EV_GET_DONE, AP_TAG_T, 0, &g->f.root, g->f.size, 0, now) != VNA_OK)
        return VNA_ERR_SPACE;
    g->done = true;
    return VNA_OK;
}

int32_t aftp_get_next(aftp_get_t *g, uint64_t now, vna_id_t *to, uint8_t *out, uint32_t cap)
{
    if (!g || !to || !out) return VNA_ERR_ARG;
    if (g->done) return 0;
    if (g->f.received == g->f.n) return finish(g, now);
    uint32_t used = 0;
    for (uint32_t i = 0; i < AFTP_WINDOW; i++) { /* G4 */
        aftp_slot_t *w = &g->win[i];
        if (!w->used) continue;
        if (now >= w->sent + g->timeout) {
            aftp_src_t *s = &g->src[w->src];
            if (logev(g->a, AFTP_EV_TIMEOUT, AP_TAG_N, &s->id, &g->f.root, w->index, 0, now) !=
                VNA_OK)
                return VNA_ERR_SPACE;
            s->timeouts++;
            s->inflight--;
            w->used = false;
        } else
            used++;
    }
    if (!live_sources(g)) return VNA_ERR_STATE;
    if (used >= g->window) return 0;
    int32_t pick = -1;
    for (uint32_t k = 0; k < g->f.n && pick < 0; k++) {
        uint32_t i = g->cursor + k;
        if (i >= g->f.n) i -= g->f.n;
        if (bit(&g->f, i)) continue;
        bool asked = false;
        for (uint32_t w = 0; w < AFTP_WINDOW && !asked; w++)
            asked = g->win[w].used && g->win[w].index == i;
        if (!asked) pick = (int32_t) i;
    }
    if (pick < 0) return 0;
    int32_t best = -1;
    for (uint32_t k = 0; k < g->nsrc; k++) {
        uint32_t j = g->rr + k;
        if (j >= g->nsrc) j -= g->nsrc;
        const aftp_src_t *s = &g->src[j];
        if (s->dropped) continue;
        if (best < 0 || s->inflight < g->src[best].inflight) best = (int32_t) j; /* ties: rr */
    }
    vna_chunk_req_t rq = {VNA_CHUNKREQ_MAGIC, g->f.root, (uint32_t) pick};
    int32_t len = vna_schema_pack(&vna_chunk_req_schema, &rq, out, cap, true);
    if (len < 0) return VNA_ERR_SPACE;
    for (uint32_t i = 0; i < AFTP_WINDOW; i++)
        if (!g->win[i].used) {
            g->win[i].used = true;
            g->win[i].src = (uint8_t) best;
            g->win[i].index = (uint32_t) pick;
            g->win[i].sent = now;
            break;
        }
    g->src[best].inflight++;
    g->cursor = (uint32_t) pick + 1u >= g->f.n ? 0 : (uint32_t) pick + 1u;
    g->rr = (uint32_t) best + 1u >= g->nsrc ? 0 : (uint32_t) best + 1u;
    *to = g->src[best].id;
    return len;
}

vna_status_t aftp_get_on_chunk(aftp_get_t *g, const vna_id_t *from, const uint8_t *in, uint32_t len,
                               uint64_t now, vna_chunk_t *c)
{
    if (!g || !from || !in || !c) return VNA_ERR_ARG;
    if (g->done) return VNA_ERR_STATE;
    int32_t j = -1;
    for (uint32_t i = 0; i < g->nsrc && j < 0; i++)
        if (vna_id_eq(&g->src[i].id, from) && !g->src[i].dropped) j = (int32_t) i;
    if (j < 0) { /* G1: not one of our sources */
        g->refused++;
        (void) logev(g->a, AFTP_EV_CHUNK_IN, AP_TAG_F, from, &g->f.root, 0, VNA_ERR_UNEXPECTED,
                     now);
        return VNA_ERR_UNEXPECTED;
    }
    if (vna_schema_unpack(&vna_chunk_schema, in, len, c, 0) < 0 ||
        !vna_id_eq(&c->root, &g->f.root) || c->file_size != g->f.size ||
        c->chunk_size != g->f.chunk_size || c->index >= g->f.n) {
        g->refused++;
        drop_source(g, (uint32_t) j, VNA_ERR_PARSE, now); /* G3 */
        return VNA_ERR_PARSE;
    }
    int32_t w = -1;
    for (uint32_t i = 0; i < AFTP_WINDOW && w < 0; i++)
        if (g->win[i].used && g->win[i].src == (uint8_t) j && g->win[i].index == c->index)
            w = (int32_t) i;
    if (w < 0) {
        g->refused++;
        if (bit(&g->f, c->index)) return VNA_ERR_DUP;
        (void) logev(g->a, AFTP_EV_CHUNK_IN, AP_TAG_F, from, &g->f.root, c->index,
                     VNA_ERR_UNEXPECTED, now);
        return VNA_ERR_UNEXPECTED;
    }
    vna_status_t st = vna_file_verify_chunk(&g->f.root, g->f.size, g->f.chunk_size, c->index,
                                            c->data, c->data_len, c->proof, c->proof_n);
    if (st != VNA_OK) { /* G2, G3 */
        g->refused++;
        g->f.rejected++;
        drop_source(g, (uint32_t) j, st, now);
        return st;
    }
    if (logev(g->a, AFTP_EV_CHUNK_IN, AP_TAG_T, from, &g->f.root, c->data_len, 0, now) != VNA_OK)
        return VNA_ERR_SPACE; /* not counted; still asked, so a retry can land */
    g->f.bitmap[c->index >> 3] = (uint8_t) (g->f.bitmap[c->index >> 3] | (1u << (c->index & 7u)));
    g->f.received++;
    g->src[j].good++;
    g->src[j].inflight--;
    g->win[w].used = false;
    (void) finish(g, now); /* a refused completion entry is retried by aftp_get_next */
    return VNA_OK;
}

bool aftp_get_done(const aftp_get_t *g)
{
    return g && g->done;
}

/* ---- serving ---- */

int32_t aftp_serve(aftp_t *a, const vna_shared_file_t *sf, const vna_agreement_t *agr,
                   vna_usage_t *us, const vna_id_t *peer, uint64_t trust, const uint8_t *req,
                   uint32_t req_len, uint64_t now, vna_chunk_t *scratch, uint8_t *out, uint32_t cap)
{
    if (!a || !sf || !peer || !req || !scratch || !out) return VNA_ERR_ARG;
    vna_chunk_req_t rq;
    vna_id_t root;
    vna_zero(&root, sizeof root);
    if (vna_schema_unpack(&vna_chunk_req_schema, req, req_len, &rq, 0) >= 0) root = rq.root;
    int32_t r = vna_file_serve(sf, agr, us, peer, trust, req, req_len, now, scratch, out, cap);
    if (r < 0) {
        a->serve_refused++;
        (void) logev(a, AFTP_EV_SERVE, AP_TAG_F, peer, &root, 0, r, now);
        return r;
    }
    if (logev(a, AFTP_EV_SERVE, AP_TAG_T, peer, &root, scratch->data_len, 0, now) != VNA_OK)
        return VNA_ERR_SPACE; /* S1: no audit, no chunk */
    a->served++;
    return r;
}
