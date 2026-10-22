/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* nutshell_model.c — Nutshell, flashcards (see nutshell_model.h). */
#include <string.h>

#include "nutshell_model.h"

/* ---- the rules ---------------------------------------------------------- */

bool nutshell_card_valid(int64_t box, int64_t due, int64_t right, int64_t wrong)
{
    if (box < 1 || box > NUTSHELL_BOX_MAX || due < 0 || due > SOLSTICE_DAY_MAX) return false;
    if (right < 0 || right > NUTSHELL_TALLY_MAX || wrong < 0 || wrong > NUTSHELL_TALLY_MAX)
        return false;
    return nutshell_review_fits(1, 1, box, due, right, wrong); /* NS5 */
}

/* The length of the UTF-8 character at s (RFC 3629: no overlong form, no
 * surrogate, nothing past U+10FFFF), or 0 when it is not one. */
static uint32_t utf8_len(const unsigned char *s, uint32_t n)
{
    unsigned char c = s[0];
    uint32_t k;
    uint32_t lo = 0x80u, hi = 0xBFu;
    if (c < 0x80u) return 1u;
    if (c >= 0xC2u && c <= 0xDFu)
        k = 2u;
    else if (c >= 0xE0u && c <= 0xEFu)
        k = 3u;
    else if (c >= 0xF0u && c <= 0xF4u)
        k = 4u;
    else
        return 0;
    if (k > n) return 0;
    if (c == 0xE0u) lo = 0xA0u;
    if (c == 0xEDu) hi = 0x9Fu;
    if (c == 0xF0u) lo = 0x90u;
    if (c == 0xF4u) hi = 0x8Fu;
    if (s[1] < lo || s[1] > hi) return 0;
    for (uint32_t i = 2u; i < k; i++)
        if (s[i] < 0x80u || s[i] > 0xBFu) return 0;
    return k;
}

bool nutshell_side_ok(const char *side)
{
    if (!side) return false;
    size_t len = strlen(side);
    if (len < 1u || len > NUTSHELL_SIDE_MAX) return false;
    const unsigned char *s = (const unsigned char *) side;
    uint32_t n = (uint32_t) len;
    for (uint32_t i = 0; i < n;) {
        if (s[i] < 0x20u || s[i] == 0x7fu) return false;
        uint32_t k = utf8_len(s + i, n - i);
        if (!k) return false; /* zxvd keeps text as UTF-8 only */
        i += k;
    }
    return true;
}

/* ---- a deck as text (NM1) ------------------------------------------------ */

static const char HEAD[] = "nutshell 1\n";

/* Append s to out at *at, bounded by cap (room for a NUL is kept). */
static bool put(char *out, uint32_t cap, uint32_t *at, const char *s)
{
    uint32_t n = (uint32_t) strlen(s);
    if (*at + n + 1u > cap) return false;
    memcpy(out + *at, s, n);
    *at += n;
    out[*at] = 0;
    return true;
}

/* v >= 0 in plain decimal. */
static bool put_num(char *out, uint32_t cap, uint32_t *at, int64_t v)
{
    char t[24];
    int k = 23;
    uint64_t u = v < 0 ? 0u : (uint64_t) v;
    t[k] = 0;
    do {
        t[--k] = (char) ('0' + (int) (u % 10u));
        u /= 10u;
    } while (u && k > 0);
    return put(out, cap, at, t + k);
}

static bool path_of(int64_t deck, char *out, uint32_t cap)
{
    uint32_t at = 0;
    return put(out, cap, &at, NUTSHELL_DIR "/") && put_num(out, cap, &at, deck) &&
           put(out, cap, &at, ".deck");
}

/* The whole deck s, with `n` cards from c, made and reviewed as given. */
static int32_t compose(const nutshell_card_t *c, uint32_t n, int64_t made, int64_t reviewed,
                       char *out, uint32_t cap)
{
    uint32_t at = 0;
    bool ok = put(out, cap, &at, HEAD) && put_num(out, cap, &at, made) && put(out, cap, &at, " ") &&
              put_num(out, cap, &at, reviewed) && put(out, cap, &at, "\n");
    for (uint32_t i = 0; i < n && ok; i++) {
        const nutshell_card_t *k = &c[i];
        ok = put_num(out, cap, &at, k->box) && put(out, cap, &at, " ") &&
             put_num(out, cap, &at, k->due) && put(out, cap, &at, " ") &&
             put_num(out, cap, &at, k->right) && put(out, cap, &at, " ") &&
             put_num(out, cap, &at, k->wrong) && put(out, cap, &at, "\t") &&
             put(out, cap, &at, k->front) && put(out, cap, &at, "\t") &&
             put(out, cap, &at, k->back) && put(out, cap, &at, "\n");
    }
    return ok ? (int32_t) at : -1;
}

/* A plain decimal of 1 to `most` digits, no leading zero, at s[*at],
 * ended by `end`. */
static bool get_num(const char *s, uint32_t n, uint32_t *at, char end, uint32_t most, int64_t *v)
{
    uint32_t i = *at, digits = 0;
    int64_t x = 0;
    while (i < n && s[i] >= '0' && s[i] <= '9' && digits <= most) {
        x = x * 10 + (s[i] - '0');
        i++;
        digits++;
    }
    if (!digits || digits > most || i >= n || s[i] != end) return false;
    if (digits > 1u && s[*at] == '0') return false;
    *v = x;
    *at = i + 1u;
    return true;
}

/* The text up to `end` at s[*at] into out (a side of a card). */
static bool get_side(const char *s, uint32_t n, uint32_t *at, char end, char *out)
{
    uint32_t i = *at;
    while (i < n && s[i] != end && s[i] != '\t' && s[i] != '\n') i++;
    if (i >= n || s[i] != end || i - *at > NUTSHELL_SIDE_MAX) return false;
    memcpy(out, s + *at, i - *at);
    out[i - *at] = 0;
    *at = i + 1u;
    return nutshell_side_ok(out);
}

/* NM1, NM2: false when the text is not a deck. */
static bool parse(nutshell_t *d, const char *s, uint32_t n)
{
    uint32_t at = (uint32_t) strlen(HEAD);
    if (n < at || memcmp(s, HEAD, at)) return false;
    if (!get_num(s, n, &at, ' ', 7u, &d->made) || !get_num(s, n, &at, '\n', 7u, &d->reviewed))
        return false;
    if (d->made > SOLSTICE_DAY_MAX || d->reviewed > SOLSTICE_DAY_MAX) return false;
    d->n = 0;
    while (at < n) {
        if (d->n >= NUTSHELL_CARDS) return false;
        nutshell_card_t *k = &d->c[d->n];
        if (!get_num(s, n, &at, ' ', 1u, &k->box) || !get_num(s, n, &at, ' ', 7u, &k->due) ||
            !get_num(s, n, &at, ' ', 6u, &k->right) || !get_num(s, n, &at, '\t', 6u, &k->wrong) ||
            !get_side(s, n, &at, '\t', k->front) || !get_side(s, n, &at, '\n', k->back) ||
            !nutshell_card_valid(k->box, k->due, k->right, k->wrong))
            return false;
        d->n++;
    }
    return nutshell_deck_fits(d->deck, d->n, d->made, d->reviewed); /* NS4 */
}

/* ---- zxvd ---------------------------------------------------------------- */

void nutshell_init(nutshell_t *s, zxm_link_t *link)
{
    memset(s, 0, sizeof *s);
    s->link = link;
}

int nutshell_open(nutshell_t *s, int64_t deck)
{
    static char text[NUTSHELL_TEXT_MAX + 1];
    static nutshell_t next;
    char path[48];
    uint64_t size;
    bool eof;
    if (!s || deck < 1 || deck > NUTSHELL_DECK_MAX || !path_of(deck, path, sizeof path))
        return ZXM_E_ARG;
    zxm_link_t *l = s->link;
    zxm_begin(l, "files.read");
    zxm_p_str(l, "path", path);
    zxm_p_u64(l, "max", NUTSHELL_TEXT_MAX);
    int rc = zxm_call(l);
    if (rc == ZXM_E_REMOTE && l->err_code == NUTSHELL_E_FILES && strstr(l->err, "no such")) {
        nutshell_init(s, l); /* not made yet: a new deck, no cards */
        s->deck = deck;
        s->loaded = true;
        return ZXM_OK;
    }
    if (rc) return rc;
    int32_t x = zxm_get(l, l->result, "text");
    if (zxm_get_u64(l, l->result, "size", &size) || zxm_get_bool(l, l->result, "eof", &eof) ||
        x < 0)
        return ZXM_E_PROTOCOL;
    if (!eof || size > NUTSHELL_TEXT_MAX) return ZXM_E_STATE; /* not a deck: left alone */
    int32_t n = w4_json_str(l->resp, &l->tok[x], text, sizeof text);
    if (n < 0) return ZXM_E_STATE;
    nutshell_init(&next, l);
    next.deck = deck;
    if (!parse(&next, text, (uint32_t) n)) return ZXM_E_STATE;
    next.exists = true;
    next.loaded = true;
    *s = next;
    return ZXM_OK;
}

/* Write the deck as it would be with these cards (NM5): a new one with
 * exclusive set, its folder made first when it is missing. */
static int save(nutshell_t *s, const nutshell_card_t *c, uint32_t n, int64_t made, int64_t reviewed)
{
    static char text[NUTSHELL_TEXT_MAX];
    char path[48];
    int32_t len = compose(c, n, made, reviewed, text, sizeof text);
    if (len < 0 || !path_of(s->deck, path, sizeof path)) return ZXM_E_TOOBIG;
    zxm_link_t *l = s->link;
    if (!s->exists) {
        zxm_begin(l, "files.mkdir");
        zxm_p_str(l, "path", NUTSHELL_DIR);
        int rc = zxm_call(l);
        if (rc && !(rc == ZXM_E_REMOTE && l->err_code == NUTSHELL_E_FILES &&
                    strstr(l->err, "already exists")))
            return rc;
    }
    zxm_begin(l, "files.write");
    zxm_p_str(l, "path", path);
    zxm_p_strn(l, "text", text, (uint32_t) len);
    if (!s->exists) zxm_p_bool(l, "exclusive", true);
    return zxm_call(l);
}

int nutshell_add_card(nutshell_t *s, const char *front, const char *back, int64_t today,
                      bool owner_confirmed)
{
    static nutshell_card_t c[NUTSHELL_CARDS];
    if (!s || !nutshell_side_ok(front) || !nutshell_side_ok(back)) return ZXM_E_ARG;
    if (today < 0 || today > SOLSTICE_DAY_MAX) return ZXM_E_ARG;
    if (!s->loaded) return ZXM_E_STATE;
    if (s->n >= NUTSHELL_CARDS) return ZXM_E_TOOBIG;
    if (!owner_confirmed) return ZXM_E_STATE; /* NM4: nothing sent */
    int64_t made = s->exists ? s->made : today;
    memcpy(c, s->c, (size_t) s->n * sizeof c[0]);
    nutshell_card_t *k = &c[s->n];
    memset(k, 0, sizeof *k);
    k->box = 1;
    k->due = today;
    memcpy(k->front, front, strlen(front) + 1u);
    memcpy(k->back, back, strlen(back) + 1u);
    int rc = save(s, c, s->n + 1u, made, s->reviewed);
    if (rc) return rc; /* NM5: refused, nothing kept */
    s->c[s->n++] = *k;
    s->made = made;
    s->exists = true;
    return ZXM_OK;
}

int nutshell_review(nutshell_t *s, uint32_t index, int64_t right, int64_t today,
                    bool owner_confirmed)
{
    static nutshell_card_t c[NUTSHELL_CARDS];
    if (!s || right < 0 || right > 1 || today < 0 || today > SOLSTICE_DAY_MAX) return ZXM_E_ARG;
    if (!s->loaded || !s->exists) return ZXM_E_STATE;
    if (index >= s->n) return ZXM_E_ARG;
    nutshell_card_t k = s->c[index];
    int64_t due = nutshell_due_day(k.box, right, today); /* NS2 */
    if (due < 0) return ZXM_E_ARG;                       /* past 9999-12-31 */
    if ((right && k.right >= NUTSHELL_TALLY_MAX) || (!right && k.wrong >= NUTSHELL_TALLY_MAX))
        return ZXM_E_TOOBIG;
    if (!owner_confirmed) return ZXM_E_STATE; /* NM4: nothing sent */
    k.box = nutshell_next_box(k.box, right);  /* NS1 */
    k.due = due;
    if (right)
        k.right++;
    else
        k.wrong++;
    int64_t reviewed = today > s->reviewed ? today : s->reviewed;
    memcpy(c, s->c, (size_t) s->n * sizeof c[0]);
    c[index] = k;
    int rc = save(s, c, s->n, s->made, reviewed);
    if (rc) return rc; /* NM5: refused, nothing kept */
    s->c[index] = k;
    s->reviewed = reviewed;
    return ZXM_OK;
}

/* ---- what the deck says ------------------------------------------------- */

int nutshell_due_total(const nutshell_t *s, int64_t today, uint32_t *count)
{
    int64_t due[NUTSHELL_CARDS];
    if (!s || !count || today < 0 || today > SOLSTICE_DAY_MAX) return ZXM_E_ARG;
    for (uint32_t i = 0; i < s->n; i++) due[i] = s->c[i].due;
    *count = (uint32_t) nutshell_due_count(due, s->n, today); /* NS3 */
    return ZXM_OK;
}

int nutshell_next_card(const nutshell_t *s, int64_t today, uint32_t *index)
{
    if (!s || !index || today < 0 || today > SOLSTICE_DAY_MAX) return ZXM_E_ARG;
    uint32_t best = NUTSHELL_CARDS;
    for (uint32_t i = 0; i < s->n; i++) {
        const nutshell_card_t *k = &s->c[i];
        if (k->due > today) continue;
        if (best == NUTSHELL_CARDS || k->due < s->c[best].due ||
            (k->due == s->c[best].due && k->box < s->c[best].box))
            best = i;
    }
    if (best == NUTSHELL_CARDS) return ZXM_E_STATE;
    *index = best;
    return ZXM_OK;
}

int nutshell_box_counts(const nutshell_t *s, int64_t counts[NUTSHELL_BOX_MAX])
{
    int64_t box[NUTSHELL_CARDS];
    if (!s || !counts) return ZXM_E_ARG;
    for (uint32_t i = 0; i < s->n; i++) box[i] = s->c[i].box;
    for (int64_t b = 1; b <= NUTSHELL_BOX_MAX; b++)
        counts[b - 1] = witness_kind_count(box, s->n, b); /* WR2 */
    return ZXM_OK;
}
