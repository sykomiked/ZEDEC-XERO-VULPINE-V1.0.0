/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* fuzz_vinea.c — Vinea wire decoding: the UBH-168 frame wrapper
 * (vna_frame_is_ubh / vna_frame_unwrap / vna_frame_accept), the schema
 * unpacker for every published schema, the agreement decoder, the command
 * line decoder and the Hackronomicon canonical-form check.
 *
 * Input: byte 0 picks the schema, the rest is the wire bytes. Each record
 * is unpacked into an exact-size heap object (so a write past the struct is
 * an ASan report). Property: a record that unpacks re-packs to the very
 * same bytes (the schema rules S1-S8 make the encoding canonical). */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "vna_agree.h"
#include "vna_cmd.h"
#include "vna_econ.h"
#include "vna_file.h"
#include "vna_frame.h"
#include "vna_lan.h"
#include "vna_schema.h"
#include "vna_session.h"
#include "vna_wire.h"
#include "fuzz_in.h"

typedef struct {
    const vna_schema_t *s;
    size_t size;
} sch_t;

static const sch_t SCHEMAS[] = {
    {&vna_msg_schema, sizeof(vna_msg_t)},
    {&vna_b_key_schema, sizeof(vna_b_key_t)},
    {&vna_b_nodes_schema, sizeof(vna_b_nodes_t)},
    {&vna_b_rec_schema, sizeof(vna_b_rec_t)},
    {&vna_b_ack_schema, sizeof(vna_b_ack_t)},
    {&vna_b_hk_schema, sizeof(vna_b_hk_t)},
    {&vna_rec_schema, sizeof(vna_rec_t)},
    {&vna_provider_schema, sizeof(vna_provider_t)},
    {&vna_noderec_schema, sizeof(vna_noderec_t)},
    {&vna_spool_item_schema, sizeof(vna_spool_item_t)},
    {&vna_b_spool_ack_schema, sizeof(vna_b_spool_ack_t)},
    {&vna_agreement_schema, sizeof(vna_agreement_t)},
    {&vna_receipt_schema, sizeof(vna_receipt_t)},
    {&vna_lentry_schema, sizeof(vna_lentry_t)},
    {&vna_chunk_req_schema, sizeof(vna_chunk_req_t)},
    {&vna_chunk_schema, sizeof(vna_chunk_t)},
    {&vna_lan_msg_schema, sizeof(vna_lan_msg_t)},
    {&vna_hs1_schema, sizeof(vna_hs1_t)},
    {&vna_hs2_schema, sizeof(vna_hs2_t)},
    {&vna_hs3_schema, sizeof(vna_hs3_t)},
};
#define NSCH (sizeof SCHEMAS / sizeof SCHEMAS[0])

static void unpack_check(const sch_t *sc, const uint8_t *in, uint32_t len)
{
    void *rec = malloc(sc->size);
    if (!rec) abort();
    memset(rec, 0xA5, sc->size);
    uint32_t sig_off = 0;
    int32_t r = vna_schema_unpack(sc->s, in, len, rec, &sig_off);
    if (r >= 0) {
        if ((uint32_t) r != len || sig_off > len) abort();
        uint32_t cap = vna_schema_max_len(sc->s);
        if (len > cap) abort();
        uint8_t *out = (uint8_t *) malloc(cap ? cap : 1u);
        if (!out) abort();
        int32_t w = vna_schema_pack(sc->s, rec, out, cap, true);
        if (w != (int32_t) len || memcmp(out, in, len) != 0) abort(); /* canonical */
        free(out);
    }
    free(rec);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 1 || size > 1u << 20) return 0;
    uint8_t sel = data[0];
    uint8_t *buf = fz_dup(data + 1, size - 1);
    uint32_t len = (uint32_t) (size - 1);

    /* framing: plain or UBH-168 */
    (void) vna_frame_is_ubh(buf, len);
    const uint8_t *rec = NULL;
    uint32_t rec_len = 0;
    uint16_t schema_id = 0;
    uint8_t frame_class = 0;
    if (vna_frame_unwrap(buf, len, &rec, &rec_len, &schema_id, &frame_class) == VNA_OK) {
        if (!rec || rec < buf || rec + rec_len > buf + len) abort();
    }

    const sch_t *sc = &SCHEMAS[sel % NSCH];
    bool was_ubh = false;
    rec = NULL;
    rec_len = 0;
    if (vna_frame_accept(sc->s, buf, len, &rec, &rec_len, &was_ubh) == VNA_OK) {
        if (!rec || rec < buf || rec + rec_len > buf + len) abort();
        unpack_check(sc, rec, rec_len);
    }
    unpack_check(sc, buf, len);

    /* text decoders */
    vna_agreement_t *a = (vna_agreement_t *) malloc(sizeof *a);
    if (!a) abort();
    (void) vna_agree_decode(buf, len, a);
    free(a);
    vna_cmd_t cmd;
    (void) vna_cmd_decode(buf, len, &cmd);
    (void) vna_hk_is_canonical(buf, len);

    free(buf);
    return 0;
}
