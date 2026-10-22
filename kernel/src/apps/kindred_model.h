/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* kindred_model.h — Kindred, the contacts, as a model (zxm.h): contacts
 * kept as records in the Hoard, a contact's agent card checked by zxvd,
 * its node taken from the peers zxvd has verified, the owner's own chat
 * identity, a name-prefix search and the likely duplicates. No drawing,
 * no allocation, integers only. The rules it runs are kindred_rules.h
 * (KR1-KR7).
 *
 *   KM1 FILES IN THE HOARD. A contact is KINDRED_DIR/c<contact>.txt, six
 *       lines: name, phone, mail, note, card (a verified agent_id, or
 *       empty) and node (a NodeID, or empty), written with files.write.
 *       Its record (KR5) goes into KINDRED_INDEX, one line a contact,
 *       rewritten whole after each change; kindred_load reads both back
 *       and refuses an index whose codes do not match its contacts.
 *   KM2 FIELDS. A contact's fields are held to KR1 and are text of one
 *       line: no control byte, the name and note valid UTF-8, the phone
 *       digits with one optional leading '+', the mail empty or one '@'
 *       between printable ASCII. Anything else is ZXM_E_ARG and nothing
 *       is sent.
 *   KM3 THE CARD ONLY WHEN IT VERIFIES. kindred_card asks zxvd
 *       (web4.card_verify); only a card that verifies is kept, with the
 *       agent_id zxvd answers. One that does not changes nothing and the
 *       notice gives zxvd's reason. A verified card is information for
 *       the owner, never a permission.
 *   KM4 THE NODE ONLY FROM THE PEERS. kindred_peers reads net.peers (each
 *       NodeID verified by a signed message); kindred_node links a contact
 *       to a NodeID only when it is among the peers last read.
 *   KM5 THE OWNER. kindred_me reads chat.me: the owner's NodeID and whom
 *       the owner follows; kindred_followed says whether a contact's node
 *       is one of them.
 *   KM6 SEARCH AND DUPLICATES. kindred_search lists the contacts whose
 *       name begins with what was typed (KR2, KR3), in the order kept;
 *       kindred_find_dupes counts each contact's likely duplicates (KR4).
 *       Both are advisory: nothing is merged or removed but by the owner
 *       (kindred_remove). */
#ifndef KINDRED_MODEL_H
#define KINDRED_MODEL_H

#include "kindred_rules.h"
#include "zxm.h"

#define KINDRED_DIR       "Kindred"
#define KINDRED_INDEX     "Kindred/index.txt"
#define KINDRED_CONTACTS  KINDRED_ROWS_MAX /* contacts kept (KR4) */
#define KINDRED_PEERS     32u
#define KINDRED_FOLLOW    32u
#define KINDRED_PATH_MAX  32u
#define KINDRED_NOTICE    200u
#define KINDRED_CARD_NAME 31u   /* web4.card_verify's name: [a-z0-9._-]{1,31} */
#define KINDRED_PK_HEX    3904u /* ML-DSA-65 public key */
#define KINDRED_SIG_HEX   6618u /* its binding signature */

typedef struct {
    int64_t contact; /* 1..KINDRED_CONTACT_MAX, in the order added */
    char name[KINDRED_NAME_MAX + 1];
    char phone[KINDRED_PHONE_MAX + 2]; /* a '+' and 15 digits */
    char mail[KINDRED_MAIL_MAX + 1];
    char note[KINDRED_NOTE_MAX + 1];
    char card[KINDRED_HEX_ID + 1];           /* a verified agent_id, "" none (KM3) */
    char node[KINDRED_HEX_ID + 1];           /* a NodeID from net.peers, "" none (KM4) */
    int64_t card_code, node_code, name_code; /* KR2, KR7 */
    int64_t verified;                        /* 1 when card holds a verified card */
    int64_t stamp;                           /* ms since 1970, the last change */
} kindred_contact_t;

typedef struct {
    char id[KINDRED_HEX_ID + 1];
    bool member;
    uint64_t last_seen_ms;
} kindred_peer_t;

/* An agent card as web4.agent_card gives it. */
typedef struct {
    const char *name;
    uint64_t bound_ms;
    const char *pk_hex;
    const char *sig_hex;
} kindred_card_t;

typedef struct {
    zxm_link_t *link;
    bool loaded, folder;
    kindred_contact_t c[KINDRED_CONTACTS];
    uint32_t n;
    int64_t last; /* the highest contact number given */
    /* KM4 */
    kindred_peer_t peer[KINDRED_PEERS];
    uint32_t npeers;
    /* KM5 */
    char me[KINDRED_HEX_ID + 1];
    char following[KINDRED_FOLLOW][KINDRED_HEX_ID + 1];
    uint32_t nfollowing;
    /* KM6 */
    uint32_t hit[KINDRED_CONTACTS]; /* indexes into c */
    uint32_t nhits;
    int64_t dupes[KINDRED_CONTACTS];
    char notice[KINDRED_NOTICE];
} kindred_t;

void kindred_init(kindred_t *k, zxm_link_t *link);
/* KM1: the index and every contact, read back (none written yet: none). */
int kindred_load(kindred_t *k);
/* KM1, KM2: a new contact; its number in *contact. ZXM_E_STATE before a
 * load, ZXM_E_TOOBIG when KINDRED_CONTACTS are kept. */
int kindred_add(kindred_t *k, const char *name, const char *phone, const char *mail,
                const char *note, int64_t stamp, int64_t *contact);
/* KM1, KM2: a contact's fields changed (its card and node stay). */
int kindred_edit(kindred_t *k, int64_t contact, const char *name, const char *phone,
                 const char *mail, const char *note, int64_t stamp);
/* KM1: a contact moved to the Trash and out of the index. */
int kindred_remove(kindred_t *k, int64_t contact);
/* KM3 */
int kindred_card(kindred_t *k, int64_t contact, const kindred_card_t *card, int64_t stamp);
/* KM4 */
int kindred_peers(kindred_t *k);
int kindred_node(kindred_t *k, int64_t contact, const char *node_id, int64_t stamp);
/* KM5 */
int kindred_me(kindred_t *k);
bool kindred_followed(const kindred_t *k, int64_t contact);
/* KM6 */
int kindred_search(kindred_t *k, const char *typed);
int kindred_find_dupes(kindred_t *k);
/* the contact numbered `contact`, or NULL */
const kindred_contact_t *kindred_get(const kindred_t *k, int64_t contact);

#endif
