/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* signpost_model.h — Signpost, names and addresses, as a model (zxm.h):
 * the forms of a content address, a Web4 agent card's check, the peers the
 * routing table verified by NodeID, and a bounded cache of name to IPv4
 * answers with their time to live. The rules are signpost_rules.h.
 *
 * zxvd methods used: web4.cid, web4.card_verify and net.peers. zxvd has no
 * DNS and no DID method, so nothing is resolved over the network: a name is
 * answered only from the cache (signpost_remember fills it). A card that
 * verifies says only that its key and its binding agree; whether to trust
 * the agent stays the owner's call (advisory). No drawing, no allocation,
 * integers only.
 *
 *   SN1 NAMES. signpost_name_check measures each label of a name (SP1) and
 *       gives the first fault (SP2) with the label it is in; a trailing dot
 *       is the root and is not counted.
 *   SN2 ADDRESSES. signpost_parse_ipv4 reads a.b.c.d, optionally :port:
 *       each octet 0..255 in plain decimal with no leading zero (so "010"
 *       can never be read as octal), a port 1..65535.
 *   SN3 THE CACHE holds at most SIGNPOST_CACHE answers. Every answer is
 *       checked first: its name fits (SN1), its address parses (SN2), and
 *       its record fits ^ZXSIGNPOST (SP5). The same name (letters compared
 *       without case) is replaced; when the cache is full a lapsed answer
 *       goes first, then the one with the fewest seconds left (SP6).
 *   SN4 A LOOKUP answers only while seconds are left; a lapsed answer is
 *       dropped and the lookup is ZXM_E_STATE.
 *   SN5 PEERS. signpost_peers reads net.peers whole into the model, each
 *       address classed (SP4); a reply that does not parse leaves the list
 *       as it was. Peers are found by NodeID (64 lower-case hex digits). */
#ifndef SIGNPOST_MODEL_H
#define SIGNPOST_MODEL_H

#include "signpost_rules.h"
#include "zxm.h"

#define SIGNPOST_CACHE     64u
#define SIGNPOST_PEERS     64u
#define SIGNPOST_ID_HEX    64u   /* a NodeID or agent_id */
#define SIGNPOST_CID_MAX   147u  /* web4.cid's longest CID or URI in */
#define SIGNPOST_URI_MAX   160u  /* "ipfs://" and the CID */
#define SIGNPOST_DATA_MAX  8000u /* bytes web4.cid addresses (16000 hex in a request) */
#define SIGNPOST_CARD_NAME 31u
#define SIGNPOST_PK_HEX    3904u /* ML-DSA-65 public key, 1952 bytes */
#define SIGNPOST_SIG_HEX   6618u /* the binding signature, 3309 bytes */
#define SIGNPOST_REASON    160u
#define SIGNPOST_TTL_MAX   2147483647 /* a DNS time to live is 31 bits */

/* SN1: what signpost_name_check found. */
typedef struct {
    int32_t fault;  /* SP2: SIGNPOST_FITS .. SIGNPOST_BAD_BYTE */
    uint32_t label; /* the label it is in (0 = the first) */
    uint32_t labels;
    uint32_t total; /* bytes, the trailing dot aside */
} signpost_scan_t;

typedef struct {
    char name[SIGNPOST_NAME_MAX + 1];
    uint8_t octet[4];
    int32_t cls; /* SP4 */
    int64_t stored, ttl;
} signpost_answer_t;

typedef struct {
    char id[SIGNPOST_ID_HEX + 1]; /* NodeID */
    bool has_addr;                /* an IPv4 address with its port */
    uint8_t octet[4];
    uint16_t port;
    int32_t cls; /* SP4, or SIGNPOST_REFUSED without an address */
    int64_t bucket;
    uint64_t last_seen_ms;
    bool member; /* admitted to the commons by the owner */
} signpost_peer_t;

typedef struct {
    char cid[SIGNPOST_URI_MAX + 1];
    char uri[SIGNPOST_URI_MAX + 1];
    int64_t version, codec;
    bool has_digest; /* sha2-256: the 32 bytes an EVM contract can store */
    char digest[SIGNPOST_ID_HEX + 1];
} signpost_cid_t;

/* A self-bound agent card as web4.agent_card makes it. */
typedef struct {
    const char *name;     /* 1..31 of a-z 0-9 . _ - */
    uint64_t bound_ms;    /* the binding time */
    const char *pk_hex;   /* SIGNPOST_PK_HEX hex digits */
    const char *sig_hex;  /* SIGNPOST_SIG_HEX hex digits */
    const char *agent_id; /* NULL, or SIGNPOST_ID_HEX hex digits to check */
} signpost_card_t;

typedef struct {
    bool valid;
    char agent_id[SIGNPOST_ID_HEX + 1];
    char reason[SIGNPOST_REASON + 1];
} signpost_verdict_t;

typedef struct {
    zxm_link_t *link;
    signpost_answer_t cache[SIGNPOST_CACHE];
    uint32_t ncache;
    signpost_peer_t peer[SIGNPOST_PEERS];
    uint32_t npeers;
    uint32_t peers_left_out; /* net.peers listed more than SIGNPOST_PEERS */
    signpost_cid_t cid;      /* the last web4.cid answer */
    signpost_verdict_t card; /* the last web4.card_verify answer */
} signpost_t;

/* SN1: the first fault of name[0..n); fills scan when it is not NULL. */
int32_t signpost_name_check(const char *name, uint32_t n, signpost_scan_t *scan);
/* SN2: true when s[0..n) is an IPv4 address (port 0 when none is given). */
bool signpost_parse_ipv4(const char *s, uint32_t n, uint8_t octet[4], uint16_t *port);
/* The address as one number, a * 2^24 + b * 2^16 + c * 2^8 + d. */
int64_t signpost_addr_number(const uint8_t octet[4]);

void signpost_init(signpost_t *s, zxm_link_t *link);

/* web4.cid: the forms of a CID or ipfs:// URI (s->cid). */
int signpost_cid(signpost_t *s, const char *cid);
/* web4.cid: the CIDv1 (raw, sha2-256) of n bytes (s->cid). */
int signpost_cid_of(signpost_t *s, const uint8_t *data, uint32_t n);
/* web4.card_verify (s->card): ZXM_E_ARG for a card that cannot be one,
 * before anything is sent. */
int signpost_card_verify(signpost_t *s, const signpost_card_t *card);
/* net.peers (SN5). */
int signpost_peers(signpost_t *s);
/* SN5: the peer with this NodeID, or NULL. */
const signpost_peer_t *signpost_peer(const signpost_t *s, const char *id);

/* SN3: remember that name[0..n) answers addr (a.b.c.d) for ttl seconds
 * from now. */
int signpost_remember(signpost_t *s, const char *name, uint32_t n, const char *addr, int64_t ttl,
                      int64_t now);
/* SN4: the answer for name[0..n) at now, and its seconds left. */
int signpost_lookup(signpost_t *s, const char *name, uint32_t n, int64_t now,
                    signpost_answer_t *out, int64_t *left);
/* Drop every answer lapsed at now; how many were dropped. */
uint32_t signpost_sweep(signpost_t *s, int64_t now);

#endif
