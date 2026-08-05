/* hypercube_scene.h — ZXV Hypercube Scene Architecture
 *
 * Applications and system spaces occupy cells or faces in a
 * multidimensional logical workspace. The user sees a controlled
 * 2D or 3D projection of this space.
 *
 * Architecture:
 *   Logical N-dim space → hypercube transform → 3D scene → 2D display
 *
 * Dimensions represent: active/background, personal/shared, local/remote,
 * trusted/untrusted, current/historical, physical/simulated, work/entertainment,
 * application grouping, Chiglet context, event ancestry.
 *
 * Graceful degradation:
 *   Holographic → full spatial projection
 *   3D display  → depth-enhanced scene
 *   Normal 2D   → animated planar projection
 *   Low-power   → simplified flat interface
 *   Screen reader → semantic navigation tree
 *   Terminal    → textual event/domain representation
 *
 * Event-driven animation: transitions are tied to event sequence progress,
 * not wall-clock frame rates. The display service maps logical progression
 * onto available physical refresh opportunities.
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#ifndef HYPERCUBE_SCENE_H
#define HYPERCUBE_SCENE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "../event_space/event_space.h"

/* ===== Constants ===== */

#define HC_MAX_CELLS          64    /* max cells in the hypercube */
#define HC_MAX_DIMENSIONS     10    /* max logical dimensions */
#define HC_MAX_TRANSITIONS    32    /* max active transitions */
#define HC_MAX_NAME_LEN       32    /* cell/scene name length */
#define HC_MAX_CHILDREN       8     /* max children per cell */
#define HC_MAX_RENDER_LAYERS  4     /* compositing layers */

/* ===== Display Modes ===== */

typedef enum {
    HC_DISPLAY_HOLOGRAPHIC = 0,  /* full spatial projection */
    HC_DISPLAY_3D_CAPABLE  = 1,  /* depth-enhanced scene */
    HC_DISPLAY_2D_NORMAL   = 2,  /* animated planar projection */
    HC_DISPLAY_LOW_POWER   = 3,  /* simplified flat interface */
    HC_DISPLAY_SCREEN_READER = 4, /* semantic navigation tree */
    HC_DISPLAY_TERMINAL    = 5,  /* textual event/domain representation */
} hc_display_mode_t;

/* ===== Cell Types ===== */

typedef enum {
    HC_CELL_EMPTY     = 0,
    HC_CELL_APPLICATION = 1,  /* app window */
    HC_CELL_SERVICE   = 2,    /* system service */
    HC_CELL_CONTAINER = 3,    /* groups other cells */
    HC_CELL_BACKGROUND = 4,   /* background process */
    HC_CELL_NOTIFICATION = 5,  /* notification/overlay */
} hc_cell_type_t;

/* ===== Cell State ===== */

typedef enum {
    HC_CELL_INACTIVE   = 0,
    HC_CELL_VISIBLE    = 1,   /* rendered on current projection */
    HC_CELL_FOCUSED    = 2,   /* has input focus */
    HC_CELL_HIDDEN     = 3,   /* exists but not projected */
    HC_CELL_TRANSITIONING = 4, /* animating between positions */
    HC_CELL_DEGRADED   = 5,   /* reduced rendering quality */
} hc_cell_state_t;

/* ===== Logical Dimensions ===== */

typedef enum {
    HC_DIM_ACTIVE_BG     = 0,  /* 0=background, 1=active */
    HC_DIM_PERSONAL_SHARED = 1, /* 0=personal, 1=shared */
    HC_DIM_LOCAL_REMOTE  = 2,  /* 0=local, 1=remote */
    HC_DIM_TRUST         = 3,  /* 0=untrusted, 1=trusted */
    HC_DIM_TEMPORAL      = 4,  /* 0=current, 1=historical */
    HC_DIM_PHYS_SIM      = 5,  /* 0=physical, 1=simulated */
    HC_DIM_WORK_PLAY     = 6,  /* 0=work, 1=entertainment */
    HC_DIM_APP_GROUP     = 7,  /* application grouping axis */
    HC_DIM_CHIGLET_CTX   = 8,  /* Chiglet context */
    HC_DIM_EVENT_ANCESTRY = 9, /* event ancestry depth */
} hc_dimension_t;

/* ===== Scene Cell ===== */

typedef struct hc_cell {
    uint32_t id;
    char name[HC_MAX_NAME_LEN];
    hc_cell_type_t type;
    hc_cell_state_t state;

    /* Logical position in N-dimensional space */
    uint8_t coords[HC_MAX_DIMENSIONS];

    /* 2D projection rectangle (computed from coords) */
    int32_t x, y, w, h;
    int32_t z_order;               /* compositing order */

    /* Parent/children for hierarchical scene graph */
    int32_t parent_idx;            /* -1 = root level */
    uint32_t children[HC_MAX_CHILDREN];
    uint32_t num_children;

    /* Rendering */
    uint32_t bg_color;
    uint32_t border_color;
    bool visible_in_current_projection;

    /* Event subscription — this cell reacts to events with this schema */
    char subscribed_schema[EV_SCHEMA_LEN];

    /* Statistics */
    uint64_t total_events_received;
    uint64_t total_renders;

    bool registered;
} hc_cell_t;

/* ===== Transition (Event-Driven Animation) ===== */

typedef struct hc_transition {
    uint32_t id;
    uint32_t cell_idx;             /* which cell is transitioning */
    uint64_t start_sequence;       /* event sequence when transition started */
    uint64_t completion_sequence;  /* event sequence when transition completes */

    /* Source and target coordinates */
    uint8_t from_coords[HC_MAX_DIMENSIONS];
    uint8_t to_coords[HC_MAX_DIMENSIONS];

    /* Progress: 0 to HC_TRANSITION_STEPS */
    uint32_t progress;
    uint32_t total_steps;

    bool active;
} hc_transition_t;

/* ===== Render Layer ===== */

typedef struct hc_render_layer {
    char name[HC_MAX_NAME_LEN];
    int32_t z_base;                /* base z-order for this layer */
    bool visible;
} hc_render_layer_t;

/* ===== Hypercube Scene ===== */

typedef struct hc_scene {
    /* Display mode — determines projection quality */
    hc_display_mode_t display_mode;

    /* Active dimensions (which dims are being used) */
    bool dimensions_active[HC_MAX_DIMENSIONS];
    uint32_t num_active_dimensions;

    /* Scene cells */
    hc_cell_t cells[HC_MAX_CELLS];
    uint32_t num_cells;
    uint32_t next_cell_id;

    /* Active transitions */
    hc_transition_t transitions[HC_MAX_TRANSITIONS];
    uint32_t num_active_transitions;
    uint32_t next_transition_id;

    /* Render layers */
    hc_render_layer_t layers[HC_MAX_RENDER_LAYERS];
    uint32_t num_layers;

    /* Focused cell */
    int32_t focused_cell_idx;      /* -1 = none */

    /* Statistics */
    uint64_t total_renders;
    uint64_t total_transitions_completed;
    uint64_t total_events_processed;
    uint64_t total_degradations;   /* times display mode was downgraded */
} hc_scene_t;

/* ===== API ===== */

/* Initialize the scene */
void hc_init(hc_scene_t *scene, hc_display_mode_t display_mode);

/* Set display mode (triggers graceful degradation if downgraded) */
void hc_set_display_mode(hc_scene_t *scene, hc_display_mode_t mode);

/* Cell management */
int32_t hc_create_cell(hc_scene_t *scene, const char *name,
                        hc_cell_type_t type);
hc_cell_t *hc_get_cell(hc_scene_t *scene, uint32_t idx);
bool hc_cell_set_coord(hc_scene_t *scene, uint32_t idx,
                        hc_dimension_t dim, uint8_t value);
bool hc_cell_subscribe(hc_scene_t *scene, uint32_t idx,
                        const char *schema);
bool hc_cell_set_parent(hc_scene_t *scene, uint32_t child_idx,
                         int32_t parent_idx);
bool hc_cell_set_colors(hc_scene_t *scene, uint32_t idx,
                         uint32_t bg, uint32_t border);

/* Focus management */
bool hc_set_focus(hc_scene_t *scene, uint32_t idx);
int32_t hc_get_focused(hc_scene_t *scene);

/* Projection: compute 2D positions from N-dimensional coordinates */
void hc_project(hc_scene_t *scene);

/* Transitions (event-driven animation) */
uint32_t hc_start_transition(hc_scene_t *scene, uint32_t cell_idx,
                              const uint8_t target_coords[HC_MAX_DIMENSIONS],
                              uint32_t steps);
bool hc_advance_transition(hc_scene_t *scene, uint32_t trans_idx,
                            uint64_t current_sequence);
void hc_update_transitions(hc_scene_t *scene, uint64_t current_sequence);

/* Render: produce output for current display mode */
void hc_render(hc_scene_t *scene);

/* Process an event — deliver to subscribed cells */
uint32_t hc_process_event(hc_scene_t *scene, const char *schema,
                           uint64_t sequence);

/* Degradation: check if display mode should be downgraded */
hc_display_mode_t hc_check_degradation(hc_scene_t *scene);

/* Get textual representation for terminal mode */
void hc_render_textual(hc_scene_t *scene, char *buf, uint32_t buf_size);

/* ===== Dimension helpers ===== */

/* Map a dimension value to a 2D position component */
int32_t hc_dim_to_x(uint8_t coords[HC_MAX_DIMENSIONS],
                     uint32_t num_dims, int32_t screen_w);
int32_t hc_dim_to_y(uint8_t coords[HC_MAX_DIMENSIONS],
                     uint32_t num_dims, int32_t screen_h);
int32_t hc_dim_to_z(uint8_t coords[HC_MAX_DIMENSIONS],
                     uint32_t num_dims);

#endif /* HYPERCUBE_SCENE_H */
