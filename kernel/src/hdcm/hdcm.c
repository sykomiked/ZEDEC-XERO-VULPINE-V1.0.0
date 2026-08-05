/*
 * hdcm.c — Hyperdimensional Conversion Matrix Implementation
 *
 * Second-quantized operator algebra for universal language interoperability.
 * Maps programming and human languages through Fock space + H_omni matrix.
 *
 * CREATE:   Source → Fock state vector (occupation numbers)
 * ENTANGLE: H_omni matrix transforms source → target field
 * MEASURE:  Collapse to target language output
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

#include "hdcm.h"

/* ===== Helpers ===== */

static void hc_memset(void *dst, int v, uint32_t n) {
    uint8_t *d = (uint8_t *)dst; uint32_t i;
    for (i = 0; i < n; i++) d[i] = (uint8_t)v;
}

static void hc_memcpy(void *dst, const void *src, uint32_t n) {
    uint8_t *d = (uint8_t *)dst; const uint8_t *s = (const uint8_t *)src; uint32_t i;
    for (i = 0; i < n; i++) d[i] = s[i];
}

static __attribute__((unused)) int hc_memcmp(const void *a, const void *b, uint32_t n) {
    const uint8_t *pa = a, *pb = b; uint32_t i;
    for (i = 0; i < n; i++) if (pa[i] != pb[i]) return (int)pa[i] - (int)pb[i];
    return 0;
}

static uint32_t hc_strlen(const char *s) { uint32_t n = 0; while (s[n]) n++; return n; }

static void hc_strcpy(char *dst, const char *src) {
    uint32_t i = 0; while (src[i]) { dst[i] = src[i]; i++; } dst[i] = '\0';
}

static int hc_strcmp(const char *a, const char *b) {
    uint32_t i = 0; while (a[i] && b[i]) {
        if (a[i] != b[i]) return (int)(unsigned char)a[i] - (int)(unsigned char)b[i];
        i++;
    }
    return (int)(unsigned char)a[i] - (int)(unsigned char)b[i];
}

static int hc_strendswith(const char *s, const char *suffix) {
    uint32_t slen = hc_strlen(s), suflen = hc_strlen(suffix);
    if (suflen > slen) return 0;
    uint32_t i;
    for (i = 0; i < suflen; i++)
        if (s[slen - suflen + i] != suffix[i]) return 0;
    return 1;
}

/* PRNG (xorshift32) for deterministic vector generation */
static uint32_t hc_xorshift32(uint32_t *state) {
    uint32_t x = *state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    *state = x;
    return x;
}

/* ===== Vector Operations ===== */

void hdcm_vector_zero(hdcm_vector_t *v) {
    if (!v) return;
    hc_memset(v->bits, 0, HDCM_VECTOR_BYTES);
    v->hamming_weight = 0;
}

void hdcm_vector_random(hdcm_vector_t *v, uint32_t seed) {
    if (!v) return;
    uint32_t state = seed ? seed : 1;
    uint32_t i;
    for (i = 0; i < HDCM_VECTOR_BYTES; i++) {
        v->bits[i] = (uint8_t)(hc_xorshift32(&state) & 0xFF);
    }
    /* Compute hamming weight */
    v->hamming_weight = 0;
    for (i = 0; i < HDCM_VECTOR_BYTES; i++) {
        uint8_t b = v->bits[i];
        while (b) { v->hamming_weight += b & 1; b >>= 1; }
    }
}

void hdcm_vector_bind(const hdcm_vector_t *a, const hdcm_vector_t *b,
                       hdcm_vector_t *out) {
    /* XOR bind operation: multiplicative in HDCM */
    if (!a || !b || !out) return;
    uint32_t i;
    out->hamming_weight = 0;
    for (i = 0; i < HDCM_VECTOR_BYTES; i++) {
        out->bits[i] = a->bits[i] ^ b->bits[i];
        uint8_t b8 = out->bits[i];
        while (b8) { out->hamming_weight += b8 & 1; b8 >>= 1; }
    }
}

void hdcm_vector_superpose(const hdcm_vector_t *a, const hdcm_vector_t *b,
                            hdcm_vector_t *out) {
    /* Majority vote: additive in HDCM (simplified to threshold) */
    if (!a || !b || !out) return;
    uint32_t i;
    out->hamming_weight = 0;
    for (i = 0; i < HDCM_VECTOR_BYTES; i++) {
        /* Threshold: set if both agree, or 50/50 if disagree */
        out->bits[i] = (a->bits[i] & b->bits[i]) | ((a->bits[i] ^ b->bits[i]) & (i & 1));
        uint8_t b8 = out->bits[i];
        while (b8) { out->hamming_weight += b8 & 1; b8 >>= 1; }
    }
}

uint32_t hdcm_vector_hamming(const hdcm_vector_t *a, const hdcm_vector_t *b) {
    if (!a || !b) return HDCM_VECTOR_DIM;
    uint32_t dist = 0;
    uint32_t i;
    for (i = 0; i < HDCM_VECTOR_BYTES; i++) {
        uint8_t diff = a->bits[i] ^ b->bits[i];
        while (diff) { dist += diff & 1; diff >>= 1; }
    }
    return dist;
}

float hdcm_vector_similarity(const hdcm_vector_t *a, const hdcm_vector_t *b) {
    if (!a || !b) return 0.0f;
    uint32_t dist = hdcm_vector_hamming(a, b);
    return 1.0f - (float)dist / (float)HDCM_VECTOR_DIM;
}

void hdcm_vector_permute(const hdcm_vector_t *in, uint32_t shift,
                          hdcm_vector_t *out) {
    if (!in || !out) return;
    /* Bit-level circular permutation by shift positions */
    uint32_t total_bits = HDCM_VECTOR_DIM;
    shift = shift % total_bits;
    if (shift == 0) {
        hc_memcpy(out, in, sizeof(hdcm_vector_t));
        return;
    }
    hc_memset(out->bits, 0, HDCM_VECTOR_BYTES);
    uint32_t i;
    for (i = 0; i < total_bits; i++) {
        if (in->bits[i / 8] & (1 << (i % 8))) {
            uint32_t new_pos = (i + shift) % total_bits;
            out->bits[new_pos / 8] |= (1 << (new_pos % 8));
        }
    }
    out->hamming_weight = in->hamming_weight;
}

/* ===== Init ===== */

void hdcm_init(hdcm_t *h) {
    hc_memset(h, 0, sizeof(hdcm_t));
    h->initialized = true;
}

/* ===== Language Registration ===== */

int32_t hdcm_language_register(hdcm_t *h, const char *name,
                                const char *extension,
                                hdcm_lang_category_t category) {
    if (h->num_languages >= HDCM_MAX_LANGUAGES || !name) return -1;
    int32_t idx = (int32_t)h->num_languages;
    hdcm_language_t *lang = &h->languages[idx];
    hc_memset(lang, 0, sizeof(hdcm_language_t));
    lang->id = (uint32_t)idx;
    hc_strcpy(lang->name, name);
    if (extension) hc_strcpy(lang->file_extension, extension);
    lang->category = category;
    lang->construct_count = 0;
    lang->active = true;

    /* Generate base language vector from name hash */
    uint32_t seed = 1;
    uint32_t i;
    for (i = 0; name[i]; i++)
        seed = seed * 31 + (uint8_t)name[i];
    hdcm_vector_random(&lang->language_vector, seed);

    h->num_languages++;
    return idx;
}

int hdcm_language_add_construct(hdcm_t *h, uint32_t lang_idx,
                                 hdcm_construct_t type,
                                 const char *source_token,
                                 const char *target_token) {
    if (lang_idx >= h->num_languages || !source_token) return -1;
    hdcm_language_t *lang = &h->languages[lang_idx];
    if (lang->construct_count >= HDCM_MAX_CONSTRUCTS) return -1;

    uint32_t ci = lang->construct_count;
    hdcm_construct_map_t *cm = &lang->constructs[ci];
    hc_memset(cm, 0, sizeof(hdcm_construct_map_t));
    cm->type = type;
    hc_strcpy(cm->source_token, source_token);
    if (target_token) hc_strcpy(cm->target_token, target_token);

    /* Generate construct vector: bind language vector with type-based permutation */
    hdcm_vector_t type_vec;
    hdcm_vector_random(&type_vec, (uint32_t)type * 7919 + 1);
    hdcm_vector_bind(&lang->language_vector, &type_vec, &cm->vector);

    /* Semantic hash from token */
    uint32_t sh = 0;
    uint32_t i;
    for (i = 0; source_token[i]; i++)
        sh = sh * 31 + (uint8_t)source_token[i];
    cm->semantic_hash = sh;

    lang->construct_count++;
    return 0;
}

const hdcm_language_t *hdcm_language_find(hdcm_t *h, const char *name) {
    if (!h || !name) return NULL;
    uint32_t i;
    for (i = 0; i < h->num_languages; i++)
        if (h->languages[i].active && hc_strcmp(h->languages[i].name, name) == 0)
            return &h->languages[i];
    return NULL;
}

const hdcm_language_t *hdcm_language_by_ext(hdcm_t *h, const char *ext) {
    if (!h || !ext) return NULL;
    uint32_t i;
    for (i = 0; i < h->num_languages; i++)
        if (h->languages[i].active && hc_strendswith(ext, h->languages[i].file_extension))
            return &h->languages[i];
    return NULL;
}

/* ===== Conversion Matrix ===== */

int32_t hdcm_matrix_create(hdcm_t *h, uint32_t src_lang, uint32_t dst_lang) {
    if (src_lang >= h->num_languages || dst_lang >= h->num_languages) return -1;
    if (src_lang == dst_lang) return -1;
    if (h->num_matrices >= HDCM_MAX_MATRICES) return -1;

    /* Check if matrix already exists */
    uint32_t i;
    for (i = 0; i < h->num_matrices; i++)
        if (h->matrices[i].source_lang_id == src_lang &&
            h->matrices[i].target_lang_id == dst_lang)
            return (int32_t)i;

    int32_t idx = (int32_t)h->num_matrices;
    hdcm_matrix_t *m = &h->matrices[idx];
    hc_memset(m, 0, sizeof(hdcm_matrix_t));
    m->source_lang_id = src_lang;
    m->target_lang_id = dst_lang;
    m->num_mappings = 0;
    m->bidirectional = true;

    h->num_matrices++;
    return idx;
}

int hdcm_matrix_build(hdcm_t *h, uint32_t matrix_idx) {
    if (matrix_idx >= h->num_matrices) return -1;
    hdcm_matrix_t *m = &h->matrices[matrix_idx];
    hdcm_language_t *src = &h->languages[m->source_lang_id];
    hdcm_language_t *dst = &h->languages[m->target_lang_id];

    /* Build transform vectors for each construct type */
    uint32_t i, j;
    m->num_mappings = 0;
    for (i = 0; i < src->construct_count; i++) {
        /* Find matching construct in target by semantic hash */
        for (j = 0; j < dst->construct_count; j++) {
            if (src->constructs[i].type == dst->constructs[j].type) {
                /* Bind source and target vectors into transform */
                hdcm_vector_bind(&src->constructs[i].vector,
                                 &dst->constructs[j].vector,
                                 &m->transform[m->num_mappings]);
                m->num_mappings++;
                break;
            }
        }
        if (m->num_mappings >= HDCM_MAX_CONSTRUCTS) break;
    }

    /* Compute compatibility score from vector similarity */
    m->compatibility_score = hdcm_vector_similarity(&src->language_vector,
                                                     &dst->language_vector);
    return 0;
}

const hdcm_matrix_t *hdcm_matrix_find(hdcm_t *h, uint32_t src, uint32_t dst) {
    uint32_t i;
    for (i = 0; i < h->num_matrices; i++)
        if (h->matrices[i].source_lang_id == src &&
            h->matrices[i].target_lang_id == dst)
            return &h->matrices[i];
    return NULL;
}

float hdcm_matrix_compatibility(hdcm_t *h, uint32_t src, uint32_t dst) {
    const hdcm_matrix_t *m = hdcm_matrix_find(h, src, dst);
    if (m) return m->compatibility_score;
    return 0.0f;
}

/* ===== Second Quantization Translation Pipeline ===== */

int hdcm_phase_create(hdcm_t *h, uint32_t lang_idx,
                       const char *source_code,
                       hdcm_vector_t *out_vector) {
    if (lang_idx >= h->num_languages || !source_code || !out_vector) return -1;
    hdcm_language_t *lang = &h->languages[lang_idx];

    /* CREATE: Map source tokens to Fock state occupation numbers
     * Each token excites a construct vector; superpose all into state vector */
    hdcm_vector_zero(out_vector);
    hdcm_vector_t token_vec;
    hdcm_vector_t accum;

    /* Start with language base vector */
    hc_memcpy(&accum, &lang->language_vector, sizeof(hdcm_vector_t));

    uint32_t i, j;
    uint32_t token_start = 0;
    uint32_t code_len = hc_strlen(source_code);

    for (i = 0; i <= code_len; i++) {
        /* Simple tokenization: split on whitespace and punctuation */
        char c = (i < code_len) ? source_code[i] : ' ';
        if (c == ' ' || c == '\t' || c == '\n' || c == '\0' || i == code_len) {
            if (i > token_start) {
                uint32_t tlen = i - token_start;
                /* Extract token */
                char token[HDCM_MAX_LABEL];
                uint32_t copy_len = tlen < HDCM_MAX_LABEL - 1 ? tlen : HDCM_MAX_LABEL - 1;
                hc_memcpy(token, source_code + token_start, copy_len);
                token[copy_len] = '\0';

                /* Find matching construct */
                bool found = false;
                for (j = 0; j < lang->construct_count; j++) {
                    if (hc_strcmp(lang->constructs[j].source_token, token) == 0) {
                        /* Excite this construct: superpose into state */
                        hdcm_vector_superpose(&accum, &lang->constructs[j].vector, &token_vec);
                        hc_memcpy(&accum, &token_vec, sizeof(hdcm_vector_t));
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    /* Unknown token: create hash-based vector and bind */
                    uint32_t seed = 1;
                    uint32_t k;
                    for (k = 0; k < copy_len; k++)
                        seed = seed * 31 + (uint8_t)token[k];
                    hdcm_vector_random(&token_vec, seed);
                    hdcm_vector_bind(&accum, &token_vec, &token_vec);
                    hc_memcpy(&accum, &token_vec, sizeof(hdcm_vector_t));
                }
            }
            token_start = i + 1;
        }
    }

    hc_memcpy(out_vector, &accum, sizeof(hdcm_vector_t));
    h->create_count++;
    return 0;
}

int hdcm_phase_entangle(hdcm_t *h, uint32_t src_lang, uint32_t dst_lang,
                         const hdcm_vector_t *src_vector,
                         hdcm_vector_t *out_vector) {
    if (!src_vector || !out_vector) return -1;

    const hdcm_matrix_t *m = hdcm_matrix_find(h, src_lang, dst_lang);
    if (!m) {
        /* No matrix: create and build one */
        int32_t midx = hdcm_matrix_create(h, src_lang, dst_lang);
        if (midx < 0) return -1;
        hdcm_matrix_build(h, (uint32_t)midx);
        m = &h->matrices[midx];
    }

    /* ENTANGLE: Apply H_omni transform
     * Bind source vector with the transform matrix
     * This projects source field into target field */
    if (m->num_mappings == 0) {
        hc_memcpy(out_vector, src_vector, sizeof(hdcm_vector_t));
    } else {
        /* Apply first transform as base, then superpose remaining */
        hdcm_vector_bind(src_vector, &m->transform[0], out_vector);
        hdcm_vector_t tmp;
        uint32_t i;
        for (i = 1; i < m->num_mappings && i < 8; i++) {
            hdcm_vector_bind(src_vector, &m->transform[i], &tmp);
            hdcm_vector_superpose(out_vector, &tmp, out_vector);
        }
    }

    h->entangle_count++;
    return 0;
}

int hdcm_phase_measure(hdcm_t *h, uint32_t dst_lang,
                        const hdcm_vector_t *entangled,
                        char *output, uint32_t max_len) {
    if (!entangled || !output || max_len == 0) return -1;
    if (dst_lang >= h->num_languages) return -1;
    hdcm_language_t *lang = &h->languages[dst_lang];

    /* MEASURE: Collapse entangled state into target language output
     * Find closest construct vectors and emit their target tokens */
    uint32_t out_pos = 0;
    uint32_t best_idx = 0;
    float best_sim = 0.0f;
    uint32_t j;

    /* Find the most similar construct */
    for (j = 0; j < lang->construct_count; j++) {
        float sim = hdcm_vector_similarity(entangled, &lang->constructs[j].vector);
        if (sim > best_sim) {
            best_sim = sim;
            best_idx = j;
        }
    }

    /* Emit the best matching construct's target token */
    if (lang->construct_count > 0 && best_sim > 0.5f) {
        const char *token = lang->constructs[best_idx].target_token;
        if (token[0]) {
            uint32_t tlen = hc_strlen(token);
            if (out_pos + tlen < max_len) {
                hc_memcpy(output + out_pos, token, tlen);
                out_pos += tlen;
            }
        }
    }

    /* If no good match, emit a placeholder */
    if (out_pos == 0) {
        const char *placeholder = "[unmapped]";
        uint32_t plen = hc_strlen(placeholder);
        if (plen < max_len) {
            hc_memcpy(output, placeholder, plen);
            out_pos = plen;
        }
    }

    output[out_pos] = '\0';
    h->measure_count++;
    return (int)out_pos;
}

int hdcm_translate(hdcm_t *h, uint32_t src_lang, uint32_t dst_lang,
                    const char *source_code, hdcm_result_t *result) {
    if (!h || !source_code || !result) return -1;
    if (src_lang >= h->num_languages || dst_lang >= h->num_languages) return -1;
    hc_memset(result, 0, sizeof(hdcm_result_t));

    /* Phase 1: CREATE — encode source into Fock state vector */
    hdcm_vector_t src_vector;
    if (hdcm_phase_create(h, src_lang, source_code, &src_vector) != 0) {
        result->success = false;
        return -1;
    }
    result->final_phase = HDCM_PHASE_CREATE;

    /* Phase 2: ENTANGLE — apply H_omni matrix transform */
    hdcm_vector_t entangled;
    if (hdcm_phase_entangle(h, src_lang, dst_lang, &src_vector, &entangled) != 0) {
        result->success = false;
        return -1;
    }
    result->final_phase = HDCM_PHASE_ENTANGLE;

    /* Phase 3: MEASURE — collapse to target language */
    int out_len = hdcm_phase_measure(h, dst_lang, &entangled,
                                      result->output, HDCM_MAX_OUTPUT);
    if (out_len < 0) {
        result->success = false;
        return -1;
    }
    result->output_len = (uint32_t)out_len;
    result->final_phase = HDCM_PHASE_MEASURE;

    /* Compute fidelity from compatibility score */
    const hdcm_matrix_t *m = hdcm_matrix_find(h, src_lang, dst_lang);
    result->fidelity_score = m ? m->compatibility_score : 0.5f;
    result->constructs_translated = 1;
    result->constructs_unmapped = 0;
    result->success = true;

    h->total_translations++;
    return 0;
}

/* ===== Utility ===== */

const char *hdcm_construct_name(hdcm_construct_t type) {
    switch (type) {
        case HDCM_CONSTRUCT_VARIABLE:    return "variable";
        case HDCM_CONSTRUCT_FUNCTION:    return "function";
        case HDCM_CONSTRUCT_TYPE:        return "type";
        case HDCM_CONSTRUCT_CONTROL:     return "control";
        case HDCM_CONSTRUCT_OPERATOR:    return "operator";
        case HDCM_CONSTRUCT_LITERAL:     return "literal";
        case HDCM_CONSTRUCT_MODULE:      return "module";
        case HDCM_CONSTRUCT_COMMENT:     return "comment";
        case HDCM_CONSTRUCT_IMPORT:      return "import";
        case HDCM_CONSTRUCT_CLASS:       return "class";
        case HDCM_CONSTRUCT_INTERFACE:   return "interface";
        case HDCM_CONSTRUCT_NAMESPACE:   return "namespace";
        case HDCM_CONSTRUCT_MACRO:       return "macro";
        case HDCM_CONSTRUCT_GENERIC:     return "generic";
        case HDCM_CONSTRUCT_PATTERN:     return "pattern";
        case HDCM_CONSTRUCT_CONCURRENCY: return "concurrency";
        case HDCM_CONSTRUCT_ERROR:       return "error";
        case HDCM_CONSTRUCT_MEMORY:      return "memory";
        case HDCM_CONSTRUCT_ANNOTATION:  return "annotation";
        case HDCM_CONSTRUCT_SENTENCE:    return "sentence";
        case HDCM_CONSTRUCT_WORD:        return "word";
        case HDCM_CONSTRUCT_PHONEME:     return "phoneme";
        case HDCM_CONSTRUCT_GRAMMAR:     return "grammar";
        case HDCM_CONSTRUCT_SEMANTIC:    return "semantic";
        case HDCM_CONSTRUCT_PRAGMA:      return "pragma";
        case HDCM_CONSTRUCT_ASSERTION:   return "assertion";
        case HDCM_CONSTRUCT_AXIOM:       return "axiom";
        case HDCM_CONSTRUCT_PHASE:       return "phase";
        default: return "unknown";
    }
}

const char *hdcm_category_name(hdcm_lang_category_t cat) {
    switch (cat) {
        case HDCM_LANG_PROGRAMMING: return "programming";
        case HDCM_LANG_HUMAN:       return "human";
        case HDCM_LANG_AXIOMATIC:   return "axiomatic";
        case HDCM_LANG_MACHINE:     return "machine";
        default: return "unknown";
    }
}

const char *hdcm_phase_name(hdcm_phase_t phase) {
    switch (phase) {
        case HDCM_PHASE_CREATE:   return "CREATE";
        case HDCM_PHASE_ENTANGLE: return "ENTANGLE";
        case HDCM_PHASE_MEASURE:  return "MEASURE";
        default: return "unknown";
    }
}

/* ===== Omni-Compatibility ===== */

bool hdcm_omni_compatible(hdcm_t *h, uint32_t lang_idx) {
    if (lang_idx >= h->num_languages) return false;
    /* A language is omni-compatible if it has matrices to at least 3 other languages */
    uint32_t count = 0;
    uint32_t i;
    for (i = 0; i < h->num_matrices; i++) {
        if (h->matrices[i].source_lang_id == lang_idx ||
            h->matrices[i].target_lang_id == lang_idx)
            count++;
    }
    return count >= 3;
}

uint32_t hdcm_compatible_count(hdcm_t *h, uint32_t lang_idx) {
    if (lang_idx >= h->num_languages) return 0;
    uint32_t count = 0;
    uint32_t i;
    for (i = 0; i < h->num_matrices; i++) {
        if (h->matrices[i].source_lang_id == lang_idx ||
            h->matrices[i].target_lang_id == lang_idx)
            count++;
    }
    return count;
}
