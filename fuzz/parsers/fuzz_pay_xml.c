/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* fuzz_pay_xml.c — the strict inbound ISO 20022 parsers in kernel/src/pay
 * (pay_iso_parse_pacs008, pay_iso_parse_camt053) and the structural XML
 * check they sit on (pay_iso_check_xml).
 *
 * The whole input is the document (NOT NUL-terminated: the parsers take a
 * length and promise never to read past it). Properties: on success every
 * output string is NUL-terminated within its field and the counts stay
 * within their arrays. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "pay_iso.h"
#include "fuzz_in.h"

#define TERM(f)                                                                                    \
    do {                                                                                           \
        if (memchr((f), 0, sizeof(f)) == NULL) abort();                                            \
    } while (0)

static pay_pacs008_in_t g_p8;
static pay_camt053_in_t g_c53;

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size > PAY_ISO_IN_MAX + 16u) return 0;
    const char *xml = (const char *) fz_dup(data, size);
    uint32_t len = (uint32_t) size;

    (void) pay_iso_check_xml(xml, len, "Document",
                             "urn:iso:std:iso:20022:tech:xsd:pacs.008.001.08");

    if (pay_iso_parse_pacs008(xml, len, &g_p8) == 0) {
        TERM(g_p8.msg_id);
        TERM(g_p8.cre_dt_tm);
        TERM(g_p8.sttlm_mtd);
        if (g_p8.n_tx > PAY_ISO_MAX_TX) abort();
        for (uint32_t i = 0; i < g_p8.n_tx; i++) {
            const pay_pacs008_in_tx_t *t = &g_p8.tx[i];
            TERM(t->instr_id);
            TERM(t->e2e);
            TERM(t->tx_id);
            TERM(t->uetr);
            TERM(t->amt.ccy);
            TERM(t->dbtr_name);
            TERM(t->cdtr_name);
            TERM(t->dbtr_acct);
            TERM(t->cdtr_acct);
            TERM(t->dbtr_agt_bic);
            TERM(t->cdtr_agt_bic);
            TERM(t->ustrd);
        }
    }
    if (pay_iso_parse_camt053(xml, len, &g_c53) == 0) {
        TERM(g_c53.msg_id);
        TERM(g_c53.stmt_id);
        TERM(g_c53.acct_id);
        TERM(g_c53.acct_ccy);
        if (g_c53.n_bal > PAY_ISO_MAX_BAL || g_c53.n_ntry > PAY_ISO_MAX_NTRY) abort();
        for (uint32_t i = 0; i < g_c53.n_ntry; i++) {
            TERM(g_c53.ntry[i].e2e);
            TERM(g_c53.ntry[i].uetr);
            TERM(g_c53.ntry[i].sts);
        }
    }
    free((void *) xml);
    return 0;
}
