/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxm_open.c — opening an app from its recipe (zxm_open.h O1-O4). */
#include <string.h>

#include "zxm_open.h"

#define PARAMS_MAX (ZXM_REQ_MAX - 256u)

static void tell(zxm_open_t *o, uint32_t phase)
{
    if (o->load) o->load(o->ctx, phase, o->done, o->total);
}

static void why(zxm_open_t *o, const char *a, const char *b)
{
    uint32_t n = 0;
    for (const char *s = a; s && *s && n + 1 < ZXM_ERR_MAX; s++) o->why[n++] = *s;
    for (const char *s = b; s && *s && n + 1 < ZXM_ERR_MAX; s++) o->why[n++] = *s;
    o->why[n] = 0;
}

/* O3: stop, tell the screen, and drop the job when zxvd may still hold it. */
static uint32_t stop(zxm_open_t *o, bool drop)
{
    if (drop && o->job) {
        zxm_begin(o->link, "fab.receive.cancel");
        zxm_p_u64(o->link, "job", o->job);
        (void) zxm_call(o->link); /* the job may already be gone: nothing to undo */
    }
    o->state = ZXM_OPEN_STOPPED;
    tell(o, ZXM_OPEN_FAILED);
    return o->state;
}

/* The call's failure in words: zxvd's own message, or what went wrong here. */
static void failed_call(zxm_open_t *o, const char *what, int rc)
{
    if (rc == ZXM_E_REMOTE)
        why(o, what, o->link->err);
    else
        why(o, what, rc == ZXM_E_TRANSPORT ? "zxvd could not be reached" : "a broken answer");
}

static bool hex64(const char *s)
{
    uint32_t n = 0;
    for (; s[n]; n++)
        if (!((s[n] >= '0' && s[n] <= '9') || (s[n] >= 'a' && s[n] <= 'f') ||
              (s[n] >= 'A' && s[n] <= 'F')))
            return false;
    return n == 64;
}

static bool put(char *p, uint32_t *n, const char *s, uint32_t len)
{
    if (*n + len >= PARAMS_MAX) return false;
    memcpy(p + *n, s, len);
    *n += len;
    return true;
}

static bool put_str(char *p, uint32_t *n, const char *key, const char *val)
{
    if (!put(p, n, "\"", 1) || !put(p, n, key, (uint32_t) strlen(key)) || !put(p, n, "\":\"", 3))
        return false;
    int32_t k = zxm_escape(val, (uint32_t) strlen(val), p + *n, PARAMS_MAX - *n);
    if (k < 0 || *n + (uint32_t) k + 1 >= PARAMS_MAX) return false;
    *n += (uint32_t) k;
    return put(p, n, "\"", 1);
}

/* The answer's counts, refused when they are not counts this open can take. */
static bool counts(zxm_open_t *o, uint64_t *done, uint64_t *total)
{
    zxm_link_t *l = o->link;
    return zxm_get_u64(l, l->result, "blocks_done", done) == ZXM_OK &&
           zxm_get_u64(l, l->result, "blocks_total", total) == ZXM_OK && *total <= UINT32_MAX &&
           *done <= *total;
}

int zxm_open_start(zxm_open_t *o, zxm_link_t *link, const char *recipe, const char *out,
                   const char *const *trust, uint32_t ntrust, bool airgap, uint32_t step,
                   zxm_open_load_fn load, void *ctx)
{
    if (!o || !link || !recipe || !recipe[0] || !out || !out[0] || ntrust > ZXM_OPEN_TRUST ||
        (ntrust && !trust) || step > ZXM_OPEN_STEP_MAX)
        return ZXM_E_ARG;
    for (uint32_t i = 0; i < ntrust; i++)
        if (!trust[i] || !hex64(trust[i])) return ZXM_E_ARG;
    static char p[PARAMS_MAX];
    uint32_t n = 0;
    bool ok = put(p, &n, "{", 1) && put_str(p, &n, "recipe", recipe) && put(p, &n, ",", 1) &&
              put_str(p, &n, "out", out);
    if (ntrust) {
        ok = ok && put(p, &n, ",\"trust\":[", 10);
        for (uint32_t i = 0; ok && i < ntrust; i++)
            ok = put(p, &n, i ? ",{\"id\":\"" : "{\"id\":\"", i ? 8 : 7) &&
                 put(p, &n, trust[i], 64) && put(p, &n, "\"}", 2);
        ok = ok && put(p, &n, "]", 1);
    }
    ok = ok && (airgap ? put(p, &n, ",\"airgap\":true}", 15) : put(p, &n, "}", 1));
    if (!ok) return ZXM_E_ARG;

    memset(o, 0, sizeof *o);
    o->link = link;
    o->load = load;
    o->ctx = ctx;
    o->step = step ? step : ZXM_OPEN_STEP;
    zxm_begin(link, "fab.receive.start");
    zxm_p_raw(link, p, n);
    int rc = zxm_call(link);
    uint64_t job = 0, done = 0, total = 0;
    if (rc == ZXM_OK && (zxm_get_u64(link, link->result, "job", &job) != ZXM_OK || !job ||
                         zxm_get_str(link, link->result, "cid", o->cid, sizeof o->cid) != ZXM_OK ||
                         !o->cid[0] || zxm_get_u64(link, link->result, "len", &o->len) != ZXM_OK ||
                         !counts(o, &done, &total) || done))
        rc = ZXM_E_PROTOCOL;
    o->job = job;
    if (rc != ZXM_OK) {
        failed_call(o, "the app could not be opened: ", rc);
        stop(o, rc == ZXM_E_PROTOCOL);
        return rc;
    }
    o->total = (uint32_t) total;
    o->state = ZXM_OPEN_RUNNING;
    tell(o, ZXM_OPEN_FETCH);
    return ZXM_OK;
}

uint32_t zxm_open_pump(zxm_open_t *o)
{
    if (!o || o->state != ZXM_OPEN_RUNNING) return o ? o->state : ZXM_OPEN_IDLE;
    zxm_link_t *l = o->link;
    zxm_begin(l, "fab.receive.step");
    zxm_p_u64(l, "job", o->job);
    zxm_p_u64(l, "blocks", o->step);
    int rc = zxm_call(l);
    if (rc != ZXM_OK) {
        failed_call(o, "loading stopped: ", rc);
        /* zxvd ends a job on any failed step (B13); a lost answer may not have */
        return stop(o, rc != ZXM_E_REMOTE);
    }
    char phase[8], cid[ZXM_OPEN_CID_MAX];
    uint64_t done = 0, total = 0, len = 0, job = 0;
    if (zxm_get_str(l, l->result, "phase", phase, sizeof phase) != ZXM_OK ||
        zxm_get_u64(l, l->result, "job", &job) != ZXM_OK || job != o->job ||
        !counts(o, &done, &total)) {
        why(o, "loading stopped: ", "a broken answer");
        return stop(o, true);
    }
    if (total != o->total || done < o->done) {
        why(o, "loading stopped: ", "the block counts went back or changed");
        return stop(o, true);
    }
    o->done = (uint32_t) done;
    if (!strcmp(phase, "fetch")) {
        tell(o, ZXM_OPEN_FETCH);
        return o->state;
    }
    if (strcmp(phase, "done")) {
        why(o, "loading stopped: ", "an unknown phase");
        return stop(o, true);
    }
    o->job = 0; /* done ends the job in zxvd */
    if (done != total || zxm_get_str(l, l->result, "cid", cid, sizeof cid) != ZXM_OK ||
        strcmp(cid, o->cid) || zxm_get_u64(l, l->result, "len", &len) != ZXM_OK || len != o->len) {
        why(o, "loading stopped: ", "what was built is not the app the recipe named");
        return stop(o, false);
    }
    o->state = ZXM_OPEN_FINISHED;
    tell(o, ZXM_OPEN_DONE);
    return o->state;
}

int zxm_open_cancel(zxm_open_t *o)
{
    if (!o || o->state != ZXM_OPEN_RUNNING) return ZXM_E_STATE;
    why(o, "cancelled", 0);
    stop(o, true);
    return ZXM_OK;
}
