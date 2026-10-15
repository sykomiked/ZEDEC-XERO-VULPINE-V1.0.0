/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zxv_thermal_host.c — zxv_thermal_host.c (T1-T7) against a Linux-style
 * /sys fixture on any POSIX host: the classes, labels, missing, garbled,
 * oversized, unreadable and absurd files, the sort, the bounds and a host
 * with no sensor; reports are compared exactly with literal JSON.
 * `test_zxv_thermal_host live` prints this machine's report instead. */
#define _XOPEN_SOURCE 700 /* nftw, mkdtemp */
#define _DARWIN_C_SOURCE
#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "zxv_thermal_host.h"

static int fails, checks;

#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            fails++;                                                                               \
            printf("  FAIL %s:%d ", __FILE__, __LINE__);                                           \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

static char R[256];
static zth_report_t REP;
static char JS[65536];

static void mk(const char *rel)
{
    char p[1024], *s;
    snprintf(p, sizeof p, "%s/%s", R, rel);
    for (s = p + strlen(R) + 1; (s = strchr(s, '/')) != NULL; s++) {
        *s = 0;
        mkdir(p, 0700);
        *s = '/';
    }
    mkdir(p, 0700);
}

/* text exactly as given (no newline added) */
static void put(const char *rel, const char *text)
{
    char p[1024], d[512];
    snprintf(p, sizeof p, "%s/%s", R, rel);
    snprintf(d, sizeof d, "%s", rel);
    char *slash = strrchr(d, '/');
    if (slash) {
        *slash = 0;
        mk(d);
    }
    FILE *f = fopen(p, "wb");
    if (!f) {
        printf("cannot write %s\n", p);
        exit(2);
    }
    fputs(text, f);
    fclose(f);
}

static int rm_one(const char *path, const struct stat *st, int flag, struct FTW *ftw)
{
    (void) st, (void) flag, (void) ftw;
    return remove(path);
}

static void wipe(void)
{
    if (R[0] && nftw(R, rm_one, 16, FTW_DEPTH | FTW_PHYS) != 0)
        printf("  (could not remove %s)\n", R);
}

static void fresh(void)
{
    wipe();
    snprintf(R, sizeof R, "%s/zxv_thermal_XXXXXX", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
    if (!mkdtemp(R)) exit(2);
}

static const char *render(void)
{
    int n = zth_render_json(&REP, JS, sizeof JS);
    CHECK(n > 0 && (size_t) n == strlen(JS), "render %d", n);
    return JS;
}

static void expect_json(const char *want, int line)
{
    const char *got = render();
    checks++;
    if (strcmp(got, want)) {
        fails++;
        printf("  FAIL line %d\n    want %s\n    got  %s\n", line, want, got);
    }
}
#define EXPECT_JSON(want) expect_json(want, __LINE__)

/* ----- T2 ----- */

static void test_classify(void)
{
    static const struct {
        const char *driver, *label;
        zth_class_t want;
    } T[] = {
        {"coretemp", "", ZTH_CPU},
        {"k10temp", "", ZTH_CPU},
        {"k8temp", NULL, ZTH_CPU},
        {"zenpower", "", ZTH_CPU},
        {"x86_pkg_temp", "", ZTH_CPU},
        {"via_cputemp", "", ZTH_CPU},
        {"cpu_thermal", "", ZTH_CPU},
        {"cpu-thermal", "", ZTH_CPU},
        {"soc_thermal", "", ZTH_CPU},
        {"tcpu", "", ZTH_CPU},
        {"tcpux", "", ZTH_OTHER}, /* tcpu is a whole name */
        {"amdgpu", "edge", ZTH_GPU},
        {"nouveau", "", ZTH_GPU},
        {"radeon", "", ZTH_GPU},
        {"i915", "", ZTH_GPU},
        {"xe", "", ZTH_GPU},
        {"xen", "", ZTH_OTHER}, /* xe is a whole name */
        {"gpu_thermal", "", ZTH_GPU},
        {"acpitz", "", ZTH_CASE},
        {"nct6798", "", ZTH_CASE},
        {"it8686", "", ZTH_CASE},
        {"w83627ehf", "", ZTH_CASE},
        {"f71882fg", "", ZTH_CASE},
        {"pch_cannonlake", "", ZTH_CASE},
        {"board_temp", "", ZTH_CASE},
        {"dell_smm", "", ZTH_CASE},
        {"thinkpad", "", ZTH_CASE},
        {"asus_wmi_sensors", "", ZTH_CASE},
        {"nvme", "Composite", ZTH_OTHER},
        {"iwlwifi_1", "", ZTH_OTHER},
        {NULL, NULL, ZTH_OTHER},
        {"", "", ZTH_OTHER},
        /* a label wins over the driver, without case */
        {"nct6798", "CPUTIN", ZTH_CPU},
        {"nct6798", "SYSTIN", ZTH_CASE},
        {"nct6798", "AUXTIN0", ZTH_CASE},
        {"thinkpad", "GPU", ZTH_GPU},
        {"coretemp", "Package id 0", ZTH_CPU},
        {"coretemp", "Core 3", ZTH_CPU},
        {"k10temp", "Tctl", ZTH_CPU},
        {"k10temp", "Tdie", ZTH_CPU},
        {"k10temp", "Tccd1", ZTH_CPU},
        {"iohid", "pACC MTR Temp Sensor2", ZTH_CPU},
        {"iohid", "eACC MTR Temp Sensor0", ZTH_CPU},
        {"iohid", "SOC MTR Temp Sensor0", ZTH_CPU},
        {"iohid", "PMU tdie1", ZTH_CPU},
        {"iohid", "GPU MTR Temp Sensor1", ZTH_GPU},
        {"iohid", "NAND CH0 temp", ZTH_OTHER},
        {"iohid", "gas gauge battery", ZTH_OTHER},
        {"smc", "TA0P ambient", ZTH_CASE},
        {"smc", "Tm0P mainboard", ZTH_CASE},
        {"other", "Case Fan Area", ZTH_CASE},
        {"other", "PCH", ZTH_CASE},
        {"other", "Chipset", ZTH_CASE},
        {"amdgpu", "x", ZTH_GPU},
        {"coretemp", "c", ZTH_CPU}, /* a one-letter label is no word */
    };
    for (size_t i = 0; i < sizeof T / sizeof T[0]; i++) {
        zth_class_t got = zth_classify(T[i].driver, T[i].label);
        CHECK(got == T[i].want, "T2: %s / %s is %s, not %s", T[i].driver ? T[i].driver : "NULL",
              T[i].label ? T[i].label : "NULL", zth_class_name(got), zth_class_name(T[i].want));
    }
    CHECK(!strcmp(zth_class_name(ZTH_CPU), "cpu") && !strcmp(zth_class_name(ZTH_GPU), "gpu") &&
              !strcmp(zth_class_name(ZTH_CASE), "case") &&
              !strcmp(zth_class_name(ZTH_OTHER), "other") &&
              !strcmp(zth_class_name(ZTH_NCLASS), "other") &&
              !strcmp(zth_class_name((zth_class_t) 99), "other"),
          "class names");
}

/* ----- T3 ----- */

static void test_parse(void)
{
    static const struct {
        const char *text;
        bool ok;
        int32_t want;
    } T[] = {
        {"45000\n", true, 45000},
        {"45000", true, 45000},
        {"-5000\n", true, -5000},
        {"0\n", true, 0},
        {"-0", true, 0},
        {"123456789", true, 123456789},
        {"-123456789\n", true, -123456789},
        {"1234567890", false, 0}, /* 10 digits */
        {"", false, 0},
        {"\n", false, 0},
        {"-", false, 0},
        {"-\n", false, 0},
        {"45000\n\n", false, 0},
        {" 45000", false, 0},
        {"45000 ", false, 0},
        {"+45000", false, 0},
        {"4a5", false, 0},
        {"45000\r\n", false, 0},
        {"--5", false, 0},
        {"5-", false, 0},
        {"9/", false, 0},
        {"9:", false, 0},
    };
    for (size_t i = 0; i < sizeof T / sizeof T[0]; i++) {
        int32_t v = 77;
        bool ok = zth_parse_mdeg(T[i].text, strlen(T[i].text), &v);
        CHECK(ok == T[i].ok && (!ok || v == T[i].want), "T3: \"%s\" -> %d %ld", T[i].text, ok,
              (long) v);
        if (!ok) CHECK(v == 77, "a refused value leaves *mdeg alone (\"%s\")", T[i].text);
    }
    /* n counts: an embedded NUL is not a digit */
    int32_t v;
    CHECK(!zth_parse_mdeg("12\0003", 4, &v), "a NUL inside");
    CHECK(zth_parse_mdeg("123", 2, &v) && v == 12, "only n bytes are read");
    CHECK(zth_in_range(-50000) && zth_in_range(150000) && zth_in_range(0), "T3: the edges are in");
    CHECK(!zth_in_range(-50001) && !zth_in_range(150001) && !zth_in_range(INT32_MAX) &&
              !zth_in_range(INT32_MIN),
          "T3: past them is out");
}

/* ----- T1-T7 on a fixture ----- */

static void build_machine(void)
{
    /* coretemp: package and a core, listed after hwmon2 and before hwmon10 */
    put("sys/class/hwmon/hwmon3/name", "coretemp\n");
    put("sys/class/hwmon/hwmon3/temp1_label", "Package id 0\n");
    put("sys/class/hwmon/hwmon3/temp1_input", "52000\n");
    put("sys/class/hwmon/hwmon3/temp2_label", "Core 0\n");
    put("sys/class/hwmon/hwmon3/temp2_input", "48000\n");
    put("sys/class/hwmon/hwmon3/temp1_max", "100000\n");   /* not an input */
    put("sys/class/hwmon/hwmon3/temp7_label", "Core 6\n"); /* a label with no value */
    /* amdgpu: edge, and an absurd junction */
    put("sys/class/hwmon/hwmon2/name", "amdgpu\n");
    put("sys/class/hwmon/hwmon2/temp1_label", "edge\n");
    put("sys/class/hwmon/hwmon2/temp1_input", "61000\n");
    put("sys/class/hwmon/hwmon2/temp2_label", "junction\n");
    put("sys/class/hwmon/hwmon2/temp2_input", "200000\n");
    /* the board chip: SYSTIN, CPUTIN by label, a garbled one, an unreadable one */
    put("sys/class/hwmon/hwmon10/name", "nct6798\n");
    put("sys/class/hwmon/hwmon10/temp1_label", "SYSTIN\n");
    put("sys/class/hwmon/hwmon10/temp1_input", "34000\n");
    put("sys/class/hwmon/hwmon10/temp2_label", "CPUTIN\n");
    put("sys/class/hwmon/hwmon10/temp2_input", "41000\n");
    put("sys/class/hwmon/hwmon10/temp3_input", "abc\n");
    mk("sys/class/hwmon/hwmon10/temp4_input"); /* a directory: the read fails */
    put("sys/class/hwmon/hwmon10/temp5_input", "1234567890123456789012345678901234567890\n");
    put("sys/class/hwmon/hwmon10/temp6_input", "-50001\n");
    put("sys/class/hwmon/hwmon10/temp8_input", "-50000\n");
    /* no name file; a label with a control byte is dropped */
    put("sys/class/hwmon/hwmon4/temp1_input", "39000\n");
    put("sys/class/hwmon/hwmon4/temp1_label", "bad\tlabel\n");
    /* names that are not followed */
    put("sys/class/hwmon/hwmonX/temp1_input", "1000\n");
    put("sys/class/hwmon/hwmon5x/temp1_input", "1000\n");
    put("sys/class/hwmon/hwmon6/tempA_input", "1000\n");
    put("sys/class/hwmon/hwmon6/temp_input", "1000\n");
    put("sys/class/hwmon/hwmon6/temp1_inputs", "1000\n");
    put("sys/class/hwmon/hwmon6/xtemp1_input", "1000\n");
    put("sys/class/hwmon/hwmon6/name", "unused\n");
    /* thermal zones */
    put("sys/class/thermal/thermal_zone0/type", "acpitz\n");
    put("sys/class/thermal/thermal_zone0/temp", "27800\n");
    put("sys/class/thermal/thermal_zone1/type", "x86_pkg_temp\n");
    put("sys/class/thermal/thermal_zone1/temp", "53000\n");
    put("sys/class/thermal/thermal_zone2/type", "INT3400 Thermal\n"); /* no temp: no reading */
    put("sys/class/thermal/cooling_device0/type", "Processor\n");
    put("sys/class/thermal/thermal_zone11/type", "iwlwifi_1\n");
    put("sys/class/thermal/thermal_zone11/temp", "150000\n");
}

static void test_machine(void)
{
    fresh();
    build_machine();
    CHECK(zth_read(R, &REP) == LPRES_STATE_BOTH, "T4: some refused, some not: BOTH");
    EXPECT_JSON(
        "{\"state\":\"both\",\"host\":\"linux\",\"reason\":null,\"classes\":{"
        "\"cpu\":{\"state\":\"true\",\"n\":4,\"max_mdeg\":53000},"
        "\"gpu\":{\"state\":\"both\",\"n\":1,\"max_mdeg\":61000},"
        "\"case\":{\"state\":\"both\",\"n\":3,\"max_mdeg\":34000},"
        "\"other\":{\"state\":\"true\",\"n\":2,\"max_mdeg\":150000}},\"readings\":["
        "{\"class\":\"gpu\",\"state\":\"true\",\"mdeg\":61000,\"source\":\"hwmon\",\"driver\":"
        "\"amdgpu\",\"label\":\"edge\",\"path\":\"/sys/class/hwmon/hwmon2/temp1_input\","
        "\"reason\":null},"
        "{\"class\":\"gpu\",\"state\":\"false\",\"mdeg\":null,\"source\":\"hwmon\",\"driver\":"
        "\"amdgpu\",\"label\":\"junction\",\"path\":\"/sys/class/hwmon/hwmon2/temp2_input\","
        "\"reason\":\"out of range\"},"
        "{\"class\":\"cpu\",\"state\":\"true\",\"mdeg\":52000,\"source\":\"hwmon\",\"driver\":"
        "\"coretemp\",\"label\":\"Package id 0\",\"path\":\"/sys/class/hwmon/hwmon3/temp1_input\","
        "\"reason\":null},"
        "{\"class\":\"cpu\",\"state\":\"true\",\"mdeg\":48000,\"source\":\"hwmon\",\"driver\":"
        "\"coretemp\",\"label\":\"Core 0\",\"path\":\"/sys/class/hwmon/hwmon3/temp2_input\","
        "\"reason\":null},"
        "{\"class\":\"other\",\"state\":\"true\",\"mdeg\":39000,\"source\":\"hwmon\",\"driver\":"
        "\"\",\"label\":\"\",\"path\":\"/sys/class/hwmon/hwmon4/temp1_input\",\"reason\":null},"
        "{\"class\":\"case\",\"state\":\"true\",\"mdeg\":34000,\"source\":\"hwmon\",\"driver\":"
        "\"nct6798\",\"label\":\"SYSTIN\",\"path\":\"/sys/class/hwmon/hwmon10/temp1_input\","
        "\"reason\":null},"
        "{\"class\":\"cpu\",\"state\":\"true\",\"mdeg\":41000,\"source\":\"hwmon\",\"driver\":"
        "\"nct6798\",\"label\":\"CPUTIN\",\"path\":\"/sys/class/hwmon/hwmon10/temp2_input\","
        "\"reason\":null},"
        "{\"class\":\"case\",\"state\":\"false\",\"mdeg\":null,\"source\":\"hwmon\",\"driver\":"
        "\"nct6798\",\"label\":\"\",\"path\":\"/sys/class/hwmon/hwmon10/temp3_input\","
        "\"reason\":\"not an integer\"},"
        "{\"class\":\"case\",\"state\":\"false\",\"mdeg\":null,\"source\":\"hwmon\",\"driver\":"
        "\"nct6798\",\"label\":\"\",\"path\":\"/sys/class/hwmon/hwmon10/temp4_input\","
        "\"reason\":\"unreadable\"},"
        "{\"class\":\"case\",\"state\":\"false\",\"mdeg\":null,\"source\":\"hwmon\",\"driver\":"
        "\"nct6798\",\"label\":\"\",\"path\":\"/sys/class/hwmon/hwmon10/temp5_input\","
        "\"reason\":\"too long\"},"
        "{\"class\":\"case\",\"state\":\"false\",\"mdeg\":null,\"source\":\"hwmon\",\"driver\":"
        "\"nct6798\",\"label\":\"\",\"path\":\"/sys/class/hwmon/hwmon10/temp6_input\","
        "\"reason\":\"out of range\"},"
        "{\"class\":\"case\",\"state\":\"true\",\"mdeg\":-50000,\"source\":\"hwmon\",\"driver\":"
        "\"nct6798\",\"label\":\"\",\"path\":\"/sys/class/hwmon/hwmon10/temp8_input\","
        "\"reason\":null},"
        "{\"class\":\"case\",\"state\":\"true\",\"mdeg\":27800,\"source\":\"thermal_zone\","
        "\"driver\":\"acpitz\",\"label\":\"\",\"path\":\"/sys/class/thermal/thermal_zone0/temp\","
        "\"reason\":null},"
        "{\"class\":\"cpu\",\"state\":\"true\",\"mdeg\":53000,\"source\":\"thermal_zone\","
        "\"driver\":\"x86_pkg_temp\",\"label\":\"\",\"path\":"
        "\"/sys/class/thermal/thermal_zone1/temp\",\"reason\":null},"
        "{\"class\":\"other\",\"state\":\"true\",\"mdeg\":150000,\"source\":\"thermal_zone\","
        "\"driver\":\"iwlwifi_1\",\"label\":\"\",\"path\":"
        "\"/sys/class/thermal/thermal_zone11/temp\",\"reason\":null}"
        "],\"rejected\":5,\"dropped\":0}");
}

/* ----- T5: no sensor, nothing accepted ----- */

static const char *EMPTY_CLASSES =
    "\"classes\":{\"cpu\":{\"state\":\"neither\",\"n\":0,\"max_mdeg\":null},"
    "\"gpu\":{\"state\":\"neither\",\"n\":0,\"max_mdeg\":null},"
    "\"case\":{\"state\":\"neither\",\"n\":0,\"max_mdeg\":null},"
    "\"other\":{\"state\":\"neither\",\"n\":0,\"max_mdeg\":null}}";

static void test_none(void)
{
    char want[2048];
    fresh();
    CHECK(zth_read(R, &REP) == LPRES_STATE_NEITHER, "T5: an empty root is NEITHER");
    snprintf(want, sizeof want,
             "{\"state\":\"neither\",\"host\":\"linux\",\"reason\":\"no temperature sensor under "
             "/sys/class/hwmon or /sys/class/thermal\",%s,\"readings\":[],\"rejected\":0,"
             "\"dropped\":0}",
             EMPTY_CLASSES);
    EXPECT_JSON(want);
    /* directories, but no value in them */
    mk("sys/class/hwmon/hwmon0");
    put("sys/class/hwmon/hwmon0/name", "coretemp\n");
    put("sys/class/thermal/thermal_zone0/type", "acpitz\n");
    CHECK(zth_read(R, &REP) == LPRES_STATE_NEITHER && REP.n == 0 && REP.reason[0],
          "T5: devices with no value are NEITHER");
    /* every reading refused: FALSE, no reason, no max */
    put("sys/class/hwmon/hwmon0/temp1_input", "-273150\n");
    put("sys/class/thermal/thermal_zone0/temp", "hot\n");
    CHECK(zth_read(R, &REP) == LPRES_STATE_FALSE, "T4: all refused is FALSE");
    snprintf(want, sizeof want,
             "{\"state\":\"false\",\"host\":\"linux\",\"reason\":null,\"classes\":{"
             "\"cpu\":{\"state\":\"false\",\"n\":0,\"max_mdeg\":null},"
             "\"gpu\":{\"state\":\"neither\",\"n\":0,\"max_mdeg\":null},"
             "\"case\":{\"state\":\"false\",\"n\":0,\"max_mdeg\":null},"
             "\"other\":{\"state\":\"neither\",\"n\":0,\"max_mdeg\":null}},\"readings\":["
             "{\"class\":\"cpu\",\"state\":\"false\",\"mdeg\":null,\"source\":\"hwmon\","
             "\"driver\":\"coretemp\",\"label\":\"\",\"path\":"
             "\"/sys/class/hwmon/hwmon0/temp1_input\",\"reason\":\"out of range\"},"
             "{\"class\":\"case\",\"state\":\"false\",\"mdeg\":null,\"source\":\"thermal_zone\","
             "\"driver\":\"acpitz\",\"label\":\"\",\"path\":"
             "\"/sys/class/thermal/thermal_zone0/temp\",\"reason\":\"not an integer\"}],"
             "\"rejected\":2,\"dropped\":0}");
    EXPECT_JSON(want);
    /* a single good one: TRUE; a negative maximum is kept */
    put("sys/class/hwmon/hwmon0/temp1_input", "-12000\n");
    put("sys/class/thermal/thermal_zone0/temp", "-11000");
    CHECK(zth_read(R, &REP) == LPRES_STATE_TRUE && REP.cls[ZTH_CPU].max_mdeg == -12000 &&
              REP.cls[ZTH_CASE].max_mdeg == -11000 && REP.cls[ZTH_CPU].n == 1 &&
              REP.cls[ZTH_CPU].state == LPRES_STATE_TRUE && REP.rejected == 0,
          "T4: all accepted is TRUE");
    put("sys/class/hwmon/hwmon0/temp2_input", "-13000\n");
    put("sys/class/hwmon/hwmon0/temp3_input", "-1000\n");
    put("sys/class/hwmon/hwmon0/temp4_input", "-1000\n");
    CHECK(zth_read(R, &REP) == LPRES_STATE_TRUE && REP.cls[ZTH_CPU].max_mdeg == -1000 &&
              REP.cls[ZTH_CPU].n == 4,
          "the highest of several below zero (%ld)", (long) REP.cls[ZTH_CPU].max_mdeg);
    /* NULL and "" read this machine: the same answer, from the Linux reader on Linux */
    static zth_report_t live;
    lpres_state_t a = zth_read(NULL, &live);
    CHECK(zth_read("", &REP) == a && REP.n == live.n && !strcmp(REP.reason, live.reason) &&
              !strcmp(REP.host, live.host),
          "NULL and \"\" are this machine (%s, %u readings)", live.host, (unsigned) live.n);
#if defined(__linux__)
    CHECK(!strcmp(live.host, "linux") &&
              (live.n || !strcmp(live.reason, "no temperature sensor "
                                              "under /sys/class/hwmon or "
                                              "/sys/class/thermal")),
          "this Linux machine: %s", live.reason);
#endif
    /* a root that is not there, and one too long */
    CHECK(zth_read("/nonexistent/zxv-thermal", &REP) == LPRES_STATE_NEITHER && REP.reason[0],
          "a missing root");
    char lng[ZTH_ROOT_MAX + 8];
    memset(lng, 'a', sizeof lng - 1);
    lng[0] = '/';
    lng[sizeof lng - 1] = 0;
    CHECK(zth_read(lng, &REP) == LPRES_STATE_NEITHER &&
              !strcmp(REP.reason, "the sensor root path is too long"),
          "a root too long: %s", REP.reason);
    lng[ZTH_ROOT_MAX] = 0;
    CHECK(zth_read(lng, &REP) == LPRES_STATE_NEITHER &&
              !strcmp(REP.reason, "the sensor root path is too long"),
          "a root of ZTH_ROOT_MAX bytes is too long: %s", REP.reason);
    lng[ZTH_ROOT_MAX - 1] = 0;
    CHECK(zth_read(lng, &REP) == LPRES_STATE_NEITHER &&
              !strcmp(REP.reason, "no temperature sensor under /sys/class/hwmon or "
                                  "/sys/class/thermal"),
          "the longest root is tried: %s", REP.reason);
}

/* ----- T3, T6: names, sort and bounds ----- */

static void test_bounds(void)
{
    char rel[128], val[32];
    fresh();
    /* names: cut at ZTH_NAME_MAX - 1, trailing spaces dropped, a DEL drops it */
    put("sys/class/hwmon/hwmon0/name", "a-very-long-driver-name-that-goes-past-forty-bytes\n");
    put("sys/class/hwmon/hwmon0/temp1_input", "1000\n");
    put("sys/class/hwmon/hwmon0/temp1_label", "Ambient \"x\" \\ y  \n");
    put("sys/class/hwmon/hwmon0/temp2_input", "2000\n");
    put("sys/class/hwmon/hwmon0/temp2_label", "del\x7f");
    put("sys/class/hwmon/hwmon0/temp3_input", "3000\n");
    put("sys/class/hwmon/hwmon0/temp3_label", "~ ok");
    put("sys/class/hwmon/hwmon0/temp4_input", "4000\n");
    put("sys/class/hwmon/hwmon0/temp4_label", "\x1f");
    put("sys/class/hwmon/hwmon0/temp5_input", "5000\n");
    put("sys/class/hwmon/hwmon0/temp5_label", "\x80hi");
    put("sys/class/hwmon/hwmon0/temp6_input", "6000\n");
    put("sys/class/hwmon/hwmon0/temp6_label", "  \n");
    /* the value file at the limit and one byte past it */
    char at[ZTH_VALUE_MAX + 2];
    memset(at, '0', sizeof at);
    at[ZTH_VALUE_MAX - 1] = '\n';
    at[ZTH_VALUE_MAX] = 0;
    put("sys/class/hwmon/hwmon0/temp7_input", at); /* 31 zeros: too many digits, not too long */
    at[ZTH_VALUE_MAX - 1] = '0';
    put("sys/class/hwmon/hwmon0/temp8_input", at); /* 32 bytes, no newline */
    at[ZTH_VALUE_MAX] = '\n';
    at[ZTH_VALUE_MAX + 1] = 0;
    put("sys/class/hwmon/hwmon0/temp9_input", at); /* 33 bytes */
    zth_read(R, &REP);
    CHECK(REP.n == 9, "nine readings (%u)", (unsigned) REP.n);
    CHECK(!strcmp(REP.r[0].driver, "a-very-long-driver-name-that-goes-past-") &&
              strlen(REP.r[0].driver) == ZTH_NAME_MAX - 1u,
          "T3: cut at 39 bytes: %s", REP.r[0].driver);
    CHECK(!strcmp(REP.r[0].label, "Ambient \"x\" \\ y") && REP.r[0].cls == ZTH_CASE,
          "T3: trailing spaces dropped: [%s]", REP.r[0].label);
    CHECK(!strcmp(REP.r[1].label, "") && !strcmp(REP.r[2].label, "~ ok") &&
              !strcmp(REP.r[3].label, "") && !strcmp(REP.r[4].label, "") &&
              !strcmp(REP.r[5].label, ""),
          "T3: DEL, control bytes and high bytes drop a label; ~ is kept; blanks are empty");
    CHECK(REP.r[6].reason && !strcmp(REP.r[6].reason, "not an integer"), "31 digits: %s",
          REP.r[6].reason ? REP.r[6].reason : "accepted");
    CHECK(REP.r[7].reason && !strcmp(REP.r[7].reason, "not an integer"), "32 bytes: %s",
          REP.r[7].reason ? REP.r[7].reason : "accepted");
    CHECK(REP.r[8].reason && !strcmp(REP.r[8].reason, "too long"), "33 bytes: %s",
          REP.r[8].reason ? REP.r[8].reason : "accepted");
    const char *js = render();
    CHECK(strstr(js, "\"label\":\"Ambient \\\"x\\\" \\\\ y\"") != NULL, "T7: escaped: %.300s", js);

    /* T6: digit runs compare as numbers, leading zeros aside */
    fresh();
    static const char *const ORDER[] = {"temp1_input", "temp02_input", "temp2_input",
                                        "temp3_input", "temp10_input", "temp010_input"};
    for (size_t i = 0; i < 6; i++) {
        snprintf(rel, sizeof rel, "sys/class/hwmon/hwmon1/%s", ORDER[i]);
        snprintf(val, sizeof val, "%u\n", (unsigned) (1000 + i));
        put(rel, val);
    }
    put("sys/class/hwmon/hwmon01/temp1_input", "7000\n");
    put("sys/class/hwmon/hwmon9/temp1_input", "9000\n");
    put("sys/class/hwmon/hwmon9/temp1a_input", "9000\n");
    zth_read(R, &REP);
    static const char *const WANT[] = {
        "/sys/class/hwmon/hwmon01/temp1_input", "/sys/class/hwmon/hwmon1/temp1_input",
        "/sys/class/hwmon/hwmon1/temp02_input", "/sys/class/hwmon/hwmon1/temp2_input",
        "/sys/class/hwmon/hwmon1/temp3_input",  "/sys/class/hwmon/hwmon1/temp010_input",
        "/sys/class/hwmon/hwmon1/temp10_input", "/sys/class/hwmon/hwmon9/temp1_input"};
    CHECK(REP.n == 8, "eight sorted (%u)", (unsigned) REP.n);
    for (uint32_t i = 0; i < REP.n && i < 8; i++)
        CHECK(!strcmp(REP.r[i].path, WANT[i]), "T6: #%u is %s, not %s", (unsigned) i, REP.r[i].path,
              WANT[i]);

    /* T6: at most ZTH_MAX_DEVS devices looked at */
    fresh();
    for (unsigned i = 1; i <= ZTH_MAX_DEVS + 5u; i++) {
        snprintf(rel, sizeof rel, "sys/class/hwmon/hwmon%u/temp1_input", i);
        put(rel, "30000\n");
    }
    CHECK(zth_read(R, &REP) == LPRES_STATE_TRUE, "only the first devices");
    CHECK(REP.n == ZTH_MAX_DEVS && REP.dropped == 0, "T6: %u devices looked at (%u, %u dropped)",
          ZTH_MAX_DEVS, (unsigned) REP.n, (unsigned) REP.dropped);
    /* at most ZTH_MAX_READINGS kept, the rest counted */
    fresh();
    for (unsigned i = 1; i <= ZTH_MAX_READINGS + 5u; i++) {
        snprintf(rel, sizeof rel, "sys/class/hwmon/hwmon0/temp%u_input", i);
        put(rel, "30000\n");
    }
    CHECK(zth_read(R, &REP) == LPRES_STATE_TRUE, "read");
    CHECK(REP.n == ZTH_MAX_READINGS && REP.dropped == 5, "T6: %u kept, %u dropped",
          (unsigned) REP.n, (unsigned) REP.dropped);
    js = render();
    CHECK(strstr(js, ",\"dropped\":5}") != NULL, "dropped in the JSON");
    /* the entry bound: past ZTH_MAX_ENTRIES names in a directory, the rest is not looked at */
    fresh();
    for (unsigned i = 1; i <= ZTH_MAX_ENTRIES + 20u; i++) {
        snprintf(rel, sizeof rel, "sys/class/thermal/x%u", i);
        mk(rel);
    }
    put("sys/class/thermal/thermal_zone0/temp", "30000\n");
    zth_read(R, &REP);
    CHECK(REP.n <= 1, "T6: entries past the bound may be skipped (%u)", (unsigned) REP.n);
}

/* ----- T7: the buffer ----- */

static void test_render(void)
{
    fresh();
    build_machine();
    zth_read(R, &REP);
    int n = zth_render_json(&REP, JS, sizeof JS);
    CHECK(n > 0, "renders");
    char *small = malloc((size_t) n + 1u);
    if (!small) exit(2);
    CHECK(zth_render_json(&REP, small, (size_t) n + 1u) == n && !strcmp(small, JS),
          "T7: exactly enough room");
    CHECK(zth_render_json(&REP, small, (size_t) n) == -1, "T7: one byte short is -1");
    CHECK(zth_render_json(&REP, small, 10) == -1, "T7: far short is -1");
    CHECK(zth_render_json(&REP, NULL, 100) == -1 && zth_render_json(&REP, small, 0) == -1,
          "T7: no buffer");
    free(small);
    static zth_report_t blank;
    memset(&blank, 0, sizeof blank);
    blank.state = LPRES_STATE_NEITHER;
    n = zth_render_json(&blank, JS, sizeof JS);
    static const char *BLANK = "{\"state\":\"neither\",\"host\":\"other\",\"reason\":null,";
    CHECK(n > 0 && !strncmp(JS, BLANK, strlen(BLANK)), "T7: a blank report: %s", JS);
}

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "live")) {
        lpres_state_t st = zth_read(NULL, &REP);
        int n = zth_render_json(&REP, JS, sizeof JS);
        printf("%s\n", n > 0 ? JS : "(the report does not fit)");
        return st == LPRES_STATE_NEITHER ? 3 : 0;
    }
    test_classify();
    test_parse();
    test_machine();
    test_none();
    test_bounds();
    test_render();
    wipe();
    printf("test_zxv_thermal_host: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
