/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zxv_peer_host.h — remote inference between two nodes over TCP, inside a
 * post-quantum Vinea session, settled with the 0.08889% assurance fee.
 *
 * The UDP node (zxv_net_host.h) finds peers and verifies who they are. This
 * file is the second transport: a TCP stream between two nodes that already
 * know each other's NodeID, used for one paid request at a time.
 *
 *   P1 OFF BY DEFAULT. A node answers remote requests only when its owner
 *      starts serving (--peer-serve PRICE, or zxv_peer_serve) and networking
 *      is on. The listener is TCP on the same address and port number as the
 *      node's UDP socket. Asking another node needs no listener.
 *   P2 POLICY. In LAN mode a connection from, or to, anything but a private,
 *      link-local or loopback IPv4 address is refused before a byte is read
 *      or written (the same ranges as zxv_net_host.h U2).
 *   P3 FRAMING. Every message is a 4-byte big-endian length (1 to
 *      ZXV_PEER_FRAME_MAX) and that many bytes. A longer or empty frame
 *      closes the connection.
 *   P4 HANDSHAKE. The asker (A) knows the server's (B's) NodeID from the UDP
 *      node and runs the Vinea session handshake (vna_session.h): X25519 +
 *      ML-KEM-1024 hybrid key exchange, both sides authenticated with
 *      ML-DSA-87, NodeID = SHA3-256(public key). A server whose key does not
 *      hash to the NodeID A expected fails the handshake. Both sides
 *      advertise UBH-168 (VNA_FEAT_UBH168).
 *   P5 ONE PAID REQUEST per connection, every message an AEAD session record
 *      (ChaCha20-Poly1305, replay window):
 *        A -> B  REQ  req_id, max_price, prompt
 *        B -> A  ANS  req_id, status, price, ts, answer, sig_B(invoice)
 *        A -> B  PAY  req_id, gross, fee, the four fee parts, sig_A(settle)
 *        B -> A  ACK  req_id, status, sig_B(ack)
 *      invoice = SHA3-256("zxv/peer/v1/invoice" || session id || req_id ||
 *                seller || buyer || SHA3(prompt) || SHA3(answer) || price || ts)
 *      settle  = SHA3-256("zxv/peer/v1/settle" || invoice || gross || fee ||
 *                parts[0..3])
 *      ack     = SHA3-256("zxv/peer/v1/ack" || settle || status)
 *      Each is signed with ML-DSA-87 under its own context string, so the
 *      pair of nodes ends up holding a receipt either can show a third party.
 *      B refuses (status PRICE) a request whose max_price is below its price
 *      and answers nothing; A refuses an answer priced above its max_price.
 *   P6 SETTLEMENT. Each node keeps a peer book: a pay_ledger (pay_ledger.h) in
 *      VFV with a wallet, a "mesh counterparties" account (a mirror of what
 *      peers owe or are owed) and the four fee bucket accounts. A pays with
 *      pay_ledger_pay_with_fee: wallet -> mesh gross, plus
 *      fee = floor(gross * 8889 / 10^7) (with the wallet's sub-unit carry,
 *      pay_assure.h F2) split exactly into the four buckets (50/25/15/10,
 *      remainder to the reserve floor). A posts BEFORE it sends PAY, so a PAY
 *      it sent is always on its book. B checks gross == its price, the fee is
 *      floor(gross * 8889 / 10^7) or one more (the carry), the parts are
 *      pay_assure_split(fee) and the signature, then credits gross to its
 *      wallet from the mesh account and signs the ACK. The payer bears the
 *      fee; the seller receives the whole price.
 *   P7 DEMO FLOAT. A new book's wallet is empty. zxv_peer_fund issues VFV to
 *      it from the book's own issuer account, for demos and tests only
 *      (--peer-float); it is not money from anywhere.
 *
 * HONEST LIMITS: one request per connection and one connection at a time;
 * the caller's loop waits while a request runs (ZXV_PEER_IO_MS per message,
 * ZXV_PEER_ANSWER_MS for the answer). IPv4 only, no NAT traversal. The peer
 * book is in memory (reset per start) and separate from the owner's libzxv
 * books. Nothing here makes VFV legal tender or a regulated payment.
 */
#ifndef ZXV_PEER_HOST_H
#define ZXV_PEER_HOST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "vna_id.h"
#include "pay_assure.h"

#define ZXV_PEER_FRAME_MAX  24576u
#define ZXV_PEER_PROMPT_MAX 4096u
#define ZXV_PEER_ANSWER_MAX 8192u
#define ZXV_PEER_IO_MS      10000u
#define ZXV_PEER_ANSWER_MS  180000u
#define ZXV_PEER_HISTORY    16u

typedef struct zxv_peer zxv_peer_t;

/* How the server answers a prompt (the host's model or swarm). */
typedef void (*zxv_peer_answer_fn)(void *ctx, const char *prompt, char *out, size_t cap);

typedef enum { ZXV_PEER_ROLE_BUYER = 1, ZXV_PEER_ROLE_SELLER = 2 } zxv_peer_role_t;

/* One settled request, as this node saw it. */
typedef struct {
    uint8_t role; /* zxv_peer_role_t */
    vna_id_t peer;
    uint64_t req_id;
    uint64_t gross, fee;
    uint64_t parts[PAY_ASSURE_BUCKETS];
    uint8_t invoice[32], settle[32], session[32];
    uint64_t ts;
} zxv_peer_settlement_t;

/* What zxv_peer_ask returns. */
typedef struct {
    int status;     /* 0 settled; else a ZXV_PEER_E_* */
    char error[96]; /* why, when status != 0 */
    char answer[ZXV_PEER_ANSWER_MAX];
    zxv_peer_settlement_t s;
} zxv_peer_result_t;

enum {
    ZXV_PEER_E_ARG = -1,       /* bad arguments, or networking off */
    ZXV_PEER_E_POLICY = -2,    /* LAN mode and a public address */
    ZXV_PEER_E_CONNECT = -3,   /* TCP connect failed or timed out */
    ZXV_PEER_E_HANDSHAKE = -4, /* the session could not be established */
    ZXV_PEER_E_PROTO = -5,     /* a malformed, tampered or unexpected record */
    ZXV_PEER_E_PRICE = -6,     /* the price is above max_price */
    ZXV_PEER_E_FUNDS = -7,     /* the wallet cannot pay max_price + its fee */
    ZXV_PEER_E_SIG = -8,       /* an ML-DSA-87 signature did not verify */
    ZXV_PEER_E_REFUSED = -9,   /* the server refused the payment or request */
    ZXV_PEER_E_BOOK = -10      /* the peer book refused the posting */
};

typedef struct {
    bool serving;
    uint16_t port;
    uint64_t price;
    uint64_t wallet;    /* VFV minor units */
    uint64_t fees_paid; /* sum of the fees this node paid */
    uint64_t buckets[PAY_ASSURE_BUCKETS];
    uint32_t sold, bought, refused, sessions;
    bool book_ok; /* pay_ledger_check and the chain verify */
    uint32_t n_history;
    zxv_peer_settlement_t history[ZXV_PEER_HISTORY]; /* oldest first */
} zxv_peer_status_t;

/* A new node with an empty book and no listener; NULL if out of memory. */
zxv_peer_t *zxv_peer_new(void);
void zxv_peer_free(zxv_peer_t *p);

/* The identity to use (the UDP node's; it changes in place when networking
 * restarts) and whether LAN policy applies (P2). */
void zxv_peer_set_identity(zxv_peer_t *p, const vna_identity_t *self, bool lan_only);

/* P1: start (or restart) serving at `price` VFV minor units per answer on TCP
 * bind_ip:port. 0, or -1 with the reason in *why (may be NULL). */
int zxv_peer_serve(zxv_peer_t *p, const char *bind_ip, uint16_t port, uint64_t price,
                   zxv_peer_answer_fn fn, void *ctx, const char **why);
void zxv_peer_stop(zxv_peer_t *p);

/* The listener to add to select()'s read set, or -1. */
intptr_t zxv_peer_fd(const zxv_peer_t *p);
/* The listener is readable: accept one connection and serve it to the end. */
void zxv_peer_on_readable(zxv_peer_t *p, uint64_t now_ms);

/* P7: issue `minor` demo VFV to the wallet. 0 or -1. */
int zxv_peer_fund(zxv_peer_t *p, uint64_t minor);

/* Ask peer_id at ip:port, paying at most max_price. Fills *r; returns
 * r->status. */
int zxv_peer_ask(zxv_peer_t *p, const char *ip, uint16_t port, const vna_id_t *peer_id,
                 const char *prompt, uint64_t max_price, uint64_t now_ms, zxv_peer_result_t *r);

/* The same two halves on an already-connected stream socket (tests). */
int zxv_peer_ask_fd(zxv_peer_t *p, intptr_t fd, const vna_id_t *peer_id, const char *prompt,
                    uint64_t max_price, uint64_t now_ms, zxv_peer_result_t *r);
int zxv_peer_serve_fd(zxv_peer_t *p, intptr_t fd, uint64_t now_ms);

void zxv_peer_status(const zxv_peer_t *p, zxv_peer_status_t *st);

/* Tests only: make this node misbehave as a buyer (1: under-pay the fee by
 * one, 2: corrupt its settle signature) or as a seller (4: corrupt its
 * invoice signature). 0 restores honest behaviour. */
void zxv_peer_test_tamper(zxv_peer_t *p, uint32_t flags);

#endif
