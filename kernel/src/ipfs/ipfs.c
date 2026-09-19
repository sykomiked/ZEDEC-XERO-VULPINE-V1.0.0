/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* ipfs.c — content has no name but its own fingerprint. Your hash is your key. */

#include "ipfs.h"
#include "sha256.h"   /* the REAL kernel SHA-256 — we do not roll our own hash */

/* ---- tiny freestanding byte/string helpers (no libc, both host + target) ---- */

static const char HEXLC[16] = { '0','1','2','3','4','5','6','7',
                                '8','9','a','b','c','d','e','f' };

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;   /* accept upper on input   */
    return -1;
}

static bool ipfs_starts_with(const char *s, const char *pfx) {
    for (uint32_t i = 0; pfx[i]; i++)
        if (s[i] != pfx[i]) return false;   /* s NUL fails the compare, safely  */
    return true;
}

static bool bytes_equal(const uint8_t *a, const uint8_t *b, uint32_t n) {
    uint8_t diff = 0;                         /* constant-time-ish; no early out */
    for (uint32_t i = 0; i < n; i++) diff |= (uint8_t)(a[i] ^ b[i]);
    return diff == 0;
}

/* Parse exactly IPFS_CID_LEN bytes of hex (IPFS_HEX_LEN chars) from s.
 * Returns true iff the first 64 chars are valid hex AND char[64] terminates the
 * token (NUL). A 32- or 40-hex string hits its NUL early and fails here — that
 * is precisely how an MD5/SHA-1-length ref is rejected. */
static bool parse_cid_hex_strict(const char *s, uint8_t out[IPFS_CID_LEN]) {
    for (uint32_t i = 0; i < IPFS_CID_LEN; i++) {
        int hi = hexval(s[2 * i]);
        int lo = (hi < 0) ? -1 : hexval(s[2 * i + 1]);
        if (hi < 0 || lo < 0) return false;   /* short/invalid: NUL lands here   */
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    /* Everything past index 63 was valid hex; require the token to END now so a
     * 128-hex or trailing-garbage ref does not sneak past as "64 plus junk". */
    return s[IPFS_HEX_LEN] == '\0';
}

/* ---- CID primitives ---- */

int32_t ipfs_cid_from_bytes(const uint8_t *data, uint32_t len,
                            uint8_t out_cid[IPFS_CID_LEN]) {
    if (!out_cid) return -1;
    /* sha256 of the empty message is well defined; guard the NULL-with-len-0
     * case so we never hand the hasher a NULL pointer. */
    static const uint8_t empty = 0;
    sha256((len && data) ? data : &empty, (len && data) ? len : 0, out_cid);
    return 0;
}

int32_t ipfs_cid_format(const uint8_t cid[IPFS_CID_LEN], ipfs_uri_scheme_t scheme,
                        char *out, uint32_t cap) {
    if (!cid || !out) return -1;
    const char *pfx;
    switch (scheme) {
        case IPFS_SCHEME_CID:  pfx = "cid:";    break;
        case IPFS_SCHEME_IPFS: pfx = "ipfs://"; break;
        case IPFS_SCHEME_AIPI: pfx = "aipi://"; break;
        default: return -1;
    }
    uint32_t plen = 0; while (pfx[plen]) plen++;
    uint32_t need = plen + IPFS_HEX_LEN;      /* chars, excluding the NUL        */
    if (cap < need + 1u) return -1;           /* need room for the terminator    */

    uint32_t w = 0;
    for (uint32_t i = 0; i < plen; i++) out[w++] = pfx[i];
    for (uint32_t i = 0; i < IPFS_CID_LEN; i++) {
        out[w++] = HEXLC[(cid[i] >> 4) & 0xF];
        out[w++] = HEXLC[cid[i] & 0xF];
    }
    out[w] = '\0';
    return (int32_t)w;
}

ipfs_result_t ipfs_cid_parse(const char *uri, uint8_t out_cid[IPFS_CID_LEN]) {
    if (!uri || !out_cid) return IPFS_ERR_BAD_CID;
    uint32_t from;
    if      (ipfs_starts_with(uri, "cid:"))    from = 4u;
    else if (ipfs_starts_with(uri, "ipfs://")) from = 7u;
    else if (ipfs_starts_with(uri, "aipi://")) from = 7u;
    else return IPFS_ERR_BAD_CID;             /* unknown scheme                  */

    if (!parse_cid_hex_strict(uri + from, out_cid)) return IPFS_ERR_BAD_CID;
    return IPFS_OK;
}

/* ---- node lifecycle ---- */

void ipfs_node_init(ipfs_node_t *n) {
    if (n) n->t = 0;
}

void ipfs_set_transport(ipfs_node_t *n, const ipfs_transport_t *t) {
    if (n) n->t = t;
}

/* ---- the self-certifying get ---- */

ipfs_result_t ipfs_get_verify(ipfs_node_t *n, const uint8_t cid[IPFS_CID_LEN],
                              uint8_t *buf, uint32_t cap, uint32_t *out_len) {
    if (!n || !cid || !buf) return IPFS_ERR_NO_TRANSPORT;
    if (!n->t || !n->t->fetch) return IPFS_ERR_NO_TRANSPORT;  /* leave buf alone */

    uint32_t got = 0;
    if (n->t->fetch(cid, buf, cap, &got, n->t->ctx) != 0)
        return IPFS_ERR_NOT_FOUND;

    /* The transport is an UNTRUSTED ops boundary: a hostile or buggy adapter can
     * report a length larger than the buffer it was handed. Reject that before
     * hashing — otherwise sha256() would over-read `buf`, the exact network-trust
     * failure this self-certifying path exists to defend against. */
    if (got > cap) return IPFS_ERR_NOT_FOUND;

    /* self-certifying: recompute over EXACTLY what came back and demand it hash
     * to the address we asked for. A single flipped byte fails this. */
    uint8_t h[IPFS_CID_LEN];
    sha256(got ? buf : (const uint8_t *)"", got, h);
    if (!bytes_equal(h, cid, IPFS_CID_LEN)) return IPFS_ERR_CID_MISMATCH;

    if (out_len) *out_len = got;
    return IPFS_OK;
}

/* ---- add + pin ---- */

ipfs_result_t ipfs_add_pin(ipfs_node_t *n, const uint8_t *data, uint32_t len,
                           uint8_t out_cid[IPFS_CID_LEN]) {
    if (!out_cid) return IPFS_ERR_BAD_CID;
    /* Local truth first: the CID is always computable and is never a hollow
     * claim, so we fill it whether or not a transport exists. */
    ipfs_cid_from_bytes(data, len, out_cid);

    if (!n || !n->t || !n->t->fetch) return IPFS_ERR_NO_TRANSPORT;
    if (len > IPFS_PIN_CONFIRM_CAP)  return IPFS_ERR_NOT_FOUND; /* can't confirm  */

    /* Confirmation = a successful self-certifying fetch of the CID we just
     * computed. If the network already serves exactly these bytes under this
     * hash, the content is present (and, per your account's semantics, pinned).
     * We never report success on an unconfirmed pin. */
    static uint8_t confirm[IPFS_PIN_CONFIRM_CAP];   /* BSS, freestanding-safe    */
    ipfs_result_t r = ipfs_get_verify(n, out_cid, confirm, IPFS_PIN_CONFIRM_CAP, 0);
    if (r == IPFS_OK) return IPFS_OK;
    return IPFS_ERR_NOT_FOUND;
}

/* ---- bridge install ----
 * The bridge strips the scheme and hands a resolver the bare 64-hex reference;
 * turning that reference into a 32-byte CID IS the resolution. Reject anything
 * that is not exactly 64 hex (MD5/SHA-1-length refs and trailing junk). */
static int ipfs_bridge_resolve_cid(const char *ref, uint8_t cid[BRIDGE_CID_LEN],
                                   void *ctx) {
    (void)ctx;   /* the CID is self-describing; no node state is needed to parse */
    if (!ref || !cid) return -1;
    return parse_cid_hex_strict(ref, cid) ? 0 : -1;
}

int32_t ipfs_install_as_bridge_resolver(ipfs_node_t *n, bridge_t *b) {
    if (!n || !b) return -1;
    b->ops.resolve_cid = ipfs_bridge_resolve_cid;
    b->ops.ctx = n;           /* shared-ctx caveat is documented in the header  */
    return 0;
}

/* ---- DECLARATION -----------------------------------------------------------

 * Content-addressed fetch. pirate_apps.o and broker.o both name
 * ipfs_get_verify. ipfs.o's own `nm -u` is exactly {sha256} -- the CID.
 */
#include "zxv_decl.h"
ZXV_DECLARE(ipfs,
    ZXV_PROVIDES(ipfs_ready),
    ZXV_REQUIRES(sha256_ready),
    ZXV_NO_BRINGUP);
