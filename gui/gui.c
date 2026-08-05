/* gui.c — GUI Window Manager Implementation
 * Renders windows, widgets, taskbar, mouse cursor on VBE framebuffer.
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#include "gui.h"

static int str_len(const char *s) { int n = 0; while (s[n]) n++; return n; }
static void str_copy(char *d, const char *s) { int i = 0; while (s[i]) { d[i] = s[i]; i++; } d[i] = 0; }

void gui_init(gui_desktop_t *gui, vbe_state_t *vbe) {
    gui->vbe = vbe;
    gui->num_windows = 0;
    gui->active_window = 0;
    gui->cursor.x = vbe->width / 2;
    gui->cursor.y = vbe->height / 2;
    gui->cursor.visible = true;
    gui->cursor.color = COLOR_WHITE;
    gui->wallpaper_color = RGB(15, 15, 35);
    gui->taskbar_color = RGB(25, 25, 45);
    gui->taskbar_height = 28;
    gui->status_text[0] = 0;
}

uint32_t gui_create_window(gui_desktop_t *gui, const char *title,
                            int32_t x, int32_t y, int32_t w, int32_t h,
                            uint32_t bg_color) {
    if (gui->num_windows >= GUI_MAX_WINDOWS) return UINT32_MAX;
    uint32_t idx = gui->num_windows++;
    gui_window_t *win = &gui->windows[idx];
    win->x = x; win->y = y; win->w = w; win->h = h;
    str_copy(win->title, title);
    win->bg_color = bg_color;
    win->border_color = RGB(100, 100, 140);
    win->title_bar_color = RGB(60, 60, 100);
    win->num_widgets = 0;
    win->visible = true;
    win->focused = true;
    win->dragging = false;
    win->drag_offset_x = 0;
    win->drag_offset_y = 0;
    win->id = idx;
    gui->active_window = idx;
    return idx;
}

uint32_t gui_add_widget(gui_desktop_t *gui, uint32_t win_idx,
                         widget_type_t type, int32_t x, int32_t y,
                         int32_t w, int32_t h, const char *text) {
    if (win_idx >= gui->num_windows) return UINT32_MAX;
    gui_window_t *win = &gui->windows[win_idx];
    if (win->num_widgets >= GUI_MAX_WIDGETS) return UINT32_MAX;
    uint32_t idx = win->num_widgets++;
    gui_widget_t *widget = &win->widgets[idx];
    widget->x = x; widget->y = y; widget->w = w; widget->h = h;
    widget->type = type;
    str_copy(widget->text, text);
    widget->fg_color = COLOR_WHITE;
    widget->bg_color = RGB(50, 50, 80);
    widget->clicked = false;
    widget->hovered = false;
    widget->checked = false;
    widget->progress = 0;
    widget->id = idx;
    widget->visible = true;

    switch (type) {
        case WIDGET_BUTTON:
            widget->bg_color = RGB(70, 70, 120);
            break;
        case WIDGET_LABEL:
            widget->bg_color = 0xFFFFFFFF;
            break;
        case WIDGET_TEXTBOX:
            widget->bg_color = RGB(30, 30, 50);
            break;
        case WIDGET_PROGRESS:
            widget->bg_color = RGB(40, 40, 60);
            break;
        case WIDGET_CHECKBOX:
            widget->bg_color = RGB(50, 50, 80);
            break;
        case WIDGET_PANEL:
            widget->bg_color = RGB(35, 35, 55);
            break;
    }
    return idx;
}

void gui_render_widget(gui_desktop_t *gui, gui_window_t *win, gui_widget_t *w) {
    if (!w->visible) return;
    vbe_state_t *vbe = gui->vbe;
    int32_t abs_x = win->x + w->x;
    int32_t abs_y = win->y + GUI_TITLE_BAR_HEIGHT + w->y;

    switch (w->type) {
        case WIDGET_BUTTON:
            vbe_fill_rect(vbe, abs_x, abs_y, w->w, w->h, w->bg_color);
            vbe_fill_rect(vbe, abs_x, abs_y, w->w, 1, RGB(100, 100, 160));
            vbe_fill_rect(vbe, abs_x, abs_y, 1, w->h, RGB(100, 100, 160));
            vbe_fill_rect(vbe, abs_x + w->w - 1, abs_y, 1, w->h, RGB(40, 40, 60));
            vbe_fill_rect(vbe, abs_x, abs_y + w->h - 1, w->w, 1, RGB(40, 40, 60));
            {
                int32_t tx = abs_x + (w->w - str_len(w->text) * 8) / 2;
                int32_t ty = abs_y + (w->h - 16) / 2;
                vbe_draw_string(vbe, tx, ty, w->text, w->fg_color, w->bg_color);
            }
            break;
        case WIDGET_LABEL:
            vbe_draw_string(vbe, abs_x, abs_y, w->text, w->fg_color, win->bg_color);
            break;
        case WIDGET_TEXTBOX:
            vbe_fill_rect(vbe, abs_x, abs_y, w->w, w->h, w->bg_color);
            vbe_fill_rect(vbe, abs_x, abs_y, w->w, 1, RGB(80, 80, 120));
            vbe_fill_rect(vbe, abs_x, abs_y, 1, w->h, RGB(80, 80, 120));
            vbe_draw_string(vbe, abs_x + 4, abs_y + 4, w->text, COLOR_WHITE, w->bg_color);
            break;
        case WIDGET_PROGRESS:
            vbe_fill_rect(vbe, abs_x, abs_y, w->w, w->h, w->bg_color);
            if (w->progress > 0) {
                int32_t fill_w = (w->w * w->progress) / 100;
                if (fill_w > w->w) fill_w = w->w;
                vbe_fill_rect(vbe, abs_x, abs_y, fill_w, w->h, RGB(0, 180, 0));
            }
            break;
        case WIDGET_CHECKBOX:
            vbe_fill_rect(vbe, abs_x, abs_y, 16, 16, w->bg_color);
            vbe_fill_rect(vbe, abs_x, abs_y, 16, 1, RGB(100, 100, 140));
            vbe_fill_rect(vbe, abs_x, abs_y, 1, 16, RGB(100, 100, 140));
            if (w->checked) {
                vbe_draw_line(vbe, abs_x + 3, abs_y + 8, abs_x + 6, abs_y + 12, COLOR_GREEN);
                vbe_draw_line(vbe, abs_x + 6, abs_y + 12, abs_x + 13, abs_y + 3, COLOR_GREEN);
            }
            if (w->text[0])
                vbe_draw_string(vbe, abs_x + 20, abs_y, w->text, w->fg_color, win->bg_color);
            break;
        case WIDGET_PANEL:
            vbe_fill_rect(vbe, abs_x, abs_y, w->w, w->h, w->bg_color);
            vbe_fill_rect(vbe, abs_x, abs_y, w->w, 1, RGB(60, 60, 90));
            break;
    }
}

void gui_render_window(gui_desktop_t *gui, gui_window_t *win) {
    if (!win->visible) return;
    vbe_state_t *vbe = gui->vbe;

    vbe_fill_rect(vbe, win->x, win->y, win->w, win->h, win->border_color);
    vbe_fill_rect(vbe, win->x + GUI_BORDER_WIDTH, win->y + GUI_BORDER_WIDTH,
                  win->w - 2 * GUI_BORDER_WIDTH, win->h - 2 * GUI_BORDER_WIDTH,
                  win->bg_color);

    vbe_fill_rect(vbe, win->x + GUI_BORDER_WIDTH, win->y + GUI_BORDER_WIDTH,
                  win->w - 2 * GUI_BORDER_WIDTH, GUI_TITLE_BAR_HEIGHT,
                  win->focused ? win->title_bar_color : RGB(40, 40, 60));

    vbe_draw_string(vbe, win->x + 8, win->y + 3, win->title,
                    COLOR_WHITE, win->focused ? win->title_bar_color : RGB(40, 40, 60));

    int32_t close_x = win->x + win->w - 18;
    int32_t close_y = win->y + 4;
    vbe_fill_rect(vbe, close_x, close_y, 12, 12, RGB(200, 50, 50));
    vbe_draw_string(vbe, close_x + 2, close_y - 1, "x", COLOR_WHITE, RGB(200, 50, 50));

    for (uint32_t i = 0; i < win->num_widgets; i++)
        gui_render_widget(gui, win, &win->widgets[i]);
}

void gui_render_cursor(gui_desktop_t *gui) {
    if (!gui->cursor.visible) return;
    vbe_state_t *vbe = gui->vbe;
    int32_t cx = gui->cursor.x;
    int32_t cy = gui->cursor.y;

    vbe_set_pixel(vbe, cx, cy, COLOR_WHITE);
    for (int32_t i = 1; i < 12; i++) {
        vbe_set_pixel(vbe, cx + i, cy + i, COLOR_WHITE);
        vbe_set_pixel(vbe, cx, cy + i, COLOR_WHITE);
        vbe_set_pixel(vbe, cx + i, cy, COLOR_WHITE);
    }
    vbe_set_pixel(vbe, cx + 1, cy + 2, COLOR_BLACK);
    vbe_set_pixel(vbe, cx + 2, cy + 4, COLOR_BLACK);
    vbe_set_pixel(vbe, cx + 3, cy + 6, COLOR_BLACK);
}

void gui_render_taskbar(gui_desktop_t *gui) {
    vbe_state_t *vbe = gui->vbe;
    int32_t tb_y = vbe->height - gui->taskbar_height;

    vbe_fill_rect(vbe, 0, tb_y, vbe->width, gui->taskbar_height, gui->taskbar_color);
    vbe_fill_rect(vbe, 0, tb_y, vbe->width, 1, RGB(60, 60, 100));

    int32_t bx = 4;
    for (uint32_t i = 0; i < gui->num_windows; i++) {
        if (!gui->windows[i].visible) continue;
        uint32_t bg = (i == gui->active_window) ? RGB(70, 70, 120) : RGB(40, 40, 60);
        vbe_fill_rect(vbe, bx, tb_y + 4, 120, gui->taskbar_height - 8, bg);
        vbe_fill_rect(vbe, bx, tb_y + 4, 120, 1, RGB(80, 80, 120));
        vbe_draw_string(vbe, bx + 4, tb_y + 6, gui->windows[i].title,
                        COLOR_WHITE, bg);
        bx += 124;
    }

    if (gui->status_text[0]) {
        int32_t sx = vbe->width - str_len(gui->status_text) * 8 - 8;
        vbe_draw_string(vbe, sx, tb_y + 6, gui->status_text,
                        RGB(150, 150, 180), gui->taskbar_color);
    }
}

void gui_render(gui_desktop_t *gui) {
    vbe_state_t *vbe = gui->vbe;
    vbe_clear_screen(vbe, gui->wallpaper_color);

    for (int32_t i = (int32_t)gui->num_windows - 1; i >= 0; i--) {
        if (i == (int32_t)gui->active_window) continue;
        gui_render_window(gui, &gui->windows[i]);
    }
    if (gui->active_window < gui->num_windows)
        gui_render_window(gui, &gui->windows[gui->active_window]);

    gui_render_taskbar(gui);
    gui_render_cursor(gui);
}

void gui_handle_mouse(gui_desktop_t *gui, mouse_state_t *ms) {
    gui->cursor.x = ms->x;
    gui->cursor.y = ms->y;
    if (gui->cursor.x < 0) gui->cursor.x = 0;
    if (gui->cursor.y < 0) gui->cursor.y = 0;
    if (gui->cursor.x >= gui->vbe->width) gui->cursor.x = gui->vbe->width - 1;
    if (gui->cursor.y >= gui->vbe->height) gui->cursor.y = gui->vbe->height - 1;

    if (ms->buttons & 0x01) {
        for (int32_t i = (int32_t)gui->num_windows - 1; i >= 0; i--) {
            gui_window_t *win = &gui->windows[i];
            if (!win->visible) continue;

            if (gui->cursor.x >= win->x && gui->cursor.x < win->x + win->w &&
                gui->cursor.y >= win->y && gui->cursor.y < win->y + GUI_TITLE_BAR_HEIGHT) {
                int32_t close_x = win->x + win->w - 18;
                int32_t close_y = win->y + 4;
                if (gui->cursor.x >= close_x && gui->cursor.x < close_x + 12 &&
                    gui->cursor.y >= close_y && gui->cursor.y < close_y + 12) {
                    win->visible = false;
                    return;
                }
                gui_bring_to_front(gui, i);
                if (!win->dragging) {
                    win->dragging = true;
                    win->drag_offset_x = gui->cursor.x - win->x;
                    win->drag_offset_y = gui->cursor.y - win->y;
                }
                win->x = gui->cursor.x - win->drag_offset_x;
                win->y = gui->cursor.y - win->drag_offset_y;
                return;
            }

            int32_t content_x = win->x + GUI_BORDER_WIDTH;
            int32_t content_y = win->y + GUI_TITLE_BAR_HEIGHT;
            if (gui->cursor.x >= content_x && gui->cursor.x < content_x + win->w &&
                gui->cursor.y >= content_y && gui->cursor.y < content_y + win->h) {
                for (uint32_t j = 0; j < win->num_widgets; j++) {
                    gui_widget_t *w = &win->widgets[j];
                    int32_t ax = win->x + w->x;
                    int32_t ay = win->y + GUI_TITLE_BAR_HEIGHT + w->y;
                    if (gui->cursor.x >= ax && gui->cursor.x < ax + w->w &&
                        gui->cursor.y >= ay && gui->cursor.y < ay + w->h) {
                        w->hovered = true;
                        if (w->type == WIDGET_BUTTON) w->clicked = true;
                        if (w->type == WIDGET_CHECKBOX) w->checked = !w->checked;
                    } else {
                        w->hovered = false;
                    }
                }
            }
        }
    } else {
        for (uint32_t i = 0; i < gui->num_windows; i++)
            gui->windows[i].dragging = false;
    }
}

void gui_handle_keyboard(gui_desktop_t *gui, keyboard_state_t *kb) {
    (void)gui;
    (void)kb;
}

void gui_set_status(gui_desktop_t *gui, const char *text) {
    str_copy(gui->status_text, text);
}

void gui_close_window(gui_desktop_t *gui, uint32_t win_idx) {
    if (win_idx >= gui->num_windows) return;
    gui->windows[win_idx].visible = false;
}

void gui_bring_to_front(gui_desktop_t *gui, uint32_t win_idx) {
    if (win_idx >= gui->num_windows) return;
    for (uint32_t i = 0; i < gui->num_windows; i++)
        gui->windows[i].focused = false;
    gui->windows[win_idx].focused = true;
    gui->active_window = win_idx;
}
