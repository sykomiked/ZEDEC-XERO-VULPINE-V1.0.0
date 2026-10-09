/* test_hypercube.c — Host-side tests for Hypercube Scene Architecture
 *
 * Tests scene graph, projection, transitions, event processing,
 * graceful degradation, and textual rendering.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "event_space.h"
#include "hypercube_scene.h"

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void name(void)
#define RUN(name) do { \
    printf("  [TEST] %s ... ", #name); \
    tests_run++; \
    name(); \
} while (0)

#define PASS() do { printf("PASS\n"); tests_passed++; } while (0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); tests_failed++; return; } while (0)
#define ASSERT(cond, msg) \
    do { \
        if (!(cond)) { FAIL(msg); } \
    } while (0)

/* ===== Tests ===== */

TEST(hc_init_test) {
    static hc_scene_t scene;
    hc_init(&scene, HC_DISPLAY_2D_NORMAL);
    ASSERT(scene.num_cells == 0, "no cells after init");
    ASSERT(scene.display_mode == HC_DISPLAY_2D_NORMAL, "display mode set");
    ASSERT(scene.focused_cell_idx == -1, "no focus after init");
    ASSERT(scene.num_layers == HC_MAX_RENDER_LAYERS, "4 render layers");
    ASSERT(scene.num_active_dimensions == 3, "3 default active dimensions");
    PASS();
}

TEST(hc_create_cell_test) {
    static hc_scene_t scene;
    hc_init(&scene, HC_DISPLAY_2D_NORMAL);

    int32_t idx = hc_create_cell(&scene, "terminal", HC_CELL_APPLICATION);
    ASSERT(idx >= 0, "cell creation succeeds");
    ASSERT(scene.num_cells == 1, "one cell registered");

    hc_cell_t *cell = hc_get_cell(&scene, (uint32_t)idx);
    ASSERT(cell != NULL, "cell found");
    ASSERT(strcmp(cell->name, "terminal") == 0, "name matches");
    ASSERT(cell->type == HC_CELL_APPLICATION, "type matches");
    ASSERT(cell->state == HC_CELL_INACTIVE, "starts inactive");
    ASSERT(cell->parent_idx == -1, "no parent initially");
    PASS();
}

TEST(hc_cell_coords_test) {
    static hc_scene_t scene;
    hc_init(&scene, HC_DISPLAY_2D_NORMAL);

    int32_t idx = hc_create_cell(&scene, "notes", HC_CELL_APPLICATION);
    ASSERT(idx >= 0, "cell created");

    bool r = hc_cell_set_coord(&scene, (uint32_t)idx, HC_DIM_ACTIVE_BG, 1);
    ASSERT(r, "set coord ACTIVE_BG");
    r = hc_cell_set_coord(&scene, (uint32_t)idx, HC_DIM_WORK_PLAY, 0);
    ASSERT(r, "set coord WORK_PLAY");

    hc_cell_t *cell = hc_get_cell(&scene, (uint32_t)idx);
    ASSERT(cell->coords[HC_DIM_ACTIVE_BG] == 1, "ACTIVE_BG is 1");
    ASSERT(cell->coords[HC_DIM_WORK_PLAY] == 0, "WORK_PLAY is 0");
    PASS();
}

TEST(hc_cell_subscribe_test) {
    static hc_scene_t scene;
    hc_init(&scene, HC_DISPLAY_2D_NORMAL);

    int32_t idx = hc_create_cell(&scene, "storage-ui", HC_CELL_APPLICATION);
    bool r = hc_cell_subscribe(&scene, (uint32_t)idx, "zxv.storage.read");
    ASSERT(r, "subscribe succeeds");

    hc_cell_t *cell = hc_get_cell(&scene, (uint32_t)idx);
    ASSERT(strcmp(cell->subscribed_schema, "zxv.storage.read") == 0,
           "schema matches");
    PASS();
}

TEST(hc_focus_test) {
    static hc_scene_t scene;
    hc_init(&scene, HC_DISPLAY_2D_NORMAL);

    int32_t c1 = hc_create_cell(&scene, "app1", HC_CELL_APPLICATION);
    int32_t c2 = hc_create_cell(&scene, "app2", HC_CELL_APPLICATION);

    bool r = hc_set_focus(&scene, (uint32_t)c1);
    ASSERT(r, "focus c1");
    ASSERT(hc_get_focused(&scene) == c1, "c1 is focused");

    hc_cell_t *cell1 = hc_get_cell(&scene, (uint32_t)c1);
    ASSERT(cell1->state == HC_CELL_FOCUSED, "c1 state is FOCUSED");

    /* Switch focus to c2 */
    r = hc_set_focus(&scene, (uint32_t)c2);
    ASSERT(r, "focus c2");
    ASSERT(hc_get_focused(&scene) == c2, "c2 is focused");

    /* c1 should no longer be focused */
    cell1 = hc_get_cell(&scene, (uint32_t)c1);
    ASSERT(cell1->state == HC_CELL_VISIBLE, "c1 is now VISIBLE");

    hc_cell_t *cell2 = hc_get_cell(&scene, (uint32_t)c2);
    ASSERT(cell2->state == HC_CELL_FOCUSED, "c2 is FOCUSED");
    PASS();
}

TEST(hc_projection_test) {
    static hc_scene_t scene;
    hc_init(&scene, HC_DISPLAY_2D_NORMAL);

    int32_t idx = hc_create_cell(&scene, "app", HC_CELL_APPLICATION);
    hc_cell_set_coord(&scene, (uint32_t)idx, HC_DIM_ACTIVE_BG, 1);
    hc_cell_set_coord(&scene, (uint32_t)idx, HC_DIM_WORK_PLAY, 0);
    hc_cell_set_coord(&scene, (uint32_t)idx, HC_DIM_APP_GROUP, 2);
    hc_get_cell(&scene, (uint32_t)idx)->state = HC_CELL_VISIBLE;

    hc_project(&scene);

    hc_cell_t *cell = hc_get_cell(&scene, (uint32_t)idx);
    ASSERT(cell->x >= 0, "x >= 0");
    ASSERT(cell->y >= 0, "y >= 0");
    ASSERT(cell->w > 0, "w > 0");
    ASSERT(cell->h > 0, "h > 0");
    ASSERT(cell->visible_in_current_projection, "should be visible in 2D mode");
    PASS();
}

TEST(hc_transition_test) {
    static hc_scene_t scene;
    hc_init(&scene, HC_DISPLAY_2D_NORMAL);

    int32_t idx = hc_create_cell(&scene, "app", HC_CELL_APPLICATION);
    hc_cell_set_coord(&scene, (uint32_t)idx, HC_DIM_ACTIVE_BG, 0);

    uint8_t target[HC_MAX_DIMENSIONS];
    memset(target, 0, sizeof(target));
    target[HC_DIM_ACTIVE_BG] = 1;

    uint32_t trans_id = hc_start_transition(&scene, (uint32_t)idx, target, 5);
    ASSERT(trans_id > 0, "transition started");
    ASSERT(scene.num_active_transitions == 1, "one active transition");

    hc_cell_t *cell = hc_get_cell(&scene, (uint32_t)idx);
    ASSERT(cell->state == HC_CELL_TRANSITIONING, "cell is transitioning");

    /* Advance transition */
    bool r;
    for (uint32_t i = 0; i < 4; i++) {
        r = hc_advance_transition(&scene, 0, 0);
        ASSERT(r, "advance should succeed");
    }

    /* Final advance should complete the transition */
    r = hc_advance_transition(&scene, 0, 0);
    ASSERT(r, "final advance succeeds");
    ASSERT(scene.num_active_transitions == 0, "no active transitions");
    ASSERT(scene.total_transitions_completed == 1, "one transition completed");

    cell = hc_get_cell(&scene, (uint32_t)idx);
    ASSERT(cell->coords[HC_DIM_ACTIVE_BG] == 1, "coord updated to target");
    ASSERT(cell->state == HC_CELL_VISIBLE, "cell is visible after transition");
    PASS();
}

TEST(hc_event_processing_test) {
    static hc_scene_t scene;
    hc_init(&scene, HC_DISPLAY_2D_NORMAL);

    int32_t c1 = hc_create_cell(&scene, "storage-ui", HC_CELL_APPLICATION);
    hc_cell_subscribe(&scene, (uint32_t)c1, "zxv.storage.read");

    int32_t c2 = hc_create_cell(&scene, "network-ui", HC_CELL_APPLICATION);
    hc_cell_subscribe(&scene, (uint32_t)c2, "zxv.network.send");

    int32_t c3 = hc_create_cell(&scene, "unrelated", HC_CELL_APPLICATION);
    (void)c3;

    /* Process a storage event */
    uint32_t delivered = hc_process_event(&scene, "zxv.storage.read", 1);
    ASSERT(delivered == 1, "one cell received storage event");

    hc_cell_t *cell1 = hc_get_cell(&scene, (uint32_t)c1);
    ASSERT(cell1->total_events_received == 1, "c1 received 1 event");
    ASSERT(cell1->state == HC_CELL_VISIBLE, "c1 activated by event");

    /* Process a network event */
    delivered = hc_process_event(&scene, "zxv.network.send", 2);
    ASSERT(delivered == 1, "one cell received network event");

    /* Process an unmatched event */
    delivered = hc_process_event(&scene, "unknown.schema", 3);
    ASSERT(delivered == 0, "no cells received unknown event");

    ASSERT(scene.total_events_processed == 2, "2 total events processed");
    PASS();
}

TEST(hc_degradation_test) {
    static hc_scene_t scene;
    hc_init(&scene, HC_DISPLAY_HOLOGRAPHIC);

    /* Create many cells to trigger degradation */
    for (uint32_t i = 0; i < 40; i++) {
        char name[32];
        snprintf(name, sizeof(name), "app%u", i);
        int32_t idx = hc_create_cell(&scene, name, HC_CELL_APPLICATION);
        if (idx >= 0) {
            hc_cell_set_coord(&scene, (uint32_t)idx, HC_DIM_ACTIVE_BG, 1);
            hc_get_cell(&scene, (uint32_t)idx)->state = HC_CELL_VISIBLE;
        }
    }

    hc_display_mode_t suggested = hc_check_degradation(&scene);
    ASSERT(suggested > HC_DISPLAY_HOLOGRAPHIC,
           "should suggest degradation with 40 active cells");

    /* Downgrade and verify */
    hc_set_display_mode(&scene, suggested);
    ASSERT(scene.display_mode == suggested, "display mode changed");
    ASSERT(scene.total_degradations > 0, "degradation counted");
    PASS();
}

TEST(hc_low_power_visibility_test) {
    static hc_scene_t scene;
    hc_init(&scene, HC_DISPLAY_LOW_POWER);

    int32_t focused = hc_create_cell(&scene, "focused-app", HC_CELL_APPLICATION);
    hc_set_focus(&scene, (uint32_t)focused);

    int32_t bg = hc_create_cell(&scene, "bg-app", HC_CELL_APPLICATION);
    hc_get_cell(&scene, (uint32_t)bg)->state = HC_CELL_VISIBLE;

    int32_t notif = hc_create_cell(&scene, "alert", HC_CELL_NOTIFICATION);
    hc_get_cell(&scene, (uint32_t)notif)->state = HC_CELL_VISIBLE;

    hc_project(&scene);

    hc_cell_t *f_cell = hc_get_cell(&scene, (uint32_t)focused);
    ASSERT(f_cell->visible_in_current_projection, "focused visible in low-power");

    hc_cell_t *n_cell = hc_get_cell(&scene, (uint32_t)notif);
    ASSERT(n_cell->visible_in_current_projection, "notification visible in low-power");

    hc_cell_t *b_cell = hc_get_cell(&scene, (uint32_t)bg);
    ASSERT(!b_cell->visible_in_current_projection, "bg hidden in low-power");
    PASS();
}

TEST(hc_parent_child_test) {
    static hc_scene_t scene;
    hc_init(&scene, HC_DISPLAY_2D_NORMAL);

    int32_t parent = hc_create_cell(&scene, "container", HC_CELL_CONTAINER);
    int32_t child1 = hc_create_cell(&scene, "child1", HC_CELL_APPLICATION);
    int32_t child2 = hc_create_cell(&scene, "child2", HC_CELL_APPLICATION);

    bool r = hc_cell_set_parent(&scene, (uint32_t)child1, parent);
    ASSERT(r, "set parent for child1");
    r = hc_cell_set_parent(&scene, (uint32_t)child2, parent);
    ASSERT(r, "set parent for child2");

    hc_cell_t *p = hc_get_cell(&scene, (uint32_t)parent);
    ASSERT(p->num_children == 2, "parent has 2 children");

    hc_cell_t *c1 = hc_get_cell(&scene, (uint32_t)child1);
    ASSERT(c1->parent_idx == parent, "child1 parent matches");

    /* Reparent child1 to root */
    r = hc_cell_set_parent(&scene, (uint32_t)child1, -1);
    ASSERT(r, "reparent to root");
    ASSERT(p->num_children == 1, "parent now has 1 child");
    ASSERT(c1->parent_idx == -1, "child1 has no parent");
    PASS();
}

TEST(hc_textual_render_test) {
    static hc_scene_t scene;
    hc_init(&scene, HC_DISPLAY_TERMINAL);

    int32_t idx = hc_create_cell(&scene, "terminal", HC_CELL_APPLICATION);
    hc_cell_set_coord(&scene, (uint32_t)idx, HC_DIM_ACTIVE_BG, 1);
    hc_get_cell(&scene, (uint32_t)idx)->state = HC_CELL_VISIBLE;

    hc_project(&scene);

    char buf[1024];
    hc_render_textual(&scene, buf, sizeof(buf));
    ASSERT(strlen(buf) > 0, "textual output not empty");
    ASSERT(strstr(buf, "ZXV Hypercube Scene") != NULL, "header present");
    ASSERT(strstr(buf, "TERMINAL") != NULL, "mode shown");
    ASSERT(strstr(buf, "terminal") != NULL, "cell name shown");
    PASS();
}

TEST(hc_render_test) {
    static hc_scene_t scene;
    hc_init(&scene, HC_DISPLAY_2D_NORMAL);

    int32_t idx = hc_create_cell(&scene, "app", HC_CELL_APPLICATION);
    hc_cell_set_coord(&scene, (uint32_t)idx, HC_DIM_ACTIVE_BG, 1);
    hc_get_cell(&scene, (uint32_t)idx)->state = HC_CELL_VISIBLE;

    hc_project(&scene);
    hc_render(&scene);

    ASSERT(scene.total_renders == 1, "one render counted");

    hc_cell_t *cell = hc_get_cell(&scene, (uint32_t)idx);
    ASSERT(cell->total_renders == 1, "cell rendered once");
    PASS();
}

TEST(hc_display_mode_change_test) {
    static hc_scene_t scene;
    hc_init(&scene, HC_DISPLAY_HOLOGRAPHIC);

    int32_t idx = hc_create_cell(&scene, "app", HC_CELL_APPLICATION);
    hc_cell_set_coord(&scene, (uint32_t)idx, HC_DIM_ACTIVE_BG, 1);
    hc_get_cell(&scene, (uint32_t)idx)->state = HC_CELL_VISIBLE;
    hc_project(&scene);

    hc_cell_t *cell = hc_get_cell(&scene, (uint32_t)idx);
    int32_t old_w = cell->w;

    /* Downgrade to low-power */
    hc_set_display_mode(&scene, HC_DISPLAY_LOW_POWER);
    cell = hc_get_cell(&scene, (uint32_t)idx);
    ASSERT(cell->w < old_w, "cell smaller in low-power mode");
    ASSERT(scene.total_degradations == 1, "degradation counted");
    PASS();
}

/* ===== Main ===== */

int main(void) {
    printf("\n=== ZXV Hypercube Scene Architecture Tests ===\n\n");

    RUN(hc_init_test);
    RUN(hc_create_cell_test);
    RUN(hc_cell_coords_test);
    RUN(hc_cell_subscribe_test);
    RUN(hc_focus_test);
    RUN(hc_projection_test);
    RUN(hc_transition_test);
    RUN(hc_event_processing_test);
    RUN(hc_degradation_test);
    RUN(hc_low_power_visibility_test);
    RUN(hc_parent_child_test);
    RUN(hc_textual_render_test);
    RUN(hc_render_test);
    RUN(hc_display_mode_change_test);

    printf("\n=== Results: %d/%d passed, %d failed ===\n",
           tests_passed, tests_run, tests_failed);
    if (tests_failed == 0) {
        printf("ALL TESTS PASSED\n");
    }
    return tests_failed > 0 ? 1 : 0;
}
