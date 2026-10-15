/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zui_hear.c — host tests for where speech goes: the Chiglet by name or
 * by its phrases, voice control's commands, dictation into a focused field
 * (where the Chiglet's phrases are typed, not obeyed), barge-in, the mic
 * mark, empty and lossy transcripts, and the speech session's events. */
#include <stdio.h>
#include <string.h>

#include "zui_hear.h"
#include "zui_tokens.h"

static int fails, checks;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            fails++;                                                                               \
            printf("  FAIL (%s:%d) ", __FILE__, __LINE__);                                         \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

#define W_ 800
#define H_ 600
static zui_ui_t UI;
static zui_voicectl_t VC;
static zui_cv_t CV;
static zui_hear_t HR;
static zui_screen_t laptop = {W_, H_, 256, ZUI_INPUT_POINTER};
static struct {
    zui_id_t root, save, field;
    char buf[256];
} V;
static int n_saved;

static void on(zui_ui_t *ui, zui_id_t w, zui_action_t act, int32_t arg, void *ctx)
{
    (void) ui, (void) arg, (void) ctx;
    if (w == V.save && act == ZUI_ACT_PRESS) n_saved++;
}

static int n_said, n_stops;
static char SAID[16][ZUI_CV_CHUNK];
static int speak(const char *t, const char *l, uint32_t v, uint32_t r, void *c)
{
    (void) l, (void) v, (void) r, (void) c;
    if (n_said < 16) snprintf(SAID[n_said], ZUI_CV_CHUNK, "%s", t);
    n_said++;
    return 0;
}
static void stop(void *c)
{
    (void) c;
    n_stops++;
}

static int n_facts;
static void facts(zui_cv_facts_t *f, void *ctx)
{
    (void) ctx;
    n_facts++;
    f->need_you = 0;
}

static void setup(void)
{
    zui_theme_t th;
    memset(&th, 0, sizeof th);
    th.body.size_px = 16;
    th.label.size_px = 12;
    th.title.size_px = 20;
    th.mono.size_px = 16;
    zui_ui_init(&UI, &laptop, &th);
    zui_ui_on(&UI, on, 0);
    memset(&V, 0, sizeof V);
    V.root = zui_box(&UI, ZUI_NONE, ZUI_COL);
    V.save = zui_button(&UI, V.root, "Save", ZUI_BTN_PRIMARY);
    V.field = zui_field(&UI, V.root, V.buf, sizeof V.buf, true);
    zui_set_name(&UI, V.field, "Message");
    static uint32_t fb[W_ * H_];
    zui_surface_t s;
    zui_surface_init(&s, fb, W_, H_, W_);
    zui_ui_paint(&UI, &s);
    zui_vc_init(&VC);
    zui_vc_attach(&VC, &UI, 0, 0, 0);
    static const zui_chiglet_t who = {{1, 2, 3, 4, 5, 6, 7, 8}, 1, ZUI_CHG_DECIDED, 0};
    zui_cv_init(&CV, &who, speak, stop, 0);
    zui_hear_init(&HR, &CV, &VC);
    zui_hear_set_facts(&HR, facts, 0);
    n_said = n_stops = n_saved = n_facts = 0;
}

static void test_route(void)
{
    setup();
    CHECK(zui_hear_text(&HR, "How are you?", 10) == ZUI_HEAR_CHIGLET, "Chiglet phrase");
    CHECK(n_said == 1 && !strcmp(SAID[0], "Nothing needs you right now."), "status: %s", SAID[0]);
    CHECK(n_facts == 1, "facts asked for");
    CHECK(!strcmp(HR.heard, "How are you?"), "heard kept");
    CHECK(zui_hear_text(&HR, "click save", 20) == ZUI_HEAR_CONTROL && n_saved == 1,
          "command to voice control");
    CHECK(zui_hear_text(&HR, "Chiglet, read this", 30) == ZUI_HEAR_CHIGLET, "by name");
    CHECK(!strcmp(SAID[n_said - 1], "I cannot find that section."), "read: %s", SAID[n_said - 1]);
    CHECK(zui_hear_text(&HR, "hey Chiglet how are you", 40) == ZUI_HEAR_CHIGLET, "hey name");
    for (int i = 0; i < 8 && zui_cv_talking(&CV); i++) zui_cv_spoken(&CV, 50 + (uint32_t) i);
    int before = n_said;
    CHECK(zui_hear_text(&HR, "Chiglet!", 70000) == ZUI_HEAR_CHIGLET && n_said == before + 1,
          "the name alone is a tap");
    CHECK(zui_hear_text(&HR, "OK chiglet, click save", 70100) == ZUI_HEAR_CHIGLET && n_saved == 1,
          "a command addressed to the Chiglet is the Chiglet's, not a click");
    CHECK(zui_hear_text(&HR, "chiglets are nice", 70200) != ZUI_HEAR_CHIGLET,
          "a longer word is not the name");
    CHECK(zui_hear_text(&HR, "make me a sandwich", 70300) == ZUI_HEAR_UNKNOWN, "nobody's");
    CHECK(HR.to_chiglet == 5 && HR.to_control == 1, "counted %u %u", HR.to_chiglet, HR.to_control);
    /* HE4 */
    CHECK(zui_hear_text(&HR, "", 0) == ZUI_HEAR_IGNORED &&
              zui_hear_text(&HR, " . ", 0) == ZUI_HEAR_IGNORED,
          "blank ignored");
    CHECK(zui_hear_text(&HR, 0, 0) == ZUI_HEAR_IGNORED, "NULL ignored");
    CHECK(zui_hear_text(0, "x", 0) == ZUI_HEAR_IGNORED, "NULL hear");
}

static void test_dictation(void)
{
    setup();
    zui_focus(&UI, V.field, true);
    CHECK(zui_hear_text(&HR, "how are you", 10) == ZUI_HEAR_DICTATED, "typed, not obeyed");
    CHECK(strstr(V.buf, "how are you") != 0 && n_said == 0, "in the field: %s", V.buf);
    CHECK(zui_hear_text(&HR, "Chiglet, how are you", 20) == ZUI_HEAR_CHIGLET && n_said == 1,
          "by name it still answers");
    CHECK(zui_hear_text(&HR, "stop", 30) == ZUI_HEAR_DICTATED, "stop is a word here");
    zui_vc_set_dictation(&VC, ZUI_VC_DICT_ON);
    CHECK(zui_hear_text(&HR, "Chiglet, how are you", 40) == ZUI_HEAR_DICTATED,
          "dictation on: even the name is typed");
    zui_vc_set_dictation(&VC, ZUI_VC_DICT_OFF);
    CHECK(zui_hear_text(&HR, "how are you", 50) == ZUI_HEAR_CHIGLET, "dictation off: obeyed");
    /* no Chiglet, no control */
    zui_hear_t bare;
    zui_hear_init(&bare, 0, 0);
    CHECK(zui_hear_text(&bare, "how are you", 0) == ZUI_HEAR_UNKNOWN, "no one to hear");
    zui_hear_init(&bare, &CV, 0);
    CHECK(zui_hear_text(&bare, "click save", 0) == ZUI_HEAR_UNKNOWN, "no control");
    zui_hear_init(0, 0, 0);
    zui_hear_set_facts(0, 0, 0);
    zui_hear_tick(0, 0);
    zui_hear_start(0, 0);
    zui_hear_end(0, 0);
    zui_hear_on_event(0, 0);
}

static void test_barge(void)
{
    setup();
    zui_cv_read(&CV, "One. Two. Three.", 0, 0);
    CHECK(CV.playing, "talking");
    zui_hear_start(&HR, 100);
    CHECK(HR.listening && n_stops == 1 && !zui_cv_talking(&CV), "speaking over it stops it");
    CHECK(HR.barge_ins == 1 && CV.busy, "counted, and it waits");
    zui_cv_read(&CV, "Later.", 0, 150);
    CHECK(n_said == 1 && CV.waiting, "it waits while the person speaks");
    zui_hear_end(&HR, 200);
    CHECK(!HR.listening && !CV.busy && n_said == 2, "then goes");
    zui_hear_start(&HR, 300);
    CHECK(HR.barge_ins == 2 && n_stops == 2, "'Later.' was playing: stopped too (%u)",
          HR.barge_ins);
    zui_hear_end(&HR, 400);
    zui_hear_start(&HR, 500);
    CHECK(HR.barge_ins == 2 && n_stops == 2, "not a barge-in when it is quiet (%u)", HR.barge_ins);
    zui_hear_end(&HR, 600);
}

static void test_events(void)
{
    setup();
    zui_hear_tick(&HR, 500);
    speech_event_t ev;
    memset(&ev, 0, sizeof ev);
    ev.type = SPEECH_EV_UTTERANCE_START;
    ev.text = "";
    zui_hear_on_event(&ev, &HR);
    CHECK(HR.listening && CV.busy, "start: mic shown");
    ev.type = SPEECH_EV_TRANSCRIPT;
    ev.text = "click save";
    zui_hear_on_event(&ev, &HR);
    CHECK(!HR.listening && n_saved == 1 && HR.last == ZUI_HEAR_CONTROL, "transcript routed");
    ev.type = SPEECH_EV_UTTERANCE_START;
    zui_hear_on_event(&ev, &HR);
    ev.type = SPEECH_EV_TRANSCRIPT;
    ev.flags = SPEECH_EVF_LOSSY;
    zui_hear_on_event(&ev, &HR);
    CHECK(n_saved == 1 && HR.last == ZUI_HEAR_IGNORED && !HR.listening, "lossy ignored");
    ev.flags = 0;
    ev.type = SPEECH_EV_UTTERANCE_START;
    zui_hear_on_event(&ev, &HR);
    ev.type = SPEECH_EV_ERROR;
    zui_hear_on_event(&ev, &HR);
    CHECK(!HR.listening && !CV.busy, "error: mic off");
    ev.type = SPEECH_EV_SPOKEN;
    zui_hear_on_event(&ev, &HR);
    CHECK(!HR.listening, "other events change nothing");
}

int main(void)
{
    test_route();
    test_dictation();
    test_barge();
    test_events();
    printf("%s test_zui_hear: %d/%d checks\n", fails ? "[FAIL]" : "[PASS]", checks - fails, checks);
    return fails ? 1 : 0;
}
