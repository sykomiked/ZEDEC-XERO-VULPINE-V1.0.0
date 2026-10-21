/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Halloo's model against zxvd's real dispatcher (LINK). zxvd_people.c,
 * which serves call.* in the daemon, is not linked into this test, so the
 * call group is served here by a small host of five lines that answers in
 * call.*'s own shapes (api/zxvd.openrpc.json), set as zxvd's people ops
 * as test_zxvd_rpc.c does; the requests still go through
 * zxvd_rpc_handle_line. */
#include "halloo_model.h"

typedef struct {
    const char *state, *reason;
    long long peer;
    bool outgoing, video;
    char id[17];
} hl_line_t;

static hl_line_t HL[HALLOO_LINES];
static const char *HL_OUT[3];
static uint32_t HL_NOUT, HL_SIGNALLED, HL_STARTS;

static void hl_reset(void)
{
    for (uint32_t i = 0; i < HALLOO_LINES; i++) {
        memset(&HL[i], 0, sizeof HL[i]);
        HL[i].state = "idle";
        HL[i].reason = "none";
    }
    HL_NOUT = 0;
}

static void hl_ring(uint32_t line, long long peer, bool video, const char *id)
{
    HL[line].state = "incoming";
    HL[line].reason = "none";
    HL[line].peer = peer;
    HL[line].outgoing = false;
    HL[line].video = video;
    snprintf(HL[line].id, sizeof HL[line].id, "%s", id);
}

static void hl_put_line(zxvd_buf_t *out, uint32_t i)
{
    const hl_line_t *l = &HL[i];
    zxvd_buf_printf(out,
                    "{\"line\":%u,\"loopback\":%s,\"state\":\"%s\",\"peer\":%lld,\"outgoing\":%s,"
                    "\"media\":\"%s\",\"end_reason\":\"%s\",\"missed\":0",
                    i, i == 4 ? "true" : "false", l->state, l->peer, l->outgoing ? "true" : "false",
                    l->video ? "audio+video" : "audio", l->reason);
    if (l->id[0]) zxvd_buf_printf(out, ",\"call_id\":\"%s\"", l->id);
    zxvd_buf_raw(out, "}", 1);
}

/* the number after "key": in the params, or -1 */
static long long hl_num(const char *ps, const char *key)
{
    char k[32];
    snprintf(k, sizeof k, "\"%s\":", key);
    const char *p = strstr(ps, k);
    if (!p) return -1;
    p += strlen(k);
    if (*p < '0' || *p > '9') return -1;
    long long v = 0;
    while (*p >= '0' && *p <= '9') v = v * 10 + (*p++ - '0');
    return v;
}

static int hl_people(const char *method, const char *params, size_t len, zxvd_buf_t *out, char *msg,
                     size_t msg_cap)
{
    char ps[4096];
    if (len >= sizeof ps) len = sizeof ps - 1u;
    memcpy(ps, params, len);
    ps[len] = 0;
    if (!strcmp(method, "call.status")) {
        zxvd_buf_printf(out,
                        "{\"self_id\":1000,\"loopback_id\":1001,\"media\":\"signalling only\","
                        "\"outbox\":%u,\"dropped\":\"0\",\"lines\":[",
                        HL_NOUT);
        for (uint32_t i = 0; i < HALLOO_LINES; i++) {
            if (i) zxvd_buf_raw(out, ",", 1);
            hl_put_line(out, i);
        }
        zxvd_buf_raw(out, "]}", 2);
        return 0;
    }
    if (!strcmp(method, "call.start")) {
        long long peer = strstr(ps, "\"peer\":\"loopback\"") ? 1001 : hl_num(ps, "peer");
        if (peer < 1) {
            snprintf(msg, msg_cap, "peer: a member id or \"loopback\"");
            return -32602;
        }
        if (strcmp(HL[0].state, "idle") && strcmp(HL[0].state, "ended")) {
            snprintf(msg, msg_cap, "every line is busy");
            return ZXVD_E_PEOPLE;
        }
        HL_STARTS++;
        HL[0].state = "ringing_out";
        HL[0].reason = "none";
        HL[0].peer = peer;
        HL[0].outgoing = true;
        HL[0].video = strstr(ps, "\"media\":\"video\"") != NULL;
        snprintf(HL[0].id, sizeof HL[0].id, "00000000000000%02u", HL_STARTS % 100u);
        hl_put_line(out, 0);
        return 0;
    }
    if (!strcmp(method, "call.accept") || !strcmp(method, "call.hangup")) {
        long long line = hl_num(ps, "line");
        if (line < 0 || line > 4) {
            snprintf(msg, msg_cap, "line: 0-4");
            return -32602;
        }
        hl_line_t *l = &HL[line];
        bool accept = !strcmp(method, "call.accept");
        if (accept && strcmp(l->state, "incoming")) {
            snprintf(msg, msg_cap, "nothing is ringing on line %lld", line);
            return ZXVD_E_PEOPLE;
        }
        if (!accept && (!strcmp(l->state, "idle") || !strcmp(l->state, "ended"))) {
            snprintf(msg, msg_cap, "no call on line %lld", line);
            return ZXVD_E_PEOPLE;
        }
        if (accept) {
            l->state = "connected";
        } else {
            l->reason = !strcmp(l->state, "incoming") ? "rejected" : "local_hangup";
            l->state = "ended";
        }
        hl_put_line(out, (uint32_t) line);
        return 0;
    }
    if (!strcmp(method, "call.signal")) {
        uint32_t n = 0;
        for (const char *p = strchr(ps, '['); p && *p && *p != ']'; p++) n += *p == '"';
        HL_SIGNALLED += n / 2u;
        zxvd_buf_printf(out, "{\"delivered\":%u,\"rejected\":0,\"out\":[", n / 2u);
        for (uint32_t i = 0; i < HL_NOUT; i++) {
            if (i) zxvd_buf_raw(out, ",", 1);
            zxvd_buf_json_str(out, HL_OUT[i]);
        }
        zxvd_buf_raw(out, "]}", 2);
        HL_NOUT = 0;
        return 0;
    }
    snprintf(msg, msg_cap, "not here");
    return -32601;
}

static halloo_t HA;

APPS_TEST(halloo)
{
    static const zxvd_people_ops_t people = {hl_people};
    char text[16];

    /* the rules as the model puts them together (HR1-HR4, HM2) */
    halloo_init(&HA, &LINK);
    CHECK(HA.ring_ms == HALLOO_RING_MAX && !HA.quiet_on && HA.nhistory == 0 && HA.next_seq == 1,
          "init: ring 30 s, no quiet hours, no history");
    CHECK(strstr(HALLOO_NO_EMERGENCY, "cannot reach emergency services") != NULL,
          "the screen says Halloo cannot reach emergency services");
    CHECK(halloo_add_contact(&HA, 7, "Ada") == ZXM_OK &&
              halloo_add_contact(&HA, 8, "Bo") == ZXM_OK &&
              halloo_add_contact(&HA, 7, "Again") == ZXM_E_STATE &&
              halloo_add_contact(&HA, 0, "Nobody") == ZXM_E_ARG &&
              halloo_add_contact(&HA, 4294967296LL, "Past") == ZXM_E_ARG &&
              halloo_add_contact(&HA, 9, "") == ZXM_E_ARG &&
              halloo_add_contact(&HA, 9, "a\nb") == ZXM_E_ARG && HA.ncontacts == 2,
          "contacts: two kept, the rest refused");
    CHECK(halloo_block(&HA, 8, true) == ZXM_OK && halloo_block(&HA, 99, true) == ZXM_E_STATE &&
              halloo_find_contact(&HA, 8)->blocked,
          "Bo blocked; an unknown member cannot be");
    CHECK(halloo_may_ring(&HA, 7, 600) && !halloo_may_ring(&HA, 8, 600) &&
              !halloo_may_ring(&HA, 9, 600) && !halloo_may_ring(&HA, 7, 1440) &&
              !halloo_may_ring(&HA, 7, -1),
          "rings: a known contact; never a blocked or unknown one, or at no minute");
    CHECK(halloo_set_quiet(&HA, true, 1320, 420) == ZXM_OK && !halloo_may_ring(&HA, 7, 1320) &&
              !halloo_may_ring(&HA, 7, 0) && !halloo_may_ring(&HA, 7, 419) &&
              halloo_may_ring(&HA, 7, 420) && halloo_may_ring(&HA, 7, 1319),
          "quiet 22:00 to 07:00: silent inside, rings either side");
    CHECK(halloo_set_quiet(&HA, true, 1440, 0) == ZXM_E_ARG && HA.quiet_opens == 1320 &&
              halloo_set_quiet(&HA, true, 600, 600) == ZXM_OK && !halloo_may_ring(&HA, 7, 1200) &&
              halloo_set_quiet(&HA, false, 0, 0) == ZXM_OK && halloo_may_ring(&HA, 7, 1200),
          "quiet: a bad minute refused, the whole day silent, then none");
    CHECK(halloo_set_ring_ms(&HA, 4999) == ZXM_E_ARG &&
              halloo_set_ring_ms(&HA, 30001) == ZXM_E_ARG &&
              halloo_set_ring_ms(&HA, 5000) == ZXM_OK && HA.ring_ms == 5000 &&
              halloo_set_ring_ms(&HA, 20000) == ZXM_OK,
          "ring time: 5 to 30 s, zxvd's own limit");
    CHECK(halloo_length_text(3723000, text, sizeof text) == ZXM_OK && !strcmp(text, "1:02:03") &&
              halloo_length_text(59999, text, sizeof text) == ZXM_OK && !strcmp(text, "0:00:59") &&
              halloo_length_text(-1, text, sizeof text) == ZXM_E_ARG &&
              halloo_length_text(HALLOO_MS_MAX + 1, text, sizeof text) == ZXM_E_ARG &&
              halloo_length_text(0, text, 4) == ZXM_E_TOOBIG,
          "length: h:mm:ss, refused outside HR4 (%s)", text);

    /* without the call group, zxvd answers -32601 and nothing changes */
    zxvd_rpc_set_people_ops(NULL);
    CHECK(halloo_refresh(&HA, 1700000000000LL, 600) == ZXM_E_REMOTE && LINK.err_code == -32601,
          "no call group: -32601 (%s)", LINK.err);

    /* calls through the dispatcher */
    zxvd_rpc_set_people_ops(&people);
    hl_reset();
    int64_t t0 = 1700000000000LL;
    CHECK(halloo_refresh(&HA, t0, 600) == ZXM_OK && HA.self_id == 1000 && HA.loopback_id == 1001 &&
              HA.line[0].state == HALLOO_IDLE && HA.line[4].loopback && !HA.notice[0],
          "status: five idle lines (%s)", LINK.err);
    hl_ring(1, 7, true, "a1a1a1a1a1a1a1a1");
    CHECK(halloo_refresh(&HA, t0 + 1000, 600) == ZXM_OK && HA.line[1].state == HALLOO_INCOMING &&
              HA.line[1].rings && HA.line[1].video && HA.line[1].since_ms == t0 + 1000 &&
              !strcmp(HA.notice, "Ada is calling (video)."),
          "Ada rings with video: %s", HA.notice);
    CHECK(halloo_refresh(&HA, t0 + 21000, 600) == ZXM_OK && !HA.line[1].rings &&
              HA.line[1].since_ms == t0 + 1000 && !HA.notice[0],
          "past the ring time it rings no more (HM3)");
    CHECK(halloo_answer(&HA, 1) == ZXM_OK && HA.line[1].state == HALLOO_CONNECTED &&
              halloo_refresh(&HA, t0 + 22000, 600) == ZXM_OK &&
              HA.line[1].connected_ms == t0 + 22000,
          "answered: connected from 22 s (%s)", LINK.err);
    CHECK(halloo_answer(&HA, 1) == ZXM_E_REMOTE && LINK.err_code == ZXVD_E_PEOPLE &&
              halloo_answer(&HA, 5) == ZXM_E_ARG && halloo_answer(&HA, -1) == ZXM_E_ARG,
          "answer: nothing ringing is zxvd's refusal; no line 5");
    CHECK(halloo_hangup(&HA, 1) == ZXM_OK && HA.line[1].state == HALLOO_ENDED &&
              halloo_refresh(&HA, t0 + 3745000, 600) == ZXM_OK && HA.nhistory == 1 &&
              HA.history[0].seq == 1 && HA.history[0].peer == 7 && HA.history[0].dir == 0 &&
              HA.history[0].media == 1 && HA.history[0].reason == HALLOO_END_LOCAL &&
              HA.history[0].ms == 3723000 && HA.history[0].stamp == t0 + 3745000,
          "hung up: filed, 1:02:03 long (HM5)");
    CHECK(halloo_refresh(&HA, t0 + 3746000, 600) == ZXM_OK && HA.nhistory == 1,
          "an ended call is filed once");

    /* a blocked caller is turned away, an unknown one stays silent */
    hl_ring(2, 8, false, "b2b2b2b2b2b2b2b2");
    hl_ring(3, 99, false, "c3c3c3c3c3c3c3c3");
    uint32_t turned = 0;
    CHECK(halloo_refresh(&HA, t0 + 4000000, 600) == ZXM_OK && !HA.line[2].rings &&
              !HA.line[3].rings && !HA.notice[0],
          "neither the blocked nor the unknown caller rings");
    CHECK(halloo_screen(&HA, &turned) == ZXM_OK && turned == 1 &&
              !strcmp(HL[2].reason, "rejected") && !strcmp(HL[3].state, "incoming"),
          "screen: Bo rejected, the unknown caller left for the owner");
    CHECK(halloo_refresh(&HA, t0 + 4001000, 600) == ZXM_OK && HA.nhistory == 2 &&
              HA.history[1].peer == 8 && HA.history[1].reason == HALLOO_END_REJECTED &&
              HA.history[1].ms == 0,
          "the rejected call is filed with no length");
    /* quiet hours keep a known caller silent */
    hl_reset();
    hl_ring(1, 7, false, "d4d4d4d4d4d4d4d4");
    CHECK(halloo_set_quiet(&HA, true, 1320, 420) == ZXM_OK &&
              halloo_refresh(&HA, t0 + 5000000, 1380) == ZXM_OK && !HA.line[1].rings &&
              halloo_refresh(&HA, t0 + 5001000, 600) == ZXM_OK && HA.line[1].rings,
          "quiet hours: silent at 23:00, rings at 10:00");

    /* placing calls (HM6) */
    hl_reset();
    int32_t line = -1;
    CHECK(halloo_call(&HA, 8, false, &line) == ZXM_E_STATE && HL_STARTS == 0,
          "a blocked contact is not called");
    CHECK(halloo_call(&HA, 7, true, &line) == ZXM_OK && line == 0 &&
              HA.line[0].state == HALLOO_RINGING_OUT && HA.line[0].outgoing && HA.line[0].video &&
              HL[0].peer == 7,
          "calling Ada with video (%s)", LINK.err);
    CHECK(halloo_call(&HA, 7, false, &line) == ZXM_E_REMOTE && LINK.err_code == ZXVD_E_PEOPLE,
          "every line busy: zxvd's refusal (%s)", LINK.err);
    CHECK(halloo_call(&HA, -1, false, &line) == ZXM_E_ARG &&
              halloo_call(&HA, 4294967296LL, false, &line) == ZXM_E_ARG,
          "no such member id");
    hl_reset();
    CHECK(halloo_call(&HA, 0, false, &line) == ZXM_OK && HL[0].peer == 1001 && !HA.line[0].video,
          "peer 0 rings the loopback line");

    /* signalling only inside an encrypted session (HM4) */
    static char got[HALLOO_SIGNAL_MAX][HALLOO_MSG_MAX + 1];
    uint32_t nout = 9;
    const char *in[2] = {"4353010000", "abcd"};
    const char *bad[1] = {"0g"};
    uint32_t before = HL_SIGNALLED;
    CHECK(halloo_signal(&HA, false, in, 2, got, HALLOO_SIGNAL_MAX, &nout) == ZXM_E_STATE &&
              nout == 0 && HL_SIGNALLED == before && strstr(HA.notice, "encrypted"),
          "not encrypted: nothing relayed");
    CHECK(halloo_signal(&HA, true, bad, 1, got, HALLOO_SIGNAL_MAX, &nout) == ZXM_E_ARG &&
              halloo_signal(&HA, true, in, HALLOO_SIGNAL_MAX + 1u, got, HALLOO_SIGNAL_MAX, &nout) ==
                  ZXM_E_ARG,
          "not hex, or too many: refused");
    HL_OUT[0] = "43530100aa";
    HL_OUT[1] = "43530200bb";
    HL_NOUT = 2;
    CHECK(halloo_signal(&HA, true, in, 2, got, HALLOO_SIGNAL_MAX, &nout) == ZXM_OK && nout == 2 &&
              !strcmp(got[0], "43530100aa") && !strcmp(got[1], "43530200bb") &&
              HL_SIGNALLED == before + 2,
          "encrypted: two in, two out (%s)", LINK.err);
    HL_NOUT = 2;
    CHECK(halloo_signal(&HA, true, NULL, 0, got, 1, &nout) == ZXM_E_PROTOCOL,
          "more out than room: refused");
    zxvd_rpc_set_people_ops(NULL);
}
