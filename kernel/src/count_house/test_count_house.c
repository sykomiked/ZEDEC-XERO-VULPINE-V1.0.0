/* test_count_house.c — Functional correctness tests for the Count House
 * & Stash Buckets fractal-reserve valuation engine.
 *
 * Verifies the documented anti-Sybil/trust-weighting behavior from
 * count_house.h by independently recomputing expected values from the
 * documented V_local formula, not by asserting against the
 * implementation's own output.
 */
#include "count_house.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "../robin_debanks/crypto_verify.h"

static int feq(double a, double b, double eps)
{
    double diff = fabs(a - b);
    double scale = fabs(a) > fabs(b) ? fabs(a) : fabs(b);
    if (scale < 1.0) scale = 1.0;
    return diff <= eps * scale;
}

static word168_t make_peer(uint8_t seed)
{
    word168_t w;
    for (int i = 0; i < WORD168_OCTETS; i++) w.bytes[i] = (uint8_t) (seed + i);
    return w;
}

static const uint8_t ZERO_SIG[CH_PROOF_SIG_LEN] = {0};
static const uint8_t PUBKEY_A[CH_PUBKEY_LEN] = {0xAA};

/* Compute a valid HMAC-SHA256 count_house signature for test buckets */
/* TEST VERIFIER.
 * ch_default_verify_sig performs real Ed25519 verification against the
 * embedded COUNT_HOUSE public key, whose private key is held offline and is
 * deliberately not compiled in — so a host test cannot mint a valid
 * signature. The API anticipates exactly this and exposes an injectable
 * verify_sig hook; we install one that checks the same HMAC construction the
 * test signs with, which lets these cases exercise the DEPOSIT/TRUST/BALANCE
 * logic without weakening the production Ed25519 path.
 * (This test previously signed HMAC and called the default verifier; when the
 * implementation was upgraded HMAC -> Ed25519 the test was left behind and
 * silently failed, because it was never wired into verify-all.) */
static bool test_verify_hmac(const stash_bucket_t *bucket)
{
    if (!bucket) return false;
    uint8_t msg[21 + 8];
    uint32_t pos = 0;
    for (uint32_t i = 0; i < 21; i++) msg[pos++] = bucket->peer_node_id.bytes[i];
    for (uint32_t i = 0; i < 8; i++) msg[pos++] = (uint8_t) (bucket->token_balance >> (i * 8));
    uint8_t expect[32];
    crypto_hmac_sha256(msg, pos, CRYPTO_AUTHORITY_KEY_COUNT_HOUSE, expect);
    for (uint32_t i = 0; i < 32; i++)
        if (expect[i] != bucket->proof_sig[i]) return false;
    return true;
}

static void compute_ch_sig(const word168_t *peer_id, uint64_t balance,
                           uint8_t sig[CH_PROOF_SIG_LEN])
{
    uint8_t msg[21 + 8];
    uint32_t pos = 0;
    for (uint32_t i = 0; i < 21; i++) msg[pos++] = peer_id->bytes[i];
    for (uint32_t i = 0; i < 8; i++) msg[pos++] = (uint8_t) (balance >> (i * 8));
    uint8_t hmac_out[32];
    crypto_hmac_sha256(msg, pos, CRYPTO_AUTHORITY_KEY_COUNT_HOUSE, hmac_out);
    for (uint32_t i = 0; i < 32; i++) sig[i] = hmac_out[i];
    for (uint32_t i = 32; i < CH_PROOF_SIG_LEN; i++) sig[i] = 0;
}

int main(void)
{
    /* ===== init defaults / ZPD path (zero supply) ===== */
    count_house_t ch;
    count_house_init(&ch, 3, "TestHouse");
    assert(ch.device_id == 3);
    assert(strcmp(ch.name, "TestHouse") == 0);
    assert(ch.num_buckets == 0);
    assert(ch.total_supply_minted == 0);
    assert(ch.verify_sig == 0);
    assert(feq(ch.v_local, 0.0, 1e-9));          /* total_reserves = 0 */
    assert(feq(ch.collateral_ratio, 1.0, 1e-9)); /* ZPD: fully-backed by convention */
    assert(!ch.irq_hyperinflation_detected);
    assert(!ch.irq_priority_dropped);

    /* ===== ch_default_verify_sig ===== */
    {
        stash_bucket_t b;
        memset(&b, 0, sizeof(b));
        assert(!ch_default_verify_sig(&b)); /* all-zero sig rejected */
        assert(!ch_default_verify_sig(0));  /* NULL-safe */
        b.proof_sig[10] = 1;
        assert(!ch_default_verify_sig(&b)); /* a junk signature is REJECTED by Ed25519 */
    }

    /* From here on, install the test verifier so the deposit/trust/balance
     * logic can be exercised without a private Ed25519 key. */
    ch.verify_sig = test_verify_hmac;

    /* ===== deposit: new peer, valid signature ===== */
    word168_t peerA = make_peer(1);
    {
        uint8_t valid_sig_a[CH_PROOF_SIG_LEN];
        compute_ch_sig(&peerA, 1000, valid_sig_a);
        int32_t idx = count_house_deposit(&ch, &peerA, PUBKEY_A, 1000, valid_sig_a);
        assert(idx == 0);
        assert(ch.num_buckets == 1);
        assert(ch.buckets[0].token_balance == 1000);
        assert(ch.buckets[0].sig_verified); /* HMAC verified */
        assert(ch.buckets[0].peer_trust_weight == CH_TRUST_INITIAL + CH_TRUST_INCREMENT);
        assert(memcmp(ch.buckets[0].peer_pubkey, PUBKEY_A, CH_PUBKEY_LEN) == 0);
    }

    /* ===== deposit: same peer, top-up, valid signature ===== */
    {
        uint8_t sig_topup[CH_PROOF_SIG_LEN];
        compute_ch_sig(&peerA, 1500, sig_topup);
        int32_t idx = count_house_deposit(&ch, &peerA, PUBKEY_A, 500, sig_topup);
        assert(idx == 0); /* same bucket, not a new one */
        assert(ch.num_buckets == 1);
        assert(ch.buckets[0].token_balance == 1500);
        assert(ch.buckets[0].peer_trust_weight == CH_TRUST_INITIAL + 2 * CH_TRUST_INCREMENT);
    }

    /* ===== deposit: existing peer, failed signature -> rollback + penalty ===== */
    {
        uint32_t trust_before = ch.buckets[0].peer_trust_weight;
        int32_t idx = count_house_deposit(&ch, &peerA, PUBKEY_A, 999, ZERO_SIG);
        assert(idx == -2);
        assert(ch.buckets[0].token_balance == 1500); /* unchanged -- rolled back */
        assert(!ch.buckets[0].sig_verified);
        uint32_t expected_trust =
            (trust_before > CH_TRUST_PENALTY) ? trust_before - CH_TRUST_PENALTY : 0;
        assert(ch.buckets[0].peer_trust_weight == expected_trust);
    }

    /* ===== deposit: brand-new peer, failed signature -> bucket still
     * committed (anti-Sybil: penalty persists), balance stays 0 ===== */
    word168_t peerB = make_peer(50);
    {
        uint32_t buckets_before = ch.num_buckets;
        int32_t idx = count_house_deposit(&ch, &peerB, PUBKEY_A, 777, ZERO_SIG);
        assert(idx == -2);
        assert(ch.num_buckets == buckets_before + 1); /* committed despite failure */
        int32_t found = count_house_find_bucket(&ch, &peerB);
        assert(found >= 0);
        assert(ch.buckets[found].token_balance == 0);
        assert(!ch.buckets[found].sig_verified);
        assert(ch.buckets[found].peer_trust_weight == 0); /* 100 - 200 floors at 0 */

        /* Retrying the SAME peer now benefits from a valid signature,
         * but starts from the penalized trust, not a fresh 100 --
         * proving the penalty truly persisted rather than being
         * silently discarded on the failed first attempt. */
        uint8_t sig_retry[CH_PROOF_SIG_LEN];
        compute_ch_sig(&peerB, 100, sig_retry);
        int32_t idx2 = count_house_deposit(&ch, &peerB, PUBKEY_A, 100, sig_retry);
        assert(idx2 == found);
        assert(ch.buckets[found].token_balance == 100);
        assert(ch.buckets[found].peer_trust_weight == 0 + CH_TRUST_INCREMENT);
    }

    /* ===== count_house_find_bucket ===== */
    {
        word168_t unknown = make_peer(200);
        assert(count_house_find_bucket(&ch, &unknown) == -1);
        assert(count_house_find_bucket(&ch, &peerA) == 0);
    }

    /* ===== capacity limit ===== */
    {
        count_house_t cap;
        count_house_init(&cap, 9, "CapTest");
        cap.verify_sig = test_verify_hmac;
        for (uint32_t i = 0; i < CH_MAX_STASH_BUCKETS; i++) {
            word168_t p = make_peer((uint8_t) (i + 1));
            uint8_t sigcap[CH_PROOF_SIG_LEN];
            compute_ch_sig(&p, 10, sigcap);
            int32_t idx = count_house_deposit(&cap, &p, PUBKEY_A, 10, sigcap);
            assert(idx == (int32_t) i);
        }
        word168_t overflow = make_peer(250);
        uint8_t sigov[CH_PROOF_SIG_LEN];
        compute_ch_sig(&overflow, 10, sigov);
        assert(count_house_deposit(&cap, &overflow, PUBKEY_A, 10, sigov) == -1);
        assert(cap.num_buckets == CH_MAX_STASH_BUCKETS);
    }

    /* ===== valuation formula (independently recomputed) ===== */
    {
        count_house_t v;
        count_house_init(&v, 1, "ValTest");
        v.verify_sig = test_verify_hmac;
        word168_t p1 = make_peer(10), p2 = make_peer(20);
        uint8_t sigv1[CH_PROOF_SIG_LEN];
        compute_ch_sig(&p1, 1000, sigv1);
        uint8_t sigv2[CH_PROOF_SIG_LEN];
        compute_ch_sig(&p2, 2000, sigv2);
        count_house_deposit(&v, &p1, PUBKEY_A, 1000, sigv1); /* trust -> 150 */
        count_house_deposit(&v, &p2, PUBKEY_A, 2000, sigv2); /* trust -> 150 */
        count_house_set_crypto_reserves(&v, SR_FROM_FLOAT(500.0));

        double expected_weighted =
            1000.0 * (150.0 / (double) CH_TRUST_MAX) + 2000.0 * (150.0 / (double) CH_TRUST_MAX);
        double expected_reserves = expected_weighted + 500.0;

        v.total_supply_minted = 10000;
        surplus_real_t vloc = count_house_valuation(&v);
        double expected_vlocal = expected_reserves / 10000.0;
        assert(feq(vloc, expected_vlocal, 1e-9));
        assert(feq(v.collateral_ratio, expected_vlocal, 1e-9));

        /* An unverified bucket must NOT contribute to valuation */
        word168_t p3 = make_peer(30);
        count_house_deposit(&v, &p3, PUBKEY_A, 999999, ZERO_SIG); /* rejected, balance 0 anyway */
        surplus_real_t vloc2 = count_house_valuation(&v);
        assert(feq(vloc2, expected_vlocal, 1e-9)); /* unchanged */
    }

    /* ===== IRQ thresholds ===== */
    {
        count_house_t t;
        count_house_init(&t, 2, "IrqTest");
        t.verify_sig = test_verify_hmac;
        word168_t p = make_peer(5);
        uint8_t sigt[CH_PROOF_SIG_LEN];
        compute_ch_sig(&p, 1000, sigt);
        count_house_deposit(&t, &p, PUBKEY_A, 1000, sigt); /* trust 150 -> weighted 150 */

        /* collateral_ratio = 150/1000 = 0.15 -- above both thresholds */
        t.total_supply_minted = 1000;
        count_house_valuation(&t);
        assert(!t.irq_hyperinflation_detected);
        assert(!t.irq_priority_dropped);

        /* collateral_ratio = 150/2000 = 0.075 -- below priority (0.10), above hyperinflation
         * (0.001) */
        t.total_supply_minted = 2000;
        count_house_valuation(&t);
        assert(!t.irq_hyperinflation_detected);
        assert(t.irq_priority_dropped);

        /* collateral_ratio = 150/1000000 = 0.00015 -- below hyperinflation (0.001) */
        t.total_supply_minted = 1000000;
        count_house_valuation(&t);
        assert(t.irq_hyperinflation_detected);
        assert(t.irq_priority_dropped);
    }

    /* ===== mint: success + anti-hyperinflation refusal ===== */
    {
        count_house_t m;
        count_house_init(&m, 4, "MintTest");
        m.verify_sig = test_verify_hmac;
        word168_t p = make_peer(7);
        uint8_t sigm[CH_PROOF_SIG_LEN];
        compute_ch_sig(&p, 1000, sigm);
        count_house_deposit(&m, &p, PUBKEY_A, 1000, sigm); /* weighted = 150 */

        assert(count_house_mint(&m, 0) == 0); /* no-op */

        /* First mint: supply 0 -> 100. candidate_ratio = 150/100 = 1.5 >= 0.001: allowed */
        assert(count_house_mint(&m, 100) == 100);
        assert(m.total_supply_minted == 100);

        /* Mint an enormous amount that would push collateral far below
         * the hyperinflation floor: candidate_ratio = 150/100000100 ~ 1.5e-6 < 0.001 */
        uint64_t before = m.total_supply_minted;
        assert(count_house_mint(&m, 100000000) == 0); /* refused */
        assert(m.total_supply_minted == before);      /* unchanged */

        /* A mint that would wrap the 64-bit supply counter is refused. It used
         * to wrap the supply to 0, pass the gate and return the huge amount. */
        assert(count_house_mint(&m, UINT64_MAX - 99) == 0);
        assert(m.total_supply_minted == before);
    }

    /* ===== audit ===== */
    {
        count_house_t a;
        count_house_init(&a, 5, "AuditTest");
        a.verify_sig = test_verify_hmac;
        assert(count_house_audit(&a)); /* zero supply, ZPD path, not flagged */

        word168_t p = make_peer(9);
        uint8_t siga[CH_PROOF_SIG_LEN];
        compute_ch_sig(&p, 10, siga);
        count_house_deposit(&a, &p, PUBKEY_A, 10, siga); /* weighted = 1.5 */
        a.total_supply_minted = 1000000;                 /* ratio ~1.5e-6, flagged */
        assert(!count_house_audit(&a));
    }

    /* ===== a top-up whose new balance would not fit in 64 bits is refused
     * (-3) and changes nothing (it used to wrap; found by fuzz_econ_count_house) */
    {
        count_house_t w;
        count_house_init(&w, 6, "WrapTest");
        w.verify_sig = test_verify_hmac;
        word168_t p = make_peer(11);
        uint8_t s1[CH_PROOF_SIG_LEN], s2[CH_PROOF_SIG_LEN];
        compute_ch_sig(&p, UINT64_MAX - 5, s1);
        int32_t idx = count_house_deposit(&w, &p, PUBKEY_A, UINT64_MAX - 5, s1);
        assert(idx >= 0 && w.buckets[idx].token_balance == UINT64_MAX - 5);
        compute_ch_sig(&p, 10, s2);
        assert(count_house_deposit(&w, &p, PUBKEY_A, 10, s2) == -3);
        assert(w.buckets[idx].token_balance == UINT64_MAX - 5);
    }

    printf("All Count House tests passed\n");
    return 0;
}
