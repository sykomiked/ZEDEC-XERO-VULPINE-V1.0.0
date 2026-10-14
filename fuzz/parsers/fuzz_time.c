/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* fuzz_time.c — the TZif parser, POSIX TZ strings and SNTP replies
 * (timesvc/tz.c, timesvc/sntp.c), all of which read network or disk data.
 *
 * Input: byte 0 picks the parser (0 TZif, 1 POSIX string, 2 SNTP reply);
 * the rest is its input. Properties: no crash or sanitizer report; a
 * lookup's offset stays within 25 hours and its abbreviation is terminated;
 * an accepted SNTP reply has a non-negative round trip. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "tz.h"
#include "sntp.h"
#include "fuzz_in.h"

static void check_info(const tz_info_t *i)
{
    if (i->utoff < -25 * 3600 || i->utoff > 25 * 3600) abort();
    if (memchr(i->abbr, 0, TZ_ABBR_MAX) == 0) abort();
}

static const int64_t probes[] = {TZ_MIN_UNIX,  -1,           0,          1791763200,
                                 2147483648LL, 4102444800LL, TZ_MAX_UNIX};

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 1 || size > 65536) return 0;
    uint8_t *buf = fz_dup(data + 1, size - 1);
    uint32_t n = (uint32_t) (size - 1);
    tz_info_t info;
    switch (data[0] % 3) {
    case 0: {
        tz_tzif_t z;
        if (tz_tzif_parse(&z, buf, n))
            for (unsigned i = 0; i < sizeof probes / sizeof *probes; i++) {
                tz_tzif_lookup(&z, probes[i], &info);
                check_info(&info);
            }
        break;
    }
    case 1: {
        tz_posix_t z;
        if (tz_posix_parse(&z, (const char *) buf, n))
            for (unsigned i = 0; i < sizeof probes / sizeof *probes; i++) {
                tz_posix_lookup(&z, probes[i], &info);
                check_info(&info);
            }
        break;
    }
    default: {
        sntp_sample_t s;
        uint64_t nonce = n >= 32 ? ((uint64_t) buf[24] << 56) | buf[31] : 0;
        if (sntp_parse_reply(buf, n, nonce, 1000, 50000, 1791763200, &s) == SNTP_OK &&
            s.delay_us < 0)
            abort();
        break;
    }
    }
    free(buf);
    return 0;
}
