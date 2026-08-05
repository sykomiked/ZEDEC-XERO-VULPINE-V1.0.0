/*
 * plnp.h — Phase-Lattice Network Protocol (PLNP)
 *
 * Replaces TCP/IP with content-addressed, cryptographic phase-routing.
 *
 * 5-Layer Stack:
 *   Layer 5: 5PL Application Layer (Dossiers: +1, -1, 0, +0, 1)
 *   Layer 4: Content & Derivation Layer (Merkle VFS, CID + K1..K5)
 *   Layer 3: PungentClove Garlic Mixnet (Anonymity, Bulbs/Cloves)
 *   Layer 2: Phase-Tick Scheduler (10ms kernel clock quantization)
 *   Layer 1: Physical Substrate (Ethernet / SDR / Packet Radio)
 *
 * Frame Format (PLNP v1):
 *   0x00-0x03: Magic (0x5A4D504E = "ZMPN")
 *   0x04:      Version (1)
 *   0x05:      Layer flags (bit0=5PL, bit1=garlic, bit2=phase-tick, bit3=RTL)
 *   0x06:      Phase state (0=FALSE, 1=TRUE, 2=GLUT, 3=GLUT+, 4=GLUT-, 5=GLUT0)
 *   0x07:      Derivation key index (K1=1..K5=5)
 *   0x08-0x27: Source CID (32-byte content hash)
 *   0x28-0x47: Destination CID (32-byte content hash)
 *   0x48-0x57: Merkle root (32-byte, truncated to 16 in compact mode)
 *   0x58-0x5B: Sequence number (uint32, phase-tick aligned)
 *   0x5C-0x5F: Payload length (uint32)
 *   0x60-0x6F: 5PL vector (claim/refutation/unknown/witness/consensus flags)
 *   0x70+:     Payload (garlic bulb or raw content)
 *   Trailer:   CRC32 (4 bytes) + end marker (0x5A)
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
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
#define PLNP_HEADER_SIZE    0x70        /* 112 bytes */
#define PLNP_TRAILER_SIZE   5           /* CRC32 + end marker */
#define PLNP_MAX_FRAME_SIZE (PLNP_HEADER_SIZE + PLNP_MAX_PAYLOAD + PLNP_TRAILER_SIZE)
#define PLNP_CRC_POLY       0xEDB88320

/* Derivation key indices */
#define PLNP_KEY_K1         1  /* Surface identity */
#define PLNP_KEY_K2         2  /* Deep authenticated */
#define PLNP_KEY_K3         3  /* Onion/Tor */
#define PLNP_KEY_K4         4  /* Garlic/I2P */
#define PLNP_KEY_K5         5  /* Shadow/ZK */

/* Layer flags */
#define PLNP_FLAG_5PL        0x01
#define PLNP_FLAG_GARLIC     0x02
#define PLNP_FLAG_PHASE_TICK 0x04
#define PLNP_FLAG_RTL        0x08
#define PLNP_FLAG_COMPACT    0x10
#define PLNP_FLAG_BROADCAST  0x20

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
    uint32_t seq_num;          /* 0x58: Phase-tick aligned sequence */
    uint32_t payload_len;      /* 0x5C: Payload length */
    uint8_t  fpl_vector;       /* 0x60: 5PL flags */
    uint8_t  reserved[15];     /* 0x61-0x6F: Reserved / alignment */
} __attribute__((packed)) plnp_header_t;

/* ===== PLNP Frame (complete) ===== */

typedef struct {
    plnp_header_t header;
    uint8_t       payload[PLNP_MAX_PAYLOAD];
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
int plnp_frame_seal(plnp_frame_t *f);
int plnp_frame_verify(const plnp_frame_t *f);
uint32_t plnp_frame_size(const plnp_frame_t *f);

/* Serialization */
int plnp_frame_serialize(const plnp_frame_t *f, uint8_t *buf, uint32_t buf_len);
int plnp_frame_deserialize(plnp_frame_t *f, const uint8_t *buf, uint32_t buf_len);

/* Connection management */
int32_t plnp_conn_create(plnp_stack_t *s, const uint8_t *src_cid,
                          const uint8_t *dst_cid, uint8_t key_index);
int plnp_conn_send(plnp_stack_t *s, uint32_t conn_idx,
                    const uint8_t *data, uint32_t len, uint8_t phase_state);
int plnp_conn_receive(plnp_stack_t *s, uint32_t conn_idx, plnp_frame_t *out);
int plnp_conn_close(plnp_stack_t *s, uint32_t conn_idx);

/* Phase resolution */
int plnp_resolve_phase(plnp_stack_t *s, uint32_t conn_idx, uint8_t phase_state);
const char *plnp_phase_name(uint8_t phase);
const char *plnp_state_name(plnp_conn_state_t state);
const char *plnp_key_name(uint8_t key_index);

/* CRC32 */
uint32_t plnp_crc32(const uint8_t *data, uint32_t len);

/* CID derivation */
void plnp_derive_cid(const uint8_t *content, uint32_t len, uint8_t *out_cid);
void plnp_derive_key(uint8_t key_index, const uint8_t *seed, uint32_t seed_len,
                     uint8_t *out_key);

#endif /* ZEDEC_PLNP_H */
