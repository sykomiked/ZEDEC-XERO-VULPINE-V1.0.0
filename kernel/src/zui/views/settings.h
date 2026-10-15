/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* settings.h — the Settings app's native screen (zv.h): the person's
 * workspace (zui_workspace.h) and comfort preferences (zui_motion.h) as
 * sections of switches, segmented controls and sliders.
 *
 *   ST1 SECTIONS. Appearance (colour scheme, text size, density),
 *       Accessibility (increase contrast, reduce transparency, bold text,
 *       flash for alerts, mono audio), Keyboard (the layout, from
 *       zui_keymap's built-in ones), Panel and dock (the dock or the strip,
 *       the strip's edge and size, auto-hide) and Motion (reduce motion,
 *       the crossfade that replaces movement). A list of sections on the
 *       left; on a compact screen one at a time with Back (zv.h V3).
 *   ST2 THROUGH THE STORE. Workspace keys go through zui_workspace (so
 *       locks, layers and watchers apply, zui_settings.h S4, S5); a write a
 *       lock refuses puts the control back and says why in the error line.
 *       The view adds two keys of its own to the schema when they are not
 *       there: keyboard.layout (a string, "us") and panel.dock (true).
 *       Comfort flags are zui_motion's (process-wide, as the platform keeps
 *       them); reduce motion is written to both.
 *   ST3 APPLIED AT ONCE. After each change the view runs zui_ws_apply on
 *       the window manager it was given (may be NULL) into its own colour
 *       set and paints with it (zui_ui_set_colors), so the scheme, increase
 *       contrast, the strip's edge and size change while the person looks.
 *       ui->reduce_motion follows zui_motion. A new layout is handed to
 *       on_layout (the host loads the keymap).
 *   ST4 Fixed size, no allocation.
 *
 * zui_workspace.h and zui_widget.h both name an enumerator ZUI_ACT_ASK;
 * this header includes the workspace's under another name, so include it
 * (or zv.h) before any direct #include "zui_workspace.h". */
#ifndef ZXV_VIEW_SETTINGS_H
#define ZXV_VIEW_SETTINGS_H

#include "zv.h"
#define ZUI_ACT_ASK ZUI_WS_ACT_ASK_
#include "zui_workspace.h"
#undef ZUI_ACT_ASK

enum { SET_APPEARANCE = 0, SET_ACCESS, SET_KEYBOARD, SET_PANEL, SET_MOTION, SET_SECTIONS };
#define SET_A11Y    5u
#define SET_LAYOUTS 256u

typedef void (*settings_layout_fn)(void *ctx, const char *layout);

typedef struct {
    zv_shell_t sh;
    zui_workspace_t *ws;
    zui_wm_t *wm;        /* may be NULL */
    zui_colors_t colors; /* ST3: the set in effect, painted with */
    zui_id_t sections, back, pane[SET_SECTIONS];
    zui_id_t scheme, text_size, density;
    zui_id_t a11y[SET_A11Y];
    zui_id_t layouts;
    zui_id_t dock, edge, size, autohide;
    zui_id_t reduce, crossfade;
    int32_t section;
    bool show_section; /* compact: the section instead of the list */
    uint32_t nlayout;
    const char *layout_id[SET_LAYOUTS];
    char layout[ZUI_SET_STR_MAX];
    settings_layout_fn on_layout;
    void *layout_ctx;
} settings_view_t;

void settings_view_build(settings_view_t *v, zui_ui_t *ui, zui_workspace_t *ws, zui_wm_t *wm);
void settings_view_sync(settings_view_t *v);
bool settings_view_action(settings_view_t *v, zui_id_t w, zui_action_t a, int32_t arg);
const char *settings_section_name(int32_t s);

#endif /* ZXV_VIEW_SETTINGS_H */
