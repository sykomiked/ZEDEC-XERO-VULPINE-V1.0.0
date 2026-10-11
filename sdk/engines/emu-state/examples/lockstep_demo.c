/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* lockstep_demo.c — two peers in one process. Peer A predicts peer B's
 * inputs and gets them three ticks late; peer B runs strict lockstep.
 * Both print the same final hash. */
#include <stdio.h>
#include <string.h>

#include "../test/game.h"
#include "zxv_emu.h"

static void input_for(uint32_t p, uint64_t t, uint8_t out[GAME_INPUT]) {
    out[0] = (uint8_t) (((t + p) % 3u) == 0 ? 0xFF : 1);
    out[1] = (uint8_t) ((t % 5u) == 0 ? 1 : 0);
}

static void hex32(const uint8_t h[32], char out[65]) {
    for (int i = 0; i < 32; i++) snprintf(out + 2 * i, 3, "%02x", h[i]);
}

#define TRY(x)                                                                       \
    do {                                                                             \
        int32_t rc_ = (x);                                                           \
        if (rc_ != ZXV_OK) {                                                         \
            char e_[256];                                                            \
            size_t n_;                                                               \
            if (zxv_emu_last_error(e_, sizeof e_, &n_) != ZXV_OK) e_[0] = '\0';      \
            fprintf(stderr, "%s: %s (%s)\n", #x, zxv_status_name(rc_), e_);          \
            return 1;                                                                \
        }                                                                            \
    } while (0)

int main(void) {
    uint32_t P = 2;
    const uint64_t TICKS = 30, LAG = 3;
    zxv_emu *a = NULL, *b = NULL;
    TRY(zxv_emu_create(P, game_rat_slots(P), P, GAME_INPUT, 16, ZXV_EMU_F_PREDICT, &a));
    TRY(zxv_emu_create(P, game_rat_slots(P), P, GAME_INPUT, 16, 0, &b));
    TRY(game_setup(a, P));
    TRY(game_setup(b, P));
    TRY(zxv_emu_start(a));
    TRY(zxv_emu_start(b));
    unsigned rollbacks = 0;
    for (uint64_t t = 1; t <= TICKS + LAG; t++) {
        uint8_t x[GAME_INPUT];
        if (t <= TICKS) {
            input_for(0, t, x); /* A's own input, sent to B at once */
            TRY(zxv_emu_input_submit(a, 0, t, x, sizeof x));
            TRY(zxv_emu_input_submit(b, 0, t, x, sizeof x));
            input_for(1, t, x); /* B's own input */
            TRY(zxv_emu_input_submit(b, 1, t, x, sizeof x));
        }
        if (t > LAG) { /* B's input reaches A LAG ticks late */
            input_for(1, t - LAG, x);
            TRY(zxv_emu_input_submit(a, 1, t - LAG, x, sizeof x));
        }
        uint64_t pt;
        int32_t pending;
        TRY(zxv_emu_rollback_pending(a, &pt, &pending));
        if (pending) rollbacks++;
        TRY(zxv_emu_rollback_replay(a, game_step, &P));
        if (t <= TICKS) {
            TRY(zxv_emu_advance(a, game_step, &P));
            TRY(zxv_emu_advance(b, game_step, &P));
        }
    }
    uint8_t ha[32], hb[32];
    int32_t lp;
    char sa[65], sb[65];
    TRY(zxv_emu_hash(a, TICKS, ha, NULL));
    TRY(zxv_emu_hash(b, TICKS, hb, NULL));
    TRY(zxv_emu_compare(a, TICKS, hb, &lp));
    hex32(ha, sa);
    hex32(hb, sb);
    int64_t n, d;
    TRY(zxv_emu_rat_get(a, 0, &n, &d));
    printf("tick %llu, %u rollbacks on peer A\n", (unsigned long long) TICKS, rollbacks);
    printf("player 0 position %lld/%lld (exact)\n", (long long) n, (long long) d);
    printf("peer A %s\npeer B %s\n%s\n", sa, sb, lp == ZXV_LP_TRUE ? "in sync" : "DESYNC");
    zxv_emu_destroy(a);
    zxv_emu_destroy(b);
    return lp == ZXV_LP_TRUE ? 0 : 1;
}
