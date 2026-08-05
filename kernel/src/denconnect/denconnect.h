/* denconnect.h — sovereign-node governance for the Den Connect server
 *
 * WHAT THIS IS
 * ------------
 * The old Den Connect/Hotline servers had the right social model: each server
 * was its own small world, and the person running it set the rules — which
 * accounts existed, what each could do, who was a guest, who was banned.
 * ZXV keeps that spirit and modernises it.
 *
 * The fixed four-role scheme in p2p_denconnect.c (guest/user/contrib/admin with
 * hard-coded permissions) is replaced here by OPERATOR-DEFINED privilege
 * sets. There is no privilege the OS imposes on a node: the operator of each
 * server decides everything, so two servers can run by completely different
 * rules and both are legitimate. Everybody sets the rules of their own node.
 *
 * MODERN, NATIVE
 * --------------
 * Identity is an Ed25519 public key (the kernel already verifies these), not
 * a plaintext account name, so accounts are portable and unforgeable. A
 * server has a visibility tier — PUBLIC, PRIVATE, or SELECT — matching the
 * per-file access tiers of the P2P layer, so a node can be an open commons,
 * an operator-only private space, or an invite list. And the check is a
 * single privilege bitmask, so the UI can present exactly the classic
 * Den Connect access checkboxes.
 *
 * SOVEREIGN, BUT NOT LAWLESS
 * --------------------------
 * The operator is sovereign ON THEIR NODE. That power stops at the node
 * boundary: it does not reach the kernel, other servers, or a user's own
 * device. And privilege escalation is structurally prevented — only an
 * identity that already holds SET_ACCESS may change the rules or grant
 * privileges, so a guest cannot quietly make themselves an admin.
 *
 * Freestanding: integer only, no libc, no allocation. Identity comparison is
 * a fixed 32-byte compare; no crypto is needed for the governance logic.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (ZXV Den Connect governance slice)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#ifndef ZXV_DENCONNECT_H
#define ZXV_DENCONNECT_H

#include <stdint.h>
#include <stdbool.h>

#define DEN_KEY_LEN       32u      /* Ed25519 public key */
#define DEN_NAME_LEN      32u
#define DEN_MAX_ACCOUNTS  64u
#define DEN_MAX_BANNED    64u

/* The classic Den Connect/Hotline access bits — the operator ticks these per
 * account, and the client shows them as the familiar checkbox list. */
#define DEN_READ_CHAT     (1u << 0)
#define DEN_SEND_CHAT     (1u << 1)
#define DEN_READ_NEWS     (1u << 2)
#define DEN_POST_NEWS     (1u << 3)
#define DEN_DOWNLOAD      (1u << 4)
#define DEN_UPLOAD        (1u << 5)
#define DEN_DELETE_FILE   (1u << 6)
#define DEN_CREATE_FOLDER (1u << 7)
#define DEN_MOVE_FILE     (1u << 8)
#define DEN_READ_USERS    (1u << 9)   /* see the user list */
#define DEN_SEND_PM       (1u << 10)  /* private message */
#define DEN_BROADCAST     (1u << 11)
#define DEN_GET_INFO      (1u << 12)  /* see another user's info */
#define DEN_KICK          (1u << 13)
#define DEN_BAN           (1u << 14)
#define DEN_CREATE_ACCOUNT (1u << 15)
#define DEN_SET_ACCESS    (1u << 16)  /* change the rules / grant privileges */
#define DEN_ALL           0x0001FFFFu /* every bit above — the operator */

/* Who may even connect to this node. */
typedef enum {
    DEN_PUBLIC = 0,   /* anyone may connect as a guest         */
    DEN_PRIVATE,      /* only the operator                     */
    DEN_SELECT        /* only named accounts (an invite list)  */
} den_visibility_t;

typedef struct {
    uint8_t  key[DEN_KEY_LEN];
    char     name[DEN_NAME_LEN];
    uint32_t privileges;
    bool     present;
} den_account_t;

#define DEN_HASH_LEN   32u
#define DEN_MAX_AUDIT  128u

/* Every rule change is recorded so a sovereign operator is also ACCOUNTABLE:
 * you can prove what a moderator did and in what order, and any edit to that
 * history is detectable. */
typedef enum {
    DEN_ACT_SET_GUEST   = 1,
    DEN_ACT_SET_VIS     = 2,
    DEN_ACT_SET_ACCOUNT = 3,
    DEN_ACT_BAN         = 4,
    DEN_ACT_UNBAN       = 5
} den_action_t;

typedef struct {
    uint8_t  actor[DEN_KEY_LEN];
    uint8_t  action;                    /* den_action_t */
    uint8_t  target[DEN_KEY_LEN];
    uint32_t param;                     /* privileges / visibility, per action */
    uint8_t  head[DEN_HASH_LEN];        /* running chain head after this action */
} den_audit_entry_t;

typedef struct {
    uint8_t          operator_key[DEN_KEY_LEN];
    uint32_t         guest_privileges;    /* what an unregistered guest may do */
    bool             registration_open;   /* may a guest self-register?        */
    den_visibility_t visibility;
    den_account_t   account[DEN_MAX_ACCOUNTS];
    uint32_t         n_accounts;
    uint8_t          banned[DEN_MAX_BANNED][DEN_KEY_LEN];
    uint32_t         n_banned;
    /* tamper-evident moderation log */
    den_audit_entry_t audit[DEN_MAX_AUDIT];
    uint32_t          n_audit;
    uint8_t           audit_head[DEN_HASH_LEN];
} den_server_t;

/* Found a node. The operator holds ALL privileges; sensible default guest
 * privileges (read/send chat, read news, download) are set and may be
 * changed. Returns false on bad args. */
bool den_init(den_server_t *s, const uint8_t operator_key[DEN_KEY_LEN]);

/* The effective privileges of an identity on this node: the operator gets
 * everything; a banned identity gets nothing; a named account gets its set;
 * anyone else gets the guest privileges. */
uint32_t den_privileges(const den_server_t *s, const uint8_t key[DEN_KEY_LEN]);

/* May `key` perform an action requiring `priv`? */
bool den_can(const den_server_t *s, const uint8_t key[DEN_KEY_LEN], uint32_t priv);

/* May `key` connect at all, given the node's visibility tier? */
bool den_may_connect(const den_server_t *s, const uint8_t key[DEN_KEY_LEN]);

/* ---- operator actions (each requires the actor to hold the right bit) ---- */

/* Set what guests may do. Requires SET_ACCESS. */
bool den_set_guest_privileges(den_server_t *s, const uint8_t actor[DEN_KEY_LEN],
                               uint32_t privileges);
/* Set the node's visibility tier. Requires SET_ACCESS. */
bool den_set_visibility(den_server_t *s, const uint8_t actor[DEN_KEY_LEN],
                         den_visibility_t v);
/* Create or update an account with an operator-chosen privilege set.
 * Requires CREATE_ACCOUNT; granting SET_ACCESS additionally requires the
 * actor to already hold SET_ACCESS (no minting an admin from below). */
bool den_set_account(den_server_t *s, const uint8_t actor[DEN_KEY_LEN],
                      const uint8_t key[DEN_KEY_LEN], const char *name,
                      uint32_t privileges);
/* Ban an identity: it loses all access until unbanned. Requires BAN.
 * The operator cannot be banned. */
bool den_ban(den_server_t *s, const uint8_t actor[DEN_KEY_LEN],
              const uint8_t key[DEN_KEY_LEN]);
bool den_unban(den_server_t *s, const uint8_t actor[DEN_KEY_LEN],
                const uint8_t key[DEN_KEY_LEN]);
bool den_is_banned(const den_server_t *s, const uint8_t key[DEN_KEY_LEN]);
bool den_is_operator(const den_server_t *s, const uint8_t key[DEN_KEY_LEN]);

/* ---- accountability: the tamper-evident moderation log ---- */
const uint8_t *den_audit_head(const den_server_t *s);
uint32_t       den_audit_length(const den_server_t *s);
/* Recompute the audit chain; true iff no entry has been altered. */
bool           den_audit_verify(const den_server_t *s);

/* ============================================================
 * A DEN FLEET — one person runs a SERIES of dens at once
 * ============================================================
 * A den is a modular server configuration. A person operates several
 * concurrently — an open public lobby, a private family den, a select work
 * den — and they are ISOLATED from one another: membership, bans, rules, and
 * the audit log of one den never leak into another. That isolation is the
 * privacy model: knowing someone in your work den tells a stranger nothing
 * about your family den, because the dens share no membership state. */
#define DEN_MAX_FLEET  8u

typedef struct {
    uint8_t      operator_key[DEN_KEY_LEN];
    den_server_t den[DEN_MAX_FLEET];
    char         label[DEN_MAX_FLEET][DEN_NAME_LEN];
    bool         active[DEN_MAX_FLEET];
    uint32_t     n;
} den_fleet_t;

void den_fleet_init(den_fleet_t *f, const uint8_t operator_key[DEN_KEY_LEN]);
/* Found a new den in the fleet under `label`; returns its index or -1. */
int32_t       den_fleet_found(den_fleet_t *f, const char *label);
den_server_t *den_fleet_get(den_fleet_t *f, uint32_t idx);
int32_t       den_fleet_find(const den_fleet_t *f, const char *label);
/* Stop running a den (its config is cleared). Returns false if idx invalid. */
bool          den_fleet_close(den_fleet_t *f, uint32_t idx);
uint32_t      den_fleet_count(const den_fleet_t *f);

#endif /* ZXV_DENCONNECT_H */
