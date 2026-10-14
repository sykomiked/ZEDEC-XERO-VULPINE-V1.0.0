/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zui_settings.c — the settings store (zui_settings.h S1..S10). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zui_settings.h"

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

static zui_settings_t ST, ST2;
static const zui_set_view_t NONE = {0, 0, 0};
static const char *const DENSITY[] = {"compact", "regular", "roomy"};
static const char *const SCHEME[] = {"dark", "light", "high-contrast"};

static void schema(zui_settings_t *st)
{
    zui_set_init(st);
    zui_set_def_int(st, "text.scale", 100, 80, 200, 10);
    zui_set_def_bool(st, "motion.reduced", false);
    zui_set_def_enum(st, "ui.density", DENSITY, 3, 1);
    zui_set_def_str(st, "user.name", "guest");
    zui_set_def_color(st, "ui.focus", ZUI_C_GOLD);
    zui_set_def_enum(st, "theme.scheme", SCHEME, 3, 0);
    zui_set_def_int(st, "ui.gap", -4, -8, 8, 4);
}

static void test_schema(void)
{
    zui_settings_t *st = &ST;
    zui_set_init(st);
    CHECK(zui_set_def_int(st, "text.scale", 100, 80, 200, 10) == 0, "first key is 0");
    CHECK(zui_set_def_bool(st, "motion.reduced", true) == 1, "second key is 1");
    CHECK(zui_set_def_enum(st, "ui.density", DENSITY, 3, 1) == 2, "enum");
    CHECK(zui_set_def_str(st, "user.name", "guest") == 3, "string");
    CHECK(zui_set_def_color(st, "ui.focus", ZUI_C_GOLD) == 4, "colour");
    CHECK(st->nkeys == 5, "five keys");
    CHECK(zui_set_def_int(st, "text.scale", 100, 80, 200, 10) == ZUI_SET_E_DUP, "dup refused");
    CHECK(zui_set_def_bool(st, "text.scale", true) == ZUI_SET_E_DUP, "dup of another type");
    CHECK(zui_set_def_int(st, "Text.x", 1, 0, 2, 1) == ZUI_SET_E_NAME, "capitals refused");
    CHECK(zui_set_def_int(st, "a b", 1, 0, 2, 1) == ZUI_SET_E_NAME, "space refused");
    CHECK(zui_set_def_int(st, "", 1, 0, 2, 1) == ZUI_SET_E_NAME, "empty refused");
    CHECK(zui_set_def_bool(st, "lock", true) == ZUI_SET_E_NAME, "'lock' refused");
    CHECK(zui_set_def_bool(st, "lock.screen", true) >= 0, "'lock.' prefix is fine");
    char longk[ZUI_SET_KEY_MAX + 1];
    memset(longk, 'k', sizeof longk);
    longk[ZUI_SET_KEY_MAX] = 0;
    CHECK(zui_set_def_bool(st, longk, true) == ZUI_SET_E_NAME, "too long a key");
    longk[ZUI_SET_KEY_MAX - 1u] = 0;
    CHECK(zui_set_def_bool(st, longk, true) >= 0, "KEY_MAX-1 chars fit");
    longk[ZUI_SET_KEY_MAX - 2u] = 0;
    CHECK(zui_set_key(st, longk) == ZUI_SET_E_NOKEY, "a shorter name is another key");
    CHECK(zui_set_def_int(st, "i.a", 75, 80, 200, 10) == ZUI_SET_E_RANGE, "default below min");
    CHECK(zui_set_def_int(st, "i.a", 210, 80, 200, 10) == ZUI_SET_E_RANGE, "default above max");
    CHECK(zui_set_def_int(st, "i.a", 85, 80, 200, 10) == ZUI_SET_E_RANGE, "default off step");
    CHECK(zui_set_def_int(st, "i.a", 1, 5, 4, 1) == ZUI_SET_E_RANGE, "min > max");
    CHECK(zui_set_def_int(st, "i.a", 1, 0, 4, 0) == ZUI_SET_E_RANGE, "step 0");
    CHECK(zui_set_def_int(st, "i.a", 1, 0, 4, -1) == ZUI_SET_E_RANGE, "negative step");
    CHECK(zui_set_def_int(st, "i.a", 200, 80, 200, 10) >= 0, "default at max is fine");
    CHECK(zui_set_def_int(st, "i.b", 80, 80, 200, 10) >= 0, "default at min is fine");
    CHECK(zui_set_def_enum(st, "e.a", DENSITY, 3, 3) == ZUI_SET_E_RANGE, "enum default past end");
    CHECK(zui_set_def_enum(st, "e.a", DENSITY, 0, 0) == ZUI_SET_E_RANGE, "no names");
    CHECK(zui_set_def_enum(st, "e.a", 0, 3, 0) == ZUI_SET_E_RANGE, "null names");
    static const char *const DUPN[] = {"a", "b", "a"};
    CHECK(zui_set_def_enum(st, "e.a", DUPN, 3, 0) == ZUI_SET_E_DUP, "a name twice");
    static const char *const SPN[] = {"a", "b c"};
    CHECK(zui_set_def_enum(st, "e.a", SPN, 2, 0) == ZUI_SET_E_NAME, "a name with a space");
    CHECK(zui_set_def_enum(st, "e.a", DENSITY, 3, 2) >= 0, "enum default at last name");
    CHECK(zui_set_def_color(st, "c.a", ZUI_C_COUNT) == ZUI_SET_E_RANGE, "no such role");
    CHECK(zui_set_def_color(st, "c.a", ZUI_C_ALERT) >= 0, "last role is fine");
    char longs[ZUI_SET_STR_MAX + 1];
    memset(longs, 's', sizeof longs);
    longs[ZUI_SET_STR_MAX] = 0;
    CHECK(zui_set_def_str(st, "s.a", longs) == ZUI_SET_E_RANGE, "too long a default");
    longs[ZUI_SET_STR_MAX - 1u] = 0;
    CHECK(zui_set_def_str(st, "s.a", longs) >= 0, "STR_MAX-1 chars fit");
    longs[ZUI_SET_STR_MAX - 2u] = 0;
    CHECK(zui_set_def_str(st, "s.c", longs) >= 0, "shorter too");
    CHECK(zui_set_def_str(st, "s.b", "tab\there") == ZUI_SET_E_RANGE, "control char default");
    CHECK(zui_set_key(st, "text.scale") == 0, "lookup 0");
    CHECK(zui_set_key(st, "ui.focus") == 4, "lookup 4");
    CHECK(zui_set_key(st, "ui.focu") == ZUI_SET_E_NOKEY, "a prefix is not a key");
    CHECK(zui_set_key(st, "ui.focuss") == ZUI_SET_E_NOKEY, "longer is not a key");
    CHECK(zui_set_key(st, 0) == ZUI_SET_E_NOKEY, "null key");
    CHECK(zui_set_schema(st, 0) && zui_set_schema(st, 0)->min == 80 &&
              zui_set_schema(st, 0)->max == 200 && zui_set_schema(st, 0)->step == 10,
          "schema kept");
    CHECK(zui_set_schema(st, st->nkeys) == 0, "past the end");
    zui_set_value_t v;
    CHECK(zui_set_get(st, NONE, "motion.reduced", &v) == 0 && v.i == 1, "bool default true");
    CHECK(zui_set_int(st, NONE, "ui.density") == 1, "enum default index");
    CHECK(zui_set_int(st, NONE, "ui.focus") == ZUI_C_GOLD, "colour default");
    char b[ZUI_SET_STR_MAX];
    zui_set_str(st, NONE, "user.name", b, sizeof b);
    CHECK(strcmp(b, "guest") == 0, "string default");
    zui_set_str(st, NONE, "no.such", b, sizeof b);
    CHECK(b[0] == 0, "unknown string key reads empty");
    CHECK(zui_set_int(st, NONE, "no.such") == 0, "unknown int key reads 0");
    CHECK(zui_set_get(st, NONE, "no.such", &v) == ZUI_SET_E_NOKEY, "unknown key");

    /* fill to the cap */
    zui_set_init(&ST2);
    int last = 0;
    char k[16];
    for (uint32_t i = 0; i < ZUI_SET_KEYS; i++) {
        snprintf(k, sizeof k, "k.%u", i);
        last = zui_set_def_bool(&ST2, k, i & 1u);
        if (last != (int) i) break;
    }
    CHECK(last == (int) ZUI_SET_KEYS - 1, "ZUI_SET_KEYS keys fit");
    CHECK(zui_set_def_bool(&ST2, "k.more", true) == ZUI_SET_E_FULL, "one more is full");
    int found = 1;
    for (uint32_t i = 0; i < ZUI_SET_KEYS; i++) {
        snprintf(k, sizeof k, "k.%u", i);
        if (zui_set_key(&ST2, k) != (int) i || zui_set_int(&ST2, NONE, k) != (int) (i & 1u))
            found = 0;
    }
    CHECK(found, "every key found with a full index");
}

static void test_names(void)
{
    zui_settings_t *st = &ST;
    schema(st);
    uint8_t s0 = zui_set_seat(st, "seat0"), s1 = zui_set_seat(st, "seat1");
    CHECK(s0 == 1 && s1 == 2, "seat ids from 1 (%u %u)", s0, s1);
    CHECK(zui_set_seat(st, "seat0") == 1, "same name, same id");
    CHECK(zui_set_seat(st, "") == 0, "empty name");
    CHECK(zui_set_seat(st, "a b") == 0, "space in a name");
    CHECK(zui_set_seat(st, 0) == 0, "null name");
    CHECK(zui_set_user(st, "Ana") == 1 && zui_set_user(st, "bo") == 2, "users");
    CHECK(zui_set_app(st, "burrow") == 1, "apps");
    char n[8];
    int full = 1;
    for (uint32_t i = 0; i < ZUI_SET_SEATS - 2u; i++) {
        snprintf(n, sizeof n, "s%u", i);
        if (zui_set_seat(st, n) != i + 3u) full = 0;
    }
    CHECK(full, "the seat table fills");
    CHECK(zui_set_seat(st, "extra") == 0, "a full table gives 0");
    CHECK(zui_set_seat(st, "s0") == 3, "existing names still found when full");
    char longn[ZUI_SET_NAME_MAX + 1];
    memset(longn, 'n', sizeof longn);
    longn[ZUI_SET_NAME_MAX] = 0;
    CHECK(zui_set_user(st, longn) == 0, "NAME_MAX chars too long");
    longn[ZUI_SET_NAME_MAX - 1u] = 0;
    CHECK(zui_set_user(st, longn) == 3, "NAME_MAX-1 chars fit");
}

static void test_types(void)
{
    zui_settings_t *st = &ST;
    schema(st);
    uint8_t u = zui_set_user(st, "ana");
    zui_set_view_t us = {0, u, 0}, v = {0, u, 0};
    CHECK(zui_set_set_int(st, ZUI_LAYER_USER, us, "text.scale", 120) == 0, "int set");
    CHECK(zui_set_int(st, v, "text.scale") == 120, "int read");
    CHECK(zui_set_set_int(st, ZUI_LAYER_USER, us, "text.scale", 125) == ZUI_SET_E_RANGE, "step");
    CHECK(zui_set_set_int(st, ZUI_LAYER_USER, us, "text.scale", 70) == ZUI_SET_E_RANGE, "min");
    CHECK(zui_set_set_int(st, ZUI_LAYER_USER, us, "text.scale", 210) == ZUI_SET_E_RANGE, "max");
    CHECK(zui_set_set_int(st, ZUI_LAYER_USER, us, "text.scale", 200) == 0, "max itself");
    CHECK(zui_set_set_int(st, ZUI_LAYER_USER, us, "text.scale", 80) == 0, "min itself");
    CHECK(zui_set_int(st, v, "text.scale") == 80, "refusals changed nothing in between");
    CHECK(zui_set_set_int(st, ZUI_LAYER_USER, us, "ui.gap", -8) == 0, "negative min");
    CHECK(zui_set_set_int(st, ZUI_LAYER_USER, us, "ui.gap", -6) == ZUI_SET_E_RANGE,
          "negative step");
    CHECK(zui_set_set_int(st, ZUI_LAYER_USER, us, "ui.gap", 8) == 0, "positive max");
    CHECK(zui_set_set_int(st, ZUI_LAYER_USER, us, "motion.reduced", 2) == ZUI_SET_E_RANGE,
          "bool 2");
    CHECK(zui_set_set_int(st, ZUI_LAYER_USER, us, "motion.reduced", -1) == ZUI_SET_E_RANGE,
          "bool -1");
    CHECK(zui_set_set_int(st, ZUI_LAYER_USER, us, "motion.reduced", 1) == 0, "bool 1");
    CHECK(zui_set_bool(st, v, "motion.reduced"), "bool read");
    CHECK(zui_set_set_int(st, ZUI_LAYER_USER, us, "ui.density", 3) == ZUI_SET_E_RANGE, "enum 3");
    CHECK(zui_set_set_int(st, ZUI_LAYER_USER, us, "ui.density", -1) == ZUI_SET_E_RANGE, "enum -1");
    CHECK(zui_set_set_int(st, ZUI_LAYER_USER, us, "ui.density", 2) == 0, "enum 2");
    CHECK(zui_set_set_int(st, ZUI_LAYER_USER, us, "ui.focus", ZUI_C_COUNT) == ZUI_SET_E_RANGE,
          "role past the end");
    CHECK(zui_set_set_int(st, ZUI_LAYER_USER, us, "ui.focus", -1) == ZUI_SET_E_RANGE, "role -1");
    CHECK(zui_set_set_int(st, ZUI_LAYER_USER, us, "ui.focus", ZUI_C_INFO) == 0, "role");
    CHECK(zui_set_set_int(st, ZUI_LAYER_USER, us, "user.name", 3) == ZUI_SET_E_TYPE,
          "int into a string");
    CHECK(zui_set_set_str(st, ZUI_LAYER_USER, us, "text.scale", "x") == ZUI_SET_E_TYPE,
          "string into an int");
    CHECK(zui_set_set_str(st, ZUI_LAYER_USER, us, "user.name", "Ana \"A\" \\o/") == 0, "string");
    char b[ZUI_SET_STR_MAX];
    zui_set_str(st, v, "user.name", b, sizeof b);
    CHECK(strcmp(b, "Ana \"A\" \\o/") == 0, "string read (%s)", b);
    zui_set_str(st, v, "user.name", b, 4);
    CHECK(strcmp(b, "Ana") == 0, "string clipped to the buffer");
    CHECK(zui_set_set_str(st, ZUI_LAYER_USER, us, "user.name", "a\nb") == ZUI_SET_E_RANGE,
          "newline refused");
    CHECK(zui_set_set_str(st, ZUI_LAYER_USER, us, "user.name", "a\x7f") == ZUI_SET_E_RANGE,
          "DEL refused");
    char longs[ZUI_SET_STR_MAX + 1];
    memset(longs, 'x', sizeof longs);
    longs[ZUI_SET_STR_MAX] = 0;
    CHECK(zui_set_set_str(st, ZUI_LAYER_USER, us, "user.name", longs) == ZUI_SET_E_RANGE,
          "too long");
    longs[ZUI_SET_STR_MAX - 1u] = 0;
    CHECK(zui_set_set_str(st, ZUI_LAYER_USER, us, "user.name", longs) == 0, "longest");
    zui_set_value_t raw;
    memset(&raw, 'y', sizeof raw); /* no NUL anywhere in s */
    CHECK(zui_set_set(st, ZUI_LAYER_USER, us, "user.name", &raw) == ZUI_SET_E_RANGE,
          "unterminated value refused");
    CHECK(zui_set_set(st, ZUI_LAYER_USER, us, "user.name", 0) == ZUI_SET_E_TYPE, "null value");
    CHECK(zui_set_set_int(st, ZUI_LAYER_USER, us, "no.key", 1) == ZUI_SET_E_NOKEY, "no key");
    CHECK(zui_set_set_str(st, ZUI_LAYER_USER, us, "no.key", "x") == ZUI_SET_E_NOKEY,
          "no key (string)");
    /* text values */
    CHECK(zui_set_set_text(st, ZUI_LAYER_USER, us, "ui.density", "compact") == 0, "enum text");
    CHECK(zui_set_int(st, v, "ui.density") == 0, "by name");
    CHECK(zui_set_set_text(st, ZUI_LAYER_USER, us, "ui.density", "Compact") == ZUI_SET_E_TYPE,
          "names are exact");
    CHECK(zui_set_set_text(st, ZUI_LAYER_USER, us, "ui.density", "compac") == ZUI_SET_E_TYPE,
          "no prefixes");
    CHECK(zui_set_set_text(st, ZUI_LAYER_USER, us, "ui.focus", "gold hi") == 0, "role by name");
    CHECK(zui_set_int(st, v, "ui.focus") == ZUI_C_GOLD_HI, "gold hi");
    CHECK(zui_set_set_text(st, ZUI_LAYER_USER, us, "ui.focus", "golden") == ZUI_SET_E_TYPE,
          "no such role");
    CHECK(zui_set_set_text(st, ZUI_LAYER_USER, us, "motion.reduced", "false") == 0, "false");
    CHECK(!zui_set_bool(st, v, "motion.reduced"), "read false");
    CHECK(zui_set_set_text(st, ZUI_LAYER_USER, us, "motion.reduced", "yes") == ZUI_SET_E_TYPE,
          "yes is not a bool");
    CHECK(zui_set_set_text(st, ZUI_LAYER_USER, us, "ui.gap", "-4") == 0, "negative int");
    CHECK(zui_set_int(st, v, "ui.gap") == -4, "-4");
    CHECK(zui_set_set_text(st, ZUI_LAYER_USER, us, "ui.gap", "+4") == ZUI_SET_E_TYPE, "no +");
    CHECK(zui_set_set_text(st, ZUI_LAYER_USER, us, "ui.gap", "-") == ZUI_SET_E_TYPE, "just -");
    CHECK(zui_set_set_text(st, ZUI_LAYER_USER, us, "ui.gap", "") == ZUI_SET_E_TYPE, "empty");
    CHECK(zui_set_set_text(st, ZUI_LAYER_USER, us, "ui.gap", "4x") == ZUI_SET_E_TYPE, "4x");
    CHECK(zui_set_set_text(st, ZUI_LAYER_USER, us, "ui.gap", "99999999999") == ZUI_SET_E_TYPE,
          "11 digits");
    CHECK(zui_set_set_text(st, ZUI_LAYER_USER, us, "ui.gap", "00000000004") == ZUI_SET_E_TYPE,
          "11 digits, small value");
    CHECK(zui_set_set_text(st, ZUI_LAYER_USER, us, "ui.gap", "2147483648") == ZUI_SET_E_TYPE,
          "past int32");
    CHECK(zui_set_set_text(st, ZUI_LAYER_USER, us, "ui.gap", "12") == ZUI_SET_E_RANGE,
          "an int past max is a range error");
    CHECK(zui_set_set_text(st, ZUI_LAYER_USER, us, "user.name", "\"a\\\"b\"") == 0, "escape");
    zui_set_str(st, v, "user.name", b, sizeof b);
    CHECK(strcmp(b, "a\"b") == 0, "unescaped (%s)", b);
    CHECK(zui_set_set_text(st, ZUI_LAYER_USER, us, "user.name", "abc") == ZUI_SET_E_TYPE,
          "unquoted string");
    CHECK(zui_set_set_text(st, ZUI_LAYER_USER, us, "user.name", "\"a\"b\"") == ZUI_SET_E_TYPE,
          "inner quote");
    CHECK(zui_set_set_text(st, ZUI_LAYER_USER, us, "user.name", "\"a\\n\"") == ZUI_SET_E_TYPE,
          "unknown escape");
    CHECK(zui_set_set_text(st, ZUI_LAYER_USER, us, "user.name", "\"a\\\"") == ZUI_SET_E_TYPE,
          "escaped closing quote");
    CHECK(zui_set_set_text(st, ZUI_LAYER_USER, us, "user.name", "\"") == ZUI_SET_E_TYPE,
          "lone quote");
    CHECK(zui_set_set_text(st, ZUI_LAYER_USER, us, "user.name", "\"\"") == 0, "empty string");
    zui_set_str(st, v, "user.name", b, sizeof b);
    CHECK(b[0] == 0, "empty");
    /* formatting */
    zui_set_value_t x = {-120, {0}};
    char f[64];
    CHECK(zui_set_format(st, (uint32_t) zui_set_key(st, "ui.gap"), &x, f, sizeof f) == 4 &&
              strcmp(f, "-120") == 0,
          "format -120 (%s)", f);
    x.i = 0;
    CHECK(zui_set_format(st, 0, &x, f, sizeof f) == 1 && strcmp(f, "0") == 0, "format 0");
    x.i = INT32_MIN;
    CHECK(zui_set_format(st, 0, &x, f, sizeof f) == 11 && strcmp(f, "-2147483648") == 0,
          "format INT32_MIN");
    x.i = 1;
    CHECK(zui_set_format(st, 1, &x, f, sizeof f) == 4 && strcmp(f, "true") == 0, "true");
    x.i = ZUI_C_ENGRAVE_LO;
    CHECK(zui_set_format(st, 4, &x, f, sizeof f) > 0 && strcmp(f, "engrave lo") == 0, "role");
    CHECK(zui_set_format(st, 4, &x, f, 5) == ZUI_SET_E_SPACE, "no room");
    CHECK(zui_set_format(st, 99, &x, f, sizeof f) == ZUI_SET_E_NOKEY, "no key");
}

static void test_layers(void)
{
    zui_settings_t *st = &ST;
    schema(st);
    uint8_t s0 = zui_set_seat(st, "seat0"), s1 = zui_set_seat(st, "seat1");
    uint8_t ua = zui_set_user(st, "ana"), ub = zui_set_user(st, "bo");
    uint8_t burrow = zui_set_app(st, "burrow"), quill = zui_set_app(st, "quillon");
    zui_set_view_t sys = {0, 0, 0}, sc0 = {s0, 0, 0}, sc1 = {s1, 0, 0};
    zui_set_view_t sa = {0, ua, 0}, sb = {0, ub, 0}, saq = {0, ua, burrow};
    zui_set_view_t va = {s0, ua, 0}, vab = {s0, ua, burrow}, vaq = {s0, ua, quill};
    zui_set_view_t vb = {s1, ub, 0}, vbb = {s1, ub, burrow}, login = {s1, 0, 0};
    const char *K = "text.scale";
    CHECK(zui_set_int(st, vab, K) == 100, "schema default");
    CHECK(zui_set_set_int(st, ZUI_LAYER_SYSTEM, sys, K, 110) == 0, "system");
    CHECK(zui_set_int(st, vab, K) == 110 && zui_set_int(st, vb, K) == 110, "system for all");
    CHECK(zui_set_set_int(st, ZUI_LAYER_DEVICE, sys, K, 120) == 0, "device");
    CHECK(zui_set_int(st, vab, K) == 120 && zui_set_int(st, login, K) == 120, "device for all");
    CHECK(zui_set_set_int(st, ZUI_LAYER_SEAT, sc0, K, 130) == 0, "seat 0");
    CHECK(zui_set_int(st, va, K) == 130, "seat 0 view");
    CHECK(zui_set_int(st, vb, K) == 120, "seat 1 still device");
    CHECK(zui_set_set_int(st, ZUI_LAYER_SEAT, sc1, K, 90) == 0, "seat 1");
    CHECK(zui_set_int(st, login, K) == 90, "login screen on seat 1");
    CHECK(zui_set_set_int(st, ZUI_LAYER_USER, sa, K, 140) == 0, "user ana");
    CHECK(zui_set_int(st, va, K) == 140, "ana's view");
    CHECK(zui_set_int(st, vb, K) == 90, "bo's view unchanged");
    zui_set_view_t ana_on_s1 = {s1, ua, 0};
    CHECK(zui_set_int(st, ana_on_s1, K) == 140, "ana's value follows her to seat 1");
    CHECK(zui_set_set_int(st, ZUI_LAYER_APP, saq, K, 150) == 0, "ana's burrow");
    CHECK(zui_set_int(st, vab, K) == 150, "app layer wins");
    CHECK(zui_set_int(st, vaq, K) == 140, "another app: the user value");
    CHECK(zui_set_int(st, vbb, K) == 90, "bo in burrow: his own chain");
    CHECK(zui_set_set_int(st, ZUI_LAYER_USER, sb, K, 160) == 0, "bo");
    CHECK(zui_set_int(st, vbb, K) == 160 && zui_set_int(st, vab, K) == 150, "independent");
    /* scopes */
    CHECK(zui_set_set_int(st, ZUI_LAYER_SEAT, sys, K, 100) == ZUI_SET_E_SCOPE, "seat needs one");
    CHECK(zui_set_set_int(st, ZUI_LAYER_USER, va, K, 100) == ZUI_SET_E_SCOPE,
          "user scope with a seat in it");
    CHECK(zui_set_set_int(st, ZUI_LAYER_APP, sa, K, 100) == ZUI_SET_E_SCOPE, "app needs an app");
    CHECK(zui_set_set_int(st, ZUI_LAYER_SYSTEM, sc0, K, 100) == ZUI_SET_E_SCOPE,
          "system has no scope");
    zui_set_view_t ghost = {0, 9, 0};
    CHECK(zui_set_set_int(st, ZUI_LAYER_USER, ghost, K, 100) == ZUI_SET_E_SCOPE, "unknown user");
    CHECK(zui_set_set_int(st, (zui_set_layer_t) 7, sys, K, 100) == ZUI_SET_E_SCOPE, "no layer");
    /* peek */
    zui_set_value_t p;
    bool lk = true;
    CHECK(zui_set_peek(st, ZUI_LAYER_SEAT, sc0, K, &p, &lk) == 0 && p.i == 130 && !lk, "peek");
    CHECK(zui_set_peek(st, ZUI_LAYER_APP, (zui_set_view_t){0, ub, burrow}, K, &p, 0) ==
              ZUI_SET_E_NOKEY,
          "nothing there");
    /* reset shows through, one level at a time */
    CHECK(zui_set_reset(st, ZUI_LAYER_APP, saq, K) == 0, "reset app");
    CHECK(zui_set_int(st, vab, K) == 140, "user shows through");
    CHECK(zui_set_reset(st, ZUI_LAYER_USER, sa, K) == 0, "reset user");
    CHECK(zui_set_int(st, vab, K) == 130, "seat shows through");
    CHECK(zui_set_reset(st, ZUI_LAYER_SEAT, sc0, K) == 0, "reset seat");
    CHECK(zui_set_int(st, vab, K) == 120, "device shows through");
    CHECK(zui_set_reset(st, ZUI_LAYER_DEVICE, sys, K) == 0, "reset device");
    CHECK(zui_set_int(st, vab, K) == 110, "system shows through");
    CHECK(zui_set_reset(st, ZUI_LAYER_SYSTEM, sys, K) == 0, "reset system");
    CHECK(zui_set_int(st, vab, K) == 100, "schema default again");
    CHECK(zui_set_reset(st, ZUI_LAYER_SYSTEM, sys, K) == 0, "reset again is fine");
    CHECK(zui_set_reset(st, ZUI_LAYER_SYSTEM, sys, "no.key") == ZUI_SET_E_NOKEY, "reset no key");
    /* reset a layer */
    zui_set_set_int(st, ZUI_LAYER_USER, sb, "ui.density", 0);
    zui_set_set_str(st, ZUI_LAYER_USER, sb, "user.name", "Bo");
    zui_set_set_int(st, ZUI_LAYER_USER, sa, "ui.density", 2);
    CHECK(zui_set_reset_layer(st, ZUI_LAYER_USER, sb) == 0, "reset bo's layer");
    CHECK(zui_set_int(st, vbb, K) == 90 && zui_set_int(st, vb, "ui.density") == 1,
          "bo back to defaults");
    char b[16];
    zui_set_str(st, vb, "user.name", b, sizeof b);
    CHECK(strcmp(b, "guest") == 0, "bo's name reset");
    CHECK(zui_set_int(st, va, "ui.density") == 2, "ana's layer untouched");
    CHECK(zui_set_peek(st, ZUI_LAYER_SEAT, sc1, K, &p, 0) == 0 && p.i == 90, "seat untouched");
    CHECK(zui_set_reset_layer(st, ZUI_LAYER_USER, sc0) == ZUI_SET_E_SCOPE, "bad scope");
}

static void test_cache(void)
{
    zui_settings_t *st = &ST;
    schema(st);
    uint8_t ua = zui_set_user(st, "ana"), ub = zui_set_user(st, "bo");
    zui_set_view_t va = {0, ua, 0}, vb = {0, ub, 0};
    st->hits = st->misses = 0;
    CHECK(zui_set_int(st, va, "text.scale") == 100, "first read");
    CHECK(st->misses == 1 && st->hits == 0, "a miss (%u %u)", st->misses, st->hits);
    for (int i = 0; i < 5; i++) zui_set_int(st, va, "text.scale");
    CHECK(st->hits == 5 && st->misses == 1, "then hits (%u %u)", st->hits, st->misses);
    zui_set_int(st, vb, "text.scale");
    CHECK(st->misses == 2, "another view is its own entry");
    zui_set_int(st, vb, "text.scale");
    CHECK(st->hits == 6, "and hits after");
    zui_set_int(st, va, "ui.density");
    zui_set_int(st, va, "ui.density");
    CHECK(st->misses == 3 && st->hits == 7, "another key");
    /* a change drops that key only, for every view */
    zui_set_set_int(st, ZUI_LAYER_DEVICE, NONE, "text.scale", 150);
    uint32_t h = st->hits, m = st->misses;
    CHECK(zui_set_int(st, va, "ui.density") == 1 && st->hits == h + 1u, "other key still cached");
    CHECK(zui_set_int(st, va, "text.scale") == 150 && st->misses == m + 1u, "changed key missed");
    CHECK(zui_set_int(st, vb, "text.scale") == 150 && st->misses == m + 2u, "for every view");
    CHECK(zui_set_int(st, vb, "text.scale") == 150 && st->hits == h + 2u, "cached again");
    /* a change under an override still refreshes (value stays right) */
    zui_set_set_int(st, ZUI_LAYER_USER, va, "text.scale", 180);
    CHECK(zui_set_int(st, va, "text.scale") == 180, "user value seen at once");
    CHECK(zui_set_int(st, vb, "text.scale") == 150, "bo unaffected");
    zui_set_reset(st, ZUI_LAYER_USER, va, "text.scale");
    CHECK(zui_set_int(st, va, "text.scale") == 150, "reset seen at once");
    /* a refused write keeps the cache */
    zui_set_int(st, va, "text.scale");
    h = st->hits;
    zui_set_set_int(st, ZUI_LAYER_USER, va, "text.scale", 7);
    zui_set_int(st, va, "text.scale");
    CHECK(st->hits == h + 1u, "a refused write drops nothing");
    /* get_k with a bad index */
    CHECK(zui_set_get_k(st, va, st->nkeys, 0) == ZUI_SET_E_NOKEY, "bad index");
    /* many views: always right even when slots collide */
    int ok = 1;
    for (uint32_t i = 0; i < 3u * ZUI_SET_CACHE; i++) {
        zui_set_view_t v = {(uint8_t) (i % 3u), (uint8_t) (i % 2u ? ua : ub), (uint8_t) (i % 5u)};
        (void) v;
        zui_set_view_t w = {0, (uint8_t) (i % 2u ? ua : ub), 0};
        if (zui_set_int(st, w, "text.scale") != 150) ok = 0;
        if (zui_set_int(st, w, "ui.gap") != -4) ok = 0;
    }
    CHECK(ok, "values right under collisions");
}

static void test_locks(void)
{
    zui_settings_t *st = &ST;
    schema(st);
    uint8_t s0 = zui_set_seat(st, "seat0"), s1 = zui_set_seat(st, "seat1");
    uint8_t ua = zui_set_user(st, "ana"), app = zui_set_app(st, "burrow");
    zui_set_view_t sc0 = {s0, 0, 0}, sa = {0, ua, 0}, saa = {0, ua, app};
    zui_set_view_t v0 = {s0, ua, app}, v1 = {s1, ua, app};
    const char *K = "theme.scheme";
    zui_set_set_int(st, ZUI_LAYER_USER, sa, K, 1);
    CHECK(zui_set_int(st, v0, K) == 1, "user picked light");
    CHECK(zui_set_lock(st, ZUI_LAYER_DEVICE, NONE, K, true) == 0, "device locks");
    CHECK(zui_set_int(st, v0, K) == 0, "the lock keeps what was below it (dark)");
    bool lk = false;
    zui_set_value_t p;
    CHECK(zui_set_peek(st, ZUI_LAYER_DEVICE, NONE, K, &p, &lk) == 0 && lk && p.i == 0,
          "a locked device value");
    CHECK(zui_set_set_int(st, ZUI_LAYER_USER, sa, K, 2) == ZUI_SET_E_LOCKED, "user refused");
    CHECK(zui_set_set_int(st, ZUI_LAYER_APP, saa, K, 2) == ZUI_SET_E_LOCKED, "app refused");
    CHECK(zui_set_set_int(st, ZUI_LAYER_SEAT, sc0, K, 2) == ZUI_SET_E_LOCKED, "seat refused");
    CHECK(zui_set_set_int(st, ZUI_LAYER_SYSTEM, NONE, K, 2) == 0, "system below is fine");
    CHECK(zui_set_int(st, v0, K) == 0, "but the device value rules");
    CHECK(zui_set_set_int(st, ZUI_LAYER_DEVICE, NONE, K, 2) == 0, "the device may change it");
    CHECK(zui_set_peek(st, ZUI_LAYER_DEVICE, NONE, K, &p, &lk) == 0 && lk && p.i == 2,
          "still locked after a set");
    CHECK(zui_set_int(st, v0, K) == 2, "high contrast for all");
    CHECK(zui_set_lock(st, ZUI_LAYER_USER, sa, K, true) == ZUI_SET_E_SCOPE, "user cannot lock");
    CHECK(zui_set_lock(st, ZUI_LAYER_DEVICE, NONE, K, false) == 0, "unlock");
    CHECK(zui_set_int(st, v0, K) == 1, "the user value shows again");
    CHECK(zui_set_lock(st, ZUI_LAYER_DEVICE, NONE, K, false) == 0, "unlock twice is fine");
    /* a lock with no value of its own keeps what shows below it */
    zui_set_set_int(st, ZUI_LAYER_SYSTEM, NONE, "ui.gap", 4);
    CHECK(zui_set_lock(st, ZUI_LAYER_DEVICE, NONE, "ui.gap", true) == 0, "lock gap");
    CHECK(zui_set_peek(st, ZUI_LAYER_DEVICE, NONE, "ui.gap", &p, &lk) == 0 && lk && p.i == 4,
          "the system's 4, not the default -4");
    CHECK(zui_set_lock(st, ZUI_LAYER_SEAT, sc0, "user.name", true) == 0, "seat lock, no value");
    zui_set_set_str(st, ZUI_LAYER_DEVICE, NONE, "user.name", "kiosk");
    CHECK(zui_set_peek(st, ZUI_LAYER_SEAT, sc0, "user.name", &p, &lk) == 0 && lk &&
              strcmp(p.s, "guest") == 0,
          "the value at lock time is kept");
    /* system lock */
    CHECK(zui_set_lock(st, ZUI_LAYER_SYSTEM, NONE, "text.scale", true) == 0, "system lock");
    CHECK(zui_set_set_int(st, ZUI_LAYER_DEVICE, NONE, "text.scale", 120) == ZUI_SET_E_LOCKED,
          "device refused under a system lock");
    CHECK(zui_set_lock(st, ZUI_LAYER_DEVICE, NONE, "text.scale", true) == ZUI_SET_E_LOCKED,
          "cannot lock over a lock");
    CHECK(zui_set_int(st, v0, "text.scale") == 100, "the schema default, locked");
    /* seat lock: masks ana on that seat only */
    zui_set_set_int(st, ZUI_LAYER_USER, sa, "ui.density", 2);
    zui_set_set_int(st, ZUI_LAYER_SEAT, sc0, "ui.density", 0);
    CHECK(zui_set_lock(st, ZUI_LAYER_SEAT, sc0, "ui.density", true) == 0, "seat lock");
    CHECK(zui_set_int(st, v0, "ui.density") == 0, "masked on seat 0");
    CHECK(zui_set_int(st, v1, "ui.density") == 2, "ana's own on seat 1");
    CHECK(zui_set_set_int(st, ZUI_LAYER_USER, sa, "ui.density", 1) == 0,
          "a user write is kept (S4)");
    CHECK(zui_set_int(st, v0, "ui.density") == 0 && zui_set_int(st, v1, "ui.density") == 1,
          "still masked on seat 0 only");
    CHECK(zui_set_lock(st, ZUI_LAYER_APP, saa, "ui.density", true) == ZUI_SET_E_SCOPE,
          "app cannot lock");
    /* reset drops a lock with its value */
    CHECK(zui_set_reset(st, ZUI_LAYER_SEAT, sc0, "ui.density") == 0, "reset the seat");
    CHECK(zui_set_int(st, v0, "ui.density") == 1, "unmasked");
}

static int fired, last_val;
static uint32_t last_key;
static zui_settings_t *cb_st;
static int busy_r;
static void on_change(zui_settings_t *st, uint32_t key, const zui_set_value_t *v, void *ctx)
{
    (void) st;
    fired++;
    last_key = key;
    last_val = v->i;
    if (ctx) *(int *) ctx += 1;
}
static void writer(zui_settings_t *st, uint32_t key, const zui_set_value_t *v, void *ctx)
{
    (void) key, (void) v, (void) ctx;
    busy_r = zui_set_set_int(st, ZUI_LAYER_DEVICE, NONE, "ui.gap", 0);
    fired++;
    /* reading is fine, and sees the new value */
    if (!zui_set_bool(st, NONE, "motion.reduced")) busy_r = 99;
}

static void test_watch(void)
{
    zui_settings_t *st = &ST;
    schema(st);
    cb_st = st;
    uint8_t s0 = zui_set_seat(st, "seat0"), ua = zui_set_user(st, "ana"),
            ub = zui_set_user(st, "bo"), app = zui_set_app(st, "burrow");
    zui_set_view_t va = {s0, ua, 0}, vb = {0, ub, 0}, sa = {0, ua, 0}, sb = {0, ub, 0};
    int na = 0, nb = 0;
    int wa = zui_set_watch(st, va, "text.", on_change, &na);
    int wb = zui_set_watch(st, vb, "text.", on_change, &nb);
    CHECK(wa == 1 && wb == 2, "watch ids (%d %d)", wa, wb);
    fired = 0;
    zui_set_set_int(st, ZUI_LAYER_DEVICE, NONE, "text.scale", 120);
    CHECK(fired == 2 && na == 1 && nb == 1 && last_val == 120, "both views see it");
    CHECK(last_key == (uint32_t) zui_set_key(st, "text.scale"), "the key index");
    zui_set_set_int(st, ZUI_LAYER_DEVICE, NONE, "text.scale", 120);
    CHECK(fired == 2, "same value: no call");
    zui_set_set_int(st, ZUI_LAYER_DEVICE, NONE, "ui.gap", 4);
    CHECK(fired == 2, "another prefix: no call");
    zui_set_set_int(st, ZUI_LAYER_USER, sa, "text.scale", 150);
    CHECK(na == 2 && nb == 1 && last_val == 150, "ana's change: ana only");
    zui_set_set_int(st, ZUI_LAYER_DEVICE, NONE, "text.scale", 130);
    CHECK(na == 2 && nb == 2, "a lower layer under ana's override: only bo");
    zui_set_set_int(st, ZUI_LAYER_SYSTEM, NONE, "text.scale", 90);
    CHECK(na == 2 && nb == 2, "deeper still: nobody");
    zui_set_set_int(st, ZUI_LAYER_USER, sb, "text.scale", 130);
    CHECK(nb == 2, "bo sets what he already sees: no call");
    zui_set_set_int(st, ZUI_LAYER_APP, (zui_set_view_t){0, ua, app}, "text.scale", 200);
    CHECK(na == 2, "an app value ana's view (no app) does not use");
    zui_set_set_int(st, ZUI_LAYER_SEAT, (zui_set_view_t){s0, 0, 0}, "text.scale", 80);
    CHECK(na == 2 && nb == 2, "seat value under ana's user value; bo not on the seat");
    zui_set_reset(st, ZUI_LAYER_USER, sa, "text.scale");
    CHECK(na == 3 && last_val == 80, "reset: the seat shows through, once");
    zui_set_lock(st, ZUI_LAYER_DEVICE, NONE, "text.scale", true);
    CHECK(na == 4 && last_val == 130 && nb == 2, "a lock changes ana's, not bo's");
    zui_set_lock(st, ZUI_LAYER_DEVICE, NONE, "text.scale", false);
    CHECK(na == 5 && last_val == 80, "unlock: back");
    /* the empty prefix sees everything */
    int nall = 0;
    int wall = zui_set_watch(st, NONE, "", on_change, &nall);
    zui_set_set_int(st, ZUI_LAYER_DEVICE, NONE, "ui.density", 0);
    zui_set_set_str(st, ZUI_LAYER_DEVICE, NONE, "user.name", "x");
    CHECK(nall == 2, "every key");
    zui_set_unwatch(st, wall);
    zui_set_set_int(st, ZUI_LAYER_DEVICE, NONE, "ui.density", 2);
    CHECK(nall == 2, "unwatched");
    zui_set_unwatch(st, 0);
    zui_set_unwatch(st, 99);
    /* a whole-layer reset fires once per key that changes */
    zui_set_set_int(st, ZUI_LAYER_USER, sb, "text.scale", 160);
    nb = 0;
    zui_set_reset_layer(st, ZUI_LAYER_USER, sb);
    CHECK(nb == 1 && last_val == 130, "layer reset: one call");
    /* a write from inside a callback is refused */
    zui_set_unwatch(st, wa);
    zui_set_unwatch(st, wb);
    int ww = zui_set_watch(st, NONE, "motion", writer, 0);
    fired = 0;
    busy_r = 0;
    CHECK(zui_set_set_int(st, ZUI_LAYER_DEVICE, NONE, "motion.reduced", 1) == 0, "outer write");
    CHECK(fired == 1 && busy_r == ZUI_SET_E_BUSY, "inner write refused (%d)", busy_r);
    CHECK(zui_set_int(st, NONE, "ui.gap") == 4, "and did nothing");
    zui_set_unwatch(st, ww);
    CHECK(zui_set_set_int(st, ZUI_LAYER_DEVICE, NONE, "ui.gap", 0) == 0, "not busy after");
    /* limits */
    CHECK(zui_set_watch(st, NONE, "x", 0, 0) == ZUI_SET_E_TYPE, "no callback");
    CHECK(zui_set_watch(st, NONE, "Bad", on_change, 0) == ZUI_SET_E_NAME, "bad prefix");
    CHECK(zui_set_watch(st, (zui_set_view_t){0, 9, 0}, "x", on_change, 0) == ZUI_SET_E_SCOPE,
          "unknown user");
    int ok = 1;
    for (uint32_t i = 0; i < ZUI_SET_WATCH; i++)
        if (zui_set_watch(st, NONE, "x", on_change, 0) != (int) i + 1) ok = 0;
    CHECK(ok, "every slot");
    CHECK(zui_set_watch(st, NONE, "x", on_change, 0) == ZUI_SET_E_FULL, "full");
    zui_set_unwatch(st, 5);
    CHECK(zui_set_watch(st, NONE, "x", on_change, 0) == 5, "a freed slot comes back");
}

static char buf[8192], buf2[8192];

static void test_text(void)
{
    zui_settings_t *st = &ST;
    schema(st);
    uint8_t s0 = zui_set_seat(st, "seat0"), ua = zui_set_user(st, "ana"),
            app = zui_set_app(st, "burrow");
    zui_set_view_t sa = {0, ua, 0}, sc = {s0, 0, 0}, saa = {0, ua, app};
    zui_set_set_int(st, ZUI_LAYER_USER, sa, "ui.gap", -8);
    zui_set_set_int(st, ZUI_LAYER_USER, sa, "text.scale", 150);
    zui_set_set_str(st, ZUI_LAYER_USER, sa, "user.name", "A \"q\" \\ #x");
    zui_set_set_int(st, ZUI_LAYER_USER, sa, "ui.focus", ZUI_C_TEXT_2);
    zui_set_set_int(st, ZUI_LAYER_USER, sa, "motion.reduced", 1);
    zui_set_set_int(st, ZUI_LAYER_USER, sa, "theme.scheme", 2);
    int n = zui_set_save(st, ZUI_LAYER_USER, sa, buf, sizeof buf);
    const char *want = "zxcfg 1\n"
                       "# user ana\n"
                       "text.scale = 150\n"
                       "motion.reduced = true\n"
                       "user.name = \"A \\\"q\\\" \\\\ #x\"\n"
                       "ui.focus = text 2\n"
                       "theme.scheme = high-contrast\n"
                       "ui.gap = -8\n";
    CHECK(n == (int) strlen(want) && strcmp(buf, want) == 0, "exact text:\n%s", buf);
    CHECK(zui_set_save(st, ZUI_LAYER_USER, sa, buf2, (uint32_t) n) == ZUI_SET_E_SPACE,
          "one byte short");
    CHECK(zui_set_save(st, ZUI_LAYER_USER, sa, buf2, (uint32_t) n + 1u) == n, "just fits");
    CHECK(zui_set_save(st, ZUI_LAYER_SEAT, sc, buf2, sizeof buf2) == 21 &&
              strcmp(buf2, "zxcfg 1\n# seat seat0\n") == 0,
          "an empty seat layer (%s)", buf2);
    CHECK(zui_set_save(st, ZUI_LAYER_USER, sc, buf2, sizeof buf2) == ZUI_SET_E_SCOPE,
          "a seat scope is not a user scope");
    CHECK(zui_set_save(st, ZUI_LAYER_APP, saa, buf2, sizeof buf2) > 0 &&
              strcmp(buf2, "zxcfg 1\n# app ana burrow\n") == 0,
          "app scope names (%s)", buf2);
    /* round trip into another store */
    schema(&ST2);
    uint8_t u2 = zui_set_user(&ST2, "ana");
    zui_set_view_t s2 = {0, u2, 0};
    uint32_t line = 7;
    CHECK(zui_set_load(&ST2, ZUI_LAYER_USER, s2, buf, (uint32_t) n, &line) == 0 && line == 0,
          "loads");
    int n2 = zui_set_save(&ST2, ZUI_LAYER_USER, s2, buf2, sizeof buf2);
    CHECK(n2 == n && memcmp(buf, buf2, (size_t) n) == 0, "save, load, save: same bytes");
    CHECK(zui_set_int(&ST2, s2, "ui.gap") == -8 && zui_set_int(&ST2, s2, "text.scale") == 150,
          "values");
    char b[ZUI_SET_STR_MAX];
    zui_set_str(&ST2, s2, "user.name", b, sizeof b);
    CHECK(strcmp(b, "A \"q\" \\ #x") == 0, "string (%s)", b);
    /* locks round-trip */
    zui_set_set_int(st, ZUI_LAYER_DEVICE, NONE, "ui.density", 2);
    zui_set_lock(st, ZUI_LAYER_DEVICE, NONE, "ui.density", true);
    zui_set_set_int(st, ZUI_LAYER_DEVICE, NONE, "text.scale", 80);
    n = zui_set_save(st, ZUI_LAYER_DEVICE, NONE, buf, sizeof buf);
    CHECK(strcmp(buf, "zxcfg 1\n# device\ntext.scale = 80\nlock ui.density = roomy\n") == 0,
          "lock line (%s)", buf);
    CHECK(zui_set_load(&ST2, ZUI_LAYER_DEVICE, NONE, buf, (uint32_t) n, &line) == 0, "loads");
    bool lk = false;
    zui_set_value_t p;
    CHECK(zui_set_peek(&ST2, ZUI_LAYER_DEVICE, NONE, "ui.density", &p, &lk) == 0 && lk && p.i == 2,
          "lock loaded");
    n2 = zui_set_save(&ST2, ZUI_LAYER_DEVICE, NONE, buf2, sizeof buf2);
    CHECK(n2 == n && strcmp(buf, buf2) == 0, "lock round-trips");

    /* hand-written text: comments, blanks, CR LF, spacing */
    const char *hand = "zxcfg 1\r\n"
                       "  # a comment\r\n"
                       "\r\n"
                       "\t text.scale=120  \r\n"
                       "theme.scheme   =   light\n"
                       "ui.focus = gold glow";
    CHECK(zui_set_load(&ST2, ZUI_LAYER_USER, s2, hand, (uint32_t) strlen(hand), &line) == 0,
          "hand-written loads (line %u)", line);
    CHECK(zui_set_int(&ST2, s2, "text.scale") == 120 &&
              zui_set_int(&ST2, s2, "theme.scheme") == 1 &&
              zui_set_int(&ST2, s2, "ui.focus") == ZUI_C_GOLD_GLOW,
          "hand-written values");
    CHECK(zui_set_int(&ST2, s2, "ui.density") == 2, "the device lock still rules");
    CHECK(zui_set_int(&ST2, s2, "ui.gap") == -4, "loading replaced the layer (gap gone)");
    zui_set_str(&ST2, s2, "user.name", b, sizeof b);
    CHECK(strcmp(b, "guest") == 0, "name gone too");

    /* errors: line numbers, and nothing changes */
    n = zui_set_save(&ST2, ZUI_LAYER_USER, s2, buf, sizeof buf);
    struct {
        const char *t;
        int err;
        uint32_t line;
    } bad[] = {
        {"", ZUI_SET_E_VERSION, 1},
        {"zxcfg 2\n", ZUI_SET_E_VERSION, 1},
        {"zxcfg 10\n", ZUI_SET_E_VERSION, 1},
        {"zxcfg\n", ZUI_SET_E_VERSION, 1},
        {"# zxcfg 1\n", ZUI_SET_E_VERSION, 1},
        {"zxcfg 1\ntext.scale = 120\nno.key = 1\n", ZUI_SET_E_NOKEY, 3},
        {"zxcfg 1\n\n\ntext.scale = 125\n", ZUI_SET_E_RANGE, 4},
        {"zxcfg 1\ntext.scale 120\n", ZUI_SET_E_PARSE, 2},
        {"zxcfg 1\n= 120\n", ZUI_SET_E_PARSE, 2},
        {"zxcfg 1\nText.scale = 120\n", ZUI_SET_E_PARSE, 2},
        {"zxcfg 1\ntext.scale = 120\r\ntext.scale = 130\n", ZUI_SET_E_DUP, 3},
        {"zxcfg 1\nmotion.reduced = maybe\n", ZUI_SET_E_TYPE, 2},
        {"zxcfg 1\nui.focus = gold\nlock ui.density = roomy\n", ZUI_SET_E_SCOPE, 3},
        {"zxcfg 1\nuser.name = \"open\n", ZUI_SET_E_TYPE, 2},
        {"zxcfg 1\ntext.scale = 120 # no trailing comments\n", ZUI_SET_E_TYPE, 2},
    };
    int all = 1;
    for (uint32_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        line = 0;
        int r =
            zui_set_load(&ST2, ZUI_LAYER_USER, s2, bad[i].t, (uint32_t) strlen(bad[i].t), &line);
        if (r != bad[i].err || line != bad[i].line) {
            all = 0;
            printf("  bad[%u]: got %d line %u, want %d line %u\n", i, r, line, bad[i].err,
                   bad[i].line);
        }
    }
    CHECK(all, "each error with its line");
    n2 = zui_set_save(&ST2, ZUI_LAYER_USER, s2, buf2, sizeof buf2);
    CHECK(n2 == n && strcmp(buf, buf2) == 0, "failed loads changed nothing");
    char longl[ZUI_SET_LINE_MAX + 40];
    snprintf(longl, sizeof longl, "zxcfg 1\n#%0*d\n", (int) ZUI_SET_LINE_MAX, 0);
    CHECK(zui_set_load(&ST2, ZUI_LAYER_USER, s2, longl, (uint32_t) strlen(longl), &line) ==
                  ZUI_SET_E_PARSE &&
              line == 2,
          "a line too long");
    CHECK(zui_set_load(&ST2, ZUI_LAYER_USER, s2, "zxcfg 1", 7, 0) == 0, "header only, no NL");
    CHECK(zui_set_save(&ST2, ZUI_LAYER_USER, s2, buf2, sizeof buf2) > 0 &&
              strcmp(buf2, "zxcfg 1\n# user ana\n") == 0,
          "an empty layer");
    CHECK(zui_set_load(&ST2, ZUI_LAYER_USER, (zui_set_view_t){0, 9, 0}, "zxcfg 1", 7, 0) ==
              ZUI_SET_E_SCOPE,
          "bad scope");
    /* a lock in the device text is fine; a lock in seat too */
    uint8_t s20 = zui_set_seat(&ST2, "seat0");
    const char *seatt = "zxcfg 1\nlock text.scale = 90\n";
    CHECK(zui_set_load(&ST2, ZUI_LAYER_SEAT, (zui_set_view_t){s20, 0, 0}, seatt,
                       (uint32_t) strlen(seatt), &line) == 0,
          "seat lock loads");
    CHECK(zui_set_int(&ST2, (zui_set_view_t){s20, u2, 0}, "text.scale") == 90,
          "masks ana on that seat");
}

static void test_load_watch_and_full(void)
{
    zui_settings_t *st = &ST;
    schema(st);
    uint8_t ua = zui_set_user(st, "ana");
    zui_set_view_t sa = {0, ua, 0};
    zui_set_set_int(st, ZUI_LAYER_USER, sa, "text.scale", 120);
    zui_set_set_int(st, ZUI_LAYER_USER, sa, "ui.density", 0);
    int cnt = 0;
    zui_set_watch(st, sa, "", on_change, &cnt);
    const char *t = "zxcfg 1\ntext.scale = 120\nui.gap = 4\n";
    CHECK(zui_set_load(st, ZUI_LAYER_USER, sa, t, (uint32_t) strlen(t), 0) == 0, "loads");
    CHECK(cnt == 2, "density back to default + gap: two calls, scale unchanged (%d)", cnt);
    CHECK(zui_set_load(st, ZUI_LAYER_USER, sa, t, (uint32_t) strlen(t), 0) == 0, "again");
    CHECK(cnt == 2, "the same text again: no calls");

    /* entries run out: an error, never a silent drop */
    schema(&ST2);
    char nm[16];
    for (uint32_t i = 0; i < ZUI_SET_USERS; i++) {
        snprintf(nm, sizeof nm, "u%u", i);
        zui_set_user(&ST2, nm);
    }
    for (uint32_t i = 0; i < ZUI_SET_APPS; i++) {
        snprintf(nm, sizeof nm, "a%u", i);
        zui_set_app(&ST2, nm);
    }
    CHECK(zui_set_user(&ST2, "zz") == 0 && zui_set_app(&ST2, "zz") == 0, "tables full");
    int r = 0;
    uint32_t sets = 0;
    for (uint32_t u = 1; u <= ZUI_SET_USERS && r == 0; u++)
        for (uint32_t a = 1; a <= ZUI_SET_APPS && r == 0; a++)
            for (uint32_t k = 0; k < ST2.nkeys && r == 0; k++) {
                zui_set_value_t v = ST2.def[k].def;
                r = zui_set_set(&ST2, ZUI_LAYER_APP, (zui_set_view_t){0, (uint8_t) u, (uint8_t) a},
                                ST2.def[k].key, &v);
                if (!r) sets++;
            }
    CHECK(r == ZUI_SET_E_FULL, "full (%d)", r);
    CHECK(sets == ZUI_SET_ENTRIES, "every slot used first (%u)", sets);
    /* a load that needs room it lacks changes nothing */
    uint32_t line = 9;
    const char *t2 = "zxcfg 1\ntext.scale = 120\n";
    zui_set_view_t far = {0, ZUI_SET_USERS, ZUI_SET_APPS}, first = {0, 1, 1};
    CHECK(zui_set_load(&ST2, ZUI_LAYER_APP, far, t2, (uint32_t) strlen(t2), &line) ==
              ZUI_SET_E_FULL,
          "no room");
    CHECK(line == 0, "no line for no room");
    CHECK(zui_set_peek(&ST2, ZUI_LAYER_APP, far, "text.scale", 0, 0) == ZUI_SET_E_NOKEY,
          "nothing stored");
    /* replacing a value in place needs no new slot */
    CHECK(zui_set_set_int(&ST2, ZUI_LAYER_APP, first, "text.scale", 150) == 0, "in place");
    CHECK(zui_set_set_int(&ST2, ZUI_LAYER_USER, (zui_set_view_t){0, 1, 0}, "text.scale", 150) ==
              ZUI_SET_E_FULL,
          "a new one does not fit");
    /* a load that frees what it needs works */
    const char *t3 = "zxcfg 1\nui.gap = 0\n";
    CHECK(zui_set_load(&ST2, ZUI_LAYER_APP, first, t3, (uint32_t) strlen(t3), &line) == 0,
          "a load that shrinks a full layer");
    CHECK(zui_set_int(&ST2, first, "ui.gap") == 0 && zui_set_int(&ST2, first, "text.scale") == 100,
          "and applies");
    CHECK(zui_set_load(&ST2, ZUI_LAYER_APP, far, t2, (uint32_t) strlen(t2), &line) == 0,
          "now there is room");
}

static void test_names_layer(void)
{
    CHECK(strcmp(zui_set_layer_name(ZUI_LAYER_SYSTEM), "system") == 0, "system");
    CHECK(strcmp(zui_set_layer_name(ZUI_LAYER_APP), "app") == 0, "app");
    CHECK(strcmp(zui_set_layer_name(ZUI_LAYER_COUNT), "?") == 0, "?");
}

int main(void)
{
    test_schema();
    test_names();
    test_types();
    test_layers();
    test_cache();
    test_locks();
    test_watch();
    test_text();
    test_load_watch_and_full();
    test_names_layer();
    printf("%s test_zui_settings: %d/%d checks\n", fails ? "[FAIL]" : "[PASS]", checks - fails,
           checks);
    return fails ? 1 : 0;
}
