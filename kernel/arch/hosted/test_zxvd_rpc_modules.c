/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zxvd_rpc_modules.c — the module methods (zxvd_rpc_modules.h) through
 * zxvd_rpc_handle_line, the same door the socket uses: every method's result,
 * its round trip where it has one, and every refusal (-32602 for a bad
 * parameter, ZXVD_E_MODULE for input the module rejects). Also checks the
 * capability rule: with the books open, no module method changes them.
 * Run by test_zxvd_rpc.sh after test_zxvd_rpc. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "zxvd_rpc.h"
#include "zxvd_rpc_modules.h"

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

static zxvd_buf_t OUT;
static char LINE[ZXVD_RPC_MAX_LINE + 64];

/* one request built from a method and a params object body -> its reply */
static const char *call(const char *method, const char *params_fmt, ...)
    __attribute__((format(printf, 2, 3)));
static const char *call(const char *method, const char *params_fmt, ...)
{
    int n = snprintf(LINE, sizeof LINE, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"%s\"", method);
    if (params_fmt) {
        va_list ap;
        va_start(ap, params_fmt);
        n += snprintf(LINE + n, sizeof LINE - (size_t) n, ",\"params\":{");
        n += vsnprintf(LINE + n, sizeof LINE - (size_t) n, params_fmt, ap);
        n += snprintf(LINE + n, sizeof LINE - (size_t) n, "}");
        va_end(ap);
    }
    snprintf(LINE + n, sizeof LINE - (size_t) n, "}");
    if (!zxvd_rpc_handle_line(LINE, strlen(LINE), &OUT)) return "<none>";
    return OUT.p ? OUT.p : "";
}

static bool ok(const char *r)
{
    return !strncmp(r, "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":", 33);
}

/* the error code of a reply, or 0 */
static int code(const char *r)
{
    const char *p = strstr(r, "\"error\":{\"code\":");
    return p ? atoi(p + 16) : 0;
}

/* copy the string value of "key" (first occurrence) into out */
static bool field(const char *r, const char *key, char *out, size_t cap)
{
    char pat[64];
    snprintf(pat, sizeof pat, "\"%s\":\"", key);
    const char *p = strstr(r, pat), *e;
    if (!p) return false;
    p += strlen(pat);
    e = strchr(p, '"');
    if (!e || (size_t) (e - p) >= cap) return false;
    memcpy(out, p, (size_t) (e - p));
    out[e - p] = 0;
    return true;
}

static long num(const char *r, const char *key)
{
    char pat[64];
    snprintf(pat, sizeof pat, "\"%s\":", key);
    const char *p = strstr(r, pat);
    return p ? atol(p + strlen(pat)) : -999999;
}

#define REFUSED(r, want, what) CHECK(code(r) == (want), "%s: %s", what, r)

/* ---------------- discovery and dispatch ---------------- */

static void test_dispatch(void)
{
    const char *r = call("rpc.discover", NULL);
    CHECK(strstr(r, "\"aipi.status\",\"freight.pack\",\"freight.unpack\",\"harmonic.encode\","
                    "\"harmonic.decode\",\"stream.plan\",\"stream.segment\",\"ehop.bands\","
                    "\"ehop.seal\",\"ehop.open\",\"web4.cid\",\"web4.agent_card\","
                    "\"web4.card_verify\"]}") != NULL,
          "rpc.discover lists the module methods after zxvd's own: %s", r);
    REFUSED(call("freight.nope", NULL), -32601, "an unknown module method");
    REFUSED(call("freight.pac", NULL), -32601, "a prefix is not a method");
    /* a notification to a module method runs and gets no reply */
    const char *note = "{\"jsonrpc\":\"2.0\",\"method\":\"ehop.bands\"}";
    CHECK(!zxvd_rpc_handle_line(note, strlen(note), &OUT), "a notification gets no reply");
}

/* ---------------- freight ---------------- */

static char DATAHEX[8000], PKTHEX[8000], HDRHEX[200], SUB[8000];

static void test_freight(void)
{
    char s[64];
    /* 50 bytes: default k = 3 (ceil(50 / 20)), id = le64 of the payload SHA-256 */
    for (int i = 0; i < 50; i++) sprintf(DATAHEX + 2 * i, "%02x", (unsigned) ((i * 7 + 1) & 0xff));
    const char *r = call("freight.pack", "\"data_hex\":\"%s\"", DATAHEX);
    CHECK(ok(r) && num(r, "k") == 3 && num(r, "payload_len") == 50 && num(r, "rows") == 168,
          "pack picks the fewest data rows: %.200s", r);
    CHECK(field(r, "freight_id", s, sizeof s) && field(r, "header_hex", HDRHEX, sizeof HDRHEX) &&
              strlen(HDRHEX) == 128 && field(r, "packets_hex", PKTHEX, sizeof PKTHEX) &&
              strlen(PKTHEX) == 2 * 3528,
          "pack: id, 64-byte header, 168 x 21-byte packets");
    CHECK(!strncmp(HDRHEX, "5a584648", 8), "the header starts with ZXFH");
    /* systematic: row 0 carries the first 20 bytes after its index */
    CHECK(!strncmp(PKTHEX, "00", 2) && !strncmp(PKTHEX + 2, DATAHEX, 40), "row 0 is data row 0");

    /* any k rows rebuild it: the last three parity rows, in reverse */
    SUB[0] = 0;
    for (int row = 167; row >= 165; row--) strncat(SUB, PKTHEX + 42 * row, 42);
    r = call("freight.unpack", "\"header_hex\":\"%s\",\"packets_hex\":\"%s\"", HDRHEX, SUB);
    char got[200];
    CHECK(ok(r) && field(r, "data_hex", got, sizeof got) && !strcmp(got, DATAHEX) &&
              num(r, "packets_received") == 3 && num(r, "k") == 3,
          "unpack from three parity rows: %.200s", r);
    /* duplicates are ignored: two copies of one row are still too few */
    snprintf(SUB, sizeof SUB, "%.42s%.42s%.42s", PKTHEX + 42 * 9, PKTHEX + 42 * 9, PKTHEX + 42 * 4);
    REFUSED(call("freight.unpack", "\"header_hex\":\"%s\",\"packets_hex\":\"%s\"", HDRHEX, SUB),
            ZXVD_E_MODULE, "duplicate rows do not count twice");
    CHECK(strstr(OUT.p, "fewer than k distinct rows") != NULL, "says why: %s", OUT.p);
    /* a corrupted row: caught by the payload SHA-256 */
    snprintf(SUB, sizeof SUB, "%.126s", PKTHEX);
    SUB[10] = SUB[10] == '0' ? '1' : '0';
    REFUSED(call("freight.unpack", "\"header_hex\":\"%s\",\"packets_hex\":\"%s\"", HDRHEX, SUB),
            ZXVD_E_MODULE, "a corrupted row");
    CHECK(strstr(OUT.p, "SHA-256") != NULL, "names the hash: %s", OUT.p);
    char bad[200];
    snprintf(bad, sizeof bad, "%s", HDRHEX);
    bad[0] = '0';
    REFUSED(call("freight.unpack", "\"header_hex\":\"%s\",\"packets_hex\":\"%.126s\"", bad, PKTHEX),
            ZXVD_E_MODULE, "a header without its magic");
    REFUSED(
        call("freight.unpack", "\"header_hex\":\"%s\",\"packets_hex\":\"%.124s\"", HDRHEX, PKTHEX),
        -32602, "a partial packet");
    REFUSED(call("freight.unpack", "\"header_hex\":\"%s\",\"packets_hex\":\"\"", HDRHEX), -32602,
            "no packets");
    REFUSED(call("freight.unpack", "\"header_hex\":\"%.126s\",\"packets_hex\":\"%.126s\"", HDRHEX,
                 PKTHEX),
            -32602, "a short header");
    REFUSED(call("freight.unpack", "\"packets_hex\":\"%.126s\"", PKTHEX), -32602, "no header");

    /* explicit k and id (a decimal string, as 64-bit values travel) */
    r = call("freight.pack",
             "\"data_hex\":\"ABCDEF\",\"k\":168,\"freight_id\":\"18446744073709551615\"");
    CHECK(ok(r) && num(r, "k") == 168 && field(r, "freight_id", s, sizeof s) &&
              !strcmp(s, "18446744073709551615"),
          "explicit k and a 64-bit id: %.200s", r);
    r = call("freight.pack", "\"data_hex\":\"\"");
    CHECK(ok(r) && num(r, "k") == 1 && num(r, "payload_len") == 0, "an empty payload: %.120s", r);
    REFUSED(call("freight.pack", "\"data_hex\":\"abc\""), -32602, "odd hex");
    REFUSED(call("freight.pack", "\"data_hex\":\"zz\""), -32602, "not hex");
    REFUSED(call("freight.pack", "\"data_hex\":12"), -32602, "a number for hex");
    REFUSED(call("freight.pack", NULL), -32602, "no data");
    REFUSED(call("freight.pack", "\"data_hex\":\"%s\",\"k\":2", DATAHEX), -32602, "k too small");
    REFUSED(call("freight.pack", "\"data_hex\":\"00\",\"k\":0"), -32602, "k = 0");
    REFUSED(call("freight.pack", "\"data_hex\":\"00\",\"k\":169"), -32602, "k = 169");
    REFUSED(call("freight.pack", "\"data_hex\":\"00\",\"freight_id\":\"-1\""), -32602,
            "a signed id");
    REFUSED(call("freight.pack", "\"data_hex\":\"00\",\"freight_id\":\"18446744073709551616\""),
            -32602, "an id past 2^64");
    REFUSED(call("freight.pack", "\"data_hex\":\"00\",\"freight_id\":1.5"), -32602, "a fraction");
    /* 3361 bytes is one over a full freight */
    static char big[2 * 3361 + 1];
    memset(big, 'a', sizeof big - 1);
    REFUSED(call("freight.pack", "\"data_hex\":\"%s\"", big), -32602, "over 3360 bytes");
    big[2 * 3360] = 0;
    r = call("freight.pack", "\"data_hex\":\"%s\"", big);
    CHECK(ok(r) && num(r, "k") == 168, "exactly 3360 bytes fills k = 168");
}

/* ---------------- harmonic ---------------- */

static void test_harmonic(void)
{
    char f[64];
    const char *r =
        call("harmonic.encode",
             "\"trunk\":3,\"shell\":2,\"sync\":true,\"s_plus\":[1,2,3],\"s_minus\":[4,5]");
    /* W1: tag, then L-B-L-B-L words */
    CHECK(ok(r) && num(r, "tag") == 123 && num(r, "trunk_hz") == 444 &&
              field(r, "frame_hex", f, sizeof f) &&
              !strcmp(f, "7b"
                         "01000000"
                         "00000004"
                         "02000000"
                         "00000005"
                         "03000000"),
          "encode lays out the frame: %s", r);
    r = call("harmonic.decode", "\"frame_hex\":\"%s\"", f);
    CHECK(ok(r) && strstr(r, "\"trunk\":3,\"shell\":2,\"sync\":true,\"axis_trunk\":true") &&
              strstr(r, "\"s_plus\":[1,2,3],\"s_minus\":[4,5]") && strstr(r, "\"idle\":false"),
          "decode reverses it: %s", r);
    /* W5: equal rails agree (TRUE); opposed equal rails are GLUT */
    r = call("harmonic.encode", "\"trunk\":9,\"s_plus\":[65536,65536,0],\"s_minus\":[65536,65536]");
    field(r, "frame_hex", f, sizeof f);
    r = call("harmonic.decode", "\"frame_hex\":\"%s\"", f);
    CHECK(strstr(r, "\"truth\":\"TRUE\"") && strstr(r, "\"axis_trunk\":false") &&
              num(r, "correlation_q16") == 131072 && num(r, "trunk_hz") == 1111,
          "constructive rails on the expansion trunk: %s", r);
    r = call("harmonic.encode",
             "\"trunk\":0,\"s_plus\":[65536,65536,0],\"s_minus\":[-65536,-65536]");
    field(r, "frame_hex", f, sizeof f);
    r = call("harmonic.decode", "\"frame_hex\":\"%s\"", f);
    CHECK(strstr(r, "\"truth\":\"GLUT\"") && strstr(r, "\"s_minus\":[-65536,-65536]"),
          "directly opposed strong rails: %s", r);
    /* the threshold decides: a weak agreement is GLUT above it, TRUE below */
    r = call("harmonic.encode", "\"trunk\":0,\"s_plus\":[256,0,0],\"s_minus\":[256,0]");
    field(r, "frame_hex", f, sizeof f);
    CHECK(strstr(call("harmonic.decode", "\"frame_hex\":\"%s\"", f), "\"truth\":\"GLUT\""),
          "under the default threshold: %s", OUT.p);
    CHECK(strstr(call("harmonic.decode", "\"frame_hex\":\"%s\",\"threshold_q16\":0", f),
                 "\"truth\":\"TRUE\""),
          "over a zero threshold: %s", OUT.p);
    r = call("harmonic.decode", "\"frame_hex\":\"%042d\"", 0);
    CHECK(strstr(r, "\"truth\":\"UNKNOWN\"") && strstr(r, "\"idle\":false"), "all zero: %s", r);
    /* W4: the idle pilot, "ZXV1" big-endian in the B words */
    r = call("harmonic.decode", "\"frame_hex\":\"00"
                                "3156585a"
                                "5a585631"
                                "3156585a"
                                "5a585631"
                                "3156585a\"");
    CHECK(strstr(r, "\"idle\":true") && strstr(r, "\"truth\":\"NEUTRAL\""), "idle pilot: %s", r);

    REFUSED(call("harmonic.encode", "\"trunk\":10,\"s_plus\":[0,0,0],\"s_minus\":[0,0]"), -32602,
            "trunk 10");
    REFUSED(
        call("harmonic.encode", "\"trunk\":1,\"shell\":10,\"s_plus\":[0,0,0],\"s_minus\":[0,0]"),
        -32602, "shell 10");
    REFUSED(call("harmonic.encode", "\"trunk\":1,\"sync\":1,\"s_plus\":[0,0,0],\"s_minus\":[0,0]"),
            -32602, "sync not a boolean");
    REFUSED(call("harmonic.encode", "\"trunk\":1,\"s_plus\":[0,0],\"s_minus\":[0,0]"), -32602,
            "two S+ words");
    REFUSED(call("harmonic.encode", "\"trunk\":1,\"s_plus\":[0,0,0,0],\"s_minus\":[0,0]"), -32602,
            "four S+ words");
    REFUSED(call("harmonic.encode", "\"trunk\":1,\"s_plus\":[0,0,4294967296],\"s_minus\":[0,0]"),
            -32602, "a word past 32 bits");
    REFUSED(call("harmonic.encode", "\"trunk\":1,\"s_plus\":[0,0,-2147483649],\"s_minus\":[0,0]"),
            -32602, "a word below -2^31");
    REFUSED(call("harmonic.encode", "\"trunk\":1,\"s_plus\":[0,0,0.5],\"s_minus\":[0,0]"), -32602,
            "a fractional word");
    REFUSED(call("harmonic.encode", "\"trunk\":1,\"s_plus\":[0,0,0]"), -32602, "no S- rail");
    REFUSED(call("harmonic.encode", "\"s_plus\":[0,0,0],\"s_minus\":[0,0]"), -32602, "no trunk");
    REFUSED(call("harmonic.decode", "\"frame_hex\":\"00\""), -32602, "a short frame");
    REFUSED(call("harmonic.decode", "\"frame_hex\":\"%044d\"", 0), -32602, "a long frame");
    REFUSED(call("harmonic.decode", "\"frame_hex\":\"%042d\",\"threshold_q16\":2147483648", 0),
            -32602, "a threshold past int32");
}

/* ---------------- stream ---------------- */

static void test_stream(void)
{
    const char *r =
        call("stream.plan", "\"units\":[[100,0,true],[50,33000,false],[70,1000000,true],"
                            "[20,1500000,false]],\"target_us\":1000000,\"max_us\":2000000,"
                            "\"tail_us\":33000");
    CHECK(ok(r) &&
              strstr(r, "{\"segments\":[{\"first_unit\":0,\"nunits\":2,\"offset\":0,\"len\":150,"
                        "\"t_start_us\":\"0\",\"dur_us\":1000000,\"key_start\":true},"
                        "{\"first_unit\":2,\"nunits\":2,\"offset\":150,\"len\":90,"
                        "\"t_start_us\":\"1000000\",\"dur_us\":533000,\"key_start\":true}]}"),
          "plan cuts at the keyframe past target: %s", r);
    /* no keyframe: max_us forces the cut; max_bytes bounds a segment */
    r = call("stream.plan", "\"units\":[[10,0,true],[10,500,false],[10,1000,false]],"
                            "\"target_us\":100,\"max_us\":600");
    CHECK(strstr(r, "\"nunits\":2") && strstr(r, "\"first_unit\":2,\"nunits\":1,\"offset\":20"),
          "max_us cuts without a keyframe: %s", r);
    r = call("stream.plan", "\"units\":[[10,0,true],[10,1,false]],\"target_us\":100,"
                            "\"max_us\":600,\"max_bytes\":15");
    CHECK(strstr(r, "\"first_unit\":1,\"nunits\":1"), "max_bytes cuts: %s", r);
    r = call("stream.plan", "\"units\":[],\"target_us\":1,\"max_us\":1");
    CHECK(ok(r) && strstr(r, "{\"segments\":[]}"), "no units, no segments: %s", r);
    REFUSED(call("stream.plan", "\"units\":[[10,0,true],[10,1,false]],\"target_us\":100,"
                                "\"max_us\":600,\"max_bytes\":5"),
            ZXVD_E_MODULE, "a unit over max_bytes");
    REFUSED(call("stream.plan", "\"units\":[[1,5,true],[1,4,true]],\"target_us\":1,\"max_us\":1"),
            -32602, "pts going backwards");
    REFUSED(call("stream.plan", "\"units\":[[1,5,true]],\"target_us\":0,\"max_us\":1"), -32602,
            "target 0");
    REFUSED(call("stream.plan", "\"units\":[[1,5,true]],\"target_us\":10,\"max_us\":9"), -32602,
            "max below target");
    REFUSED(call("stream.plan", "\"units\":[[1,5,true]],\"max_us\":9"), -32602, "no target");
    REFUSED(call("stream.plan", "\"target_us\":1,\"max_us\":1"), -32602, "no units");
    REFUSED(call("stream.plan", "\"units\":{},\"target_us\":1,\"max_us\":1"), -32602,
            "units not an array");
    REFUSED(call("stream.plan", "\"units\":[[1,5]],\"target_us\":1,\"max_us\":1"), -32602,
            "a unit of two");
    REFUSED(call("stream.plan", "\"units\":[[1,5,true,1]],\"target_us\":1,\"max_us\":1"), -32602,
            "a unit of four");
    REFUSED(call("stream.plan", "\"units\":[[1,5,0]],\"target_us\":1,\"max_us\":1"), -32602,
            "keyframe not a boolean");
    REFUSED(call("stream.plan", "\"units\":[[4294967296,5,true]],\"target_us\":1,\"max_us\":1"),
            -32602, "a unit past 4 GiB");
    REFUSED(call("stream.plan",
                 "\"units\":[[4294967295,0,true],[1,1,true]],\"target_us\":1,\"max_us\":1"),
            -32602, "units adding up past 4 GiB");
    char units[2000];
    int n = 0;
    for (int i = 0; i < 121; i++) {
        int wrote = snprintf(units + n, sizeof units - (size_t) n, "%s[1,%d,true]", i ? "," : "", i);
        if (wrote < 0 || (size_t) wrote >= sizeof units - (size_t) n) {
            n = (int) sizeof units - 1;
            break;
        }
        n += wrote;
    }
    REFUSED(call("stream.plan", "\"units\":[%s],\"target_us\":1,\"max_us\":1", units), -32602,
            "121 units");
    CHECK(strstr(OUT.p, "at most 120 units") != NULL, "names the limit: %s", OUT.p);

    /* a segment's CID is the CIDv1 web4.cid gives the same bytes */
    char cid1[200], cid2[200], fid[40];
    r = call("stream.segment", "\"data_hex\":\"%0500d\",\"k\":10,\"stream_id\":\"7\",\"seq\":3", 0);
    CHECK(ok(r) && num(r, "nfreights") == 2 && num(r, "data_len") == 250 && num(r, "seq") == 3 &&
              field(r, "cid", cid1, sizeof cid1) && field(r, "freight_id", fid, sizeof fid) &&
              strstr(r, "\"stream_id\":\"7\""),
          "segment: 250 bytes at k = 10 is two freights: %.300s", r);
    r = call("web4.cid", "\"data_hex\":\"%0500d\"", 0);
    CHECK(field(r, "cid", cid2, sizeof cid2) && !strcmp(cid1, cid2), "the same CID: %s / %s", cid1,
          cid2);
    r = call("stream.segment", "\"data_hex\":\"\"");
    CHECK(ok(r) && num(r, "nfreights") == 1 && num(r, "k") == 168, "an empty segment: %.200s", r);
    REFUSED(call("stream.segment", "\"data_hex\":\"00\",\"k\":0"), -32602, "k 0");
    REFUSED(call("stream.segment", "\"data_hex\":\"00\",\"k\":169"), -32602, "k 169");
    static char big[2 * 1281 + 1];
    memset(big, '0', sizeof big - 1);
    REFUSED(call("stream.segment", "\"data_hex\":\"%s\",\"k\":1", big), -32602,
            "more than 64 freights");
    REFUSED(call("stream.segment", "\"k\":1"), -32602, "no data");
}

/* ---------------- ehop ---------------- */

static char FRAME[200];

#define CHAN "\"key_hex\":\"%s\",\"band\":\"chat\",\"chan_id\":7,\"epoch\":1"

static void test_ehop(void)
{
    char key[65], s[16];
    memset(key, '1', 64);
    key[64] = 0;
    const char *r = call("ehop.bands", NULL);
    CHECK(ok(r) && strstr(r, "\"name\":\"control\"") && strstr(r, "\"name\":\"settle\"") &&
              strstr(r, "\"name\":\"media\"") && strstr(r, "\"name\":\"chat\"") &&
              strstr(r, "{\"band\":4,\"name\":\"bulk\",\"fm_min\":512,\"fm_max\":4096"),
          "the band plan: %s", r);

    r = call("ehop.seal", CHAN ",\"sender\":5,\"seq\":9,\"plaintext_hex\":\"68656c6c6f\"", key);
    CHECK(ok(r) && field(r, "frame_hex", FRAME, sizeof FRAME) && num(r, "frame_len") == 5 + 48 &&
              num(r, "priority") == 3 && field(r, "seq", s, sizeof s) && !strcmp(s, "9"),
          "seal: %s", r);
    CHECK(!strncmp(FRAME, "5a4801030100000005000000", 24),
          "the clear header: ZH, v1, chat, epoch, sender");
    char again[200];
    r = call("ehop.seal", CHAN ",\"sender\":5,\"seq\":9,\"plaintext_hex\":\"68656c6c6f\"", key);
    CHECK(field(r, "frame_hex", again, sizeof again) && !strcmp(again, FRAME),
          "stateless: the same inputs seal the same frame");
    r = call("ehop.seal", CHAN ",\"sender\":5,\"seq\":10,\"plaintext_hex\":\"68656c6c6f\"", key);
    CHECK(field(r, "frame_hex", again, sizeof again) && strcmp(again + 48, FRAME + 48),
          "the next seq seals a different frame");
    r = call("ehop.open", CHAN ",\"frame_hex\":\"%s\"", key, FRAME);
    CHECK(ok(r) && field(r, "plaintext_hex", s, sizeof s) && !strcmp(s, "68656c6c6f") &&
              num(r, "sender") == 5 && strstr(r, "\"seq\":\"9\""),
          "open: %s", r);

    /* refusals by the module */
    char k2[65];
    memcpy(k2, key, 65);
    k2[0] = '2';
    REFUSED(call("ehop.open", CHAN ",\"frame_hex\":\"%s\"", k2, FRAME), ZXVD_E_MODULE,
            "another key");
    REFUSED(call("ehop.open",
                 "\"key_hex\":\"%s\",\"band\":\"chat\",\"chan_id\":7,\"epoch\":2,"
                 "\"frame_hex\":\"%s\"",
                 key, FRAME),
            ZXVD_E_MODULE, "another epoch");
    CHECK(strstr(OUT.p, "another epoch") != NULL, "says so: %s", OUT.p);
    REFUSED(call("ehop.open", CHAN ",\"fm_rate\":129,\"frame_hex\":\"%s\"", key, FRAME),
            ZXVD_E_MODULE, "another configuration");
    char t[200];
    snprintf(t, sizeof t, "%s", FRAME);
    t[strlen(t) - 1] = t[strlen(t) - 1] == '0' ? '1' : '0';
    REFUSED(call("ehop.open", CHAN ",\"frame_hex\":\"%s\"", key, t), ZXVD_E_MODULE,
            "a flipped bit");
    CHECK(strstr(OUT.p, "AEAD") != NULL, "the AEAD caught it: %s", OUT.p);
    REFUSED(call("ehop.open", CHAN ",\"frame_hex\":\"%.40s\"", key, FRAME), ZXVD_E_MODULE,
            "a truncated frame");
    REFUSED(call("ehop.seal",
                 CHAN ",\"sender\":5,\"seq\":\"18446744073709551615\","
                      "\"plaintext_hex\":\"00\"",
                 key),
            ZXVD_E_MODULE, "the last sequence number is never used");
    static char big[2 * 600 + 1];
    memset(big, '0', sizeof big - 1);
    REFUSED(call("ehop.seal",
                 "\"key_hex\":\"%s\",\"band\":\"control\",\"chan_id\":1,\"epoch\":1,"
                 "\"sender\":1,\"seq\":0,\"plaintext_hex\":\"%s\"",
                 key, big),
            ZXVD_E_MODULE, "600 bytes do not fit the control band");
    /* parameters */
    REFUSED(call("ehop.seal",
                 "\"key_hex\":\"%s\",\"band\":\"radio\",\"chan_id\":1,\"epoch\":1,"
                 "\"sender\":1,\"seq\":0,\"plaintext_hex\":\"00\"",
                 key),
            -32602, "an unknown band");
    REFUSED(call("ehop.seal", CHAN ",\"fm_rate\":5,\"sender\":1,\"seq\":0,\"plaintext_hex\":\"00\"",
                 key),
            -32602, "a flip rate outside the band");
    REFUSED(
        call("ehop.seal", CHAN ",\"width\":3,\"sender\":1,\"seq\":0,\"plaintext_hex\":\"00\"", key),
        -32602, "width 3");
    REFUSED(call("ehop.seal", CHAN ",\"pm_mode\":3,\"sender\":1,\"seq\":0,\"plaintext_hex\":\"00\"",
                 key),
            -32602, "pm_mode 3");
    REFUSED(call("ehop.seal",
                 "\"key_hex\":\"%s\",\"band\":\"media\",\"chan_id\":1,\"epoch\":1,"
                 "\"am_mask\":8,\"sender\":1,\"seq\":0,\"plaintext_hex\":\"00\"",
                 key),
            -32602, "a depth the band does not allow");
    REFUSED(call("ehop.seal", CHAN ",\"sender\":1,\"seq\":0,\"plaintext_hex\":\"00\"", "00"),
            -32602, "a one-byte key");
    REFUSED(call("ehop.seal", CHAN ",\"seq\":0,\"plaintext_hex\":\"00\"", key), -32602,
            "no sender");
    REFUSED(call("ehop.seal", CHAN ",\"sender\":1,\"plaintext_hex\":\"00\"", key), -32602,
            "no seq");
    REFUSED(
        call("ehop.seal", CHAN ",\"sender\":4294967296,\"seq\":0,\"plaintext_hex\":\"00\"", key),
        -32602, "a sender past 32 bits");
    REFUSED(call("ehop.seal",
                 "\"key_hex\":\"%s\",\"band\":\"chat\",\"epoch\":1,\"sender\":1,"
                 "\"seq\":0,\"plaintext_hex\":\"00\"",
                 key),
            -32602, "no channel id");
    REFUSED(call("ehop.open", CHAN, key), -32602, "open without a frame");
    /* a custom configuration inside the band round-trips */
    r = call("ehop.seal",
             "\"key_hex\":\"%s\",\"band\":\"settle\",\"chan_id\":2,\"epoch\":0,"
             "\"width\":8,\"am_mask\":2,\"pm_mode\":2,\"fm_rate\":20,\"fm_hop\":3,"
             "\"pm_phase\":4,\"pm_step\":5,\"sender\":1,\"seq\":0,"
             "\"plaintext_hex\":\"000102030405060708090a0b0c0d0e0f1011\"",
             key);
    CHECK(field(r, "frame_hex", FRAME, sizeof FRAME), "seal on a custom settle channel: %s", r);
    r = call("ehop.open",
             "\"key_hex\":\"%s\",\"band\":\"settle\",\"chan_id\":2,\"epoch\":0,"
             "\"width\":8,\"am_mask\":2,\"pm_mode\":2,\"fm_rate\":20,\"fm_hop\":3,"
             "\"pm_phase\":4,\"pm_step\":5,\"frame_hex\":\"%s\"",
             key, FRAME);
    CHECK(strstr(r, "\"plaintext_hex\":\"000102030405060708090a0b0c0d0e0f1011\""),
          "and opens on it: %s", r);
}

/* ---------------- web4 ---------------- */

static char CARD[24000], PK[4000], SIG[7000];

static void test_web4(void)
{
    char s[200];
    /* "hello": the CIDv1 every IPFS implementation gives these bytes */
    const char *r = call("web4.cid", "\"data_hex\":\"68656c6c6f\"");
    CHECK(
        ok(r) && field(r, "cid", s, sizeof s) &&
            !strcmp(s, "bafkreibm6jg3ux5qumhcn2b3flc3tyu6dmlb4xa7u5bf44yegnrjhc4yeq") &&
            strstr(
                r,
                "\"uri\":\"ipfs://bafkreibm6jg3ux5qumhcn2b3flc3tyu6dmlb4xa7u5bf44yegnrjhc4yeq\"") &&
            strstr(r, "\"version\":1,\"codec\":85") &&
            strstr(r, "\"bytes32_hex\":"
                      "\"2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824\""),
        "the CID of hello: %s", r);
    r = call("web4.cid",
             "\"cid\":\"ipfs://bafkreibm6jg3ux5qumhcn2b3flc3tyu6dmlb4xa7u5bf44yegnrjhc4yeq\"");
    CHECK(ok(r) && strstr(r, "\"bytes32_hex\":\"2cf24dba"), "parse an ipfs:// URI: %s", r);
    r = call("web4.cid", "\"cid\":\"QmWATWQ7fVPP2EFGu71UkfnqhYXDYH566qy47CnJDgvs8u\"");
    CHECK(ok(r) && strstr(r, "\"version\":0,\"codec\":112") && strstr(r, "\"bytes32_hex\":\""),
          "a CIDv0 (dag-pb, sha2-256): %s", r);
    r = call("web4.cid", "\"cid\":\"bafkqaaa\"");
    CHECK(ok(r) && strstr(r, "\"bytes32_hex\":null"), "an identity CID has no bytes32 form: %s", r);
    REFUSED(call("web4.cid", "\"cid\":\"bafy-not-a-cid\""), -32602, "not a CID");
    REFUSED(call("web4.cid", NULL), -32602, "neither form");
    REFUSED(call("web4.cid", "\"cid\":\"bafkqaaa\",\"data_hex\":\"00\""), -32602, "both forms");
    REFUSED(call("web4.cid", "\"data_hex\":\"0g\""), -32602, "bad hex");

    /* A1-A3: a deterministic self-bound card that verifies */
    char seed[65];
    memset(seed, '7', 64);
    seed[64] = 0;
    r = call("web4.agent_card",
             "\"seed_hex\":\"%s\",\"name\":\"helper-1\",\"bound_ms\":\"1700000000000\"", seed);
    snprintf(CARD, sizeof CARD, "%s", r);
    char id[70], peer[70];
    CHECK(ok(CARD) && field(CARD, "agent_id", id, sizeof id) && strlen(id) == 64 &&
              field(CARD, "peer_id", peer, sizeof peer) && !strcmp(id, peer) &&
              field(CARD, "pk_hex", PK, sizeof PK) && strlen(PK) == 2 * 1952 &&
              field(CARD, "bind_sig_hex", SIG, sizeof SIG) && strlen(SIG) == 2 * 3309 &&
              strstr(CARD,
                     "\"name\":\"helper-1\",\"bound_ms\":\"1700000000000\",\"self_bound\":true"),
          "agent_card: %.200s", CARD);
    CHECK(!strstr(CARD, seed), "the seed is not echoed");
    r = call("web4.agent_card",
             "\"seed_hex\":\"%s\",\"name\":\"helper-1\",\"bound_ms\":\"1700000000000\"", seed);
    CHECK(!strcmp(r, CARD), "the same seed gives the same card");
    r = call("web4.card_verify",
             "\"name\":\"helper-1\",\"bound_ms\":1700000000000,\"pk_hex\":\"%s\",\"bind_sig_hex\":"
             "\"%s\","
             "\"agent_id\":\"%s\"",
             PK, SIG, id);
    CHECK(ok(r) && strstr(r, "\"valid\":true") && field(r, "agent_id", s, sizeof s) &&
              !strcmp(s, id) && !strstr(r, "reason"),
          "card_verify accepts it: %s", r);
    r = call("web4.card_verify",
             "\"name\":\"helper-2\",\"bound_ms\":1700000000000,\"pk_hex\":\"%s\","
             "\"bind_sig_hex\":\"%s\"",
             PK, SIG);
    CHECK(strstr(r, "\"valid\":false") && strstr(r, "binding signature"), "another name: %s", r);
    r = call("web4.card_verify",
             "\"name\":\"helper-1\",\"bound_ms\":1,\"pk_hex\":\"%s\","
             "\"bind_sig_hex\":\"%s\"",
             PK, SIG);
    CHECK(strstr(r, "\"valid\":false"), "another time: %s", r);
    char other[70];
    snprintf(other, sizeof other, "%s", id);
    other[0] = other[0] == 'a' ? 'b' : 'a';
    r = call("web4.card_verify",
             "\"name\":\"helper-1\",\"bound_ms\":1700000000000,\"pk_hex\":\"%s\","
             "\"bind_sig_hex\":\"%s\",\"agent_id\":\"%s\"",
             PK, SIG, other);
    CHECK(strstr(r, "\"valid\":false") && strstr(r, "not SHA3-256 of pk"), "a false agent_id: %s",
          r);
    SIG[100] = SIG[100] == '0' ? '1' : '0';
    r = call("web4.card_verify",
             "\"name\":\"helper-1\",\"bound_ms\":1700000000000,\"pk_hex\":\"%s\","
             "\"bind_sig_hex\":\"%s\"",
             PK, SIG);
    CHECK(strstr(r, "\"valid\":false"), "a flipped signature bit: %s", r);

    REFUSED(call("web4.agent_card", "\"seed_hex\":\"%s\",\"name\":\"Helper\"", seed), -32602,
            "an upper-case name");
    REFUSED(call("web4.agent_card", "\"seed_hex\":\"%s\",\"name\":\"\"", seed), -32602, "no name");
    REFUSED(call("web4.agent_card", "\"seed_hex\":\"%s\",\"name\":\"%s\"", seed,
                 "abcdefghijabcdefghijabcdefghijab"),
            -32602, "a 32-byte name");
    REFUSED(call("web4.agent_card", "\"seed_hex\":\"00\",\"name\":\"a\""), -32602, "a short seed");
    REFUSED(call("web4.agent_card", "\"name\":\"a\""), -32602, "no seed");
    REFUSED(call("web4.card_verify",
                 "\"name\":\"UP\",\"bound_ms\":0,\"pk_hex\":\"%s\","
                 "\"bind_sig_hex\":\"%s\"",
                 PK, SIG),
            -32602, "card_verify: a bad name");
    REFUSED(call("web4.card_verify",
                 "\"name\":\"a\",\"bound_ms\":0,\"pk_hex\":\"00\","
                 "\"bind_sig_hex\":\"%s\"",
                 SIG),
            -32602, "card_verify: a short key");
    REFUSED(call("web4.card_verify", "\"name\":\"a\",\"pk_hex\":\"%s\",\"bind_sig_hex\":\"%s\"", PK,
                 SIG),
            -32602, "card_verify: no time");
    REFUSED(call("web4.card_verify",
                 "\"name\":\"a\",\"bound_ms\":0,\"pk_hex\":\"%s\","
                 "\"bind_sig_hex\":\"%s\",\"agent_id\":\"00\"",
                 PK, SIG),
            -32602, "card_verify: a short agent_id");
}

/* ---------------- the capability rule ---------------- */

static char BEFORE[4096], ENTRIES[4096];

static void test_books_untouched(void)
{
    /* with the books open: nothing a module method does reaches them */
    CHECK(zxvd_rpc_books_open() == 0, "the books open");
    snprintf(BEFORE, sizeof BEFORE, "%s", call("ledger.accounts", NULL));
    CHECK(ok(BEFORE), "ledger.accounts: %s", BEFORE);
    snprintf(ENTRIES, sizeof ENTRIES, "%s", call("swarm.earnings", NULL));
    test_freight();
    test_harmonic();
    test_stream();
    test_ehop();
    test_web4();
    CHECK(!strcmp(call("ledger.accounts", NULL), BEFORE), "no account opened: %s", OUT.p);
    CHECK(!strcmp(call("swarm.earnings", NULL), ENTRIES), "no posting: %s", OUT.p);
    CHECK(strstr(call("ledger.check", NULL), "\"ok\":true") != NULL, "the books still check");
    zxvd_rpc_books_close();
}

int main(void)
{
    test_dispatch();
    /* first without books: the module methods need none */
    test_freight();
    test_harmonic();
    test_stream();
    test_ehop();
    test_web4();
    test_books_untouched();
    free(OUT.p);
    if (fails) {
        printf("[FAIL] test_zxvd_rpc_modules: %d of %d checks failed\n", fails, checks);
        return 1;
    }
    printf("[PASS] test_zxvd_rpc_modules: %d checks\n", checks);
    return 0;
}
