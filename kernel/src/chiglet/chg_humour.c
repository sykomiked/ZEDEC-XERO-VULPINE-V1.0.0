/* chg_humour.c — the Chiglet's sense of humour (see chg_humour.h).
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 *
 * Freestanding: no libc, no float, no RNG. The pick is a deterministic
 * function of (outcome, tick, phase): the same outcome at the same tick and
 * phase yields the same line, reproducible like everything else.
 */
#include "chg_humour.h"

/* --gc-sections KEEP — see mirror_ui.h: no boot caller yet, retain the API. */
#if defined(__GNUC__) && (__GNUC__ >= 11)
#  define CH_KEEP __attribute__((used, retain))
#else
#  define CH_KEEP __attribute__((used))
#endif

/* a deterministic index from the low bits of tick ^ phase */
static uint32_t pick(uint64_t tick, uint32_t phase, uint32_t n){
    uint64_t x = tick ^ ((uint64_t)phase << 32);
    x ^= x >> 33; x *= 0xFF51AFD7ED558CCDull;
    x ^= x >> 33; x *= 0xC4CEB9FE1A85EC53ull;
    x ^= x >> 33;
    return (uint32_t)(x % n);
}

CH_KEEP const char *chg_quip(const chg_result_t *r, uint64_t tick, uint32_t phase){
    static const char *const decided[4] = {
        "Decided. The evidence and I reached an understanding.",
        "Verdict in: confident, which is polite for I won.",
        "Settled. The margins were clear; so was my confidence.",
        "Decision made. I consulted the experts, then chose anyway.",
    };
    static const char *const uncertain[4] = {
        "Uncertain, and honestly? Refreshing.",
        "I could not decide. Neither could the experts. A team.",
        "My confidence is currently on a coffee break.",
        "Indecision is also a decision, just a quieter one.",
    };
    static const char *const none[2] = {
        "No model loaded. I am all opinions and no evidence.",
        "Standing by. My brain is unpacking itself.",
    };
    static const char *const noev[2] = {
        "No evidence. Even I cannot argue with nothing.",
        "You gave me nothing to work with. Impressive commitment.",
    };
    static const char *const red[2] = {
        "The experts all agree, which means they are not independent.",
        "Everyone voted the same way. Suspiciously cooperative.",
    };
    static const char *const margin[2] = {
        "The top two are too close to call. I am calling it a draw.",
        "Two strong candidates, one weak stomach for guessing.",
    };
    static const char *const neutral[1] = {
        "Processing complete.",
    };

    if (!r) return neutral[0];
    switch (r->state){
    case CHG_DECIDED:
        if (r->reason == CHG_REASON_LOW_MARGIN)
            return margin[pick(tick, phase, 2u)];
        return decided[pick(tick, phase, 4u)];
    case CHG_UNCERTAIN:
        switch (r->reason){
        case CHG_REASON_NO_MODEL:    return none[pick(tick, phase, 2u)];
        case CHG_REASON_NO_EVIDENCE: return noev[pick(tick, phase, 2u)];
        case CHG_REASON_REDUNDANT:   return red[pick(tick, phase, 2u)];
        default:                     return uncertain[pick(tick, phase, 4u)];
        }
    default:
        /* UNAVAILABLE and anything else: the neutral line, always. */
        return neutral[0];
    }
}

CH_KEEP const char *chg_quip_state(chg_state_t s, uint64_t tick){
    static const char *const idle[3] = {
        "Idle. Thinking about things, mostly the things.",
        "On standby. My neurons are doing stretches.",
        "Awaiting input. I have opinions ready to go.",
    };
    static const char *const busy[2] = {
        "Working. Please do not explain my job to me.",
        "Computing. The answer is being negotiated.",
    };
    if (s == CHG_DECIDED)   return busy[pick(tick, 0u, 2u)];
    if (s == CHG_UNCERTAIN) return idle[pick(tick, 1u, 3u)];
    return "Booting my sense of humour. It should be up shortly.";
}