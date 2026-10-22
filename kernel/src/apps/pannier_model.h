/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* pannier_model.h — Pannier, the store-and-forward carrier, as a model
 * (zxm.h): messages and data parcels wait in a bounded queue for a peer,
 * go out cut into freights that survive lost packets, and are tracked to
 * a receipt. No drawing, no allocation, integers only; the rules it runs
 * are pannier_rules.h (PR1-PR7) and its parts' (Rookery, Trellis, Pollen,
 * Ferry, Caravan, Cove, Warren).
 *
 * zxvd methods used: net.peers, peer.status, files.stat, files.read,
 * files.write, freight.pack and freight.unpack. zxvd has no relay queue,
 * no method that hands packets to a peer or takes a peer's receipt, and no
 * notification method: the queue is PANNIER_FILE in the Hoard, the
 * carrier is this machine (a freight rebuilt from its last k packets only
 * into PANNIER_OUT), receipts and hop reports come from the view, and a
 * notice is text only.
 *
 *   PM1 CARRIER PEERS. pannier_add_peer keeps up to PANNIER_PEERS peers by
 *       NodeID (64 hex digits), numbered 1.. in the order added, each
 *       waiting until net.peers lists it; pannier_peers reads net.peers:
 *       a peer listed is active, one not listed is waiting, one the owner
 *       revoked stays revoked (pannier_revoke, on the owner's word). How
 *       many are active is trellis.peers_in_state; a peer's record is
 *       trellis.peer_record_ok.
 *   PM2 THE PEER BOOK. pannier_book reads peer.status; while the book is
 *       known not to verify, nothing is carried.
 *   PM3 A MESSAGE PARCEL is a letter in the Hoard: its subject and body
 *       within Rookery's size limits (rookery.letter_size_ok), the
 *       recipient's card verified and every consent present (may this be
 *       sent, rookery.letter_send_ok), its file the size of the payload
 *       ("Subject: " + subject + two newlines + body) and its index line a
 *       ^ZXROOKERY record (rookery.letter_record_ok, folder 3, held).
 *   PM4 A DATA PARCEL is any Hoard file of 1..PANNIER_PARCEL_MAX bytes,
 *       sent at k data rows a freight (1..168); its chunk plan is Pollen's
 *       (pollen.freight_count, pollen.freight_packets).
 *   PM5 THE QUEUE. Parcels wait in a ring of PANNIER_QUEUE slots (PR5):
 *       head - tail >= PANNIER_QUEUE is backpressure and a new parcel is
 *       refused, nothing changed; parcels leave oldest first, the tail
 *       passing over every parcel closed or given up. Each parcel is a
 *       shipment record (caravan.ship_ok: ship its number, carrier its
 *       peer, mass its bytes, charged its packets, a Caravan status) and a
 *       node of the parcel tree (PR6).
 *   PM6 CARRYING, on the owner's word. A parcel goes when send now holds
 *       (PR1: its peer active, within size, the attempt at most 16), its
 *       wait is over (PR2: warren.backoff from 30 s, capped at 3600 s), it
 *       fits on the receiver (ferry.target_fits: its bytes plus the spare
 *       the owner keeps there within what the receiver has) and it is not
 *       held BOTH or already delivered. Each freight is read from the
 *       Hoard, packed (freight.pack), rebuilt from its last k packets only
 *       (freight.unpack) and written to PANNIER_OUT; the parcel is then in
 *       transit.
 *   PM7 RECEIPTS AND HOP REPORTS. A signed receipt's sequence must rise
 *       past the peer's last (cove.tunnel_seq_gap: a reused sequence or
 *       one past eighteen digits refused, nothing changed). A receipt says
 *       delivered, a hop report of loss says lost; joined four-valued
 *       (PR7) a parcel with both is BOTH: neither sent again nor closed
 *       until a second receipt or the recipient's word (pannier_witness,
 *       on the owner's word) settles it.
 *   PM8 CLOSING. A delivered parcel is closed, and one still undelivered
 *       given up, only on the owner's word; neither applies to a BOTH
 *       parcel.
 *   PM9 THE FILE. PANNIER_FILE holds the peers and the queue, written
 *       whole (files.write) only on the owner's word and read back with
 *       files.read; a file that does not parse leaves what is open as it
 *       was.
 *
 * Advisory: the route time (PR4, pannier_route) and the notices say what
 * the numbers say; the owner decides. Pannier never pays. */
#ifndef PANNIER_MODEL_H
#define PANNIER_MODEL_H

#include "pannier_rules.h"
#include "zxm.h"

#define PANNIER_FILE       "Pannier.queue"
#define PANNIER_OUT        "Pannier.out" /* + ".<peer>.<parcel>" */
#define PANNIER_PEERS      8u            /* a carrier's peers, Trellis's DM_MAX_DEVICES */
#define PANNIER_QUEUE      16u           /* PM5: ring slots */
#define PANNIER_NODE       64u           /* a NodeID's hex digits */
#define PANNIER_PATH_MAX   128u
#define PANNIER_TEXT_MAX   12288u
#define PANNIER_NOTICE_MAX 256u
#define PANNIER_CHUNK      3360u /* freight.pack's most bytes, 168 rows of 20 */
#define PANNIER_ROWS       168u
#define PANNIER_FOLDER     3 /* PM3: Rookery's held folder */

enum { PANNIER_MESSAGE = 1, PANNIER_DATA = 2 };

typedef struct {
    char id[PANNIER_NODE + 1];
    int64_t state;    /* TRELLIS_ACTIVE, TRELLIS_REVOKED or TRELLIS_WAITING */
    int64_t seen_ms;  /* net.peers' last_seen_ms, 0 never */
    int64_t last_seq; /* the last receipt's sequence, 0 none */
} pannier_peer_t;

typedef struct {
    int64_t n;    /* its number, 1.. */
    int64_t peer; /* 1..PANNIER_PEERS */
    int64_t kind; /* PANNIER_MESSAGE or PANNIER_DATA */
    char path[PANNIER_PATH_MAX + 1];
    int64_t bytes, k, freights, packets;
    int64_t attempt;                 /* attempts made */
    int64_t next_at;                 /* seconds since 1970: not before */
    int64_t status;                  /* CARAVAN_DRAFTED .. CARAVAN_RETURNED */
    int64_t stamp;                   /* seconds since 1970 of the last change */
    int64_t receipts, lost, witness; /* PR7 */
} pannier_parcel_t;

typedef struct {
    zxm_link_t *link;
    pannier_peer_t peer[PANNIER_PEERS];
    uint32_t npeers;
    pannier_parcel_t q[PANNIER_QUEUE];
    int64_t head, tail; /* PR5's counters */
    int64_t next_n;
    bool book_known, book_ok, serving; /* PM2 */
    bool dirty;
    char notice[PANNIER_NOTICE_MAX + 1];
    /* working space */
    uint8_t bytes[PANNIER_CHUNK];
    char b64[4u * (PANNIER_CHUNK / 3u) + 8u];
    char hex[2u * PANNIER_CHUNK + 1u];
    char packets_hex[2u * 21u * PANNIER_ROWS + 1u];
    char header_hex[129];
} pannier_t;

void pannier_init(pannier_t *p, zxm_link_t *link);
/* PM1: the new peer's number in *peer (may be NULL) */
int pannier_add_peer(pannier_t *p, const char *id, int64_t *peer);
int pannier_peers(pannier_t *p);
int pannier_revoke(pannier_t *p, int64_t peer, bool owner_confirmed);
int64_t pannier_active(const pannier_t *p);
/* PM2 */
int pannier_book(pannier_t *p);
/* PM3, PM4: the new parcel's number in *n (may be NULL) */
int pannier_put_message(pannier_t *p, int64_t peer, const char *path, int64_t subject, int64_t body,
                        int64_t verified, int64_t consentgap, int64_t now, int64_t *n);
int pannier_put_file(pannier_t *p, int64_t peer, const char *path, int64_t k, int64_t now,
                     int64_t *n);
pannier_parcel_t *pannier_find(pannier_t *p, int64_t n);
/* PM5: slots left (PR5) */
int64_t pannier_room(const pannier_t *p);
/* PM5: the oldest parcel that may be carried at now, 0 none */
int64_t pannier_next(const pannier_t *p, int64_t now);
/* PM6: avail what the receiver may still write, spare what it keeps free */
int pannier_send(pannier_t *p, int64_t n, int64_t avail, int64_t spare, int64_t now,
                 bool owner_confirmed);
/* PM7 */
int pannier_receipt(pannier_t *p, int64_t n, int64_t seq, int64_t now);
int pannier_hop_lost(pannier_t *p, int64_t n, int64_t now);
int pannier_witness(pannier_t *p, int64_t n, int64_t said, int64_t now, bool owner_confirmed);
lpres_state_t pannier_state(const pannier_t *p, int64_t n);
/* PM8 */
int pannier_close(pannier_t *p, int64_t n, int64_t now, bool owner_confirmed);
int pannier_give_up(pannier_t *p, int64_t n, int64_t now, bool owner_confirmed);
/* PM9 */
int pannier_save(pannier_t *p, bool owner_confirmed);
int pannier_load(pannier_t *p);
/* advisory: PR4 over n hops of minutes, *num / *den hours */
int pannier_route(const int64_t *hop, uint32_t n, int64_t *num, int64_t *den);

#endif
