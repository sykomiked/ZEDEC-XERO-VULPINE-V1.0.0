/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* cn_emv.h — BER-TLV codec and an EMV-style data container for the networks.
 *
 * BER-TLV (ISO/IEC 8825-1 subset as used by EMV Book 3 Annex B): tags of 1-3
 * bytes (multi-byte when the low five bits of the first byte are all set;
 * bit 8 of each following byte marks continuation), definite lengths in
 * short form or long form 81 xx / 82 xx xx. Indefinite length (80) and
 * tags or lengths longer than that are rejected. Constructed tags (bit 6 of
 * the first byte) may be nested up to CN_TLV_MAX_DEPTH.
 *
 * EMV tags carried (all standard EMV Book 3 tags except the DF81xx ones):
 *   4F   AID                    F0 5A 58 56 43 4E + network byte (see below)
 *   5A   PAN                    cn 16, BCD, 8 bytes
 *   5F24 expiry date            n 6 YYMMDD (last day of the expiry month)
 *   5F2A currency code          n 4 BCD = 0555
 *   82   AIP                    2 bytes, 00 00 (no EMV features claimed)
 *   95   TVR                    5 bytes, 00 .. 00
 *   9A   transaction date       n 6 YYMMDD
 *   9C   transaction type       n 2 = 00 (purchase)
 *   9F02 amount, authorised     n 12 BCD, 6 bytes
 *   9F16 merchant identifier    ans 15
 *   9F1C terminal identifier    an 8
 *   9F21 transaction time       n 6 hhmmss
 *   9F26 application cryptogram 8 bytes = first 8 of SHA3-256(ML-DSA sig)
 *   9F36 ATC                    2 bytes
 *   9F37 unpredictable number   4 bytes
 *   9F41 transaction seq ctr    n 8 BCD, 4 bytes, carries the STAN
 *   DF8101 (private) full ML-DSA-65 holder signature, 3309 bytes
 *   DF8102 (private) capital form, 1 byte
 *   DF8103 (private) network id, 1 byte
 * The whole set is wrapped in the private constructed template E1.
 *
 * The AID starts with F0, ISO/IEC 7816-5 category F: "proprietary, not
 * registered". No RID has been registered for these networks.
 *
 * HONEST LIMITS. This carries Phoenix / Dragon / Thunderbird data INSIDE an
 * EMV-shaped TLV message so EMV-aware software can parse and route it. It is
 * not an EMV kernel, it implements none of the EMV card/terminal protocol
 * (no SELECT/GPO/READ RECORD, no CVM, no offline data authentication), and
 * 9F26 is NOT an EMV ARQC: it is a digest of a post-quantum signature that
 * only this issuer can check. EMVCo certification, scheme CA public keys,
 * issuer master keys and any card personalisation are out of scope.
 */
#ifndef ZXV_CN_EMV_H
#define ZXV_CN_EMV_H

#include <stdint.h>
#include <stdbool.h>
#include "cardnet.h"

#define CN_TLV_MAX_DEPTH 4u

typedef enum {
    CN_TLV_OK = 0,
    CN_TLV_ERR_ARG = -1,
    CN_TLV_ERR_SPACE = -2,
    CN_TLV_ERR_TAG = -3, /* malformed or > 3-byte tag */
    CN_TLV_ERR_LEN = -4, /* indefinite, oversized or truncated length */
    CN_TLV_ERR_DEPTH = -5,
    CN_TLV_ERR_MISSING = -6,
    CN_TLV_ERR_VALUE = -7 /* a tag is present but its value is wrong */
} cn_tlv_rc_t;

typedef struct {
    uint8_t *buf;
    uint32_t cap, len;
    int err; /* first error, sticky */
} cn_tlv_writer_t;

void cn_tlv_writer_init(cn_tlv_writer_t *w, uint8_t *buf, uint32_t cap);
/* Append one TLV. tag is written big-endian in its 1..3 byte form. */
cn_tlv_rc_t cn_tlv_put(cn_tlv_writer_t *w, uint32_t tag, const uint8_t *val, uint32_t len);
bool cn_tlv_tag_valid(uint32_t tag);
bool cn_tlv_constructed(uint32_t tag);

/* Read the TLV at *pos within buf[0..len); advances *pos past it. */
cn_tlv_rc_t cn_tlv_next(const uint8_t *buf, uint32_t len, uint32_t *pos, uint32_t *tag,
                        const uint8_t **val, uint32_t *vlen);
/* Depth-first search for tag, descending into constructed TLVs. Also
 * validates the structure it walks; a malformed container is an error. */
cn_tlv_rc_t cn_tlv_find(const uint8_t *buf, uint32_t len, uint32_t tag, const uint8_t **val,
                        uint32_t *vlen);

/* Tags */
#define CN_EMV_AID         0x4Fu
#define CN_EMV_PAN         0x5Au
#define CN_EMV_EXPIRY      0x5F24u
#define CN_EMV_CURRENCY    0x5F2Au
#define CN_EMV_AIP         0x82u
#define CN_EMV_TVR         0x95u
#define CN_EMV_TXN_DATE    0x9Au
#define CN_EMV_TXN_TYPE    0x9Cu
#define CN_EMV_AMOUNT      0x9F02u
#define CN_EMV_MERCHANT_ID 0x9F16u
#define CN_EMV_TERMINAL_ID 0x9F1Cu
#define CN_EMV_TXN_TIME    0x9F21u
#define CN_EMV_CRYPTOGRAM  0x9F26u
#define CN_EMV_ATC         0x9F36u
#define CN_EMV_UN          0x9F37u
#define CN_EMV_TSC         0x9F41u
#define CN_EMV_ZXV_SIG     0xDF8101u
#define CN_EMV_ZXV_FORM    0xDF8102u
#define CN_EMV_ZXV_NET     0xDF8103u
#define CN_EMV_TEMPLATE    0xE1u

/* BCD helpers. cn_bcd_from_digits packs an even or odd digit string, padding
 * an odd tail with F (as EMV does for 5A). */
cn_tlv_rc_t cn_bcd_from_digits(const char *digits, uint32_t ndigits, uint8_t *out, uint32_t cap,
                               uint32_t *out_len);
/* Unsigned value as exactly nbytes of BCD (2*nbytes digits). */
cn_tlv_rc_t cn_bcd_from_u64(uint64_t v, uint32_t nbytes, uint8_t *out);
cn_tlv_rc_t cn_bcd_to_u64(const uint8_t *bcd, uint32_t nbytes, uint64_t *v);

/* Worst-case size of an encoded authorization container. */
#define CN_EMV_AUTH_MAX 3500u

/* Encode an authorization request (plus the card's expiry and the holder
 * signature) as an E1 container. */
cn_tlv_rc_t cn_emv_encode_auth(const cn_auth_req_t *req, uint32_t exp_year, uint32_t exp_month,
                               const uint8_t sig[CN_SIG_BYTES], uint8_t *out, uint32_t cap,
                               uint32_t *out_len);
/* Decode it back. Checks 9F26 against the carried signature, the AID
 * against the PAN's network, and every fixed length. */
cn_tlv_rc_t cn_emv_decode_auth(const uint8_t *buf, uint32_t len, cn_auth_req_t *req,
                               uint8_t sig[CN_SIG_BYTES]);

#endif /* ZXV_CN_EMV_H */
