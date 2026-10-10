/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* cn_iso8583.h — minimal ISO 8583:1987 codec for the card networks.
 *
 * Message types: 0100/0110 (authorization), 0200/0210 (financial),
 * 0400/0410 (reversal). Wire layout: 4 ASCII MTI digits, an 8-byte binary
 * primary bitmap, an 8-byte binary secondary bitmap when bit 1 is set, then
 * the present fields in ascending order, all ASCII. Variable fields carry an
 * ASCII length prefix (LL or LLL).
 *
 * Fields supported (anything else in a bitmap is rejected on decode):
 *    2 PAN                n..19 LLVAR     37 retrieval ref no   an 12
 *    3 processing code    n 6             38 approval code      an 6
 *    4 amount, minor      n 12            39 response code      an 2
 *    7 transmission time  n 10 MMDDhhmmss 41 terminal id        ans 8
 *   11 STAN               n 6             42 card acceptor id   ans 15
 *   12 local time         n 6 hhmmss      49 currency           n 3
 *   13 local date         n 4 MMDD        60/61/63 private      ans..999 LLLVAR
 *   90 original data elements n 42 (secondary bitmap; used by 0400/0410)
 *
 * VSS metadata (VSS proposal section 4.2) rides in the private fields:
 *   60  "VSS1;NET=<D|P|T>;FORM=<0-8>;DR=555;CR=777;EQ=888"
 *   61  "RCPT=<receipt id>;ATC=<atc>"
 *   63  "CRY=<64 hex: SHA3-256 of the ML-DSA signature>"
 *
 * HONEST LIMITS. This is a codec for interoperating with ISO 8583 tooling,
 * not network membership and not a certified host interface: every real
 * network publishes its own field dialect (encodings, sub-fields, private
 * use), which this does not implement. The full ML-DSA-65 signature (3309
 * bytes) does not fit any 1987 field (max 999); field 63 carries only its
 * SHA3-256 commitment, so the signature itself must travel in the EMV TLV
 * container (cn_emv.h) or a side channel for the issuer to verify it.
 * No MAC (field 64/128) is computed.
 */
#ifndef ZXV_CN_ISO8583_H
#define ZXV_CN_ISO8583_H

#include <stdint.h>
#include <stdbool.h>
#include "cardnet.h"

#define CN8583_POOL     4096u
#define CN8583_MAX_WIRE 4200u

typedef enum {
    CN8583_OK = 0,
    CN8583_ERR_ARG = -1,
    CN8583_ERR_MTI = -2,
    CN8583_ERR_FIELD = -3,  /* unsupported field number */
    CN8583_ERR_FORMAT = -4, /* wrong characters or length for the field */
    CN8583_ERR_SPACE = -5,  /* pool or output buffer too small */
    CN8583_ERR_TRUNC = -6,  /* input ended inside a field */
    CN8583_ERR_MISSING = -7 /* a mandatory field for this MTI is absent */
} cn8583_rc_t;

typedef struct {
    uint32_t mti;
    uint8_t bitmap[16];
    uint16_t off[129];
    uint16_t len[129];
    uint8_t pool[CN8583_POOL];
    uint32_t used;
} cn8583_msg_t;

bool cn8583_mti_supported(uint32_t mti);
cn8583_rc_t cn8583_init(cn8583_msg_t *m, uint32_t mti);
bool cn8583_has(const cn8583_msg_t *m, uint32_t field);
/* Set a field (validated against its format). Setting twice replaces it. */
cn8583_rc_t cn8583_set(cn8583_msg_t *m, uint32_t field, const char *data, uint32_t len);
/* Fixed numeric field from an integer (zero padded). */
cn8583_rc_t cn8583_set_num(cn8583_msg_t *m, uint32_t field, uint64_t v);
cn8583_rc_t cn8583_get(const cn8583_msg_t *m, uint32_t field, const char **data, uint32_t *len);
cn8583_rc_t cn8583_get_num(const cn8583_msg_t *m, uint32_t field, uint64_t *v);

/* Mandatory-field check per MTI:
 *   0100/0200: 2 3 4 7 11 41 42 49     0110/0210: 3 4 11 39 41
 *   0400: 2 3 4 11 90                  0410: 3 4 11 39 90 */
cn8583_rc_t cn8583_check_mandatory(const cn8583_msg_t *m);

cn8583_rc_t cn8583_pack(const cn8583_msg_t *m, uint8_t *out, uint32_t cap, uint32_t *out_len);
cn8583_rc_t cn8583_unpack(cn8583_msg_t *m, const uint8_t *in, uint32_t len);

/* ---- VSS metadata in 60/61/63 ---- */
typedef struct {
    uint32_t network; /* cn_network_t */
    uint32_t form;    /* cn_form_t */
    uint32_t rail_dr, rail_cr, rail_eq;
    uint32_t receipt; /* receipt id, 0..999999999 */
    uint32_t atc;
    uint8_t sig_digest[32];
} cn_vss_meta_t;

cn8583_rc_t cn8583_put_vss(cn8583_msg_t *m, const cn_vss_meta_t *v);
cn8583_rc_t cn8583_get_vss(const cn8583_msg_t *m, cn_vss_meta_t *v);

/* ---- Mapping to and from cardnet ----
 * Builds a 0100 or 0200 from an auth request (fields 2 3 4 7 11 12 13 41 42
 * 49, plus VSS 60/61/63 from the signature digest). Processing code 000000
 * (purchase). Fields 7/12/13 come from req->time, in UTC. */
cn8583_rc_t cn8583_from_auth_req(cn8583_msg_t *m, uint32_t mti, const cn_auth_req_t *req,
                                 const uint8_t sig[CN_SIG_BYTES]);
/* Response (0110/0210) for an authorization record: echoes 3 4 11 41,
 * adds 37 (derived from STAN), 38 when approved, 39. */
cn8583_rc_t cn8583_response_for(cn8583_msg_t *m, uint32_t mti, const cn_auth_t *au);
/* Recover the parts of an auth request a 0100/0200 carries. The year is not
 * in ISO 8583 fields 7/12/13, so the caller supplies it; the terminal's
 * unpredictable number and the ATC must come from the EMV data (cn_emv.h),
 * except that ATC is also read back from field 61 when present. */
cn8583_rc_t cn8583_to_auth_req(const cn8583_msg_t *m, uint32_t year, cn_auth_req_t *req);

#endif /* ZXV_CN_ISO8583_H */
