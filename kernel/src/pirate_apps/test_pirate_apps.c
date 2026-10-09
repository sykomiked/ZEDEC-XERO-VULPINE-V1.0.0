/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_pirate_apps.c — the pirate deck is a thin, honest UI. Prove it:
 *   (1) a dark module => UNAVAILABLE + the module's NOT-AVAILABLE code verbatim
 *   (2) a delegate returns EXACTLY what the module returned (code equality)
 *   (3) a serving module => AVAILABLE
 *   (4) the app holds NO capability (source grep: no crypto/ledger reimpl)
 *   (5) an app never fabricates: an errored module surfaces the error
 * TEST_HOST only: this test may use libc; the module .c may not. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "pirate_apps.h"
#include "triple_ledger.h"
#include "surplus.h"

#ifndef PIRATE_SRC
#define PIRATE_SRC "src/pirate_apps/pirate_apps.c"
#endif

static int checks = 0, fails = 0;
#define CHECK(c, msg) do { checks++; if (!(c)) { fails++; \
    printf("  [FAIL] %s\n", msg); } else { printf("  [ok]   %s\n", msg); } } while (0)

/* ---- a real in-memory ipfs transport: serves ONE stored blob by its CID ---- */
typedef struct { uint8_t cid[IPFS_CID_LEN]; const uint8_t *bytes; uint32_t len; } blob_t;
static int mem_fetch(const uint8_t cid[IPFS_CID_LEN], uint8_t *buf, uint32_t cap,
                     uint32_t *out_len, void *ctx) {
    const blob_t *b = (const blob_t *)ctx;
    if (memcmp(cid, b->cid, IPFS_CID_LEN) != 0) return -1;   /* not found */
    if (b->len > cap) return -1;
    memcpy(buf, b->bytes, b->len);
    *out_len = b->len;
    return 0;
}

/* ---- a trivial broker verify hook (accepts any sig): binds the ops boundary
 *      so the Counter can serve. NOT a real verifier — a test double. ---- */
static bool yes_verify(const uint8_t *msg, uint32_t len,
                       const uint8_t sig[64], const uint8_t pk[32]) {
    (void)msg; (void)len; (void)sig; (void)pk; return true;
}

int main(void) {
    printf("=== pirate_apps: the whole deck — Hold, Counter, Cellar, Spyglass, Club ===\n");

    /* ================= (1) THE HOLD, DARK: no transport bound ================ */
    static ipfs_node_t dark_node;
    ipfs_node_init(&dark_node);                 /* no transport => fail closed */
    app_ctx_t hold;
    CHECK(app_hold_open(&hold, &dark_node) == 0, "Hold opens on a transportless node");
    CHECK(app_status(&hold) == APP_STATUS_UNAVAILABLE,
          "(1) dark Hold reports UNAVAILABLE");

    uint8_t cid[IPFS_CID_LEN];
    memset(cid, 0xAB, sizeof cid);
    uint8_t buf[64];
    memset(buf, 0x7E, sizeof buf);              /* sentinel: must stay untouched */
    uint32_t got = 12345u;
    ipfs_result_t r = app_hold_fetch(&hold, cid, buf, sizeof buf, &got);
    CHECK(r == IPFS_ERR_NO_TRANSPORT,
          "(1) app_hold_fetch returns the module's NO_TRANSPORT verbatim");
    CHECK(buf[0] == 0x7E, "(1) buffer NOT fabricated — content untouched");

    /* ================= (3)+(5) THE HOLD, LIT: a real transport ============== */
    static const uint8_t payload[] = "smuggled cargo, addressed by its own hash";
    static blob_t blob;
    blob.bytes = payload; blob.len = (uint32_t)sizeof(payload);
    CHECK(ipfs_cid_from_bytes(payload, blob.len, blob.cid) == 0, "compute a real CID");
    static ipfs_transport_t xport;
    xport.fetch = mem_fetch; xport.ctx = &blob;

    static ipfs_node_t lit_node;
    ipfs_node_init(&lit_node);
    ipfs_set_transport(&lit_node, &xport);
    app_ctx_t hold2;
    app_hold_open(&hold2, &lit_node);
    CHECK(app_status(&hold2) == APP_STATUS_AVAILABLE, "(3) lit Hold reports AVAILABLE");

    uint8_t out[128]; uint32_t olen = 0;
    r = app_hold_fetch(&hold2, blob.cid, out, sizeof out, &olen);
    CHECK(r == IPFS_OK && olen == blob.len && memcmp(out, payload, olen) == 0,
          "(3) app_hold_fetch delegates a REAL self-certifying get");
    /* (5): ask for a CID the transport does not serve -> honest error, no invent */
    uint8_t missing[IPFS_CID_LEN]; memset(missing, 0x11, sizeof missing);
    r = app_hold_fetch(&hold2, missing, out, sizeof out, &olen);
    CHECK(r == IPFS_ERR_NOT_FOUND, "(5) an absent CID surfaces NOT_FOUND, not a forgery");

    /* ===================== (2) THE CELLAR: verbatim swap ==================== */
    static triple_ledger_t tl;
    static vino_stores_t vs;
    triple_ledger_init(&tl);
    vino_stores_init(&vs, &tl);
    uint8_t pcid[VINO_PROOF_CID_LEN]; memset(pcid, 0x5A, sizeof pcid);
    uint64_t vid = 0;
    int32_t mrc = vino_mint(&vs, &vid, SR_FROM_INT(89), SECOND_MINTS_OIL,
                            VINO_ACTIVE_DIGITAL, pcid);
    CHECK(mrc == VINO_OK && vid == 1, "Cellar: a real Vino voucher is minted");

    app_ctx_t cellar;
    app_cellar_open(&cellar, &vs);
    CHECK(app_status(&cellar) == APP_STATUS_AVAILABLE, "(3) Cellar with a ledger is AVAILABLE");

    /* verbatim equality on SUCCESS: the app forwards to vino_swap_to_physical.
     * (idempotent: swapping to physical twice both return VINO_OK) */
    int32_t direct_ok = vino_swap_to_physical(&vs, vid);
    int32_t viaapp_ok = app_cellar_swap(&cellar, vid);
    CHECK(direct_ok == VINO_OK && viaapp_ok == direct_ok,
          "(2) app_cellar_swap == vino_swap_to_physical (success code, verbatim)");

    /* verbatim equality on ERROR: an unknown voucher id */
    int32_t direct_nf = vino_swap_to_physical(&vs, 99999u);
    int32_t viaapp_nf = app_cellar_swap(&cellar, 99999u);
    CHECK(direct_nf == VINO_ERR_NOT_FOUND && viaapp_nf == direct_nf,
          "(2) app_cellar_swap == vino_swap_to_physical (NOT_FOUND, verbatim)");

    /* ============ (3)+(5) THE COUNTER: verify boundary + no settlement ====== */
    static broker_t br;
    broker_init(&br);
    app_ctx_t counter_dark;
    app_counter_open(&counter_dark, &br);        /* no verify hook yet */
    CHECK(app_status(&counter_dark) == APP_STATUS_UNAVAILABLE,
          "(1) Counter with no verify hook is UNAVAILABLE");

    broker_set_verifier(&br, yes_verify);
    uint8_t author[BROKER_KEY_LEN]; memset(author, 0x42, sizeof author);
    broker_trust_author(&br, author);
    uint8_t lcid[BROKER_CID_LEN]; memset(lcid, 0xC1, sizeof lcid);
    uint8_t sig[BROKER_SIG_LEN];  memset(sig, 0x00, sizeof sig);
    aipic_contract_t terms; memset(&terms, 0, sizeof terms);
    terms.function = 1; terms.in_use = true; strcpy(terms.scope, "*");
    int32_t li = broker_list(&br, lcid, author, sig, &terms);
    CHECK(li >= 0, "Counter: a listing is posted (verify boundary bound)");

    app_ctx_t counter;
    app_counter_open(&counter, &br);
    CHECK(app_status(&counter) == APP_STATUS_AVAILABLE, "(3) Counter with a verifier is AVAILABLE");
    CHECK(app_counter_count(&counter) == 1u, "Counter surfaces the real listing count");

    /* (5): settlement backend is DARK -> the sale is never simulated */
    uint8_t buyer[BROKER_KEY_LEN]; memset(buyer, 0x24, sizeof buyer);
    broker_result_t sr_direct = broker_settle(&br, li, buyer, 100u);
    broker_result_t sr_app    = app_counter_settle(&counter, li, buyer, 100u);
    CHECK(sr_direct == BROKER_ERR_NO_SETTLEMENT && sr_app == sr_direct,
          "(5) app_counter_settle surfaces NO_SETTLEMENT verbatim — no fabricated fill");

    /* ===================== (3) THE SPYGLASS: init boundary ================== */
    app_ctx_t spy_dark;
    static fm_book_t book_uninit;                /* zeroed, inited == false */
    memset(&book_uninit, 0, sizeof book_uninit);
    app_spyglass_open(&spy_dark, &book_uninit);
    CHECK(app_status(&spy_dark) == APP_STATUS_UNAVAILABLE,
          "(1) uninitialised Spyglass is UNAVAILABLE");

    static fm_book_t book;
    fm_book_init(&book);
    app_ctx_t spy;
    app_spyglass_open(&spy, &book);
    CHECK(app_status(&spy) == APP_STATUS_AVAILABLE, "(3) initialised Spyglass is AVAILABLE");
    /* delegate: fm_pnl_report with no position -> the module's own honest code */
    fm_pnl_t pnl; memset(&pnl, 0, sizeof pnl);
    int32_t p_direct = fm_pnl_report(&book, 7u, &pnl);
    int32_t p_app    = app_spyglass_pnl(&spy, 7u, &pnl);
    CHECK(p_app == p_direct, "(2) app_spyglass_pnl == fm_pnl_report (verbatim)");

    /* ======================= (3) THE CLUB: a room to gather ================= */
    static soc_world_t world;
    soc_init(&world);
    app_ctx_t club_empty;
    app_club_open(&club_empty, &world);
    CHECK(app_status(&club_empty) == APP_STATUS_UNAVAILABLE,
          "(1) an empty Club (no rooms) is UNAVAILABLE");

    int32_t sp = soc_open_space(&world, SOC_SOCIAL_CLUB, "Don Tovani's Social Club");
    CHECK(sp >= 0, "Club: Don Tovani's room opens");
    app_ctx_t club;
    app_club_open(&club, &world);
    CHECK(app_status(&club) == APP_STATUS_AVAILABLE, "(3) a Club with a room is AVAILABLE");
    CHECK(app_club_spaces(&club) == 1u, "Club surfaces the real room count");
    /* delegate: soc_post verbatim (join first; membership is the only gate) */
    CHECK(soc_join(&world, sp, 501u) == SOC_OK, "a person joins the room");
    const uint8_t hello[] = "ahoy";
    int32_t post_direct_next = soc_post_count(&world, sp); /* index the app post will get */
    int32_t post_rc = app_club_say(&club, sp, 501u, hello, (uint32_t)sizeof hello - 1u);
    CHECK(post_rc == post_direct_next,
          "(2) app_club_say delegates to soc_post (returns the real post index)");

    /* ============ wrong-kind / unbound delegates fail closed (never OK) ===== */
    CHECK(app_hold_fetch(&cellar, cid, buf, sizeof buf, &got) == IPFS_ERR_NO_TRANSPORT,
          "wrong-kind Hold fetch fails closed");
    CHECK(app_cellar_swap(&hold2, vid) == VINO_ERR_NULL, "wrong-kind Cellar swap fails closed");
    app_ctx_t unbound; memset(&unbound, 0, sizeof unbound);
    unbound.kind = APP_NONE;
    CHECK(app_status(&unbound) == APP_STATUS_UNBOUND, "an unopened handle is UNBOUND");

    /* the render buffer is a LOGICAL view, NUL-terminated, non-empty */
    CHECK(app_render(&club)[0] != '\0' && app_render_len(&club) > 0u,
          "app_render produces a logical view (ops boundary: pixels are the display's)");

    /* ============ (4) SOURCE GREP: the app holds NO capability ============== */
    {
        FILE *f = fopen(PIRATE_SRC, "rb");
        CHECK(f != NULL, "(4) can open pirate_apps.c for the capability grep");
        if (f) {
            static char src[65536];
            size_t n = fread(src, 1, sizeof src - 1, f);
            src[n] = '\0';
            fclose(f);
            /* No re-implemented capability may appear in the app source. It may
             * only CALL the modules. These tokens would each be a reimpl smell. */
            const char *forbidden[] = {
                "sha256", "ed25519", "vino_ledger_act", "triple_ledger_post",
                "hmac_sha256", "hkdf_", "aes256", "keccak", NULL
            };
            int clean = 1;
            for (int i = 0; forbidden[i]; i++) {
                if (strstr(src, forbidden[i])) {
                    printf("    !! found forbidden token: %s\n", forbidden[i]);
                    clean = 0;
                }
            }
            CHECK(clean, "(4) pirate_apps.c reimplements NO settlement/ledger/crypto");
        }
    }

    printf("\n%d checks, %d failures\n", checks, fails);
    if (fails == 0) printf("[PASS] test_pirate_apps\n");
    return fails ? 1 : 0;
}
