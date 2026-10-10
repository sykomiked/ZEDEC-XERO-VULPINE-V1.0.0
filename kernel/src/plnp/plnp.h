/*
 * plnp.h — Phase-Lattice Network Protocol (PLNP): a frame format and a
 * per-connection framing state machine
 *
 * WHAT THIS IS
 *   A frame layout with content-ID addressing fields, a CRC32 trailer, an
 *   optional HMAC-SHA256 authentication tag, and connection records that
 *   build outgoing frames and check incoming ones. It does not replace TCP/IP
 *   and carries no traffic by itself: there is no routing, no transport, no
 *   retransmission and no congestion control. A host transport moves the
 *   bytes that plnp_conn_send() produces and hands received bytes to
 *   plnp_conn_receive().
 *
 * INTEGRITY AND AUTHENTICITY
 *   - CRC32 detects accidental corruption only. Anyone can recompute it.
 *   - Authenticated frames (PLNP_FLAG_AUTH) carry HMAC-SHA256 over header and
 *     payload under a 32-byte connection key the caller supplies (e.g. from a
 *     KEM handshake elsewhere); plnp_conn_set_key() enables it. With a key
 *     set, a connection accepts only frames whose tag verifies. A connection
 *     without a key cannot verify an AUTH frame and rejects it.
 *   - plnp_conn_receive() counts a frame only after it deserializes, its CRC
 *     matches, its content IDs match the connection, its sequence number is
 *     the next one expected, and (if keyed) its tag verifies.
 *   - Payloads are not encrypted. The "garlic" flag is a label.
 *   - Content IDs from plnp_derive_cid() are SHA3-256 digests; keys from
 *     plnp_derive_key() are HKDF-SHA256 outputs and are only as secret as
 *     the seed.
 *
 * 5-Layer naming (labels): 5PL application, content/derivation, garlic
 * mixnet, phase-tick scheduler, physical substrate.
 *
 * Frame Format (PLNP v1):
 *   0x00-0x03: Magic (0x5A4D504E = "ZMPN")
 *   0x04:      Version (1)
 *   0x05:      Layer flags (bit0=5PL, bit1=garlic label, bit2=phase-tick,
 *              bit3=RTL, bit4=compact, bit5=broadcast, bit6=AUTH)
 *   0x06:      Phase state (0=FALSE, 1=TRUE, 2=GLUT, 3=GLUT+, 4=GLUT-, 5=GLUT0)
 *   0x07:      Derivation key index (K1=1..K5=5)
 *   0x08-0x27: Source CID (32-byte content hash)
 *   0x28-0x47: Destination CID (32-byte content hash)
 *   0x48-0x67: Merkle root (32 bytes)
 *   0x68-0x6B: Sequence number (uint32, host byte order)
 *   0x6C-0x6F: Payload length (uint32, host byte order)
 *   0x70:      5PL vector (claim/refutation/unknown/witness/consensus flags)
 *   0x71-0x7F: Reserved (zero)
 *   (The old PLNP_HEADER_SIZE of 0x70 cut the 5PL vector off the wire and out
 *   of the CRC; the header is now sizeof(plnp_header_t) = 128 bytes.)
 *   Payload, then [HMAC-SHA256 tag (32) if AUTH], CRC32 (4), end marker 0x5A
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */

#ifndef ZEDEC_PLNP_H
#define ZEDEC_PLNP_H

#include <stdint.h>
#include <stdbool.h>

/* ===== Constants ===== */

#define PLNP_MAGIC          0x5A4D504E  /* "ZMPN" */
#define PLNP_VERSION        1
#define PLNP_MAX_FRAMES     256
#define PLNP_MAX_PAYLOAD    4096
#define PLNP_HASH_SIZE      32
#define PLNP_CID_SIZE       32
#define PLNP_HEADER_SIZE    ((uint32_t) sizeof(plnp_header_t))
#define PLNP_TRAILER_SIZE   5           /* CRC32 + end marker */
#define PLNP_MAC_SIZE       32          /* HMAC-SHA256 tag on AUTH frames */
#define PLNP_MAX_FRAME_SIZE                                                                        \
    (PLNP_HEADER_SIZE + PLNP_MAX_PAYLOAD + PLNP_MAC_SIZE + PLNP_TRAILER_SIZE)
#define PLNP_CRC_POLY       0xEDB88320

/* Derivation key indices */
#define PLNP_KEY_K1         1  /* Surface identity */
#define PLNP_KEY_K2         2  /* Deep authenticated */
#define PLNP_KEY_K3         3  /* Onion label */
#define PLNP_KEY_K4         4  /* Garlic label */
#define PLNP_KEY_K5         5  /* Shadow/ZK */

/* Layer flags */
#define PLNP_FLAG_5PL        0x01
#define PLNP_FLAG_GARLIC     0x02
#define PLNP_FLAG_PHASE_TICK 0x04
#define PLNP_FLAG_RTL        0x08
#define PLNP_FLAG_COMPACT    0x10
#define PLNP_FLAG_BROADCAST  0x20
#define PLNP_FLAG_AUTH       0x40 /* HMAC-SHA256 tag present */

/* Phase states (maps to 5VL trit logic) */
#define PLNP_PHASE_FALSE     0
#define PLNP_PHASE_TRUE      1
#define PLNP_PHASE_GLUT      2
#define PLNP_PHASE_GLUT_PLUS 3
#define PLNP_PHASE_GLUT_MINUS 4
#define PLNP_PHASE_GLUT_ZERO  5

/* 5PL vector flags */
#define PLNP_5PL_CLAIM       0x01  /* (+1) */
#define PLNP_5PL_REFUTATION  0x02  /* (-1) */
#define PLNP_5PL_UNKNOWN     0x04  /* (0)  */
#define PLNP_5PL_WITNESS     0x08  /* (+0) */
#define PLNP_5PL_CONSENSUS   0x10  /* (1)  */

/* ===== PLNP Frame Header ===== */

typedef struct {
    uint32_t magic;            /* 0x00: PLNP_MAGIC */
    uint8_t  version;          /* 0x04: PLNP_VERSION */
    uint8_t  flags;            /* 0x05: Layer flags */
    uint8_t  phase_state;      /* 0x06: Current phase */
    uint8_t  key_index;        /* 0x07: K1..K5 */
    uint8_t  src_cid[PLNP_CID_SIZE];   /* 0x08: Source content ID */
    uint8_t  dst_cid[PLNP_CID_SIZE];   /* 0x28: Destination content ID */
    uint8_t  merkle_root[PLNP_HASH_SIZE]; /* 0x48: Merkle root */
    uint32_t seq_num;                     /* 0x68: Phase-tick aligned sequence */
    uint32_t payload_len;                 /* 0x6C: Payload length */
    uint8_t fpl_vector;                   /* 0x70: 5PL flags */
    uint8_t reserved[15];                 /* 0x71-0x7F: Reserved */
} __attribute__((packed)) plnp_header_t;

_Static_assert(sizeof(plnp_header_t) == 0x80, "PLNP header is 128 bytes on the wire");

/* ===== PLNP Frame (complete) ===== */

typedef struct {
    plnp_header_t header;
    uint8_t       payload[PLNP_MAX_PAYLOAD];
    uint8_t mac[PLNP_MAC_SIZE]; /* valid when PLNP_FLAG_AUTH is set */
    uint32_t      crc32;
    uint8_t       end_marker;
} plnp_frame_t;

/* ===== PLNP Connection State ===== */

typedef enum {
    PLNP_CONN_IDLE       = 0,
    PLNP_CONN_CREATING   = 1,  /* Frame being assembled */
    PLNP_CONN_SENDING    = 2,  /* Frame in transit */
    PLNP_CONN_RECEIVING  = 3,  /* Frame being received */
    PLNP_CONN_VERIFYING  = 4,  /* Merkle/CRC verification */
    PLNP_CONN_GLUT_FREEZE = 5, /* G0: corrupt frame locked */
    PLNP_CONN_COMPLETE   = 6,
    PLNP_CONN_FAILED     = 7,
} plnp_conn_state_t;

/* ===== PLNP Connection ===== */

typedef struct {
    uint32_t          id;
    plnp_conn_state_t state;
    uint8_t           src_cid[PLNP_CID_SIZE];
    uint8_t           dst_cid[PLNP_CID_SIZE];
    uint8_t           key_index;
    uint32_t          seq_num;
    uint32_t          bytes_sent;
    uint32_t          bytes_received;
    uint32_t          frames_sent;
    uint32_t          frames_received;
    uint32_t          glut_freezes;     /* G0 lock count */
    uint32_t          glut_plus_count;  /* Speculative accepts */
    uint32_t          glut_minus_count; /* Safe drops */
    uint32_t rx_seq;                    /* next sequence number expected */
    uint32_t frames_rejected;           /* failed any receive check */
    uint32_t frames_authenticated;      /* passed the HMAC check */
    uint8_t key[32];                    /* HMAC key (when has_key) */
    bool has_key;
    bool              active;
} plnp_conn_t;

/* ===== PLNP Protocol Stack ===== */

typedef struct {
    plnp_conn_t connections[PLNP_MAX_FRAMES];
    uint32_t    num_connections;
    uint32_t    total_frames_sent;
    uint32_t    total_frames_received;
    uint32_t    total_glut_freezes;
    uint32_t    total_glut_plus;
    uint32_t    total_glut_minus;
    uint32_t    phase_tick_ms;       /* 10ms default */
    uint8_t     default_key_index;   /* K1 default */
    bool        initialized;
} plnp_stack_t;

/* ===== API ===== */

void plnp_init(plnp_stack_t *s);

/* Frame operations */
int plnp_frame_init(plnp_frame_t *f, uint8_t key_index, uint8_t phase_state);
int plnp_frame_set_cids(plnp_frame_t *f, const uint8_t *src, const uint8_t *dst);
int plnp_frame_set_payload(plnp_frame_t *f, const uint8_t *data, uint32_t len);
int plnp_frame_set_5pl(plnp_frame_t *f, uint8_t vector_flags);
int plnp_frame_set_merkle(plnp_frame_t *f, const uint8_t *root);
/* seal/verify: CRC32 only (accidental corruption). */
int plnp_frame_seal(plnp_frame_t *f);
int plnp_frame_verify(const plnp_frame_t *f);
/* seal_auth/verify_auth: HMAC-SHA256 tag plus CRC32. verify_auth returns 0
 * only when the AUTH flag is set and the tag verifies under key; -3 on a bad
 * or missing tag. */
int plnp_frame_seal_auth(plnp_frame_t *f, const uint8_t key[32]);
int plnp_frame_verify_auth(const plnp_frame_t *f, const uint8_t key[32]);
uint32_t plnp_frame_size(const plnp_frame_t *f);

/* Serialization */
int plnp_frame_serialize(const plnp_frame_t *f, uint8_t *buf, uint32_t buf_len);
int plnp_frame_deserialize(plnp_frame_t *f, const uint8_t *buf, uint32_t buf_len);

/* Connection management */
int32_t plnp_conn_create(plnp_stack_t *s, const uint8_t *src_cid,
                          const uint8_t *dst_cid, uint8_t key_index);
/* Set the 32-byte HMAC key: frames sent are AUTH, frames received must be. */
int plnp_conn_set_key(plnp_stack_t *s, uint32_t conn_idx, const uint8_t key[32]);
/* Builds, seals and serializes one frame into out. Returns its length, or -1.
 * Nothing is transmitted. */
int plnp_conn_send(plnp_stack_t *s, uint32_t conn_idx, const uint8_t *data, uint32_t len,
                   uint8_t phase_state, uint8_t *out, uint32_t out_cap);
/* Checks received bytes (see above) and, if they pass, stores the frame in
 * out and returns the payload length. Returns a negative code otherwise and
 * counts nothing as received. */
int plnp_conn_receive(plnp_stack_t *s, uint32_t conn_idx, const uint8_t *buf, uint32_t buf_len,
                      plnp_frame_t *out);
int plnp_conn_close(plnp_stack_t *s, uint32_t conn_idx);

/* Phase resolution */
int plnp_resolve_phase(plnp_stack_t *s, uint32_t conn_idx, uint8_t phase_state);
const char *plnp_phase_name(uint8_t phase);
const char *plnp_state_name(plnp_conn_state_t state);
const char *plnp_key_name(uint8_t key_index);

/* CRC32 (IEEE 802.3) */
uint32_t plnp_crc32(const uint8_t *data, uint32_t len);

/* CID derivation (SHA3-256) and key derivation (HKDF-SHA256) */
void plnp_derive_cid(const uint8_t *content, uint32_t len, uint8_t *out_cid);
void plnp_derive_key(uint8_t key_index, const uint8_t *seed, uint32_t seed_len,
                     uint8_t *out_key);

#endif /* ZEDEC_PLNP_H */
