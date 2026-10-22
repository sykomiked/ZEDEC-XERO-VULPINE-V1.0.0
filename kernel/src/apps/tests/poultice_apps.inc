/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Poultice's model against zxvd's real dispatcher (LINK): the care plan a
 * file in the Hoard (files.read, files.write), set and logged only on the
 * owner's word, doses measured against the clinician's limits and never
 * changed, two carers' heart rates for one minute held BOTH with both
 * kept and every carer told, the measures over the parts it composes, and
 * the notices (notes.list, notes.mark_read) through host ops that hold
 * them, as Stillwater's test does. */
#include "poultice_model.h"

static poultice_t PLT, PLT2;
static const char *PLT_NOTES = "{\"unread\":0,\"notes\":[]}";
static uint32_t plt_marked;

static void plt_notes(zxvd_buf_t *o)
{
    zxvd_buf_raw(o, PLT_NOTES, strlen(PLT_NOTES));
}

static uint32_t plt_read(void)
{
    return plt_marked;
}

#define PLTN(seq, read, src)                                                                       \
    "{\"seq\":" #seq ",\"kind\":\"info\",\"pri\":1,\"read\":" #read                                \
    ",\"count\":1,\"source\":\"" src "\",\"title\":\"t\",\"body\":\"\",\"action\":\"\"}"

static bool plt_file_has(const char *line)
{
    static uint8_t buf[40960];
    uint32_t got = 0;
    uint64_t size = 0;
    if (zxf_read(POULTICE_PLAN, 0, buf, sizeof buf - 1u, true, &got, &size) != 0) return false;
    buf[got] = 0;
    return size == got && strstr((const char *) buf, line) != NULL;
}

static bool plt_write(const char *text)
{
    zxm_begin(&LINK, "files.write");
    zxm_p_str(&LINK, "path", POULTICE_PLAN);
    zxm_p_str(&LINK, "text", text);
    return zxm_call(&LINK) == ZXM_OK;
}

APPS_TEST(poultice)
{
    const int64_t DAY = 20748; /* 2026-10-22, a Thursday */
    const int64_t STAMP = INT64_C(1792627200000);
    int64_t m[3] = {0, 0, 0}, v = 0, span[3] = {0, 0, 0};
    uint32_t marked = 0;

    /* the rules */
    CHECK(poultice_dose_in_limit(0, 0, 0) && !poultice_dose_in_limit(-1, 0, 0) &&
              !poultice_dose_in_limit(0, POULTICE_DAY_MAX + 1, 0) &&
              poultice_dose_in_limit_n(0, -1, 0) == 0 && poultice_dose_in_limit_n(5, 5, 5) == 1 &&
              poultice_dose_in_limit_n(0, -POULTICE_DAY_LOW - 1, 0) == POULTICE_REFUSED &&
              poultice_month_cost(450, 2, 31) == 27900 &&
              poultice_month_cost(POULTICE_PRICE_MAX + 1, 1, 1) == POULTICE_REFUSED &&
              poultice_node_ok(9999, 3, 9999, 999999, 1439, 99, 999999999) &&
              !poultice_node_ok(1, 3, 1, DAY, 600, 100, 72),
          "poultice: the rules");

    /* nothing before a load */
    CHECK(zxf_use_space(NULL) == 0, "poultice: home");
    poultice_init(&PLT, &LINK);
    int before = sent;
    CHECK(poultice_set_patient(&PLT, 7, true) == ZXM_E_STATE &&
              poultice_add_carer(&PLT, 1, 7, STAMP, "Ann", true) == ZXM_E_STATE &&
              poultice_log_entry(&PLT, POULTICE_VITAL, DAY, 600, POULTICE_HEART, 72, 1, true) ==
                  ZXM_E_STATE &&
              sent == before,
          "poultice: nothing written before the plan is read");
    CHECK(poultice_load(&PLT) == ZXM_OK && PLT.loaded && PLT.nmed == 0 && PLT.nlog == 0,
          "poultice: no plan yet is an empty one: %s", LINK.err);

    /* PM2: the plan, set only on the owner's word, refused before anything is sent */
    poultice_med_t para = {1, 1000000, 4, 240, DAY, 0, 0, DAY - 8, "Paracetamol"};
    poultice_med_t jab = {2, 50, 1, 10080, DAY - 5, 7, 96, 0, "Weekly injection"};
    poultice_med_t bad = para;
    bad.daymax = 100;
    before = sent;
    CHECK(poultice_set_patient(&PLT, 0, true) == ZXM_E_ARG &&
              poultice_set_patient(&PLT, 10000, true) == ZXM_E_ARG &&
              poultice_set_med(&PLT, &bad, true) == ZXM_E_ARG &&
              poultice_set_med(&PLT, NULL, true) == ZXM_E_ARG &&
              poultice_set_range(&PLT, 9, 1, 2, true) == ZXM_E_ARG &&
              poultice_set_range(&PLT, POULTICE_HEART, 110, 50, true) == ZXM_E_ARG &&
              poultice_add_carer(&PLT, 100, 7, STAMP, "Ann", true) == ZXM_E_ARG &&
              poultice_add_carer(&PLT, 1, 7, STAMP, "A\tnn", true) == ZXM_E_ARG &&
              poultice_add_carer(&PLT, 1, 7, STAMP, "", true) == ZXM_E_ARG &&
              poultice_set_item(&PLT, 1, POULTICE_PRICE_MAX + 1, 1, "Dressings", true) ==
                  ZXM_E_ARG &&
              poultice_set_field(&PLT, 7, STAMP, "x", true) == ZXM_E_ARG &&
              poultice_set_field(&PLT, 2, STAMP, "", true) == ZXM_E_ARG && sent == before,
          "poultice: limits, ranges, carers, items and fields that do not fit are refused");
    CHECK(poultice_set_patient(&PLT, 7, false) == ZXM_E_STATE &&
              poultice_set_med(&PLT, &para, false) == ZXM_E_STATE &&
              poultice_set_range(&PLT, POULTICE_HEART, 50, 110, false) == ZXM_E_STATE &&
              poultice_add_carer(&PLT, 1, 7, STAMP, "Ann", false) == ZXM_E_STATE &&
              poultice_set_item(&PLT, 1, 450, 2, "Dressings", false) == ZXM_E_STATE &&
              poultice_set_field(&PLT, 2, STAMP, "Allergy: penicillin", false) == ZXM_E_STATE &&
              sent == before && PLT.nmed == 0 && PLT.ncarer == 0 && PLT.patient == 1 &&
              !PLT.range[POULTICE_HEART].set,
          "poultice: without the owner's word nothing is sent and nothing kept");
    CHECK(poultice_set_patient(&PLT, 7, true) == ZXM_OK &&
              poultice_set_med(&PLT, &para, true) == ZXM_OK &&
              poultice_set_med(&PLT, &jab, true) == ZXM_OK &&
              poultice_set_range(&PLT, POULTICE_HEART, 50, 110, true) == ZXM_OK &&
              poultice_add_carer(&PLT, 1, 7, STAMP, "Ann", true) == ZXM_OK &&
              poultice_add_carer(&PLT, 2, INT64_C(4294967295), STAMP, "Ben", true) == ZXM_OK &&
              poultice_add_carer(&PLT, 2, 9, STAMP, "Cal", true) == ZXM_E_ARG &&
              poultice_set_item(&PLT, 1, 450, 2, "Dressings", true) == ZXM_OK &&
              poultice_set_item(&PLT, 2, 12, 10, "Gloves", true) == ZXM_OK &&
              poultice_set_field(&PLT, 2, STAMP, "Allergy: penicillin", true) == ZXM_OK &&
              PLT.nmed == 2 && PLT.ncarer == 2 && PLT.nitem == 2 && PLT.patient == 7 &&
              !strcmp(PLT.field[2].text, "Allergy: penicillin"),
          "poultice: the plan as the clinician gave it, entered by a carer: %s", LINK.err);
    CHECK(plt_file_has("poultice 1 7\nM\t1\t1000000\t4\t240\t20748\t0\t0\t20740\tParacetamol\n") &&
              plt_file_has("V\t1\t50\t110\n") && plt_file_has("C\t2\t4294967295\t") &&
              plt_file_has("D\t2\t1792627200000\tAllergy: penicillin\n"),
          "poultice: the plan in the Hoard (PM1)");

    /* PM3: doses measured against the limits, never changed */
    CHECK(poultice_dose_check(&PLT, 1, 1000000, DAY, 480, m) == LPRES_STATE_TRUE && m[0] == 0 &&
              m[1] == 3 && m[2] == POULTICE_WAIT_MAX &&
              poultice_dose_check(&PLT, 9, 1, DAY, 480, m) == LPRES_STATE_NEITHER &&
              poultice_dose_check(&PLT, 1, 0, DAY, 480, m) == LPRES_STATE_NEITHER &&
              poultice_dose_check(&PLT, 1, 1, DAY, 1440, m) == LPRES_STATE_NEITHER,
          "poultice: the first dose is within; an unknown medicine says nothing");
    CHECK(poultice_log_entry(&PLT, POULTICE_DOSE, DAY, 480, 1, 1000000, 1, true) == ZXM_OK &&
              PLT.nescalate == 0,
          "poultice: a dose given at 08:00: %s", LINK.err);
    CHECK(poultice_dose_check(&PLT, 1, 1000000, DAY, 600, m) == LPRES_STATE_FALSE && m[2] == -120 &&
              poultice_dose_check(&PLT, 1, 1000001, DAY, 720, m) == LPRES_STATE_FALSE &&
              m[0] == -1 && poultice_dose_check(&PLT, 1, 500000, DAY, 720, m) == LPRES_STATE_TRUE &&
              m[0] == 500000 && m[1] == 2 && m[2] == 0,
          "poultice: too soon, too much, then within");
    CHECK(poultice_log_entry(&PLT, POULTICE_DOSE, DAY, 720, 1, 500000, 2, true) == ZXM_OK &&
              poultice_log_entry(&PLT, POULTICE_DOSE, DAY, 960, 1, 1000000, 1, true) == ZXM_OK &&
              poultice_log_entry(&PLT, POULTICE_DOSE, DAY, 1200, 1, 1000000, 2, true) == ZXM_OK &&
              PLT.nescalate == 0 &&
              poultice_dose_check(&PLT, 1, 1000000, DAY + 1, 0, m) == LPRES_STATE_FALSE &&
              m[1] == -1 && m[2] == 0,
          "poultice: a fifth dose inside 24 hours passes the daily maximum");
    CHECK(poultice_log_entry(&PLT, POULTICE_DOSE, DAY + 1, 0, 1, 1000000, 1, true) == ZXM_OK &&
              PLT.nescalate == 2 && strstr(PLT.escalate[0], "To Ann: Paracetamol 1000000") &&
              strstr(PLT.escalate[1], "To Ben: ") && strstr(PLT.escalate[1], "outside") &&
              PLT.med[0].perdose == 1000000 && PLT.med[0].daymax == 4,
          "poultice: given anyway, it is logged as given, every carer told, no limit changed: "
          "%s",
          PLT.escalate[0]);

    /* PM5, the BOTH test: Ann and Ben log heart rate 72 and 128 for 10:00 */
    CHECK(poultice_log_entry(&PLT, POULTICE_VITAL, DAY, 600, POULTICE_HEART, 72, 1, true) ==
                  ZXM_OK &&
              PLT.nescalate == 0 &&
              poultice_reading(&PLT, POULTICE_HEART, DAY, 600) == LPRES_STATE_TRUE,
          "poultice: one reading of 72 is TRUE: %s", LINK.err);
    before = sent;
    uint32_t n0 = PLT.nlog;
    CHECK(poultice_log_entry(&PLT, POULTICE_VITAL, DAY, 600, POULTICE_HEART, 128, 2, false) ==
                  ZXM_E_STATE &&
              sent == before && PLT.nlog == n0,
          "poultice: a reading without the owner's word: nothing sent, nothing kept");
    CHECK(poultice_log_entry(&PLT, POULTICE_VITAL, DAY, 600, POULTICE_HEART, 128, 2, true) ==
                  ZXM_OK &&
              PLT.nlog == n0 + 1u &&
              poultice_reading(&PLT, POULTICE_HEART, DAY, 600) == LPRES_STATE_BOTH &&
              PLT.nescalate == PLT.ncarer && strstr(PLT.escalate[0], "To Ann: heart rate 128") &&
              strstr(PLT.escalate[1], "To Ben: ") && strstr(PLT.escalate[1], "both are kept"),
          "poultice: 72 and 128 for one minute: BOTH, both kept, every carer told: %s",
          PLT.escalate[0]);
    CHECK(plt_file_has("L\t3\t20748\t600\t1\t72\t1\n") &&
              plt_file_has("L\t3\t20748\t600\t1\t128\t2\n"),
          "poultice: neither reading is dropped from the Hoard");
    CHECK(poultice_log_entry(&PLT, POULTICE_VITAL, DAY, 600, POULTICE_HEART, 99, 2, true) ==
                  ZXM_E_ARG &&
              poultice_log_entry(&PLT, POULTICE_VITAL, DAY, 600, POULTICE_HEART, 99, 3, true) ==
                  ZXM_E_ARG &&
              poultice_log_entry(&PLT, POULTICE_VITAL, DAY, 1440, POULTICE_HEART, 99, 1, true) ==
                  ZXM_E_ARG &&
              poultice_log_entry(&PLT, POULTICE_VITAL, DAY, 601, 9, 99, 1, true) == ZXM_E_ARG &&
              poultice_log_entry(&PLT, POULTICE_VITAL, DAY, 601, 1, 1001, 1, true) == ZXM_E_ARG &&
              poultice_log_entry(&PLT, POULTICE_SYMPTOM, DAY, 601, 3, 11, 1, true) == ZXM_E_ARG &&
              poultice_log_entry(&PLT, POULTICE_DOSE, DAY, 601, 9, 1, 1, true) == ZXM_E_ARG &&
              poultice_log_entry(&PLT, 4, DAY, 601, 1, 1, 1, true) == ZXM_E_ARG &&
              PLT.nlog == n0 + 1u,
          "poultice: a second node for one carer and minute, an unknown carer and entries that "
          "do not fit are refused");
    poultice_init(&PLT2, &LINK);
    CHECK(poultice_load(&PLT2) == ZXM_OK && PLT2.nlog == PLT.nlog && PLT2.patient == 7 &&
              PLT2.nmed == 2 && PLT2.ncarer == 2 && PLT2.nitem == 2 && PLT2.field[2].set &&
              poultice_reading(&PLT2, POULTICE_HEART, DAY, 600) == LPRES_STATE_BOTH,
          "poultice: read back, both readings are still there and still BOTH: %s", LINK.err);

    /* a reading inside, then outside, the declared range (wellspring.band) */
    CHECK(poultice_log_entry(&PLT, POULTICE_VITAL, DAY + 1, 600, POULTICE_HEART, 64, 1, true) ==
                  ZXM_OK &&
              PLT.nescalate == 0 &&
              poultice_log_entry(&PLT, POULTICE_VITAL, DAY + 2, 600, POULTICE_HEART, 130, 1,
                                 true) == ZXM_OK &&
              PLT.nescalate == 2 && strstr(PLT.escalate[1], "outside the plan's declared range"),
          "poultice: 64 is inside 50..110; 130 is not, every carer told");

    /* PM7: measures over the parts */
    CHECK(poultice_vital_week(&PLT, POULTICE_HEART, DAY, span) == ZXM_OK && span[0] == 64 &&
              span[1] == 130 && span[2] == 98 &&
              poultice_vital_week(&PLT, POULTICE_TEMP, DAY, span) == ZXM_E_STATE &&
              poultice_vital_week(&PLT, 0, DAY, span) == ZXM_E_ARG,
          "poultice: the week's heart rate, 64 to 130, mean 98 (weatherglass.week_temps)");
    CHECK(poultice_log_entry(&PLT, POULTICE_SYMPTOM, DAY, 500, 3, 4, 1, true) == ZXM_OK &&
              poultice_log_entry(&PLT, POULTICE_SYMPTOM, DAY, 510, 3, 5, 2, true) == ZXM_OK &&
              poultice_log_entry(&PLT, POULTICE_SYMPTOM, DAY + 1, 500, 3, 2, 1, true) == ZXM_OK &&
              poultice_log_entry(&PLT, POULTICE_SYMPTOM, DAY, 520, 7, 1, 1, true) == ZXM_OK &&
              poultice_symptom_cell(&PLT, 0) == 2 && poultice_symptom_cell(&PLT, 1) == 2 &&
              poultice_symptom_cell(&PLT, 2) == 1 && poultice_symptom_cell(&PLT, 3) == 1 &&
              poultice_symptom_cell(&PLT, 4) == POULTICE_REFUSED,
          "poultice: the symptom grid, symptom by day (cartulary.class_year_cell)");
    CHECK(poultice_dose_week(&PLT, 1, DAY, &v) == ZXM_OK && v == 4500000 &&
              poultice_dose_week(&PLT, 2, DAY, &v) == ZXM_OK && v == 0 &&
              poultice_dose_week(&PLT, 9, DAY, &v) == ZXM_E_ARG,
          "poultice: a week's paracetamol, exact (wellspring.week)");
    CHECK(poultice_month_total(&PLT, 2026, 10, &v) == ZXM_OK && v == 31620 &&
              poultice_month_total(&PLT, 2024, 2, &v) == ZXM_OK && v == 29580 &&
              poultice_month_total(&PLT, 2026, 13, &v) == ZXM_E_ARG,
          "poultice: the month's care costs in minor units (solstice.month_days, wellspring.week)");

    /* PM6: times over the parts */
    CHECK(poultice_next_dose_day(&PLT, 2, DAY) == DAY + 4 &&
              poultice_next_dose_day(&PLT, 2, DAY + 5) == DAY + 11 &&
              poultice_next_dose_day(&PLT, 1, DAY) == POULTICE_REFUSED &&
              poultice_review_overdue(&PLT, 1, DAY) && !poultice_review_overdue(&PLT, 1, DAY - 8) &&
              !poultice_review_overdue(&PLT, 2, DAY),
          "poultice: the weekly dose falls on Saturday, moved to Monday; a review 8 days late");
    CHECK(poultice_windows_clash(&PLT, 1, DAY * 1440 + 480, 2, DAY * 1440 + 600) &&
              !poultice_windows_clash(&PLT, 1, DAY * 1440 + 480, 2, DAY * 1440 + 720) &&
              !poultice_windows_clash(&PLT, 1, DAY * 1440 + 480, 9, DAY * 1440 + 600),
          "poultice: two dose windows overlap (solstice.events_overlap)");

    /* PM8: the notices */
    static zxvd_host_ops_t ops;
    ops = OPS;
    ops.notes = plt_notes;
    ops.notes_read = plt_read;
    zxvd_rpc_set_ops(&ops);
    PLT_NOTES = "{\"unread\":3,\"notes\":[" PLTN(3, false, "poultice") "," PLTN(
        2, false, "poultice") "," PLTN(1, true, "poultice") "," PLTN(4, false, "bowline") "]}";
    CHECK(poultice_notices(&PLT) == ZXM_OK && PLT.unread == 3 && PLT.ours == 2,
          "poultice: three unread, two of them Poultice's: %s", LINK.err);
    PLT_NOTES = "{\"unread\":1,\"notes\":{}}";
    CHECK(poultice_notices(&PLT) == ZXM_E_PROTOCOL && PLT.unread == 3 && PLT.ours == 2,
          "poultice: notes that are not a list: refused, nothing changes");
    before = sent;
    plt_marked = 3;
    CHECK(poultice_mark_read(&PLT, &marked, false) == ZXM_E_STATE && sent == before &&
              PLT.unread == 3,
          "poultice: marking read without the owner's word: nothing sent");
    CHECK(poultice_mark_read(&PLT, &marked, true) == ZXM_OK && marked == 3 && PLT.unread == 0 &&
              PLT.ours == 0,
          "poultice: every notice marked read on the owner's word: %s", LINK.err);
    zxvd_rpc_set_ops(&OPS);
    CHECK(poultice_notices(&PLT) == ZXM_E_REMOTE && LINK.err_code == ZXVD_E_METHOD,
          "poultice: notes.list not available: %s", LINK.err);

    /* PM1: a plan that does not read is never written over */
    CHECK(plt_write("poultice 1 7\nX\tnot a line\n") && poultice_load(&PLT2) == ZXM_E_PROTOCOL &&
              !PLT2.loaded &&
              poultice_log_entry(&PLT2, POULTICE_SYMPTOM, DAY, 1, 3, 1, 1, true) == ZXM_E_STATE &&
              plt_file_has("X\tnot a line\n"),
          "poultice: a broken plan is refused and left as it is");
}
