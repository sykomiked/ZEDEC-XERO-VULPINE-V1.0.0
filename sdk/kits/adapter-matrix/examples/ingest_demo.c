/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* ingest_demo.c — four inbound payloads through the adapters, validated
 * against examples/market.zc, with an evidence log and one attestation.
 *
 *   ingest_demo examples/market.zc
 *
 * The payloads are written here, as the bytes an HTTP client, an MQTT
 * client or a FIX session would have handed over: nothing is fetched. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zadapter.h"

struct inbound {
    int32_t kind;
    const char *source; /* a URL, an MQTT topic name, or "" for FIX */
    const char *payload;
    const char *fn;     /* the contract function the record must satisfy */
};

static const struct inbound k_inbound[] = {
    {ZA_SRC_JSON, "https://bank.example/api/payments/42",
     "{\"amount\": {\"value\": 1250.50, \"currency\": \"EUR\"},"
     " \"reference\": \"INV-2026-0042\", \"urgent\": false}",
     "payment"},
    {ZA_SRC_JSON, "https://bank.example/api/payments/43",
     "{\"amount\": {\"value\": 0.105, \"currency\": \"EUR\"},"
     " \"reference\": \"INV-2026-0043\", \"urgent\": false}",
     "payment"},
    {ZA_SRC_FIX_PIPE, "",
     "8=FIX.4.4|9=131|35=D|49=BUYSIDE|56=EXCHANGE|34=2|52=20261011-10:00:00.000|11=ORD-0001|"
     "55=ACME|54=1|38=100|40=2|44=12.3450|60=20261011-10:00:00.000|10=023|",
     "new_order"},
    {ZA_SRC_FIX_PIPE, "",
     "8=FIX.4.4|9=132|35=D|49=BUYSIDE|56=EXCHANGE|34=3|52=20261011-10:00:01.000|11=ORD-0002|"
     "55=ACME|54=1|38=100|40=2|44=12.34567|60=20261011-10:00:01.000|10=089|",
     "new_order"},
    {ZA_SRC_MQTT, "plant/7/boiler", "{\"value\": 87.125, \"unit\": \"C\"}", "reading"},
    {ZA_SRC_MQTT, "plant/7/boiler", "\x08\xff\xfe binary", "reading"},
};

static char *slurp(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    char *b;
    long n;
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    b = (char *) malloc((size_t) n + 1u);
    if (b && fread(b, 1, (size_t) n, f) != (size_t) n) {
        free(b);
        b = NULL;
    }
    fclose(f);
    *len = (size_t) n;
    return b;
}

static const char *label(const za_record *r) {
    static char out[96];
    char tn[64];
    size_t tl = 0;
    int32_t lp = 0, tag = 0, act = 0;
    za_record_state(r, &lp, &tag);
    za_record_action(r, &act);
    zc_tag_format(tag, tn, sizeof tn, &tl);
    snprintf(out, sizeof out, "%s [%s -> %s]",
             lp == ZXV_LP_NEITHER ? "accepted, unverified" : lp == ZXV_LP_FALSE ? "REFUSED" : "attested",
             tn, zxv_hex_action_name(act));
    return out;
}

int main(int argc, char **argv) {
    zc_contract *c = NULL;
    zc_evlog *log = NULL;
    zxv_hexlog *quarantine = NULL;
    zc_hexsink *hs = NULL;
    zxv_ctx *ctx = NULL;
    zxv_cap *root = NULL, *attester = NULL;
    za_record *keep = NULL;
    size_t len = 0;
    char msg[300];
    uint32_t line = 0, col = 0;
    char *text = argc == 2 ? slurp(argv[1], &len) : NULL;
    if (!text) {
        fprintf(stderr, "usage: ingest_demo examples/market.zc\n");
        return 2;
    }
    if (zc_parse(text, len, &c, &line, &col, NULL, msg, sizeof msg, NULL) != ZXV_OK) {
        fprintf(stderr, "%s:%u:%u: %s\n", argv[1], (unsigned) line, (unsigned) col, msg);
        return 2;
    }
    free(text);
    /* The host's authority, and a quarantine log (libzxv ABI 1.3) that gets
     * every validation refusal as a Forbidden record, through z-contract's
     * ready-made sink. Writing to it needs ZXV_CAP_ATTEST. */
    if (zxv_ctx_create(&ctx) != ZXV_OK || zxv_cap_create(ctx, 1, NULL, ZXV_CAP_ALL, &root) != ZXV_OK ||
        zxv_cap_attenuate(root, ZXV_CAP_ATTEST, &attester) != ZXV_OK ||
        zxv_hexlog_create(attester, 64, &quarantine) != ZXV_OK ||
        zc_hexsink_create(attester, quarantine, 0, &hs) != ZXV_OK || zc_evlog_create(64, &log) != ZXV_OK ||
        zc_evlog_attach_hexsink(log, hs) != ZXV_OK)
        return 2;

    for (size_t i = 0; i < sizeof k_inbound / sizeof k_inbound[0]; i++) {
        const struct inbound *in = &k_inbound[i];
        za_record *r = NULL;
        const char *src = "";
        int32_t lp = 0;
        uint32_t nr = 0;
        if (za_ingest(in->kind, in->source, (const uint8_t *) in->payload, strlen(in->payload), &r) !=
            ZXV_OK)
            return 2;
        za_record_source(r, NULL, &src);
        za_record_state(r, &lp, NULL);
        if (lp == ZXV_LP_NEITHER) (void) za_validate(r, c, in->fn, log);
        printf("%-38s %-10s %s\n", src, in->fn, label(r));
        za_record_reason_count(r, &nr);
        for (uint32_t k = 0; k < nr; k++) {
            int32_t code = 0;
            const char *m = "";
            za_record_reason(r, k, &code, &m);
            printf("    %s: %s\n", za_reason_name(code), m);
        }
        if (i == 0) keep = r; /* the host has its own evidence for this one */
        else za_record_destroy(r);
    }

    /* The host settles the first payment against its own ledger and says
     * so with a capability that may attest. The adapter could not. */
    if (za_record_attest(keep, attester, ZXV_LP_TRUE) == ZXV_OK)
        printf("host attests the first payment:        %s\n", label(keep));

    uint64_t count = 0;
    zc_evlog_count(log, &count);
    printf("evidence records: %llu, chain %s\n", (unsigned long long) count,
           zc_evlog_verify(log) == ZXV_OK ? "verifies" : "BROKEN");
    uint32_t qn = 0;
    zxv_hexlog_count(quarantine, &qn);
    printf("quarantine records: %u, chain %s\n", (unsigned) qn,
           zxv_hexlog_verify(quarantine) == ZXV_OK ? "verifies" : "BROKEN");
    for (uint32_t i = 0; i < qn; i++) {
        uint8_t qtag = 0, reason = 0;
        zxv_hexlog_at(quarantine, i, NULL, &qtag, &reason, NULL, NULL, NULL, NULL);
        printf("    #%u %s, %s\n", (unsigned) i, zxv_hex_name(qtag), zc_reason_name(reason));
    }

    za_record_destroy(keep);
    zc_evlog_destroy(log);
    zc_hexsink_destroy(hs);
    zxv_hexlog_destroy(quarantine);
    zxv_cap_destroy(attester);
    zxv_cap_destroy(root);
    zxv_ctx_destroy(ctx);
    zc_contract_destroy(c);
    return 0;
}
