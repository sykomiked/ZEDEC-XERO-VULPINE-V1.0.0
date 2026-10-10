/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zcap_forms.h — THE canonical index order of the nine forms of capital.
 *
 * Every module that stores, sends or indexes a capital form uses these
 * values: either ZCAP_* directly, or its own names defined as aliases equal
 * to ZCAP_* with a _Static_assert per form at its header boundary
 * (vino.h, swarm_market.h, pay_ledger.h, finance/capital_forms.h,
 * sdk/m5_api.h). A form index handed from one module to another therefore
 * means the same form on both sides (test_capital_canon.c checks this).
 *
 * This header has no dependencies (no surplus.h, no libc), so a module can
 * include it by relative path without new -I flags. zcapital.h includes it
 * and adds the exchange substrate on top.
 *
 *   index  form           authority  other names that alias this index
 *   0      FINANCIAL      Ministry
 *   1      MANUFACTURED   Ministry   vino CAP_MATERIAL / CAP_PHYSICAL,
 *                                    finance CAPITAL_MATERIAL
 *   2      INTELLECTUAL   Ministry   vino CAP_KNOWLEDGE, finance CAPITAL_KNOWLEDGE
 *   3      HUMAN          Ministry   finance CAPITAL_LIVING (developer time)
 *   4      SOCIAL         Crown
 *   5      NATURAL        Crown      vino CAP_LIVING / CAP_LAND (ecosystems)
 *   6      CULTURAL       Crown      finance CAPITAL_HERITAGE_INTELLECTUAL
 *   7      SPIRITUAL      Crown      finance CAPITAL_GOVERNANCE_INSTITUTIONAL
 *   8      SYSTEM         Co-Juris   vino CAP_BUILT / CAP_ECOLOGICAL (legacy),
 *                                    finance CAPITAL_BUILT (infrastructure)
 *
 * Note the one name clash: vino's CAP_LIVING (ecosystems) is NATURAL, while
 * finance's CAPITAL_LIVING (developer time) is HUMAN. Both are aliases of
 * different canonical forms; prefer the ZCAP_* name in new code.
 */
#ifndef ZXV_ZCAP_FORMS_H
#define ZXV_ZCAP_FORMS_H

typedef enum {
    ZCAP_FINANCIAL = 0,
    ZCAP_MANUFACTURED = 1,
    ZCAP_INTELLECTUAL = 2,
    ZCAP_HUMAN = 3,
    ZCAP_SOCIAL = 4,    /* Crown */
    ZCAP_NATURAL = 5,   /* Crown */
    ZCAP_CULTURAL = 6,  /* Crown */
    ZCAP_SPIRITUAL = 7, /* Crown */
    ZCAP_SYSTEM = 8
} zcap_form_t;

#define ZCAP_FORM_COUNT 9

/* True for the four inalienable Crown forms (SOCIAL..SPIRITUAL). */
static inline int zcap_form_is_crown(unsigned form)
{
    return form >= (unsigned) ZCAP_SOCIAL && form <= (unsigned) ZCAP_SPIRITUAL;
}

/* Canonical upper-case name of a form; "UNKNOWN" when out of range. */
static inline const char *zcap_form_name(unsigned form)
{
    static const char *const names[ZCAP_FORM_COUNT] = {"FINANCIAL", "MANUFACTURED", "INTELLECTUAL",
                                                       "HUMAN",     "SOCIAL",       "NATURAL",
                                                       "CULTURAL",  "SPIRITUAL",    "SYSTEM"};
    return form < (unsigned) ZCAP_FORM_COUNT ? names[form] : "UNKNOWN";
}

#endif /* ZXV_ZCAP_FORMS_H */
