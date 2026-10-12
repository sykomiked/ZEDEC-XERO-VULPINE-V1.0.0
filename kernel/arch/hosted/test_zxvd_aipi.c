/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zxvd_aipi.c — zxvd_aipi.c (zxvd_aipi.h Z1-Z4) behind the real RPC
 * layer, on the real AIPI core and the Z-Wasm sandbox: every reply exact,
 * every refusal named. Run from an empty folder (test_zxvd_aipi.sh): it
 * writes its fixture files there. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zxvd_aipi.h"
#include "aipi_mods.h" /* the kit's assembled Wasm modules */
#include "web4_util.h"

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
static char REQB[40000], WANT[200000];

static const char *rpc(const char *line)
{
    CHECK(zxvd_rpc_handle_line(line, strlen(line), &OUT), "a reply for %s", line);
    return OUT.p ? OUT.p : "";
}

static void expect(const char *req, const char *want, int line)
{
    const char *got = rpc(req);
    checks++;
    if (strcmp(got, want)) {
        fails++;
        printf("  FAIL line %d\n    req  %.300s\n    want %.300s\n    got  %.300s\n", line, req,
               want, got);
    }
}
#define EXPECT(req, want) expect(req, want, __LINE__)

/* a request with printf-style params */
static const char *req(const char *method, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
static const char *req(const char *method, const char *fmt, ...)
{
    va_list ap;
    int k = snprintf(REQB, sizeof REQB,
                     "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"%s\",\"params\":", method);
    va_start(ap, fmt);
    k += vsnprintf(REQB + k, sizeof REQB - (size_t) k, fmt, ap);
    va_end(ap);
    snprintf(REQB + k, sizeof REQB - (size_t) k, "}");
    return REQB;
}

static const char *ok(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static const char *ok(const char *fmt, ...)
{
    va_list ap;
    int k = snprintf(WANT, sizeof WANT, "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":");
    va_start(ap, fmt);
    k += vsnprintf(WANT + k, sizeof WANT - (size_t) k, fmt, ap);
    va_end(ap);
    snprintf(WANT + k, sizeof WANT - (size_t) k, "}");
    return WANT;
}

static const char *err(const char *msg)
{
    snprintf(WANT, sizeof WANT,
             "{\"jsonrpc\":\"2.0\",\"id\":1,\"error\":{\"code\":-32011,\"message\":\"%s\"}}", msg);
    return WANT;
}

static void cid_text(const uint8_t *data, uint32_t len, uint32_t codec, char *out)
{
    ipfsn_cid_t c;
    CHECK(ipfsn_cid_sha256(codec, data, len, &c) == IPFSN_OK, "cid");
    CHECK(aipi_cid_to_text(&c, out, AIPI_TEXT_MAX) > 0, "cid text");
}

static char *hex_of(const uint8_t *p, uint32_t n)
{
    char *h = malloc(2u * n + 1u);
    w4_hex_encode(p, n, h, 2u * n + 1u);
    return h;
}

static void write_file(const char *path, const uint8_t *p, size_t n)
{
    FILE *f = fopen(path, "wb");
    CHECK(f && fwrite(p, 1, n, f) == n, "write %s", path);
    if (f) fclose(f);
}

#define TAG_TRUE                                                                                   \
    "\"tag\":{\"base\":\"true\",\"polarity\":\"negentropic\",\"phase\":\"manifest\",\"action\":"   \
    "\"proceed\"}"

static const char *status_json(unsigned blocks, unsigned long long bytes, unsigned puts,
                               unsigned gets, unsigned pubs, unsigned calls, unsigned failed)
{
    static char s[600];
    snprintf(s, sizeof s,
             "{\"online\":false,\"blocks\":%u,\"store_bytes\":%llu,\"store_cap\":33554432,"
             "\"puts\":%u,\"gets\":%u,\"publishes\":%u,\"calls\":%u,\"failed\":%u,\"forbidden\":0,",
             blocks, bytes, puts, gets, pubs, calls, failed);
    return s;
}

static void test_lifetime(void)
{
    const zxvd_aipi_ops_t *a = zxvd_aipi_open();
    CHECK(a && a->put && a->get && a->publish && a->execute && a->status, "open fills the ops");
    CHECK(zxvd_aipi_open() == a, "a second open returns the same ops");
    zxvd_rpc_set_aipi_ops(a);
    EXPECT(req("aipi.status", "{}"),
           ok("%s\"sandbox\":{\"runs\":0,\"checks\":0,\"trapped\":0,\"refused_module\":0,"
              "\"refused_contract\":0,\"fuel_used\":\"0\"}}",
              status_json(0, 0, 0, 0, 0, 0, 0)));
    zxvd_aipi_close();
    zxvd_aipi_close(); /* twice is harmless */
    zxvd_rpc_set_aipi_ops(NULL);
}

static void test_blocks(void)
{
    char cid[AIPI_TEXT_MAX], cid2[AIPI_TEXT_MAX];
    zxvd_rpc_set_aipi_ops(zxvd_aipi_open());

    /* Z1/Z2: put by hex, get it back verified */
    static const uint8_t B[] = {0x00, 0xFF, 0x10, 0x7A};
    cid_text(B, sizeof B, IPFSN_MC_RAW, cid);
    EXPECT(req("aipi.put", "{\"hex\":\"00ff107a\"}"),
           ok("{\"cid\":\"%s\",\"bytes\":4,\"private\":false}", cid));
    EXPECT(req("aipi.get", "{\"cid\":\"%s\"}", cid),
           ok("{\"cid\":\"%s\",\"bytes\":4,\"hex\":\"00ff107a\"," TAG_TRUE "}", cid));
    /* a private block: stored sealed, still readable here */
    static const uint8_t P[] = {'k', 'e', 'y'};
    cid_text(P, sizeof P, IPFSN_MC_RAW, cid2);
    EXPECT(req("aipi.put", "{\"hex\":\"6b6579\",\"private\":true}"),
           ok("{\"cid\":\"%s\",\"bytes\":3,\"private\":true}", cid2));
    EXPECT(req("aipi.get", "{\"cid\":\"%s\"}", cid2),
           ok("{\"cid\":\"%s\",\"bytes\":3,\"hex\":\"6b6579\"," TAG_TRUE "}", cid2));
    /* an empty block */
    cid_text(NULL, 0, IPFSN_MC_RAW, cid2);
    EXPECT(req("aipi.put", "{\"hex\":\"\"}"),
           ok("{\"cid\":\"%s\",\"bytes\":0,\"private\":false}", cid2));
    EXPECT(req("aipi.get", "{\"cid\":\"%s\"}", cid2),
           ok("{\"cid\":\"%s\",\"bytes\":0,\"hex\":\"\"," TAG_TRUE "}", cid2));

    /* a file: exactly 1 MiB is taken, one byte more is refused */
    uint8_t *big = malloc(AIPI_MODULE_MAX + 1u);
    for (uint32_t i = 0; i <= AIPI_MODULE_MAX; i++) big[i] = (uint8_t) (i * 7u);
    write_file("mib.bin", big, AIPI_MODULE_MAX);
    cid_text(big, AIPI_MODULE_MAX, IPFSN_MC_RAW, cid);
    EXPECT(req("aipi.put", "{\"path\":\"mib.bin\"}"),
           ok("{\"cid\":\"%s\",\"bytes\":1048576,\"private\":false}", cid));
    write_file("over.bin", big, AIPI_MODULE_MAX + 1u);
    EXPECT(req("aipi.put", "{\"path\":\"over.bin\"}"),
           err("the file is larger than 1 MiB (AIPI_MODULE_MAX)"));
    EXPECT(req("aipi.put", "{\"path\":\"missing.bin\"}"), err("the file could not be opened"));
    EXPECT(req("aipi.put", "{\"path\":\".\"}"), err("the file could not be read"));
    /* Z4: a 1 MiB block is refused by aipi.get, not cut */
    EXPECT(req("aipi.get", "{\"cid\":\"%s\"}", cid), err("the block is larger than 65536 bytes"));
    /* ... and exactly ZXVD_AIPI_GET_MAX comes back whole */
    write_file("get.bin", big, ZXVD_AIPI_GET_MAX);
    cid_text(big, ZXVD_AIPI_GET_MAX, IPFSN_MC_RAW, cid);
    EXPECT(req("aipi.put", "{\"path\":\"get.bin\",\"private\":true}"),
           ok("{\"cid\":\"%s\",\"bytes\":65536,\"private\":true}", cid));
    char *h = hex_of(big, ZXVD_AIPI_GET_MAX);
    EXPECT(req("aipi.get", "{\"cid\":\"%s\"}", cid),
           ok("{\"cid\":\"%s\",\"bytes\":65536,\"hex\":\"%s\"," TAG_TRUE "}", cid, h));
    free(h);
    write_file("get1.bin", big, ZXVD_AIPI_GET_MAX + 1u);
    cid_text(big, ZXVD_AIPI_GET_MAX + 1u, IPFSN_MC_RAW, cid);
    rpc(req("aipi.put", "{\"path\":\"get1.bin\"}"));
    EXPECT(req("aipi.get", "{\"cid\":\"%s\"}", cid), err("the block is larger than 65536 bytes"));
    free(big);

    /* unknown and malformed CIDs */
    static const uint8_t N[] = {1, 2, 3};
    cid_text(N, sizeof N, IPFSN_MC_RAW, cid);
    EXPECT(req("aipi.get", "{\"cid\":\"%s\"}", cid),
           err("AIPI_ERR_OFFLINE: offline and the code is not in the local blockstore"));
    EXPECT(req("aipi.get", "{\"cid\":\"not-a-cid\"}"), err("not a CID"));
    EXPECT(req("aipi.get", "{\"cid\":\"\"}"), err("not a CID"));

    {
        const char *s = rpc(req("aipi.status", "{}"));
        CHECK(
            strstr(s, "\"blocks\":8,") && strstr(s, "\"puts\":6,") && strstr(s, "\"gets\":4,") &&
                strstr(s, "\"publishes\":0,") && strstr(s, "\"calls\":0,"),
            "status counts the blocks (a private one is indexed twice), puts and successful gets: "
            "%.300s",
            s);
        unsigned long long used = 0;
        const char *u = strstr(s, "\"store_bytes\":");
        CHECK(u && sscanf(u, "\"store_bytes\":%llu", &used) == 1 &&
                  used > AIPI_MODULE_MAX + 2u * ZXVD_AIPI_GET_MAX && used < (3u << 20),
              "store_bytes is the memstore's length (%llu)", used);
    }
    zxvd_aipi_close();
    zxvd_rpc_set_aipi_ops(zxvd_aipi_open());
    CHECK(strstr(rpc(req("aipi.status", "{}")), "\"blocks\":0,\"store_bytes\":0,"),
          "Z2: a new open starts with an empty blockstore");
    zxvd_aipi_close();
    zxvd_rpc_set_aipi_ops(NULL);
}

static void test_functions(void)
{
    wb m;
    char mod[AIPI_TEXT_MAX], con[AIPI_TEXT_MAX], fl[AIPI_TEXT_MAX], fcid[AIPI_TEXT_MAX];
    char call[AIPI_TEXT_MAX], *h;
    memset(&m, 0, sizeof m);
    zxvd_rpc_set_aipi_ops(zxvd_aipi_open());

    mod_halves(&m);
    cid_text(m.b, (uint32_t) m.n, IPFSN_MC_RAW, mod);
    h = hex_of(m.b, (uint32_t) m.n);
    EXPECT(req("aipi.put", "{\"hex\":\"%s\"}", h),
           ok("{\"cid\":\"%s\",\"bytes\":%u,\"private\":false}", mod, (unsigned) m.n));
    free(h);
    write_file("halves.zc", (const uint8_t *) HALVES_ZC, sizeof HALVES_ZC - 1);
    cid_text((const uint8_t *) HALVES_ZC, sizeof HALVES_ZC - 1, IPFSN_MC_RAW, con);
    rpc(req("aipi.put", "{\"path\":\"halves.zc\"}"));

    /* the descriptor zxvd publishes is the one the kit would */
    aipi_fn_t fn;
    memset(&fn, 0, sizeof fn);
    fn.intent = AIPI_INTENT_FN;
    fn.verify = AIPI_VERIFY_CHECKER;
    fn.determinism = AIPI_DET_DETERMINISTIC;
    fn.fuel_max = 100000;
    aipi_cid_from_text(mod, (uint32_t) strlen(mod), &fn.module);
    fn.has_contract = true;
    aipi_cid_from_text(con, (uint32_t) strlen(con), &fn.contract);
    strcpy(fn.entry, "half");
    strcpy(fn.checker, "half_ok");
    {
        uint8_t enc[AIPI_FN_ENC_MAX];
        int n = aipi_fn_encode(&fn, enc, sizeof enc);
        cid_text(enc, (uint32_t) n, AIPI_MC_FN, fcid);
    }
    EXPECT(req("aipi.publish",
               "{\"module\":\"%s\",\"contract\":\"%s\",\"entry\":\"half\",\"checker\":\"half_ok\","
               "\"verify\":\"checker\",\"fuel_max\":100000}",
               mod, con),
           ok("{\"function\":\"%s\",\"local_only\":false}", fcid));

    /* the call record's CID, as aipi_call_cid makes it */
    {
        int64_t v = 2026;
        uint8_t in[8];
        uint32_t len;
        ipfsn_cid_t f, c;
        aipi_zw_pack(&v, 1, in, sizeof in, &len);
        aipi_cid_from_text(fcid, (uint32_t) strlen(fcid), &f);
        aipi_call_cid(&f, in, len, &c);
        aipi_cid_to_text(&c, call, sizeof call);
    }
    EXPECT(req("aipi.execute", "{\"function\":\"%s\",\"args\":[2026]}", fcid),
           ok("{\"function\":\"%s\",\"call\":\"%s\",\"route\":\"local\",\"result\":\"1013\","
              "\"output_hex\":\"f503000000000000\"," TAG_TRUE "}",
              fcid, call));
    EXPECT(req("aipi.execute", "{\"function\":\"%s\",\"args\":[2026]}", fcid),
           ok("{\"function\":\"%s\",\"call\":\"%s\",\"route\":\"cache\",\"result\":\"1013\","
              "\"output_hex\":\"f503000000000000\"," TAG_TRUE "}",
              fcid, call));
    /* private input is never memoised: run here again */
    CHECK(strstr(rpc(req("aipi.execute", "{\"function\":\"%s\",\"args\":[2026],\"private\":true}",
                         fcid)),
                 "\"route\":\"local\",\"result\":\"1013\""),
          "private input runs locally, not from the memo");
    CHECK(strstr(rpc(req("aipi.execute", "{\"function\":\"%s\",\"args\":[0]}", fcid)),
                 "\"result\":\"0\",\"output_hex\":\"0000000000000000\""),
          "half(0) = 0");
    /* the contract bounds the argument */
    EXPECT(req("aipi.execute", "{\"function\":\"%s\",\"args\":[1000001]}", fcid),
           err("AIPI_ERR_EXEC: the sandbox refused or stopped the run (validator, contract, trap "
               "or fuel)"));
    /* a missing argument is a sandbox refusal too, never a garbage result */
    EXPECT(req("aipi.execute", "{\"function\":\"%s\",\"args\":[]}", fcid),
           err("AIPI_ERR_EXEC: the sandbox refused or stopped the run (validator, contract, trap "
               "or fuel)"));
    /* jobs are released after failures: more failures than job slots, then a success */
    for (int i = 0; i < 6; i++)
        rpc(req("aipi.execute", "{\"function\":\"%s\",\"args\":[-1]}", fcid));
    CHECK(strstr(rpc(req("aipi.execute", "{\"function\":\"%s\",\"args\":[10]}", fcid)),
                 "\"result\":\"5\""),
          "failed jobs free their slots");
    for (int i = 0; i < 6; i++)
        rpc(req("aipi.execute", "{\"function\":\"%s\",\"args\":[%d]}", fcid, 100 + i));
    CHECK(strstr(rpc(req("aipi.execute", "{\"function\":\"%s\",\"args\":[12]}", fcid)),
                 "\"result\":\"6\""),
          "finished jobs free their slots");

    /* fuel stops a loop */
    char spin[AIPI_TEXT_MAX];
    const char *r = rpc(req("aipi.publish",
                            "{\"module\":\"%s\",\"entry\":\"spin\",\"verify\":\"local\","
                            "\"fuel_max\":5000,\"local_only\":true,\"deterministic\":false}",
                            mod));
    CHECK(sscanf(r, "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"function\":\"%[^\"]", spin) == 1 &&
              strstr(r, "\"local_only\":true}"),
          "a local-only function: %s", r);
    EXPECT(req("aipi.execute", "{\"function\":\"%s\",\"args\":[1]}", spin),
           err("AIPI_ERR_EXEC: the sandbox refused or stopped the run (validator, contract, trap "
               "or fuel)"));

    /* floats are refused by the validator */
    mod_floaty(&m);
    cid_text(m.b, (uint32_t) m.n, IPFSN_MC_RAW, fl);
    h = hex_of(m.b, (uint32_t) m.n);
    rpc(req("aipi.put", "{\"hex\":\"%s\"}", h));
    free(h);
    char ff[AIPI_TEXT_MAX];
    r = rpc(req("aipi.publish",
                "{\"module\":\"%s\",\"entry\":\"half\",\"verify\":\"local\",\"fuel_max\":100}",
                fl));
    CHECK(sscanf(r, "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"function\":\"%[^\"]", ff) == 1,
          "publish floaty: %s", r);
    EXPECT(req("aipi.execute", "{\"function\":\"%s\",\"args\":[4]}", ff),
           err("AIPI_ERR_EXEC: the sandbox refused or stopped the run (validator, contract, trap "
               "or fuel)"));

    /* publish refusals */
    EXPECT(req("aipi.publish",
               "{\"module\":\"x\",\"entry\":\"e\",\"verify\":\"local\",\"fuel_max\":1}"),
           err("\\\"module\\\" is not a CID"));
    EXPECT(req("aipi.publish",
               "{\"module\":\"%s\",\"contract\":\"x\",\"entry\":\"e\",\"verify\":\"local\","
               "\"fuel_max\":1}",
               mod),
           err("\\\"contract\\\" is not a CID"));
    static const char *const BADFN =
        "the descriptor breaks the rules in docs/AIPI.md section 3 (AIPI_ERR_MALFORMED)";
    EXPECT(req("aipi.publish",
               "{\"module\":\"%s\",\"entry\":\"e\",\"verify\":\"local\",\"fuel_max\":0}", mod),
           err(BADFN));
    EXPECT(req("aipi.publish",
               "{\"module\":\"%s\",\"entry\":\"e\",\"verify\":\"checker\",\"fuel_max\":1}", mod),
           err(BADFN));
    EXPECT(req("aipi.publish",
               "{\"module\":\"%s\",\"entry\":\"e\",\"checker\":\"c\",\"verify\":\"local\","
               "\"fuel_max\":1}",
               mod),
           err(BADFN));
    EXPECT(req("aipi.publish",
               "{\"module\":\"%s\",\"entry\":\"e\",\"checker\":\"c\",\"verify\":\"cert\","
               "\"fuel_max\":1}",
               mod),
           err(BADFN));
    EXPECT(req("aipi.publish",
               "{\"module\":\"%s\",\"entry\":\"e\",\"verify\":\"quorum\",\"fuel_max\":1}", mod),
           err(BADFN));
    /* a module CID is not a function */
    EXPECT(req("aipi.execute", "{\"function\":\"%s\",\"args\":[1]}", mod),
           err("AIPI_ERR_UNSUPP: not an AIPI function"));
    EXPECT(req("aipi.execute", "{\"function\":\"zzz\",\"args\":[1]}"),
           err("\\\"function\\\" is not a CID"));
    /* a function whose module is not here: A3 */
    {
        char gone[AIPI_TEXT_MAX];
        static const uint8_t G[] = {9, 9};
        cid_text(G, sizeof G, IPFSN_MC_RAW, gone);
        r = rpc(req("aipi.publish",
                    "{\"module\":\"%s\",\"entry\":\"e\",\"verify\":\"local\",\"fuel_max\":9}",
                    gone));
        CHECK(sscanf(r, "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"function\":\"%[^\"]", ff) == 1,
              "publish without the module: %s", r);
        EXPECT(req("aipi.execute", "{\"function\":\"%s\",\"args\":[1]}", ff),
               err("AIPI_ERR_OFFLINE: offline and the code is not in the local blockstore"));
    }
    /* names: 63 bytes fit, 64 do not (the RPC layer takes 63; zxvd_aipi
     * checks again for direct callers) */
    {
        zxvd_aipi_fn_t f;
        zxvd_buf_t o = {0};
        memset(&f, 0, sizeof f);
        snprintf(f.module, sizeof f.module, "%s", mod);
        memset(f.entry, 'e', 63);
        f.verify = AIPI_VERIFY_LOCAL;
        f.fuel_max = 1;
        const zxvd_aipi_ops_t *a = zxvd_aipi_open();
        CHECK(a->publish(&f, &o) == NULL, "a 63-byte entry name is taken");
        o.len = 0;
        memset(f.entry, 'e', 64);
        const char *why = a->publish(&f, &o);
        CHECK(why && !strcmp(why, "export names are at most 63 bytes") && o.len == 0,
              "a 64-byte entry name is refused");
        memset(f.entry, 0, sizeof f.entry);
        f.entry[0] = 'e';
        memset(f.checker, 'c', 64);
        why = a->publish(&f, &o);
        CHECK(why && !strcmp(why, "export names are at most 63 bytes"), "checker too");
        memset(f.checker, 0, sizeof f.checker);
        memset(f.certifier, 'c', 64);
        why = a->publish(&f, &o);
        CHECK(why && !strcmp(why, "export names are at most 63 bytes"), "certifier too");
        free(o.p);
    }

    const char *s = rpc(req("aipi.status", "{}"));
    CHECK(strstr(s, "\"puts\":3,\"gets\":0,\"publishes\":5,\"calls\":24,\"failed\":12,"
                    "\"forbidden\":0,\"sandbox\":{\"runs\":"),
          "status counts publishes, calls and failures: %s", s);
    unsigned long long runs, chk, trapped, refm, refc, fuel;
    CHECK(sscanf(
              strstr(s, "\"sandbox\""),
              "\"sandbox\":{\"runs\":%llu,\"checks\":%llu,\"trapped\":%llu,\"refused_module\":%llu,"
              "\"refused_contract\":%llu,\"fuel_used\":\"%llu\"}}",
              &runs, &chk, &trapped, &refm, &refc, &fuel) == 6 &&
              trapped >= 1 && refm == 1 && refc >= 2 && fuel >= 5000 && runs >= 17,
          "the sandbox's own counters (%s)", s);
    zxvd_aipi_close();
    zxvd_rpc_set_aipi_ops(NULL);
}

int main(void)
{
    test_lifetime();
    test_blocks();
    test_functions();
    free(OUT.p);
    if (fails) {
        printf("[FAIL] test_zxvd_aipi: %d of %d checks failed\n", fails, checks);
        return 1;
    }
    printf("[PASS] test_zxvd_aipi: %d checks\n", checks);
    return 0;
}
