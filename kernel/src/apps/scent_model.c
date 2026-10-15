/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* scent_model.c — Scent (see scent_model.h). */
#include <stdio.h>
#include <string.h>

#include "scent_model.h"

const zxm_ai_action_t SCENT_AI[SCENT_AI_COUNT] = {
    {"Sharpen this search", "Companion", 200},
};

static void clear(scent_t *s)
{
    s->n = s->total = 0;
    s->sel = -1;
}

void scent_init(scent_t *s, zxm_link_t *link)
{
    memset(s, 0, sizeof *s);
    s->link = link;
    s->sel = -1;
}

int scent_set_space(scent_t *s, const char *space)
{
    if (!space) space = "";
    if (strlen(space) >= sizeof s->space) return ZXM_E_ARG;
    snprintf(s->space, sizeof s->space, "%s", strcmp(space, "home") ? space : "");
    clear(s);
    return ZXM_OK;
}

static bool blank(const char *q)
{
    for (; *q; q++)
        if (*q != ' ' && *q != '\t' && *q != '\n' && *q != '\r') return false;
    return true;
}

int scent_search(scent_t *s, const char *query)
{
    if (!query || strlen(query) > SCENT_QUERY) return ZXM_E_ARG;
    snprintf(s->query, sizeof s->query, "%s", query);
    if (blank(query)) {
        clear(s);
        return ZXM_OK;
    }
    zxm_link_t *l = s->link;
    zxm_begin(l, "search.query");
    zxm_p_space(l, s->space);
    zxm_p_str(l, "q", query);
    zxm_p_u64(l, "max", SCENT_HITS);
    int rc = zxm_call(l);
    if (rc) return rc;
    static scent_hit_t got[SCENT_HITS]; /* read into a copy: a bad reply keeps the old hits */
    uint64_t total;
    int32_t a = zxm_get(l, l->result, "hits");
    if (a < 0 || l->tok[a].type != W4_J_ARR || zxm_get_u64(l, l->result, "total", &total) ||
        zxm_len(l, a) > SCENT_HITS || total < zxm_len(l, a) || total > UINT32_MAX)
        return ZXM_E_PROTOCOL;
    uint32_t n = zxm_len(l, a);
    for (uint32_t i = 0; i < n; i++) {
        int32_t o = zxm_at(l, a, i);
        scent_hit_t *h = &got[i];
        uint64_t score;
        if (o < 0 || l->tok[o].type != W4_J_OBJ ||
            zxm_get_str(l, o, "path", h->path, sizeof h->path) ||
            zxm_get_u64(l, o, "size", &h->size) || zxm_get_i64(l, o, "mtime", &h->mtime) ||
            zxm_get_bool(l, o, "in_name", &h->in_name) || zxm_get_u64(l, o, "score", &score) ||
            score > UINT32_MAX || zxm_get_str(l, o, "snippet", h->snippet, sizeof h->snippet) ||
            !h->path[0])
            return ZXM_E_PROTOCOL;
        h->score = (uint32_t) score;
    }
    memcpy(s->hit, got, n * sizeof *got);
    s->n = n;
    s->total = (uint32_t) total;
    s->sel = -1;
    return ZXM_OK;
}

int scent_reindex(scent_t *s, uint32_t *files)
{
    zxm_link_t *l = s->link;
    zxm_begin(l, "search.refresh");
    zxm_p_space(l, s->space);
    int rc = zxm_call(l);
    if (rc) return rc;
    uint64_t f;
    if (zxm_get_u64(l, l->result, "files", &f) || f > UINT32_MAX) return ZXM_E_PROTOCOL;
    if (files) *files = (uint32_t) f;
    return ZXM_OK;
}

int scent_select(scent_t *s, int32_t i)
{
    if (i < -1 || i >= (int32_t) s->n) return ZXM_E_ARG;
    s->sel = i;
    return ZXM_OK;
}

const scent_hit_t *scent_selected(const scent_t *s)
{
    return s->sel >= 0 && (uint32_t) s->sel < s->n ? &s->hit[s->sel] : NULL;
}

int scent_opener(const scent_hit_t *h)
{
    const char *base = strrchr(h->path, '/');
    switch (burrow_kind(base ? base + 1 : h->path)) {
    case BURROW_K_TEXT:
        return SCENT_OPEN_QUILLON;
    case BURROW_K_IMAGE:
    case BURROW_K_AUDIO:
    case BURROW_K_VIDEO:
        return SCENT_OPEN_GLINT;
    default:
        return SCENT_OPEN_BURROW;
    }
}

void scent_folder(const scent_hit_t *h, char *out, uint32_t cap)
{
    if (!cap) return;
    const char *base = strrchr(h->path, '/');
    size_t n = base ? (size_t) (base - h->path) : 0;
    if (n >= cap) n = zxm_utf8_cut(h->path, cap - 1);
    memcpy(out, h->path, n);
    out[n] = 0;
}

int scent_ai(scent_t *s, int32_t action)
{
    if (action < 0 || action >= SCENT_AI_COUNT) return ZXM_E_ARG;
    if (blank(s->query)) return ZXM_E_STATE; /* nothing searched yet */
    static char in[ZXM_ASK_MAX];
    int k =
        snprintf(in, sizeof in, "Search: %s\nFound %u. Names of the best:\n", s->query, s->total);
    for (uint32_t i = 0; i < s->n && k > 0 && (size_t) k < sizeof in; i++) {
        int m = snprintf(in + k, sizeof in - (size_t) k, "%s\n", s->hit[i].path);
        if (m < 0 || (size_t) m >= sizeof in - (size_t) k) break;
        k += m;
    }
    if (k < 0) return ZXM_E_ARG;
    if ((size_t) k >= sizeof in) k = (int) sizeof in - 1;
    return zxm_ai_ask(s->link, &SCENT_AI[action], action,
                      "The owner searched their files with these words and found these names. "
                      "Reply with one better search of at most eight words and nothing else.",
                      in, (uint32_t) k, &s->held);
}

int scent_ai_accept(scent_t *s)
{
    if (s->held.state != ZXM_HELD_PENDING) return ZXM_E_STATE;
    char q[SCENT_QUERY + 1];
    uint32_t n = s->held.len < SCENT_QUERY ? s->held.len : zxm_utf8_cut(s->held.text, SCENT_QUERY);
    memcpy(q, s->held.text, n);
    q[n] = 0;
    for (uint32_t i = 0; i < n; i++) /* one line: the first */
        if (q[i] == '\n' || q[i] == '\r') {
            q[i] = 0;
            break;
        }
    int rc = zxm_held_accept(&s->held);
    if (rc) return rc;
    return scent_search(s, q);
}

int scent_ai_reject(scent_t *s)
{
    return zxm_held_reject(&s->held);
}
