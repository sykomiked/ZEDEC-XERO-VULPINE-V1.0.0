/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* spindle_model.h — Spindle, the console, as a model (zxm.h): the methods
 * zxvd offers (rpc.discover), a raw call with JSON parameters, the last
 * replies, and AI help reading a reply. No drawing, no allocation. */
#ifndef SPINDLE_MODEL_H
#define SPINDLE_MODEL_H

#include "zxm.h"

#define SPINDLE_METHODS    128u
#define SPINDLE_METHOD_MAX 63u
#define SPINDLE_PARAMS_MAX 4096u /* bytes of the params object as typed */
#define SPINDLE_REPLY_MAX  8192u /* bytes of a reply kept in the history */
#define SPINDLE_HISTORY    16u

typedef struct {
    char method[SPINDLE_METHOD_MAX + 1];
    char params[SPINDLE_PARAMS_MAX + 1];
    char reply[SPINDLE_REPLY_MAX + 1]; /* the result as JSON, or the error message */
    uint32_t reply_len;
    bool reply_cut;   /* the reply was longer than SPINDLE_REPLY_MAX */
    int rc;           /* ZXM_OK, ZXM_E_REMOTE (err_code says which) or local */
    int32_t err_code; /* zxvd's code on ZXM_E_REMOTE */
} spindle_entry_t;

typedef struct {
    zxm_link_t *link;
    char methods[SPINDLE_METHODS][SPINDLE_METHOD_MAX + 1];
    uint32_t n_methods;
    char protocol[64];
    spindle_entry_t hist[SPINDLE_HISTORY]; /* a ring */
    uint32_t n_hist;                       /* calls made, ever */
    zxm_held_t held;                       /* the last AI answer (M3) */
} spindle_t;

enum { SPINDLE_AI_EXPLAIN, SPINDLE_AI_COUNT };
extern const zxm_ai_action_t SPINDLE_AI[SPINDLE_AI_COUNT];

void spindle_init(spindle_t *s, zxm_link_t *link);
int spindle_discover(spindle_t *s); /* fills methods and protocol */

/* Call `method` with `params` (a JSON object, "" for none). Calls that
 * reach zxvd, including ones it refuses, enter the history; a bad method
 * name or params that are not one JSON object are refused (ZXM_E_ARG) and
 * not sent. */
int spindle_call(spindle_t *s, const char *method, const char *params);

/* History: 0 is the newest; NULL past the end. */
uint32_t spindle_history_len(const spindle_t *s);
const spindle_entry_t *spindle_entry(const spindle_t *s, uint32_t i);

/* AI: explain history entry i in plain words; the answer is held (M3).
 * Accepting only marks it read: it changes nothing in zxvd. */
int spindle_ai(spindle_t *s, int32_t action, uint32_t i);
int spindle_ai_accept(spindle_t *s);
int spindle_ai_reject(spindle_t *s);

#endif
