/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* opal_arb.h — unlock an Opal drive with a key the arbiter releases.
 *
 * OEM_INSTITUTIONAL.md 6.3 and SERVER_COMMON.md 5: a drive's authentication
 * secret is released by the arbiter only after a good measured boot. Here
 * the arbiter's key slot holds that secret itself (arb_dev.h KEYS); it is
 * used as the Locking SP password of the chosen authority. */
#ifndef ZXV_OPAL_ARB_H
#define ZXV_OPAL_ARB_H

#include "opal.h"
#include "../arbiter/arb_host.h"

/* Discover first: a drive that is not an SED or not locked never costs a
 * key. Otherwise the arbiter must be attested; the key for `slot` is
 * requested, the host polls it up to `polls` times (calling wait(wctx)
 * before each poll; wait may be 0), the drive is unlocked with it
 * (opal_unlock), and every copy of the key on this side is wiped. Returns
 * OPAL_OK (res->needed says whether anything was unlocked), an opal_unlock
 * error, OPAL_E_NOT_ATTESTED, OPAL_E_KEY (res->key_status holds the
 * arbiter's ARB_KEY_* answer, 0 if none came) or OPAL_E_ARG for a bad slot. */
int opal_arb_unlock(opal_dev_t *d, arb_host_t *h, uint32_t slot, opal_auth_t a, uint32_t range,
                    uint32_t polls, void (*wait)(void *), void *wctx, opal_unlock_result_t *res);

#endif
