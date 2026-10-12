/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* murmuration.c — Murmuration's native screen (murmuration.h). */
#include "murmuration.h"
#include "zui_emote.h"
#include "zui_tokens.h"

static const murm_agent_t *agent(const murm_view_t *v, int32_t i)
{
    return i >= 0 && (uint32_t) i < v->m->sw.n ? &v->m->sw.agent[i] : 0;
}

static uint64_t total(const murm_agent_t *a)
{
    uint64_t t = a->re + a->mk;
    t = t < a->re ? UINT64_MAX : t;
    return t + a->im < t ? UINT64_MAX : t + a->im;
}

/* "12.3%" from per mille. */
static uint32_t permille(char *out, uint32_t cap, uint32_t pm)
{
    uint32_t n = zv_minor(out, cap, (int64_t) pm, 1);
    return zv_cat(out + n, cap - n, "%", 0) + n;
}

static swarm_feeling_t feeling_of(uint32_t cp, uint8_t intensity)
{
    int e = murm_emotion(cp);
    swarm_feeling_t f = {SWARM_EMO_NEUTRAL, 0};
    if (e >= 0) f.emotion = (swarm_emotion_t) e;
    f.intensity = intensity > SWARM_EMO_MAX_INTENSITY ? SWARM_EMO_MAX_INTENSITY : intensity;
    return f;
}

static void row(const zui_ui_t *ui, zui_id_t list, int32_t i, zui_row_t *out, void *ctx)
{
    (void) ui;
    (void) list;
    const murm_view_t *v = ctx;
    const murm_agent_t *a = agent(v, i);
    if (!a) return;
    /* the feeling as a word: the face is in the detail pane, and a text
     * face may have no colour-emoji glyphs */
    zv_cat(out->text, sizeof out->text, a->name, 0);
    /* over the limits, that is what the row says instead of the feeling */
    zv_cat(out->detail, sizeof out->detail,
           v->over[i] ? "over limit"
                      : zui_emote_word(feeling_of(a->codepoint, a->intensity).emotion),
           "  \xC2\xB7  ");
    char pm[16];
    permille(pm, sizeof pm, v->share[i]);
    zv_cat(out->detail, sizeof out->detail, out->detail, pm);
    out->lead_rgb = a->witness == MURM_WIT_TRUE   ? ZUI_OK
                    : a->witness == MURM_WIT_GLUT ? ZUI_INFO
                                                  : ZUI_ALERT;
    out->muted = total(a) == 0;
}

/* U2: the selected agent's face, or the swarm's mood. */
static void face(zui_ui_t *ui, zui_id_t w, zui_surface_t *s, zui_rect_t r, void *ctx)
{
    (void) w;
    const murm_view_t *v = ctx;
    const murm_swarm_t *sw = &v->m->sw;
    const murm_agent_t *a = agent(v, v->m->sel);
    int32_t rad = zui_px(&ui->screen, ZUI_R_PANEL);
    zui_fill_rrect(s, r, rad, ZUI_PANEL, 255);
    zui_engrave(s, r, rad, ZUI_ENGRAVE_LO, ZUI_ENGRAVE_HI, 40);
    if (!v->m->refreshes) return;
    swarm_feeling_t f = a ? feeling_of(a->codepoint, a->intensity)
                          : feeling_of(sw->mood_codepoint, sw->mood_intensity);
    zui_health_t h = ZUI_HEALTH_OK;
    if (sw->ledger_halt != 0 || (a && a->witness == MURM_WIT_FALSE)) h = ZUI_HEALTH_FAILING;
    zui_face_t fc = zui_face_feeling(f);
    int32_t d = r.w < r.h ? r.w : r.h;
    zui_face_draw(s, &fc, h, r.x + r.w / 2, r.y + r.h / 2, d * 3 / 8, ui->now_ms);
}

void murm_view_build(murm_view_t *v, zui_ui_t *ui, murmuration_t *m)
{
    v->m = m;
    v->show_detail = false;
    v->app = 0;
    v->host = 0;
    v->ntop = 0;
    v->kron = v->eve = false;
    for (uint32_t i = 0; i < MURM_AGENTS; i++) {
        v->share[i] = 0;
        v->over[i] = false;
    }
    zv_shell_t *s = &v->sh;
    zv_shell_build(s, ui, "Murmuration");
    zv_shell_palette(s, MURM_AI, MURM_AI_COUNT);
    v->list_pane = zui_box(ui, s->body, ZUI_COL);
    zui_set_size(ui, v->list_pane, ZUI_SIZE_FLEX, 1);
    zui_set_pad(ui, v->list_pane, 0, ZUI_S2);
    zui_id_t bar = zui_box(ui, v->list_pane, ZUI_ROW);
    v->refresh = zui_button(ui, bar, "Refresh", ZUI_BTN_PRIMARY);
    zui_set_shortcut(ui, v->refresh, "R");
    v->agents = zui_list(ui, v->list_pane, row, v);
    zui_set_name(ui, v->agents, "Agents");
    zui_set_text(ui, v->agents, "No snapshot yet: press Refresh");
    zui_set_size(ui, v->agents, ZUI_SIZE_FLEX, 1);
    v->detail = zui_box(ui, s->body, ZUI_COL);
    zui_set_pad(ui, v->detail, ZUI_S3, ZUI_S2);
    v->back = zui_button(ui, v->detail, "Back", ZUI_BTN_GHOST);
    v->face = zui_canvas(ui, v->detail, face, v);
    zui_set_name(ui, v->face, "Feeling");
    zui_set_size(ui, v->face, ZUI_SIZE_FIXED, 160);
    v->who = zui_label(ui, v->detail, "");
    v->feeling = zui_label(ui, v->detail, "");
    v->allot = zui_label(ui, v->detail, "");
    zui_set_font(ui, v->allot, ZUI_TEXT_MONO, ZUI_TEXT_2);
    v->witness = zui_label(ui, v->detail, "");
    v->limits = zui_label(ui, v->detail, "");
    zui_separator(ui, v->detail);
    v->busiest = zui_label(ui, v->detail, "");
    v->model = zui_label(ui, v->detail, "");
    v->earnings = zui_label(ui, v->detail, "");
    zui_set_font(ui, v->earnings, ZUI_TEXT_MONO, ZUI_TEXT_2);
    murm_view_sync(v);
}

void murm_view_logic(murm_view_t *v, zxapp_t *app, zxapp_host_t *host)
{
    v->app = app;
    v->host = host;
    murm_view_sync(v);
}

/* U1: the KRON entries over the kept agents; false sends the caller to C. */
static bool kron_numbers(murm_view_t *v)
{
    const murm_swarm_t *sw = &v->m->sw;
    if (!v->app || !v->host || sw->truncated || !sw->n) return false;
    int64_t t[MURM_AGENTS];
    for (uint32_t i = 0; i < sw->n; i++) {
        uint64_t x = total(&sw->agent[i]);
        if (x > (uint64_t) INT64_MAX / 1000u) return false; /* KRON's integers are 64-bit */
        t[i] = (int64_t) x;
    }
    zxapp_arg_t arg = {"t", t, sw->n};
    zxapp_num_t out[MURM_AGENTS];
    uint32_t n = 0;
    if (zxapp_call(v->app, v->host, "cycle_share", &arg, 1, out, MURM_AGENTS, &n) != ZXAPP_OK ||
        n != sw->n)
        return false;
    uint32_t share[MURM_AGENTS];
    for (uint32_t i = 0; i < n; i++) {
        if (out[i].den != 1 || out[i].num < 0 || out[i].num > 1000) return false;
        share[i] = (uint32_t) out[i].num;
    }
    zxapp_num_t top[MURM_VIEW_BUSIEST];
    uint32_t k = 0;
    if (zxapp_call(v->app, v->host, "busiest", &arg, 1, top, MURM_VIEW_BUSIEST, &k) != ZXAPP_OK)
        return false;
    for (uint32_t i = 0; i < k; i++)
        if (top[i].den != 1 || top[i].num < 0 || top[i].num >= (int64_t) sw->n) return false;
    for (uint32_t i = 0; i < n; i++) v->share[i] = share[i];
    for (uint32_t i = 0; i < k; i++) v->top[i] = (int32_t) top[i].num;
    v->ntop = k;
    return true;
}

/* The same numbers in C: the model's share, and the three largest totals,
 * earliest first on a tie (KRON's grade down is stable too). */
static void c_numbers(murm_view_t *v)
{
    const murm_swarm_t *sw = &v->m->sw;
    for (uint32_t i = 0; i < MURM_AGENTS; i++) v->share[i] = murm_cycle_share(v->m, i);
    v->ntop = 0;
    for (uint32_t k = 0; k < MURM_VIEW_BUSIEST && k < sw->n; k++) {
        int32_t best = -1;
        for (uint32_t i = 0; i < sw->n; i++) {
            bool used = false;
            for (uint32_t j = 0; j < k; j++) used |= v->top[j] == (int32_t) i;
            if (!used && (best < 0 || total(&sw->agent[i]) > total(&sw->agent[best])))
                best = (int32_t) i;
        }
        v->top[v->ntop++] = best;
    }
}

/* U4: Eve's within_limits over each agent's share and intensity. */
static bool eve_limits(murm_view_t *v)
{
    const murm_swarm_t *sw = &v->m->sw;
    if (!v->app || !v->host || !sw->n) return false;
    int64_t share[MURM_AGENTS], inten[MURM_AGENTS];
    for (uint32_t i = 0; i < sw->n; i++) {
        share[i] = v->share[i];
        inten[i] = sw->agent[i].intensity;
    }
    zxapp_arg_t args[2] = {{"share", share, sw->n}, {"intensity", inten, sw->n}};
    zxapp_num_t out[MURM_AGENTS];
    uint32_t n = 0;
    if (zxapp_call(v->app, v->host, "within_limits", args, 2, out, MURM_AGENTS, &n) != ZXAPP_OK ||
        n != sw->n)
        return false;
    for (uint32_t i = 0; i < n; i++)
        if (out[i].den != 0 || (out[i].num != ZXAPP_TRUE && out[i].num != ZXAPP_FALSE))
            return false;
    for (uint32_t i = 0; i < n; i++) v->over[i] = out[i].num == ZXAPP_FALSE;
    return true;
}

static void c_limits(murm_view_t *v)
{
    const murm_swarm_t *sw = &v->m->sw;
    for (uint32_t i = 0; i < MURM_AGENTS; i++)
        v->over[i] = i < sw->n && (v->share[i] > MURM_VIEW_SHARE_CAP ||
                                   sw->agent[i].intensity > SWARM_EMO_MAX_INTENSITY);
}

static void put_u(char *out, uint32_t cap, const char *a, uint64_t x, const char *b)
{
    char d[24];
    zv_u64(d, sizeof d, x);
    zv_cat(out, cap, out, a);
    zv_cat(out, cap, out, d);
    zv_cat(out, cap, out, b);
}

static void sync_detail(murm_view_t *v)
{
    zui_ui_t *ui = v->sh.ui;
    const murmuration_t *m = v->m;
    const murm_swarm_t *sw = &m->sw;
    const murm_agent_t *a = agent(v, m->sel);
    char t[ZUI_LABEL_MAX];
    t[0] = 0;
    if (a) {
        zv_cat(t, sizeof t, a->name, "  \xC2\xB7  ");
        if (a->level)
            put_u(t, sizeof t, "level ", a->level, "");
        else
            zv_cat(t, sizeof t, t, "companion");
    } else if (m->refreshes) {
        put_u(t, sizeof t, "The swarm  \xC2\xB7  ", sw->n_total,
              sw->n_total == 1 ? " agent" : " agents");
    }
    zui_set_text(ui, v->who, t);
    t[0] = 0;
    swarm_feeling_t f = a ? feeling_of(a->codepoint, a->intensity)
                          : feeling_of(sw->mood_codepoint, sw->mood_intensity);
    if (m->refreshes) {
        zv_cat(t, sizeof t, a ? "Feels " : "Mood ", zui_emote_word(f.emotion));
        put_u(t, sizeof t, ", intensity ", f.intensity, "");
    }
    zui_set_text(ui, v->feeling, t);
    t[0] = 0;
    if (a) {
        put_u(t, sizeof t, "", a->re, " real + ");
        put_u(t, sizeof t, "", a->mk, " market + ");
        put_u(t, sizeof t, "", a->im, "i  = ");
        char pm[16];
        permille(pm, sizeof pm, v->share[m->sel]);
        zv_cat(t, sizeof t, t, pm);
    } else if (m->refreshes) {
        put_u(t, sizeof t, "Cycle ", sw->cycle, "  \xC2\xB7  ");
        put_u(t, sizeof t, "", sw->tokens_per_cycle, " tokens per cycle");
    }
    zui_set_text(ui, v->allot, t);
    t[0] = 0;
    if (a) {
        zv_cat(t, sizeof t,
               a->witness == MURM_WIT_TRUE   ? "Agreed by "
               : a->witness == MURM_WIT_GLUT ? "Contested by "
                                             : "Does not add up, says ",
               a->witness_by);
    } else if (m->refreshes) {
        zv_cat(t, sizeof t, sw->ledger_halt ? "Ledger halted" : "Ledger running",
               sw->audit[0] ? "  \xC2\xB7  audit " : 0);
        zv_cat(t, sizeof t, t, sw->audit);
    }
    zui_set_text(ui, v->witness, t);
    t[0] = 0;
    if (a && v->over[m->sel])
        zv_cat(t, sizeof t, "Over the swarm's limits: more than half a cycle", 0);
    else if (!a && m->refreshes) {
        uint32_t over = 0;
        for (uint32_t i = 0; i < sw->n; i++) over += v->over[i];
        if (over)
            put_u(t, sizeof t, "", over,
                  over == 1 ? " agent over the limits" : " agents over the limits");
    }
    zui_set_text(ui, v->limits, t);
    zui_set_font(ui, v->limits, ZUI_TEXT_LABEL, ZUI_ALERT);
}

void murm_view_sync(murm_view_t *v)
{
    zui_ui_t *ui = v->sh.ui;
    murmuration_t *m = v->m;
    const murm_swarm_t *sw = &m->sw;
    v->kron = kron_numbers(v);
    if (!v->kron) c_numbers(v);
    v->eve = eve_limits(v);
    if (!v->eve) c_limits(v);
    zui_list_set_rows(ui, v->agents, (int32_t) sw->n);
    zui_list_select(ui, v->agents, m->sel);
    sync_detail(v);
    char t[ZUI_LABEL_MAX];
    t[0] = 0;
    for (uint32_t i = 0; i < v->ntop; i++) {
        zv_cat(t, sizeof t, t, i ? ", " : "Busiest: ");
        zv_cat(t, sizeof t, t, sw->agent[v->top[i]].name);
    }
    zui_set_text(ui, v->busiest, t);
    t[0] = 0;
    if (m->have_model)
        zv_cat(t, sizeof t, m->model.loaded ? "Model " : "No model loaded",
               m->model.loaded ? m->model.name : 0);
    else if (m->refreshes)
        zv_cat(t, sizeof t, "Model details not available", 0);
    zui_set_text(ui, v->model, t);
    t[0] = 0;
    if (m->have_earnings) {
        char amt[32];
        zv_minor(amt, sizeof amt, m->earn.balance_minor, 2);
        zv_cat(t, sizeof t, "Earned ", amt);
        zv_cat(t, sizeof t, t, " ");
        zv_cat(t, sizeof t, t, m->earn.currency);
        put_u(t, sizeof t, " over ", m->earn.cycles, m->earn.cycles == 1 ? " cycle" : " cycles");
    } else if (m->refreshes) {
        zv_cat(t, sizeof t, "Earnings not available", 0);
    }
    zui_set_text(ui, v->earnings, t);
    bool compact = zv_compact(&v->sh);
    zui_set_hidden(ui, v->back, !compact);
    zui_set_hidden(ui, v->list_pane, compact && v->show_detail);
    zui_set_hidden(ui, v->detail, compact && !v->show_detail);
    zui_set_size(ui, v->detail, compact ? ZUI_SIZE_FLEX : ZUI_SIZE_FIXED, compact ? 1 : 340);
    t[0] = 0;
    if (m->refreshes) {
        put_u(t, sizeof t, "", sw->n_total, sw->n_total == 1 ? " agent" : " agents");
        zv_cat(t, sizeof t, t, v->kron ? "  \xC2\xB7  shares in KRON" : "  \xC2\xB7  shares in C");
        zv_cat(t, sizeof t, t, v->eve ? ", limits in Eve" : ", limits in C");
    }
    zv_shell_status(&v->sh, t);
    zv_shell_held(&v->sh, &m->held);
    zui_invalidate(ui, v->face);
}

bool murm_view_action(murm_view_t *v, zui_id_t w, zui_action_t a, int32_t arg)
{
    murmuration_t *m = v->m;
    zv_shell_t *s = &v->sh;
    int rc = ZXM_OK;
    if (w == v->agents && (a == ZUI_ACT_SELECT || a == ZUI_ACT_OPEN)) {
        rc = murm_select(m, arg);
        if (zv_compact(s) && arg >= 0) v->show_detail = true;
    } else if (w == v->refresh && a == ZUI_ACT_PRESS) {
        zui_set_hidden(s->ui, s->bar, false);
        rc = murm_refresh(m);
        zui_set_hidden(s->ui, s->bar, true);
    } else if (w == v->back && a == ZUI_ACT_PRESS) {
        v->show_detail = false;
    } else if (w == s->root && a == ZUI_ACT_PICK) {
        rc = murm_ai(m, arg);
    } else if (w == s->held && a == ZUI_ACT_ACCEPT) {
        rc = murm_ai_accept(m);
    } else if (w == s->held && a == ZUI_ACT_REJECT) {
        rc = murm_ai_reject(m);
    } else {
        return false;
    }
    zv_shell_result(s, rc, m->link);
    murm_view_sync(v);
    return true;
}
