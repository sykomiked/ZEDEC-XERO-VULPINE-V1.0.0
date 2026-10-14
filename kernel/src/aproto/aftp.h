/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* aftp.h — AFTP, the peer-to-peer AI twin of FTP (docs/P2P_PROTOCOL_TWINS.md 4.2).
 *
 * LISTINGS (LIST / MLSD)
 *   D1  A listing verifies only if publisher == SHA3-256(pk), the ML-DSA-87
 *       signature verifies under "aproto/v1/aftp-dir", now < expires and
 *       created <= now + AFTP_SKEW_MS.
 *   D2  Paths are relative, '/'-separated, with no empty, "." or ".." part, no
 *       backslash, no control byte (< 0x20 or 0x7f), at most AFTP_NAME_MAX
 *       bytes, and valid UTF-8 (no overlong forms, surrogates or values above
 *       U+10FFFF). A listing cannot name a place outside the receiver's folder.
 *   D3  Paths are in strictly increasing byte order (unique, canonical).
 *   D4  Every entry's size and chunk size give a valid chunk count.
 *   D5  Per (publisher, share) the receiver keeps the highest seq it accepted
 *       and that listing's digest. Older seq: rollback, F. Same seq and
 *       digest: duplicate. Same seq, other digest: equivocation, B; the first
 *       listing is kept and the share stays marked.
 *
 * TRANSFERS (RETR / REST / FXP)
 *   G1  A chunk is asked of one source at a time; only that source's answer
 *       for that index is accepted, anything else is unsolicited.
 *   G2  A chunk counts only once its Merkle path proves it into the file root
 *       (vna_file.h F3-F5).
 *   G3  A source that sends one chunk failing the proof (or a malformed
 *       chunk) is dropped for the rest of the transfer.
 *   G4  A request unanswered after the timeout goes to another source; the
 *       slow source is not dropped.
 *   G5  Every verified chunk, refused chunk, dropped source and the finished
 *       file is logged (ap_log.h) with the peer concerned.
 *
 * OFFERS (STOR / APPE): a push is a pull the receiver consents to.
 *   O1  An offer verifies only if `to` is this node, from == SHA3-256(pk),
 *       the signature verifies under "aproto/v1/aftp-offer", |time - now| <=
 *       AFTP_SKEW_MS and (from, nonce) is new.
 *   O2  A verified offer waits in the inbox as N. Accepting it starts a get
 *       with the sender as first source; declining it logs F. Nothing is
 *       fetched, opened or run because an offer arrived; the note is data.
 *
 * SERVING
 *   S1  The node's Vinea agreement is checked first (vna_file_serve). Every
 *       served chunk and every refusal is logged; if the log refuses the
 *       entry the chunk is not sent ("no audit, no action").
 *
 * Records ride inside Vinea sessions (vna_sess_seal) with the AFTP_REC_*
 * record types. Like src/vinea this is a pure state machine: no sockets, no
 * clock, no entropy, no allocation, integer only.
 */
#ifndef ZXV_AFTP_H
#define ZXV_AFTP_H

#include "ap_log.h"
#include "../vinea/vna_file.h"
#include "../vinea/vna_session.h"

#define AFTP_NAME_MAX 128u
#define AFTP_DIR_MAX  32u
#define AFTP_NOTE_MAX 256u
#define AFTP_SOURCES  8u
#define AFTP_WINDOW   16u
#define AFTP_KNOWN    32u
#define AFTP_NONCES   64u
#define AFTP_INBOX    8u
#define AFTP_SKEW_MS  300000u

#define AFTP_DIR_MAGIC    0x31445441u /* "ATD1" */
#define AFTP_DIRREQ_MAGIC 0x31515441u /* "ATQ1" */
#define AFTP_OFFER_MAGIC  0x314F5441u /* "ATO1" */
#define AFTP_CTX_DIR      "aproto/v1/aftp-dir"
#define AFTP_CTX_OFFER    "aproto/v1/aftp-offer"

/* Session record types (vna_sess_seal `type`). */
enum {
    AFTP_REC_DIR_REQ = 0x21,
    AFTP_REC_DIR = 0x22,
    AFTP_REC_OFFER = 0x23,
    AFTP_REC_CHUNK_REQ = 0x24,
    AFTP_REC_CHUNK = 0x25
};

/* Entry kinds: labels for the interface, never permission to run. */
enum {
    AFTP_KIND_DATA = 0,
    AFTP_KIND_TEXT = 1,
    AFTP_KIND_MEDIA = 2,
    AFTP_KIND_MODEL = 3,
    AFTP_KIND_CODE = 4,
    AFTP_KIND_LISTING = 5, /* the file is another signed listing */
    AFTP_KIND_MAX = 5
};

/* Log events (ap_entry_t.event, twin AP_TWIN_AFTP). */
enum {
    AFTP_EV_DIR_OUT = 1,
    AFTP_EV_DIR_IN = 2,
    AFTP_EV_OFFER_OUT = 3,
    AFTP_EV_OFFER_IN = 4,
    AFTP_EV_OFFER_TAKE = 5,
    AFTP_EV_OFFER_DECLINE = 6,
    AFTP_EV_GET_START = 7,
    AFTP_EV_CHUNK_IN = 8,
    AFTP_EV_SOURCE_DROP = 9,
    AFTP_EV_TIMEOUT = 10,
    AFTP_EV_GET_DONE = 11,
    AFTP_EV_SERVE = 12
};

typedef struct {
    uint16_t name_len;
    uint8_t name[AFTP_NAME_MAX];
    vna_id_t root;
    uint64_t size;
    uint32_t chunk_size;
    uint8_t kind;
    uint8_t tag; /* the publisher's own claim about the entry */
} aftp_entry_t;

typedef struct {
    uint32_t magic;
    vna_id_t publisher;
    vna_id_t share; /* which of the publisher's folders */
    uint64_t seq;
    uint64_t created;
    uint64_t expires;
    uint8_t tag;
    uint16_t n;
    aftp_entry_t e[AFTP_DIR_MAX];
    uint8_t sig[VNA_SIG_LEN];
} aftp_dir_t;

typedef struct {
    uint32_t magic;
    vna_id_t publisher;
    vna_id_t share;
    uint64_t have_seq; /* answer only with a newer listing */
} aftp_dir_req_t;

typedef struct {
    uint32_t magic;
    vna_id_t from;
    vna_id_t to;
    uint64_t nonce;
    uint64_t time;
    uint8_t tag;
    uint16_t name_len;
    uint8_t name[AFTP_NAME_MAX];
    vna_id_t root;
    uint64_t size;
    uint32_t chunk_size;
    uint8_t kind;
    uint16_t note_len;
    uint8_t note[AFTP_NOTE_MAX];
    uint8_t sig[VNA_SIG_LEN];
} aftp_offer_t;

extern const vna_schema_t aftp_entry_schema, aftp_dir_schema, aftp_dir_req_schema,
    aftp_offer_schema;

typedef struct {
    bool used;
    bool equivocated;
    vna_id_t publisher, share;
    uint64_t seq;
    uint8_t digest[32];
} aftp_known_t;

typedef struct {
    bool used;
    vna_id_t from;
    uint64_t nonce;
    uint64_t time;
} aftp_nonce_t;

typedef struct {
    bool used;
    vna_id_t from;
    uint64_t nonce;
    uint64_t time;
    aftp_entry_t file;
    uint16_t note_len;
    uint8_t note[AFTP_NOTE_MAX];
} aftp_pending_t;

typedef struct {
    const vna_identity_t *self;
    ap_log_t *log;
    aftp_known_t known[AFTP_KNOWN];
    aftp_nonce_t nonces[AFTP_NONCES];
    aftp_pending_t inbox[AFTP_INBOX];
    uint32_t dirs_in, dirs_refused, equivocations, offers_in, offers_refused, served, serve_refused;
} aftp_t;

/* `log` must be the self node's log. */
vna_status_t aftp_init(aftp_t *a, const vna_identity_t *self, ap_log_t *log);

/* D2 for one path. */
bool aftp_path_ok(const uint8_t *p, uint32_t len);
/* D2-D4 for a decoded listing. */
vna_status_t aftp_dir_check(const aftp_dir_t *d);

/* Publisher: sign a listing (sets d->magic and d->publisher). Returns its
 * length or a negative vna_status_t. Logged as DIR_OUT. */
int32_t aftp_dir_sign(aftp_t *a, aftp_dir_t *d, const uint8_t rnd[32], uint64_t now, uint8_t *out,
                      uint32_t cap);
/* D1-D4, no state. */
vna_status_t aftp_dir_open(const uint8_t *in, uint32_t len, const uint8_t pk[VNA_PK_LEN],
                           uint64_t now, aftp_dir_t *out);
/* D1-D5 with state, logged as DIR_IN. *tag gets the outcome. Returns VNA_OK
 * (newer listing, T), VNA_ERR_DUP (same listing again, T), VNA_ERR_REPLAY
 * (rollback, F), VNA_ERR_FORK (equivocation, B), VNA_ERR_SPACE (table or log
 * full, N) or the D1-D4 error (F). `out` holds the listing on VNA_OK / DUP. */
vna_status_t aftp_dir_learn(aftp_t *a, const uint8_t *in, uint32_t len,
                            const uint8_t pk[VNA_PK_LEN], uint64_t now, aftp_dir_t *out,
                            uint8_t *tag);
/* Is this (publisher, share) marked as having equivocated? */
bool aftp_dir_equivocated(const aftp_t *a, const vna_id_t *publisher, const vna_id_t *share);

int32_t aftp_dir_req_encode(const vna_id_t *publisher, const vna_id_t *share, uint64_t have_seq,
                            uint8_t *out, uint32_t cap);
vna_status_t aftp_dir_req_decode(const uint8_t *in, uint32_t len, aftp_dir_req_t *out);

/* ---- offers ---- */
/* Sender: sign an offer of `file` to `to`. Logged as OFFER_OUT. */
int32_t aftp_offer_make(aftp_t *a, const vna_id_t *to, const aftp_entry_t *file,
                        const uint8_t *note, uint16_t note_len, uint64_t nonce,
                        const uint8_t rnd[32], uint64_t now, uint8_t *out, uint32_t cap);
/* Receiver: O1, then into the inbox as N (O2). *slot gets its index. */
vna_status_t aftp_offer_receive(aftp_t *a, const uint8_t *in, uint32_t len,
                                const uint8_t pk[VNA_PK_LEN], uint64_t now, int32_t *slot);
vna_status_t aftp_offer_decline(aftp_t *a, uint32_t slot, uint64_t now);

/* ---- multi-source get ---- */
typedef struct {
    vna_id_t id;
    uint32_t good, timeouts, inflight;
    bool dropped;
} aftp_src_t;

typedef struct {
    bool used;
    uint8_t src;
    uint32_t index;
    uint64_t sent;
} aftp_slot_t;

typedef struct {
    aftp_t *a;
    vna_fetch_t f;
    aftp_src_t src[AFTP_SOURCES];
    uint32_t nsrc;
    aftp_slot_t win[AFTP_WINDOW];
    uint32_t window;
    uint64_t timeout;
    uint32_t cursor;
    uint32_t rr;
    uint32_t refused;
    bool done;
} aftp_get_t;

/* Start a get. bitmap: (n + 7) / 8 bytes; with resume it already holds the
 * chunks this host verified and stored earlier (REST), else it is cleared.
 * window: requests in flight at once (1..AFTP_WINDOW). Logged as GET_START. */
vna_status_t aftp_get_start(aftp_t *a, aftp_get_t *g, const vna_id_t *root, uint64_t size,
                            uint32_t chunk_size, uint8_t *bitmap, uint32_t bitmap_len, bool resume,
                            uint32_t window, uint64_t timeout, uint64_t now);
/* O2: accept an inbox offer as a get from its sender (logged OFFER_TAKE). */
vna_status_t aftp_offer_accept(aftp_t *a, uint32_t slot, aftp_get_t *g, uint8_t *bitmap,
                               uint32_t bitmap_len, uint32_t window, uint64_t timeout,
                               uint64_t now);
vna_status_t aftp_get_add_source(aftp_get_t *g, const vna_id_t *id);

/* The next request to send: returns its length (to: whom), 0 when nothing can
 * go now (window full, everything asked, or done), VNA_ERR_STATE when every
 * source is dropped. Times out stale requests first (G4). */
int32_t aftp_get_next(aftp_get_t *g, uint64_t now, vna_id_t *to, uint8_t *out, uint32_t cap);

/* A CHUNK record from `from` (G1-G5). VNA_OK: c->data[0..c->data_len) is the
 * verified chunk c->index, for the host to store. Errors: VNA_ERR_UNEXPECTED
 * (not asked of this source), VNA_ERR_DUP, VNA_ERR_PARSE / VNA_ERR_MERKLE
 * (source dropped), VNA_ERR_SPACE (the log refused; the chunk is not
 * counted and stays asked), VNA_ERR_STATE (finished). */
vna_status_t aftp_get_on_chunk(aftp_get_t *g, const vna_id_t *from, const uint8_t *in, uint32_t len,
                               uint64_t now, vna_chunk_t *c);
bool aftp_get_done(const aftp_get_t *g);

/* ---- serving (S1) ---- */
int32_t aftp_serve(aftp_t *a, const vna_shared_file_t *sf, const vna_agreement_t *agr,
                   vna_usage_t *us, const vna_id_t *peer, uint64_t trust, const uint8_t *req,
                   uint32_t req_len, uint64_t now, vna_chunk_t *scratch, uint8_t *out,
                   uint32_t cap);

#endif /* ZXV_AFTP_H */
