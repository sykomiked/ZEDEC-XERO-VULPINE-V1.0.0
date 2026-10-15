/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* settings.c — the Settings app's native screen (settings.h). */
#include "settings.h"
#include "zui_keymap.h"
#include "zui_motion.h"
#include "zui_tokens.h"

static const char *const SECTION[SET_SECTIONS] = {"Appearance", "Accessibility", "Keyboard",
                                                  "Panel and dock", "Motion"};

static const struct {
    uint32_t flag;
    const char *name, *tip;
} A11Y[SET_A11Y] = {
    {ZUI_PREF_INCREASE_CONTRAST, "Increase contrast", "Use the high-contrast colours"},
    {ZUI_PREF_REDUCE_TRANSPARENCY, "Reduce transparency", "Make translucent layers solid"},
    {ZUI_PREF_BOLD_TEXT, "Bold text", "Draw text with the bold face"},
    {ZUI_PREF_FLASH_ALERT, "Flash the screen for alerts", "A flash instead of a sound"},
    {ZUI_PREF_MONO_AUDIO, "Mono audio", "The same sound in both ears"},
};

const char *settings_section_name(int32_t s)
{
    return s >= 0 && s < SET_SECTIONS ? SECTION[s] : "";
}

static void section_row(const zui_ui_t *ui, zui_id_t list, int32_t i, zui_row_t *out, void *ctx)
{
    (void) ui;
    (void) list;
    (void) ctx;
    zv_cat(out->text, sizeof out->text, settings_section_name(i), 0);
}

static void layout_row(const zui_ui_t *ui, zui_id_t list, int32_t i, zui_row_t *out, void *ctx)
{
    (void) ui;
    (void) list;
    const settings_view_t *v = ctx;
    if (i < 0 || (uint32_t) i >= v->nlayout) return;
    zv_cat(out->text, sizeof out->text, v->layout_id[i], 0);
    bool on = 1;
    for (uint32_t k = 0; v->layout_id[i][k] || v->layout[k]; k++)
        if (v->layout_id[i][k] != v->layout[k]) on = 0;
    if (on) zv_cat(out->detail, sizeof out->detail, "in use", 0);
}

static zui_id_t caption(zui_ui_t *ui, zui_id_t parent, const char *text)
{
    zui_id_t l = zui_label(ui, parent, text);
    zui_set_font(ui, l, ZUI_TEXT_LABEL, ZUI_MUTED);
    return l;
}

static zui_id_t pane(zui_ui_t *ui, zui_id_t parent)
{
    zui_id_t p = zui_box(ui, parent, ZUI_COL);
    zui_set_size(ui, p, ZUI_SIZE_FLEX, 1);
    zui_set_pad(ui, p, 0, ZUI_S3);
    return p;
}

/* ST3 */
static void apply(settings_view_t *v)
{
    zui_ui_t *ui = v->sh.ui;
    zui_ws_apply(v->ws, v->wm, &v->colors);
    zui_ui_set_colors(ui, &v->colors);
    ui->reduce_motion = zui_motion_get()->reduce_motion;
}

static void say(settings_view_t *v, int rc)
{
    const char *m = 0;
    if (rc == ZUI_SET_E_LOCKED)
        m = "Your administrator has set this";
    else if (rc == ZUI_WS_E_CONTRAST)
        m = "That colour does not keep the contrast rules here";
    else if (rc == ZUI_SET_E_RANGE || rc == ZUI_SET_E_TYPE)
        m = "That is not a valid value";
    else if (rc < 0)
        m = "That could not be saved";
    zv_cat(v->sh.error_text, sizeof v->sh.error_text, m ? m : "", 0);
    zui_set_text(v->sh.ui, v->sh.error, v->sh.error_text);
    zui_set_hidden(v->sh.ui, v->sh.error, !m);
}

static bool use_dock(settings_view_t *v)
{
    return zui_set_bool(v->ws->st, v->ws->view, "panel.dock");
}

void settings_view_build(settings_view_t *v, zui_ui_t *ui, zui_workspace_t *ws, zui_wm_t *wm)
{
    v->ws = ws;
    v->wm = wm;
    v->section = SET_APPEARANCE;
    v->show_section = false;
    v->on_layout = 0;
    v->layout_ctx = 0;
    if (zui_set_key(ws->st, "keyboard.layout") < 0)
        zui_set_def_str(ws->st, "keyboard.layout", "us");
    if (zui_set_key(ws->st, "panel.dock") < 0) zui_set_def_bool(ws->st, "panel.dock", true);
    v->nlayout = 0;
    for (uint32_t i = 0; v->nlayout < SET_LAYOUTS && zui_keymap_builtin_id(i); i++)
        v->layout_id[v->nlayout++] = zui_keymap_builtin_id(i);

    zv_shell_t *s = &v->sh;
    zv_shell_build(s, ui, "Settings");
    v->sections = zui_list(ui, s->body, section_row, v);
    zui_set_name(ui, v->sections, "Sections");
    zui_set_size(ui, v->sections, ZUI_SIZE_FIXED, 220);
    zui_list_set_rows(ui, v->sections, SET_SECTIONS);
    zui_id_t right = zui_box(ui, s->body, ZUI_COL);
    zui_set_size(ui, right, ZUI_SIZE_FLEX, 1);
    zui_set_pad(ui, right, 0, ZUI_S3);
    v->back = zui_button(ui, right, "Back", ZUI_BTN_GHOST);

    zui_id_t p = v->pane[SET_APPEARANCE] = pane(ui, right);
    static const char *const SCHEMES[] = {"Dark", "Light", "High contrast"};
    caption(ui, p, "Colour scheme");
    v->scheme = zui_tabs(ui, p, SCHEMES, 3, 0, true);
    zui_set_name(ui, v->scheme, "Colour scheme");
    caption(ui, p, "Text size");
    v->text_size = zui_slider(ui, p, 80, 200, 10, 100);
    zui_set_name(ui, v->text_size, "Text size, percent");
    static const char *const DENSITY[] = {"Compact", "Regular", "Roomy"};
    caption(ui, p, "Density");
    v->density = zui_tabs(ui, p, DENSITY, 3, 1, true);
    zui_set_name(ui, v->density, "Density");

    p = v->pane[SET_ACCESS] = pane(ui, right);
    for (uint32_t i = 0; i < SET_A11Y; i++) {
        v->a11y[i] = zui_toggle(ui, p, A11Y[i].name, false);
        zui_set_tooltip(ui, v->a11y[i], A11Y[i].tip);
    }

    p = v->pane[SET_KEYBOARD] = pane(ui, right);
    caption(ui, p, "Keyboard layout");
    v->layouts = zui_list(ui, p, layout_row, v);
    zui_set_name(ui, v->layouts, "Keyboard layout");
    zui_set_size(ui, v->layouts, ZUI_SIZE_FLEX, 1);
    zui_list_set_rows(ui, v->layouts, (int32_t) v->nlayout);

    p = v->pane[SET_PANEL] = pane(ui, right);
    v->dock = zui_toggle(ui, p, "Use the dock", true);
    zui_set_tooltip(ui, v->dock, "The dock in a corner, or the strip along an edge");
    static const char *const EDGE[] = {"Bottom", "Left", "Right", "Top"};
    caption(ui, p, "Strip edge");
    v->edge = zui_tabs(ui, p, EDGE, 4, 0, true);
    zui_set_name(ui, v->edge, "Strip edge");
    caption(ui, p, "Strip size");
    v->size = zui_slider(ui, p, ZUI_WS_PANEL_MIN, ZUI_WS_PANEL_MAX, 4, ZUI_WM_STRIP);
    zui_set_name(ui, v->size, "Strip size, points");
    v->autohide = zui_toggle(ui, p, "Hide the strip automatically", false);

    p = v->pane[SET_MOTION] = pane(ui, right);
    v->reduce = zui_toggle(ui, p, "Reduce motion", false);
    zui_set_tooltip(ui, v->reduce, "Fade instead of moving");
    caption(ui, p, "Crossfade, in milliseconds");
    v->crossfade = zui_slider(ui, p, 0, ZUI_MOTION_MS, 20, ZUI_MOTION_FADE_MS);
    zui_set_name(ui, v->crossfade, "Crossfade, milliseconds");

    if (wm) zui_wm_use_dock(wm, use_dock(v));
    apply(v);
    settings_view_sync(v);
}

void settings_view_sync(settings_view_t *v)
{
    zui_ui_t *ui = v->sh.ui;
    zui_workspace_t *ws = v->ws;
    zui_ws_theme_t t;
    zui_ws_theme(ws, &t);
    zui_ws_panel_t p;
    zui_ws_panel(ws, &p);
    const zui_motion_prefs_t *m = zui_motion_get();
    zui_set_str(ws->st, ws->view, "keyboard.layout", v->layout, sizeof v->layout);

    zui_list_select(ui, v->sections, v->section);
    zui_tabs_select(ui, v->scheme, (int32_t) t.scheme);
    zui_slider_set(ui, v->text_size, t.text_scale);
    zui_tabs_select(ui, v->density, (int32_t) t.density);
    const bool flags[SET_A11Y] = {m->increase_contrast, m->reduce_transparency, m->bold_text,
                                  m->flash_alert, m->mono_audio};
    for (uint32_t i = 0; i < SET_A11Y; i++) zui_set_checked(ui, v->a11y[i], flags[i]);
    int32_t sel = -1;
    for (uint32_t i = 0; i < v->nlayout; i++) {
        bool same = true;
        for (uint32_t k = 0; v->layout_id[i][k] || v->layout[k]; k++)
            if (v->layout_id[i][k] != v->layout[k]) same = false;
        if (same) sel = (int32_t) i;
    }
    zui_list_select(ui, v->layouts, sel);
    zui_invalidate(ui, v->layouts);
    bool dock = use_dock(v);
    zui_set_checked(ui, v->dock, dock);
    zui_tabs_select(ui, v->edge, (int32_t) p.pos);
    zui_slider_set(ui, v->size, p.size);
    zui_set_checked(ui, v->autohide, p.autohide);
    zui_set_disabled(ui, v->edge, dock); /* the strip's, not the dock's */
    zui_set_disabled(ui, v->size, dock);
    zui_set_disabled(ui, v->autohide, dock);
    zui_set_checked(ui, v->reduce, t.reduced_motion || m->reduce_motion);
    zui_slider_set(ui, v->crossfade, m->crossfade_ms);

    /* V3: one pane at a time on a phone */
    bool compact = zv_compact(&v->sh);
    zui_set_hidden(ui, v->sections, compact && v->show_section);
    zui_set_hidden(ui, v->back, !compact || !v->show_section);
    for (int32_t i = 0; i < SET_SECTIONS; i++)
        zui_set_hidden(ui, v->pane[i], i != v->section || (compact && !v->show_section));
    zui_set_size(ui, v->sections, compact ? ZUI_SIZE_FLEX : ZUI_SIZE_FIXED, compact ? 1 : 220);
    zv_shell_status(&v->sh, settings_section_name(v->section));
}

bool settings_view_action(settings_view_t *v, zui_id_t w, zui_action_t a, int32_t arg)
{
    zui_workspace_t *ws = v->ws;
    zui_ws_theme_t t;
    zui_ws_theme(ws, &t);
    zui_ws_panel_t p;
    zui_ws_panel(ws, &p);
    int rc = ZUI_SET_OK;
    if (w == v->sections && (a == ZUI_ACT_SELECT || a == ZUI_ACT_OPEN) && arg >= 0) {
        v->section = arg;
        v->show_section = zv_compact(&v->sh);
    } else if (w == v->back && a == ZUI_ACT_PRESS) {
        v->show_section = false;
    } else if (w == v->scheme && a == ZUI_ACT_SELECT) {
        rc = zui_ws_set_theme(ws, (zui_scheme_t) arg, t.accent);
        if (rc == ZUI_WS_E_CONTRAST) /* the accent is for another ground: the set's own */
            rc = zui_ws_set_theme(ws, (zui_scheme_t) arg, ZUI_ACCENT_GOLD);
    } else if (w == v->text_size && a == ZUI_ACT_CHANGE) {
        rc = zui_ws_set_comfort(ws, arg, t.reduced_motion, t.density);
    } else if (w == v->density && a == ZUI_ACT_SELECT) {
        rc = zui_ws_set_comfort(ws, t.text_scale, t.reduced_motion, (zui_density_t) arg);
    } else if (w == v->layouts && (a == ZUI_ACT_SELECT || a == ZUI_ACT_OPEN) && arg >= 0 &&
               (uint32_t) arg < v->nlayout) {
        rc = zui_set_set_str(ws->st, ws->layer, ws->scope, "keyboard.layout", v->layout_id[arg]);
        if (rc == ZUI_SET_OK && v->on_layout) v->on_layout(v->layout_ctx, v->layout_id[arg]);
    } else if (w == v->dock && a == ZUI_ACT_CHANGE) {
        rc = zui_set_set_int(ws->st, ws->layer, ws->scope, "panel.dock", arg != 0);
        if (rc == ZUI_SET_OK && v->wm) zui_wm_use_dock(v->wm, arg != 0);
    } else if (w == v->edge && a == ZUI_ACT_SELECT) {
        rc = zui_ws_set_panel(ws, (zui_panel_pos_t) arg, p.size, p.autohide);
    } else if (w == v->size && a == ZUI_ACT_CHANGE) {
        rc = zui_ws_set_panel(ws, p.pos, arg, p.autohide);
    } else if (w == v->autohide && a == ZUI_ACT_CHANGE) {
        rc = zui_ws_set_panel(ws, p.pos, p.size, arg != 0);
    } else if (w == v->reduce && a == ZUI_ACT_CHANGE) {
        rc = zui_ws_set_comfort(ws, t.text_scale, arg != 0, t.density);
        zui_motion_set_flag(ZUI_PREF_REDUCE_MOTION, arg != 0);
    } else if (w == v->crossfade && a == ZUI_ACT_CHANGE) {
        zui_motion_set_crossfade((uint32_t) arg);
    } else {
        bool found = false;
        for (uint32_t i = 0; i < SET_A11Y; i++)
            if (w == v->a11y[i] && a == ZUI_ACT_CHANGE) {
                zui_motion_set_flag(A11Y[i].flag, arg != 0);
                found = true;
            }
        if (!found) return false;
    }
    say(v, rc);
    apply(v);
    settings_view_sync(v); /* ST2: a refused write puts the control back */
    return true;
}
