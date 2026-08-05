/* sutra_chiglet.c — Simple keyword/pattern-based NL extractor for
 * SUTRA. Deliberately NOT a general NLU/NLP system -- see sutra.h's
 * chiglet_result_t doc comment for the honestly-scoped contract.
 *
 * Freestanding: no ctype.h/string.h/stdlib.h -- all helpers below are
 * hand-written since this file may be compiled without libc.
 */
#include "sutra.h"

#define CHIGLET_MAX_TOKENS 32
#define CHIGLET_MAX_WORD   31

static bool is_digit_c(char c) { return c >= '0' && c <= '9'; }
static char to_lower_c(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }

static bool word_is_all_digits(const char *w) {
    if (!*w) return false;
    for (const char *p = w; *p; p++) if (!is_digit_c(*p)) return false;
    return true;
}

static bool ieq(const char *a, const char *b) {
    while (*a && *b) {
        if (to_lower_c(*a) != to_lower_c(*b)) return false;
        a++; b++;
    }
    return *a == '\0' && *b == '\0';
}

static int64_t parse_uint(const char *w) {
    int64_t v = 0;
    for (const char *p = w; *p; p++) v = v * 10 + (*p - '0');
    return v;
}

static uint32_t str_len_c(const char *s) { uint32_t n = 0; while (s[n]) n++; return n; }
static bool has_at_sign(const char *s) { for (; *s; s++) if (*s == '@') return true; return false; }

/* Lowercases src into dst (dst must be >= src's length + 1); returns
 * the length written (excluding the terminator). */
static uint32_t lower_copy(char *dst, const char *src, uint32_t max_dst) {
    uint32_t n = 0;
    while (src[n] && n < max_dst - 1) { dst[n] = to_lower_c(src[n]); n++; }
    dst[n] = '\0';
    return n;
}

chiglet_result_t sutra_chiglet_translate(const char *sentence) {
    chiglet_result_t result;
    for (uint8_t *b = (uint8_t *)&result; b < (uint8_t *)&result + sizeof(result); b++) *b = 0;

    char tokens[CHIGLET_MAX_TOKENS][CHIGLET_MAX_WORD + 1];
    uint32_t token_count = 0;
    uint32_t word_len = 0;

    while (*sentence && token_count < CHIGLET_MAX_TOKENS) {
        if (*sentence == ' ' || *sentence == '\t' || *sentence == '\n') {
            if (word_len > 0) {
                tokens[token_count][word_len] = '\0';
                token_count++;
                word_len = 0;
            }
        } else if (word_len < CHIGLET_MAX_WORD) {
            tokens[token_count][word_len++] = *sentence;
        }
        sentence++;
    }
    if (word_len > 0 && token_count < CHIGLET_MAX_TOKENS) {
        tokens[token_count][word_len] = '\0';
        token_count++;
    }

    /* Amount: first purely-numeric token. */
    int32_t amount_idx = -1;
    for (uint32_t i = 0; i < token_count; i++) {
        if (word_is_all_digits(tokens[i])) { amount_idx = (int32_t)i; break; }
    }
    if (amount_idx < 0) return result; /* matched stays false, rest stays zero */
    result.amount.num = parse_uint(tokens[amount_idx]);
    result.amount.den = 1;

    /* Rail inference from currency keyword. */
    result.rail = RAIL_VINO_NATIVE;
    for (uint32_t i = 0; i < token_count; i++) {
        if (ieq(tokens[i], "rupees") || ieq(tokens[i], "rupee")) { result.rail = RAIL_UPI; break; }
        if (ieq(tokens[i], "dollars") || ieq(tokens[i], "dollar")) { result.rail = RAIL_FEDWIRE; break; }
        if (ieq(tokens[i], "euros") || ieq(tokens[i], "euro")) { result.rail = RAIL_SEPA; break; }
        if (ieq(tokens[i], "pix")) { result.rail = RAIL_PIX; break; }
    }

    /* "from <NAME> to <NAME>" pattern. */
    int32_t from_idx = -1, to_idx = -1;
    for (uint32_t i = 0; i + 1 < token_count; i++) {
        if (from_idx < 0 && ieq(tokens[i], "from")) from_idx = (int32_t)(i + 1);
        else if (from_idx >= 0 && to_idx < 0 && ieq(tokens[i], "to")) { to_idx = (int32_t)(i + 1); break; }
    }
    if (from_idx < 0 || to_idx < 0 || (uint32_t)from_idx >= token_count || (uint32_t)to_idx >= token_count) {
        return result; /* matched stays false */
    }

    /* Each name is lowercased and finished INDEPENDENTLY (the
     * generated draft copied both names in one shared loop and broke
     * on whichever ended first, silently truncating the longer one --
     * that bug is why these are two separate calls). */
    uint32_t from_len = lower_copy(result.from_account, tokens[from_idx], sizeof(result.from_account));
    uint32_t to_len = lower_copy(result.to_account, tokens[to_idx], sizeof(result.to_account));

    if (!has_at_sign(result.from_account) && from_len + 5 < sizeof(result.from_account)) {
        const char *suffix = "@vino";
        for (uint32_t i = 0; suffix[i]; i++) result.from_account[from_len + i] = suffix[i];
        result.from_account[from_len + str_len_c(suffix)] = '\0';
    }
    if (!has_at_sign(result.to_account) && to_len + 5 < sizeof(result.to_account)) {
        const char *suffix = "@vino";
        for (uint32_t i = 0; suffix[i]; i++) result.to_account[to_len + i] = suffix[i];
        result.to_account[to_len + str_len_c(suffix)] = '\0';
    }

    result.matched = true;
    result.capital = CAP_FINANCIAL;
    return result;
}
