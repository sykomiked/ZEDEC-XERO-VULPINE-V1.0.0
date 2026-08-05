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
 * Integrates with: plnp (encrypted transport), smap (content addressing),
 * pungent (garlic routing), lpres (paraconsistent error handling),
 * identity (authentication), phase_coord (coordination)
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * License: SEL-3.3 — Streisand Engine License
 * 36N9 Genetics, LLC — Irrevocable, Interdimensional
 */

#ifndef BOOTLEGGER_H
#define BOOTLEGGER_H

#include "m5_types.h"

/* ============================================================
 * Protocol Constants
 * ============================================================ */

#define BOOTLEGGER_MAGIC       "TCPZEDEC"
#define BOOTLEGGER_MAGIC_LEN   8
#define BOOTLEGGER_VERSION     1
#define BOOTLEGGER_MAX_PEERS   256
#define BOOTLEGGER_MAX_CHUNKS  65536
#define BOOTLEGGER_CHUNK_SIZE  4096
#define BOOTLEGGER_MAX_MSG     512
#define BOOTLEGGER_MAX_CHANNEL 64
#define BOOTLEGGER_HASH_SIZE   32

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

/* Handshake */
typedef struct {
    char     magic[BOOTLEGGER_MAGIC_LEN];  /* "TCPZEDEC" */
    uint16_t version;                    /* Protocol version (big-endian) */
    uint16_t phase;                      /* Current phase tick */
    uint32_t peer_id;                    /* Identity-derived peer ID */
    uint8_t  pubkey[32];                 /* Ed25519 public key */
    uint8_t  signature[64];              /* Handshake signature */
} bootlegger_handshake_t;

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
    uint8_t  session_key[32]; /* Ephemeral session key */
} bootlegger_call_setup_t;

/* IRC-style channel */
typedef struct {
    uint32_t channel_id;
    uint8_t  name[64];
    uint8_t  topic[256];
    uint32_t member_count;
    uint8_t  encrypted;     /* 1 if PLNP-wrapped */
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
    uint8_t  connected;
    uint8_t  authenticated;
    bootlegger_role_t role;
    uint64_t last_activity;
    /* Stream state for paraconsistent error handling */
    uint8_t  lpres_state;   /* LPRES 5-state: 0=ok, 1=speculative, 2=isolated, 3=contradiction, 4=drop */
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
} bootlegger_node_t;

/* ============================================================
 * Core API
 * ============================================================ */

/* Initialization */
int bootlegger_init(bootlegger_node_t *node, uint16_t port);
int bootlegger_shutdown(bootlegger_node_t *node);

/* Handshake */
int bootlegger_handshake_send(bootlegger_conn_t *conn);
int bootlegger_handshake_recv(bootlegger_conn_t *conn, bootlegger_handshake_t *hs);

/* Peer discovery via DHT (smap-backed) */
int bootlegger_tracker_register(bootlegger_node_t *node);
int bootlegger_tracker_list(bootlegger_node_t *node, bootlegger_peer_t *peers, int max);
int bootlegger_tracker_unregister(bootlegger_node_t *node);
int bootlegger_tracker_heartbeat(bootlegger_node_t *node);

/* Direct-stream peer connection */
int bootlegger_connect(bootlegger_node_t *node, uint32_t ip, uint16_t port);
int bootlegger_disconnect(bootlegger_node_t *node, uint32_t peer_id);

/* Message dispatch — routes through LPRES paraconsistent filter */
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
 * Garlic Routing Integration (PungentClove)
 * ============================================================ */

int bootlegger_garlic_wrap(bootlegger_conn_t *conn, uint8_t *data, uint16_t *len);
int bootlegger_garlic_unwrap(bootlegger_conn_t *conn, uint8_t *data, uint16_t *len);

#endif /* BOOTLEGGER_H */
