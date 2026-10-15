/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* display.c (views) — the Display settings panel and its model (display.h). */
#include "display.h"
#include "zui_tokens.h"
#include "../../display/display.h" /* the kernel's standard modes and scanout budget */

static uint32_t umin(uint32_t a, uint32_t b)
{
    return a < b ? a : b;
}

/* ---- the model ------------------------------------------------------------ */

void dsp_init(dsp_model_t *m, uint32_t now_ms)
{
    uint8_t *z = (uint8_t *) m;
    for (uint32_t i = 0; i < sizeof *m; i++) z[i] = 0;
    m->now = now_ms;
}

uint32_t dsp_scale_for(uint32_t h)
{
    if (h >= 2160u) return 250u; /* as display.c's scale_for, in percent, capped at 300 */
    if (h >= 1440u) return 175u;
    if (h >= 1080u) return 150u;
    if (h >= 900u) return 125u;
    return 100u;
}

static void defaults(dsp_settings_t *s, const dsp_screen_t *sc)
{
    uint8_t *z = (uint8_t *) s;
    for (uint32_t i = 0; i < sizeof *s; i++) z[i] = 0;
    s->scale = sc->nmode ? dsp_scale_for(sc->mode[0].h) : 100u;
    s->depth = 128;
    s->warmth = 128;
    s->night_from = 21 * 60;
    s->night_to = 7 * 60;
}

int32_t dsp_add_screen(dsp_model_t *m, const char *name, const dsp_mode_t *modes, uint32_t n)
{
    if (m->nscreen >= DSP_SCREENS || !modes || !n) return -1;
    dsp_screen_t *sc = &m->screen[m->nscreen];
    zv_cat(sc->name, sizeof sc->name, name, 0);
    sc->nmode = 0;
    for (uint32_t i = 0; i < n && sc->nmode < DSP_MODES; i++) {
        if (!modes[i].w || !modes[i].h || !modes[i].nrate) continue;
        dsp_mode_t *md = &sc->mode[sc->nmode++];
        *md = modes[i];
        if (md->nrate > DSP_RATES) md->nrate = DSP_RATES;
    }
    if (!sc->nmode) return -1;
    uint32_t i = m->nscreen++;
    defaults(&m->applied.s[i], sc);
    m->proposed.s[i] = m->before.s[i] = m->applied.s[i];
    return (int32_t) i;
}

int32_t dsp_add_kernel_screen(dsp_model_t *m, const char *name)
{
    dsp_mode_t modes[DSP_MODES];
    uint32_t n = 0;
    for (uint32_t i = 0; n < DSP_MODES; i++) {
        uint32_t w, h;
        const char *nm;
        if (!zxv_display_mode(i, &w, &h, &nm)) break;
        /* D3: what the scanout cannot back is not offered (display.h refuses it) */
        if (w > ZXV_DISPLAY_MAX_W || h > ZXV_DISPLAY_MAX_H ||
            (uint64_t) w * h > ZXV_DISPLAY_MAX_PIXELS)
            continue;
        dsp_mode_t *md = &modes[n++];
        uint8_t *z = (uint8_t *) md;
        for (uint32_t k = 0; k < sizeof *md; k++) z[k] = 0;
        md->w = w;
        md->h = h;
        md->rate[0] = 60000;
        md->nrate = 1;
    }
    return dsp_add_screen(m, name, modes, n);
}

int dsp_select(dsp_model_t *m, uint32_t screen)
{
    if (screen >= m->nscreen) return DSP_E_ARG;
    m->sel = screen;
    return DSP_OK;
}

const dsp_settings_t *dsp_proposed(const dsp_model_t *m)
{
    return &m->proposed.s[m->sel];
}

const dsp_settings_t *dsp_applied(const dsp_model_t *m)
{
    return &m->applied.s[m->sel];
}

/* A proposal can be made: there is a screen and no confirmation asking. */
static dsp_settings_t *prop(dsp_model_t *m)
{
    if (m->confirming || !m->nscreen) return 0;
    return &m->proposed.s[m->sel];
}

int dsp_set_mode(dsp_model_t *m, uint32_t mode)
{
    dsp_settings_t *s = prop(m);
    if (!s) return DSP_E_STATE;
    const dsp_screen_t *sc = &m->screen[m->sel];
    if (mode >= sc->nmode) return DSP_E_ARG;
    /* D3: keep the rate when the new mode has it, else its highest */
    uint32_t want = sc->mode[s->mode].rate[umin(s->rate, sc->mode[s->mode].nrate - 1u)];
    const dsp_mode_t *md = &sc->mode[mode];
    s->rate = 0;
    for (uint32_t i = 0; i < md->nrate; i++)
        if (md->rate[i] == want) s->rate = i;
    s->mode = mode;
    return DSP_OK;
}

int dsp_set_rate(dsp_model_t *m, uint32_t rate)
{
    dsp_settings_t *s = prop(m);
    if (!s) return DSP_E_STATE;
    if (rate >= m->screen[m->sel].mode[s->mode].nrate) return DSP_E_ARG;
    s->rate = rate;
    return DSP_OK;
}

int dsp_set_scale(dsp_model_t *m, uint32_t pct)
{
    dsp_settings_t *s = prop(m);
    if (!s) return DSP_E_STATE;
    if (pct < DSP_SCALE_MIN || pct > DSP_SCALE_MAX) return DSP_E_ARG;
    s->scale = (pct + DSP_SCALE_STEP / 2u) / DSP_SCALE_STEP * DSP_SCALE_STEP; /* nearest step */
    return DSP_OK;
}

int dsp_set_orient(dsp_model_t *m, uint32_t o)
{
    dsp_settings_t *s = prop(m);
    if (!s) return DSP_E_STATE;
    if (o > DSP_ROT_270) return DSP_E_ARG;
    s->orient = (uint8_t) o;
    return DSP_OK;
}

int dsp_set_depth(dsp_model_t *m, uint32_t level)
{
    dsp_settings_t *s = prop(m);
    if (!s) return DSP_E_STATE;
    if (level > DSP_LEVEL_MAX) return DSP_E_ARG;
    s->depth = level;
    return DSP_OK;
}

int dsp_set_night(dsp_model_t *m, uint32_t mode)
{
    dsp_settings_t *s = prop(m);
    if (!s) return DSP_E_STATE;
    if (mode > DSP_NIGHT_SCHEDULE) return DSP_E_ARG;
    s->night = (uint8_t) mode;
    return DSP_OK;
}

int dsp_set_warmth(dsp_model_t *m, uint32_t level)
{
    dsp_settings_t *s = prop(m);
    if (!s) return DSP_E_STATE;
    if (level > DSP_LEVEL_MAX) return DSP_E_ARG;
    s->warmth = level;
    return DSP_OK;
}

int dsp_set_schedule(dsp_model_t *m, uint32_t from_min, uint32_t to_min)
{
    dsp_settings_t *s = prop(m);
    if (!s) return DSP_E_STATE;
    if (from_min >= 24u * 60u || to_min >= 24u * 60u) return DSP_E_ARG;
    s->night_from = (uint16_t) from_min;
    s->night_to = (uint16_t) to_min;
    return DSP_OK;
}

int dsp_set_theme(dsp_model_t *m, uint32_t theme)
{
    if (m->confirming) return DSP_E_STATE;
    if (theme >= DSP_THEMES) return DSP_E_ARG;
    m->proposed.theme = (uint8_t) theme;
    return DSP_OK;
}

bool dsp_settings_equal(const dsp_settings_t *a, const dsp_settings_t *b)
{
    return a->mode == b->mode && a->rate == b->rate && a->scale == b->scale &&
           a->orient == b->orient && a->depth == b->depth && a->night == b->night &&
           a->warmth == b->warmth && a->night_from == b->night_from && a->night_to == b->night_to;
}

static bool config_equal(const dsp_model_t *m, const dsp_config_t *a, const dsp_config_t *b)
{
    if (a->theme != b->theme) return false;
    for (uint32_t i = 0; i < m->nscreen; i++)
        if (!dsp_settings_equal(&a->s[i], &b->s[i])) return false;
    return true;
}

bool dsp_pending(const dsp_model_t *m)
{
    return !config_equal(m, &m->proposed, &m->applied);
}

void dsp_discard(dsp_model_t *m)
{
    if (!m->confirming) m->proposed = m->applied;
}

int dsp_apply(dsp_model_t *m, uint32_t now_ms)
{
    m->now = now_ms;
    if (m->confirming || !dsp_pending(m)) return DSP_E_STATE;
    if (m->apply && m->apply(m->apply_ctx, &m->proposed) != DSP_OK) return DSP_E_APPLY;
    m->before = m->applied;
    m->applied = m->proposed;
    m->confirming = true; /* D2 */
    m->deadline = now_ms + DSP_CONFIRM_MS;
    m->applies++;
    return DSP_OK;
}

int dsp_keep(dsp_model_t *m)
{
    if (!m->confirming) return DSP_E_STATE;
    m->confirming = false;
    m->before = m->applied;
    m->keeps++;
    return DSP_OK;
}

int dsp_revert(dsp_model_t *m)
{
    if (!m->confirming) return DSP_E_STATE;
    /* What was there before worked; put it back even if the callback
     * complains (there is nothing better to fall back to). */
    if (m->apply) m->apply(m->apply_ctx, &m->before);
    m->applied = m->proposed = m->before;
    m->confirming = false;
    m->reverts++;
    return DSP_OK;
}

bool dsp_tick(dsp_model_t *m, uint32_t now_ms)
{
    m->now = now_ms;
    if (!m->confirming || (int32_t) (now_ms - m->deadline) < 0) return false;
    dsp_revert(m);
    return true;
}

uint32_t dsp_confirm_left_ms(const dsp_model_t *m)
{
    if (!m->confirming) return 0;
    int32_t left = (int32_t) (m->deadline - m->now);
    return left > 0 ? (uint32_t) left : 0u;
}

bool dsp_night_active(const dsp_settings_t *s, uint32_t minute)
{
    minute %= 24u * 60u;
    if (s->night == DSP_NIGHT_ON) return true;
    if (s->night != DSP_NIGHT_SCHEDULE || s->night_from == s->night_to) return false;
    if (s->night_from < s->night_to) return minute >= s->night_from && minute < s->night_to;
    return minute >= s->night_from || minute < s->night_to; /* past midnight */
}

void dsp_night_gain(uint32_t warmth, uint32_t *r, uint32_t *g, uint32_t *b)
{
    if (warmth > DSP_LEVEL_MAX) warmth = DSP_LEVEL_MAX;
    *r = 256u;
    *g = 256u - warmth * 90u / 256u;
    *b = 256u - warmth * 180u / 256u;
}

const char *dsp_theme_name(uint32_t t)
{
    static const char *const N[DSP_THEMES] = {"Dark", "Light", "High contrast"};
    return t < DSP_THEMES ? N[t] : "?";
}

const char *dsp_orient_name(uint32_t o)
{
    static const char *const N[] = {"Landscape", "Portrait", "Landscape (flipped)",
                                    "Portrait (flipped)"};
    return o <= DSP_ROT_270 ? N[o] : "?";
}

const char *dsp_night_name(uint32_t n)
{
    static const char *const N[] = {"Off", "On", "Scheduled"};
    return n <= DSP_NIGHT_SCHEDULE ? N[n] : "?";
}

uint32_t dsp_mode_text(char *out, uint32_t cap, const dsp_mode_t *md)
{
    uint32_t n = zv_u64(out, cap, md->w);
    n += zv_cat(out + n, cap - n, " \xC3\x97 ", 0);
    return n + zv_u64(out + n, cap - n, md->h);
}

uint32_t dsp_rate_text(char *out, uint32_t cap, uint32_t mhz)
{
    uint32_t c = (mhz + 5u) / 10u; /* hundredths of a hertz */
    uint32_t n = zv_u64(out, cap, c / 100u);
    uint32_t f = c % 100u;
    if (f) {
        char d[4] = {'.', (char) ('0' + f / 10u), (char) ('0' + f % 10u), 0};
        if (d[2] == '0') d[2] = 0;
        n += zv_cat(out + n, cap - n, d, 0);
    }
    return n + zv_cat(out + n, cap - n, " Hz", 0);
}

uint32_t dsp_minutes_text(char *out, uint32_t cap, uint32_t minutes)
{
    minutes %= 24u * 60u;
    char t[6] = {(char) ('0' + minutes / 600u),      (char) ('0' + minutes / 60u % 10u), ':',
                 (char) ('0' + minutes % 60u / 10u), (char) ('0' + minutes % 10u),       0};
    return zv_cat(out, cap, t, 0);
}

/* ---- the view ------------------------------------------------------------- */

static void screen_row(const zui_ui_t *ui, zui_id_t list, int32_t row, zui_row_t *out, void *ctx)
{
    (void) ui, (void) list;
    const dsp_model_t *m = ctx;
    if (row < 0 || (uint32_t) row >= m->nscreen) return;
    const dsp_screen_t *sc = &m->screen[row];
    zv_cat(out->text, sizeof out->text, sc->name, 0);
    dsp_mode_text(out->detail, sizeof out->detail, &sc->mode[m->applied.s[row].mode]);
}

/* A level 0..256 as an engraved track with a light fill and a knob. */
static void track(zui_ui_t *ui, zui_id_t w, zui_surface_t *s, zui_rect_t r, void *ctx)
{
    (void) w;
    const uint32_t *level = ctx;
    int32_t cy = r.y + r.h / 2, x0 = r.x + 6, len = r.w - 12;
    if (len <= 0) return;
    zui_rect_t t = {x0, cy - 2, len, 4};
    zui_fill_rrect(s, t, 2, zui_ui_color(ui, ZUI_C_ENGRAVE_LO), 255);
    zui_engrave(s, t, 2, zui_ui_color(ui, ZUI_C_ENGRAVE_LO), zui_ui_color(ui, ZUI_C_ENGRAVE_HI),
                60u);
    int32_t fill = (int32_t) (((int64_t) len * *level) / DSP_LEVEL_MAX);
    if (fill > 0)
        zui_fill_rrect(s, (zui_rect_t){x0, cy - 2, fill, 4}, 2, zui_ui_color(ui, ZUI_C_TEXT_2),
                       255);
    zui_fill_rrect(s, (zui_rect_t){x0 + fill - 6, cy - 6, 12, 12}, 6, zui_ui_color(ui, ZUI_C_TEXT),
                   255);
}

static zui_id_t row(zui_ui_t *ui, zui_id_t col, const char *label)
{
    zui_id_t r = zui_box(ui, col, ZUI_ROW);
    zui_set_pad(ui, r, 0, ZUI_S2);
    zui_id_t l = zui_label(ui, r, label);
    zui_set_size(ui, l, ZUI_SIZE_FIXED, 120);
    zui_set_font(ui, l, ZUI_TEXT_LABEL, ZUI_MUTED);
    return r;
}

static zui_id_t stepper(zui_ui_t *ui, zui_id_t r, const char *name, zui_id_t *dn, zui_id_t *val,
                        zui_id_t *up, zui_paint_fn paint, void *ctx, zui_id_t *trk)
{
    char nm[ZUI_NAME_MAX];
    *dn = zui_button(ui, r, "\xE2\x88\x92", ZUI_BTN_SECONDARY); /* minus sign */
    zv_cat(nm, sizeof nm, "Less ", name);
    zui_set_name(ui, *dn, nm);
    if (trk) {
        *trk = zui_canvas(ui, r, paint, ctx);
        zui_set_size(ui, *trk, ZUI_SIZE_FIXED, 160);
        zui_set_name(ui, *trk, name);
    }
    *val = zui_label(ui, r, "");
    zui_set_font(ui, *val, ZUI_TEXT_MONO, ZUI_TEXT);
    *up = zui_button(ui, r, "+", ZUI_BTN_SECONDARY);
    zv_cat(nm, sizeof nm, "More ", name);
    zui_set_name(ui, *up, nm);
    return r;
}

void display_view_build(display_view_t *v, zui_ui_t *ui, dsp_model_t *m)
{
    v->m = m;
    v->shown_left_s = 0;
    zv_shell_t *s = &v->sh;
    zv_shell_build(s, ui, "Display");
    v->screens = zui_list(ui, s->body, screen_row, m);
    zui_set_size(ui, v->screens, ZUI_SIZE_FIXED, 200);
    zui_set_name(ui, v->screens, "Screens");
    zui_id_t col = zui_box(ui, s->body, ZUI_COL);
    zui_set_size(ui, col, ZUI_SIZE_FLEX, 1);
    zui_set_pad(ui, col, 0, ZUI_S2);

    zui_id_t r = row(ui, col, "Resolution");
    v->res = zui_button(ui, r, "", ZUI_BTN_SECONDARY);
    r = row(ui, col, "Refresh rate");
    v->rate = zui_button(ui, r, "", ZUI_BTN_SECONDARY);
    stepper(ui, row(ui, col, "Scale"), "scale", &v->scale_dn, &v->scale_val, &v->scale_up, 0, 0, 0);
    r = row(ui, col, "Orientation");
    v->orient = zui_button(ui, r, "", ZUI_BTN_SECONDARY);
    stepper(ui, row(ui, col, "Depth effect"), "depth effect strength", &v->depth_dn, &v->depth_val,
            &v->depth_up, track, &m->proposed.s[0].depth, &v->depth_track);
    zui_separator(ui, col);
    r = row(ui, col, "Night light");
    v->night = zui_button(ui, r, "", ZUI_BTN_SECONDARY);
    v->warmth_row = stepper(ui, row(ui, col, "Warmth"), "warmth", &v->warmth_dn, &v->warmth_val,
                            &v->warmth_up, track, &m->proposed.s[0].warmth, &v->warmth_track);
    v->sched_row = row(ui, col, "Schedule");
    stepper(ui, v->sched_row, "start time", &v->from_dn, &v->from_val, &v->from_up, 0, 0, 0);
    stepper(ui, v->sched_row, "end time", &v->to_dn, &v->to_val, &v->to_up, 0, 0, 0);
    zui_separator(ui, col);
    r = row(ui, col, "Theme");
    v->theme = zui_button(ui, r, "", ZUI_BTN_SECONDARY);

    zui_id_t foot = zui_box(ui, col, ZUI_ROW);
    zui_set_pad(ui, foot, 0, ZUI_S2);
    v->pending = zui_label(ui, foot, "");
    zui_set_size(ui, v->pending, ZUI_SIZE_FLEX, 1);
    zui_set_font(ui, v->pending, ZUI_TEXT_LABEL, ZUI_MUTED);
    v->discard = zui_button(ui, foot, "Discard", ZUI_BTN_SECONDARY);
    v->apply = zui_button(ui, foot, "Apply", ZUI_BTN_PRIMARY);

    v->confirm = zui_panel(ui, col, ZUI_ROW); /* D2 */
    zui_set_pad(ui, v->confirm, ZUI_S3, ZUI_S2);
    v->confirm_text = zui_label(ui, v->confirm, "");
    zui_set_size(ui, v->confirm_text, ZUI_SIZE_FLEX, 1);
    v->revert = zui_button(ui, v->confirm, "Revert", ZUI_BTN_SECONDARY);
    v->keep = zui_button(ui, v->confirm, "Keep", ZUI_BTN_PRIMARY);
    display_view_sync(v);
}

static void level_text(char *out, uint32_t cap, uint32_t level)
{
    uint32_t n = zv_u64(out, cap, level);
    zv_cat(out + n, cap - n, " / 256", 0);
}

void display_view_sync(display_view_t *v)
{
    zui_ui_t *ui = v->sh.ui;
    dsp_model_t *m = v->m;
    bool any = m->nscreen > 0, ask = m->confirming;
    zui_list_set_rows(ui, v->screens, (int32_t) m->nscreen);
    zui_list_select(ui, v->screens, any ? (int32_t) m->sel : -1);
    char b[ZUI_LABEL_MAX];
    const dsp_settings_t *p = dsp_proposed(m);
    const dsp_screen_t *sc = &m->screen[m->sel];
    const dsp_mode_t *md = &sc->mode[umin(p->mode, sc->nmode ? sc->nmode - 1u : 0u)];
    dsp_mode_text(b, sizeof b, md);
    zui_set_text(ui, v->res, any ? b : "No screen");
    dsp_rate_text(b, sizeof b, md->nrate ? md->rate[umin(p->rate, md->nrate - 1u)] : 0);
    zui_set_text(ui, v->rate, any ? b : "");
    uint32_t n = zv_u64(b, sizeof b, p->scale);
    zv_cat(b + n, sizeof b - n, "%", 0);
    zui_set_text(ui, v->scale_val, b);
    zui_set_text(ui, v->orient, dsp_orient_name(p->orient));
    level_text(b, sizeof b, p->depth);
    zui_set_text(ui, v->depth_val, b);
    zui_set_text(ui, v->night, dsp_night_name(p->night));
    level_text(b, sizeof b, p->warmth);
    zui_set_text(ui, v->warmth_val, b);
    dsp_minutes_text(b, sizeof b, p->night_from);
    zui_set_text(ui, v->from_val, b);
    dsp_minutes_text(b, sizeof b, p->night_to);
    zui_set_text(ui, v->to_val, b);
    zui_set_text(ui, v->theme, dsp_theme_name(m->proposed.theme));
    /* the tracks paint the selected screen's levels */
    zui_widget_t *t = zui_w(ui, v->depth_track);
    if (t) t->u.canvas.ctx = &m->proposed.s[m->sel].depth;
    t = zui_w(ui, v->warmth_track);
    if (t) t->u.canvas.ctx = &m->proposed.s[m->sel].warmth;
    zui_invalidate(ui, v->depth_track);
    zui_invalidate(ui, v->warmth_track);
    zui_set_hidden(ui, v->warmth_row, p->night == DSP_NIGHT_OFF);
    zui_set_hidden(ui, v->sched_row, p->night != DSP_NIGHT_SCHEDULE);

    /* D1, D2: nothing is proposed while the confirmation asks */
    const zui_id_t edit[] = {v->screens,  v->res,       v->rate,      v->scale_dn,
                             v->scale_up, v->orient,    v->depth_dn,  v->depth_up,
                             v->night,    v->warmth_dn, v->warmth_up, v->from_dn,
                             v->from_up,  v->to_dn,     v->to_up,     v->theme};
    for (uint32_t i = 0; i < sizeof edit / sizeof edit[0]; i++)
        zui_set_disabled(ui, edit[i], ask || !any);
    if (!ask && any) {
        zui_set_disabled(ui, v->scale_dn, p->scale <= DSP_SCALE_MIN);
        zui_set_disabled(ui, v->scale_up, p->scale >= DSP_SCALE_MAX);
        zui_set_disabled(ui, v->depth_dn, p->depth == 0);
        zui_set_disabled(ui, v->depth_up, p->depth >= DSP_LEVEL_MAX);
        zui_set_disabled(ui, v->warmth_dn, p->warmth == 0);
        zui_set_disabled(ui, v->warmth_up, p->warmth >= DSP_LEVEL_MAX);
        zui_set_disabled(ui, v->rate, md->nrate < 2u);
    }
    bool pend = dsp_pending(m);
    zui_set_text(ui, v->pending, ask ? "" : pend ? "Changes not applied yet" : "");
    zui_set_hidden(ui, v->apply, ask);
    zui_set_hidden(ui, v->discard, ask);
    zui_set_disabled(ui, v->apply, !pend);
    zui_set_disabled(ui, v->discard, !pend);
    zui_set_hidden(ui, v->confirm, !ask);
    uint32_t left = (dsp_confirm_left_ms(m) + 999u) / 1000u;
    v->shown_left_s = ask ? left : 0;
    n = zv_cat(b, sizeof b, "Keep these display settings? Reverting in ", 0);
    n += zv_u64(b + n, sizeof b - n, left);
    zv_cat(b + n, sizeof b - n, " s", 0);
    zui_set_text(ui, v->confirm_text, ask ? b : "");
    zv_shell_status(&v->sh, ask ? "Confirm" : pend ? "Edited" : "");
}

/* Open a choice menu under a button, the current choice highlighted. */
static void choose(display_view_t *v, zui_id_t owner, const char *const *items, uint32_t n,
                   uint32_t cur)
{
    zui_ui_t *ui = v->sh.ui;
    zui_widget_t *w = zui_w(ui, owner);
    if (!w || !n) return;
    zui_menu_open(ui, owner, w->r.x, w->r.y + w->r.h, items, 0, n);
    if (cur < n) ui->menu.hot = (int32_t) cur;
}

static void open_menu(display_view_t *v, zui_id_t w)
{
    dsp_model_t *m = v->m;
    const dsp_settings_t *p = dsp_proposed(m);
    const dsp_screen_t *sc = &m->screen[m->sel];
    static char buf[ZUI_MENU_ITEMS][32];
    const char *items[ZUI_MENU_ITEMS];
    uint32_t n = 0, cur = 0;
    if (w == v->res) {
        for (; n < sc->nmode && n < ZUI_MENU_ITEMS; n++) {
            dsp_mode_text(buf[n], sizeof buf[n], &sc->mode[n]);
            items[n] = buf[n];
        }
        cur = p->mode;
    } else if (w == v->rate) {
        const dsp_mode_t *md = &sc->mode[p->mode];
        for (; n < md->nrate; n++) {
            dsp_rate_text(buf[n], sizeof buf[n], md->rate[n]);
            items[n] = buf[n];
        }
        cur = p->rate;
    } else if (w == v->orient) {
        for (; n <= DSP_ROT_270; n++) items[n] = dsp_orient_name(n);
        cur = p->orient;
    } else if (w == v->night) {
        for (; n <= DSP_NIGHT_SCHEDULE; n++) items[n] = dsp_night_name(n);
        cur = p->night;
    } else {
        for (; n < DSP_THEMES; n++) items[n] = dsp_theme_name(n);
        cur = m->proposed.theme;
    }
    choose(v, w, items, n, cur);
}

static uint32_t step(uint32_t v, int32_t d, uint32_t lo, uint32_t hi, uint32_t by)
{
    int64_t x = (int64_t) v + (int64_t) d * by;
    return x < (int64_t) lo ? lo : x > (int64_t) hi ? hi : (uint32_t) x;
}

static uint32_t minutes(uint32_t v, int32_t d)
{
    return (v + 24u * 60u + (uint32_t) (d * (int32_t) DSP_MINUTES_STEP)) % (24u * 60u);
}

bool display_view_action(display_view_t *v, zui_id_t w, zui_action_t a, int32_t arg)
{
    dsp_model_t *m = v->m;
    /* the action's own time: a confirmation that ran out reverts first */
    if (v->sh.ui->now_ms) dsp_tick(m, v->sh.ui->now_ms);
    const dsp_settings_t *p = dsp_proposed(m);
    int rc = DSP_OK;
    bool menu = w == v->res || w == v->rate || w == v->orient || w == v->night || w == v->theme;
    if (menu && a == ZUI_ACT_PRESS) {
        open_menu(v, w);
        return true;
    }
    if (menu && a == ZUI_ACT_DISMISS) return true;
    if (menu && a == ZUI_ACT_PICK) {
        uint32_t i = (uint32_t) arg;
        rc = w == v->res      ? dsp_set_mode(m, i)
             : w == v->rate   ? dsp_set_rate(m, i)
             : w == v->orient ? dsp_set_orient(m, i)
             : w == v->night  ? dsp_set_night(m, i)
                              : dsp_set_theme(m, i);
    } else if (w == v->screens && a == ZUI_ACT_SELECT) {
        if (arg >= 0) rc = dsp_select(m, (uint32_t) arg);
    } else if (a != ZUI_ACT_PRESS) {
        return false;
    } else if (w == v->scale_dn || w == v->scale_up) {
        rc = dsp_set_scale(m, step(p->scale, w == v->scale_up ? 1 : -1, DSP_SCALE_MIN,
                                   DSP_SCALE_MAX, DSP_SCALE_STEP));
    } else if (w == v->depth_dn || w == v->depth_up) {
        rc = dsp_set_depth(
            m, step(p->depth, w == v->depth_up ? 1 : -1, 0, DSP_LEVEL_MAX, DSP_LEVEL_STEP));
    } else if (w == v->warmth_dn || w == v->warmth_up) {
        rc = dsp_set_warmth(
            m, step(p->warmth, w == v->warmth_up ? 1 : -1, 0, DSP_LEVEL_MAX, DSP_LEVEL_STEP));
    } else if (w == v->from_dn || w == v->from_up) {
        rc = dsp_set_schedule(m, minutes(p->night_from, w == v->from_up ? 1 : -1), p->night_to);
    } else if (w == v->to_dn || w == v->to_up) {
        rc = dsp_set_schedule(m, p->night_from, minutes(p->night_to, w == v->to_up ? 1 : -1));
    } else if (w == v->discard) {
        dsp_discard(m);
    } else if (w == v->apply) {
        rc = dsp_apply(m, m->now);
    } else if (w == v->keep) {
        rc = dsp_keep(m);
    } else if (w == v->revert) {
        rc = dsp_revert(m);
    } else {
        return false;
    }
    const char *err = rc == DSP_OK        ? 0
                      : rc == DSP_E_APPLY ? "The screen did not take these settings"
                      : rc == DSP_E_STATE ? "That does not apply here"
                                          : "That is not a valid value";
    zv_shell_status(&v->sh, "");
    zv_cat(v->sh.error_text, sizeof v->sh.error_text, err, 0);
    zui_set_text(v->sh.ui, v->sh.error, v->sh.error_text);
    zui_set_hidden(v->sh.ui, v->sh.error, rc == DSP_OK);
    display_view_sync(v);
    return true;
}

bool display_view_tick(display_view_t *v, uint32_t now_ms)
{
    bool was = v->m->confirming;
    bool reverted = dsp_tick(v->m, now_ms);
    uint32_t left = (dsp_confirm_left_ms(v->m) + 999u) / 1000u;
    if (reverted || (was && left != v->shown_left_s)) display_view_sync(v);
    return reverted;
}
