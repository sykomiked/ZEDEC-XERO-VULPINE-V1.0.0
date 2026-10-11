/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* host_demo.c — a ZXV host running Z-Wasm plugins.
 *
 *   host_demo rational_plugin.wasm float_plugin.wasm [spin_plugin.wasm]
 *
 * Shows the whole loop: least-privilege capabilities, a budget that pays
 * for plugin fuel, a float plugin refused at load, an exact-rational plugin
 * proposing into the S0 queue, and the host accepting (and posting) with
 * its own capability. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zwasm.h"

static uint8_t *slurp(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    uint8_t *b;
    long n;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    b = (uint8_t *) malloc((size_t) n);
    if (b && fread(b, 1, (size_t) n, f) != (size_t) n) {
        free(b);
        b = NULL;
    }
    fclose(f);
    *len = (size_t) n;
    return b;
}

static const char *err(void) {
    static char b[512];
    size_t n;
    if (zw_last_error(b, sizeof b, &n) != ZXV_OK) b[0] = 0;
    return b;
}

#define TRY(x)                                                                                 \
    do {                                                                                       \
        int32_t st_ = (x);                                                                     \
        if (st_ != ZXV_OK) {                                                                   \
            fprintf(stderr, "%s failed: %s (%s)\n", #x, zw_status_name(st_), err());           \
            return 1;                                                                          \
        }                                                                                      \
    } while (0)

static void print_rat(int64_t n, int64_t d) {
    char b[64];
    size_t l;
    if (zxv_rat_format(n, d, b, sizeof b, &l) == ZXV_OK) printf("%s", b);
    else printf("%lld/%lld", (long long) n, (long long) d);
}

int main(int argc, char **argv) {
    zxv_ctx *ctx;
    zxv_cap *root, *budget_cap, *post_cap;
    zxv_rmag *rm;
    uint32_t bud, issuer, holder;
    zw_host *host;
    zw_plugin *plugin = NULL;
    size_t len;
    uint8_t *w;
    if (argc < 3) {
        fprintf(stderr, "usage: host_demo rational_plugin.wasm float_plugin.wasm [spin_plugin.wasm]\n");
        return 2;
    }

    /* 1. The host's own authority, split by least privilege. */
    TRY(zxv_ctx_create(&ctx));
    TRY(zxv_cap_create(ctx, 1, NULL, ZXV_CAP_ALL, &root));
    TRY(zxv_cap_attenuate(root, ZXV_CAP_BUDGET, &budget_cap));
    TRY(zxv_cap_attenuate(root, ZXV_CAP_LEDGER_POST | ZXV_CAP_LEDGER_READ, &post_cap));
    TRY(zxv_ccy_enable_cap(post_cap, "NGN"));
    TRY(zxv_ledger_open_account_cap(post_cap, "treasury", "NGN", ZXV_ACCT_ISSUER, &issuer));
    TRY(zxv_ledger_open_account_cap(post_cap, "merchant", "NGN", ZXV_ACCT_HOLDER, &holder));

    /* 2. A compute budget that pays for every Wasm instruction. */
    TRY(zxv_rmag_create_cap(root, &rm));
    TRY(zxv_rmag_budget(root, rm, "plugin-fuel", ZXV_RES_COMPUTE_CYCLES, 1000000, 1, 1, &bud));
    TRY(zw_host_create(budget_cap, rm, bud, &host));
    TRY(zw_host_grant(host, ZW_GRANT_RATIONAL | ZW_GRANT_LOGIC | ZW_GRANT_PROPOSE));
    TRY(zw_host_set_call_fuel(host, 100000));

    /* 3. A float plugin never loads. */
    w = slurp(argv[2], &len);
    if (!w) return 2;
    int32_t st = zw_plugin_load(host, w, len, "float", &plugin);
    printf("load %s: %s\n  %s\n", argv[2], zw_status_name(st), err());
    free(w);

    /* 4. The exact plugin runs and proposes. */
    w = slurp(argv[1], &len);
    if (!w) return 2;
    TRY(zw_plugin_load(host, w, len, "rational", &plugin));
    free(w);
    int64_t n = 12, res = 0;
    uint64_t fuel = 0;
    TRY(zw_plugin_call(plugin, "run", &n, 1, &res, NULL, &fuel));
    printf("run(12) -> verdict %s, %llu fuel\n", zxv_lp_name((int32_t) res),
           (unsigned long long) fuel);
    int64_t fa[2] = {48000, 125}; /* 1.25% of 480.00 NGN */
    TRY(zw_plugin_call(plugin, "fee", fa, 2, &res, NULL, &fuel));
    int64_t fb[2] = {333, 125};
    TRY(zw_plugin_call(plugin, "fee", fb, 2, &res, NULL, &fuel));

    /* 5. The host decides, with its own capability. */
    uint64_t id;
    while (zw_queue_next(host, &id) == ZXV_OK) {
        int64_t num, den;
        uint32_t kind;
        char who[32];
        uint8_t label[65];
        size_t ll;
        TRY(zw_proposal_get(host, id, NULL, &kind, &num, &den, who, label, sizeof label, &ll));
        printf("proposal #%llu from '%s' kind %u: ", (unsigned long long) id, who, kind);
        print_rat(num, den);
        printf("  \"%s\"\n", (char *) label);
        if (kind == 2 && den == 1) {
            uint64_t entry = 0;
            char ref[36];
            snprintf(ref, sizeof ref, "zw-fee-%llu", (unsigned long long) id);
            TRY(zw_proposal_accept_post(host, post_cap, id, issuer, holder, "NGN", ref, &entry));
            printf("  accepted and posted as ledger entry %llu\n", (unsigned long long) entry);
        } else {
            TRY(zw_proposal_reject(host, post_cap, id));
            printf("  rejected by the host (not a postable integer amount)\n");
        }
    }
    int64_t eq = 0;
    TRY(zxv_ledger_balance_cap(post_cap, holder, NULL, NULL, &eq));
    printf("merchant balance: %lld minor units\n", (long long) eq);

    /* 6. A runaway plugin is stopped by its fuel; the host carries on. */
    if (argc > 3 && (w = slurp(argv[3], &len)) != NULL) {
        zw_plugin *spin = NULL;
        int64_t one = 1;
        TRY(zw_plugin_load(host, w, len, "spin", &spin));
        st = zw_plugin_call(spin, "spin", &one, 1, NULL, NULL, &fuel);
        printf("spin(1): %s after %llu fuel: %s\n", zw_status_name(st), (unsigned long long) fuel,
               err());
        zw_plugin_destroy(spin);
        free(w);
    }
    int64_t un = 0, ud = 1;
    TRY(zxv_rmag_status(rm, bud, NULL, NULL, &un, &ud, NULL));
    printf("budget used: ");
    print_rat(un, ud);
    printf(" of 1000000 compute units\n");
    TRY(zxv_ledger_check_cap(post_cap));
    printf("ledger check: OK\n");

    zw_plugin_destroy(plugin);
    zw_host_destroy(host);
    zxv_rmag_destroy(rm);
    zxv_cap_destroy(post_cap);
    zxv_cap_destroy(budget_cap);
    zxv_cap_destroy(root);
    zxv_ctx_destroy(ctx);
    return 0;
}
