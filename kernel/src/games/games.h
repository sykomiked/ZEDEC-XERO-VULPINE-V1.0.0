/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* games.h — play ALONGSIDE your Chiglet.
 *
 * "The world is a pure function of the tick, so two captains on two ships
 *  see the same sea." — the P2P-multiplayer guarantee, stated as a fact.
 *
 * WHAT THIS IS
 * -----------
 * A REAL deterministic game engine core. A game is a phase-tick state
 * machine: a bounded entity array plus a PURE integer transition. The full
 * state is a deterministic function of (seed, input-sequence, tick) — so the
 * SAME seed + SAME inputs produce a bit-identical game_state_hash on every
 * node. That equality IS lockstep P2P multiplayer: no state is shipped over
 * the wire, only the tiny input stream, and every peer recomputes the world.
 *
 * The demo is a grid-forager: a captain (the player) and a Chiglet companion
 * roam a grid gathering food. Any "randomness" (food spawns) comes from a
 * deterministic LCG seeded at init — never a wall clock, never true random.
 *
 * THE COMPANION IS NOT RANDOM. Each tick the companion ACTS through the real
 * AI runtime (src/chiglet): it builds evidence vectors from the game features
 * and calls chg_infer. The chosen move is a deterministic function of those
 * features — same features, same move, every run. When no Chiglet is bound,
 * or its model is unloaded, the companion returns a TYPED "not available" and
 * holds position; it never fabricates a decision.
 *
 * OPS BOUNDARIES (not implemented here, and honestly so):
 *   - Rendering / graphics output: the engine produces state, a display draws
 *     it. There is no framebuffer in this module.
 *   - The live network transport: peers exchange the input stream and CIDs
 *     over ZXV's P2P layer; this module gives them the deterministic core and
 *     the content-addressed ruleset, not the socket.
 *
 * Freestanding: integer only, fixed-size arrays, no libc, no malloc, no float
 * on target (the Chiglet's surplus_real_t is Q32.32 there), no clock.
 */
#ifndef ZXV_GAMES_H
#define ZXV_GAMES_H

#include <stdint.h>
#include <stdbool.h>
#include "chiglet.h"   /* the real companion runtime: chg_infer / chg_result_t */
#include "ipfs.h"      /* content-address a game definition by its own hash    */

/* ---- bounds (everything is statically sized) ---- */
#define GAME_MAX_ENTITIES  64u
#define GAME_GRID_W        16u
#define GAME_GRID_H        16u
#define GAME_INIT_FOOD      4u   /* food present at init                       */
#define GAME_MAX_FOOD      16u   /* never exceed this many food on the board   */
#define GAME_SPAWN_PERIOD   4u   /* spawn a food every N ticks (tick, not time)*/
#define GAME_NAME_LEN      32u

/* ---- entity kinds ---- */
typedef enum {
    GAME_ENT_EMPTY     = 0,
    GAME_ENT_PLAYER    = 1,   /* the human captain                            */
    GAME_ENT_COMPANION = 2,   /* the Chiglet, sailing the same sea            */
    GAME_ENT_FOOD      = 3
} game_ent_kind_t;

/* ---- moves: a 5-way step. Player input and companion action share it. ---- */
typedef enum {
    GAME_MOVE_STAY  = 0,
    GAME_MOVE_UP    = 1,      /* toward smaller y                             */
    GAME_MOVE_DOWN  = 2,      /* toward larger y                              */
    GAME_MOVE_LEFT  = 3,      /* toward smaller x                             */
    GAME_MOVE_RIGHT = 4
} game_move_t;

/* A single entity. Fixed-width fields only, so the state hash is portable. */
typedef struct {
    uint8_t  kind;            /* game_ent_kind_t                              */
    uint8_t  alive;           /* 0 = free slot                               */
    int16_t  x;
    int16_t  y;
} game_entity_t;

/* The whole world. A pure function of (seed, inputs, tick) lives in here. */
typedef struct {
    uint64_t      seed;                       /* the shared secret of the sea */
    uint64_t      rng;                        /* LCG state (deterministic)    */
    uint32_t      tick;                       /* phase-tick counter, not time */
    int32_t       score;                      /* forage points (both captains)*/
    uint16_t      width, height;
    uint32_t      count;                      /* entity slots in use          */
    uint32_t      player;                     /* index of the player entity   */
    uint32_t      companion;                  /* index of the Chiglet entity  */
    game_entity_t ents[GAME_MAX_ENTITIES];

    /* companion wiring (an ops binding, not fabricated state) */
    chiglet_t    *chg;                        /* NULL until bound             */
    uint8_t       companion_available;        /* 1 iff last decision came from
                                                 a bound+loaded runtime       */
    uint8_t       last_companion_action;      /* game_move_t the Chiglet chose*/
} game_t;

/* ---- a shareable, content-addressed ruleset ----
 * Peers fetch the IDENTICAL ruleset by CID; a changed param changes the CID. */
typedef struct {
    char     name[GAME_NAME_LEN];
    uint32_t version;
    uint16_t width, height;
    uint16_t init_food;
    uint16_t max_food;
    uint16_t spawn_period;
    int32_t  food_reward;
    uint32_t rule_flags;
} game_def_t;

/* ================= public API ================= */

/* Seed the world deterministically. Same seed => same starting sea. */
void game_init(game_t *g, uint64_t seed);

/* Bind the real AI companion. Pass NULL to unbind (companion then holds and
 * reports "not available"). The Chiglet's model must be loaded by the caller
 * (see game_companion_model to build the demo's prototypes). */
void game_bind_companion(game_t *g, chiglet_t *chg);

/* Advance EXACTLY one phase tick. `input` low 3 bits = the player's move
 * (game_move_t). The transition is a pure integer function of (g, input):
 * no clock, no true randomness. The companion acts this tick via chg_infer. */
void game_step(game_t *g, uint32_t input);

/* 32-bit digest of the ENTIRE simulation state, for replay / lockstep
 * comparison. Two nodes agree iff their hashes agree. */
uint32_t game_state_hash(const game_t *g);

/* Compute (without applying) the companion's move for the current state, the
 * same way game_step does. Deterministic function of the game features via
 * chg_infer. Returns GAME_MOVE_STAY when no runtime is bound/loaded and sets
 * *available = 0 (a typed not-available, never a fabricated move). */
game_move_t game_companion_decide(const game_t *g, uint8_t *available);

/* Fill a chg_model_t with the demo companion's four directional prototypes
 * (UP/DOWN/LEFT/RIGHT). The caller loads it into a chiglet_t and binds that.
 * This keeps the prototype geometry and the evidence geometry in one place. */
void game_companion_model(chg_model_t *m);

/* ---- P2P: content-address a ruleset ----
 * out_cid = sha256(canonical(def)) via the kernel IPFS layer. Two identical
 * defs share a CID; any changed param yields a different CID. Returns 0 on
 * success, -1 on a NULL argument. */
int32_t game_def_publish(const game_def_t *def, uint8_t out_cid[IPFS_CID_LEN]);

/* The demo's canonical ruleset (matches the compiled-in constants). */
void game_def_default(game_def_t *def);

#endif /* ZXV_GAMES_H */
