#include "apps.h"

// Helper function to convert uint64_t to decimal string
static void uint64_to_decimal(uint64_t value, char *buffer, size_t buffer_size) {
    size_t index = 0;
    if (value == 0) {
        buffer[index++] = '0';
    } else {
        while (value > 0) {
            buffer[index++] = (value % 10) + '0';
            value /= 10;
        }
    }
    // Reverse the string to get correct order
    for (size_t i = 0; i < index / 2; ++i) {
        char temp = buffer[i];
        buffer[i] = buffer[index - 1 - i];
        buffer[index - 1 - i] = temp;
    }
    buffer[index] = '\0';
}

// Helper function to copy string manually
static void string_copy(char *dest, const char *src, size_t dest_size) {
    size_t i = 0;
    while (i < dest_size && *src != '\0') {
        *dest++ = *src++;
        ++i;
    }
    *dest = '\0';
}

void clock_init(clock_app_t *app, uint32_t gui_win, oseq_state_t *oseq) {
    // Zero-initialize the app structure
    for (uint32_t i = 0; (void*)(i) < (void*)(((uint8_t*)app) + sizeof(clock_app_t)); i++) {
        ((uint8_t*)app)[i] = 0;
    }
    app->base.type = APP_CLOCK;
    app->base.gui_win = gui_win;
    app->base.active = true;
    app->base.state = APP_STATE_RUNNING;
    string_copy(app->base.name, "Clock", sizeof(app->base.name));
    app->oseq = oseq;
    app->last_displayed_cycle = 0;
}

void clock_tick(clock_app_t *app) {
    if (app->oseq != NULL) {
        app->last_displayed_cycle = app->oseq->current_cycle;
    }
}

void clock_render(clock_app_t *app, gui_desktop_t *gui) {
    if (app->base.gui_win >= gui->num_windows) {
        return;
    }
    gui_window_t *win = &gui->windows[app->base.gui_win];
    win->num_widgets = 0;

    // Title label
    gui_widget_t *title = &win->widgets[win->num_widgets++];
    title->type = WIDGET_LABEL;
    title->x = 5;
    title->y = 5;
    title->w = win->w - 10;
    title->h = 20;
    title->fg_color = 0x00FFFF;
    string_copy(title->text, "ZXV Cycle Clock", sizeof(title->text));

    // Cycle label
    gui_widget_t *cycle = &win->widgets[win->num_widgets++];
    cycle->type = WIDGET_LABEL;
    cycle->x = 5;
    cycle->y = 35;
    cycle->w = win->w - 10;
    cycle->h = 30;
    cycle->fg_color = 0xFFFF00;

    char cycle_str[21]; // Enough for 0 to 18446744073709551615
    uint64_to_decimal(app->last_displayed_cycle, cycle_str, sizeof(cycle_str));
    string_copy(cycle->text, "Cycle: ", sizeof(cycle->text));
    string_copy(cycle->text + 7, cycle_str, sizeof(cycle->text) - 7);

    // Info label
    gui_widget_t *info = &win->widgets[win->num_widgets++];
    info->type = WIDGET_LABEL;
    info->x = 5;
    info->y = 70;
    info->w = win->w - 10;
    info->h = 16;
    info->fg_color = 0x888888;
    string_copy(info->text, "No wall clock -- OSEQ event-cycle pulses only", sizeof(info->text));
}