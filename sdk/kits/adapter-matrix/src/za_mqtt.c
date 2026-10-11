/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* za_mqtt.c — MQTT PUBLISH payload bodies. The transport (CONNECT,
 * PUBLISH framing, QoS, sessions) belongs to the broker client; this
 * adapter takes the topic name and the payload bytes it delivered.
 *
 *   topic    MQTT 3.1.1 / 5.0 topic NAME rules: 1..65535 bytes of UTF-8,
 *            no NUL, and no '+' or '#' (wildcards belong to filters, never
 *            to a published name).
 *   payload  a JSON object or array (first non-space byte '{' or '['), or
 *            an exact decimal ("21.5", "-3e2") as the field "value", or
 *            other UTF-8 text as the TEXT field "value". Binary payloads
 *            are refused (UTF8). An empty payload (the MQTT idiom for
 *            clearing a retained message) gives a record with no fields.
 * Integer code only (R11). */
#include <string.h>

#include "za_internal.h"

void za_mqtt_into(za_record *rec, const char *topic, const uint8_t *s, size_t n) {
    size_t tl = strlen(topic), a = 0, b = n;
    int64_t num = 0, den = 1;
    if (tl == 0 || tl > 65535u) {
        za_rec_refuse(rec, ZA_R_TOPIC, "MQTT topic name must be 1..65535 bytes");
        return;
    }
    if (!za_utf8_ok((const uint8_t *) topic, tl)) {
        za_rec_refuse(rec, ZA_R_TOPIC, "MQTT topic name is not valid UTF-8");
        return;
    }
    if (strpbrk(topic, "+#")) {
        za_rec_refuse(rec, ZA_R_TOPIC, "MQTT topic name contains a wildcard ('+' or '#')");
        return;
    }
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) a++;
    while (b > a && (s[b - 1u] == ' ' || s[b - 1u] == '\t' || s[b - 1u] == '\r' || s[b - 1u] == '\n'))
        b--;
    if (a == b) return; /* empty payload: no fields */
    if (s[a] == '{' || s[a] == '[') {
        za_json_into(rec, s, n);
        return;
    }
    if (!za_utf8_ok(s, n)) {
        za_rec_refuse(rec, ZA_R_UTF8, "MQTT payload is neither JSON, a decimal, nor UTF-8 text");
        return;
    }
    int32_t st = za_decimal_parse((const char *) s + a, b - a, &num, &den);
    if (st == ZXV_OK) {
        (void) za_rec_add(rec, "value", 5, ZA_F_NUMBER, num, den, s + a, b - a);
        return;
    }
    if (st == ZXV_E_RANGE) { /* a number, but not an exact int64 one: never kept as text */
        za_rec_refuse(rec, ZA_R_NUMBER, "MQTT payload is a number with no exact int64 rational form");
        return;
    }
    (void) za_rec_add(rec, "value", 5, ZA_F_TEXT, 0, 1, s, n);
}
