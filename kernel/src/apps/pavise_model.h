/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* pavise_model.h — Pavise, the security perimeter, as a model (zxm.h): the
 * firewall's paths, the apps' permissions and the keys under one policy,
 * each decision filed in an access ledger. No drawing, no allocation,
 * integers only. The rules it runs are pavise_rules.h (PR1-PR7).
 *
 * zxvd methods used: features.get, sys.net, net.peers, devmesh.devices,
 * passkeys.list, journal.query and journal.verify. zxvd has no method that
 * writes a firewall rule, grants a capability, lists the installed apps
 * with their signatures or posts a notice: the grants and policies are the
 * model's own, the view hands it each app (pavise_add_app), and a notice
 * is composed for the view (notice).
 *
 *   PM1 A REFRESH reads features.get (the switches), sys.net (how many
 *       interfaces are up with a public address), net.peers (each peer's
 *       address scoped, hedgerow.addr_scope, and counted by how far it is),
 *       devmesh.devices (the signed roster) and passkeys.list (the keys:
 *       their public ids, whether revoked, when registered; never a key's
 *       bytes). A part zxvd refuses (a method it does not serve, a feature
 *       that is off, a mesh it does not link) is unknown and the rest goes
 *       on; a reply out of bounds or a broken link leaves everything as it
 *       was.
 *   PM2 THE APPS come from the view: a number 1..9999, the capabilities
 *       its manifest needs (a mask by zxapp.c R3's index), whether its
 *       recipe is signed, how widely its listener binds (HR1, 0..2) and the
 *       feature switch its network path rides on ("" for none). The owner
 *       grants (gatehouse.use_ok's granted) on a confirmed word; a revoke
 *       is at once. The owner binds an app to a key (a passkey's id) and
 *       sets a key's lifetime in days (default PAVISE_LIFE_DAYS), each on a
 *       confirmed word.
 *   PM3 THE NETWORK MODE (HR1, 0 off, 1 lan, 2 online) is the one Hedgerow
 *       last set: zxvd has no method that reads it, so the view tells the
 *       model (pavise_set_mode). It starts off, as zxvd does.
 *   PM4 POLICIES are declared rules (PR6) from sources 1..99, each allow or
 *       deny for one app and capability, kept on the owner's word; one
 *       rule per source, app and capability, a newer one replacing it. The
 *       clause for a use is every source's word joined (PR2); the owner's
 *       ruling (1 allow, 2 deny, 0 none) settles a BOTH.
 *   PM5 A DECISION (pavise_decide) checks its inputs, then asks the rules:
 *       gatehouse.use_ok of the app's flags; hedgerow.path_open for "net"
 *       and "peers" (the app's switch on, the mode and its bind against how
 *       far the asker is; any other capability opens no path); the key
 *       valid and lockbox.days_left of its expiry; the clause. The verdict
 *       is PR2's: TRUE only when every part holds; keys unknown make it
 *       NEITHER, policies that disagree BOTH: denied until the owner rules.
 *       Every decision is filed (PM6), the verdict with it.
 *   PM6 THE ACCESS LEDGER holds the newest PAVISE_LEDGER decisions; each
 *       number is tallystick.next_doc_number of the one before (never
 *       reused, never wrapped: past 999999999 a decision is refused), each
 *       entry fits PR3 and links to the one before (witness.chain_ok).
 *   PM7 MEASURES. The exposure score is PR4 over the apps' network paths
 *       open from beyond (a path reachable from far 2, with a public
 *       interface up or sys.net unknown), every key not revoked and every
 *       app's signature; the permission matrix's app totals are PR5 and its
 *       capability totals gatehouse.holders; the trust tree (PR7) is each
 *       active device of the roster, each app under it and the app's key,
 *       trusted when the device is active and the key valid.
 *   PM8 HISTORY. pavise_history reads the journal's newest entries
 *       (journal.query), each fitting witness.audit_ok and newer than the
 *       next, and its hash chain (journal.verify): NEITHER with no journal,
 *       TRUE when both hold, FALSE when one does not. */
#ifndef PAVISE_MODEL_H
#define PAVISE_MODEL_H

#include "pavise_rules.h"
#include "zxm.h"

#define PAVISE_APPS      32u
#define PAVISE_KEYS      16u
#define PAVISE_POLICIES  64u
#define PAVISE_RULINGS   32u
#define PAVISE_LEDGER    64u
#define PAVISE_DEVICES   16u
#define PAVISE_FEATURES  48u
#define PAVISE_NODES     (PAVISE_DEVICES * PAVISE_APPS)
#define PAVISE_FEAT_MAX  23u  /* a feature's name (features.get) */
#define PAVISE_ID_HEX    32u  /* a passkey's or a device's id, hex digits */
#define PAVISE_NOTICE    240u /* bytes of the notice */
#define PAVISE_LIFE_DAYS 365  /* PM2: a key's lifetime unless the owner sets one */
#define PAVISE_LIFE_MAX  36525
#define PAVISE_HISTORY   100u /* journal entries read at most (PM8) */

/* zxapp.c R3's indexes of the capabilities that open a network path */
#define PAVISE_CAP_NET   0
#define PAVISE_CAP_PEERS 1

typedef struct {
    int64_t number;
    uint32_t needs, granted, revoked; /* masks by capability index */
    int64_t signd;                    /* 1 signed, 0 not */
    int64_t bind;                     /* HR1 */
    int32_t key;                      /* index into key[], -1 none */
    char feature[PAVISE_FEAT_MAX + 1];
} pavise_app_t;

typedef struct {
    char id[PAVISE_ID_HEX + 1]; /* passkeys.list's cred_id: public */
    bool revoked;
    int64_t registered; /* seconds since 1970 */
    int64_t life_days;
} pavise_key_t;

typedef struct {
    int64_t source, app, cap, effect; /* PR6 */
} pavise_policy_t;

typedef struct {
    int64_t app, cap, ruling; /* PM4: 1 allow, 2 deny */
} pavise_ruling_t;

typedef struct {
    int64_t decision, prev, app, cap, verdict, stamp; /* PR3, seconds */
} pavise_entry_t;

typedef struct {
    char id[PAVISE_ID_HEX + 1];
    bool active, self;
    int64_t since;
} pavise_device_t;

typedef struct {
    char name[PAVISE_FEAT_MAX + 1];
    bool enabled;
} pavise_feature_t;

typedef struct {
    int64_t device, app, key, trust, since; /* PR7 */
} pavise_node_t;

typedef struct {
    zxm_link_t *link;
    int64_t mode; /* PM3 */
    pavise_app_t app[PAVISE_APPS];
    uint32_t napps;
    bool keys_known;
    pavise_key_t key[PAVISE_KEYS];
    uint32_t nkeys;
    pavise_policy_t policy[PAVISE_POLICIES];
    uint32_t npolicies;
    pavise_ruling_t ruling[PAVISE_RULINGS];
    uint32_t nrulings;
    /* PM6: a ring of the newest decisions */
    pavise_entry_t entry[PAVISE_LEDGER];
    uint32_t head, count;
    int64_t last; /* the newest decision number, 0 before the first */
    bool devices_known;
    pavise_device_t device[PAVISE_DEVICES];
    uint32_t ndevices;
    bool features_known;
    pavise_feature_t feature[PAVISE_FEATURES];
    uint32_t nfeatures;
    bool net_known;
    uint32_t public_ifaces;
    bool peers_known;
    uint32_t peers_far[3], peers_unscoped;
    pavise_node_t node[PAVISE_NODES];
    uint32_t nnodes;
    int64_t history; /* entries PM8 read */
    char notice[PAVISE_NOTICE];
} pavise_t;

void pavise_init(pavise_t *p, zxm_link_t *link);
/* PM1 */
int pavise_refresh(pavise_t *p);
/* PM2: ZXM_E_ARG for a value off its range or a number already known */
int pavise_add_app(pavise_t *p, int64_t number, uint32_t needs, int64_t signd, int64_t bind,
                   const char *feature);
int pavise_grant(pavise_t *p, int64_t app, int64_t cap, bool on, bool confirmed);
int pavise_bind_key(pavise_t *p, int64_t app, const char *key_id, bool confirmed);
int pavise_key_life(pavise_t *p, const char *key_id, int64_t days, bool confirmed);
/* PM3 */
int pavise_set_mode(pavise_t *p, int64_t mode);
/* PM4: effect 1 allow, 0 deny */
int pavise_policy(pavise_t *p, int64_t source, int64_t app, int64_t cap, int64_t effect,
                  bool confirmed);
int pavise_rule(pavise_t *p, int64_t app, int64_t cap, int64_t ruling, bool confirmed);
lpres_state_t pavise_clause_now(const pavise_t *p, int64_t app, int64_t cap);
/* PM5: far 0..2 (HR1), now in seconds since 1970; *verdict the decision */
int pavise_decide(pavise_t *p, int64_t app, int64_t cap, int64_t far, int64_t now,
                  lpres_state_t *verdict);
/* PM6: the i-th newest decision (0 the newest), or NULL */
const pavise_entry_t *pavise_entry(const pavise_t *p, uint32_t i);
bool pavise_ledger_intact(const pavise_t *p);
/* PM7 */
int pavise_exposure_now(const pavise_t *p, int64_t now, int64_t *score);
int64_t pavise_app_caps(const pavise_t *p, int64_t app);
int64_t pavise_cap_holders(const pavise_t *p, int64_t cap);
uint32_t pavise_trust_tree(pavise_t *p);
/* HR4/HR5 of a dotted IPv4 address ("a.b.c.d" or "a.b.c.d:port"):
 * how far the asker is, or PAVISE_REFUSED */
int64_t pavise_far_of(const char *addr);
/* PM8 */
lpres_state_t pavise_history(pavise_t *p);
int32_t pavise_find_app(const pavise_t *p, int64_t number);

#endif
