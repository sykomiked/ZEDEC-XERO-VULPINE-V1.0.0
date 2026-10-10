/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_dial_resolve.c — see zt_dial_resolve.h (R1-R3). */
#include "zt_dial_resolve.h"
#include "../robin_debanks/sha256.h"

bool zt_dial_rendezvous_id(const zt_phone_descriptor_t *desc, uint8_t id[CALL_RV_ID_LEN])
{
    char s[ZT_DIAL_MAX_CHARS];
    size_t n = zt_dial_format(desc, s, sizeof s);
    if (!n || !id) return false;
    /* s = "101--" digits; the canonical form is "+" digits. */
    s[4] = '+';
    static const uint8_t dom[] = ZT_DIAL_RV_DOMAIN;
    sha256_ctx_t c;
    sha256_init(&c);
    sha256_update(&c, dom, sizeof dom); /* includes the NUL separator */
    sha256_update(&c, (const uint8_t *) s + 4, n - 4u);
    sha256_final(&c, id);
    return true;
}

bool zt_dial_resolve_start(call_lookup_t *l, const zt_phone_descriptor_t *desc,
                           const call_rv_peer_t *seeds, uint32_t nseeds, uint32_t timeout_ms,
                           uint32_t txid0, call_rv_verify_fn verify, void *vctx)
{
    uint8_t id[CALL_RV_ID_LEN];
    if (!l || !verify || (!seeds && nseeds) || !zt_dial_rendezvous_id(desc, id)) return false;
    call_lookup_init(l, id, seeds, nseeds, timeout_ms, txid0, verify, vctx);
    return true;
}
