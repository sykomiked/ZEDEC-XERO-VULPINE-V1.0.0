/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* cellar_model.c — Cellar (see cellar_model.h). */
#include <stdio.h>
#include <string.h>

#include "cellar_model.h"

enum { WALK_SURVEY, WALK_TRASH, WALK_CHECK };

/* the folders still to list in one walk, and which folder in the base each lies in */
static char g_dir[CELLAR_DIRS][CELLAR_PATH + 1];
static int32_t g_top[CELLAR_DIRS];

void cellar_init(cellar_t *c, zxm_link_t *link)
{
    memset(c, 0, sizeof *c);
    c->link = link;
    c->used_x100 = CELLAR_REFUSED;
    c->trash_x100 = CELLAR_REFUSED;
}

/* src into dst[cap] whole, or false */
static bool put(char *dst, uint32_t cap, const char *src)
{
    size_t n = strlen(src);
    if (n >= cap) return false;
    memcpy(dst, src, n + 1u);
    return true;
}

/* dir/name (or name alone in the root) into out[CELLAR_PATH + 1], or false */
static bool join(char *out, const char *dir, const char *name)
{
    int k = dir[0] ? snprintf(out, CELLAR_PATH + 1u, "%s/%s", dir, name)
                   : snprintf(out, CELLAR_PATH + 1u, "%s", name);
    return k >= 0 && (uint32_t) k <= CELLAR_PATH;
}

static int refuse(cellar_t *c, uint32_t fault, int rc)
{
    c->fault = fault;
    return rc;
}

/* *acc + v by CR3 (banker.total), refused past CELLAR_BYTES_MAX */
static bool add(int64_t *acc, int64_t v)
{
    int64_t pair[2] = {*acc, v};
    int64_t s = cellar_total(pair, 2);
    if (s == CELLAR_REFUSED) return false;
    *acc = s;
    return true;
}

bool cellar_name_ok(const char *name)
{
    size_t n = strlen(name);
    if (n == 0 || n > CELLAR_NAME || name[0] == '.' || name[n - 1u] == '.' || name[n - 1u] == ' ')
        return false;
    for (size_t i = 0; i < n; i++) {
        unsigned char ch = (unsigned char) name[i];
        if (ch < 0x20u || ch == 0x7fu || ch == '/' || ch == '\\' || ch == ':') return false;
    }
    return true;
}

int cellar_set_base(cellar_t *c, const char *base)
{
    if (!base || !put(c->base, sizeof c->base, base)) return ZXM_E_ARG;
    c->surveyed = false;
    c->checked = false;
    return ZXM_OK;
}

/* each folder's share of the volume (CR1), the Trash's too, once both are known */
static void volume_shares(cellar_t *c)
{
    for (uint32_t i = 0; i < c->nfolders; i++) {
        cellar_folder_t *f = &c->folder[i];
        f->disk_x100 = c->measured && f->bytes <= c->total
                           ? cellar_used_x100(c->total, c->total - f->bytes)
                           : CELLAR_REFUSED;
    }
    c->trash_x100 = c->measured && c->trash_known && c->trash_bytes <= c->total
                        ? cellar_used_x100(c->total, c->total - c->trash_bytes)
                        : CELLAR_REFUSED;
}

int cellar_measure(cellar_t *c)
{
    zxm_link_t *l = c->link;
    uint64_t total, free, avail;
    zxm_begin(l, "sys.disk");
    int rc = zxm_call(l);
    if (rc) return rc;
    if (zxm_get_u64(l, l->result, "total", &total) || zxm_get_u64(l, l->result, "free", &free) ||
        zxm_get_u64(l, l->result, "avail", &avail) || free > total || avail > total)
        return ZXM_E_PROTOCOL;
    if (total > (uint64_t) CELLAR_BYTES_MAX) return refuse(c, CELLAR_F_TOO_BIG, ZXM_E_TOOBIG);
    c->total = (int64_t) total;
    c->free = (int64_t) free;
    c->avail = (int64_t) avail;
    c->used_x100 = cellar_used_x100(c->total, c->free); /* CR1; -1 for a volume of 0 bytes */
    c->measured = true;
    volume_shares(c);
    return ZXM_OK;
}

/* one walk from root, breadth first (CM2, CM3, CM4) */
static int walk(cellar_t *c, const char *root, int mode)
{
    static char name[CELLAR_NAME + 2], path[CELLAR_PATH + 1];
    zxm_link_t *l = c->link;
    uint32_t nd = 1;
    if (!put(g_dir[0], sizeof g_dir[0], root)) return refuse(c, CELLAR_F_LIMITS, ZXM_E_TOOBIG);
    g_top[0] = -1;
    for (uint32_t head = 0; head < nd; head++) {
        zxm_begin(l, "files.list");
        zxm_p_str(l, "path", g_dir[head]);
        int rc = zxm_call(l);
        if (rc) return rc;
        int32_t a = zxm_get(l, l->result, "entries");
        if (a < 0 || l->tok[a].type != W4_J_ARR) return ZXM_E_PROTOCOL;
        uint32_t n = zxm_len(l, a);
        for (uint32_t i = 0; i < n; i++) {
            int32_t o = zxm_at(l, a, i);
            bool isdir;
            int64_t mtime;
            if (o < 0 || l->tok[o].type != W4_J_OBJ || zxm_get_bool(l, o, "dir", &isdir) ||
                zxm_get_i64(l, o, "mtime", &mtime) ||
                zxm_get_str(l, o, "name", name, sizeof name) || !name[0])
                return ZXM_E_PROTOCOL;
            if (!join(path, g_dir[head], name)) return refuse(c, CELLAR_F_LIMITS, ZXM_E_TOOBIG);
            int32_t top = g_top[head];
            bool named = cellar_name_ok(name);
            if (mode == WALK_CHECK) {
                if (!named && c->misnamed++ == 0)
                    put(c->first_misnamed, sizeof c->first_misnamed, path);
                if (named && mtime > c->now + CELLAR_SKEW && c->misdated++ == 0)
                    put(c->first_misdated, sizeof c->first_misdated, path);
            }
            if (head == 0 && mode == WALK_TRASH) c->trash_items++;
            if (isdir) {
                if (!named) continue; /* zxvd refuses its path: not walked (CM4) */
                if (head == 0 && mode == WALK_SURVEY) {
                    if (c->nfolders == CELLAR_FOLDERS)
                        return refuse(c, CELLAR_F_LIMITS, ZXM_E_TOOBIG);
                    cellar_folder_t *f = &c->folder[c->nfolders];
                    memset(f, 0, sizeof *f);
                    put(f->name, sizeof f->name, name);
                    f->disk_x100 = CELLAR_REFUSED;
                    top = (int32_t) c->nfolders++;
                }
                if (nd == CELLAR_DIRS) return refuse(c, CELLAR_F_LIMITS, ZXM_E_TOOBIG);
                memcpy(g_dir[nd], path, strlen(path) + 1u);
                g_top[nd++] = top;
                continue;
            }
            uint64_t size;
            if (zxm_get_u64(l, o, "size", &size)) return ZXM_E_PROTOCOL;
            if (size > (uint64_t) CELLAR_BYTES_MAX)
                return refuse(c, CELLAR_F_TOO_BIG, ZXM_E_TOOBIG);
            int64_t *bytes = NULL, *files = NULL;
            if (mode == WALK_SURVEY) {
                bytes = top >= 0 ? &c->folder[top].bytes : &c->loose_bytes;
                files = top >= 0 ? &c->folder[top].files : &c->loose_files;
            } else if (mode == WALK_TRASH) {
                bytes = &c->trash_bytes;
            }
            if (bytes && !add(bytes, (int64_t) size))
                return refuse(c, CELLAR_F_TOO_BIG, ZXM_E_TOOBIG);
            if (files) (*files)++;
        }
    }
    return ZXM_OK;
}

int cellar_survey(cellar_t *c)
{
    static int64_t sizes[CELLAR_FOLDERS + 1u];
    c->surveyed = false;
    c->nfolders = 0;
    c->loose_bytes = c->loose_files = 0;
    c->base_bytes = c->base_files = 0;
    int rc = walk(c, c->base, WALK_SURVEY);
    if (rc) return rc;
    int64_t files = c->loose_files;
    for (uint32_t i = 0; i < c->nfolders; i++) {
        sizes[i] = c->folder[i].bytes;
        if (!add(&files, c->folder[i].files)) return refuse(c, CELLAR_F_TOO_BIG, ZXM_E_TOOBIG);
    }
    sizes[c->nfolders] = c->loose_bytes;
    int64_t whole = cellar_total(sizes, c->nfolders + 1u); /* CR3 */
    if (whole == CELLAR_REFUSED) return refuse(c, CELLAR_F_TOO_BIG, ZXM_E_TOOBIG);
    for (uint32_t i = 0; i < c->nfolders; i++)
        c->folder[i].share_x100 = cellar_share_x100(c->folder[i].bytes, whole); /* CR2 */
    c->base_bytes = whole;
    c->base_files = files;
    c->surveyed = true;
    volume_shares(c);
    return ZXM_OK;
}

int cellar_trash_size(cellar_t *c)
{
    c->trash_known = false;
    c->trash_items = c->trash_bytes = 0;
    int rc = walk(c, CELLAR_TRASH, WALK_TRASH);
    if (rc) return rc;
    c->trash_known = true;
    volume_shares(c);
    return ZXM_OK;
}

int cellar_empty_trash(cellar_t *c, bool approved, uint32_t *removed)
{
    zxm_link_t *l = c->link;
    uint64_t n;
    if (!removed) return ZXM_E_ARG;
    *removed = 0;
    if (!approved) return ZXM_E_STATE; /* the owner's yes first: nothing is asked */
    zxm_begin(l, "files.empty_trash");
    int rc = zxm_call(l);
    if (rc) return rc;
    if (zxm_get_u64(l, l->result, "removed", &n) || n > UINT32_MAX) return ZXM_E_PROTOCOL;
    *removed = (uint32_t) n;
    c->trash_items = c->trash_bytes = 0;
    c->trash_known = true;
    volume_shares(c);
    return ZXM_OK;
}

int cellar_check(cellar_t *c)
{
    zxm_link_t *l = c->link;
    uint64_t ms;
    c->checked = false;
    zxm_begin(l, "sys.info");
    int rc = zxm_call(l);
    if (rc) return rc;
    if (zxm_get_u64(l, l->result, "time_ms", &ms) || ms > (uint64_t) INT64_MAX)
        return ZXM_E_PROTOCOL;
    c->now = (int64_t) (ms / 1000u);
    c->misnamed = c->misdated = 0;
    c->first_misnamed[0] = c->first_misdated[0] = 0;
    rc = walk(c, c->base, WALK_CHECK);
    if (rc) return rc;
    c->checked = true;
    return ZXM_OK;
}

/* append to the advice, cut at its end */
static void say(cellar_t *c, const char *text)
{
    size_t at = strlen(c->advice);
    snprintf(c->advice + at, sizeof c->advice - at, "%s", text);
}

/* one kind of finding: its first path and how many more */
static void say_item(cellar_t *c, const char *path, uint32_t count, const char *what)
{
    size_t at = strlen(c->advice);
    snprintf(c->advice + at, sizeof c->advice - at, "\"%s\" and %u more %s ", path,
             (unsigned) (count - 1u), what);
}

int cellar_repair_advice(cellar_t *c, bool approved)
{
    char more[64];
    if (!c->measured || !c->checked) return ZXM_E_STATE;
    /* avail is 0..CELLAR_BYTES_MAX: no overflow; below 1 is no room */
    c->room = c->avail - CELLAR_REPAIR_RESERVE;
    c->repair_safe = cellar_repair_safe(c->room, c->used_x100, approved ? 1 : 0); /* CR4 */
    c->advice[0] = 0;
    if (!c->misnamed && !c->misdated) {
        say(c, "The Hoard checks clean: nothing to repair.");
        return ZXM_OK;
    }
    if (c->misnamed)
        say_item(c, c->first_misnamed, c->misnamed,
                 "have names ZXV cannot open: rename them outside ZXV.");
    if (c->misdated)
        say_item(c, c->first_misdated, c->misdated,
                 "are dated after the clock: open and save them again.");
    if (c->repair_safe) {
        say(c, "Safe to repair now.");
        return ZXM_OK;
    }
    say(c, "Not safe to repair yet:");
    if (c->room < 1) {
        snprintf(more, sizeof more, " free %lld more bytes first;", (long long) (1 - c->room));
        say(c, more);
    }
    if (c->used_x100 < 0 || c->used_x100 > CELLAR_FILL_MAX)
        say(c, " the volume is over 95% full, empty the Trash first;");
    if (!approved) say(c, " waiting for your confirmation.");
    return ZXM_OK;
}

int32_t cellar_find(const cellar_t *c, const char *name)
{
    for (uint32_t i = 0; i < c->nfolders; i++)
        if (!strcmp(c->folder[i].name, name)) return (int32_t) i;
    return -1;
}
