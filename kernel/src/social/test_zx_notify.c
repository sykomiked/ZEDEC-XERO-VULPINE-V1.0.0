/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zx_notify.c — the notification bus, the AI-task hook (with chiglet
 * and swarm examples) and the hosted bridge's escaping.
 *
 * Build and run from kernel/ (host test, uses libc):
 *
 *   PATH=/tmp/claude-0/gcc11:$PATH gcc -std=c11 -Wall -Wextra -Werror -O2 -DTEST_HOST \
 *     -Iinclude -Isrc/modbind -Isrc/e8 -Isrc/event_space -Isrc/social -Isrc/chiglet \
 *     -Isrc/surplus -Isrc/swarm \
 *     src/social/test_zx_notify.c src/social/zx_notify.c arch/hosted/zx_notify_host.c \
 *     src/chiglet/chiglet.c src/surplus/surplus.c src/swarm/swarm_hk.c \
 *     -o /tmp/test_zx_notify -lm && /tmp/test_zx_notify
 *
 * Sanitizers: add -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer.
 * -DTEST_HOST is for chiglet/surplus (double precision on the host); the bus
 * itself is integer-only either way.
 */
#include <stdio.h>
#include <string.h>

#include "zx_notify.h"
#include "chiglet.h"
#include "swarm_hk.h"

static int failures, passes;
#define CHECK(c, m)                                                                                \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("[FAIL] %s (line %d)\n", m, __LINE__);                                          \
            failures++;                                                                            \
        } else {                                                                                   \
            printf("[PASS] %s\n", m);                                                              \
            passes++;                                                                              \
        }                                                                                          \
    } while (0)

typedef struct {
    uint32_t calls, coalesced;
    uint32_t last_seq;
    uint8_t last_kind;
    char last_title[ZXN_TITLE_MAX];
    char last_body[ZXN_BODY_MAX];
} sub_log_t;

static void sub(void *ctx, const zxn_note_t *n, bool coalesced)
{
    sub_log_t *l = ctx;
    l->calls++;
    l->coalesced += coalesced;
    l->last_seq = n->seq;
    l->last_kind = n->kind;
    snprintf(l->last_title, sizeof l->last_title, "%s", n->title);
    snprintf(l->last_body, sizeof l->last_body, "%s", n->body);
}

static uint32_t post(zxn_bus_t *b, zxn_kind_t k, zxn_pri_t p, uint64_t key, const char *title)
{
    zxn_spec_t s = {k, p, "test", title, "body", NULL, key, 0};
    return zxn_post(b, &s);
}

static void test_bus(void)
{
    printf("\n=== bus: coalescing, read state, filters ===\n");
    static zxn_note_t slots[16];
    zxn_bus_t b;
    CHECK(zxn_init(&b, slots, 16), "bus over caller slots");
    sub_log_t all = {0}, urgent_calls = {0};
    zxn_subscribe(&b, sub, &all, ZXN_ALL_KINDS, ZXN_PRI_LOW);
    zxn_subscribe(&b, sub, &urgent_calls, ZXN_KIND_BIT(ZXN_CALL_INCOMING), ZXN_PRI_URGENT);

    uint32_t first = 0;
    for (int i = 0; i < 10; i++) {
        char t[32];
        snprintf(t, sizeof t, "%d new messages from Ana", i + 1);
        uint32_t s = zxn_message_received(&b, "social", t, 42);
        if (!first) first = s;
        CHECK(i == 0 || s == first, i == 0 ? "first message note" : "repeat coalesces into it");
    }
    const zxn_note_t *n = zxn_get(&b, first);
    printf("       10 posts with one key -> 1 note, count %u, title \"%s\", body \"%s\"\n",
           n->count, n->title, n->body);
    CHECK(n->count == 10 && !strcmp(n->body, "10 new messages from Ana"),
          "coalesced: count 10, latest text kept");
    CHECK(all.calls == 10 && all.coalesced == 9, "subscriber saw 1 new + 9 coalesced deliveries");
    CHECK(zxn_unread(&b, ZXN_ALL_KINDS) == 1, "one unread note");
    zxn_mark_read(&b, first);
    uint32_t again = zxn_message_received(&b, "social", "Ana again", 42);
    CHECK(again != first && zxn_get(&b, again)->count == 1, "after reading, the next one is new");
    uint32_t a = post(&b, ZXN_ECONOMY, ZXN_PRI_NORMAL, 0, "trade filled");
    uint32_t c = post(&b, ZXN_ECONOMY, ZXN_PRI_NORMAL, 0, "trade filled");
    CHECK(a != c, "key 0 never coalesces");
    post(&b, ZXN_ECONOMY, ZXN_PRI_NORMAL, 7, "from source A");
    zxn_spec_t s2 = {ZXN_ECONOMY, ZXN_PRI_NORMAL, "other", "from source B", NULL, NULL, 7, 0};
    CHECK(zxn_post(&b, &s2) != 0 && zxn_unread(&b, ZXN_KIND_BIT(ZXN_ECONOMY)) == 4,
          "same key from another source does not coalesce");
    zxn_call(&b, "call", false, "Bo", 9);
    zxn_call(&b, "call", true, "Bo", 9);
    CHECK(urgent_calls.calls == 1 && urgent_calls.last_kind == ZXN_CALL_INCOMING,
          "kind + priority filters: the urgent-calls subscriber saw only the ring");
    const zxn_note_t *list[4];
    uint32_t nl = zxn_list(&b, list, 4, true);
    CHECK(nl == 4 && list[0]->kind == ZXN_CALL_MISSED && list[0]->last_ts > list[1]->last_ts,
          "list is newest first");
    CHECK(zxn_mark_all_read(&b, ZXN_KIND_BIT(ZXN_ECONOMY)) == 4 &&
              zxn_unread(&b, ZXN_ALL_KINDS) == 3,
          "mark all read by kind");

    /* UTF-8-safe truncation */
    char longt[200];
    memset(longt, 'x', sizeof longt);
    memcpy(longt + 61, "\xe2\x82\xac", 3); /* a euro sign straddling the 63-byte limit */
    longt[199] = 0;
    uint32_t q = post(&b, ZXN_UPDATE, ZXN_PRI_LOW, 0, longt);
    const zxn_note_t *qn = zxn_get(&b, q);
    CHECK(strlen(qn->title) == 61, "a title is cut before a split UTF-8 sequence");
}

static void test_dnd_overflow(void)
{
    printf("\n=== do not disturb, mute, overflow ===\n");
    static zxn_note_t slots[8];
    zxn_bus_t b;
    zxn_init(&b, slots, 8);
    sub_log_t l = {0};
    zxn_subscribe(&b, sub, &l, ZXN_ALL_KINDS, ZXN_PRI_LOW);
    zxn_set_dnd(&b, true, ZXN_PRI_URGENT, ZXN_KIND_BIT(ZXN_AI_TASK_DONE));
    post(&b, ZXN_MESSAGE, ZXN_PRI_NORMAL, 0, "held 1");
    post(&b, ZXN_ECONOMY, ZXN_PRI_HIGH, 0, "held 2");
    CHECK(l.calls == 0 && zxn_unread(&b, ZXN_ALL_KINDS) == 2, "DND: stored, not delivered");
    zxn_call(&b, "call", false, "Cy", 3);
    CHECK(l.calls == 1, "DND: an urgent note breaks through");
    zxn_task_finished(&b, "chiglet", 77, true, "Summary ready", "3 pages", "chat:77");
    CHECK(l.calls == 2 && l.last_kind == ZXN_AI_TASK_DONE, "DND: an allowed kind is delivered");
    uint32_t rel = zxn_set_dnd(&b, false, ZXN_PRI_URGENT, 0);
    CHECK(rel == 2 && l.calls == 4 && !strcmp(l.last_title, "held 2"),
          "DND off: held notes delivered, oldest first");
    zxn_mute(&b, ZXN_KIND_BIT(ZXN_UPDATE));
    post(&b, ZXN_UPDATE, ZXN_PRI_HIGH, 0, "update");
    CHECK(l.calls == 4 && zxn_unread(&b, ZXN_KIND_BIT(ZXN_UPDATE)) == 1, "muted: stored, silent");
    zxn_mute(&b, 0);

    /* overflow: 8 slots */
    zxn_init(&b, slots, 8);
    for (int i = 0; i < 8; i++) post(&b, ZXN_MESSAGE, ZXN_PRI_NORMAL, 0, "normal");
    CHECK(post(&b, ZXN_ECONOMY, ZXN_PRI_LOW, 0, "low") == 0 && b.dropped == 1,
          "full of unread NORMAL: a LOW newcomer is dropped");
    uint32_t oldest = slots[0].seq;
    uint32_t h = post(&b, ZXN_PEER_REQUEST, ZXN_PRI_HIGH, 0, "high");
    CHECK(h && !zxn_get(&b, oldest) && b.evicted == 1, "a HIGH newcomer evicts the oldest NORMAL");
    zxn_mark_read(&b, slots[5].seq);
    uint32_t read_seq = slots[5].seq;
    CHECK(post(&b, ZXN_ECONOMY, ZXN_PRI_LOW, 0, "low2") && !zxn_get(&b, read_seq),
          "a read note is evicted first, even by LOW");
    zxn_init(&b, slots, 8);
    for (int i = 0; i < 8; i++) zxn_call(&b, "call", false, "ring", (uint64_t) i + 1);
    uint32_t dropped = 0;
    for (int i = 0; i < 1000; i++) dropped += post(&b, ZXN_ECONOMY, ZXN_PRI_HIGH, 0, "spam") == 0;
    CHECK(dropped == 1000 && zxn_unread(&b, ZXN_KIND_BIT(ZXN_CALL_INCOMING)) == 8,
          "a flood of 1000 lower-priority notes cannot push out urgent unread ones");
    printf("       overflow counters: dropped %u, evicted %u\n", b.dropped, b.evicted);
}

/* ---------------- the AI-task hook, used the way a caller would ---------------- */

static void setvec(surplus_real_t *v, double a, double b2, double c, double d)
{
    v[0] = SR_FROM_FLOAT(a), v[1] = SR_FROM_FLOAT(b2), v[2] = SR_FROM_FLOAT(c),
    v[3] = SR_FROM_FLOAT(d);
    for (uint32_t i = 4; i < CHG_DIM; i++) v[i] = SR_ZERO;
}

/* Example: wrap chg_infer so the user hears when the companion is done. */
static chg_status_t infer_and_notify(zxn_bus_t *bus, chiglet_t *c, uint64_t task,
                                     const surplus_real_t ev[][CHG_DIM], uint32_t k,
                                     chg_result_t *r)
{
    chg_status_t st = chg_infer(c, ev, k, r);
    char summary[96], action[32];
    snprintf(action, sizeof action, "chat:%llu", (unsigned long long) task);
    if (st != CHG_OK || r->state == CHG_UNAVAILABLE) {
        snprintf(summary, sizeof summary, "chiglet could not run (status %d, %s)", (int) st,
                 st == CHG_OK ? chg_reason_name(r->reason) : "error");
        zxn_task_finished(bus, "chiglet", task, false, NULL, summary, action);
    } else {
        snprintf(summary, sizeof summary, "%s: %s", chg_state_name(r->state),
                 r->state == CHG_DECIDED ? c->model.label_name[r->label]
                                         : chg_reason_name(r->reason));
        zxn_task_finished(bus, "chiglet", task, true, "Your request is done", summary, action);
    }
    return st;
}

/* Example: a swarm request that fails to parse is a failed task. */
static void swarm_request_and_notify(zxn_bus_t *bus, uint64_t task, const char *text)
{
    swarm_hk_msg_t msg;
    swarm_hk_status_t st = swarm_hk_request(&msg, 1, 2, 1, SWARM_HK_TRUE, text);
    char body[96];
    snprintf(body, sizeof body, st == SWARM_HK_OK ? "routed %u operator(s)" : "refused (%d)",
             st == SWARM_HK_OK ? (unsigned) msg.ast.num_ops : (unsigned) -st);
    zxn_task_finished(bus, "swarm", task, st == SWARM_HK_OK, NULL, body, NULL);
}

static void test_task_hook(void)
{
    printf("\n=== AI task completion hook ===\n");
    static zxn_note_t slots[16];
    zxn_bus_t b;
    zxn_init(&b, slots, 16);
    sub_log_t l = {0};
    zxn_subscribe(&b, sub, &l, ZXN_KIND_BIT(ZXN_AI_TASK_DONE) | ZXN_KIND_BIT(ZXN_AI_TASK_FAILED),
                  ZXN_PRI_LOW);
    chiglet_t c;
    chg_init(&c, CHG_CAP_INFER);
    surplus_real_t ev[CHG_MAX_EXPERTS][CHG_DIM];
    setvec(ev[0], 1, 0, 0, 0), setvec(ev[1], 0.9, 0.2, 0, 0), setvec(ev[2], 0, 0, 1, 0),
        setvec(ev[3], 0, 0, 0, 1);
    chg_result_t r;
    infer_and_notify(&b, &c, 101, ev, 4, &r); /* no model yet: fails */
    CHECK(l.calls == 1 && l.last_kind == ZXN_AI_TASK_FAILED &&
              zxn_get(&b, l.last_seq)->pri == ZXN_PRI_HIGH,
          "chg_infer without a model -> AI_TASK_FAILED at HIGH");
    chg_model_t m;
    memset(&m, 0, sizeof m);
    m.K = 4, m.D = 4, m.L = 2, m.epoch = 1, m.loaded = true;
    setvec(m.proto[0], 1, 0, 0, 0), setvec(m.proto[1], 0, 1, 0, 0);
    m.R_min = SR_FROM_FLOAT(1.8), m.margin_min = SR_FROM_FLOAT(0.01);
    snprintf(m.label_name[0], CHG_NAME_LEN, "summarise");
    snprintf(m.label_name[1], CHG_NAME_LEN, "translate");
    chg_load_model(&c, &m);
    infer_and_notify(&b, &c, 102, ev, 4, &r);
    printf("       chiglet task 102 -> \"%s\" / \"%s\"\n", l.last_title, l.last_body);
    CHECK(l.last_kind == ZXN_AI_TASK_DONE && strstr(l.last_body, "summarise"),
          "chg_infer DECIDED -> AI_TASK_DONE naming the result");
    const zxn_note_t *n = zxn_get(&b, l.last_seq);
    CHECK(!strcmp(n->action, "chat:102") && !strcmp(n->source, "chiglet"),
          "the note carries the action id and source module");
    infer_and_notify(&b, &c, 102, ev, 4, &r); /* the same task reports again */
    CHECK(zxn_get(&b, l.last_seq)->count == 2, "the same task id coalesces");
    swarm_request_and_notify(&b, 201, "summarise(report, depth: 2) -> translate(fr)");
    CHECK(l.last_kind == ZXN_AI_TASK_DONE, "swarm request routed -> AI_TASK_DONE");
    swarm_request_and_notify(&b, 202, "a + b + c + d + e + f + g");
    printf("       swarm task 202 -> \"%s\" / \"%s\"\n", l.last_title, l.last_body);
    CHECK(l.last_kind == ZXN_AI_TASK_FAILED, "stacked swarm request refused -> AI_TASK_FAILED");
}

/* ---------------- hosted bridge escaping ---------------- */

/* Does s, read as the inside of an AppleScript "..." literal, stay inside it?
 * Every '"' must be escaped, every '\' must start a pair, and there must be
 * no control bytes. */
static bool literal_safe(const char *s)
{
    for (const char *p = s; *p; p++) {
        if ((unsigned char) *p < 0x20 || *p == 0x7f) return false;
        if (*p == '"') return false;
        if (*p == '\\') {
            if (p[1] != '\\' && p[1] != '"') return false;
            p++;
        }
    }
    return true;
}

static void test_bridge(void)
{
    printf("\n=== hosted bridge escaping ===\n");
    char out[512];
    const char *evil = "Done\" & (do shell script \"rm -rf ~\") & \"\n\r\tback\\slash\\\"";
    zxn_host_escape_applescript(evil, out, sizeof out);
    printf("       in : %s\n       out: %s\n", "Done\" & (do shell script \"rm -rf ~\") ...", out);
    CHECK(literal_safe(out), "quotes, backslashes and newlines cannot close the literal");
    CHECK(strstr(out, "\\\"") && strstr(out, "\\\\") && !strchr(out, '\n') && !strchr(out, '\r'),
          "\" -> \\\", \\ -> \\\\, CR/LF -> space");
    zxn_host_escape_applescript("ok \xff\xfe bad utf8 \xe2\x82\xac", out, sizeof out);
    CHECK(!strcmp(out, "ok ?? bad utf8 \xe2\x82\xac"), "invalid UTF-8 -> '?', valid UTF-8 kept");
    zxn_host_escape_applescript("a\"b", out, 3);
    CHECK(!strcmp(out, "a"), "truncation never leaves half an escape");
    zxn_host_escape_applescript("\\\\\\", out, 6);
    CHECK(literal_safe(out) && !strcmp(out, "\\\\\\\\"), "a run of backslashes truncates in pairs");

    /* full argv, built but not run */
    zxn_note_t n;
    memset(&n, 0, sizeof n);
    n.kind = ZXN_AI_TASK_DONE, n.pri = ZXN_PRI_URGENT, n.count = 1;
    snprintf(n.title, sizeof n.title, "%s", "-x \"quoted\" title");
    snprintf(n.body, sizeof n.body, "%s", "line1\nline2 $(reboot) `id` <b>&amp;</b> ';");
    snprintf(n.source, sizeof n.source, "%s", "chig\"let");
    zxn_host_set_mode(ZXN_HOST_DRY_RUN_MAC);
    zxn_host_deliver(NULL, &n, false);
    const char *a2 = zxn_host_last_argv(2);
    printf("       mac argv: %s %s '%s'\n", zxn_host_last_argv(0), zxn_host_last_argv(1), a2);
    bool mac_ok = !strcmp(zxn_host_last_argv(0), "osascript") &&
                  !strcmp(zxn_host_last_argv(1), "-e") && !zxn_host_last_argv(3) && a2;
    /* the script has exactly three literals: split on bare quotes */
    int bare = 0;
    for (const char *p = a2; p && *p; p++) {
        if (*p == '\\') {
            p++;
            continue;
        }
        bare += *p == '"';
    }
    CHECK(mac_ok && bare == 6 && !strchr(a2, '\n'),
          "osascript argv: one -e script, three intact string literals, no newline");
    zxn_host_set_mode(ZXN_HOST_DRY_RUN_LINUX);
    zxn_host_deliver(NULL, &n, false);
    printf("       linux argv:");
    for (uint32_t i = 0; zxn_host_last_argv(i); i++) printf(" [%s]", zxn_host_last_argv(i));
    printf("\n");
    bool lin_ok = !strcmp(zxn_host_last_argv(0), "notify-send") &&
                  !strcmp(zxn_host_last_argv(2), "critical") &&
                  !strcmp(zxn_host_last_argv(5), "--") &&
                  !strcmp(zxn_host_last_argv(6), "-x \"quoted\" title") &&
                  strstr(zxn_host_last_argv(7), "&lt;b&gt;&amp;amp;") &&
                  !strchr(zxn_host_last_argv(7), '\n') && !zxn_host_last_argv(8);
    CHECK(lin_ok, "notify-send argv: '--' before text, markup escaped, no newline, no shell");
    zxn_host_set_mode(ZXN_HOST_PRINT);
    zxn_host_deliver(NULL, &n, false); /* one sanitised line on stdout */
}

int main(void)
{
    printf("=== ZXV notification bus ===\n");
    test_bus();
    test_dnd_overflow();
    test_task_hook();
    test_bridge();
    printf("\n%d passed, %d failed\n", passes, failures);
    return failures ? 1 : 0;
}
