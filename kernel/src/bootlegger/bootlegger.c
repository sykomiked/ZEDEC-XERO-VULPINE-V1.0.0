/*
 * bootlegger.c — Boot Legger P2P transport for ZEDEC pqOS
 *
 * What is real here: the v2 handshake (ML-DSA-65 identities signing a
 * transcript of both ephemeral ML-KEM-768 keys and both nonces, replay cache,
 * version check), session keys from both KEM secrets and the transcript, and
 * ChaCha20-Poly1305 message sealing. What is not: any transport. Every
 * operation that would move bytes returns BOOTLEGGER_ENOTIMPL (or
 * BOOTLEGGER_ENOKEY) instead of reporting success. See bootlegger.h.
 *
 * Integer-only, no libc.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 * 36N9 Genetics, LLC — Irrevocable, Interdimensional
 */

#include "bootlegger.h"
#include "../mlkem/keccak.h"
#include "../pqsec/pq_security.h"
#include "../provenance/zx_provenance.h"
#include "../robin_debanks/sha256.h"
#include "../tls/aead.h"
#include "../tls/hkdf.h"

/* ============================================================
 * Helpers
 * ============================================================ */

static void bl_memset(void *dst, int v, uint32_t n)
{
    volatile uint8_t *d = (volatile uint8_t *) dst;
    for (uint32_t i = 0; i < n; i++) d[i] = (uint8_t) v;
}

static void bl_memcpy(void *dst, const void *src, uint32_t n)
{
    uint8_t *d = (uint8_t *) dst;
    const uint8_t *s = (const uint8_t *) src;
    for (uint32_t i = 0; i < n; i++) d[i] = s[i];
}

static bool bl_eq(const uint8_t *a, const uint8_t *b, uint32_t n)
{
    uint8_t acc = 0;
    for (uint32_t i = 0; i < n; i++) acc |= (uint8_t) (a[i] ^ b[i]);
    return acc == 0;
}

static void safe_strncpy(uint8_t *dst, const char *src, int max)
{
    int i = 0;
    if (src) {
        while (src[i] && i < max - 1) {
            dst[i] = (uint8_t) src[i];
            i++;
        }
    }
    dst[i] = 0;
}

static uint32_t g_next_call_id = 1;

/* ============================================================
 * Init / shutdown
 * ============================================================ */

int bootlegger_init(bootlegger_node_t *node, uint16_t port)
{
    if (!node) return BOOTLEGGER_EINVAL;
    bl_memset(node, 0, sizeof(*node));
    node->listen_port = port;
    node->local_peer_id = 0x5AED3C01;
    node->active_calls = 0;
    return 0;
}

int bootlegger_shutdown(bootlegger_node_t *node)
{
    if (!node) return BOOTLEGGER_EINVAL;
    for (uint32_t i = 0; i < BOOTLEGGER_MAX_PEERS; i++) {
        if (node->connections[i].connected)
            bootlegger_disconnect(node, node->connections[i].peer_id);
    }
    bl_memset(node, 0, sizeof(*node));
    return 0;
}

/* ============================================================
 * Handshake v2: ML-DSA-65 identities, ML-KEM-768 ephemeral keys, signed
 * transcript, replay cache. See bootlegger.h for the protocol.
 * ============================================================ */

#if PQ_MLDSA65_PK_BYTES != BOOTLEGGER_ID_PK_BYTES ||                                               \
    PQ_MLDSA65_SK_BYTES != BOOTLEGGER_ID_SK_BYTES ||                                               \
    PQ_MLDSA65_SIG_BYTES != BOOTLEGGER_ID_SIG_BYTES
#    error "bootlegger identity sizes do not match ML-DSA-65"
#endif

#define BL_ROLE_INIT        1
#define BL_ROLE_RESP        2
#define BL_STAGE_HELLO_SENT 1
#define BL_STAGE_REPLY_SENT 2

static const char BL_CTX_RESP[] = "bootlegger-v2-resp";
static const char BL_CTX_INIT[] = "bootlegger-v2-init";

uint32_t bootlegger_peer_id_of(const uint8_t identity[BOOTLEGGER_ID_PK_BYTES])
{
    uint8_t h[32];
    sha3_256(identity, BOOTLEGGER_ID_PK_BYTES, h);
    return ((uint32_t) h[0] << 24) | ((uint32_t) h[1] << 16) | ((uint32_t) h[2] << 8) | h[3];
}

int bootlegger_identity_init(bootlegger_identity_t *id, const uint8_t seed[32])
{
    if (!id || !seed) return BOOTLEGGER_EINVAL;
    pq_mldsa65_keygen(seed, id->pk, id->sk);
    return 0;
}

int bootlegger_pin_peer(bootlegger_conn_t *conn, const uint8_t identity[BOOTLEGGER_ID_PK_BYTES])
{
    if (!conn || !identity) return BOOTLEGGER_EINVAL;
    sha256(identity, BOOTLEGGER_ID_PK_BYTES, conn->pinned_id_hash);
    conn->pinned = 1;
    return 0;
}

void bootlegger_replay_init(bootlegger_replay_cache_t *rc)
{
    if (rc) bl_memset(rc, 0, sizeof(*rc));
}

static bool bl_is_zero(const uint8_t *p, uint32_t n)
{
    uint8_t acc = 0;
    for (uint32_t i = 0; i < n; i++) acc |= p[i];
    return acc == 0;
}

static bool bl_replay_seen(const bootlegger_replay_cache_t *rc, const uint8_t n[32])
{
    uint32_t cnt = rc->count < BOOTLEGGER_REPLAY_SLOTS ? rc->count : BOOTLEGGER_REPLAY_SLOTS;
    for (uint32_t i = 0; i < cnt; i++)
        if (bl_eq(rc->nonce[i], n, BOOTLEGGER_NONCE_BYTES)) return true;
    return false;
}

static void bl_replay_remember(bootlegger_replay_cache_t *rc, const uint8_t n[32])
{
    uint32_t slot = rc->next % BOOTLEGGER_REPLAY_SLOTS;
    bl_memcpy(rc->nonce[slot], n, BOOTLEGGER_NONCE_BYTES);
    rc->next = (slot + 1u) % BOOTLEGGER_REPLAY_SLOTS;
    if (rc->count < BOOTLEGGER_REPLAY_SLOTS) rc->count++;
}

/* rnd -> nonce(32) || d(32) || z(32) || m(32) || sig_rnd(32) */
#define BL_RNG_BYTES 160
static void bl_expand(const uint8_t rnd[BOOTLEGGER_RAND_BYTES], uint8_t out[BL_RNG_BYTES])
{
    static const char label[] = "bootlegger-v2-rng";
    uint8_t in[BOOTLEGGER_RAND_BYTES + sizeof(label) - 1];
    bl_memcpy(in, rnd, BOOTLEGGER_RAND_BYTES);
    bl_memcpy(in + BOOTLEGGER_RAND_BYTES, label, sizeof(label) - 1);
    shake256(in, sizeof(in), out, BL_RNG_BYTES);
    bl_memset(in, 0, sizeof(in));
}

void bootlegger_transcript_hash(uint16_t version, const uint8_t id_a[BOOTLEGGER_ID_PK_BYTES],
                                const uint8_t ek_a[MLKEM768_EK_BYTES],
                                const uint8_t nonce_b[BOOTLEGGER_NONCE_BYTES],
                                const uint8_t id_b[BOOTLEGGER_ID_PK_BYTES],
                                const uint8_t ek_b[MLKEM768_EK_BYTES],
                                const uint8_t nonce_a[BOOTLEGGER_NONCE_BYTES], uint8_t out[32])
{
    zxp_canon_t c;
    zxp_canon_init(&c, "bootlegger-v2-handshake");
    zxp_canon_u32(&c, version);
    zxp_canon_bytes(&c, id_a, BOOTLEGGER_ID_PK_BYTES);
    zxp_canon_bytes(&c, ek_a, MLKEM768_EK_BYTES);
    zxp_canon_bytes(&c, nonce_b, BOOTLEGGER_NONCE_BYTES);
    zxp_canon_bytes(&c, id_b, BOOTLEGGER_ID_PK_BYTES);
    zxp_canon_bytes(&c, ek_b, MLKEM768_EK_BYTES);
    zxp_canon_bytes(&c, nonce_a, BOOTLEGGER_NONCE_BYTES);
    zxp_canon_final(&c, out);
}

/* auth_tag = HMAC(tx_key || rx_key, "bootlegger v2 auth" || H(T) || peer_id_hash) */
static void bl_auth_tag(const bootlegger_conn_t *conn, uint8_t out[32])
{
    uint8_t key[2 * BOOTLEGGER_KEY_SIZE], msg[18 + 32 + 32];
    bl_memcpy(key, conn->tx_key, BOOTLEGGER_KEY_SIZE);
    bl_memcpy(key + BOOTLEGGER_KEY_SIZE, conn->rx_key, BOOTLEGGER_KEY_SIZE);
    bl_memcpy(msg, "bootlegger v2 auth", 18);
    bl_memcpy(msg + 18, conn->transcript_hash, 32);
    bl_memcpy(msg + 50, conn->peer_id_hash, 32);
    hmac_sha256(key, sizeof(key), msg, sizeof(msg), out);
    bl_memset(key, 0, sizeof(key));
}

/* Derive both directions and the confirmation key from both KEM secrets and
 * the transcript; install them on `conn` and mark it authenticated. */
static void bl_install_session(bootlegger_conn_t *conn, bool initiator, const uint8_t t_hash[32],
                               const uint8_t ss_b[32], const uint8_t ct_b_h[32],
                               const uint8_t ss_a[32], const uint8_t ct_a_h[32],
                               const uint8_t peer_identity[BOOTLEGGER_ID_PK_BYTES],
                               uint8_t confirm_key[32])
{
    uint8_t salt[32], ikm[64], prk[HASH_LEN], i2r[32], r2i[32];
    zxp_canon_t c;
    zxp_canon_init(&c, "bootlegger-v2-keys");
    zxp_canon_bytes(&c, t_hash, 32);
    zxp_canon_bytes(&c, ct_b_h, 32);
    zxp_canon_bytes(&c, ct_a_h, 32);
    zxp_canon_final(&c, salt);
    bl_memcpy(ikm, ss_b, 32);
    bl_memcpy(ikm + 32, ss_a, 32);
    hkdf_extract(salt, sizeof(salt), ikm, sizeof(ikm), prk);
    (void) hkdf_expand(prk, (const uint8_t *) "bootlegger v2 i2r", 17, i2r, 32);
    (void) hkdf_expand(prk, (const uint8_t *) "bootlegger v2 r2i", 17, r2i, 32);
    (void) hkdf_expand(prk, (const uint8_t *) "bootlegger v2 confirm", 21, confirm_key, 32);

    bl_memcpy(conn->tx_key, initiator ? i2r : r2i, 32);
    bl_memcpy(conn->rx_key, initiator ? r2i : i2r, 32);
    bl_memcpy(conn->transcript_hash, t_hash, 32);
    sha256(peer_identity, BOOTLEGGER_ID_PK_BYTES, conn->peer_id_hash);
    conn->peer_id = bootlegger_peer_id_of(peer_identity);
    conn->version = BOOTLEGGER_VERSION;
    conn->is_initiator = initiator ? 1 : 0;
    conn->tx_seq = 0;
    conn->rx_seq = 0;
    conn->connected = 1;
    conn->has_session = 1;
    conn->authenticated = 1;
    bl_auth_tag(conn, conn->auth_tag);

    bl_memset(ikm, 0, sizeof(ikm));
    bl_memset(prk, 0, sizeof(prk));
    bl_memset(i2r, 0, sizeof(i2r));
    bl_memset(r2i, 0, sizeof(r2i));
}

static void bl_confirm_tag(const uint8_t key[32], const uint8_t t_hash[32], uint8_t out[32])
{
    uint8_t msg[18 + 32];
    bl_memcpy(msg, "initiator finished", 18);
    bl_memcpy(msg + 18, t_hash, 32);
    hmac_sha256(key, 32, msg, sizeof(msg), out);
}

static void bl_conn_clear_auth(bootlegger_conn_t *conn)
{
    if (!conn) return;
    conn->authenticated = 0;
    conn->has_session = 0;
    bl_memset(conn->tx_key, 0, sizeof(conn->tx_key));
    bl_memset(conn->rx_key, 0, sizeof(conn->rx_key));
    bl_memset(conn->auth_tag, 0, sizeof(conn->auth_tag));
}

static int bl_fail(bootlegger_hs_t *hs, bootlegger_conn_t *conn, int rc)
{
    if (hs) bl_memset(hs, 0, sizeof(*hs));
    bl_conn_clear_auth(conn);
    return rc;
}

/* Common checks on a peer's hello part. */
static int bl_check_peer_hello(const bootlegger_conn_t *conn, const bootlegger_identity_t *self,
                               const bootlegger_replay_cache_t *rc, const bootlegger_hello_t *h)
{
    if (h->version != BOOTLEGGER_VERSION) return BOOTLEGGER_EVERSION;
    if (bl_is_zero(h->nonce, BOOTLEGGER_NONCE_BYTES)) return BOOTLEGGER_ENONCE;
    if (bl_replay_seen(rc, h->nonce)) return BOOTLEGGER_EREPLAY;
    if (bl_eq(h->identity, self->pk, BOOTLEGGER_ID_PK_BYTES)) return BOOTLEGGER_EAUTH;
    if (conn->pinned) {
        uint8_t ih[32];
        sha256(h->identity, BOOTLEGGER_ID_PK_BYTES, ih);
        if (!bl_eq(ih, conn->pinned_id_hash, 32)) return BOOTLEGGER_EAUTH;
    }
    return 0;
}

int bootlegger_hs_initiate(bootlegger_hs_t *hs, const bootlegger_identity_t *self,
                           bootlegger_replay_cache_t *rc, const uint8_t rnd[BOOTLEGGER_RAND_BYTES],
                           bootlegger_hello_t *out)
{
    if (!hs || !self || !rc || !rnd || !out) return BOOTLEGGER_EINVAL;
    uint8_t r[BL_RNG_BYTES];
    bl_memset(hs, 0, sizeof(*hs));
    bl_expand(rnd, r);
    bl_memcpy(hs->nonce_self, r, BOOTLEGGER_NONCE_BYTES);
    if (bl_is_zero(hs->nonce_self, BOOTLEGGER_NONCE_BYTES) || bl_replay_seen(rc, hs->nonce_self)) {
        bl_memset(r, 0, sizeof(r));
        return bl_fail(hs, 0, BOOTLEGGER_EREPLAY); /* entropy was reused */
    }
    mlkem768_keygen(r + 32, r + 64, hs->ek_self, hs->dk_self);
    bl_memset(r, 0, sizeof(r));
    bl_replay_remember(rc, hs->nonce_self);

    out->version = BOOTLEGGER_VERSION;
    bl_memcpy(out->nonce, hs->nonce_self, BOOTLEGGER_NONCE_BYTES);
    bl_memcpy(out->identity, self->pk, BOOTLEGGER_ID_PK_BYTES);
    bl_memcpy(out->ek, hs->ek_self, MLKEM768_EK_BYTES);
    hs->role = BL_ROLE_INIT;
    hs->stage = BL_STAGE_HELLO_SENT;
    return 0;
}

int bootlegger_hs_respond(bootlegger_hs_t *hs, bootlegger_conn_t *conn,
                          const bootlegger_identity_t *self, bootlegger_replay_cache_t *rc,
                          const bootlegger_hello_t *in, const uint8_t rnd[BOOTLEGGER_RAND_BYTES],
                          bootlegger_reply_t *out)
{
    if (!hs || !conn || !self || !rc || !in || !rnd || !out) return BOOTLEGGER_EINVAL;
    bl_memset(hs, 0, sizeof(*hs));
    bl_conn_clear_auth(conn);
    int e = bl_check_peer_hello(conn, self, rc, in);
    if (e) return bl_fail(hs, conn, e);
    bl_replay_remember(rc, in->nonce);

    uint8_t r[BL_RNG_BYTES];
    bl_expand(rnd, r);
    bl_memcpy(hs->nonce_self, r, BOOTLEGGER_NONCE_BYTES);
    if (bl_is_zero(hs->nonce_self, BOOTLEGGER_NONCE_BYTES) || bl_replay_seen(rc, hs->nonce_self)) {
        bl_memset(r, 0, sizeof(r));
        return bl_fail(hs, conn, BOOTLEGGER_EREPLAY);
    }
    mlkem768_keygen(r + 32, r + 64, hs->ek_self, hs->dk_self);
    bl_replay_remember(rc, hs->nonce_self);

    /* encapsulate to A's ephemeral key (ss stays here until FINISH) */
    mlkem768_encaps(in->ek, r + 96, out->ct, hs->ss_reply);
    sha256(out->ct, MLKEM768_CT_BYTES, hs->ct_reply_h);

    out->hello.version = BOOTLEGGER_VERSION;
    bl_memcpy(out->hello.nonce, hs->nonce_self, BOOTLEGGER_NONCE_BYTES);
    bl_memcpy(out->hello.identity, self->pk, BOOTLEGGER_ID_PK_BYTES);
    bl_memcpy(out->hello.ek, hs->ek_self, MLKEM768_EK_BYTES);

    bootlegger_transcript_hash(BOOTLEGGER_VERSION, in->identity, in->ek, hs->nonce_self, self->pk,
                               hs->ek_self, in->nonce, hs->t_hash);
    pq_mldsa65_sign(self->sk, hs->t_hash, 32, (const uint8_t *) BL_CTX_RESP,
                    (uint32_t) (sizeof(BL_CTX_RESP) - 1), r + 128, out->sig);
    bl_memset(r, 0, sizeof(r));

    bl_memcpy(hs->peer_identity, in->identity, BOOTLEGGER_ID_PK_BYTES);
    hs->role = BL_ROLE_RESP;
    hs->stage = BL_STAGE_REPLY_SENT;
    return 0;
}

int bootlegger_hs_finish(bootlegger_hs_t *hs, bootlegger_conn_t *conn,
                         const bootlegger_identity_t *self, bootlegger_replay_cache_t *rc,
                         const bootlegger_reply_t *in, const uint8_t rnd[BOOTLEGGER_RAND_BYTES],
                         bootlegger_finish_t *out)
{
    if (!hs || !conn || !self || !rc || !in || !rnd || !out) return BOOTLEGGER_EINVAL;
    bl_conn_clear_auth(conn);
    if (hs->role != BL_ROLE_INIT || hs->stage != BL_STAGE_HELLO_SENT)
        return bl_fail(hs, conn, BOOTLEGGER_ESTATE);
    int e = bl_check_peer_hello(conn, self, rc, &in->hello);
    if (e) return bl_fail(hs, conn, e);

    /* B's signature over T, with OUR ephemeral key and nonce: a swapped key,
     * a reply from another session, or any edited field fails here. */
    bootlegger_transcript_hash(BOOTLEGGER_VERSION, self->pk, hs->ek_self, in->hello.nonce,
                               in->hello.identity, in->hello.ek, hs->nonce_self, hs->t_hash);
    if (!pq_mldsa65_verify(in->hello.identity, hs->t_hash, 32, (const uint8_t *) BL_CTX_RESP,
                           (uint32_t) (sizeof(BL_CTX_RESP) - 1), in->sig))
        return bl_fail(hs, conn, BOOTLEGGER_EAUTH);
    bl_replay_remember(rc, in->hello.nonce);

    uint8_t r[BL_RNG_BYTES], ss_b[32], ss_a[32], ct_b_h[32], ct_a_h[32], ck[32];
    bl_expand(rnd, r);
    mlkem768_decaps(hs->dk_self, in->ct, ss_b);
    sha256(in->ct, MLKEM768_CT_BYTES, ct_b_h);
    mlkem768_encaps(in->hello.ek, r + 96, out->ct, ss_a);
    sha256(out->ct, MLKEM768_CT_BYTES, ct_a_h);
    pq_mldsa65_sign(self->sk, hs->t_hash, 32, (const uint8_t *) BL_CTX_INIT,
                    (uint32_t) (sizeof(BL_CTX_INIT) - 1), r + 128, out->sig);
    bl_install_session(conn, true, hs->t_hash, ss_b, ct_b_h, ss_a, ct_a_h, in->hello.identity, ck);
    bl_confirm_tag(ck, hs->t_hash, out->confirm);

    bl_memset(r, 0, sizeof(r));
    bl_memset(ss_a, 0, sizeof(ss_a));
    bl_memset(ss_b, 0, sizeof(ss_b));
    bl_memset(ck, 0, sizeof(ck));
    bl_memset(hs, 0, sizeof(*hs));
    return 0;
}

int bootlegger_hs_accept(bootlegger_hs_t *hs, bootlegger_conn_t *conn,
                         const bootlegger_finish_t *in)
{
    if (!hs || !conn || !in) return BOOTLEGGER_EINVAL;
    bl_conn_clear_auth(conn);
    if (hs->role != BL_ROLE_RESP || hs->stage != BL_STAGE_REPLY_SENT)
        return bl_fail(hs, conn, BOOTLEGGER_ESTATE);
    if (!pq_mldsa65_verify(hs->peer_identity, hs->t_hash, 32, (const uint8_t *) BL_CTX_INIT,
                           (uint32_t) (sizeof(BL_CTX_INIT) - 1), in->sig))
        return bl_fail(hs, conn, BOOTLEGGER_EAUTH);

    uint8_t ss_a[32], ct_a_h[32], ck[32], want[32];
    mlkem768_decaps(hs->dk_self, in->ct, ss_a);
    sha256(in->ct, MLKEM768_CT_BYTES, ct_a_h);
    bl_install_session(conn, false, hs->t_hash, hs->ss_reply, hs->ct_reply_h, ss_a, ct_a_h,
                       hs->peer_identity, ck);
    bl_confirm_tag(ck, hs->t_hash, want);
    bool ok = bl_eq(want, in->confirm, 32);
    bl_memset(ss_a, 0, sizeof(ss_a));
    bl_memset(ck, 0, sizeof(ck));
    if (!ok) return bl_fail(hs, conn, BOOTLEGGER_EAUTH); /* a ciphertext was swapped */
    bl_memset(hs, 0, sizeof(*hs));
    return 0;
}

static void bl_nonce(uint64_t seq, uint8_t nonce[CHACHA20_NONCE_LEN])
{
    bl_memset(nonce, 0, CHACHA20_NONCE_LEN);
    for (uint32_t i = 0; i < 8; i++) nonce[4 + i] = (uint8_t) (seq >> (8 * i));
}

int bootlegger_seal_msg(bootlegger_conn_t *conn, bootlegger_msg_type_t type, const void *payload,
                        uint16_t len, uint8_t *out, uint32_t cap)
{
    if (!conn || !out || (len > 0 && !payload)) return BOOTLEGGER_EINVAL;
    if (!conn->has_session) return BOOTLEGGER_ENOKEY;
    uint32_t total = BOOTLEGGER_MSG_HDR + (uint32_t) len + BOOTLEGGER_TAG_SIZE;
    if (cap < total) return BOOTLEGGER_EINVAL;
    out[0] = (uint8_t) type;
    out[1] = (uint8_t) (len >> 8);
    out[2] = (uint8_t) len;
    uint8_t nonce[CHACHA20_NONCE_LEN];
    bl_nonce(conn->tx_seq, nonce);
    aead_seal(conn->tx_key, nonce, out, BOOTLEGGER_MSG_HDR, (const uint8_t *) payload,
              out + BOOTLEGGER_MSG_HDR, len, out + BOOTLEGGER_MSG_HDR + len);
    conn->tx_seq++;
    return (int) total;
}

int bootlegger_open_msg(bootlegger_conn_t *conn, const uint8_t *in, uint32_t in_len,
                        bootlegger_msg_type_t *type, void *payload, uint32_t cap)
{
    if (!conn || !in || !type || !payload) return BOOTLEGGER_EINVAL;
    if (!conn->has_session) return BOOTLEGGER_ENOKEY;
    if (in_len < BOOTLEGGER_MSG_HDR + BOOTLEGGER_TAG_SIZE) return BOOTLEGGER_EINVAL;
    uint32_t len = ((uint32_t) in[1] << 8) | in[2];
    if (in_len != BOOTLEGGER_MSG_HDR + len + BOOTLEGGER_TAG_SIZE || len > cap)
        return BOOTLEGGER_EINVAL;
    uint8_t nonce[CHACHA20_NONCE_LEN];
    bl_nonce(conn->rx_seq, nonce);
    if (!aead_open(conn->rx_key, nonce, in, BOOTLEGGER_MSG_HDR, in + BOOTLEGGER_MSG_HDR,
                   (uint8_t *) payload, len, in + BOOTLEGGER_MSG_HDR + len))
        return BOOTLEGGER_EAUTH;
    conn->rx_seq++;
    *type = (bootlegger_msg_type_t) in[0];
    return (int) len;
}

/* ============================================================
 * Tracker / DHT: not implemented
 * ============================================================ */

int bootlegger_tracker_register(bootlegger_node_t *node)
{
    return node ? BOOTLEGGER_ENOTIMPL : BOOTLEGGER_EINVAL;
}

/* Lists only peers already recorded locally; there is no DHT lookup. */
int bootlegger_tracker_list(bootlegger_node_t *node, bootlegger_peer_t *peers, int max)
{
    if (!node || !peers) return 0;
    int count = 0;
    for (uint32_t i = 0; i < BOOTLEGGER_MAX_PEERS && count < max; i++) {
        if (node->tracker[i].peer_id != 0) peers[count++] = node->tracker[i];
    }
    return count;
}

int bootlegger_tracker_unregister(bootlegger_node_t *node)
{
    return node ? BOOTLEGGER_ENOTIMPL : BOOTLEGGER_EINVAL;
}

int bootlegger_tracker_heartbeat(bootlegger_node_t *node)
{
    return node ? BOOTLEGGER_ENOTIMPL : BOOTLEGGER_EINVAL;
}

/* ============================================================
 * Connection slots
 * ============================================================ */

/* Records a connection slot. No socket is opened and the peer is not
 * authenticated until bootlegger_handshake_recv() verifies its handshake. */
int bootlegger_connect(bootlegger_node_t *node, uint32_t ip, uint16_t port)
{
    if (!node) return BOOTLEGGER_EINVAL;
    for (uint32_t i = 0; i < BOOTLEGGER_MAX_PEERS; i++) {
        bootlegger_conn_t *c = &node->connections[i];
        if (!c->connected) {
            bl_memset(c, 0, sizeof(*c));
            c->peer_id = ip ^ port;
            c->ip = ip;
            c->port = port;
            c->connected = 1;
            c->role = BOOTLEGGER_ROLE_GUEST;
            c->last_activity = node->phase_tick;
            return 0;
        }
    }
    return -2;
}

int bootlegger_disconnect(bootlegger_node_t *node, uint32_t peer_id)
{
    if (!node) return BOOTLEGGER_EINVAL;
    for (uint32_t i = 0; i < BOOTLEGGER_MAX_PEERS; i++) {
        bootlegger_conn_t *c = &node->connections[i];
        if (c->connected && c->peer_id == peer_id) {
            bl_memset(c, 0, sizeof(*c));
            return 0;
        }
    }
    return -2;
}

int bootlegger_send_msg(bootlegger_conn_t *conn, bootlegger_msg_type_t type, const void *payload,
                        uint16_t len)
{
    (void) type;
    (void) payload;
    (void) len;
    if (!conn || !conn->connected) return BOOTLEGGER_EINVAL;
    if (!conn->has_session) return BOOTLEGGER_ENOKEY;
    return BOOTLEGGER_ENOTIMPL; /* no transport */
}

int bootlegger_recv_msg(bootlegger_conn_t *conn, bootlegger_msg_type_t *type, void *payload,
                        uint16_t *len)
{
    (void) type;
    (void) payload;
    (void) len;
    if (!conn || !conn->connected) return BOOTLEGGER_EINVAL;
    return BOOTLEGGER_ENOTIMPL;
}

/* Send to every connected peer. 0 if at least one send succeeded, else the
 * last error (BOOTLEGGER_ENOTIMPL when there are no peers at all). */
static int bl_broadcast(bootlegger_node_t *node, bootlegger_msg_type_t type, const void *p,
                        uint16_t len)
{
    int rc = BOOTLEGGER_ENOTIMPL, any = 0;
    for (uint32_t i = 0; i < BOOTLEGGER_MAX_PEERS; i++) {
        if (!node->connections[i].connected) continue;
        int r = bootlegger_send_msg(&node->connections[i], type, p, len);
        if (r == 0)
            any = 1;
        else
            rc = r;
    }
    return any ? 0 : rc;
}

/* ============================================================
 * Chat API
 * ============================================================ */

int bootlegger_chat_join(bootlegger_node_t *node, uint32_t channel_id)
{
    if (!node) return BOOTLEGGER_EINVAL;
    for (uint32_t i = 0; i < BOOTLEGGER_MAX_CHANNEL; i++)
        if (node->channels[i].channel_id == channel_id) return 0;
    return -2; /* no such channel */
}

int bootlegger_chat_leave(bootlegger_node_t *node, uint32_t channel_id)
{
    (void) channel_id;
    return node ? 0 : BOOTLEGGER_EINVAL;
}

int bootlegger_chat_send(bootlegger_node_t *node, uint32_t channel_id, const char *message)
{
    if (!node || !message) return BOOTLEGGER_EINVAL;
    bootlegger_chat_msg_t msg;
    bl_memset(&msg, 0, sizeof(msg));
    msg.channel_id = channel_id;
    msg.sender_id = node->local_peer_id;
    msg.timestamp = node->phase_tick;
    safe_strncpy(msg.message, message, BOOTLEGGER_MAX_MSG);
    return bl_broadcast(node, BOOTLEGGER_MSG_CHAT, &msg, (uint16_t) sizeof(msg));
}

int bootlegger_chat_recv(bootlegger_node_t *node, bootlegger_chat_msg_t *msg)
{
    if (!node || !msg) return BOOTLEGGER_EINVAL;
    return BOOTLEGGER_ENOTIMPL;
}

int bootlegger_chat_create_channel(bootlegger_node_t *node, const char *name, const char *topic,
                                   uint8_t encrypted)
{
    if (!node || !name) return BOOTLEGGER_EINVAL;
    for (uint32_t i = 0; i < BOOTLEGGER_MAX_CHANNEL; i++) {
        if (node->channels[i].channel_id == 0) {
            node->channels[i].channel_id = i + 1;
            safe_strncpy(node->channels[i].name, name, 64);
            safe_strncpy(node->channels[i].topic, topic ? topic : "", 256);
            node->channels[i].member_count = 1;
            node->channels[i].encrypted = encrypted; /* label */
            return (int) node->channels[i].channel_id;
        }
    }
    return -2;
}

/* ============================================================
 * File Transfer API: not implemented
 * ============================================================ */

int bootlegger_file_list(bootlegger_conn_t *conn, void *result, int max)
{
    (void) result;
    (void) max;
    return conn ? BOOTLEGGER_ENOTIMPL : BOOTLEGGER_EINVAL;
}

int bootlegger_file_request(bootlegger_conn_t *conn, uint32_t file_id, uint32_t start_chunk)
{
    if (!conn) return BOOTLEGGER_EINVAL;
    uint8_t req[8];
    for (uint32_t i = 0; i < 4; i++) {
        req[i] = (uint8_t) (file_id >> (24 - 8 * i));
        req[4 + i] = (uint8_t) (start_chunk >> (24 - 8 * i));
    }
    return bootlegger_send_msg(conn, BOOTLEGGER_MSG_FILE_GET, req, sizeof(req));
}

int bootlegger_file_recv_chunk(bootlegger_conn_t *conn, bootlegger_file_chunk_t *chunk)
{
    if (!conn || !chunk) return BOOTLEGGER_EINVAL;
    return BOOTLEGGER_ENOTIMPL;
}

/* Used to hand back phase_tick as a "file id" without reading the file. */
int bootlegger_file_share(bootlegger_node_t *node, const char *path, uint32_t *file_id)
{
    if (!node || !path || !file_id) return BOOTLEGGER_EINVAL;
    *file_id = 0;
    return BOOTLEGGER_ENOTIMPL;
}

/* ============================================================
 * News / Social Feed API
 * ============================================================ */

static void bl_post(bootlegger_node_t *node, bootlegger_news_post_t *post, uint32_t thread,
                    uint32_t parent, uint8_t type)
{
    bl_memset(post, 0, sizeof(*post));
    post->thread_id = thread;
    post->parent_id = parent;
    post->author_id = node->local_peer_id;
    post->timestamp = node->phase_tick;
    post->post_type = type;
}

int bootlegger_news_create_thread(bootlegger_node_t *node, const char *subject, const char *body)
{
    if (!node || !subject) return BOOTLEGGER_EINVAL;
    bootlegger_news_post_t post;
    bl_post(node, &post, (uint32_t) node->phase_tick, 0, 0);
    safe_strncpy(post.subject, subject, 128);
    safe_strncpy(post.body, body ? body : "", 4096);
    int rc = bl_broadcast(node, BOOTLEGGER_MSG_NEWS_POST, &post, (uint16_t) sizeof(post));
    return rc == 0 ? (int) post.thread_id : rc;
}

int bootlegger_news_reply(bootlegger_node_t *node, uint32_t thread_id, const char *body)
{
    if (!node || !body) return BOOTLEGGER_EINVAL;
    bootlegger_news_post_t post;
    bl_post(node, &post, thread_id, thread_id, 0);
    safe_strncpy(post.body, body, 4096);
    return bl_broadcast(node, BOOTLEGGER_MSG_NEWS_POST, &post, (uint16_t) sizeof(post));
}

int bootlegger_news_list(bootlegger_node_t *node, bootlegger_news_post_t *posts, int max)
{
    (void) max;
    if (!node || !posts) return 0;
    return 0; /* no local cache: zero posts */
}

int bootlegger_social_post(bootlegger_node_t *node, const char *body, uint8_t post_type)
{
    if (!node || !body) return BOOTLEGGER_EINVAL;
    bootlegger_news_post_t post;
    bl_post(node, &post, (uint32_t) node->phase_tick, 0, post_type);
    safe_strncpy(post.body, body, 4096);
    return bl_broadcast(node, BOOTLEGGER_MSG_SOCIAL_POST, &post, (uint16_t) sizeof(post));
}

int bootlegger_social_sync(bootlegger_node_t *node, uint64_t since_tick)
{
    if (!node) return BOOTLEGGER_EINVAL;
    return bl_broadcast(node, BOOTLEGGER_MSG_SOCIAL_SYNC, &since_tick,
                        (uint16_t) sizeof(since_tick));
}

/* ============================================================
 * Media Streaming API
 * ============================================================ */

int bootlegger_call_setup(bootlegger_node_t *node, uint32_t peer_id, uint8_t call_type,
                          uint32_t *call_id)
{
    if (!node || !call_id) return BOOTLEGGER_EINVAL;
    bootlegger_call_setup_t setup;
    bl_memset(&setup, 0, sizeof(setup));
    setup.call_id = g_next_call_id++;
    setup.caller_id = node->local_peer_id;
    setup.callee_id = peer_id;
    setup.call_type = call_type;
    setup.codec_pref = 1;
    *call_id = setup.call_id;
    for (uint32_t i = 0; i < BOOTLEGGER_MAX_PEERS; i++) {
        bootlegger_conn_t *c = &node->connections[i];
        if (c->connected && c->peer_id == peer_id) {
            int rc =
                bootlegger_send_msg(c, BOOTLEGGER_MSG_CALL_SETUP, &setup, (uint16_t) sizeof(setup));
            if (rc == 0) node->active_calls++;
            return rc;
        }
    }
    return -2; /* no such peer */
}

int bootlegger_call_accept(bootlegger_node_t *node, uint32_t call_id)
{
    if (!node) return BOOTLEGGER_EINVAL;
    return bl_broadcast(node, BOOTLEGGER_MSG_CALL_ACCEPT, &call_id, (uint16_t) sizeof(call_id));
}

int bootlegger_call_hangup(bootlegger_node_t *node, uint32_t call_id)
{
    if (!node) return BOOTLEGGER_EINVAL;
    int rc = bl_broadcast(node, BOOTLEGGER_MSG_CALL_HANGUP, &call_id, (uint16_t) sizeof(call_id));
    if (node->active_calls > 0) node->active_calls--;
    return rc;
}

int bootlegger_voice_send(bootlegger_node_t *node, uint32_t call_id, const uint8_t *frame,
                          uint16_t size, uint8_t codec)
{
    if (!node || !frame) return BOOTLEGGER_EINVAL;
    bootlegger_voice_frame_t vf;
    bl_memset(&vf, 0, sizeof(vf));
    vf.call_id = call_id;
    vf.seq_num = (uint16_t) (node->phase_tick & 0xFFFF);
    vf.frame_size = size > 512 ? 512 : size;
    vf.codec = codec;
    bl_memcpy(vf.frame, frame, vf.frame_size);
    return bl_broadcast(node, BOOTLEGGER_MSG_VOICE_FRAME, &vf, (uint16_t) sizeof(vf));
}

int bootlegger_voice_recv(bootlegger_node_t *node, uint32_t call_id,
                          bootlegger_voice_frame_t *frame)
{
    (void) call_id;
    if (!node || !frame) return BOOTLEGGER_EINVAL;
    return BOOTLEGGER_ENOTIMPL;
}

int bootlegger_video_send(bootlegger_node_t *node, uint32_t call_id, const uint8_t *frame,
                          uint16_t size, uint8_t frame_type)
{
    if (!node || !frame) return BOOTLEGGER_EINVAL;
    bootlegger_video_frame_t vf;
    bl_memset(&vf, 0, sizeof(vf));
    vf.call_id = call_id;
    vf.seq_num = (uint16_t) (node->phase_tick & 0xFFFF);
    vf.frame_type = frame_type;
    vf.frame_size = size > 4096 ? 4096 : size;
    bl_memcpy(vf.frame, frame, vf.frame_size);
    return bl_broadcast(node, BOOTLEGGER_MSG_VIDEO_FRAME, &vf, (uint16_t) sizeof(vf));
}

int bootlegger_video_recv(bootlegger_node_t *node, uint32_t call_id,
                          bootlegger_video_frame_t *frame)
{
    (void) call_id;
    if (!node || !frame) return BOOTLEGGER_EINVAL;
    return BOOTLEGGER_ENOTIMPL;
}

int bootlegger_adapt_bitrate(bootlegger_node_t *node, uint32_t call_id, uint32_t available_bw)
{
    (void) call_id;
    (void) available_bw;
    return node ? BOOTLEGGER_ENOTIMPL : BOOTLEGGER_EINVAL;
}

/* ============================================================
 * Legacy Protocol Bridges: framing only, no protocol implementation
 * ============================================================ */

int bootlegger_irc_init(bootlegger_node_t *node)
{
    if (!node) return BOOTLEGGER_EINVAL;
    bootlegger_chat_create_channel(node, "#zedec", "ZEDEC pqOS main channel", 0);
    bootlegger_chat_create_channel(node, "#help", "Help and support", 0);
    bootlegger_chat_create_channel(node, "#dev", "Developer discussion", 0);
    return 0;
}

int bootlegger_irc_send(bootlegger_node_t *node, uint32_t channel_id, const char *nick,
                        const char *message)
{
    if (!node || !nick || !message) return BOOTLEGGER_EINVAL;
    char buf[BOOTLEGGER_MAX_MSG];
    uint32_t n = 0;
    buf[n++] = '<';
    for (uint32_t i = 0; nick[i] && n < BOOTLEGGER_MAX_MSG - 3; i++) buf[n++] = nick[i];
    buf[n++] = '>';
    buf[n++] = ' ';
    for (uint32_t i = 0; message[i] && n < BOOTLEGGER_MAX_MSG - 1; i++) buf[n++] = message[i];
    buf[n] = 0;
    return bootlegger_chat_send(node, channel_id, buf);
}

int bootlegger_irc_recv(bootlegger_node_t *node, char *nick, char *message)
{
    (void) nick;
    (void) message;
    return node ? BOOTLEGGER_ENOTIMPL : BOOTLEGGER_EINVAL;
}

int bootlegger_nntp_init(bootlegger_node_t *node)
{
    return node ? 0 : BOOTLEGGER_EINVAL;
}

int bootlegger_nntp_post(bootlegger_node_t *node, const char *group, const char *subject,
                         const char *body)
{
    (void) group;
    if (!node) return BOOTLEGGER_EINVAL;
    return bootlegger_news_create_thread(node, subject, body);
}

int bootlegger_nntp_fetch(bootlegger_node_t *node, const char *group, bootlegger_news_post_t *posts,
                          int max)
{
    (void) group;
    if (!node) return BOOTLEGGER_EINVAL;
    return bootlegger_news_list(node, posts, max);
}

int bootlegger_gopher_serve(bootlegger_node_t *node, const char *selector, const uint8_t *content,
                            uint16_t len)
{
    (void) selector;
    (void) content;
    (void) len;
    return node ? BOOTLEGGER_ENOTIMPL : BOOTLEGGER_EINVAL;
}

static uint16_t bl_strlen16(const char *s)
{
    uint16_t n = 0;
    while (s[n] && n < 0xFFFF) n++;
    return n;
}

int bootlegger_gopher_fetch(bootlegger_conn_t *conn, const char *selector, uint8_t *buf,
                            uint16_t *len)
{
    (void) buf;
    (void) len;
    if (!conn || !selector) return BOOTLEGGER_EINVAL;
    return bootlegger_send_msg(conn, BOOTLEGGER_MSG_GOPHER, selector, bl_strlen16(selector));
}

int bootlegger_gemini_serve(bootlegger_node_t *node, const char *url, const uint8_t *content,
                            uint16_t len)
{
    (void) url;
    (void) content;
    (void) len;
    return node ? BOOTLEGGER_ENOTIMPL : BOOTLEGGER_EINVAL;
}

int bootlegger_gemini_fetch(bootlegger_conn_t *conn, const char *url, uint8_t *buf, uint16_t *len)
{
    (void) buf;
    (void) len;
    if (!conn || !url) return BOOTLEGGER_EINVAL;
    return bootlegger_send_msg(conn, BOOTLEGGER_MSG_GEMINI, url, bl_strlen16(url));
}

int bootlegger_smtp_send(bootlegger_node_t *node, uint32_t peer_id, const char *from,
                         const char *to, const char *subject, const char *body)
{
    (void) peer_id;
    (void) from;
    (void) to;
    (void) subject;
    (void) body;
    return node ? BOOTLEGGER_ENOTIMPL : BOOTLEGGER_EINVAL;
}

int bootlegger_smtp_recv(bootlegger_node_t *node, char *from, char *subject, char *body)
{
    (void) from;
    (void) subject;
    (void) body;
    return node ? BOOTLEGGER_ENOTIMPL : BOOTLEGGER_EINVAL;
}

/* ============================================================
 * Security & Access Control
 * ============================================================ */

/* Recomputes the binding tag from the session keys, transcript hash and peer
 * identity hash every time: the old version returned success whenever the
 * `authenticated` byte was non-zero. */
int bootlegger_authenticate(bootlegger_conn_t *conn, bootlegger_role_t *role)
{
    if (!conn) return BOOTLEGGER_EINVAL;
    if (!conn->authenticated) return -2;
    uint8_t tag[32];
    if (conn->has_session) bl_auth_tag(conn, tag);
    if (!conn->has_session || conn->version != BOOTLEGGER_VERSION ||
        !bl_eq(tag, conn->auth_tag, 32)) {
        bl_conn_clear_auth(conn);
        return BOOTLEGGER_EAUTH;
    }
    if (role) *role = conn->role;
    return 0;
}

int bootlegger_authorize(bootlegger_conn_t *conn, bootlegger_msg_type_t action)
{
    if (!conn) return BOOTLEGGER_EINVAL;
    /* Unauthenticated peers get guest rights whatever role is recorded. */
    bootlegger_role_t role = conn->authenticated ? conn->role : BOOTLEGGER_ROLE_GUEST;
    switch (role) {
    case BOOTLEGGER_ROLE_GUEST:
        if (action == BOOTLEGGER_MSG_CHAT || action == BOOTLEGGER_MSG_FILE_GET ||
            action == BOOTLEGGER_MSG_PING)
            return 0;
        return -2;
    case BOOTLEGGER_ROLE_USER:
        if (action != BOOTLEGGER_MSG_FILE_LIST && action != BOOTLEGGER_MSG_NEWS_POST &&
            action != BOOTLEGGER_MSG_SOCIAL_POST)
            return 0;
        return -2;
    case BOOTLEGGER_ROLE_CONTRIB:
    case BOOTLEGGER_ROLE_ADMIN:
        return 0;
    }
    return -3;
}

int bootlegger_set_role(bootlegger_node_t *node, uint32_t peer_id, bootlegger_role_t role)
{
    if (!node) return BOOTLEGGER_EINVAL;
    for (uint32_t i = 0; i < BOOTLEGGER_MAX_PEERS; i++) {
        if (node->connections[i].peer_id == peer_id) {
            node->connections[i].role = role;
            return 0;
        }
    }
    return -2;
}

/* ============================================================
 * Paraconsistent Error Handling (LPRES state labels)
 * ============================================================ */

int bootlegger_lpres_eval(bootlegger_conn_t *conn, uint8_t anomaly_type)
{
    if (!conn) return BOOTLEGGER_EINVAL;
    /* 0 = ok, 1 = speculative, 2 = isolated, 3 = contradiction, 4 = drop */
    conn->lpres_state = anomaly_type <= 4 ? anomaly_type : 2;
    return conn->lpres_state;
}

int bootlegger_lpres_recover(bootlegger_conn_t *conn)
{
    if (!conn) return BOOTLEGGER_EINVAL;
    switch (conn->lpres_state) {
    case 1: /* speculative — promote to ok */
    case 2: /* isolated — discard and request retransmit */
        conn->lpres_state = 0;
        return 0;
    case 3: /* contradiction — flag for resolution */
        return -2;
    case 4: /* drop — graceful disconnect */
        conn->connected = 0;
        return -3;
    default:
        return 0;
    }
}

/* ============================================================
 * Garlic Routing: not implemented (the old version applied the fixed-key
 * "frequency" XOR and called it a garlic layer)
 * ============================================================ */

int bootlegger_garlic_wrap(bootlegger_conn_t *conn, uint8_t *data, uint16_t *len)
{
    if (!conn || !data || !len) return BOOTLEGGER_EINVAL;
    return BOOTLEGGER_ENOTIMPL;
}

int bootlegger_garlic_unwrap(bootlegger_conn_t *conn, uint8_t *data, uint16_t *len)
{
    if (!conn || !data || !len) return BOOTLEGGER_EINVAL;
    return BOOTLEGGER_ENOTIMPL;
}

/* ---- DECLARATION -----------------------------------------------------------

 * The native P2P transport's authentication and sealing layer. PROVIDES
 * p2p_transport_ready in the narrow sense that its primitives work; it moves
 * no bytes (see bootlegger.h).
 *
 * REQUIRES_NONE: the crypto it now calls (pq_mldsa65_*, sha3_256, shake256,
 * sha256, mlkem768_*, aead_*, hkdf_*) are leaf primitives, not declared modules.
 */
#ifndef TEST_HOST
#    include "zxv_decl.h"
static int zxvd_bootlegger_bringup(void)
{
    static bootlegger_node_t node;
    return (bootlegger_init(&node, 4747u) == 0) ? 0 : -1;
}

ZXV_DECLARE(bootlegger, ZXV_PROVIDES(p2p_transport_ready), ZXV_REQUIRES_NONE,
            ZXV_BRINGUP(zxvd_bootlegger_bringup));
#endif
