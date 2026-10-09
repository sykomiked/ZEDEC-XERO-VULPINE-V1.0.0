/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* ehop.h — Endian Hopping: keyed alternating-endianness channels, bands and
 * private networks, always inside a ChaCha20-Poly1305 AEAD.
 *
 * WHAT IT IS. A frame's plaintext is cut into words (16, 32 or 64 bits). The
 * words are grouped into segments, and the segments alternate between "carrier
 * off" (bytes left as they are) and "carrier on" (bytes reordered). The
 * schedule borrows radio's three modulation knobs:
 *
 *   FM  flip rate: mean words per segment, with keyed hopping of each
 *       segment's length inside [rate - hop, rate + hop].
 *   PM  phase: where in the pattern the first flip lands; static, or stepping
 *       per epoch or per frame, plus a keyed per-frame offset.
 *   AM  depth: how much of each "on" word is reordered (nibble swap, adjacent
 *       byte-pair swap, half-word swap, full byte reversal), keyed per segment
 *       from the channel's allowed set.
 *
 * Every reordering is an involution, so ehop_apply() twice is the identity.
 * The schedule (lengths, phase, depth choices) is squeezed from SHAKE256 of
 * (channel key, channel id, epoch, sender, sequence number).
 *
 * HONEST SECURITY STATEMENT. Confidentiality and integrity come from the AEAD
 * (ChaCha20-Poly1305, kernel/src/tls/aead.c) and nothing else. Endianness
 * reordering on its own is a public permutation; keyed, it is still only a
 * byte permutation of the plaintext, not a cipher. What it adds: channel
 * separation (every AEAD key, tag key and schedule is bound to the full
 * channel configuration, so a frame opened under a different AM/FM/PM or key
 * fails), traffic-shape diversity, and defence in depth, at a measured cost
 * far below the AEAD's. Nothing here is a radio waveform; AM/FM/PM name the
 * parameters of a byte-order schedule.
 *
 * Freestanding integer C11: no libc, no float, no allocation, no 64-bit
 * division, fixed buffers. Randomness is always supplied by the caller.
 */
#ifndef ZXV_EHOP_H
#define ZXV_EHOP_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "../mlkem/mlkem768.h"

/* ===== sizes ===== */
#define EHOP_KEY_BYTES    32u
#define EHOP_TAG_BYTES    8u  /* channel-identification tag (truncated SHA3) */
#define EHOP_HDR_BYTES    24u /* clear header, authenticated as AAD */
#define EHOP_MAC_BYTES    16u /* Poly1305 tag */
#define EHOP_OVERHEAD     (EHOP_HDR_BYTES + EHOP_TAG_BYTES + EHOP_MAC_BYTES)
#define EHOP_MAX_PAYLOAD  2048u
#define EHOP_MAX_FRAME    (EHOP_MAX_PAYLOAD + EHOP_OVERHEAD)
#define EHOP_RATE_MAX     4096u /* FM: largest mean words per flip */
#define EHOP_REPLAY_SLOTS 16u   /* senders tracked per channel */
#define EHOP_VERSION      1u

/* ===== result codes ===== */
typedef enum {
    EHOP_OK = 0,
    EHOP_EARG = -1,     /* missing pointer or invalid configuration */
    EHOP_ESIZE = -2,    /* too long for a buffer, the band, or the frame */
    EHOP_EFORMAT = -3,  /* bad magic, version or length field */
    EHOP_ECHANNEL = -4, /* tag does not match: not this channel */
    EHOP_EEPOCH = -5,   /* frame is for another epoch */
    EHOP_EAUTH = -6,    /* AEAD tag failed: tampered, or wrong schedule/key */
    EHOP_EREPLAY = -7,  /* sequence number already seen */
    EHOP_ESIG = -8,     /* ML-DSA signature did not verify */
    EHOP_EMEMBER = -9,  /* not a member, or wrong recipient */
    EHOP_EFULL = -10,   /* table full (peers, channels, members, senders) */
    EHOP_ESEQ = -11,    /* sequence space exhausted */
    EHOP_EBAND = -12,   /* configuration outside its band, or no channel */
} ehop_err_t;

/* ===== AM: reordering depth (bit positions in an AM mask) =====
 * Each is an involution on a word. On a 16-bit word PAIR, HALF and FULL all
 * mean "swap the two bytes". */
#define EHOP_AM_NIBBLE 0x01u /* swap the two nibbles of every byte */
#define EHOP_AM_PAIR   0x02u /* swap adjacent bytes: b0<->b1, b2<->b3, ... */
#define EHOP_AM_HALF   0x04u /* swap the two halves of the word */
#define EHOP_AM_FULL   0x08u /* reverse all bytes (classic endianness flip) */
#define EHOP_AM_ALL    0x0Fu

/* ===== PM: how the phase evolves ===== */
#define EHOP_PM_STATIC    0u /* base phase + keyed per-frame offset */
#define EHOP_PM_PER_EPOCH 1u /* ... + step * epoch */
#define EHOP_PM_PER_FRAME 2u /* ... + step * sequence number */

/* ===== bands: named regions of the (FM, PM, AM) space ===== */
typedef enum {
    EHOP_BAND_CONTROL = 0, /* control and routing */
    EHOP_BAND_SETTLE = 1,  /* payments and settlement */
    EHOP_BAND_MEDIA = 2,   /* voice and video */
    EHOP_BAND_CHAT = 3,    /* chat and alerts */
    EHOP_BAND_BULK = 4,    /* bulk files */
    EHOP_BAND_COUNT = 5
} ehop_band_t;

typedef struct {
    const char *name;
    uint16_t fm_min, fm_max; /* allowed mean words per flip */
    uint8_t am_mask;         /* allowed AM depths */
    uint8_t pm_modes;        /* bit (1 << EHOP_PM_*) allowed */
    uint8_t priority;        /* 0 = forwarded first */
    uint16_t max_frame;      /* bytes on the wire, header included */
    uint16_t latency_ms;     /* target one-hop latency (a hint) */
} ehop_band_info_t;

/* The band plan; NULL for an out-of-range band. */
const ehop_band_info_t *ehop_band_info(uint32_t band);

/* Message classes an application or the overlay asks for. */
typedef enum {
    EHOP_MSG_CONTROL = 0,
    EHOP_MSG_ROUTING,
    EHOP_MSG_PAYMENT,
    EHOP_MSG_SETTLEMENT,
    EHOP_MSG_VOICE,
    EHOP_MSG_VIDEO,
    EHOP_MSG_CHAT,
    EHOP_MSG_ALERT,
    EHOP_MSG_FILE,
    EHOP_MSG_COUNT
} ehop_msg_class_t;

/* Band for a message class; EHOP_BAND_BULK for an unknown class. */
uint32_t ehop_band_for_class(uint32_t msg_class);

/* ===== channel configuration ===== */
typedef struct {
    uint8_t width;     /* word width in bytes: 2, 4 or 8 */
    uint8_t band;      /* EHOP_BAND_* */
    uint8_t am_mask;   /* AM: allowed depths, non-empty subset of the band's */
    uint8_t pm_mode;   /* PM: EHOP_PM_* */
    uint16_t fm_rate;  /* FM: mean words per flip, inside the band */
    uint16_t fm_hop;   /* FM hop deviation, < fm_rate */
    uint16_t pm_phase; /* PM base phase in words, < fm_rate */
    uint16_t pm_step;  /* PM step per epoch or frame, < fm_rate */
} ehop_cfg_t;

/* EHOP_OK when cfg is self-consistent and lies inside its band. */
int ehop_cfg_check(const ehop_cfg_t *cfg);

/* ===== the schedule =====
 * One frame's keyed byte-order pattern. ehop_apply() reads it and never
 * changes it, so the same schedule can be applied any number of times. */
typedef struct {
    uint8_t seed[32]; /* per-frame SHAKE256 output; secret */
    uint8_t width;
    uint8_t am_mask;
    uint8_t am_fixed; /* the depth when only one is allowed, else 0 */
    uint8_t start_on; /* first segment reordered? */
    uint16_t fm_rate, fm_hop;
    uint16_t first_len; /* words in the first segment (the PM result) */
} ehop_schedule_t;

/* Derive the schedule for frame (sender, seq) of channel (key, chan_id,
 * epoch, cfg). Deterministic: every node holding the same inputs derives the
 * same schedule. */
int ehop_schedule_init(ehop_schedule_t *s, const uint8_t key[EHOP_KEY_BYTES], const ehop_cfg_t *cfg,
                       uint32_t chan_id, uint32_t epoch, uint32_t sender, uint64_t seq);

/* Reorder buf in place. Involutive: applying twice restores buf. A trailing
 * partial word is reordered as a short word (still an involution). */
void ehop_apply(const ehop_schedule_t *s, uint8_t *buf, uint32_t len);

/* Wipe a schedule. */
void ehop_schedule_wipe(ehop_schedule_t *s);

/* ===== channels ===== */
typedef struct {
    uint32_t sender;
    uint64_t top;    /* highest sequence number accepted */
    uint64_t window; /* bit i: top - i seen */
    uint8_t used;
} ehop_replay_t;

typedef struct {
    ehop_cfg_t cfg;
    uint32_t chan_id;
    uint32_t epoch;
    uint32_t self_sender; /* this node's sender id: the nonce domain */
    uint64_t next_seq;    /* next sequence number this node seals with */
    uint8_t sched_key[EHOP_KEY_BYTES];
    uint8_t aead_key[EHOP_KEY_BYTES];
    uint8_t tag_key[EHOP_KEY_BYTES];
    ehop_replay_t replay[EHOP_REPLAY_SLOTS];
    uint8_t ready;
} ehop_channel_t;

/* Build a channel from a 32-byte secret (a pairwise ML-KEM shared secret, or
 * a key from ehop_net_channel). All three derived keys bind key, chan_id,
 * epoch and every field of cfg. self_sender must be unique among the nodes
 * that seal on this channel (the AEAD nonce is sender || seq). */
int ehop_channel_init(ehop_channel_t *ch, const uint8_t key[EHOP_KEY_BYTES], uint32_t chan_id,
                      uint32_t epoch, const ehop_cfg_t *cfg, uint32_t self_sender);
/* NONCE DISCIPLINE. The AEAD nonce is (self_sender, seq) under a key fixed
 * for (key, chan_id, epoch, cfg). Re-deriving the same channel in the same
 * epoch restarts next_seq at 0, so a node that does that (after a reboot, or
 * by calling ehop_net_channel twice) must restore its persisted counter with
 * ehop_channel_set_seq before sealing, and must seal on only one copy of a
 * channel (after ehop_router_add_channel, seal through the router only).
 * set_seq refuses to move the counter backwards. */
int ehop_channel_set_seq(ehop_channel_t *ch, uint64_t next_seq);
void ehop_channel_wipe(ehop_channel_t *ch);
void ehop_channel_copy(ehop_channel_t *dst, const ehop_channel_t *src);

/* Fast channel-identification tag over a 24-byte header:
 * first 8 bytes of SHA3-256(tag_key || "ZXV-EHOP-v1/tag" || header).
 * It changes with every header (seq is in it), so it is not a static label an
 * observer can follow, and without tag_key nobody can compute it. */
void ehop_channel_tag(const ehop_channel_t *ch, const uint8_t hdr[EHOP_HDR_BYTES],
                      uint8_t tag[EHOP_TAG_BYTES]);

/* Seal: schedule the plaintext, then AEAD-encrypt it. Frame layout:
 *   hdr[24] = "ZH" | ver | band | le32 epoch | le32 sender | le64 seq |
 *             le16 len | le16 0
 *   tag[8]  channel tag of hdr
 *   ct[len] ChaCha20 of the scheduled plaintext
 *   mac[16] Poly1305 over aad = hdr || tag and ct
 * Uses and advances ch->next_seq. */
int ehop_seal(ehop_channel_t *ch, const uint8_t *pt, uint32_t pt_len, uint8_t *frame, uint32_t cap,
              uint32_t *frame_len);

/* Open: check format, epoch and tag, verify the AEAD, reject replays, undo
 * the schedule. On any failure out is zeroed and nothing is recorded. */
int ehop_open(ehop_channel_t *ch, const uint8_t *frame, uint32_t frame_len, uint8_t *out,
              uint32_t cap, uint32_t *out_len);

/* Index of the channel in chans[0..n) whose tag (and band) matches the frame,
 * or -1. No decryption. */
int ehop_route(const ehop_channel_t *chans, uint32_t n, const uint8_t *frame, uint32_t frame_len);

/* QoS read straight off the clear header, no keys needed: band and priority
 * so a forwarding node can order settlement before bulk. */
int ehop_frame_qos(const uint8_t *frame, uint32_t frame_len, uint32_t *band, uint32_t *priority);

/* ===== private networks =====
 * An admin creates a network key; members join through an ML-KEM-768
 * encapsulated key share signed with ML-DSA-65 by the admin. Each epoch the
 * key ratchets forward through SHAKE256 and the old key is wiped. Removing a
 * member re-keys from fresh entropy and re-sends the key to the rest. */
#define EHOP_NET_ID_BYTES    16u
#define EHOP_NET_MAX_MEMBERS 8u
#define EHOP_MLDSA_PK_BYTES  1952u
#define EHOP_MLDSA_SK_BYTES  4032u
#define EHOP_MLDSA_SIG_BYTES 3309u
#define EHOP_WELCOME_BODY                                                                          \
    (4u + EHOP_NET_ID_BYTES + 4u + 4u + MLKEM768_CT_BYTES + EHOP_KEY_BYTES + 32u)
#define EHOP_WELCOME_BYTES (EHOP_WELCOME_BODY + EHOP_MLDSA_SIG_BYTES)

#define EHOP_WELCOME_INVITE 1u
#define EHOP_WELCOME_REKEY  2u

typedef struct {
    uint32_t member_id;
    uint8_t active;
    uint8_t ek[MLKEM768_EK_BYTES];
} ehop_member_t;

typedef struct {
    uint8_t net_id[EHOP_NET_ID_BYTES];
    uint32_t epoch;
    uint32_t self_id;
    uint8_t key[EHOP_KEY_BYTES];
    uint8_t is_admin;
    uint8_t has_key;
    uint8_t admin_pk[EHOP_MLDSA_PK_BYTES];
    uint8_t admin_sk[EHOP_MLDSA_SK_BYTES];       /* zero on members */
    ehop_member_t members[EHOP_NET_MAX_MEMBERS]; /* admin's roster */
} ehop_net_t;

/* Admin: create a network. sig_seed (32) makes the ML-DSA-65 key pair;
 * key_entropy (32) makes the epoch-0 key. Both must be fresh randomness. */
int ehop_net_create(ehop_net_t *net, const uint8_t net_id[EHOP_NET_ID_BYTES], uint32_t admin_id,
                    const uint8_t sig_seed[32], const uint8_t key_entropy[32]);

/* Member: prepare to join net_id run by the admin whose public key is
 * admin_pk (obtained out of band, e.g. from a signed invitation card). */
int ehop_net_member_init(ehop_net_t *net, const uint8_t net_id[EHOP_NET_ID_BYTES], uint32_t self_id,
                         const uint8_t admin_pk[EHOP_MLDSA_PK_BYTES]);

/* Admin: add member_id (with its ML-KEM-768 encapsulation key) and write a
 * signed welcome carrying the current key. m: 32 bytes fresh randomness for
 * the encapsulation; rnd: 32 bytes for hedged ML-DSA, or NULL. */
int ehop_net_invite(ehop_net_t *net, uint32_t member_id, const uint8_t ek[MLKEM768_EK_BYTES],
                    const uint8_t m[32], const uint8_t *rnd, uint8_t welcome[EHOP_WELCOME_BYTES]);

/* Member: verify, decapsulate with dk and install the key. Accepts an invite
 * when not yet joined, and only newer epochs after that (no rollback). */
int ehop_net_join(ehop_net_t *net, const uint8_t dk[MLKEM768_DK_BYTES],
                  const uint8_t welcome[EHOP_WELCOME_BYTES]);

/* Everyone: advance one epoch. key' = SHAKE256(tag || net_id || epoch' ||
 * key); the old key is wiped, so a later compromise cannot read old epochs. */
int ehop_net_rotate(ehop_net_t *net);

/* Admin: remove member_id, re-key from fresh entropy (epoch + 1) and write
 * one signed re-key welcome per remaining member into welcomes (room for
 * EHOP_NET_MAX_MEMBERS). *count receives how many were written. */
int ehop_net_remove(ehop_net_t *net, uint32_t member_id, const uint8_t entropy[32],
                    const uint8_t *rnd, uint8_t welcomes[][EHOP_WELCOME_BYTES], uint32_t *count);

/* Derive this network's channel chan_id for the current epoch. Call again
 * after every rotate/remove/join. */
int ehop_net_channel(const ehop_net_t *net, uint32_t chan_id, const ehop_cfg_t *cfg,
                     uint32_t self_sender, ehop_channel_t *ch);

void ehop_net_wipe(ehop_net_t *net);

/* ===== routing for a peer-to-peer overlay =====
 * Per-neighbour channel tables. There are no servers: a server that joins is
 * a peer with a larger capacity hint. */
#define EHOP_PEER_ID_BYTES 32u
#define EHOP_MAX_PEERS     16u
#define EHOP_PEER_CHANNELS 8u

typedef struct {
    uint8_t id[EHOP_PEER_ID_BYTES]; /* overlay node id (e.g. key hash) */
    uint32_t capacity;              /* relative capacity hint */
    uint32_t n;
    uint8_t used;
    ehop_channel_t chans[EHOP_PEER_CHANNELS];
} ehop_peer_t;

typedef struct {
    uint32_t n;
    ehop_peer_t peers[EHOP_MAX_PEERS];
} ehop_router_t;

void ehop_router_init(ehop_router_t *r);
/* Returns the peer index, or EHOP_EFULL; re-adding an id returns its slot. */
int ehop_router_add_peer(ehop_router_t *r, const uint8_t id[EHOP_PEER_ID_BYTES], uint32_t capacity);
int ehop_router_find_peer(const ehop_router_t *r, const uint8_t id[EHOP_PEER_ID_BYTES]);
/* Copies ch into the peer's table; returns its channel index. */
int ehop_router_add_channel(ehop_router_t *r, uint32_t peer, const ehop_channel_t *ch);
/* Pick the channel for a message class and size: a channel in the class's
 * band if the frame fits it; otherwise the nearest band of LOWER priority
 * that has a channel and fits (traffic is never promoted above its class).
 * Returns the channel index, or EHOP_EBAND. */
int ehop_router_pick(const ehop_router_t *r, uint32_t peer, uint32_t msg_class, uint32_t pt_len,
                     uint32_t *band);
/* Pick and seal in one call. */
int ehop_router_send(ehop_router_t *r, uint32_t peer, uint32_t msg_class, const uint8_t *pt,
                     uint32_t pt_len, uint8_t *frame, uint32_t cap, uint32_t *frame_len);
/* Route an inbound frame from peer by band + tag, then open it. *chan gets
 * the channel index. */
int ehop_router_recv(ehop_router_t *r, uint32_t peer, const uint8_t *frame, uint32_t frame_len,
                     uint8_t *out, uint32_t cap, uint32_t *out_len, uint32_t *chan);

/* ===== transport-agnostic hook =====
 * A transport (kernel/src/carracho or any other) holds one of these and calls
 * through it; it never needs to know about schedules or keys. */
typedef struct {
    void *ctx;
    int (*seal)(void *ctx, uint32_t peer, uint32_t msg_class, const uint8_t *pt, uint32_t pt_len,
                uint8_t *frame, uint32_t cap, uint32_t *frame_len);
    int (*open)(void *ctx, uint32_t peer, const uint8_t *frame, uint32_t frame_len, uint8_t *pt,
                uint32_t cap, uint32_t *pt_len, uint32_t *chan);
    int (*qos)(const uint8_t *frame, uint32_t frame_len, uint32_t *band, uint32_t *priority);
} ehop_hook_t;

/* Fill hook so that its calls go to router r. */
void ehop_router_hook(ehop_router_t *r, ehop_hook_t *hook);

#endif /* ZXV_EHOP_H */
