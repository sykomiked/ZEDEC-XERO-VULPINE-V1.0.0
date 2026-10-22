/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* lore_model.c — Lore (see lore_model.h). */
#include <stdio.h>
#include <string.h>

#include "lore_model.h"

const zxm_ai_action_t LORE_AI[LORE_AI_COUNT] = {
    {"Ask about this file", "Companion", 900},
};

static const char *const KINDS[] = {"home", "office", "team", "cloudtop", "park"};

static void clear_views(lore_t *l)
{
    l->nentries = 0;
    l->folder[0] = 0;
    l->nhits = l->total = 0;
    l->query[0] = 0;
    l->open_path[0] = 0;
    l->text[0] = 0;
    l->text_len = 0;
    l->text_cut = false;
}

void lore_init(lore_t *l, zxm_link_t *link)
{
    memset(l, 0, sizeof *l);
    l->link = link;
    l->kind = LORE_KIND_HOME;
}

int64_t lore_kind_of(const char *name)
{
    if (!name) return LORE_REFUSED;
    for (int64_t k = 0; k < (int64_t) (sizeof KINDS / sizeof KINDS[0]); k++)
        if (!strcmp(name, KINDS[k])) return k;
    return LORE_REFUSED;
}

int lore_set_space(lore_t *l, const char *space)
{
    if (!space) space = "";
    if (!strcmp(space, "home")) space = "";
    if (strlen(space) > ZXM_SPACE_MAX) return ZXM_E_ARG;
    zxm_link_t *k = l->link;
    zxm_begin(k, "spaces.get");
    zxm_p_str(k, "space", space[0] ? space : "home");
    int rc = zxm_call(k);
    if (rc) return rc;
    char kind[16];
    if (zxm_get_str(k, k->result, "kind", kind, sizeof kind)) return ZXM_E_PROTOCOL;
    int64_t n = lore_kind_of(kind);
    if (n < 0) return ZXM_E_PROTOCOL;
    snprintf(l->space, sizeof l->space, "%s", space);
    l->kind = n;
    clear_views(l);
    return ZXM_OK;
}

/* A folder as Lore keeps it in a leave: "" (the whole space) or segments
 * joined by '/', none empty, "." or "..", and no '\\', ':' or control
 * character (zxv_files.h F2). */
static bool folder_ok(const char *f)
{
    size_t n = strlen(f);
    if (n > LORE_PATH) return false;
    if (!n) return true;
    const char *seg = f;
    for (size_t i = 0; i <= n; i++) {
        unsigned char c = (unsigned char) f[i];
        if (c == '/' || c == 0) {
            size_t len = (size_t) (f + i - seg);
            if (!len) return false;
            if (len == 1 && seg[0] == '.') return false;
            if (len == 2 && seg[0] == '.' && seg[1] == '.') return false;
            seg = f + i + 1;
            continue;
        }
        if (c < 0x20 || c == 0x7f || c == '\\' || c == ':') return false;
    }
    return true;
}

int64_t lore_depth(const char *folder)
{
    if (!folder || !folder[0]) return 0;
    int64_t d = 1;
    for (const char *p = folder; *p; p++)
        if (*p == '/') d++;
    return d;
}

bool lore_under(const char *path, const char *folder)
{
    if (!path || !folder) return false;
    size_t n = strlen(folder);
    if (!n) return true;
    if (strncmp(path, folder, n)) return false;
    return path[n] == 0 || path[n] == '/';
}

int lore_list(lore_t *l, const char *folder)
{
    if (!folder) folder = "";
    if (!folder_ok(folder)) return ZXM_E_ARG;
    zxm_link_t *k = l->link;
    zxm_begin(k, "files.list");
    zxm_p_space(k, l->space);
    zxm_p_str(k, "path", folder);
    int rc = zxm_call(k);
    if (rc) return rc;
    static lore_entry_t got[LORE_ENTRIES]; /* a bad reply keeps the old listing */
    int32_t a = zxm_get(k, k->result, "entries");
    if (a < 0 || k->tok[a].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    uint32_t n = zxm_len(k, a);
    if (n > LORE_ENTRIES) n = LORE_ENTRIES; /* the first LORE_ENTRIES, folders first */
    for (uint32_t i = 0; i < n; i++) {
        int32_t o = zxm_at(k, a, i);
        lore_entry_t *e = &got[i];
        if (o < 0 || k->tok[o].type != W4_J_OBJ ||
            zxm_get_str(k, o, "name", e->name, sizeof e->name) ||
            zxm_get_bool(k, o, "dir", &e->dir) || zxm_get_u64(k, o, "size", &e->size) ||
            zxm_get_i64(k, o, "mtime", &e->mtime) || !e->name[0])
            return ZXM_E_PROTOCOL;
    }
    memcpy(l->entry, got, n * sizeof *got);
    l->nentries = n;
    snprintf(l->folder, sizeof l->folder, "%s", folder);
    return ZXM_OK;
}

/* files.read of up to max bytes of path in the space in use, as UTF-8 text */
static int read_text(lore_t *l, const char *path, uint32_t max, char *out, uint32_t cap,
                     uint32_t *len, bool *eof)
{
    zxm_link_t *k = l->link;
    zxm_begin(k, "files.read");
    zxm_p_space(k, l->space);
    zxm_p_str(k, "path", path);
    zxm_p_u64(k, "max", max);
    zxm_p_str(k, "encoding", "utf8");
    int rc = zxm_call(k);
    if (rc) return rc;
    if (zxm_get_bool(k, k->result, "eof", eof) || zxm_get_str(k, k->result, "text", out, cap))
        return ZXM_E_PROTOCOL;
    size_t n = strlen(out);
    if (n > max) return ZXM_E_PROTOCOL;
    *len = (uint32_t) n;
    return ZXM_OK;
}

int lore_open(lore_t *l, const char *path)
{
    if (!path || !path[0] || !folder_ok(path)) return ZXM_E_ARG;
    static char got[LORE_TEXT_MAX + 1];
    uint32_t n = 0;
    bool eof = false;
    int rc = read_text(l, path, LORE_TEXT_MAX, got, sizeof got, &n, &eof);
    if (rc) return rc;
    memcpy(l->text, got, n);
    l->text[n] = 0;
    l->text_len = n;
    l->text_cut = !eof;
    snprintf(l->open_path, sizeof l->open_path, "%s", path);
    return ZXM_OK;
}

static bool blank(const char *q)
{
    for (; *q; q++)
        if (*q != ' ' && *q != '\t' && *q != '\n' && *q != '\r') return false;
    return true;
}

int lore_search(lore_t *l, const char *query, int64_t now)
{
    if (!query || strlen(query) > LORE_QUERY) return ZXM_E_ARG;
    if (blank(query)) {
        snprintf(l->query, sizeof l->query, "%s", query);
        l->nhits = l->total = 0;
        return ZXM_OK;
    }
    zxm_link_t *k = l->link;
    zxm_begin(k, "search.query");
    zxm_p_space(k, l->space);
    zxm_p_str(k, "q", query);
    zxm_p_u64(k, "max", LORE_HITS);
    int rc = zxm_call(k);
    if (rc) return rc;
    static lore_hit_t got[LORE_HITS]; /* a bad reply keeps the old hits */
    uint64_t total;
    int32_t a = zxm_get(k, k->result, "hits");
    if (a < 0 || k->tok[a].type != W4_J_ARR || zxm_get_u64(k, k->result, "total", &total) ||
        zxm_len(k, a) > LORE_HITS || total < zxm_len(k, a) || total > UINT32_MAX)
        return ZXM_E_PROTOCOL;
    uint32_t n = zxm_len(k, a);
    for (uint32_t i = 0; i < n; i++) {
        int32_t o = zxm_at(k, a, i);
        lore_hit_t *h = &got[i];
        uint64_t score;
        if (o < 0 || k->tok[o].type != W4_J_OBJ ||
            zxm_get_str(k, o, "path", h->path, sizeof h->path) ||
            zxm_get_u64(k, o, "size", &h->size) || zxm_get_i64(k, o, "mtime", &h->mtime) ||
            zxm_get_u64(k, o, "score", &score) || score > UINT32_MAX ||
            zxm_get_str(k, o, "snippet", h->snippet, sizeof h->snippet) || !h->path[0])
            return ZXM_E_PROTOCOL;
        h->score = (uint32_t) score;
        h->ai_ok = lore_may_read(l, h->path, now) == LORE_READ_OK;
    }
    memcpy(l->hit, got, n * sizeof *got);
    l->nhits = n;
    l->total = (uint32_t) total;
    snprintf(l->query, sizeof l->query, "%s", query);
    return ZXM_OK;
}

int lore_reindex(lore_t *l, uint32_t *files)
{
    zxm_link_t *k = l->link;
    zxm_begin(k, "search.refresh");
    zxm_p_space(k, l->space);
    int rc = zxm_call(k);
    if (rc) return rc;
    uint64_t f;
    if (zxm_get_u64(k, k->result, "files", &f) || f > UINT32_MAX) return ZXM_E_PROTOCOL;
    if (files) *files = (uint32_t) f;
    return ZXM_OK;
}

int lore_leave_give(lore_t *l, const char *folder, int64_t reach, int64_t given, int64_t expires,
                    uint32_t *idx)
{
    if (!folder || !folder_ok(folder)) return ZXM_E_ARG;
    uint32_t i = 0;
    while (i < LORE_LEAVES && l->leave[i].used) i++;
    if (i == LORE_LEAVES) return ZXM_E_STATE;
    if (!lore_leave_fits((int64_t) i, reach, lore_depth(folder), given, expires)) return ZXM_E_ARG;
    lore_leave_t *v = &l->leave[i];
    memset(v, 0, sizeof *v);
    snprintf(v->folder, sizeof v->folder, "%s", folder);
    v->reach = reach;
    v->given = given;
    v->expires = expires;
    v->used = true;
    if (idx) *idx = i;
    return ZXM_OK;
}

int lore_leave_revoke(lore_t *l, uint32_t idx)
{
    if (idx >= LORE_LEAVES || !l->leave[idx].used) return ZXM_E_ARG;
    if (l->leave[idx].revoked) return ZXM_E_STATE;
    l->leave[idx].revoked = true;
    return ZXM_OK;
}

int64_t lore_leave_days(const lore_t *l, uint32_t idx, int64_t now)
{
    if (idx >= LORE_LEAVES || !l->leave[idx].used) return LORE_REFUSED;
    if (l->leave[idx].revoked) return 0;
    return lore_leave_days_left(l->leave[idx].expires, now);
}

int lore_may_read(const lore_t *l, const char *path, int64_t now)
{
    if (!path || !path[0]) return LORE_NOT_GRANTED;
    int why = LORE_NO_LEAVE;
    for (uint32_t i = 0; i < LORE_LEAVES; i++) {
        const lore_leave_t *v = &l->leave[i];
        if (!v->used || v->revoked) continue;
        int64_t live = v->given <= now && now < v->expires ? 1 : 0;
        int64_t granted = lore_under(path, v->folder) ? 1 : 0;
        int64_t room = lore_room(l->kind, v->reach);
        if (lore_ai_read_ok(live, granted, room)) return LORE_READ_OK;
        int miss = !granted ? LORE_NOT_GRANTED : !live ? LORE_LAPSED : LORE_TOO_PRIVATE;
        /* the nearest miss: granted beats not granted, live beats lapsed */
        int rank = miss == LORE_NOT_GRANTED ? 1 : miss == LORE_LAPSED ? 2 : 3;
        int was = why == LORE_NO_LEAVE      ? 0
                  : why == LORE_NOT_GRANTED ? 1
                  : why == LORE_LAPSED      ? 2
                                            : 3;
        if (rank > was) why = miss;
    }
    return why;
}

const char *lore_why(int reason)
{
    switch (reason) {
    case LORE_READ_OK:
        return "the companion may read this file";
    case LORE_NO_LEAVE:
        return "you have given the companion no leave to read your files";
    case LORE_LAPSED:
        return "your leave for this folder has expired";
    case LORE_NOT_GRANTED:
        return "no leave you gave covers this folder";
    case LORE_TOO_PRIVATE:
        return "this space is more private than your leave reaches";
    default:
        return "unknown";
    }
}

int lore_ask(lore_t *l, const char *path, const char *question, int64_t now, int *reason)
{
    if (!path || !path[0] || !folder_ok(path) || !question || blank(question) ||
        strlen(question) > LORE_QUERY)
        return ZXM_E_ARG;
    int why = lore_may_read(l, path, now);
    if (reason) *reason = why;
    if (why != LORE_READ_OK) return ZXM_E_STATE; /* LO4: nothing is sent */
    static char text[LORE_ASK_TEXT + 1];
    uint32_t n = 0;
    bool eof = false;
    int rc = read_text(l, path, LORE_ASK_TEXT, text, sizeof text, &n, &eof);
    if (rc) return rc;
    static char in[ZXM_ASK_MAX];
    int k = snprintf(in, sizeof in, "Question: %s\nFile: %s%s\n\n", question, path,
                     eof ? "" : " (its beginning)");
    if (k < 0) return ZXM_E_ARG;
    uint32_t used = (size_t) k < sizeof in ? (uint32_t) k : (uint32_t) sizeof in - 1u;
    uint32_t take = n < (uint32_t) sizeof in - 1u - used ? n : (uint32_t) sizeof in - 1u - used;
    take = zxm_utf8_cut(text, take);
    memcpy(in + used, text, take);
    used += take;
    in[used] = 0;
    rc = zxm_ai_ask(l->link, &LORE_AI[LORE_AI_ASK], LORE_AI_ASK,
                    "Answer the owner's question from this one file of their notes only, and "
                    "say so when the file does not answer it.",
                    in, used, &l->held);
    if (rc) return rc;
    snprintf(l->asked_path, sizeof l->asked_path, "%s", path);
    return ZXM_OK;
}

int lore_ai_accept(lore_t *l)
{
    return zxm_held_accept(&l->held);
}

int lore_ai_reject(lore_t *l)
{
    return zxm_held_reject(&l->held);
}
