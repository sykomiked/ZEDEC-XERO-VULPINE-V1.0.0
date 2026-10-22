/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Conch's model against zxvd's real dispatcher: sys.info read for real,
 * notes.list through a host op that holds the notes below. The settings
 * are the model's own (zxvd has no audio-device method). */
#include "conch_model.h"

static const char *CN_NOTES = "{\"unread\":0,\"notes\":[]}";

static void cn_notes(zxvd_buf_t *o)
{
    zxvd_buf_raw(o, CN_NOTES, strlen(CN_NOTES));
}

static uint32_t cn_marked;

static uint32_t cn_read(void)
{
    return cn_marked;
}

#define CNN(seq, kind, pri, read, count)                                                           \
    "{\"seq\":" #seq ",\"kind\":\"" kind "\",\"pri\":" #pri ",\"read\":" #read                     \
    ",\"count\":" #count ",\"source\":\"s\",\"title\":\"t\",\"body\":\"\",\"action\":\"\"}"
#define CNL(unread, notes) "{\"unread\":" #unread ",\"notes\":[" notes "]}"

static const conch_alert_t *cn_a(const conch_t *c, uint32_t seq)
{
    static const conch_alert_t none;
    const conch_alert_t *a = conch_alert(c, seq);
    return a ? a : &none;
}

APPS_TEST(conch)
{
    static conch_t C;
    static zxvd_host_ops_t ops;
    int64_t out = -1;
    bool lim = false;
    ops = OPS;
    ops.notes = cn_notes;
    ops.notes_read = cn_read;
    zxvd_rpc_set_ops(&ops);
    conch_init(&C, &LINK);
    CHECK(C.cap == CONCH_CAP_START && C.loudest == CONCH_LOUDEST_START && C.ndev == 0 &&
              C.map[CONCH_SOUND_ALARM].flash == 3 && !C.seen,
          "conch: a cap of 90 dB SPL, checked at 65 dB SPL, to begin with");

    /* CM1, CM6: pairing on the owner's word */
    {
        int before = sent;
        CHECK(conch_pair(&C, "left aid", CONCH_LEFT, false) == ZXM_E_STATE && C.ndev == 0 &&
                  sent == before,
              "conch: no pairing without the owner's word, nothing sent");
    }
    CHECK(conch_pair(&C, "left aid", CONCH_LEFT, true) == ZXM_OK &&
              conch_pair(&C, "right aid", CONCH_RIGHT, true) == ZXM_OK && C.ndev == 2 &&
              conch_device(&C, "right aid")->side == CONCH_RIGHT,
          "conch: two aids paired");
    {
        static char longname[CONCH_NAME + 2];
        memset(longname, 'n', CONCH_NAME + 1);
        CHECK(conch_pair(&C, "left aid", CONCH_BOTH_EARS, true) == ZXM_E_ARG &&
                  conch_pair(&C, "", CONCH_LEFT, true) == ZXM_E_ARG &&
                  conch_pair(&C, NULL, CONCH_LEFT, true) == ZXM_E_ARG &&
                  conch_pair(&C, longname, CONCH_LEFT, true) == ZXM_E_ARG &&
                  conch_pair(&C, "tab\there", CONCH_LEFT, true) == ZXM_E_ARG &&
                  conch_pair(&C, "loop", CONCH_SIDES, true) == ZXM_E_ARG && C.ndev == 2,
              "conch: a name twice, an empty, long or unprintable name, or no side: refused");
    }
    CHECK(conch_pair(&C, "loop", CONCH_BOTH_EARS, true) == ZXM_OK &&
              conch_pair(&C, "streamer", CONCH_BOTH_EARS, true) == ZXM_OK &&
              conch_pair(&C, "fifth", CONCH_LEFT, true) == ZXM_E_STATE && C.ndev == CONCH_DEVICES,
          "conch: at most four devices");
    CHECK(conch_unpair(&C, "loop", false) == ZXM_E_STATE && C.ndev == 4 &&
              conch_unpair(&C, "loop", true) == ZXM_OK && C.ndev == 3 &&
              !conch_device(&C, "loop") && !strcmp(C.dev[2].name, "streamer") &&
              conch_unpair(&C, "loop", true) == ZXM_E_ARG,
          "conch: unpaired on the owner's word");

    /* CM2: gains under the cap */
    CHECK(conch_set_gain(&C, 3, 250, true) == ZXM_OK && C.gain[3] == 250,
          "conch: +25 dB at 1 kHz, 65 + 25 = 90 dB SPL, at the cap");
    CHECK(conch_set_gain(&C, 3, 251, true) == ZXM_E_ARG && C.gain[3] == 250 &&
              conch_set_gain(&C, 0, CONCH_GAIN_MAX + 1, true) == ZXM_E_ARG &&
              conch_set_gain(&C, 0, CONCH_GAIN_MIN - 1, true) == ZXM_E_ARG &&
              conch_set_gain(&C, CONCH_BANDS, 0, true) == ZXM_E_ARG && C.gain[0] == 0,
          "conch: past the cap, past 30 dB either way or no band: refused");
    {
        int before = sent;
        CHECK(conch_set_gain(&C, 4, 100, false) == ZXM_E_STATE && C.gain[4] == 0 &&
                  conch_set_cap(&C, 1000, false) == ZXM_E_STATE && C.cap == CONCH_CAP_START &&
                  conch_set_loudest(&C, 600, false) == ZXM_E_STATE && C.loudest == 650 &&
                  sent == before,
              "conch: no level changes without the owner's word");
    }
    CHECK(conch_set_cap(&C, 899, true) == ZXM_E_ARG && C.cap == 900 &&
              conch_set_loudest(&C, 651, true) == ZXM_E_ARG && C.loudest == 650,
          "conch: a cap or input that would put a band past the cap: refused, not turned down");
    CHECK(conch_set_cap(&C, 1000, true) == ZXM_OK &&
              conch_set_gain(&C, 4, CONCH_GAIN_MAX, true) == ZXM_OK &&
              conch_set_loudest(&C, 700, true) == ZXM_OK && C.cap == 1000 && C.loudest == 700 &&
              conch_set_cap(&C, CONCH_SPL_MAX + 1, true) == ZXM_E_ARG &&
              conch_set_cap(&C, -1, true) == ZXM_E_ARG &&
              conch_set_loudest(&C, -1, true) == ZXM_E_ARG,
          "conch: the owner raises the cap, then the gain");

    /* CM3: what a band plays, held at the cap */
    CHECK(conch_band_level(&C, 4, 600, &out, &lim) == ZXM_OK && out == 900 && !lim &&
              conch_band_level(&C, 4, 701, &out, &lim) == ZXM_OK && out == 1000 && lim &&
              conch_band_level(&C, 0, 1200, &out, &lim) == ZXM_OK && out == 1000 && lim &&
              conch_band_level(&C, 0, 0, &out, &lim) == ZXM_OK && out == 0 && !lim &&
              conch_band_level(&C, 0, 1201, &out, &lim) == ZXM_E_ARG &&
              conch_band_level(&C, CONCH_BANDS, 0, &out, &lim) == ZXM_E_ARG,
          "conch: a band played, the cap as a ceiling");
    CHECK(conch_set_gain(&C, 5, -300, true) == ZXM_OK &&
              conch_band_level(&C, 5, 100, &out, &lim) == ZXM_OK && out == 0 && !lim,
          "conch: a cut below silence is silence");

    /* CN2, CN4: balance and mono */
    CHECK(conch_set_balance(&C, 30, true, false) == ZXM_E_STATE && C.balance == 0 && !C.mono,
          "conch: no balance change without the owner's word");
    CHECK(conch_set_balance(&C, 30, true, true) == ZXM_OK && C.balance == 30 && C.mono &&
              conch_ear(&C, CONCH_LEFT) == 70 && conch_ear(&C, CONCH_RIGHT) == 100 &&
              conch_set_balance(&C, 101, false, true) == ZXM_E_ARG &&
              conch_set_balance(&C, -101, false, true) == ZXM_E_ARG && C.balance == 30 &&
              conch_set_balance(&C, -100, false, true) == ZXM_OK && conch_ear(&C, CONCH_RIGHT) == 0,
          "conch: balance to the right, then all to the left");

    /* CN3: an alert's pattern */
    CHECK(conch_set_alert(&C, CONCH_SOUND_MESSAGE, 2, 0, false) == ZXM_E_STATE &&
              C.map[CONCH_SOUND_MESSAGE].flash == 1,
          "conch: no pattern change without the owner's word");
    CHECK(conch_set_alert(&C, CONCH_SOUND_MESSAGE, 4, 0, true) == ZXM_E_ARG &&
              conch_set_alert(&C, 0, 1, 1, true) == ZXM_E_ARG &&
              conch_set_alert(&C, CONCH_SOUNDS + 1, 1, 1, true) == ZXM_E_ARG &&
              conch_set_alert(&C, 1, -1, 1, true) == ZXM_E_ARG &&
              conch_set_alert(&C, CONCH_SOUND_MESSAGE, 2, 0, true) == ZXM_OK &&
              C.map[CONCH_SOUND_MESSAGE].flash == 2 && C.map[CONCH_SOUND_MESSAGE].vibrate == 0,
          "conch: four flashes, no kind or a negative pulse refused; messages flash twice");
    CHECK(conch_sound_of("call-missed", 1) == CONCH_SOUND_CALL &&
              conch_sound_of("peer-request", 0) == CONCH_SOUND_MESSAGE &&
              conch_sound_of("ai-task-failed", 2) == CONCH_SOUND_TASK &&
              conch_sound_of("update", 1) == CONCH_SOUND_OTHER &&
              conch_sound_of("message", 3) == CONCH_SOUND_ALARM &&
              conch_sound_of(NULL, 0) == CONCH_SOUND_OTHER,
          "conch: a notice's kind as a sound kind, urgent first");

    /* CM4: alerts from notes.list, at zxvd's own time */
    CN_NOTES = CNL(3, CNN(3, "update", 1, false, 1) "," CNN(2, "message", 1, false, 1) "," CNN(
                          1, "call-incoming", 1, true, 1));
    CHECK(conch_refresh(&C) == ZXM_OK && C.seen && C.now_ms > INT64_C(1700000000000) &&
              C.nalert == 3 && C.unread == 3 && C.fresh == 2 && C.flash_now == 2 &&
              C.vibrate_now == 0 && cn_a(&C, 2)->fresh && cn_a(&C, 2)->flash == 2 &&
              cn_a(&C, 3)->sound == CONCH_SOUND_OTHER && !cn_a(&C, 1)->fresh &&
              cn_a(&C, 1)->sound == CONCH_SOUND_CALL && cn_a(&C, 2)->seen_ms == C.now_ms,
          "conch: a message flashes twice, a read call never: %s", LINK.err);
    int64_t first = C.now_ms;
    CN_NOTES = CNL(3, CNN(4, "message", 3, false, 1) "," CNN(3, "update", 1, false,
                                                             1) "," CNN(2, "message", 1, false, 1));
    CHECK(conch_refresh(&C) == ZXM_OK && C.fresh == 1 && C.flash_now == 3 && C.vibrate_now == 3 &&
              cn_a(&C, 4)->sound == CONCH_SOUND_ALARM && !cn_a(&C, 2)->fresh &&
              cn_a(&C, 2)->seen_ms == first && !conch_alert(&C, 1),
          "conch: an urgent notice is an alarm: three flashes, three pulses");
    CN_NOTES = CNL(1, CNN(2, "message", 1, false, 2));
    CHECK(conch_refresh(&C) == ZXM_OK && C.fresh == 1 && cn_a(&C, 2)->fresh && C.flash_now == 2 &&
              C.nalert == 1,
          "conch: the same message again (count 2) is fresh");
    CHECK(conch_refresh(&C) == ZXM_OK && C.fresh == 0 && C.flash_now == 0 && C.vibrate_now == 0,
          "conch: nothing new, nothing flashed");

    /* CM7: dismissed on the owner's word only */
    CN_NOTES = CNL(1, CNN(2, "message", 1, false, 3));
    CHECK(conch_refresh(&C) == ZXM_OK && C.fresh == 1 && C.flash_now == 2 && C.unread == 1,
          "conch: a third message flashes");
    {
        uint32_t marked = 0;
        int before = sent;
        cn_marked = 1;
        CHECK(conch_dismiss(&C, &marked, false) == ZXM_E_STATE && sent == before && marked == 0 &&
                  C.unread == 1 && C.flash_now == 2 && !cn_a(&C, 2)->read,
              "conch: without the owner's word nothing is sent and nothing dismissed");
        CHECK(conch_dismiss(&C, &marked, true) == ZXM_OK && sent == before + 1 && marked == 1 &&
                  C.unread == 0 && C.fresh == 0 && C.flash_now == 0 && C.vibrate_now == 0 &&
                  cn_a(&C, 2)->read && !cn_a(&C, 2)->fresh,
              "conch: dismissed through notes.mark_read: %s", LINK.err);
    }

    /* CM5: a reply out of bounds, or the clock going back, changes nothing */
    static const char *const bad[] = {
        CNL(1, CNN(0, "message", 1, false, 1)),
        CNL(1, CNN(5, "message", 4, false, 1)),
        CNL(1, CNN(5, "message", 1, false, 0)),
        CNL(2, CNN(5, "message", 1, false, 1) "," CNN(5, "message", 1, false, 1)),
        CNL(1, CNN(5, "a-kind-of-sixteen", 1, false, 1)),
        "{\"unread\":1,\"notes\":{}}",
        "{\"notes\":[]}",
    };
    bool kept = true;
    for (uint32_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        CN_NOTES = bad[i];
        kept = kept && conch_refresh(&C) == ZXM_E_PROTOCOL && C.nalert == 1 && conch_alert(&C, 2);
    }
    CHECK(kept, "conch: a reply out of bounds leaves what was shown");
    {
        static char many[4096];
        size_t n = (size_t) snprintf(many, sizeof many, "{\"unread\":21,\"notes\":[");
        for (uint32_t i = 1; i <= CONCH_NOTES + 1; i++)
            n += (size_t) snprintf(many + n, sizeof many - n,
                                   "%s{\"seq\":%u,\"kind\":\"message\",\"pri\":1,\"read\":false,"
                                   "\"count\":1,\"source\":\"a\",\"title\":\"t\"}",
                                   i > 1 ? "," : "", i);
        snprintf(many + n, sizeof many - n, "]}");
        CN_NOTES = many;
        CHECK(conch_refresh(&C) == ZXM_E_PROTOCOL && C.nalert == 1,
              "conch: more notes than notes.list gives");
    }
    CN_NOTES = CNL(1, CNN(9, "message", 1, false, 1));
    {
        int64_t was = C.now_ms;
        C.now_ms = CONCH_MS_MAX;
        CHECK(conch_refresh(&C) == ZXM_E_PROTOCOL && C.nalert == 1 && !conch_alert(&C, 9),
              "conch: zxvd's clock behind the last refresh: nothing changes");
        C.now_ms = was;
    }

    /* without the host's notes, zxvd answers an error and nothing changes */
    zxvd_rpc_set_ops(&OPS);
    CHECK(conch_refresh(&C) == ZXM_E_REMOTE && LINK.err_code == ZXVD_E_METHOD && C.nalert == 1,
          "conch: notes.list not available: %s", LINK.err);
}
