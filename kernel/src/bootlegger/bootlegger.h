/*
 * bootlegger.h — Boot Legger: ZEDEC pqOS's native P2P transport
 *
 * Direct-stream peer routing and data synchronization without
 * centralized choke points -- peers run their own "routes" (trade
 * routes for files, chat, news, and calls) instead of trusting a
 * central server. The name is the joke: a "bootlegger" runs goods
 * peer-to-peer down unofficial routes to avoid checkpoints, and this
 * is what boots up ("boot") the mesh's illegitimate-but-thriving
 * distribution network. Same spirit as the underground BBS/warez-era
 * P2P systems this kernel's whole "hidden humor" naming convention
 * riffs on (see Porter House, Count House, Robin DeBanks, Immigration
 * Enforcement) -- descriptive first, pun second, never the other way.
 *
 * WHAT IS REAL
 *   - Handshake (protocol version 2), three messages, initiator A, responder B:
 *       HELLO  A->B  version, Nonce_A, Identity_A, EphemeralKey_A
 *       REPLY  B->A  version, Nonce_B, Identity_B, EphemeralKey_B,
 *                    ct_B = ML-KEM-768.Encaps(EphemeralKey_A),
 *                    sig_B = ML-DSA-65(sk_B, H(T), ctx "bootlegger-v2-resp")
 *       FINISH A->B  ct_A = ML-KEM-768.Encaps(EphemeralKey_B),
 *                    sig_A = ML-DSA-65(sk_A, H(T), ctx "bootlegger-v2-init"),
 *                    confirm = HMAC(k_confirm, "initiator finished" || H(T))
 *     where Identity_* is the peer's long-term ML-DSA-65 public key (FIPS 204,
 *     pqsec/pq_mldsa65.c), EphemeralKey_* a fresh ML-KEM-768 key (FIPS 203,
 *     mlkem/mlkem768.c), Nonce_* 32 fresh random bytes, and
 *       T = Identity_A || EphemeralKey_A || Nonce_B || Identity_B ||
 *           EphemeralKey_B || Nonce_A
 *     encoded with the shared canonical length-prefixed encoder
 *     (provenance/zx_provenance.h, domain "bootlegger-v2-handshake", the
 *     protocol version as the first field), hashed with SHA-256. Each side
 *     signs H(T) with its long-term identity key; the different context
 *     strings keep one side's signature from being reflected as the other's.
 *   - Rejected: a version other than BOOTLEGGER_VERSION (downgrade), a missing
 *     or all-zero nonce, a nonce already in the bounded replay cache, a
 *     signature that does not verify under the claimed identity over the
 *     transcript (so an ephemeral key swapped by a man in the middle, a
 *     replayed REPLY or FINISH from another session, and any edited field all
 *     fail), a peer presenting our own identity (reflection), an identity that
 *     does not match a pinned one, and a FINISH whose key confirmation fails.
 *   - Session keys: both ML-KEM shared secrets AND the transcript hash:
 *       k = HKDF-SHA256(salt = SHA-256(canon(H(T), H(ct_B), H(ct_A))),
 *                       ikm  = ss_B || ss_A)
 *     expanded into one key per direction plus the confirmation key.
 *   - Message sealing: bootlegger_seal_msg()/bootlegger_open_msg() use
 *     ChaCha20-Poly1305 with a per-direction sequence-number nonce.
 *   - bootlegger_authenticate() recomputes a binding tag from the session keys,
 *     transcript hash and peer identity hash; setting the `authenticated`
 *     field by hand is not enough. (It is a consistency check, not a defence
 *     against code that can already write this memory.)
 *
 * WHAT FAILS CLOSED (returns BOOTLEGGER_ENOTIMPL or BOOTLEGGER_ENOKEY)
 *   - There is no transport: send/recv, file transfer, tracker/DHT, media,
 *     legacy bridges and garlic wrapping do not move bytes and never report
 *     success. The old "alternating endianness frequency encryption" (a fixed
 *     built-in key and XOR, using floating point) is gone: it was obfuscation,
 *     not encryption.
 *   - The kernel has no RNG at this layer: every handshake step takes 32 bytes
 *     of fresh caller entropy (expanded with SHAKE256). Reusing entropy reuses
 *     nonces and ephemeral keys; the replay cache then refuses the peer's side.
 *   - No identity key is provisioned: callers create one with
 *     bootlegger_identity_init() from a secret seed they hold.
 *   - The replay cache is bounded (BOOTLEGGER_REPLAY_SLOTS, oldest evicted).
 *     A HELLO replayed after eviction gets a REPLY with a fresh Nonce_B, so the
 *     replayer still cannot produce sig_A over the new transcript.
 *   - The `encrypted` channel flag and call session_key fields are labels.
 *   - ML-DSA-65 sign/verify use tens of KiB of stack (reference code); run the
 *     handshake on a thread with a large enough stack.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 * 36N9 Genetics, LLC — Irrevocable, Interdimensional
 */

#ifndef BOOTLEGGER_H
#define BOOTLEGGER_H

#include <stdint.h>
#include <stdbool.h>
#include "../mlkem/mlkem768.h"

/* ============================================================
 * Protocol Constants
 * ============================================================ */

#define BOOTLEGGER_MAGIC       "TCPZEDEC"
#define BOOTLEGGER_MAGIC_LEN   8
#define BOOTLEGGER_VERSION      2 /* the only version accepted: v1 had no nonce */
#define BOOTLEGGER_MAX_PEERS   256
#define BOOTLEGGER_MAX_CHUNKS  65536
#define BOOTLEGGER_CHUNK_SIZE  4096
#define BOOTLEGGER_MAX_MSG     512
#define BOOTLEGGER_MAX_CHANNEL 64
#define BOOTLEGGER_HASH_SIZE   32
#define BOOTLEGGER_KEY_SIZE     32
#define BOOTLEGGER_TAG_SIZE     16
#define BOOTLEGGER_MSG_HDR      3 /* type(1) + length(2, big-endian) */
#define BOOTLEGGER_NONCE_BYTES  32
#define BOOTLEGGER_RAND_BYTES   32
#define BOOTLEGGER_ID_PK_BYTES  1952 /* ML-DSA-65 public key (PQ_MLDSA65_PK_BYTES) */
#define BOOTLEGGER_ID_SK_BYTES  4032 /* ML-DSA-65 secret key */
#define BOOTLEGGER_ID_SIG_BYTES 3309 /* ML-DSA-65 signature */
#define BOOTLEGGER_REPLAY_SLOTS 64

/* Error codes (all negative; 0 is success) */
#define BOOTLEGGER_EINVAL   (-1)  /* bad argument / not connected */
#define BOOTLEGGER_ENOTIMPL (-4)  /* capability not implemented: nothing happened */
#define BOOTLEGGER_EAUTH    (-5)  /* authentication or tag check failed */
#define BOOTLEGGER_ENOKEY   (-6)  /* no session key established */
#define BOOTLEGGER_EREPLAY  (-7)  /* nonce already seen (replay) */
#define BOOTLEGGER_EVERSION (-8)  /* protocol version not accepted (downgrade) */
#define BOOTLEGGER_ENONCE   (-9)  /* missing (all-zero) nonce */
#define BOOTLEGGER_ESTATE   (-10) /* handshake step out of order */

/* ============================================================
 * Message Types
 * ============================================================ */

typedef enum {
    BOOTLEGGER_MSG_HANDSHAKE    = 0,
    BOOTLEGGER_MSG_CHAT         = 1,
    BOOTLEGGER_MSG_PRIVATE      = 2,
    BOOTLEGGER_MSG_FILE_LIST    = 3,
    BOOTLEGGER_MSG_FILE_GET     = 4,
    BOOTLEGGER_MSG_FILE_CHUNK   = 5,
    BOOTLEGGER_MSG_NEWS_POST    = 6,
    BOOTLEGGER_MSG_NEWS_LIST    = 7,
    BOOTLEGGER_MSG_TRACKER_REG  = 8,
    BOOTLEGGER_MSG_TRACKER_LIST = 9,
    BOOTLEGGER_MSG_PING         = 10,
    BOOTLEGGER_MSG_PONG         = 11,
    BOOTLEGGER_MSG_BYE          = 12,
    /* Media streaming */
    BOOTLEGGER_MSG_VOICE_FRAME  = 13,
    BOOTLEGGER_MSG_VIDEO_FRAME  = 14,
    BOOTLEGGER_MSG_CALL_SETUP   = 15,
    BOOTLEGGER_MSG_CALL_ACCEPT  = 16,
    BOOTLEGGER_MSG_CALL_HANGUP  = 17,
    /* Legacy protocol bridges */
    BOOTLEGGER_MSG_IRC          = 18,
    BOOTLEGGER_MSG_NNTP         = 19,
    BOOTLEGGER_MSG_GOPHER       = 20,
    BOOTLEGGER_MSG_GEMINI       = 21,
    BOOTLEGGER_MSG_SMTP         = 22,
    BOOTLEGGER_MSG_IMAP         = 23,
    /* Social feed */
    BOOTLEGGER_MSG_SOCIAL_POST  = 24,
    BOOTLEGGER_MSG_SOCIAL_SYNC  = 25,
    BOOTLEGGER_MSG_SOCIAL_LIKE  = 26,
} bootlegger_msg_type_t;

/* ============================================================
 * Data Structures
 * ============================================================ */

/* Long-term identity: an ML-DSA-65 key pair. sk is secret. */
typedef struct {
    uint8_t pk[BOOTLEGGER_ID_PK_BYTES];
    uint8_t sk[BOOTLEGGER_ID_SK_BYTES];
} bootlegger_identity_t;

/* HELLO (A->B), also the first part of REPLY (B->A). */
typedef struct {
    uint16_t version;
    uint8_t nonce[BOOTLEGGER_NONCE_BYTES];
    uint8_t identity[BOOTLEGGER_ID_PK_BYTES]; /* long-term ML-DSA-65 public key */
    uint8_t ek[MLKEM768_EK_BYTES];            /* ephemeral ML-KEM-768 key */
} bootlegger_hello_t;

/* REPLY (B->A). */
typedef struct {
    bootlegger_hello_t hello;             /* B's version, nonce, identity, ephemeral key */
    uint8_t ct[MLKEM768_CT_BYTES];        /* encapsulation to A's ephemeral key */
    uint8_t sig[BOOTLEGGER_ID_SIG_BYTES]; /* B's signature over H(T) */
} bootlegger_reply_t;

/* FINISH (A->B). */
typedef struct {
    uint8_t ct[MLKEM768_CT_BYTES];        /* encapsulation to B's ephemeral key */
    uint8_t sig[BOOTLEGGER_ID_SIG_BYTES]; /* A's signature over H(T) */
    uint8_t confirm[32];                  /* key confirmation (HMAC-SHA256) */
} bootlegger_finish_t;

/* Bounded cache of nonces seen (our own and peers'), oldest evicted. */
typedef struct {
    uint8_t nonce[BOOTLEGGER_REPLAY_SLOTS][BOOTLEGGER_NONCE_BYTES];
    uint32_t next;
    uint32_t count;
} bootlegger_replay_cache_t;

/* In-progress handshake, caller-owned (one per handshake; wiped when done or
 * on failure). Holds the ephemeral ML-KEM secret key. */
typedef struct {
    uint8_t role; /* 1 = initiator, 2 = responder */
    uint8_t stage;
    uint8_t nonce_self[BOOTLEGGER_NONCE_BYTES];
    uint8_t ek_self[MLKEM768_EK_BYTES];
    uint8_t dk_self[MLKEM768_DK_BYTES];
    uint8_t peer_identity[BOOTLEGGER_ID_PK_BYTES]; /* responder: A's identity */
    uint8_t t_hash[32];
    uint8_t ss_reply[32];   /* responder: the secret it encapsulated to A */
    uint8_t ct_reply_h[32]; /* responder: SHA-256 of that ciphertext */
} bootlegger_hs_t;

/* Peer entry in DHT tracker */
typedef struct {
    uint32_t peer_id;
    uint32_t ip_address;
    uint16_t port;
    uint8_t  pubkey[32];
    uint8_t  flags;          /* bit0=online, bit1=fileshare, bit2=chat, bit3=news, bit4=voice, bit5=video */
    uint64_t last_seen;      /* Phase tick of last heartbeat */
} bootlegger_peer_t;

/* Chat message */
typedef struct {
    uint32_t channel_id;
    uint32_t sender_id;
    uint8_t  message[BOOTLEGGER_MAX_MSG];
    uint64_t timestamp;      /* Phase tick */
    uint8_t  msg_type;       /* 0=normal, 1=action, 2=system */
} bootlegger_chat_msg_t;

/* File chunk — content-addressed via smap */
typedef struct {
    uint32_t file_id;        /* smap CID */
    uint32_t chunk_index;
    uint32_t total_chunks;
    uint16_t chunk_size;
    uint8_t  data[BOOTLEGGER_CHUNK_SIZE];
    uint8_t  hash[BOOTLEGGER_HASH_SIZE];  /* SHA-256 of chunk */
} bootlegger_file_chunk_t;

/* News/social post — NNTP-style threaded */
typedef struct {
    uint32_t thread_id;
    uint32_t parent_id;     /* 0 for top-level */
    uint32_t author_id;
    uint8_t  subject[128];
    uint8_t  body[4096];
    uint64_t timestamp;
    uint8_t  post_type;     /* 0=news, 1=social, 2=essay */
} bootlegger_news_post_t;

/* Voice frame — for VoIP streaming */
typedef struct {
    uint32_t call_id;
    uint16_t seq_num;
    uint16_t frame_size;    /* Actual bytes in frame */
    uint8_t  codec;         /* 0=PCM, 1=opus-like, 2=ADPCM */
    uint8_t  frame[512];    /* Audio frame data */
} bootlegger_voice_frame_t;

/* Video frame — for video chat */
typedef struct {
    uint32_t call_id;
    uint16_t seq_num;
    uint16_t width;
    uint16_t height;
    uint8_t  codec;         /* 0=raw, 1=H.264-like, 2=MJPEG-like */
    uint8_t  frame_type;    /* 0=I, 1=P, 2=B */
    uint16_t frame_size;
    uint8_t  frame[4096];   /* Video frame data (may span multiple packets) */
} bootlegger_video_frame_t;

/* Call setup */
typedef struct {
    uint32_t call_id;
    uint32_t caller_id;
    uint32_t callee_id;
    uint8_t  call_type;     /* 0=voice, 1=video, 2=conference */
    uint8_t  codec_pref;    /* Preferred codec */
    uint16_t audio_port;
    uint16_t video_port;
    uint8_t session_key[32]; /* label only: never filled */
} bootlegger_call_setup_t;

/* IRC-style channel */
typedef struct {
    uint32_t channel_id;
    uint8_t  name[64];
    uint8_t  topic[256];
    uint32_t member_count;
    uint8_t encrypted; /* label only: nothing is encrypted by this flag */
} bootlegger_channel_t;

/* Role-based access */
typedef enum {
    BOOTLEGGER_ROLE_GUEST    = 0,
    BOOTLEGGER_ROLE_USER     = 1,
    BOOTLEGGER_ROLE_CONTRIB  = 2,
    BOOTLEGGER_ROLE_ADMIN    = 3,
} bootlegger_role_t;

/* Connection state */
typedef struct {
    uint32_t peer_id;
    uint32_t ip;
    uint16_t port;
    uint8_t connected;     /* slot in use (no socket exists) */
    uint8_t authenticated; /* set only by a verified handshake */
    bootlegger_role_t role;
    uint64_t last_activity;
    /* Stream state for paraconsistent error handling */
    uint8_t  lpres_state;   /* LPRES 5-state: 0=ok, 1=speculative, 2=isolated, 3=contradiction, 4=drop */
    /* Authentication and session state */
    uint16_t version;            /* negotiated protocol version */
    uint8_t peer_id_hash[32];    /* SHA-256 of the peer's ML-DSA identity key */
    uint8_t transcript_hash[32]; /* H(T) of the handshake */
    uint8_t auth_tag[32];        /* binds keys, transcript and peer (authenticate) */
    uint8_t pinned_id_hash[32];  /* SHA-256 of the expected identity, if pinned */
    uint8_t pinned;
    uint8_t has_session;
    uint8_t is_initiator;
    uint8_t tx_key[BOOTLEGGER_KEY_SIZE];
    uint8_t rx_key[BOOTLEGGER_KEY_SIZE];
    uint64_t tx_seq;
    uint64_t rx_seq;
} bootlegger_conn_t;

/* P2P node state */
typedef struct {
    bootlegger_conn_t   connections[BOOTLEGGER_MAX_PEERS];
    bootlegger_peer_t   tracker[BOOTLEGGER_MAX_PEERS];
    bootlegger_channel_t channels[BOOTLEGGER_MAX_CHANNEL];
    uint32_t         local_peer_id;
    uint8_t          local_pubkey[32];
    uint16_t         listen_port;
    uint32_t         active_calls;
    uint64_t         phase_tick;
    bootlegger_replay_cache_t replay; /* nonces seen by this node's handshakes */
} bootlegger_node_t;

/* ============================================================
 * Core API
 * ============================================================ */

/* Initialization */
int bootlegger_init(bootlegger_node_t *node, uint16_t port);
int bootlegger_shutdown(bootlegger_node_t *node);

/* Identity: derive an ML-DSA-65 key pair from a 32-byte secret seed. */
int bootlegger_identity_init(bootlegger_identity_t *id, const uint8_t seed[32]);
/* Peer id: first 4 bytes (BE) of SHA3-256(identity public key). */
uint32_t bootlegger_peer_id_of(const uint8_t identity[BOOTLEGGER_ID_PK_BYTES]);
/* Pin the identity the next handshake on `conn` must authenticate. */
int bootlegger_pin_peer(bootlegger_conn_t *conn, const uint8_t identity[BOOTLEGGER_ID_PK_BYTES]);
void bootlegger_replay_init(bootlegger_replay_cache_t *rc);

/* Handshake (see the header comment). Every call returns 0 or a negative
 * BOOTLEGGER_E* code; on any error the hs state is wiped and the connection
 * is left unauthenticated. `rnd` is 32 bytes of fresh entropy per call.
 *   initiate: A builds HELLO.
 *   respond:  B checks HELLO (version, nonce, replay, pin) and builds REPLY.
 *   finish:   A checks REPLY (version, nonce, replay, pin, sig_B over T),
 *             builds FINISH and completes its session on `conn`.
 *   accept:   B checks FINISH (sig_A over T, key confirmation) and completes
 *             its session on `conn`. */
int bootlegger_hs_initiate(bootlegger_hs_t *hs, const bootlegger_identity_t *self,
                           bootlegger_replay_cache_t *rc, const uint8_t rnd[BOOTLEGGER_RAND_BYTES],
                           bootlegger_hello_t *out);
int bootlegger_hs_respond(bootlegger_hs_t *hs, bootlegger_conn_t *conn,
                          const bootlegger_identity_t *self, bootlegger_replay_cache_t *rc,
                          const bootlegger_hello_t *in, const uint8_t rnd[BOOTLEGGER_RAND_BYTES],
                          bootlegger_reply_t *out);
int bootlegger_hs_finish(bootlegger_hs_t *hs, bootlegger_conn_t *conn,
                         const bootlegger_identity_t *self, bootlegger_replay_cache_t *rc,
                         const bootlegger_reply_t *in, const uint8_t rnd[BOOTLEGGER_RAND_BYTES],
                         bootlegger_finish_t *out);
int bootlegger_hs_accept(bootlegger_hs_t *hs, bootlegger_conn_t *conn,
                         const bootlegger_finish_t *in);

/* The transcript hash H(T) both sides sign (exposed for tests and tools). */
void bootlegger_transcript_hash(uint16_t version, const uint8_t id_a[BOOTLEGGER_ID_PK_BYTES],
                                const uint8_t ek_a[MLKEM768_EK_BYTES],
                                const uint8_t nonce_b[BOOTLEGGER_NONCE_BYTES],
                                const uint8_t id_b[BOOTLEGGER_ID_PK_BYTES],
                                const uint8_t ek_b[MLKEM768_EK_BYTES],
                                const uint8_t nonce_a[BOOTLEGGER_NONCE_BYTES], uint8_t out[32]);

/* Seal / open one message: hdr(3) || ciphertext || tag(16). seal returns the
 * output length; open returns the payload length. Both need a session. */
int bootlegger_seal_msg(bootlegger_conn_t *conn, bootlegger_msg_type_t type, const void *payload,
                        uint16_t len, uint8_t *out, uint32_t cap);
int bootlegger_open_msg(bootlegger_conn_t *conn, const uint8_t *in, uint32_t in_len,
                        bootlegger_msg_type_t *type, void *payload, uint32_t cap);

/* Peer discovery via DHT (smap-backed) */
int bootlegger_tracker_register(bootlegger_node_t *node);
int bootlegger_tracker_list(bootlegger_node_t *node, bootlegger_peer_t *peers, int max);
int bootlegger_tracker_unregister(bootlegger_node_t *node);
int bootlegger_tracker_heartbeat(bootlegger_node_t *node);

/* Direct-stream peer connection */
int bootlegger_connect(bootlegger_node_t *node, uint32_t ip, uint16_t port);
int bootlegger_disconnect(bootlegger_node_t *node, uint32_t peer_id);

/* Message dispatch: no transport exists, so send returns BOOTLEGGER_ENOKEY
 * without a session and BOOTLEGGER_ENOTIMPL otherwise; recv returns
 * BOOTLEGGER_ENOTIMPL. Everything built on them inherits that. */
int bootlegger_send_msg(bootlegger_conn_t *conn, bootlegger_msg_type_t type,
                     const void *payload, uint16_t len);
int bootlegger_recv_msg(bootlegger_conn_t *conn, bootlegger_msg_type_t *type,
                     void *payload, uint16_t *len);

/* ============================================================
 * Chat API (IRC-style channels)
 * ============================================================ */

int bootlegger_chat_join(bootlegger_node_t *node, uint32_t channel_id);
int bootlegger_chat_leave(bootlegger_node_t *node, uint32_t channel_id);
int bootlegger_chat_send(bootlegger_node_t *node, uint32_t channel_id,
                      const char *message);
int bootlegger_chat_recv(bootlegger_node_t *node, bootlegger_chat_msg_t *msg);
int bootlegger_chat_create_channel(bootlegger_node_t *node, const char *name,
                                const char *topic, uint8_t encrypted);

/* ============================================================
 * File Transfer API (chunked, resumable, smap-addressed)
 * ============================================================ */

int bootlegger_file_list(bootlegger_conn_t *conn, void *result, int max);
int bootlegger_file_request(bootlegger_conn_t *conn, uint32_t file_id,
                         uint32_t start_chunk);
int bootlegger_file_recv_chunk(bootlegger_conn_t *conn,
                            bootlegger_file_chunk_t *chunk);
int bootlegger_file_share(bootlegger_node_t *node, const char *path,
                       uint32_t *file_id);

/* ============================================================
 * News / Social Feed API (NNTP-style threaded)
 * ============================================================ */

int bootlegger_news_create_thread(bootlegger_node_t *node, const char *subject,
                               const char *body);
int bootlegger_news_reply(bootlegger_node_t *node, uint32_t thread_id,
                       const char *body);
int bootlegger_news_list(bootlegger_node_t *node, bootlegger_news_post_t *posts,
                      int max);
int bootlegger_social_post(bootlegger_node_t *node, const char *body,
                        uint8_t post_type);
int bootlegger_social_sync(bootlegger_node_t *node, uint64_t since_tick);

/* ============================================================
 * Media Streaming API (VoIP + Video)
 * ============================================================ */

int bootlegger_call_setup(bootlegger_node_t *node, uint32_t peer_id,
                       uint8_t call_type, uint32_t *call_id);
int bootlegger_call_accept(bootlegger_node_t *node, uint32_t call_id);
int bootlegger_call_hangup(bootlegger_node_t *node, uint32_t call_id);
int bootlegger_voice_send(bootlegger_node_t *node, uint32_t call_id,
                       const uint8_t *frame, uint16_t size, uint8_t codec);
int bootlegger_voice_recv(bootlegger_node_t *node, uint32_t call_id,
                       bootlegger_voice_frame_t *frame);
int bootlegger_video_send(bootlegger_node_t *node, uint32_t call_id,
                       const uint8_t *frame, uint16_t size,
                       uint8_t frame_type);
int bootlegger_video_recv(bootlegger_node_t *node, uint32_t call_id,
                       bootlegger_video_frame_t *frame);

/* Bandwidth adaptation — scales based on hardware constraints */
int bootlegger_adapt_bitrate(bootlegger_node_t *node, uint32_t call_id,
                          uint32_t available_bw);

/* ============================================================
 * Legacy Protocol Bridges
 * ============================================================ */

/* IRC bridge — lightweight text channels over P2P mesh */
int bootlegger_irc_init(bootlegger_node_t *node);
int bootlegger_irc_send(bootlegger_node_t *node, uint32_t channel_id,
                     const char *nick, const char *message);
int bootlegger_irc_recv(bootlegger_node_t *node, char *nick, char *message);

/* NNTP bridge — sovereign news feeds */
int bootlegger_nntp_init(bootlegger_node_t *node);
int bootlegger_nntp_post(bootlegger_node_t *node, const char *group,
                      const char *subject, const char *body);
int bootlegger_nntp_fetch(bootlegger_node_t *node, const char *group,
                       bootlegger_news_post_t *posts, int max);

/* Gopher bridge — minimalist document publishing */
int bootlegger_gopher_serve(bootlegger_node_t *node, const char *selector,
                         const uint8_t *content, uint16_t len);
int bootlegger_gopher_fetch(bootlegger_conn_t *conn, const char *selector,
                         uint8_t *buf, uint16_t *len);

/* Gemini bridge — gemini protocol support */
int bootlegger_gemini_serve(bootlegger_node_t *node, const char *url,
                         const uint8_t *content, uint16_t len);
int bootlegger_gemini_fetch(bootlegger_conn_t *conn, const char *url,
                         uint8_t *buf, uint16_t *len);

/* SMTP bridge — sovereign email */
int bootlegger_smtp_send(bootlegger_node_t *node, uint32_t peer_id,
                      const char *from, const char *to,
                      const char *subject, const char *body);
int bootlegger_smtp_recv(bootlegger_node_t *node, char *from, char *subject,
                      char *body);

/* ============================================================
 * Security & Access Control
 * ============================================================ */

int bootlegger_authenticate(bootlegger_conn_t *conn, bootlegger_role_t *role);
int bootlegger_authorize(bootlegger_conn_t *conn, bootlegger_msg_type_t action);
int bootlegger_set_role(bootlegger_node_t *node, uint32_t peer_id,
                     bootlegger_role_t role);

/* ============================================================
 * Paraconsistent Error Handling (LPRES integration)
 * ============================================================ */

/* LPRES 5-state evaluation for stream anomalies:
 *   G+  (speculative) — delayed frame, route through speculative buffer
 *   G0  (isolated)    — corrupted frame, isolate without stalling stream
 *   G-  (contradiction) — conflicting state, flag for resolution
 *   G*  (drop)        — unrecoverable, graceful degradation
 *   G=  (ok)          — normal flow
 */
int bootlegger_lpres_eval(bootlegger_conn_t *conn, uint8_t anomaly_type);
int bootlegger_lpres_recover(bootlegger_conn_t *conn);

/* ============================================================
 * Garlic Routing Integration: not implemented (BOOTLEGGER_ENOTIMPL)
 * ============================================================ */

int bootlegger_garlic_wrap(bootlegger_conn_t *conn, uint8_t *data, uint16_t *len);
int bootlegger_garlic_unwrap(bootlegger_conn_t *conn, uint8_t *data, uint16_t *len);

#endif /* BOOTLEGGER_H */
