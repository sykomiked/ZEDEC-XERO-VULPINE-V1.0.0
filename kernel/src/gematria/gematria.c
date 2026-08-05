/*
 * gematria.c — Gematria Syntactic Mapping System Implementation
 *
 * Maps natural language to second-quantized operator algebra via
 * character values, grammatical roles, and punctuation operators.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * License: ZEDEC Open Patent License 1.0
 */

#ifdef TEST_HOST
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#else
#include "freestanding.h"
#endif

#include "gematria.h"

/* ===== Helpers ===== */

static void gm_memset(void *dst, int v, uint32_t n) {
    uint8_t *d = (uint8_t *)dst; uint32_t i;
    for (i = 0; i < n; i++) d[i] = (uint8_t)v;
}

static void gm_memcpy(void *dst, const void *src, uint32_t n) {
    uint8_t *d = (uint8_t *)dst; const uint8_t *s = (const uint8_t *)src; uint32_t i;
    for (i = 0; i < n; i++) d[i] = s[i];
}

static uint32_t gm_strlen(const char *s) { uint32_t n = 0; while (s[n]) n++; return n; }

static void gm_strcpy(char *dst, const char *src) {
    uint32_t i = 0; while (src[i]) { dst[i] = src[i]; i++; } dst[i] = '\0';
}

static int gm_strcmp(const char *a, const char *b) {
    uint32_t i = 0; while (a[i] && b[i]) {
        if (a[i] != b[i]) return (int)(unsigned char)a[i] - (int)(unsigned char)b[i];
        i++;
    }
    return (int)(unsigned char)a[i] - (int)(unsigned char)b[i];
}

static int gm_strcasecmp(const char *a, const char *b) {
    uint32_t i = 0;
    while (a[i] && b[i]) {
        char ca = a[i], cb = b[i];
        if (ca >= 'a' && ca <= 'z') ca -= 32;
        if (cb >= 'a' && cb <= 'z') cb -= 32;
        if (ca != cb) return (int)(unsigned char)ca - (int)(unsigned char)cb;
        i++;
    }
    return (int)(unsigned char)a[i] - (int)(unsigned char)b[i];
}

/* ===== Prime table for prime gematria ===== */

static const uint32_t primes[26] = {
    2, 3, 5, 7, 11, 13, 17, 19, 23, 29,
    31, 37, 41, 43, 47, 53, 59, 61, 67, 71,
    73, 79, 83, 89, 97, 101
};

/* ===== Hebrew standard values ===== */

static const uint32_t hebrew_standard[26] = {
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
    10, 20, 20, 30, 30, 40, 40, 50, 50, 60,
    60, 70, 70, 80, 80, 90
};

/* ===== Common English verb list (simplified) ===== */

static const char *common_verbs[] = {
    "is", "am", "are", "was", "were", "be", "been", "being",
    "have", "has", "had", "do", "does", "did",
    "go", "goes", "went", "gone",
    "run", "walk", "jump", "jumps", "eat", "drink", "sleep",
    "make", "take", "give", "find", "tell", "ask",
    "build", "create", "destroy", "open", "close",
    "read", "write", "think", "know", "see", "hear",
    "move", "stop", "start", "change", "become",
    "execute", "compile", "render", "measure", "entangle",
    NULL
};

static const char *common_articles[] = {
    "the", "a", "an", "this", "that", "these", "those", NULL
};

static const char *common_prepositions[] = {
    "of", "in", "on", "at", "to", "for", "with", "by",
    "from", "into", "over", "under", "between", "through",
    "across", "about", "against", "during", "without", NULL
};

static const char *common_conjunctions[] = {
    "and", "or", "but", "nor", "yet", "so", "if", "because",
    "while", "although", "unless", "since", NULL
};

static const char *common_pronouns[] = {
    "i", "you", "he", "she", "it", "we", "they",
    "me", "him", "her", "us", "them",
    "my", "your", "his", "its", "our", "their",
    NULL
};

static int is_in_list(const char *word, const char **list) {
    uint32_t i = 0;
    while (list[i]) {
        if (gm_strcasecmp(word, list[i]) == 0) return 1;
        i++;
    }
    return 0;
}

/* ===== Init ===== */

void gematria_init(gematria_t *g) {
    gm_memset(g, 0, sizeof(gematria_t));
    g->initialized = true;
}

/* ===== Character Value Lookup ===== */

uint32_t gematria_char_value(gematria_t *g, char c, gem_system_t system) {
    /* Convert to uppercase index (0-25) */
    char uc = c;
    if (uc >= 'a' && uc <= 'z') uc -= 32;
    if (uc < 'A' || uc > 'Z') {
        /* Non-alpha: use ASCII for ASCII system, 0 otherwise */
        if (system == GEM_SYSTEM_ASCII) return (uint32_t)(uint8_t)c;
        return 0;
    }
    uint32_t idx = (uint32_t)(uc - 'A');

    switch (system) {
        case GEM_SYSTEM_ORDINAL:   return idx + 1;
        case GEM_SYSTEM_STANDARD:  return hebrew_standard[idx];
        case GEM_SYSTEM_REDUCTION: return gematria_reduce(idx + 1);
        case GEM_SYSTEM_ASCII:     return (uint32_t)(uint8_t)uc;
        case GEM_SYSTEM_PRIME:     return primes[idx];
        case GEM_SYSTEM_TORAH:     return hebrew_standard[idx] * 10;  /* Extended */
        case GEM_SYSTEM_GREEK:     return idx + 1;  /* Simplified Greek isopsophy */
        case GEM_SYSTEM_CUSTOM:
            if (g && g->custom_mappings[GEM_SYSTEM_CUSTOM].active)
                return g->custom_mappings[GEM_SYSTEM_CUSTOM].char_value[(uint8_t)c];
            return idx + 1;
        default: return idx + 1;
    }
}

uint32_t gematria_word_value(gematria_t *g, const char *word, gem_system_t system) {
    if (!word) return 0;
    uint32_t total = 0;
    uint32_t i;
    for (i = 0; word[i]; i++)
        total += gematria_char_value(g, word[i], system);
    return total;
}

uint32_t gematria_reduce(uint32_t value) {
    while (value > 9) {
        uint32_t sum = 0;
        while (value > 0) { sum += value % 10; value /= 10; }
        value = sum;
    }
    return value;
}

int gematria_set_custom(gematria_t *g, char c, uint32_t value) {
    if (!g) return -1;
    g->custom_mappings[GEM_SYSTEM_CUSTOM].char_value[(uint8_t)c] = value;
    g->custom_mappings[GEM_SYSTEM_CUSTOM].active = true;
    return 0;
}

/* ===== Grammatical Role Detection ===== */

gem_role_t gematria_detect_role(const char *word) {
    if (!word || !word[0]) return GEM_ROLE_UNKNOWN;

    /* Check articles first */
    if (is_in_list(word, common_articles)) return GEM_ROLE_ARTICLE;

    /* Check pronouns */
    if (is_in_list(word, common_pronouns)) return GEM_ROLE_PRONOUN;

    /* Check prepositions */
    if (is_in_list(word, common_prepositions)) return GEM_ROLE_PREPOSITION;

    /* Check conjunctions */
    if (is_in_list(word, common_conjunctions)) return GEM_ROLE_CONJUNCTION;

    /* Check verbs */
    if (is_in_list(word, common_verbs)) return GEM_ROLE_VERB;

    /* Heuristic: words ending in -ly are adverbs */
    uint32_t len = gm_strlen(word);
    if (len >= 3 && word[len-2] == 'l' && word[len-1] == 'y')
        return GEM_ROLE_ADVERB;

    /* Heuristic: words ending in -ful, -less, -ous, -ive, -al are adjectives */
    if (len >= 4) {
        if ((word[len-3] == 'f' && word[len-2] == 'u' && word[len-1] == 'l') ||
            (word[len-4] == 'l' && word[len-3] == 'e' && word[len-2] == 's' && word[len-1] == 's') ||
            (word[len-3] == 'o' && word[len-2] == 'u' && word[len-1] == 's') ||
            (word[len-3] == 'i' && word[len-2] == 'v' && word[len-1] == 'e') ||
            (word[len-2] == 'a' && word[len-1] == 'l'))
            return GEM_ROLE_ADJECTIVE;
    }

    /* Default: noun */
    return GEM_ROLE_NOUN;
}

gem_punct_t gematria_detect_punct(char c) {
    switch (c) {
        case '.': return GEM_PUNCT_PERIOD;
        case ',': return GEM_PUNCT_COMMA;
        case '?': return GEM_PUNCT_QUESTION;
        case '!': return GEM_PUNCT_EXCLAIM;
        case ';': return GEM_PUNCT_SEMICOLON;
        case ':': return GEM_PUNCT_COLON;
        case '(': return GEM_PUNCT_PAREN_OPEN;
        case ')': return GEM_PUNCT_PAREN_CLOSE;
        default:  return GEM_PUNCT_NONE;
    }
}

/* ===== Fock State Projection ===== */

int gematria_to_fock(gematria_t *g, const gem_word_t *word,
                     const hdcm_vector_t *language_base,
                     hdcm_vector_t *out_vector) {
    if (!word || !out_vector) return -1;

    /* Generate vector from gematria value as seed */
    uint32_t seed = word->gematria_value;
    if (seed == 0) seed = 1;

    /* Incorporate grammatical role into seed */
    seed = seed * 31 + (uint32_t)word->grammatical_role;
    seed = seed * 17 + word->char_count;

    hdcm_vector_random(out_vector, seed);

    /* Bind with language base vector */
    if (language_base) {
        hdcm_vector_t bound;
        hdcm_vector_bind(out_vector, language_base, &bound);
        gm_memcpy(out_vector, &bound, sizeof(hdcm_vector_t));
    }

    /* Apply phase shift for adjectives/adverbs */
    if (word->phase_shift != 0.0f) {
        uint32_t shift = (uint32_t)(word->phase_shift * 1000.0f) % HDCM_VECTOR_DIM;
        hdcm_vector_t shifted;
        hdcm_vector_permute(out_vector, shift, &shifted);
        gm_memcpy(out_vector, &shifted, sizeof(hdcm_vector_t));
    }

    return 0;
}

int gematria_sentence_to_fock(gematria_t *g, const gem_sentence_t *sentence,
                               hdcm_vector_t *out_vector) {
    if (!sentence || !out_vector) return -1;

    if (sentence->num_words == 0) {
        hdcm_vector_zero(out_vector);
        return 0;
    }

    /* Start with first word's vector */
    gm_memcpy(out_vector, &sentence->words[0].fock_vector, sizeof(hdcm_vector_t));

    /* Superpose subsequent words (additive in Fock space) */
    uint32_t i;
    for (i = 1; i < sentence->num_words; i++) {
        hdcm_vector_t tmp;

        /* Check punctuation operator */
        if (sentence->words[i-1].trailing_punct == GEM_PUNCT_COMMA) {
            /* Tensor product: bind the vectors */
            hdcm_vector_bind(out_vector, &sentence->words[i].fock_vector, &tmp);
        } else {
            /* Superposition: majority vote */
            hdcm_vector_superpose(out_vector, &sentence->words[i].fock_vector, &tmp);
        }

        gm_memcpy(out_vector, &tmp, sizeof(hdcm_vector_t));
    }

    /* Apply state collapse if period present */
    if (sentence->has_collapse) {
        /* Measurement: the vector is already determined, just mark it */
        /* In real quantum: this would project onto an eigenstate */
    }

    return 0;
}

/* ===== Sentence Analysis ===== */

int gematria_analyze_sentence(gematria_t *g, const char *text,
                               gem_system_t system,
                               gem_sentence_t *out) {
    if (!g || !text || !out) return -1;
    gm_memset(out, 0, sizeof(gem_sentence_t));

    uint32_t i = 0;
    uint32_t word_start = 0;
    uint32_t text_len = gm_strlen(text);

    while (i <= text_len) {
        char c = (i < text_len) ? text[i] : ' ';

        if (c == ' ' || c == '\t' || c == '\n' || i == text_len) {
            /* End of word */
            if (i > word_start && out->num_words < GEM_MAX_WORDS) {
                uint32_t wlen = i - word_start;
                if (wlen >= GEM_MAX_WORD_LEN) wlen = GEM_MAX_WORD_LEN - 1;

                gem_word_t *w = &out->words[out->num_words];
                gm_memset(w, 0, sizeof(gem_word_t));
                gm_memcpy(w->text, text + word_start, wlen);
                w->text[wlen] = '\0';
                w->char_count = wlen;

                /* Check for trailing punctuation */
                if (wlen > 0) {
                    char last = w->text[wlen - 1];
                    gem_punct_t p = gematria_detect_punct(last);
                    if (p != GEM_PUNCT_NONE) {
                        w->trailing_punct = p;
                        w->text[wlen - 1] = '\0';
                        w->char_count = wlen - 1;

                        if (p == GEM_PUNCT_PERIOD || p == GEM_PUNCT_EXCLAIM)
                            out->has_collapse = true;
                        if (p == GEM_PUNCT_QUESTION)
                            out->has_superposition = true;
                    }
                }

                /* Compute gematria value */
                w->gematria_value = gematria_word_value(g, w->text, system);

                /* Per-character values */
                uint32_t j;
                for (j = 0; j < w->char_count && j < GEM_MAX_WORD_LEN; j++)
                    w->char_values[j] = gematria_char_value(g, w->text[j], system);

                /* Detect grammatical role */
                w->grammatical_role = gematria_detect_role(w->text);

                /* Set phase shift for modifiers */
                if (w->grammatical_role == GEM_ROLE_ADJECTIVE ||
                    w->grammatical_role == GEM_ROLE_ADVERB) {
                    w->phase_shift = (float)(w->gematria_value % 360) / 360.0f;
                    out->modifier_count++;
                } else if (w->grammatical_role == GEM_ROLE_NOUN) {
                    out->noun_count++;
                } else if (w->grammatical_role == GEM_ROLE_VERB) {
                    out->verb_count++;
                }

                /* Generate Fock vector */
                gematria_to_fock(g, w, NULL, &w->fock_vector);

                out->total_gematria += w->gematria_value;
                out->num_words++;
            }
            word_start = i + 1;
        }
        i++;
    }

    /* Generate composite sentence vector */
    gematria_sentence_to_fock(g, out, &out->sentence_vector);
    out->measured = out->has_collapse;

    g->total_sentences_processed++;
    g->total_words_processed += out->num_words;

    return 0;
}

/* ===== Phase Mapping ===== */

const char *gematria_phase_mapping(gem_role_t role) {
    switch (role) {
        case GEM_ROLE_NOUN:        return ".36n9 (TRUE: state vector)";
        case GEM_ROLE_VERB:        return ".zedec (GLUT+: operator excitation)";
        case GEM_ROLE_ADJECTIVE:   return ".36m9 (parity: phase modifier)";
        case GEM_ROLE_ADVERB:      return ".36m9 (parity: phase modifier)";
        case GEM_ROLE_ARTICLE:     return ".ula (GLUT0: identity anchor)";
        case GEM_ROLE_PREPOSITION: return ".36m9 (parity: tensor link)";
        case GEM_ROLE_CONJUNCTION: return ".zedec (GLUT+: superposition)";
        case GEM_ROLE_PRONOUN:     return ".9n63 (FALSE: reference mirror)";
        case GEM_ROLE_INTERJECTION: return ".vino (GLUT-: delta event)";
        default: return ".36n9 (default)";
    }
}

/* ===== Utility ===== */

const char *gematria_system_name(gem_system_t system) {
    switch (system) {
        case GEM_SYSTEM_ORDINAL:   return "Ordinal (A=1..Z=26)";
        case GEM_SYSTEM_STANDARD:  return "Standard (Hebrew traditional)";
        case GEM_SYSTEM_REDUCTION: return "Reduction (digital root)";
        case GEM_SYSTEM_ASCII:     return "ASCII (raw values)";
        case GEM_SYSTEM_PRIME:     return "Prime (A=2,B=3,C=5...)";
        case GEM_SYSTEM_TORAH:     return "Torah (extended)";
        case GEM_SYSTEM_GREEK:     return "Greek (isopsophy)";
        case GEM_SYSTEM_CUSTOM:    return "Custom (user-defined)";
        default: return "Unknown";
    }
}

const char *gematria_role_name(gem_role_t role) {
    switch (role) {
        case GEM_ROLE_NOUN:        return "Noun (state vector |v>)";
        case GEM_ROLE_VERB:        return "Verb (operator a†/a)";
        case GEM_ROLE_ADJECTIVE:   return "Adjective (scalar modifier)";
        case GEM_ROLE_ADVERB:      return "Adverb (phase modifier)";
        case GEM_ROLE_ARTICLE:     return "Article (identity operator)";
        case GEM_ROLE_PREPOSITION: return "Preposition (tensor product ⊗)";
        case GEM_ROLE_CONJUNCTION: return "Conjunction (superposition +)";
        case GEM_ROLE_PRONOUN:     return "Pronoun (reference vector)";
        case GEM_ROLE_INTERJECTION: return "Interjection (delta function)";
        case GEM_ROLE_UNKNOWN:     return "Unknown";
        default: return "Unknown";
    }
}

const char *gematria_punct_name(gem_punct_t p) {
    switch (p) {
        case GEM_PUNCT_NONE:       return "none";
        case GEM_PUNCT_PERIOD:     return "period (state collapse)";
        case GEM_PUNCT_COMMA:      return "comma (tensor product ⊗)";
        case GEM_PUNCT_QUESTION:   return "question (superposition G+)";
        case GEM_PUNCT_EXCLAIM:    return "exclaim (forced collapse)";
        case GEM_PUNCT_SEMICOLON:  return "semicolon (weak tensor)";
        case GEM_PUNCT_COLON:      return "colon (definition operator)";
        case GEM_PUNCT_PAREN_OPEN: return "paren_open (subspace open)";
        case GEM_PUNCT_PAREN_CLOSE: return "paren_close (subspace close)";
        default: return "unknown";
    }
}
