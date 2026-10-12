/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* quillon_model.c — Quillon, the text editor (see quillon_model.h). */
#include <string.h>

#include "quillon_model.h"

const zxm_ai_action_t QUILLON_AI[QUILLON_AI_COUNT] = {
    {"Summarize", "Writer", 600},
    {"Rewrite more clearly", "Writer", 900},
    {"Continue writing", "Writer", 700},
    {"Proofread", "Writer", 800},
};

static uint64_t fnv(const char *s, uint32_t n)
{
    uint64_t h = 1469598103934665603ull;
    for (uint32_t i = 0; i < n; i++) h = (h ^ (uint8_t) s[i]) * 1099511628211ull;
    return h;
}

/* strict UTF-8, no NUL (files.write text cannot carry one) */
static bool utf8_ok(const char *p, uint32_t n)
{
    const uint8_t *s = (const uint8_t *) p;
    for (uint32_t i = 0; i < n;) {
        uint8_t c = s[i];
        if (c == 0) return false;
        if (c < 0x80) {
            i++;
            continue;
        }
        uint32_t k = c >= 0xc2 && c <= 0xdf   ? 1
                     : c >= 0xe0 && c <= 0xef ? 2
                     : c >= 0xf0 && c <= 0xf4 ? 3
                                              : 0;
        if (!k) return false;
        uint32_t cp = c & (k == 1 ? 0x1f : k == 2 ? 0x0f : 0x07);
        for (uint32_t j = 1; j <= k; j++) {
            if (i + j >= n || (s[i + j] & 0xc0) != 0x80) return false;
            cp = cp << 6 | (s[i + j] & 0x3f);
        }
        if ((k == 2 && cp < 0x800) || (k == 3 && (cp < 0x10000 || cp > 0x10ffff)) ||
            (cp >= 0xd800 && cp <= 0xdfff))
            return false;
        i += k + 1;
    }
    return true;
}

static bool boundary(const quillon_t *q, uint32_t at)
{
    return at == 0 || at >= q->len || ((uint8_t) q->text[at] & 0xc0) != 0x80;
}

static void recount(quillon_t *q)
{
    quillon_counts_t c = {0, 0, q->len ? 1u : 0u};
    bool in_word = false;
    for (uint32_t i = 0; i < q->len; i++) {
        uint8_t ch = (uint8_t) q->text[i];
        if ((ch & 0xc0) != 0x80) c.chars++;
        if (ch == '\n') c.lines++;
        bool space =
            ch == ' ' || ch == '\n' || ch == '\t' || ch == '\r' || ch == '\f' || ch == '\v';
        if (!space && !in_word) c.words++;
        in_word = !space;
    }
    q->counts = c;
}

static void mark_saved(quillon_t *q)
{
    q->saved_hash = fnv(q->text, q->len);
    q->saved_len = q->len;
}

static void drop_held(quillon_t *q)
{
    if (q->held.state == ZXM_HELD_PENDING) {
        q->held.state = ZXM_HELD_REJECTED;
        q->held.len = 0;
        q->held.text[0] = 0;
    }
}

void quillon_new(quillon_t *q, zxm_link_t *link)
{
    q->link = link;
    q->path[0] = 0;
    q->len = 0;
    q->text[0] = 0;
    memset(&q->held, 0, sizeof q->held);
    q->sel_start = q->sel_end = 0;
    mark_saved(q);
    recount(q);
}

bool quillon_dirty(const quillon_t *q)
{
    return q->len != q->saved_len || fnv(q->text, q->len) != q->saved_hash;
}

static int set_path(quillon_t *q, const char *path)
{
    size_t n = strlen(path);
    if (!n || n > QUILLON_PATH_MAX) return ZXM_E_ARG;
    memcpy(q->path, path, n + 1);
    return ZXM_OK;
}

int quillon_open(quillon_t *q, const char *path)
{
    static char chunk[64 * 1024];
    zxm_link_t *l = q->link;
    uint32_t len = 0;
    uint64_t off = 0;
    /* gathered apart, so a failed open leaves the document as it was */
    static char into[QUILLON_TEXT_MAX + 1];
    for (;;) {
        zxm_begin(l, "files.read");
        zxm_p_str(l, "path", path);
        zxm_p_u64(l, "offset", off);
        int rc = zxm_call(l);
        if (rc) return rc;
        uint64_t size, next;
        bool eof;
        int32_t t = zxm_get(l, l->result, "text");
        if (zxm_get_u64(l, l->result, "size", &size) || zxm_get_u64(l, l->result, "next", &next) ||
            zxm_get_bool(l, l->result, "eof", &eof) || t < 0)
            return ZXM_E_PROTOCOL;
        if (size > QUILLON_TEXT_MAX) return ZXM_E_TOOBIG;
        int32_t n = w4_json_str(l->resp, &l->tok[t], chunk, sizeof chunk);
        if (n < 0 || len + (uint32_t) n > QUILLON_TEXT_MAX) return ZXM_E_TOOBIG;
        memcpy(into + len, chunk, (size_t) n);
        len += (uint32_t) n;
        if (eof) break;
        if (next <= off) return ZXM_E_PROTOCOL;
        off = next;
    }
    int rc = set_path(q, path);
    if (rc) return rc;
    memcpy(q->text, into, len);
    q->text[len] = 0;
    q->len = len;
    memset(&q->held, 0, sizeof q->held);
    q->sel_start = q->sel_end = 0;
    mark_saved(q);
    recount(q);
    return ZXM_OK;
}

/* One request's text: whole characters whose escaped form fits the line. */
#define PIECE 9000u

static int write_all(quillon_t *q, const char *path)
{
    static char esc[ZXM_REQ_MAX];
    zxm_link_t *l = q->link;
    uint32_t at = 0;
    bool first = true;
    do {
        uint32_t n = q->len - at < PIECE ? q->len - at : PIECE;
        n = at + n < q->len ? zxm_utf8_cut(q->text + at, n) : n;
        while (n && zxm_escape(q->text + at, n, esc, 12000u) < 0)
            n = zxm_utf8_cut(q->text + at, n / 2u);
        if (!n && at < q->len) return ZXM_E_TOOBIG;
        bool last = at + n >= q->len;
        zxm_begin(l, "files.write");
        zxm_p_str(l, "path", path);
        zxm_p_strn(l, "text", q->text + at, n);
        if (!(first && last)) zxm_p_str(l, "mode", first ? "begin" : last ? "end" : "add");
        int rc = zxm_call(l);
        if (rc) return rc;
        at += n;
        first = false;
    } while (at < q->len);
    return ZXM_OK;
}

int quillon_save(quillon_t *q)
{
    if (!q->path[0]) return ZXM_E_STATE;
    int rc = write_all(q, q->path);
    if (!rc) mark_saved(q);
    return rc;
}

int quillon_save_as(quillon_t *q, const char *path)
{
    size_t n = strlen(path);
    if (!n || n > QUILLON_PATH_MAX) return ZXM_E_ARG;
    int rc = write_all(q, path);
    if (rc) return rc;
    set_path(q, path);
    mark_saved(q);
    return ZXM_OK;
}

int quillon_replace(quillon_t *q, uint32_t start, uint32_t end, const char *s, uint32_t n)
{
    if (start > end || end > q->len || !boundary(q, start) || !boundary(q, end) || !utf8_ok(s, n))
        return ZXM_E_ARG;
    if (q->len - (end - start) + n > QUILLON_TEXT_MAX) return ZXM_E_TOOBIG;
    memmove(q->text + start + n, q->text + end, q->len - end);
    memcpy(q->text + start, s, n);
    q->len = q->len - (end - start) + n;
    q->text[q->len] = 0;
    drop_held(q);
    recount(q);
    return ZXM_OK;
}

int quillon_set_text(quillon_t *q, const char *s, uint32_t n)
{
    if (!utf8_ok(s, n)) return ZXM_E_ARG;
    if (n > QUILLON_TEXT_MAX) return ZXM_E_TOOBIG;
    memmove(q->text, s, n);
    q->len = n;
    q->text[n] = 0;
    drop_held(q);
    recount(q);
    return ZXM_OK;
}

int quillon_ai(quillon_t *q, int32_t action, uint32_t start, uint32_t end)
{
    static const char *const ASK[QUILLON_AI_COUNT] = {
        "Summarize the following text in a few sentences. Answer with the summary only.",
        "Rewrite the following text so it reads more clearly, keeping its meaning. Answer with "
        "the rewritten text only.",
        "Continue the following text with the next paragraph, in the same voice. Answer with "
        "the new paragraph only.",
        "Correct the spelling and grammar of the following text, changing nothing else. Answer "
        "with the corrected text only.",
    };
    if (action < 0 || action >= QUILLON_AI_COUNT) return ZXM_E_ARG;
    if (start > end || end > q->len || !boundary(q, start) || !boundary(q, end)) return ZXM_E_ARG;
    if (start == end) {
        start = 0;
        end = q->len;
    }
    if (start == end) return ZXM_E_STATE; /* nothing to work on */
    int rc = zxm_ai_ask(q->link, &QUILLON_AI[action], action, ASK[action], q->text + start,
                        end - start, &q->held);
    if (rc) return rc;
    q->sel_start = start;
    q->sel_end = end;
    return ZXM_OK;
}

int quillon_ai_accept(quillon_t *q)
{
    if (q->held.state != ZXM_HELD_PENDING) return ZXM_E_STATE;
    bool replace = q->held.action == QUILLON_AI_REWRITE || q->held.action == QUILLON_AI_PROOFREAD;
    uint32_t at = replace ? q->sel_start : q->sel_end;
    uint32_t to = q->sel_end;
    static char ins[ZXM_ANSWER_MAX + 3];
    uint32_t n = 0;
    if (!replace) { /* an insertion goes after a blank line */
        ins[n++] = '\n';
        ins[n++] = '\n';
    }
    memcpy(ins + n, q->held.text, q->held.len);
    n += q->held.len;
    if (!utf8_ok(ins, n)) return ZXM_E_PROTOCOL;
    if (q->len - (to - at) + n > QUILLON_TEXT_MAX) return ZXM_E_TOOBIG;
    memmove(q->text + at + n, q->text + to, q->len - to);
    memcpy(q->text + at, ins, n);
    q->len = q->len - (to - at) + n;
    q->text[q->len] = 0;
    q->held.state = ZXM_HELD_ACCEPTED;
    q->sel_start = at;
    q->sel_end = at + n;
    recount(q);
    return ZXM_OK;
}

int quillon_ai_reject(quillon_t *q)
{
    if (q->held.state != ZXM_HELD_PENDING) return ZXM_E_STATE;
    drop_held(q);
    return ZXM_OK;
}
