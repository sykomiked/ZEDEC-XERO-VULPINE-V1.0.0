/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* updraft_model.h — Updraft, signal station, as a model (zxm.h). It pairs
 * relay stations into the owner's mesh, relays messages through Aerie's
 * model and watches each peer's link. The rules are updraft_rules.h; the
 * parts' are Aerie's (aerie_rules.h, through aerie_model.h), Trellis's
 * (trellis_rules.h) and Commons's (commons_rules.h).
 *
 * zxvd methods used: devmesh.invite, devmesh.join, devmesh.confirm_sas and
 * net.peers here; ehop.bands, ehop.seal, ehop.open, harmonic.encode and
 * harmonic.decode through Aerie's model (aerie_t, composed). zxvd has no
 * method that drives radio hardware, so nothing here transmits: the
 * operator keys the transmitter, and only within their licence. Messages
 * only, never money. No drawing, no allocation, integers only.
 *
 *   UM1 THE LICENCE. The operator gives each radio band's licensed maximum
 *       power (updraft_set_licence, dBm 0..UPDRAFT_POWER_MAX) on their
 *       confirmed word; none until they do. updraft_may_transmit answers
 *       UR2 with headroom = licmax - power: a band with no licence, or a
 *       power past it, is never allowed. Advice only.
 *   UM2 PAIRING a relay station (devmesh.invite as a thin device, not
 *       held and never an admin: a relay can neither confirm money actions
 *       nor revoke a device), joining with a short code (devmesh.join) and
 *       the owner's word on the six digits (devmesh.confirm_sas). Each one
 *       is checked, then takes the owner's confirmed word (ZXM_E_STATE and
 *       nothing sent without it), then is sent.
 *   UM3 LINKS. updraft_set_link keeps a directed link (src to dst, stations
 *       0..UPDRAFT_STATION_MAX, 0 this one, never a station to itself)
 *       with its margin (UR1); refused parts keep nothing. Each link's
 *       state is UR3 over every link kept.
 *   UM4 RELAYING. updraft_relay goes out on the confirmed word of the
 *       owner: on a private bearer sealed by Aerie (aerie_seal, which
 *       checks the band and the log record), on an amateur bearer in the
 *       clear (aerie_ham_compose: printable text, the call sign in the
 *       clear, never sealed). aerie.ham_clear_ok is checked first, so a
 *       sealed message asked for on amateur radio is refused
 *       (ZXM_E_STATE) before anything is asked; a private link carries
 *       sealed frames only. A relayed message is counted.
 *   UM5 HEARING. updraft_hear opens a sealed frame (aerie_open, which
 *       keeps no replay) and files what this relay heard of it
 *       (updraft_report): the frame, the relay, its parent, the hops, the
 *       payload's Fletcher checksum, each fitting ^ZXUPDRAFT (UR4) before
 *       it is kept. A frame keeps up to UPDRAFT_RELAYS reports; the
 *       oldest frame goes when UPDRAFT_FRAMES are kept and a new one
 *       comes.
 *   UM6 BOTH. Every report re-joins its frame (UR5). Two relays hearing
 *       two payloads make the frame BOTH: it is kept and never posted or
 *       passed on until a third relay or its checksum
 *       (updraft_frame_checksum) settles it.
 *   UM7 THE WATCH. updraft_watch reads net.peers whole: each peer's
 *       NodeID and when it was last heard; a peer heard in the last
 *       UPDRAFT_STALE_MS is live (Trellis's active), else stale (waiting),
 *       counted with trellis.peers_in_state. Frames heard a second since
 *       the last watch is sounding.rate (UR6) over the frames counter.
 *   UM8 THE BULLETIN. A settled frame (TRUE) may be posted to the
 *       station's room when commons.room_post_ok holds for it (member,
 *       joined, enabled); a BOTH, FALSE or NEITHER frame never is.
 *       Updraft posts nothing itself: the view posts through Commons. */
#ifndef UPDRAFT_MODEL_H
#define UPDRAFT_MODEL_H

#include "aerie_model.h"
#include "updraft_rules.h"
#include "zxm.h"

#define UPDRAFT_PEERS         32u
#define UPDRAFT_LINKS         64u
#define UPDRAFT_FRAMES        16u
#define UPDRAFT_RELAYS        8u
#define UPDRAFT_NODE          64u                      /* a NodeID: 32 bytes as hex */
#define UPDRAFT_DEVICE        32u                      /* a device id: 16 bytes as hex */
#define UPDRAFT_CODE          11u                      /* XXXXX-XXXXX */
#define UPDRAFT_STALE_MS      INT64_C(300000)          /* five minutes */
#define UPDRAFT_INVITE_TTL_MS INT64_C(600000)          /* devmesh.h DM_INVITE_TTL_MS */
#define UPDRAFT_MS_MAX        INT64_C(253402300799999) /* the last ms of the year 9999 */
#define UPDRAFT_NOTICE        383u

typedef struct {
    char id[UPDRAFT_NODE + 1];
    int64_t last_seen_ms;
    bool member;
    int64_t state; /* TRELLIS_ACTIVE live, TRELLIS_WAITING stale */
} updraft_peer_t;

typedef struct {
    int64_t src, dst, power, step, rf, rxfloor, margin;
} updraft_link_t;

typedef struct {
    int64_t relay, parent, hops, payload; /* payload: the Fletcher checksum heard */
} updraft_report_t;

typedef struct {
    int64_t frame;
    updraft_report_t rep[UPDRAFT_RELAYS];
    uint32_t nrep;
    bool has_sum;
    int64_t sum;
    lpres_state_t state; /* UR5 */
    int64_t settled;     /* the payload it settled to, -1 while it has not */
} updraft_frame_t;

typedef struct {
    zxm_link_t *link;
    aerie_t radio; /* Aerie's model, composed: ehop.* and harmonic.* */
    /* UM1 */
    bool licence_set[UPDRAFT_RF];
    int64_t licmax[UPDRAFT_RF];
    /* UM2 */
    bool invited;
    char code[UPDRAFT_CODE + 1];
    int64_t issued_ms, ttl_ms;
    bool pairing; /* devmesh.join answered that a pairing waits */
    int64_t devices;
    /* UM3 */
    updraft_link_t links[UPDRAFT_LINKS];
    uint32_t nlinks;
    /* UM4, UM5, UM6 */
    uint64_t relayed, heard;
    updraft_frame_t frames[UPDRAFT_FRAMES];
    uint32_t nframes;
    /* UM7 */
    updraft_peer_t peers[UPDRAFT_PEERS];
    uint32_t npeers, peers_seen; /* kept, and listed by zxvd */
    int64_t live, stale;
    int64_t watch_ms; /* the last watch, -1 before the first */
    uint64_t heard_was;
    int64_t frame_rate; /* frames heard a second, -1 until two watches */
    /* UM8 */
    int64_t room_member, room_joined, room_enabled;
    char notice[UPDRAFT_NOTICE + 1];
} updraft_t;

void updraft_init(updraft_t *u, zxm_link_t *link);

/* UM1: ZXM_E_ARG for a band or power outside its range, ZXM_E_STATE
 * without the operator's confirmed word. */
int updraft_set_licence(updraft_t *u, int64_t rf, int64_t licmax, bool owner_confirmed);
bool updraft_may_transmit(updraft_t *u, int64_t rf, int64_t power);

/* UM2 */
int updraft_invite(updraft_t *u, int64_t now_ms, bool owner_confirmed);
int updraft_join(updraft_t *u, const char *code, bool owner_confirmed);
int updraft_confirm_sas(updraft_t *u, const char *device, bool match, bool owner_confirmed);

/* UM3: the index of the link kept, or a ZXM_E_* below 0. */
int updraft_set_link(updraft_t *u, int64_t src, int64_t dst, int64_t power, int64_t step,
                     int64_t rf, int64_t rxfloor);
int64_t updraft_link_state_at(const updraft_t *u, uint32_t i);

/* UM4: ehop.bands through Aerie, then the bands the owner allows. */
int updraft_bands(updraft_t *u);
int updraft_allow_bands(updraft_t *u, uint32_t mask, bool owner_confirmed);
int updraft_relay(updraft_t *u, const aerie_chan_t *ch, bool sealed, const uint8_t *data,
                  uint32_t n, int64_t now_ms, bool owner_confirmed);

/* UM5: open frame_hex on ch and file it as frame `frame` heard by relay. */
int updraft_hear(updraft_t *u, const aerie_chan_t *ch, const char *frame_hex, int64_t now_ms,
                 int64_t frame, int64_t relay, int64_t parent, int64_t hops);
int updraft_report(updraft_t *u, int64_t frame, int64_t relay, int64_t parent, int64_t hops,
                   const uint8_t *data, uint32_t n);
int64_t updraft_fletcher(const uint8_t *data, uint32_t n);

/* UM6 */
int updraft_frame_checksum(updraft_t *u, int64_t frame, int64_t sum);
const updraft_frame_t *updraft_frame(const updraft_t *u, int64_t frame);

/* UM7 */
int updraft_watch(updraft_t *u, int64_t now_ms);

/* UM8 */
int updraft_set_room(updraft_t *u, int64_t member, int64_t joined, int64_t enabled);
bool updraft_bulletin_ok(const updraft_t *u, int64_t frame);

#endif
