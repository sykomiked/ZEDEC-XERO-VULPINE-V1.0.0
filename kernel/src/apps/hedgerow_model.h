/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* hedgerow_model.h — Hedgerow, the firewall, as a model (zxm.h): who may
 * reach this machine, and the owner's openings and closings. The rules are
 * hedgerow_rules.h (HR1-HR6), the same ones the app's Eve, KRON, C++, Rust
 * and MYRA blocks run; opening is a use the owner grants
 * (gatehouse_rules.h GR3). No drawing, no allocation, integers only.
 *
 *   HM1 THE PATHS (HEDGEROW_P_*): the network mode (lan, online), answering
 *       other nodes (peer.serve), the phone link (phone.link), the hosted
 *       spaces (pspace.list) and the IRC gateway (irc.status). Each has a
 *       switch (on or off) and a bind (HR1): the mesh and answering nodes
 *       listen on every interface; the phone link takes phones on private
 *       addresses only; the IRC gateway listens on 127.0.0.1 alone; a
 *       hosted space is reached through the mesh.
 *   HM2 OPENING IS ASKED FIRST. Every opening (a wider network mode,
 *       answering nodes, the phone link) marks the path asked and, without
 *       the owner's confirmed word, sends nothing and composes the ask
 *       (notice). With it, the path is granted, and it opens only when
 *       gatehouse_use_ok(asked, granted, revoked) says so. CLOSING IS AT
 *       ONCE: a narrower mode, serving stopped, the link off need no
 *       confirmation; a close revokes the grant, so the next opening is
 *       asked again. Every change zxvd took is filed as a rule record
 *       (HR6), the newest HEDGEROW_RULES kept.
 *   HM3 THE MODE: zxvd has no method that reads the network mode, and
 *       net.command answers only "networking is on" or "networking is
 *       off", so the model keeps the mode it last set. zxvd starts off,
 *       and so does the model.
 *   HM4 A REFRESH reads zxvd's clock (sys.info), then peer.status,
 *       phone.list, features.get, pspace.list, irc.status and net.peers.
 *       A method zxvd does not serve (-32601) leaves that part unknown and
 *       the rest goes on; any other failure, or a reply out of bounds,
 *       leaves everything shown as it was. Each peer's address (a.b.c.d)
 *       is scoped (HR4) and counted by how far it is (HR5).
 *   HM5 PEER SERVING has a price, VFV minor units per answer: money, shown
 *       and passed through as the owner set it, never chosen by Hedgerow. */
#ifndef HEDGEROW_MODEL_H
#define HEDGEROW_MODEL_H

#include "gatehouse_rules.h"
#include "hedgerow_rules.h"
#include "zxm.h"

#define HEDGEROW_PHONES     8u   /* phones shown */
#define HEDGEROW_FEATURES   48u  /* feature switches shown */
#define HEDGEROW_RULES      32u  /* rule records kept (HM2) */
#define HEDGEROW_NOTICE_MAX 320u /* bytes of the notice */
#define HEDGEROW_NAME_MAX   32u  /* a phone's name (phone.pair) */
#define HEDGEROW_FEAT_MAX   23u  /* a feature's name (features.get) */
#define HEDGEROW_ID_HEX     32u  /* a phone id, hex digits */
#define HEDGEROW_PRICE_MAX  999999999999999999ull

/* HR1: the mode ladder, as net.command takes it */
enum { HEDGEROW_OFF = 0, HEDGEROW_LAN = 1, HEDGEROW_ONLINE = 2 };

typedef struct {
    char id[HEDGEROW_ID_HEX + 1];
    char name[HEDGEROW_NAME_MAX + 1];
    bool allow_post; /* may move money and change the network */
    int64_t connected;
} hedgerow_phone_t;

typedef struct {
    char name[HEDGEROW_FEAT_MAX + 1];
    bool enabled, regulated;
    int64_t methods; /* methods its groups expose */
} hedgerow_feature_t;

typedef struct {
    int64_t seq, path, open, port, stamp; /* HR6 */
} hedgerow_rule_t;

typedef struct {
    zxm_link_t *link;
    int64_t now_ms; /* zxvd's clock at the last refresh or change */
    int64_t mode;   /* HM3 */
    /* answering other nodes (peer.status / peer.serve) */
    bool serve_known, serving;
    int64_t serve_port;
    uint64_t serve_price; /* HM5 */
    /* the phone link (phone.list / phone.link) */
    bool phone_known, phone_on;
    int64_t phone_port;
    hedgerow_phone_t phone[HEDGEROW_PHONES];
    uint32_t nphones;
    /* the switches (features.get) */
    bool features_known;
    hedgerow_feature_t feature[HEDGEROW_FEATURES];
    uint32_t nfeatures;
    uint32_t exposed; /* switches on */
    /* hosted spaces (pspace.list) */
    bool spaces_known, spaces_online;
    uint32_t nspaces, spaces_in;
    /* the IRC gateway (irc.status) */
    bool irc_known, irc_on, irc_listening;
    int64_t irc_port, irc_clients;
    /* the routing table's peers by how far they are (net.peers, HR5) */
    bool peers_known;
    uint32_t peers_far[3], peers_unscoped;
    /* HM2: per path (bit HEDGEROW_P_*), the gatehouse flags */
    uint32_t asked, granted, revoked;
    hedgerow_rule_t rule[HEDGEROW_RULES];
    uint32_t nrules;
    int64_t seq;
    char notice[HEDGEROW_NOTICE_MAX]; /* "" when nothing waits */
} hedgerow_t;

void hedgerow_init(hedgerow_t *h, zxm_link_t *link);
/* HM4 */
int hedgerow_refresh(hedgerow_t *h);
/* HM2/HM3: net.command. A wider mode is an opening: ZXM_E_STATE (asked,
 * nothing sent) unless confirmed. A narrower one or the same is at once.
 * ZXM_E_ARG for a mode off the ladder. */
int hedgerow_set_mode(hedgerow_t *h, int64_t mode, bool confirmed);
/* HM2/HM5: peer.serve. on is an opening (needs the network on and the
 * owner's word); off is at once. */
int hedgerow_serve(hedgerow_t *h, bool on, uint64_t price_minor, bool confirmed);
/* HM2: phone.link. on is an opening; bind_ip NULL listens on every
 * interface, else it must be a private IPv4 address (HR4 scope 2). port
 * 0..65535, -1 for zxvd's default. off is at once. */
int hedgerow_phone(hedgerow_t *h, bool on, int32_t port, const char *bind_ip, bool confirmed);
/* HR1-HR3 for one path (HEDGEROW_P_*) and an asker `far` away (0..2) */
bool hedgerow_reachable(const hedgerow_t *h, int64_t path, int64_t far);
/* HR4/HR5 of a dotted IPv4 address ("a.b.c.d", or "a.b.c.d:port"):
 * the scope, or HEDGEROW_REFUSED for text that is no address */
int64_t hedgerow_scope_of(const char *addr);
/* HR4 of an IPv6 address's 16 octets (HM4: an IPv4-mapped one is scoped
 * as IPv4, and c = d = 255 when ::/16 holds a nonzero octet between) */
int64_t hedgerow_scope6(const uint8_t octets[16]);
/* how many paths are reachable from an asker `far` away */
uint32_t hedgerow_open_count(const hedgerow_t *h, int64_t far);

#endif
