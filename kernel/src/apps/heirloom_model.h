/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* heirloom_model.h — Heirloom, estate plans and recovery guardians, as a
 * model (zxm.h): the owner's inheritance manifest, the guardians who
 * confirm a death report, a split preview of what the owner holds, and
 * account recovery by guardians, over zxvd's estate engine (estate.*,
 * recovery.*). The engine keeps the estate; the model keeps a copy for the
 * view, holds no key and moves no money. The rules are heirloom_rules.h.
 *
 *   HM1 A WILL IS WHOLE BEFORE IT IS SENT. heirloom_set_will checks the
 *       heirs' shares with heirloom_manifest_gap (HR2) and sends
 *       estate.manifest only when they make exactly one whole; otherwise
 *       ZXM_E_ARG, a notice saying how far off it is, and nothing is sent.
 *       zxvd's answer (the shares over their least common denominator)
 *       must give every heir the very share sent (cross-multiplied) and be
 *       whole again; else ZXM_E_PROTOCOL and the copy is kept.
 *   HM2 FARAID IS ADVICE. heirloom_faraid asks estate.faraid for one
 *       common majority computation; its class shares and what it leaves
 *       unallocated must make one whole (HR2), and it must say it is
 *       advisory and not a ruling, else ZXM_E_PROTOCOL.
 *   HM3 SIGNATURES ARE THE SIGNERS'. heirloom_message answers the bytes a
 *       signer signs (estate.message); heirloom_report, heirloom_approve
 *       and heirloom_veto pass a signature through (128 hex digits, one
 *       Ed25519 signature as zxvd's sandbox takes it). The model never
 *       signs and never holds a key.
 *   HM4 RELEASE DUE. heirloom_refresh reads estate.status and works out
 *       whether the release is due at now (HR3): a report this model filed
 *       (reported_at, in the epoch it was filed: a living veto moves the
 *       epoch on), approvals - k, and now - reported_at - wait. zxvd does
 *       not answer when a report was filed, so a report filed elsewhere
 *       leaves it not due, and the notice says why. heirloom_confirm sends
 *       estate.confirm only when it is due.
 *   HM5 THE PREVIEW REPORTS, NEVER MOVES. heirloom_preview splits an
 *       amount (0 .. CROWN_NINE minor units) by the manifest with Crown's
 *       rule (crown_allot, largest remainder) and, once the estate is
 *       distributing, asks estate.distribute for the engine's parts
 *       (moved false): an uncontested heir's part must equal the
 *       preview's, and the contested heirs' preview parts must sum to what
 *       goes to escrow; else mismatch and ZXM_E_PROTOCOL.
 *       heirloom_preview_holdings previews what the owner holds in one
 *       currency (Coffer's coffer_holdings_in over ledger.read).
 *   HM6 RECOVERY. heirloom_recovery_setup, heirloom_recovery_claim and
 *       heirloom_recovery_status read zxvd's recovery control: the
 *       account is view-only while a claim is open, and spending thaws
 *       only as recovery.status says (it reports; the ledger enforces).
 *   HM7 Nothing is changed before the whole answer is read and checked.
 *
 * No drawing, no allocation, integers only. */
#ifndef HEIRLOOM_MODEL_H
#define HEIRLOOM_MODEL_H

#include "coffer_model.h"
#include "crown_rules.h"
#include "heirloom_rules.h"
#include "zxm.h"

#define HEIRLOOM_KEY_HEX    64u  /* one 32-byte key */
#define HEIRLOOM_SIG_HEX    128u /* one 64-byte Ed25519 signature */
#define HEIRLOOM_MSG_HEX    512u /* the bytes to sign, in hex (est_msg is 95 bytes) */
#define HEIRLOOM_CLASSES    6u /* faraid classes: husband, wives, father, mother, sons, daughters */
#define HEIRLOOM_PHASE_MAX  15u
#define HEIRLOOM_NOTICE_MAX 255u

typedef struct {
    uint32_t id;
    int64_t num, den; /* the share, as zxvd answers it */
    bool contested;
    int64_t escrow; /* minor units held for it */
} heirloom_heir_t;

/* estate.setup: hex keys (guardians and attestors back to back) */
typedef struct {
    const char *owner, *guardians, *attestors, *court, *gateway;
    uint32_t k;
    int64_t wait, release; /* seconds */
} heirloom_setup_t;

/* estate.faraid: who survives */
typedef struct {
    bool husband, father, mother;
    uint32_t wives, sons, daughters;
} heirloom_family_t;

typedef struct {
    char name[16];
    uint32_t persons;
    int64_t num, den;
    int64_t each; /* each person's part of the amount, or -1 */
} heirloom_class_t;

typedef struct {
    zxm_link_t *link;
    coffer_t books; /* ledger.read, for the preview's amount */

    /* the estate (estate.status) */
    bool set_up;
    uint32_t guardians, k, approvals;
    int64_t wait, release, epoch;
    char phase[HEIRLOOM_PHASE_MAX + 1];
    bool advisory, key_release;
    heirloom_heir_t heir[HEIRLOOM_HEIRS];
    uint32_t n;

    /* HM4: a report this model filed */
    bool filed;
    int64_t reported_at, report_epoch;
    bool release_due;

    /* HM2 */
    heirloom_class_t cls[HEIRLOOM_CLASSES];
    uint32_t ncls;
    int64_t unalloc_num, unalloc_den;

    /* HM5 */
    int64_t amount;
    int64_t preview[HEIRLOOM_HEIRS];
    int64_t engine[HEIRLOOM_HEIRS]; /* estate.distribute's parts */
    int64_t to_escrow;
    bool compared, mismatch;

    /* HM6 */
    bool rec_set_up;
    char rec_state[HEIRLOOM_PHASE_MAX + 1];
    int64_t claim, timelock, vbill_spendable, liquid_allowance, notices;
    uint32_t rec_approvals, rec_needed;
    bool rec_vetoed, can_view, can_spend;

    char notice[HEIRLOOM_NOTICE_MAX + 1];
} heirloom_t;

void heirloom_init(heirloom_t *h, zxm_link_t *link);

/* estate.setup, once per run. */
int heirloom_setup(heirloom_t *h, const heirloom_setup_t *s);
/* HM1: the will, n heirs (1..HEIRLOOM_HEIRS) by id and share. */
int heirloom_set_will(heirloom_t *h, const uint32_t *id, const int64_t *num, const int64_t *den,
                      uint32_t n);
/* HM2: amount -1 for shares only. */
int heirloom_faraid(heirloom_t *h, const heirloom_family_t *f, int64_t amount);
/* HM4 */
int heirloom_refresh(heirloom_t *h, int64_t now);
/* HM3: kind "report" (bond), "approve", "veto" or "contest" (heir); the
 * hex bytes into out (cap at least HEIRLOOM_MSG_HEX + 1). */
int heirloom_message(heirloom_t *h, const char *kind, int64_t value, char *out, uint32_t cap);
int heirloom_report(heirloom_t *h, uint32_t attestor, int64_t bond, int64_t now, const char *sig);
int heirloom_approve(heirloom_t *h, uint32_t guardian, const char *sig);
int heirloom_veto(heirloom_t *h, const char *sig);
int heirloom_confirm(heirloom_t *h, int64_t now);
/* HM5 */
int heirloom_preview(heirloom_t *h, int64_t amount);
int heirloom_preview_holdings(heirloom_t *h, const char *ccy);
/* HM6: guardians are exactly five keys back to back. */
int heirloom_recovery_setup(heirloom_t *h, const char *root, const char *guardians,
                            const char *gateway, const char *first_device);
int heirloom_recovery_claim(heirloom_t *h, const char *new_key, int64_t now);
int heirloom_recovery_status(heirloom_t *h, int64_t now, int64_t liquid, int64_t vbill);

#endif
