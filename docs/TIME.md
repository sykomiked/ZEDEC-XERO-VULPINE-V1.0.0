<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# Wall-clock time and time zones

TLS refuses to verify a server certificate without the current time, because
it cannot check expiry otherwise (see [TLS_CERTIFICATES.md](TLS_CERTIFICATES.md)).
A device that boots without knowing the time therefore cannot make a verified
connection. `kernel/src/timesvc` gives the system its time, from the RTC and
from SNTP, and converts it to local time.

## Getting the time at boot

```c
static timesvc_t clock;
timesvc_init(&clock, 0);                       /* floor = build time */
timesvc_set_rtc(&clock, rtc_unix_s, local_us); /* from the RTC driver, if any */

/* For each NTP server (DHCP option 42 first, then the configured list): */
uint64_t nonce = random64();
uint8_t q[SNTP_PKT_LEN], r[SNTP_PKT_LEN];
sntp_build_request(q, nonce);
int64_t t1 = local_us_now();
/* ... send q to server:123 over UDP, receive r ... */
int64_t t4 = local_us_now();
sntp_sample_t s;
if (sntp_parse_reply(r, len, nonce, t1, t4, ZXV_BUILD_UNIX, &s) == SNTP_OK)
    timesvc_add_sample(&clock, &s);

timesvc_commit(&clock, local_us_now());        /* needs 2 agreeing servers */
tls_client_set_trust(&c, &trust, timesvc_now_s(&clock, local_us_now()));
```

`local_us` is any monotonic microsecond counter, such as the timer driver's.
`timesvc_now_s()` returns 0 until the RTC or the network has given a
plausible time, and TLS fails closed on 0.

The DHCP client now asks for option 42 and keeps the first NTP server it
offers in `dhcp_client_t.ntp`. Networks that block public NTP usually hand
out their own server this way.

## What protects the time

| Threat | Defence |
|---|---|
| A forged reply from off the path | The request carries 64 random bits instead of the local clock, and the reply must echo them |
| A server that is not synchronised | Leap indicator 3, stratum 0 or 16+, and a root distance of 16 s or more are refused |
| A server that asks clients to back off | The kiss-o'-death code (RATE, DENY, RSTR) is returned to the caller |
| One wrong or lying server | At least two servers must agree within 1 s plus their round trips and root distances, and they must be a strict majority |
| Winding the clock back to revive expired certificates | The time never goes below the build time of the image |
| A flat RTC battery (2000-01-01) | RTC readings below the build time are ignored |
| The NTP era rollover in 2036 | Timestamps are read in the era nearest the build time |

**Not covered:** SNTP is not authenticated. An attacker on the path to every
server can still choose a time at or after the build time. Network Time
Security (RFC 8915) would close this and is not built yet.

## Time zones

`tz.h` converts UTC to local time in three ways:

- **Built-in table** (`tz_builtin_find("Europe/Paris")`): all 497 IANA zones,
  each with the POSIX rule from tzdata (currently 2026c). These are the zones'
  current rules, so they are exact from 2027 on but not for older dates.
- **TZif files** (`tz_tzif_parse`): the full history of a zone, for when the
  filesystem ships `/usr/share/zoneinfo`.
- **POSIX TZ strings** (`tz_posix_parse`), including the version 3 extensions
  for all-year daylight time.

Two zones cannot be written as a POSIX rule: Asia/Gaza and Asia/Hebron end
daylight time early in years when Ramadan falls inside it. For those the
built-in table is sometimes an hour off, and a TZif file is needed.

To update to a new tzdata release:

```
python3 kernel/src/timesvc/gen_tz_zones.py /usr/share/zoneinfo > kernel/src/timesvc/tz_zones.h
python3 kernel/src/timesvc/gen_tz_vectors.py /usr/share/zoneinfo > kernel/src/timesvc/test_tz_vectors.h
```

tzdata is in the public domain.

## Tests

`make -C kernel test-timesvc` is part of `verify-all`. It runs:

- `test_timesvc`: SNTP replies built from known times, so offset and round
  trip are checked exactly. There is a reply for every refusal reason, and
  era handling past 2036 is checked. The agreement rules are checked too:
  - a liar is outvoted;
  - a 2-2 split sets nothing;
  - the build floor holds;
  - a flat RTC is ignored;
  - the RTC cannot override the network.
- `test_tz`: every zone in the built-in table against Python's `zoneinfo`
  from 2027 to 2046. Each change of offset is checked a second before and at
  the instant. Twelve embedded TZif files are checked the same way from 1901
  to 2100. These include Lord Howe (30-minute daylight time), Chatham,
  Nuuk (negative rule times), Troll and Dublin.
  - Every truncation of a TZif file is refused.
  - Every day from 1901 to 6000 round-trips through the calendar code.
- Both tests run again under ASan and UBSan.
- A freestanding build for aarch64, i386, armv7a and rv32, which checks that
  nothing pulls in libc or a compiler helper.

The fuzz harness `fuzz/parsers/fuzz_time.c` covers the TZif parser, POSIX
strings and SNTP replies.
