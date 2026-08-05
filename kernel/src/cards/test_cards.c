/* test_cards.c — activation cards as Chiglet modules.
 *
 * The point of these tests is the PROGRESSION RULE: stacking duplicates
 * must be worthless and breadth must pay, and that must hold because of
 * the ISF geometry rather than a hand-written rule.
 *
 *   gcc -std=c11 -Wall -Wextra -DTEST_HOST -Isrc/cards -Isrc/chiglet -Isrc/surplus \
 *       src/cards/test_cards.c src/cards/cards.c src/chiglet/chiglet.c \
 *       src/surplus/surplus.c -o /tmp/test_cards -lm && /tmp/test_cards
 */
#include <stdio.h>
#include <string.h>
#include "cards.h"

static int failures = 0;
#define CHECK(c,m) do{ if(!(c)){printf("[FAIL] %s\n",m);failures++;} \
    else printf("[PASS] %s\n",m);}while(0)
#define D(x) ((double)(x) / (double)SR_ONE)

static card_t mk(uint32_t index, uint8_t disc, uint8_t set,
                 uint16_t gem, uint8_t root, const char *name) {
    card_t c; memset(&c, 0, sizeof(c));
    c.index = index; c.discipline = disc; c.set = set;
    c.gematria = gem; c.root = root;
    for (int i = 0; name[i] && i < CARD_NAME_LEN-1; i++) c.name[i] = name[i];
    return c;
}

int main(void) {
    printf("=== Glyph & Grid activation cards as Chiglet modules ===\n");

    /* ---- a card's direction is deterministic and device-independent ---- */
    {
        card_t a = mk(24525, 0, 1, 54, 9, "Light Portal Creation");
        surplus_real_t v1[CHG_DIM], v2[CHG_DIM];
        card_evidence(&a, v1);
        card_evidence(&a, v2);
        CHECK(memcmp(v1, v2, sizeof(v1)) == 0,
              "the same card always yields the same evidence direction");
        surplus_real_t mag = SR_ZERO;
        for (uint32_t d = 0; d < CHG_DIM; d++) mag = SR_ADD(mag, SR_MUL(v1[d], v1[d]));
        CHECK(mag > SR_ZERO, "a card carries a non-zero direction");
    }

    /* ---- equip / unequip / no duplicate physical card ---- */
    {
        loadout_t l; loadout_init(&l);
        card_t a = mk(100, 0, 1, 54, 9, "Acousto A");
        CHECK(loadout_equip(&l, &a) == 0, "first card takes slot 0");
        CHECK(l.count == 1, "count updates");
        CHECK(loadout_has(&l, 100), "loadout reports the card");
        CHECK(loadout_equip(&l, &a) == -1,
              "the SAME physical card cannot fill two slots");
        CHECK(loadout_unequip(&l, 0), "card unslots");
        CHECK(l.count == 0 && !loadout_has(&l, 100), "loadout is empty again");
        CHECK(!loadout_unequip(&l, 0), "unslotting an empty slot fails");
        /* reconfigurable at will */
        card_t b = mk(200, 5, 1, 77, 4, "Botano B");
        CHECK(loadout_equip(&l, &b) == 0, "a different card reuses the slot");
    }

    /* ================= THE PROGRESSION RULE =================
     * Stacking near-identical cards must NOT raise the loadout's power. */
    {
        loadout_t dup; loadout_init(&dup);
        /* eight cards, all from ONE discipline with the same gematria/root:
         * the collector who grinds a single school */
        for (uint32_t i = 0; i < 8; i++) {
            card_t c = mk(1000 + i, 3, 1, 42, 6, "same school");
            loadout_equip(&dup, &c);
        }
        printf("       8 cards, one discipline -> R=%.3f distinct=%u\n",
               D(dup.R), dup.distinct);
        CHECK(dup.count == 8, "all eight slotted");
        CHECK(dup.distinct == 1 && D(dup.R) < 1.05,
              "EIGHT CARDS OF ONE SCHOOL = ONE direction (grinding is worthless)");

        loadout_t broad; loadout_init(&broad);
        /* eight cards from eight different disciplines: the collector who
         * went wide */
        for (uint32_t i = 0; i < 8; i++) {
            card_t c = mk(2000 + i, (uint8_t)i, 1, (uint16_t)(30 + i*7),
                          (uint8_t)(1 + i), "different school");
            loadout_equip(&broad, &c);
        }
        printf("       8 cards, eight disciplines -> R=%.3f distinct=%u\n",
               D(broad.R), broad.distinct);
        CHECK(broad.distinct > 4, "breadth yields many distinct directions");
        CHECK(D(broad.R) > D(dup.R) + 1.0,
              "BREADTH BEATS DEPTH — and by the geometry, not by a rule");
    }

    /* ---- the payoff: a broad loadout can DECIDE where a narrow one abstains ---- */
    {
        chg_model_t m; memset(&m, 0, sizeof(m));
        m.K = 8; m.D = CHG_DIM; m.L = 2; m.epoch = 1; m.loaded = true;
        for (uint32_t d = 0; d < CHG_DIM; d++) m.proto[0][d] = SR_ZERO;
        m.proto[0][0] = SR_ONE;
        m.proto[1][1] = SR_ONE;
        m.R_min = SR_FROM_FLOAT(2.5);       /* demand real independence */
        m.margin_min = SR_FROM_FLOAT(0.001);

        /* narrow loadout */
        loadout_t narrow; loadout_init(&narrow);
        for (uint32_t i = 0; i < 6; i++) {
            card_t c = mk(3000 + i, 0, 1, 54, 9, "one school");
            loadout_equip(&narrow, &c);
        }
        chiglet_t cn; chg_init(&cn, 0); chg_load_model(&cn, &m);
        loadout_apply(&narrow, &cn);
        chg_result_t rn; loadout_infer(&narrow, &cn, &rn);
        printf("       narrow loadout -> %s (%s) R=%.3f\n",
               chg_state_name(rn.state), chg_reason_name(rn.reason), D(rn.R));
        CHECK(rn.state == CHG_UNCERTAIN && rn.reason == CHG_REASON_REDUNDANT,
              "a narrow loadout leaves the companion UNCERTAIN");

        /* broad loadout */
        loadout_t wide; loadout_init(&wide);
        for (uint32_t i = 0; i < 6; i++) {
            card_t c = mk(4000 + i, (uint8_t)(i * 3), 2, (uint16_t)(20 + i*11),
                          (uint8_t)(2 + i), "many schools");
            loadout_equip(&wide, &c);
        }
        chiglet_t cw; chg_init(&cw, 0); chg_load_model(&cw, &m);
        loadout_apply(&wide, &cw);
        chg_result_t rw; loadout_infer(&wide, &cw, &rw);
        printf("       broad loadout  -> %s R=%.3f distinct=%u S=%.3f\n",
               chg_state_name(rw.state), D(rw.R), rw.k_distinct, D(rw.S));
        CHECK(rw.state == CHG_DECIDED,
              "a BROAD loadout upgrades the companion into deciding");
        CHECK(D(rw.R) > D(rn.R), "broad loadout holds strictly more independence");
    }

    /* ---- capability grants are the union, and gate the companion ---- */
    {
        loadout_t l; loadout_init(&l);
        card_t low = mk(5000, 1, 1, 10, 2, "low root");     /* infer only */
        loadout_equip(&l, &low);
        CHECK((l.grants & CARD_GRANT_INFER) != 0, "every card grants INFER");
        CHECK((l.grants & CARD_GRANT_ADVISE) == 0,
              "a low-root card does NOT grant ADVISE");

        card_t high = mk(5001, 2, 3, 88, 7, "high root");   /* adds advise+recall */
        loadout_equip(&l, &high);
        CHECK((l.grants & CARD_GRANT_ADVISE) != 0,
              "slotting a high-root card grants ADVISE (an upgrade)");
        CHECK((l.grants & CARD_GRANT_RECALL) != 0, "a set-2+ card grants RECALL");

        /* and removing it takes the capability away again */
        loadout_unequip(&l, 1);
        CHECK((l.grants & CARD_GRANT_ADVISE) == 0,
              "unslotting REVOKES the capability (reconfigurable at will)");
    }

    /* ---- an empty loadout leaves the companion unable to act ---- */
    {
        loadout_t l; loadout_init(&l);
        chiglet_t c; chg_init(&c, CHG_CAP_INFER);
        CHECK(loadout_apply(&l, &c) == 0, "empty loadout supplies no experts");
        CHECK(c.caps == 0, "and grants NO capabilities");
        CHECK(D(l.R) == 0.0 && l.distinct == 0, "empty loadout has zero power");
    }

    /* ---- slots are bounded ---- */
    {
        loadout_t l; loadout_init(&l);
        for (uint32_t i = 0; i < CARD_SLOTS; i++) {
            card_t c = mk(6000 + i, (uint8_t)i, 1, (uint16_t)i, 3, "x");
            CHECK(loadout_equip(&l, &c) >= 0, i == 0 ? "slots fill" : "slots fill");
        }
        card_t over = mk(9999, 9, 1, 1, 1, "overflow");
        CHECK(loadout_equip(&l, &over) == -1, "slotting past capacity is refused");
    }

    /* ---- discipline names resolve ---- */
    CHECK(card_discipline_name(0)[0] == 'A', "discipline 0 names Acoustomancy");
    CHECK(card_discipline_name(200)[0] == 'u', "out-of-range discipline is 'unknown'");

    printf("\n%s: %d failure(s)\n", failures?"*** FAILED ***":"ALL PASS", failures);
    return failures?1:0;
}
