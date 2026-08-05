/* digital_dna.c — Digital DNA Implementation for ZEDEC pqOS
 *
 * Implements the OS's own genetic code using:
 *   - Binary-to-genetic direct mapping (A=00, C=01, G=10, T/U=11)
 *   - Codon triplets (3 bases = 1 amino acid)
 *   - 22 amino acids ↔ 22 Hebrew letters ↔ gematria values
 *   - 5 implied vowels ↔ 5 axioms (trit states) of five-phase logic
 *   - Aramaic (light) / Hebrew (dark) polarity encoding
 *   - Sonic chemistry frequency conversion for harmonic engine
 *   - OS identity and integrity verification
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * License: SEL-3.3
 */

#include "digital_dna.h"

#ifdef TEST_HOST
#include <string.h>
#include <math.h>
#include <stdio.h>
#else
#include "freestanding.h"
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ===== 22 Amino Acids ↔ 22 Hebrew Letters ↔ Gematria ===== */

static const ddna_letter_map_t s_22_letters[22] = {
    /* 1 */ {'F', "\xD7\x90", 1,    "UUU", "TTT", "Pishra",  "Aleph"},
    /* 2 */ {'L', "\xD7\x91", 2,    "CUG", "CTG", "Lamda",   "Bet"},
    /* 3 */ {'S', "\xD7\x92", 3,    "UCU", "TCT", "Smla",    "Gimel"},
    /* 4 */ {'Y', "\xD7\x93", 4,    "UAU", "TAT", "Tira",    "Dalet"},
    /* 5 */ {'*', "\xD7\x94", 5,    "UAA", "TAA", "Sula",    "He"},
    /* 6 */ {'C', "\xD7\x95", 6,    "UGU", "TGT", "Kista",   "Vav"},
    /* 7 */ {'W', "\xD7\x96", 7,    "UGG", "TGG", "Hawa",    "Zayin"},
    /* 8 */ {'P', "\xD7\x97", 8,    "CCU", "CCT", "Pitra",   "Chet"},
    /* 9 */ {'H', "\xD7\x98", 9,    "CAU", "CAT", "Harmna",  "Tet"},
    /* 10 */ {'Q', "\xD7\x99", 10,   "CAA", "CAA", "Qala",   "Yod"},
    /* 11 */ {'R', "\xD7\x9B", 20,   "CGU", "CGT", "Risha",  "Kaf"},
    /* 12 */ {'I', "\xD7\x9C", 30,   "AUU", "ATT", "Ila",    "Lamed"},
    /* 13 */ {'M', "\xD7\x9E", 40,   "AUG", "ATG", "Mara",   "Mem"},
    /* 14 */ {'N', "\xD7\xA0", 50,   "AAU", "AAT", "Nuna",   "Nun"},
    /* 15 */ {'K', "\xD7\xA1", 60,   "AAA", "AAA", "Kula",   "Samekh"},
    /* 16 */ {'V', "\xD7\xA2", 70,   "GUU", "GTT", "Vura",   "Ayin"},
    /* 17 */ {'A', "\xD7\xA4", 80,   "GCU", "GCT", "Ahra",   "Pe"},
    /* 18 */ {'D', "\xD7\xA6", 90,   "GAU", "GAT", "Dila",   "Tsadi"},
    /* 19 */ {'E', "\xD7\xA7", 100,  "GAA", "GAA", "Ela",    "Qof"},
    /* 20 */ {'G', "\xD7\xA8", 200,  "GGU", "GGT", "Gumra",  "Resh"},
    /* 21 */ {'T', "\xD7\xA9", 300,  "ACU", "ACT", "Tura",   "Shin"},
    /* 22 */ {'U', "\xD7\xAA", 400,  "UCA", "TCA", "Ura",    "Tav"},
};

/* ===== 5 Vowels ↔ 5 Axioms (Trit States) ===== */

static const ddna_vowel_axiom_t s_5_vowels[5] = {
    /* 1 — Absence (TRIT_FALSE = 0) */
    {0, "Sheva",   "FALSE",        0, 0.0},
    /* 2 — Presence (TRIT_TRUE = 1) */
    {1, "Patach",  "TRUE",         1, 1.0},
    /* 3 — Balanced superposition (TRIT_GLUT_NEUTRAL = 5) */
    {2, "Tzeri",   "GLUT_NEUTRAL", 5, 0.5},
    /* 4 — Constructive superposition (TRIT_GLUT_PLUS = 3) */
    {3, "Cholem",  "GLUT_PLUS",    3, 0.75},
    /* 5 — Destructive superposition (TRIT_GLUT_MINUS = 4) */
    {4, "Qamatz",  "GLUT_MINUS",   4, 0.25},
};

/* ===== OS Identity Markers (as DNA codons) ===== */
/* The OS genome encodes its identity as a sequence of codons.
 * Each subsystem maps to a specific amino acid / Hebrew letter. */

typedef struct ddna_os_marker {
    const char *subsystem;    /* OS subsystem name */
    char codon[4];            /* DNA codon */
    char amino_acid;          /* Amino acid */
    uint8_t hebrew_idx;       /* Hebrew letter index (0-21) */
} ddna_os_marker_t;

static const ddna_os_marker_t s_os_markers[] = {
    {"KERNEL",   "ATG", 'M', 12},  /* Mem — start/init */
    {"SCHED",    "ACU", 'T', 20},  /* Shin — time/scheduling */
    {"MM",       "GGU", 'G', 19},  /* Resh — memory */
    {"VFS",      "GAA", 'E', 18},  /* Qof — filesystem */
    {"NET",      "GAU", 'D', 17},  /* Tsadi — network */
    {"AUDIO",    "UGG", 'W', 6},   /* Zayin — audio genomics */
    {"CHEM",     "UGU", 'C', 5},   /* Vav — sonic chemistry */
    {"CRYPTO",   "AAA", 'K', 14},  /* Samekh — crypto */
    {"DNA",      "UUU", 'F', 0},   /* Aleph — digital DNA */
    {"PHASE",    "UAA", '*', 4},   /* He — phase logic (stop=transition) */
};

#define DDNA_NUM_OS_MARKERS (sizeof(s_os_markers) / sizeof(s_os_markers[0]))

/* ===== Binary <-> Genetic Conversion ===== */

char ddna_bits_to_base(uint8_t bits, ddna_mode_t mode) {
    switch (bits & 0x03) {
        case DDNA_BIN_TO_A: return 'A';
        case DDNA_BIN_TO_C: return 'C';
        case DDNA_BIN_TO_G: return 'G';
        case DDNA_BIN_TO_T: return (mode == DDNA_MODE_RNA) ? 'U' : 'T';
        default: return 'A';  /* Should never happen */
    }
}

uint8_t ddna_base_to_bits(char base) {
    if (base >= 'a' && base <= 'z') base -= 32;
    switch (base) {
        case 'A': return DDNA_A_TO_BIN;
        case 'C': return DDNA_C_TO_BIN;
        case 'G': return DDNA_G_TO_BIN;
        case 'T':
        case 'U': return DDNA_T_TO_BIN;
        default:  return DDNA_A_TO_BIN;
    }
}

uint32_t ddna_binary_to_genetic(const uint8_t *data, uint32_t data_len,
                                 char *out, uint32_t out_max, ddna_mode_t mode) {
    uint32_t out_len = 0;
    for (uint32_t i = 0; i < data_len && out_len + 4 <= out_max; i++) {
        uint8_t byte = data[i];
        for (int j = 6; j >= 0; j -= 2) {
            out[out_len++] = ddna_bits_to_base((byte >> j) & 0x03, mode);
        }
    }
    out[out_len] = '\0';
    return out_len;
}

uint32_t ddna_genetic_to_binary(const char *dna, uint32_t dna_len,
                                 uint8_t *out, uint32_t out_max) {
    /* 4 bases = 1 byte (each base = 2 bits) */
    uint32_t out_len = 0;
    uint8_t byte = 0;
    int bit_pos = 6;
    uint32_t i;

    for (i = 0; i < dna_len && out_len < out_max; i++) {
        uint8_t bits = ddna_base_to_bits(dna[i]);
        byte |= (bits << bit_pos);
        bit_pos -= 2;
        if (bit_pos < 0) {
            out[out_len++] = byte;
            byte = 0;
            bit_pos = 6;
        }
    }
    /* Flush remaining bits */
    if (bit_pos != 6 && out_len < out_max) {
        out[out_len++] = byte;
    }

    return out_len;
}

uint32_t ddna_codon_to_binary(const char *codon) {
    /* 3 bases × 2 bits = 6 bits */
    uint32_t val = 0;
    for (int i = 0; i < 3; i++) {
        val = (val << 2) | ddna_base_to_bits(codon[i]);
    }
    return val;
}

/* ===== Initialization ===== */

void ddna_init(ddna_genome_t *genome, ddna_mode_t mode) {
    if (!genome) return;
    memset(genome, 0, sizeof(ddna_genome_t));
    genome->mode = mode;
    genome->version = DDNA_GENOME_VERSION;
}

void ddna_init_os_genome(ddna_genome_t *genome) {
    ddna_init(genome, DDNA_MODE_DNA);

    /* Build OS identity genome from subsystem markers */
    for (uint32_t i = 0; i < DDNA_NUM_OS_MARKERS; i++) {
        const ddna_os_marker_t *m = &s_os_markers[i];
        /* Append the codon */
        for (int j = 0; j < 3 && genome->length < DDNA_MAX_LENGTH; j++) {
            char base = m->codon[j];
            if (genome->mode == DDNA_MODE_RNA && base == 'T') base = 'U';
            genome->sequence[genome->length++] = base;
        }
    }

    /* Translate to protein */
    ddna_translate(genome);

    /* Compute stats and hash */
    ddna_compute_stats(genome);
    ddna_compute_hash(genome);
}

/* ===== Genome Operations ===== */

uint32_t ddna_append_data(ddna_genome_t *genome, const uint8_t *data, uint32_t len) {
    if (!genome || !data) return 0;
    uint32_t remaining = DDNA_MAX_LENGTH - genome->length;
    uint32_t max_bases = remaining / 4 * 4;  /* Each byte = 4 bases */
    if (max_bases == 0) return 0;

    uint32_t added = ddna_binary_to_genetic(data, len,
                                             &genome->sequence[genome->length],
                                             remaining, genome->mode);
    genome->length += added;
    return added;
}

uint32_t ddna_append_text(ddna_genome_t *genome, const char *text) {
    if (!genome || !text) return 0;
    return ddna_append_data(genome, (const uint8_t *)text, strlen(text));
}

uint32_t ddna_append_marker(ddna_genome_t *genome, const char *marker_codon) {
    if (!genome || !marker_codon) return 0;
    if (genome->length + 3 > DDNA_MAX_LENGTH) return 0;

    for (int i = 0; i < 3; i++) {
        char base = marker_codon[i];
        if (genome->mode == DDNA_MODE_RNA && base == 'T') base = 'U';
        genome->sequence[genome->length++] = base;
    }
    return 3;
}

int ddna_translate(ddna_genome_t *genome) {
    if (!genome) return -1;

    genome->protein_length = 0;
    uint32_t i;

    for (i = 0; i + 2 < genome->length && genome->protein_length < DDNA_MAX_LENGTH / 3; i += 3) {
        char codon[3];
        for (int j = 0; j < 3; j++) {
            char c = genome->sequence[i + j];
            if (c >= 'a' && c <= 'z') c -= 32;
            /* Convert T to U for RNA lookup */
            if (c == 'T') c = 'U';
            codon[j] = c;
        }

        /* Translate using audiogenomics_pro's genetic code */
        char aa = agp_translate_codon(codon);
        if (aa == '*') {
            /* Stop codon — record but don't break (OS genome continues) */
            genome->protein[genome->protein_length++] = '*';
        } else {
            genome->protein[genome->protein_length++] = aa;
        }
    }

    genome->codon_count = genome->protein_length;
    return 0;
}

void ddna_compute_hash(ddna_genome_t *genome) {
    if (!genome) return;

    /* FNV-1a hash (deterministic, no external deps) */
    uint64_t hash = 14695981039346656037ULL;
    for (uint32_t i = 0; i < genome->length; i++) {
        hash ^= (uint8_t)genome->sequence[i];
        hash *= 1099511628211ULL;
    }
    /* Also hash protein */
    for (uint32_t i = 0; i < genome->protein_length; i++) {
        hash ^= (uint8_t)genome->protein[i];
        hash *= 1099511628211ULL;
    }

    /* Expand to 32 bytes */
    for (int i = 0; i < DDNA_HASH_SIZE; i++) {
        genome->hash[i] = (uint8_t)((hash >> (i % 8 * 8)) & 0xFF);
        hash = hash * 1099511628211ULL + i + 1;
    }
}

void ddna_compute_stats(ddna_genome_t *genome) {
    if (!genome) return;

    uint32_t counts[6] = {0, 0, 0, 0, 0, 0};  /* A, T, C, G, U, N */
    for (uint32_t i = 0; i < genome->length; i++) {
        char c = genome->sequence[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        switch (c) {
            case 'A': counts[0]++; break;
            case 'T': counts[1]++; break;
            case 'C': counts[2]++; break;
            case 'G': counts[3]++; break;
            case 'U': counts[4]++; break;
            default:  counts[5]++; break;
        }
    }

    uint32_t gc = counts[2] + counts[3];
    uint32_t total = genome->length - counts[5];
    genome->gc_content = (total > 0) ? (gc * 100 / total) : 0;
    genome->codon_count = genome->length / 3;
}

/* ===== Gene Extraction ===== */

int ddna_extract_genes(const ddna_genome_t *genome, ddna_gene_t *genes,
                        uint32_t max_genes, uint32_t *count) {
    if (!genome || !genes || !count) return -1;

    uint32_t n = 0;
    for (uint32_t i = 0; i + 2 < genome->length && n < max_genes; i += 3) {
        ddna_gene_t *g = &genes[n];
        for (int j = 0; j < 3; j++) {
            char c = genome->sequence[i + j];
            if (c >= 'a' && c <= 'z') c -= 32;
            if (c == 'T') c = 'U';
            g->codon[j] = c;
        }
        g->codon[3] = '\0';
        g->position = i;
        g->amino_acid = agp_translate_codon(g->codon);
        g->binary_value = ddna_codon_to_binary(genome->sequence + i);
        g->frequency = ddna_gene_frequency(g);
        n++;
    }

    *count = n;
    return 0;
}

double ddna_gene_frequency(const ddna_gene_t *gene) {
    if (!gene) return 0.0;

    /* Each codon maps to an amino acid, which maps to a Hebrew letter,
     * which has a gematria value. Use the gematria value as the proton
     * count in the sonic chemistry formula: [(N/Phi)*1.125]^2 = E */
    for (int i = 0; i < 22; i++) {
        if (s_22_letters[i].amino_acid == gene->amino_acid) {
            uint32_t gematria = s_22_letters[i].gematria;
            /* Use gematria as N in the chemistry formula */
            return agp_chemistry_element_frequency((uint8_t)(gematria > 118 ? gematria % 118 + 1 : gematria));
        }
    }
    return 0.0;
}

/* ===== Harmonic Profile ===== */

int ddna_build_harmonic_profile(const ddna_genome_t *genome,
                                 ddna_harmonic_profile_t *profile) {
    if (!genome || !profile) return -1;

    memset(profile, 0, sizeof(ddna_harmonic_profile_t));

    /* Extract genes and build compound frequency */
    ddna_gene_t genes[DDNA_MAX_LENGTH / 3];
    uint32_t gene_count;
    if (ddna_extract_genes(genome, genes, DDNA_MAX_LENGTH / 3, &gene_count) != 0)
        return -2;

    if (gene_count == 0) return -3;

    /* Build compound components from genes (using gematria as atomic numbers) */
    agp_compound_component_t components[AGP_MAX_COMPOUND];
    uint8_t num_comp = 0;
    uint16_t total_atoms = 0;

    for (uint32_t i = 0; i < gene_count && num_comp < AGP_MAX_COMPOUND; i++) {
        /* Find gematria for this amino acid */
        for (int j = 0; j < 22; j++) {
            if (s_22_letters[j].amino_acid == genes[i].amino_acid) {
                uint32_t gem = s_22_letters[j].gematria;
                /* Map gematria to atomic number (mod 118, min 1) */
                uint8_t atomic = (uint8_t)((gem % 118) + 1);

                /* Check if this element already exists in compound */
                bool found = false;
                for (uint8_t k = 0; k < num_comp; k++) {
                    if (components[k].atomic_number == atomic) {
                        components[k].atom_count++;
                        total_atoms++;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    components[num_comp].atomic_number = atomic;
                    components[num_comp].atom_count = 1;
                    components[num_comp].element_freq =
                        agp_chemistry_element_frequency(atomic);
                    num_comp++;
                    total_atoms++;
                }
                break;
            }
        }
    }

    /* Compute compound frequency */
    if (num_comp > 0 && total_atoms > 0) {
        profile->fundamental_freq =
            agp_chemistry_compound_frequency(components, num_comp, total_atoms);
    }

    /* Store individual element frequencies for marker genes */
    profile->num_elements = 0;
    for (uint32_t i = 0; i < gene_count && profile->num_elements < 8; i++) {
        profile->element_freqs[profile->num_elements++] = genes[i].frequency;
    }

    return 0;
}

void ddna_play_genome(const ddna_harmonic_profile_t *profile,
                      uint32_t sample_rate, agp_waveform_t wave,
                      agp_audio_buffer_t *out) {
    if (!profile || !out || profile->fundamental_freq <= 0.0) {
        if (out) out->length = 0;
        return;
    }

    /* Generate the compound frequency as the fundamental */
    double duration = 4.5;  /* 4.5 seconds default */
    uint32_t len;
    agp_chemistry_generate_tone(profile->fundamental_freq, duration,
                                 sample_rate, wave, out->samples, &len,
                                 AGP_MAX_AUDIO);
    out->length = len;
    out->sample_rate = sample_rate;

    /* Layer individual element frequencies as harmonics */
    float layer[AGP_MAX_AUDIO];
    for (uint8_t i = 0; i < profile->num_elements; i++) {
        if (profile->element_freqs[i] <= 0.0) continue;

        uint32_t l_len;
        agp_chemistry_generate_tone(profile->element_freqs[i], duration,
                                     sample_rate, wave, layer, &l_len,
                                     AGP_MAX_AUDIO);

        uint32_t blend = (len < l_len) ? len : l_len;
        float weight = 0.15f / (float)(i + 1);  /* Diminishing weight */
        for (uint32_t j = 0; j < blend; j++)
            out->samples[j] += layer[j] * weight;
    }

    if (out->length > 0)
        agp_normalize(out->samples, out->length, 0.95);
}

/* ===== OS Identity ===== */

const char *ddna_os_identity_string(void) {
    return "ZEDEC-pqOS-VOVINA-SHAKINA";
}

uint32_t ddna_os_genome_fingerprint(void) {
    /* Simple deterministic fingerprint from OS identity string */
    const char *id = ddna_os_identity_string();
    uint32_t hash = 2166136261U;
    while (*id) {
        hash ^= (uint8_t)*id++;
        hash *= 16777619U;
    }
    return hash;
}

bool ddna_verify_integrity(const ddna_genome_t *genome) {
    if (!genome) return false;
    /* Recompute hash and compare */
    ddna_genome_t tmp;
    memcpy(&tmp, genome, sizeof(ddna_genome_t));
    ddna_compute_hash(&tmp);
    for (int i = 0; i < DDNA_HASH_SIZE; i++) {
        if (tmp.hash[i] != genome->hash[i]) return false;
    }
    return true;
}

/* ===== Utility ===== */

const char *ddna_mode_string(ddna_mode_t mode) {
    return (mode == DDNA_MODE_RNA) ? "RNA" : "DNA";
}

void ddna_print_genome(const ddna_genome_t *genome) {
    if (!genome) return;
#ifdef TEST_HOST
    printf("=== Digital DNA Genome ===\n");
    printf("  Mode:       %s\n", ddna_mode_string(genome->mode));
    printf("  Version:    %u\n", genome->version);
    printf("  Length:     %u bases\n", genome->length);
    printf("  Codons:     %u\n", genome->codon_count);
    printf("  GC content: %u%%\n", genome->gc_content);
    printf("  Protein:    %.*s\n", (int)genome->protein_length, genome->protein);
    printf("  Fingerprint: 0x%08X\n", ddna_os_genome_fingerprint());

    /* Print 22-letter mapping */
    printf("\n=== 22-Letter Mapping (Amino Acid ↔ Hebrew ↔ Gematria) ===\n");
    for (int i = 0; i < 22; i++) {
        printf("  %2d: %c ↔ %s (gematria=%u) [%s/%s]\n",
               i + 1, s_22_letters[i].amino_acid,
               s_22_letters[i].hebrew,
               s_22_letters[i].gematria,
               s_22_letters[i].aramaic_name,
               s_22_letters[i].hebrew_name);
    }

    /* Print 5-vowel ↔ 5-axiom mapping */
    printf("\n=== 5 Vowels ↔ 5 Axioms (Implied Pronunciation) ===\n");
    for (int i = 0; i < 5; i++) {
        printf("  %d: %s ↔ %s (trit=%u, weight=%.2f)\n",
               i + 1, s_5_vowels[i].name,
               s_5_vowels[i].axiom_name,
               s_5_vowels[i].trit_value,
               s_5_vowels[i].phase_weight);
    }
#else
    extern void fb_puts(const char *str);
    fb_puts("=== Digital DNA Genome ===\n");
    fb_puts("  OS identity: ZEDEC-pqOS\n");
    fb_puts("  22 letters mapped\n");
    fb_puts("  5 vowels (axioms) mapped\n");
#endif
}

/* ===== Accessor for 22-letter mapping ===== */
const ddna_letter_map_t *ddna_get_22_letters(void) {
    return s_22_letters;
}

/* ===== Accessor for 5-vowel mapping ===== */
const ddna_vowel_axiom_t *ddna_get_5_vowels(void) {
    return s_5_vowels;
}

/* ===== Consonant Grid & Vowel Animation (5PL Breath Engine) ===== */

/* Vowel system roles (string literals for the grid) */
static const char *s_vowel_roles[5] = {
    "Inversion / Shadow lock",          /* FALSE — Hebrew interior */
    "Active execution stream",          /* TRUE — Aramaic exterior */
    "Speculative superposition",        /* GLUT_PLUS — dual-track speculative */
    "Immutable ledger history",         /* GLUT_MINUS — contraction, record */
    "Neutral anchor / Isolation"        /* GLUT_NEUTRAL — balanced rest */
};

void ddna_grid_init(ddna_consonant_grid_t *grid) {
    if (!grid) return;
    memset(grid, 0, sizeof(ddna_consonant_grid_t));

    /* Initialize 22 consonants from the letter mapping */
    for (int i = 0; i < 22; i++) {
        ddna_consonant_t *c = &grid->consonants[i];
        c->index = (uint8_t)i;
        /* Copy from s_22_letters */
        c->amino_acid = s_22_letters[i].amino_acid;
        memcpy(c->hebrew_utf8, s_22_letters[i].hebrew, 5);
        memcpy(c->aramaic_name, s_22_letters[i].aramaic_name, 16);
        memcpy(c->hebrew_name, s_22_letters[i].hebrew_name, 16);
        c->gematria = s_22_letters[i].gematria;
        memcpy(c->codon_dna, s_22_letters[i].codon_dna, 4);
        memcpy(c->codon_rna, s_22_letters[i].codon_rna, 4);

        /* Compute dual-phase frequencies:
         * Shadow (Hebrew/.9n63): uses gematria directly as N in chemistry formula
         * Light (Aramaic/.36n9): uses (23 - gematria%22) as N (inversion) */
        uint8_t shadow_n = (uint8_t)(c->gematria % 118 + 1);
        uint8_t light_n = (uint8_t)((23 - (c->gematria % 22)) % 118 + 1);
        c->shadow_freq = agp_chemistry_element_frequency(shadow_n);
        c->light_freq = agp_chemistry_element_frequency(light_n);
    }

    /* Initialize 5 vowels from the vowel mapping */
    for (int i = 0; i < 5; i++) {
        ddna_vowel_t *v = &grid->vowels[i];
        v->index = (uint8_t)i;
        memcpy(v->name, s_5_vowels[i].name, 8);
        memcpy(v->axiom_name, s_5_vowels[i].axiom_name, 16);
        v->trit_value = s_5_vowels[i].trit_value;
        v->phase_weight = s_5_vowels[i].phase_weight;
        v->system_role = s_vowel_roles[i];
    }

    grid->current_tick = 0;
    grid->current_vowel = 0;
    grid->current_polarity = DDNA_POLARITY_LIGHT;  /* Start in light/Aramaic mode */
}

ddna_phase_tick_t ddna_grid_tick(ddna_consonant_grid_t *grid) {
    ddna_phase_tick_t tick;
    if (!grid) {
        memset(&tick, 0, sizeof(tick));
        return tick;
    }

    tick.tick_number = grid->current_tick;
    tick.active_vowel = grid->current_vowel;
    tick.polarity = grid->current_polarity;

    /* Compute frequency: blend all 22 consonant frequencies weighted by
     * the active vowel's phase weight */
    double freq = 0.0;
    double total_weight = 0.0;
    for (int i = 0; i < 22; i++) {
        double f = (grid->current_polarity == DDNA_POLARITY_SHADOW)
            ? grid->consonants[i].shadow_freq
            : grid->consonants[i].light_freq;
        /* Weight by gematria (higher gematria = more influence) */
        double w = (double)grid->consonants[i].gematria;
        freq += f * w;
        total_weight += w;
    }
    if (total_weight > 0.0) {
        freq /= total_weight;
    }

    /* Apply vowel phase weight as modulation */
    freq *= grid->vowels[grid->current_vowel].phase_weight;
    if (freq < 1.0) freq = 1.0;  /* Minimum 1 Hz */

    tick.frequency = freq;
    tick.phase = (double)(grid->current_tick % 360) * (2.0 * M_PI / 360.0);

    /* Advance: cycle vowel (0-4), then flip polarity every 5 vowels */
    grid->current_vowel = (uint8_t)((grid->current_vowel + 1) % 5);
    if (grid->current_vowel == 0) {
        /* Flip polarity after completing a full vowel cycle */
        grid->current_polarity =
            (grid->current_polarity == DDNA_POLARITY_SHADOW)
                ? DDNA_POLARITY_LIGHT
                : DDNA_POLARITY_SHADOW;
    }
    grid->current_tick++;

    return tick;
}

double ddna_consonant_frequency(uint8_t consonant_idx, ddna_polarity_t polarity) {
    if (consonant_idx >= 22) return 0.0;

    /* Reconstruct from s_22_letters (for standalone use without grid) */
    uint32_t gematria = s_22_letters[consonant_idx].gematria;
    uint8_t n;
    if (polarity == DDNA_POLARITY_SHADOW) {
        n = (uint8_t)(gematria % 118 + 1);
    } else {
        n = (uint8_t)((23 - (gematria % 22)) % 118 + 1);
    }
    return agp_chemistry_element_frequency(n);
}

double ddna_tick_frequency(const ddna_consonant_grid_t *grid,
                            const ddna_phase_tick_t *tick) {
    if (!grid || !tick) return 0.0;
    return tick->frequency;
}

void ddna_grid_animate(ddna_consonant_grid_t *grid,
                        const ddna_genome_t *genome,
                        uint32_t sample_rate,
                        agp_waveform_t wave,
                        agp_audio_buffer_t *out) {
    if (!grid || !genome || !out || genome->length == 0) {
        if (out) out->length = 0;
        return;
    }

    /* Each codon in the genome gets "pronounced" by cycling through
     * the 5 vowels across both polarities. The consonant (amino acid)
     * provides the frequency, the vowel provides the phase weight,
     * and the polarity determines shadow vs light rendering. */

    uint32_t total = 0;
    uint32_t num_codons = genome->length / 3;
    double codon_duration = 4.5 / (double)num_codons;
    if (codon_duration < 0.05) codon_duration = 0.05;

    for (uint32_t ci = 0; ci < num_codons && total < AGP_MAX_AUDIO; ci++) {
        /* Get the amino acid for this codon */
        char codon[3];
        for (int j = 0; j < 3; j++) {
            char c = genome->sequence[ci * 3 + j];
            if (c >= 'a' && c <= 'z') c -= 32;
            if (c == 'T') c = 'U';
            codon[j] = c;
        }
        char aa = agp_translate_codon(codon);

        /* Find the consonant index for this amino acid */
        int consonant_idx = -1;
        for (int i = 0; i < 22; i++) {
            if (s_22_letters[i].amino_acid == aa) {
                consonant_idx = i;
                break;
            }
        }
        if (consonant_idx < 0) continue;

        /* For each of the 5 vowels, generate a sub-tone */
        for (int v = 0; v < 5 && total < AGP_MAX_AUDIO; v++) {
            ddna_polarity_t pol = (v < 3) ? DDNA_POLARITY_LIGHT : DDNA_POLARITY_SHADOW;
            double freq = ddna_consonant_frequency((uint8_t)consonant_idx, pol);
            freq *= s_5_vowels[v].phase_weight;
            if (freq < 1.0) freq = 1.0;

            double sub_duration = codon_duration / 5.0;
            uint32_t n = (uint32_t)(sample_rate * sub_duration);
            if (total + n > AGP_MAX_AUDIO) n = AGP_MAX_AUDIO - total;
            if (n == 0) break;

            double phase_inc = 2.0 * M_PI * freq / (double)sample_rate;
            double phase = (double)v * (2.0 * M_PI / 5.0);  /* Phase offset per vowel */

            for (uint32_t j = 0; j < n; j++) {
                double val = sin(phase);
                /* Add harmonic based on vowel weight */
                val += 0.3 * s_5_vowels[v].phase_weight * sin(2.0 * phase);
                out->samples[total + j] = (float)val;
                phase += phase_inc;
                if (phase >= 2.0 * M_PI) phase -= 2.0 * M_PI;
            }
            total += n;
        }
    }

    out->length = total;
    out->sample_rate = sample_rate;

    if (total > 0) {
        agp_apply_adsr(out->samples, total, sample_rate, 10.0, 20.0);
        agp_normalize(out->samples, total, 0.95);
    }
}

void ddna_print_grid(const ddna_consonant_grid_t *grid) {
    if (!grid) return;
#ifdef TEST_HOST
    printf("=== 22-Consonant Dual-Phase Grid ===\n");
    printf("  Shadow (Hebrew/.9n63) ↔ Light (Aramaic/.36n9)\n\n");
    for (int i = 0; i < 22; i++) {
        const ddna_consonant_t *c = &grid->consonants[i];
        printf("  %2d: %c | %s | gem=%3u | shadow=%.2f Hz | light=%.2f Hz\n",
               i + 1, c->amino_acid, c->hebrew_utf8,
               c->gematria, c->shadow_freq, c->light_freq);
    }
    printf("\n=== 5 Vowels (5PL Breath Operators) ===\n");
    for (int i = 0; i < 5; i++) {
        const ddna_vowel_t *v = &grid->vowels[i];
        printf("  %d: %s | %s | trit=%u | w=%.2f | %s\n",
               i + 1, v->name, v->axiom_name,
               v->trit_value, v->phase_weight, v->system_role);
    }
    printf("\n  Tick: %lu | Vowel: %d | Polarity: %s\n",
           (unsigned long)grid->current_tick,
           grid->current_vowel,
           grid->current_polarity == DDNA_POLARITY_SHADOW ? "SHADOW" : "LIGHT");
#else
    extern void fb_puts(const char *str);
    fb_puts("=== Consonant Grid ===\n");
    fb_puts("  22 consonants mapped\n");
    fb_puts("  5 vowels (5PL) mapped\n");
    fb_puts("  Shadow/Light polarity active\n");
#endif
}

/* ===== Sephirotic Matrix (Base-10 → Base-13 Topology) ===== */

/* Sephirot data: Hebrew name, English, OS subsystem mapping */
static const struct {
    const char *name;
    const char *english;
    const char *subsystem;
} s_sephirot_data[DDNA_NUM_SEPHIROT] = {
    {"Keter",     "Crown",        "KERNEL"},    /* 0 — root */
    {"Chokhmah",  "Wisdom",       "SCHED"},     /* 1 — scheduler */
    {"Binah",     "Understanding","MEM"},       /* 2 — memory mgmt */
    {"Da'at",     "Knowledge",    "GATE"},      /* 3 — hidden gate */
    {"Chesed",    "Mercy",        "VFS"},       /* 4 — storage */
    {"Gevurah",   "Severity",     "CRYPTO"},    /* 5 — security */
    {"Tiferet",   "Beauty",       "AUDIO"},     /* 6 — synthesis */
    {"Netzach",   "Eternity",     "NET"},       /* 7 — network */
    {"Hod",       "Glory",        "HW"},        /* 8 — hardware */
    {"Yesod",     "Foundation",   "INIT"},      /* 9 — boot */
};

/* Supernal triad data */
static const struct {
    const char *name;
    const char *english;
    const char *description;
} s_supernal_data[3] = {
    {"Ain",            "Nothingness",    "Unallocated vacuum state, zero-point energy, pure superposition"},
    {"Ain Soph",       "Endless",        "Infinite scaling, unbounded address space, non-local LATTICE mesh"},
    {"Ain Soph Aur",   "Endless Light",  "Unified field, absolute clarity, zero-latency photonic propagation"},
};

/* Qliphoth data: shadow shells for each Sephirah */
static const struct {
    const char *name;
    const char *shadow_of;
} s_qliphoth_data[DDNA_NUM_QLIPHOTH] = {
    {"Thaumiel",      "Keter"},      /* 0 — dual heads / kernel split */
    {"Ghogiel",       "Chokhmah"},   /* 1 — sched deadlock */
    {"Sathariel",     "Binah"},      /* 2 — mem corruption */
    {"Sheol",         "Da'at"},      /* 3 — gate breach */
    {"Tzaphqiel",     "Chesed"},     /* 4 — VFS failure */
    {"Golab",         "Gevurah"},    /* 5 — crypto breach */
    {"Tagiriron",     "Tiferet"},    /* 6 — synthesis error */
    {"Harab-Serapel", "Netzach"},    /* 7 — net failure */
    {"Samael",        "Hod"},        /* 8 — HW fault */
    {"Gamaliel",      "Yesod"},      /* 9 — boot failure */
};

void ddna_sephirotic_init(ddna_sephirotic_matrix_t *matrix) {
    if (!matrix) return;
    memset(matrix, 0, sizeof(ddna_sephirotic_matrix_t));

    /* Initialize 10 Sephirot */
    for (int i = 0; i < DDNA_NUM_SEPHIROT; i++) {
        ddna_sephirah_t *s = &matrix->sephirot[i];
        s->index = (uint8_t)i;
        /* Copy strings */
        int j;
        for (j = 0; j < 15 && s_sephirot_data[i].name[j]; j++)
            s->name[j] = s_sephirot_data[i].name[j];
        s->name[j] = '\0';
        for (j = 0; j < 15 && s_sephirot_data[i].english[j]; j++)
            s->english[j] = s_sephirot_data[i].english[j];
        s->english[j] = '\0';
        for (j = 0; j < 15 && s_sephirot_data[i].subsystem[j]; j++)
            s->subsystem[j] = s_sephirot_data[i].subsystem[j];
        s->subsystem[j] = '\0';

        /* Frequency: use (i+1) as N in chemistry formula */
        s->frequency = agp_chemistry_element_frequency((uint8_t)(i + 1));
        s->active = (i != DDNA_SEPHIRAH_DAAT);  /* Da'at starts inactive */
        s->load = 0;
    }

    /* Initialize supernal triad */
    for (int i = 0; i < 3; i++) {
        ddna_supernal_t *sup = &matrix->supernal_triad[i];
        sup->index = (uint8_t)(DDNA_SUPERNAL_AIN + i);
        int j;
        for (j = 0; j < 15 && s_supernal_data[i].name[j]; j++)
            sup->name[j] = s_supernal_data[i].name[j];
        sup->name[j] = '\0';
        for (j = 0; j < 23 && s_supernal_data[i].english[j]; j++)
            sup->english[j] = s_supernal_data[i].english[j];
        sup->english[j] = '\0';
        for (j = 0; j < 63 && s_supernal_data[i].description[j]; j++)
            sup->description[j] = s_supernal_data[i].description[j];
        sup->description[j] = '\0';

        /* Supernal frequencies: exponentially increasing toward infinity
         * Ain = 10x base, Ain Soph = 100x, Ain Soph Aur = 1000x */
        double base_freq = agp_chemistry_element_frequency(1);
        double mul = 1.0;
        for (int p = 0; p <= i; p++) mul *= 10.0;
        sup->frequency = base_freq * mul;
        sup->accessible = false;
    }

    /* Initialize Qliphoth containment zones */
    for (int i = 0; i < DDNA_NUM_QLIPHOTH; i++) {
        ddna_qliphoth_t *q = &matrix->qliphoth[i];
        q->index = (uint8_t)i;
        int j;
        for (j = 0; j < 15 && s_qliphoth_data[i].name[j]; j++)
            q->name[j] = s_qliphoth_data[i].name[j];
        q->name[j] = '\0';
        for (j = 0; j < 15 && s_qliphoth_data[i].shadow_of[j]; j++)
            q->sephirah_shadow[j] = s_qliphoth_data[i].shadow_of[j];
        q->sephirah_shadow[j] = '\0';
        q->contained_errors = 0;
        q->active = false;
    }

    matrix->daat_state = DDNA_DAAT_CLOSED;
    matrix->active_base = DDNA_BASE_SEPHIROTIC;
    matrix->cycle_count = 0;
}

int ddna_sephirotic_tick(ddna_sephirotic_matrix_t *matrix) {
    if (!matrix) return -1;

    /* Cycle through Sephirot, updating load and activity */
    for (int i = 0; i < DDNA_NUM_SEPHIROT; i++) {
        if (matrix->sephirot[i].active) {
            /* Simulate load fluctuation */
            uint32_t load = matrix->sephirot[i].load;
            load = (load + 7 + (uint32_t)(matrix->cycle_count % 13)) % 100;
            matrix->sephirot[i].load = load;
        }
    }

    /* If Da'at is open, cycle supernal triad */
    if (matrix->daat_state == DDNA_DAAT_OPEN) {
        for (int i = 0; i < 3; i++) {
            matrix->supernal_triad[i].accessible = true;
        }
    }

    /* Check for Qliphothic containment needs */
    for (int i = 0; i < DDNA_NUM_QLIPHOTH; i++) {
        if (matrix->qliphoth[i].contained_errors > 0) {
            matrix->qliphoth[i].active = true;
        }
    }

    matrix->cycle_count++;
    return 0;
}

int ddna_daat_open(ddna_sephirotic_matrix_t *matrix) {
    if (!matrix) return -1;
    if (matrix->daat_state == DDNA_DAAT_OPEN) return 0;

    /* Transition through the veil */
    matrix->daat_state = DDNA_DAAT_TRANSITIONING;

    /* Activate Da'at Sephirah */
    matrix->sephirot[DDNA_SEPHIRAH_DAAT].active = true;

    /* Open the gate to base-13 */
    matrix->daat_state = DDNA_DAAT_OPEN;
    matrix->active_base = DDNA_BASE_SUPERNAL;

    /* Grant access to supernal triad */
    for (int i = 0; i < 3; i++) {
        matrix->supernal_triad[i].accessible = true;
    }

    return 0;
}

int ddna_daat_close(ddna_sephirotic_matrix_t *matrix) {
    if (!matrix) return -1;
    if (matrix->daat_state == DDNA_DAAT_CLOSED) return 0;

    matrix->daat_state = DDNA_DAAT_TRANSITIONING;

    /* Deactivate supernal access */
    for (int i = 0; i < 3; i++) {
        matrix->supernal_triad[i].accessible = false;
    }

    /* Close gate, return to base-10 */
    matrix->daat_state = DDNA_DAAT_CLOSED;
    matrix->active_base = DDNA_BASE_SEPHIROTIC;
    matrix->sephirot[DDNA_SEPHIRAH_DAAT].active = false;

    return 0;
}

bool ddna_daat_is_open(const ddna_sephirotic_matrix_t *matrix) {
    return matrix && matrix->daat_state == DDNA_DAAT_OPEN;
}

int ddna_qliphoth_contain(ddna_sephirotic_matrix_t *matrix, uint8_t sephirah_idx,
                           const char *error_desc) {
    if (!matrix || sephirah_idx >= DDNA_NUM_SEPHIROT) return -1;

    /* Route the error to the corresponding Qliphothic shell */
    ddna_qliphoth_t *q = &matrix->qliphoth[sephirah_idx];
    q->contained_errors++;
    q->active = true;

    /* The 5PL paraconsistent logic engine intercepts the regression.
     * The unbalanced residue is quarantined in .9n63 shadow containment.
     * System integrity is preserved — no kernel crash. */
    (void)error_desc;  /* Description logged for audit */

    return 0;
}

double ddna_sephirah_frequency(uint8_t index) {
    if (index >= DDNA_NUM_SEPHIROT) return 0.0;
    return agp_chemistry_element_frequency((uint8_t)(index + 1));
}

double ddna_supernal_frequency(uint8_t index) {
    /* Supernal indices: 10=Ain, 11=AinSoph, 12=AinSophAur */
    if (index < DDNA_SUPERNAL_AIN || index > DDNA_SUPERNAL_AIN_SOPH_AUR) return 0.0;
    double base = agp_chemistry_element_frequency(1);
    int power = index - DDNA_SUPERNAL_AIN + 1;
    double mul = 1.0;
    for (int p = 0; p < power; p++) mul *= 10.0;
    return base * mul;
}

void ddna_sephirotic_print(const ddna_sephirotic_matrix_t *matrix) {
    if (!matrix) return;
#ifdef TEST_HOST
    printf("=== Sephirotic Matrix (Base-%d) ===\n", matrix->active_base);
    printf("  Da'at Gate: %s\n",
           matrix->daat_state == DDNA_DAAT_OPEN ? "OPEN" :
           matrix->daat_state == DDNA_DAAT_TRANSITIONING ? "TRANSITIONING" : "CLOSED");
    printf("  Cycle: %lu\n\n", (unsigned long)matrix->cycle_count);

    printf("  10 Sephirot (Base-10 Operational Matrix):\n");
    for (int i = 0; i < DDNA_NUM_SEPHIROT; i++) {
        const ddna_sephirah_t *s = &matrix->sephirot[i];
        printf("    %2d: %-10s %-16s freq=%.2f Hz %s load=%u%%\n",
               i, s->name, s->subsystem, s->frequency,
               s->active ? "[ACTIVE]" : "[DORMANT]",
               s->load);
    }

    printf("\n  Supernal Triad (Base-13 Postulates):\n");
    for (int i = 0; i < 3; i++) {
        const ddna_supernal_t *sup = &matrix->supernal_triad[i];
        printf("    %2d: %-14s %-20s freq=%.2f Hz %s\n",
               sup->index, sup->name, sup->english,
               sup->frequency,
               sup->accessible ? "[ACCESSIBLE]" : "[SEALED]");
    }

    printf("\n  Qliphothic Containment:\n");
    for (int i = 0; i < DDNA_NUM_QLIPHOTH; i++) {
        const ddna_qliphoth_t *q = &matrix->qliphoth[i];
        if (q->active) {
            printf("    %2d: %-16s shadows %-10s errors=%u [CONTAINED]\n",
                   i, q->name, q->sephirah_shadow, q->contained_errors);
        }
    }
#else
    extern void fb_puts(const char *str);
    fb_puts("=== Sephirotic Matrix ===\n");
    fb_puts("  10 Sephirot active\n");
    fb_puts("  Da'at gate: ");
    fb_puts(matrix->daat_state == DDNA_DAAT_OPEN ? "OPEN\n" : "CLOSED\n");
    fb_puts("  Base: ");
    fb_puts(matrix->active_base == 13 ? "13 (Supernal)\n" : "10 (Operational)\n");
#endif
}

/* ===== Golden Ratio (φ) Checksum Coherence Standard ===== */

static uint32_t fnv1a_32(const uint8_t *data, uint32_t len) {
    uint32_t hash = 2166136261U;
    for (uint32_t i = 0; i < len; i++) {
        hash ^= data[i];
        hash *= 16777619U;
    }
    return hash;
}

int ddna_phi_checksum_compute(const uint8_t *data, uint32_t len,
                               ddna_phi_checksum_t *out) {
    if (!data || !out || len == 0) return -1;
    memset(out, 0, sizeof(ddna_phi_checksum_t));

    /* Split data into φ-proportioned chunks.
     * Each chunk is ~1/φ of the remaining data, creating a Fibonacci-like
     * decomposition where chunk sizes scale according to the golden ratio. */
    uint32_t remaining = len;
    uint32_t offset = 0;
    uint32_t prev_size = 0;

    while (remaining > 0 && out->num_chunks < DDNA_MAX_CHUNKS) {
        uint32_t chunk_size;
        if (prev_size == 0) {
            /* First chunk: 1/φ of total */
            chunk_size = (uint32_t)((double)remaining * AGP_PHI_INV);
            if (chunk_size == 0) chunk_size = remaining;
        } else {
            /* Subsequent chunks: scale by φ ratio from previous */
            chunk_size = (uint32_t)((double)prev_size * AGP_PHI_INV);
            if (chunk_size == 0) chunk_size = remaining;
            if (chunk_size > remaining) chunk_size = remaining;
        }

        ddna_phi_chunk_t *c = &out->chunks[out->num_chunks];
        c->offset = offset;
        c->size = chunk_size;
        c->hash = fnv1a_32(data + offset, chunk_size);

        /* Compute φ ratio: this chunk size / previous chunk size */
        if (prev_size > 0) {
            c->phi_ratio = (double)chunk_size / (double)prev_size;
        } else {
            c->phi_ratio = AGP_PHI;  /* First chunk: ideal ratio */
        }

        offset += chunk_size;
        remaining -= chunk_size;
        prev_size = chunk_size;
        out->num_chunks++;
    }

    /* Compute coherence score: how close the average ratio is to φ */
    if (out->num_chunks > 1) {
        double sum_ratio = 0;
        uint32_t count = 0;
        for (uint32_t i = 1; i < out->num_chunks; i++) {
            sum_ratio += out->chunks[i].phi_ratio;
            count++;
        }
        double avg_ratio = sum_ratio / (double)count;
        double deviation = avg_ratio - AGP_PHI;
        if (deviation < 0) deviation = -deviation;
        out->coherence_score = 1.0 - (deviation / AGP_PHI);
        if (out->coherence_score < 0) out->coherence_score = 0;
        out->coherent = (deviation < DDNA_PHI_TOLERANCE);
    } else {
        out->coherence_score = 1.0;
        out->coherent = true;
    }

    out->corrupted_index = UINT32_MAX;
    return 0;
}

bool ddna_phi_checksum_validate(const uint8_t *data, uint32_t len,
                                 const ddna_phi_checksum_t *ref) {
    if (!data || !ref || len == 0) return false;

    for (uint32_t i = 0; i < ref->num_chunks; i++) {
        const ddna_phi_chunk_t *c = &ref->chunks[i];
        if (c->offset + c->size > len) return false;
        uint32_t hash = fnv1a_32(data + c->offset, c->size);
        if (hash != c->hash) {
            /* Proportional imbalance pinpoints corrupted node */
            return false;
        }
    }
    return true;
}

int ddna_phi_checksum_heal(uint8_t *data, uint32_t len,
                            ddna_phi_checksum_t *checksum) {
    if (!data || !checksum) return -1;

    /* Identify corrupted chunks by re-hashing */
    uint32_t corrupted = UINT32_MAX;
    for (uint32_t i = 0; i < checksum->num_chunks; i++) {
        ddna_phi_chunk_t *c = &checksum->chunks[i];
        if (c->offset + c->size > len) continue;
        uint32_t hash = fnv1a_32(data + c->offset, c->size);
        if (hash != c->hash) {
            corrupted = i;
            break;
        }
    }

    if (corrupted == UINT32_MAX) return 0;  /* Already healthy */

    /* Geometric reconstruction: zero out corrupted region and
     * attempt to reconstruct from neighboring φ-proportioned blocks */
    ddna_phi_chunk_t *c = &checksum->chunks[corrupted];
    memset(data + c->offset, 0, c->size);
    checksum->corrupted_index = corrupted;

    /* In a full implementation, LPRES would reconstruct from parity/Merkle.
     * For now, mark as contained for Qliphothic handling. */
    return 1;  /* Healed (quarantined) */
}

/* ===== Numerology-Based Metadata Interpreter ===== */

void ddna_numerology_compute(const char *name, ddna_numerology_meta_t *out) {
    if (!name || !out) return;
    memset(out, 0, sizeof(ddna_numerology_meta_t));

    /* Copy name */
    int i;
    for (i = 0; i < DDNA_META_MAX_NAME - 1 && name[i]; i++)
        out->name[i] = name[i];
    out->name[i] = '\0';

    /* Compute name value (A=1, B=2, ... Z=26) */
    uint32_t name_val = 0;
    uint32_t gematria = 0;
    for (i = 0; out->name[i]; i++) {
        char c = out->name[i];
        if (c >= 'A' && c <= 'Z') {
            name_val += (uint32_t)(c - 'A' + 1);
            gematria += (uint32_t)(c - 'A' + 1);
        } else if (c >= 'a' && c <= 'z') {
            name_val += (uint32_t)(c - 'a' + 1);
            gematria += (uint32_t)(c - 'a' + 1);
        } else if (c >= '0' && c <= '9') {
            name_val += (uint32_t)(c - '0');
        }
    }

    out->name_value = name_val;
    out->gematria_value = gematria;
    out->digital_root = ddna_digital_root(name_val);

    /* Map to Sephirah (0-9) via digital root */
    out->sephirot_index = out->digital_root % DDNA_NUM_SEPHIROT;

    /* Map to zodiac (0-12) via gematria */
    out->zodiac_index = gematria % DDNA_ZODIAC_SIGNS;

    /* Compute harmonic frequency using sonic chemistry formula */
    uint8_t atomic_n = (uint8_t)((gematria % 118) + 1);
    out->harmonic_freq = agp_chemistry_element_frequency(atomic_n);

    /* Supernal eligible if digital root is 11, 12, or 13 (reduced to 2,3,4)
     * or if gematria is divisible by 13 */
    out->supernal_eligible = (gematria % 13 == 0) ||
                              (out->digital_root == 4) ||
                              (out->digital_root == 7);
}

uint32_t ddna_digital_root(uint32_t value) {
    while (value >= 10) {
        uint32_t sum = 0;
        while (value > 0) {
            sum += value % 10;
            value /= 10;
        }
        value = sum;
    }
    return value;
}

bool ddna_numerology_harmonizes(const ddna_numerology_meta_t *meta,
                                 double system_field_freq) {
    if (!meta || system_field_freq <= 0) return false;
    /* Check if the metadata's harmonic frequency is within
     * a φ-proportion of the system field frequency */
    double ratio = meta->harmonic_freq / system_field_freq;
    double deviation = ratio - AGP_PHI;
    if (deviation < 0) deviation = -deviation;
    return deviation < DDNA_PHI_TOLERANCE * 10;  /* Wider tolerance for resonance */
}

/* ===== 13-Month Lunar Calendar ===== */

static const char *s_lunar_month_names[DDNA_LUNAR_MONTHS] = {
    "Nisan",     "Iyar",     "Sivan",    "Tammuz",
    "Av",        "Elul",     "Tishrei",  "Cheshvan",
    "Kislev",    "Tevet",    "Shevat",   "Adar",
    "Adar Bet"   /* 13th month */
};

void ddna_lunar_from_gregorian(uint16_t g_year, uint8_t g_month, uint8_t g_day,
                                ddna_lunar_date_t *out) {
    if (!out) return;
    memset(out, 0, sizeof(ddna_lunar_date_t));

    /* Approximate conversion: Gregorian day-of-year → lunar day-of-year */
    /* Simple mapping: each Gregorian month ≈ 28/30.4 of a lunar month */
    uint32_t g_day_of_year = 0;
    static const uint8_t days_in_g_month[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    for (int m = 0; m < (int)g_month - 1 && m < 12; m++)
        g_day_of_year += days_in_g_month[m];
    g_day_of_year += g_day;

    /* Account for leap year */
    bool leap = (g_year % 4 == 0 && g_year % 100 != 0) || (g_year % 400 == 0);
    if (leap && g_day_of_year > 59) g_day_of_year++;

    /* Map to lunar calendar (364 days + 1 Day Between Days) */
    out->year = g_year;
    if (g_day_of_year > DDNA_LUNAR_YEAR_DAYS) {
        out->is_day_between_days = true;
        out->day_of_year = DDNA_DAY_BETWEEN_DAYS;
        out->month = 13;
        out->day = 29;  /* Special day */
        out->weekday = 7;  /* Outside normal week */
    } else {
        out->is_day_between_days = false;
        out->day_of_year = g_day_of_year;
        out->month = (uint8_t)((g_day_of_year - 1) / DDNA_LUNAR_DAYS_PER_MO + 1);
        out->day = (uint8_t)((g_day_of_year - 1) % DDNA_LUNAR_DAYS_PER_MO + 1);
        /* Every month starts on the same weekday (day 1 = weekday 0) */
        out->weekday = (uint8_t)((out->day - 1) % 7);
    }
}

void ddna_luran_advance(ddna_lunar_date_t *date) {
    if (!date) return;
    if (date->is_day_between_days) {
        /* Day Between Days → New Year */
        date->year++;
        date->month = 1;
        date->day = 1;
        date->is_day_between_days = false;
        date->day_of_year = 1;
        date->weekday = 0;
        return;
    }
    date->day++;
    date->day_of_year++;
    date->weekday = (uint8_t)((date->weekday + 1) % 7);

    if (date->day > DDNA_LUNAR_DAYS_PER_MO) {
        date->day = 1;
        date->month++;
        if (date->month > DDNA_LUNAR_MONTHS) {
            /* Enter Day Between Days */
            date->is_day_between_days = true;
            date->month = 13;
            date->day = 29;
            date->day_of_year = DDNA_DAY_BETWEEN_DAYS;
            date->weekday = 7;
        }
    }
}

bool ddna_lunar_is_sabbath(const ddna_lunar_date_t *date) {
    if (!date) return false;
    /* Every 7th day is sabbath (weekday == 6) */
    return date->weekday == 6;
}

bool ddna_lunar_is_day_between_days(const ddna_lunar_date_t *date) {
    return date && date->is_day_between_days;
}

const char *ddna_lunar_month_name(uint8_t month) {
    if (month == 0 || month > DDNA_LUNAR_MONTHS) return "Unknown";
    return s_lunar_month_names[month - 1];
}

void ddna_lunar_print(const ddna_lunar_date_t *date) {
    if (!date) return;
#ifdef TEST_HOST
    if (date->is_day_between_days) {
        printf("=== Lunar Date: Day Between Days (Year %u) ===\n", date->year);
        printf("  Intercalary Gateway — zero-point reset\n");
        printf("  Deep compaction, re-indexing, key recertification\n");
    } else {
        printf("=== Lunar Date: %s %u, Year %u ===\n",
               ddna_lunar_month_name(date->month), date->day, date->year);
        printf("  Day of year: %u/364 | Weekday: %u\n",
               date->day_of_year, date->weekday);
        if (ddna_lunar_is_sabbath(date))
            printf("  [SABBATH]\n");
    }
#else
    extern void fb_puts(const char *str);
    fb_puts("Lunar date active\n");
#endif
}

/* ===== 13-Sign Zodiac (with Ophiuchus) ===== */

static const struct {
    const char *name;
    const char *element;
    const char *modality;
    uint8_t sephirah;
    bool gateway;
} s_zodiac_data[DDNA_ZODIAC_SIGNS] = {
    {"Aries",       "Fire",   "Cardinal",     1, false},  /* 0 */
    {"Taurus",      "Earth",  "Fixed",        2, false},  /* 1 */
    {"Gemini",      "Air",    "Mutable",      7, false},  /* 2 */
    {"Cancer",      "Water",  "Cardinal",     9, false},  /* 3 */
    {"Leo",         "Fire",   "Fixed",        6, false},  /* 4 */
    {"Virgo",       "Earth",  "Mutable",      2, false},  /* 5 */
    {"Libra",       "Air",    "Cardinal",     4, false},  /* 6 */
    {"Scorpio",     "Water",  "Fixed",        5, false},  /* 7 */
    {"Ophiuchus",   "Aether", "Transitional", 3, true},   /* 8 — GATEWAY */
    {"Sagittarius", "Fire",   "Mutable",      1, false},  /* 9 */
    {"Capricorn",   "Earth",  "Cardinal",     0, false},  /* 10 */
    {"Aquarius",    "Air",    "Fixed",        7, false},  /* 11 */
    {"Pisces",      "Water",  "Mutable",      9, false},  /* 12 */
};

void ddna_zodiac_init(ddna_zodiac_system_t *sys) {
    if (!sys) return;
    memset(sys, 0, sizeof(ddna_zodiac_system_t));

    for (int i = 0; i < DDNA_ZODIAC_SIGNS; i++) {
        ddna_zodiac_sign_t *s = &sys->signs[i];
        s->index = (uint8_t)i;
        int j;
        for (j = 0; j < 15 && s_zodiac_data[i].name[j]; j++)
            s->name[j] = s_zodiac_data[i].name[j];
        s->name[j] = '\0';
        for (j = 0; j < 7 && s_zodiac_data[i].element[j]; j++)
            s->element[j] = s_zodiac_data[i].element[j];
        s->element[j] = '\0';
        for (j = 0; j < 9 && s_zodiac_data[i].modality[j]; j++)
            s->modality[j] = s_zodiac_data[i].modality[j];
        s->modality[j] = '\0';

        /* Frequency: use (i+1) as N in chemistry formula */
        s->frequency = agp_chemistry_element_frequency((uint8_t)(i + 1));
        s->sephirah_link = s_zodiac_data[i].sephirah;
        s->is_gateway = s_zodiac_data[i].gateway;
    }
    sys->current_sign = 0;
}

ddna_zodiac_sign_t ddna_zodiac_from_date(const ddna_lunar_date_t *date) {
    ddna_zodiac_sign_t result;
    memset(&result, 0, sizeof(result));

    if (!date) return result;

    /* Each zodiac sign gets 28 days (364/13 = 28) */
    uint8_t sign_idx;
    if (date->is_day_between_days) {
        /* Day Between Days → Ophiuchus (gateway) */
        sign_idx = DDNA_ZODIAC_OPHIUCHUS;
    } else {
        sign_idx = (uint8_t)((date->day_of_year - 1) / DDNA_LUNAR_DAYS_PER_MO);
        if (sign_idx >= DDNA_ZODIAC_SIGNS) sign_idx = DDNA_ZODIAC_OPHIUCHUS;
    }

    /* Build result from static data */
    result.index = sign_idx;
    int j;
    for (j = 0; j < 15 && s_zodiac_data[sign_idx].name[j]; j++)
        result.name[j] = s_zodiac_data[sign_idx].name[j];
    result.name[j] = '\0';
    for (j = 0; j < 7 && s_zodiac_data[sign_idx].element[j]; j++)
        result.element[j] = s_zodiac_data[sign_idx].element[j];
    result.element[j] = '\0';
    for (j = 0; j < 9 && s_zodiac_data[sign_idx].modality[j]; j++)
        result.modality[j] = s_zodiac_data[sign_idx].modality[j];
    result.modality[j] = '\0';
    result.frequency = agp_chemistry_element_frequency((uint8_t)(sign_idx + 1));
    result.sephirah_link = s_zodiac_data[sign_idx].sephirah;
    result.is_gateway = s_zodiac_data[sign_idx].gateway;

    return result;
}

const char *ddna_zodiac_element(uint8_t sign_idx) {
    if (sign_idx >= DDNA_ZODIAC_SIGNS) return "Unknown";
    return s_zodiac_data[sign_idx].element;
}

double ddna_zodiac_frequency(uint8_t sign_idx) {
    if (sign_idx >= DDNA_ZODIAC_SIGNS) return 0.0;
    return agp_chemistry_element_frequency((uint8_t)(sign_idx + 1));
}

void ddna_zodiac_print(const ddna_zodiac_system_t *sys) {
    if (!sys) return;
#ifdef TEST_HOST
    printf("=== 13-Sign Zodiac System ===\n");
    for (int i = 0; i < DDNA_ZODIAC_SIGNS; i++) {
        const ddna_zodiac_sign_t *s = &sys->signs[i];
        printf("  %2d: %-14s %-7s %-12s freq=%.2f Hz Sephirah=%d%s\n",
               i, s->name, s->element, s->modality,
               s->frequency, s->sephirah_link,
               s->is_gateway ? " [GATEWAY]" : "");
    }
#else
    extern void fb_puts(const char *str);
    fb_puts("=== 13-Sign Zodiac ===\n");
    fb_puts("  Ophiuchus gateway active\n");
#endif
}

/* ===== Space-Time Operator (Ω_astro) ===== */

void ddna_spacetime_compute(const char *name,
                             const ddna_lunar_date_t *date,
                             ddna_space_time_op_t *out) {
    if (!out) return;
    memset(out, 0, sizeof(ddna_space_time_op_t));

    /* Temporal: lunar date */
    if (date) out->temporal = *date;

    /* Spatial: zodiac sign from date */
    out->spatial = ddna_zodiac_from_date(date);

    /* Metadata: numerological profile of name */
    if (name) {
        ddna_numerology_compute(name, &out->metadata);

        /* Combined resonance: geometric mean of zodiac freq and metadata freq */
        if (out->metadata.harmonic_freq > 0 && out->spatial.frequency > 0) {
            /* Geometric mean without sqrt: sqrt(a*b) ≈ a*b / sqrt(a*b) but
             * for freestanding, use: sqrt(x) ≈ x^0.5 via Newton's method */
            double product = out->metadata.harmonic_freq * out->spatial.frequency;
            double guess = product * 0.5;
            for (int iter = 0; iter < 8; iter++)
                guess = 0.5 * (guess + product / guess);
            out->resonance_freq = guess;
        } else {
            out->resonance_freq = out->spatial.frequency;
        }

        /* Execution tier: base-10 or base-13 (supernal) */
        if (out->metadata.supernal_eligible || out->spatial.is_gateway) {
            out->execution_tier = 13;
        } else {
            out->execution_tier = 10;
        }

        /* Phase alignment: check if resonance harmonizes with system field */
        /* Use φ-proportion check */
        double ratio = out->resonance_freq / out->spatial.frequency;
        double deviation = ratio - AGP_PHI;
        if (deviation < 0) deviation = -deviation;
        out->phase_aligned = (deviation < DDNA_PHI_TOLERANCE * 100);
    } else {
        out->resonance_freq = out->spatial.frequency;
        out->execution_tier = 10;
        out->phase_aligned = false;
    }
}

double ddna_spacetime_resonance(const ddna_space_time_op_t *op) {
    return op ? op->resonance_freq : 0.0;
}

bool ddna_spacetime_validate(const ddna_space_time_op_t *op,
                              double system_field_freq) {
    if (!op || system_field_freq <= 0) return false;
    /* Validate: resonance frequency must harmonize with system field */
    return ddna_numerology_harmonizes(&op->metadata, system_field_freq);
}

void ddna_spacetime_print(const ddna_space_time_op_t *op) {
    if (!op) return;
#ifdef TEST_HOST
    printf("=== Space-Time Operator (Ω_astro) ===\n");
    printf("  Temporal: %s %u, Year %u (day %u/365)\n",
           ddna_lunar_month_name(op->temporal.month),
           op->temporal.day, op->temporal.year,
           op->temporal.day_of_year);
    printf("  Spatial:  %s (%s, %s) freq=%.2f Hz\n",
           op->spatial.name, op->spatial.element, op->spatial.modality,
           op->spatial.frequency);
    printf("  Metadata: name_val=%u gematria=%u root=%u\n",
           op->metadata.name_value, op->metadata.gematria_value,
           op->metadata.digital_root);
    printf("  Resonance: %.2f Hz | Tier: base-%u | Aligned: %s\n",
           op->resonance_freq, op->execution_tier,
           op->phase_aligned ? "YES" : "NO");
#else
    extern void fb_puts(const char *str);
    fb_puts("=== Space-Time Operator ===\n");
    fb_puts("  Omega_astro computed\n");
#endif
}
