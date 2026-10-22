/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* pollen_model.h — Pollen, share with nearby devices, as a model (zxm.h).
 * The rules are pollen_rules.h. No drawing, no allocation, integers only;
 * zxvd only through the link.
 *
 * zxvd methods used: net.command (lan and off only, share, want),
 * net.peers, net.offers, sys.disk, files.stat, files.read, files.write,
 * files.cross, freight.pack and freight.unpack.
 *
 *   PM1 THE LOCAL NETWORK. pollen_lan turns zxvd's peer network to the
 *       local network ("lan") or off; Pollen never asks for "online".
 *   PM2 NEARBY. net.peers lists the verified contacts; a peer is nearby
 *       when its address is loopback, private or link-local (Hedgerow's
 *       addr_scope, pollen_nearby_scope). Only nearby peers count.
 *   PM3 OFFERS. An offer is 1..16 files of the Hoard (files.stat), drafted
 *       first and checked against PR3 (its largest file, its files, the
 *       offers open, counted with Witness's kind_count) and PR4. It opens
 *       only on the owner's approval (pollen_approve): zxvd is told the
 *       storage bytes Pollen shares (net.command "share C S", the compute
 *       share read from net.offers kept as it was), and only then the
 *       offer is open. Withdrawing shares the bytes left again.
 *   PM4 FETCHING. A receiver asks for bytes (net.command "want storage
 *       UNITS 0") only with the owner's approval, with a nearby peer known,
 *       and only when they fit on the receiver: Ferry's target_fits over
 *       avail (sys.disk, or what the owner reads off it), the bytes and the
 *       spare kept free. The price is always 0: Pollen never pays.
 *   PM5 CARRYING. A file of an open offer is cut into freights of k data
 *       rows (freight.pack, PR1), and each freight is rebuilt from its last
 *       k packets only (freight.unpack: parity rows when k <= 84, so the
 *       erasure code is what carries it) and written to the Hoard
 *       (files.write begin, add, end; never over a file that is there).
 *       Progress x100 is used_share.used_x100 over the packets needed (PR2)
 *       and the packets still to rebuild, and the link's progress hears
 *       each freight. zxvd has no method that sends a freight's packets to
 *       a peer, so the carrier here is this machine (loopback); the path is
 *       the one a peer's packets would take.
 *   PM6 CROSSING. A file leaves a space only through files.cross; a
 *       crossing into a more open space is held by zxvd and done only when
 *       the owner confirms its ticket.
 *   PM7 NOTICE. Each step leaves a one-line notice for the owner (the
 *       notify capability); zxvd has no notification method yet, so the
 *       model composes the text only. */
#ifndef POLLEN_MODEL_H
#define POLLEN_MODEL_H

#include "pollen_rules.h"
#include "zxm.h"

#define POLLEN_PEERS    32u
#define POLLEN_OFFERS   16u   /* offers kept: drafted, open or withdrawn */
#define POLLEN_PATH_MAX 512u  /* a path of the Hoard Pollen keeps */
#define POLLEN_NODE     64u   /* a NodeID in hex */
#define POLLEN_CHUNK    3360u /* one freight's bytes at most: 168 rows of 20 */
#define POLLEN_NOTICE   160u
#define POLLEN_E_FILES  (-32014) /* zxvd's refusal by the Hoard (ZXVD_E_FILES) */

/* why the last request was refused before anything was asked */
enum {
    POLLEN_FINE = 0,
    POLLEN_F_LIMITS = 1,  /* outside PR3 */
    POLLEN_F_RECORD = 2,  /* outside PR4 */
    POLLEN_F_NO_ROOM = 3, /* does not fit on the receiver (target_fits) */
    POLLEN_F_NOBODY = 4,  /* no nearby peer known */
    POLLEN_F_WORD = 5,    /* the owner did not approve */
    POLLEN_F_FILE = 6,    /* a path that is no file */
};

typedef struct {
    char path[POLLEN_PATH_MAX + 1];
    int64_t size;
} pollen_file_t;

typedef struct {
    int64_t number; /* 1..POLLEN_OFFER_MAX */
    pollen_file_t file[POLLEN_FILES_MAX];
    uint32_t nfiles;
    int64_t bytes, largest;
    int64_t state; /* POLLEN_DRAFTED, POLLEN_OPEN, POLLEN_WITHDRAWN */
    int64_t stamp; /* ms since 1970 of its last change */
} pollen_offer_t;

typedef struct {
    char id[POLLEN_NODE + 1];
    char addr[24];
    int64_t scope; /* Hedgerow's HR4 scope; HEDGEROW_SCOPE_NONE without an address */
    bool nearby;
} pollen_peer_t;

typedef struct {
    zxm_link_t *link;
    int32_t net; /* -1 not known, 0 off, 1 the local network */
    /* PM2 */
    pollen_peer_t peer[POLLEN_PEERS];
    uint32_t npeers, nearby, left_out;
    /* PM3 */
    pollen_offer_t offer[POLLEN_OFFERS];
    uint32_t noffers;
    int64_t next_number;
    uint64_t share_compute, share_storage; /* net.offers, as last read */
    uint32_t wants;                        /* open wants, as last read */
    /* PM4 */
    int64_t avail; /* -1 until measured */
    int64_t spare;
    int64_t wanted; /* bytes of the last want zxvd took */
    /* PM5 */
    int64_t freights, packets, left, progress_x100;
    bool carried;
    uint32_t fault;
    char notice[POLLEN_NOTICE + 1];
    /* working space */
    uint8_t bytes[POLLEN_CHUNK];
    char b64[4 * (POLLEN_CHUNK / 3u) + 8u];
    char hex[2u * POLLEN_CHUNK + 1u];
    char packets_hex[2u * 21u * POLLEN_ROWS + 1u];
    char header_hex[129];
} pollen_t;

void pollen_init(pollen_t *p, zxm_link_t *link);

/* PM1: the local network on, or networking off */
int pollen_lan(pollen_t *p, bool on);
/* PM2: the peers, afresh; the last list stays when the answer does not parse */
int pollen_peers(pollen_t *p);
/* PM3: what zxvd shares and wants now */
int pollen_read_offers(pollen_t *p);
/* PM3: draft an offer of n files of the Hoard; *index its place */
int pollen_offer(pollen_t *p, const char *const *paths, uint32_t n, int64_t now_ms,
                 uint32_t *index);
/* PM3: the owner's word on a drafted offer; yes opens it */
int pollen_approve(pollen_t *p, uint32_t index, bool yes, int64_t now_ms);
/* PM3: take an open offer back */
int pollen_withdraw(pollen_t *p, uint32_t index, int64_t now_ms);
/* how many offers are in a state (witness.kind_count over their states) */
int64_t pollen_count(const pollen_t *p, int64_t state);

/* PM4: the receiver's room */
int pollen_measure(pollen_t *p);                  /* sys.disk, here */
int pollen_set_avail(pollen_t *p, int64_t avail); /* read off the receiver */
int pollen_set_spare(pollen_t *p, int64_t spare);
bool pollen_fits(const pollen_t *p, int64_t need); /* ferry.target_fits */
int pollen_want(pollen_t *p, int64_t bytes, bool approved);

/* PM5: carry file j of open offer i to `to` (a new file of the Hoard) at k
 * data rows a freight */
int pollen_carry(pollen_t *p, uint32_t i, uint32_t j, const char *to, int64_t k);

/* PM6: copy a file into another space; *ticket is 0 when done at once,
 * else the held crossing's ticket for pollen_cross_confirm */
int pollen_cross(pollen_t *p, const char *from_space, const char *path, const char *to_space,
                 const char *to, uint32_t *ticket);
int pollen_cross_confirm(pollen_t *p, uint32_t ticket);

#endif
