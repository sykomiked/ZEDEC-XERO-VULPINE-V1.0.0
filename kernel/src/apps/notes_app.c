/* notes_app.c — ZXV OS Notes: ordinal-timestamped short entries.
 * Distinct from Editor (one free-form buffer): Notes is a LIST of
 * short entries, each tagged with the OSEQ cycle it was created on
 * (no wall clock, per the codebase's own design philosophy), backed
 * by a single VFS file.
 *
 * Serialization format: one entry per line, "<cycle decimal>|<text>\n".
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#include "apps.h"

static void str_copy_n(char *dst, const char *src, uint32_t max_dst) {
    uint32_t i = 0;
    while (src[i] && i < max_dst - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static uint32_t u64_to_dec(uint64_t v, char *buf) {
    char tmp[21];
    uint32_t n = 0;
    if (v == 0) { buf[0] = '0'; buf[1] = '\0'; return 1; }
    while (v > 0) { tmp[n++] = (char)('0' + (v % 10)); v /= 10; }
    for (uint32_t i = 0; i < n; i++) buf[i] = tmp[n - 1 - i];
    buf[n] = '\0';
    return n;
}

static uint64_t dec_to_u64(const char *s, uint32_t *consumed) {
    uint64_t v = 0;
    uint32_t i = 0;
    while (s[i] >= '0' && s[i] <= '9') { v = v * 10 + (uint64_t)(s[i] - '0'); i++; }
    if (consumed) *consumed = i;
    return v;
}

void notes_init(notes_app_t *app, uint32_t gui_win, vfs_state_t *vfs, oseq_state_t *oseq) {
    for (uint8_t *b = (uint8_t *)app; b < (uint8_t *)app + sizeof(*app); b++) *b = 0;
    app->base.type = APP_CUSTOM;
    app->base.gui_win = gui_win;
    app->base.active = true;
    app->base.state = APP_STATE_RUNNING;
    str_copy_n(app->base.name, "Notes", sizeof(app->base.name));
    app->vfs = vfs;
    app->oseq = oseq;
    str_copy_n(app->filename, "notes.txt", sizeof(app->filename));
    notes_load(app); /* a missing file on first run is not an error */
}

void notes_handle_key(notes_app_t *app, char ch) {
    if (ch == '\n' || ch == '\r') {
        if (app->input_len > 0 && app->num_entries < NOTES_MAX_ENTRIES) {
            notes_entry_t *e = &app->entries[app->num_entries];
            e->created_cycle = app->oseq ? app->oseq->current_cycle : 0;
            str_copy_n(e->text, app->input_buf, NOTES_ENTRY_LEN);
            app->num_entries++;
            app->input_len = 0;
            app->input_buf[0] = '\0';
            notes_save(app);
        }
        return;
    }
    if (ch == '\b' || ch == 127) {
        if (app->input_len > 0) { app->input_len--; app->input_buf[app->input_len] = '\0'; }
        return;
    }
    if (app->input_len < NOTES_ENTRY_LEN - 1) {
        app->input_buf[app->input_len++] = ch;
        app->input_buf[app->input_len] = '\0';
    }
}

void notes_handle_special(notes_app_t *app, uint8_t scancode) {
    if (scancode == 0x48 && app->selected > 0) app->selected--;
    if (scancode == 0x50 && app->num_entries > 0 && app->selected < app->num_entries - 1) app->selected++;
    if (scancode == 0x53 && app->num_entries > 0 && app->selected < app->num_entries) {
        for (uint32_t i = app->selected; i + 1 < app->num_entries; i++) app->entries[i] = app->entries[i + 1];
        app->num_entries--;
        if (app->num_entries == 0) app->selected = 0;
        else if (app->selected >= app->num_entries) app->selected = app->num_entries - 1;
        notes_save(app);
    }
}

int32_t notes_save(notes_app_t *app) {
    if (!app->vfs) return -1;
    char buf[4096];
    uint32_t pos = 0;
    for (uint32_t i = 0; i < app->num_entries && pos < sizeof(buf) - 128; i++) {
        char numbuf[21];
        uint32_t n = u64_to_dec(app->entries[i].created_cycle, numbuf);
        for (uint32_t j = 0; j < n; j++) buf[pos++] = numbuf[j];
        buf[pos++] = '|';
        for (uint32_t j = 0; app->entries[i].text[j] && pos < sizeof(buf) - 2; j++) buf[pos++] = app->entries[i].text[j];
        buf[pos++] = '\n';
    }
    int32_t fd = vfs_open(app->vfs, app->filename);
    if (fd < 0) return -1;
    int32_t written = vfs_write(app->vfs, fd, buf, (int32_t)pos);
    vfs_close(app->vfs, fd);
    return (written == (int32_t)pos) ? 0 : -1;
}

int32_t notes_load(notes_app_t *app) {
    if (!app->vfs) return -1;
    int32_t fd = vfs_open(app->vfs, app->filename);
    if (fd < 0) return -1;
    char buf[4096];
    int32_t n = vfs_read(app->vfs, fd, buf, sizeof(buf) - 1);
    vfs_close(app->vfs, fd);
    if (n < 0) return -1;
    buf[n] = '\0';

    app->num_entries = 0;
    const char *line = buf;
    while (*line && app->num_entries < NOTES_MAX_ENTRIES) {
        uint32_t consumed = 0;
        uint64_t cycle = dec_to_u64(line, &consumed);
        if (consumed == 0 || line[consumed] != '|') {
            /* Malformed line: skip to next newline (or end) and continue,
             * rather than corrupting entries with garbage or crashing. */
            while (*line && *line != '\n') line++;
            if (*line == '\n') line++;
            continue;
        }
        const char *text_start = line + consumed + 1;
        const char *text_end = text_start;
        while (*text_end && *text_end != '\n') text_end++;

        notes_entry_t *e = &app->entries[app->num_entries];
        e->created_cycle = cycle;
        uint32_t tlen = (uint32_t)(text_end - text_start);
        if (tlen > NOTES_ENTRY_LEN - 1) tlen = NOTES_ENTRY_LEN - 1;
        for (uint32_t i = 0; i < tlen; i++) e->text[i] = text_start[i];
        e->text[tlen] = '\0';
        app->num_entries++;

        line = (*text_end == '\n') ? text_end + 1 : text_end;
    }
    return 0;
}

void notes_render(notes_app_t *app, gui_desktop_t *gui) {
    if (app->base.gui_win >= gui->num_windows) return;
    gui_window_t *win = &gui->windows[app->base.gui_win];
    win->num_widgets = 0;

    if (win->num_widgets < GUI_MAX_WIDGETS) {
        gui_widget_t *w = &win->widgets[win->num_widgets++];
        w->type = WIDGET_LABEL; w->x = 5; w->y = 5; w->w = win->w - 10; w->h = 16;
        w->fg_color = 0x00FFFF; str_copy_n(w->text, "Notes", sizeof(w->text)); w->visible = true;
    }
    if (win->num_widgets < GUI_MAX_WIDGETS) {
        gui_widget_t *w = &win->widgets[win->num_widgets++];
        w->type = WIDGET_TEXTBOX; w->x = 5; w->y = 25; w->w = win->w - 10; w->h = 20;
        str_copy_n(w->text, app->input_buf, sizeof(w->text)); w->visible = true;
    }
    uint32_t shown = app->num_entries < 15 ? app->num_entries : 15;
    for (uint32_t i = 0; i < shown; i++) {
        if (win->num_widgets >= GUI_MAX_WIDGETS) break;
        gui_widget_t *w = &win->widgets[win->num_widgets++];
        w->type = WIDGET_LABEL; w->x = 10; w->y = 55 + (int32_t)i * 16; w->w = win->w - 20; w->h = 14;
        if (i == app->selected) { w->fg_color = 0x000000; w->bg_color = 0x00AA00; }
        else { w->fg_color = 0xFFFFFF; w->bg_color = win->bg_color; }
        str_copy_n(w->text, app->entries[i].text, sizeof(w->text));
        w->visible = true;
    }
}
