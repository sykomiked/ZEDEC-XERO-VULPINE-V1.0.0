/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_share.c — the share sheet (zui_share.h). */
#include "zui_share.h"

static bool same(const char *a, const char *b)
{
    uint32_t i = 0;
    for (; a[i] && a[i] == b[i]; i++) {
    }
    return a[i] == b[i];
}

static void copy(char *d, uint32_t cap, const char *s)
{
    uint32_t i = 0;
    for (; s && s[i] && i + 1u < cap; i++) d[i] = s[i];
    d[i] = 0;
}

void zui_share_init(zui_share_t *sh)
{
    uint8_t *z = (uint8_t *) sh;
    for (uint32_t i = 0; i < sizeof *sh; i++) z[i] = 0;
}

bool zui_share_add(zui_share_t *sh, zxapp_t *app)
{
    if (!sh || !app || sh->napps >= ZUI_SHARE_APPS) return false;
    for (uint32_t i = 0; i < sh->napps; i++)
        if (sh->app[i] == app) return false;
    sh->app[sh->napps++] = app;
    return true;
}

static void forget(zui_share_t *sh, const char *id)
{
    uint32_t k = 0;
    for (uint32_t i = 0; i < sh->nrecent; i++)
        if (!same(sh->recent[i], id)) {
            if (k != i) copy(sh->recent[k], ZXAPP_ID_MAX, sh->recent[i]);
            k++;
        }
    sh->nrecent = k;
}

bool zui_share_remove(zui_share_t *sh, const zxapp_t *app)
{
    if (!sh || !app) return false;
    for (uint32_t i = 0; i < sh->napps; i++)
        if (sh->app[i] == app) {
            for (uint32_t j = i + 1u; j < sh->napps; j++) sh->app[j - 1u] = sh->app[j];
            sh->app[--sh->napps] = 0;
            forget(sh, app->id);
            return true;
        }
    return false;
}

static int32_t recent_at(const zui_share_t *sh, const char *id)
{
    for (uint32_t i = 0; i < sh->nrecent; i++)
        if (same(sh->recent[i], id)) return (int32_t) i;
    return -1;
}

uint32_t zui_share_open(const zui_share_t *sh, zui_share_sheet_t *sheet, const char *verb,
                        const char *type, const char *from)
{
    if (!sheet) return 0;
    sheet->n = sheet->more = 0;
    sheet->sel = -1;
    copy(sheet->verb, sizeof sheet->verb, verb);
    copy(sheet->type, sizeof sheet->type, type);
    if (!sh || !verb || !type) return 0;
    zxapp_handler_t h[ZUI_SHARE_APPS];
    uint32_t n =
        zxapp_handlers((const zxapp_t *const *) sh->app, sh->napps, verb, type, h, ZUI_SHARE_APPS);
    if (n > ZUI_SHARE_APPS) n = ZUI_SHARE_APPS; /* cannot happen: one per app */
    bool taken[ZUI_SHARE_APPS] = {false};
    /* SH2: recents first, in recency order, then the rest as found */
    for (uint32_t r = 0; r <= sh->nrecent; r++)
        for (uint32_t i = 0; i < n; i++) {
            const zxapp_t *a = h[i].a;
            if (taken[i]) continue;
            if (from && from[0] && same(a->id, from)) {
                taken[i] = true;
                continue;
            }
            int32_t at = recent_at(sh, a->id);
            bool now = r < sh->nrecent ? at == (int32_t) r : true;
            if (!now) continue;
            taken[i] = true;
            if (sheet->n >= ZUI_SHARE_TARGETS) {
                sheet->more++;
                continue;
            }
            zui_share_target_t *T = &sheet->t[sheet->n++];
            uint8_t *z = (uint8_t *) T; /* SH5: padding too, so sheets compare */
            for (uint32_t b = 0; b < sizeof *T; b++) z[b] = 0;
            T->app = sh->app[h[i].app];
            T->in = h[i].in;
            T->rank = h[i].rank;
            T->recent = r < sh->nrecent;
        }
    if (sheet->n) sheet->sel = 0;
    return sheet->n;
}

static void remember(zui_share_t *sh, const char *id)
{
    forget(sh, id);
    uint32_t n = sh->nrecent < ZUI_SHARE_RECENT ? sh->nrecent : ZUI_SHARE_RECENT - 1u;
    for (uint32_t i = n; i > 0; i--) copy(sh->recent[i], ZXAPP_ID_MAX, sh->recent[i - 1u]);
    copy(sh->recent[0], ZXAPP_ID_MAX, id);
    sh->nrecent = n + 1u;
}

int zui_share_pick(zui_share_t *sh, const zui_share_sheet_t *sheet, uint32_t i, zxapp_host_t *h,
                   const zxapp_arg_t *args, uint32_t nargs, zxapp_num_t *out, uint32_t cap,
                   uint32_t *n_out)
{
    if (n_out) *n_out = 0;
    if (!sh || !sheet || !h || i >= sheet->n) return ZXAPP_E_ARG;
    const zui_share_target_t *T = &sheet->t[i];
    bool installed = false; /* SH4: only a target that is still installed */
    for (uint32_t k = 0; k < sh->napps && !installed; k++) installed = sh->app[k] == T->app;
    if (!installed || !T->in) return ZXAPP_E_ARG;
    int rc = zxapp_call(T->app, h, T->in->entry, args, nargs, out, cap, n_out);
    if (rc == ZXAPP_OK) remember(sh, T->app->id);
    return rc;
}

void zui_share_move(zui_share_sheet_t *sheet, int32_t step)
{
    if (!sheet || !sheet->n) return;
    int32_t n = (int32_t) sheet->n;
    if (sheet->sel < 0 || sheet->sel >= n) {
        sheet->sel = step < 0 ? n - 1 : 0;
        return;
    }
    sheet->sel = ((sheet->sel + step) % n + n) % n;
}
