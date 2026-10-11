/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* za_fix.c — FIX tag=value messages (FIX 4.x and FIXT 1.1 framing).
 *
 * Checked, each a refusal of the whole message:
 *   - every field is TAG=VALUE<delim>, TAG a positive integer without
 *     leading zeros (at most 9 digits), VALUE non-empty UTF-8;
 *   - the message ends with a delimiter;
 *   - BeginString (8), BodyLength (9) and MsgType (35) are the first three
 *     fields and CheckSum (10) is the last (FIX_HEADER);
 *   - BodyLength equals the bytes from the start of field 35 up to and
 *     including the delimiter before "10=" (FIX_LENGTH);
 *   - CheckSum is three digits equal to the byte sum of everything before
 *     "10=", modulo 256 (FIX_CHECKSUM). With ZA_SRC_FIX_PIPE each '|' is
 *     counted as the SOH (0x01) it stands for, so a message keeps its
 *     checksum when its SOHs are printed as '|';
 *   - price, quantity and integer fields (the table below) are exact
 *     decimals without exponents (NUMBER otherwise); FIX data fields
 *     (RawData and the other length-prefixed binary fields) are refused
 *     (UNSUPPORTED), since a data field may itself contain the delimiter.
 *
 * Fields keep their order. A repeated tag (a repeating group) is kept with
 * an occurrence suffix: the second "Price" is "Price.1". Groups are not
 * interpreted. Integer code only (R11). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "za_internal.h"

#define FIX_TEXT 0
#define FIX_NUM  1 /* Price, Qty, Amt, int: an exact decimal */

typedef struct {
    uint32_t tag;
    const char *name;
    int type;
} fix_def;

static const fix_def k_defs[] = {
    {1, "Account", FIX_TEXT},      {6, "AvgPx", FIX_NUM},        {7, "BeginSeqNo", FIX_NUM},
    {8, "BeginString", FIX_TEXT},  {9, "BodyLength", FIX_NUM},   {10, "CheckSum", FIX_TEXT},
    {11, "ClOrdID", FIX_TEXT},     {14, "CumQty", FIX_NUM},      {15, "Currency", FIX_TEXT},
    {16, "EndSeqNo", FIX_NUM},     {17, "ExecID", FIX_TEXT},     {21, "HandlInst", FIX_TEXT},
    {31, "LastPx", FIX_NUM},       {32, "LastQty", FIX_NUM},     {34, "MsgSeqNum", FIX_NUM},
    {35, "MsgType", FIX_TEXT},     {36, "NewSeqNo", FIX_NUM},    {37, "OrderID", FIX_TEXT},
    {38, "OrderQty", FIX_NUM},     {39, "OrdStatus", FIX_TEXT},  {40, "OrdType", FIX_TEXT},
    {41, "OrigClOrdID", FIX_TEXT}, {44, "Price", FIX_NUM},       {45, "RefSeqNum", FIX_NUM},
    {49, "SenderCompID", FIX_TEXT}, {52, "SendingTime", FIX_TEXT}, {54, "Side", FIX_TEXT},
    {55, "Symbol", FIX_TEXT},      {56, "TargetCompID", FIX_TEXT}, {58, "Text", FIX_TEXT},
    {59, "TimeInForce", FIX_TEXT}, {60, "TransactTime", FIX_TEXT}, {98, "EncryptMethod", FIX_NUM},
    {99, "StopPx", FIX_NUM},       {108, "HeartBtInt", FIX_NUM}, {112, "TestReqID", FIX_TEXT},
    {117, "QuoteID", FIX_TEXT},    {132, "BidPx", FIX_NUM},      {133, "OfferPx", FIX_NUM},
    {134, "BidSize", FIX_NUM},     {135, "OfferSize", FIX_NUM},  {150, "ExecType", FIX_TEXT},
    {151, "LeavesQty", FIX_NUM},   {152, "CashOrderQty", FIX_NUM}, {262, "MDReqID", FIX_TEXT},
    {268, "NoMDEntries", FIX_NUM}, {269, "MDEntryType", FIX_TEXT}, {270, "MDEntryPx", FIX_NUM},
    {271, "MDEntrySize", FIX_NUM}, {1128, "ApplVerID", FIX_TEXT},
};

/* Data fields (FIX type "data"): their value may contain the delimiter. */
static const uint32_t k_data_tags[] = {89, 91, 96, 213, 349, 351, 353, 355, 357, 359, 619, 622};

static const fix_def *find_def(uint32_t tag) {
    for (size_t i = 0; i < sizeof k_defs / sizeof k_defs[0]; i++)
        if (k_defs[i].tag == tag) return &k_defs[i];
    return NULL;
}

typedef struct {
    uint32_t tag;
    size_t start, vstart, vend; /* field start, value [vstart, vend) */
} fixf;

#define FIX_MAX_FIELDS ZA_MAX_FIELDS

void za_fix_into(za_record *rec, const uint8_t *s, size_t n, uint8_t delim) {
    static _Thread_local fixf f[FIX_MAX_FIELDS];
    uint32_t nf = 0;
    size_t pos = 0;
    const char *sender = NULL, *target = NULL;
    size_t slen = 0, tlen = 0;

    if (n == 0) {
        za_rec_refuse(rec, ZA_R_MALFORMED, "FIX: empty message");
        return;
    }
    if (s[n - 1u] != delim) {
        za_rec_refuse(rec, ZA_R_MALFORMED, "FIX: the message does not end with a delimiter");
        return;
    }
    while (pos < n) {
        size_t start = pos;
        uint64_t tag = 0;
        size_t td = 0;
        while (pos < n && s[pos] >= '0' && s[pos] <= '9') {
            if (td == 0 && s[pos] == '0') {
                za_rec_refuse(rec, ZA_R_MALFORMED, "FIX at byte %zu: a tag with a leading zero", pos);
                return;
            }
            if (++td > 9u) {
                za_rec_refuse(rec, ZA_R_MALFORMED, "FIX at byte %zu: a tag longer than 9 digits", start);
                return;
            }
            tag = tag * 10u + (uint64_t) (s[pos] - '0');
            pos++;
        }
        if (td == 0 || pos >= n || s[pos] != '=') {
            za_rec_refuse(rec, ZA_R_MALFORMED, "FIX at byte %zu: expected TAG=VALUE", start);
            return;
        }
        pos++;
        size_t vs = pos;
        while (pos < n && s[pos] != delim) pos++;
        if (pos == vs) {
            za_rec_refuse(rec, ZA_R_MALFORMED, "FIX at byte %zu: tag %u has an empty value", vs,
                          (unsigned) tag);
            return;
        }
        if (nf >= FIX_MAX_FIELDS) {
            za_rec_refuse(rec, ZA_R_LIMIT, "FIX: more than %u fields", (unsigned) FIX_MAX_FIELDS);
            return;
        }
        f[nf].tag = (uint32_t) tag;
        f[nf].start = start;
        f[nf].vstart = vs;
        f[nf].vend = pos;
        nf++;
        pos++; /* the delimiter */
    }
    if (nf < 4u || f[0].tag != 8u || f[1].tag != 9u || f[2].tag != 35u || f[nf - 1u].tag != 10u) {
        za_rec_refuse(rec, ZA_R_FIX_HEADER,
                      "FIX: 8 (BeginString), 9 (BodyLength), 35 (MsgType) must come first and "
                      "10 (CheckSum) last");
        return;
    }
    for (uint32_t i = 1; i + 1u < nf; i++) {
        if (f[i].tag == 8u || f[i].tag == 10u || (i != 1u && f[i].tag == 9u)) {
            za_rec_refuse(rec, ZA_R_FIX_HEADER, "FIX: tag %u repeated inside the message",
                          (unsigned) f[i].tag);
            return;
        }
    }
    /* BodyLength */
    {
        uint64_t bl = 0;
        for (size_t k = f[1].vstart; k < f[1].vend; k++) {
            if (s[k] < '0' || s[k] > '9' || bl > 100000000u) {
                za_rec_refuse(rec, ZA_R_FIX_LENGTH, "FIX: BodyLength is not a small non-negative integer");
                return;
            }
            bl = bl * 10u + (uint64_t) (s[k] - '0');
        }
        size_t body = f[nf - 1u].start - f[2].start;
        if (bl != body) {
            za_rec_refuse(rec, ZA_R_FIX_LENGTH, "FIX: BodyLength says %llu, the body has %zu bytes",
                          (unsigned long long) bl, body);
            return;
        }
    }
    /* CheckSum */
    {
        uint32_t sum = 0, want = 0;
        const fixf *c = &f[nf - 1u];
        for (size_t k = 0; k < c->start; k++) sum += s[k] == delim ? 1u : s[k];
        sum &= 0xFFu;
        if (c->vend - c->vstart != 3u) {
            za_rec_refuse(rec, ZA_R_FIX_CHECKSUM, "FIX: CheckSum must be three digits");
            return;
        }
        for (size_t k = c->vstart; k < c->vend; k++) {
            if (s[k] < '0' || s[k] > '9') {
                za_rec_refuse(rec, ZA_R_FIX_CHECKSUM, "FIX: CheckSum must be three digits");
                return;
            }
            want = want * 10u + (uint32_t) (s[k] - '0');
        }
        if (want != sum) {
            za_rec_refuse(rec, ZA_R_FIX_CHECKSUM, "FIX: CheckSum says %03u, the bytes sum to %03u",
                          (unsigned) want, (unsigned) sum);
            return;
        }
    }
    /* the fields */
    for (uint32_t i = 0; i < nf; i++) {
        const uint8_t *v = s + f[i].vstart;
        size_t vl = f[i].vend - f[i].vstart;
        char name[64];
        int nl;
        for (size_t k = 0; k < sizeof k_data_tags / sizeof k_data_tags[0]; k++) {
            if (f[i].tag == k_data_tags[k]) {
                za_rec_refuse(rec, ZA_R_UNSUPPORTED, "FIX: data field %u (binary data) is not supported",
                              (unsigned) f[i].tag);
                return;
            }
        }
        if (!za_utf8_ok(v, vl)) {
            za_rec_refuse(rec, ZA_R_UTF8, "FIX: the value of tag %u is not valid UTF-8",
                          (unsigned) f[i].tag);
            return;
        }
        const fix_def *d = find_def(f[i].tag);
        uint32_t occ = 0;
        for (uint32_t k = 0; k < i; k++)
            if (f[k].tag == f[i].tag) occ++;
        if (d) nl = occ ? snprintf(name, sizeof name, "%s.%u", d->name, (unsigned) occ)
                        : snprintf(name, sizeof name, "%s", d->name);
        else nl = occ ? snprintf(name, sizeof name, "Tag%u.%u", (unsigned) f[i].tag, (unsigned) occ)
                      : snprintf(name, sizeof name, "Tag%u", (unsigned) f[i].tag);
        int r;
        if (d && d->type == FIX_NUM) {
            int64_t num = 0, den = 1;
            int plain = 1;
            for (size_t k = 0; k < vl; k++)
                if (v[k] == 'e' || v[k] == 'E') plain = 0;
            if (!plain || za_decimal_parse((const char *) v, vl, &num, &den) != ZXV_OK) {
                za_rec_refuse(rec, ZA_R_NUMBER, "FIX: %s (%u) is not an exact decimal: \"%.*s\"",
                              d->name, (unsigned) f[i].tag, (int) (vl > 40u ? 40u : vl), (const char *) v);
                return;
            }
            r = za_rec_add(rec, name, (size_t) nl, ZA_F_NUMBER, num, den, v, vl);
        } else {
            r = za_rec_add(rec, name, (size_t) nl, ZA_F_TEXT, 0, 1, v, vl);
        }
        if (r < 0) return;
        if (r) {
            za_rec_refuse(rec, r, "FIX: field %u cannot be kept", (unsigned) f[i].tag);
            return;
        }
        if (f[i].tag == 49u && !occ) {
            sender = (const char *) v;
            slen = vl;
        }
        if (f[i].tag == 56u && !occ) {
            target = (const char *) v;
            tlen = vl;
        }
    }
    if (sender && target && rec->source[0] == '\0') {
        char src[300];
        int k = snprintf(src, sizeof src, "%.*s->%.*s", (int) (slen > 140u ? 140u : slen), sender,
                         (int) (tlen > 140u ? 140u : tlen), target);
        char *ns = (char *) realloc(rec->source, (size_t) k + 1u);
        if (ns) {
            memcpy(ns, src, (size_t) k + 1u);
            rec->source = ns;
        }
    }
}
