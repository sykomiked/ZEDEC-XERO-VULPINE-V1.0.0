/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* recovery.c — ledger-side account recovery control. See recovery.h R1-R6. */
#include "recovery.h"
#include "../pay/pay_util.h"

void rec_cfg_default(rec_cfg_t *cfg, uint64_t ticks_per_day)
{
    if (!cfg) return;
    pay_memset(cfg, 0, sizeof *cfg);
    cfg->ticks_per_day = ticks_per_day;
    cfg->rate_max = REC_DEFAULT_RATE;
    /* overflow leaves 0, which rec_init refuses */
    if (__builtin_mul_overflow(ticks_per_day, (uint64_t) REC_DEFAULT_TDAYS, &cfg->timelock_ticks))
        cfg->timelock_ticks = 0;
    if (__builtin_mul_overflow(ticks_per_day, (uint64_t) REC_DEFAULT_WDAYS,
                               &cfg->rate_window_ticks))
        cfg->rate_window_ticks = 0;
}

static void notify_all(const rec_account_t *r, rec_event_t ev)
{
    if (!r->notify) return;
    for (uint32_t g = 0; g < REC_GUARDIANS; g++) r->notify(r->nctx, g, ev, r->claim_id);
}

/* the slot holding key, REC_MAX_DEVICES when none */
static uint32_t dev_find(const rec_account_t *r, const uint8_t key[REC_KEY])
{
    uint32_t i = 0;
    while (i < REC_MAX_DEVICES && !(r->dev_used[i] && pay_memeq(r->device[i], key, REC_KEY))) i++;
    return i;
}

rec_status_t rec_init(rec_account_t *r, const rec_cfg_t *cfg, const uint8_t first_device[REC_KEY],
                      rec_verify_fn verify, void *vctx, rec_notify_fn notify, void *nctx)
{
    if (!r || !cfg || !first_device || !verify) return REC_ERR_ARG;
    if (!cfg->ticks_per_day || !cfg->timelock_ticks || !cfg->rate_window_ticks || !cfg->rate_max ||
        cfg->rate_max > REC_RATE_CAP)
        return REC_ERR_ARG;
    pay_memset(r, 0, sizeof *r);
    r->cfg = *cfg;
    r->verify = verify;
    r->vctx = vctx;
    r->notify = notify;
    r->nctx = nctx;
    pay_memcpy(r->device[0], first_device, REC_KEY);
    r->dev_used[0] = true;
    return REC_OK;
}

static void put64(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; i++) p[i] = (uint8_t) (v >> (8 * i));
}

/* tag (14) || kind || a (32) || n (8, LE) || b (32) */
static void msg(uint8_t kind, const uint8_t *a, uint64_t n, const uint8_t *b, uint8_t *out)
{
    static const char tag[] = "zxv/rec-msg/v1";
    out[14] = kind;
    for (uint32_t i = 0; i < 14; i++) out[i] = (uint8_t) tag[i];
    pay_memcpy(out + 15, a, REC_KEY);
    put64(out + 15 + REC_KEY, n);
    pay_memcpy(out + 23 + REC_KEY, b, REC_KEY);
}

void rec_device_msg(const rec_account_t *r, uint8_t kind, const uint8_t key[REC_KEY],
                    uint8_t out[REC_DEV_MSG])
{
    msg(kind, r->cfg.root, r->dev_seq, key, out);
}

void rec_claim_msg(const rec_account_t *r, uint8_t kind, uint8_t out[REC_CLAIM_MSG])
{
    msg(kind, r->cfg.root, r->claim_id, r->new_key, out);
}

static bool root_signed(const rec_account_t *r, uint8_t kind, const uint8_t key[REC_KEY],
                        const uint8_t *sig, uint32_t sig_len)
{
    uint8_t m[REC_DEV_MSG];
    rec_device_msg(r, kind, key, m);
    return r->verify(r->vctx, r->cfg.root, m, sizeof m, sig, sig_len);
}

rec_status_t rec_device_add(rec_account_t *r, const uint8_t key[REC_KEY], const uint8_t *sig,
                            uint32_t sig_len)
{
    if (!r || !key || !sig) return REC_ERR_ARG;
    if (dev_find(r, key) < REC_MAX_DEVICES) return REC_DUP;
    uint32_t slot = 0;
    while (slot < REC_MAX_DEVICES && r->dev_used[slot]) slot++;
    if (slot == REC_MAX_DEVICES) return REC_ERR_FULL;
    if (!root_signed(r, REC_MSG_DEV_ADD, key, sig, sig_len)) return REC_ERR_SIG;
    pay_memcpy(r->device[slot], key, REC_KEY);
    r->dev_used[slot] = true;
    r->dev_seq++;
    return REC_OK;
}

rec_status_t rec_device_revoke(rec_account_t *r, const uint8_t key[REC_KEY], const uint8_t *sig,
                               uint32_t sig_len)
{
    if (!r || !key || !sig) return REC_ERR_ARG;
    uint32_t i = dev_find(r, key);
    if (i == REC_MAX_DEVICES) return REC_ERR_DEVICE;
    if (!root_signed(r, REC_MSG_DEV_DEL, key, sig, sig_len)) return REC_ERR_SIG;
    r->dev_used[i] = false;
    r->dev_seq++;
    return REC_OK;
}

bool rec_device_active(const rec_account_t *r, const uint8_t key[REC_KEY])
{
    return r && key && dev_find(r, key) < REC_MAX_DEVICES;
}

/* R1: opens in (tick - window, tick] */
static uint32_t recent_opens(const rec_account_t *r, uint64_t tick)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < REC_RATE_CAP && i < r->n_opens; i++)
        if (tick - r->opens[i] < r->cfg.rate_window_ticks) n++;
    return n;
}

rec_status_t rec_claim_open(rec_account_t *r, const uint8_t new_device_key_hash[REC_KEY],
                            uint64_t tick)
{
    if (!r || !new_device_key_hash) return REC_ERR_ARG;
    if (r->state == REC_PENDING) return REC_ERR_BUSY;
    if (dev_find(r, new_device_key_hash) < REC_MAX_DEVICES) return REC_ERR_ARG;
    if (r->n_opens && tick < r->opens[(r->n_opens - 1) % REC_RATE_CAP]) return REC_ERR_ARG;
    if (recent_opens(r, tick) >= r->cfg.rate_max) return REC_ERR_RATE;
    r->opens[r->n_opens % REC_RATE_CAP] = tick;
    r->n_opens++;
    r->claim_id++;
    pay_memcpy(r->new_key, new_device_key_hash, REC_KEY);
    r->opened = tick;
    r->approved = 0;
    r->vetoed = false;
    r->state = REC_PENDING;
    notify_all(r, REC_EV_CLAIM);
    return REC_OK;
}

static bool claim_signed(const rec_account_t *r, uint8_t kind, const uint8_t key[REC_KEY],
                         const uint8_t *sig, uint32_t sig_len)
{
    uint8_t m[REC_CLAIM_MSG];
    rec_claim_msg(r, kind, m);
    return r->verify(r->vctx, key, m, sizeof m, sig, sig_len);
}

rec_status_t rec_approve(rec_account_t *r, uint32_t guardian, const uint8_t *sig, uint32_t sig_len)
{
    if (!r || !sig) return REC_ERR_ARG;
    if (r->state != REC_PENDING) return REC_ERR_STATE;
    if (guardian >= REC_GUARDIANS) return REC_ERR_GUARDIAN;
    if (r->approved & (1u << guardian)) return REC_DUP;
    if (!claim_signed(r, REC_MSG_APPROVE, r->cfg.guardian[guardian], sig, sig_len))
        return REC_ERR_SIG;
    r->approved |= (uint8_t) (1u << guardian);
    notify_all(r, REC_EV_APPROVE);
    return REC_OK;
}

static rec_status_t old_device(const rec_account_t *r, uint8_t kind, const uint8_t dev[REC_KEY],
                               const uint8_t *sig, uint32_t sig_len)
{
    if (!r || !dev || !sig) return REC_ERR_ARG;
    if (r->state != REC_PENDING) return REC_ERR_STATE;
    if (dev_find(r, dev) == REC_MAX_DEVICES) return REC_ERR_DEVICE;
    if (!claim_signed(r, kind, dev, sig, sig_len)) return REC_ERR_SIG;
    return REC_OK;
}

rec_status_t rec_veto(rec_account_t *r, const uint8_t device[REC_KEY], const uint8_t *sig,
                      uint32_t sig_len)
{
    rec_status_t st = old_device(r, REC_MSG_VETO, device, sig, sig_len);
    if (st != REC_OK) return st;
    r->vetoed = true;
    notify_all(r, REC_EV_VETO);
    return REC_OK;
}

rec_status_t rec_cancel(rec_account_t *r, const uint8_t device[REC_KEY], const uint8_t *sig,
                        uint32_t sig_len)
{
    rec_status_t st = old_device(r, REC_MSG_CANCEL, device, sig, sig_len);
    if (st != REC_OK) return st;
    r->state = r->finalised ? REC_THAWING : REC_IDLE;
    notify_all(r, REC_EV_CANCEL);
    return REC_OK;
}

uint32_t rec_approvals(const rec_account_t *r)
{
    if (!r) return 0;
    uint32_t n = 0;
    for (uint32_t x = r->approved; x; x &= x - 1) n++;
    return n;
}

/* *out = a + days * ticks_per_day, false on overflow */
static bool after_days(const rec_account_t *r, uint64_t a, uint64_t days, uint64_t *out)
{
    uint64_t d;
    if (__builtin_mul_overflow(days, r->cfg.ticks_per_day, &d)) return false;
    return !__builtin_add_overflow(a, d, out);
}

rec_status_t rec_finalize(rec_account_t *r, uint64_t tick)
{
    if (!r) return REC_ERR_ARG;
    if (r->state != REC_PENDING) return REC_ERR_STATE;
    uint32_t need = r->vetoed ? REC_VETO_QUORUM : REC_QUORUM;
    if (rec_approvals(r) < need) return REC_ERR_QUORUM;
    uint64_t due;
    bool ok = r->vetoed ? after_days(r, r->opened, REC_VETO_DAYS, &due)
                        : !__builtin_add_overflow(r->opened, r->cfg.timelock_ticks, &due);
    if (!ok || tick < due) return REC_ERR_TIME;
    pay_memset(r->dev_used, 0, sizeof r->dev_used);
    pay_memcpy(r->device[0], r->new_key, REC_KEY);
    r->dev_used[0] = true;
    r->dev_seq++;
    r->state = REC_THAWING;
    r->finalised = true;
    r->fin_tick = tick;
    r->gateway_ok = false;
    notify_all(r, REC_EV_FINAL);
    return REC_OK;
}

rec_status_t rec_gateway_verify(rec_account_t *r, const uint8_t *sig, uint32_t sig_len)
{
    if (!r || !sig) return REC_ERR_ARG;
    if (r->state != REC_THAWING) return REC_ERR_STATE;
    if (!claim_signed(r, REC_MSG_GATEWAY, r->cfg.gateway, sig, sig_len)) return REC_ERR_SIG;
    r->gateway_ok = true;
    return REC_OK;
}

bool rec_can_view(const rec_account_t *r)
{
    return r != 0;
}

bool rec_can_spend(const rec_account_t *r)
{
    return r && r->state != REC_PENDING;
}

uint64_t rec_vbill_spendable(const rec_account_t *r, uint64_t vbill_balance, uint64_t tick)
{
    if (!rec_can_spend(r)) return 0;
    if (r->state == REC_IDLE) return vbill_balance;
    uint64_t due;
    if (!after_days(r, r->fin_tick, REC_VBILL_DAYS, &due) || tick < due) return 0;
    return vbill_balance;
}

uint64_t rec_liquid_allowance(const rec_account_t *r, uint64_t balance_at_finalise, uint64_t tick)
{
    if (!rec_can_spend(r)) return 0;
    if (r->state == REC_IDLE || r->gateway_ok) return balance_at_finalise;
    if (tick < r->fin_tick) return 0;
    uint64_t days = (tick - r->fin_tick) / r->cfg.ticks_per_day, out;
    if (days >= REC_LIQ_DAYS) return balance_at_finalise;
    /* floor(balance * 5 * (days + 1) / 100); the quotient is < balance */
    pay_muldiv(balance_at_finalise, REC_LIQ_PCT * (days + 1), 100, &out, 0);
    return out;
}
