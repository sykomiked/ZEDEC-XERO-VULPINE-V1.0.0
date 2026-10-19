/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* fuzz_cobol_copybook.c — legacy bridge: COBOL copybook parser and record codec.
 *
 * Input: u16 copybook length L, then L bytes of copybook text, then the
 * record image. The copybook is parsed; every field it yields is decoded
 * from (and re-encoded into) the record; the record image is also walked as
 * fixed-length and as RDW variable-length records, and the raw COMP-3 /
 * zoned / COMP decoders run on its bytes. Buffers are exact-size heap
 * copies so a read one byte past the end is an ASan report. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "cobol.h"
#include "fuzz_in.h"

static cob_layout g_lo;

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 2) return 0;
    uint32_t L = (uint32_t) data[0] | ((uint32_t) data[1] << 8);
    data += 2;
    size -= 2;
    if (L > size) L = (uint32_t) size;
    char *text = (char *) fz_dup(data, L);
    uint32_t rlen = (uint32_t) (size - L);
    uint8_t *rec = fz_dup(data + L, rlen);

    memset(&g_lo, 0, sizeof g_lo);
    if (cobol_parse_copybook(text, L, &g_lo)) {
        if (g_lo.nfields > COB_MAX_FIELDS) abort();
        for (uint32_t i = 0; i < g_lo.nfields; i++) {
            const cob_field *f = &g_lo.fields[i];
            if (memchr(f->name, 0, COB_MAX_NAME) == NULL) abort(); /* name must be terminated */
            if (cobol_find(&g_lo, f->name) == NULL) abort();
            (void) cobol_find_indexed(&g_lo, f->name, f->occurs_index);
            int64_t v = 0;
            if (f->type == COB_ALPHA) {
                uint8_t out[64];
                uint32_t n = 0;
                if (cobol_get_text(rec, rlen, f, out, sizeof out, &n) && n > sizeof out) abort();
                (void) cobol_set_text(rec, rlen, f, out, n);
            } else if (cobol_get_int(rec, rlen, f, &v)) {
                /* a decoded value must re-encode into the same field */
                if (!cobol_set_int(rec, rlen, f, v)) abort();
                int64_t w = 0;
                if (!cobol_get_int(rec, rlen, f, &w) || w != v) abort();
            }
        }
    }

    /* record readers over the same image */
    for (uint32_t rs = 1; rs <= 16 && rlen; rs *= 2) {
        uint32_t out_len = 0;
        for (uint32_t i = 0; i < 64; i++) {
            const uint8_t *r = cobol_fixed_record(rec, rlen, rs, i, &out_len);
            if (!r) break;
            if (r < rec || r + out_len > rec + rlen) abort();
        }
    }
    cob_rdw_iter it;
    cobol_rdw_init(&it, rec, rlen);
    uint32_t got = 0;
    for (int i = 0; i < 4096; i++) {
        const uint8_t *r = cobol_rdw_next(&it, &got);
        if (!r) break;
        if (r < rec || r + got > rec + rlen) abort();
    }

    /* raw numeric decoders on prefixes of the record */
    if (rlen) {
        uint32_t w = (uint32_t) rec[0] % 20u;
        int64_t v = 0;
        if (w <= rlen) {
            (void) comp3_decode(rec, w, &v);
            (void) zoned_decode(rec, w, &v);
            (void) comp_decode(rec, w, (rec[0] & 1) != 0, &v);
        }
    }

    free(text);
    free(rec);
    return 0;
}
