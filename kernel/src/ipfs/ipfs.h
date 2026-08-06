/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* ipfs.h — the smuggler's hold: content has no name but its own fingerprint.
 *
 * A real content-addressed layer that reuses the kernel's existing 32-byte
 * SHA-256 CID (src/robin_debanks/sha256), a SELF-CERTIFYING get (fetched bytes
 * that do not hash to the CID you asked for are rejected — no port authority
 * can rename or seize what is addressed by its own hash), a transport OPS
 * BOUNDARY (bind your own IPFS account by supplying a fetch adapter — nothing
 * is invented when it is absent), and a one-call install into the Web2/Web3/Web4
 * bridge's resolve_cid slot.
 *
 * The transport is the same shape the update system already trusts
 * (upd_transport_t) — one content-addressed fetch, ctx-carried, fail-closed.
 *
 * Freestanding: integer only, no allocation, no floating point, no libc.
 */
#ifndef ZXV_IPFS_H
#define ZXV_IPFS_H

#include <stdint.h>
#include "update.h"   /* upd_transport_t — reused verbatim, not re-invented */
#include "bridge.h"   /* bridge_t + bridge_ops_t.resolve_cid install point   */

#define IPFS_CID_LEN     32u   /* a CID is exactly one SHA-256 digest         */
#define IPFS_HEX_LEN     64u   /* ...which is 64 lowercase hex characters     */

typedef enum {
    IPFS_OK = 0,
    IPFS_ERR_NO_TRANSPORT,   /* no fetch adapter bound — fail closed          */
    IPFS_ERR_CID_MISMATCH,   /* returned bytes do not hash to the CID asked   */
    IPFS_ERR_NOT_FOUND,      /* the transport could not retrieve the content  */
    IPFS_ERR_BAD_CID         /* URI/CID malformed (wrong length, bad hex...)  */
} ipfs_result_t;

typedef enum {
    IPFS_SCHEME_CID = 0,     /* "cid:<64hex>"                                 */
    IPFS_SCHEME_IPFS,        /* "ipfs://<64hex>"                              */
    IPFS_SCHEME_AIPI         /* "aipi://<64hex>"                              */
} ipfs_uri_scheme_t;

/* The transport ops boundary. The update system's shape fits exactly, so we
 * reuse it rather than mint a parallel type: one content-addressed fetch that
 * returns 0 on success and fills *out_len, carrying its own ctx. With no
 * adapter bound, every fetch fails — bytes are never fabricated. */
typedef upd_transport_t ipfs_transport_t;

typedef struct {
    const ipfs_transport_t *t;   /* NULL until you hook up your IPFS account  */
} ipfs_node_t;

/* ---- CID primitives (all local, all honest — no transport required) ---- */

/* out_cid = sha256(data, len). Returns 0 on success, -1 on a NULL out. */
int32_t ipfs_cid_from_bytes(const uint8_t *data, uint32_t len,
                            uint8_t out_cid[IPFS_CID_LEN]);

/* Format a CID as a URI: "cid:<64hex>", "ipfs://<64hex>", "aipi://<64hex>".
 * Returns the number of characters written (excluding the NUL), or -1 if the
 * buffer is too small or an argument is NULL / the scheme is unknown. */
int32_t ipfs_cid_format(const uint8_t cid[IPFS_CID_LEN], ipfs_uri_scheme_t scheme,
                        char *out, uint32_t cap);

/* Parse the 64 hex characters that follow a known scheme into out_cid. A CID
 * that is 32 hex (an MD5) or 40 hex (a SHA-1) — anything not exactly 64 hex —
 * is rejected with IPFS_ERR_BAD_CID, as is an unknown scheme. */
ipfs_result_t ipfs_cid_parse(const char *uri, uint8_t out_cid[IPFS_CID_LEN]);

/* ---- node lifecycle + transport binding ---- */
void ipfs_node_init(ipfs_node_t *n);
void ipfs_set_transport(ipfs_node_t *n, const ipfs_transport_t *t);

/* ---- the self-certifying get ----
 * Fetch the content for `cid` over the bound transport, RECOMPUTE the SHA-256
 * over exactly the bytes that came back, and return IPFS_ERR_CID_MISMATCH if it
 * differs from `cid`. On IPFS_OK, buf holds the content and *out_len its length.
 * With no transport bound, returns IPFS_ERR_NO_TRANSPORT and leaves buf
 * untouched. IPFS_ERR_NOT_FOUND if the transport could not retrieve it. */
ipfs_result_t ipfs_get_verify(ipfs_node_t *n, const uint8_t cid[IPFS_CID_LEN],
                              uint8_t *buf, uint32_t cap, uint32_t *out_len);

/* ---- add + pin ----
 * Computes the CID of `data` (always — a local truth is not a hollow claim,
 * so out_cid is filled even with no transport) and confirms the content is
 * actually served under that CID by round-tripping it through the transport.
 * Returns IPFS_ERR_NO_TRANSPORT with no adapter bound (never claims "pinned"
 * without confirmation), IPFS_ERR_NOT_FOUND if the network does not (yet) serve
 * those exact bytes, IPFS_OK once confirmed. A fetch-only adapter can confirm
 * PRESENCE, not force durable server-side pinning — that is an ops boundary of
 * whatever account you bound. Objects larger than the internal confirm buffer
 * (IPFS_PIN_CONFIRM_CAP) cannot be confirmed on-node and return NOT_FOUND. */
#define IPFS_PIN_CONFIRM_CAP 4096u
ipfs_result_t ipfs_add_pin(ipfs_node_t *n, const uint8_t *data, uint32_t len,
                           uint8_t out_cid[IPFS_CID_LEN]);

/* ---- bridge install ----
 * Wire this node into a bridge_t's resolve_cid slot so every Web3 CID the
 * bridge classifies flows through here. The bare hex reference the bridge hands
 * a resolver is parsed into a 32-byte CID. Returns 0 on success, -1 on NULL.
 * NOTE: bridge_ops_t shares one ctx across all three realms; installing here
 * sets ctx to this node, so bind the bridge's DNS/intent backends (if any) to
 * a design that tolerates that, or install this last. */
int32_t ipfs_install_as_bridge_resolver(ipfs_node_t *n, bridge_t *b);

#endif /* ZXV_IPFS_H */
