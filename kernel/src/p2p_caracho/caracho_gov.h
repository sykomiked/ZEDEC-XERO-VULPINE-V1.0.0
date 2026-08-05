/* caracho_gov.h — sovereign-node governance for the Carracho server
 *
 * WHAT THIS IS
 * ------------
 * The old Carracho/Hotline servers had the right social model: each server
 * was its own small world, and the person running it set the rules — which
 * accounts existed, what each could do, who was a guest, who was banned.
 * ZXV keeps that spirit and modernises it.
 *
 * The fixed four-role scheme in p2p_caracho.c (guest/user/contrib/admin with
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
 * Carracho access checkboxes.
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
 * Author: H.M. Michael-Laurence: Curzi (c)  (ZXV Carracho governance slice)
 * License: SEL-3.3
 */
#ifndef ZXV_CARACHO_GOV_H
#define ZXV_CARACHO_GOV_H

#include <stdint.h>
#include <stdbool.h>

#define CGOV_KEY_LEN       32u      /* Ed25519 public key */
#define CGOV_NAME_LEN      32u
#define CGOV_MAX_ACCOUNTS  64u
#define CGOV_MAX_BANNED    64u

/* The classic Carracho/Hotline access bits — the operator ticks these per
 * account, and the client shows them as the familiar checkbox list. */
#define CGOV_READ_CHAT     (1u << 0)
#define CGOV_SEND_CHAT     (1u << 1)
#define CGOV_READ_NEWS     (1u << 2)
#define CGOV_POST_NEWS     (1u << 3)
#define CGOV_DOWNLOAD      (1u << 4)
#define CGOV_UPLOAD        (1u << 5)
#define CGOV_DELETE_FILE   (1u << 6)
#define CGOV_CREATE_FOLDER (1u << 7)
#define CGOV_MOVE_FILE     (1u << 8)
#define CGOV_READ_USERS    (1u << 9)   /* see the user list */
#define CGOV_SEND_PM       (1u << 10)  /* private message */
#define CGOV_BROADCAST     (1u << 11)
#define CGOV_GET_INFO      (1u << 12)  /* see another user's info */
#define CGOV_KICK          (1u << 13)
#define CGOV_BAN           (1u << 14)
#define CGOV_CREATE_ACCOUNT (1u << 15)
#define CGOV_SET_ACCESS    (1u << 16)  /* change the rules / grant privileges */
#define CGOV_ALL           0x0001FFFFu /* every bit above — the operator */

/* Who may even connect to this node. */
typedef enum {
    CGOV_PUBLIC = 0,   /* anyone may connect as a guest         */
    CGOV_PRIVATE,      /* only the operator                     */
    CGOV_SELECT        /* only named accounts (an invite list)  */
} cgov_visibility_t;

typedef struct {
    uint8_t  key[CGOV_KEY_LEN];
    char     name[CGOV_NAME_LEN];
    uint32_t privileges;
    bool     present;
} cgov_account_t;

typedef struct {
    uint8_t          operator_key[CGOV_KEY_LEN];
    uint32_t         guest_privileges;    /* what an unregistered guest may do */
    bool             registration_open;   /* may a guest self-register?        */
    cgov_visibility_t visibility;
    cgov_account_t   account[CGOV_MAX_ACCOUNTS];
    uint32_t         n_accounts;
    uint8_t          banned[CGOV_MAX_BANNED][CGOV_KEY_LEN];
    uint32_t         n_banned;
} cgov_server_t;

/* Found a node. The operator holds ALL privileges; sensible default guest
 * privileges (read/send chat, read news, download) are set and may be
 * changed. Returns false on bad args. */
bool cgov_init(cgov_server_t *s, const uint8_t operator_key[CGOV_KEY_LEN]);

/* The effective privileges of an identity on this node: the operator gets
 * everything; a banned identity gets nothing; a named account gets its set;
 * anyone else gets the guest privileges. */
uint32_t cgov_privileges(const cgov_server_t *s, const uint8_t key[CGOV_KEY_LEN]);

/* May `key` perform an action requiring `priv`? */
bool cgov_can(const cgov_server_t *s, const uint8_t key[CGOV_KEY_LEN], uint32_t priv);

/* May `key` connect at all, given the node's visibility tier? */
bool cgov_may_connect(const cgov_server_t *s, const uint8_t key[CGOV_KEY_LEN]);

/* ---- operator actions (each requires the actor to hold the right bit) ---- */

/* Set what guests may do. Requires SET_ACCESS. */
bool cgov_set_guest_privileges(cgov_server_t *s, const uint8_t actor[CGOV_KEY_LEN],
                               uint32_t privileges);
/* Set the node's visibility tier. Requires SET_ACCESS. */
bool cgov_set_visibility(cgov_server_t *s, const uint8_t actor[CGOV_KEY_LEN],
                         cgov_visibility_t v);
/* Create or update an account with an operator-chosen privilege set.
 * Requires CREATE_ACCOUNT; granting SET_ACCESS additionally requires the
 * actor to already hold SET_ACCESS (no minting an admin from below). */
bool cgov_set_account(cgov_server_t *s, const uint8_t actor[CGOV_KEY_LEN],
                      const uint8_t key[CGOV_KEY_LEN], const char *name,
                      uint32_t privileges);
/* Ban an identity: it loses all access until unbanned. Requires BAN.
 * The operator cannot be banned. */
bool cgov_ban(cgov_server_t *s, const uint8_t actor[CGOV_KEY_LEN],
              const uint8_t key[CGOV_KEY_LEN]);
bool cgov_unban(cgov_server_t *s, const uint8_t actor[CGOV_KEY_LEN],
                const uint8_t key[CGOV_KEY_LEN]);
bool cgov_is_banned(const cgov_server_t *s, const uint8_t key[CGOV_KEY_LEN]);
bool cgov_is_operator(const cgov_server_t *s, const uint8_t key[CGOV_KEY_LEN]);

#endif /* ZXV_CARACHO_GOV_H */
