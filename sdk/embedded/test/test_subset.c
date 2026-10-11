/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_subset.c — the embedded libzxv subset (no PQC, no engine) built
 * through the port layer, on the host: fail-closed start-up, the groups
 * that run on a microcontroller, the heap each handle needs, and the
 * ledger's ~24 MB book refused under an MCU-sized heap limit. */
#define _POSIX_C_SOURCE 200809L
#include <stdint.h>

#include "zxe.h"
#include "zxe_test.h"
#include "zxp.h"
#include "zxp_posix.h"

#define OK(call) IS((call), ZXV_OK)

static size_t live(void)
{
    zxp_heap_stats_t s;
    zxp_heap_stats(&s);
    return s.live_bytes;
}

static int g_denied;
static void on_budget(void *user, uint32_t budget, int32_t ev)
{
    (void) user;
    (void) budget;
    if (ev == ZXV_RMAG_EV_DENIED) g_denied++;
}

static void test_closed(void)
{
    zxv_ctx *ctx = NULL;
    uint8_t r[16];
    int64_t n, d;
    /* No zxp_init yet: no context, no randomness... */
    IS(zxv_ctx_create(&ctx), ZXV_E_INTERNAL);
    CHECK(ctx == NULL, "no context handed out");
    IS(zxv_random(r, sizeof r), ZXV_E_IO);
    /* ...but pure functions work. */
    OK(zxv_rat_add(1, 3, 1, 6, &n, &d));
    CHECK(n == 1 && d == 2, "1/3 + 1/6 = %lld/%lld", (long long) n, (long long) d);
}

static void test_features(void)
{
    uint32_t f = 0, maj = 0, min = 0, pat = 0;
    OK(zxv_features(&f));
    OK(zxv_version(&maj, &min, &pat));
    IS(maj, 1);
    CHECK(min >= 3, "ABI 1.3+");
    CHECK(f & ZXV_FEAT_RATIONAL && f & ZXV_FEAT_LPRES && f & ZXV_FEAT_CAPS && f & ZXV_FEAT_RMAG &&
              f & ZXV_FEAT_PHASE && f & ZXV_FEAT_HEX && f & ZXV_FEAT_HASH && f & ZXV_FEAT_LEDGER,
          "core groups live (0x%08x)", f);
    CHECK(!(f & (ZXV_FEAT_MLKEM1024 | ZXV_FEAT_MLDSA87 | ZXV_FEAT_ED25519 | ZXV_FEAT_TENSOR)),
          "PQC and engine left out (0x%08x)", f);
}

static void test_pure(void)
{
    int64_t n, d;
    int32_t c, t;
    char buf[64];
    size_t len;
    OK(zxv_rat_parse("12.75", &n, &d));
    CHECK(n == 51 && d == 4, "12.75 = 51/4");
    OK(zxv_rat_mul(51, 4, 4, 3, &n, &d));
    CHECK(n == 17 && d == 1, "51/4 * 4/3 = 17");
    OK(zxv_rat_cmp(1, 3, 333333333, 1000000000, &c));
    IS(c, 1);
    IS(zxv_rat_div(1, 2, 0, 1, &n, &d) < 0, 1);
    OK(zxv_rat_format(-7, 3, buf, sizeof buf, &len));
    IS(strcmp(buf, "-7/3"), 0);
    /* INT64 edges: overflow is refused, never wrapped */
    CHECK(zxv_rat_mul(INT64_MAX, 1, 2, 1, &n, &d) != ZXV_OK, "overflow refused");
    OK(zxv_rat_from_ieee754(0x3FB999999999999AULL, &n, &d)); /* 0.1 */
    CHECK(n == 3602879701896397LL && d == 36028797018963968LL, "0.1 exactly");

    OK(zxv_lp_and(ZXV_LP_TRUE, ZXV_LP_BOTH, &t));
    IS(t, ZXV_LP_BOTH);
    OK(zxv_lp_or(ZXV_LP_FALSE, ZXV_LP_NEITHER, &t));
    IS(t, ZXV_LP_NEITHER);
    OK(zxv_lp_not(ZXV_LP_BOTH, &t));
    IS(t, ZXV_LP_BOTH);

    uint8_t tag, tag2;
    int32_t act;
    OK(zxv_hex_pack(ZXV_HEX_BASE_TRUE, ZXV_HEX_POL_NEGENTROPIC, ZXV_HEX_PHASE_MANIFEST, &tag));
    OK(zxv_hex_action(tag, &act));
    IS(act, ZXV_HEX_ACT_PROCEED);
    IS(strcmp(zxv_hex_name(tag), "true/negentropic/manifest"), 0);
    OK(zxv_hex_forbid(tag, &tag2));
    OK(zxv_hex_action(tag2, &act));
    IS(act, ZXV_HEX_ACT_ROLLBACK);
    IS(zxv_hex_step(tag, ZXV_HEX_PHASE_EMERGING, &tag2), ZXV_E_STATE); /* a jump */
    IS(zxv_hex_pack(4, 0, 0, &tag), ZXV_E_ARG);
    /* all 64 tags: forbidden is absorbing under not */
    int absorbing = 1;
    for (unsigned x = 0; x < 64; x++) {
        uint8_t b, p, ph, nt, np;
        OK(zxv_hex_unpack((uint8_t) x, &b, &p, &ph));
        OK(zxv_hex_not((uint8_t) x, &nt));
        OK(zxv_hex_unpack(nt, NULL, &np, NULL));
        if (p == ZXV_HEX_POL_FORBIDDEN && np != ZXV_HEX_POL_FORBIDDEN) absorbing = 0;
    }
    CHECK(absorbing, "forbidden absorbing under not");

    uint8_t h[32];
    static const uint8_t abc_sha3[32] = {
        0x3a, 0x98, 0x5d, 0xa7, 0x4f, 0xe2, 0x25, 0xb2, 0x04, 0x5c, 0x17, 0x2d, 0x6b, 0xd3, 0x90, 0xbd,
        0x85, 0x5f, 0x08, 0x6e, 0x3e, 0x9d, 0x52, 0x5b, 0x46, 0xbf, 0xe2, 0x45, 0x11, 0x43, 0x15, 0x32};
    OK(zxv_sha3_256((const uint8_t *) "abc", 3, h));
    IS(memcmp(h, abc_sha3, 32), 0);

    uint8_t frame[21], wtag, wtag2;
    uint32_t sp[3] = {1, 2, 3}, sm[2] = {4, 5}, sp2[3], sm2[2];
    OK(zxv_wire_tag(1, 2, 1, &wtag));
    OK(zxv_wire_pack(wtag, sp, sm, frame));
    OK(zxv_wire_unpack(frame, &wtag2, sp2, sm2));
    CHECK(wtag2 == wtag && sp2[2] == 3 && sm2[1] == 5, "wire round trip");
}

static void test_handles(void)
{
    zxv_ctx *ctx;
    zxv_cap *root, *ro;
    size_t b0 = live();
    OK(zxv_ctx_create(&ctx));
    size_t b_ctx = live() - b0;
    OK(zxv_cap_create(ctx, 1, NULL, ZXV_CAP_ALL, &root));
    size_t b_cap = live() - b0 - b_ctx;
    OK(zxv_cap_attenuate(root, ZXV_CAP_LEDGER_READ, &ro));

    /* budgets: an energy budget of 5/2 units, exact */
    zxv_rmag *rm;
    uint32_t bud;
    size_t b1 = live();
    OK(zxv_rmag_create_cap(root, &rm));
    size_t b_rmag = live() - b1;
    OK(zxv_rmag_listen(rm, on_budget, NULL));
    OK(zxv_rmag_budget(root, rm, "energy", ZXV_RES_ENERGY, 5, 2, 1, &bud));
    OK(zxv_rmag_consume(root, rm, bud, 1, 3));
    OK(zxv_rmag_consume(root, rm, bud, 2, 1));
    IS(zxv_rmag_consume(root, rm, bud, 1, 3), ZXV_E_BUDGET); /* 1/3 + 2 + 1/3 > 5/2 */
    IS(g_denied, 1);
    int64_t rn, rd, un, ud;
    uint64_t ep0, ep1;
    OK(zxv_rmag_status(rm, bud, &rn, &rd, &un, &ud, &ep0));
    CHECK(rn == 1 && rd == 6 && un == 7 && ud == 3, "remaining 1/6, used 7/3 (%lld/%lld %lld/%lld)",
          (long long) rn, (long long) rd, (long long) un, (long long) ud);
    OK(zxv_rmag_release(root, rm, bud, 1, 1));
    OK(zxv_rmag_status(rm, bud, &rn, &rd, NULL, NULL, &ep1));
    CHECK(rn == 7 && rd == 6 && ep1 == ep0 + 1, "a release replenishes and bumps the epoch");
    IS(zxv_rmag_consume(ro, rm, bud, 1, 1), ZXV_E_CAP); /* read-only cap */
    IS(zxv_rmag_consume(root, rm, bud, -1, 1), ZXV_E_RMAG_INVALID);

    /* phase clock, driven exactly by zxe_clock */
    zxv_phase *ph;
    size_t b2 = live();
    OK(zxv_phase_create_cap(root, &ph));
    size_t b_phase = live() - b2;
    uint64_t ev, now, added;
    int32_t st;
    uint32_t cleared;
    OK(zxv_phase_defer(root, ph, ZXV_PHASE_CYCLE, &ev));
    zxe_clock clk;
    OK(zxe_clock_init(&clk, ZXV_PHASE_CYCLE)); /* one fundamental per second */
    OK(zxe_clock_pump_at(&clk, root, ph, 1000000, &added, &now, &cleared));
    IS(added, 0);
    uint64_t total = 0, t_us = 1000000;
    uint32_t total_cleared = 0;
    /* 1 s in uneven steps: exactly 27,720 ticks, whatever the split */
    static const uint64_t steps[] = {1, 7, 333, 4999, 12345, 98765, 383550, 500000};
    for (size_t i = 0; i < sizeof steps / sizeof steps[0]; i++) {
        t_us += steps[i];
        OK(zxe_clock_pump_at(&clk, root, ph, t_us, &added, &now, &cleared));
        total += added;
        total_cleared += cleared;
    }
    IS(t_us, 2000000);
    IS(total, ZXV_PHASE_CYCLE);
    IS(total_cleared, 1);
    OK(zxv_phase_event(ph, ev, &st, NULL));
    IS(st, ZXV_EV_CLEARED);
    IS(zxe_clock_pump_at(&clk, root, ph, t_us - 1, NULL, NULL, NULL), ZXV_E_STATE);
    IS(zxe_clock_pump_at(&clk, ro, ph, t_us + 1, NULL, NULL, NULL), ZXV_E_CAP);
    IS(zxe_clock_init(&clk, 0), ZXV_E_ARG);
    /* a long run: 10^6 random steps at 1000 ticks/s add floor(T * 1000 / 1e6) */
    OK(zxe_clock_init(&clk, 1000));
    uint64_t t0 = 5, t = 5, sum = 0;
    OK(zxe_clock_pump_at(&clk, root, ph, t, NULL, NULL, NULL));
    uint32_t x = 12345;
    int pumps_ok = 1;
    for (int i = 0; i < 1000000; i++) {
        x = x * 1103515245u + 12345u;
        t += (x >> 16) % 3000;
        pumps_ok &= zxe_clock_pump_at(&clk, root, ph, t, &added, NULL, NULL) == ZXV_OK;
        sum += added;
    }
    CHECK(pumps_ok, "10^6 pumps");
    IS(sum, (t - t0) * 1000 / 1000000);

    /* quarantine log */
    zxv_hexlog *log;
    size_t b3 = live();
    OK(zxv_hexlog_create(root, 16, &log));
    size_t b_log = live() - b3;
    uint8_t ft, subj[32] = {0}, head[32];
    uint64_t seq;
    uint32_t cnt;
    OK(zxv_hex_pack(ZXV_HEX_BASE_BOTH, ZXV_HEX_POL_FORBIDDEN, ZXV_HEX_PHASE_EMERGING, &ft));
    OK(zxv_hexlog_append(root, log, ft, 1, 7, subj, &seq));
    OK(zxv_hexlog_append(root, log, ft, 2, 7, subj, &seq));
    OK(zxv_hexlog_count(log, &cnt));
    IS(cnt, 2);
    OK(zxv_hexlog_head(log, head));
    OK(zxv_hexlog_verify(log));
    IS(zxv_hexlog_append(ro, log, ft, 1, 7, subj, &seq), ZXV_E_CAP);

    /* randomness through the port */
    uint8_t r1[64], r2[64];
    OK(zxv_random(r1, sizeof r1));
    OK(zxv_random(r2, sizeof r2));
    CHECK(memcmp(r1, r2, sizeof r1) != 0, "two random reads differ");

    printf("heap per handle on this host (64-bit; 32-bit cores are smaller): ctx %zu, cap %zu, "
           "rmag %zu, phase %zu, hexlog(16) %zu bytes\n",
           b_ctx, b_cap, b_rmag, b_phase, b_log);

    /* fail closed: halting the context stops every handle made from it */
    OK(zxv_ctx_halt(ctx, "test halt"));
    IS(zxv_rmag_consume(root, rm, bud, 1, 100), ZXV_E_POISONED);
    IS(zxv_phase_advance(root, ph, 1, NULL, NULL), ZXV_E_POISONED);
    IS(zxv_hexlog_append(root, log, ft, 1, 7, subj, &seq), ZXV_E_POISONED);

    zxv_hexlog_destroy(log);
    zxv_phase_destroy(ph);
    zxv_rmag_destroy(rm);
    zxv_cap_destroy(ro);
    zxv_cap_destroy(root);
    zxv_ctx_destroy(ctx);
    IS(live(), b0); /* everything returned through the port allocator */
}

static void test_ledger_book(void)
{
    /* libzxv's own ledger: the first account allocates a kernel book of
     * sizeof(triple_ledger_t). Under an MCU-sized limit it is refused. */
    zxv_ctx *ctx;
    zxv_cap *root;
    uint32_t a, b;
    uint64_t id;
    size_t b0 = live();
    OK(zxv_ctx_create(&ctx));
    OK(zxv_cap_create(ctx, 1, NULL, ZXV_CAP_ALL, &root));
    OK(zxv_ccy_enable_with_minor_cap(root, "EUR", 2));
    zxp_set_heap_limit(live() + 256u * 1024u);
    IS(zxv_ledger_open_account_cap(root, "alice", "EUR", ZXV_ACCT_ISSUER, &a), ZXV_E_NOMEM);
    zxp_heap_stats_t s;
    zxp_heap_stats(&s);
    CHECK(s.failures > 0, "the refusal is counted");
    zxp_set_heap_limit(0);
    size_t before = live();
    OK(zxv_ledger_open_account_cap(root, "alice", "EUR", ZXV_ACCT_ISSUER, &a));
    size_t book = live() - before;
    printf("libzxv ledger: the first account allocated %zu bytes (one kernel book)\n", book);
    CHECK(book > 16u * 1024u * 1024u, "a book is tens of MB");
    OK(zxv_ledger_open_account_cap(root, "bob", "EUR", ZXV_ACCT_HOLDER, &b));
    OK(zxv_ledger_post_cap(root, a, b, 12345, "EUR", "ref-1", &id));
    int64_t eq;
    OK(zxv_ledger_balance_cap(root, b, NULL, NULL, &eq));
    IS(eq, 12345);
    OK(zxv_ledger_check_cap(root));
    zxv_cap_destroy(root);
    zxv_ctx_destroy(ctx);
    IS(live(), b0);
}

int main(void)
{
    test_closed();
    IS(zxp_set_entropy(zxp_posix_entropy, NULL), ZXP_OK);
    IS(zxp_init(), ZXP_OK);
    test_features();
    test_pure();
    test_handles();
    test_ledger_book();
    return finish("test_subset");
}
