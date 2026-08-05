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

char eno_fold(char c) {
    if (c >= 'a' && c <= 'z') c = (char)(c - 32);
    switch (c) {                       /* grammar §1: J,K,W,Y are allographs */
    case 'J': return 'I';
    case 'K': return 'C';
    case 'W': return 'U';
    case 'Y': return 'I';
    default:  return c;
    }
}

uint32_t eno_letter_value(char c) {
    c = eno_fold(c);
    for (uint32_t i = 0; i < ENO_LETTERS; i++)
        if (ALPHA[i].c == c) return ALPHA[i].v;
    return 0;
}

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
