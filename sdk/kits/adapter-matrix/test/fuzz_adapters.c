/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* fuzz_adapters.c — fuzzing the JSON, MQTT and FIX adapters, the exact
 * decimal parser and validation.
 *
 * The first input byte picks the adapter (JSON, MQTT, FIX, FIX_PIPE); the
 * rest is the payload (MQTT topic: "fuzz/topic"). For every input:
 *   - za_ingest returns ZXV_OK and is deterministic (state, tag, reasons,
 *     fields and digest identical on a second run);
 *   - the label is consistent: accepted = NEITHER, neither/neutral/emerging,
 *     no reasons; refused = FALSE, false/neutral/emerging, at least one
 *     reason with a known code and a non-empty message, and no fields;
 *   - every field has a non-empty name of at most ZA_MAX_NAME bytes that
 *     za_field_find maps back to it, a known kind, and for a NUMBER a
 *     reduced rational (den > 0) that za_decimal_parse of its arrival text
 *     gives again, that zxv_rat_parse agrees with when it accepts that
 *     text, and that survives zxv_rat_format -> za_decimal_parse;
 *   - za_validate of an accepted record against a built-in contract gives
 *     ZXV_OK (record still NEITHER) or ZA_E_REFUSED (record FALSE, one
 *     evidence record, chain verifies);
 *   - za_decimal_parse of the whole payload is deterministic and agrees
 *     with zxv_rat_parse whenever both accept.
 *
 * Built like z-contract's fuzz_parse: -DZA_FUZZ_MAIN gives a deterministic
 * mutation fuzzer with built-in seeds (`make test`, `make fuzz`, CI under
 * ASan/UBSan); clang -fsanitize=fuzzer drives the same entry point.
 *
 *   fuzz_adapters ITERATIONS          ZA_FUZZ_SEED=n: another stream */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zadapter.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static unsigned long long g_acc[5], g_ref[5], g_fields, g_valid_ok, g_valid_ref;
static zc_contract *g_contract;

static const char k_contract[] =
    "fn any(value: rat[-10^6..10^6] den<=1000);\n"
    "fn pay(amount.value: rat[0..10^12] den|100, amount.currency: bytes[3..3], urgent: truth);\n"
    "fn order(Symbol: bytes[1..12], OrderQty: int[1..10^9], Price: rat[0..10^7] den|10000);\n";

static void die(const char *what, const uint8_t *d, size_t n) {
    fprintf(stderr, "fuzz_adapters: INVARIANT BROKEN: %s (input %zu bytes)\n", what, n);
    FILE *f = fopen("fuzz-crash.bin", "wb");
    if (f) {
        fwrite(d, 1, n, f);
        fclose(f);
        fprintf(stderr, "  input written to fuzz-crash.bin\n");
    }
    abort();
}

static int64_t gcd64(int64_t a, int64_t b) {
    if (a < 0) a = -a;
    while (b) {
        int64_t t = a % b;
        a = b;
        b = t;
    }
    return a;
}

/* Two records from the same input must be identical. */
static void same(const za_record *a, const za_record *b, const uint8_t *d, size_t n) {
    int32_t la, lb, ta, tb;
    uint32_t ra, rb, fa, fb;
    uint8_t da[32], db[32];
    za_record_state(a, &la, &ta);
    za_record_state(b, &lb, &tb);
    za_record_reason_count(a, &ra);
    za_record_reason_count(b, &rb);
    za_record_field_count(a, &fa);
    za_record_field_count(b, &fb);
    if (za_record_digest(a, da) != ZXV_OK || za_record_digest(b, db) != ZXV_OK)
        die("za_record_digest failed", d, n);
    if (la != lb || ta != tb || ra != rb || fa != fb || memcmp(da, db, 32) != 0)
        die("za_ingest not deterministic", d, n);
    for (uint32_t i = 0; i < ra; i++) {
        int32_t ca, cb;
        const char *ma, *mb;
        za_record_reason(a, i, &ca, &ma);
        za_record_reason(b, i, &cb, &mb);
        if (ca != cb || strcmp(ma, mb) != 0) die("reasons not deterministic", d, n);
    }
}

static void check_number(const za_record *r, uint32_t i, const uint8_t *d, size_t n) {
    int64_t num = 0, den = 0, n2 = 0, d2 = 0;
    const uint8_t *t = NULL;
    size_t tl = 0;
    char txt[128], fmt[128];
    size_t fl = 0;
    if (za_field_rat(r, i, &num, &den) != ZXV_OK) die("za_field_rat on a NUMBER", d, n);
    if (den <= 0 || num == INT64_MIN || gcd64(num, den) != 1) die("a NUMBER is not reduced", d, n);
    if (za_field_text(r, i, &t, &tl) != ZXV_OK || tl == 0) die("a NUMBER has no arrival text", d, n);
    if (za_decimal_parse((const char *) t, tl, &n2, &d2) != ZXV_OK || n2 != num || d2 != den)
        die("a NUMBER does not re-parse from its own text", d, n);
    if (tl < sizeof txt) {
        memcpy(txt, t, tl);
        txt[tl] = 0;
        if (zxv_rat_parse(txt, &n2, &d2) == ZXV_OK && (n2 != num || d2 != den))
            die("za_decimal_parse and zxv_rat_parse disagree on a field", d, n);
    }
    if (zxv_rat_format(num, den, fmt, sizeof fmt, &fl) != ZXV_OK) die("zxv_rat_format failed", d, n);
    if (!strchr(fmt, '/') &&
        (za_decimal_parse(fmt, fl, &n2, &d2) != ZXV_OK || n2 != num || d2 != den))
        die("the exact decimal text of a NUMBER does not parse back", d, n);
}

static void check_record(const za_record *r, const uint8_t *d, size_t n) {
    int32_t lp = -1, tag = -1, kind = 0;
    uint32_t nr = 0, nf = 0;
    char tn[64];
    size_t tl = 0;
    if (za_record_state(r, &lp, &tag) != ZXV_OK || zc_tag_format(tag, tn, sizeof tn, &tl) != ZXV_OK)
        die("state", d, n);
    za_record_reason_count(r, &nr);
    za_record_field_count(r, &nf);
    if (lp == ZXV_LP_NEITHER) {
        if (nr != 0 || strcmp(tn, "neither/neutral/emerging") != 0) die("accepted label", d, n);
    } else if (lp == ZXV_LP_FALSE) {
        if (nr == 0 || nr > ZA_MAX_REASONS || nf != 0 || strcmp(tn, "false/neutral/emerging") != 0)
            die("refused label", d, n);
        for (uint32_t i = 0; i < nr; i++) {
            int32_t code = 0;
            const char *m = NULL;
            if (za_record_reason(r, i, &code, &m) != ZXV_OK || code < ZA_R_MALFORMED ||
                code > ZA_R_UNSUPPORTED || !m || !*m)
                die("a refusal reason", d, n);
        }
    } else {
        die("an ingested record is neither accepted nor refused", d, n);
    }
    if (nf > ZA_MAX_FIELDS) die("too many fields", d, n);
    for (uint32_t i = 0; i < nf; i++) {
        const char *name = NULL;
        uint32_t back = 0;
        if (za_field(r, i, &name, &kind) != ZXV_OK || !name || !*name || strlen(name) > ZA_MAX_NAME)
            die("a field name", d, n);
        if (za_field_find(r, name, &back) != ZXV_OK || back != i) die("field names are not unique", d, n);
        if (kind < ZA_F_NUMBER || kind > ZA_F_NULL) die("a field kind", d, n);
        if (kind == ZA_F_NUMBER) check_number(r, i, d, n);
        g_fields++;
    }
}

static void validate(za_record *r, const uint8_t *d, size_t n) {
    static const char *const fns[] = {"any", "pay", "order"};
    zc_evlog *log = NULL;
    uint64_t count = 0;
    int32_t lp = -1;
    const char *fn = fns[(n ? d[n - 1u] : 0u) % 3u];
    if (zc_evlog_create(4, &log) != ZXV_OK) return;
    int32_t st = za_validate(r, g_contract, fn, log);
    za_record_state(r, &lp, NULL);
    zc_evlog_count(log, &count);
    if (st == ZXV_OK) {
        g_valid_ok++;
        if (lp != ZXV_LP_NEITHER || count != 0) die("a passing record changed state or logged", d, n);
    } else if (st == ZA_E_REFUSED) {
        g_valid_ref++;
        if (lp != ZXV_LP_FALSE || count != 1 || zc_evlog_verify(log) != ZXV_OK)
            die("a refused record is not FALSE with one verified evidence record", d, n);
    } else {
        die("za_validate returned an unexpected status", d, n);
    }
    zc_evlog_destroy(log);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    za_record *a = NULL, *b = NULL;
    int32_t kind, lp = -1;
    const uint8_t *p;
    size_t n;
    if (size == 0) return 0;
    if (!g_contract &&
        zc_parse(k_contract, strlen(k_contract), &g_contract, NULL, NULL, NULL, NULL, 0, NULL) != ZXV_OK)
        abort();
    kind = ZA_SRC_JSON + (int32_t) (data[0] % 4u);
    p = data + 1;
    n = size - 1u;
    if (za_ingest(kind, "fuzz/topic", p, n, &a) != ZXV_OK || za_ingest(kind, "fuzz/topic", p, n, &b) != ZXV_OK)
        die("za_ingest failed", data, size);
    same(a, b, data, size);
    check_record(a, data, size);
    za_record_state(a, &lp, NULL);
    if (lp == ZXV_LP_NEITHER) {
        g_acc[kind]++;
        validate(a, data, size);
    } else {
        g_ref[kind]++;
    }
    za_record_destroy(a);
    za_record_destroy(b);

    /* the decimal parser on its own */
    int64_t n1 = 0, d1 = 0, n2 = 0, d2 = 0;
    int32_t s1 = za_decimal_parse((const char *) p, n, &n1, &d1);
    int32_t s2 = za_decimal_parse((const char *) p, n, &n2, &d2);
    if (s1 != s2 || (s1 == ZXV_OK && (n1 != n2 || d1 != d2))) die("za_decimal_parse not deterministic", data, size);
    if (s1 != ZXV_OK && s1 != ZXV_E_FORMAT && s1 != ZXV_E_RANGE) die("za_decimal_parse status", data, size);
    if (s1 == ZXV_OK && (d1 <= 0 || gcd64(n1, d1) != 1)) die("za_decimal_parse not reduced", data, size);
    if (n < 64 && !memchr(p, 0, n)) {
        char t[64];
        memcpy(t, p, n);
        t[n] = 0;
        if (s1 == ZXV_OK && zxv_rat_parse(t, &n2, &d2) == ZXV_OK && (n2 != n1 || d2 != d1))
            die("za_decimal_parse and zxv_rat_parse disagree", data, size);
    }
    return 0;
}

#ifdef ZA_FUZZ_MAIN

static uint64_t g_rng = 0x9E3779B97F4A7C15ull;
static uint64_t rnd(void) {
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 7;
    g_rng ^= g_rng << 17;
    return g_rng;
}

/* Seeds: the selector byte, then a payload. FIX seeds carry correct
 * BodyLength and CheckSum, so mutants start from accepted messages. */
static const char *const k_seeds[] = {
    "\x00{\"amount\": {\"value\": 1250.50, \"currency\": \"EUR\"}, \"reference\": \"INV-1\", \"urgent\": false}",
    "\x00[1, -2.5e-3, \"x\", true, null, {\"a\": [0.1, 1E+2]}, \"\\u00e9\\ud83d\\ude00\"]",
    "\x00{\"value\": 87.125, \"unit\": \"C\", \"deep\": {\"a\": {\"b\": {\"c\": 9223372036854775807}}}}",
    "\x01{\"value\": 21.5, \"unit\": \"C\"}",
    "\x01  -3e2\r\n",
    "\x01OPEN",
    "\x03" "8=FIX.4.2|9=65|35=A|49=SERVER|56=CLIENT|34=177|52=20090107-18:15:16|98=0|108=30|10=062|",
    "\x03" "8=FIX.4.4|9=131|35=D|49=BUYSIDE|56=EXCHANGE|34=2|52=20261011-10:00:00.000|11=ORD-0001|"
    "55=ACME|54=1|38=100|40=2|44=12.3450|60=20261011-10:00:00.000|10=023|",
    "\x02" "8=FIX.4.2\x01" "9=65\x01" "35=A\x01" "49=SERVER\x01" "56=CLIENT\x01" "34=177\x01"
    "52=20090107-18:15:16\x01" "98=0\x01" "108=30\x01" "10=062\x01",
};

static const char *const k_dict[] = {
    "{", "}", "[", "]", ":", ",", "\"", "\\u", "\\ud800", "true", "false", "null", "-", "0", ".",
    "e", "E+", "e-", "1e400", "1e-30", "9223372036854775808", "0.000000000000000001", " ", "\n",
    "\xc3\xa9", "\xff", "\xef\xbb\xbf", "|", "\x01", "=", "8=FIX.4.4|", "9=", "10=", "35=D|",
    "44=", "38=", "95=", "96=", "55=", "269=", "270=", "a.b", "\"value\":", "#", "+",
};

static size_t mutate(uint8_t *buf, size_t n, size_t cap) {
    int rounds = 1 + (int) (rnd() % 4u);
    for (int r = 0; r < rounds; r++) {
        uint64_t op = rnd() % 6u;
        size_t pos = n > 1 ? 1u + (size_t) (rnd() % n) : n; /* keep the selector byte */
        if (pos > n) pos = n;
        if (op == 0 && n > 1) {
            buf[1u + (pos - 1u) % (n - 1u)] ^= (uint8_t) (1u << (rnd() % 8u));
        } else if (op == 1 && n > 1) {
            buf[1u + (pos - 1u) % (n - 1u)] = (uint8_t) rnd();
        } else if (op == 2) {
            const char *t = k_dict[rnd() % (sizeof k_dict / sizeof k_dict[0])];
            size_t tl = strlen(t);
            if (n + tl <= cap) {
                memmove(buf + pos + tl, buf + pos, n - pos);
                memcpy(buf + pos, t, tl);
                n += tl;
            }
        } else if (op == 3 && n > 1) {
            size_t at = 1u + (pos - 1u) % (n - 1u), len = 1u + (size_t) (rnd() % 8u);
            if (len > n - at) len = n - at;
            memmove(buf + at, buf + at + len, n - at - len);
            n -= len;
        } else if (op == 4 && n > 1) {
            size_t at = 1u + (pos - 1u) % (n - 1u), len = 1u + (size_t) (rnd() % 16u);
            if (len > n - at) len = n - at;
            if (n + len <= cap) {
                memmove(buf + at + len, buf + at, n - at);
                n += len;
            }
        } else if (op == 5) {
            buf[0] = (uint8_t) (rnd() % 4u); /* same payload, another adapter */
        } else if (n > 1) {
            n = pos;
        }
    }
    return n;
}


/* For FIX mutants: recompute BodyLength (9) and CheckSum (10) so that the
 * mutant reaches the field parser instead of stopping at the trailer. */
static size_t refix(uint8_t *buf, size_t n, size_t cap) {
    static uint8_t tmp[1 << 15];
    uint8_t dl = buf[0] % 4u == 2u ? 0x01 : (uint8_t) '|';
    size_t i = 1, b0, end, m;
    unsigned sum = 0;
    while (i < n && buf[i] != dl) i++; /* end of field 1 (8=...) */
    if (i + 3u >= n || buf[i + 1u] != '9' || buf[i + 2u] != '=') return n;
    b0 = i + 3u;
    while (b0 < n && buf[b0] != dl) b0++; /* end of field 2 (9=...) */
    if (b0 >= n) return n;
    b0++;
    end = n;
    for (size_t k = n; k > b0 + 2u; k--)
        if (buf[k - 3u] == dl && buf[k - 2u] == '1' && buf[k - 1u] == '0') {
            end = k - 2u;
            break;
        }
    if (end == n) return n;
    m = (size_t) snprintf((char *) tmp, sizeof tmp, "%.*s9=%zu%c", (int) (i + 1u), (const char *) buf, end - b0, dl);
    if (m + (end - b0) + 8u > cap || m + (end - b0) + 8u > sizeof tmp) return n;
    memcpy(tmp + m, buf + b0, end - b0);
    m += end - b0;
    for (size_t k = 1; k < m; k++) sum += tmp[k] == dl ? 1u : tmp[k];
    m += (size_t) snprintf((char *) tmp + m, 8, "10=%03u%c", sum % 256u, dl);
    memcpy(buf, tmp, m);
    return m;
}

int main(int argc, char **argv) {
    unsigned long long iters = argc > 1 ? strtoull(argv[1], NULL, 10) : 1000ull;
    const char *seed_env = getenv("ZA_FUZZ_SEED");
    const size_t nseeds = sizeof k_seeds / sizeof k_seeds[0];
    size_t len[sizeof k_seeds / sizeof k_seeds[0]];
    static uint8_t buf[1 << 15];
    if (seed_env) g_rng ^= strtoull(seed_env, NULL, 10) * 0x2545F4914F6CDD1Dull;
    for (size_t i = 0; i < nseeds; i++) {
        len[i] = 1u + strlen(k_seeds[i] + 1); /* the selector may be 0 */
        LLVMFuzzerTestOneInput((const uint8_t *) k_seeds[i], len[i]);
    }
    for (unsigned long long it = 0; it < iters; it++) {
        size_t s = (size_t) (rnd() % nseeds), n = len[s];
        memcpy(buf, k_seeds[s], n);
        if (rnd() % 8u == 0) { /* splice half of another seed's payload in */
            size_t o = (size_t) (rnd() % nseeds), at = 1u + (size_t) (rnd() % n);
            size_t take = (len[o] - 1u) / 2u;
            if (at + take <= sizeof buf) {
                memcpy(buf + at, k_seeds[o] + 1, take);
                if (at + take > n) n = at + take;
            }
        }
        n = mutate(buf, n, sizeof buf);
        if (n > 1 && buf[0] % 4u >= 2u && rnd() % 4u != 0) n = refix(buf, n, sizeof buf);
        LLVMFuzzerTestOneInput(buf, n);
    }
    printf("fuzz_adapters: %llu mutants + %zu seeds, 0 invariant failures\n", iters, nseeds);
    printf("  accepted/refused  json %llu/%llu  mqtt %llu/%llu  fix %llu/%llu  fix-pipe %llu/%llu\n",
           g_acc[1], g_ref[1], g_acc[2], g_ref[2], g_acc[3], g_ref[3], g_acc[4], g_ref[4]);
    printf("  %llu fields checked; validation passed %llu, refused %llu\n", g_fields, g_valid_ok,
           g_valid_ref);
    zc_contract_destroy(g_contract);
    return 0;
}

#endif /* ZA_FUZZ_MAIN */
