/* hypercube_scene.c — ZXV Hypercube Scene Architecture
 *
 * Implements the N-dimensional scene graph, projection math,
 * event-driven transitions, and graceful degradation across
 * display modes.
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */

#include "hypercube_scene.h"

/* ===== Helpers ===== */

static void copy_str(char *dst, const char *src, uint32_t max) {
    uint32_t i;
    for (i = 0; i + 1 < max && src && src[i]; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

static bool str_eq(const char *a, const char *b) {
    uint32_t i;
    for (i = 0; a[i] != '\0' && b[i] != '\0'; i++) {
        if (a[i] != b[i]) return false;
    }
    return a[i] == '\0' && b[i] == '\0';
}

/* ===== Initialization ===== */

void hc_init(hc_scene_t *scene, hc_display_mode_t display_mode) {
    if (!scene) return;
    ev_memset(scene, 0, sizeof(*scene));
    scene->display_mode = display_mode;
    scene->focused_cell_idx = -1;
    scene->next_cell_id = 1;
    scene->next_transition_id = 1;

    /* Default render layers */
    scene->layers[0].z_base = 0;   copy_str(scene->layers[0].name, "background", HC_MAX_NAME_LEN);
    scene->layers[0].visible = true;
    scene->layers[1].z_base = 100; copy_str(scene->layers[1].name, "applications", HC_MAX_NAME_LEN);
    scene->layers[1].visible = true;
    scene->layers[2].z_base = 200; copy_str(scene->layers[2].name, "notifications", HC_MAX_NAME_LEN);
    scene->layers[2].visible = true;
    scene->layers[3].z_base = 300; copy_str(scene->layers[3].name, "overlay", HC_MAX_NAME_LEN);
    scene->layers[3].visible = true;
    scene->num_layers = HC_MAX_RENDER_LAYERS;

    /* Default active dimensions */
    scene->dimensions_active[HC_DIM_ACTIVE_BG] = true;
    scene->dimensions_active[HC_DIM_APP_GROUP] = true;
    scene->dimensions_active[HC_DIM_WORK_PLAY] = true;
    scene->num_active_dimensions = 3;
}

/* ===== Display Mode ===== */

void hc_set_display_mode(hc_scene_t *scene, hc_display_mode_t mode) {
    if (!scene) return;
    if (mode == scene->display_mode) return;

    /* Track degradation if downgrading */
    if (mode > scene->display_mode) {
        scene->total_degradations++;
    }

    scene->display_mode = mode;

    /* Re-project all cells for new display mode */
    hc_project(scene);
}

/* ===== Cell Management ===== */

int32_t hc_create_cell(hc_scene_t *scene, const char *name,
                        hc_cell_type_t type) {
    if (!scene || !name) return -1;

    for (uint32_t i = 0; i < HC_MAX_CELLS; i++) {
        if (!scene->cells[i].registered) {
            ev_memset(&scene->cells[i], 0, sizeof(scene->cells[i]));
            scene->cells[i].id = scene->next_cell_id++;
            copy_str(scene->cells[i].name, name, HC_MAX_NAME_LEN);
            scene->cells[i].type = type;
            scene->cells[i].state = HC_CELL_INACTIVE;
            scene->cells[i].parent_idx = -1;
            scene->cells[i].bg_color = 0;
            scene->cells[i].border_color = 0;
            scene->cells[i].registered = true;
            scene->num_cells++;
            return (int32_t)i;
        }
    }
    return -1;
}

hc_cell_t *hc_get_cell(hc_scene_t *scene, uint32_t idx) {
    if (!scene || idx >= HC_MAX_CELLS) return NULL;
    if (!scene->cells[idx].registered) return NULL;
    return &scene->cells[idx];
}

bool hc_cell_set_coord(hc_scene_t *scene, uint32_t idx,
                        hc_dimension_t dim, uint8_t value) {
    if (!scene) return false;
    hc_cell_t *cell = hc_get_cell(scene, idx);
    if (!cell) return false;
    if (dim >= HC_MAX_DIMENSIONS) return false;
    cell->coords[dim] = value;
    return true;
}

bool hc_cell_subscribe(hc_scene_t *scene, uint32_t idx,
                        const char *schema) {
    if (!scene || !schema) return false;
    hc_cell_t *cell = hc_get_cell(scene, idx);
    if (!cell) return false;
    copy_str(cell->subscribed_schema, schema, EV_SCHEMA_LEN);
    return true;
}

bool hc_cell_set_parent(hc_scene_t *scene, uint32_t child_idx,
                         int32_t parent_idx) {
    if (!scene) return false;
    hc_cell_t *child = hc_get_cell(scene, child_idx);
    if (!child) return false;
    if (parent_idx >= 0) {
        hc_cell_t *parent = hc_get_cell(scene, (uint32_t)parent_idx);
        if (!parent) return false;
        if (parent->num_children >= HC_MAX_CHILDREN) return false;
        parent->children[parent->num_children++] = child_idx;
    }
    /* Remove from old parent's children list */
    if (child->parent_idx >= 0) {
        hc_cell_t *old = hc_get_cell(scene, (uint32_t)child->parent_idx);
        if (old) {
            for (uint32_t i = 0; i < old->num_children; i++) {
                if (old->children[i] == child_idx) {
                    old->children[i] = old->children[--old->num_children];
                    break;
                }
            }
        }
    }
    child->parent_idx = parent_idx;
    return true;
}

bool hc_cell_set_colors(hc_scene_t *scene, uint32_t idx,
                         uint32_t bg, uint32_t border) {
    if (!scene) return false;
    hc_cell_t *cell = hc_get_cell(scene, idx);
    if (!cell) return false;
    cell->bg_color = bg;
    cell->border_color = border;
    return true;
}

/* ===== Focus Management ===== */

bool hc_set_focus(hc_scene_t *scene, uint32_t idx) {
    if (!scene) return false;
    hc_cell_t *cell = hc_get_cell(scene, idx);
    if (!cell) return false;

    /* Remove focus from previous */
    if (scene->focused_cell_idx >= 0) {
        hc_cell_t *prev = hc_get_cell(scene, (uint32_t)scene->focused_cell_idx);
        if (prev && prev->state == HC_CELL_FOCUSED) {
            prev->state = HC_CELL_VISIBLE;
        }
    }

    cell->state = HC_CELL_FOCUSED;
    scene->focused_cell_idx = (int32_t)idx;
    return true;
}

int32_t hc_get_focused(hc_scene_t *scene) {
    if (!scene) return -1;
    return scene->focused_cell_idx;
}

/* ===== Projection ===== */

/* Map N-dimensional coordinates to 2D screen position.
 * Uses the active dimensions to compute x, y, and z-order. */

int32_t hc_dim_to_x(uint8_t coords[HC_MAX_DIMENSIONS],
                     uint32_t num_dims, int32_t screen_w) {
    /* Use first two active dimensions for x position */
    int32_t x = 0;
    uint32_t dims_used = 0;
    for (uint32_t d = 0; d < HC_MAX_DIMENSIONS && dims_used < 2; d++) {
        x = x * 256 + coords[d];
        dims_used++;
    }
    /* Normalize to screen width */
    if (num_dims == 0) return 0;
    int32_t span = 256;
    for (uint32_t i = 1; i < num_dims && i < 2; i++) span *= 256;
    return (x * screen_w) / span;
}

int32_t hc_dim_to_y(uint8_t coords[HC_MAX_DIMENSIONS],
                     uint32_t num_dims, int32_t screen_h) {
    /* Use dimensions 2-3 for y position */
    int32_t y = 0;
    uint32_t dims_used = 0;
    for (uint32_t d = 2; d < HC_MAX_DIMENSIONS && dims_used < 2; d++) {
        y = y * 256 + coords[d];
        dims_used++;
    }
    if (num_dims <= 2) return 0;
    int32_t span = 256;
    uint32_t avail = (num_dims > 2) ? num_dims - 2 : 1;
    for (uint32_t i = 1; i < avail && i < 2; i++) span *= 256;
    return (y * screen_h) / span;
}

int32_t hc_dim_to_z(uint8_t coords[HC_MAX_DIMENSIONS],
                     uint32_t num_dims) {
    /* Use remaining dimensions for z-ordering */
    int32_t z = 0;
    for (uint32_t d = 4; d < num_dims && d < HC_MAX_DIMENSIONS; d++) {
        z += coords[d];
    }
    return z;
}

void hc_project(hc_scene_t *scene) {
    if (!scene) return;

    /* Screen dimensions depend on display mode */
    int32_t screen_w, screen_h;
    switch (scene->display_mode) {
        case HC_DISPLAY_HOLOGRAPHIC: screen_w = 1920; screen_h = 1080; break;
        case HC_DISPLAY_3D_CAPABLE:  screen_w = 1920; screen_h = 1080; break;
        case HC_DISPLAY_2D_NORMAL:   screen_w = 1280; screen_h = 720;  break;
        case HC_DISPLAY_LOW_POWER:   screen_w = 800;  screen_h = 600;  break;
        case HC_DISPLAY_SCREEN_READER: screen_w = 80; screen_h = 24;   break;
        case HC_DISPLAY_TERMINAL:    screen_w = 80;   screen_h = 24;   break;
        default:                     screen_w = 1280; screen_h = 720;  break;
    }

    for (uint32_t i = 0; i < HC_MAX_CELLS; i++) {
        hc_cell_t *cell = &scene->cells[i];
        if (!cell->registered) continue;

        /* Compute 2D position from N-D coordinates */
        cell->x = hc_dim_to_x(cell->coords, scene->num_active_dimensions, screen_w);
        cell->y = hc_dim_to_y(cell->coords, scene->num_active_dimensions, screen_h);
        cell->z_order = hc_dim_to_z(cell->coords, scene->num_active_dimensions);

        /* Cell size depends on display mode */
        switch (scene->display_mode) {
            case HC_DISPLAY_HOLOGRAPHIC:
            case HC_DISPLAY_3D_CAPABLE:
                cell->w = 400; cell->h = 300; break;
            case HC_DISPLAY_2D_NORMAL:
                cell->w = 320; cell->h = 240; break;
            case HC_DISPLAY_LOW_POWER:
                cell->w = 200; cell->h = 150; break;
            case HC_DISPLAY_SCREEN_READER:
            case HC_DISPLAY_TERMINAL:
                cell->w = 80; cell->h = 3; break;
            default:
                cell->w = 320; cell->h = 240; break;
        }

        /* Visibility based on display mode and cell state */
        if (cell->state == HC_CELL_HIDDEN || cell->state == HC_CELL_INACTIVE) {
            cell->visible_in_current_projection = false;
        } else if (scene->display_mode == HC_DISPLAY_LOW_POWER) {
            /* Low-power: only show focused and notification cells */
            cell->visible_in_current_projection =
                (cell->state == HC_CELL_FOCUSED || cell->type == HC_CELL_NOTIFICATION);
        } else if (scene->display_mode == HC_DISPLAY_TERMINAL ||
                   scene->display_mode == HC_DISPLAY_SCREEN_READER) {
            /* Terminal: show all non-background cells */
            cell->visible_in_current_projection =
                (cell->coords[HC_DIM_ACTIVE_BG] == 1);
        } else {
            cell->visible_in_current_projection = true;
        }

        /* Mark degraded cells */
        if (scene->display_mode >= HC_DISPLAY_LOW_POWER &&
            cell->state == HC_CELL_VISIBLE) {
            cell->state = HC_CELL_DEGRADED;
        }
    }
}

/* ===== Transitions ===== */

uint32_t hc_start_transition(hc_scene_t *scene, uint32_t cell_idx,
                              const uint8_t target_coords[HC_MAX_DIMENSIONS],
                              uint32_t steps) {
    if (!scene || !target_coords) return 0;
    hc_cell_t *cell = hc_get_cell(scene, cell_idx);
    if (!cell) return 0;
    if (steps == 0) steps = 5; /* default */

    for (uint32_t i = 0; i < HC_MAX_TRANSITIONS; i++) {
        if (!scene->transitions[i].active) {
            scene->transitions[i].id = scene->next_transition_id++;
            scene->transitions[i].cell_idx = cell_idx;
            scene->transitions[i].start_sequence = 0; /* set by caller */
            scene->transitions[i].completion_sequence = 0;
            ev_memcpy(scene->transitions[i].from_coords, cell->coords, HC_MAX_DIMENSIONS);
            ev_memcpy(scene->transitions[i].to_coords, target_coords, HC_MAX_DIMENSIONS);
            scene->transitions[i].progress = 0;
            scene->transitions[i].total_steps = steps;
            scene->transitions[i].active = true;
            scene->num_active_transitions++;

            cell->state = HC_CELL_TRANSITIONING;
            return scene->transitions[i].id;
        }
    }
    return 0;
}

bool hc_advance_transition(hc_scene_t *scene, uint32_t trans_idx,
                            uint64_t current_sequence) {
    if (!scene || trans_idx >= HC_MAX_TRANSITIONS) return false;
    hc_transition_t *t = &scene->transitions[trans_idx];
    if (!t->active) return false;

    t->progress++;
    (void)current_sequence;

    /* Interpolate coordinates */
    hc_cell_t *cell = hc_get_cell(scene, t->cell_idx);
    if (!cell) {
        t->active = false;
        if (scene->num_active_transitions > 0) scene->num_active_transitions--;
        return false;
    }

    if (t->progress >= t->total_steps) {
        /* Transition complete */
        ev_memcpy(cell->coords, t->to_coords, HC_MAX_DIMENSIONS);
        cell->state = HC_CELL_VISIBLE;
        t->active = false;
        if (scene->num_active_transitions > 0) scene->num_active_transitions--;
        scene->total_transitions_completed++;
        hc_project(scene);
        return true;
    }

    /* Linear interpolation of coordinates */
    for (uint32_t d = 0; d < HC_MAX_DIMENSIONS; d++) {
        int32_t from = t->from_coords[d];
        int32_t to = t->to_coords[d];
        int32_t val = from + ((to - from) * (int32_t)t->progress) / (int32_t)t->total_steps;
        if (val < 0) val = 0;
        if (val > 255) val = 255;
        cell->coords[d] = (uint8_t)val;
    }

    hc_project(scene);
    return true;
}

void hc_update_transitions(hc_scene_t *scene, uint64_t current_sequence) {
    if (!scene) return;
    for (uint32_t i = 0; i < HC_MAX_TRANSITIONS; i++) {
        if (scene->transitions[i].active) {
            hc_advance_transition(scene, i, current_sequence);
        }
    }
}

/* ===== Render ===== */

void hc_render(hc_scene_t *scene) {
    if (!scene) return;

    /* Update transitions before rendering */
    hc_update_transitions(scene, 0);

    /* Terminal/screen-reader mode uses textual rendering */
    if (scene->display_mode == HC_DISPLAY_TERMINAL ||
        scene->display_mode == HC_DISPLAY_SCREEN_READER) {
        /* Textual rendering is handled by hc_render_textual */
        scene->total_renders++;
        return;
    }

    /* For graphical modes, mark cells as rendered */
    for (uint32_t i = 0; i < HC_MAX_CELLS; i++) {
        hc_cell_t *cell = &scene->cells[i];
        if (!cell->registered) continue;
        if (!cell->visible_in_current_projection) continue;
        cell->total_renders++;
    }

    scene->total_renders++;
}

/* ===== Event Processing ===== */

uint32_t hc_process_event(hc_scene_t *scene, const char *schema,
                           uint64_t sequence) {
    if (!scene || !schema) return 0;
    uint32_t delivered = 0;
    (void)sequence;

    for (uint32_t i = 0; i < HC_MAX_CELLS; i++) {
        hc_cell_t *cell = &scene->cells[i];
        if (!cell->registered) continue;
        if (cell->subscribed_schema[0] == '\0') continue;
        if (str_eq(cell->subscribed_schema, schema)) {
            cell->total_events_received++;
            delivered++;
            /* Activate cell if it was inactive */
            if (cell->state == HC_CELL_INACTIVE) {
                cell->state = HC_CELL_VISIBLE;
            }
        }
    }

    scene->total_events_processed += delivered;
    return delivered;
}

/* ===== Degradation Check ===== */

hc_display_mode_t hc_check_degradation(hc_scene_t *scene) {
    if (!scene) return HC_DISPLAY_TERMINAL;

    /* Count active cells */
    uint32_t active_count = 0;
    for (uint32_t i = 0; i < HC_MAX_CELLS; i++) {
        if (scene->cells[i].registered &&
            scene->cells[i].state != HC_CELL_INACTIVE &&
            scene->cells[i].state != HC_CELL_HIDDEN) {
            active_count++;
        }
    }

    /* Degrade if too many cells for current mode */
    if (scene->display_mode == HC_DISPLAY_HOLOGRAPHIC && active_count > 32) {
        return HC_DISPLAY_3D_CAPABLE;
    }
    if (scene->display_mode == HC_DISPLAY_3D_CAPABLE && active_count > 48) {
        return HC_DISPLAY_2D_NORMAL;
    }
    if (scene->display_mode == HC_DISPLAY_2D_NORMAL && active_count > 56) {
        return HC_DISPLAY_LOW_POWER;
    }

    return scene->display_mode;
}

/* ===== Textual Rendering ===== */

void hc_render_textual(hc_scene_t *scene, char *buf, uint32_t buf_size) {
    if (!scene || !buf || buf_size == 0) return;
    uint32_t pos = 0;

    /* Header */
    const char *mode_names[] = {
        "HOLOGRAPHIC", "3D_CAPABLE", "2D_NORMAL",
        "LOW_POWER", "SCREEN_READER", "TERMINAL"
    };
    const char *mode = mode_names[(uint32_t)scene->display_mode];

    /* Write header */
    uint32_t i;
    const char *header = "=== ZXV Hypercube Scene ===\n";
    for (i = 0; header[i] && pos + 1 < buf_size; i++) buf[pos++] = header[i];

    buf[pos++] = 'M'; buf[pos++] = 'o'; buf[pos++] = 'd'; buf[pos++] = 'e';
    buf[pos++] = ':'; buf[pos++] = ' ';
    for (i = 0; mode[i] && pos + 1 < buf_size; i++) buf[pos++] = mode[i];
    buf[pos++] = '\n';

    /* List visible cells */
    for (uint32_t c = 0; c < HC_MAX_CELLS && pos + 1 < buf_size; c++) {
        hc_cell_t *cell = &scene->cells[c];
        if (!cell->registered) continue;
        if (!cell->visible_in_current_projection) continue;

        /* Cell name */
        buf[pos++] = '[';
        for (i = 0; cell->name[i] && pos + 1 < buf_size; i++)
            buf[pos++] = cell->name[i];
        buf[pos++] = ']';
        buf[pos++] = ' ';

        /* State */
        const char *state_str = "";
        switch (cell->state) {
            case HC_CELL_VISIBLE:    state_str = "vis"; break;
            case HC_CELL_FOCUSED:    state_str = "foc"; break;
            case HC_CELL_HIDDEN:     state_str = "hid"; break;
            case HC_CELL_TRANSITIONING: state_str = "trn"; break;
            case HC_CELL_DEGRADED:   state_str = "dgr"; break;
            default:                 state_str = "ina"; break;
        }
        for (i = 0; state_str[i] && pos + 1 < buf_size; i++)
            buf[pos++] = state_str[i];

        buf[pos++] = '\n';
    }

    buf[pos] = '\0';
}
