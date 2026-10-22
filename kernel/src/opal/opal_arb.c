/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* opal_arb.c — arbiter key release to Opal unlock (opal_arb.h). */
#include "opal_arb.h"

int opal_arb_unlock(opal_dev_t *d, arb_host_t *h, uint32_t slot, opal_auth_t a, uint32_t range,
                    uint32_t polls, void (*wait)(void *), void *wctx, opal_unlock_result_t *res)
{
    opal_wipe(res, sizeof *res);
    if (d->in_session) return OPAL_E_STATE;
    int r = opal_discover(d);
    if (r != OPAL_OK) {
        res->stage = OPAL_STAGE_DISCOVERY;
        return r;
    }
    res->flags_before = res->flags_after = d->disc.locking_flags;
    if (!opal_needs_unlock(&d->disc)) return OPAL_OK;
    res->needed = 1;
    res->stage = OPAL_STAGE_KEY;
    if (!h || h->state != ARB_H_ATTESTED || h->tampered) return OPAL_E_NOT_ATTESTED;
    if (slot >= ARB_HOST_KEYS) return OPAL_E_ARG;
    if (arb_host_key_request(h, slot) != 0) {
        /* the ring is full: drain it once and try again */
        arb_host_poll(h);
        int q = arb_host_key_request(h, slot);
        if (q == -2) return OPAL_E_NOT_ATTESTED;
        if (q != 0) return OPAL_E_KEY;
    }
    uint8_t key[ARB_KEY_LEN];
    uint32_t st = 0;
    for (uint32_t k = 0; k < polls && st == 0; k++) {
        if (wait) wait(wctx);
        arb_host_poll(h);
        st = arb_host_take_key(h, slot, key);
    }
    res->key_status = st;
    if (st != ARB_KEY_OK) {
        opal_wipe(key, sizeof key);
        return st == 0 && h->state != ARB_H_ATTESTED ? OPAL_E_NOT_ATTESTED : OPAL_E_KEY;
    }
    r = opal_unlock(d, a, key, ARB_KEY_LEN, range, res);
    opal_wipe(key, sizeof key);
    res->key_status = st;
    return r;
}
