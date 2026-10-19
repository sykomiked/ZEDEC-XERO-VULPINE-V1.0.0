/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zxvd_ident.c — zxvd_ident.c (passkeys.*) behind the real RPC layer
 * on ident's real passkey code. The test plays the authenticator with
 * id_passkey_create / _register / _assert, so every login carries a real
 * ML-DSA-65 signature; every reply is compared exactly. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zxvd_groups.h"
#include "ident.h"
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
static char REQB[16000], WANT[20000];

static const char *rpc(const char *line)
{
    CHECK(zxvd_rpc_handle_line(line, strlen(line), &OUT), "a reply for %.200s", line);
    return OUT.p ? OUT.p : "";
}

static void expect(const char *req, const char *want, int line)
{
    const char *got = rpc(req);
    checks++;
    if (strcmp(got, want)) {
        fails++;
        printf("  FAIL line %d\n    req  %.300s\n    want %.400s\n    got  %.400s\n", line, req,
               want, got);
    }
}
#define EXPECT(req, want) expect(req, want, __LINE__)

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

static const char *errc(int code, const char *msg)
{
    snprintf(WANT, sizeof WANT,
             "{\"jsonrpc\":\"2.0\",\"id\":1,\"error\":{\"code\":%d,\"message\":\"%s\"}}", code,
             msg);
    return WANT;
}
#define ERR(m)  errc(-32041, m)
#define PERR(m) errc(-32602, m)

static const char *hx(const uint8_t *p, uint32_t n)
{
    static char b[4][8000];
    static int i;
    i = (i + 1) & 3;
    w4_hex_encode(p, n, b[i], sizeof b[i]);
    return b[i];
}

/* ----- fixtures ----- */

#define NOW 1791547200ll /* 2026-10-09 12:00:00Z */
static uint8_t RCOUNT;
static bool RFAIL;
static uint32_t AUTHN; /* the authenticator's own random stream */

static int fake_random(uint8_t *p, size_t n)
{
    if (RFAIL) return -1;
    for (size_t i = 0; i < n; i++) p[i] = (uint8_t) (RCOUNT + i * 3u);
    RCOUNT++;
    return 0;
}

static void authn_random(void *ctx, uint8_t *out, uint32_t n)
{
    (void) ctx;
    for (uint32_t i = 0; i < n; i++) out[i] = (uint8_t) (AUTHN * 131u + i * 7u + 5u);
    AUTHN++;
}

static const id_host_t H = {NULL, authn_random, NULL, NULL, NULL, NULL};

/* the challenge the fake source draws as its k-th draw */
static void chal_of(uint8_t k, uint8_t out[32])
{
    for (size_t i = 0; i < 32; i++) out[i] = (uint8_t) (k + i * 3u);
}

static const uint8_t USER1[16] = "user-number-one";
static const uint8_t USER2[16] = "user-number-two";
static const uint8_t DEV[16] = "phone-device-01";
static id_passkey_t PK1, PK2, PKX;
static uint8_t AUTH[ID_AUTHDATA_MAX], A2[ID_AUTHDATA_MIN], SIG[ID_MLDSA65_SIG];

static const char *cred_json(const id_passkey_t *pk, const char *rp, uint32_t count, bool revoked,
                             long long reg, long long used)
{
    static char s[400];
    char u[24];
    if (used >= 0)
        snprintf(u, sizeof u, "%lld", used);
    else
        snprintf(u, sizeof u, "null");
    snprintf(
        s, sizeof s,
        "{\"cred_id\":\"%s\",\"user\":\"%s\",\"rp_id\":\"%s\",\"sign_count\":%u,\"revoked\":%s,"
        "\"registered\":%lld,\"last_used\":%s}",
        hx(pk->cred_id, 16), hx(pk->user, 16), rp, (unsigned) count, revoked ? "true" : "false",
        reg, u);
    return s;
}

static void fresh(void)
{
    zxvd_rpc_groups_reset();
    CHECK(zxvd_rpc_add_group(zxvd_passkeys_group()) == 0, "add the passkeys group");
    zxvd_passkeys_set_random(fake_random);
    RCOUNT = 0;
    RFAIL = false;
}

/* register_begin for user at rp (default) and return the challenge draw */
static uint8_t begin_reg(const uint8_t user[16], const char *extra)
{
    uint8_t k = RCOUNT;
    rpc(req("passkeys.register_begin", "{\"user\":\"%s\",\"now\":%lld%s}", hx(user, 16), NOW,
            extra));
    return k;
}

static const char *finish_reg(uint8_t k, const id_passkey_t *pk, bool uv, long long now)
{
    uint8_t ch[32];
    uint32_t n = 0;
    chal_of(k, ch);
    CHECK(id_passkey_register(pk, uv, AUTH, sizeof AUTH, &n) == ID_OK, "authenticator register");
    return req("passkeys.register_finish", "{\"challenge\":\"%s\",\"auth\":\"%s\",\"now\":%lld}",
               hx(ch, 32), hx(AUTH, n), now);
}

/* the authenticator answers challenge draw k for origin */
static const char *finish_auth(uint8_t k, id_passkey_t *pk, const char *origin, bool uv,
                               long long now)
{
    uint8_t ch[32], cdh[32];
    chal_of(k, ch);
    CHECK(id_client_data_hash("webauthn.get", ch, 32, origin, cdh) == ID_OK, "cdh");
    CHECK(id_passkey_assert(pk, &H, uv, cdh, A2, SIG) == ID_OK, "assert");
    return req(
        "passkeys.auth_finish",
        "{\"challenge\":\"%s\",\"cred_id\":\"%s\",\"auth\":\"%s\",\"sig\":\"%s\",\"now\":%lld}",
        hx(ch, 32), hx(pk->cred_id, 16), hx(A2, sizeof A2), hx(SIG, sizeof SIG), now);
}

/* ----- tests ----- */

static void test_switch(void)
{
    fresh();
    const zxvd_group_t *g = zxvd_passkeys_group();
    CHECK(!g->regulated && !strcmp(g->feature, "passkeys") && g->n_methods == 7, "group shape");
    const char *d = rpc(req("rpc.discover", "{}"));
    CHECK(strstr(d, "\"web4.card_verify\",\"passkeys.status\",\"passkeys.register_begin\","
                    "\"passkeys.register_finish\",\"passkeys.auth_begin\","
                    "\"passkeys.auth_finish\",\"passkeys.list\",\"passkeys.revoke\"]") != NULL,
          "discover: %.300s", d);
    EXPECT(
        req("passkeys.register_begin", "{}"),
        errc(-32012, "passkeys is off: start zxvd with --enable passkeys, or call features.set"));
    EXPECT(
        req("passkeys.list", "{}"),
        errc(-32012, "passkeys is off: start zxvd with --enable passkeys, or call features.set"));
    EXPECT(req("passkeys.status", "{\"now\":%lld}", NOW),
           ok("{\"rp_id\":\"zxv.id\",\"alg\":-49,\"attestation\":\"none\",\"credentials\":0,"
              "\"active\":0,\"open_challenges\":0,\"max_credentials\":32,\"in_memory\":true}"));
    EXPECT(req("passkeys.status", "{\"now\":-1}"), PERR("\\\"now\\\" must be unix seconds"));
    EXPECT(req("features.get", "{}"),
           ok("{\"features\":[{\"name\":\"passkeys\",\"enabled\":false,\"regulated\":false,"
              "\"groups\":[{\"note\":\"ML-DSA-65 passkeys, WebAuthn-shaped (kernel/src/ident): "
              "zxvd is the relying party; public keys only, in memory\",\"methods\":7}]}],"
              "\"saved\":false}"));
    CHECK(zxvd_rpc_enable("passkeys") == 0, "enable passkeys");
}

static void test_register(void)
{
    uint8_t ch[32];
    /* begin: parameter checks */
    EXPECT(req("passkeys.register_begin", "{}"), PERR("missing hex parameter \\\"user\\\""));
    EXPECT(req("passkeys.register_begin", "{\"user\":\"00\"}"),
           PERR("\\\"user\\\" must be 16 bytes of hex"));
    EXPECT(req("passkeys.register_begin", "{\"user\":\"%s\",\"rp_id\":\"a\\\"b\"}", hx(USER1, 16)),
           PERR("\\\"rp_id\\\" must be printable text"));
    EXPECT(
        req("passkeys.register_begin", "{\"user\":\"%s\",\"rp_id\":\"a\\u0001b\"}", hx(USER1, 16)),
        PERR("\\\"rp_id\\\" must be printable text"));
    EXPECT(req("passkeys.register_begin", "{\"user\":\"%s\",\"origin\":\"x\\\\y\"}", hx(USER1, 16)),
           PERR("\\\"origin\\\" must be printable text"));
    EXPECT(
        req("passkeys.register_begin", "{\"user\":\"%s\",\"origin\":\"x\\u007fy\"}", hx(USER1, 16)),
        PERR("\\\"origin\\\" must be printable text"));
    EXPECT(req("passkeys.register_begin", "{\"user\":\"%s\",\"rp_id\":\"%s\"}", hx(USER1, 16),
               "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),
           PERR("give \\\"origin\\\" for an rp_id this long"));
    EXPECT(req("passkeys.register_begin", "{\"user\":\"%s\",\"rp_id\":\"%s\"}", hx(USER1, 16),
               "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),
           PERR("\\\"rp_id\\\" must be a string of at most 64 bytes"));
    EXPECT(
        req("passkeys.register_begin", "{\"user\":\"%s\",\"require_uv\":\"yes\"}", hx(USER1, 16)),
        PERR("\\\"require_uv\\\" must be true or false"));
    RFAIL = true;
    EXPECT(req("passkeys.register_begin", "{\"user\":\"%s\"}", hx(USER1, 16)),
           errc(-32603, "no random bytes"));
    RFAIL = false;
    EXPECT(req("passkeys.status", "{\"now\":%lld}", NOW),
           ok("{\"rp_id\":\"zxv.id\",\"alg\":-49,\"attestation\":\"none\",\"credentials\":0,"
              "\"active\":0,\"open_challenges\":0,\"max_credentials\":32,\"in_memory\":true}"));

    /* begin with a long rp id that still fits "https://" */
    EXPECT(
        req("passkeys.register_begin", "{\"user\":\"%s\",\"rp_id\":\"%s\",\"now\":%lld}",
            hx(USER1, 16), "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", NOW),
        ok("{\"type\":\"webauthn.create\",\"challenge\":\"%s\",\"rp_id\":\"%s\",\"origin\":"
           "\"https://%s\",\"alg\":-49,\"require_uv\":true,\"expires\":%lld,\"user\":\"%s\"}",
           (chal_of(0, ch), hx(ch, 32)), "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
           "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", NOW + 300, hx(USER1, 16)));

    /* the platform rp, the default */
    CHECK(id_passkey_create(&PK1, &H, ID_PLATFORM_RP, DEV, USER1) == ID_OK, "authenticator 1");
    EXPECT(
        req("passkeys.register_begin", "{\"user\":\"%s\",\"now\":%lld}", hx(USER1, 16), NOW),
        ok("{\"type\":\"webauthn.create\",\"challenge\":\"%s\",\"rp_id\":\"zxv.id\",\"origin\":"
           "\"https://zxv.id\",\"alg\":-49,\"require_uv\":true,\"expires\":%lld,\"user\":\"%s\"}",
           (chal_of(1, ch), hx(ch, 32)), NOW + 300, hx(USER1, 16)));
    EXPECT(req("passkeys.status", "{\"now\":%lld}", NOW),
           ok("{\"rp_id\":\"zxv.id\",\"alg\":-49,\"attestation\":\"none\",\"credentials\":0,"
              "\"active\":0,\"open_challenges\":2,\"max_credentials\":32,\"in_memory\":true}"));

    /* finish: parameter checks do not consume the challenge */
    EXPECT(req("passkeys.register_finish", "{\"challenge\":\"%s\"}", hx(ch, 32)),
           PERR("missing hex parameter \\\"auth\\\""));
    EXPECT(req("passkeys.register_finish", "{\"auth\":\"00\"}"),
           PERR("missing hex parameter \\\"challenge\\\""));
    EXPECT(req("passkeys.register_finish", "{\"auth\":\"00\",\"challenge\":\"00\"}"),
           PERR("\\\"challenge\\\" must be 32 bytes of hex"));
    EXPECT(req("passkeys.register_finish", "{\"auth\":\"00\",\"challenge\":\"%s\",\"now\":-5}",
               hx(ch, 32)),
           PERR("\\\"now\\\" must be unix seconds"));
    /* UV required, the device did not verify the user: refused, challenge used up */
    EXPECT(finish_reg(1, &PK1, false, NOW + 10),
           ERR("register: the authenticator data or signature does not verify"));
    EXPECT(finish_reg(1, &PK1, true, NOW + 10), ERR("unknown or used challenge"));

    /* a good registration */
    uint8_t k = begin_reg(USER1, "");
    EXPECT(finish_reg(k, &PK1, true, NOW + 10),
           ok("%s", cred_json(&PK1, "zxv.id", 0, false, NOW + 10, -1)));
    /* the same credential again */
    k = begin_reg(USER1, "");
    EXPECT(finish_reg(k, &PK1, true, NOW + 10),
           ERR("register: that credential is already registered"));

    /* an authenticator for another rp, a short blob, an expired challenge */
    CHECK(id_passkey_create(&PKX, &H, "evil.example", DEV, USER1) == ID_OK, "authenticator X");
    k = begin_reg(USER1, "");
    EXPECT(finish_reg(k, &PKX, true, NOW + 10),
           ERR("register: the authenticator data or signature does not verify"));
    k = begin_reg(USER1, "");
    chal_of(k, ch);
    EXPECT(req("passkeys.register_finish", "{\"challenge\":\"%s\",\"auth\":\"0011\",\"now\":%lld}",
               hx(ch, 32), NOW),
           ERR("register: malformed authenticator data"));
    k = begin_reg(USER1, "");
    EXPECT(finish_reg(k, &PK1, true, NOW + 300), ERR("the challenge has expired"));
    k = begin_reg(USER1, "");
    EXPECT(finish_reg(k, &PK1, true, NOW + 299),
           ERR("register: that credential is already registered"));

    /* a second user without UV required, on the default clock */
    CHECK(id_passkey_create(&PK2, &H, ID_PLATFORM_RP, DEV, USER2) == ID_OK, "authenticator 2");
    k = RCOUNT;
    rpc(req("passkeys.register_begin", "{\"user\":\"%s\",\"require_uv\":false}", hx(USER2, 16)));
    chal_of(k, ch);
    uint32_t n = 0;
    id_passkey_register(&PK2, false, AUTH, sizeof AUTH, &n);
    const char *got = rpc(req("passkeys.register_finish", "{\"challenge\":\"%s\",\"auth\":\"%s\"}",
                              hx(ch, 32), hx(AUTH, n)));
    char want[200];
    snprintf(want, sizeof want, "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"cred_id\":\"%s\"",
             hx(PK2.cred_id, 16));
    CHECK(!strncmp(got, want, strlen(want)) && strstr(got, "\"last_used\":null}}"),
          "registered on the clock: %s", got);
}

static void test_login(void)
{
    uint8_t ch[32];
    uint8_t k = RCOUNT;
    /* auth_begin: the user's live credentials for the rp */
    EXPECT(req("passkeys.auth_begin", "{\"user\":\"%s\",\"now\":%lld}", hx(USER1, 16), NOW),
           ok("{\"type\":\"webauthn.get\",\"challenge\":\"%s\",\"rp_id\":\"zxv.id\",\"origin\":"
              "\"https://zxv.id\",\"alg\":-49,\"require_uv\":true,\"expires\":%lld,\"allow\":"
              "[\"%s\"]}",
              (chal_of(k, ch), hx(ch, 32)), NOW + 300, hx(PK1.cred_id, 16)));
    EXPECT(finish_auth(k, &PK1, "https://zxv.id", true, NOW + 5),
           ok("{\"verified\":true,\"uv\":true,\"credential\":%s}",
              cred_json(&PK1, "zxv.id", 1, false, NOW + 10, NOW + 5)));
    /* the same answer again: the challenge is gone */
    EXPECT(finish_auth(k, &PK1, "https://zxv.id", true, NOW + 5), ERR("unknown or used challenge"));

    /* no user: any credential; a stated origin */
    k = RCOUNT;
    EXPECT(req("passkeys.auth_begin", "{\"origin\":\"https://app.example\",\"now\":%lld}", NOW),
           ok("{\"type\":\"webauthn.get\",\"challenge\":\"%s\",\"rp_id\":\"zxv.id\",\"origin\":"
              "\"https://app.example\",\"alg\":-49,\"require_uv\":true,\"expires\":%lld,"
              "\"allow\":[]}",
              (chal_of(k, ch), hx(ch, 32)), NOW + 300));
    /* signed for another origin: refused */
    EXPECT(finish_auth(k, &PK1, "https://zxv.id", true, NOW),
           ERR("login: the authenticator data or signature does not verify"));
    k = RCOUNT;
    rpc(req("passkeys.auth_begin", "{\"origin\":\"https://app.example\",\"now\":%lld}", NOW));
    EXPECT(finish_auth(k, &PK1, "https://app.example", true, NOW + 7),
           ok("{\"verified\":true,\"uv\":true,\"credential\":%s}",
              cred_json(&PK1, "zxv.id", 4, false, NOW + 10, NOW + 7)));

    /* a replayed counter: the authenticator's counter set back */
    k = RCOUNT;
    rpc(req("passkeys.auth_begin", "{\"now\":%lld}", NOW));
    PK1.sign_count = 1;
    EXPECT(finish_auth(k, &PK1, "https://zxv.id", true, NOW),
           ERR("login: the signature counter did not increase (replay or cloned key)"));
    PK1.sign_count = 10;
    /* UV required but not given */
    k = RCOUNT;
    rpc(req("passkeys.auth_begin", "{\"now\":%lld}", NOW));
    EXPECT(finish_auth(k, &PK1, "https://zxv.id", false, NOW),
           ERR("login: the authenticator data or signature does not verify"));
    /* not required: accepted, and uv reported false */
    k = RCOUNT;
    rpc(req("passkeys.auth_begin", "{\"require_uv\":false,\"now\":%lld}", NOW));
    EXPECT(finish_auth(k, &PK1, "https://zxv.id", false, NOW + 8),
           ok("{\"verified\":true,\"uv\":false,\"credential\":%s}",
              cred_json(&PK1, "zxv.id", 12, false, NOW + 10, NOW + 8)));
    /* a credential of another user than the challenge names */
    k = RCOUNT;
    rpc(req("passkeys.auth_begin", "{\"user\":\"%s\",\"now\":%lld}", hx(USER2, 16), NOW));
    EXPECT(finish_auth(k, &PK1, "https://zxv.id", true, NOW),
           ERR("login: unknown credential for this challenge"));
    /* a credential of another rp */
    k = RCOUNT;
    rpc(req("passkeys.auth_begin", "{\"rp_id\":\"other.example\",\"now\":%lld}", NOW));
    EXPECT(finish_auth(k, &PK1, "https://other.example", true, NOW),
           ERR("login: unknown credential for this challenge"));
    /* an unknown credential */
    k = RCOUNT;
    rpc(req("passkeys.auth_begin", "{\"now\":%lld}", NOW));
    EXPECT(finish_auth(k, &PKX, "https://zxv.id", true, NOW),
           ERR("login: unknown credential for this challenge"));
    /* a register challenge cannot log in, and vice versa */
    k = begin_reg(USER1, "");
    EXPECT(finish_auth(k, &PK1, "https://zxv.id", true, NOW), ERR("unknown or used challenge"));
    k = RCOUNT;
    rpc(req("passkeys.auth_begin", "{\"now\":%lld}", NOW));
    EXPECT(finish_reg(k, &PK1, true, NOW), ERR("unknown or used challenge"));
    /* an expired one */
    k = RCOUNT;
    rpc(req("passkeys.auth_begin", "{\"now\":%lld}", NOW));
    EXPECT(finish_auth(k, &PK1, "https://zxv.id", true, NOW + 300),
           ERR("the challenge has expired"));
    /* space and tilde are the printable ends */
    k = RCOUNT;
    EXPECT(req("passkeys.auth_begin", "{\"rp_id\":\" ~\",\"origin\":\"~ \",\"now\":%lld}", NOW),
           ok("{\"type\":\"webauthn.get\",\"challenge\":\"%s\",\"rp_id\":\" ~\",\"origin\":"
              "\"~ \",\"alg\":-49,\"require_uv\":true,\"expires\":%lld,\"allow\":[]}",
              (chal_of(k, ch), hx(ch, 32)), NOW + 300));
    /* now 0 is a time like any other: the challenge expires at 300 */
    k = RCOUNT;
    EXPECT(req("passkeys.auth_begin", "{\"now\":0}"),
           ok("{\"type\":\"webauthn.get\",\"challenge\":\"%s\",\"rp_id\":\"zxv.id\",\"origin\":"
              "\"https://zxv.id\",\"alg\":-49,\"require_uv\":true,\"expires\":300,\"allow\":[]}",
              (chal_of(k, ch), hx(ch, 32))));
    /* parameter shapes */
    EXPECT(req("passkeys.auth_finish", "{}"), PERR("missing hex parameter \\\"cred_id\\\""));
    EXPECT(req("passkeys.auth_finish", "{\"cred_id\":\"%s\"}", hx(PK1.cred_id, 16)),
           PERR("missing hex parameter \\\"auth\\\""));
    EXPECT(req("passkeys.auth_finish", "{\"cred_id\":\"%s\",\"auth\":\"%s00\"}",
               hx(PK1.cred_id, 16), hx(A2, sizeof A2)),
           PERR("\\\"auth\\\" must be a hex string of at most 37 bytes"));
    EXPECT(req("passkeys.auth_finish", "{\"cred_id\":\"%s\",\"auth\":\"00\"}", hx(PK1.cred_id, 16)),
           PERR("missing hex parameter \\\"sig\\\""));
    EXPECT(req("passkeys.auth_finish", "{\"cred_id\":\"%s\",\"auth\":\"00\",\"sig\":\"%s\"}",
               hx(PK1.cred_id, 16), hx(SIG, sizeof SIG)),
           PERR("missing hex parameter \\\"challenge\\\""));
    EXPECT(req("passkeys.auth_finish", "{\"cred_id\":\"00\"}"),
           PERR("\\\"cred_id\\\" must be 16 bytes of hex"));
    /* a short authenticatorData */
    k = RCOUNT;
    rpc(req("passkeys.auth_begin", "{\"now\":%lld}", NOW));
    chal_of(k, ch);
    EXPECT(
        req("passkeys.auth_finish",
            "{\"challenge\":\"%s\",\"cred_id\":\"%s\",\"auth\":\"00\",\"sig\":\"%s\",\"now\":%lld}",
            hx(ch, 32), hx(PK1.cred_id, 16), hx(SIG, sizeof SIG), NOW),
        ERR("login: malformed authenticator data"));
}

static void test_list_revoke(void)
{
    char a[400], b[500];
    snprintf(a, sizeof a, "%s", cred_json(&PK1, "zxv.id", 12, false, NOW + 10, NOW + 8));
    const char *l = rpc(req("passkeys.list", "{}"));
    snprintf(b, sizeof b, "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"credentials\":[%s,", a);
    CHECK(!strncmp(l, b, strlen(b)), "list all: %s", l);
    EXPECT(req("passkeys.list", "{\"user\":\"%s\"}", hx(USER1, 16)),
           ok("{\"credentials\":[%s]}", a));
    EXPECT(req("passkeys.list", "{\"user\":\"%s\"}", hx(PK1.cred_id, 16)),
           ok("{\"credentials\":[]}"));
    EXPECT(req("passkeys.list", "{\"user\":\"0011\"}"),
           PERR("\\\"user\\\" must be 16 bytes of hex"));

    EXPECT(req("passkeys.revoke", "{\"cred_id\":\"%s\"}", hx(PK1.cred_id, 16)),
           ok("%s", cred_json(&PK1, "zxv.id", 12, true, NOW + 10, NOW + 8)));
    EXPECT(req("passkeys.revoke", "{\"cred_id\":\"%s\"}", hx(PK1.cred_id, 16)),
           ERR("revoke: already revoked"));
    EXPECT(req("passkeys.revoke", "{\"cred_id\":\"%s\"}", hx(PKX.cred_id, 16)),
           ERR("revoke: unknown credential"));
    EXPECT(req("passkeys.revoke", "{}"), PERR("missing hex parameter \\\"cred_id\\\""));
    /* a revoked credential never logs in, and auth_begin no longer offers it */
    uint8_t k = RCOUNT, ch[32];
    EXPECT(req("passkeys.auth_begin", "{\"user\":\"%s\",\"now\":%lld}", hx(USER1, 16), NOW),
           ok("{\"type\":\"webauthn.get\",\"challenge\":\"%s\",\"rp_id\":\"zxv.id\",\"origin\":"
              "\"https://zxv.id\",\"alg\":-49,\"require_uv\":true,\"expires\":%lld,\"allow\":[]}",
              (chal_of(k, ch), hx(ch, 32)), NOW + 300));
    EXPECT(finish_auth(k, &PK1, "https://zxv.id", true, NOW),
           ERR("login: the credential is revoked"));
}

static void test_limits(void)
{
    fresh();
    zxvd_rpc_enable("passkeys");
    /* A1: 16 open challenges; the 17th replaces the oldest */
    uint8_t first = RCOUNT;
    for (int i = 0; i < 17; i++) begin_reg(USER1, "");
    EXPECT(req("passkeys.status", "{\"now\":%lld}", NOW),
           ok("{\"rp_id\":\"zxv.id\",\"alg\":-49,\"attestation\":\"none\",\"credentials\":0,"
              "\"active\":0,\"open_challenges\":16,\"max_credentials\":32,\"in_memory\":true}"));
    EXPECT(req("passkeys.status", "{\"now\":%lld}", NOW + 300),
           ok("{\"rp_id\":\"zxv.id\",\"alg\":-49,\"attestation\":\"none\",\"credentials\":0,"
              "\"active\":0,\"open_challenges\":0,\"max_credentials\":32,\"in_memory\":true}"));
    EXPECT(finish_reg(first, &PK1, true, NOW), ERR("unknown or used challenge"));
    EXPECT(finish_reg((uint8_t) (first + 2), &PK1, true, NOW),
           ok("%s", cred_json(&PK1, "zxv.id", PK1.sign_count, false, NOW, -1)));
    /* a freed slot is taken before the oldest live challenge is replaced */
    begin_reg(USER1, ""); /* first+17 */
    EXPECT(finish_reg((uint8_t) (first + 1), &PK1, true, NOW),
           ERR("register: that credential is already registered"));
    EXPECT(finish_reg((uint8_t) (first + 16), &PK1, true, NOW),
           ERR("register: that credential is already registered"));
    begin_reg(USER1, ""); /* first+18, slot 0 */
    begin_reg(USER1, ""); /* first+19, slot 1: all 16 open again */
    /* all expired: the first expired slot (first+18) goes, not the oldest (first+3) */
    rpc(req("passkeys.register_begin", "{\"user\":\"%s\",\"now\":%lld}", hx(USER1, 16), NOW + 300));
    EXPECT(finish_reg((uint8_t) (first + 18), &PK1, true, NOW), ERR("unknown or used challenge"));
    EXPECT(finish_reg((uint8_t) (first + 3), &PK1, true, NOW),
           ERR("register: that credential is already registered"));

    /* A5: 32 credentials, then the table is full */
    for (uint32_t i = 1; i < 32; i++) {
        uint8_t user[16] = {0};
        user[0] = (uint8_t) i;
        id_passkey_t pk;
        CHECK(id_passkey_create(&pk, &H, ID_PLATFORM_RP, DEV, user) == ID_OK, "create");
        uint8_t k = begin_reg(user, "");
        const char *g = rpc(finish_reg(k, &pk, true, NOW));
        CHECK(strstr(g, "\"result\"") != NULL, "credential %u: %.120s", (unsigned) i, g);
    }
    id_passkey_t last;
    id_passkey_create(&last, &H, ID_PLATFORM_RP, DEV, USER2);
    uint8_t k = begin_reg(USER2, "");
    EXPECT(finish_reg(k, &last, true, NOW), ERR("register: the credential table is full"));
    EXPECT(req("passkeys.status", "{\"now\":%lld}", NOW),
           ok("{\"rp_id\":\"zxv.id\",\"alg\":-49,\"attestation\":\"none\",\"credentials\":32,"
              "\"active\":32,\"open_challenges\":%u,\"max_credentials\":32,\"in_memory\":true}",
              15u));
    /* off keeps the state; status answers while off */
    EXPECT(req("features.set", "{\"feature\":\"passkeys\",\"enabled\":false}"),
           ok("{\"feature\":\"passkeys\",\"enabled\":false,\"regulated\":false,\"saved\":false}"));
    EXPECT(
        req("passkeys.revoke", "{\"cred_id\":\"%s\"}", hx(PK1.cred_id, 16)),
        errc(-32012, "passkeys is off: start zxvd with --enable passkeys, or call features.set"));
    const char *s = rpc(req("passkeys.status", "{}"));
    CHECK(strstr(s, "\"credentials\":32,\"active\":32,") != NULL, "kept: %s", s);
    /* close forgets everything */
    fresh();
    EXPECT(req("passkeys.status", "{\"now\":%lld}", NOW),
           ok("{\"rp_id\":\"zxv.id\",\"alg\":-49,\"attestation\":\"none\",\"credentials\":0,"
              "\"active\":0,\"open_challenges\":0,\"max_credentials\":32,\"in_memory\":true}"));
    zxvd_rpc_groups_reset();
}

int main(void)
{
    test_switch();
    test_register();
    test_login();
    test_list_revoke();
    test_limits();
    free(OUT.p);
    if (fails) {
        printf("[FAIL] test_zxvd_ident: %d of %d checks failed\n", fails, checks);
        return 1;
    }
    printf("[PASS] test_zxvd_ident: %d checks\n", checks);
    return 0;
}
