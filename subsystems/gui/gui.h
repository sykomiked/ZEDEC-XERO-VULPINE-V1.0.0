/* gui.h — Graphical User Interface: Window Manager, Widgets, Cursor
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#ifndef GUI_H
#define GUI_H

#include <stdint.h>
#include <stdbool.h>
#include "../kernel/src/vbe/vbe.h"
#include "../kernel/src/keyboard/keyboard.h"
#include "../kernel/src/mouse/mouse.h"

#define GUI_MAX_WINDOWS 32
#define GUI_MAX_WIDGETS 256
#define GUI_TITLE_BAR_HEIGHT 20
#define GUI_BORDER_WIDTH 2

typedef enum {
    WIDGET_BUTTON = 0,
    WIDGET_LABEL = 1,
    WIDGET_TEXTBOX = 2,
    WIDGET_PROGRESS = 3,
    WIDGET_CHECKBOX = 4,
    WIDGET_PANEL = 5
} widget_type_t;

typedef struct gui_widget {
    int32_t x, y, w, h;
    widget_type_t type;
    char text[128];
    uint32_t fg_color;
    uint32_t bg_color;
    bool clicked;
    bool hovered;
    bool checked;
    int32_t progress;
    uint32_t id;
    bool visible;
} gui_widget_t;

typedef struct gui_window {
    int32_t x, y, w, h;
    char title[64];
    uint32_t bg_color;
    uint32_t border_color;
    uint32_t title_bar_color;
    gui_widget_t widgets[GUI_MAX_WIDGETS];
    uint32_t num_widgets;
    bool visible;
    bool focused;
    bool dragging;
    int32_t drag_offset_x;
    int32_t drag_offset_y;
    uint32_t id;
} gui_window_t;

typedef struct gui_cursor {
    int32_t x, y;
    bool visible;
    uint32_t color;
} gui_cursor_t;

typedef struct gui_desktop {
    vbe_state_t *vbe;
    gui_window_t windows[GUI_MAX_WINDOWS];
    uint32_t num_windows;
    gui_cursor_t cursor;
    uint32_t active_window;
    uint32_t wallpaper_color;
    uint32_t taskbar_color;
    uint32_t taskbar_height;
    char status_text[128];
} gui_desktop_t;

void gui_init(gui_desktop_t *gui, vbe_state_t *vbe);
uint32_t gui_create_window(gui_desktop_t *gui, const char *title,
                            int32_t x, int32_t y, int32_t w, int32_t h,
                            uint32_t bg_color);
uint32_t gui_add_widget(gui_desktop_t *gui, uint32_t win_idx,
                         widget_type_t type, int32_t x, int32_t y,
                         int32_t w, int32_t h, const char *text);
void gui_render(gui_desktop_t *gui);
void gui_render_window(gui_desktop_t *gui, gui_window_t *win);
void gui_render_widget(gui_desktop_t *gui, gui_window_t *win, gui_widget_t *w);
void gui_render_cursor(gui_desktop_t *gui);
void gui_render_taskbar(gui_desktop_t *gui);
void gui_handle_mouse(gui_desktop_t *gui, mouse_state_t *ms);
void gui_handle_keyboard(gui_desktop_t *gui, keyboard_state_t *kb);
void gui_set_status(gui_desktop_t *gui, const char *text);
void gui_close_window(gui_desktop_t *gui, uint32_t win_idx);
void gui_bring_to_front(gui_desktop_t *gui, uint32_t win_idx);

#endif
