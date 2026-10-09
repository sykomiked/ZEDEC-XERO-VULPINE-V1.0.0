/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* cardnet.c — charge-card issuer for Dragon / Phoenix / Thunderbird.
 * See cardnet.h for the model and its honest limits. */
#include "cardnet.h"
#include "cn_util.h"
#include "cn_pq.h"
#include "../mlkem/keccak.h"

#define NONE_IDX 0xFFFFFFFFu

/* ===== Networks ===== */

static const char *const net_names[CN_NET_COUNT] = {"Dragon", "Phoenix", "Thunderbird"};
static const char *const net_prefixes[CN_NET_COUNT] = {"880", "882", "884"};

const char *cn_network_name(cn_network_t net)
{
    return ((uint32_t) net < CN_NET_COUNT) ? net_names[net] : "none";
}

const char *cn_network_prefix(cn_network_t net)
{
    return ((uint32_t) net < CN_NET_COUNT) ? net_prefixes[net] : "";
}

int cn_network_check_digit(cn_network_t net, const char *digits, uint32_t len)
{
    switch (net) {
    case CN_NET_DRAGON:
        return cn_luhn_digit(digits, len);
    case CN_NET_PHOENIX:
        return cn_damm_digit(digits, len);
    case CN_NET_THUNDERBIRD:
        return cn_verhoeff_digit(digits, len);
    default:
        return -1;
    }
}

bool cn_network_check_valid(cn_network_t net, const char *digits, uint32_t len)
{
    switch (net) {
    case CN_NET_DRAGON:
        return cn_luhn_valid(digits, len);
    case CN_NET_PHOENIX:
        return cn_damm_valid(digits, len);
    case CN_NET_THUNDERBIRD:
        return cn_verhoeff_valid(digits, len);
    default:
        return false;
    }
}

bool cn_pan_valid(cn_network_t net, const char *pan, uint32_t len)
{
    if ((uint32_t) net >= CN_NET_COUNT || !pan || len != CN_PAN_LEN) return false;
    if (!cn_all_digits(pan, len)) return false;
    if (!cn_eq(pan, net_prefixes[net], CN_PREFIX_LEN)) return false;
    for (uint32_t n = 0; n < CN_NET_COUNT; n++) {
        bool ok = cn_network_check_valid((cn_network_t) n, pan, len);
        if (n == (uint32_t) net ? !ok : ok) return false;
    }
    return true;
}

cn_network_t cn_pan_network(const char *pan, uint32_t len)
{
    for (uint32_t n = 0; n < CN_NET_COUNT; n++)
        if (cn_pan_valid((cn_network_t) n, pan, len)) return (cn_network_t) n;
    return CN_NET_NONE;
}

/* ===== Fees ===== */

bool cn_fee_validate(const cn_fee_t *fee)
{
    if (!fee) return false;
    if (fee->kind == CN_FEE_NONE) return fee->amount_minor == 0;
    if (fee->kind == CN_FEE_FLAT_PER_TXN) return fee->amount_minor <= CN_FEE_MAX_MINOR;
    return false;
}

uint64_t cn_fee_for_charge(const cn_fee_t *fee, uint64_t charge_minor)
{
    if (!cn_fee_validate(fee) || fee->kind != CN_FEE_FLAT_PER_TXN) return 0;
    return (fee->amount_minor < charge_minor) ? fee->amount_minor : 0;
}

/* ===== Configuration ===== */

void cn_cfg_default(cn_issuer_cfg_t *cfg, cn_network_t net)
{
    if (!cfg) return;
    cn_zero(cfg, (uint32_t) sizeof(*cfg));
    cfg->network = (uint32_t) net;
    cfg->issuer_code[0] = '0';
    cfg->issuer_code[1] = '0';
    cfg->issuer_code[2] = '1';
    cfg->validity_months = 36;
    cfg->cycle_days = 30;
    cfg->grace_days = 21;
    for (uint32_t f = CN_FORM_FINANCIAL; f < CN_FORM_COUNT; f++) {
        cfg->per_txn_limit[f] = 1000000ull;
        cfg->per_cycle_limit[f] = 5000000ull;
    }
    cfg->account_ceiling = 10000000ull;
    cfg->merchant_fee.kind = CN_FEE_NONE;
    cfg->merchant_fee.amount_minor = 0;
}

cn_rc_t cn_cfg_validate(const cn_issuer_cfg_t *cfg)
{
    if (!cfg) return CN_ERR_ARG;
    if (cfg->network >= CN_NET_COUNT) return CN_ERR_CONFIG;
    if (cn_strnlen(cfg->issuer_code, CN_ISSUER_CODE_LEN + 1) != CN_ISSUER_CODE_LEN ||
        !cn_all_digits(cfg->issuer_code, CN_ISSUER_CODE_LEN))
        return CN_ERR_CONFIG;
    if (cfg->validity_months < 1 || cfg->validity_months > 120) return CN_ERR_CONFIG;
    if (cfg->cycle_days < 7 || cfg->cycle_days > 62) return CN_ERR_CONFIG;
    if (cfg->grace_days < 1 || cfg->grace_days > 62) return CN_ERR_CONFIG;
    if (!cn_fee_validate(&cfg->merchant_fee)) return CN_ERR_CONFIG;
    if (cfg->account_ceiling == 0 || cfg->account_ceiling > CN_AMOUNT_MAX_MINOR)
        return CN_ERR_CONFIG;
    for (uint32_t f = 0; f < CN_FORM_COUNT; f++) {
        uint64_t t = cfg->per_txn_limit[f], c = cfg->per_cycle_limit[f];
        if (!cn_form_priceable(f)) {
            /* Inalienable forms are not for sale: no card may spend them. */
            if (t != 0 || c != 0) return CN_ERR_CONFIG;
            continue;
        }
        if (t > c || c > cfg->account_ceiling) return CN_ERR_CONFIG;
    }
    return CN_OK;
}

/* ===== Calendar (days since 2000-01-01; 32-bit arithmetic only) ===== */

void cn_civil_from_time(cn_time_t t, cn_civil_t *out)
{
    if (!out) return;
    uint32_t days = t / CN_SECS_PER_DAY, sod = t % CN_SECS_PER_DAY;
    /* H. Hinnant's civil_from_days, shifted so day 0 is 2000-01-01. */
    uint32_t z = days + 730425u; /* days from 0000-03-01 */
    uint32_t era = z / 146097u;
    uint32_t doe = z - era * 146097u;
    uint32_t yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
    uint32_t doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
    uint32_t mp = (5u * doy + 2u) / 153u;
    uint32_t d = doy - (153u * mp + 2u) / 5u + 1u;
    uint32_t m = mp < 10u ? mp + 3u : mp - 9u;
    uint32_t y = yoe + era * 400u + (m <= 2u ? 1u : 0u);
    out->year = y;
    out->month = m;
    out->day = d;
    out->hour = sod / 3600u;
    out->minute = (sod % 3600u) / 60u;
    out->second = sod % 60u;
}

static bool is_leap(uint32_t y)
{
    return (y % 4u == 0 && y % 100u != 0) || y % 400u == 0;
}

bool cn_time_from_civil(const cn_civil_t *c, cn_time_t *out)
{
    static const uint8_t mdays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (!c || !out) return false;
    if (c->year < 2000 || c->year > 2135 || c->month < 1 || c->month > 12) return false;
    uint32_t dim = mdays[c->month - 1] + ((c->month == 2 && is_leap(c->year)) ? 1u : 0u);
    if (c->day < 1 || c->day > dim || c->hour > 23 || c->minute > 59 || c->second > 59)
        return false;
    /* days_from_civil, same shift as above. */
    uint32_t y = c->year - (c->month <= 2 ? 1u : 0u);
    uint32_t era = y / 400u;
    uint32_t yoe = y - era * 400u;
    uint32_t mp = c->month > 2 ? c->month - 3u : c->month + 9u;
    uint32_t doy = (153u * mp + 2u) / 5u + c->day - 1u;
    uint32_t doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    uint32_t days = era * 146097u + doe - 730425u;
    if (days > 0xFFFFFFFFu / CN_SECS_PER_DAY) return false;
    uint32_t t = days * CN_SECS_PER_DAY;
    uint32_t sod = c->hour * 3600u + c->minute * 60u + c->second;
    if (t > 0xFFFFFFFFu - sod) return false;
    *out = t + sod;
    return true;
}

/* ===== Serialization helpers ===== */

typedef struct {
    uint8_t *buf;
    uint32_t cap, len;
    bool err;
} bw_t;

static void bw_bytes(bw_t *w, const void *p, uint32_t n)
{
    if (w->err || n > w->cap - w->len) {
        w->err = true;
        return;
    }
    cn_copy(w->buf + w->len, p, n);
    w->len += n;
}

static void bw_u8(bw_t *w, uint32_t v)
{
    uint8_t b = (uint8_t) v;
    bw_bytes(w, &b, 1);
}

static void bw_u16(bw_t *w, uint32_t v)
{
    uint8_t b[2] = {(uint8_t) (v >> 8), (uint8_t) v};
    bw_bytes(w, b, 2);
}

static void bw_u32(bw_t *w, uint32_t v)
{
    uint8_t b[4] = {(uint8_t) (v >> 24), (uint8_t) (v >> 16), (uint8_t) (v >> 8), (uint8_t) v};
    bw_bytes(w, b, 4);
}

static void bw_u64(bw_t *w, uint64_t v)
{
    bw_u32(w, (uint32_t) (v >> 32));
    bw_u32(w, (uint32_t) v);
}

static bool printable_exact(const char *s, uint32_t n)
{
    if (cn_strnlen(s, n + 1) != n) return false;
    for (uint32_t i = 0; i < n; i++)
        if (s[i] < 0x20 || s[i] > 0x7E) return false;
    return true;
}

static bool req_well_formed(const cn_auth_req_t *r)
{
    if (!r) return false;
    if (cn_strnlen(r->pan, CN_PAN_LEN + 1) != CN_PAN_LEN || !cn_all_digits(r->pan, CN_PAN_LEN))
        return false;
    if (r->amount_minor == 0 || r->amount_minor > CN_AMOUNT_MAX_MINOR) return false;
    if (r->atc == 0 || r->atc > 0xFFFFu) return false;
    if (r->stan > 999999u || r->currency > 999u || r->form >= CN_FORM_COUNT) return false;
    return printable_exact(r->terminal_id, CN_TID_LEN) &&
           printable_exact(r->merchant_id, CN_MID_LEN);
}

cn_rc_t cn_challenge(const cn_auth_req_t *req, uint8_t out[CN_CHALLENGE_MAX], uint32_t *out_len)
{
    if (!out || !out_len || !req_well_formed(req)) return CN_ERR_ARG;
    bw_t w = {out, CN_CHALLENGE_MAX, 0, false};
    bw_bytes(&w, CN_SIG_CTX, CN_SIG_CTX_LEN);
    bw_u8(&w, (uint32_t) cn_pan_network(req->pan, CN_PAN_LEN));
    bw_bytes(&w, req->pan, CN_PAN_LEN);
    bw_u16(&w, req->atc);
    bw_u64(&w, req->amount_minor);
    bw_u16(&w, req->currency);
    bw_u8(&w, req->form);
    bw_bytes(&w, req->un, 4);
    bw_u32(&w, req->time);
    bw_u32(&w, req->stan);
    bw_bytes(&w, req->terminal_id, CN_TID_LEN);
    bw_bytes(&w, req->merchant_id, CN_MID_LEN);
    if (w.err) return CN_ERR_ARG;
    *out_len = w.len;
    return CN_OK;
}

cn_rc_t cn_holder_sign(const uint8_t sk[CN_SK_BYTES], const cn_auth_req_t *req,
                       const uint8_t *rnd32, uint8_t sig[CN_SIG_BYTES])
{
    uint8_t ch[CN_CHALLENGE_MAX];
    uint32_t n = 0;
    if (!sk || !sig) return CN_ERR_ARG;
    cn_rc_t rc = cn_challenge(req, ch, &n);
    if (rc != CN_OK) return rc;
    pq_mldsa65_sign(sk, ch, n, (const uint8_t *) CN_SIG_CTX, CN_SIG_CTX_LEN, rnd32, sig);
    return CN_OK;
}

void cn_cryptogram(const uint8_t sig[CN_SIG_BYTES], uint8_t out[8])
{
    uint8_t h[32];
    if (!out) return;
    if (!sig) {
        cn_zero(out, 8);
        return;
    }
    sha3_256(sig, CN_SIG_BYTES, h);
    cn_copy(out, h, 8);
}

const char *cn_decline_rc(cn_decline_t d)
{
    switch (d) {
    case CN_DECL_NONE:
        return "00";
    case CN_DECL_BAD_PAN:
    case CN_DECL_UNKNOWN_CARD:
    case CN_DECL_REVOKED:
        return "14";
    case CN_DECL_FROZEN:
        return "62";
    case CN_DECL_EXPIRED:
        return "54";
    case CN_DECL_BAD_SIG:
    case CN_DECL_DELINQUENT:
        return "05";
    case CN_DECL_REPLAY:
        return "94";
    case CN_DECL_FORM:
    case CN_DECL_CURRENCY:
        return "57";
    case CN_DECL_TXN_LIMIT:
        return "61";
    case CN_DECL_CYCLE_LIMIT:
    case CN_DECL_CEILING:
        return "51";
    case CN_DECL_FORMAT:
        return "30";
    default:
        return "96";
    }
}

/* ===== Issuer ===== */

static uint32_t day_of(cn_time_t t)
{
    return t / CN_SECS_PER_DAY;
}

cn_rc_t cn_issuer_init(cn_issuer_t *iss, const cn_issuer_cfg_t *cfg)
{
    if (!iss || !cfg) return CN_ERR_ARG;
    cn_rc_t rc = cn_cfg_validate(cfg);
    if (rc != CN_OK) return rc;
    cn_zero(iss, (uint32_t) sizeof(*iss));
    cn_copy(&iss->cfg, cfg, (uint32_t) sizeof(*cfg));
    return CN_OK;
}

cn_rc_t cn_open_account(cn_issuer_t *iss, cn_time_t now, uint32_t *out_account)
{
    if (!iss || !out_account) return CN_ERR_ARG;
    for (uint32_t i = 0; i < CN_MAX_ACCOUNTS; i++) {
        cn_account_t *a = &iss->accounts[i];
        if (a->used) continue;
        cn_zero(a, (uint32_t) sizeof(*a));
        a->used = true;
        a->state = CN_ACCT_ACTIVE;
        a->cycle_start_day = day_of(now);
        *out_account = i;
        return CN_OK;
    }
    return CN_ERR_FULL;
}

static bool pan_in_table(const cn_issuer_t *iss, const char *pan)
{
    for (uint32_t i = 0; i < CN_MAX_CARDS; i++)
        if (iss->cards[i].used && cn_eq(iss->cards[i].pan, pan, CN_PAN_LEN)) return true;
    return false;
}

/* Derive a PAN for (account, generation). Pure: writes only `pan`. */
static cn_rc_t mint_pan(const cn_issuer_t *iss, uint32_t account, uint32_t generation,
                        const uint8_t entropy[32], char pan[CN_PAN_LEN + 1])
{
    cn_network_t net = (cn_network_t) iss->cfg.network;
    for (uint32_t attempt = 0; attempt < 64u; attempt++) {
        uint8_t in[18 + 1 + 32 + 32 + 12];
        uint8_t xof[48];
        bw_t w = {in, (uint32_t) sizeof(in), 0, false};
        bw_bytes(&w, "ZXV-CARDNET-PAN-v1", 18);
        bw_u8(&w, (uint32_t) net);
        bw_bytes(&w, iss->cfg.issuer_seed, 32);
        bw_bytes(&w, entropy, 32);
        bw_u32(&w, account);
        bw_u32(&w, generation);
        bw_u32(&w, attempt);
        if (w.err) return CN_ERR_ARG;
        shake256(in, w.len, xof, sizeof(xof));

        char body[CN_PAN_LEN + 1];
        cn_copy(body, net_prefixes[net], CN_PREFIX_LEN);
        cn_copy(body + CN_PREFIX_LEN, iss->cfg.issuer_code, CN_ISSUER_CODE_LEN);
        uint32_t k = CN_PREFIX_LEN + CN_ISSUER_CODE_LEN;
        /* Unbiased digits: accept bytes < 250, take them mod 10. */
        for (uint32_t i = 0; i < sizeof(xof) && k < CN_PAN_LEN - 1; i++)
            if (xof[i] < 250u) body[k++] = (char) ('0' + xof[i] % 10u);
        if (k != CN_PAN_LEN - 1) continue;
        int cd = cn_network_check_digit(net, body, CN_PAN_LEN - 1);
        if (cd < 0) return CN_ERR_ARG;
        body[CN_PAN_LEN - 1] = (char) ('0' + cd);
        body[CN_PAN_LEN] = 0;
        /* C5 exclusivity: skip a candidate that also passes a foreign check. */
        if (!cn_pan_valid(net, body, CN_PAN_LEN)) continue;
        if (pan_in_table(iss, body)) continue;
        cn_copy(pan, body, CN_PAN_LEN + 1);
        return CN_OK;
    }
    return CN_ERR_MINT;
}

static void expiry_for(const cn_issuer_t *iss, cn_time_t now, uint32_t *y, uint32_t *m)
{
    cn_civil_t c;
    cn_civil_from_time(now, &c);
    uint32_t months = c.year * 12u + (c.month - 1u) + iss->cfg.validity_months;
    *y = months / 12u;
    *m = months % 12u + 1u;
}

static cn_rc_t build_card(const cn_issuer_t *iss, uint32_t account, uint32_t form,
                          const uint8_t *pk, const uint8_t entropy[32], cn_time_t now,
                          uint32_t replaces, cn_card_t *c)
{
    cn_rc_t rc = mint_pan(iss, account, iss->accounts[account].generation, entropy, c->pan);
    if (rc != CN_OK) return rc;
    c->used = true;
    c->state = CN_CARD_ACTIVE;
    c->account = account;
    c->form = form;
    expiry_for(iss, now, &c->exp_year, &c->exp_month);
    c->rail_debit = CN_RAIL_DEBIT;
    c->rail_credit = CN_RAIL_CREDIT;
    c->rail_equity = CN_RAIL_EQUITY;
    c->atc = 0;
    c->replaces = replaces;
    c->issued_at = now;
    cn_copy(c->holder_pk, pk, CN_PK_BYTES);
    return CN_OK;
}

static uint32_t free_card_slot(const cn_issuer_t *iss, uint32_t exclude)
{
    for (uint32_t i = 0; i < CN_MAX_CARDS; i++)
        if (!iss->cards[i].used) return i;
    for (uint32_t i = 0; i < CN_MAX_CARDS; i++)
        if (i != exclude && iss->cards[i].state == CN_CARD_REVOKED) return i;
    return NONE_IDX;
}

static bool account_ok(const cn_issuer_t *iss, uint32_t account)
{
    return account < CN_MAX_ACCOUNTS && iss->accounts[account].used &&
           iss->accounts[account].state != CN_ACCT_CLOSED;
}

cn_rc_t cn_issue_card(cn_issuer_t *iss, uint32_t account, uint32_t form,
                      const uint8_t holder_pk[CN_PK_BYTES], const uint8_t entropy[32],
                      cn_time_t now, uint32_t *out_card)
{
    if (!iss || !holder_pk || !entropy || !out_card) return CN_ERR_ARG;
    if (!account_ok(iss, account)) return CN_ERR_NOT_FOUND;
    if (!cn_form_priceable(form) || iss->cfg.per_cycle_limit[form] == 0) return CN_ERR_FORM;
    uint32_t slot = free_card_slot(iss, NONE_IDX);
    if (slot == NONE_IDX) return CN_ERR_FULL;
    static cn_card_t tmp; /* single-threaded, like the rest of the kernel layer */
    cn_zero(&tmp, (uint32_t) sizeof(tmp));
    cn_rc_t rc = build_card(iss, account, form, holder_pk, entropy, now, NONE_IDX, &tmp);
    if (rc != CN_OK) return rc;
    cn_copy(&iss->cards[slot], &tmp, (uint32_t) sizeof(tmp));
    iss->accounts[account].generation++;
    *out_card = slot;
    return CN_OK;
}

cn_rc_t cn_reissue_card(cn_issuer_t *iss, uint32_t card, const uint8_t *new_pk,
                        const uint8_t entropy[32], cn_time_t now, uint32_t *out_card)
{
    if (!iss || !entropy || !out_card || card >= CN_MAX_CARDS) return CN_ERR_ARG;
    cn_card_t *old = &iss->cards[card];
    if (!old->used) return CN_ERR_NOT_FOUND;
    if (old->state == CN_CARD_REVOKED) return CN_ERR_STATE;
    if (!account_ok(iss, old->account)) return CN_ERR_STATE;
    uint32_t slot = free_card_slot(iss, card);
    if (slot == NONE_IDX) return CN_ERR_FULL;
    static cn_card_t tmp;
    cn_zero(&tmp, (uint32_t) sizeof(tmp));
    cn_rc_t rc = build_card(iss, old->account, old->form, new_pk ? new_pk : old->holder_pk, entropy,
                            now, card, &tmp);
    if (rc != CN_OK) return rc; /* nothing has been written */
    /* Commit: both writes happen together, after every check has passed. */
    cn_copy(&iss->cards[slot], &tmp, (uint32_t) sizeof(tmp));
    old->state = CN_CARD_REVOKED;
    iss->accounts[old->account].generation++;
    *out_card = slot;
    return CN_OK;
}

cn_rc_t cn_set_frozen(cn_issuer_t *iss, uint32_t card, bool frozen)
{
    if (!iss || card >= CN_MAX_CARDS) return CN_ERR_ARG;
    cn_card_t *c = &iss->cards[card];
    if (!c->used) return CN_ERR_NOT_FOUND;
    if (c->state == CN_CARD_REVOKED) return CN_ERR_STATE;
    c->state = frozen ? CN_CARD_FROZEN : CN_CARD_ACTIVE;
    return CN_OK;
}

cn_rc_t cn_find_card(const cn_issuer_t *iss, const char *pan, uint32_t *out_card)
{
    if (!iss || !pan || !out_card) return CN_ERR_ARG;
    if (cn_strnlen(pan, CN_PAN_LEN + 1) != CN_PAN_LEN) return CN_ERR_ARG;
    for (uint32_t i = 0; i < CN_MAX_CARDS; i++) {
        if (iss->cards[i].used && cn_eq(iss->cards[i].pan, pan, CN_PAN_LEN)) {
            *out_card = i;
            return CN_OK;
        }
    }
    return CN_ERR_NOT_FOUND;
}

uint64_t cn_account_owed(const cn_issuer_t *iss, uint32_t account)
{
    if (!iss || account >= CN_MAX_ACCOUNTS || !iss->accounts[account].used) return 0;
    const cn_account_t *a = &iss->accounts[account];
    /* Each term is bounded by the ceiling (<= 10^12), so the sum cannot wrap. */
    return a->held + a->unbilled + a->billed;
}

static void approval_code(const uint8_t crypt[8], uint32_t idx, char out[7])
{
    static const char alnum[37] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    uint8_t in[16 + 8 + 4], h[32];
    bw_t w = {in, (uint32_t) sizeof(in), 0, false};
    bw_bytes(&w, "ZXV-CARDNET-APPR", 16);
    bw_bytes(&w, crypt, 8);
    bw_u32(&w, idx);
    sha3_256(in, w.len, h);
    for (uint32_t i = 0; i < 6; i++) out[i] = alnum[h[i] % 36u];
    out[6] = 0;
}

cn_rc_t cn_authorize(cn_issuer_t *iss, const cn_auth_req_t *req, const uint8_t sig[CN_SIG_BYTES],
                     uint32_t *out_auth)
{
    if (!iss || !out_auth) return CN_ERR_ARG;
    if (iss->n_auths >= CN_MAX_AUTHS) return CN_ERR_FULL;
    uint32_t idx = iss->n_auths;
    cn_auth_t *au = &iss->auths[idx];
    cn_zero(au, (uint32_t) sizeof(*au));
    au->used = true;
    au->card = NONE_IDX;
    au->account = NONE_IDX;
    au->txn = NONE_IDX;
    if (req) cn_copy(&au->req, req, (uint32_t) sizeof(*req));
    au->req.pan[CN_PAN_LEN] = 0;
    au->req.terminal_id[CN_TID_LEN] = 0;
    au->req.merchant_id[CN_MID_LEN] = 0;
    iss->n_auths++;
    *out_auth = idx;

    cn_decline_t d = CN_DECL_NONE;
    uint8_t ch[CN_CHALLENGE_MAX];
    uint32_t chlen = 0;
    cn_card_t *c = 0;
    cn_account_t *a = 0;
    cn_network_t net = (cn_network_t) iss->cfg.network;

    if (!req || !sig || cn_challenge(req, ch, &chlen) != CN_OK) {
        d = CN_DECL_FORMAT;
        goto done;
    }
    cn_cryptogram(sig, au->cryptogram);
    if (!cn_pan_valid(net, req->pan, CN_PAN_LEN)) {
        d = CN_DECL_BAD_PAN;
        goto done;
    }
    uint32_t ci;
    if (cn_find_card(iss, req->pan, &ci) != CN_OK) {
        d = CN_DECL_UNKNOWN_CARD;
        goto done;
    }
    c = &iss->cards[ci];
    au->card = ci;
    au->account = c->account;
    a = &iss->accounts[c->account];
    if (c->state == CN_CARD_REVOKED) {
        d = CN_DECL_REVOKED;
        goto done;
    }
    if (c->state == CN_CARD_FROZEN) {
        d = CN_DECL_FROZEN;
        goto done;
    }
    {
        cn_civil_t cv;
        cn_civil_from_time(req->time, &cv);
        if (cv.year * 12u + cv.month > c->exp_year * 12u + c->exp_month) {
            d = CN_DECL_EXPIRED;
            goto done;
        }
    }
    if (!pq_mldsa65_verify(c->holder_pk, ch, chlen, (const uint8_t *) CN_SIG_CTX, CN_SIG_CTX_LEN,
                           sig)) {
        d = CN_DECL_BAD_SIG;
        goto done;
    }
    if (req->atc <= c->atc) {
        d = CN_DECL_REPLAY;
        goto done;
    }
    c->atc = req->atc; /* a genuine holder signature consumes the counter */
    if (req->currency != CN_RAIL_DEBIT) {
        d = CN_DECL_CURRENCY;
        goto done;
    }
    if (req->form != c->form || !cn_form_priceable(req->form)) {
        d = CN_DECL_FORM;
        goto done;
    }
    if (!a->used || a->state != CN_ACCT_ACTIVE) {
        d = CN_DECL_DELINQUENT;
        goto done;
    }
    if (req->amount_minor > iss->cfg.per_txn_limit[req->form]) {
        d = CN_DECL_TXN_LIMIT;
        goto done;
    }
    if (a->cycle_spent[req->form] + req->amount_minor > iss->cfg.per_cycle_limit[req->form]) {
        d = CN_DECL_CYCLE_LIMIT;
        goto done;
    }
    if (cn_account_owed(iss, c->account) + req->amount_minor > iss->cfg.account_ceiling) {
        d = CN_DECL_CEILING;
        goto done;
    }
    a->held += req->amount_minor;
    a->cycle_spent[req->form] += req->amount_minor;

done:
    au->decline = (uint32_t) d;
    cn_copy(au->rc, cn_decline_rc(d), 3);
    if (d == CN_DECL_NONE) {
        au->state = CN_AUTH_APPROVED;
        approval_code(au->cryptogram, idx, au->approval);
    } else {
        au->state = CN_AUTH_DECLINED;
    }
    return CN_OK;
}

/* ===== Ledger and receipts ===== */

static void receipt_digest(const cn_receipt_t *r, uint8_t out[32])
{
    uint8_t buf[160];
    bw_t w = {buf, (uint32_t) sizeof(buf), 0, false};
    bw_bytes(&w, "ZXV-CARDNET-RCPT-v1", 19);
    bw_u32(&w, r->id);
    bw_u32(&w, r->txn);
    bw_u32(&w, r->kind);
    bw_u32(&w, r->account);
    bw_u32(&w, r->auth);
    bw_bytes(&w, r->merchant_id, CN_MID_LEN);
    bw_u64(&w, r->amount);
    bw_u64(&w, r->fee);
    bw_u32(&w, r->equity_rail);
    bw_u32(&w, r->form);
    bw_u32(&w, r->time);
    bw_bytes(&w, r->cryptogram, 8);
    bw_bytes(&w, r->prev, 32);
    sha3_256(buf, w.len, out);
}

/* Room for one transaction with nlegs legs and its receipt? */
static bool room(const cn_issuer_t *iss, uint32_t nlegs)
{
    return iss->n_txns < CN_MAX_TXNS && iss->n_receipts < CN_MAX_RECEIPTS &&
           iss->n_legs + nlegs <= CN_MAX_LEGS;
}

static void post_leg(cn_issuer_t *iss, uint32_t txn, cn_side_t side, uint32_t rail, cn_lacct_t acct,
                     uint64_t amount)
{
    cn_leg_t *l = &iss->legs[iss->n_legs++];
    l->txn = txn;
    l->side = (uint32_t) side;
    l->rail = rail;
    l->account = (uint32_t) acct;
    l->amount = amount;
}

static void emit_receipt(cn_issuer_t *iss, uint32_t txn, cn_txn_kind_t kind, uint32_t account,
                         uint32_t auth, const char *mid, uint64_t amount, uint64_t fee,
                         uint32_t form, cn_time_t t, const uint8_t *crypt)
{
    cn_receipt_t *r = &iss->receipts[iss->n_receipts];
    cn_zero(r, (uint32_t) sizeof(*r));
    r->id = iss->n_receipts;
    r->txn = txn;
    r->kind = (uint32_t) kind;
    r->account = account;
    r->auth = auth;
    if (mid) cn_copy(r->merchant_id, mid, CN_MID_LEN);
    r->amount = amount;
    r->fee = fee;
    r->equity_rail = CN_RAIL_EQUITY;
    r->form = form;
    r->time = t;
    if (crypt) cn_copy(r->cryptogram, crypt, 8);
    if (iss->n_receipts > 0) cn_copy(r->prev, iss->receipts[iss->n_receipts - 1].digest, 32);
    receipt_digest(r, r->digest);
    iss->n_receipts++;
}

cn_rc_t cn_settle(cn_issuer_t *iss, uint32_t auth)
{
    if (!iss || auth >= iss->n_auths) return CN_ERR_ARG;
    cn_auth_t *au = &iss->auths[auth];
    if (au->state != CN_AUTH_APPROVED) return CN_ERR_STATE;
    if (!room(iss, 3)) return CN_ERR_FULL;
    cn_account_t *a = &iss->accounts[au->account];
    uint64_t amt = au->req.amount_minor;
    uint64_t fee = cn_fee_for_charge(&iss->cfg.merchant_fee, amt);
    uint32_t txn = iss->n_txns++;
    post_leg(iss, txn, CN_DR, CN_RAIL_DEBIT, CN_LACCT_HOLDER_RECEIVABLE, amt);
    post_leg(iss, txn, CN_CR, CN_RAIL_CREDIT, CN_LACCT_MERCHANT_PAYABLE, amt - fee);
    if (fee) post_leg(iss, txn, CN_CR, CN_RAIL_CREDIT, CN_LACCT_NETWORK_FEES, fee);
    a->held -= amt;
    a->unbilled += amt;
    au->state = CN_AUTH_SETTLED;
    au->txn = txn;
    emit_receipt(iss, txn, CN_TXN_CHARGE, au->account, auth, au->req.merchant_id, amt, fee,
                 au->req.form, au->req.time, au->cryptogram);
    return CN_OK;
}

static uint64_t sat_sub(uint64_t a, uint64_t b)
{
    return a > b ? a - b : 0;
}

cn_rc_t cn_reverse(cn_issuer_t *iss, uint32_t auth, cn_time_t now)
{
    if (!iss || auth >= iss->n_auths) return CN_ERR_ARG;
    cn_auth_t *au = &iss->auths[auth];
    cn_account_t *a;
    uint64_t amt = au->req.amount_minor;
    if (au->state == CN_AUTH_APPROVED) {
        a = &iss->accounts[au->account];
        a->held -= amt;
        a->cycle_spent[au->req.form] = sat_sub(a->cycle_spent[au->req.form], amt);
        au->state = CN_AUTH_REVERSED;
        return CN_OK;
    }
    if (au->state != CN_AUTH_SETTLED) return CN_ERR_STATE;
    if (!room(iss, 3)) return CN_ERR_FULL;
    a = &iss->accounts[au->account];
    /* The refund comes out of what is still owed: unbilled first, then billed. */
    uint64_t owed = a->unbilled + a->billed;
    if (owed < amt) return CN_ERR_STATE; /* already paid: refund outside this model */
    uint64_t fee = cn_fee_for_charge(&iss->cfg.merchant_fee, amt);
    uint32_t txn = iss->n_txns++;
    post_leg(iss, txn, CN_DR, CN_RAIL_CREDIT, CN_LACCT_MERCHANT_PAYABLE, amt - fee);
    if (fee) post_leg(iss, txn, CN_DR, CN_RAIL_CREDIT, CN_LACCT_NETWORK_FEES, fee);
    post_leg(iss, txn, CN_CR, CN_RAIL_DEBIT, CN_LACCT_HOLDER_RECEIVABLE, amt);
    uint64_t from_unbilled = a->unbilled < amt ? a->unbilled : amt;
    a->unbilled -= from_unbilled;
    a->billed -= amt - from_unbilled;
    a->cycle_spent[au->req.form] = sat_sub(a->cycle_spent[au->req.form], amt);
    if (a->billed == 0 && a->state == CN_ACCT_DELINQUENT) a->state = CN_ACCT_ACTIVE;
    au->state = CN_AUTH_REVERSED;
    au->txn = txn;
    emit_receipt(iss, txn, CN_TXN_REFUND, au->account, auth, au->req.merchant_id, amt, fee,
                 au->req.form, now, au->cryptogram);
    return CN_OK;
}

cn_rc_t cn_advance(cn_issuer_t *iss, cn_time_t now)
{
    if (!iss) return CN_ERR_ARG;
    uint32_t today = day_of(now);
    for (uint32_t i = 0; i < CN_MAX_ACCOUNTS; i++) {
        cn_account_t *a = &iss->accounts[i];
        if (!a->used || a->state == CN_ACCT_CLOSED) continue;
        while (today >= a->cycle_start_day + iss->cfg.cycle_days) {
            uint32_t close = a->cycle_start_day + iss->cfg.cycle_days;
            if (a->unbilled > 0) {
                if (iss->n_statements >= CN_MAX_STATEMENTS) return CN_ERR_FULL;
                cn_statement_t *s = &iss->statements[iss->n_statements];
                s->id = iss->n_statements;
                s->account = i;
                s->open_day = a->cycle_start_day;
                s->close_day = close;
                /* Older arrears keep their own (earlier) due day. */
                if (a->billed == 0) a->due_day = close + iss->cfg.grace_days;
                s->due_day = a->due_day;
                s->new_charges = a->unbilled;
                a->billed += a->unbilled;
                a->unbilled = 0;
                s->amount_due = a->billed;
                iss->n_statements++;
            }
            for (uint32_t f = 0; f < CN_FORM_COUNT; f++) a->cycle_spent[f] = 0;
            a->cycle_start_day = close;
        }
        /* Past due: stop new spending. The amount owed is NOT touched. */
        if (a->billed > 0 && today > a->due_day) a->state = CN_ACCT_DELINQUENT;
    }
    return CN_OK;
}

cn_rc_t cn_pay(cn_issuer_t *iss, uint32_t account, uint64_t amount, cn_time_t now)
{
    if (!iss || account >= CN_MAX_ACCOUNTS || !iss->accounts[account].used) return CN_ERR_ARG;
    cn_account_t *a = &iss->accounts[account];
    if (amount == 0 || amount > a->billed + a->unbilled) return CN_ERR_AMOUNT;
    if (!room(iss, 2)) return CN_ERR_FULL;
    uint32_t txn = iss->n_txns++;
    post_leg(iss, txn, CN_DR, CN_RAIL_DEBIT, CN_LACCT_ISSUER_CASH, amount);
    post_leg(iss, txn, CN_CR, CN_RAIL_CREDIT, CN_LACCT_HOLDER_RECEIVABLE, amount);
    uint64_t to_billed = a->billed < amount ? a->billed : amount;
    a->billed -= to_billed;
    a->unbilled -= amount - to_billed;
    if (a->billed == 0 && a->state == CN_ACCT_DELINQUENT) a->state = CN_ACCT_ACTIVE;
    emit_receipt(iss, txn, CN_TXN_PAYMENT, account, NONE_IDX, 0, amount, 0, CN_FORM_FINANCIAL, now,
                 0);
    return CN_OK;
}

bool cn_txn_balanced(const cn_issuer_t *iss, uint32_t txn)
{
    if (!iss || txn >= iss->n_txns) return false;
    uint64_t dr = 0, cr = 0, r846 = 0, r810 = 0;
    uint32_t n = 0;
    for (uint32_t i = 0; i < iss->n_legs; i++) {
        const cn_leg_t *l = &iss->legs[i];
        if (l->txn != txn) continue;
        n++;
        if (l->side == CN_DR)
            dr += l->amount;
        else if (l->side == CN_CR)
            cr += l->amount;
        else
            return false;
        if (l->rail == CN_RAIL_DEBIT)
            r846 += l->amount;
        else if (l->rail == CN_RAIL_CREDIT)
            r810 += l->amount;
        else
            return false;
    }
    /* C1: debits equal credits, and the 846 side equals the 810 side, to
     * the minor unit, with no rounding slack. */
    return n >= 2 && dr == cr && r846 == r810 && dr > 0;
}

bool cn_ledger_balanced(const cn_issuer_t *iss)
{
    if (!iss) return false;
    for (uint32_t t = 0; t < iss->n_txns; t++)
        if (!cn_txn_balanced(iss, t)) return false;
    return true;
}

bool cn_receipts_verify(const cn_issuer_t *iss)
{
    if (!iss || iss->n_receipts != iss->n_txns) return false;
    uint8_t prev[32];
    cn_zero(prev, 32);
    for (uint32_t i = 0; i < iss->n_receipts; i++) {
        const cn_receipt_t *r = &iss->receipts[i];
        uint8_t d[32];
        if (r->id != i || r->txn != i || r->equity_rail != CN_RAIL_EQUITY) return false;
        if (!cn_eq(r->prev, prev, 32)) return false;
        receipt_digest(r, d);
        if (!cn_eq(d, r->digest, 32)) return false;
        cn_copy(prev, r->digest, 32);
    }
    return true;
}
