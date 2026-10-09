/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* cn_mobile.c — mobile-money wallet, push requests, USSD menus. See cn_mobile.h. */
#include "cn_mobile.h"
#include "cn_util.h"
#include "../mlkem/keccak.h"

#define NONE_IDX 0xFFFFFFFFu

/* ---- bounded text ---- */

typedef struct {
    char *buf;
    uint32_t cap, len; /* len excludes the NUL; cap includes room for it */
    bool err;
} txt_t;

static void tx_init(txt_t *t, char *buf, uint32_t cap)
{
    t->buf = buf;
    t->cap = cap;
    t->len = 0;
    t->err = (buf == 0 || cap == 0);
    if (!t->err) buf[0] = 0;
}

static void tx_str(txt_t *t, const char *s)
{
    if (t->err || !s) return;
    for (uint32_t i = 0; s[i]; i++) {
        /* Respect both the caller's buffer and the USSD screen limit. */
        if (t->len + 1 >= t->cap || t->len >= CN_USSD_MAX_TEXT) {
            t->err = true;
            return;
        }
        t->buf[t->len++] = s[i];
        t->buf[t->len] = 0;
    }
}

static void tx_amount(txt_t *t, uint64_t minor)
{
    char a[32];
    if (cn_mm_format_amount(minor, a, sizeof(a)) == 0) {
        t->err = true;
        return;
    }
    tx_str(t, a);
}

static void tx_u32(txt_t *t, uint32_t v, uint32_t width)
{
    char d[11];
    if (width == 0) {
        width = 1;
        while (width < 10 && v >= (uint32_t) cn_pow10_tab[width]) width++;
    }
    if (width > 10 || !cn_u64_to_dec_fixed(v, width, d)) {
        t->err = true;
        return;
    }
    d[width] = 0;
    tx_str(t, d);
}

/* ---- amounts ---- */

bool cn_mm_parse_amount(const char *s, uint64_t *minor)
{
    if (!s || !minor) return false;
    uint32_t n = cn_strnlen(s, 16);
    if (n == 0 || n >= 16) return false;
    uint32_t i = 0, ip = 0;
    uint64_t units = 0;
    while (i < n && cn_is_digit(s[i])) {
        units = units * 10u + (uint64_t) (s[i] - '0');
        i++;
        ip++;
    }
    if (ip == 0 || ip > 10) return false;
    uint64_t cents = 0;
    if (i < n) {
        if (s[i] != '.') return false;
        i++;
        uint32_t fp = 0;
        while (i < n && cn_is_digit(s[i]) && fp < 2) {
            cents = cents * 10u + (uint64_t) (s[i] - '0');
            i++;
            fp++;
        }
        if (fp == 0 || i != n) return false;
        if (fp == 1) cents *= 10u;
    }
    *minor = units * 100u + cents;
    return true;
}

uint32_t cn_mm_format_amount(uint64_t minor, char *out, uint32_t cap)
{
    char d[20];
    if (!out || cap == 0) return 0;
    cn_u64_to_dec_fixed(minor, 20, d); /* width 20 always fits */
    uint32_t first = 0;
    while (first < 17 && d[first] == '0') first++; /* keep at least "0.cc" */
    uint32_t need = (20 - 2 - first) + 1 + 2;
    if (need + 1 > cap) return 0;
    uint32_t k = 0;
    for (uint32_t i = first; i < 18; i++) out[k++] = d[i];
    out[k++] = '.';
    out[k++] = d[18];
    out[k++] = d[19];
    out[k] = 0;
    return k;
}

/* ---- wallets ---- */

static bool msisdn_ok(const char *m)
{
    uint32_t n = cn_strnlen(m, CN_MSISDN_MAX + 1);
    return n >= CN_MSISDN_MIN && n <= CN_MSISDN_MAX && cn_all_digits(m, n) && m[0] != '0';
}

static bool pin_ok(const char *p)
{
    uint32_t n = cn_strnlen(p, 7);
    return n >= 4 && n <= 6 && cn_all_digits(p, n);
}

static void pin_hash(const uint8_t salt[16], const char *pin, uint8_t out[32])
{
    uint8_t buf[18 + 16 + 1 + 6];
    uint32_t n = cn_strnlen(pin, 6), k = 0;
    cn_copy(buf, "ZXV-CARDNET-PIN-v1", 18);
    k = 18;
    cn_copy(buf + k, salt, 16);
    k += 16;
    buf[k++] = (uint8_t) n;
    cn_copy(buf + k, pin, n);
    k += n;
    sha3_256(buf, k, out);
}

cn_mm_rc_t cn_mm_init(cn_mm_hub_t *hub, uint32_t push_timeout_secs)
{
    if (!hub || push_timeout_secs == 0 || push_timeout_secs > 86400u) return CN_MM_ERR_ARG;
    cn_zero(hub, (uint32_t) sizeof(*hub));
    hub->push_timeout = push_timeout_secs;
    return CN_MM_OK;
}

cn_mm_rc_t cn_mm_find(const cn_mm_hub_t *hub, const char *msisdn, uint32_t *out_wallet)
{
    if (!hub || !out_wallet || !msisdn_ok(msisdn)) return CN_MM_ERR_ARG;
    uint32_t n = cn_strnlen(msisdn, CN_MSISDN_MAX);
    for (uint32_t i = 0; i < CN_MM_MAX_WALLETS; i++) {
        const cn_mm_wallet_t *w = &hub->wallets[i];
        if (w->used && cn_strnlen(w->msisdn, CN_MSISDN_MAX) == n && cn_eq(w->msisdn, msisdn, n)) {
            *out_wallet = i;
            return CN_MM_OK;
        }
    }
    return CN_MM_ERR_NOT_FOUND;
}

cn_mm_rc_t cn_mm_register(cn_mm_hub_t *hub, const char *msisdn, const char *pan, const char *pin,
                          const uint8_t salt[16], uint32_t *out_wallet)
{
    uint32_t w;
    if (!hub || !salt || !out_wallet || !msisdn_ok(msisdn) || !pin_ok(pin)) return CN_MM_ERR_ARG;
    if (!pan || cn_pan_network(pan, cn_strnlen(pan, CN_PAN_LEN + 1)) == CN_NET_NONE)
        return CN_MM_ERR_ARG;
    if (cn_mm_find(hub, msisdn, &w) == CN_MM_OK) return CN_MM_ERR_DUP;
    for (uint32_t i = 0; i < CN_MM_MAX_WALLETS; i++) {
        cn_mm_wallet_t *x = &hub->wallets[i];
        if (x->used) continue;
        cn_zero(x, (uint32_t) sizeof(*x));
        x->used = true;
        cn_copy(x->msisdn, msisdn, cn_strnlen(msisdn, CN_MSISDN_MAX));
        cn_copy(x->pan, pan, CN_PAN_LEN);
        cn_copy(x->pin_salt, salt, 16);
        pin_hash(salt, pin, x->pin_hash);
        *out_wallet = i;
        return CN_MM_OK;
    }
    return CN_MM_ERR_FULL;
}

cn_mm_rc_t cn_mm_relink(cn_mm_hub_t *hub, uint32_t wallet, const char *new_pan)
{
    if (!hub || wallet >= CN_MM_MAX_WALLETS || !hub->wallets[wallet].used) return CN_MM_ERR_ARG;
    if (!new_pan || cn_pan_network(new_pan, cn_strnlen(new_pan, CN_PAN_LEN + 1)) == CN_NET_NONE)
        return CN_MM_ERR_ARG;
    cn_copy(hub->wallets[wallet].pan, new_pan, CN_PAN_LEN);
    return CN_MM_OK;
}

cn_mm_rc_t cn_mm_verify_pin(cn_mm_hub_t *hub, uint32_t wallet, const char *pin)
{
    uint8_t h[32];
    if (!hub || wallet >= CN_MM_MAX_WALLETS || !hub->wallets[wallet].used) return CN_MM_ERR_ARG;
    cn_mm_wallet_t *w = &hub->wallets[wallet];
    if (w->locked) return CN_MM_ERR_LOCKED;
    bool ok = pin_ok(pin);
    if (ok) {
        pin_hash(w->pin_salt, pin, h);
        ok = cn_eq(h, w->pin_hash, 32);
    }
    if (ok) {
        w->pin_fails = 0;
        return CN_MM_OK;
    }
    if (++w->pin_fails >= CN_MM_PIN_TRIES) w->locked = true;
    return w->locked ? CN_MM_ERR_LOCKED : CN_MM_ERR_PIN;
}

cn_mm_rc_t cn_mm_unlock(cn_mm_hub_t *hub, uint32_t wallet)
{
    if (!hub || wallet >= CN_MM_MAX_WALLETS || !hub->wallets[wallet].used) return CN_MM_ERR_ARG;
    hub->wallets[wallet].locked = false;
    hub->wallets[wallet].pin_fails = 0;
    return CN_MM_OK;
}

/* ---- push requests ---- */

static bool printable_exact(const char *s, uint32_t n)
{
    if (!s || cn_strnlen(s, n + 1) != n) return false;
    for (uint32_t i = 0; i < n; i++)
        if (s[i] < 0x20 || s[i] > 0x7E) return false;
    return true;
}

cn_mm_rc_t cn_mm_push_create(cn_mm_hub_t *hub, const char *msisdn, uint64_t amount_minor,
                             const char *merchant_id, const char *terminal_id,
                             const char *reference, cn_time_t now, uint32_t *out_id)
{
    uint32_t w;
    if (!hub || !out_id || amount_minor == 0 || amount_minor > CN_AMOUNT_MAX_MINOR)
        return CN_MM_ERR_ARG;
    if (!printable_exact(merchant_id, CN_MID_LEN) || !printable_exact(terminal_id, CN_TID_LEN))
        return CN_MM_ERR_ARG;
    uint32_t rl = cn_strnlen(reference, CN_MM_REF_MAX + 1);
    if (!reference || rl > CN_MM_REF_MAX || !printable_exact(reference, rl)) return CN_MM_ERR_ARG;
    cn_mm_rc_t rc = cn_mm_find(hub, msisdn, &w);
    if (rc != CN_MM_OK) return rc;
    if (hub->wallets[w].locked) return CN_MM_ERR_LOCKED;
    if (hub->n_push >= CN_MM_MAX_PUSH) return CN_MM_ERR_FULL;
    if (now > 0xFFFFFFFFu - hub->push_timeout) return CN_MM_ERR_ARG;
    cn_mm_push_t *p = &hub->push[hub->n_push];
    cn_zero(p, (uint32_t) sizeof(*p));
    p->used = true;
    p->id = hub->n_push;
    p->wallet = w;
    p->amount_minor = amount_minor;
    cn_copy(p->merchant_id, merchant_id, CN_MID_LEN);
    cn_copy(p->terminal_id, terminal_id, CN_TID_LEN);
    cn_copy(p->reference, reference, rl);
    p->created = now;
    p->expires = now + hub->push_timeout;
    p->state = CN_PUSH_PENDING;
    *out_id = hub->n_push++;
    return CN_MM_OK;
}

void cn_mm_expire(cn_mm_hub_t *hub, cn_time_t now)
{
    if (!hub) return;
    for (uint32_t i = 0; i < hub->n_push; i++)
        if (hub->push[i].state == CN_PUSH_PENDING && now > hub->push[i].expires)
            hub->push[i].state = CN_PUSH_EXPIRED;
}

cn_mm_rc_t cn_mm_push_callback(cn_mm_hub_t *hub, const cn_mm_callback_t *cb, cn_time_t now)
{
    if (!hub || !cb) return CN_MM_ERR_ARG;
    if (cb->request_id >= hub->n_push) return CN_MM_ERR_NOT_FOUND;
    uint32_t rl = cn_strnlen(cb->op_receipt, CN_MM_RCPT_MAX + 1);
    if (rl > CN_MM_RCPT_MAX || !printable_exact(cb->op_receipt, rl)) return CN_MM_ERR_ARG;
    cn_mm_push_t *p = &hub->push[cb->request_id];
    uint32_t want = cb->result == 0 ? CN_PUSH_ACCEPTED : CN_PUSH_REJECTED;
    if (p->state == CN_PUSH_PENDING) {
        if (now > p->expires) {
            p->state = CN_PUSH_EXPIRED;
            return CN_MM_ERR_STATE;
        }
        p->state = want;
        p->result = cb->result;
        cn_copy(p->op_receipt, cb->op_receipt, rl);
        p->op_receipt[rl] = 0;
        return CN_MM_OK;
    }
    /* Idempotent replay: same outcome and receipt is fine. */
    bool same_state =
        p->state == want || (want == CN_PUSH_ACCEPTED && p->state == CN_PUSH_AUTHORIZED);
    if (same_state && p->result == cb->result &&
        cn_strnlen(p->op_receipt, CN_MM_RCPT_MAX + 1) == rl &&
        cn_eq(p->op_receipt, cb->op_receipt, rl))
        return CN_MM_OK;
    return CN_MM_ERR_CONFLICT;
}

cn_mm_rc_t cn_mm_push_cancel(cn_mm_hub_t *hub, uint32_t id)
{
    if (!hub || id >= hub->n_push) return CN_MM_ERR_NOT_FOUND;
    if (hub->push[id].state != CN_PUSH_PENDING) return CN_MM_ERR_STATE;
    hub->push[id].state = CN_PUSH_CANCELLED;
    return CN_MM_OK;
}

static cn_mm_rc_t fill_req(const cn_mm_wallet_t *w, uint64_t amount, const char *mid,
                           const char *tid, uint32_t form, uint32_t atc, const uint8_t un[4],
                           uint32_t stan, cn_time_t now, cn_auth_req_t *req)
{
    if (!un || !req || atc == 0 || atc > 0xFFFFu || stan > 999999u || form >= CN_FORM_COUNT)
        return CN_MM_ERR_ARG;
    if (!printable_exact(mid, CN_MID_LEN) || !printable_exact(tid, CN_TID_LEN))
        return CN_MM_ERR_ARG;
    cn_zero(req, (uint32_t) sizeof(*req));
    cn_copy(req->pan, w->pan, CN_PAN_LEN);
    req->amount_minor = amount;
    req->currency = CN_RAIL_DEBIT;
    req->form = form;
    req->atc = atc;
    cn_copy(req->un, un, 4);
    req->time = now;
    req->stan = stan;
    cn_copy(req->terminal_id, tid, CN_TID_LEN);
    cn_copy(req->merchant_id, mid, CN_MID_LEN);
    return CN_MM_OK;
}

cn_mm_rc_t cn_mm_push_to_auth(cn_mm_hub_t *hub, uint32_t id, uint32_t form, uint32_t atc,
                              const uint8_t un[4], uint32_t stan, cn_time_t now, cn_auth_req_t *req)
{
    if (!hub || id >= hub->n_push) return CN_MM_ERR_NOT_FOUND;
    cn_mm_push_t *p = &hub->push[id];
    if (p->state != CN_PUSH_ACCEPTED) return CN_MM_ERR_STATE;
    const cn_mm_wallet_t *w = &hub->wallets[p->wallet];
    if (!w->used || w->locked) return CN_MM_ERR_LOCKED;
    cn_mm_rc_t rc =
        fill_req(w, p->amount_minor, p->merchant_id, p->terminal_id, form, atc, un, stan, now, req);
    if (rc != CN_MM_OK) return rc;
    p->state = CN_PUSH_AUTHORIZED;
    return CN_MM_OK;
}

cn_mm_rc_t cn_mm_action_to_auth(const cn_mm_hub_t *hub, const cn_mm_action_t *act,
                                const char *terminal_id, uint32_t form, uint32_t atc,
                                const uint8_t un[4], uint32_t stan, cn_time_t now,
                                cn_auth_req_t *req)
{
    if (!hub || !act || act->kind != CN_MM_ACT_PAY || act->wallet >= CN_MM_MAX_WALLETS)
        return CN_MM_ERR_ARG;
    const cn_mm_wallet_t *w = &hub->wallets[act->wallet];
    if (!w->used || w->locked) return CN_MM_ERR_LOCKED;
    return fill_req(w, act->amount_minor, act->merchant_id, terminal_id, form, atc, un, stan, now,
                    req);
}

/* ---- USSD ---- */

static cn_mm_rc_t screen_end(cn_ussd_t *s, char *text, uint32_t cap, const char *msg)
{
    txt_t t;
    tx_init(&t, text, cap);
    tx_str(&t, "END ");
    tx_str(&t, msg);
    s->state = CN_USSD_ENDED;
    return t.err ? CN_MM_ERR_ARG : CN_MM_OK;
}

static cn_mm_rc_t screen_con(char *text, uint32_t cap, const char *msg)
{
    txt_t t;
    tx_init(&t, text, cap);
    tx_str(&t, "CON ");
    tx_str(&t, msg);
    return t.err ? CN_MM_ERR_ARG : CN_MM_OK;
}

static cn_mm_rc_t main_menu(const cn_mm_hub_t *hub, const cn_ussd_t *s, char *text, uint32_t cap)
{
    txt_t t;
    const char *pan = hub->wallets[s->wallet].pan;
    tx_init(&t, text, cap);
    tx_str(&t, "CON ");
    tx_str(&t, cn_network_name(cn_pan_network(pan, CN_PAN_LEN)));
    tx_str(&t, " card ..");
    char last4[5] = {pan[12], pan[13], pan[14], pan[15], 0};
    tx_str(&t, last4);
    tx_str(&t, "\n1 Pay merchant\n2 Statement balance\n3 Freeze card\n4 Replace card\n0 Exit");
    return t.err ? CN_MM_ERR_ARG : CN_MM_OK;
}

cn_mm_rc_t cn_ussd_begin(cn_mm_hub_t *hub, cn_ussd_t *s, const char *msisdn, cn_time_t now,
                         char *text, uint32_t cap)
{
    if (!hub || !s || !text || cap == 0) return CN_MM_ERR_ARG;
    cn_zero(s, (uint32_t) sizeof(*s));
    s->last = now;
    s->wallet = NONE_IDX;
    if (cn_mm_find(hub, msisdn, &s->wallet) != CN_MM_OK) {
        screen_end(s, text, cap, "This number has no card wallet.");
        return CN_MM_ERR_NOT_FOUND;
    }
    if (hub->wallets[s->wallet].locked) {
        screen_end(s, text, cap, "Wallet locked. Contact your issuer.");
        return CN_MM_ERR_LOCKED;
    }
    s->state = CN_USSD_MAIN;
    return main_menu(hub, s, text, cap);
}

static bool till_to_mid(const char *in, char mid[CN_MID_LEN + 1])
{
    uint32_t n = cn_strnlen(in, CN_TILL_MAX + 1);
    if (n < CN_TILL_MIN || n > CN_TILL_MAX || !cn_all_digits(in, n)) return false;
    for (uint32_t i = 0; i < CN_MID_LEN - n; i++) mid[i] = '0';
    cn_copy(mid + (CN_MID_LEN - n), in, n);
    mid[CN_MID_LEN] = 0;
    return true;
}

/* PIN step shared by every branch. Returns true when the PIN is good. */
static bool ussd_pin(cn_mm_hub_t *hub, cn_ussd_t *s, const char *input, char *text, uint32_t cap,
                     cn_mm_rc_t *rc)
{
    cn_mm_rc_t r = cn_mm_verify_pin(hub, s->wallet, input);
    if (r == CN_MM_OK) return true;
    *rc = r;
    screen_end(s, text, cap,
               r == CN_MM_ERR_LOCKED ? "Wrong PIN. Wallet locked." : "Wrong PIN. Try again later.");
    return false;
}

cn_mm_rc_t cn_ussd_input(cn_mm_hub_t *hub, cn_ussd_t *s, const cn_issuer_t *iss, const char *input,
                         cn_time_t now, char *text, uint32_t cap, cn_mm_action_t *act)
{
    if (!hub || !s || !input || !text || cap == 0 || !act) return CN_MM_ERR_ARG;
    cn_zero(act, (uint32_t) sizeof(*act));
    if (s->state == CN_USSD_ENDED || s->wallet >= CN_MM_MAX_WALLETS) {
        screen_end(s, text, cap, "Session ended.");
        return CN_MM_ERR_STATE;
    }
    if (now < s->last || now - s->last > CN_USSD_TIMEOUT) {
        screen_end(s, text, cap, "Session timed out.");
        return CN_MM_ERR_STATE;
    }
    s->last = now;
    cn_mm_rc_t rc = CN_MM_OK;
    uint32_t n = cn_strnlen(input, 32);
    switch (s->state) {
    case CN_USSD_MAIN:
        if (n == 1 && input[0] == '1') {
            s->state = CN_USSD_PAY_TILL;
            return screen_con(text, cap, "Enter merchant till number");
        }
        if (n == 1 && input[0] == '2') {
            s->state = CN_USSD_BAL_PIN;
            return screen_con(text, cap, "Enter PIN");
        }
        if (n == 1 && input[0] == '3') {
            s->state = CN_USSD_FREEZE_PIN;
            return screen_con(text, cap, "Freeze card. Enter PIN");
        }
        if (n == 1 && input[0] == '4') {
            s->state = CN_USSD_REPLACE_PIN;
            return screen_con(text, cap, "Replace card with a new number. Enter PIN");
        }
        if (n == 1 && input[0] == '0') return screen_end(s, text, cap, "Goodbye.");
        return main_menu(hub, s, text, cap);

    case CN_USSD_PAY_TILL:
        if (!till_to_mid(input, s->merchant_id))
            return screen_con(text, cap, "Till must be 4-10 digits. Enter merchant till number");
        s->state = CN_USSD_PAY_AMOUNT;
        return screen_con(text, cap, "Enter amount");

    case CN_USSD_PAY_AMOUNT:
        if (!cn_mm_parse_amount(input, &s->amount_minor) || s->amount_minor == 0 ||
            s->amount_minor > CN_AMOUNT_MAX_MINOR)
            return screen_con(text, cap, "Invalid amount. Enter amount");
        s->state = CN_USSD_PAY_PIN;
        return screen_con(text, cap, "Enter PIN");

    case CN_USSD_PAY_PIN: {
        if (!ussd_pin(hub, s, input, text, cap, &rc)) return rc;
        s->state = CN_USSD_PAY_CONFIRM;
        txt_t t;
        tx_init(&t, text, cap);
        tx_str(&t, "CON Pay ");
        tx_amount(&t, s->amount_minor);
        tx_str(&t, " to till ");
        tx_str(&t, s->merchant_id);
        tx_str(&t, "?\nPaid in full each statement. No interest.\n1 Confirm\n2 Cancel");
        return t.err ? CN_MM_ERR_ARG : CN_MM_OK;
    }

    case CN_USSD_PAY_CONFIRM:
        if (n == 1 && input[0] == '1') {
            act->kind = CN_MM_ACT_PAY;
            act->wallet = s->wallet;
            cn_copy(act->merchant_id, s->merchant_id, CN_MID_LEN + 1);
            act->amount_minor = s->amount_minor;
            return screen_end(s, text, cap, "Payment sent for authorization.");
        }
        return screen_end(s, text, cap, "Cancelled.");

    case CN_USSD_BAL_PIN: {
        if (!ussd_pin(hub, s, input, text, cap, &rc)) return rc;
        act->kind = CN_MM_ACT_BALANCE;
        act->wallet = s->wallet;
        uint32_t ci;
        if (!iss || cn_find_card(iss, hub->wallets[s->wallet].pan, &ci) != CN_OK)
            return screen_end(s, text, cap, "Balance unavailable.");
        const cn_account_t *a = &iss->accounts[iss->cards[ci].account];
        txt_t t;
        tx_init(&t, text, cap);
        tx_str(&t, "END Statement due: ");
        tx_amount(&t, a->billed);
        if (a->billed > 0) {
            cn_civil_t c;
            cn_civil_from_time(a->due_day * CN_SECS_PER_DAY, &c);
            tx_str(&t, " by ");
            tx_u32(&t, c.year, 4);
            tx_str(&t, "-");
            tx_u32(&t, c.month, 2);
            tx_str(&t, "-");
            tx_u32(&t, c.day, 2);
        }
        tx_str(&t, "\nThis cycle: ");
        tx_amount(&t, a->unbilled + a->held);
        tx_str(&t, "\nPay in full. No interest, ever.");
        if (a->state == CN_ACCT_DELINQUENT) tx_str(&t, "\nPast due: spending paused.");
        s->state = CN_USSD_ENDED;
        return t.err ? CN_MM_ERR_ARG : CN_MM_OK;
    }

    case CN_USSD_FREEZE_PIN:
        if (!ussd_pin(hub, s, input, text, cap, &rc)) return rc;
        act->kind = CN_MM_ACT_FREEZE;
        act->wallet = s->wallet;
        return screen_end(s, text, cap, "Card frozen. Dial again to replace it.");

    case CN_USSD_REPLACE_PIN:
        if (!ussd_pin(hub, s, input, text, cap, &rc)) return rc;
        act->kind = CN_MM_ACT_REPLACE;
        act->wallet = s->wallet;
        return screen_end(
            s, text, cap,
            "Replacement requested. The old card stops working once the new one is issued.");

    default:
        return screen_end(s, text, cap, "Session ended.");
    }
}
