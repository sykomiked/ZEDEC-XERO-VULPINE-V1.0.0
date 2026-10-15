/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zxv_idle.c — see zxv_idle.h (I1-I6). */
#include <string.h>

#include "zxv_idle.h"
#include "zxv_tiamat.h"

void zxv_idle_init(zxv_idle_t *q)
{
    memset(q, 0, sizeof *q);
}

void zxv_idle_clear(zxv_idle_t *q)
{
    for (uint32_t i = q->tail; i != q->head; i++) {
        zxv_idle_task_t *t = &q->t[i % ZXV_IDLE_CAP];
        if (t->f) fclose(t->f);
        t->f = NULL;
    }
    q->head = q->tail = 0;
}

uint32_t zxv_idle_queued(const zxv_idle_t *q)
{
    return q->head - q->tail;
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static zxv_idle_task_t *reserve(zxv_idle_t *q)
{
    if (q->head - q->tail >= ZXV_IDLE_CAP) return NULL; /* I1: full is backpressure */
    zxv_idle_task_t *t = &q->t[q->head % ZXV_IDLE_CAP];
    memset(t, 0, sizeof *t);
    return t;
}

int zxv_idle_add_pin(zxv_idle_t *q, const char *path, const char *want_hex, uint32_t tag)
{
    if (!q || !path || !want_hex || !path[0] || strlen(path) >= ZXV_IDLE_PATH ||
        strlen(want_hex) != 64)
        return -1;
    uint8_t want[32];
    for (int i = 0; i < 32; i++) {
        int a = hexval(want_hex[2 * i]), b = hexval(want_hex[2 * i + 1]);
        if (a < 0 || b < 0) return -1;
        want[i] = (uint8_t) (a * 16 + b);
    }
    zxv_idle_task_t *t = reserve(q);
    if (!t) return -1;
    t->kind = ZXV_IDLE_PIN;
    memcpy(t->path, path, strlen(path) + 1);
    memcpy(t->want, want, sizeof want);
    t->tag = tag;
    q->head++;
    return 0;
}

int zxv_idle_add_selftest(zxv_idle_t *q, uint32_t tag)
{
    if (!q) return -1;
    zxv_idle_task_t *t = reserve(q);
    if (!t) return -1;
    t->kind = ZXV_IDLE_SELFTEST;
    t->tag = tag;
    q->head++;
    return 0;
}

uint64_t zxv_idle_cost(const zxv_idle_t *q)
{
    return q && q->head != q->tail ? 1u : 0u;
}

int zxv_idle_selftest(char *why, size_t cap)
{
    static const int S[4] = {ZXV_TV_NEITHER, ZXV_TV_TRUE, ZXV_TV_FALSE, ZXV_TV_BOTH};
    for (int i = 0; i < 4; i++) {
        int a = S[i];
        if (zxv_tiamat_join(a, a) != a || zxv_tiamat_join(a, ZXV_TV_NEITHER) != a ||
            zxv_tiamat_join(a, ZXV_TV_BOTH) != ZXV_TV_BOTH) {
            snprintf(why, cap, "join: %s is not idempotent or BOTH/NEITHER misbehave",
                     zxv_tiamat_tv_name(a));
            return -1;
        }
        for (int j = 0; j < 4; j++) {
            int b = S[j];
            if (zxv_tiamat_join(a, b) != zxv_tiamat_join(b, a)) {
                snprintf(why, cap, "join: %s with %s depends on the order", zxv_tiamat_tv_name(a),
                         zxv_tiamat_tv_name(b));
                return -1;
            }
            for (int k = 0; k < 4; k++)
                if (zxv_tiamat_join(zxv_tiamat_join(a, b), S[k]) !=
                    zxv_tiamat_join(a, zxv_tiamat_join(b, S[k]))) {
                    snprintf(why, cap, "join: grouping changes %s, %s, %s", zxv_tiamat_tv_name(a),
                             zxv_tiamat_tv_name(b), zxv_tiamat_tv_name(S[k]));
                    return -1;
                }
        }
    }
    if (zxv_tiamat_join(ZXV_TV_TRUE, ZXV_TV_FALSE) != ZXV_TV_BOTH) {
        snprintf(why, cap, "join: TRUE with FALSE is not BOTH");
        return -1;
    }
    static const struct {
        const char *q, *a;
        int want;
    } R[] = {
        {"What is 17 times 23?", "17 x 23 = 391", ZXV_TV_TRUE},
        {"What is 17 times 23?", "It is 371.", ZXV_TV_FALSE},
        {"What is 9876 minus 1234?", "8642", ZXV_TV_TRUE},
        {"What is 84 divided by 4?", "21", ZXV_TV_TRUE},
        {"What is 7 divided by 2?", "3.5", -1},
        {"What is 999999999999 times 999999999999?", "1", -1},
        {"Who wrote Hamlet?", "Shakespeare, 1600", -1},
    };
    for (size_t i = 0; i < sizeof R / sizeof R[0]; i++)
        if (zxv_tiamat_reckon(R[i].q, R[i].a, NULL, 0) != R[i].want) {
            snprintf(why, cap, "reckoner: \"%s\" answered \"%s\" is not %s", R[i].q, R[i].a,
                     R[i].want < 0 ? "refused" : zxv_tiamat_tv_name(R[i].want));
            return -1;
        }
    return 0;
}

static int finish(zxv_idle_t *q, zxv_idle_task_t *t, zxv_idle_result_t r, const char *why,
                  zxv_idle_outcome_t *out)
{
    if (t->f) fclose(t->f);
    t->f = NULL;
    zxv_idle_outcome_t o;
    memset(&o, 0, sizeof o);
    o.kind = t->kind;
    o.tag = t->tag;
    o.result = r;
    snprintf(o.why, sizeof o.why, "%s", why);
    q->last = o;
    q->finished++;
    if (r == ZXV_IDLE_FAIL) q->failed++;
    q->tail++;
    if (out) *out = o;
    return 1;
}

int zxv_idle_slice(zxv_idle_t *q, zxv_idle_outcome_t *out)
{
    if (!q || q->head == q->tail) return -1;
    zxv_idle_task_t *t = &q->t[q->tail % ZXV_IDLE_CAP];
    q->slices++;
    if (t->kind == ZXV_IDLE_SELFTEST) {
        char why[160] = "the join's laws and the reckoner hold";
        int r = zxv_idle_selftest(why, sizeof why);
        return finish(q, t, r == 0 ? ZXV_IDLE_PASS : ZXV_IDLE_FAIL, why, out);
    }
    if (!t->f) {
        t->f = fopen(t->path, "rb");
        if (!t->f) return finish(q, t, ZXV_IDLE_FAIL, "the file cannot be opened", out);
        sha256_init(&t->h);
        t->done = 0;
    }
    static uint8_t buf[ZXV_IDLE_CHUNK];
    size_t n = fread(buf, 1, sizeof buf, t->f);
    if (n) sha256_update(&t->h, buf, n);
    t->done += n;
    if (n == sizeof buf) return 0; /* more to come: the next slice goes on */
    if (ferror(t->f)) return finish(q, t, ZXV_IDLE_FAIL, "the file cannot be read", out);
    uint8_t got[32];
    sha256_final(&t->h, got);
    char why[160];
    if (memcmp(got, t->want, 32) != 0) {
        snprintf(why, sizeof why, "SHA-256 differs from the pinned hash after %llu bytes",
                 (unsigned long long) t->done);
        return finish(q, t, ZXV_IDLE_FAIL, why, out);
    }
    snprintf(why, sizeof why, "SHA-256 matches the pin (%llu bytes)", (unsigned long long) t->done);
    return finish(q, t, ZXV_IDLE_PASS, why, out);
}

void zxv_idle_describe(const zxv_idle_t *q, char *out, size_t cap)
{
    if (!q || q->head == q->tail) {
        snprintf(out, cap, "none");
        return;
    }
    const zxv_idle_task_t *t = &q->t[q->tail % ZXV_IDLE_CAP];
    if (t->kind == ZXV_IDLE_SELFTEST)
        snprintf(out, cap, "selftest %u", (unsigned) t->tag);
    else
        snprintf(out, cap, "pin %u at %llu bytes", (unsigned) t->tag, (unsigned long long) t->done);
}
