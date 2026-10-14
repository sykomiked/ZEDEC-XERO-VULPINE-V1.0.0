/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zt_gram_lib.c — grammars for the shorthands, for zt_gram_compile.
 *
 * zxt-sutra  The treaty compact form exactly as zxt_parse_sutra reads it
 *            (src/treaty/zxt.c): canonical numbers (no leading zeros), names
 *            and ids that start with a letter and do not end in '-', 1..16
 *            PARTY lines then 0..64 OWE lines, single spaces, '\n' line
 *            ends. What a grammar cannot say stays with zxt_validate: the
 *            weights' sum, claim <= deposit, distinct and known party ids,
 *            lowest-terms fractions, the PIC S9(18) bound on 18-digit
 *            numbers. test_zt_gram checks that every canonical treaty the
 *            emitter writes is admitted.
 * lpx-tag    The 64-state tag's text form "base/polarity/phase"
 *            (lpx_base_name, lpx_pol_name, lpx_phase_name).
 * json       RFC 8259 JSON text. Strings are bytes: any byte >= 0x80 is
 *            admitted, so invalid UTF-8 is not refused.
 */
#include "zt_gram.h"

static const char G_SUTRA[] =
    "root  ::= \"TREATY \" name \" \" asset \" \" op \"\\n\" party{1,16} owe{0,64} \"END\\n\"\n"
    "op    ::= \"NET\" | \"SETL\"\n"
    "name  ::= [A-Z] ([A-Z0-9\\-]{0,28} [A-Z0-9])?\n"
    "asset ::= [A-Z]{3}\n"
    "id    ::= [A-Z] ([A-Z0-9\\-]{0,6} [A-Z0-9])?\n"
    "party ::= \"PARTY \" id \" \" amt \" \" amt \" \" wnum \"/\" wden \"\\n\"\n"
    "owe   ::= \"OWE \" id \" \" id \" \" pos \"\\n\"\n"
    "amt   ::= \"0\" | pos\n"
    "pos   ::= [1-9] [0-9]{0,17}\n"
    "wnum  ::= \"0\" | wden\n"
    "wden  ::= [1-9] [0-9]{0,8}\n";

static const char G_LPX[] =
    "root  ::= base \"/\" pol \"/\" phase\n"
    "base  ::= \"false\" | \"neither\" | \"both\" | \"true\"\n"
    "pol   ::= \"entropic\" | \"neutral\" | \"forbidden\" | \"negentropic\"\n"
    "phase ::= \"potential\" | \"emerging\" | \"dissolving\" | \"manifest\"\n";

static const char G_JSON[] =
    "root   ::= ws value ws\n"
    "value  ::= object | array | string | number | \"true\" | \"false\" | \"null\"\n"
    "object ::= \"{\" ws ( member ( ws \",\" ws member )* ws )? \"}\"\n"
    "member ::= string ws \":\" ws value\n"
    "array  ::= \"[\" ws ( value ( ws \",\" ws value )* ws )? \"]\"\n"
    "string ::= \"\\\"\" char* \"\\\"\"\n"
    "char   ::= [^\"\\\\\\x00-\\x1f] | \"\\\\\" esc\n"
    "esc    ::= [\"\\\\/bfnrt] | \"u\" hex hex hex hex\n"
    "hex    ::= [0-9a-fA-F]\n"
    "number ::= \"-\"? int frac? exp?\n"
    "int    ::= \"0\" | [1-9] [0-9]*\n"
    "frac   ::= \".\" [0-9]+\n"
    "exp    ::= [eE] [\\-+]? [0-9]+\n"
    "ws     ::= [ \\t\\n\\r]*\n";

static bool eq(const char *a, const char *b)
{
    while (*a && *a == *b) a++, b++;
    return *a == *b;
}

const char *zt_gram_lib(const char *name, uint32_t *len)
{
    const char *g = 0;
    uint32_t n = 0;
    if (!name) return 0;
    if (eq(name, "zxt-sutra")) {
        g = G_SUTRA;
        n = sizeof G_SUTRA - 1u;
    } else if (eq(name, "lpx-tag")) {
        g = G_LPX;
        n = sizeof G_LPX - 1u;
    } else if (eq(name, "json")) {
        g = G_JSON;
        n = sizeof G_JSON - 1u;
    }
    if (g && len) *len = n;
    return g;
}
