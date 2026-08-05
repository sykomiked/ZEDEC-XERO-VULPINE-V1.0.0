/* boot_evidence.c — Boot evidence measurement system
 *
 * Accumulates a SHA-256 hash over all boot messages and assigns
 * sequential evidence IDs. This makes the boot log self-authenticating.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#include "boot_evidence.h"
#include "../src/robin_debanks/sha256.h"
#include <string.h>

/* Extern UART functions */
extern void uart_puts(const char *s);
extern void uart_put_hex(uint64_t val);
extern void uart_put_dec(uint64_t val);

/* We use the existing sha256.c implementation */
extern void sha256(const uint8_t *data, size_t len, uint8_t out[32]);

static uint32_t evidence_counter = 0;
static uint8_t measurement[32];
static bool initialized = false;

/* Simpler approach: accumulate all messages and hash at the end */
static char evidence_log[8192];
static size_t evidence_log_len = 0;

void boot_evidence_init(void) {
    evidence_counter = 0;
    evidence_log_len = 0;
    memset(measurement, 0, 32);
    initialized = true;
}

uint32_t boot_evidence_record(const char *msg) {
    if (!initialized) boot_evidence_init();
    evidence_counter++;

    /* Append message to evidence log for final hashing */
    if (evidence_log_len < sizeof(evidence_log) - 128) {
        /* Append evidence ID as prefix */
        char id_str[8];
        int id_len = 0;
        uint32_t id = evidence_counter;
        if (id == 0) { id_str[id_len++] = '0'; }
        else {
            char tmp[8]; int tl = 0;
            while (id > 0) { tmp[tl++] = '0' + (id % 10); id /= 10; }
            while (tl > 0) id_str[id_len++] = tmp[--tl];
        }
        id_str[id_len] = '\0';

        evidence_log[evidence_log_len++] = 'E';
        for (int i = 0; i < id_len; i++) evidence_log[evidence_log_len++] = id_str[i];
        evidence_log[evidence_log_len++] = ':';

        const char *p = msg;
        while (*p && evidence_log_len < sizeof(evidence_log) - 1) {
            evidence_log[evidence_log_len++] = *p++;
        }
        evidence_log[evidence_log_len++] = '\n';
    }

    return evidence_counter;
}

uint32_t boot_evidence_count(void) {
    return evidence_counter;
}

void boot_evidence_get_hash(uint8_t out[32]) {
    if (!initialized) { memset(out, 0, 32); return; }
    sha256((const uint8_t *)evidence_log, evidence_log_len, out);
}

void boot_evidence_final(void) {
    if (!initialized) return;

    sha256((const uint8_t *)evidence_log, evidence_log_len, measurement);

    uart_puts("\n[BOOT] Evidence measurement: ");
    for (int i = 0; i < 32; i++) {
        if (measurement[i] < 16) uart_puts("0");
        uart_put_hex((uint64_t)measurement[i]);
    }
    uart_puts("\n");
    uart_puts("[BOOT] Evidence count: ");
    uart_put_dec((uint64_t)evidence_counter);
    uart_puts(" messages recorded\n");
}
