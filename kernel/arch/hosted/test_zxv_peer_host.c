/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_zxv_peer_host.c — remote inference and settlement between two nodes
 * (zxv_peer_host.h), over a real TCP listener on 127.0.0.1 and over a
 * socketpair. The server runs in a forked child so each side has its own
 * process, as on two machines; the parent checks the buyer's book and
 * reads the seller's results back through a pipe.
 *
 * Checked: a paid request settles with fee = floor(gross * 8889 / 10^7)
 * split 50/25/15/10 on the buyer's book and the full price on the seller's;
 * the carry makes many small payments pay what their sum would; a price
 * above max_price is refused before anything is paid; an impersonated
 * NodeID fails the handshake; an under-paid fee and a forged settle
 * signature are refused by the seller (signed refusal); a forged invoice
 * signature is refused by the buyer before it pays; a wallet without funds
 * pays nothing; LAN policy refuses a public address.
 *
 *   sh kernel/arch/hosted/test_hosted.sh runs it (POSIX hosts). */
#define _DEFAULT_SOURCE
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "zxv_peer_host.h"

static int failures = 0;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        int ok_ = (c) ? 1 : 0;                                                                     \
        if (!ok_) failures++;                                                                      \
        printf(ok_ ? "  ok   " : "  FAIL ");                                                       \
        printf(__VA_ARGS__);                                                                       \
        printf("\n");                                                                              \
    } while (0)

static vna_identity_t idA, idB, idC;

static void echo_answer(void *ctx, const char *prompt, char *out, size_t cap)
{
    (void) ctx;
    snprintf(out, cap, "B says: %s", prompt);
}

static void mk_id(vna_identity_t *id, uint8_t tag)
{
    uint8_t seed[32];
    memset(seed, tag, sizeof seed);
    if (vna_identity_create(id, seed, 0, 1) != VNA_OK) {
        printf("identity failed\n");
        exit(1);
    }
}

/* What the seller reports back to the parent after serving. */
typedef struct {
    int rc;
    zxv_peer_status_t st;
} seller_report_t;

/* Run one exchange over a socketpair: the child serves as B (with `stamper`
 * tamper flags and `price`), the parent asks as A with `buyer`. Returns the
 * ask's status and fills *rep with the seller's view. */
static int exchange(zxv_peer_t *buyer, const vna_id_t *expect_id, uint64_t price, uint32_t stamper,
                    const char *prompt, uint64_t max_price, zxv_peer_result_t *res,
                    seller_report_t *rep, uint32_t rounds)
{
    int sv[2], pp[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0 || pipe(pp) != 0) return -100;
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) {
        close(sv[0]);
        close(pp[0]);
        zxv_peer_t *b = zxv_peer_new();
        zxv_peer_set_identity(b, &idB, true);
        zxv_peer_test_tamper(b, stamper);
        /* serve only on the socketpair: a listener on an ephemeral port
         * gives the answer callback and price */
        const char *why;
        if (zxv_peer_serve(b, "127.0.0.1", 0, price, echo_answer, NULL, &why) != 0) _exit(3);
        seller_report_t r;
        memset(&r, 0, sizeof r);
        for (uint32_t i = 0; i < rounds; i++) r.rc |= zxv_peer_serve_fd(b, sv[1], 1000 + i);
        zxv_peer_status(b, &r.st);
        if (write(pp[1], &r, sizeof r) != (ssize_t) sizeof r) _exit(4);
        zxv_peer_free(b);
        _exit(0);
    }
    close(sv[1]);
    close(pp[1]);
    int rc = 0;
    for (uint32_t i = 0; i < rounds; i++)
        rc = zxv_peer_ask_fd(buyer, sv[0], expect_id, prompt, max_price, 1000 + i, res);
    close(sv[0]);
    memset(rep, 0, sizeof *rep);
    ssize_t got = read(pp[0], rep, sizeof *rep);
    close(pp[0]);
    int ws = 0;
    waitpid(pid, &ws, 0);
    if (got != (ssize_t) sizeof *rep) rep->rc = -99;
    return rc;
}

int main(void)
{
    mk_id(&idA, 0xA1);
    mk_id(&idB, 0xB2);
    mk_id(&idC, 0xC3);
    zxv_peer_result_t *res = calloc(1, sizeof *res);
    seller_report_t rep;
    zxv_peer_status_t st;

    printf("[1] one paid request: PQ session, answer, settlement with the 0.08889%% fee\n");
    zxv_peer_t *a = zxv_peer_new();
    zxv_peer_set_identity(a, &idA, true);
    CHECK(zxv_peer_fund(a, 1000000) == 0, "demo float issued to A's wallet");
    int rc = exchange(a, &idB.id, 125000, 0, "what is 2+2?", 200000, res, &rep, 1);
    CHECK(rc == 0, "A's ask settled (%d %s)", rc, res->error);
    CHECK(strcmp(res->answer, "B says: what is 2+2?") == 0, "answer came from B: \"%s\"",
          res->answer);
    CHECK(res->s.gross == 125000 && res->s.fee == 111,
          "gross 125000, fee 111 = floor(125000 * "
          "8889 / 10^7) (got %llu, %llu)",
          (unsigned long long) res->s.gross, (unsigned long long) res->s.fee);
    CHECK(res->s.parts[0] == 57 && res->s.parts[1] == 27 && res->s.parts[2] == 16 &&
              res->s.parts[3] == 11,
          "fee split 50/25/15/10, remainder to the reserve: 57 27 16 11");
    CHECK(memcmp(res->s.peer.b, idB.id.b, 32) == 0, "the seller is B's NodeID");
    zxv_peer_status(a, &st);
    CHECK(st.wallet == 1000000 - 125000 - 111, "A's wallet paid price + fee (%llu)",
          (unsigned long long) st.wallet);
    CHECK(st.buckets[0] == 57 && st.buckets[1] == 27 && st.buckets[2] == 16 && st.buckets[3] == 11,
          "A's four fee buckets hold the fee");
    CHECK(st.book_ok, "A's book: invariants hold and the hash chain verifies");
    CHECK(st.bought == 1 && st.n_history == 1 && st.history[0].role == ZXV_PEER_ROLE_BUYER,
          "A remembers one purchase");
    CHECK(rep.rc == 0 && rep.st.sold == 1 && rep.st.wallet == 125000 && rep.st.book_ok,
          "B was paid the whole price (%llu) and its book holds",
          (unsigned long long) rep.st.wallet);
    CHECK(rep.st.n_history == 1 && memcmp(rep.st.history[0].settle, res->s.settle, 32) == 0 &&
              memcmp(rep.st.history[0].invoice, res->s.invoice, 32) == 0,
          "both nodes hold the same invoice and settle digests");

    printf("[2] the sub-unit carry: 3 payments of 500 pay what one of 1500 would\n");
    zxv_peer_t *c = zxv_peer_new();
    zxv_peer_set_identity(c, &idA, true);
    zxv_peer_fund(c, 100000);
    rc = exchange(c, &idB.id, 500, 0, "small", 500, res, &rep, 3);
    zxv_peer_status(c, &st);
    CHECK(rc == 0 && st.bought == 3 && st.fees_paid == 1,
          "fees 0 + 0 + 1 = floor(1500 * 8889 / 10^7) = 1 (paid %llu)",
          (unsigned long long) st.fees_paid);
    CHECK(rep.st.sold == 3 && rep.st.wallet == 1500, "B accepted the carried fee each time");
    zxv_peer_free(c);

    printf("[3] refusals\n");
    zxv_peer_status(a, &st);
    uint64_t before = st.wallet;
    rc = exchange(a, &idB.id, 125000, 0, "too cheap", 1000, res, &rep, 1);
    zxv_peer_status(a, &st);
    CHECK(rc == ZXV_PEER_E_PRICE && st.wallet == before && rep.st.sold == 0,
          "price above max_price: refused, nothing paid, nothing answered");
    rc = exchange(a, &idC.id, 125000, 0, "who are you", 200000, res, &rep, 1);
    CHECK(rc == ZXV_PEER_E_HANDSHAKE, "B's key does not hash to the NodeID A expected (C's): "
                                      "handshake fails");
    zxv_peer_test_tamper(a, 1);
    rc = exchange(a, &idB.id, 125000, 0, "underpay", 200000, res, &rep, 1);
    CHECK(rc == ZXV_PEER_E_REFUSED && rep.st.sold == 0 && rep.st.wallet == 0,
          "fee under-paid by one: B refuses and credits nothing (signed refusal)");
    zxv_peer_test_tamper(a, 2);
    rc = exchange(a, &idB.id, 125000, 0, "forged", 200000, res, &rep, 1);
    CHECK(rc == ZXV_PEER_E_REFUSED && rep.st.sold == 0, "forged settle signature: B refuses");
    zxv_peer_test_tamper(a, 0);
    zxv_peer_status(a, &st);
    before = st.wallet;
    rc = exchange(a, &idB.id, 125000, 4, "bad invoice", 200000, res, &rep, 1);
    zxv_peer_status(a, &st);
    CHECK(rc == ZXV_PEER_E_SIG && st.wallet == before,
          "forged invoice signature: A refuses before paying");
    zxv_peer_t *poor = zxv_peer_new();
    zxv_peer_set_identity(poor, &idA, true);
    rc = zxv_peer_ask_fd(poor, -1, &idB.id, "hi", 125000, 1, res);
    CHECK(rc == ZXV_PEER_E_FUNDS, "an empty wallet asks nothing");
    zxv_peer_fund(poor, 10);
    rc = zxv_peer_ask(poor, "8.8.8.8", 8723, &idB.id, "hi", 0, 1, res);
    CHECK(rc == ZXV_PEER_E_POLICY, "LAN mode refuses a public address before connecting");
    zxv_peer_free(poor);

    printf("[4] the real TCP listener on 127.0.0.1\n");
    zxv_peer_t *b = zxv_peer_new();
    zxv_peer_set_identity(b, &idB, true);
    const char *why = "";
    CHECK(zxv_peer_serve(b, "127.0.0.1", 0, 70000, echo_answer, NULL, &why) == 0 &&
              zxv_peer_fd(b) >= 0,
          "B listens (%s)", why);
    zxv_peer_status(b, &st);
    uint16_t port = st.port;
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) {
        zxv_peer_on_readable(b, 5000); /* accept and serve one connection */
        zxv_peer_status(b, &st);
        _exit(st.sold == 1 && st.wallet == 70000 ? 0 : 1);
    }
    rc = zxv_peer_ask(a, "127.0.0.1", port, &idB.id, "over TCP", 70000, 5000, res);
    int ws = 0;
    waitpid(pid, &ws, 0);
    CHECK(rc == 0 && strcmp(res->answer, "B says: over TCP") == 0 && res->s.fee == 62,
          "TCP: answered and settled, fee 62 on 70000 (%d %s)", rc, res->error);
    CHECK(WIFEXITED(ws) && WEXITSTATUS(ws) == 0, "TCP: B's book shows the sale");
    zxv_peer_stop(b);
    CHECK(zxv_peer_fd(b) < 0, "stopped: no listener");
    rc = zxv_peer_ask(a, "127.0.0.1", port, &idB.id, "anyone?", 70000, 5000, res);
    CHECK(rc == ZXV_PEER_E_CONNECT, "nobody serving: connect fails cleanly");
    zxv_peer_status(a, &st);
    CHECK(st.book_ok, "A's book still verifies after every case");
    zxv_peer_free(b);
    zxv_peer_free(a);
    free(res);

    if (failures) {
        printf("[FAIL] peer host: %d failure(s)\n", failures);
        return 1;
    }
    printf("[PASS] peer host: PQ session, paid request, fee settlement, refusals\n");
    return 0;
}
