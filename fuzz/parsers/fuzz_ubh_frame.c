/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* fuzz_ubh_frame.c — UBH-168 frame header unpack/validate/pack, the 21-octet
 * <-> 24-septet / 28-sextet regroupings, and format detection.
 *
 * Properties checked on every 21-byte window of the input:
 *   - unpack succeeds => pack(unpack(x)) unpacks to the same header
 *   - octets -> septets -> octets and octets -> sextets -> octets are the
 *     identity, and ubh_168_roundtrip_test agrees
 *   - name lookups for any enum value return a non-NULL string */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "ubh.h"
#include "fuzz_in.h"

static int hdr_eq(const ubh_168_header_t *a, const ubh_168_header_t *b)
{
    return memcmp(a->magic, b->magic, 3) == 0 && a->version == b->version &&
           a->frame_class == b->frame_class && a->source_format_id == b->source_format_id &&
           a->target_type == b->target_type && a->order_flags == b->order_flags &&
           a->flags == b->flags && a->payload_length == b->payload_length &&
           a->schema_id == b->schema_id && memcmp(a->integrity_ref, b->integrity_ref, 3) == 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    uint8_t *buf = fz_dup(data, size);

    (void) ubh_detect_format(buf, (uint32_t) size);

    for (size_t off = 0; off + UBH_168_OCTETS <= size && off < 21u * 64u; off += UBH_168_OCTETS) {
        const uint8_t *w = buf + off;
        ubh_168_header_t h, h2;
        memset(&h, 0, sizeof h);
        if (ubh_168_header_unpack(&h, w)) {
            (void) ubh_168_header_validate(&h);
            uint8_t out[UBH_168_OCTETS];
            ubh_168_header_pack(&h, out);
            memset(&h2, 0, sizeof h2);
            if (!ubh_168_header_unpack(&h2, out) || !hdr_eq(&h, &h2)) abort();
        }

        uint8_t sep[UBH_168_SEPTETS], sex[UBH_168_SEXTETS], back[UBH_168_OCTETS];
        if (ubh_octets_to_septets(w, sep)) {
            for (int i = 0; i < UBH_168_SEPTETS; i++)
                if (sep[i] > 0x7Fu) abort();
            if (!ubh_septets_to_octets(sep, back) || memcmp(back, w, UBH_168_OCTETS) != 0) abort();
        }
        if (ubh_octets_to_sextets(w, sex)) {
            for (int i = 0; i < UBH_168_SEXTETS; i++)
                if (sex[i] > 0x3Fu) abort();
            if (!ubh_sextets_to_octets(sex, back) || memcmp(back, w, UBH_168_OCTETS) != 0) abort();
        }
        if (!ubh_168_roundtrip_test(w)) abort();
    }

    /* untrusted septet / sextet streams (values may exceed 7 / 6 bits) */
    if (size >= UBH_168_SEXTETS) {
        uint8_t back[UBH_168_OCTETS];
        (void) ubh_septets_to_octets(buf, back);
        (void) ubh_sextets_to_octets(buf, back);
    }

    if (size >= 1) {
        uint8_t v = buf[0];
        if (!ubh_frame_class_name((ubh_frame_class_t) v)) abort();
        if (!ubh_format_name((ubh_format_id_t) v)) abort();
        if (!ubh_trust_name((ubh_trust_level_t) v)) abort();
        if (!ubh7_operator_name((ubh7_operator_t) v)) abort();
        if (!ubh7_profile_name((ubh7_profile_t) v)) abort();
    }

    free(buf);
    return 0;
}
