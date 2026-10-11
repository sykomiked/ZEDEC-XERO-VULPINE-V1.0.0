/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* game.h — a tiny deterministic game shared by the tests, the fuzzer and
 * the demo. State, for P players:
 *
 *   rat slot p      (p < P)  player p's position, moved by 1/3 per tick
 *   rat slot P               a shared "tide" that grows by (tick mod 7)/5
 *   hex slot p      (p < P)  player p's claim: set TRUE/negentropic when
 *                            the player asserts it, FORBIDDEN when the
 *                            player cheats, and its phase advances one step
 *                            on the Gray cycle every tick
 *
 * Input (2 bytes): byte 0 = direction as int8 (-1, 0, +1; others clamp),
 * byte 1 = bit 0 assert, bit 1 cheat. Each player costs 1 budget unit. */
#ifndef ZXV_EMU_TEST_GAME_H
#define ZXV_EMU_TEST_GAME_H

#include "zxv_emu.h"

#define GAME_INPUT 2u

static inline uint32_t game_rat_slots(uint32_t players) { return players + 1u; }

static inline int32_t game_setup(zxv_emu *sim, uint32_t players) {
    int32_t rc;
    for (uint32_t p = 0; p < players; p++) {
        uint8_t tag;
        if ((rc = zxv_emu_rat_set(sim, p, (int64_t) p, 2)) != ZXV_OK) return rc;
        if ((rc = zxv_hex_pack(ZXV_HEX_BASE_NEITHER, ZXV_HEX_POL_NEUTRAL, ZXV_HEX_PHASE_POTENTIAL,
                               &tag)) != ZXV_OK)
            return rc;
        if ((rc = zxv_emu_hex_set(sim, p, tag)) != ZXV_OK) return rc;
    }
    return zxv_emu_rat_set(sim, players, 0, 1);
}

static inline int32_t game_step(zxv_emu *sim, uint64_t tick, void *user) {
    uint32_t players = *(const uint32_t *) user;
    int32_t rc;
    for (uint32_t p = 0; p < players; p++) {
        uint8_t in[GAME_INPUT], tag, phase, next;
        size_t n = 0;
        if ((rc = zxv_emu_input_get(sim, p, in, sizeof in, &n, NULL)) != ZXV_OK) return rc;
        if ((rc = zxv_emu_charge(sim, 1, 1)) != ZXV_OK) return rc;
        int64_t dir = in[0] == 0 ? 0 : (in[0] < 128 ? 1 : -1);
        if (dir != 0 && (rc = zxv_emu_rat_add(sim, p, dir, 3)) != ZXV_OK) return rc;

        if ((rc = zxv_emu_hex_get(sim, p, &tag)) != ZXV_OK) return rc;
        if (in[1] & 2u) {
            if ((rc = zxv_hex_forbid(tag, &tag)) != ZXV_OK) return rc;
        } else if (in[1] & 1u) {
            uint8_t pol;
            if ((rc = zxv_hex_unpack(tag, NULL, &pol, NULL)) != ZXV_OK) return rc;
            if (pol != ZXV_HEX_POL_FORBIDDEN &&
                (rc = zxv_hex_set(tag, ZXV_HEX_BASE_TRUE, ZXV_HEX_POL_NEGENTROPIC, &tag)) != ZXV_OK)
                return rc;
        }
        if ((rc = zxv_hex_unpack(tag, NULL, NULL, &phase)) != ZXV_OK) return rc;
        if ((rc = zxv_hex_phase_next(phase, &next)) != ZXV_OK) return rc;
        if ((rc = zxv_hex_step(tag, next, &tag)) != ZXV_OK) return rc;
        if ((rc = zxv_emu_hex_set(sim, p, tag)) != ZXV_OK) return rc;
    }
    return zxv_emu_rat_add(sim, players, (int64_t) (tick % 7u), 5);
}

#endif /* ZXV_EMU_TEST_GAME_H */
