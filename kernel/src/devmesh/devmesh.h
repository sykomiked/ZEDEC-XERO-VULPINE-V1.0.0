/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* devmesh.h — the user's own private network of devices.
 *
 * WHAT IT IS
 * ----------
 * One person's devices (a computer left on at home, a phone, a tablet) form
 * a small mesh with no server in the middle. Each device has its own
 * post-quantum identity. A signed roster says which devices belong to the
 * mesh, what role each has, and which have been revoked. Paired devices open
 * mutually authenticated, encrypted sessions to each other over whatever
 * transport the host supplies (LAN, Carracho DHT, an EHOP private network, a
 * relay the user runs), and use them to:
 *   - send assistant prompts, file actions and wallet actions from the phone
 *     to the home node and stream the replies back (dm_remote.h);
 *   - advertise what each device can run (RAM, compute, battery, metered or
 *     unmetered network, which models) and route each request to the best
 *     reachable device, falling back to a small on-device model when the
 *     home node is unreachable (dm_remote.h);
 *   - keep settings in sync with last-writer-wins per field under per-field
 *     vector clocks (dm_sync.h);
 *   - confirm every money action on a device the user holds (dm_remote.h).
 *
 * ROLES
 *   DM_ROLE_HOME        always-on computer; runs the large models, holds files.
 *   DM_ROLE_THIN        phone used as a client of the home node.
 *   DM_ROLE_STANDALONE  phone with no home node, running small models alone.
 * A phone-only user is a mesh of one STANDALONE device. Adding a home node
 * later is a pairing like any other.
 *
 * CRYPTOGRAPHY (all from kernel/src/pqsec/pq_matrix.h, nothing new here)
 *   identity   a pq_matrix dual-signature key (ML-DSA-87 at HIGH, ML-DSA-87 +
 *              SLH-DSA-SHAKE-256s at MATRIX) and a pq_matrix hybrid KEM key
 *              (ML-KEM-1024 + X25519 at HIGH, + HQC-5 at MATRIX).
 *   dev id     the first 16 bytes of keyhash = SHA3-256(sig pk enc || kem pk enc).
 *   handshake  three messages. M1: initiator's ephemeral KEM key, signed.
 *              M2: responder encapsulates to that ephemeral key AND to the
 *              initiator's static KEM key, signed over M1 || M2. M3: key
 *              confirmation (AEAD of the transcript hash). Session keys =
 *              SHAKE256(ss_ephemeral || ss_static || [pairing key] || transcript).
 *              The ephemeral key gives forward secrecy; the static
 *              encapsulation and both signatures give mutual authentication.
 *   records    ChaCha20-Poly1305 (kernel/src/tls/aead.c), one key per
 *              direction, strictly increasing 64-bit sequence numbers.
 * The default level is PQM_LEVEL_HIGH: every component is NIST category 5,
 * and ML-DSA-87 signs in milliseconds on a phone. PQM_LEVEL_MATRIX adds the
 * hash-based SLH-DSA-256s and code-based HQC-5 layers; it is supported but an
 * SLH-DSA-256s signature takes seconds on a phone and is ~29 KB, and it is
 * paid on every session handshake and roster change.
 *
 * PAIRING
 *   The existing device (an admin) makes an invitation:
 *     QR code     mesh id, inviter dev id, inviter keyhash, 32-byte one-time
 *                 secret, optional address hint. The joiner checks the
 *                 inviter's keys against the keyhash.
 *     short code  10 characters of Crockford base32 (50 bits) shown on the
 *                 inviter, typed on the joiner. It carries no keyhash.
 *   The secret keys a MAC on M1 and M2 and is mixed into the session keys.
 *   Both screens then show a 6-digit short authentication string (SAS)
 *   derived from the session; the user confirms they match on both. A
 *   man in the middle has two different sessions and therefore (except with
 *   probability 1e-6) two different SAS values. The SAS check is what makes
 *   the 50-bit short code safe: an attacker who brute-forces a recorded code
 *   still cannot make the two numbers agree. Wrong codes are counted; after
 *   DM_PAIR_TRIES failures the invitation is dead.
 *   After both confirmations the inviter adds the joiner to the roster,
 *   signs it and sends it together with the keys of the other devices.
 *
 * ROSTER RULES (dm_roster_accept)
 *   - version strictly increases; the mesh id matches;
 *   - the signer is an ACTIVE ADMIN of the roster the receiver already has
 *     (a revoked device cannot sign a roster that brings itself back);
 *   - no device is ever dropped or has its keyhash changed (revocations are
 *     tombstones and travel with every later roster);
 *   - a REVOKED device can never become ACTIVE again (re-pairing gives a new
 *     device identity);
 *   - at least one active admin remains.
 *   Sessions only open between devices that are ACTIVE in the receiver's
 *   roster, so a revoked device is locked out of every new session; its
 *   current sessions are torn down as soon as the revocation arrives. A
 *   device that finds itself revoked forgets the mesh.
 *
 * TRANSPORT
 *   dm_host_t.send(ctx, to_dev_id, frame, len) hands an opaque frame to the
 *   host. to_dev_id is all zero for the first pairing message sent with a
 *   short code (the host delivers it to whichever local device shows that
 *   code, e.g. by LAN broadcast with dm_code_tag()). Incoming frames go to
 *   dm_receive(). Frames are self-protecting, so any carrier works: a
 *   Carracho CARR_MSG_HK payload, an EHOP frame (dm_session_export_key()
 *   gives a 32-byte key suitable for ehop_channel_init()), raw UDP on a LAN.
 *   Handshake frames are up to DM_FRAME_MAX bytes and carriers with smaller
 *   frames must fragment them; record frames are at most DM_REC_FRAME_MAX.
 *
 * WHAT IT DOES NOT DO
 *   - It has no randomness source and no clock: host.random() must be a
 *     CSPRNG, and every call takes the time in milliseconds.
 *   - It does not discover devices or traverse NAT; the host does.
 *   - It does not hide traffic metadata (who talks to whom, when, how much).
 *   - Secret keys live in the dm_mesh_t; protecting that memory (Android
 *     Keystore / iOS Secure Enclave wrapping of the seed) is the host's job.
 *   - It does not run models; it routes requests to the device that can.
 *
 * Freestanding integer C11: no libc, no float, no allocation, no 64-bit
 * division, fixed buffers. dm_mesh_t is large (about 0.3 MB at MATRIX);
 * keep it in static storage, never on a stack.
 */
#ifndef ZXV_DEVMESH_H
#define ZXV_DEVMESH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../pqsec/pq_matrix.h"

/* ===== limits ===== */
#define DM_ID_BYTES      16u
#define DM_HASH_BYTES    32u
#define DM_NAME_MAX      32u /* bytes, NUL-padded */
#define DM_MAX_DEVICES   8u
#define DM_PAIR_TRIES    3u
#define DM_INVITE_TTL_MS (10u * 60u * 1000u)
#define DM_ADDR_MAX      64u
#define DM_CODE_CHARS    10u /* short code, Crockford base32 */
#define DM_SAS_MOD       1000000u

#define DM_ROSTER_MAX    36864u /* encoded roster incl. a MATRIX signature */
#define DM_REC_MAX       (DM_ROSTER_MAX + 64u)
#define DM_HDR_BYTES     36u /* 'D' 'M' ver type | mesh id | sender id */
#define DM_REC_FRAME_MAX (DM_HDR_BYTES + 8u + 16u + DM_REC_MAX)
#define DM_FRAME_MAX     98304u

#define DM_LIVE_MS 15000u /* peer considered reachable if heard within */
#define DM_PING_MS 5000u

/* ===== status codes ===== */
typedef enum {
    DM_OK = 0,
    DM_ERR_ARG = -1,
    DM_ERR_SIZE = -2,
    DM_ERR_FORMAT = -3,
    DM_ERR_AUTH = -4,     /* signature, MAC or AEAD failure */
    DM_ERR_UNKNOWN = -5,  /* device not in roster or keys not known */
    DM_ERR_REVOKED = -6,  /* device revoked */
    DM_ERR_STATE = -7,    /* message not expected now */
    DM_ERR_FULL = -8,     /* table full */
    DM_ERR_REPLAY = -9,   /* sequence number or roster version not newer */
    DM_ERR_PERM = -10,    /* not allowed (not admin, not held, last admin) */
    DM_ERR_EXPIRED = -11, /* invitation or confirmation expired */
    DM_ERR_NOPEER = -12,  /* no session with that device */
    DM_ERR_CRYPTO = -13,  /* key generation / encapsulation failed */
    DM_ERR_TRANSPORT = -14
} dm_status_t;

/* ===== roles and flags ===== */
typedef enum {
    DM_ROLE_HOME = 1,
    DM_ROLE_THIN = 2,
    DM_ROLE_STANDALONE = 3,
} dm_role_t;

#define DM_FLAG_ADMIN 0x01u /* may sign rosters (add, rename, revoke) */
#define DM_FLAG_HELD  0x02u /* a device the user carries: may confirm money */

#define DM_DEV_ACTIVE  1u
#define DM_DEV_REVOKED 2u

typedef struct {
    uint8_t id[DM_ID_BYTES];
    uint8_t keyhash[DM_HASH_BYTES];
    char name[DM_NAME_MAX];
    uint8_t role;   /* dm_role_t */
    uint8_t flags;  /* DM_FLAG_* */
    uint8_t status; /* DM_DEV_* */
    uint64_t since; /* roster version that last changed this entry */
} dm_dev_t;

typedef struct {
    uint64_t version;
    uint8_t mesh_id[DM_ID_BYTES];
    uint8_t n;
    dm_dev_t dev[DM_MAX_DEVICES];
    uint8_t signer[DM_ID_BYTES];
    uint32_t sig_len;
    uint8_t sig[PQM_SIG_MAX_BYTES]; /* pqm_sig_encode() of the signature */
} dm_roster_t;

/* ===== capabilities (exchanged after every session opens) ===== */
#define DM_NET_OFFLINE   0u
#define DM_NET_LAN       1u /* local network only */
#define DM_NET_UNMETERED 2u
#define DM_NET_METERED   3u /* cellular / capped: avoid bulk */

#define DM_FEAT_ASSIST_SMALL 0x0001u /* runs a small local model */
#define DM_FEAT_ASSIST_LARGE 0x0002u /* runs a large model */
#define DM_FEAT_FILES        0x0004u /* holds the user's file store */
#define DM_FEAT_WALLET       0x0008u /* holds wallet keys */
#define DM_FEAT_CALLS        0x0010u
#define DM_FEAT_STREAM       0x0020u
#define DM_FEAT_SPEECH       0x0040u
#define DM_FEAT_TRANSLATE    0x0080u
#define DM_FEAT_MARKET       0x0100u /* can buy capacity on the network */

#define DM_MODEL_SMALL 1u
#define DM_MODEL_LARGE 2u
#define DM_MAX_MODELS  6u
#define DM_MODEL_NAME  40u

typedef struct {
    char name[DM_MODEL_NAME]; /* e.g. "qwen2.5-1.5b-instruct-q4_k_m" */
    uint32_t params_m;        /* parameters, millions */
    uint32_t file_mb;         /* GGUF file size */
    uint32_t ram_mb;          /* resident estimate at 4096 context */
    uint8_t quant_bits;
    uint8_t cls;             /* DM_MODEL_SMALL / DM_MODEL_LARGE */
    uint8_t license_apache2; /* 1 if Apache-2.0 */
} dm_model_t;

typedef struct {
    uint32_t ram_mb;
    uint32_t free_ram_mb;
    uint32_t compute;    /* relative integer score: int8 GOPS, roughly */
    uint8_t battery_pct; /* 0..100, 255 = mains powered */
    uint8_t charging;
    uint8_t net; /* DM_NET_* */
    uint8_t nmodels;
    uint32_t features; /* DM_FEAT_* */
    dm_model_t models[DM_MAX_MODELS];
} dm_caps_t;

/* ===== events reported to the host ===== */
typedef enum {
    DM_EV_SAS = 1,          /* pairing: show ev.sas on screen, ask the user */
    DM_EV_PAIRED = 2,       /* this device joined / a device was added */
    DM_EV_ROSTER = 3,       /* roster changed (ev.u64 = version) */
    DM_EV_REVOKED = 4,      /* a device was revoked (ev.peer) */
    DM_EV_SELF_REVOKED = 5, /* this device was revoked: mesh forgotten */
    DM_EV_PEER_UP = 6,
    DM_EV_PEER_DOWN = 7,
    DM_EV_CAPS = 8,          /* a peer's capabilities arrived */
    DM_EV_SYNC = 9,          /* settings merged (ev.u64 = fields changed) */
    DM_EV_REQ_FALLBACK = 10, /* request ev.u64 timed out: run it locally */
    DM_EV_PAIR_FAILED = 11,
    DM_EV_MONEY_CONFIRMED = 12, /* a held device approved money action */
    DM_EV_MONEY_DECLINED = 13,
} dm_event_kind_t;

typedef struct {
    uint8_t kind; /* dm_event_kind_t */
    uint8_t peer[DM_ID_BYTES];
    uint32_t sas;
    uint64_t u64;
    int32_t status;
} dm_event_t;

/* ===== money actions (confirmed on a held device) ===== */
#define DM_RAIL_DEBIT  555u /* = PAY_RAIL_DEBIT_CODE  (kernel/src/pay/pay_ledger.h) */
#define DM_RAIL_CREDIT 777u /* = PAY_RAIL_CREDIT_CODE */
#define DM_RAIL_EQUITY 888u /* = PAY_RAIL_EQUITY_CODE */

#define DM_MONEY_PAY     1u
#define DM_MONEY_ESCROW  2u /* lock funds for a capacity-market contract */
#define DM_MONEY_RELEASE 3u
#define DM_MONEY_SIGN    4u /* sign a ledger posting */
#define DM_MONEY_MEMO    48u
#define DM_MONEY_SEEN    16u /* replay ring */

typedef struct {
    uint8_t action_id[DM_ID_BYTES]; /* unique per action (host random) */
    uint8_t kind;                   /* DM_MONEY_* */
    uint16_t rail;                  /* 555 / 777 / 888 */
    char currency[4];               /* "VFV" */
    uint64_t amount;                /* VFV minor units */
    uint8_t payee[DM_HASH_BYTES];   /* account / provider id */
    char memo[DM_MONEY_MEMO];
    uint8_t origin[DM_ID_BYTES]; /* device that asked */
    uint64_t expires_ms;
} dm_money_t;

typedef struct {
    uint8_t digest[DM_HASH_BYTES]; /* SHA3-256 of dm_money_encode() */
    uint8_t confirmer[DM_ID_BYTES];
    uint8_t approve;
    uint32_t sig_len;
    uint8_t sig[PQM_SIG_MAX_BYTES];
} dm_confirm_t;

/* ===== host interface ===== */
typedef struct {
    void *ctx;
    /* Fill out[0..n) with CSPRNG output. Required. */
    void (*random)(void *ctx, uint8_t *out, uint32_t n);
    /* Deliver a frame. Return 0 on success. Required. */
    int (*send)(void *ctx, const uint8_t to[DM_ID_BYTES], const uint8_t *frame, uint32_t len);
    void (*on_event)(void *ctx, const dm_event_t *ev);
    /* Home node side: a request arrived; answer with dm_reply(). */
    void (*on_request)(void *ctx, const uint8_t from[DM_ID_BYTES], uint32_t req_id, uint8_t kind,
                       const uint8_t *payload, uint32_t len);
    /* Client side: a reply chunk arrived. */
    void (*on_reply)(void *ctx, const uint8_t from[DM_ID_BYTES], uint32_t req_id,
                     const uint8_t *data, uint32_t len, bool final);
    /* Held device: show this money action to the user, then call
     * dm_money_answer(). */
    void (*on_money)(void *ctx, const uint8_t from[DM_ID_BYTES], const dm_money_t *m);
    /* A held device answered a money action this device asked about. */
    void (*on_money_answer)(void *ctx, const dm_money_t *m, const dm_confirm_t *c, int status);
} dm_host_t;

/* ===== settings (dm_sync.h) ===== */
#define DM_SET_MAX   32u
#define DM_SET_VALUE 64u

typedef struct {
    uint8_t id[DM_ID_BYTES];
    uint32_t ctr;
} dm_vc_ent_t;

typedef struct {
    uint16_t key; /* 0 = unused slot */
    uint8_t len;
    uint8_t value[DM_SET_VALUE];
    uint64_t ts; /* writer's clock at the write (tie-break only) */
    uint8_t writer[DM_ID_BYTES];
    uint8_t nvc;
    dm_vc_ent_t vc[DM_MAX_DEVICES];
} dm_field_t;

typedef struct {
    dm_field_t f[DM_SET_MAX];
    uint32_t conflicts; /* concurrent writes resolved by last-writer-wins */
} dm_settings_t;

/* ===== internal state (opaque to hosts; exposed for static sizing) ===== */
#define DM_SESS_NONE     0u
#define DM_SESS_M1_SENT  1u
#define DM_SESS_M2_SENT  2u
#define DM_SESS_PAIR_SAS 3u /* keys agreed, waiting for the user's SAS check */
#define DM_SESS_UP       4u

#define DM_MODE_SESSION 1u
#define DM_MODE_PAIR    2u

typedef struct {
    uint8_t state, mode, initiator, sas_ok, peer_sas_ok;
    uint8_t th[DM_HASH_BYTES];
    uint8_t k_tx[32], k_rx[32], k_ext[32];
    uint32_t sas;
    uint64_t tx_seq, rx_seq;
    uint64_t last_rx_ms, last_ping_ms;
} dm_sess_t;

typedef struct {
    uint8_t used, have_keys, have_caps, paired_role, paired_flags;
    uint8_t id[DM_ID_BYTES];
    uint8_t keyhash[DM_HASH_BYTES];
    char name[DM_NAME_MAX];
    pqm_sig_pk_t sig_pk;
    pqm_kem_pk_t kem_pk;
    dm_caps_t caps;
    dm_sess_t s;    /* the live session */
    dm_sess_t pend; /* responder: handshake not yet confirmed by M3 */
} dm_peer_t;

#define DM_MAX_REQS 16u
typedef struct {
    uint8_t used, kind, fell_back;
    uint32_t req_id;
    uint8_t target[DM_ID_BYTES];
    uint64_t sent_ms, last_ms;
} dm_req_t;

typedef struct {
    dm_host_t host;
    pqm_level_t level;
    uint8_t joined;
    uint8_t mesh_id[DM_ID_BYTES];
    /* identity */
    uint8_t self_id[DM_ID_BYTES];
    uint8_t self_kh[DM_HASH_BYTES];
    char self_name[DM_NAME_MAX];
    uint8_t self_role, self_flags;
    pqm_sig_pk_t sig_pk;
    pqm_sig_sk_t sig_sk;
    pqm_kem_pk_t kem_pk;
    pqm_kem_sk_t kem_sk;
    dm_caps_t caps;
    dm_roster_t roster;
    dm_peer_t peers[DM_MAX_DEVICES];
    /* inviter side */
    uint8_t inv_active, inv_tries, inv_flags, inv_role;
    uint8_t inv_qr[32];   /* QR secret */
    uint8_t inv_code[32]; /* secret derived from the short code */
    uint64_t inv_expires;
    /* joiner side */
    uint8_t join_active, join_have_kh;
    uint8_t join_kh[DM_HASH_BYTES];
    uint8_t join_secret[32];
    uint8_t join_peer[DM_ID_BYTES];
    /* handshake scratch (one initiator handshake at a time) */
    uint8_t hs_active, hs_mode;
    uint8_t hs_peer[DM_ID_BYTES];
    uint64_t hs_started;
    uint8_t hs_th1[DM_HASH_BYTES];
    pqm_kem_pk_t eph_pk;
    pqm_kem_sk_t eph_sk;
    pqm_kem_pk_t tmp_kem_pk;
    pqm_kem_pk_t peer_eph;
    pqm_sig_pk_t tmp_sig_pk;
    pqm_kem_ct_t ct_a, ct_b;
    pqm_sig_t sig;
    dm_roster_t roster_tmp;
    uint8_t tx[DM_FRAME_MAX];
    uint8_t rx[DM_REC_MAX];
    uint8_t app_buf[DM_REC_MAX];
    uint8_t kh_buf[PQM_SIG_PK_MAX_BYTES + PQM_KEM_PK_MAX_BYTES];
    /* apps */
    dm_settings_t settings;
    dm_req_t reqs[DM_MAX_REQS];
    uint32_t next_req;
    uint8_t money_seen[DM_MONEY_SEEN][DM_ID_BYTES];
    uint32_t money_seen_n;
    dm_money_t money_pending; /* held side: last money action shown */
    uint8_t money_pending_from[DM_ID_BYTES];
    uint8_t money_pending_used;
    dm_money_t money_asked; /* asking side: last action sent for confirmation */
    uint8_t money_asked_used;
    dm_confirm_t conf_tmp;
} dm_mesh_t;

/* ===== identity and mesh lifecycle ===== */

/* Create this device's identity from 64 seed bytes (32 for the signature
 * key, 32 for the KEM key; both from a CSPRNG, kept by the host in secure
 * storage so the identity survives restarts). Does not join a mesh. */
dm_status_t dm_init(dm_mesh_t *m, const dm_host_t *host, pqm_level_t level, const uint8_t seed[64],
                    const char *name, dm_role_t role, uint8_t flags);

/* Start a new mesh with this device as its first (admin) member. */
dm_status_t dm_create(dm_mesh_t *m, uint64_t now_ms);

/* Wipe every secret in m. */
void dm_wipe(dm_mesh_t *m);

const uint8_t *dm_self_id(const dm_mesh_t *m);
const dm_roster_t *dm_roster(const dm_mesh_t *m);
const dm_dev_t *dm_roster_find(const dm_roster_t *r, const uint8_t id[DM_ID_BYTES]);
bool dm_is_admin(const dm_mesh_t *m);

/* ===== pairing ===== */

/* Inviter (an admin): start an invitation. new_flags/new_role are what the
 * new device will get. Writes the QR payload (binary, *qr_len bytes; see
 * dm_b32_encode for a text form) and the short code (DM_CODE_CHARS + NUL).
 * addr: optional transport address hint (NULL / 0). */
#define DM_QR_MAX (5u + DM_ID_BYTES * 2u + DM_HASH_BYTES + 32u + 1u + DM_ADDR_MAX)
dm_status_t dm_invite(dm_mesh_t *m, uint8_t new_role, uint8_t new_flags, const uint8_t *addr,
                      uint32_t addr_len, uint64_t now_ms, uint8_t qr[DM_QR_MAX], uint32_t *qr_len,
                      char code[DM_CODE_CHARS + 1u]);

/* Joiner: start pairing from a scanned QR payload, or from a typed short
 * code (case-insensitive, dashes and spaces ignored, I/L read as 1, O as 0).
 * Sends M1. Then wait for DM_EV_SAS. */
dm_status_t dm_join_qr(dm_mesh_t *m, const uint8_t *qr, uint32_t qr_len, uint64_t now_ms);
dm_status_t dm_join_code(dm_mesh_t *m, const char *code, uint64_t now_ms);

/* 8-byte tag a host can broadcast on a LAN to find the device showing
 * `code` without revealing it. */
dm_status_t dm_code_tag(const char *code, uint8_t tag[8]);

/* Both devices: the user says whether the SAS shown matches the other
 * screen. On the inviter a match adds the device to the roster and sends
 * it; a mismatch on either side abandons the pairing. */
dm_status_t dm_pair_confirm(dm_mesh_t *m, const uint8_t peer[DM_ID_BYTES], bool match,
                            uint64_t now_ms);

/* ===== roster management (admin only) ===== */
dm_status_t dm_rename(dm_mesh_t *m, const uint8_t id[DM_ID_BYTES], const char *name,
                      uint64_t now_ms);
dm_status_t dm_set_role(dm_mesh_t *m, const uint8_t id[DM_ID_BYTES], uint8_t role, uint8_t flags,
                        uint64_t now_ms);
dm_status_t dm_revoke(dm_mesh_t *m, const uint8_t id[DM_ID_BYTES], uint64_t now_ms);

/* Check and install an encoded roster from a peer (see ROSTER RULES).
 * Exposed for tests and for hosts that carry rosters out of band. */
dm_status_t dm_roster_accept(dm_mesh_t *m, const uint8_t *enc, uint32_t len, uint64_t now_ms);
int32_t dm_roster_encode(const dm_roster_t *r, uint8_t *out, uint32_t cap);

/* ===== sessions ===== */

/* Open a session to a device in the roster (no-op if one is up). */
dm_status_t dm_connect(dm_mesh_t *m, const uint8_t peer[DM_ID_BYTES], uint64_t now_ms);

/* Feed one received frame. */
dm_status_t dm_receive(dm_mesh_t *m, const uint8_t *frame, uint32_t len, uint64_t now_ms);

/* Periodic work: pings, peer-down detection, request fallback. */
void dm_tick(dm_mesh_t *m, uint64_t now_ms);

bool dm_peer_up(const dm_mesh_t *m, const uint8_t peer[DM_ID_BYTES], uint64_t now_ms);

/* 32 bytes for an EHOP channel or other carrier key, bound to this session
 * and `label`. */
dm_status_t dm_session_export_key(const dm_mesh_t *m, const uint8_t peer[DM_ID_BYTES],
                                  const char *label, uint8_t out[32]);

/* Send an application record (used by dm_remote.c and dm_sync.c). */
dm_status_t dm_send_app(dm_mesh_t *m, const uint8_t peer[DM_ID_BYTES], uint8_t app,
                        const uint8_t *data, uint32_t len, uint64_t now_ms);

/* ===== text helpers ===== */

/* RFC 4648 base32 (A-Z 2-7, no padding): fits QR alphanumeric mode. Returns
 * characters written (NUL-terminated) or -1. */
int32_t dm_b32_encode(const uint8_t *in, uint32_t len, char *out, uint32_t cap);
int32_t dm_b32_decode(const char *in, uint8_t *out, uint32_t cap);

/* ===== small byte helpers shared by the devmesh files ===== */
void dm_mcpy(void *dst, const void *src, size_t n);
void dm_mset(void *dst, uint8_t v, size_t n);
bool dm_meq(const void *a, const void *b, size_t n);
int dm_mcmp(const void *a, const void *b, size_t n);

/* App record types (first byte of every record plaintext). */
#define DM_APP_CONFIRM_KEYS 1u /* internal: M3 */
#define DM_APP_ROSTER       2u
#define DM_APP_KEYS         3u
#define DM_APP_CAPS         4u
#define DM_APP_SYNC         5u
#define DM_APP_REQ          6u
#define DM_APP_REPLY        7u
#define DM_APP_MONEY_ASK    8u
#define DM_APP_MONEY_ANSWER 9u
#define DM_APP_PING         10u
#define DM_APP_PONG         11u
#define DM_APP_SAS_OK       12u /* pairing: the joiner's user confirmed the SAS */

/* Hooks implemented in dm_remote.c / dm_sync.c and called by devmesh.c. */
void dm_remote_on_app(dm_mesh_t *m, dm_peer_t *p, uint8_t app, const uint8_t *d, uint32_t n,
                      uint64_t now_ms);
void dm_remote_on_session_up(dm_mesh_t *m, dm_peer_t *p, uint64_t now_ms);
void dm_remote_tick(dm_mesh_t *m, uint64_t now_ms);
dm_peer_t *dm_peer_find(dm_mesh_t *m, const uint8_t id[DM_ID_BYTES]);
void dm_emit(dm_mesh_t *m, uint8_t kind, const uint8_t *peer, uint32_t sas, uint64_t u64,
             int32_t status);

#endif /* ZXV_DEVMESH_H */
