/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/*
 * DISCLAIMER: This engine evaluates deterministic mathematical and state
 * constraints (boolean/paraconsistent policy logic). It does not provide
 * legal advice, make legal determinations, or replace statutory judicial
 * processes. Users are solely responsible for ensuring that parameterized
 * policies comply with applicable local and international laws.
 */
/* zxp_draft.c — drafting plug-ins (see zxp_draft.h). Freestanding. */
#include "zxp_draft.h"

/* Copy a NUL-terminated string into a zeroed field of `size`; false if it
 * does not fit. */
static bool put_field(char *dst, uint32_t size, const char *src)
{
    if (!src) return false;
    uint32_t i = 0;
    for (; src[i] != 0; i++) {
        if (i + 1u >= size) return false;
        dst[i] = src[i];
    }
    return true;
}

static int32_t str_cmp(const char *a, const char *b)
{
    uint32_t i = 0;
    while (a[i] != 0 && a[i] == b[i]) i++;
    return (int32_t) (uint8_t) a[i] - (int32_t) (uint8_t) b[i];
}

/* Add the two-letter country of `jur` to the sorted ALLOW list, once. */
static void allow_country(zxp_contract_t *c, const char *jur)
{
    char cc[ZXP_JUR_MAX + 1u] = {jur[0], jur[1], 0, 0, 0, 0, 0};
    uint32_t at = c->n_allow;
    for (uint32_t i = 0; i < c->n_allow; i++) {
        int32_t d = str_cmp(c->allow[i], cc);
        if (d == 0) return;
        if (d > 0) {
            at = i;
            break;
        }
    }
    for (uint32_t i = c->n_allow; i > at; i--)
        for (uint32_t k = 0; k <= ZXP_JUR_MAX; k++) c->allow[i][k] = c->allow[i - 1][k];
    for (uint32_t k = 0; k <= ZXP_JUR_MAX; k++) c->allow[at][k] = cc[k];
    c->n_allow++;
}

static void clause_id(char *id, char prefix, uint32_t n)
{
    id[0] = prefix;
    if (n < 10u) {
        id[1] = (char) ('0' + n);
    } else {
        id[1] = (char) ('0' + n / 10u);
        id[2] = (char) ('0' + n % 10u);
    }
}

/* Installments of `total` from `payer` to `payee`: equal parts, the
 * remainder on the last. Returns the largest single installment or -1. */
static int64_t schedule(zxp_contract_t *c, const zxp_draft_params_t *p, char prefix, uint8_t payer,
                        uint8_t payee, int64_t total, uint8_t after)
{
    int64_t part = total / p->installments, big = 0;
    for (uint32_t k = 0; k < p->installments; k++) {
        zxp_clause_t *cl;
        if (c->n_clause >= ZXP_MAX_CLAUSES) return -1;
        cl = &c->clause[c->n_clause];
        cl->kind = ZXP_CL_DUE;
        clause_id(cl->id, prefix, k + 1u);
        cl->payer = payer;
        cl->payee = payee;
        for (uint32_t i = 0; i < 3u; i++) cl->asset[i] = p->asset[i];
        cl->amount =
            k + 1u == p->installments ? total - part * (int64_t) (p->installments - 1u) : part;
        cl->date = zxp_date_add(p->first_due, (int32_t) (k * p->interval_days));
        cl->penalty = p->penalty;
        cl->after = after;
        if (cl->amount <= 0 || cl->date == 0) return -1;
        if (cl->amount > big) big = cl->amount;
        c->n_clause++;
    }
    return big;
}

static zxp_status_t draft(zxp_template_t tpl, const zxp_draft_params_t *p, zxp_contract_t *out)
{
    if (!out) return ZXP_E_ARG;
    zxp_clear(out);
    if (!p || tpl < ZXP_TPL_SERVICES || tpl > ZXP_TPL_BILATERAL) return ZXP_E_ARG;
    if (!put_field(out->name, sizeof out->name, p->name)) return ZXP_E_NAME;
    out->date = p->date;
    if (!put_field(out->law[0], ZXP_JUR_MAX + 1u, p->law)) return ZXP_E_JUR;
    out->n_law = 1;
    if (!put_field(out->party[0].id, ZXP_ID_MAX + 1u, p->a_id) ||
        !put_field(out->party[1].id, ZXP_ID_MAX + 1u, p->b_id))
        return ZXP_E_NAME;
    if (!put_field(out->party[0].jur, ZXP_JUR_MAX + 1u, p->a_jur) ||
        !put_field(out->party[1].jur, ZXP_JUR_MAX + 1u, p->b_jur) ||
        !zxp_jur_ok(out->party[0].jur) || !zxp_jur_ok(out->party[1].jur))
        return ZXP_E_JUR;
    out->n_party = 2;
    out->sign = 2;
    out->window.from = p->date;
    out->window.to = p->window_to;
    allow_country(out, out->party[0].jur);
    allow_country(out, out->party[1].jur);

    bool pays = tpl != ZXP_TPL_MOU && !(tpl == ZXP_TPL_BILATERAL && p->amount == 0);
    if (pays) {
        int64_t big, adv = 0;
        if (!p->asset || p->asset[0] == 0 || p->asset[1] == 0 || p->asset[2] == 0 ||
            p->asset[3] != 0)
            return ZXP_E_ASSET;
        if (p->installments < 1 || p->installments > 12 || p->interval_days < 1 ||
            p->interval_days > 366)
            return ZXP_E_COUNT;
        if (p->amount <= 0 || p->amount > ZXP_PIC18_MAX || p->penalty < 0 ||
            p->penalty > ZXP_PIC18_MAX)
            return ZXP_E_NUMBER;
        if (tpl == ZXP_TPL_LOAN) {
            zxp_clause_t *cl = &out->clause[0];
            if (p->repay < p->amount || p->repay > ZXP_PIC18_MAX) return ZXP_E_NUMBER;
            cl->kind = ZXP_CL_DUE;
            cl->id[0] = 'A', cl->id[1] = 'D', cl->id[2] = 'V';
            cl->payer = 0;
            cl->payee = 1;
            for (uint32_t i = 0; i < 3u; i++) cl->asset[i] = p->asset[i];
            cl->amount = p->amount;
            cl->date = p->date;
            out->n_clause = 1;
            adv = p->amount;
            big = schedule(out, p, 'R', 1, 0, p->repay, 1);
        } else {
            big = schedule(out, p, 'T', 0, 1, p->amount, 0);
        }
        if (big < 0) return ZXP_E_DATE;
        if (adv > big) big = adv;
        for (uint32_t i = 0; i < 3u; i++) out->cap[0].asset[i] = p->asset[i];
        out->cap[0].amount = big;
        out->n_cap = 1;
        /* LIMIT: each paying party may pay no more than the schedule. */
        if (tpl == ZXP_TPL_LOAN) {
            out->limit[0].party = 0;
            out->limit[1].party = 1;
            out->limit[0].amount = p->amount;
            out->limit[1].amount = p->repay;
            for (uint32_t i = 0; i < 3u; i++)
                out->limit[0].asset[i] = out->limit[1].asset[i] = p->asset[i];
            out->n_limit = 2;
        } else {
            out->limit[0].party = 0;
            out->limit[0].amount = p->amount;
            for (uint32_t i = 0; i < 3u; i++) out->limit[0].asset[i] = p->asset[i];
            out->n_limit = 1;
        }
    }
    return zxp_validate(out);
}

zxp_status_t zxp_draft(zxp_template_t tpl, const zxp_draft_params_t *p, zxp_contract_t *out)
{
    zxp_status_t st = draft(tpl, p, out);
    if (st != ZXP_OK && out) zxp_clear(out);
    return st;
}

/* ------------------------------------------------------------ family table */

/* Informational only, after JuriGlobe (University of Ottawa). Sorted. */
static const struct {
    char tag[7];
    uint8_t fam;
} FAM[] = {
    {"AE", ZXP_FAM_CIVIL | ZXP_FAM_MUSLIM},
    {"AR", ZXP_FAM_CIVIL},
    {"AT", ZXP_FAM_CIVIL},
    {"AU", ZXP_FAM_COMMON},
    {"BE", ZXP_FAM_CIVIL},
    {"BR", ZXP_FAM_CIVIL},
    {"CA", ZXP_FAM_COMMON},
    {"CA-QC", ZXP_FAM_CIVIL},
    {"CH", ZXP_FAM_CIVIL},
    {"CL", ZXP_FAM_CIVIL},
    {"CO", ZXP_FAM_CIVIL},
    {"CZ", ZXP_FAM_CIVIL},
    {"DE", ZXP_FAM_CIVIL},
    {"DK", ZXP_FAM_CIVIL},
    {"DZ", ZXP_FAM_CIVIL | ZXP_FAM_MUSLIM},
    {"EG", ZXP_FAM_CIVIL | ZXP_FAM_MUSLIM},
    {"ES", ZXP_FAM_CIVIL},
    {"ET", ZXP_FAM_CIVIL | ZXP_FAM_CUSTOMARY},
    {"FI", ZXP_FAM_CIVIL},
    {"FR", ZXP_FAM_CIVIL},
    {"GB", ZXP_FAM_COMMON | ZXP_FAM_CIVIL},
    {"GB-ENG", ZXP_FAM_COMMON},
    {"GB-SCT", ZXP_FAM_COMMON | ZXP_FAM_CIVIL},
    {"GH", ZXP_FAM_COMMON | ZXP_FAM_CUSTOMARY},
    {"GR", ZXP_FAM_CIVIL},
    {"ID", ZXP_FAM_CIVIL | ZXP_FAM_CUSTOMARY | ZXP_FAM_MUSLIM},
    {"IE", ZXP_FAM_COMMON},
    {"IL", ZXP_FAM_CIVIL | ZXP_FAM_COMMON | ZXP_FAM_MUSLIM | ZXP_FAM_OTHER},
    {"IN", ZXP_FAM_COMMON | ZXP_FAM_MUSLIM | ZXP_FAM_CUSTOMARY | ZXP_FAM_OTHER},
    {"IR", ZXP_FAM_MUSLIM},
    {"IT", ZXP_FAM_CIVIL},
    {"JO", ZXP_FAM_CIVIL | ZXP_FAM_MUSLIM},
    {"KE", ZXP_FAM_COMMON | ZXP_FAM_MUSLIM | ZXP_FAM_CUSTOMARY},
    {"KR", ZXP_FAM_CIVIL},
    {"MA", ZXP_FAM_CIVIL | ZXP_FAM_MUSLIM},
    {"MX", ZXP_FAM_CIVIL},
    {"MY", ZXP_FAM_COMMON | ZXP_FAM_MUSLIM | ZXP_FAM_CUSTOMARY},
    {"NG", ZXP_FAM_COMMON | ZXP_FAM_MUSLIM | ZXP_FAM_CUSTOMARY},
    {"NL", ZXP_FAM_CIVIL},
    {"NO", ZXP_FAM_CIVIL},
    {"NZ", ZXP_FAM_COMMON},
    {"PE", ZXP_FAM_CIVIL},
    {"PH", ZXP_FAM_CIVIL | ZXP_FAM_COMMON | ZXP_FAM_MUSLIM | ZXP_FAM_CUSTOMARY},
    {"PK", ZXP_FAM_COMMON | ZXP_FAM_MUSLIM},
    {"PL", ZXP_FAM_CIVIL},
    {"PT", ZXP_FAM_CIVIL},
    {"QA", ZXP_FAM_CIVIL | ZXP_FAM_MUSLIM},
    {"RO", ZXP_FAM_CIVIL},
    {"RU", ZXP_FAM_CIVIL},
    {"SA", ZXP_FAM_MUSLIM},
    {"SE", ZXP_FAM_CIVIL},
    {"SG", ZXP_FAM_COMMON},
    {"TN", ZXP_FAM_CIVIL | ZXP_FAM_MUSLIM},
    {"TR", ZXP_FAM_CIVIL},
    {"UA", ZXP_FAM_CIVIL},
    {"US", ZXP_FAM_COMMON},
    {"US-LA", ZXP_FAM_COMMON | ZXP_FAM_CIVIL},
    {"UY", ZXP_FAM_CIVIL},
    {"ZA", ZXP_FAM_CIVIL | ZXP_FAM_COMMON | ZXP_FAM_CUSTOMARY},
};

static int32_t fam_find(const char *tag)
{
    int32_t lo = 0, hi = (int32_t) (sizeof FAM / sizeof FAM[0]) - 1;
    while (lo <= hi) {
        int32_t mid = (lo + hi) / 2, d = str_cmp(FAM[mid].tag, tag);
        if (d == 0) return mid;
        if (d < 0)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return -1;
}

uint8_t zxp_family(const char *jur)
{
    int32_t i;
    if (!jur || !zxp_jur_ok(jur)) return 0;
    if ((i = fam_find(jur)) >= 0) return FAM[i].fam;
    char cc[3] = {jur[0], jur[1], 0};
    return (i = fam_find(cc)) >= 0 ? FAM[i].fam : 0;
}

/* ------------------------------------------------------------ prose */

typedef struct {
    char *p;
    uint32_t cap, n;
    bool ok;
} tb_t;

static void c_(tb_t *b, char ch)
{
    if (b->n + 1u >= b->cap) {
        b->ok = false;
        return;
    }
    b->p[b->n++] = ch;
}
static void s_(tb_t *b, const char *s)
{
    for (uint32_t i = 0; s[i] != 0; i++) c_(b, s[i]);
}
static void u_(tb_t *b, uint64_t v)
{
    char d[20];
    uint32_t n = 0;
    do {
        d[n++] = (char) ('0' + v % 10u);
        v /= 10u;
    } while (v != 0);
    while (n > 0) c_(b, d[--n]);
}
static void date_(tb_t *b, uint32_t d)
{
    u_(b, d / 10000u);
    c_(b, '-');
    c_(b, (char) ('0' + (d / 1000u) % 10u));
    c_(b, (char) ('0' + (d / 100u) % 10u));
    c_(b, '-');
    c_(b, (char) ('0' + (d / 10u) % 10u));
    c_(b, (char) ('0' + d % 10u));
}

uint32_t zxp_family_text(uint8_t fam, char *out, uint32_t cap)
{
    static const char *const names[] = {"civil law", "common law", "Muslim law", "customary law",
                                        "other religious or personal law"};
    if (!out || cap == 0) return 0;
    tb_t b = {out, cap, 0, true};
    bool first = true;
    for (uint32_t i = 0; i < 5u; i++) {
        if (!((fam >> i) & 1u)) continue;
        if (!first) s_(&b, ", ");
        s_(&b, names[i]);
        first = false;
    }
    if (first) s_(&b, "not in the built-in table");
    if (!b.ok) {
        out[0] = 0;
        return 0;
    }
    out[b.n] = 0;
    return b.n;
}

static const char *const TITLE[] = {"",
                                    "Services Agreement",
                                    "Sale Agreement",
                                    "Loan Agreement",
                                    "Memorandum of Understanding",
                                    "Bilateral Accord"};
static const char *const ROLE_A[] = {"", "Client", "Buyer", "Lender", "Participant", "State Party"};
static const char *const ROLE_B[] = {"",         "Provider",    "Seller",
                                     "Borrower", "Participant", "State Party"};

#define COUNSEL "[TO BE COMPLETED BY COUNSEL]"

uint32_t zxp_draft_prose(zxp_template_t tpl, const zxp_contract_t *c, char *out, uint32_t cap)
{
    char fam[96];
    if (!c || !out || cap == 0 || tpl < ZXP_TPL_SERVICES || tpl > ZXP_TPL_BILATERAL ||
        zxp_validate(c) != ZXP_OK || c->n_party < 2)
        return 0;
    tb_t b = {out, cap, 0, true};
    s_(&b, "# ");
    s_(&b, TITLE[tpl]);
    s_(&b, " (draft template)\n\n");
    s_(&b, "> This is a template produced by software. It is not legal advice and makes no\n"
           "> legal determination. Whether it suits your purpose or can be enforced depends on\n"
           "> the governing law and the facts. Have it reviewed by a qualified lawyer in each\n"
           "> jurisdiction involved before anyone signs it.\n\n");
    s_(&b, "Reference: ");
    s_(&b, c->name);
    s_(&b, ". Date: ");
    date_(&b, c->date);
    s_(&b, ".\n\n## 1. Parties\n\n");
    for (uint32_t i = 0; i < 2u; i++) {
        s_(&b, "- **");
        s_(&b, c->party[i].id);
        s_(&b, "** (");
        s_(&b, i == 0 ? ROLE_A[tpl] : ROLE_B[tpl]);
        s_(&b, "), jurisdiction ");
        s_(&b, c->party[i].jur);
        s_(&b, ". Full legal name, address and signatory: " COUNSEL "\n");
    }
    s_(&b, "\n## 2. Purpose\n\n");
    switch (tpl) {
    case ZXP_TPL_SERVICES:
        s_(&b, "The Provider performs the services described in Schedule A; the Client pays for "
               "them as set out in section 4.\n");
        break;
    case ZXP_TPL_SALE:
        s_(&b, "The Seller sells and delivers the goods described in Schedule A; the Buyer pays "
               "the price as set out in section 4.\n");
        break;
    case ZXP_TPL_LOAN:
        s_(&b,
           "The Lender advances a sum to the Borrower, who repays it as set out in section 4.\n");
        break;
    case ZXP_TPL_MOU:
        s_(&b,
           "The Participants record their shared intentions. Except for confidentiality and costs, "
           "nothing in this memorandum is intended to create legally binding obligations.\n");
        break;
    case ZXP_TPL_BILATERAL:
        s_(&b, "The State Parties record the cooperation described in Schedule A. Its status under "
               "international law, and any ratification each State Party needs, are matters for "
               "each State Party.\n");
        break;
    }
    s_(&b, "\nSchedule A: " COUNSEL "\n\n## 3. Governing law and disputes\n\n");
    s_(&b, "Governing law named in the rule layer: ");
    for (uint32_t i = 0; i < c->n_law; i++) {
        if (i) s_(&b, ", ");
        s_(&b, c->law[i]);
        (void) zxp_family_text(zxp_family(c->law[i]), fam, sizeof fam);
        s_(&b, " (legal family, for information only: ");
        s_(&b, fam);
        s_(&b, ")");
    }
    s_(&b, ".\nChoice of law, forum and dispute resolution: " COUNSEL "\n\n");
    s_(&b, "## 4. Machine-checked payment schedule\n\n");
    uint32_t n_due = 0;
    for (uint32_t i = 0; i < c->n_clause; i++) {
        const zxp_clause_t *k = &c->clause[i];
        if (k->kind != ZXP_CL_DUE) continue;
        n_due++;
        s_(&b, "- ");
        s_(&b, k->id);
        s_(&b, ": ");
        s_(&b, c->party[k->payer].id);
        s_(&b, " pays ");
        s_(&b, c->party[k->payee].id);
        c_(&b, ' ');
        u_(&b, (uint64_t) k->amount);
        c_(&b, ' ');
        s_(&b, k->asset);
        s_(&b, " (smallest unit) by ");
        date_(&b, k->date);
        if (k->after) {
            s_(&b, ", once ");
            s_(&b, c->clause[k->after - 1u].id);
            s_(&b, " has been performed");
        }
        if (k->penalty > 0) {
            s_(&b, "; if not paid by then, a fixed further sum of ");
            u_(&b, (uint64_t) k->penalty);
            c_(&b, ' ');
            s_(&b, k->asset);
            s_(&b, " is due");
        }
        s_(&b, ".\n");
    }
    if (n_due == 0) s_(&b, "No payment terms.\n");
    s_(&b, "\nThe software checks transfers under this agreement against these terms, against "
           "the signature threshold (");
    u_(&b, c->sign);
    s_(&b, " of ");
    u_(&b, c->n_party);
    s_(&b, ") and against the dates ");
    date_(&b, c->window.from);
    s_(&b, " to ");
    date_(&b, c->window.to);
    s_(&b, ". It does not decide disputes: a contested term is held for the parties and their "
           "chosen forum.\n\n");
    s_(&b, "## 5. Other terms\n\nRepresentations, termination, notices, confidentiality, "
           "liability and any terms the governing law requires: " COUNSEL "\n\n");
    s_(&b, "## 6. Signatures\n\n" COUNSEL "\n");
    if (!b.ok) {
        out[0] = 0;
        return 0;
    }
    out[b.n] = 0;
    return b.n;
}
