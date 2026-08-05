/*
 * SUPERSEDED — moved to kernel/src/bootlegger/bootlegger.h under new,
 * OS-original naming (no reference to any third-party product name).
 * This file is kept only because this environment's tooling cannot
 * delete files; do not include or build it. See bootlegger.h/.c for
 * the live implementation.
 *
 * p2p_caracho.h — Carracho-style P2P transport layer for ZEDEC pqOS
 *
 * High-performance, low-latency decentralized routing and data
 * synchronization without traditional choke points. Direct-stream
 * peer channels bypass multi-hop lookups and congested trackers.
 *
 * Integrates with: plnp (encrypted transport), smap (content addressing),
 * pungent (garlic routing), lpres (paraconsistent error handling),
 * identity (authentication), phase_coord (coordination)
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 * 36N9 Genetics, LLC — Irrevocable, Interdimensional
 */

#ifndef P2P_CARACHO_H
#define P2P_CARACHO_H

#include "m5_types.h"

/* ============================================================
 * Protocol Constants
 * ============================================================ */

#define CARACHO_MAGIC       "TCPZEDEC"
#define CARACHO_MAGIC_LEN   8
#define CARACHO_VERSION     1
#define CARACHO_MAX_PEERS   256
#define CARACHO_MAX_CHUNKS  65536
#define CARACHO_CHUNK_SIZE  4096
#define CARACHO_MAX_MSG     512
#define CARACHO_MAX_CHANNEL 64
#define CARACHO_HASH_SIZE   32

/* ============================================================
 * Message Types
 * ============================================================ */

typedef enum {
    CARACHO_MSG_HANDSHAKE    = 0,
    CARACHO_MSG_CHAT         = 1,
    CARACHO_MSG_PRIVATE      = 2,
    CARACHO_MSG_FILE_LIST    = 3,
    CARACHO_MSG_FILE_GET     = 4,
    CARACHO_MSG_FILE_CHUNK   = 5,
    CARACHO_MSG_NEWS_POST    = 6,
    CARACHO_MSG_NEWS_LIST    = 7,
    CARACHO_MSG_TRACKER_REG  = 8,
    CARACHO_MSG_TRACKER_LIST = 9,
    CARACHO_MSG_PING         = 10,
    CARACHO_MSG_PONG         = 11,
    CARACHO_MSG_BYE          = 12,
    /* Media streaming */
    CARACHO_MSG_VOICE_FRAME  = 13,
    CARACHO_MSG_VIDEO_FRAME  = 14,
    CARACHO_MSG_CALL_SETUP   = 15,
    CARACHO_MSG_CALL_ACCEPT  = 16,
    CARACHO_MSG_CALL_HANGUP  = 17,
    /* Legacy protocol bridges */
    CARACHO_MSG_IRC          = 18,
    CARACHO_MSG_NNTP         = 19,
    CARACHO_MSG_GOPHER       = 20,
    CARACHO_MSG_GEMINI       = 21,
    CARACHO_MSG_SMTP         = 22,
    CARACHO_MSG_IMAP         = 23,
    /* Social feed */
    CARACHO_MSG_SOCIAL_POST  = 24,
    CARACHO_MSG_SOCIAL_SYNC  = 25,
    CARACHO_MSG_SOCIAL_LIKE  = 26,
} caracho_msg_type_t;

/* ============================================================
 * Data Structures
 * ============================================================ */

/* Handshake — Carracho-inspired with quantum identity */
typedef struct {
    char     magic[CARACHO_MAGIC_LEN];  /* "TCPZEDEC" */
    uint16_t version;                    /* Protocol version (big-endian) */
    uint16_t phase;                      /* Current phase tick */
    uint32_t peer_id;                    /* Identity-derived peer ID */
    uint8_t  pubkey[32];                 /* Ed25519 public key */
    uint8_t  signature[64];              /* Handshake signature */
} caracho_handshake_t;

/* Peer entry in DHT tracker */
typedef struct {
    uint32_t peer_id;
    uint32_t ip_address;
    uint16_t port;
    uint8_t  pubkey[32];
    uint8_t  flags;          /* bit0=online, bit1=fileshare, bit2=chat, bit3=news, bit4=voice, bit5=video */
    uint64_t last_seen;      /* Phase tick of last heartbeat */
} caracho_peer_t;

/* Chat message */
typedef struct {
    uint32_t channel_id;
    uint32_t sender_id;
    uint8_t  message[CARACHO_MAX_MSG];
    uint64_t timestamp;      /* Phase tick */
    uint8_t  msg_type;       /* 0=normal, 1=action, 2=system */
} caracho_chat_msg_t;

/* File chunk — content-addressed via smap */
typedef struct {
    uint32_t file_id;        /* smap CID */
    uint32_t chunk_index;
    uint32_t total_chunks;
    uint16_t chunk_size;
    uint8_t  data[CARACHO_CHUNK_SIZE];
    uint8_t  hash[CARACHO_HASH_SIZE];  /* SHA-256 of chunk */
} caracho_file_chunk_t;

/* News/social post — NNTP-style threaded */
typedef struct {
    uint32_t thread_id;
    uint32_t parent_id;     /* 0 for top-level */
    uint32_t author_id;
    uint8_t  subject[128];
    uint8_t  body[4096];
    uint64_t timestamp;
    uint8_t  post_type;     /* 0=news, 1=social, 2=essay */
} caracho_news_post_t;

/* Voice frame — for VoIP streaming */
typedef struct {
    uint32_t call_id;
    uint16_t seq_num;
    uint16_t frame_size;    /* Actual bytes in frame */
    uint8_t  codec;         /* 0=PCM, 1=opus-like, 2=ADPCM */
    uint8_t  frame[512];    /* Audio frame data */
} caracho_voice_frame_t;

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
} caracho_video_frame_t;

/* Call setup */
typedef struct {
    uint32_t call_id;
    uint32_t caller_id;
    uint32_t callee_id;
    uint8_t  call_type;     /* 0=voice, 1=video, 2=conference */
    uint8_t  codec_pref;    /* Preferred codec */
    uint16_t audio_port;
    uint16_t video_port;
    uint8_t  session_key[32]; /* Ephemeral session key */
} caracho_call_setup_t;

/* IRC-style channel */
typedef struct {
    uint32_t channel_id;
    uint8_t  name[64];
    uint8_t  topic[256];
    uint32_t member_count;
    uint8_t  encrypted;     /* 1 if PLNP-wrapped */
} caracho_channel_t;

/* Role-based access (Carracho had admin/user/guest) */
typedef enum {
    CARACHO_ROLE_GUEST    = 0,
    CARACHO_ROLE_USER     = 1,
    CARACHO_ROLE_CONTRIB  = 2,
    CARACHO_ROLE_ADMIN    = 3,
} caracho_role_t;

/* Connection state */
typedef struct {
    uint32_t peer_id;
    uint32_t ip;
    uint16_t port;
    uint8_t  connected;
    uint8_t  authenticated;
    caracho_role_t role;
    uint64_t last_activity;
    /* Stream state for paraconsistent error handling */
    uint8_t  lpres_state;   /* LPRES 5-state: 0=ok, 1=speculative, 2=isolated, 3=contradiction, 4=drop */
} caracho_conn_t;

/* P2P node state */
typedef struct {
    caracho_conn_t   connections[CARACHO_MAX_PEERS];
    caracho_peer_t   tracker[CARACHO_MAX_PEERS];
    caracho_channel_t channels[CARACHO_MAX_CHANNEL];
    uint32_t         local_peer_id;
    uint8_t          local_pubkey[32];
    uint16_t         listen_port;
    uint32_t         active_calls;
    uint64_t         phase_tick;
} caracho_node_t;

/* ============================================================
 * Core API
 * ============================================================ */

/* Initialization */
int caracho_init(caracho_node_t *node, uint16_t port);
int caracho_shutdown(caracho_node_t *node);

/* Handshake — Carracho-style direct TCP connection */
int caracho_handshake_send(caracho_conn_t *conn);
int caracho_handshake_recv(caracho_conn_t *conn, caracho_handshake_t *hs);

/* Peer discovery via DHT (smap-backed) */
int caracho_tracker_register(caracho_node_t *node);
int caracho_tracker_list(caracho_node_t *node, caracho_peer_t *peers, int max);
int caracho_tracker_unregister(caracho_node_t *node);
int caracho_tracker_heartbeat(caracho_node_t *node);

/* Direct-stream peer connection */
int caracho_connect(caracho_node_t *node, uint32_t ip, uint16_t port);
int caracho_disconnect(caracho_node_t *node, uint32_t peer_id);

/* Message dispatch — routes through LPRES paraconsistent filter */
int caracho_send_msg(caracho_conn_t *conn, caracho_msg_type_t type,
                     const void *payload, uint16_t len);
int caracho_recv_msg(caracho_conn_t *conn, caracho_msg_type_t *type,
                     void *payload, uint16_t *len);

/* ============================================================
 * Chat API (IRC-style channels)
 * ============================================================ */

int caracho_chat_join(caracho_node_t *node, uint32_t channel_id);
int caracho_chat_leave(caracho_node_t *node, uint32_t channel_id);
int caracho_chat_send(caracho_node_t *node, uint32_t channel_id,
                      const char *message);
int caracho_chat_recv(caracho_node_t *node, caracho_chat_msg_t *msg);
int caracho_chat_create_channel(caracho_node_t *node, const char *name,
                                const char *topic, uint8_t encrypted);

/* ============================================================
 * File Transfer API (chunked, resumable, smap-addressed)
 * ============================================================ */

int caracho_file_list(caracho_conn_t *conn, void *result, int max);
int caracho_file_request(caracho_conn_t *conn, uint32_t file_id,
                         uint32_t start_chunk);
int caracho_file_recv_chunk(caracho_conn_t *conn,
                            caracho_file_chunk_t *chunk);
int caracho_file_share(caracho_node_t *node, const char *path,
                       uint32_t *file_id);

/* ============================================================
 * News / Social Feed API (NNTP-style threaded)
 * ============================================================ */

int caracho_news_create_thread(caracho_node_t *node, const char *subject,
                               const char *body);
int caracho_news_reply(caracho_node_t *node, uint32_t thread_id,
                       const char *body);
int caracho_news_list(caracho_node_t *node, caracho_news_post_t *posts,
                      int max);
int caracho_social_post(caracho_node_t *node, const char *body,
                        uint8_t post_type);
int caracho_social_sync(caracho_node_t *node, uint64_t since_tick);

/* ============================================================
 * Media Streaming API (VoIP + Video)
 * ============================================================ */

int caracho_call_setup(caracho_node_t *node, uint32_t peer_id,
                       uint8_t call_type, uint32_t *call_id);
int caracho_call_accept(caracho_node_t *node, uint32_t call_id);
int caracho_call_hangup(caracho_node_t *node, uint32_t call_id);
int caracho_voice_send(caracho_node_t *node, uint32_t call_id,
                       const uint8_t *frame, uint16_t size, uint8_t codec);
int caracho_voice_recv(caracho_node_t *node, uint32_t call_id,
                       caracho_voice_frame_t *frame);
int caracho_video_send(caracho_node_t *node, uint32_t call_id,
                       const uint8_t *frame, uint16_t size,
                       uint8_t frame_type);
int caracho_video_recv(caracho_node_t *node, uint32_t call_id,
                       caracho_video_frame_t *frame);

/* Bandwidth adaptation — scales based on hardware constraints */
int caracho_adapt_bitrate(caracho_node_t *node, uint32_t call_id,
                          uint32_t available_bw);

/* ============================================================
 * Legacy Protocol Bridges
 * ============================================================ */

/* IRC bridge — lightweight text channels over P2P mesh */
int caracho_irc_init(caracho_node_t *node);
int caracho_irc_send(caracho_node_t *node, uint32_t channel_id,
                     const char *nick, const char *message);
int caracho_irc_recv(caracho_node_t *node, char *nick, char *message);

/* NNTP bridge — sovereign news feeds */
int caracho_nntp_init(caracho_node_t *node);
int caracho_nntp_post(caracho_node_t *node, const char *group,
                      const char *subject, const char *body);
int caracho_nntp_fetch(caracho_node_t *node, const char *group,
                       caracho_news_post_t *posts, int max);

/* Gopher bridge — minimalist document publishing */
int caracho_gopher_serve(caracho_node_t *node, const char *selector,
                         const uint8_t *content, uint16_t len);
int caracho_gopher_fetch(caracho_conn_t *conn, const char *selector,
                         uint8_t *buf, uint16_t *len);

/* Gemini bridge — gemini protocol support */
int caracho_gemini_serve(caracho_node_t *node, const char *url,
                         const uint8_t *content, uint16_t len);
int caracho_gemini_fetch(caracho_conn_t *conn, const char *url,
                         uint8_t *buf, uint16_t *len);

/* SMTP bridge — sovereign email */
int caracho_smtp_send(caracho_node_t *node, uint32_t peer_id,
                      const char *from, const char *to,
                      const char *subject, const char *body);
int caracho_smtp_recv(caracho_node_t *node, char *from, char *subject,
                      char *body);

/* ============================================================
 * Security & Access Control
 * ============================================================ */

int caracho_authenticate(caracho_conn_t *conn, caracho_role_t *role);
int caracho_authorize(caracho_conn_t *conn, caracho_msg_type_t action);
int caracho_set_role(caracho_node_t *node, uint32_t peer_id,
                     caracho_role_t role);

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
int caracho_lpres_eval(caracho_conn_t *conn, uint8_t anomaly_type);
int caracho_lpres_recover(caracho_conn_t *conn);

/* ============================================================
 * Garlic Routing Integration (PungentClove)
 * ============================================================ */

int caracho_garlic_wrap(caracho_conn_t *conn, uint8_t *data, uint16_t *len);
int caracho_garlic_unwrap(caracho_conn_t *conn, uint8_t *data, uint16_t *len);

#endif /* P2P_CARACHO_H */
