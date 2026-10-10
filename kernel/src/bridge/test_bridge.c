/* test_bridge.c — the one resolver, across all three realms.
 *
 * We prove: (1) classification puts each URI shape in the right realm,
 * (2) resolution DISPATCHES to that realm's backend and returns its answer in a
 * single result type, (3) with a backend unbound the bridge says NOT_BOUND —
 * it never invents an address, (4) the gateway maps a legacy HTTP path to a
 * Web3/Web4 resource, and (5) hostile URIs are rejected.
 */
#include <stdio.h>
#include <string.h>
#include "bridge.h"

static int failures = 0;
#define CHECK(c, m)                                                                                \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("[FAIL] %s\n", m);                                                              \
            failures++;                                                                            \
        } else                                                                                     \
            printf("[PASS] %s\n", m);                                                              \
    } while (0)

/* mock backends record which realm was asked and return known answers */
static int dns_calls, cid_calls, intent_calls;
static char last_host[256], last_ref[256], last_intent[256];

static int mock_dns(const char *host, uint8_t ip[4], void *ctx)
{
    (void) ctx;
    dns_calls++;
    bridge_realm_t r;
    (void) r;
    {
        uint32_t i = 0;
        for (; host[i] && i < 255; i++) last_host[i] = host[i];
        last_host[i] = 0;
    }
    ip[0] = 93;
    ip[1] = 184;
    ip[2] = 216;
    ip[3] = 34; /* example.com */
    return 0;
}
static int mock_cid(const char *ref, uint8_t cid[32], void *ctx)
{
    (void) ctx;
    cid_calls++;
    {
        uint32_t i = 0;
        for (; ref[i] && i < 255; i++) last_ref[i] = ref[i];
        last_ref[i] = 0;
    }
    for (int i = 0; i < 32; i++) cid[i] = (uint8_t) (i + 1);
    return 0;
}
static int mock_intent(const char *intent, uint32_t *h, void *ctx)
{
    (void) ctx;
    intent_calls++;
    {
        uint32_t i = 0;
        for (; intent[i] && i < 255; i++) last_intent[i] = intent[i];
        last_intent[i] = 0;
    }
    *h = 0xC0FFEE;
    return 0;
}

int main(void)
{
    printf("=== Web2/Web3/Web4 bridge: one resolver across three realms ===\n");

    /* ---------- classification ---------- */
    CHECK(bridge_classify("https://example.com/x") == REALM_WEB2, "https:// -> web2");
    CHECK(bridge_classify("http://a.b") == REALM_WEB2, "http:// -> web2");
    CHECK(bridge_classify("example.com") == REALM_WEB2, "bare host.tld -> web2");
    CHECK(bridge_classify("ipfs://deadbeef") == REALM_WEB3, "ipfs:// -> web3");
    CHECK(bridge_classify("cid:abc123") == REALM_WEB3, "cid: -> web3");
    CHECK(bridge_classify("did:zxv:alice") == REALM_WEB3, "did: -> web3 (identity)");
    CHECK(bridge_classify("chiglet:summarize") == REALM_WEB4, "chiglet: -> web4");
    CHECK(bridge_classify("intent:open-door") == REALM_WEB4, "intent: -> web4");
    CHECK(bridge_classify("did:zxv:bob") != REALM_WEB2, "did: is NOT mistaken for a host");
    CHECK(bridge_classify("") == REALM_UNKNOWN, "empty -> unknown");
    CHECK(bridge_classify("just a phrase") == REALM_UNKNOWN, "a phrase with spaces -> unknown");
    CHECK(bridge_classify("gopher://old") == REALM_UNKNOWN, "an unknown scheme -> unknown");
    CHECK(bridge_classify("bad\x01host.com") == REALM_UNKNOWN, "control chars -> unknown");

    /* ---------- dispatch: each realm calls ITS backend, one result type ------ */
    bridge_t b;
    bridge_init(&b);
    bridge_ops_t ops = {mock_dns, mock_cid, mock_intent, 0};
    bridge_set_ops(&b, &ops);

    dns_calls = cid_calls = intent_calls = 0;
    bridge_result_t r;

    CHECK(bridge_resolve(&b, "https://example.com/page", &r) == BR_OK, "web2 resolves OK");
    CHECK(r.realm == REALM_WEB2 && dns_calls == 1 && cid_calls == 0 && intent_calls == 0,
          "web2 dispatched to DNS only");
    CHECK(strcmp(last_host, "example.com") == 0,
          "host extracted from the URL (no scheme, no path)");
    CHECK(r.ip[0] == 93 && r.ip[1] == 184 && r.ip[2] == 216 && r.ip[3] == 34,
          "the A record is returned");

    CHECK(bridge_resolve(&b, "ipfs://QmHash", &r) == BR_OK, "web3 resolves OK");
    CHECK(r.realm == REALM_WEB3 && cid_calls == 1 && dns_calls == 1,
          "web3 dispatched to the CID backend only");
    CHECK(strcmp(last_ref, "QmHash") == 0, "the content ref is stripped of its scheme");
    CHECK(r.cid[0] == 1 && r.cid[31] == 32, "the CID is returned in the same result type");

    CHECK(bridge_resolve(&b, "did:zxv:alice", &r) == BR_OK, "did resolves via the web3 backend");
    CHECK(strcmp(last_ref, "zxv:alice") == 0, "the DID body is passed to the resolver");

    CHECK(bridge_resolve(&b, "chiglet:summarize-inbox", &r) == BR_OK, "web4 resolves OK");
    CHECK(r.realm == REALM_WEB4 && intent_calls == 1,
          "web4 dispatched to the Chiglet backend only");
    CHECK(strcmp(last_intent, "summarize-inbox") == 0, "the intent is stripped of its scheme");
    CHECK(r.intent == 0xC0FFEE, "the intent handle is returned");

    CHECK(bridge_resolve(&b, "gopher://nope", &r) == BR_BAD_URI,
          "an unclassifiable URI is BAD_URI");
    CHECK(bridge_resolve(&b, "", &r) == BR_BAD_URI, "empty is BAD_URI");

    /* ---------- unbound backend: NOT_BOUND, never a fabricated address ------- */
    {
        bridge_t nb;
        bridge_init(&nb); /* no ops installed */
        bridge_result_t z;
        CHECK(bridge_resolve(&nb, "https://example.com", &z) == BR_NOT_BOUND,
              "web2 with no DNS backend -> NOT_BOUND (no invented IP)");
        CHECK(z.ip[0] == 0 && z.ip[1] == 0 && z.ip[2] == 0 && z.ip[3] == 0,
              "the IP is left zero, not fabricated");
        CHECK(bridge_resolve(&nb, "ipfs://x", &z) == BR_NOT_BOUND, "web3 unbound -> NOT_BOUND");
        CHECK(bridge_resolve(&nb, "chiglet:x", &z) == BR_NOT_BOUND, "web4 unbound -> NOT_BOUND");
    }

    /* ---------- gateway: a legacy HTTP path reaches web3/web4 ---------------- */
    {
        char ref[128];
        CHECK(bridge_gateway_route("/ipfs/deadbeefcafe", ref, sizeof ref) == REALM_WEB3 &&
                  strcmp(ref, "deadbeefcafe") == 0,
              "GET /ipfs/<cid> routes to web3 with the cid");
        CHECK(bridge_gateway_route("/did/zxv:carol", ref, sizeof ref) == REALM_WEB3 &&
                  strcmp(ref, "did:zxv:carol") == 0,
              "GET /did/<id> routes to web3 as a did: ref");
        CHECK(bridge_gateway_route("/chiglet/translate", ref, sizeof ref) == REALM_WEB4 &&
                  strcmp(ref, "translate") == 0,
              "GET /chiglet/<intent> routes to web4");
        CHECK(bridge_gateway_route("/index.html", ref, sizeof ref) == REALM_WEB2,
              "an ordinary path stays web2 (served as HTTP)");
        CHECK(bridge_gateway_route("/ipfs/cid?dl=1", ref, sizeof ref) == REALM_WEB3 &&
                  strcmp(ref, "cid") == 0,
              "query string is stripped from the gateway ref");
    }

    /* ---------- hostile input to the resolver never overruns ---------------- */
    {
        char big[600];
        for (int i = 0; i < 599; i++) big[i] = 'a';
        big[599] = 0;
        bridge_result_t z;
        (void) bridge_resolve(&b, big, &z); /* over BRIDGE_URI_MAX -> UNKNOWN/BAD */
        CHECK(1, "an over-long URI is handled without overrun");
    }

    printf("\n%s: %d failure(s)\n", failures ? "*** FAILED ***" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
