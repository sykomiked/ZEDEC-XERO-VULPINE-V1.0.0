/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* vna_exchange.c — stateless 168-byte UBH-168 Epistemic Exchange order book,
 * 3-leaf spot audit, and octane standing gate; see vna_exchange.h. */

#include "vna_exchange.h"
#include "../robin_debanks/sha256.h"

static void put_be16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t) (v >> 8);
    p[1] = (uint8_t) v;
}

static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t) (v >> 24);
    p[1] = (uint8_t) (v >> 16);
    p[2] = (uint8_t) (v >> 8);
    p[3] = (uint8_t) v;
}

static void put_be64(uint8_t *p, uint64_t v)
{
    put_be32(p, (uint32_t) (v >> 32));
    put_be32(p + 4, (uint32_t) v);
}

static uint16_t get_be16(const uint8_t *p)
{
    return (uint16_t) (((uint16_t) p[0] << 8) | p[1]);
}

static uint32_t get_be32(const uint8_t *p)
{
    return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | p[3];
}

static uint64_t get_be64(const uint8_t *p)
{
    return ((uint64_t) get_be32(p) << 32) | get_be32(p + 4);
}

static bool valid_octane(uint8_t o)
{
    return o == SWARM_OCTANE_PLAIN || o == SWARM_OCTANE_REGULAR || o == SWARM_OCTANE_MIDGRADE ||
           o == SWARM_OCTANE_PREMIUM || o == SWARM_OCTANE_RACING || o == SWARM_OCTANE_MAX;
}

/* True iff `f` is one of the five priceable forms (in range, not a Crown
 * form): the inalienability wall that vna_book_conserved also enforces. */
static bool priceable_form(uint8_t f)
{
    return f < (uint8_t) SWARM_CAP_COUNT && !swarm_cap_is_crown((swarm_cap_t) f);
}

vna_status_t vna_exch_pack(const vna_exch_rec_t *r, uint8_t out[VNA_EXCH_ENV_BYTES])
{
    ubh_168_header_t h;
    uint8_t d[SHA256_DIGEST_LEN];
    uint8_t *pl;
    uint32_t i;

    if (!r || !out) return VNA_ERR_ARG;
    if (r->kind != VNA_EXCH_T_LISTING && r->kind != VNA_EXCH_T_RECEIPT) return VNA_ERR_ARG;
    if (!priceable_form(r->form)) return VNA_ERR_INALIENABLE;
    if (!valid_octane(r->octane) || r->lpx_tag >= LPX_STATES) return VNA_ERR_ARG;

    ubh_168_header_init(&h, UBH_FRAME_CONTENT_OBJECT);
    h.source_format_id = UBH_FMT_UBH_168;
    h.target_type = r->kind;
    h.flags = 0x05u; /* has_payload | is_canonical */
    h.payload_length = VNA_EXCH_PAYLOAD_BYTES;
    h.schema_id = VNA_EXCH_SCHEMA;
    for (i = 0; i < 3; i++) h.integrity_ref[i] = r->root_digest[i];
    ubh_168_header_pack(&h, out);

    pl = out + VNA_EXCH_FRAME_BYTES;
    vna_copy(pl + 0, r->root_digest, 32);
    vna_copy(pl + 32, r->provider.b, 32);
    vna_copy(pl + 64, r->witness, 32);
    put_be64(pl + 96, r->file_size);
    put_be32(pl + 104, r->n_chunks);
    put_be16(pl + 108, r->spot_idx[0]);
    put_be16(pl + 110, r->spot_idx[1]);
    put_be16(pl + 112, r->spot_idx[2]);
    vna_copy(pl + 114, r->addr, 6);
    pl[120] = r->form;
    pl[121] = r->octane;
    pl[122] = r->lpx_tag;
    pl[123] = r->armada;
    put_be16(pl + 124, r->domain);

    sha256(out, VNA_EXCH_FRAME_BYTES + VNA_EXCH_PAYLOAD_BYTES, d);
    ubh_168_header_init(&h, UBH_FRAME_INTEGRITY);
    h.source_format_id = UBH_FMT_UBH_168;
    h.target_type = r->kind;
    h.payload_length = VNA_EXCH_PAYLOAD_BYTES;
    h.schema_id = VNA_EXCH_PAYLOAD_BYTES / VNA_EXCH_FRAME_BYTES; /* 6 frames */
    for (i = 0; i < 3; i++) h.integrity_ref[i] = d[i];
    ubh_168_header_pack(&h, out + VNA_EXCH_FRAME_BYTES + VNA_EXCH_PAYLOAD_BYTES);
    return VNA_OK;
}

vna_status_t vna_exch_unpack(const uint8_t in[VNA_EXCH_ENV_BYTES], vna_exch_rec_t *out)
{
    ubh_168_header_t h, ref, t;
    uint8_t d[SHA256_DIGEST_LEN];
    const uint8_t *pl;

    if (!in || !out) return VNA_ERR_ARG;
    ubh_168_header_init(&ref, UBH_FRAME_CONTENT_OBJECT);
    if (!ubh_168_header_unpack(&h, in)) return VNA_ERR_PARSE;
    if (h.version != ref.version || h.frame_class != UBH_FRAME_CONTENT_OBJECT ||
        h.source_format_id != UBH_FMT_UBH_168 ||
        (h.target_type != VNA_EXCH_T_LISTING && h.target_type != VNA_EXCH_T_RECEIPT) ||
        h.order_flags != ref.order_flags || h.flags != 0x05u ||
        h.payload_length != VNA_EXCH_PAYLOAD_BYTES || h.schema_id != VNA_EXCH_SCHEMA)
        return VNA_ERR_PARSE;

    if (!ubh_168_header_unpack(&t, in + VNA_EXCH_FRAME_BYTES + VNA_EXCH_PAYLOAD_BYTES))
        return VNA_ERR_PARSE;
    if (t.version != ref.version || t.frame_class != UBH_FRAME_INTEGRITY ||
        t.source_format_id != UBH_FMT_UBH_168 || t.target_type != h.target_type ||
        t.order_flags != ref.order_flags || t.flags != ref.flags ||
        t.payload_length != VNA_EXCH_PAYLOAD_BYTES ||
        t.schema_id != VNA_EXCH_PAYLOAD_BYTES / VNA_EXCH_FRAME_BYTES)
        return VNA_ERR_PARSE;

    sha256(in, VNA_EXCH_FRAME_BYTES + VNA_EXCH_PAYLOAD_BYTES, d);
    if (!vna_ct_eq(t.integrity_ref, d, 3)) return VNA_ERR_PARSE;

    pl = in + VNA_EXCH_FRAME_BYTES;
    if (!vna_ct_eq(h.integrity_ref, pl, 3)) return VNA_ERR_PARSE;
    if (!priceable_form(pl[120])) return VNA_ERR_INALIENABLE;
    if (!valid_octane(pl[121]) || pl[122] >= LPX_STATES) return VNA_ERR_PARSE;

    vna_zero(out, sizeof *out);
    out->kind = h.target_type;
    vna_copy(out->root_digest, pl + 0, 32);
    vna_copy(out->provider.b, pl + 32, 32);
    vna_copy(out->witness, pl + 64, 32);
    out->file_size = get_be64(pl + 96);
    out->n_chunks = get_be32(pl + 104);
    out->spot_idx[0] = get_be16(pl + 108);
    out->spot_idx[1] = get_be16(pl + 110);
    out->spot_idx[2] = get_be16(pl + 112);
    vna_copy(out->addr, pl + 114, 6);
    out->form = pl[120];
    out->octane = pl[121];
    out->lpx_tag = pl[122];
    out->armada = pl[123];
    out->domain = get_be16(pl + 124);
    return VNA_OK;
}

void vna_exch_pick_leaves(const uint8_t root_digest[32], uint8_t shard, uint32_t n_chunks,
                          uint16_t out_idx[3])
{
    uint8_t buf[34], d[SHA256_DIGEST_LEN];
    uint32_t span = n_chunks ? (n_chunks > 65535u ? 65535u : n_chunks) : 1u;
    uint32_t i;

    vna_copy(buf, root_digest, 32);
    buf[32] = shard;
    for (i = 0; i < 3; i++) {
        buf[33] = (uint8_t) i;
        sha256(buf, sizeof buf, d);
        out_idx[i] = (uint16_t) (get_be32(d) % span);
    }
}

void vna_exch_init(vna_exchange_t *x, vna_book_t *book, const uint8_t curriculum_digest[32],
                   uint8_t local_shard)
{
    if (!x) return;
    vna_zero(x, sizeof *x);
    x->book = book;
    if (curriculum_digest) vna_copy(x->curriculum_digest, curriculum_digest, 32);
    x->local_shard = (uint8_t) (local_shard & 7u);
    lpx_qlog_init(&x->qlog, x->qrec, VNA_EXCH_QLOG_CAP);
}

static vna_exch_standing_t *peer_slot(vna_exchange_t *x, const vna_id_t *peer, bool create)
{
    uint32_t i, free_i = VNA_EXCH_BOOK_CAP;
    for (i = 0; i < VNA_EXCH_BOOK_CAP; i++) {
        if (x->peers[i].used && vna_id_eq(&x->peers[i].peer, peer)) return &x->peers[i];
        if (!x->peers[i].used && free_i == VNA_EXCH_BOOK_CAP) free_i = i;
    }
    if (!create || free_i == VNA_EXCH_BOOK_CAP) return 0;
    x->peers[free_i].used = true;
    x->peers[free_i].peer = *peer;
    x->peers[free_i].octane = SWARM_OCTANE_PLAIN;
    return &x->peers[free_i];
}

uint8_t vna_exch_peer_octane(const vna_exchange_t *x, const vna_id_t *peer)
{
    uint32_t i;
    if (!x || !peer) return SWARM_OCTANE_PLAIN;
    for (i = 0; i < VNA_EXCH_BOOK_CAP; i++)
        if (x->peers[i].used && vna_id_eq(&x->peers[i].peer, peer)) return x->peers[i].octane;
    return SWARM_OCTANE_PLAIN;
}

vna_status_t vna_exch_verify_curriculum_pin(vna_exchange_t *x, const vna_id_t *peer,
                                            const uint8_t announced_digest[32],
                                            const vna_provider_t *pv)
{
    vna_exch_standing_t *st;
    vna_account_t *ac;

    if (!x || !peer || !announced_digest || !pv) return VNA_ERR_ARG;
    if (pv->visibility != VNA_VIS_PUBLIC || pv->file_size == 0 || pv->n_chunks == 0 ||
        pv->addr_len == 0)
        return VNA_ERR_DENIED;
    if (!vna_ct_eq(announced_digest, x->curriculum_digest, 32)) return VNA_ERR_DENIED;

    st = peer_slot(x, peer, true);
    if (!st) return VNA_ERR_SPACE;
    if (!st->curriculum_pin) {
        st->curriculum_pin = 1;
        if (st->octane < SWARM_OCTANE_MIDGRADE) {
            st->octane = SWARM_OCTANE_MIDGRADE;
            x->upgraded++;
        }
        /* Social is a Crown form: earned, never held in a token balance
         * (vna_book_conserved requires the Crown rows to stay zero). */
        if (x->book && (ac = vna_book_find(x->book, peer)) != 0) ac->social += 1;
    }
    return VNA_OK;
}

static vna_exch_slot_t *find_listing(vna_exchange_t *x, const uint8_t root_digest[32], bool create)
{
    uint32_t i, free_i = VNA_EXCH_BOOK_CAP;
    for (i = 0; i < VNA_EXCH_BOOK_CAP; i++) {
        if (x->slots[i].used && vna_ct_eq(x->slots[i].rec.root_digest, root_digest, 32))
            return &x->slots[i];
        if (!x->slots[i].used && free_i == VNA_EXCH_BOOK_CAP) free_i = i;
    }
    if (!create || free_i == VNA_EXCH_BOOK_CAP) return 0;
    x->slots[free_i].used = true;
    return &x->slots[free_i];
}

vna_status_t vna_exch_spot_audit(vna_exchange_t *x, vna_exch_rec_t *offer,
                                 const uint8_t *const leaf_data[3], const uint32_t leaf_len[3],
                                 const uint8_t want_leaf_sha[3][32])
{
    lpx_t tag;
    sha256_ctx_t comb;
    uint8_t leaf_sha[32];
    uint64_t sum_len = 0;
    vna_exch_slot_t *sl;
    vna_exch_standing_t *st;
    uint32_t i;

    if (!x || !offer || !leaf_data || !leaf_len || !want_leaf_sha) return VNA_ERR_ARG;
    if (!priceable_form(offer->form)) return VNA_ERR_INALIENABLE;

    /* 1. Start at NEITHER / NEUTRAL / POTENTIAL and step to EMERGING (X3). */
    tag = lpx_make(LPX_B_NEITHER, LPX_P_NEUTRAL, LPX_PH_POTENTIAL);
    if (!lpx_advance(&tag, LPX_PH_EMERGING)) return VNA_ERR_PARSE;

    /* 2. Check bounds and the 3 spot-audited leaves in streaming order. */
    if (offer->file_size == 0 || offer->n_chunks == 0) {
        tag = lpx_forbid(tag);
        (void) lpx_qlog_append(&x->qlog, tag, 1u, offer->provider.b[0], offer->root_digest);
        x->quarantined++;
        offer->lpx_tag = tag;
        return VNA_ERR_PARSE;
    }

    vna_exch_pick_leaves(offer->root_digest, x->local_shard, offer->n_chunks, offer->spot_idx);
    sha256_init(&comb);
    for (i = 0; i < 3; i++) {
        if (!leaf_data[i] || leaf_len[i] == 0 || leaf_len[i] > VNA_EXCH_LEAF_MAX) {
            tag = lpx_forbid(tag);
            (void) lpx_qlog_append(&x->qlog, tag, 2u, offer->provider.b[0], offer->root_digest);
            x->quarantined++;
            offer->lpx_tag = tag;
            return VNA_ERR_PARSE;
        }
        sum_len += leaf_len[i];
        sha256(leaf_data[i], leaf_len[i], leaf_sha);
        if (!vna_ct_eq(leaf_sha, want_leaf_sha[i], 32)) {
            tag = lpx_forbid(tag);
            (void) lpx_qlog_append(&x->qlog, tag, 3u, offer->provider.b[0], offer->root_digest);
            x->quarantined++;
            offer->lpx_tag = tag;
            return VNA_ERR_SIG;
        }
        sha256_update(&comb, leaf_sha, 32);
    }
    if (offer->n_chunks == 1 && leaf_len[0] != offer->file_size) {
        tag = lpx_forbid(tag);
        (void) lpx_qlog_append(&x->qlog, tag, 4u, offer->provider.b[0], offer->root_digest);
        x->quarantined++;
        offer->lpx_tag = tag;
        return VNA_ERR_PARSE;
    }
    (void) sum_len;
    sha256_final(&comb, offer->witness);

    /* 3. Advance EMERGING -> MANIFEST with TRUE + NEGENTROPIC -> PROCEED. */
    if (!lpx_advance(&tag, LPX_PH_MANIFEST)) return VNA_ERR_PARSE;
    tag = lpx_set(tag, LPX_B_TRUE, LPX_P_NEGENTROPIC);
    if (lpx_action(tag) != LPX_ACT_PROCEED) return VNA_ERR_DENIED;

    offer->kind = VNA_EXCH_T_LISTING;
    offer->lpx_tag = tag;
    offer->armada |= (uint8_t) (1u << (x->local_shard & 7u));
    if (offer->octane < SWARM_OCTANE_PREMIUM)
        offer->octane =
            (offer->form == SWARM_CAP_INTELLECTUAL) ? SWARM_OCTANE_RACING : SWARM_OCTANE_PREMIUM;

    sl = find_listing(x, offer->root_digest, true);
    if (!sl) return VNA_ERR_SPACE;
    sl->rec = *offer;
    sl->pos_ev++;
    if (vna_exch_pack(&sl->rec, sl->env) != VNA_OK) return VNA_ERR_PARSE;

    st = peer_slot(x, &offer->provider, true);
    if (st) {
        st->listings_ok++;
        if (st->octane < offer->octane) {
            st->octane = offer->octane;
            x->upgraded++;
        }
    }
    x->listed++;
    x->audited_ok++;
    return VNA_OK;
}

vna_status_t vna_exch_settle_receipt(vna_exchange_t *x, const uint8_t root_digest[32],
                                     const vna_id_t *buyer, lpres_state_t seller_obs,
                                     lpres_state_t buyer_obs, uint64_t units)
{
    vna_exch_slot_t *sl;
    lpres_state_t joined;
    uint8_t pol, base;
    vna_account_t *ac;
    vna_exch_standing_t *st;

    if (!x || !root_digest || !buyer || units == 0) return VNA_ERR_ARG;
    sl = find_listing(x, root_digest, false);
    if (!sl) return VNA_ERR_NOTFOUND;

    joined = lpres_kjoin(seller_obs, buyer_obs);
    base = lpx_base_from_lpres(joined);
    if (joined == LPRES_STATE_TRUE) {
        pol = lpx_polarity(sl->pos_ev, sl->neg_ev, sl->pos_ev + 1u, sl->neg_ev, false);
        sl->pos_ev++;
    } else if (joined == LPRES_STATE_FALSE || joined == LPRES_STATE_BOTH) {
        pol = lpx_polarity(sl->pos_ev, sl->neg_ev, sl->pos_ev, sl->neg_ev + 1u, false);
        sl->neg_ev++;
    } else {
        pol = LPX_P_NEUTRAL;
    }
    sl->rec.lpx_tag = lpx_set(sl->rec.lpx_tag, base, pol);
    (void) vna_exch_pack(&sl->rec, sl->env);

    if (lpx_action(sl->rec.lpx_tag) != LPX_ACT_PROCEED) return VNA_ERR_DENIED;

    if (x->book && (ac = vna_book_find(x->book, &sl->rec.provider)) != 0) {
        ac->contrib[sl->rec.form] += units;
        ac->contrib_total += units;
        ac->social += 1;
    }
    st = peer_slot(x, &sl->rec.provider, true);
    if (st) st->settlements_ok++;
    x->settled++;
    return VNA_OK;
}

vna_status_t vna_exch_match(const vna_exchange_t *x, const vna_id_t *caller,
                            const uint8_t root_digest[32], uint8_t out_env[VNA_EXCH_ENV_BYTES])
{
    uint32_t i;
    uint8_t caller_oct;
    if (!x || !caller || !root_digest || !out_env) return VNA_ERR_ARG;
    caller_oct = vna_exch_peer_octane(x, caller);
    for (i = 0; i < VNA_EXCH_BOOK_CAP; i++) {
        if (!x->slots[i].used) continue;
        if (!vna_ct_eq(x->slots[i].rec.root_digest, root_digest, 32)) continue;
        if (lpx_action(x->slots[i].rec.lpx_tag) != LPX_ACT_PROCEED) return VNA_ERR_DENIED;
        /* Ring 1/2 access requires at least MIDGRADE (9) standing (achieved by
         * pinning zxv-curriculum.car or contributing a verified .car). */
        if (caller_oct < SWARM_OCTANE_MIDGRADE) return VNA_ERR_DENIED;
        vna_copy(out_env, x->slots[i].env, VNA_EXCH_ENV_BYTES);
        return VNA_OK;
    }
    return VNA_ERR_NOTFOUND;
}
