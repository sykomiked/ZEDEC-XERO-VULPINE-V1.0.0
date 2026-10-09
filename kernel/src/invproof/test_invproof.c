/* test_invproof.c — inverse witness tests.
 *
 * The headline test is THE UNDO THAT DOES NOT UNDO: an artifact declaring a
 * proven exact inverse whose delta lands somewhere other than the prior state.
 * Under the declared-only model that shipped as "proven". Here it is caught by
 * applying it.
 *
 * Build/run:
 *   gcc -std=c11 -Wall -Wextra -Werror -Isrc/invproof \
 *       src/invproof/test_invproof.c src/invproof/invproof.c \
 *       src/robin_debanks/sha256.c -o /tmp/test_invproof && /tmp/test_invproof
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <string.h>
#include "invproof.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("  [FAIL] %s\n", msg); failures++; } \
    else         { printf("  [PASS] %s\n", msg); } } while (0)

#define N 512
static uint8_t before[N], after[N], scratch[N], wit[4096];

static void state_init(void) {
    for (uint32_t i = 0; i < N; i++) { before[i] = (uint8_t)(i * 7 + 3); after[i] = before[i]; }
}

int main(void) {
    printf("ZXI inverse witness\n");

    /* ---- a real transition, proven by applying it ---- */
    printf("proof by application:\n");
    state_init();
    after[10] = 0xAA; after[11] = 0xBB; after[300] = 0xCC;   /* two edit sites */
    int wl = zxi_build(before, after, N, wit, sizeof(wit));
    CHECK(wl > 0, "witness builds");
    CHECK((uint32_t)wl < 120, "witness is SPARSE (3 changed bytes -> tiny witness)");
    CHECK(zxi_verify(wit, (uint32_t)wl, after, N, scratch, N) == ZXI_OK,
          "witness verifies: applying the undo restores `before`");
    CHECK(memcmp(scratch, before, N) == 0,
          "recovered state is byte-identical to `before`");

    /* an unchanged transition is still a valid (empty) witness */
    state_init();
    wl = zxi_build(before, after, N, wit, sizeof(wit));
    CHECK(wl > 0 && zxi_verify(wit, (uint32_t)wl, after, N, scratch, N) == ZXI_OK,
          "identity transition witnesses cleanly");

    /* ---- THE ATTACK: an undo that does not undo ---- */
    printf("the undo that does not undo:\n");
    state_init();
    after[42] = 0x99;
    wl = zxi_build(before, after, N, wit, sizeof(wit));
    CHECK(wl > 0, "  witness built for a real transition");
    /* tamper with one delta byte: the undo now lands on the WRONG prior state,
     * while still being a well-formed, correctly-bounded witness */
    wit[ZXI_HDR_BYTES + 6] ^= 0x01;
    wl = (int)(uint32_t)wl;
    /* reseal so it is not merely a seal failure — this is the honest hard case */
    {   uint32_t s = 2166136261u;
        for (int i = 0; i < wl; i++) { if (i >= 84 && i < 88) continue;
            s ^= wit[i]; s *= 16777619u; }
        if (!s) s = 1u;
        wit[84]=(uint8_t)s; wit[85]=(uint8_t)(s>>8);
        wit[86]=(uint8_t)(s>>16); wit[87]=(uint8_t)(s>>24); }
    CHECK(zxi_verify(wit, (uint32_t)wl, after, N, scratch, N) == ZXI_ERR_NOT_INVERSE,
          "a WELL-FORMED witness whose undo is wrong is REFUSED");

    /* ---- witness lifted from a different transition ---- */
    printf("substitution:\n");
    state_init();
    after[5] = 0x11;
    wl = zxi_build(before, after, N, wit, sizeof(wit));
    { uint8_t other[N];
      for (uint32_t i = 0; i < N; i++) other[i] = (uint8_t)(i * 3 + 1);
      CHECK(zxi_verify(wit, (uint32_t)wl, other, N, scratch, N) == ZXI_ERR_AFTER_DIGEST,
            "witness from another transition is refused (not about THIS state)"); }

    /* ---- hostile / malformed witnesses ---- */
    printf("hostile input (all must refuse):\n");
    state_init(); after[7] = 0x55;
    wl = zxi_build(before, after, N, wit, sizeof(wit));

    { uint8_t bad[4096]; memcpy(bad, wit, (uint32_t)wl);
      bad[ZXI_HDR_BYTES + 1] = 0xFF;   /* run offset far outside the state */
      bad[ZXI_HDR_BYTES + 2] = 0xFF; bad[ZXI_HDR_BYTES + 3] = 0xFF;
      uint32_t s = 2166136261u;
      for (int i = 0; i < wl; i++) { if (i >= 84 && i < 88) continue; s ^= bad[i]; s *= 16777619u; }
      if (!s) s = 1u;
      bad[84]=(uint8_t)s; bad[85]=(uint8_t)(s>>8);
      bad[86]=(uint8_t)(s>>16); bad[87]=(uint8_t)(s>>24);
      CHECK(zxi_verify(bad, (uint32_t)wl, after, N, scratch, N) == ZXI_ERR_RUN_BOUNDS,
            "run pointing outside the state refused (no OOB write)"); }

    { uint8_t bad[4096]; memcpy(bad, wit, (uint32_t)wl); bad[85] ^= 0xFF;
      CHECK(zxi_verify(bad, (uint32_t)wl, after, N, scratch, N) == ZXI_ERR_SEAL,
            "corrupted seal refused"); }

    CHECK(zxi_verify(wit, (uint32_t)wl - 2, after, N, scratch, N) != ZXI_OK,
          "truncated witness refused");
    CHECK(zxi_verify(wit, (uint32_t)wl, after, N - 1, scratch, N) == ZXI_ERR_STATE_LEN,
          "wrong state length refused");
    CHECK(zxi_verify(wit, (uint32_t)wl, after, N, scratch, N - 1) == ZXI_ERR_TRUNCATED,
          "undersized scratch refused (no overflow)");
    { const char *notw = "I am not a witness at all";
      CHECK(zxi_verify((const uint8_t *)notw, (uint32_t)strlen(notw),
                       after, N, scratch, N) == ZXI_ERR_NOT_WITNESS,
            "non-witness data refused"); }

    /* ---- sparseness is real, not incidental ---- */
    printf("sparseness:\n");
    { state_init();
      for (uint32_t i = 0; i < N; i++) after[i] = (uint8_t)~before[i];  /* all differ */
      int dense = zxi_build(before, after, N, wit, sizeof(wit));
      state_init(); after[0] ^= 0xFF;                                   /* one differs */
      int sparse = zxi_build(before, after, N, wit, sizeof(wit));
      CHECK(dense > 500 && sparse < 100 && sparse < dense / 5,
            "witness size tracks the CHANGE, not the state size"); }

    printf("\n%s invproof: %d failure(s)\n", failures ? "FAILED" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
