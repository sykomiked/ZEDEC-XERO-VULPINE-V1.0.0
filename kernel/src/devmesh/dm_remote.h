/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* dm_remote.h — using the home node from the phone, and what runs where.
 *
 * CAPABILITIES. Each device describes itself in a dm_caps_t (RAM, compute
 * score, battery, network kind, features, the models it can hold) and sends
 * it to every peer when a session opens and whenever it changes
 * (dm_caps_update). dm_caps_fit() fills the model list from the catalogue
 * below: a model is offered only if its resident size fits the device's
 * budget (half the RAM on a phone, three quarters on a home node).
 *
 * ROUTING (dm_route). For a request of a given kind and model class:
 *   1. Money actions are never routed silently: they need dm_money_*.
 *   2. If a reachable peer (session up, heard within DM_LIVE_MS) has the
 *      needed feature, prefer a HOME device, then the one with the larger
 *      compute score. A large-model request goes there even on a metered
 *      link; a small-model request stays local on a metered link when the
 *      local device can run it, unless the local battery is low (< 20 %,
 *      not charging) or the setting DM_KEY_ALLOW_METERED is "1".
 *   3. Otherwise, if this device can run a model of the class (or a small
 *      model for a large request: DM_ROUTE_LOCAL_DEGRADED), run locally.
 *   4. Otherwise DM_ROUTE_NEED_CAPACITY when online (capacity can be bought
 *      on the network, kernel/src/capmkt) or DM_ROUTE_UNAVAILABLE offline.
 *   Link class (MOBILE_COMMON 3.3): on DM_NET_SATELLITE no media is sent and
 *   the metered rule applies; on DM_NET_LORA no prompt, file or media
 *   request is sent. A request that would have gone out but cannot ride the
 *   link gets DM_ROUTE_WAIT_LINK, unless this device can run it.
 *
 * ROUTE POLICY (dm_policy_set, MOBILE_COMMON 3.2). The owner picks, per
 * device and per kind of task (prompt, file, media), one of:
 *   DM_POL_AUTO    the rule above, unchanged (the default; no entry).
 *   DM_POL_HERE    never leaves this device: LOCAL, LOCAL_DEGRADED or
 *                  UNAVAILABLE. Never REMOTE, never NEED_CAPACITY.
 *   DM_POL_HOME    always a home device: LOCAL if this device is itself a
 *                  home device that can run it, else the best live HOME peer
 *                  (even on a metered link or a low battery), else
 *                  UNAVAILABLE. Never a local fallback on a non-home device.
 *   DM_POL_PINNED  always the named device: REMOTE to it if it is live,
 *                  ACTIVE in the roster and has the feature, else
 *                  UNAVAILABLE. Pinned to this device behaves as DM_POL_HERE.
 * dm_route() applies the entry for this device (dm_self_id); entries for
 * other devices are kept so a configuring device can hold the owner's
 * choices. dm_request() refuses with DM_ERR_PERM a send the policy forbids,
 * and with DM_ERR_TRANSPORT one the link class forbids. Money actions are
 * not policy-routed: they always follow MONEY CONFIRMATION.
 *
 * REMOTE REQUESTS. dm_request() sends a prompt, file action or wallet action
 * to a peer and remembers it; the peer's host answers with dm_reply()
 * chunks, which arrive at host.on_reply in order (final = true on the last).
 * If a request gets no chunk for DM_REQ_TIMEOUT_MS, or the peer's session
 * goes down, the host gets DM_EV_REQ_FALLBACK with the request id and can
 * re-run it on the device (graceful fallback to the small local model).
 * A request sent under DM_POL_HOME or DM_POL_PINNED gets
 * DM_EV_REQ_UNAVAILABLE instead: its policy forbids running it elsewhere.
 *
 * MONEY CONFIRMATION. A wallet action is only executed with a dm_confirm_t
 * that dm_money_verify() accepts: approve = 1, signed (pq_matrix dual
 * signature, context "zxv-devmesh/v1/money") by a device that is ACTIVE and
 * DM_FLAG_HELD in the verifier's roster, over the exact action (amount,
 * rail 555/777/888, currency VFV, payee, memo, expiry), not expired and not
 * seen before. The home node can never confirm for the user: an action it
 * starts is sent to a held device (dm_money_ask), shown to the user there
 * (host.on_money), and answered (dm_money_answer). An action started on the
 * phone is confirmed on the phone itself before it is sent.
 * There is no interest, fee accrual or late charge anywhere in this path:
 * the amount confirmed is the amount moved.
 */
#ifndef ZXV_DM_REMOTE_H
#define ZXV_DM_REMOTE_H

#include "devmesh.h"

#define DM_REQ_TIMEOUT_MS 20000u
#define DM_CHUNK_MAX      4096u

/* request kinds */
#define DM_REQ_PROMPT 1u /* assistant prompt (UTF-8) */
#define DM_REQ_FILE   2u /* file action (host-defined encoding) */
#define DM_REQ_WALLET 3u /* wallet action: must carry a verified confirmation */
#define DM_REQ_MEDIA  4u

/* ===== model catalogue ===== */
/* Recommended on-device and home models (estimates documented in
 * docs/MOBILE_AND_DEVICES.md). Returns NULL past the end. */
const dm_model_t *dm_model_catalog(uint32_t i);

/* Fill caps->models from the catalogue for this device's RAM and role. */
void dm_caps_fit(dm_caps_t *caps, uint8_t role);
/* Encode / decode for the wire (fixed layout). */
#define DM_CAPS_WIRE (16u + 4u + DM_MAX_MODELS * (DM_MODEL_NAME + 4u * 3u + 3u))
int32_t dm_caps_encode(const dm_caps_t *c, uint8_t *out, uint32_t cap);
dm_status_t dm_caps_decode(dm_caps_t *c, const uint8_t *in, uint32_t len);
bool dm_caps_can_run(const dm_caps_t *c, uint8_t cls);

/* Set this device's capabilities and send them to every live peer. */
dm_status_t dm_caps_update(dm_mesh_t *m, const dm_caps_t *caps, uint64_t now_ms);

/* ===== routing ===== */
typedef enum {
    DM_ROUTE_REMOTE = 1,
    DM_ROUTE_LOCAL = 2,
    DM_ROUTE_LOCAL_DEGRADED = 3, /* large request, small local model */
    DM_ROUTE_NEED_CAPACITY = 4,
    DM_ROUTE_UNAVAILABLE = 5,
    DM_ROUTE_CONFIRM = 6,   /* money: confirm on a held device first */
    DM_ROUTE_WAIT_LINK = 7, /* the current link cannot carry it: wait for a better one */
} dm_route_t;

/* ===== route policy ===== */
/* Set the policy (DM_POL_*) for task kind req_kind (DM_REQ_PROMPT, _FILE,
 * _MEDIA) on device dev. pin names the device for DM_POL_PINNED (ignored
 * otherwise; it need not be paired yet: routing answers UNAVAILABLE until
 * it is live). DM_POL_AUTO removes the entry. DM_ERR_PERM for
 * DM_REQ_WALLET, DM_ERR_ARG for a zero id or unknown kind / policy,
 * DM_ERR_FULL when all DM_POL_MAX entries are in use. */
dm_status_t dm_policy_set(dm_mesh_t *m, const uint8_t dev[DM_ID_BYTES], uint8_t req_kind,
                          uint8_t policy, const uint8_t pin[DM_ID_BYTES]);
/* The policy in force (DM_POL_AUTO if none); pin (may be NULL) receives the
 * pinned device, or zeros. */
uint8_t dm_policy_get(const dm_mesh_t *m, const uint8_t dev[DM_ID_BYTES], uint8_t req_kind,
                      uint8_t pin[DM_ID_BYTES]);

dm_route_t dm_route(const dm_mesh_t *m, uint8_t req_kind, uint8_t model_cls, uint64_t now_ms,
                    uint8_t target[DM_ID_BYTES]);

/* ===== remote requests ===== */
/* Sends; *req_id receives the id. Wallet requests must start with the
 * encoded money action and confirmation (dm_wallet_request). */
dm_status_t dm_request(dm_mesh_t *m, const uint8_t peer[DM_ID_BYTES], uint8_t kind,
                       const uint8_t *payload, uint32_t len, uint64_t now_ms, uint32_t *req_id);
/* Home node: answer request req_id from `to` (any number of chunks). */
dm_status_t dm_reply(dm_mesh_t *m, const uint8_t to[DM_ID_BYTES], uint32_t req_id,
                     const uint8_t *data, uint32_t len, bool final, uint64_t now_ms);

/* ===== money ===== */
#define DM_MONEY_WIRE                                                                              \
    (DM_ID_BYTES + 1u + 2u + 4u + 8u + DM_HASH_BYTES + DM_MONEY_MEMO + DM_ID_BYTES + 8u)
#define DM_CONFIRM_WIRE (DM_HASH_BYTES + DM_ID_BYTES + 1u + 4u + PQM_SIG_MAX_BYTES)

int32_t dm_money_encode(const dm_money_t *a, uint8_t *out, uint32_t cap);
dm_status_t dm_money_decode(dm_money_t *a, const uint8_t *in, uint32_t len);
int32_t dm_confirm_encode(const dm_confirm_t *c, uint8_t *out, uint32_t cap);
dm_status_t dm_confirm_decode(dm_confirm_t *c, const uint8_t *in, uint32_t len);

/* This device (must be ACTIVE + HELD) records the user's decision. */
dm_status_t dm_money_confirm_local(dm_mesh_t *m, const dm_money_t *a, bool approve, uint64_t now_ms,
                                   dm_confirm_t *out);
/* Check a confirmation (see header comment). Marks the action seen on
 * success, so a second call with the same action fails with DM_ERR_REPLAY. */
dm_status_t dm_money_verify(dm_mesh_t *m, const dm_money_t *a, const dm_confirm_t *c,
                            uint64_t now_ms);
/* Ask held device `peer` to confirm `a`; the answer arrives at
 * host.on_money_answer, already verified. Only `peer` can answer (approve
 * or decline): a MONEY_ANSWER from any other device is dropped and the
 * request stays pending. Asking again replaces the pending request. */
dm_status_t dm_money_ask(dm_mesh_t *m, const uint8_t peer[DM_ID_BYTES], const dm_money_t *a,
                         uint64_t now_ms);
/* Held device: answer the last action shown by host.on_money. */
dm_status_t dm_money_answer(dm_mesh_t *m, bool approve, uint64_t now_ms);

/* Phone side: confirm locally, then send the wallet request (money action
 * and confirmation as its payload) to the device holding the wallet. */
dm_status_t dm_wallet_request(dm_mesh_t *m, const uint8_t peer[DM_ID_BYTES], const dm_money_t *a,
                              uint64_t now_ms, uint32_t *req_id);
/* Home side: parse and verify a DM_REQ_WALLET payload. */
dm_status_t dm_wallet_check(dm_mesh_t *m, const uint8_t *payload, uint32_t len, uint64_t now_ms,
                            dm_money_t *out);

#endif /* ZXV_DM_REMOTE_H */
