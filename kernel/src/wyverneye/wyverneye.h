/* wyverneye.h — WyvernEye: intent forged, then judged before it acts
 *
 * WHAT THIS IS
 * ------------
 * A second composed flagship, stacked on the first. WyvernEye takes a
 * person's intent in any language, forges it into a sigil-card with the
 * Magitech Refinery, and then passes the proposed ACTION through Wyrmgate's
 * six-fold Tri-Space judgment. Compiling it pulls in the whole lower stack —
 * refinery, enochian, sigil, cards/zca, chiglet, surplus, sha256, rmag,
 * lpres, wyrmgate — assembled, not rewritten.
 *
 * THE PROPERTY THAT FALLS OUT OF THE COMPOSITION
 * ----------------------------------------------
 * Forging and acting are separated, and the separation is the point:
 *
 *   * The sigil ALWAYS forges. Anyone may speak their intent; expression is
 *     free and never gated. The forged card is deterministic and shareable.
 *
 *   * The ACTION is judged. A proposal that violates a law (value not
 *     conserved, disproven evidence) is REJECTED (S-). A proposal that is
 *     merely a lone assertion — a sincerely forged sigil with nothing
 *     independent behind it — is DEFERRED (S0), not obeyed. Only intent that
 *     is lawful AND independently corroborated COMMITS (S+).
 *
 * So a single voice, however confident, cannot compel the system; it is held
 * until the world corroborates it. That is an anti-manipulation property, and
 * it comes for free from stacking the Refinery on Wyrmgate — no new logic.
 *
 * Freestanding: integer only, no libc, no allocation, no float.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (ZXV WyvernEye slice)
 * License: SEL-3.3
 */
#ifndef ZXV_WYVERNEYE_H
#define ZXV_WYVERNEYE_H

#include <stdint.h>
#include <stdbool.h>
#include "../refinery/refinery.h"
#include "../wyrmgate/wyrmgate.h"

#define WYV_MAX_CORROB  (CHG_MAX_EXPERTS - 1u)  /* the sigil takes one slot */

/* Everything the judgment needs beyond the intent text itself. */
typedef struct {
    /* value proposal — signed rational deltas that must conserve */
    rmag_rational_t delta[WYRM_MAX_DELTAS];
    uint32_t        n_deltas;

    /* attestation about whether this action should happen */
    lpres_state_t   evidence;

    /* INDEPENDENT corroboration — evidence directions from the world, beyond
     * the sigil the speaker forged. The sigil is always counted as one
     * direction; these are the others. */
    surplus_real_t  corroboration[WYV_MAX_CORROB][CHG_DIM];
    uint32_t        n_corroboration;
    surplus_real_t  r_min;              /* independence floor */

    /* causal + routing context */
    uint64_t ordinal, parent_ordinal;
    bool     parent_committed;
    bool     route_available, phases_healthy;
} wyvern_context_t;

typedef struct {
    ref_card_t    card;       /* the forged sigil (Refinery sub-result) */
    wyrm_result_t verdict;    /* the judgment (Wyrmgate sub-result)      */
    uint32_t      corroborators; /* independent directions behind the intent */
} wyvern_reading_t;

/* Forge `intent` into a sigil and judge whether it may act, in `ctx`.
 * Returns false only on bad arguments or a forge failure; a lawful-but-
 * unsupported intent still returns true with an S0 DEFER verdict. */
bool wyvern_read(const char *intent, uint32_t len, eno_voice_t voice,
                 const wyvern_context_t *ctx, wyvern_reading_t *out);

#endif /* ZXV_WYVERNEYE_H */
