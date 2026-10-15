/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_evo_comm.c — host tests for community-maintained updates.
 *
 *  C1  compatibility contract: what forks may and may not change
 *  C2  contract extension: old installs keep talking to new ones
 *  C3  audit attestations signed with ML-DSA-65
 *  C4  the install gate, rule by rule
 *  C5  rollback journal, including a random-operation invariant check
 */
#include <stdio.h>
#include <string.h>

#include "evo_comm.h"
#include "pq_security.h"

static int g_pass, g_fail;
#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (c)                                                                                     \
            g_pass++;                                                                              \
        else {                                                                                     \
            g_fail++;                                                                              \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                  \
        }                                                                                          \
    } while (0)

static uint64_t g_rng = 0x51ed2701cafef00dull;
static uint64_t rnd(void)
{
    g_rng ^= g_rng >> 12;
    g_rng ^= g_rng << 25;
    g_rng ^= g_rng >> 27;
    return g_rng * 0x2545f4914f6cdd1dull;
}

/* ----- keys ----- */
typedef struct {
    uint8_t pk[PQ_MLDSA65_PK_BYTES];
    uint8_t sk[PQ_MLDSA65_SK_BYTES];
    uint8_t id[32];
} key_t_;

static const uint8_t CTX[] = "zxv/evo/community/v1";

static void make_key(key_t_ *k, uint8_t b)
{
    uint8_t seed[32];
    memset(seed, b, 32);
    pq_mldsa65_keygen(seed, k->pk, k->sk);
    evo_key_id(k->pk, PQ_MLDSA65_PK_BYTES, k->id);
}

static bool hook(const uint8_t *pk, uint32_t pk_len, const uint8_t *msg, uint32_t msg_len,
                 const uint8_t *sig, uint32_t sig_len, void *ctx)
{
    (void) ctx;
    if (pk_len != PQ_MLDSA65_PK_BYTES || sig_len != PQ_MLDSA65_SIG_BYTES) return false;
    return pq_mldsa65_verify(pk, msg, msg_len, CTX, sizeof(CTX) - 1, sig);
}

static uint8_t g_sig[PQ_MLDSA65_SIG_BYTES];
static uint8_t g_wire[16384];

static uint32_t seal(const key_t_ *k, const uint8_t *body, uint32_t bl)
{
    pq_mldsa65_sign(k->sk, body, bl, CTX, sizeof(CTX) - 1, 0, g_sig);
    return evo_fork_wire(body, bl, k->pk, PQ_MLDSA65_PK_BYTES, g_sig, PQ_MLDSA65_SIG_BYTES, g_wire,
                         sizeof(g_wire));
}

/* ----- descriptors ----- */
static evo_registry_t g_reg;
static evo_desc_t d_chat, d_ledger, d_presence;
static evo_cid_t c_chat, c_ledger, c_presence;

static void base_descs(void)
{
    evo_reg_init(&g_reg);
    evo_desc_init(&d_chat, "chat.message", EVO_KIND_MESSAGE, EVO_CLASS_GENERAL);
    evo_desc_add(&d_chat, 1, EVO_T_UINT, EVO_FF_REQUIRED, 0, 0);
    evo_desc_add(&d_chat, 2, EVO_T_TEXT, 0, 280, 0);
    evo_desc_init(&d_ledger, "ledger.post", EVO_KIND_MESSAGE, EVO_CLASS_MONEY);
    evo_desc_add(&d_ledger, 1, EVO_T_UINT, EVO_FF_REQUIRED | EVO_FF_MUST_UNDERSTAND, 0, 0);
    evo_desc_add(&d_ledger, 2, EVO_T_UINT, EVO_FF_REQUIRED, 0, 0);
    evo_desc_init(&d_presence, "chiglet.visit", EVO_KIND_MESSAGE, EVO_CLASS_GENERAL);
    evo_desc_add(&d_presence, 1, EVO_T_CID, EVO_FF_REQUIRED, 0, 0);
    evo_reg_add(&g_reg, &d_chat, &c_chat);
    evo_reg_add(&g_reg, &d_ledger, &c_ledger);
    evo_reg_add(&g_reg, &d_presence, &c_presence);
}

static evo_contract_t g_con;

static void caps_with(evo_capset_t *s, const evo_desc_t *chat, bool with_ledger)
{
    evo_capset_init(s);
    if (chat) {
        evo_cid_t c;
        evo_status_t st = evo_reg_add(&g_reg, chat, &c);
        (void) st; /* DUP is fine */
        evo_capset_put(s, chat);
    }
    if (with_ledger) evo_capset_put(s, &d_ledger);
}

/* ===== C1 ===== */
static void c1_contract(void)
{
    printf("C1 compatibility contract\n");
    base_descs();
    CHECK(evo_contract_init(&g_con, 0x40) == EVO_ERR_ARG);
    CHECK(evo_contract_init(&g_con, EVO_CHK_ALL) == EVO_OK);
    CHECK(evo_contract_add(&g_con, &c_ledger) == EVO_OK);
    CHECK(evo_contract_add(&g_con, &c_chat) == EVO_OK);
    CHECK(evo_contract_add(&g_con, &c_chat) == EVO_ERR_DUP);
    CHECK(g_con.n == 2 && evo_cid_cmp(&g_con.desc[0], &g_con.desc[1]) < 0);

    evo_capset_t s;
    uint32_t bad = 99;
    caps_with(&s, &d_chat, true);
    CHECK(evo_contract_check(&g_con, &g_reg, &s, EVO_CHK_ALL, &bad) == EVO_OK);

    /* a fork that adds an optional field of its own keeps the contract */
    evo_desc_t f = d_chat;
    evo_desc_add(&f, evo_ext_tag("mia.sticker"), EVO_T_BYTES, 0, 64, 0);
    caps_with(&s, &f, true);
    CHECK(evo_contract_check(&g_con, &g_reg, &s, EVO_CHK_ALL, &bad) == EVO_OK);

    /* ... a new REQUIRED field breaks every older install: refused */
    f = d_chat;
    evo_desc_add(&f, 9, EVO_T_UINT, EVO_FF_REQUIRED, 0, 0);
    caps_with(&s, &f, true);
    CHECK(evo_contract_check(&g_con, &g_reg, &s, EVO_CHK_ALL, &bad) == EVO_ERR_UNSATISFIED);

    /* ... retyping a contract field: refused */
    evo_desc_init(&f, "chat.message", EVO_KIND_MESSAGE, EVO_CLASS_GENERAL);
    evo_desc_add(&f, 1, EVO_T_UINT, EVO_FF_REQUIRED, 0, 0);
    evo_desc_add(&f, 2, EVO_T_BYTES, 0, 280, 0);
    caps_with(&s, &f, true);
    CHECK(evo_contract_check(&g_con, &g_reg, &s, EVO_CHK_ALL, &bad) == EVO_ERR_UNSATISFIED);

    /* ... dropping an optional contract field: refused (the floor is the floor) */
    evo_desc_init(&f, "chat.message", EVO_KIND_MESSAGE, EVO_CLASS_GENERAL);
    evo_desc_add(&f, 1, EVO_T_UINT, EVO_FF_REQUIRED, 0, 0);
    caps_with(&s, &f, true);
    CHECK(evo_contract_check(&g_con, &g_reg, &s, EVO_CHK_ALL, &bad) == EVO_ERR_UNSATISFIED);

    /* ... dropping a whole family: refused, and bad names it */
    caps_with(&s, &d_chat, false);
    CHECK(evo_contract_check(&g_con, &g_reg, &s, EVO_CHK_ALL, &bad) == EVO_ERR_UNSATISFIED);
    CHECK(evo_cid_eq(&g_con.desc[bad], &c_ledger));

    /* ... failing one money-path conformance check: refused */
    caps_with(&s, &d_chat, true);
    CHECK(evo_contract_check(&g_con, &g_reg, &s, EVO_CHK_ALL & ~EVO_CHK_USURY, &bad) ==
          EVO_ERR_NONCONFORMANT);

    /* a contract descriptor nobody has fetched yet */
    evo_contract_t c2 = g_con;
    evo_cid_t ghost;
    evo_cid_of((const uint8_t *) "ghost", 5, &ghost);
    evo_contract_add(&c2, &ghost);
    CHECK(evo_contract_check(&c2, &g_reg, &s, EVO_CHK_ALL, &bad) == EVO_ERR_NOT_FOUND);

    /* encoding: round trip, CID stable, non-canonical refused */
    uint8_t buf[EVO_CONTRACT_ENC_MAX + 1];
    uint32_t n = evo_contract_encode(&g_con, buf, sizeof(buf));
    evo_contract_t back;
    CHECK(n == 4 + 4 + 1 + 2 * EVO_CID_LEN);
    CHECK(evo_contract_decode(buf, n, &back) == EVO_OK && back.n == 2 &&
          back.conform_req == EVO_CHK_ALL && evo_cid_eq(&back.desc[1], &g_con.desc[1]));
    evo_cid_t a, b;
    evo_contract_cid(&g_con, &a);
    evo_contract_cid(&back, &b);
    CHECK(evo_cid_eq(&a, &b));
    CHECK(evo_contract_decode(buf, n - 1, &back) == EVO_ERR_PARSE);
    buf[n] = 0;
    CHECK(evo_contract_decode(buf, n + 1, &back) == EVO_ERR_PARSE);
    uint8_t sw[EVO_CONTRACT_ENC_MAX];
    memcpy(sw, buf, n);
    memcpy(sw + 9, buf + 9 + EVO_CID_LEN, EVO_CID_LEN); /* swap the two CIDs */
    memcpy(sw + 9 + EVO_CID_LEN, buf + 9, EVO_CID_LEN);
    CHECK(evo_contract_decode(sw, n, &back) == EVO_ERR_PARSE);
    memcpy(sw, buf, n);
    sw[4] |= 0x80; /* unknown conformance bit */
    CHECK(evo_contract_decode(sw, n, &back) == EVO_ERR_PARSE);
    for (uint32_t i = 0; i < 2000; i++) { /* decoder fuzz: never crashes, never half-writes */
        memcpy(sw, buf, n);
        sw[rnd() % n] ^= (uint8_t) (1u << (rnd() % 8));
        evo_contract_t t;
        memset(&t, 0xAB, sizeof(t));
        evo_status_t st = evo_contract_decode(sw, n, &t);
        if (st != EVO_OK) CHECK(t.n == 0xAB);
    }
}

/* ===== C2 ===== */
static void c2_extends(void)
{
    printf("C2 contract extension\n");
    evo_contract_t nw = g_con;
    CHECK(evo_contract_extends(&nw, &g_con, &g_reg));
    CHECK(evo_contract_add(&nw, &c_presence) == EVO_OK);
    CHECK(evo_contract_extends(&nw, &g_con, &g_reg));  /* new floor adds a family */
    CHECK(!evo_contract_extends(&g_con, &nw, &g_reg)); /* old misses it */

    /* replacing chat with a richer optional-field chat: extends */
    evo_desc_t rich = d_chat;
    evo_desc_add(&rich, 3, EVO_T_CID, 0, 0, 0);
    evo_cid_t c_rich;
    evo_reg_add(&g_reg, &rich, &c_rich);
    evo_contract_t r;
    evo_contract_init(&r, EVO_CHK_ALL);
    evo_contract_add(&r, &c_rich);
    evo_contract_add(&r, &c_ledger);
    CHECK(evo_contract_extends(&r, &g_con, &g_reg));

    /* a new required field would strand old installs: does not extend */
    evo_desc_t req = d_chat;
    evo_desc_add(&req, 4, EVO_T_UINT, EVO_FF_REQUIRED, 0, 0);
    evo_cid_t c_req;
    evo_reg_add(&g_reg, &req, &c_req);
    evo_contract_init(&r, EVO_CHK_ALL);
    evo_contract_add(&r, &c_req);
    evo_contract_add(&r, &c_ledger);
    CHECK(!evo_contract_extends(&r, &g_con, &g_reg));

    /* lowering conformance does not extend */
    evo_contract_init(&r, EVO_CHK_ALL & ~EVO_CHK_FEE);
    evo_contract_add(&r, &c_chat);
    evo_contract_add(&r, &c_ledger);
    CHECK(!evo_contract_extends(&r, &g_con, &g_reg));
    CHECK(evo_contract_extends(&g_con, &r, &g_reg));
}

/* ===== C3 ===== */
static key_t_ k_root, k_fixer, k_a, k_b, k_c, k_d;
static evo_cid_t g_ccid;

static uint32_t audit_wire(const key_t_ *k, const evo_cid_t *rec, const evo_cid_t *con,
                           uint8_t verdict, uint32_t conform, const char *bounty)
{
    evo_audit_t a;
    memset(&a, 0, sizeof(a));
    a.record = *rec;
    a.contract = *con;
    memcpy(a.auditor, k->id, 32);
    a.verdict = verdict;
    a.conform = conform;
    a.stamp = 20261015;
    if (bounty) {
        a.bounty_len = (uint8_t) strlen(bounty);
        memcpy(a.bounty, bounty, a.bounty_len);
    }
    uint8_t body[EVO_AUDIT_BODY_MAX];
    uint32_t bl = evo_audit_encode(&a, body, sizeof(body));
    if (!bl) return 0;
    return seal(k, body, bl);
}

static void c3_audits(void)
{
    printf("C3 audit attestations\n");
    make_key(&k_root, 1);
    make_key(&k_fixer, 2);
    make_key(&k_a, 3);
    make_key(&k_b, 4);
    make_key(&k_c, 5);
    make_key(&k_d, 6);
    evo_contract_cid(&g_con, &g_ccid);
    evo_cid_t rec;
    evo_cid_of((const uint8_t *) "rec", 3, &rec);

    uint32_t n = audit_wire(&k_a, &rec, &g_ccid, EVO_AUDIT_PASS, EVO_CHK_ALL, "bug-42");
    CHECK(n > 0);
    evo_audit_t a;
    CHECK(evo_audit_open(g_wire, n, hook, 0, &a) == EVO_OK);
    CHECK(evo_cid_eq(&a.record, &rec) && a.verdict == EVO_AUDIT_PASS && a.bounty_len == 6 &&
          memcmp(a.bounty, "bug-42", 6) == 0 && memcmp(a.auditor, k_a.id, 32) == 0);

    /* tampering with the body (the verdict byte) breaks the signature */
    uint32_t vpos = 4 + 2 * EVO_CID_LEN + 32; /* magic, two CIDs, auditor */
    /* the wire starts with the u32 body length */
    g_wire[4 + vpos] = EVO_AUDIT_FAIL;
    CHECK(evo_audit_open(g_wire, n, hook, 0, &a) == EVO_ERR_BAD_SIG);
    g_wire[4 + vpos] = EVO_AUDIT_PASS;
    CHECK(evo_audit_open(g_wire, n, hook, 0, &a) == EVO_OK);
    CHECK(evo_audit_open(g_wire, n - 1, hook, 0, &a) == EVO_ERR_PARSE);

    /* signed by B but claiming to be A: the key id does not match */
    evo_audit_t forged;
    memset(&forged, 0, sizeof(forged));
    forged.record = rec;
    forged.contract = g_ccid;
    memcpy(forged.auditor, k_a.id, 32);
    forged.verdict = EVO_AUDIT_PASS;
    forged.conform = EVO_CHK_ALL;
    uint8_t body[EVO_AUDIT_BODY_MAX];
    uint32_t bl = evo_audit_encode(&forged, body, sizeof(body));
    n = seal(&k_b, body, bl);
    CHECK(evo_audit_open(g_wire, n, hook, 0, &a) == EVO_ERR_BAD_SIG);

    /* non-canonical bodies */
    forged.verdict = 3;
    CHECK(evo_audit_encode(&forged, body, sizeof(body)) == 0);
    forged.verdict = EVO_AUDIT_PASS;
    bl = evo_audit_encode(&forged, body, sizeof(body));
    body[vpos] = 3;
    CHECK(evo_audit_decode(body, bl, &a) == EVO_ERR_PARSE);
    body[vpos] = EVO_AUDIT_PASS;
    body[vpos + 1] |= 0x40;
    CHECK(evo_audit_decode(body, bl, &a) == EVO_ERR_PARSE);
    body[vpos + 1] &= (uint8_t) ~0x40;
    CHECK(evo_audit_decode(body, bl, &a) == EVO_OK);
    evo_cid_t z;
    memset(&z, 0, sizeof(z));
    forged.contract = z;
    CHECK(evo_audit_encode(&forged, body, sizeof(body)) == 0);
}

/* ===== C4 ===== */
static evo_dag_t g_dag;
static evo_cid_t r_root, r_fix, r_other;

static evo_status_t add_record(const key_t_ *k, const evo_cid_t *parent, const char *slot,
                               const char *val, evo_cid_t *cid)
{
    evo_fork_t f;
    evo_fork_init(&f, k->id, "community", 1);
    if (parent) evo_fork_parent(&f, parent);
    evo_cid_t v;
    evo_cid_of((const uint8_t *) val, (uint32_t) strlen(val), &v);
    evo_fork_change(&f, EVO_CH_ADD, slot, &v, 0);
    uint8_t body[EVO_LIN_BODY_MAX];
    uint32_t bl = evo_fork_encode(&f, body, sizeof(body));
    evo_cid_of(body, bl, cid);
    uint32_t n = seal(k, body, bl);
    uint32_t idx;
    return evo_dag_add_wire(&g_dag, g_wire, n, hook, 0, &idx);
}

static evo_audit_t g_aud[16];
static uint32_t g_naud;

static void push_audit(const key_t_ *k, const evo_cid_t *rec, const evo_cid_t *con, uint8_t verdict,
                       uint32_t conform, const char *bounty)
{
    uint32_t n = audit_wire(k, rec, con, verdict, conform, bounty);
    CHECK(evo_audit_open(g_wire, n, hook, 0, &g_aud[g_naud]) == EVO_OK);
    g_naud++;
}

static void c4_gate(void)
{
    printf("C4 install gate\n");
    evo_dag_init(&g_dag);
    CHECK(add_record(&k_root, 0, "ui", "ui-v-root", &r_root) == EVO_OK);
    CHECK(add_record(&k_fixer, &r_root, "fix", "fix-1", &r_fix) == EVO_OK);
    CHECK(add_record(&k_fixer, &r_root, "fix2", "fix-2", &r_other) == EVO_OK);

    evo_audit_policy_t pol;
    CHECK(evo_audit_policy_init(&pol, 0) == EVO_OK && pol.k == 2);
    CHECK(evo_audit_policy_add(&pol, k_a.id) == EVO_OK);
    CHECK(evo_audit_policy_add(&pol, k_b.id) == EVO_OK);
    CHECK(evo_audit_policy_add(&pol, k_c.id) == EVO_OK);
    CHECK(evo_audit_policy_add(&pol, k_fixer.id) == EVO_OK); /* listed, but authored it */
    CHECK(evo_audit_policy_add(&pol, k_a.id) == EVO_ERR_DUP);

    evo_capset_t good, broken;
    caps_with(&good, &d_chat, true);
    caps_with(&broken, &d_chat, false);
    evo_install_consent_t yes = {r_fix, true, 1000};
    evo_gate_result_t res;
    const uint64_t now = 500;

#define GATE(caps, cons, tr)                                                                       \
    evo_community_gate(&g_dag, &r_fix, tr, &g_con, &g_reg, caps, EVO_CHK_ALL, g_aud, g_naud, &pol, \
                       cons, now, &res)

    evo_cid_t ghost;
    evo_cid_of((const uint8_t *) "nope", 4, &ghost);
    CHECK(evo_community_gate(&g_dag, &ghost, 0, &g_con, &g_reg, &good, EVO_CHK_ALL, 0, 0, &pol,
                             &yes, now, &res) == EVO_OK &&
          res.verdict == EVO_GATE_UNKNOWN_RECORD);

    g_naud = 0;
    CHECK(GATE(&good, &yes, 0) == EVO_OK && res.verdict == EVO_GATE_NEED_AUDITS);
    push_audit(&k_a, &r_fix, &g_ccid, EVO_AUDIT_PASS, EVO_CHK_ALL, "bug-42");
    push_audit(&k_a, &r_fix, &g_ccid, EVO_AUDIT_PASS, EVO_CHK_ALL, 0); /* same auditor twice */
    GATE(&good, &yes, 0);
    CHECK(res.verdict == EVO_GATE_NEED_AUDITS && res.passes == 1);
    push_audit(&k_fixer, &r_fix, &g_ccid, EVO_AUDIT_PASS, EVO_CHK_ALL, 0); /* self-audit */
    push_audit(&k_d, &r_fix, &g_ccid, EVO_AUDIT_PASS, EVO_CHK_ALL, 0);     /* not chosen */
    push_audit(&k_b, &r_other, &g_ccid, EVO_AUDIT_PASS, EVO_CHK_ALL, 0);   /* other record */
    evo_cid_t other_con;
    evo_cid_of((const uint8_t *) "older contract", 14, &other_con);
    push_audit(&k_b, &r_fix, &other_con, EVO_AUDIT_PASS, EVO_CHK_ALL, 0); /* other contract */
    push_audit(&k_b, &r_fix, &g_ccid, EVO_AUDIT_PASS, EVO_CHK_ALL & ~EVO_CHK_LEDGER, 0);
    GATE(&good, &yes, 0);
    CHECK(res.verdict == EVO_GATE_NEED_AUDITS && res.passes == 1 && !res.bounty_ready);

    push_audit(&k_b, &r_fix, &g_ccid, EVO_AUDIT_PASS, EVO_CHK_ALL, 0);
    GATE(&good, 0, 0);
    CHECK(res.verdict == EVO_GATE_NEED_CONSENT && res.passes == 2);
    CHECK(res.bounty_ready && res.bounty_len == 6 && memcmp(res.bounty, "bug-42", 6) == 0);
    evo_install_consent_t wrong = {r_other, true, 1000};
    GATE(&good, &wrong, 0);
    CHECK(res.verdict == EVO_GATE_NEED_CONSENT);
    evo_install_consent_t late = {r_fix, true, now};
    GATE(&good, &late, 0);
    CHECK(res.verdict == EVO_GATE_NEED_CONSENT);
    evo_install_consent_t no = {r_fix, false, 1000};
    GATE(&good, &no, 0);
    CHECK(res.verdict == EVO_GATE_NEED_CONSENT);
    GATE(&good, &yes, 0);
    CHECK(res.verdict == EVO_GATE_INSTALLABLE);
    CHECK(strcmp(evo_gate_str(res.verdict), "installable") == 0);

    /* the build breaks the contract: refused before audits are looked at */
    GATE(&broken, &yes, 0);
    CHECK(res.verdict == EVO_GATE_INCOMPATIBLE);
    CHECK(evo_community_gate(&g_dag, &r_fix, 0, &g_con, &g_reg, &good, EVO_CHK_HASH, g_aud, g_naud,
                             &pol, &yes, now, &res) == EVO_OK &&
          res.verdict == EVO_GATE_INCOMPATIBLE);

    /* user blocks: the author, or an ancestor record */
    evo_trust_t tr;
    evo_trust_init(&tr, 0, 0);
    tr.min_stars = 3;
    GATE(&good, &yes, &tr);
    CHECK(res.verdict == EVO_GATE_INSTALLABLE); /* unrated never blocks */
    evo_trust_rate_key(&tr, k_fixer.id, 4);
    GATE(&good, &yes, &tr);
    CHECK(res.verdict == EVO_GATE_INSTALLABLE);
    evo_trust_rate_key(&tr, k_fixer.id, 1);
    GATE(&good, &yes, &tr);
    CHECK(res.verdict == EVO_GATE_BLOCKED);
    evo_trust_init(&tr, 0, 0);
    tr.min_stars = 3;
    evo_trust_rate_record(&tr, &r_root, 2);
    GATE(&good, &yes, &tr);
    CHECK(res.verdict == EVO_GATE_BLOCKED);
    evo_trust_rate_record(&tr, &r_root, 5);
    GATE(&good, &yes, &tr);
    CHECK(res.verdict == EVO_GATE_INSTALLABLE);

    /* one listed auditor failing it vetoes, whatever else passed */
    push_audit(&k_c, &r_fix, &other_con, EVO_AUDIT_FAIL, 0, 0);
    GATE(&good, &yes, 0);
    CHECK(res.verdict == EVO_GATE_AUDIT_FAILED && res.fails == 1 && !res.bounty_ready);

    /* k = 3 needs three */
    g_naud = 0;
    push_audit(&k_a, &r_fix, &g_ccid, EVO_AUDIT_PASS, EVO_CHK_ALL, 0);
    push_audit(&k_b, &r_fix, &g_ccid, EVO_AUDIT_PASS, EVO_CHK_ALL, 0);
    pol.k = 3;
    GATE(&good, &yes, 0);
    CHECK(res.verdict == EVO_GATE_NEED_AUDITS);
    push_audit(&k_c, &r_fix, &g_ccid, EVO_AUDIT_PASS, EVO_CHK_ALL, 0);
    GATE(&good, &yes, 0);
    CHECK(res.verdict == EVO_GATE_INSTALLABLE && res.passes == 3 && !res.bounty_ready);

    CHECK(evo_community_gate(0, &r_fix, 0, &g_con, &g_reg, &good, 0, 0, 0, &pol, 0, 0, &res) ==
          EVO_ERR_ARG);
    pol.k = 0;
    CHECK(GATE(&good, &yes, 0) == EVO_ERR_ARG);
#undef GATE
}

/* ===== C5 ===== */
static void c5_journal(void)
{
    printf("C5 rollback journal\n");
    evo_cid_t base, r[24];
    evo_cid_of((const uint8_t *) "base", 4, &base);
    for (uint32_t i = 0; i < 24; i++) {
        uint8_t b[2] = {'r', (uint8_t) i};
        evo_cid_of(b, 2, &r[i]);
    }
    evo_journal_t j;
    evo_journal_init(&j, &base, 100);
    CHECK(evo_cid_eq(evo_journal_current(&j), &base));
    CHECK(evo_journal_rollback(&j) == EVO_ERR_NOT_FOUND);
    CHECK(evo_journal_confirm(&j, true) == EVO_ERR_NOT_FOUND);

    CHECK(evo_journal_install(&j, &r[0], 10) == EVO_OK && evo_journal_in_trial(&j));
    CHECK(evo_cid_eq(evo_journal_current(&j), &r[0]));
    CHECK(evo_journal_install(&j, &r[1], 11) == EVO_ERR_CONFLICT);
    CHECK(evo_journal_confirm(&j, true) == EVO_OK && !evo_journal_in_trial(&j));

    CHECK(evo_journal_install(&j, &r[1], 20) == EVO_OK);
    CHECK(evo_journal_confirm(&j, false) == EVO_OK); /* health check failed */
    CHECK(evo_cid_eq(evo_journal_current(&j), &r[0]));

    CHECK(evo_journal_install(&j, &r[2], 30) == EVO_OK);
    CHECK(!evo_journal_tick(&j, 129));
    CHECK(evo_journal_tick(&j, 130)); /* nobody confirmed: back it goes */
    CHECK(evo_cid_eq(evo_journal_current(&j), &r[0]));
    CHECK(!evo_journal_tick(&j, 1000));

    CHECK(evo_journal_rollback(&j) == EVO_OK); /* user's own choice */
    CHECK(evo_cid_eq(evo_journal_current(&j), &base));

    /* fill past capacity: the running build is never dropped */
    evo_journal_init(&j, &base, 100);
    for (uint32_t i = 0; i < 20; i++) {
        CHECK(evo_journal_install(&j, &r[i], i) == EVO_OK);
        CHECK(evo_journal_confirm(&j, true) == EVO_OK);
    }
    CHECK(j.n == EVO_JOURNAL_MAX && evo_cid_eq(evo_journal_current(&j), &r[19]));
    CHECK(evo_journal_rollback(&j) == EVO_OK && evo_cid_eq(evo_journal_current(&j), &r[18]));

    /* random operations: one trial at most, always the newest entry; the
     * running build is the base or a KEPT/TRIAL entry; never rolled back */
    evo_journal_init(&j, &base, 50);
    uint64_t t = 0;
    bool ok = true;
    for (uint32_t step = 0; step < 20000; step++) {
        t += rnd() % 30;
        switch (rnd() % 4) {
        case 0:
            evo_journal_install(&j, &r[rnd() % 24], t);
            break;
        case 1:
            evo_journal_confirm(&j, rnd() & 1);
            break;
        case 2:
            if (rnd() % 4 == 0) evo_journal_rollback(&j);
            break;
        default:
            evo_journal_tick(&j, t);
            break;
        }
        uint32_t trials = 0;
        for (uint32_t i = 0; i < j.n; i++) trials += j.e[i].state == EVO_J_TRIAL;
        if (trials > 1 || j.n > EVO_JOURNAL_MAX || j.cur >= (int32_t) j.n) ok = false;
        if (trials && !(j.cur == (int32_t) j.n - 1 && j.e[j.cur].state == EVO_J_TRIAL)) ok = false;
        if (j.cur >= 0 && j.e[j.cur].state == EVO_J_ROLLED_BACK) ok = false;
    }
    CHECK(ok);
}

int main(void)
{
    c1_contract();
    c2_extends();
    c3_audits();
    c4_gate();
    c5_journal();
    printf("evo_comm: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
