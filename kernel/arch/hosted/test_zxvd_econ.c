/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zxvd_econ.c — zxvd_econ.c (vault.*, swap.*, auction.*, zenny.*,
 * stats.*) behind the real RPC layer, on the real engines and pay_ledger:
 * the off switches, exact replies, every refusal named, and the flows the
 * engines are for (a fee swept into the vault, a swap in and out, a lock
 * held through its notice period, a dividend, an auction epoch held to the
 * reserve floor, a chip minted and partly decompressed, a chained stats
 * snapshot). Every figure below is worked out in the comments beside it. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zxvd_groups.h"
#include "zstats.h"
#include "sha256.h"

static int fails, checks;

#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            fails++;                                                                               \
            printf("  FAIL %s:%d ", __FILE__, __LINE__);                                           \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

static zxvd_buf_t OUT;
static char REQB[20000], WANT[20000];

static const char *rpc(const char *line)
{
    OUT.len = 0;
    CHECK(zxvd_rpc_handle_line(line, strlen(line), &OUT), "a reply for %s", line);
    return OUT.p ? OUT.p : "";
}

static void expect(const char *req, const char *want, int line)
{
    char r[20000];
    snprintf(r, sizeof r, "%s", req); /* req and want may share a buffer */
    const char *got = rpc(r);
    checks++;
    if (strcmp(got, want)) {
        fails++;
        printf("  FAIL line %d\n    req  %.400s\n    want %.1200s\n    got  %.1200s\n", line, r,
               want, got);
    }
}
#define EXPECT(req, want) expect(req, want, __LINE__)

static const char *req(const char *method, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
static const char *req(const char *method, const char *fmt, ...)
{
    va_list ap;
    int k = snprintf(REQB, sizeof REQB, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"%s\"", method);
    if (*fmt) {
        k += snprintf(REQB + k, sizeof REQB - (size_t) k, ",\"params\":");
        va_start(ap, fmt);
        k += vsnprintf(REQB + k, sizeof REQB - (size_t) k, fmt, ap);
        va_end(ap);
    }
    snprintf(REQB + k, sizeof REQB - (size_t) k, "}");
    return REQB;
}

static const char *req0(const char *method)
{
    snprintf(REQB, sizeof REQB, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"%s\"}", method);
    return REQB;
}

static const char *ok(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static const char *ok(const char *fmt, ...)
{
    va_list ap;
    int k = snprintf(WANT, sizeof WANT, "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":");
    va_start(ap, fmt);
    k += vsnprintf(WANT + k, sizeof WANT - (size_t) k, fmt, ap);
    va_end(ap);
    snprintf(WANT + k, sizeof WANT - (size_t) k, "}");
    return WANT;
}

static const char *err(int code, const char *msg)
{
    snprintf(WANT, sizeof WANT,
             "{\"jsonrpc\":\"2.0\",\"id\":1,\"error\":{\"code\":%d,\"message\":\"%s\"}}", code,
             msg);
    return WANT;
}
#define ECON(msg)  err(-32050, msg)
#define PARAM(msg) err(-32602, msg)

static bool has(const char *r, const char *needle)
{
    return strstr(rpc(r), needle) != NULL;
}

/* the last reply has it (the request is not sent again) */
static bool last(const char *needle)
{
    return OUT.p && strstr(OUT.p, needle) != NULL;
}

/* the hex value of "key":"..." in the last reply */
static void hexval(const char *key, char *out, size_t cap)
{
    char k[64];
    snprintf(k, sizeof k, "\"%s\":\"", key);
    const char *p = OUT.p ? strstr(OUT.p, k) : NULL;
    out[0] = 0;
    if (!p) return;
    p += strlen(k);
    size_t n = strcspn(p, "\"");
    if (n >= cap) n = cap - 1;
    memcpy(out, p, n);
    out[n] = 0;
}

static void unhex(const char *h, uint8_t *out, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        unsigned v;
        sscanf(h + 2 * i, "%2x", &v);
        out[i] = (uint8_t) v;
    }
}

static void tohex(const uint8_t *p, size_t n, char *out)
{
    for (size_t i = 0; i < n; i++) sprintf(out + 2 * i, "%02x", p[i]);
}

static void groups(void)
{
    zxvd_rpc_groups_reset();
    CHECK(zxvd_rpc_add_group(zxvd_vault_group()) == 0 &&
              zxvd_rpc_add_group(zxvd_swap_group()) == 0 &&
              zxvd_rpc_add_group(zxvd_auctions_group()) == 0 &&
              zxvd_rpc_add_group(zxvd_zenny_group()) == 0 &&
              zxvd_rpc_add_group(zxvd_stats_group()) == 0,
          "the five groups register");
}

#define VAULT0                                                                                     \
    "{\"reserve_minor\":\"0\",\"cash_minor\":\"0\",\"outstanding\":\"0\",\"held\":\"0\","          \
    "\"cap\":\"0\",\"room\":\"0\",\"floor\":null,\"fees_minor\":\"0\",\"share_total_minor\":"      \
    "\"0\",\"swept_minor\":\"0\",\"kept_minor\":\"0\",\"spent_minor\":\"0\",\"bought\":\"0\","     \
    "\"burned\":\"0\",\"carry\":\"0\",\"dividend_pool_minor\":\"0\",\"bounty_minor\":\"0\","       \
    "\"regen_minor\":\"0\"}"

/* ---- the switches (zxvd_ext.h X1-X6) ---- */
static void test_switches(void)
{
    groups();
    CHECK(zxvd_rpc_add_group(zxvd_vault_group()) == 0, "re-adding a group held is a no-op");
    EXPECT(
        req0("features.get"),
        ok("{\"features\":[{\"name\":\"vault\",\"enabled\":false,\"regulated\":true,\"groups\":[{"
           "\"note\":\"protocol solvency vault (svault): fee reserve share, V-Bill buys, first "
           "loss; in memory; V-Bills are likely securities\",\"methods\":5}]},"
           "{\"name\":\"swap\",\"enabled\":false,\"regulated\":true,\"groups\":[{\"note\":\"Vino "
           "<-> V-Bill swap, lock tiers and the dividend (vswap); in memory; V-Bills are likely "
           "securities\",\"methods\":12}]},{\"name\":\"auctions\",\"enabled\":false,"
           "\"regulated\":true,\"groups\":[{\"note\":\"V-Bill issuance by uniform-price "
           "descending auction, backed only (vauction); in memory; V-Bills are likely "
           "securities\",\"methods\":8}]},{\"name\":\"zenny\",\"enabled\":false,\"regulated\":"
           "true,\"groups\":[{\"note\":\"Zenny chips: baskets with a V-Bill anchor (zenny); in "
           "memory; likely securities; the name has no trademark clearance\",\"methods\":6}]},"
           "{\"name\":\"stats\",\"enabled\":false,\"regulated\":false,\"groups\":[{\"note\":"
           "\"aggregate economy statistics (zstats), read-only, hash-chained, unsigned\","
           "\"methods\":2}]}],\"saved\":false}"));
    CHECK(
        has(req0("rpc.discover"),
            "\"backup.prune\",\"vault.deposit\",\"vault.buy\",\"vault.first_loss\","
            "\"vault.status\",\"vault.check\",\"swap.deposit\",\"swap.withdraw\",\"swap.enable\","
            "\"swap.quote\",\"swap.in\",\"swap.out\",\"swap.lock\",\"swap.notice\",\"swap.unlock\","
            "\"swap.dividend\",\"swap.status\",\"swap.check\",\"auction.configure\","
            "\"auction.open\",\"auction.bid\",\"auction.run\",\"auction.cancel\","
            "\"auction.quote\",\"auction.status\",\"auction.check\",\"zenny.mint\","
            "\"zenny.quote\",\"zenny.decompress\",\"zenny.receipts\",\"zenny.status\","
            "\"zenny.check\",\"stats.snapshot\",\"stats.status\"]"),
        "discover lists the 33 methods in order");

    /* off: the money methods refuse and do nothing; status still answers */
    const char *const OFF[][2] = {{"vault.deposit", "vault"},
                                  {"vault.buy", "vault"},
                                  {"vault.first_loss", "vault"},
                                  {"swap.deposit", "swap"},
                                  {"swap.withdraw", "swap"},
                                  {"swap.enable", "swap"},
                                  {"swap.in", "swap"},
                                  {"swap.out", "swap"},
                                  {"swap.lock", "swap"},
                                  {"swap.notice", "swap"},
                                  {"swap.unlock", "swap"},
                                  {"swap.dividend", "swap"},
                                  {"auction.configure", "auctions"},
                                  {"auction.open", "auctions"},
                                  {"auction.bid", "auctions"},
                                  {"auction.run", "auctions"},
                                  {"auction.cancel", "auctions"},
                                  {"zenny.mint", "zenny"},
                                  {"zenny.decompress", "zenny"},
                                  {"zenny.receipts", "zenny"}};
    for (size_t i = 0; i < sizeof OFF / sizeof OFF[0]; i++) {
        char msg[200];
        snprintf(msg, sizeof msg,
                 "%s is off: start zxvd with --enable %s, or call features.set (regulated: test or "
                 "licensed use only)",
                 OFF[i][1], OFF[i][1]);
        EXPECT(req(OFF[i][0], "{\"fee_minor\":5}"), err(-32012, msg));
    }
    EXPECT(req0("stats.snapshot"),
           err(-32012, "stats is off: start zxvd with --enable stats, or call features.set"));
    EXPECT(req0("vault.status"), ok(VAULT0));
    EXPECT(req0("stats.status"),
           ok("{\"published\":\"0\",\"head\":\"0000000000000000000000000000000000000000000000000000"
              "000000000000\",\"signed\":false}"));
    EXPECT(req("swap.quote", "{\"direction\":\"in\",\"amount\":250}"),
           ok("{\"direction\":\"in\",\"rate\":{\"num\":\"100\",\"den\":\"1\"},\"par\":true,"
              "\"vbills\":\"2\",\"vino_minor\":\"200\"}"));
    EXPECT(req0("swap.check"), ok("{\"ok\":true,\"status\":\"VSWAP_OK\"}"));
    EXPECT(req("auction.check", "{\"now\":5}"),
           ok("{\"ok\":true,\"floor_ok\":true,\"ledger_ok\":true}"));
    EXPECT(req0("zenny.check"), ok("{\"chips\":[],\"ok\":true}"));
    EXPECT(req0("zenny.status"), ok("{\"chips\":[],\"receipts_pending\":\"0\"}"));
    EXPECT(req0("vault.check"),
           ok("{\"ok\":true,\"vault_ok\":true,\"ledger_ok\":true,\"solvent\":true,\"floor_num\":"
              "\"1\",\"floor_den\":\"1\",\"backing\":\"100\"}"));

    /* on: one switch at a time */
    CHECK(zxvd_rpc_enable("vault") == 0, "vault on");
    EXPECT(req("vault.deposit", "{\"fee_minor\":0}"),
           PARAM("\\\"fee_minor\\\" must be at least 1"));
    CHECK(has(req0("swap.deposit"), "-32012"), "swap stays off");
    zxvd_rpc_groups_reset();
}

/* ---- vault.* (svault.h S1-S6) and the fee buckets ---- */
static void test_vault(void)
{
    groups();
    zxvd_rpc_enable("vault,swap");
    EXPECT(req0("vault.deposit"), PARAM("missing integer parameter \\\"fee_minor\\\""));
    EXPECT(req("vault.deposit", "{\"fee_minor\":-1}"),
           PARAM("\\\"fee_minor\\\" must not be negative"));
    EXPECT(req("vault.deposit", "{\"fee_minor\":1.5}"),
           PARAM("\\\"fee_minor\\\" must be an integer or a decimal string (no fraction or "
                 "exponent)"));
    EXPECT(req("vault.deposit", "{\"fee_minor\":1,\"now\":-3}"),
           PARAM("\\\"now\\\" must not be negative"));
    EXPECT(req0("vault.status"), ok(VAULT0));

    /* no V-Bills yet: the cap is 0, so the vault's third stays in the reserve.
     * fee 9: B1 floors 25% 2, 15% 1, 10% 0, the reserve takes the rest 6;
     * share 6 + carry 0: part 2 (kept), carry 0 */
    EXPECT(req("vault.deposit", "{\"fee_minor\":9,\"now\":10}"),
           ok("{\"fee_minor\":\"9\",\"reserve_minor\":\"6\",\"dividend_minor\":\"2\","
              "\"bounty_minor\":\"1\",\"regen_minor\":\"0\",\"swept_minor\":\"0\",\"kept_minor\":"
              "\"2\",\"carry\":\"0\"}"));
    /* withdraw the reserve again? no: the reserve is not a holder. A holder
     * buys 10 V-Bills at the rate R/B... B == 0 with R > 0 is refused by
     * newer vswap, so start a fresh economy with V-Bills first. */
    zxvd_rpc_groups_reset();
    groups();
    zxvd_rpc_enable("vault,swap");
    EXPECT(req("swap.deposit", "{\"holder\":7,\"amount_minor\":20000,\"now\":1000}"),
           ok("{\"holder\":7,\"vino_minor\":\"20000\",\"vbills\":\"0\"}"));
    EXPECT(req0("swap.enable"), ok("{\"enabled\":true,\"epoch\":\"1\"}"));
    /* par 100 minor units per V-Bill: 1000 buys 10 */
    CHECK(has(req("swap.in", "{\"holder\":7,\"amount_minor\":1000,\"now\":1001}"),
              "\"vbills\":\"10\",\"vino_minor\":\"1000\""),
          "10 V-Bills at par");
    /* fee 10000: reserve share 5000 -> part 1666 (carry 2), room floor(10/3) = 3 > 0: swept */
    EXPECT(req("vault.deposit", "{\"fee_minor\":10000,\"now\":1002}"),
           ok("{\"fee_minor\":\"10000\",\"reserve_minor\":\"5000\",\"dividend_minor\":\"2500\","
              "\"bounty_minor\":\"1500\",\"regen_minor\":\"1000\",\"swept_minor\":\"1666\","
              "\"kept_minor\":\"0\",\"carry\":\"2\"}"));
    /* reserve 1000 (the swap) + 5000 - 1666 = 4334; floor 4334 / (10 - 0) */
    EXPECT(req0("vault.status"),
           ok("{\"reserve_minor\":\"4334\",\"cash_minor\":\"1666\",\"outstanding\":\"10\","
              "\"held\":\"0\",\"cap\":\"3\",\"room\":\"3\",\"floor\":{\"num\":\"4334\",\"den\":"
              "\"10\"},\"fees_minor\":\"10000\",\"share_total_minor\":\"5000\",\"swept_minor\":"
              "\"1666\",\"kept_minor\":\"0\",\"spent_minor\":\"0\",\"bought\":\"0\",\"burned\":"
              "\"0\",\"carry\":\"2\",\"dividend_pool_minor\":\"2500\",\"bounty_minor\":\"1500\","
              "\"regen_minor\":\"1000\"}"));

    /* S3: min(max 2, room 3, 1666 / 100) = 2 */
    EXPECT(req("vault.buy", "{\"seller\":7,\"price_minor\":100,\"max_qty\":2}"),
           ok("{\"bought\":\"2\",\"cost_minor\":\"200\",\"price_minor\":\"100\",\"seller\":{"
              "\"holder\":7,\"vino_minor\":\"19200\",\"vbills\":\"8\"}}"));
    EXPECT(req("vault.buy", "{\"seller\":7,\"price_minor\":\"100\"}"),
           ok("{\"bought\":\"1\",\"cost_minor\":\"100\",\"price_minor\":\"100\",\"seller\":{"
              "\"holder\":7,\"vino_minor\":\"19300\",\"vbills\":\"7\"}}"));
    EXPECT(req("vault.buy", "{\"seller\":7,\"price_minor\":100}"),
           ok("{\"bought\":\"0\",\"cost_minor\":\"0\",\"price_minor\":\"100\",\"seller\":{"
              "\"holder\":7,\"vino_minor\":\"19300\",\"vbills\":\"7\"}}"));
    EXPECT(req("vault.buy", "{\"seller\":7,\"price_minor\":0}"), ECON("SVAULT_ERR_ARG"));
    EXPECT(req("vault.buy", "{\"seller\":8,\"price_minor\":1}"),
           ECON("unknown holder 8: swap.deposit opens one"));
    EXPECT(req("vault.buy", "{\"price_minor\":1}"),
           PARAM("missing integer parameter \\\"seller\\\""));
    EXPECT(req("vault.buy", "{\"seller\":7}"),
           PARAM("missing integer parameter \\\"price_minor\\\""));
    EXPECT(req("vault.buy", "{\"seller\":7,\"price_minor\":1,\"max_qty\":-1}"),
           PARAM("\\\"max_qty\\\" must not be negative"));
    EXPECT(req("vault.buy", "{\"seller\":7,\"price_minor\":1,\"now\":\"x\"}"),
           PARAM("\\\"now\\\" must be an integer or a decimal string (no fraction or exponent)"));

    /* at the cap (h 3 = floor(10 / 3)): fee 3 -> share 3 + carry 2 = 5 -> part 1, kept */
    EXPECT(req("vault.deposit", "{\"fee_minor\":3,\"now\":1003}"),
           ok("{\"fee_minor\":\"3\",\"reserve_minor\":\"3\",\"dividend_minor\":\"0\","
              "\"bounty_minor\":\"0\",\"regen_minor\":\"0\",\"swept_minor\":\"0\",\"kept_minor\":"
              "\"1\",\"carry\":\"2\"}"));
    /* 2^62: the ledger refuses a balance that large */
    EXPECT(req("vault.deposit", "{\"fee_minor\":\"4611686018427387904\"}"),
           ECON("PAY_ERR_OVERFLOW"));
    /* S5: the vault's 3 V-Bills take the first loss of 5 */
    EXPECT(req("vault.first_loss", "{\"loss_vbills\":5,\"now\":1004}"),
           ok("{\"burned\":\"3\",\"uncovered\":\"2\"}"));
    EXPECT(req("vault.first_loss", "{\"loss_vbills\":5}"),
           ok("{\"burned\":\"0\",\"uncovered\":\"5\"}"));
    EXPECT(req0("vault.first_loss"), PARAM("missing integer parameter \\\"loss_vbills\\\""));
    EXPECT(req("vault.first_loss", "{\"loss_vbills\":1,\"now\":-1}"),
           PARAM("\\\"now\\\" must not be negative"));
    /* reserve 4334 + 3 = 4337, cash 1666 - 300, N 10 - 3 = 7, h 0 */
    EXPECT(req0("vault.status"),
           ok("{\"reserve_minor\":\"4337\",\"cash_minor\":\"1366\",\"outstanding\":\"7\","
              "\"held\":\"0\",\"cap\":\"2\",\"room\":\"2\",\"floor\":{\"num\":\"4337\",\"den\":"
              "\"7\"},\"fees_minor\":\"10003\",\"share_total_minor\":\"5003\",\"swept_minor\":"
              "\"1666\",\"kept_minor\":\"1\",\"spent_minor\":\"300\",\"bought\":\"3\",\"burned\":"
              "\"3\",\"carry\":\"2\",\"dividend_pool_minor\":\"2500\",\"bounty_minor\":\"1500\","
              "\"regen_minor\":\"1000\"}"));
    /* S4: 4337 * 1 >= 6 * 7 * 100 = 4200, but not 7 * 7 * 100 = 4900 */
    EXPECT(req("vault.check", "{\"floor_num\":6,\"floor_den\":1,\"backing\":100}"),
           ok("{\"ok\":true,\"vault_ok\":true,\"ledger_ok\":true,\"solvent\":true,\"floor_num\":"
              "\"6\",\"floor_den\":\"1\",\"backing\":\"100\"}"));
    EXPECT(req("vault.check", "{\"floor_num\":7}"),
           ok("{\"ok\":true,\"vault_ok\":true,\"ledger_ok\":true,\"solvent\":false,\"floor_num\":"
              "\"7\",\"floor_den\":\"1\",\"backing\":\"100\"}"));
    EXPECT(req("vault.check", "{\"floor_num\":7,\"floor_den\":2}"),
           ok("{\"ok\":true,\"vault_ok\":true,\"ledger_ok\":true,\"solvent\":true,\"floor_num\":"
              "\"7\",\"floor_den\":\"2\",\"backing\":\"100\"}"));
    EXPECT(req("vault.check", "{\"backing\":620}"),
           ok("{\"ok\":true,\"vault_ok\":true,\"ledger_ok\":true,\"solvent\":false,\"floor_num\":"
              "\"1\",\"floor_den\":\"1\",\"backing\":\"620\"}"));
    EXPECT(req("vault.check", "{\"floor_num\":-1}"),
           PARAM("\\\"floor_num\\\" must not be negative"));
    EXPECT(req("vault.check", "{\"floor_den\":-1}"),
           PARAM("\\\"floor_den\\\" must not be negative"));
    EXPECT(req("vault.check", "{\"backing\":-1}"), PARAM("\\\"backing\\\" must not be negative"));
    /* X6: off keeps the state */
    EXPECT(req("features.set", "{\"feature\":\"vault\",\"enabled\":false}"),
           ok("{\"feature\":\"vault\",\"enabled\":false,\"regulated\":true,\"saved\":false}"));
    CHECK(has(req0("vault.status"), "\"reserve_minor\":\"4337\""), "off keeps the vault");
    zxvd_rpc_groups_reset();
    CHECK(zxvd_rpc_enabled("vault") == false, "reset forgets the switch");
    groups();
    EXPECT(req0("vault.status"), ok(VAULT0));
    zxvd_rpc_groups_reset();
}

#define H(id, vino, vb) "{\"holder\":" #id ",\"vino_minor\":\"" vino "\",\"vbills\":\"" vb "\"}"

/* ---- swap.* (vswap.h W1-W6, T1-T4) ---- */
static void test_swap(void)
{
    groups();
    zxvd_rpc_enable("swap,vault");
    EXPECT(req("swap.deposit", "{\"amount_minor\":1}"),
           PARAM("missing integer parameter \\\"holder\\\""));
    EXPECT(req("swap.deposit", "{\"holder\":1}"),
           PARAM("missing integer parameter \\\"amount_minor\\\""));
    EXPECT(req("swap.deposit", "{\"holder\":1,\"amount_minor\":0}"),
           PARAM("\\\"amount_minor\\\" must be at least 1"));
    EXPECT(req("swap.deposit", "{\"holder\":1,\"amount_minor\":1,\"now\":-1}"),
           PARAM("\\\"now\\\" must not be negative"));
    EXPECT(req("swap.deposit", "{\"holder\":1,\"amount_minor\":\"4611686018427387904\"}"),
           ECON("PAY_ERR_ARG"));
    EXPECT(req("swap.deposit", "{\"holder\":1,\"amount_minor\":500}"), ok(H(1, "500", "0")));
    EXPECT(req("swap.deposit", "{\"holder\":1,\"amount_minor\":\"700\",\"now\":3}"),
           ok(H(1, "1200", "0")));
    EXPECT(req("swap.deposit", "{\"holder\":2,\"amount_minor\":10000}"), ok(H(2, "10000", "0")));
    EXPECT(req("swap.withdraw", "{\"holder\":1,\"amount_minor\":200,\"now\":4}"),
           ok(H(1, "1000", "0")));
    EXPECT(req("swap.withdraw", "{\"holder\":1,\"amount_minor\":1001}"), ECON("PAY_ERR_FUNDS"));
    EXPECT(req("swap.withdraw", "{\"holder\":1,\"amount_minor\":0}"),
           PARAM("\\\"amount_minor\\\" must be at least 1"));
    EXPECT(req("swap.withdraw", "{\"holder\":1}"),
           PARAM("missing integer parameter \\\"amount_minor\\\""));
    EXPECT(req("swap.withdraw", "{\"holder\":1,\"amount_minor\":1,\"now\":-1}"),
           PARAM("\\\"now\\\" must not be negative"));
    EXPECT(req("swap.withdraw", "{\"holder\":9,\"amount_minor\":1}"),
           ECON("unknown holder 9: swap.deposit opens one"));

    /* W1: off by default; leaving is never blocked but there is nothing to leave */
    EXPECT(req("swap.in", "{\"holder\":1,\"amount_minor\":500}"), ECON("VSWAP_ERR_DISABLED"));
    EXPECT(req("swap.lock", "{\"holder\":1,\"tier\":\"deep\",\"vbills\":1}"),
           ECON("VSWAP_ERR_DISABLED"));
    EXPECT(req("swap.enable", "{\"enabled\":false}"), ok("{\"enabled\":false,\"epoch\":\"1\"}"));
    EXPECT(req("swap.enable", "{\"enabled\":1}"), PARAM("\\\"enabled\\\" must be true or false"));
    CHECK(has(req0("swap.status"), "{\"enabled\":false,\"switch_epoch\":\"1\""), "still off");
    EXPECT(req("swap.enable", "{\"enabled\":true}"), ok("{\"enabled\":true,\"epoch\":\"2\"}"));

    /* W3 at par 100/1: 550 buys 5, charged 500 */
    EXPECT(req("swap.quote", "{\"direction\":\"in\",\"amount\":550}"),
           ok("{\"direction\":\"in\",\"rate\":{\"num\":\"100\",\"den\":\"1\"},\"par\":true,"
              "\"vbills\":\"5\",\"vino_minor\":\"500\"}"));
    EXPECT(req("swap.in", "{\"holder\":1,\"amount_minor\":550,\"now\":5}"),
           ok("{\"direction\":\"in\",\"rate\":{\"num\":\"100\",\"den\":\"1\"},\"par\":true,"
              "\"vbills\":\"5\",\"vino_minor\":\"500\",\"holder\":" H(1, "500", "5") "}"));
    EXPECT(req("swap.in", "{\"holder\":1,\"amount_minor\":99}"), ECON("VSWAP_ERR_DUST"));
    EXPECT(req("swap.in", "{\"holder\":1,\"amount_minor\":600}"), ECON("VSWAP_ERR_FUNDS"));
    EXPECT(req("swap.in", "{\"holder\":1}"),
           PARAM("missing integer parameter \\\"amount_minor\\\""));
    EXPECT(req("swap.in", "{\"holder\":1,\"amount_minor\":1,\"now\":-1}"),
           PARAM("\\\"now\\\" must not be negative"));
    EXPECT(req("swap.in", "{\"holder\":5,\"amount_minor\":1}"),
           ECON("unknown holder 5: swap.deposit opens one"));
    /* a fee lands in the reserve: R = 500 + 50, B = 5; rate 550/5 */
    CHECK(has(req("vault.deposit", "{\"fee_minor\":100,\"now\":6}"), "\"reserve_minor\":\"50\""),
          "fee recorded");
    /* swept: share 50 -> part 16 (room floor(5/3) = 1): reserve 550 - 16 = 534 */
    EXPECT(req("swap.quote", "{\"direction\":\"in\",\"amount\":300}"),
           ok("{\"direction\":\"in\",\"rate\":{\"num\":\"534\",\"den\":\"5\"},\"par\":false,"
              "\"vbills\":\"2\",\"vino_minor\":\"214\"}"));
    /* holder 2 buys 2 at 534/5: ceil(2 * 534 / 5) = 214 */
    EXPECT(req("swap.in", "{\"holder\":2,\"amount_minor\":300,\"now\":7}"),
           ok("{\"direction\":\"in\",\"rate\":{\"num\":\"534\",\"den\":\"5\"},\"par\":false,"
              "\"vbills\":\"2\",\"vino_minor\":\"214\",\"holder\":" H(2, "9786", "2") "}"));
    /* W4 at R/B = 748/7: 3 V-Bills out pay floor(3 * 748 / 7) = 320 */
    EXPECT(req("swap.quote", "{\"direction\":\"out\",\"amount\":3}"),
           ok("{\"direction\":\"out\",\"rate\":{\"num\":\"748\",\"den\":\"7\"},\"par\":false,"
              "\"vbills\":\"3\",\"vino_minor\":\"320\"}"));
    EXPECT(req("swap.quote", "{\"direction\":\"out\",\"amount\":8}"), ECON("VSWAP_ERR_FUNDS"));
    EXPECT(req("swap.quote", "{\"direction\":\"in\",\"amount\":0}"), ECON("VSWAP_ERR_ARG"));
    EXPECT(req("swap.quote", "{\"direction\":\"up\",\"amount\":1}"),
           PARAM("\\\"direction\\\" must be \\\"in\\\" or \\\"out\\\""));
    EXPECT(req("swap.quote", "{\"direction\":\"outs\",\"amount\":1}"),
           PARAM("\\\"direction\\\" must be a string of at most 3 bytes"));
    EXPECT(req("swap.quote", "{\"amount\":1}"),
           PARAM("missing string parameter \\\"direction\\\""));
    EXPECT(req("swap.quote", "{\"direction\":\"in\"}"),
           PARAM("missing integer parameter \\\"amount\\\""));
    EXPECT(req("swap.out", "{\"holder\":1,\"vbills\":3,\"now\":8}"),
           ok("{\"direction\":\"out\",\"rate\":{\"num\":\"748\",\"den\":\"7\"},\"par\":false,"
              "\"vbills\":\"3\",\"vino_minor\":\"320\",\"holder\":" H(1, "820", "2") "}"));
    EXPECT(req("swap.out", "{\"holder\":1,\"vbills\":3}"), ECON("VSWAP_ERR_FUNDS"));
    EXPECT(req("swap.out", "{\"holder\":1}"), PARAM("missing integer parameter \\\"vbills\\\""));
    EXPECT(req("swap.out", "{\"holder\":1,\"vbills\":1,\"now\":-1}"),
           PARAM("\\\"now\\\" must not be negative"));

    /* T1: lock 2 of holder 2's V-Bills deep (F(20) = 6765), holder 1's 1 medium (144) */
    EXPECT(req("swap.lock",
               "{\"holder\":2,\"tier\":\"deep\",\"vbills\":2,\"reinvest\":true,\"now\":10}"),
           ok("{\"position\":0,\"tier\":\"deep\",\"vbills\":\"2\",\"notice_period\":\"6765\","
              "\"reinvest\":true}"));
    EXPECT(req("swap.lock", "{\"holder\":1,\"tier\":\"medium\",\"vbills\":\"1\",\"now\":11}"),
           ok("{\"position\":1,\"tier\":\"medium\",\"vbills\":\"1\",\"notice_period\":\"144\","
              "\"reinvest\":false}"));
    EXPECT(req("swap.lock", "{\"holder\":1,\"tier\":\"low\",\"vbills\":1}"),
           PARAM("\\\"tier\\\" must be \\\"medium\\\" or \\\"deep\\\""));
    EXPECT(req("swap.lock", "{\"holder\":1,\"tier\":\"shallowest\",\"vbills\":1}"),
           PARAM("\\\"tier\\\" must be a string of at most 7 bytes"));
    EXPECT(req("swap.lock", "{\"holder\":1,\"vbills\":1}"),
           PARAM("missing string parameter \\\"tier\\\""));
    EXPECT(req("swap.lock", "{\"holder\":1,\"tier\":\"deep\"}"),
           PARAM("missing integer parameter \\\"vbills\\\""));
    EXPECT(req("swap.lock", "{\"holder\":1,\"tier\":\"deep\",\"vbills\":1,\"reinvest\":2}"),
           PARAM("\\\"reinvest\\\" must be true or false"));
    EXPECT(req("swap.lock", "{\"holder\":1,\"tier\":\"deep\",\"vbills\":1,\"now\":-2}"),
           PARAM("\\\"now\\\" must not be negative"));
    EXPECT(req("swap.lock", "{\"holder\":1,\"tier\":\"deep\",\"vbills\":0}"),
           ECON("VSWAP_ERR_ARG"));
    EXPECT(req("swap.lock", "{\"holder\":1,\"tier\":\"deep\",\"vbills\":2}"),
           ECON("VSWAP_ERR_FUNDS"));
    EXPECT(req("swap.lock", "{\"holder\":3,\"tier\":\"deep\",\"vbills\":1}"),
           ECON("unknown holder 3: swap.deposit opens one"));
    /* T3: no notice yet */
    EXPECT(req("swap.unlock", "{\"holder\":1,\"position\":1,\"now\":10000}"),
           ECON("VSWAP_ERR_LOCKED"));
    EXPECT(req("swap.notice", "{\"holder\":2,\"position\":1,\"now\":20}"), ECON("VSWAP_ERR_ARG"));
    EXPECT(req("swap.notice", "{\"holder\":1,\"position\":1,\"now\":20}"),
           ok("{\"position\":1,\"unlock_at\":\"164\"}"));
    EXPECT(req("swap.notice", "{\"holder\":1,\"position\":1,\"now\":21}"), ECON("VSWAP_ERR_STATE"));
    EXPECT(req("swap.notice", "{\"holder\":1,\"now\":21}"),
           PARAM("missing integer parameter \\\"position\\\""));
    EXPECT(req("swap.notice", "{\"position\":1}"),
           PARAM("missing integer parameter \\\"holder\\\""));
    EXPECT(req("swap.notice", "{\"holder\":1,\"position\":1,\"now\":-1}"),
           PARAM("\\\"now\\\" must not be negative"));
    EXPECT(req("swap.notice", "{\"holder\":2,\"position\":0,\"now\":30}"),
           ok("{\"position\":0,\"unlock_at\":\"6795\"}"));
    EXPECT(req("swap.unlock", "{\"holder\":1,\"position\":1,\"now\":163}"),
           ECON("VSWAP_ERR_LOCKED"));
    EXPECT(req("swap.unlock", "{\"holder\":1,\"position\":64,\"now\":163}"), ECON("VSWAP_ERR_ARG"));

    /* W5: pool 25 (25% of the fee 100). Units: holder 1 1 V-Bill x 1, holder 2
     * 0 x 1, position 0 (deep) 2 x 3, position 1 (medium) 1 x 2: U = 9.
     * Shares 25 * 1/9 = 2 r 7, 25 * 6/9 = 16 r 6, 25 * 2/9 = 5 r 5; the 2
     * left go to the largest remainders: 3, 17, 5. Position 0 reinvests 17 at
     * R/B = 534 + 214 - 320 = 428 over 4: 17 * 4 / 428 buys 0 (dust), so it
     * is paid whole. Holder 1: 820 + 3 + 5, holder 2: 9786 + 17. */
    EXPECT(req("swap.dividend", "{\"now\":40}"),
           ok("{\"pool_minor\":\"25\",\"units\":\"9\",\"paid_minor\":\"25\",\"reinvest_minor\":"
              "\"0\",\"vbills\":\"0\",\"postings\":1}"));
    EXPECT(req("swap.dividend", "{\"now\":41}"), ECON("VSWAP_ERR_EMPTY"));
    EXPECT(req("swap.dividend", "{\"now\":-1}"), PARAM("\\\"now\\\" must not be negative"));
    EXPECT(
        req0("swap.status"),
        ok("{\"enabled\":true,\"switch_epoch\":\"2\",\"rate\":{\"num\":\"428\",\"den\":\"4\"},"
           "\"par\":false,\"reserve_minor\":\"428\",\"outstanding\":\"4\",\"vault_vbills\":"
           "\"0\",\"dividend_pool_minor\":\"0\",\"notice_medium\":\"144\",\"notice_deep\":"
           "\"6765\",\"weights\":[1,2,3],\"holders\":[" H(1, "828", "1") "," H(
               2, "9803",
               "0") "],\"positions\":[{\"position\":0,\"holder\":2,\"tier\":\"deep\",\"vbills\":"
                    "\"2\","
                    "\"reinvest\":true,\"noticed\":true,\"notice_tick\":\"30\",\"last_vino_minor\":"
                    "\"17\",\"last_vbills\":\"0\"},{\"position\":1,\"holder\":1,\"tier\":"
                    "\"medium\","
                    "\"vbills\":\"1\",\"reinvest\":false,\"noticed\":true,\"notice_tick\":\"20\","
                    "\"last_vino_minor\":\"5\",\"last_vbills\":\"0\"}]}"));
    EXPECT(req0("swap.check"), ok("{\"ok\":true,\"status\":\"VSWAP_OK\"}"));
    /* T3: the notice period has run */
    EXPECT(req("swap.unlock", "{\"holder\":1,\"position\":1,\"now\":164}"),
           ok("{\"position\":1,\"vbills\":\"1\",\"holder\":" H(1, "828", "2") "}"));
    EXPECT(req("swap.unlock", "{\"holder\":1,\"position\":1,\"now\":165}"), ECON("VSWAP_ERR_ARG"));
    EXPECT(req("swap.unlock", "{\"holder\":2,\"position\":0,\"now\":6794}"),
           ECON("VSWAP_ERR_LOCKED"));
    EXPECT(req("swap.unlock", "{\"holder\":2,\"position\":0,\"now\":6795}"),
           ok("{\"position\":0,\"vbills\":\"2\",\"holder\":" H(2, "9803", "2") "}"));
    CHECK(has(req0("swap.status"), "\"positions\":[]}"), "no positions left");
    /* leaving is never blocked: switch off, swap out still works */
    EXPECT(req("swap.enable", "{\"enabled\":false}"), ok("{\"enabled\":false,\"epoch\":\"3\"}"));
    CHECK(
        has(req("swap.out", "{\"holder\":2,\"vbills\":2,\"now\":6800}"), "\"vino_minor\":\"214\""),
        "swap out while off: 2 * 428 / 4");
    EXPECT(req("swap.dividend", "{\"now\":1}"), ECON("VSWAP_ERR_DISABLED"));

    /* the holder table is full at 32 */
    for (uint32_t i = 3; i <= 32; i++)
        CHECK(
            has(req("swap.deposit", "{\"holder\":%u,\"amount_minor\":1}", 100 + i), "\"holder\":"),
            "holder %u", (unsigned) i);
    EXPECT(req("swap.deposit", "{\"holder\":999,\"amount_minor\":1}"),
           ECON("holder table full (32)"));
    CHECK(has(req("swap.deposit", "{\"holder\":132,\"amount_minor\":1}"), "\"vino_minor\":\"2\""),
          "a known holder still deposits");
    zxvd_rpc_groups_reset();

    /* a dividend with nobody to pay, and reinvestment that buys */
    groups();
    zxvd_rpc_enable("swap,vault");
    CHECK(has(req("vault.deposit", "{\"fee_minor\":4000}"), "\"dividend_minor\":\"1000\""), "pool");
    EXPECT(req0("swap.dividend"), ECON("VSWAP_ERR_DISABLED"));
    rpc(req0("swap.enable"));
    EXPECT(req0("swap.dividend"), ECON("VSWAP_ERR_NOHOLD"));
    zxvd_rpc_groups_reset();
    groups();
    zxvd_rpc_enable("swap,vault");
    req("swap.deposit", "{\"holder\":4,\"amount_minor\":1000}");
    rpc(REQB);
    rpc(req0("swap.enable"));
    CHECK(has(req("swap.in", "{\"holder\":4,\"amount_minor\":1000}"), "\"vbills\":\"10\""),
          "10 in");
    EXPECT(req("swap.lock",
               "{\"holder\":4,\"tier\":\"medium\",\"vbills\":10,\"reinvest\":true,\"now\":1}"),
           ok("{\"position\":0,\"tier\":\"medium\",\"vbills\":\"10\",\"notice_period\":\"144\","
              "\"reinvest\":true}"));
    /* fee 4000: reserve 2000 -> 666 swept? room floor(10/3) = 3 > 0: yes;
     * R = 1000 + 2000 - 666 = 2334, B = 10; pool 1000 all to the position;
     * reinvest at 2334/10: v = floor(1000 * 10 / 2334) = 4, cost ceil(4 * 2334 / 10) = 934;
     * 66 paid out */
    CHECK(has(req("vault.deposit", "{\"fee_minor\":4000}"), "\"swept_minor\":\"666\""), "swept");
    EXPECT(req("swap.dividend", "{\"now\":2}"),
           ok("{\"pool_minor\":\"1000\",\"units\":\"20\",\"paid_minor\":\"66\",\"reinvest_minor\":"
              "\"934\",\"vbills\":\"4\",\"postings\":2}"));
    CHECK(has(req0("swap.status"), "\"vbills\":\"14\",\"reinvest\":true"), "position grew to 14");
    EXPECT(req0("swap.check"), ok("{\"ok\":true,\"status\":\"VSWAP_OK\"}"));
    zxvd_rpc_groups_reset();
}

#define CFG0                                                                                       \
    "{\"floor_num\":\"1\",\"floor_den\":\"1\",\"backing\":\"100\",\"cap\":\"1000\",\"p_start\":"   \
    "\"200\",\"p_step\":\"10\",\"p_min\":\"100\"}"

/* ---- each method sets the economy up when it is the first call ---- */
static void test_fresh(void)
{
    groups();
    zxvd_rpc_enable("all");
    CHECK(has(req0("swap.status"), "\"notice_medium\":\"144\",\"notice_deep\":\"6765\","
                                   "\"weights\":[1,2,3],\"holders\":[],\"positions\":[]}"),
          "swap.status first");
    groups();
    zxvd_rpc_enable("all");
    EXPECT(req0("auction.configure"), ok(CFG0));
    groups();
    zxvd_rpc_enable("all");
    EXPECT(req0("auction.open"), ok("{\"epoch\":\"1\",\"state\":\"open\"}"));
    groups();
    zxvd_rpc_enable("all");
    EXPECT(req0("swap.check"), ok("{\"ok\":true,\"status\":\"VSWAP_OK\"}"));
    /* a second fee reports only its own overflow, not the running total */
    groups();
    zxvd_rpc_enable("all");
    CHECK(has(req("vault.deposit", "{\"fee_minor\":10000}"), "\"kept_minor\":\"1666\""), "fee 1");
    /* 5000 / 3 with the carry of 2 from the first: 1667, carry 1 */
    EXPECT(req("vault.deposit", "{\"fee_minor\":10000}"),
           ok("{\"fee_minor\":\"10000\",\"reserve_minor\":\"5000\",\"dividend_minor\":\"2500\","
              "\"bounty_minor\":\"1500\",\"regen_minor\":\"1000\",\"swept_minor\":\"0\","
              "\"kept_minor\":\"1667\",\"carry\":\"1\"}"));
    /* holder 0 is never opened by accident */
    req("swap.deposit", "{\"holder\":4,\"amount_minor\":100}");
    rpc(REQB);
    EXPECT(req("swap.withdraw", "{\"holder\":0,\"amount_minor\":1}"),
           ECON("unknown holder 0: swap.deposit opens one"));
    zxvd_rpc_groups_reset();
}

/* ---- auction.* (vauction.h A1-A8) ---- */
static void test_auctions(void)
{
    groups();
    zxvd_rpc_enable("auctions,swap,vault");
    EXPECT(req("auction.status", "{\"now\":1}"),
           ok("{\"state\":\"idle\",\"epoch\":\"0\",\"config\":" CFG0 ",\"reserve_minor\":\"0\","
              "\"outstanding\":\"0\",\"floor_ok\":true,\"bids\":[],\"history\":[]}"));
    EXPECT(req0("auction.configure"), ok(CFG0));
    EXPECT(req("auction.configure", "{\"p_min\":300}"), ECON("VAUC_ERR_ARG"));
    EXPECT(req("auction.configure", "{\"floor_den\":0}"), ECON("VAUC_ERR_ARG"));
    EXPECT(req("auction.configure", "{\"cap\":-1}"), PARAM("\\\"cap\\\" must not be negative"));
    const char *const K[] = {"floor_num", "floor_den", "backing", "cap",
                             "p_start",   "p_step",    "p_min"};
    for (size_t i = 0; i < 7; i++) {
        char m[80];
        snprintf(m, sizeof m, "\\\"%s\\\" must not be negative", K[i]);
        EXPECT(req("auction.configure", "{\"%s\":-5}", K[i]), PARAM(m));
    }
    EXPECT(req("auction.configure",
               "{\"floor_num\":2,\"floor_den\":1,\"backing\":100,\"cap\":500,\"p_start\":200,"
               "\"p_step\":10,\"p_min\":100}"),
           ok("{\"floor_num\":\"2\",\"floor_den\":\"1\",\"backing\":\"100\",\"cap\":\"500\","
              "\"p_start\":\"200\",\"p_step\":\"10\",\"p_min\":\"100\"}"));
    EXPECT(req("auction.run", "{\"now\":1}"), ECON("VAUC_ERR_STATE"));
    EXPECT(req("auction.cancel", "{\"now\":1}"), ECON("VAUC_ERR_STATE"));
    EXPECT(req("auction.run", "{\"now\":-1}"), PARAM("\\\"now\\\" must not be negative"));
    EXPECT(req("auction.cancel", "{\"now\":-1}"), PARAM("\\\"now\\\" must not be negative"));

    /* a reserve of 3334 with no V-Bills out: fee 10000 -> reserve share 5000,
     * nothing swept (cap 0), so R = 5000 */
    CHECK(has(req("vault.deposit", "{\"fee_minor\":10000}"), "\"kept_minor\":\"1666\""), "fee");
    req("swap.deposit", "{\"holder\":8,\"amount_minor\":30000}");
    rpc(REQB);
    req("swap.deposit", "{\"holder\":9,\"amount_minor\":30000}");
    rpc(REQB);
    EXPECT(req("auction.bid", "{\"holder\":8,\"qty\":10,\"limit_minor\":150}"),
           ECON("VAUC_ERR_STATE"));
    EXPECT(req0("auction.open"), ok("{\"epoch\":\"1\",\"state\":\"open\"}"));
    EXPECT(req0("auction.open"), ECON("VAUC_ERR_STATE"));
    EXPECT(req("auction.configure", "{\"cap\":5}"), ECON("VAUC_ERR_STATE"));
    EXPECT(req("auction.bid", "{\"holder\":8,\"qty\":100,\"limit_minor\":150,\"now\":10}"),
           ok("{\"epoch\":\"1\",\"bid\":0,\"locked_minor\":\"15000\"}"));
    EXPECT(req("auction.bid", "{\"holder\":9,\"qty\":20,\"limit_minor\":\"180\",\"now\":11}"),
           ok("{\"epoch\":\"1\",\"bid\":1,\"locked_minor\":\"3600\"}"));
    EXPECT(req("auction.bid", "{\"holder\":9,\"qty\":10,\"limit_minor\":99}"),
           ECON("VAUC_ERR_ARG"));
    EXPECT(req("auction.bid", "{\"holder\":9,\"qty\":1000,\"limit_minor\":100}"),
           ECON("VAUC_ERR_LEDGER (PAY_ERR_FUNDS)"));
    EXPECT(req("auction.bid", "{\"holder\":3,\"qty\":1,\"limit_minor\":100}"),
           ECON("unknown holder 3: swap.deposit opens one"));
    EXPECT(req("auction.bid", "{\"holder\":9,\"limit_minor\":100}"),
           PARAM("missing integer parameter \\\"qty\\\""));
    EXPECT(req("auction.bid", "{\"holder\":9,\"qty\":1}"),
           PARAM("missing integer parameter \\\"limit_minor\\\""));
    EXPECT(req("auction.bid", "{\"holder\":9,\"qty\":1,\"limit_minor\":100,\"now\":-1}"),
           PARAM("\\\"now\\\" must not be negative"));
    /* A4 with k = 2 * 100 = 200: Q(p) = floor(5000 / (200 - p)) below 200.
     * p 190: Q 500 (cap 500), D 20; 180: Q 250, D 20; 170: 166, D 0... D(170) = 20;
     * 160: 125, 20; 150: 100, D 120 >= 100: clears at 150, quantity 100.
     * Previous rung 160: D(160) = 20 <= 100, so bid 1 fills 20 in full and
     * bid 0 gets the marginal 80. */
    EXPECT(req("auction.quote", "{\"price_minor\":150,\"now\":12}"),
           ok("{\"price_minor\":\"150\",\"reserve_minor\":\"5000\",\"outstanding\":\"0\","
              "\"supply\":\"100\",\"demand\":\"120\",\"floor_ok\":true}"));
    EXPECT(req("auction.quote", "{\"price_minor\":200}"),
           ok("{\"price_minor\":\"200\",\"reserve_minor\":\"5000\",\"outstanding\":\"0\","
              "\"supply\":\"500\",\"demand\":\"0\",\"floor_ok\":true}"));
    EXPECT(req0("auction.quote"), PARAM("missing integer parameter \\\"price_minor\\\""));
    EXPECT(req("auction.quote", "{\"price_minor\":1,\"now\":-1}"),
           PARAM("\\\"now\\\" must not be negative"));
    EXPECT(req("auction.status", "{\"now\":12}"),
           ok("{\"state\":\"open\",\"epoch\":\"1\",\"config\":{\"floor_num\":\"2\",\"floor_den\":"
              "\"1\",\"backing\":\"100\",\"cap\":\"500\",\"p_start\":\"200\",\"p_step\":\"10\","
              "\"p_min\":\"100\"},\"reserve_minor\":\"5000\",\"outstanding\":\"0\",\"floor_ok\":"
              "true,\"bids\":[{\"bid\":0,\"holder\":8,\"qty\":\"100\",\"limit_minor\":\"150\","
              "\"locked_minor\":\"15000\",\"fill\":\"0\",\"settled\":false},{\"bid\":1,\"holder\":"
              "9,\"qty\":\"20\",\"limit_minor\":\"180\",\"locked_minor\":\"3600\",\"fill\":\"0\","
              "\"settled\":false}],\"history\":[]}"));
    /* A7: R after = 5000 + 100 * 150 = 20000 >= 2 * 100 * 100 */
    EXPECT(req("auction.run", "{\"now\":13}"),
           ok("{\"epoch\":\"1\",\"price_minor\":\"150\",\"qty\":\"100\",\"winners\":2,"
              "\"cancelled\":false,\"reserve_before_minor\":\"5000\",\"reserve_after_minor\":"
              "\"20000\",\"outstanding_before\":\"0\",\"outstanding_after\":\"100\",\"floor_ok\":"
              "true}"));
    EXPECT(req("auction.run", "{\"now\":13}"), ECON("VAUC_ERR_STATE"));
    /* bidder 8: 30000 - 80 * 150; bidder 9: 30000 - 20 * 150 */
    CHECK(has(req0("swap.status"), "\"holders\":[" H(8, "18000", "80") "," H(9, "27000", "20") "]"),
          "fills and refunds");
    EXPECT(req("auction.check", "{\"now\":14}"),
           ok("{\"ok\":true,\"floor_ok\":true,\"ledger_ok\":true}"));
    EXPECT(req("auction.check", "{\"now\":-1}"), PARAM("\\\"now\\\" must not be negative"));
    EXPECT(req("auction.status", "{\"now\":-1}"), PARAM("\\\"now\\\" must not be negative"));

    /* epoch 2: R 20000 sits exactly on the floor 2 * 100 * 100. At 200 every
     * quantity keeps the floor (S = cap) but nobody bids 200; at 190 S = 0 and
     * the clock stops: the price is the last rung with supply, 200, nothing
     * issues and the bid is refunded */
    EXPECT(req0("auction.open"), ok("{\"epoch\":\"2\",\"state\":\"open\"}"));
    CHECK(has(req("auction.bid", "{\"holder\":9,\"qty\":5,\"limit_minor\":190}"), "\"bid\":0"),
          "bid");
    EXPECT(req("auction.run", "{\"now\":20}"),
           ok("{\"epoch\":\"2\",\"price_minor\":\"200\",\"qty\":\"0\",\"winners\":0,"
              "\"cancelled\":false,\"reserve_before_minor\":\"20000\",\"reserve_after_minor\":"
              "\"20000\",\"outstanding_before\":\"100\",\"outstanding_after\":\"100\",\"floor_ok\":"
              "true}"));
    /* epoch 3: cancelled, every lock refunded */
    EXPECT(req0("auction.open"), ok("{\"epoch\":\"3\",\"state\":\"open\"}"));
    CHECK(has(req("auction.bid", "{\"holder\":9,\"qty\":5,\"limit_minor\":200}"),
              "\"locked_minor\":\"1000\""),
          "bid");
    EXPECT(req("auction.cancel", "{\"now\":30}"),
           ok("{\"epoch\":\"3\",\"price_minor\":\"0\",\"qty\":\"0\",\"winners\":0,"
              "\"cancelled\":true,\"reserve_before_minor\":\"0\",\"reserve_after_minor\":"
              "\"20000\",\"outstanding_before\":\"0\",\"outstanding_after\":\"100\",\"floor_ok\":"
              "true}"));
    CHECK(has(req0("swap.status"), H(9, "27000", "20")), "refunded");
    CHECK(has(req("auction.status", "{\"now\":31}"),
              "\"history\":[{\"epoch\":\"3\",\"price_minor\":\"0\""),
          "history newest first");
    CHECK(last("{\"epoch\":\"2\",") && last("{\"epoch\":\"1\","), "and older");
    zxvd_rpc_groups_reset();
}

#define MINT2                                                                                      \
    "{\"minter\":3,\"kappa\":10,\"legs\":[{\"kind\":\"vbill\",\"asset\":1,\"qty\":60,\"weight\":"  \
    "3,\"price_num\":100},{\"kind\":\"fiat\",\"asset\":2,\"qty\":4000,\"weight\":2,"               \
    "\"price_num\":1}],\"now\":20}"

#define LEG1 "{\"kind\":\"vbill\",\"asset\":1,\"qty\":1,\"weight\":1,\"price_num\":1}"

/* ---- zenny.* (zenny.h Z1-Z6) ---- */
static void test_zenny(void)
{
    groups();
    zxvd_rpc_enable("zenny");
    /* value 60 * 100 + 4000 = 10000 = 1000 chips of kappa 10; weights 3:2 = 6000:4000 */
    EXPECT(req("zenny.mint", MINT2),
           ok("{\"chip\":\"e812e0fc66129e2d4c038033a3ec7ca27bada9825125b283756c7567daa8670d\","
              "\"slot\":0,\"nonce\":\"0\",\"version\":\"0\",\"chips\":\"1000\"}"));
    char id[80];
    hexval("chip", id, sizeof id);
    /* the same content again gets the next nonce, so another id */
    CHECK(has(req("zenny.mint", MINT2), "\"slot\":1,\"nonce\":\"1\""), "a second chip");
    EXPECT(req("zenny.mint", "{\"minter\":3,\"kappa\":10,\"nonce\":0,\"legs\":[{\"kind\":\"vbill\","
                             "\"asset\":1,\"qty\":60,\"weight\":3,\"price_num\":100},{\"kind\":"
                             "\"fiat\",\"asset\":2,\"qty\":4000,\"weight\":2,\"price_num\":1}]}"),
           ECON("ZENNY_ERR_DUP"));
    /* refusals: the engine's and the parameters' */
    const char *const BAD[][2] = {
        {"{\"minter\":3,\"kappa\":1,\"legs\":[{\"kind\":\"fiat\",\"asset\":2,\"qty\":5,\"weight\":"
         "1,\"price_num\":1}]}",
         "ZENNY_ERR_ANCHOR"},
        {"{\"minter\":3,\"kappa\":1,\"legs\":[{\"kind\":\"vbill\",\"asset\":1,\"qty\":5,\"weight\":"
         "1,\"price_num\":1},{\"kind\":\"commodity\",\"custodian\":9,\"asset\":4,\"qty\":5,"
         "\"weight\":1,\"price_num\":1}]}",
         "ZENNY_ERR_ATTEST"},
        {"{\"minter\":3,\"co_minter\":4,\"rules\":2,\"kappa\":1,\"legs\":[{\"kind\":\"vbill\","
         "\"asset\":1,\"qty\":5,\"weight\":1,\"price_num\":1}]}",
         "ZENNY_ERR_SIG"},
        {"{\"minter\":3,\"rules\":4,\"kappa\":1,\"legs\":[{\"kind\":\"vbill\",\"asset\":1,\"qty\":"
         "5,\"weight\":1,\"price_num\":1}]}",
         "ZENNY_ERR_LEVERAGE"},
        {"{\"minter\":3,\"rules\":8,\"kappa\":1,\"legs\":[{\"kind\":\"vbill\",\"asset\":1,\"qty\":"
         "5,\"weight\":1,\"price_num\":1}]}",
         "ZENNY_ERR_CREDIT"},
        {"{\"minter\":3,\"kappa\":0,\"legs\":[{\"kind\":\"vbill\",\"asset\":1,\"qty\":5,\"weight\":"
         "1,\"price_num\":1}]}",
         "ZENNY_ERR_KAPPA"},
        {"{\"minter\":3,\"kappa\":3,\"legs\":[{\"kind\":\"vbill\",\"asset\":1,\"qty\":5,\"weight\":"
         "1,\"price_num\":1}]}",
         "ZENNY_ERR_INEXACT"},
        {"{\"minter\":3,\"kappa\":1,\"legs\":[{\"kind\":\"vbill\",\"asset\":1,\"qty\":5,\"weight\":"
         "1,\"price_num\":1,\"price_den\":0}]}",
         "ZENNY_ERR_PRICE"},
        {"{\"minter\":3,\"kappa\":1,\"legs\":[{\"kind\":\"vbill\",\"asset\":1,\"qty\":5,\"weight\":"
         "1,\"price_num\":1},{\"kind\":\"compute\",\"asset\":7,\"qty\":5,\"weight\":2,"
         "\"price_num\":1}]}",
         "ZENNY_ERR_WEIGHT"},
    };
    for (size_t i = 0; i < sizeof BAD / sizeof BAD[0]; i++)
        EXPECT(req("zenny.mint", "%s", BAD[i][0]), ECON(BAD[i][1]));
    /* a compute leg is fine when its weight matches its value; and the price den */
    CHECK(has(req("zenny.mint",
                  "{\"minter\":5,\"kappa\":1,\"rules\":1,\"legs\":[{\"kind\":\"vbill\",\"asset\":1,"
                  "\"qty\":6,\"weight\":1,\"price_num\":1,\"price_den\":2},{\"kind\":\"compute\","
                  "\"asset\":7,\"qty\":3,\"weight\":1,\"price_num\":1}]}"),
              "\"slot\":2,\"nonce\":\"2\",\"version\":\"0\",\"chips\":\"6\""),
          "compute leg, rebalance allowed, price 1/2");
    EXPECT(req("zenny.mint", "{\"kappa\":1}"), PARAM("missing integer parameter \\\"minter\\\""));
    EXPECT(req("zenny.mint", "{\"minter\":1,\"co_minter\":-1}"),
           PARAM("\\\"co_minter\\\" is out of range"));
    EXPECT(req("zenny.mint", "{\"minter\":1}"), PARAM("missing integer parameter \\\"kappa\\\""));
    EXPECT(req("zenny.mint", "{\"minter\":1,\"kappa\":1,\"rules\":\"x\"}"),
           PARAM("\\\"rules\\\" must be an integer (no fraction or exponent)"));
    EXPECT(req("zenny.mint", "{\"minter\":1,\"kappa\":1,\"nonce\":-1}"),
           PARAM("\\\"nonce\\\" must not be negative"));
    EXPECT(req("zenny.mint", "{\"minter\":1,\"kappa\":1,\"now\":-1}"),
           PARAM("\\\"now\\\" must not be negative"));
    EXPECT(req("zenny.mint", "{\"minter\":1,\"kappa\":1}"),
           PARAM("\\\"legs\\\" must have 1 to 8 legs"));
    EXPECT(req("zenny.mint", "{\"minter\":1,\"kappa\":1,\"legs\":[]}"),
           PARAM("\\\"legs\\\" must have 1 to 8 legs"));
    EXPECT(req("zenny.mint", "{\"minter\":1,\"kappa\":1,\"legs\":[1,2,3,4,5,6,7,8,9]}"),
           PARAM("\\\"legs\\\" must have 1 to 8 legs"));
    EXPECT(req("zenny.mint", "{\"minter\":1,\"kappa\":1,\"legs\":{}}"),
           PARAM("\\\"legs\\\" must be an array"));
    EXPECT(req("zenny.mint", "{\"minter\":1,\"kappa\":1,\"legs\":[1]}"),
           PARAM("\\\"legs\\\"[0] must be an object"));
    EXPECT(req("zenny.mint", "{\"minter\":1,\"kappa\":1,\"legs\":[{\"kind\":\"gold\"}]}"),
           PARAM("\\\"kind\\\" must be vbill, fiat, commodity or compute"));
    EXPECT(req("zenny.mint", "{\"minter\":1,\"kappa\":1,\"legs\":[{\"asset\":1}]}"),
           PARAM("missing string parameter \\\"kind\\\""));
    const char *const LEGKEY[] = {"asset", "qty", "weight", "price_num"};
    const char *const LEGS[] = {"{\"kind\":\"vbill\"}", "{\"kind\":\"vbill\",\"asset\":1}",
                                "{\"kind\":\"vbill\",\"asset\":1,\"qty\":1}",
                                "{\"kind\":\"vbill\",\"asset\":1,\"qty\":1,\"weight\":1}"};
    for (size_t i = 0; i < 4; i++) {
        char m[80];
        snprintf(m, sizeof m, "missing integer parameter \\\"%s\\\"", LEGKEY[i]);
        EXPECT(req("zenny.mint", "{\"minter\":1,\"kappa\":1,\"legs\":[%s]}", LEGS[i]), PARAM(m));
    }
    EXPECT(req("zenny.mint",
               "{\"minter\":1,\"kappa\":1,\"legs\":[{\"kind\":\"vbill\",\"custodian\":"
               "-1}]}"),
           PARAM("\\\"custodian\\\" is out of range"));
    EXPECT(req("zenny.mint", "{\"minter\":1,\"kappa\":1,\"legs\":[{\"kind\":\"vbill\",\"asset\":1,"
                             "\"qty\":1,\"weight\":1,\"price_num\":1,\"price_den\":-1}]}"),
           PARAM("\\\"price_den\\\" must not be negative"));

    /* Z5: quote and decompress. Every c is exact against 60 and 4000 out of
     * 1000 when c is a multiple of 50 (60c/1000 needs 50 | c, 4000c/1000 any) */
    EXPECT(req("zenny.quote", "{\"chip\":\"%s\",\"want\":120}", id), ok("{\"chips\":\"100\"}"));
    EXPECT(req("zenny.quote", "{\"chip\":\"%s\",\"want\":5000}", id), ok("{\"chips\":\"1000\"}"));
    EXPECT(req("zenny.quote", "{\"chip\":\"%s\",\"want\":49}", id), ok("{\"chips\":\"0\"}"));
    /* batch price 1/3 for the fiat leg: value 4000c/1000/3 exact needs 3 | 4c */
    EXPECT(req("zenny.quote",
               "{\"chip\":\"%s\",\"want\":120,\"prices\":[{\"num\":100,\"den\":1},{\"num\":1,"
               "\"den\":3}]}",
               id),
           ok("{\"chips\":\"0\"}"));
    EXPECT(req("zenny.quote",
               "{\"chip\":\"%s\",\"want\":200,\"prices\":[{\"num\":100,\"den\":1},{\"num\":1,"
               "\"den\":3}]}",
               id),
           ok("{\"chips\":\"150\"}"));
    EXPECT(req("zenny.quote", "{\"chip\":\"%s\",\"want\":1,\"prices\":[]}", id),
           PARAM("\\\"prices\\\" must have one {num, den} per leg (2)"));
    EXPECT(req("zenny.quote", "{\"chip\":\"%s\",\"want\":1,\"prices\":3}", id),
           PARAM("\\\"prices\\\" must be an array"));
    EXPECT(req("zenny.quote", "{\"chip\":\"%s\",\"want\":1,\"prices\":[1,2]}", id),
           PARAM("\\\"prices\\\"[0] must be an object"));
    EXPECT(req("zenny.quote", "{\"chip\":\"%s\",\"want\":1,\"prices\":[{\"num\":1},{}]}", id),
           PARAM("missing integer parameter \\\"den\\\""));
    EXPECT(req("zenny.quote", "{\"chip\":\"%s\",\"want\":1,\"prices\":[{\"num\":1,\"den\":1},{}]}",
               id),
           PARAM("missing integer parameter \\\"num\\\""));
    EXPECT(req("zenny.quote", "{\"chip\":\"%s\"}", id),
           PARAM("missing integer parameter \\\"want\\\""));
    EXPECT(req("zenny.quote", "{\"want\":1}"), PARAM("missing hex parameter \\\"chip\\\""));
    EXPECT(req("zenny.quote", "{\"chip\":\"00\",\"want\":1}"), ECON("unknown chip"));
    EXPECT(req("zenny.quote", "{\"chip\":\"%.62s00\",\"want\":1}",
               "000000000000000000000000000000000000000000000000000000000000000000"),
           ECON("unknown chip"));

    /* tap to pay 100 of 1000 at batch prices 110 and 1: out 6 and 400, value
     * 660 + 400 = 1060, fee floor(1060 * 8889 / 10^7) = 0 */
    EXPECT(req("zenny.decompress",
               "{\"chip\":\"%s\",\"chips\":100,\"prices\":[{\"num\":110,\"den\":1},{\"num\":1,"
               "\"den\":1}],\"now\":30}",
               id),
           ok("{\"seq\":\"0\",\"chip\":\"%s\",\"version\":\"1\",\"chips\":\"100\",\"out\":[\"6\","
              "\"400\"],\"value\":[\"660\",\"400\"],\"total\":\"1060\",\"fee\":\"0\",\"tick\":"
              "\"30\"}",
              id));
    /* 500 more at 30000 per V-Bill: 30 * 30000 + 2000 = 902000, fee 801 */
    EXPECT(req("zenny.decompress",
               "{\"chip\":\"%s\",\"chips\":\"500\",\"prices\":[{\"num\":30000,\"den\":1},{\"num\":"
               "1,\"den\":1}],\"now\":31}",
               id),
           ok("{\"seq\":\"1\",\"chip\":\"%s\",\"version\":\"2\",\"chips\":\"500\",\"out\":[\"30\","
              "\"2000\"],\"value\":[\"900000\",\"2000\"],\"total\":\"902000\",\"fee\":\"801\","
              "\"tick\":\"31\"}",
              id));
    EXPECT(req("zenny.decompress",
               "{\"chip\":\"%s\",\"chips\":401,\"prices\":[{\"num\":1,\"den\":1},{\"num\":1,"
               "\"den\":1}]}",
               id),
           ECON("ZENNY_ERR_FUNDS"));
    EXPECT(req("zenny.decompress",
               "{\"chip\":\"%s\",\"chips\":10,\"prices\":[{\"num\":1,\"den\":1},{\"num\":1,"
               "\"den\":1}]}",
               id),
           ECON("ZENNY_ERR_INEXACT"));
    EXPECT(req("zenny.decompress", "{\"chip\":\"%s\",\"chips\":100}", id),
           PARAM("\\\"prices\\\" must have one {num, den} per leg (2)"));
    EXPECT(req("zenny.decompress", "{\"chip\":\"%s\",\"prices\":[]}", id),
           PARAM("missing integer parameter \\\"chips\\\""));
    EXPECT(req("zenny.decompress",
               "{\"chip\":\"%s\",\"chips\":100,\"prices\":[{\"num\":1,\"den\":1},{\"num\":1,"
               "\"den\":1}],\"now\":-1}",
               id),
           PARAM("\\\"now\\\" must not be negative"));
    EXPECT(req("zenny.decompress", "{\"chip\":\"ab\",\"chips\":1}"), ECON("unknown chip"));

    EXPECT(
        req0("zenny.status"),
        ok("{\"chips\":[{\"chip\":\"%s\",\"slot\":0,\"minter\":3,\"co_minter\":0,\"rules\":0,"
           "\"kappa\":\"10\",\"versions\":3,\"outstanding\":\"400\",\"legs\":[{\"kind\":"
           "\"vbill\",\"custodian\":0,\"asset\":\"1\",\"qty\":\"24\",\"weight\":\"3\",\"price\":"
           "{\"num\":\"100\",\"den\":\"1\"}},{\"kind\":\"fiat\",\"custodian\":0,\"asset\":\"2\","
           "\"qty\":\"1600\",\"weight\":\"2\",\"price\":{\"num\":\"1\",\"den\":\"1\"}}]},"
           "{\"chip\":\"41ae4fc0693b03270d2a90fba92c8ee459c7a30ac62f5490992cfcd9288807c2\","
           "\"slot\":"
           "1,\"minter\":3,\"co_minter\":0,\"rules\":0,\"kappa\":\"10\",\"versions\":1,"
           "\"outstanding\":\"1000\",\"legs\":[{\"kind\":\"vbill\",\"custodian\":0,\"asset\":"
           "\"1\",\"qty\":\"60\",\"weight\":\"3\",\"price\":{\"num\":\"100\",\"den\":\"1\"}},"
           "{\"kind\":\"fiat\",\"custodian\":0,\"asset\":\"2\",\"qty\":\"4000\",\"weight\":"
           "\"2\",\"price\":{\"num\":\"1\",\"den\":\"1\"}}]},{\"chip\":\"743bc35329aded391fc1cc"
           "67e0a51c079e43fbf9cb222636ff7be3d48d597dc1\",\"slot\":2,\"minter\":5,\"co_minter\":0,"
           "\"rules\":1,\"kappa\":\"1\",\"versions\":1,\"outstanding\":\"6\",\"legs\":[{"
           "\"kind\":\"vbill\",\"custodian\":0,\"asset\":\"1\",\"qty\":\"6\",\"weight\":\"1\","
           "\"price\":{\"num\":\"1\",\"den\":\"2\"}},{\"kind\":\"compute\",\"custodian\":0,"
           "\"asset\":\"7\",\"qty\":\"3\",\"weight\":\"1\",\"price\":{\"num\":\"1\",\"den\":"
           "\"1\"}}]}],\"receipts_pending\":\"2\"}",
           id));
    CHECK(has(req0("zenny.status"),
              "{\"kind\":\"compute\",\"custodian\":0,\"asset\":\"7\",\"qty\":\"3\",\"weight\":"
              "\"1\",\"price\":{\"num\":\"1\",\"den\":\"1\"}}]}],\"receipts_pending\":\"2\"}"),
          "three chips, two receipts");
    CHECK(last("\"slot\":2,\"minter\":5,\"co_minter\":0,\"rules\":1,\"kappa\":\"1\""),
          "chip 3 header");
    CHECK(has(req0("zenny.check"), "\"chain_ok\":true,\"claim_ok\":true}],\"ok\":true}"),
          "every chip checks");
    CHECK(last("{\"chips\":[{\"chip\":\"") && last("\"claim_ok\":true},{\"chip\":\""),
          "check lists each chip");

    /* receipts: list, then drain */
    CHECK(has(req0("zenny.receipts"), "\"tick\":\"31\"}],\"next\":\"2\",\"acked\":\"0\"}"),
          "two receipts");
    CHECK(last("{\"receipts\":[{\"seq\":\"0\",") && last("},{\"seq\":\"1\","),
          "both listed in order");
    EXPECT(req("zenny.receipts", "{\"ack\":3}"), ECON("ZENNY_ERR_ARG"));
    EXPECT(req("zenny.receipts", "{\"ack\":-3}"), PARAM("\\\"ack\\\" must not be negative"));
    CHECK(has(req("zenny.receipts", "{\"ack\":1}"), "{\"receipts\":[{\"seq\":\"1\","),
          "one left after ack 1");
    EXPECT(req("zenny.receipts", "{\"ack\":2}"),
           ok("{\"receipts\":[],\"next\":\"2\",\"acked\":\"2\"}"));
    EXPECT(req("zenny.receipts", "{\"ack\":1}"), ECON("ZENNY_ERR_ARG"));
    CHECK(has(req0("zenny.status"), "\"receipts_pending\":\"0\"}"), "nothing pending after ack");
    /* an explicit nonce leaves the counter alone: the next default is 3 */
    CHECK(has(req("zenny.mint",
                  "{\"minter\":3,\"kappa\":1,\"nonce\":77,\"legs\":[{\"kind\":\"vbill\",\"asset\":"
                  "1,\"qty\":5,\"weight\":1,\"price_num\":1}]}"),
              "\"slot\":3,\"nonce\":\"77\""),
          "explicit nonce");
    CHECK(has(req("zenny.mint", MINT2), "\"slot\":4,\"nonce\":\"3\""), "the counter did not move");
    /* eight legs is the most */
    CHECK(has(req("zenny.mint", "{\"minter\":3,\"kappa\":1,\"legs\":[" LEG1 "," LEG1 "," LEG1
                                "," LEG1 "," LEG1 "," LEG1 "," LEG1 "," LEG1 "]}"),
              "\"slot\":5,\"nonce\":\"4\",\"version\":\"0\",\"chips\":\"8\"}"),
          "eight legs");
    EXPECT(req("zenny.mint", "{\"minter\":1,\"kappa\":1,\"legs\":[{\"kind\":\"abcdefghijk\"}]}"),
           PARAM("\\\"kind\\\" must be vbill, fiat, commodity or compute"));
    EXPECT(req("zenny.mint", "{\"minter\":1,\"kappa\":1,\"legs\":[{\"kind\":\"abcdefghijkl\"}]}"),
           PARAM("\\\"kind\\\" must be a string of at most 11 bytes"));
    EXPECT(req("zenny.quote", "{\"chip\":\"%s00\",\"want\":1}", id),
           PARAM("\\\"chip\\\" must be a hex string of at most 32 bytes"));
    zxvd_rpc_groups_reset();
}

/* ---- stats.* (zstats.h S1-S6) ---- */
static void test_stats(void)
{
    groups();
    zxvd_rpc_enable("all");
    char prev[80], dig[80], enc[1300];
    const char *empty = "{\"format\":\"zxv/stats/v1\",\"epoch\":\"1\",\"tick\":\"50\",";
    CHECK(has(req("stats.snapshot", "{\"now\":50}"), empty), "epoch 1 at tick 50");
    hexval("digest", dig, sizeof dig);
    hexval("encoded", enc, sizeof enc);
    CHECK(strlen(enc) == 2 * ZSTATS_ENC_LEN, "the encoding is %u bytes", ZSTATS_ENC_LEN);
    uint8_t raw[ZSTATS_ENC_LEN], d[32], p0[32];
    char dh[65];
    unhex(enc, raw, sizeof raw);
    sha256(raw, sizeof raw, d);
    tohex(d, 32, dh);
    CHECK(!strcmp(dh, dig), "digest = SHA-256 of the encoding");
    zstats_snapshot_t s;
    CHECK(zstats_decode(raw, sizeof raw, &s, p0) == ZSTATS_OK && s.epoch == 1 && s.tick == 50,
          "the encoding decodes");
    CHECK(last("\"prev\":\"0000000000000000000000000000000000000000000000000000000000000000\""),
          "the first prev is zeros");
    CHECK(last("\"required\":{\"num\":\"1\",\"den\":\"1\"}"), "required 1/1 by default");
    EXPECT(req0("stats.status"), ok("{\"published\":\"1\",\"head\":\"%s\",\"signed\":false}", dig));

    /* a small economy: 3 V-Bills issued, a fee, an auction */
    req("swap.deposit", "{\"holder\":1,\"amount_minor\":1000}");
    rpc(REQB);
    rpc(req0("swap.enable"));
    rpc(req("swap.in", "{\"holder\":1,\"amount_minor\":300}"));
    rpc(req("vault.deposit", "{\"fee_minor\":1000}"));
    CHECK(has(req("zenny.mint", MINT2), "\"chips\":\"1000\""), "a chip");
    snprintf(prev, sizeof prev, "%s", dig);
    /* reserve 300 + 500 - floor(500 / 3) = 634 against 2000 Vino issued (the
     * deposit 1000 and the fee 1000) */
    CHECK(has(req("stats.snapshot", "{\"now\":60,\"min_cohort\":1,\"req_num\":1,\"req_den\":2}"),
              "{\"epoch\":\"2\",\"prev\":\""),
          "epoch 2");
    hexval("prev", dig, sizeof dig);
    CHECK(!strcmp(prev, dig), "prev is the last digest");
    CHECK(last("\"zenny\":{\"live_chips\":\"1\",\"chips_outstanding\":\"1000\",\"suppressed\":"
               "false}}}}"),
          "zenny aggregates at cohort 1");
    CHECK(last("\"vault\":{\"vbills_held\":\"0\",\"vbills_outstanding\":\"3\""), "vault section");
    CHECK(last("\"reserve\":{\"reserve_vino\":\"634\",\"claims_vino\":\"2000\",\"floor\":{\"num\":"
               "\"634\",\"den\":\"2000\"}"),
          "reserve section");
    CHECK(last("\"required\":{\"num\":\"1\",\"den\":\"2\"},\"floor_ok\":false"), "634 * 2 < 2000");
    /* min_cohort 5 suppresses the single chip */
    CHECK(has(req("stats.snapshot", "{\"now\":70}"),
              "\"zenny\":{\"live_chips\":\"0\",\"chips_outstanding\":\"0\",\"suppressed\":true}"),
          "suppressed");
    CHECK(last("{\"epoch\":\"3\","), "epoch 3");
    EXPECT(req("stats.snapshot", "{\"min_cohort\":0}"),
           PARAM("\\\"min_cohort\\\" must be 1 to 4294967295"));
    EXPECT(req("stats.snapshot", "{\"min_cohort\":4294967296}"),
           PARAM("\\\"min_cohort\\\" must be 1 to 4294967295"));
    CHECK(has(req("stats.snapshot", "{\"min_cohort\":4294967295,\"now\":71}"), "{\"epoch\":\"4\","),
          "the largest cohort");
    EXPECT(req("stats.snapshot", "{\"req_den\":0}"), ECON("ZSTATS_ERR_ARG"));
    EXPECT(req("stats.snapshot", "{\"req_num\":-1}"),
           PARAM("\\\"req_num\\\" must not be negative"));
    EXPECT(req("stats.snapshot", "{\"req_den\":-1}"),
           PARAM("\\\"req_den\\\" must not be negative"));
    EXPECT(req("stats.snapshot", "{\"min_cohort\":-1}"),
           PARAM("\\\"min_cohort\\\" must not be negative"));
    EXPECT(req("stats.snapshot", "{\"now\":-1}"), PARAM("\\\"now\\\" must not be negative"));
    CHECK(has(req0("stats.status"), "{\"published\":\"4\",\"head\":\""), "four published");
    zxvd_rpc_groups_reset();
    EXPECT(req0("stats.status"), err(-32601, "method not found"));
}

int main(void)
{
    test_switches();
    test_vault();
    test_swap();
    test_auctions();
    test_fresh();
    test_zenny();
    test_stats();
    free(OUT.p);
    if (fails) {
        printf("[FAIL] test_zxvd_econ: %d of %d checks failed\n", fails, checks);
        return 1;
    }
    printf("[PASS] test_zxvd_econ: %d checks\n", checks);
    return 0;
}
