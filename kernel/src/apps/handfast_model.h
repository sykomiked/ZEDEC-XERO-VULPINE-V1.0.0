/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* handfast_model.h — Handfast, multi-party agreements, as a model (zxm.h):
 * drafted, negotiated and signed in the Hoard, every clause revision on
 * the journal. No drawing, no allocation, integers only. The rules it runs
 * are handfast_rules.h (HR1-HR5 and the parts it composes).
 *
 * zxvd methods used: files.read, files.write, files.mkdir, journal.query,
 * journal.verify, pspace.list, pspace.post and web4.verify.
 *
 *   HM1 THE AGREEMENT is Handfast/deal-<deal>.txt, one record a line, its
 *       fields tab-separated: H deal total instalments paid (the payment
 *       schedule), T title, R room (a space's CID, optional), P contact
 *       key name (each party, in order), V clause rev parent bytes seq
 *       prev (each clause revision, in order), M party clause mark (each
 *       mark that is not silent) and O clause due amount mask (each
 *       obligation: due in days since 1970, amount in minor units, mask
 *       the obligors, bit p for party p). Each clause's text is
 *       Handfast/deal-<deal>/clause-<n>.txt. Every line holds its rules
 *       before it is written, and a line that does not is refused on load.
 *       The file is written whole (files.write); a write that fails leaves
 *       the model as it was.
 *   HM2 PARTIES. 2..16 (HR1), each a contact: its name inside Kindred's
 *       field limits (kindred.contact_fields_ok), its contact number and
 *       name code a fitting contact record (kindred.contact_record_ok),
 *       and the 64-hex-digit key its signature is checked against.
 *   HM3 CLAUSES. A clause's text is 1..8192 bytes (HR1) and sits under a
 *       parent clause (0 at the top; never under itself or one of its
 *       own). Each revision is filed in the clause tree (HR5), stamped
 *       with the journal's newest seq after its text is written, linked
 *       to the revision before it (prev), and clears every party's mark
 *       on that clause: a changed clause is agreed again.
 *   HM4 THE PARTY MATRIX. Each party marks each clause silent, agreed or
 *       disputed, on that party's own word (recorded on the owner's
 *       confirmed word). A clause's state is HR2: BOTH when one party
 *       agreed and another disputed, held in escrow; the clauses every
 *       party agreed (TRUE) can go ahead.
 *   HM5 READY TO SIGN is HR3 (every cell agreed) AND sealwax.sign_ready
 *       for the signature page: one field a party (missing until its key
 *       is known) and one an obligation (outside when its clause is gone
 *       or its amount field does not fit sealwax.field_record_ok).
 *   HM6 OBLIGATIONS. An obligation's amount is entered as units and cents
 *       (sealwax_amount_minor, furrow.money_cell); a party's obligation on
 *       a due date is the exact sum (coffer.holdings, sealwax_total) of
 *       its shares (HR4) of the obligations due then. The payment
 *       schedule is trickle.stream_due: instalment n of N owes
 *       floor(total * n / N) - paid. Advice only: Handfast moves no money.
 *   HM7 NEGOTIATION. A note goes to the agreement's room (pspace.post)
 *       when commons.room_post_ok holds for it (pspace.list lists it, its
 *       phase is "in", the space network is online) and its text is
 *       commons.message_text_ok.
 *   HM8 SIGNATURES AND PROOF. Each party signs elsewhere, by its own act:
 *       a signed copy (the hex of a Web4 envelope) is checked with
 *       web4.verify against that party's key and counts only when it holds
 *       and comes from that key. A clause's history is proven by joining
 *       two witnesses with lpres_kjoin: its revisions (witness.chain_ok,
 *       each witness.audit_ok) and the journal (journal.verify intact and
 *       each revision's seq still in it); no journal says nothing
 *       (NEITHER), a disagreement is BOTH, held. */
#ifndef HANDFAST_MODEL_H
#define HANDFAST_MODEL_H

#include "handfast_rules.h"
#include "zxm.h"

#define HANDFAST_DIR         "Handfast"
#define HANDFAST_REVS        128u /* clause revisions kept in one agreement */
#define HANDFAST_OBLIGATIONS 16u  /* the signature page holds 16 parties + 16 obligations */
#define HANDFAST_NAME_MAX    63u
#define HANDFAST_TITLE_MAX   79u
#define HANDFAST_KEY_HEX     64u
#define HANDFAST_CID_MAX     96u
#define HANDFAST_SIG_MAX     16000u /* hex characters of a signed copy, inside a request line */
#define HANDFAST_NOTICE      240u

typedef struct {
    int64_t contact, name_code;
    char key[HANDFAST_KEY_HEX + 1]; /* "" until known */
    char name[HANDFAST_NAME_MAX + 1];
    bool signed_ok; /* a checked signed copy holds (HM8); not kept in the file */
} handfast_party_t;

typedef struct {
    int64_t clause, rev, parent, bytes, seq, prev;
} handfast_rev_t;

typedef struct {
    int64_t clause, due, amount, mask;
} handfast_obligation_t;

typedef struct {
    zxm_link_t *link;
    bool loaded;
    int64_t deal;
    char title[HANDFAST_TITLE_MAX + 1];
    char room[HANDFAST_CID_MAX + 1];
    handfast_party_t party[HANDFAST_PARTY_MAX];
    uint32_t nparty;
    handfast_rev_t rev[HANDFAST_REVS];
    uint32_t nrev;
    int64_t clause[HANDFAST_CLAUSE_MAX]; /* the clause numbers, in the order first filed */
    uint32_t nclause;
    int64_t mark[HANDFAST_PARTY_MAX][HANDFAST_CLAUSE_MAX];
    handfast_obligation_t obl[HANDFAST_OBLIGATIONS];
    uint32_t nobl;
    int64_t total, instalments, paid; /* the payment schedule (HM6); instalments 0: none */
    char notice[HANDFAST_NOTICE];
} handfast_t;

void handfast_init(handfast_t *h, zxm_link_t *link);
/* HM1: open a new agreement, or load one */
int handfast_open(handfast_t *h, int64_t deal, const char *title, bool owner_confirmed);
int handfast_load(handfast_t *h, int64_t deal);
/* HM2: *index the party's place */
int handfast_add_party(handfast_t *h, const char *name, int64_t contact, const char *key,
                       bool owner_confirmed, uint32_t *index);
/* HM3 */
int handfast_put_clause(handfast_t *h, int64_t clause, int64_t parent, const char *text,
                        uint32_t len, bool owner_confirmed);
int handfast_read_clause(handfast_t *h, int64_t clause, char *text, uint32_t cap);
/* the newest revision of a clause, or -1 */
int32_t handfast_latest(const handfast_t *h, int64_t clause);
/* HM4 */
int handfast_mark(handfast_t *h, uint32_t party, int64_t clause, int64_t mark,
                  bool owner_confirmed);
lpres_state_t handfast_clause_state(const handfast_t *h, int64_t clause);
bool handfast_goes_ahead(const handfast_t *h, int64_t clause);
/* the clauses held in escrow (BOTH) into out; how many */
uint32_t handfast_escrow(const handfast_t *h, int64_t *out, uint32_t cap);
int64_t handfast_unagreed(const handfast_t *h);
/* HM5 */
bool handfast_ready(const handfast_t *h);
/* HM6 */
int handfast_add_obligation(handfast_t *h, int64_t clause, int64_t due, int64_t units,
                            int64_t cents, int64_t mask, bool owner_confirmed);
int handfast_owed(const handfast_t *h, uint32_t party, int64_t due, int64_t *minor);
int handfast_set_schedule(handfast_t *h, int64_t total, int64_t instalments, bool owner_confirmed);
int handfast_record_paid(handfast_t *h, int64_t paid, bool owner_confirmed);
int handfast_instalment(const handfast_t *h, int64_t n, int64_t *due);
/* HM7 */
int handfast_set_room(handfast_t *h, const char *cid, bool owner_confirmed);
int handfast_post(handfast_t *h, const char *text, bool owner_confirmed);
/* HM8 */
int handfast_check_signed(handfast_t *h, uint32_t party, const char *envelope_hex);
lpres_state_t handfast_prove(handfast_t *h, int64_t clause);

#endif
