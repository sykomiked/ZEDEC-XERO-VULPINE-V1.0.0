/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Nightjar's model against zxvd's real dispatcher (LINK): sys.info as zxvd
 * answers it; notes.list through host ops that hold the notices below.
 * zxvd_people.c, which serves ui.* and xlate.* in the daemon, is not
 * linked into this test, so ui.get and xlate.detect are answered by a
 * small host in their own shapes (api/zxvd.openrpc.json), set as zxvd's
 * people ops; the requests still go through zxvd_rpc_handle_line. */
#include "nightjar_model.h"

static const char *NJ_NOTES = "{\"unread\":0,\"notes\":[]}";
static bool nj_simple, nj_detect_refused;
static uint32_t nj_detects;

static void nj_notes(zxvd_buf_t *o)
{
    zxvd_buf_raw(o, NJ_NOTES, strlen(NJ_NOTES));
}

static uint32_t nj_read(void)
{
    return 0;
}

static int nj_people(const char *method, const char *params, size_t len, zxvd_buf_t *out, char *msg,
                     size_t msg_cap)
{
    char ps[1024];
    if (len >= sizeof ps) len = sizeof ps - 1u;
    memcpy(ps, params, len);
    ps[len] = 0;
    if (!strcmp(method, "ui.get")) {
        zxvd_buf_printf(out, "{\"simple_mode\":%s}", nj_simple ? "true" : "false");
        return 0;
    }
    if (!strcmp(method, "xlate.detect") && !nj_detect_refused) {
        nj_detects++;
        bool de = strstr(ps, "Speichern") != NULL;
        zxvd_buf_printf(out,
                        "{\"language\":\"%s\",\"name\":\"%s\",\"confidence\":80,\"letters\":8,"
                        "\"reliable\":true}",
                        de ? "de" : "en", de ? "German" : "English");
        return 0;
    }
    snprintf(msg, msg_cap, "not here");
    return -32601;
}

#define NJN(seq, pri, read, src, title)                                                            \
    "{\"seq\":" #seq ",\"kind\":\"message\",\"pri\":" #pri ",\"read\":" #read                      \
    ",\"count\":1,\"source\":\"" src "\",\"title\":\"" title "\",\"body\":\"\",\"action\":\"\"}"
#define NJL(unread, notes) "{\"unread\":" #unread ",\"notes\":[" notes "]}"

APPS_TEST(nightjar)
{
    static nightjar_t N, was;
    static zxvd_host_ops_t ops;
    static const zxvd_people_ops_t people = {nj_people};
    nightjar_say_t s;
    char text[NIGHTJAR_SAY_MAX + 1];
    uint32_t idx = 0, q = 0, order[NIGHTJAR_ELEMENTS], count = 0;

    /* the rules, as the blocks run them (NR1-NR4) */
    {
        int64_t row[4] = {3, 1, 0, 3}, col[4] = {40, 5, 0, 10};
        CHECK(nightjar_cuts_in(NIGHTJAR_ALERT) && nightjar_cuts_in(NIGHTJAR_FOCUS) &&
                  !nightjar_cuts_in(NIGHTJAR_PAGE) && !nightjar_cuts_in(0) &&
                  !nightjar_cuts_in(-1) && !nightjar_cuts_in(4),
              "nightjar: NR1, alerts and focus changes cut in, page reading does not");
        CHECK(nightjar_voice_ok(80, 50) && nightjar_voice_ok(450, 200) &&
                  !nightjar_voice_ok(79, 100) && !nightjar_voice_ok(451, 100) &&
                  !nightjar_voice_ok(180, 49) && !nightjar_voice_ok(180, 201),
              "nightjar: NR2, the speech limits");
        CHECK(nightjar_element_fits(999, 9, 99, 9999, 9999, 9) &&
                  !nightjar_element_fits(1000, 0, 1, 0, 0, 0) &&
                  !nightjar_element_fits(0, 0, 100, 0, 0, 0),
              "nightjar: NR3, the record's digits");
        CHECK(nightjar_reading_place(row, col, 4, 0) == 4 &&
                  nightjar_reading_place(row, col, 4, 1) == 2 &&
                  nightjar_reading_place(row, col, 4, 2) == 1 &&
                  nightjar_reading_place(row, col, 4, 3) == 3 &&
                  nightjar_reading_place(row, col, 4, 4) == 0 &&
                  nightjar_reading_place(NULL, col, 4, 0) == 0,
              "nightjar: NR4, by row then column");
        CHECK(nightjar_speak_ms("one two three", 180) == 1000 &&
                  nightjar_speak_ms("  one  ", 450) == 134 && nightjar_speak_ms("", 180) == 0 &&
                  nightjar_speak_ms("one", 79) == -1 && nightjar_speak_ms(NULL, 180) == -1,
              "nightjar: how long a thing takes to say");
    }

    ops = OPS;
    ops.notes = nj_notes;
    ops.notes_read = nj_read;
    zxvd_rpc_set_ops(&ops);
    zxvd_rpc_set_people_ops(&people);
    nightjar_init(&N, &LINK);
    CHECK(N.wpm == 180 && N.pitch == 100 && N.focus == -1 && nightjar_waiting(&N) == 0 &&
              nightjar_next(&N, &s) == ZXM_E_STATE,
          "nightjar: init, 180 words a minute, nothing to say");

    /* NJ1: the screen's elements, each checked before it is kept */
    {
        char longname[NIGHTJAR_NAME_MAX + 2];
        memset(longname, 'n', sizeof longname - 1u);
        longname[sizeof longname - 1u] = 0;
        int before = sent;
        CHECK(nightjar_add_element(&N, 10, "x", 0, 0, 0, &idx) == ZXM_E_ARG &&
                  nightjar_add_element(&N, -1, "x", 0, 0, 0, &idx) == ZXM_E_ARG &&
                  nightjar_add_element(&N, 2, "", 0, 0, 0, &idx) == ZXM_E_ARG &&
                  nightjar_add_element(&N, 2, NULL, 0, 0, 0, &idx) == ZXM_E_ARG &&
                  nightjar_add_element(&N, 2, "a\nb", 0, 0, 0, &idx) == ZXM_E_ARG &&
                  nightjar_add_element(&N, 2, longname, 0, 0, 0, &idx) == ZXM_E_ARG &&
                  nightjar_add_element(&N, 2, "x", 10000, 0, 0, &idx) == ZXM_E_ARG &&
                  nightjar_add_element(&N, 2, "x", 0, -1, 0, &idx) == ZXM_E_ARG &&
                  nightjar_add_element(&N, 2, "x", 0, 0, 8, &idx) == ZXM_E_ARG && N.nel == 0 &&
                  sent == before,
              "nightjar: a role, name, spot or state outside its range: refused, nothing kept");
        longname[NIGHTJAR_NAME_MAX] = 0;
        CHECK(nightjar_add_element(&N, 2, longname, 9999, 9999, 7, &idx) == ZXM_OK && idx == 0,
              "nightjar: the longest name at the last spot");
        nightjar_clear(&N);
    }
    CHECK(nightjar_add_element(&N, 2, "Speichern", 3, 40, NIGHTJAR_CHECKED, &idx) == ZXM_OK &&
              idx == 0 && nightjar_add_element(&N, 4, "Name", 1, 5, 0, &idx) == ZXM_OK &&
              nightjar_add_element(&N, 1, "Title", 0, 0, 0, &idx) == ZXM_OK &&
              nightjar_add_element(&N, 2, "Cancel", 3, 10, NIGHTJAR_DIMMED, &idx) == ZXM_OK &&
              idx == 3 && N.nel == 4,
          "nightjar: four elements on the screen");

    /* NJ2, NJ3: reading order and words */
    CHECK(nightjar_order(&N, order, NIGHTJAR_ELEMENTS, &count) == ZXM_OK && count == 4 &&
              order[0] == 2 && order[1] == 1 && order[2] == 3 && order[3] == 0 &&
              nightjar_order(&N, order, 3, &count) == ZXM_E_ARG,
          "nightjar: Title, Name, Cancel, then the button on the right");
    CHECK(nightjar_describe(&N, 0, text, sizeof text) > 0 &&
              !strcmp(text, "Speichern, button, checked, 4 of 4") &&
              nightjar_describe(&N, 3, text, sizeof text) > 0 &&
              !strcmp(text, "Cancel, button, dimmed, 3 of 4") &&
              nightjar_describe(&N, 4, text, sizeof text) == -1,
          "nightjar: an element in words: %s", text);

    /* NJ8: the voice, only inside its limits and on the owner's word */
    {
        int before = sent;
        CHECK(nightjar_set_voice(&N, 451, 100, true) == ZXM_E_ARG &&
                  nightjar_set_voice(&N, 300, 201, true) == ZXM_E_ARG &&
                  nightjar_set_voice(&N, 300, 120, false) == ZXM_E_STATE && N.wpm == 180 &&
                  N.pitch == 100 && sent == before,
              "nightjar: a voice outside its limits, or without the owner's word: unchanged");
        CHECK(nightjar_set_voice(&N, 300, 120, true) == ZXM_OK && N.wpm == 300 && N.pitch == 120,
              "nightjar: 300 words a minute at the owner's word");
    }

    /* NJ3, NJ6: simple mode and the notices; the urgent one cuts in on the
     * waiting one */
    NJ_NOTES = NJL(2, NJN(3, 3, false, "power", "Battery low") "," NJN(
                          2, 1, false, "mail", "New mail") "," NJN(1, 1, true, "mail", "Old"));
    nj_simple = true;
    CHECK(nightjar_refresh(&N, &q) == ZXM_OK && q == 2 && N.simple && N.last_seq == 3 &&
              nightjar_waiting(&N) == 1 && N.cut && N.dropped == 1,
          "nightjar: two new notices, the urgent one cut in: %s", LINK.err);
    CHECK(nightjar_next(&N, &s) == ZXM_OK && s.kind == NIGHTJAR_ALERT && s.element == -1 &&
              !strcmp(s.text, "Notice from power: Battery low") && !strcmp(s.lang, "en") &&
              !N.cut && nightjar_waiting(&N) == 0,
          "nightjar: the alert said first, in English: %s", s.text);
    CHECK(nightjar_refresh(&N, &q) == ZXM_OK && q == 0 && nightjar_waiting(&N) == 0,
          "nightjar: nothing new is said again");
    CHECK(nightjar_describe(&N, 0, text, sizeof text) > 0 && !strcmp(text, "Speichern, checked"),
          "nightjar: simple mode leaves the role and place out: %s", text);
    nj_simple = false;

    /* NJ4, NJ5: the page waits its turn; a focus change cuts in */
    CHECK(nightjar_refresh(&N, &q) == ZXM_OK && !N.simple && nightjar_read_page(&N, &q) == ZXM_OK &&
              q == 4 && nightjar_waiting(&N) == 4 && !N.cut,
          "nightjar: the page queued: %s", LINK.err);
    CHECK(nightjar_next(&N, &s) == ZXM_OK && s.kind == NIGHTJAR_PAGE && s.element == 2 &&
              !strcmp(s.text, "Title, text, 1 of 4"),
          "nightjar: the page from the top: %s", s.text);
    CHECK(nightjar_focus(&N, 0) == ZXM_OK && N.focus == 0 && N.cut && nightjar_waiting(&N) == 1 &&
              N.dropped == 4 && nightjar_next(&N, &s) == ZXM_OK && s.kind == NIGHTJAR_FOCUS &&
              s.element == 0 && !strcmp(s.lang, "de") &&
              !strcmp(s.text, "Speichern, button, checked, 4 of 4"),
          "nightjar: focus on the button cut in, in German: %s", s.text);
    CHECK(nightjar_focus(&N, 4) == ZXM_E_ARG && N.focus == 0, "nightjar: no such element");

    /* NJ7: where am I, with the machine's name from sys.info */
    CHECK(nightjar_where(&N) == ZXM_OK && nightjar_next(&N, &s) == ZXM_OK &&
              s.kind == NIGHTJAR_FOCUS && s.element == 0 &&
              !strncmp(s.text, "Speichern, button, checked, 4 of 4", 34) &&
              (!N.host[0] || strstr(s.text, N.host)),
          "nightjar: where am I: %s (%s)", s.text, LINK.err);

    /* NJ5: zxvd cannot tell the language: "und" */
    nj_detect_refused = true;
    CHECK(nightjar_focus(&N, 1) == ZXM_OK && nightjar_next(&N, &s) == ZXM_OK &&
              !strcmp(s.lang, "und") && !strcmp(s.text, "Name, field, 2 of 4"),
          "nightjar: the language refused, said as undetermined");
    nj_detect_refused = false;

    /* NJ4: a full ring is refused, never overwritten, and a refused call
     * queues nothing */
    {
        int rc = ZXM_OK;
        uint32_t k = 0;
        while (rc == ZXM_OK && k < 20u) {
            rc = nightjar_read_page(&N, &q);
            k++;
        }
        CHECK(rc == ZXM_E_STATE && k == 9 && nightjar_waiting(&N) == NIGHTJAR_QUEUE &&
                  N.head - N.tail == NIGHTJAR_QUEUE,
              "nightjar: eight pages fill the ring, the ninth is refused (%u)", k);
        was = N;
        CHECK(nightjar_focus(&N, 2) == ZXM_OK && nightjar_waiting(&N) == 1 &&
                  N.dropped == was.dropped + NIGHTJAR_QUEUE,
              "nightjar: a focus change clears a full page of reading");
        rc = ZXM_OK;
        k = 0;
        while (rc == ZXM_OK && k < 40u) {
            rc = nightjar_focus(&N, k % 4u);
            k++;
        }
        CHECK(rc == ZXM_E_STATE && k == NIGHTJAR_QUEUE && nightjar_waiting(&N) == NIGHTJAR_QUEUE &&
                  N.focus == (int32_t) ((NIGHTJAR_QUEUE - 2u) % 4u),
              "nightjar: thirty-two focus changes fill it, the next is refused (%u)", k);
        was = N;
        NJ_NOTES = NJL(1, NJN(4, 3, false, "power", "Battery empty"));
        CHECK(nightjar_refresh(&N, &q) == ZXM_E_STATE && N.last_seq == was.last_seq &&
                  nightjar_waiting(&N) == NIGHTJAR_QUEUE,
              "nightjar: an alert with no room: refused, the notice left to the next refresh");
        while (nightjar_next(&N, &s) == ZXM_OK) {
        }
        CHECK(nightjar_waiting(&N) == 0 && nightjar_refresh(&N, &q) == ZXM_OK && q == 1 &&
                  N.last_seq == 4,
              "nightjar: said once there is room");
    }

    /* NJ6: a reply out of bounds changes nothing */
    was = N;
    NJ_NOTES = NJL(1, NJN(5, 7, false, "power", "Bad"));
    CHECK(nightjar_refresh(&N, &q) == ZXM_E_PROTOCOL && N.last_seq == was.last_seq &&
              nightjar_waiting(&N) == nightjar_waiting(&was),
          "nightjar: a priority past urgent is no notice");
    NJ_NOTES = "{\"unread\":1,\"notes\":{}}";
    CHECK(nightjar_refresh(&N, &q) == ZXM_E_PROTOCOL && N.last_seq == was.last_seq,
          "nightjar: notes that are no list");

    /* the screen changes: its page reading goes, an alert stays */
    NJ_NOTES = NJL(1, NJN(6, 2, false, "power", "Plug in"));
    while (nightjar_next(&N, &s) == ZXM_OK) {
    }
    CHECK(nightjar_read_page(&N, &q) == ZXM_OK && nightjar_refresh(&N, &q) == ZXM_OK &&
              nightjar_read_page(&N, &q) == ZXM_OK && nightjar_waiting(&N) == 5,
          "nightjar: an alert, then a page");
    nightjar_clear(&N);
    CHECK(N.nel == 0 && N.focus == -1 && nightjar_waiting(&N) == 1 &&
              nightjar_read_page(&N, &q) == ZXM_E_STATE && nightjar_where(&N) == ZXM_OK &&
              nightjar_next(&N, &s) == ZXM_OK && s.kind == NIGHTJAR_ALERT &&
              nightjar_next(&N, &s) == ZXM_OK && !strncmp(s.text, "Nothing has focus", 17),
          "nightjar: a new screen: the alert, then nothing has focus: %s", s.text);

    /* without ui.get, a refresh changes nothing */
    zxvd_rpc_set_people_ops(NULL);
    was = N;
    CHECK(nightjar_refresh(&N, &q) == ZXM_E_REMOTE && N.last_seq == was.last_seq &&
              N.simple == was.simple,
          "nightjar: ui.get not served: %s", LINK.err);
    zxvd_rpc_set_ops(&OPS);
}
