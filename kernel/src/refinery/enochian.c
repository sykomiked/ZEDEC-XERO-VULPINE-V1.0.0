/* enochian.c — the Enochian language core. See enochian.h. */
#include "enochian.h"

/* The alphabet of twenty-one letters, values from the reconstruction
 * grammar §1 (identical to alphabet.json in the corpus). */
static const struct { char c; uint8_t v; const char *name; } ALPHA[ENO_LETTERS] = {
    {'A', 6,"Un"},   {'B', 5,"Pa"},   {'C', 3,"Veh"},  {'D', 4,"Gal"},
    {'E', 9,"Graph"},{'F', 6,"Orth"}, {'G', 1,"Ged"},  {'H', 5,"Na-hath"},
    {'I', 3,"Gon"},  {'L', 8,"Ur"},   {'M', 9,"Tal"},  {'N', 8,"Drun"},
    {'O', 6,"Med"},  {'P', 5,"Mals"}, {'Q',10,"Ger"},  {'R', 3,"Don"},
    {'S', 3,"Fam"},  {'T', 9,"Gisg"}, {'U', 4,"Vau"},  {'X', 1,"Pal"},
    {'Z', 7,"Ceph"}
};

/* Case folding, plus the three allographs that remain UNATTESTED.
 *
 * Y IS NOT FOLDED — the shipped deck scores it as itself, i.e. as zero.
 * Y occurs in 15 of the 52,095 rows of cards/cards.tsv, all of them the
 * lexeme GNAY. All 15 match Y=0 and ZERO match Y->I: card 17 `GNAY` is
 * printed 15 (the kernel used to compute 18), card 45795 `GNAY AUMDUEZ`
 * is printed 58 (the kernel used to compute 61). Scoring the 21 ALPHA
 * letters with no fold at all reproduces 52,095 / 52,095; with Y->I it
 * is 52,080 / 52,095. The module's contract is byte-parity with the
 * physical deck and printed cards cannot be recalled, so the deck is
 * authoritative where it speaks — and here it speaks.
 *
 * J, K and W are KEPT folded and are a deliberate open question, not a
 * verified rule: they occur ZERO times in the corpus, so the deck cannot
 * test them in either direction. Removing them would be a guess dressed
 * as a fix. They stay, and the header no longer calls the table grammar
 * provenance (GRAMMAR.md is cited there but absent from this tree). V is
 * likewise unfolded and scores 0; that IS deck-verified — 552 rows carry
 * V and all 552 match V=0. */
char eno_fold(char c) {
    if (c >= 'a' && c <= 'z') c = (char)(c - 32);
    switch (c) {                       /* unattested in the deck; see above */
    case 'J': return 'I';
    case 'K': return 'C';
    case 'W': return 'U';
    default:  return c;                /* Y, V and all else pass through */
    }
}

uint32_t eno_letter_value(char c) {
    c = eno_fold(c);
    for (uint32_t i = 0; i < ENO_LETTERS; i++)
        if (ALPHA[i].c == c) return ALPHA[i].v;
    return 0;
}

/* ---- polarity ---------------------------------------------------------
 * Two entries only. Vau's vowel face is light, its consonant face is
 * shadow. NOT folded: eno_fold maps W->U, and W must stay neutral.
 *
 * This is the CHARGE channel. It sits beside the magnitude above and does
 * not touch it: ALPHA, eno_fold, eno_letter_value, eno_gematria and
 * eno_root are unchanged, so every number in the shipped deck and in the
 * boot self-check (OLPIRT HPOU = 54 / root 9 / {12,5} / 16) is the number
 * it was. See PROVENANCE/ENOCHIAN_POLARITY.md for the derivation and for
 * the two schemes that were measured and rejected. */
static const struct { char c; int8_t q; } CHARGE[2] = { {'U', +1}, {'V', -1} };

/* eno_letter_charge is not marked retained and must not be. Measured on
 * this toolchain (aarch64 gcc 11.4 / binutils 2.38) __attribute__((retain))
 * is IGNORED -- it warns and emits a plain AX section -- so the marker buys
 * nothing but noise, and the Makefile's GC_ROOTS comment states the rule
 * anyway: retention must not be able to impersonate use. eno_net_charge is
 * a genuine caller; at -O2 it inlines this body, so the standalone section
 * is discarded while the CODE ships inside eno_net_charge. That is the
 * honest state, not a defect to paper over. */
int32_t eno_letter_charge(char c) {
    if (c >= 'a' && c <= 'z') c = (char)(c - 32);   /* case only, no fold */
    for (uint32_t i = 0; i < 2u; i++)
        if (CHARGE[i].c == c) return (int32_t)CHARGE[i].q;
    return 0;
}

int32_t eno_net_charge(const char *text, uint32_t len) {
    if (!text) return 0;
    int32_t n = 0;
    for (uint32_t i = 0; i < len && text[i]; i++)
        n += eno_letter_charge(text[i]);
    return n;
}

uint32_t eno_charge_role(int32_t net) {
    return (net > 0) ? 0u : (net < 0) ? 1u : 2u;
}

/* Consequence of unfolding Y: eno_letter_name('Y') now returns "" rather
 * than "Gon". That is correct and intended — Y is not one of the twenty-
 * one letters, it carries no gematria, and naming it after I's angel
 * would assert an identity the deck contradicts. "" is the documented
 * answer for anything outside the alphabet. */
const char *eno_letter_name(char c) {
    c = eno_fold(c);
    for (uint32_t i = 0; i < ENO_LETTERS; i++)
        if (ALPHA[i].c == c) return ALPHA[i].name;
    return "";
}

uint32_t eno_gematria(const char *text, uint32_t len) {
    if (!text) return 0;
    uint32_t g = 0;
    for (uint32_t i = 0; i < len && text[i]; i++)
        g += eno_letter_value(text[i]);
    return g;
}

uint32_t eno_root(uint32_t g) {
    while (g > 9u) {
        uint32_t s = 0;
        while (g) { s += g % 10u; g /= 10u; }
        g = s;
    }
    return g ? g : 9u;    /* the pipeline maps zero to 9; parity matters */
}

uint32_t eno_root_mirror(uint32_t root) {
    if (root < 1 || root > 9) return 0;
    return (root == 9u) ? 9u : 9u - root;
}

const char *eno_root_domain(uint32_t root) {
    static const char *D[10] = { "?",
        "Monadic - Unity",        "Dyadic - Polarity",
        "Triadic - Manifestation","Tetradic - Structure",
        "Pentadic - Sovereignty", "Hexadic - Labor",
        "Heptadic - Movement",    "Octadic - Messenger",
        "Enneadic - Power" };
    return (root >= 1 && root <= 9) ? D[root] : D[0];
}

/* ---- the three voices -------------------------------------------------- */
static const char *TITLE[3] = {
    "THE WIZARD'S COMPENDIUM",
    "THE WITCH'S GRIMOIRE",
    "THE ALCHEMIST'S TOME"
};
static const char *VERB[3]   = { "Invoke",  "Awaken",  "Activate" };
static const char *MANNER[3] = { "Speak the phonetic tongue with conviction",
                                 "Whisper the phonetic words with devotion",
                                 "Vibrate the phonetic sounds with presence" };

/* The Card-Between-Cards law, verbatim from the shipped deck. The three
 * texts are the engine's source: the same law in three registers. */
static const char *LAW[3] = {
    /* SOLAR — Laws of the Glyph & Grid, as written by the Grand Archmage */
    "Draw forth thy seal from the deck with steadfast purpose. Each seal "
    "bears a tarot alignment - consult thy cards to seek harmony with "
    "destiny's path.\n"
    "When thy drawn seal aligns with the tarot's counsel, thou standest in "
    "accord with fate itself. The magic flows unimpeded through thy will.\n"
    "Invoke the seal's power through its Enochian invocation. Speak the "
    "phonetic tongue with conviction. The sigil matrix shall channel thy "
    "intent into manifestation.\n"
    "Play these seals in complement with live tarot readings. The cards "
    "whisper truth; the seals enact it. This is the sacred covenant of the "
    "Glyph and Grid.",
    /* LUNAR — Wisdom of the Glyph & Grid, as inscribed by the High Priestess */
    "Receive your seal with open heart and clear intention. Each card "
    "carries a tarot resonance - listen to the cards to find your alignment "
    "with destiny.\n"
    "When your seal speaks in harmony with the tarot's guidance, you flow "
    "with the current of fate. The magic blossoms through your receptive "
    "grace.\n"
    "Awaken the seal's power through its Enochian prayer. Whisper the "
    "phonetic words with devotion. The sigil weaves your desire into being.\n"
    "Dance these seals in harmony with live tarot readings. The cards "
    "reveal wisdom; the seals embody it. This is the sacred union of the "
    "Glyph and Grid.",
    /* AEON — Principles of the Glyph & Grid, as transcribed by the Eternal Sage */
    "Select your seal with balanced mind and focused awareness. Each seal "
    "holds a tarot correspondence - observe the cards to discern your "
    "alignment with destiny.\n"
    "When your seal resonates with the tarot's message, you merge with the "
    "pattern of fate. The magic manifests through your integrated being.\n"
    "Activate the seal's power through its Enochian formula. Vibrate the "
    "phonetic sounds with presence. The sigil transforms your intent into "
    "reality.\n"
    "Weave these seals in synthesis with live tarot readings. The cards "
    "illuminate; the seals actualize. This is the sacred transmutation of "
    "the Glyph and Grid."
};

static uint32_t vix(eno_voice_t v) { return (v <= ENO_VOICE_AEON) ? (uint32_t)v : 2u; }

const char *eno_law(eno_voice_t v)        { return LAW[vix(v)]; }
const char *eno_law_title(eno_voice_t v)  { return TITLE[vix(v)]; }
const char *eno_voice_verb(eno_voice_t v) { return VERB[vix(v)]; }
const char *eno_voice_manner(eno_voice_t v){ return MANNER[vix(v)]; }
