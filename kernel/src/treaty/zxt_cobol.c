/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxt_cobol.c — CALL 'ZXV_ENGINE' USING ZXV-TREATY ZXV-RESULT (zxt_exec.h).
 *
 * GnuCOBOL (and IBM Enterprise COBOL with a C linkage stub) passes each
 * USING item BY REFERENCE, i.e. as a pointer to its first byte. The
 * records are laid out by copybooks/ZXVTRTY.cpy and ZXVRSLT.cpy. */
#include "zxt_exec.h"

int ZXV_ENGINE(const char *treaty, char *result)
{
    static zxv_engine_t ws;
    static zxt_treaty_t t;
    static zxt_result_t r;
    uint8_t seed[32];
    uint32_t where = 0;
    if (!treaty || !result) return ZXT_E_ARG;
    /* Posting ids only need to differ between treaties, not to be secret:
     * the seed is the 32 record bytes after the magic (name and asset). */
    for (uint32_t i = 0; i < 32u; i++) seed[i] = (uint8_t) treaty[4u + i];
    zxt_status_t st = zxt_parse_record((const uint8_t *) treaty, ZXT_REC_LEN, &t, &where);
    if (st == ZXT_OK) {
        (void) zxt_exec(&t, &ws, seed, &r);
    } else {
        for (uint32_t i = 0; i < (uint32_t) sizeof(r); i++) ((volatile uint8_t *) &r)[i] = 0;
        r.status = st;
        r.where = where;
    }
    (void) zxt_emit_result(&r, (uint8_t *) result, ZXT_RES_LEN);
    return r.status;
}
