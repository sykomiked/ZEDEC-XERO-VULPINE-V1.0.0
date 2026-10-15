/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* circuit.h — the Circuit board's screen: apps wired like a circuit diagram.
 *
 * Three panes over the board model (zxboard.h): the shelf of everything
 * that can be placed, the board itself, and the part's inspector.
 *
 *   CV1 PLACE. Opening a shelf row (double click, Enter) puts that entry on
 *       the board as a part: a carved tile with its vector badge, its name,
 *       what it is ("fees.fee"), its inputs down the left and its answer on
 *       the right. The board's own inputs sit at the left edge and its
 *       answer at the right.
 *   CV2 WIRE. Drag from an answer (a dot on a right edge) to an input (a dot
 *       on a left edge). The same can be done without a pointer: pick a part,
 *       pick one of its inputs in the inspector, then pick its source in the
 *       "Wire from" list, or type a setting ("25" or "1,2,3") and press
 *       Enter. Dragging a tile moves it.
 *   CV3 THE LOGIC, DRAWN. Each wire is drawn in its state (zxboard_check):
 *       TRUE a solid green line, BOTH a dashed gold line tagged "held",
 *       NEITHER a faint dot with nothing in it. Round dots carry numbers,
 *       square dots truths. The status line says the first problem in words.
 *       The answer's kind (numbers or a truth) is the person's choice, a
 *       button on the answer's inspector; it is never changed to fit a wire.
 *   CV4 SAVE is the screen's one gold button: it writes the manifest and
 *       shows its content id, or says in words why it cannot (an open wire,
 *       a loop).
 *   CV5 Text is drawn with the toolkit's fonts; the badge is a vector mark
 *       (a ring and the part's initial), never a bitmap.
 */
#ifndef ZXV_VIEW_CIRCUIT_H
#define ZXV_VIEW_CIRCUIT_H

#include "zv.h"
#include "zxboard.h"

typedef struct {
    zv_shell_t sh;
    zxboard_t *b;
    zxboard_check_t chk;
    zui_id_t shelf, board, side, part_name, part_what, kind, ports, sources, setting, remove, save;
    char setting_buf[64];
    int node;     /* ZXBOARD_NODE_*, or -1: nothing picked */
    uint32_t sel; /* which part or input */
    int32_t port; /* the picked input of the picked part, or -1 */
    /* a drag on the board */
    int drag; /* 0 none, 1 moving a node, 2 drawing a wire */
    int drag_node;
    uint32_t drag_i;
    int32_t grab_x, grab_y, ptr_x, ptr_y;
    char saved[ZXBOARD_TEXT];
    int32_t saved_len;
    char cid[ZXAPP_CID_MAX];
    char status_text[ZUI_LABEL_MAX];
} cb_view_t;

void cb_view_build(cb_view_t *v, zui_ui_t *ui, zxboard_t *b);
void cb_view_sync(cb_view_t *v);
bool cb_view_action(cb_view_t *v, zui_id_t w, zui_action_t a, int32_t arg);
/* CV2: pointer events over the board (the shell passes them on first);
 * true when the board used it. */
bool cb_view_pointer(cb_view_t *v, const zui_event_t *e);

/* Where things are on the board, in device pixels (for tests and the
 * shell): a node's tile, an input dot of a part, a node's answer dot. */
zui_rect_t cb_node_rect(const cb_view_t *v, int node, uint32_t i);
void cb_port_at(const cb_view_t *v, uint32_t part, uint32_t port, int32_t *x, int32_t *y);
void cb_answer_at(const cb_view_t *v, int node, uint32_t i, int32_t *x, int32_t *y);

#endif /* ZXV_VIEW_CIRCUIT_H */
