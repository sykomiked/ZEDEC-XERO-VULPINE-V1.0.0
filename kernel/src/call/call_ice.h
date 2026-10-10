/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* call_ice.h — NAT traversal helpers: candidates, STUN-like binding
 * checks, an ICE-style connectivity-check agent and DHT rendezvous.
 *
 * Nothing here opens a socket. Every outgoing datagram goes through a send
 * callback naming the local candidate (socket) and destination address, and
 * every incoming datagram is handed in with the same two facts.
 *
 * CANDIDATES. Priority follows RFC 8445 5.1.2.1:
 *   (type_pref << 24) | (local_pref << 8) | (256 - component),
 *   type_pref host 126, peer-reflexive 110, server-reflexive 100, relay 0.
 * Server-reflexive candidates are learned by sending a binding request to a
 * reflector: any DHT peer that answers with the address it saw
 * (call_gather_*). Relay candidates (a peer that forwards for us) are only
 * a type and a priority here.
 *
 * BINDING MESSAGES (STUN-like, RFC 8489 layout, big-endian):
 *   0 2 type: 0x0001 request, 0x0101 success, 0x0111 error
 *   2 2 attribute bytes   4 4 magic 0x2112A442   8 12 transaction id
 *   then TLV attributes (type 2, length 2, value padded to 4):
 *   0x0006 USERNAME (remote ufrag 8 + local ufrag 8), 0x0024 PRIORITY (4),
 *   0x0025 USE-CANDIDATE (0), 0x8029 ICE-CONTROLLED (8), 0x802A
 *   ICE-CONTROLLING (8), 0x0020 XOR-MAPPED-ADDRESS, 0x0009 ERROR-CODE (4),
 *   0x0008 MESSAGE-INTEGRITY (16, last; caller-supplied MAC keyed with the
 *   password exchanged in the offer/answer). Unknown attributes below 0x8000
 *   are rejected, as STUN requires.
 *
 * AGENT. Both sides check every (local, remote) pair, highest pair priority
 * first (RFC 8445 6.1.2.3), one new check per 20 ms tick, retransmitting
 * with RTO 100 ms doubling to 1.6 s and failing a pair after 5 tries. All
 * pairs start WAITING (no frozen/foundation logic). The controlling agent
 * nominates aggressively (USE-CANDIDATE on every check) and selects the
 * first pair that succeeds; the controlled agent selects a pair once it is
 * nominated by the peer and its own triggered check on it has succeeded.
 * Role conflicts are resolved with the 64-bit tie-breaker and error 487.
 * Requests from unknown addresses add peer-reflexive remote candidates.
 *
 * RENDEZVOUS. A node behind NAT REGISTERs a signed record (node id, expiry,
 * candidates) with the DHT peers closest to its id (XOR metric). A caller
 * LOOKUPs the callee's id: a peer answers FOUND with the stored record (the
 * signature travels with it, so any peer may serve it) or NOT_FOUND with up
 * to 8 closer peers, and the lookup walks towards the id. INTRODUCE asks the
 * peer holding the callee's registration to forward our candidates, so both
 * sides start connectivity checks at once (simultaneous-open hole punching).
 *
 * RULES. Freestanding C11: no libc, no malloc, no floating point, no 64-bit
 * division, no __int128. All memory is the caller's.
 *
 * HONEST LIMITS. No sockets, no real STUN/TURN server interop testing: the
 * binding format follows STUN's layout but is only exercised against
 * itself. Without a MAC callback, MESSAGE-INTEGRITY is omitted and checks
 * are unauthenticated. No TURN allocation, no consent freshness, no
 * frozen-pair scheduling, a single component, and hole punching fails on
 * symmetric NATs that a relay would be needed for. The DHT is message
 * formats and a lookup state machine; the routing table and storage live
 * with the DHT owner. There are no codecs and no camera or microphone
 * capture here (host OS / bundled libopus+libvpx later).
 */
#ifndef CALL_ICE_H
#define CALL_ICE_H

#include <stdbool.h>
#include <stdint.h>

#define CALL_STUN_MAGIC     0x2112A442u
#define CALL_STUN_HDR_LEN   20
#define CALL_STUN_REQ       0x0001
#define CALL_STUN_RESP      0x0101
#define CALL_STUN_ERR       0x0111
#define CALL_STUN_MAX       128
#define CALL_ICE_UFRAG_LEN  8
#define CALL_ICE_PWD_LEN    16
#define CALL_ICE_MAX_CANDS  8
#define CALL_ICE_MAX_PAIRS  64
#define CALL_ICE_TA_MS      20
#define CALL_ICE_RTO_MS     100
#define CALL_ICE_RTO_MAX_MS 1600
#define CALL_ICE_MAX_TRIES  5

typedef struct call_addr {
    uint8_t family; /* 4 or 6 */
    uint8_t ip[16]; /* IPv4 in ip[0..3] */
    uint16_t port;
} call_addr_t;

bool call_addr_eq(const call_addr_t *a, const call_addr_t *b);

typedef enum {
    CALL_CAND_HOST = 0,
    CALL_CAND_SRFLX = 1,
    CALL_CAND_PRFLX = 2,
    CALL_CAND_RELAY = 3,
} call_cand_type_t;

typedef struct call_cand {
    call_addr_t addr;
    call_addr_t base; /* local socket the candidate is reached through */
    uint32_t priority;
    uint32_t foundation;
    uint8_t type;
    uint8_t component;
    uint8_t base_idx; /* index of the host candidate (socket) */
} call_cand_t;

uint32_t call_cand_priority(call_cand_type_t type, uint16_t local_pref, uint8_t component);
void call_cand_make(call_cand_t *c, call_cand_type_t type, const call_addr_t *addr,
                    const call_addr_t *base, uint16_t local_pref, uint8_t base_idx);

/* Candidate wire form inside offers and rendezvous records: 29 octets. */
#define CALL_CAND_WIRE 29
int call_cand_write(const call_cand_t *c, uint8_t *out, uint32_t cap);
int call_cand_parse(const uint8_t *in, uint32_t len, call_cand_t *c);

/* ---- binding messages ---- */

typedef struct call_stun {
    uint16_t type;
    uint8_t txid[12];
    uint8_t has_username, has_priority, use_candidate, has_controlling, has_controlled;
    uint8_t has_mapped, has_error, has_integrity;
    uint8_t username[16];
    uint32_t priority;
    uint64_t tie; /* controlling or controlled tie-breaker */
    call_addr_t mapped;
    uint16_t error;         /* e.g. 487 */
    uint32_t integrity_off; /* offset of the MESSAGE-INTEGRITY attribute */
    uint8_t integrity[16];
} call_stun_t;

typedef bool (*call_mac_fn)(void *ctx, const uint8_t *key, uint32_t klen, const uint8_t *msg,
                            uint32_t len, uint8_t out[16]);

/* Write a binding message; key/mac may be NULL (no integrity). */
int call_stun_write(const call_stun_t *m, const uint8_t *key, uint32_t klen, call_mac_fn mac,
                    void *mctx, uint8_t *out, uint32_t cap);
int call_stun_parse(const uint8_t *in, uint32_t len, call_stun_t *m);
/* Check MESSAGE-INTEGRITY of a parsed message (true if mac is NULL). */
bool call_stun_verify(const call_stun_t *m, const uint8_t *in, uint32_t len, const uint8_t *key,
                      uint32_t klen, call_mac_fn mac, void *mctx);

/* ---- gathering ---- */

typedef int (*call_ice_send_fn)(void *ctx, uint32_t local_idx, const call_addr_t *to,
                                const uint8_t *msg, uint32_t len);

typedef struct call_gather {
    call_cand_t *c;
    uint32_t cap, n;
    uint32_t nhost;
    call_addr_t refl[4];
    uint32_t nrefl;
    uint8_t txid[4 * CALL_ICE_MAX_CANDS][12];
    uint8_t pending[4 * CALL_ICE_MAX_CANDS];
    uint8_t tries;
    uint32_t next_ms;
    uint32_t seed;
    uint8_t done;
    call_ice_send_fn send;
    void *sctx;
} call_gather_t;

void call_gather_init(call_gather_t *g, call_cand_t *store, uint32_t cap, uint32_t seed,
                      call_ice_send_fn send, void *sctx);
int call_gather_add_host(call_gather_t *g, const call_addr_t *addr, uint16_t local_pref);
int call_gather_start(call_gather_t *g, const call_addr_t *reflectors, uint32_t n, uint32_t now);
/* Returns 1 if a new server-reflexive candidate was added. */
int call_gather_on_packet(call_gather_t *g, uint32_t local_idx, const call_addr_t *from,
                          const uint8_t *msg, uint32_t len);
void call_gather_tick(call_gather_t *g, uint32_t now);

/* ---- connectivity checks ---- */

typedef enum {
    CALL_PAIR_WAITING = 0,
    CALL_PAIR_IN_PROGRESS = 1,
    CALL_PAIR_SUCCEEDED = 2,
    CALL_PAIR_FAILED = 3,
} call_pair_state_t;

typedef struct call_pair {
    uint64_t prio;
    uint32_t rto_ms;
    uint32_t deadline;
    uint8_t txid[12];
    uint8_t l, r;
    uint8_t state;
    uint8_t tries;
    uint8_t nominated; /* by the peer (controlled side) */
    uint8_t triggered;
} call_pair_t;

typedef enum {
    CALL_ICE_IDLE = 0,
    CALL_ICE_RUNNING,
    CALL_ICE_COMPLETED,
    CALL_ICE_FAILED
} call_ice_state_t;

typedef struct call_ice {
    uint8_t controlling;
    uint64_t tie;
    uint8_t lufrag[CALL_ICE_UFRAG_LEN], lpwd[CALL_ICE_PWD_LEN];
    uint8_t rufrag[CALL_ICE_UFRAG_LEN], rpwd[CALL_ICE_PWD_LEN];
    call_cand_t local[CALL_ICE_MAX_CANDS];
    call_cand_t remote[CALL_ICE_MAX_CANDS];
    uint32_t nlocal, nremote;
    call_pair_t pairs[CALL_ICE_MAX_PAIRS];
    uint32_t npairs;
    int32_t selected;
    call_ice_state_t state;
    uint32_t next_tick;
    uint32_t seed;
    call_ice_send_fn send;
    void *sctx;
    call_mac_fn mac;
    void *mctx;
    uint32_t checks_sent, role_switches, bad_integrity;
} call_ice_t;

void call_ice_init(call_ice_t *a, bool controlling, uint64_t tie, const uint8_t *ufrag,
                   const uint8_t *pwd, uint32_t seed, call_ice_send_fn send, void *sctx,
                   call_mac_fn mac, void *mctx);
int call_ice_add_local(call_ice_t *a, const call_cand_t *c);
int call_ice_set_remote(call_ice_t *a, const uint8_t *ufrag, const uint8_t *pwd,
                        const call_cand_t *c, uint32_t n);
/* Form and sort the pair list; checks start on the next tick. */
int call_ice_start(call_ice_t *a, uint32_t now);
void call_ice_tick(call_ice_t *a, uint32_t now);
/* Returns 1 if the datagram was a binding message for this agent. */
int call_ice_on_packet(call_ice_t *a, uint32_t local_idx, const call_addr_t *from,
                       const uint8_t *msg, uint32_t len, uint32_t now);
const call_pair_t *call_ice_selected(const call_ice_t *a);

/* ---- DHT rendezvous ---- */

#define CALL_RV_MAGIC      0x5256
#define CALL_RV_ID_LEN     32
#define CALL_RV_SIG_LEN    64
#define CALL_RV_MAX_PEERS  8
#define CALL_RV_REGISTER   1
#define CALL_RV_REGISTERED 2
#define CALL_RV_LOOKUP     3
#define CALL_RV_FOUND      4
#define CALL_RV_NOT_FOUND  5
#define CALL_RV_INTRODUCE  6
/* largest message: NOT_FOUND with 8 peers (453), INTRODUCE is 378 */
#define CALL_RV_MAX_MSG (12 + 33 + CALL_RV_MAX_PEERS * (CALL_RV_ID_LEN + 19))

typedef struct call_rv_peer {
    uint8_t id[CALL_RV_ID_LEN];
    call_addr_t addr;
} call_rv_peer_t;

typedef struct call_rv_record {
    uint8_t id[CALL_RV_ID_LEN];
    uint32_t expires_s; /* absolute, sender clock */
    uint8_t ncands;
    call_cand_t cands[CALL_ICE_MAX_CANDS];
    uint8_t sig[CALL_RV_SIG_LEN];
} call_rv_record_t;

typedef struct call_rv_msg {
    uint8_t type;
    uint32_t txid;
    uint8_t target[CALL_RV_ID_LEN]; /* LOOKUP/NOT_FOUND/INTRODUCE: who is wanted */
    call_rv_record_t rec;           /* REGISTER/FOUND/INTRODUCE */
    uint8_t npeers;                 /* NOT_FOUND */
    call_rv_peer_t peers[CALL_RV_MAX_PEERS];
} call_rv_msg_t;

/*  0 2 magic  2 1 version (1)  3 1 type  4 4 txid  8 4 reserved (0)
 *  REGISTER/FOUND: record = 32 id, 4 expiry, 1 count, count*29 cands, 64 sig
 *  REGISTERED: 32 id, 4 expiry
 *  LOOKUP: 32 target
 *  NOT_FOUND: 32 target, 1 count, count * (32 id + 19 address)
 *  INTRODUCE: 32 target, then a record (the requester's) */
int call_rv_write(const call_rv_msg_t *m, uint8_t *out, uint32_t cap);
int call_rv_parse(const uint8_t *in, uint32_t len, call_rv_msg_t *m);
/* Bytes of a record that its signature covers, written to out. */
int call_rv_record_signed_bytes(const call_rv_record_t *r, uint8_t *out, uint32_t cap);

/* XOR distance comparison: <0 if a is closer to target than b. */
int call_rv_closer(const uint8_t *target, const uint8_t *a, const uint8_t *b);

typedef bool (*call_rv_verify_fn)(void *ctx, const uint8_t id[CALL_RV_ID_LEN], const uint8_t *msg,
                                  uint32_t len, const uint8_t sig[CALL_RV_SIG_LEN]);

typedef enum {
    CALL_LOOKUP_IDLE = 0,
    CALL_LOOKUP_RUNNING,
    CALL_LOOKUP_FOUND,
    CALL_LOOKUP_FAILED,
} call_lookup_state_t;

typedef struct call_lookup {
    uint8_t target[CALL_RV_ID_LEN];
    call_rv_peer_t cand[16]; /* shortlist, closest first */
    uint8_t queried[16];
    uint32_t ncand;
    int32_t inflight; /* index queried, -1 none */
    uint32_t txid;
    uint32_t deadline;
    uint32_t timeout_ms;
    uint32_t queries;
    call_lookup_state_t state;
    call_rv_record_t found;
    call_rv_verify_fn verify;
    void *vctx;
    uint32_t bad_sig;
} call_lookup_t;

/* Iterative lookup, one query in flight. The caller sends each query it is
 * handed (call_lookup_next) and feeds replies to call_lookup_on_reply. A
 * FOUND record is accepted only if its id is the target and verify (over
 * call_rv_record_signed_bytes) accepts its signature. */
void call_lookup_init(call_lookup_t *l, const uint8_t *target, const call_rv_peer_t *seeds,
                      uint32_t nseeds, uint32_t timeout_ms, uint32_t txid0,
                      call_rv_verify_fn verify, void *vctx);
/* Returns true and fills *to and *msg when a query should be sent now. */
bool call_lookup_next(call_lookup_t *l, uint32_t now, call_rv_peer_t *to, call_rv_msg_t *msg);
void call_lookup_on_reply(call_lookup_t *l, const call_rv_msg_t *msg);

#endif /* CALL_ICE_H */
