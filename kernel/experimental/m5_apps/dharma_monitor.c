/* dharma_monitor.c — Dharma Monitor: a live instrument panel onto the
 * running OS layer. Every value shown here is read fresh from the
 * actual tantra_engine_t each tick, not a static mockup.
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#include "apps.h"

static void str_copy_dm(char *dst, const char *src, uint32_t max_dst) {
    uint32_t i = 0;
    while (src[i] && i < max_dst - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static void i64_to_dec(int64_t v, char *buf) {
    char tmp[24];
    uint32_t n = 0;
    bool neg = v < 0;
    uint64_t uv = neg ? (uint64_t)(-v) : (uint64_t)v;
    if (uv == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    while (uv > 0) { tmp[n++] = (char)('0' + (uv % 10)); uv /= 10; }
    uint32_t pos = 0;
    if (neg) buf[pos++] = '-';
    for (uint32_t i = 0; i < n; i++) buf[pos++] = tmp[n - 1 - i];
    buf[pos] = '\0';
}

static void append_rational(char *buf, uint32_t *pos, uint32_t max, rational_t r) {
    char num[24], den[24];
    i64_to_dec(r.num, num);
    i64_to_dec(r.den, den);
    for (uint32_t i = 0; num[i] && *pos < max - 1; i++) buf[(*pos)++] = num[i];
    if (*pos < max - 1) buf[(*pos)++] = '/';
    for (uint32_t i = 0; den[i] && *pos < max - 1; i++) buf[(*pos)++] = den[i];
    buf[*pos] = '\0';
}

void dharma_monitor_init(dharma_monitor_app_t *app, uint32_t gui_win, tantra_engine_t *engine) {
    for (uint8_t *b = (uint8_t *)app; b < (uint8_t *)app + sizeof(*app); b++) *b = 0;
    app->base.type = APP_DHARMA_MONITOR;
    app->base.gui_win = gui_win;
    app->base.active = true;
    app->base.state = APP_STATE_RUNNING;
    str_copy_dm(app->base.name, "Dharma Monitor", sizeof(app->base.name));
    app->engine = engine;
    chakra_init(&app->chakra);
}

void dharma_monitor_tick(dharma_monitor_app_t *app) {
    if (!app->engine) return;
    app->last_observation = bodhi_observe(&app->engine->dharma);
    app->last_kundalini_result = kundalini_rise(&app->chakra, app->last_observation.coherence);
}

static const char *chakra_name(chakra_node_id_t id) {
    switch (id) {
        case CHAKRA_MULADHARA:    return "Muladhara ";
        case CHAKRA_SVADHISTHANA: return "Svadhisth.";
        case CHAKRA_MANIPURA:     return "Manipura  ";
        case CHAKRA_ANAHATA:      return "Anahata   ";
        case CHAKRA_VISHUDDHA:    return "Vishuddha ";
        case CHAKRA_AJNA:         return "Ajna      ";
        case CHAKRA_SAHASRARA:    return "Sahasrara ";
        default:                  return "?         ";
    }
}

void dharma_monitor_render(dharma_monitor_app_t *app, gui_desktop_t *gui) {
    if (app->base.gui_win >= gui->num_windows) return;
    gui_window_t *win = &gui->windows[app->base.gui_win];
    win->num_widgets = 0;
    char line[96];
    uint32_t pos;

    if (win->num_widgets < GUI_MAX_WIDGETS) {
        gui_widget_t *w = &win->widgets[win->num_widgets++];
        w->type = WIDGET_LABEL; w->x = 5; w->y = 5; w->w = win->w - 10; w->h = 16;
        w->fg_color = 0x00FFFF; str_copy_dm(w->text, "Dharma Monitor -- live OS-layer state", sizeof(w->text));
        w->visible = true;
    }
    if (win->num_widgets < GUI_MAX_WIDGETS) {
        gui_widget_t *w = &win->widgets[win->num_widgets++];
        w->type = WIDGET_LABEL; w->x = 5; w->y = 28; w->w = win->w - 10; w->h = 14;
        pos = 0;
        str_copy_dm(line, "|D| = ", sizeof(line)); pos = 6;
        char nbuf[24]; i64_to_dec((int64_t)(app->engine ? dharma_set_size(&app->engine->dharma) : 0), nbuf);
        for (uint32_t i = 0; nbuf[i] && pos < sizeof(line) - 1; i++) line[pos++] = nbuf[i];
        line[pos] = '\0';
        w->fg_color = 0xFFFFFF; str_copy_dm(w->text, line, sizeof(w->text)); w->visible = true;
    }
    if (win->num_widgets < GUI_MAX_WIDGETS) {
        gui_widget_t *w = &win->widgets[win->num_widgets++];
        w->type = WIDGET_LABEL; w->x = 5; w->y = 46; w->w = win->w - 10; w->h = 14;
        pos = 0;
        str_copy_dm(line, "Bodhi: coherence=", sizeof(line)); pos = 17;
        append_rational(line, &pos, sizeof(line), app->last_observation.coherence);
        str_copy_dm(line + pos, " dispersion=", sizeof(line) - pos); pos += 12;
        append_rational(line, &pos, sizeof(line), app->last_observation.dispersion);
        w->fg_color = app->last_observation.transcendent ? 0x00FF00 : 0xFFAA00;
        str_copy_dm(w->text, line, sizeof(w->text)); w->visible = true;
    }
    if (win->num_widgets < GUI_MAX_WIDGETS) {
        gui_widget_t *w = &win->widgets[win->num_widgets++];
        w->type = WIDGET_LABEL; w->x = 5; w->y = 64; w->w = win->w - 10; w->h = 14;
        str_copy_dm(w->text, app->last_observation.transcendent ?
            "State: TRANSCENDENT (coherence dominates)" : "State: differentiated (dispersion present)", sizeof(w->text));
        w->fg_color = 0xAAAAAA; w->visible = true;
    }

    /* 7 chakra nodes: name + charge, one per row. */
    for (uint32_t i = 0; i < CHAKRA_NUM_NODES && win->num_widgets < GUI_MAX_WIDGETS; i++) {
        gui_widget_t *w = &win->widgets[win->num_widgets++];
        w->type = WIDGET_LABEL; w->x = 5; w->y = (int32_t)(88 + i * 16); w->w = win->w - 10; w->h = 14;
        pos = 0;
        const char *nm = chakra_name((chakra_node_id_t)i);
        for (uint32_t j = 0; nm[j] && pos < sizeof(line) - 1; j++) line[pos++] = nm[j];
        line[pos++] = ':'; line[pos++] = ' ';
        append_rational(line, &pos, sizeof(line), app->chakra.nodes[i].charge);
        w->fg_color = (i == CHAKRA_SAHASRARA) ? 0xFFFF00 : 0xCCCCCC;
        str_copy_dm(w->text, line, sizeof(w->text)); w->visible = true;
    }
}
