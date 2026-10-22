/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* assize_model.h — Assize, dispute cases, as a model (zxm.h): a case's
 * docket of filings, evidence, hearings, annotations and the arbiter's
 * rulings, kept in the Hoard on the signed record. No drawing, no
 * allocation, integers only. The rules it runs are assize_rules.h
 * (AR1-AR6). Assize never decides a case: a ruling is the arbiter's (a
 * person's), entered on the owner's confirmed word.
 *
 * zxvd methods used: files.read, files.write, files.mkdir, files.stat,
 * journal.query and journal.verify.
 *
 *   AM1 THE CASE FILE is Assize/case-NNNNNN.txt (the case number in six
 *       digits), one docket line a line, seventeen tab-separated integer
 *       fields and a path: seq (the line's place on the docket, 1 for the
 *       opening), prev (the seq before it, 0 for the first), kind
 *       (ASSIZE_OPENED .. ASSIZE_NOTE), party (who lodged it, 0 the
 *       tribunal), other (the respondent on the opening line, the
 *       evidence's seq on an annotation), filed
 *       (days since 1970), due (the deadline to respond on the opening
 *       line, the day set on a hearing), claim, site (where the line places the event: asserted by
 *       a filing, witnessed by evidence), pages, exhibits, page, x, y and
 *       mark (an annotation's place on a page of evidence and its kind),
 *       finding (a ruling's: 1 upheld, 2 dismissed), jseq (the journal's
 *       newest seq when it was lodged, 0 with no journal), then the
 *       document's Hoard path ("-" for none). Every line holds AR5
 *       (dispute_node_ok) and witness.audit_ok before it is written, and a
 *       line that does not is refused on load.
 *   AM2 OPENING. assize_open checks the opening form (sealwax.sign_ready
 *       over fields defined by sealwax.field_record_ok), then the owner's
 *       word, then that no case of that number exists; the case file is
 *       written whole (files.mkdir, files.write).
 *   AM3 LODGING. assize_lodge takes a filing or an evidence item: its
 *       form must be sign_ready, then it must be accepted (AR1: by a party
 *       to the case, inside the deadline, within the procedural limits),
 *       then the owner's word. The document must be a file (files.stat),
 *       and a PDF must be broadsheet.well_formed. Its seq is
 *       tallystick.next_doc_number of the docket's last, never reused,
 *       and its docket number AR3. A write that fails leaves the case as
 *       it was.
 *   AM4 THE EVIDENCE MATRIX. A claim's state is AR4 over the case's
 *       evidence (claim_evidence_state), each item saying what AR6 makes
 *       of the site it gives against the site the claim asserts. Two
 *       witnesses that place one event at two sites make the claim BOTH:
 *       both items are kept, never merged, and the claim is held in
 *       escrow until a ruling is recorded for it.
 *   AM5 RULINGS are the arbiter's. assize_rule records the finding the
 *       owner enters on the arbiter's word (once a claim), with the
 *       arbiter's signed document; it releases the escrow and changes no
 *       evidence. Whether the finding follows from the claim's state
 *       (twelvefold.lp_entail_kept) is said as advice, never acted on.
 *   AM6 HEARINGS AND ANNOTATIONS: a hearing is set on a day not before
 *       today; an annotation marks a page of an evidence item and must be
 *       broadsheet.note_ok. Both on the owner's word.
 *   AM7 THE TRAIL joins two witnesses with lpres_kjoin: the docket's own
 *       chain (witness.chain_ok, each line linked to the one before) and
 *       the journal (journal.verify intact, and each stamped jseq inside
 *       it). No journal says nothing (NEITHER); a break is FALSE; when the
 *       two disagree it is BOTH, held.
 *   AM8 MEASURES. Days left to respond (AR2); lines of one kind and claims
 *       of one value (witness.kind_count); the notice's text, since zxvd
 *       has no notification method. */
#ifndef ASSIZE_MODEL_H
#define ASSIZE_MODEL_H

#include "assize_rules.h"
#include "zxm.h"

#define ASSIZE_DIR      "Assize"
#define ASSIZE_ROWS     64u  /* lines of one case's docket */
#define ASSIZE_PATH_MAX 120u /* bytes of a document's path: a text field's most (sealwax) */
#define ASSIZE_NOTICE   240u
#define ASSIZE_FILE_MAX 32u /* bytes of the case file's path */

/* a docket line's kind (AM1) */
enum {
    ASSIZE_OPENED = 1,
    ASSIZE_FILING = 2,
    ASSIZE_EVIDENCE = 3,
    ASSIZE_HEARING = 4,
    ASSIZE_RULING = 5,
    ASSIZE_NOTE = 6
};

/* a ruling's finding (AM5), in the kernel's codes */
enum { ASSIZE_UPHELD = 1, ASSIZE_DISMISSED = 2 };

typedef struct {
    int64_t seq, prev, kind, party, other, filed, due, claim, site, pages, exhibits, page, x, y,
        mark, finding, jseq;
    char path[ASSIZE_PATH_MAX + 1];
} assize_line_t;

typedef struct {
    zxm_link_t *link;
    bool loaded;
    int64_t caseno;
    char file[ASSIZE_FILE_MAX];
    assize_line_t line[ASSIZE_ROWS];
    uint32_t n;
    char notice[ASSIZE_NOTICE];
} assize_t;

void assize_init(assize_t *a, zxm_link_t *link);
/* AM2: a new case; due is the deadline to respond (days since 1970) */
int assize_open(assize_t *a, int64_t caseno, int64_t claimant, int64_t respondent, int64_t today,
                int64_t due, bool owner_confirmed);
/* AM1 */
int assize_load(assize_t *a, int64_t caseno);
int32_t assize_find(const assize_t *a, int64_t seq);
/* AM3: kind ASSIZE_FILING (claim and site asserted) or ASSIZE_EVIDENCE (site witnessed);
 * *docket the number given */
int assize_lodge(assize_t *a, int64_t kind, int64_t party, int64_t today, int64_t pages,
                 int64_t exhibits, int64_t claim, int64_t site, const char *path,
                 bool owner_confirmed, int64_t *docket);
/* AM4 */
lpres_state_t assize_claim(const assize_t *a, int64_t claim);
bool assize_escrowed(const assize_t *a, int64_t claim);
/* AM5 */
int assize_rule(assize_t *a, int64_t claim, int64_t finding, int64_t today, const char *path,
                bool owner_confirmed);
int64_t assize_finding_follows(const assize_t *a, int64_t claim, int64_t finding);
/* AM6 */
int assize_hearing(assize_t *a, int64_t day, int64_t today, bool owner_confirmed);
int assize_annotate(assize_t *a, int64_t target, int64_t page, int64_t x, int64_t y, int64_t mark,
                    int64_t today, bool owner_confirmed);
/* AM7 */
lpres_state_t assize_trail(assize_t *a);
/* AM8 */
int assize_respond_left(const assize_t *a, int64_t today, int64_t *left);
int64_t assize_count(const assize_t *a, int64_t kind);
int64_t assize_claims_of(const assize_t *a, int64_t code);

#endif
