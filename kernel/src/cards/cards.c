/* cards.c — activation cards as Chiglet modules. See cards.h. */
#include "cards.h"
#include "zca.h"

const char *card_discipline_name(uint8_t d) {
    return zca_discipline_name(d);
}

/* A card's effect is not computed by this file. The card carries a ZCA
 * program; running that program IS the effect. Both paths into it —
 * deriving from the printed attributes, or assembling a listing read off
 * the card by OCR — produce identical bytes, so it does not matter which
 * one a given card came in through. */
static void card_run(const card_t *c, card_effect_t *fx) {
    zca_attrs_t at;
    at.index      = c->index;
    at.discipline = c->discipline;
    at.set        = c->set;
    at.gematria   = c->gematria;
    at.root       = c->root;
    at.tarot      = c->tarot;
    zca_program_t p;
    if (zca_derive(&at, &p) != ZCA_OK) {
        for (uint32_t d = 0; d < CHG_DIM; d++) fx->evidence[d] = SR_ZERO;
        fx->grants = 0;
        return;
    }
    zca_exec(&p, fx);
}

void card_evidence(const card_t *c, surplus_real_t out[CHG_DIM]) {
    if (!c || !out) return;
    card_effect_t fx;
    card_run(c, &fx);
    for (uint32_t d = 0; d < CHG_DIM; d++) out[d] = fx.evidence[d];
}

uint32_t card_grants(const card_t *c) {
    if (!c) return 0;
    card_effect_t fx;
    card_run(c, &fx);
    return fx.grants;
}

void loadout_init(loadout_t *l) {
    if (!l) return;
    for (uint32_t i = 0; i < CARD_SLOTS; i++) {
        l->filled[i] = false;
        for (uint32_t d = 0; d < CHG_DIM; d++) l->evidence[i][d] = SR_ZERO;
    }
    l->count = 0;
    l->R = SR_ZERO;
    l->distinct = 0;
    l->grants = 0;
}

bool loadout_has(const loadout_t *l, uint32_t card_index) {
    if (!l) return false;
    for (uint32_t i = 0; i < CARD_SLOTS; i++)
        if (l->filled[i] && l->card[i].index == card_index) return true;
    return false;
}

void loadout_recompute(loadout_t *l) {
    if (!l) return;
    /* pack the filled slots' evidence contiguously for the Chiglet */
    surplus_real_t ev[CARD_SLOTS][CHG_DIM];
    uint32_t n = 0;
    uint32_t grants = 0;
    for (uint32_t i = 0; i < CARD_SLOTS; i++) {
        if (!l->filled[i]) continue;
        card_evidence(&l->card[i], l->evidence[i]);
        for (uint32_t d = 0; d < CHG_DIM; d++) ev[n][d] = l->evidence[i][d];
        grants |= card_grants(&l->card[i]);
        n++;
    }
    l->grants = grants;
    if (n == 0) { l->R = SR_ZERO; l->distinct = 0; return; }

    /* This is the same duplication-robust computation the decision gate
     * uses: near-identical directions collapse, so stacking copies of one
     * card cannot inflate the loadout's power. */
    uint32_t distinct = 0;
    l->R = chg_effective_experts((const surplus_real_t (*)[CHG_DIM])ev,
                                 n, CHG_DIM, &distinct);
    l->distinct = distinct;
}

int32_t loadout_equip(loadout_t *l, const card_t *c) {
    if (!l || !c) return -1;
    if (loadout_has(l, c->index)) return -1;   /* one physical card, one slot */
    for (uint32_t i = 0; i < CARD_SLOTS; i++) {
        if (l->filled[i]) continue;
        l->card[i] = *c;
        l->filled[i] = true;
        l->count++;
        loadout_recompute(l);
        return (int32_t)i;
    }
    return -1;                                  /* full */
}

bool loadout_unequip(loadout_t *l, uint32_t slot) {
    if (!l || slot >= CARD_SLOTS || !l->filled[slot]) return false;
    l->filled[slot] = false;
    if (l->count) l->count--;
    loadout_recompute(l);
    return true;
}

uint32_t loadout_apply(const loadout_t *l, chiglet_t *c) {
    if (!l || !c) return 0;
    /* the companion may only do what its cards grant */
    uint32_t caps = 0;
    if (l->grants & CARD_GRANT_INFER)  caps |= CHG_CAP_INFER;
    if (l->grants & CARD_GRANT_ADVISE) caps |= CHG_CAP_ADVISE;
    c->caps = caps;
    uint32_t n = 0;
    for (uint32_t i = 0; i < CARD_SLOTS; i++) if (l->filled[i]) n++;
    return n;
}

chg_status_t loadout_infer(const loadout_t *l, chiglet_t *c, chg_result_t *out) {
    if (!l || !c || !out) return CHG_ERR_ARG;
    surplus_real_t ev[CARD_SLOTS][CHG_DIM];
    uint32_t n = 0;
    for (uint32_t i = 0; i < CARD_SLOTS; i++) {
        if (!l->filled[i]) continue;
        for (uint32_t d = 0; d < CHG_DIM; d++) ev[n][d] = l->evidence[i][d];
        n++;
    }
    return chg_infer(c, (const surplus_real_t (*)[CHG_DIM])ev, n, out);
}
