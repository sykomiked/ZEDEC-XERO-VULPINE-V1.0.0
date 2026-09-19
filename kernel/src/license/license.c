/* license.c — Apache-2.0 license implementation
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 */
#include "license.h"

/* For freestanding kernel, provide minimal output abstraction */
#ifdef __KERNEL__
#include "freestanding.h"
extern void fb_puts(const char *s);
extern void uart_puts(const char *s);
static void puts_impl(const char *s) {
#ifdef __ARM_ARCH__
    uart_puts(s);
#else
    fb_puts(s);
#endif
}
#else
#include <stdio.h>
static void puts_impl(const char *s) { printf("%s", s); }
#endif

/* ===== Apache-2.0 Full Text ===== */
static const char *apache_full_text =
    "\n"
    "========================================\n"
    "  Apache License 2.0\n"
    "========================================\n"
    "\n"
    "Licensed under the Apache License, Version 2.0 (the \"License\");\n"
    "you may not use this file except in compliance with the License.\n"
    "You may obtain a copy of the License at\n"
    "\n"
    "    http://www.apache.org/licenses/LICENSE-2.0\n"
    "\n"
    "Unless required by applicable law or agreed to in writing, software\n"
    "distributed under the License is distributed on an \"AS IS\" BASIS,\n"
    "WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.\n"
    "See the License for the specific language governing permissions and\n"
    "limitations under the License.\n"
    "\n"
    "Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC\n"
    "========================================\n"
    "\n";

/* ===== API Implementation ===== */

void license_print(void) {
    puts_impl(apache_full_text);
}

void license_print_all(void) {
    puts_impl(apache_full_text);
}

const char *license_get_version(void) {
    return LICENSE_VERSION;
}

const char *license_get_issuer(void) {
    return LICENSE_ATTRIBUTION_ENTITY;
}

const char *license_get_name(license_id_t id) {
    switch (id) {
        case LICENSE_APACHE_20: return LICENSE_FULL_NAME;
        default:               return "Unknown License";
    }
}

const char *license_get_full_text(license_id_t id) {
    switch (id) {
        case LICENSE_APACHE_20: return apache_full_text;
        default:               return "";
    }
}

const char *license_get_attribution(void) {
    return "Author: Michael Laurence Curzi | "
           "36N9 Genetics, LLC | admin@zedec.ai | "
           "Apache-2.0";
}

const char *license_spdx_bundle(void) {
    return LICENSE_SPDX;
}

bool license_is_share_alike(license_id_t id) {
    /* Apache-2.0 is permissive, not copyleft */
    (void)id;
    return false;
}

bool license_travels_together(license_id_t id) {
    /* Apache-2.0 derivatives may be relicensed */
    (void)id;
    return false;
}

int license_precedence_rank(license_id_t id) {
    /* Single license, no precedence */
    (void)id;
    return 0;
}
