/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* keepsake_model.c — Keepsake (see keepsake_model.h). */
#include <stdio.h>
#include <string.h>

#include "keepsake_model.h"

const zxm_ai_action_t KEEPSAKE_AI[KEEPSAKE_AI_COUNT] = {
    {"Explain this check", "Companion", 300},
};

void keepsake_init(keepsake_t *k, zxm_link_t *link)
{
    memset(k, 0, sizeof *k);
    k->link = link;
}

static void forget(keepsake_t *k)
{
    k->nsnaps = k->total = k->nvers = 0;
    k->vpath[0] = 0;
    memset(&k->check, 0, sizeof k->check);
    memset(&k->last, 0, sizeof k->last);
}

int keepsake_set_space(keepsake_t *k, const char *space)
{
    if (!space) space = "";
    if (strlen(space) >= sizeof k->space) return ZXM_E_ARG;
    snprintf(k->space, sizeof k->space, "%s", strcmp(space, "home") ? space : "");
    forget(k);
    return ZXM_OK;
}

int keepsake_set_dest(keepsake_t *k, const char *dest)
{
    if (!dest || !dest[0] || strlen(dest) > KEEPSAKE_DEST) return ZXM_E_ARG;
    memcpy(k->dest, dest, strlen(dest) + 1);
    forget(k);
    return ZXM_OK;
}

/* a call on the space and destination in use */
static int start(keepsake_t *k, const char *method)
{
    if (!k->dest[0]) return ZXM_E_STATE;
    zxm_begin(k->link, method);
    zxm_p_space(k->link, k->space);
    zxm_p_str(k->link, "dest", k->dest);
    return ZXM_OK;
}

static int u32(zxm_link_t *l, int32_t o, const char *key, uint32_t *v)
{
    uint64_t x;
    if (zxm_get_u64(l, o, key, &x) || x > UINT32_MAX) return ZXM_E_PROTOCOL;
    *v = (uint32_t) x;
    return ZXM_OK;
}

int keepsake_refresh(keepsake_t *k)
{
    int rc = start(k, "backup.list");
    if (rc) return rc;
    zxm_link_t *l = k->link;
    zxm_p_u64(l, "max", KEEPSAKE_SNAPS);
    if ((rc = zxm_call(l)) != 0) return rc;
    static keepsake_snap_t got[KEEPSAKE_SNAPS];
    uint32_t total;
    int32_t a = zxm_get(l, l->result, "snapshots");
    if (u32(l, l->result, "total", &total) || a < 0 || l->tok[a].type != W4_J_ARR ||
        zxm_len(l, a) > KEEPSAKE_SNAPS || zxm_len(l, a) > total)
        return ZXM_E_PROTOCOL;
    uint32_t n = zxm_len(l, a);
    for (uint32_t i = 0; i < n; i++) {
        int32_t o = zxm_at(l, a, i);
        keepsake_snap_t *s = &got[i];
        if (o < 0 || l->tok[o].type != W4_J_OBJ || u32(l, o, "snapshot", &s->id) ||
            zxm_get_i64(l, o, "time", &s->time) || u32(l, o, "files", &s->files) ||
            zxm_get_u64(l, o, "bytes", &s->bytes) || zxm_get_bool(l, o, "damaged", &s->damaged))
            return ZXM_E_PROTOCOL;
    }
    memcpy(k->snap, got, n * sizeof *got);
    k->nsnaps = n;
    k->total = total;
    return ZXM_OK;
}

int keepsake_backup(keepsake_t *k, bool full, bool owner_confirmed)
{
    if (!owner_confirmed) return ZXM_E_STATE;
    int rc = start(k, "backup.run");
    if (rc) return rc;
    zxm_link_t *l = k->link;
    if (full) zxm_p_bool(l, "full", true);
    if ((rc = zxm_call(l)) != 0) return rc;
    keepsake_run_t r;
    if (u32(l, l->result, "snapshot", &r.snapshot) || zxm_get_i64(l, l->result, "time", &r.time) ||
        u32(l, l->result, "files", &r.files) || u32(l, l->result, "new_files", &r.new_files) ||
        u32(l, l->result, "skipped", &r.skipped) || zxm_get_u64(l, l->result, "bytes", &r.bytes) ||
        zxm_get_u64(l, l->result, "new_bytes", &r.new_bytes) || r.new_files > r.files ||
        r.new_bytes > r.bytes || !r.snapshot)
        return ZXM_E_PROTOCOL;
    k->last = r;
    memset(&k->check, 0, sizeof k->check); /* the new backup is not checked yet */
    return keepsake_refresh(k);
}

int keepsake_versions(keepsake_t *k, const char *path)
{
    if (!path || !path[0] || strlen(path) > KEEPSAKE_PATH) return ZXM_E_ARG;
    int rc = start(k, "backup.versions");
    if (rc) return rc;
    zxm_link_t *l = k->link;
    zxm_p_str(l, "path", path);
    if ((rc = zxm_call(l)) != 0) return rc;
    static keepsake_version_t got[KEEPSAKE_VERS];
    int32_t a = zxm_get(l, l->result, "versions");
    if (a < 0 || l->tok[a].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    uint32_t n = zxm_len(l, a) < KEEPSAKE_VERS ? zxm_len(l, a) : KEEPSAKE_VERS;
    for (uint32_t i = 0; i < n; i++) {
        int32_t o = zxm_at(l, a, i);
        keepsake_version_t *v = &got[i];
        if (o < 0 || l->tok[o].type != W4_J_OBJ || u32(l, o, "snapshot", &v->snapshot) ||
            zxm_get_i64(l, o, "time", &v->time) || zxm_get_u64(l, o, "size", &v->size) ||
            zxm_get_i64(l, o, "mtime", &v->mtime) ||
            zxm_get_str(l, o, "hash", v->hash, sizeof v->hash) || strlen(v->hash) != 64)
            return ZXM_E_PROTOCOL;
    }
    memcpy(k->ver, got, n * sizeof *got);
    k->nvers = n;
    memcpy(k->vpath, path, strlen(path) + 1);
    return ZXM_OK;
}

int keepsake_check(keepsake_t *k, uint32_t snapshot)
{
    int rc = start(k, "backup.verify");
    if (rc) return rc;
    zxm_link_t *l = k->link;
    if (snapshot) zxm_p_u64(l, "snapshot", snapshot);
    if ((rc = zxm_call(l)) != 0) return rc;
    static keepsake_check_t c;
    memset(&c, 0, sizeof c);
    bool ok;
    if (u32(l, l->result, "snapshot", &c.snapshot) || u32(l, l->result, "files", &c.files) ||
        u32(l, l->result, "bad", &c.bad) || u32(l, l->result, "missing", &c.missing) ||
        zxm_get_u64(l, l->result, "bytes", &c.bytes) || zxm_get_bool(l, l->result, "ok", &ok) ||
        zxm_get_str(l, l->result, "first_bad", c.first_bad, sizeof c.first_bad) ||
        ok != (!c.bad && !c.missing) || c.bad > c.files || c.missing > c.files - c.bad)
        return ZXM_E_PROTOCOL;
    c.done = true;
    k->check = c;
    return ZXM_OK;
}

bool keepsake_tested(const keepsake_t *k)
{
    const keepsake_check_t *c = &k->check;
    return c->done && k->nsnaps && c->snapshot == k->snap[0].id && !c->bad && !c->missing;
}

int keepsake_restore(keepsake_t *k, uint32_t snapshot, const char *path, const char *to,
                     bool owner_confirmed)
{
    if ((path && strlen(path) > KEEPSAKE_PATH) || (to && strlen(to) > KEEPSAKE_PATH))
        return ZXM_E_ARG;
    if (!owner_confirmed) return ZXM_E_STATE;
    int rc = start(k, "backup.restore");
    if (rc) return rc;
    zxm_link_t *l = k->link;
    if (snapshot) zxm_p_u64(l, "snapshot", snapshot);
    if (path && path[0]) zxm_p_str(l, "path", path);
    if (to && to[0]) zxm_p_str(l, "to", to);
    if ((rc = zxm_call(l)) != 0) return rc;
    static char landed[KEEPSAKE_PATH + 1];
    uint32_t files;
    if (zxm_get_str(l, l->result, "to", landed, sizeof landed) || !landed[0] ||
        u32(l, l->result, "files", &files) || !files)
        return ZXM_E_PROTOCOL;
    memcpy(k->landed, landed, strlen(landed) + 1);
    k->restored = files;
    return ZXM_OK;
}

int keepsake_prune(keepsake_t *k, uint32_t keep, bool owner_confirmed)
{
    if (!keep) return ZXM_E_ARG;
    if (!owner_confirmed) return ZXM_E_STATE;
    int rc = start(k, "backup.prune");
    if (rc) return rc;
    zxm_link_t *l = k->link;
    zxm_p_u64(l, "keep", keep);
    if ((rc = zxm_call(l)) != 0) return rc;
    uint32_t s, o;
    uint64_t f;
    if (u32(l, l->result, "snapshots_removed", &s) || u32(l, l->result, "objects_removed", &o) ||
        zxm_get_u64(l, l->result, "freed", &f))
        return ZXM_E_PROTOCOL;
    k->pruned_snaps = s;
    k->pruned_objects = o;
    k->freed = f;
    return keepsake_refresh(k);
}

int keepsake_ai(keepsake_t *k, int32_t action)
{
    if (action < 0 || action >= KEEPSAKE_AI_COUNT) return ZXM_E_ARG;
    const keepsake_check_t *c = &k->check;
    if (!c->done || (!c->bad && !c->missing)) return ZXM_E_STATE; /* nothing wrong */
    static char in[ZXM_ASK_MAX];
    int n = snprintf(in, sizeof in,
                     "Backup %u was checked: %u files, %u read back wrong, %u missing.\n"
                     "First problem: %s\n",
                     c->snapshot, c->files, c->bad, c->missing, c->first_bad);
    if (n < 0) return ZXM_E_ARG;
    if ((size_t) n >= sizeof in) n = (int) zxm_utf8_cut(in, sizeof in - 1);
    return zxm_ai_ask(k->link, &KEEPSAKE_AI[action], action,
                      "This is the result of checking a backup. Say in plain words what it means "
                      "and what the owner can do: a full backup re-reads every file and repairs "
                      "kept copies, and the disk may be failing. Be brief.",
                      in, (uint32_t) n, &k->held);
}

int keepsake_ai_accept(keepsake_t *k)
{
    return zxm_held_accept(&k->held);
}

int keepsake_ai_reject(keepsake_t *k)
{
    return zxm_held_reject(&k->held);
}
