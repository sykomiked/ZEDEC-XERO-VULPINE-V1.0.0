/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* trellis_model.h — Trellis, peers and mesh links, as a model (zxm.h): the
 * owner's device mesh, pairing and revoking a device, the paired phones,
 * and the peer network. The rules are trellis_rules.h.
 *
 * zxvd methods used: devmesh.status, devmesh.devices, devmesh.invite,
 * devmesh.join, devmesh.confirm_sas, devmesh.revoke, phone.list,
 * phone.pair, phone.revoke, net.peers and net.command. No drawing, no
 * allocation, integers only.
 *
 *   TM1 THE MESH: devmesh.status (joined, this device's id, whether it is
 *       an admin, the roster version, the pairing waiting for its six
 *       digits) and devmesh.devices (the signed roster, up to
 *       TRELLIS_DEVICES rows, each active or revoked). An answer in any
 *       other form is not zxvd's (ZXM_E_PROTOCOL) and nothing of it is
 *       kept.
 *   TM2 PAIRING: an invitation (devmesh.invite) keeps its short code and
 *       its life (expires_in_ms, 1..TRELLIS_INVITE_TTL_MS), from the time
 *       the caller gives; its minutes left are TL2. Joining takes a short
 *       code (devmesh.join, five and five of 0-9 A-Z). The six digits are
 *       the owner's to compare: trellis_confirm_sas sends their word on
 *       the waiting pairing, yes or no.
 *   TM3 PHONES: phone.list's phones (id, name, allow_post, connections),
 *       up to TRELLIS_PHONES. Pairing one (phone.pair) hands its link back
 *       to the caller once, to show as a QR code; the link holds the
 *       phone's secret key and is never kept here. Whether a phone may
 *       post is TL5 (gatehouse.use_ok).
 *   TM4 REVOKING: a device (TL1, from the roster: adminsleft is the active
 *       admins less the device when it is one) or a phone (paired, not yet
 *       revoked) is asked for first; refused, it says why, the first that
 *       applies: no admin session, this device, already revoked, the last
 *       admin. Allowed, it waits for the owner's word twice: the first yes
 *       asks again, the second sends devmesh.revoke or phone.revoke; a no
 *       at either drops it. Nothing is sent before the second yes.
 *   TM5 PEERS: net.peers' contacts (up to TRELLIS_PEERS kept, the rest
 *       counted) and the network's mode by net.command: off, lan or
 *       online, nothing else.
 *   TM6 PEERS PER STATE: the roster's devices (active or revoked), the
 *       phones (active, or revoked once revoked here) and a waiting
 *       pairing (waiting) counted by state with TL3.
 *   TM7 THE RECORDS: every roster row is checked to fit ^ZXTRELLIS (TL4)
 *       before the roster is kept. */
#ifndef TRELLIS_MODEL_H
#define TRELLIS_MODEL_H

#include "trellis_rules.h"
#include "zxm.h"

#define TRELLIS_ID      32u  /* a device or phone id: 16 bytes as hex */
#define TRELLIS_NODE    64u  /* a peer's NodeID: 32 bytes as hex */
#define TRELLIS_NAME    32u  /* a device's or phone's name */
#define TRELLIS_TEXT    63u  /* an address, a code, zxvd's answer to a command */
#define TRELLIS_PHONES  16u  /* phones kept */
#define TRELLIS_PEERS   64u  /* contacts kept */
#define TRELLIS_SUMMARY 383u /* the notice */

/* a device's role in the mesh (TL4's role) */
enum { TRELLIS_ROLE_HOME = 1, TRELLIS_ROLE_THIN = 2, TRELLIS_ROLE_STANDALONE = 3 };

/* the network's mode (TM5) */
enum { TRELLIS_NET_OFF = 0, TRELLIS_NET_LAN = 1, TRELLIS_NET_ONLINE = 2 };

/* what waits to be revoked (TM4) */
enum { TRELLIS_NOTHING = 0, TRELLIS_DEVICE = 1, TRELLIS_PHONE = 2 };

typedef struct {
    char id[TRELLIS_ID + 1];
    char name[TRELLIS_NAME + 1];
    int64_t role, admin, held, state, since;
    bool self, up;
} trellis_device_t;

typedef struct {
    char id[TRELLIS_ID + 1];
    char name[TRELLIS_NAME + 1];
    bool allow_post;
    bool revoked; /* revoked here */
    int64_t connected;
} trellis_phone_t;

typedef struct {
    char id[TRELLIS_NODE + 1];
    char addr[TRELLIS_TEXT + 1];
    int64_t last_seen_ms;
    bool member, in_book;
} trellis_peer_t;

typedef struct {
    zxm_link_t *link;
    /* TM1 */
    bool known, joined, admin;
    char self[TRELLIS_ID + 1];
    int64_t version;
    bool pairing;
    char pair_device[TRELLIS_ID + 1];
    char sas[7];
    bool roster_known;
    uint32_t devices;
    trellis_device_t device[TRELLIS_DEVICES];
    /* TM2 */
    bool invited;
    char code[TRELLIS_TEXT + 1];
    int64_t issued_ms, ttl_ms;
    /* TM3 */
    bool phones_known, phone_link_on;
    uint32_t phones;
    trellis_phone_t phone[TRELLIS_PHONES];
    /* TM4 */
    int32_t revoke_kind; /* TRELLIS_NOTHING, TRELLIS_DEVICE, TRELLIS_PHONE */
    uint32_t revoke_at;  /* its index */
    uint32_t revoke_yes; /* the owner's yes so far: 0 or 1 */
    /* TM5 */
    uint32_t peers, peers_kept;
    trellis_peer_t peer[TRELLIS_PEERS];
    int64_t net_mode; /* -1 until set here */
    char net_answer[TRELLIS_TEXT + 1];
    char notice[TRELLIS_SUMMARY + 1];
} trellis_t;

void trellis_init(trellis_t *t, zxm_link_t *link);
/* TM1: devmesh.status; then devmesh.devices. */
int trellis_read_status(trellis_t *t);
int trellis_read_devices(trellis_t *t);
/* the roster index of a device id, or -1 */
int32_t trellis_find_device(const trellis_t *t, const char *id);
/* TM2: invite a device of `role` (TRELLIS_ROLE_*), at now_ms. */
int trellis_invite(trellis_t *t, int64_t role, bool held, bool admin, int64_t now_ms);
/* TL2 on the last invitation at now_ms; TRELLIS_REFUSED when none. */
int64_t trellis_invite_left(const trellis_t *t, int64_t now_ms);
/* TM2: join with a short code (XXXXX-XXXXX); ZXM_E_ARG on a code of another form. */
int trellis_join(trellis_t *t, const char *code);
/* TM2: the owner's word on the six digits; ZXM_E_STATE when no pairing waits. */
int trellis_confirm_sas(trellis_t *t, bool match);
/* TM3: phone.list; phone.pair (the link copied to link_out once, never kept). */
int trellis_read_phones(trellis_t *t);
int trellis_pair_phone(trellis_t *t, const char *name, bool allow_post, char *link_out,
                       uint32_t cap);
/* TL5 on phone i for an action that asks to post (asked 1) or not (0). */
bool trellis_phone_may_post(const trellis_t *t, uint32_t i, int64_t asked);
/* TL1 on roster row i from what the model knows. */
bool trellis_device_revocable(const trellis_t *t, uint32_t i);
/* TM4: ask to revoke roster row i or phone i; ZXM_OK with the outcome in
 * the notice (and revoke_kind set when it waits), ZXM_E_STATE while the
 * mesh or the phones are not known or another revocation waits, ZXM_E_ARG
 * on an index past the list. */
int trellis_revoke_device(trellis_t *t, uint32_t i);
int trellis_revoke_phone(trellis_t *t, uint32_t i);
/* TM4: the owner's word; ZXM_E_STATE when nothing waits. */
int trellis_revoke_confirm(trellis_t *t, bool yes);
/* TM5: net.peers; net.command with TRELLIS_NET_*. */
int trellis_read_peers(trellis_t *t);
int trellis_net(trellis_t *t, int64_t mode);
/* TM6: TL3 over the mesh's peers; TRELLIS_REFUSED when there are none. */
int64_t trellis_count(const trellis_t *t, int64_t state);
/* TM7: TL4 on roster row i (false past the roster). */
bool trellis_record_ok(const trellis_t *t, uint32_t i);

#endif
