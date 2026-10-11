/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* ti_worker.c — the Tri-Isolate worker: runs ONE Z-Wasm plugin for a host
 * process on the other end of fd 3.
 *
 *   ti-worker [--seccomp]
 *
 * Started by ti_plugin_load (fork + exec), never by hand. It:
 *   1. sets up a private libzxv context and a Z-Wasm host;
 *   2. with --seccomp, installs the seccomp filter (ti_seccomp.c);
 *   3. sends HELLO, then reads LOAD, loads the plugin, answers LOADED;
 *   4. answers each CALL with a RESULT;
 *   5. exits on QUIT or end of stream.
 *
 * The worker's own Z-Wasm queue is not the real one: after each call it
 * reads the call's proposals, sends them to the host in the RESULT, and
 * clears them from its queue. The host decides with its own capability.
 *
 * The worker's fuel budget is effectively unlimited: the host gives each
 * call its fuel (CALL.fuel), and the host's budget is what pays.
 *
 * Exit status: 0 QUIT or end of stream; 2 bad start; 3 seccomp could not
 * be installed; 4 protocol error from the host; 5 out of memory. */
#if !defined(__APPLE__)
#define _GNU_SOURCE
#endif
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/syscall.h>
#endif

#include "ti_frame.h"
#include "ti_seccomp.h"
#include "ti_wio.h"
#include "zwasm.h"

typedef struct {
    zxv_ctx *ctx;
    zxv_cap *root, *budget_cap;
    zxv_rmag *rm;
    uint32_t budget;
    zw_host *host;
    zw_plugin *plugin;
    int seccomp;
} worker;

static void teardown(worker *w) {
    zw_plugin_destroy(w->plugin);
    zw_host_destroy(w->host);
    zxv_rmag_destroy(w->rm);
    zxv_cap_destroy(w->budget_cap);
    zxv_cap_destroy(w->root);
    zxv_ctx_destroy(w->ctx);
    memset(w, 0, sizeof *w);
}

/* Under seccomp, leave with the raw exit_group system call: exit handlers
 * (and a sanitizer's leak checker, which hooks even _exit) may need system
 * calls the filter refuses. Without seccomp, exit normally. */
static void leave(worker *w, int code) {
    int sc = w->seccomp;
    teardown(w);
#if defined(__linux__) && defined(SYS_exit_group)
    if (sc) syscall(SYS_exit_group, code);
#endif
    if (sc) _exit(code);
    exit(code);
}

static int setup(worker *w) {
    /* 2^62 units: never the limit; the host's budget is. */
    if (zxv_ctx_create(&w->ctx) != ZXV_OK) return -1;
    if (zxv_cap_create(w->ctx, 1, NULL, ZXV_CAP_ALL, &w->root) != ZXV_OK) return -1;
    if (zxv_cap_attenuate(w->root, ZXV_CAP_BUDGET, &w->budget_cap) != ZXV_OK) return -1;
    if (zxv_rmag_create_cap(w->root, &w->rm) != ZXV_OK) return -1;
    if (zxv_rmag_budget(w->root, w->rm, "ti-worker", ZXV_RES_COMPUTE_CYCLES, INT64_C(1) << 62, 1, 1,
                        &w->budget) != ZXV_OK)
        return -1;
    if (zw_host_create(w->budget_cap, w->rm, w->budget, &w->host) != ZXV_OK) return -1;
    return 0;
}

static void last_error(char out[TI_MSGTXT_MAX + 1u], uint32_t *out_len) {
    char b[TI_MSGTXT_MAX + 1u];
    size_t n = 0;
    if (zw_last_error(b, sizeof b, &n) != ZXV_OK) b[0] = '\0';
    *out_len = ti_msg_clean(out, b);
}

static int32_t do_load(worker *w, const ti_load *m) {
    zw_policy *pol = NULL;
    if (zw_host_grant(w->host, m->grants) != ZXV_OK) return ZXV_E_ARG;
    if (zw_host_policy(w->host, &pol) != ZXV_OK) return ZXV_E_ARG;
    for (uint32_t i = 0; i < TI_LIMITS_COUNT; i++)
        if (zw_policy_set_limit(pol, (int32_t) (i + 1u), m->limits[i]) != ZXV_OK) return ZXV_E_ARG;
    return zw_plugin_load(w->host, m->wasm, m->wasm_len, m->name_len ? m->name : NULL, &w->plugin);
}

/* Read the call's proposals from the worker's queue into the RESULT and
 * clear them (reject with the worker's own, private cap). */
static void drain(worker *w, ti_result *res) {
    uint64_t id;
    while (zw_queue_next(w->host, &id) == ZXV_OK) {
        if (res->nprops < TI_PROPS_MAX) {
            ti_prop *p = &res->props[res->nprops];
            size_t ll = 0;
            char who[32];
            uint8_t lb[TI_LABEL_MAX + 1u]; /* R4: the buffer must be longer than the label */
            if (zw_proposal_get(w->host, id, NULL, &p->kind, &p->num, &p->den, who, lb, sizeof lb, &ll) ==
                    ZXV_OK &&
                ll <= TI_LABEL_MAX) {
                memcpy(p->label, lb, ll);
                p->label_len = (uint32_t) ll;
                res->nprops++;
            }
        }
        if (zw_proposal_reject(w->host, w->root, id) != ZXV_OK) break;
    }
}

static void do_call(worker *w, const ti_call *c, ti_result *res) {
    memset(res, 0, sizeof *res);
    if (zw_host_set_call_fuel(w->host, c->fuel) != ZXV_OK) {
        res->status = ZXV_E_ARG;
        return;
    }
    int64_t result = 0;
    int32_t has = 0;
    uint64_t used = 0;
    int32_t st = zw_plugin_call(w->plugin, c->export_name, c->args, c->nargs, &result, &has, &used);
    switch (st) {
    case ZXV_OK:
        res->has_result = has ? 1 : 0;
        res->result = has ? result : 0;
        res->fuel_used = used;
        drain(w, res);
        break;
    case ZW_E_TRAP:
    case ZW_E_FUEL:
        res->fuel_used = used;
        break;
    case ZW_E_EXPORT:
    case ZXV_E_RANGE:
    case ZXV_E_ARG:
    case ZXV_E_BUDGET:
        break;
    default:
        /* anything else (ZW_E_DEAD, ZXV_E_INTERNAL, ...) stops the plugin:
         * report it as a trap so the host kills this worker */
        st = ZW_E_TRAP;
        break;
    }
    res->status = st;
    if (st != ZXV_OK) last_error(res->msg, &res->msg_len);
}

int main(int argc, char **argv) {
    worker w;
    ti_buf out = {NULL, 0, 0};
    memset(&w, 0, sizeof w);
    w.seccomp = argc > 1 && strcmp(argv[1], "--seccomp") == 0;
    if (fcntl(TI_WORKER_FD, F_GETFD) < 0) return 2;
    if (setup(&w) != 0) leave(&w, 2);
    if (w.seccomp && ti_seccomp_install() != ZXV_OK) {
        w.seccomp = 0;
        leave(&w, 3);
    }

    ti_hello h = {(uint16_t) TI_PROTO_VERSION, (uint16_t) (w.seccomp ? TI_HELLO_SECCOMP : 0u)};
    if (ti_enc_hello(&out, &h) != ZXV_OK || ti_send_buf(TI_WORKER_FD, &out) != 0) leave(&w, 2);

    for (;;) {
        uint32_t type = 0;
        uint8_t *body = NULL;
        size_t len = 0;
        int rc = ti_read_frame(TI_WORKER_FD, TI_REQ_MAX, &type, &body, &len);
        int code = -1;
        if (rc == 1) code = 0;      /* the host closed the socket */
        else if (rc != 0) code = 4; /* bad frame */
        else if (type == TI_MSG_QUIT) code = ti_dec_quit(body, len) == ZXV_OK ? 0 : 4;
        else if (type == TI_MSG_LOAD && !w.plugin) {
            ti_load m;
            ti_loaded r;
            memset(&r, 0, sizeof r);
            if (ti_dec_load(body, len, &m) != ZXV_OK) code = 4;
            else {
                r.status = do_load(&w, &m);
                if (!ti_loaded_status_ok(r.status)) r.status = ZW_E_LINK;
                if (r.status != ZXV_OK) last_error(r.msg, &r.msg_len);
                if (ti_enc_loaded(&out, &r) != ZXV_OK || ti_send_buf(TI_WORKER_FD, &out) != 0) code = 4;
            }
        } else if (type == TI_MSG_CALL && w.plugin) {
            ti_call c;
            ti_result *res = (ti_result *) malloc(sizeof *res);
            if (!res) code = 5;
            else if (ti_dec_call(body, len, &c) != ZXV_OK) code = 4;
            else {
                do_call(&w, &c, res);
                if (ti_enc_result(&out, res) != ZXV_OK || ti_send_buf(TI_WORKER_FD, &out) != 0) code = 4;
            }
            free(res);
        } else {
            code = 4; /* unexpected frame */
        }
        free(body);
        if (code >= 0) {
            ti_buf_free(&out);
            leave(&w, code);
        }
    }
}
