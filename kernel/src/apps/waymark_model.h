/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* waymark_model.h — Waymark, places and location, as a model (zxm.h): where
 * this machine is and how sure, the places the owner keeps (nearest first
 * once the location is known), and which apps may know the location, with
 * the asks waiting for the owner's answer (zxvd's place.*,
 * kernel/arch/hosted/zxv_place.h). AI can plan a day around the kept
 * places, held until read (M3). No drawing, no allocation. */
#ifndef WAYMARK_MODEL_H
#define WAYMARK_MODEL_H

#include "zxm.h"

#define WAYMARK_NAME   63u
#define WAYMARK_NOTE   255u
#define WAYMARK_APP    31u
#define WAYMARK_PLACES 200u
#define WAYMARK_APPS   64u

enum { WAYMARK_ASK, WAYMARK_ONCE, WAYMARK_ALWAYS, WAYMARK_NEVER };

typedef struct {
    bool known, stale;
    int32_t lat, lon; /* millionths of a degree */
    uint32_t accuracy_m;
    char source[16];
    int64_t time;
} waymark_here_t;

typedef struct {
    char name[WAYMARK_NAME + 1];
    int32_t lat, lon;
    char note[WAYMARK_NOTE + 1];
    int64_t distance_m; /* from here; -1 when here is not known */
} waymark_place_t;

typedef struct {
    char app[WAYMARK_APP + 1];
    uint8_t mode;
    bool pending;
    int64_t asked;
} waymark_app_t;

typedef struct {
    zxm_link_t *link;
    waymark_here_t here;
    bool reset;
    waymark_place_t place[WAYMARK_PLACES]; /* nearest first when here is known */
    uint32_t nplaces;
    waymark_app_t app[WAYMARK_APPS];
    uint32_t napps, npending;
    zxm_held_t held;
} waymark_t;

enum { WAYMARK_AI_PLAN, WAYMARK_AI_COUNT };
extern const zxm_ai_action_t WAYMARK_AI[WAYMARK_AI_COUNT];

void waymark_init(waymark_t *w, zxm_link_t *link);
int waymark_refresh(waymark_t *w);
/* The owner says where the machine is (no system location service yet). */
int waymark_set_here(waymark_t *w, int32_t lat, int32_t lon, uint32_t accuracy_m);
int waymark_forget_here(waymark_t *w);
int waymark_add(waymark_t *w, const char *name, int32_t lat, int32_t lon, const char *note);
/* Keep the location itself as a place. */
int waymark_add_here(waymark_t *w, const char *name, const char *note);
int waymark_remove(waymark_t *w, const char *name);
/* Answer an app's ask, or change its answer. */
int waymark_permit(waymark_t *w, const char *app, uint8_t mode);
const waymark_place_t *waymark_find(const waymark_t *w, const char *name);

int waymark_ai(waymark_t *w, int32_t action);
int waymark_ai_accept(waymark_t *w);
int waymark_ai_reject(waymark_t *w);

#endif
