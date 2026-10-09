/* cards.h — Glyph & Grid activation cards as Chiglet modules
 *
 * THE IDEA
 * --------
 * A card is a module you slot into your AI companion. Each one gives the
 * Chiglet an additional evidence source — a way of looking at a problem.
 * Cards are earned by doing particular kinds of work, and can be
 * reconfigured at will: unslot one, slot another, no rebuild, no reboot.
 *
 * WHY THIS FALLS OUT OF THE MATH RATHER THAN BEING BOLTED ON
 * ----------------------------------------------------------
 * The Chiglet is an ISF-weighted mixture of experts, and its decision gate
 * is R — the number of genuinely INDEPENDENT evidence directions it holds
 * (see chiglet.h). A card contributes one direction. Therefore:
 *
 *   * Slotting eight copies of the same card leaves R = 1. The companion
 *     is no more capable than with one. Stacking duplicates is worthless,
 *     and it is worthless for a *mathematical* reason, not a rule someone
 *     wrote down.
 *
 *   * Slotting cards from different disciplines raises R, and the
 *     companion starts being able to DECIDE where before it abstained.
 *
 * So the progression is: collect BROADLY, not deeply. The duplication
 * safeguard in chg_effective_experts() is what enforces it, and it is the
 * same code that stops an adversary faking confidence. One mechanism,
 * two jobs.
 *
 * EVIDENCE DIRECTION
 * ------------------
 * A card's direction is derived deterministically from its own attributes
 * — discipline, gematria, root, tarot — so the same card always means the
 * same thing, on every device, with no lookup table:
 *
 *   * discipline sets the PRIMARY axis (38 disciplines spread across the
 *     evidence dimensions), so two cards from the same discipline point
 *     in nearly the same direction and largely duplicate each other;
 *   * gematria and root perturb the secondary components, so cards within
 *     a discipline are not perfectly identical — a second card from the
 *     same school adds a little, but far less than a card from a new one.
 *
 * That is exactly the desired feel, and it is a consequence of the
 * geometry rather than a tuned constant.
 *
 * Freestanding: integer only, no libc, no allocation.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (ZXV card-module slice)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#ifndef ZXV_CARDS_H
#define ZXV_CARDS_H

#include <stdint.h>
#include <stdbool.h>
#include "../chiglet/chiglet.h"

#define CARD_SLOTS        CHG_MAX_EXPERTS   /* equipped at once */
#define CARD_NAME_LEN     36
#define CARD_ENOCH_LEN    16
#define CARD_MAX_DISC     40

/* A catalog entry — 64 bytes on disk, matching cards.zcx. */
typedef struct {
    uint32_t index;        /* seal_<index>.png, the canonical card id */
    uint8_t  discipline;   /* 0..37 */
    uint8_t  set;          /* 1..3 */
    uint16_t gematria;
    uint8_t  root;         /* digital root 1..9 */
    uint8_t  tarot;
    char     name[CARD_NAME_LEN];
    char     enochian[CARD_ENOCH_LEN];
} card_t;

/* Capabilities a card can grant the companion. A card never grants more
 * than it names, and the loadout's grant is the union of its cards'. */
#define CARD_GRANT_INFER    (1u << 0)
#define CARD_GRANT_ADVISE   (1u << 1)
#define CARD_GRANT_RECALL   (1u << 2)
#define CARD_GRANT_COMPOSE  (1u << 3)

typedef struct {
    card_t   card[CARD_SLOTS];
    bool     filled[CARD_SLOTS];
    uint32_t count;

    /* derived on every change — never stale */
    surplus_real_t evidence[CARD_SLOTS][CHG_DIM];
    surplus_real_t R;            /* independent directions the loadout holds */
    uint32_t       distinct;     /* how many cards actually count */
    uint32_t       grants;       /* union of card capability grants */
} loadout_t;

/* Derive a card's evidence direction. Deterministic and device-independent:
 * the same card yields the same vector everywhere. */
void card_evidence(const card_t *c, surplus_real_t out[CHG_DIM]);

/* Which capabilities a card grants, from its discipline and root. */
uint32_t card_grants(const card_t *c);

void  loadout_init(loadout_t *l);

/* Slot a card. Returns the slot index, or -1 if full or already present
 * (the same physical card cannot occupy two slots). */
int32_t loadout_equip(loadout_t *l, const card_t *c);

/* Unslot by slot index. Cards are reconfigurable at will. */
bool loadout_unequip(loadout_t *l, uint32_t slot);

/* True if this card index is already slotted. */
bool loadout_has(const loadout_t *l, uint32_t card_index);

/* Recompute R, distinct and grants. Called automatically by equip/unequip;
 * exposed so a caller can assert the loadout is self-consistent. */
void loadout_recompute(loadout_t *l);

/* Point a Chiglet at this loadout: hands it the evidence set and the
 * capability union. Returns the number of experts supplied. */
uint32_t loadout_apply(const loadout_t *l, chiglet_t *c);

/* Run the companion using the loadout's cards as its experts. */
chg_status_t loadout_infer(const loadout_t *l, chiglet_t *c, chg_result_t *out);

const char *card_discipline_name(uint8_t discipline);

#endif /* ZXV_CARDS_H */
