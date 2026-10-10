/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* pay_ledger.c — three-rail nine-capital ledger. See pay_ledger.h. */
#include "pay_ledger.h"

#define LINE_MAX_DELTA ((int64_t) 1 << 59) /* 8 lines * 2^59 = 2^62: sums never overflow */

uint16_t pay_rail_code(pay_rail_t r)
{
    switch (r) {
    case PAY_RAIL_DEBIT:
        return PAY_RAIL_DEBIT_CODE;
    case PAY_RAIL_CREDIT:
        return PAY_RAIL_CREDIT_CODE;
    case PAY_RAIL_EQUITY:
        return PAY_RAIL_EQUITY_CODE;
    default:
        return 0;
    }
}

const char *pay_rail_juris(pay_rail_t r)
{
    switch (r) {
    case PAY_RAIL_DEBIT:
        return PAY_RAIL_DEBIT_JURIS;
    case PAY_RAIL_CREDIT:
        return PAY_RAIL_CREDIT_JURIS;
    case PAY_RAIL_EQUITY:
        return PAY_RAIL_EQUITY_JURIS;
    default:
        return "";
    }
}

bool pay_cap_is_crown(pay_cap_t c)
{
    return c == PAY_CAP_SOCIAL || c == PAY_CAP_NATURAL || c == PAY_CAP_CULTURAL ||
           c == PAY_CAP_SPIRITUAL;
}

pay_status_t pay_usury_check(uint64_t principal, uint64_t repaid, bool time_based)
{
    if (time_based) return PAY_ERR_USURY;
    if (repaid > principal) return PAY_ERR_USURY;
    return PAY_OK;
}

void pay_platform_default(pay_platform_t *p)
{
    if (!p) return;
    pay_memset(p, 0, sizeof *p);
    pay_strlcpy(p->vfv_alpha, "VFV", sizeof p->vfv_alpha);
    p->vfv_numeric = PAY_RAIL_DEBIT_CODE;
    p->vfv_minor = 2;
    pay_strlcpy(p->jurisdiction, PAY_RAIL_DEBIT_JURIS, sizeof p->jurisdiction);
    p->jurisdiction_iso = false;
    for (int r = 0; r < PAY_RAIL_COUNT; r++)
        pay_strlcpy(p->rail_juris[r], pay_rail_juris((pay_rail_t) r), sizeof p->rail_juris[r]);
}

/* ===== Assets ===== */

static pay_status_t add_asset_raw(pay_ledger_t *L, const char *code, uint16_t numeric,
                                  uint8_t minor, pay_asset_kind_t kind, const char *dti, bool iso,
                                  bool store_credit, uint16_t *out)
{
    size_t n = pay_strnlen(code, PAY_CODE_MAX + 1);
    if (n == 0 || n > PAY_CODE_MAX || minor > 19) return PAY_ERR_ARG;
    for (uint32_t i = 0; i < L->n_assets; i++)
        if (pay_streq(L->assets[i].code, code)) return PAY_ERR_STATE;
    if (L->n_assets >= PAY_MAX_ASSETS) return PAY_ERR_FULL;
    pay_asset_t *a = &L->assets[L->n_assets];
    pay_memset(a, 0, sizeof *a);
    pay_strlcpy(a->code, code, sizeof a->code);
    a->numeric = numeric;
    a->minor = minor;
    a->kind = (uint8_t) kind;
    if (dti) pay_strlcpy(a->dti, dti, sizeof a->dti);
    a->iso4217 = iso;
    a->store_credit = store_credit;
    a->active = true;
    if (out) *out = (uint16_t) L->n_assets;
    L->n_assets++;
    return PAY_OK;
}

void pay_ledger_init(pay_ledger_t *L, const pay_platform_t *platform)
{
    if (!L) return;
    pay_memset(L, 0, sizeof *L);
    if (platform)
        L->platform = *platform;
    else
        pay_platform_default(&L->platform);
    if (L->platform.vfv_alpha[0] == '\0') pay_strlcpy(L->platform.vfv_alpha, "VFV", 8);
    add_asset_raw(L, L->platform.vfv_alpha, L->platform.vfv_numeric, L->platform.vfv_minor,
                  PAY_ASSET_PLATFORM, 0, false, true, &L->vfv_asset);
}

pay_status_t pay_ledger_add_fiat(pay_ledger_t *L, const char *alpha, uint16_t numeric,
                                 uint8_t minor, uint16_t *out_asset)
{
    if (!L || !alpha || pay_strnlen(alpha, 4) != 3) return PAY_ERR_ARG;
    for (int i = 0; i < 3; i++)
        if (!pay_is_upper(alpha[i])) return PAY_ERR_ARG;
    if (pay_streq(alpha, L->platform.vfv_alpha)) return PAY_ERR_POLICY;
    if (numeric == 0 || numeric > 999 || numeric == PAY_RAIL_DEBIT_CODE ||
        numeric == PAY_RAIL_CREDIT_CODE || numeric == PAY_RAIL_EQUITY_CODE ||
        numeric == L->platform.vfv_numeric || minor > 4)
        return PAY_ERR_ARG;
    return add_asset_raw(L, alpha, numeric, minor, PAY_ASSET_FIAT, 0, true, false, out_asset);
}

bool pay_dti_format_ok(const char *dti)
{
    if (!dti || pay_strnlen(dti, 10) != 9) return false;
    for (int i = 0; i < 9; i++) {
        char c = dti[i];
        if (pay_is_digit(c)) continue;
        if (!pay_is_upper(c) || c == 'A' || c == 'E' || c == 'I' || c == 'O' || c == 'U')
            return false;
    }
    return true;
}

pay_status_t pay_ledger_add_crypto(pay_ledger_t *L, const char *code, uint8_t minor,
                                   const char *dti, uint16_t *out_asset)
{
    if (!L) return PAY_ERR_ARG;
    if (dti && dti[0] && !pay_dti_format_ok(dti)) return PAY_ERR_ARG;
    return add_asset_raw(L, code, 0, minor, PAY_ASSET_CRYPTO, dti, false, false, out_asset);
}

pay_status_t pay_ledger_add_asset(pay_ledger_t *L, const char *code, uint8_t minor,
                                  pay_asset_kind_t kind, uint16_t *out_asset)
{
    if (!L || kind == PAY_ASSET_FIAT || kind == PAY_ASSET_PLATFORM) return PAY_ERR_ARG;
    return add_asset_raw(L, code, 0, minor, kind, 0, false, false, out_asset);
}

const pay_asset_t *pay_ledger_asset(const pay_ledger_t *L, uint16_t asset)
{
    if (!L || asset >= L->n_assets) return 0;
    return &L->assets[asset];
}

/* ===== Accounts ===== */

pay_status_t pay_ledger_open(pay_ledger_t *L, uint32_t owner, uint16_t asset, pay_cap_t cap,
                             uint8_t flags, uint32_t *out_acct)
{
    if (!L || (unsigned) cap >= PAY_CAP_COUNT) return PAY_ERR_ARG;
    if (asset >= L->n_assets) return PAY_ERR_NO_ASSET;
    if (L->n_accounts >= PAY_MAX_ACCOUNTS) return PAY_ERR_FULL;
    pay_account_t *a = &L->acct[L->n_accounts];
    pay_memset(a, 0, sizeof *a);
    a->owner = owner;
    a->asset = asset;
    a->cap = (uint8_t) cap;
    a->flags = flags;
    a->active = true;
    if (out_acct) *out_acct = L->n_accounts;
    L->n_accounts++;
    return PAY_OK;
}

const pay_account_t *pay_ledger_account(const pay_ledger_t *L, uint32_t acct)
{
    if (!L || acct >= L->n_accounts || !L->acct[acct].active) return 0;
    return &L->acct[acct];
}

pay_status_t pay_ledger_set_flags(pay_ledger_t *L, uint32_t acct, uint8_t flags)
{
    if (!L || acct >= L->n_accounts) return PAY_ERR_NO_ACCOUNT;
    pay_account_t *a = &L->acct[acct];
    if (!(flags & PAY_ACCT_ISSUER) && a->credit) return PAY_ERR_CREDIT;
    a->flags = flags;
    return PAY_OK;
}

/* ===== Request digest and journal ===== */

static void req_digest(const pay_posting_req_t *r, uint8_t out[PAY_HASH_LEN])
{
    pay_hbuf h;
    pay_hbuf_init(&h);
    pay_hbuf_str(&h, "ZXV-PAY-POSTING-v1");
    pay_hbuf_str(&h, r->uetr);
    pay_hbuf_str(&h, r->e2e);
    pay_hbuf_str(&h, r->memo);
    pay_hbuf_u64(&h, r->initiator);
    pay_hbuf_u64(&h, r->nonce);
    pay_hbuf_u64(&h, r->tick);
    pay_hbuf_u64(&h, r->attestor);
    pay_hbuf_u64(&h, r->kind);
    pay_hbuf_u64(&h, r->ext_cap);
    pay_hbuf_u64(&h, (uint64_t) r->ext_value);
    pay_hbuf_u64(&h, r->n_lines);
    for (uint32_t i = 0; i < r->n_lines && i < PAY_MAX_LINES; i++) {
        pay_hbuf_u64(&h, r->lines[i].account);
        pay_hbuf_u64(&h, (uint64_t) r->lines[i].d_debit);
        pay_hbuf_u64(&h, (uint64_t) r->lines[i].d_credit);
    }
    pay_hbuf_final(&h, out);
}

static void chain_hash(const pay_journal_t *j, uint8_t out[PAY_HASH_LEN])
{
    pay_hbuf h;
    pay_hbuf_init(&h);
    pay_hbuf_str(&h, "ZXV-PAY-CHAIN-v1");
    pay_hbuf_put(&h, j->prev, PAY_HASH_LEN);
    pay_hbuf_u64(&h, j->seq);
    pay_hbuf_put(&h, j->digest, PAY_HASH_LEN);
    pay_hbuf_str(&h, j->uetr);
    pay_hbuf_str(&h, j->e2e);
    pay_hbuf_u64(&h, j->attestor);
    pay_hbuf_u64(&h, j->tick);
    pay_hbuf_final(&h, out);
}

static uint32_t journal_count(const pay_ledger_t *L)
{
    return L->seq < PAY_JOURNAL_MAX ? (uint32_t) L->seq : PAY_JOURNAL_MAX;
}

static uint32_t journal_slot(uint64_t seq)
{
    return (uint32_t) (seq & (PAY_JOURNAL_MAX - 1u)); /* PAY_JOURNAL_MAX is 2^10 */
}

const pay_journal_t *pay_ledger_find_uetr(const pay_ledger_t *L, const char *uetr)
{
    if (!L || !uetr) return 0;
    for (uint32_t i = 0; i < PAY_JOURNAL_MAX; i++)
        if (L->journal[i].used && pay_streq(L->journal[i].uetr, uetr)) return &L->journal[i];
    return 0;
}

static bool e2e_ok(const char *s)
{
    size_t n = pay_strnlen(s, PAY_E2E_MAX + 1);
    if (n == 0 || n > PAY_E2E_MAX) return false;
    for (size_t i = 0; i < n; i++)
        if (s[i] < 0x20 || s[i] > 0x7e) return false;
    return true;
}

/* ===== Invariants ===== */

static void totals128(const pay_ledger_t *L, uint16_t asset, uint8_t cap, pay_u128 *d, pay_u128 *c,
                      pay_u128 *ep, pay_u128 *en)
{
    pay_u128 z = {0, 0};
    *d = *c = *ep = *en = z;
    for (uint32_t i = 0; i < L->n_accounts; i++) {
        const pay_account_t *a = &L->acct[i];
        if (!a->active || a->asset != asset || a->cap != cap) continue;
        *d = pay_u128_add64(*d, a->debit);
        *c = pay_u128_add64(*c, a->credit);
        if (a->equity >= 0)
            *ep = pay_u128_add64(*ep, (uint64_t) a->equity);
        else
            *en = pay_u128_add64(*en, (uint64_t) 0 - (uint64_t) a->equity);
    }
}

bool pay_ledger_check(const pay_ledger_t *L)
{
    if (!L) return false;
    for (uint32_t i = 0; i < L->n_accounts; i++) {
        const pay_account_t *a = &L->acct[i];
        if (!a->active) continue;
        if (a->debit >= PAY_BAL_MAX || a->credit >= PAY_BAL_MAX) return false;
        if (a->credit && !(a->flags & PAY_ACCT_ISSUER)) return false;
        if (a->equity != (int64_t) a->debit - (int64_t) a->credit) return false;
    }
    for (uint32_t as = 0; as < L->n_assets; as++)
        for (uint8_t c = 0; c < PAY_CAP_COUNT; c++) {
            pay_u128 d, cr, ep, en;
            totals128(L, (uint16_t) as, c, &d, &cr, &ep, &en);
            if (pay_u128_cmp(d, cr) != 0 || pay_u128_cmp(ep, en) != 0) return false;
        }
    return true;
}

void pay_ledger_totals(const pay_ledger_t *L, uint16_t asset, pay_cap_t cap, uint64_t *debit,
                       uint64_t *credit, int64_t *equity)
{
    pay_u128 d, c, ep, en;
    totals128(L, asset, (uint8_t) cap, &d, &c, &ep, &en);
    /* By L2 and L3 each total is < 2^62; saturate defensively otherwise. */
    if (debit) *debit = d.hi ? UINT64_MAX : d.lo;
    if (credit) *credit = c.hi ? UINT64_MAX : c.lo;
    if (equity) {
        if (ep.hi || en.hi || ep.lo > (uint64_t) INT64_MAX || en.lo > (uint64_t) INT64_MAX)
            *equity = INT64_MIN;
        else
            *equity = (int64_t) ep.lo - (int64_t) en.lo;
    }
}

bool pay_ledger_verify_chain(const pay_ledger_t *L)
{
    uint32_t n = journal_count(L);
    if (n == 0) return true;
    uint64_t first = L->seq - n;
    const uint8_t *prev = L->journal[journal_slot(first)].prev;
    for (uint64_t s = first; s < L->seq; s++) {
        const pay_journal_t *j = &L->journal[journal_slot(s)];
        uint8_t h[PAY_HASH_LEN];
        if (!j->used || j->seq != s || !pay_memeq(j->prev, prev, PAY_HASH_LEN)) return false;
        chain_hash(j, h);
        if (!pay_memeq(h, j->hash, PAY_HASH_LEN)) return false;
        prev = j->hash;
    }
    return pay_memeq(prev, L->chain_head, PAY_HASH_LEN);
}

/* ===== The choke point ===== */

pay_status_t pay_ledger_post(pay_ledger_t *L, const pay_posting_req_t *r, pay_receipt_t *rc)
{
    pay_receipt_t dummy;
    if (!rc) rc = &dummy;
    pay_memset(rc, 0, sizeof *rc);
    if (!L || !r || r->n_lines == 0 || r->n_lines > PAY_MAX_LINES) return rc->status = PAY_ERR_ARG;
    if (!pay_uetr_valid(r->uetr)) return rc->status = PAY_ERR_UETR;
    if (!e2e_ok(r->e2e) || r->ext_cap >= PAY_CAP_COUNT) return rc->status = PAY_ERR_ARG;
    if (r->kind > PAY_KIND_ADJUST) return rc->status = PAY_ERR_ARG;

    uint8_t dg[PAY_HASH_LEN];
    req_digest(r, dg);

    /* R2 idempotency, R1 uniqueness (journal window). */
    for (uint32_t i = 0; i < PAY_JOURNAL_MAX; i++) {
        const pay_journal_t *j = &L->journal[i];
        if (!j->used) continue;
        if (pay_memeq(j->idem_key, r->idem_key, 32)) {
            if (!pay_memeq(j->digest, dg, PAY_HASH_LEN)) return rc->status = PAY_ERR_REPLAY;
            rc->seq = j->seq;
            pay_memcpy(rc->digest, j->digest, PAY_HASH_LEN);
            pay_memcpy(rc->hash, j->hash, PAY_HASH_LEN);
            return rc->status = PAY_DUPLICATE;
        }
        if (pay_streq(j->uetr, r->uetr)) return rc->status = PAY_ERR_DUP_UETR;
        if (pay_streq(j->e2e, r->e2e)) return rc->status = PAY_ERR_DUP_UETR;
    }

    /* R3 nonce. */
    uint32_t nslot = PAY_MAX_PRINCIPALS;
    if (r->nonce) {
        for (uint32_t i = 0; i < L->n_nonce; i++)
            if (L->nonce_owner[i] == r->initiator) nslot = i;
        if (nslot < PAY_MAX_PRINCIPALS && r->nonce <= L->nonce_last[nslot])
            return rc->status = PAY_ERR_REPLAY;
        if (nslot == PAY_MAX_PRINCIPALS && L->n_nonce >= PAY_MAX_PRINCIPALS)
            return rc->status = PAY_ERR_FULL;
    }

    /* Simulate on a scratch copy (L4: nothing is written until all passes). */
    uint32_t ids[PAY_MAX_LINES];
    pay_account_t nw[PAY_MAX_LINES];
    uint32_t nu = 0;
    bool crown = false;
    for (uint32_t i = 0; i < r->n_lines; i++) {
        const pay_line_t *ln = &r->lines[i];
        if (ln->account >= L->n_accounts || !L->acct[ln->account].active)
            return rc->status = PAY_ERR_NO_ACCOUNT;
        if (ln->d_debit > LINE_MAX_DELTA || ln->d_debit < -LINE_MAX_DELTA ||
            ln->d_credit > LINE_MAX_DELTA || ln->d_credit < -LINE_MAX_DELTA)
            return rc->status = PAY_ERR_OVERFLOW;
        uint32_t k = 0;
        while (k < nu && ids[k] != ln->account) k++;
        if (k == nu) {
            ids[nu] = ln->account;
            nw[nu] = L->acct[ln->account];
            nu++;
        }
        pay_account_t *a = &nw[k];
        if (pay_cap_is_crown((pay_cap_t) a->cap)) crown = true;
        if (ln->d_debit < 0 && (a->flags & PAY_ACCT_FROZEN)) return rc->status = PAY_ERR_POLICY;
        int64_t nd, nc;
        if (__builtin_add_overflow((int64_t) a->debit, ln->d_debit, &nd) ||
            __builtin_add_overflow((int64_t) a->credit, ln->d_credit, &nc))
            return rc->status = PAY_ERR_OVERFLOW;
        if (nd < 0) return rc->status = PAY_ERR_FUNDS;
        if (nc < 0) return rc->status = PAY_ERR_CREDIT;
        if ((uint64_t) nd >= PAY_BAL_MAX || (uint64_t) nc >= PAY_BAL_MAX)
            return rc->status = PAY_ERR_OVERFLOW;
        if (nc > 0 && !(a->flags & PAY_ACCT_ISSUER)) return rc->status = PAY_ERR_CREDIT;
        a->debit = (uint64_t) nd;
        a->credit = (uint64_t) nc;
        a->equity = nd - nc;
    }

    /* L1: balanced per (asset, cap). */
    for (uint32_t i = 0; i < r->n_lines; i++) {
        const pay_account_t *ai = &L->acct[r->lines[i].account];
        int64_t sd = 0, sc = 0;
        for (uint32_t j = 0; j < r->n_lines; j++) {
            const pay_account_t *aj = &L->acct[r->lines[j].account];
            if (aj->asset != ai->asset || aj->cap != ai->cap) continue;
            if (__builtin_add_overflow(sd, r->lines[j].d_debit, &sd) ||
                __builtin_add_overflow(sc, r->lines[j].d_credit, &sc))
                return rc->status = PAY_ERR_OVERFLOW;
        }
        if (sd != sc) return rc->status = PAY_ERR_UNBALANCED;
    }

    /* L5: Crown forms never change hands. */
    if (crown) {
        uint32_t owner = L->acct[r->lines[0].account].owner;
        for (uint32_t i = 1; i < r->n_lines; i++)
            if (L->acct[r->lines[i].account].owner != owner) return rc->status = PAY_ERR_CROWN;
    }

    int64_t ext = L->externality[r->ext_cap];
    if (!pay_sadd_ok(ext, r->ext_value, &ext)) return rc->status = PAY_ERR_OVERFLOW;

    /* Apply. */
    pay_account_t saved[PAY_MAX_LINES];
    for (uint32_t k = 0; k < nu; k++) {
        saved[k] = L->acct[ids[k]];
        L->acct[ids[k]] = nw[k];
    }
    if (!pay_ledger_check(L)) { /* defence in depth: restore, refuse */
        for (uint32_t k = 0; k < nu; k++) L->acct[ids[k]] = saved[k];
        return rc->status = PAY_ERR_INVARIANT;
    }
    L->externality[r->ext_cap] = ext;
    if (r->nonce) {
        if (nslot == PAY_MAX_PRINCIPALS) {
            nslot = L->n_nonce++;
            L->nonce_owner[nslot] = r->initiator;
        }
        L->nonce_last[nslot] = r->nonce;
    }

    pay_journal_t *j = &L->journal[journal_slot(L->seq)];
    pay_memset(j, 0, sizeof *j);
    j->used = true;
    j->seq = L->seq;
    j->tick = r->tick;
    j->kind = r->kind;
    pay_memcpy(j->idem_key, r->idem_key, 32);
    pay_memcpy(j->digest, dg, PAY_HASH_LEN);
    pay_memcpy(j->prev, L->chain_head, PAY_HASH_LEN);
    pay_strlcpy(j->uetr, r->uetr, sizeof j->uetr);
    pay_strlcpy(j->e2e, r->e2e, sizeof j->e2e);
    j->attestor = r->attestor;
    for (uint32_t i = 0; i < r->n_lines; i++) j->lines[i] = r->lines[i];
    j->n_lines = r->n_lines;
    chain_hash(j, j->hash);
    pay_memcpy(L->chain_head, j->hash, PAY_HASH_LEN);
    L->seq++;

    rc->seq = j->seq;
    pay_memcpy(rc->digest, dg, PAY_HASH_LEN);
    pay_memcpy(rc->hash, j->hash, PAY_HASH_LEN);
    if (L->mirror) L->mirror(L->mirror_ctx, j);
    return rc->status = PAY_OK;
}

/* ===== Helpers ===== */

static bool amt_ok(uint64_t a)
{
    return a > 0 && a <= (uint64_t) LINE_MAX_DELTA;
}

static void set_line(pay_posting_req_t *req, uint32_t i, uint32_t acct, int64_t d, int64_t c)
{
    req->lines[i].account = acct;
    req->lines[i].d_debit = d;
    req->lines[i].d_credit = c;
}

pay_status_t pay_ledger_transfer(pay_ledger_t *L, pay_posting_req_t *req, uint32_t from,
                                 uint32_t to, uint64_t amount, pay_receipt_t *rc)
{
    if (!req || !amt_ok(amount)) return PAY_ERR_ARG;
    set_line(req, 0, from, -(int64_t) amount, 0);
    set_line(req, 1, to, (int64_t) amount, 0);
    req->n_lines = 2;
    return pay_ledger_post(L, req, rc);
}

pay_status_t pay_ledger_issue(pay_ledger_t *L, pay_posting_req_t *req, uint32_t issuer, uint32_t to,
                              uint64_t amount, pay_receipt_t *rc)
{
    if (!req || !amt_ok(amount)) return PAY_ERR_ARG;
    set_line(req, 0, to, (int64_t) amount, 0);
    set_line(req, 1, issuer, 0, (int64_t) amount);
    req->n_lines = 2;
    req->kind = PAY_KIND_ISSUE;
    return pay_ledger_post(L, req, rc);
}

pay_status_t pay_ledger_redeem(pay_ledger_t *L, pay_posting_req_t *req, uint32_t holder,
                               uint32_t issuer, uint64_t amount, pay_receipt_t *rc)
{
    if (!req || !amt_ok(amount)) return PAY_ERR_ARG;
    /* Redemption is at par: exactly `amount` of claim is extinguished for
     * exactly `amount` surrendered. Nothing above par (pay_usury_check). */
    if (pay_usury_check(amount, amount, false) != PAY_OK) return PAY_ERR_USURY;
    set_line(req, 0, holder, -(int64_t) amount, 0);
    set_line(req, 1, issuer, 0, -(int64_t) amount);
    req->n_lines = 2;
    req->kind = PAY_KIND_REDEEM;
    return pay_ledger_post(L, req, rc);
}

pay_status_t pay_ledger_reverse(pay_ledger_t *L, pay_posting_req_t *req, const char *orig_uetr,
                                pay_receipt_t *rc)
{
    if (!L || !req) return PAY_ERR_ARG;
    pay_journal_t *o = (pay_journal_t *) pay_ledger_find_uetr(L, orig_uetr);
    if (!o) return PAY_ERR_NOT_FOUND;
    if (o->reversed) return PAY_ERR_STATE;
    for (uint32_t i = 0; i < o->n_lines; i++)
        set_line(req, i, o->lines[i].account, -o->lines[i].d_debit, -o->lines[i].d_credit);
    req->n_lines = o->n_lines;
    req->kind = PAY_KIND_RETURN;
    pay_status_t st = pay_ledger_post(L, req, rc);
    if (st == PAY_OK) o->reversed = true;
    return st;
}

pay_status_t pay_ledger_pay_tithed(pay_ledger_t *L, pay_posting_req_t *req, uint32_t from,
                                   uint32_t to, uint64_t amount, uint32_t commons,
                                   uint64_t contribution, uint32_t vfv_issuer, uint32_t vfv_to,
                                   uint64_t vfv_credit, pay_receipt_t *rc)
{
    uint64_t total;
    uint32_t n = 0;
    if (!L || !req || !amt_ok(amount) || !pay_add_ok(amount, contribution, &total) ||
        !amt_ok(total) || vfv_credit > (uint64_t) LINE_MAX_DELTA)
        return PAY_ERR_ARG;
    set_line(req, n++, from, -(int64_t) total, 0);
    set_line(req, n++, to, (int64_t) amount, 0);
    if (contribution) set_line(req, n++, commons, (int64_t) contribution, 0);
    if (vfv_credit) {
        set_line(req, n++, vfv_to, (int64_t) vfv_credit, 0);
        set_line(req, n++, vfv_issuer, 0, (int64_t) vfv_credit);
    }
    req->n_lines = n;
    req->kind = PAY_KIND_TITHE;
    return pay_ledger_post(L, req, rc);
}
