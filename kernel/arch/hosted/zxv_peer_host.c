/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zxv_peer_host.c — remote inference over TCP in a Vinea session, settled with
 * the assurance fee. See zxv_peer_host.h (P1-P7). */
#if defined(_WIN32)
#    define WIN32_LEAN_AND_MEAN
#    include <winsock2.h>
#    include <ws2tcpip.h>
#    include <windows.h>
typedef SOCKET psock_t;
#    define PCLOSE    closesocket
#    define PBAD_SOCK INVALID_SOCKET
typedef int plen_t;
#else
#    define _DEFAULT_SOURCE
#    define _DARWIN_C_SOURCE
#    include <sys/types.h>
#    include <sys/socket.h>
#    include <sys/select.h>
#    include <sys/time.h>
#    include <netinet/in.h>
#    include <netinet/tcp.h>
#    include <arpa/inet.h>
#    include <errno.h>
#    include <fcntl.h>
#    include <unistd.h>
typedef int psock_t;
#    define PCLOSE    close
#    define PBAD_SOCK (-1)
typedef socklen_t plen_t;
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zxv_peer_host.h"
#include "zxv_http_guard.h" /* zxv_os_random */
#include "vna_session.h"
#include "vna_frame.h" /* VNA_FEAT_UBH168 */
#include "pay_ledger.h"

#define REC_REQ 0x51u
#define REC_ANS 0x52u
#define REC_PAY 0x53u
#define REC_ACK 0x54u

#define ANS_OK    0u
#define ANS_PRICE 1u
#define ANS_BAD   2u

#define ACK_OK      0u
#define ACK_REFUSED 1u

#define CTX_INVOICE "zxv/peer/v1/invoice"
#define CTX_SETTLE  "zxv/peer/v1/settle"
#define CTX_ACK     "zxv/peer/v1/ack"

/* book owners */
#define OWN_ISSUER 1u
#define OWN_WALLET 2u
#define OWN_MESH   3u
#define OWN_BUCKET 10u

struct zxv_peer {
    const vna_identity_t *self;
    bool lan_only;
    psock_t ls;
    uint16_t port;
    uint64_t price;
    zxv_peer_answer_fn answer_fn;
    void *answer_ctx;
    uint32_t tamper;
    /* the peer book (P6) */
    pay_ledger_t L;
    uint32_t a_issuer, a_wallet, a_mesh, a_bucket[PAY_ASSURE_BUCKETS];
    uint64_t posting_seq;
    uint64_t fees_paid;
    uint32_t sold, bought, refused, sessions;
    zxv_peer_settlement_t hist[ZXV_PEER_HISTORY];
    uint32_t n_hist, hist_head;
    /* scratch: keeps large objects off the stack */
    vna_hs_t hs;
    vna_sess_t sess;
    uint8_t peer_pk[VNA_PK_LEN];
    uint8_t fbuf[ZXV_PEER_FRAME_MAX];
    uint8_t pbuf[ZXV_PEER_FRAME_MAX];
};

/* ---------- small helpers ---------- */

static void put64(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; i++) p[i] = (uint8_t) (v >> (56 - 8 * i));
}

static uint64_t get64(const uint8_t *p)
{
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v = (v << 8) | p[i];
    return v;
}

static void wipe(void *p, size_t n)
{
    volatile uint8_t *q = (volatile uint8_t *) p;
    while (n--) *q++ = 0;
}

static bool ip_private(const uint8_t a[4])
{
    return a[0] == 10 || a[0] == 127 || (a[0] == 172 && (a[1] & 0xF0) == 16) ||
           (a[0] == 192 && a[1] == 168) || (a[0] == 169 && a[1] == 254);
}

static bool addr_allowed(const zxv_peer_t *p, const struct sockaddr_in *sa)
{
    if (!p->lan_only) return true;
    uint8_t a[4];
    memcpy(a, &sa->sin_addr.s_addr, 4);
    return ip_private(a);
}

static uint64_t sat_add(uint64_t a, uint64_t b)
{
    return a + b < a ? UINT64_MAX : a + b;
}

/* ---------- the peer book (P6, P7) ---------- */

static int book_init(zxv_peer_t *p)
{
    pay_ledger_init(&p->L, NULL);
    uint16_t vfv = p->L.vfv_asset;
    if (pay_ledger_open(&p->L, OWN_ISSUER, vfv, PAY_CAP_FINANCIAL, PAY_ACCT_ISSUER, &p->a_issuer) !=
            PAY_OK ||
        pay_ledger_open(&p->L, OWN_WALLET, vfv, PAY_CAP_FINANCIAL, 0, &p->a_wallet) != PAY_OK ||
        pay_ledger_open(&p->L, OWN_MESH, vfv, PAY_CAP_FINANCIAL,
                        PAY_ACCT_ISSUER | PAY_ACCT_EXTERNAL, &p->a_mesh) != PAY_OK)
        return -1;
    for (uint32_t i = 0; i < PAY_ASSURE_BUCKETS; i++)
        if (pay_ledger_open(&p->L, OWN_BUCKET + i, vfv, PAY_CAP_FINANCIAL, PAY_ACCT_COMMONS,
                            &p->a_bucket[i]) != PAY_OK)
            return -1;
    return 0;
}

/* A posting request whose identifiers derive from `key` (a settle digest, or
 * the funding sequence) and the role, so a retry of the same settlement is a
 * PAY_DUPLICATE, never a second payment. */
static void new_req(zxv_peer_t *p, pay_posting_req_t *req, const uint8_t key[32], uint8_t role,
                    const char *memo, uint64_t tick)
{
    uint8_t in[34], h[32];
    memset(req, 0, sizeof *req);
    in[0] = 'Z';
    in[1] = role;
    memcpy(in + 2, key, 32);
    sha3_256(in, sizeof in, req->idem_key);
    in[0] = 'U';
    sha3_256(in, sizeof in, h);
    pay_uetr_from_random(h, req->uetr);
    snprintf(req->e2e, sizeof req->e2e, "ZXP-%llu", (unsigned long long) ++p->posting_seq);
    snprintf(req->memo, sizeof req->memo, "%s", memo);
    req->initiator = OWN_WALLET;
    req->attestor = OWN_WALLET;
    req->tick = tick;
}

int zxv_peer_fund(zxv_peer_t *p, uint64_t minor)
{
    if (!p || !minor) return -1;
    uint8_t key[32] = {0};
    put64(key, ++p->posting_seq);
    key[8] = 'F';
    pay_posting_req_t req;
    pay_receipt_t rc;
    new_req(p, &req, key, 'F', "peer demo float", p->posting_seq);
    return pay_ledger_issue(&p->L, &req, p->a_issuer, p->a_wallet, minor, &rc) == PAY_OK ? 0 : -1;
}

static uint64_t wallet_balance(const zxv_peer_t *p)
{
    const pay_account_t *a = pay_ledger_account(&p->L, p->a_wallet);
    return a ? a->debit : 0;
}

static void remember(zxv_peer_t *p, const zxv_peer_settlement_t *s)
{
    uint32_t i = (p->hist_head + p->n_hist) % ZXV_PEER_HISTORY;
    if (p->n_hist == ZXV_PEER_HISTORY)
        p->hist_head = (p->hist_head + 1) % ZXV_PEER_HISTORY;
    else
        p->n_hist++;
    p->hist[i] = *s;
}

/* ---------- digests ---------- */

static void invoice_digest(const uint8_t sid[32], uint64_t req_id, const vna_id_t *seller,
                           const vna_id_t *buyer, const uint8_t *prompt, uint32_t plen,
                           const uint8_t *answer, uint32_t alen, uint64_t price, uint64_t ts,
                           uint8_t out[32])
{
    uint8_t m[sizeof CTX_INVOICE - 1 + 32 + 8 + 32 + 32 + 32 + 32 + 8 + 8], *q = m;
    memcpy(q, CTX_INVOICE, sizeof CTX_INVOICE - 1);
    q += sizeof CTX_INVOICE - 1;
    memcpy(q, sid, 32);
    q += 32;
    put64(q, req_id);
    q += 8;
    memcpy(q, seller->b, 32);
    q += 32;
    memcpy(q, buyer->b, 32);
    q += 32;
    sha3_256(prompt, plen, q);
    q += 32;
    sha3_256(answer, alen, q);
    q += 32;
    put64(q, price);
    q += 8;
    put64(q, ts);
    q += 8;
    sha3_256(m, (size_t) (q - m), out);
}

static void settle_digest(const uint8_t inv[32], uint64_t gross, uint64_t fee,
                          const uint64_t parts[PAY_ASSURE_BUCKETS], uint8_t out[32])
{
    uint8_t m[sizeof CTX_SETTLE - 1 + 32 + 16 + 8 * PAY_ASSURE_BUCKETS], *q = m;
    memcpy(q, CTX_SETTLE, sizeof CTX_SETTLE - 1);
    q += sizeof CTX_SETTLE - 1;
    memcpy(q, inv, 32);
    q += 32;
    put64(q, gross);
    q += 8;
    put64(q, fee);
    q += 8;
    for (uint32_t i = 0; i < PAY_ASSURE_BUCKETS; i++, q += 8) put64(q, parts[i]);
    sha3_256(m, (size_t) (q - m), out);
}

static void ack_digest(const uint8_t settle[32], uint8_t status, uint8_t out[32])
{
    uint8_t m[sizeof CTX_ACK - 1 + 33];
    memcpy(m, CTX_ACK, sizeof CTX_ACK - 1);
    memcpy(m + sizeof CTX_ACK - 1, settle, 32);
    m[sizeof m - 1] = status;
    sha3_256(m, sizeof m, out);
}

static int sign32(const zxv_peer_t *p, const char *ctx, const uint8_t d[32],
                  uint8_t sig[VNA_SIG_LEN])
{
    uint8_t rnd[32];
    if (zxv_os_random(rnd, sizeof rnd) != 0) return -1;
    vna_sign(p->self->sk, ctx, d, 32, rnd, sig);
    wipe(rnd, sizeof rnd);
    return 0;
}

/* ---------- stream I/O (P3) ---------- */

static void set_timeout(psock_t s, uint32_t ms)
{
#if defined(_WIN32)
    DWORD to = ms;
#else
    struct timeval to = {(time_t) (ms / 1000u), (suseconds_t) ((ms % 1000u) * 1000u)};
#endif
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *) &to, sizeof to);
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char *) &to, sizeof to);
}

static int send_all(psock_t s, const uint8_t *b, uint32_t n)
{
    while (n) {
#if defined(_WIN32)
        int k = send(s, (const char *) b, (int) n, 0);
#elif defined(MSG_NOSIGNAL)
        ssize_t k = send(s, b, n, MSG_NOSIGNAL);
#else
        ssize_t k = send(s, b, n, 0);
#endif
        if (k <= 0) return -1;
        b += k;
        n -= (uint32_t) k;
    }
    return 0;
}

static int recv_all(psock_t s, uint8_t *b, uint32_t n)
{
    while (n) {
#if defined(_WIN32)
        int k = recv(s, (char *) b, (int) n, 0);
#else
        ssize_t k = recv(s, b, n, 0);
#endif
        if (k <= 0) return -1;
        b += k;
        n -= (uint32_t) k;
    }
    return 0;
}

static int send_frame(psock_t s, const uint8_t *b, uint32_t n)
{
    uint8_t h[4] = {(uint8_t) (n >> 24), (uint8_t) (n >> 16), (uint8_t) (n >> 8), (uint8_t) n};
    if (!n || n > ZXV_PEER_FRAME_MAX) return -1;
    return send_all(s, h, 4) == 0 && send_all(s, b, n) == 0 ? 0 : -1;
}

/* One frame into p->fbuf; its length, or -1. */
static int32_t recv_frame(zxv_peer_t *p, psock_t s)
{
    uint8_t h[4];
    if (recv_all(s, h, 4) != 0) return -1;
    uint32_t n = ((uint32_t) h[0] << 24) | ((uint32_t) h[1] << 16) | ((uint32_t) h[2] << 8) | h[3];
    if (!n || n > ZXV_PEER_FRAME_MAX) return -1;
    return recv_all(s, p->fbuf, n) == 0 ? (int32_t) n : -1;
}

/* Seal pt as one record of `type` and send it. */
static int send_rec(zxv_peer_t *p, psock_t s, uint8_t type, const uint8_t *pt, uint32_t len)
{
    int32_t n = vna_sess_seal(&p->sess, type, pt, len, p->fbuf, sizeof p->fbuf);
    return n > 0 ? send_frame(s, p->fbuf, (uint32_t) n) : -1;
}

/* Receive one record of `type` into p->pbuf; its length, or -1. */
static int32_t recv_rec(zxv_peer_t *p, psock_t s, uint8_t want)
{
    int32_t n = recv_frame(p, s);
    uint8_t type;
    uint32_t len;
    if (n < 0 ||
        vna_sess_open(&p->sess, p->fbuf, (uint32_t) n, p->pbuf, sizeof p->pbuf, &type, &len) !=
            VNA_OK ||
        type != want)
        return -1;
    return (int32_t) len;
}

static int fail(zxv_peer_result_t *r, int status, const char *why)
{
    r->status = status;
    snprintf(r->error, sizeof r->error, "%s", why);
    return status;
}

/* ---------- lifecycle ---------- */

zxv_peer_t *zxv_peer_new(void)
{
    zxv_peer_t *p = (zxv_peer_t *) calloc(1, sizeof *p);
    if (!p) return NULL;
    p->ls = PBAD_SOCK;
    p->lan_only = true;
    if (book_init(p) != 0) {
        free(p);
        return NULL;
    }
    return p;
}

void zxv_peer_stop(zxv_peer_t *p)
{
    if (!p) return;
    if (p->ls != PBAD_SOCK) PCLOSE(p->ls);
    p->ls = PBAD_SOCK;
    p->port = 0;
}

void zxv_peer_free(zxv_peer_t *p)
{
    if (!p) return;
    zxv_peer_stop(p);
    wipe(p, sizeof *p);
    free(p);
}

void zxv_peer_set_identity(zxv_peer_t *p, const vna_identity_t *self, bool lan_only)
{
    if (!p) return;
    p->self = self;
    p->lan_only = lan_only;
}

void zxv_peer_test_tamper(zxv_peer_t *p, uint32_t flags)
{
    if (p) p->tamper = flags;
}

int zxv_peer_serve(zxv_peer_t *p, const char *bind_ip, uint16_t port, uint64_t price,
                   zxv_peer_answer_fn fn, void *ctx, const char **why)
{
    const char *dummy;
    if (!why) why = &dummy;
    *why = "";
    if (!p || !fn) return (*why = "bad arguments"), -1;
    zxv_peer_stop(p);
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    if (inet_pton(AF_INET, bind_ip && bind_ip[0] ? bind_ip : "0.0.0.0", &sa.sin_addr) != 1)
        return (*why = "bad bind address"), -1;
    psock_t s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == PBAD_SOCK) return (*why = "socket failed"), -1;
    int yes = 1;
#if defined(_WIN32)
    setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char *) &yes, sizeof yes);
#else
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *) &yes, sizeof yes);
#endif
    if (bind(s, (struct sockaddr *) &sa, sizeof sa) != 0 || listen(s, 4) != 0) {
        PCLOSE(s);
        return (*why = "TCP port is busy"), -1;
    }
    plen_t al = sizeof sa;
    if (getsockname(s, (struct sockaddr *) &sa, &al) == 0) port = ntohs(sa.sin_port);
    p->ls = s;
    p->port = port;
    p->price = price;
    p->answer_fn = fn;
    p->answer_ctx = ctx;
    return 0;
}

intptr_t zxv_peer_fd(const zxv_peer_t *p)
{
    return (p && p->ls != PBAD_SOCK) ? (intptr_t) p->ls : (intptr_t) -1;
}

void zxv_peer_on_readable(zxv_peer_t *p, uint64_t now_ms)
{
    if (!p || p->ls == PBAD_SOCK) return;
    struct sockaddr_in sa;
    plen_t al = sizeof sa;
    psock_t c = accept(p->ls, (struct sockaddr *) &sa, &al);
    if (c == PBAD_SOCK) return;
    if (sa.sin_family != AF_INET || !addr_allowed(p, &sa)) { /* P2 */
        p->refused++;
        PCLOSE(c);
        return;
    }
    set_timeout(c, ZXV_PEER_IO_MS);
    (void) zxv_peer_serve_fd(p, (intptr_t) c, now_ms);
    PCLOSE(c);
}

/* ---------- the server's half ---------- */

int zxv_peer_serve_fd(zxv_peer_t *p, intptr_t fd, uint64_t now_ms)
{
    psock_t s = (psock_t) fd;
    if (!p || !p->self || !p->answer_fn) return -1;
    uint8_t rnd[128];
    int ret = -1;
    static char answer[ZXV_PEER_ANSWER_MAX];
    static uint8_t prompt[ZXV_PEER_PROMPT_MAX + 1];

    /* P4: M1 -> M2 -> M3 */
    int32_t n = recv_frame(p, s);
    if (n < 0 || zxv_os_random(rnd, sizeof rnd) != 0) goto out;
    int32_t l2 = vna_hs_respond(&p->hs, p->self, p->self->pow_bits, VNA_FEAT_UBH168, p->fbuf,
                                (uint32_t) n, rnd, p->pbuf, sizeof p->pbuf);
    if (l2 <= 0 || send_frame(s, p->pbuf, (uint32_t) l2) != 0) goto out;
    n = recv_frame(p, s);
    if (n < 0 || vna_hs_responder_finish(&p->hs, p->fbuf, (uint32_t) n, &p->sess) != VNA_OK)
        goto out;
    memcpy(p->peer_pk, p->hs.inner.pk, VNA_PK_LEN);
    vna_hs_clear(&p->hs);
    p->sessions++;

    /* P5: REQ */
    n = recv_rec(p, s, REC_REQ);
    if (n < 16 || (uint32_t) n - 16u > ZXV_PEER_PROMPT_MAX) goto out;
    uint64_t req_id = get64(p->pbuf), max_price = get64(p->pbuf + 8);
    uint32_t plen = (uint32_t) n - 16u;
    memcpy(prompt, p->pbuf + 16, plen);
    prompt[plen] = 0;
    uint8_t status = ANS_OK;
    if (max_price < p->price)
        status = ANS_PRICE;
    else if (!plen || memchr(prompt, 0, plen))
        status = ANS_BAD;
    answer[0] = 0;
    if (status == ANS_OK) p->answer_fn(p->answer_ctx, (const char *) prompt, answer, sizeof answer);
    uint32_t alen = status == ANS_OK ? (uint32_t) strnlen(answer, sizeof answer - 1) : 0;

    /* ANS: req_id, status, price, ts, alen, answer, sig_B(invoice) */
    uint8_t inv[32];
    uint64_t ts = now_ms;
    invoice_digest(p->sess.session_id, req_id, &p->self->id, &p->sess.peer, prompt, plen,
                   (const uint8_t *) answer, alen, p->price, ts, inv);
    uint8_t *q = p->pbuf;
    put64(q, req_id);
    q[8] = status;
    put64(q + 9, p->price);
    put64(q + 17, ts);
    q[25] = (uint8_t) (alen >> 8);
    q[26] = (uint8_t) alen;
    memcpy(q + 27, answer, alen);
    uint32_t len = 27u + alen;
    if (status == ANS_OK) {
        if (sign32(p, CTX_INVOICE, inv, q + len) != 0) goto out;
        if (p->tamper & 4u) q[len + 7] ^= 1u;
        len += VNA_SIG_LEN;
    }
    if (send_rec(p, s, REC_ANS, p->pbuf, len) != 0) goto out;
    if (status != ANS_OK) {
        p->refused++;
        ret = 0;
        goto out;
    }

    /* PAY: req_id, gross, fee, parts[4], sig_A(settle) */
    set_timeout(s, ZXV_PEER_IO_MS);
    n = recv_rec(p, s, REC_PAY);
    if (n != (int32_t) (8 + 16 + 8 * PAY_ASSURE_BUCKETS + VNA_SIG_LEN)) goto out;
    zxv_peer_settlement_t st;
    memset(&st, 0, sizeof st);
    st.role = ZXV_PEER_ROLE_SELLER;
    st.peer = p->sess.peer;
    st.req_id = get64(p->pbuf);
    st.gross = get64(p->pbuf + 8);
    st.fee = get64(p->pbuf + 16);
    for (uint32_t i = 0; i < PAY_ASSURE_BUCKETS; i++) st.parts[i] = get64(p->pbuf + 24 + 8 * i);
    const uint8_t *sig_a = p->pbuf + 24 + 8 * PAY_ASSURE_BUCKETS;
    memcpy(st.invoice, inv, 32);
    memcpy(st.session, p->sess.session_id, 32);
    st.ts = ts;
    settle_digest(inv, st.gross, st.fee, st.parts, st.settle);
    /* P6: the seller's checks */
    uint64_t f0 = pay_assure_fee(st.gross), split[PAY_ASSURE_BUCKETS];
    pay_assure_split(st.fee, split);
    uint8_t ack = ACK_OK;
    if (st.req_id != req_id || st.gross != p->price || st.fee < f0 || st.fee > f0 + 1u ||
        memcmp(split, st.parts, sizeof split) != 0 ||
        !vna_verify(p->peer_pk, CTX_SETTLE, st.settle, 32, sig_a))
        ack = ACK_REFUSED;
    if (ack == ACK_OK && st.gross) {
        pay_posting_req_t req;
        pay_receipt_t rc;
        new_req(p, &req, st.settle, 'S', "peer inference sold", now_ms / 1000u);
        pay_status_t ps =
            pay_ledger_issue(&p->L, &req, p->a_mesh, p->a_wallet, st.gross, &rc); /* P6 */
        if (ps != PAY_OK && ps != PAY_DUPLICATE) ack = ACK_REFUSED;
    }
    uint8_t ad[32];
    ack_digest(st.settle, ack, ad);
    q = p->pbuf;
    put64(q, req_id);
    q[8] = ack;
    if (sign32(p, CTX_ACK, ad, q + 9) != 0) goto out;
    if (send_rec(p, s, REC_ACK, q, 9u + VNA_SIG_LEN) != 0) goto out;
    if (ack == ACK_OK) {
        p->sold++;
        remember(p, &st);
        ret = 0;
    } else {
        p->refused++;
    }
out:
    vna_hs_clear(&p->hs);
    wipe(&p->sess, sizeof p->sess);
    wipe(rnd, sizeof rnd);
    return ret;
}

/* ---------- the asker's half ---------- */

int zxv_peer_ask_fd(zxv_peer_t *p, intptr_t fd, const vna_id_t *peer_id, const char *prompt,
                    uint64_t max_price, uint64_t now_ms, zxv_peer_result_t *r)
{
    psock_t s = (psock_t) fd;
    uint8_t rnd[128];
    if (!r) return ZXV_PEER_E_ARG;
    memset(r, 0, sizeof *r);
    if (!p || !p->self || !peer_id || !prompt) return fail(r, ZXV_PEER_E_ARG, "networking is off");
    size_t plen = strlen(prompt);
    if (!plen || plen > ZXV_PEER_PROMPT_MAX) return fail(r, ZXV_PEER_E_ARG, "prompt length");
    uint64_t need = sat_add(max_price, pay_assure_fee(max_price) + 1u);
    if (wallet_balance(p) < need)
        return fail(r, ZXV_PEER_E_FUNDS, "the peer wallet cannot pay max_price and its fee");

    /* P4 */
    if (zxv_os_random(rnd, sizeof rnd) != 0) return fail(r, ZXV_PEER_E_ARG, "no random source");
    int32_t l1 = vna_hs_initiate(&p->hs, p->self, peer_id, p->self->pow_bits, VNA_FEAT_UBH168, rnd,
                                 p->pbuf, sizeof p->pbuf);
    int32_t n;
    if (l1 <= 0 || send_frame(s, p->pbuf, (uint32_t) l1) != 0 || (n = recv_frame(p, s)) < 0 ||
        zxv_os_random(rnd, 32) != 0) {
        vna_hs_clear(&p->hs);
        return fail(r, ZXV_PEER_E_HANDSHAKE, "handshake: no answer");
    }
    int32_t l3 = vna_hs_initiator_finish(&p->hs, p->fbuf, (uint32_t) n, rnd, p->pbuf,
                                         sizeof p->pbuf, &p->sess);
    if (l3 > 0) memcpy(p->peer_pk, p->hs.m2.pk, VNA_PK_LEN);
    vna_hs_clear(&p->hs);
    wipe(rnd, sizeof rnd);
    if (l3 <= 0)
        return fail(r, ZXV_PEER_E_HANDSHAKE,
                    "handshake refused (wrong NodeID, bad signature or tampered)");
    if (send_frame(s, p->pbuf, (uint32_t) l3) != 0) {
        wipe(&p->sess, sizeof p->sess);
        return fail(r, ZXV_PEER_E_HANDSHAKE, "handshake: send failed");
    }
    p->sessions++;

    /* REQ */
    uint64_t req_id;
    uint8_t rb[8];
    int ret;
    if (zxv_os_random(rb, sizeof rb) != 0) {
        ret = fail(r, ZXV_PEER_E_ARG, "no random source");
        goto out;
    }
    req_id = get64(rb);
    put64(p->pbuf, req_id);
    put64(p->pbuf + 8, max_price);
    memcpy(p->pbuf + 16, prompt, plen);
    if (send_rec(p, s, REC_REQ, p->pbuf, (uint32_t) (16 + plen)) != 0) {
        ret = fail(r, ZXV_PEER_E_PROTO, "request not sent");
        goto out;
    }

    /* ANS */
    set_timeout(s, ZXV_PEER_ANSWER_MS);
    n = recv_rec(p, s, REC_ANS);
    set_timeout(s, ZXV_PEER_IO_MS);
    if (n < 27 || get64(p->pbuf) != req_id) {
        ret = fail(r, ZXV_PEER_E_PROTO, "no valid answer record");
        goto out;
    }
    uint8_t status = p->pbuf[8];
    uint64_t price = get64(p->pbuf + 9), ts = get64(p->pbuf + 17);
    uint32_t alen = ((uint32_t) p->pbuf[25] << 8) | p->pbuf[26];
    if (status == ANS_PRICE || (status == ANS_OK && price > max_price)) {
        ret = fail(r, ZXV_PEER_E_PRICE, "the peer's price is above max_price");
        goto out;
    }
    if (status != ANS_OK || alen >= sizeof r->answer || (uint32_t) n != 27u + alen + VNA_SIG_LEN) {
        ret = fail(r, ZXV_PEER_E_REFUSED, "the peer refused the request");
        goto out;
    }
    zxv_peer_settlement_t *st = &r->s;
    st->role = ZXV_PEER_ROLE_BUYER;
    st->peer = p->sess.peer;
    st->req_id = req_id;
    st->ts = ts;
    memcpy(st->session, p->sess.session_id, 32);
    invoice_digest(p->sess.session_id, req_id, &p->sess.peer, &p->self->id,
                   (const uint8_t *) prompt, (uint32_t) plen, p->pbuf + 27, alen, price, ts,
                   st->invoice);
    if (!vna_verify(p->peer_pk, CTX_INVOICE, st->invoice, 32, p->pbuf + 27 + alen)) {
        ret = fail(r, ZXV_PEER_E_SIG, "the invoice signature did not verify");
        goto out;
    }
    memcpy(r->answer, p->pbuf + 27, alen);
    r->answer[alen] = 0;

    /* P6: post first, then PAY */
    st->gross = price;
    {
        uint64_t rem;
        const pay_account_t *w = pay_ledger_account(&p->L, p->a_wallet);
        if (!w || !pay_assure_fee_carry(w->fee_carry, price, &st->fee, &rem)) {
            ret = fail(r, ZXV_PEER_E_BOOK, "the peer book is inconsistent");
            goto out;
        }
    }
    pay_assure_split(st->fee, st->parts);
    if (p->tamper & 1u) { /* tests: under-pay the fee by one (still a valid split) */
        st->fee = st->fee ? st->fee - 1u : 0;
        pay_assure_split(st->fee, st->parts);
    }
    settle_digest(st->invoice, st->gross, st->fee, st->parts, st->settle);
    if (price) {
        pay_posting_req_t req;
        pay_receipt_t rc;
        uint64_t fee_out = 0;
        new_req(p, &req, st->settle, 'B', "peer inference bought", now_ms / 1000u);
        pay_status_t ps = pay_ledger_pay_with_fee(&p->L, &req, p->a_wallet, p->a_mesh, price,
                                                  p->a_bucket, 0, 0, 0, 0, &rc, &fee_out);
        if (ps != PAY_OK || (!(p->tamper & 1u) && fee_out != st->fee)) {
            ret = fail(r, ZXV_PEER_E_BOOK, "the peer book refused the payment");
            goto out;
        }
        p->fees_paid += fee_out;
    }
    uint8_t *q = p->pbuf;
    put64(q, req_id);
    put64(q + 8, st->gross);
    put64(q + 16, st->fee);
    for (uint32_t i = 0; i < PAY_ASSURE_BUCKETS; i++) put64(q + 24 + 8 * i, st->parts[i]);
    uint8_t *sig = q + 24 + 8 * PAY_ASSURE_BUCKETS;
    if (sign32(p, CTX_SETTLE, st->settle, sig) != 0) {
        ret = fail(r, ZXV_PEER_E_ARG, "no random source");
        goto out;
    }
    if (p->tamper & 2u) sig[11] ^= 1u;
    if (send_rec(p, s, REC_PAY, q, 24u + 8u * PAY_ASSURE_BUCKETS + VNA_SIG_LEN) != 0) {
        ret = fail(r, ZXV_PEER_E_PROTO, "payment not sent");
        goto out;
    }

    /* ACK */
    n = recv_rec(p, s, REC_ACK);
    if (n != (int32_t) (9 + VNA_SIG_LEN) || get64(p->pbuf) != req_id) {
        ret = fail(r, ZXV_PEER_E_PROTO, "no valid acknowledgement");
        goto out;
    }
    uint8_t ad[32];
    ack_digest(st->settle, p->pbuf[8], ad);
    if (!vna_verify(p->peer_pk, CTX_ACK, ad, 32, p->pbuf + 9)) {
        ret = fail(r, ZXV_PEER_E_SIG, "the acknowledgement signature did not verify");
        goto out;
    }
    if (p->pbuf[8] != ACK_OK) {
        /* Our book already paid: the signed refusal and our signed payment are
         * the evidence for a dispute. Nothing is reversed automatically. */
        ret = fail(r, ZXV_PEER_E_REFUSED, "the peer refused the payment (signed refusal kept)");
        goto out;
    }
    p->bought++;
    remember(p, st);
    ret = 0;
out:
    wipe(&p->sess, sizeof p->sess);
    return ret;
}

int zxv_peer_ask(zxv_peer_t *p, const char *ip, uint16_t port, const vna_id_t *peer_id,
                 const char *prompt, uint64_t max_price, uint64_t now_ms, zxv_peer_result_t *r)
{
    if (!r) return ZXV_PEER_E_ARG;
    memset(r, 0, sizeof *r);
    if (!p || !p->self || !ip || !port) return fail(r, ZXV_PEER_E_ARG, "networking is off");
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &sa.sin_addr) != 1) return fail(r, ZXV_PEER_E_ARG, "bad address");
    if (wallet_balance(p) < sat_add(max_price, pay_assure_fee(max_price) + 1u)) /* before any I/O */
        return fail(r, ZXV_PEER_E_FUNDS, "the peer wallet cannot pay max_price and its fee");
    if (!addr_allowed(p, &sa)) return fail(r, ZXV_PEER_E_POLICY, "LAN mode: not a private address");
    psock_t s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == PBAD_SOCK) return fail(r, ZXV_PEER_E_CONNECT, "socket failed");
    /* connect with a deadline: non-blocking connect, then wait for writable */
    bool ok = false;
#if defined(_WIN32)
    u_long nb = 1;
    ioctlsocket(s, FIONBIO, &nb);
#else
    int fl = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, fl | O_NONBLOCK);
#endif
    if (connect(s, (struct sockaddr *) &sa, sizeof sa) == 0) {
        ok = true;
    } else {
        fd_set w;
        FD_ZERO(&w);
        FD_SET(s, &w);
        struct timeval tv = {(long) (ZXV_PEER_IO_MS / 1000u), 0};
        if (select((int) s + 1, NULL, &w, NULL, &tv) == 1) {
            int err = 0;
            plen_t el = sizeof err;
            ok = getsockopt(s, SOL_SOCKET, SO_ERROR, (char *) &err, &el) == 0 && err == 0;
        }
    }
#if defined(_WIN32)
    nb = 0;
    ioctlsocket(s, FIONBIO, &nb);
#else
    fcntl(s, F_SETFL, fl);
#endif
    if (!ok) {
        PCLOSE(s);
        return fail(r, ZXV_PEER_E_CONNECT, "TCP connect failed (is the peer serving?)");
    }
    int one = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *) &one, sizeof one);
    set_timeout(s, ZXV_PEER_IO_MS);
    int rc = zxv_peer_ask_fd(p, (intptr_t) s, peer_id, prompt, max_price, now_ms, r);
    PCLOSE(s);
    return rc;
}

void zxv_peer_status(const zxv_peer_t *p, zxv_peer_status_t *st)
{
    if (!st) return;
    memset(st, 0, sizeof *st);
    if (!p) return;
    st->serving = p->ls != PBAD_SOCK;
    st->port = p->port;
    st->price = p->price;
    st->wallet = wallet_balance(p);
    st->fees_paid = p->fees_paid;
    for (uint32_t i = 0; i < PAY_ASSURE_BUCKETS; i++) {
        const pay_account_t *a = pay_ledger_account(&p->L, p->a_bucket[i]);
        st->buckets[i] = a ? a->debit : 0;
    }
    st->sold = p->sold;
    st->bought = p->bought;
    st->refused = p->refused;
    st->sessions = p->sessions;
    st->book_ok = pay_ledger_check(&p->L) && pay_ledger_verify_chain(&p->L);
    st->n_history = p->n_hist;
    for (uint32_t i = 0; i < p->n_hist; i++)
        st->history[i] = p->hist[(p->hist_head + i) % ZXV_PEER_HISTORY];
}
