/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxm.h — what every built-in app model shares (docs/STAPLE_APPS.md).
 *
 * An app model is a state struct plus action functions. It draws nothing and
 * allocates nothing: the view (kernel/src/zui/views/<app>.c, or any other
 * front end) reads the state and calls the actions. Every action talks to
 * zxvd through one zxm_link_t, which carries its own fixed buffers.
 *
 *   M1 TRANSPORT. The link sends one JSON-RPC 2.0 request line and gets one
 *      response line back through the caller's zxm_send_fn (the control
 *      socket, the window's /api/rpc, or zxvd_rpc_handle_line in-process).
 *   M2 RESULTS. Every action returns ZXM_OK or a ZXM_E_* code; on
 *      ZXM_E_REMOTE the link holds zxvd's error code and message, which the
 *      view shows as is.
 *   M3 HELD AI. Swarm output never changes a document directly: it enters
 *      the model as a zxm_held_t in ZXM_HELD_PENDING, and only the owner's
 *      accept applies it (reject drops it).
 *   M4 COST. Every action that spends swarm tokens records which agent ran
 *      it and how many tokens it took (zxm_cost_t). zxvd's swarm.ask does not
 *      report a count yet, so until it does the count is an estimate (one
 *      token per four bytes of prompt and answer) and says so (exact=false).
 *   M5 PALETTE. Each app lists its AI actions in a static zxm_ai_action_t
 *      table (name, agent, estimated tokens) for the command palette. */
#ifndef ZXM_H
#define ZXM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "web4_web2.h" /* the bounded JSON tokenizer */

#define ZXM_REQ_MAX    16384u         /* zxvd's request line limit */
#define ZXM_RESP_MAX   (320u * 1024u) /* one response: a 48 KiB read, every byte escaped */
#define ZXM_TOK_MAX    8192u          /* JSON tokens in one response */
#define ZXM_ERR_MAX    240u
#define ZXM_ASK_MAX    4000u /* bytes of prompt swarm.ask takes */
#define ZXM_ANSWER_MAX 8192u
#define ZXM_AGENT_MAX  32u
#define ZXM_SPACE_MAX  48u /* a space id (zxvd's spaces.*); "" = home */

enum {
    ZXM_OK = 0,
    ZXM_E_TRANSPORT = -1, /* the send function failed */
    ZXM_E_TOOBIG = -2,    /* a request or response does not fit */
    ZXM_E_PROTOCOL = -3,  /* not a JSON-RPC response to this request */
    ZXM_E_REMOTE = -4,    /* zxvd answered with an error (see the link) */
    ZXM_E_STATE = -5,     /* the action does not apply in this state */
    ZXM_E_ARG = -6,       /* a bad argument */
};

/* Send `req` (len bytes, no LF) and write the response line (no LF) into
 * resp; return its length, or a negative number on failure. */
typedef int32_t (*zxm_send_fn)(void *ctx, const char *req, uint32_t len, char *resp, uint32_t cap);

/* How far a long action (an import, a load) has got, after each part:
 * done of total bytes; total 0 = not known yet. Views draw it as an
 * ordinary progress bar in ZXV's green and gold. The mirrored boot-style
 * screen is only for the OS boot, launching an app and connecting to a
 * remote space, and it follows the real progress of that work. */
typedef void (*zxm_progress_fn)(void *ctx, uint64_t done, uint64_t total);

typedef struct {
    zxm_send_fn send;
    void *ctx;
    zxm_progress_fn progress; /* optional (zxm_link_progress) */
    void *progress_ctx;
    uint32_t seq;
    char req[ZXM_REQ_MAX + 1];
    uint32_t req_len;
    bool req_over;
    bool req_first;
    char resp[ZXM_RESP_MAX + 1];
    uint32_t resp_len;
    w4_jtok_t tok[ZXM_TOK_MAX];
    int32_t ntok;
    int32_t result;        /* token index of "result" after ZXM_OK */
    int32_t err_code;      /* zxvd's error code after ZXM_E_REMOTE */
    char err[ZXM_ERR_MAX]; /* its message, or what went wrong locally */
} zxm_link_t;

void zxm_link_init(zxm_link_t *l, zxm_send_fn send, void *ctx);
void zxm_link_progress(zxm_link_t *l, zxm_progress_fn fn, void *ctx); /* NULL: none */
void zxm_progress(zxm_link_t *l, uint64_t done, uint64_t total);      /* report, if set */

/* Building one request: begin, add parameters, call. A parameter that does
 * not fit makes zxm_call return ZXM_E_TOOBIG without sending anything. */
void zxm_begin(zxm_link_t *l, const char *method);
void zxm_p_str(zxm_link_t *l, const char *key, const char *val);
void zxm_p_strn(zxm_link_t *l, const char *key, const char *val, uint32_t n);
void zxm_p_u64(zxm_link_t *l, const char *key, uint64_t v);
void zxm_p_i64(zxm_link_t *l, const char *key, int64_t v);
void zxm_p_bool(zxm_link_t *l, const char *key, bool v);
/* params given as a ready JSON object (Spindle), instead of zxm_p_* */
void zxm_p_raw(zxm_link_t *l, const char *json_obj, uint32_t n);
/* "space": id, left out for home ("" or "home") */
void zxm_p_space(zxm_link_t *l, const char *space);
int zxm_call(zxm_link_t *l);

/* Reading the result (or any object/array token in it). */
int32_t zxm_get(const zxm_link_t *l, int32_t obj, const char *key);
int zxm_get_str(const zxm_link_t *l, int32_t obj, const char *key, char *out, uint32_t cap);
int zxm_get_u64(const zxm_link_t *l, int32_t obj, const char *key, uint64_t *v);
int zxm_get_i64(const zxm_link_t *l, int32_t obj, const char *key, int64_t *v);
int zxm_get_bool(const zxm_link_t *l, int32_t obj, const char *key, bool *v);
int32_t zxm_at(const zxm_link_t *l, int32_t arr, uint32_t i);
uint32_t zxm_len(const zxm_link_t *l, int32_t arr); /* elements of an array token */

/* JSON-escape n bytes of s into out (no quotes); the length, or -1 when it
 * does not fit in cap (out is then not terminated). */
int32_t zxm_escape(const char *s, uint32_t n, char *out, uint32_t cap);
/* The longest prefix of s[0..n) that ends on a UTF-8 character boundary. */
uint32_t zxm_utf8_cut(const char *s, uint32_t n);

/* ----- M3-M5: AI ----- */

typedef struct {
    const char *name;  /* palette label, e.g. "Summarize" */
    const char *agent; /* the swarm agent that runs it */
    uint32_t est_tokens;
} zxm_ai_action_t;

typedef struct {
    char agent[ZXM_AGENT_MAX];
    uint64_t tokens;
    bool exact; /* false: estimated (M4) */
} zxm_cost_t;

enum { ZXM_HELD_NONE, ZXM_HELD_PENDING, ZXM_HELD_ACCEPTED, ZXM_HELD_REJECTED };

typedef struct {
    uint8_t state;
    int32_t action; /* index in the app's table */
    bool cut;       /* the input was cut to fit ZXM_ASK_MAX */
    zxm_cost_t cost;
    char text[ZXM_ANSWER_MAX + 1];
    uint32_t len;
} zxm_held_t;

/* Ask the swarm (swarm.ask) with `instruction` followed by up to as much of
 * `input` as fits; the answer goes into `held` as PENDING with its cost. */
int zxm_ai_ask(zxm_link_t *l, const zxm_ai_action_t *act, int32_t idx, const char *instruction,
               const char *input, uint32_t input_len, zxm_held_t *held);
/* The owner's word on a pending answer: accept marks it read (it changes
 * nothing by itself), reject drops it. ZXM_E_STATE when none is pending. */
int zxm_held_accept(zxm_held_t *held);
int zxm_held_reject(zxm_held_t *held);

#endif
