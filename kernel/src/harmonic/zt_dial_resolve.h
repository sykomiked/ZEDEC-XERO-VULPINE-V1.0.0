/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_dial_resolve.h — the dial router's hook into the call engine's signed
 * DHT rendezvous (kernel/src/call/call_ice.h: call_rv_*, call_lookup_*).
 *
 *   R1  RENDEZVOUS ID.  A parsed number maps to a 32-byte DHT target:
 *       SHA-256("ZXV-DIAL-1" || 0x00 || "+" CC NATIONAL), the number in
 *       canonical E.164 digits (zt_dial_format without the "101--"). Every
 *       caller derives the same id, so the subscriber REGISTERs its record
 *       under it and anyone who dials the number looks it up.
 *   R2  TRUST.  The id is public: anyone can compute it, so it authenticates
 *       nothing. call_lookup accepts a FOUND record only when its id is the
 *       target AND the caller's verify callback accepts the record's
 *       signature (over call_rv_record_signed_bytes). That callback must
 *       check the signature against a key the caller already trusts for
 *       this number (a contact entry, a directory signature); the number
 *       alone never binds a key. Without such a verifier, refuse the call.
 *   R3  The answer is the subscriber's ICE candidates (real addresses from
 *       the record). The virtual coordinates of zt_dial_router.h D5 play no
 *       part in it and must never be used as an address.
 */
#ifndef ZT_DIAL_RESOLVE_H
#define ZT_DIAL_RESOLVE_H

#include "zt_dial_router.h"
#include "../call/call_ice.h"

#define ZT_DIAL_RV_DOMAIN "ZXV-DIAL-1"

/* R1. False if desc is invalid. */
bool zt_dial_rendezvous_id(const zt_phone_descriptor_t *desc, uint8_t id[CALL_RV_ID_LEN]);

/* R1 + call_lookup_init: starts an iterative lookup of the number. verify is
 * required (R2): with verify == NULL nothing is started and false returned.
 * The caller then drives call_lookup_next / call_lookup_on_reply as usual. */
bool zt_dial_resolve_start(call_lookup_t *lookup, const zt_phone_descriptor_t *desc,
                           const call_rv_peer_t *seeds, uint32_t nseeds, uint32_t timeout_ms,
                           uint32_t txid0, call_rv_verify_fn verify, void *vctx);

#endif /* ZT_DIAL_RESOLVE_H */
