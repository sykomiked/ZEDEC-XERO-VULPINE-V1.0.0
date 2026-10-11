/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* isolate_demo.c — Z-Wasm's example plugins, each in its own worker
 * process.
 *
 *   isolate_demo ti-worker rational_plugin.wasm float_plugin.wasm spin_plugin.wasm
 *
 * The same loop as Z-Wasm's host demo, across a process boundary: a float
 * plugin refused before any worker starts, an exact plugin proposing
 * values, the host accepting with its OWN capability, and a runaway
 * plugin whose worker is killed when its fuel runs out. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "triisolate.h"

static uint8_t *slurp(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    uint8_t *b;
    long n;
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    b = (uint8_t *) malloc((size_t) n + 1u);
    if (b && fread(b, 1, (size_t) n, f) != (size_t) n) {
        free(b);
        b = NULL;
    }
    fclose(f);
    *len = (size_t) n;
    return b;
}

static const char *err(void) {
    static char b[600];
    size_t n;
    if (ti_last_error(b, sizeof b, &n) != ZXV_OK) b[0] = 0;
    return b;
}

#define TRY(x)                                                                                 \
    do {                                                                                       \
        int32_t st_ = (x);                                                                     \
        if (st_ != ZXV_OK) {                                                                   \
            fprintf(stderr, "%s failed: %s (%s)\n", #x, ti_status_name(st_), err());           \
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
    ti_host *host;
    ti_plugin *plugin = NULL;
    uint8_t *w;
    size_t len;
    int64_t pid = 0;
    if (argc < 5) {
        fprintf(stderr, "usage: isolate_demo ti-worker rational.wasm float.wasm spin.wasm\n");
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

    /* 2. A compute budget that pays for every Wasm instruction, in every worker. */
    TRY(zxv_rmag_create_cap(root, &rm));
    TRY(zxv_rmag_budget(root, rm, "plugin-fuel", ZXV_RES_COMPUTE_CYCLES, 1000000, 1, 1, &bud));
    TRY(ti_host_create(budget_cap, rm, bud, argv[1], &host));
    TRY(ti_host_grant(host, ZW_GRANT_RATIONAL | ZW_GRANT_LOGIC | ZW_GRANT_PROPOSE));
    TRY(ti_host_set_call_fuel(host, 100000));
    TRY(ti_host_set_timeouts(host, 2000, 5000));
    if (ti_seccomp_available()) TRY(ti_host_set_flags(host, TI_F_SECCOMP));
    printf("worker: %s%s\n", argv[1], ti_seccomp_available() ? " (with the seccomp filter)" : "");

    /* 3. A float plugin never reaches a worker. */
    if (!(w = slurp(argv[3], &len))) return 2;
    int32_t st = ti_plugin_load(host, w, len, "float", &plugin);
    printf("load float plugin: %s\n  %s\n", ti_status_name(st), err());
    free(w);

    /* 4. The exact plugin runs in its own process and proposes. */
    if (!(w = slurp(argv[2], &len))) return 2;
    TRY(ti_plugin_load(host, w, len, "rational", &plugin));
    free(w);
    TRY(ti_plugin_pid(plugin, &pid));
    printf("rational plugin: running in worker process %lld\n", (long long) pid);
    int64_t n = 12, res = 0;
    uint64_t fuel = 0;
    TRY(ti_plugin_call(plugin, "run", &n, 1, &res, NULL, &fuel));
    printf("run(12) -> verdict %s, %llu fuel\n", zxv_lp_name((int32_t) res), (unsigned long long) fuel);
    int64_t fa[2] = {48000, 125}, fb[2] = {333, 125};
    TRY(ti_plugin_call(plugin, "fee", fa, 2, &res, NULL, &fuel));
    TRY(ti_plugin_call(plugin, "fee", fb, 2, &res, NULL, &fuel));

    /* 5. The host decides, with its own capability. */
    uint64_t id;
    while (ti_queue_next(host, &id) == ZXV_OK) {
        int64_t num, den;
        uint32_t kind;
        char who[32];
        uint8_t label[65];
        size_t ll;
        TRY(ti_proposal_get(host, id, NULL, &kind, &num, &den, who, label, sizeof label, &ll));
        printf("proposal #%llu from '%s' kind %u: ", (unsigned long long) id, who, kind);
        print_rat(num, den);
        printf("  \"%s\"\n", (char *) label);
        if (kind == 2 && den == 1) {
            uint64_t entry = 0;
            char ref[36];
            snprintf(ref, sizeof ref, "ti-fee-%llu", (unsigned long long) id);
            TRY(ti_proposal_accept_post(host, post_cap, id, issuer, holder, "NGN", ref, &entry));
            printf("  accepted and posted as ledger entry %llu\n", (unsigned long long) entry);
        } else {
            TRY(ti_proposal_reject(host, post_cap, id));
            printf("  rejected by the host (not a postable integer amount)\n");
        }
    }
    int64_t eq = 0;
    TRY(zxv_ledger_balance_cap(post_cap, holder, NULL, NULL, &eq));
    printf("merchant balance: %lld minor units\n", (long long) eq);
    ti_plugin_destroy(plugin);

    /* 6. A runaway plugin: its worker is killed when the fuel runs out. */
    if ((w = slurp(argv[4], &len)) != NULL) {
        ti_plugin *spin = NULL;
        int64_t one = 1;
        int32_t how = 0, val = 0;
        TRY(ti_plugin_load(host, w, len, "spin", &spin));
        TRY(ti_plugin_pid(spin, &pid));
        st = ti_plugin_call(spin, "spin", &one, 1, NULL, NULL, &fuel);
        (void) ti_plugin_exit(spin, &how, &val);
        printf("spin(1) in process %lld: %s after %llu fuel; worker %s %d\n", (long long) pid,
               ti_status_name(st), (unsigned long long) fuel, how == TI_EXIT_SIGNAL ? "killed by signal" : "exit",
               (int) val);
        ti_plugin_destroy(spin);
        free(w);
    }
    int64_t un = 0, ud = 1;
    uint64_t spawned = 0, killed = 0;
    TRY(zxv_rmag_status(rm, bud, NULL, NULL, &un, &ud, NULL));
    TRY(ti_host_stats(host, &spawned, &killed, NULL));
    printf("budget used: ");
    print_rat(un, ud);
    printf(" of 1000000 compute units; %llu worker(s) started, %llu killed\n", (unsigned long long) spawned,
           (unsigned long long) killed);

    ti_host_destroy(host);
    zxv_rmag_destroy(rm);
    zxv_cap_destroy(post_cap);
    zxv_cap_destroy(budget_cap);
    zxv_cap_destroy(root);
    zxv_ctx_destroy(ctx);
    return 0;
}
