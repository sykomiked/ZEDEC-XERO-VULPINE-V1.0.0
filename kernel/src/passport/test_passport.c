/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_passport.c -- log in from any device: seed, merge, capsule, café. */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "pp_int.h"
#include "../mlkem/keccak.h"
#include "../tls/aead.h"

static int pass_n, fail_n;
#define CHECK(c, m)                                                                                \
    do {                                                                                           \
        if (c)                                                                                     \
            pass_n++;                                                                              \
        else {                                                                                     \
            fail_n++;                                                                              \
            printf("FAIL: %s (line %d)\n", m, __LINE__);                                           \
        }                                                                                          \
    } while (0)

/* ---- deterministic host ---- */
static uint32_t rng_state = 168;
static void rnd(void *ctx, uint8_t *o, uint32_t n)
{
    (void) ctx;
    for (uint32_t i = 0; i < n; i++) {
        rng_state = rng_state * 1103515245u + 12345u;
        o[i] = (uint8_t) (rng_state >> 16);
    }
}

/* in-memory swarm: blobs announced under topics */
#define SW_MAX 8
static struct {
    uint8_t topic[32];
    uint8_t blob[16384];
    uint32_t len;
} swarm[SW_MAX];
static int n_swarm;

static int sw_publish(void *ctx, const uint8_t lookup[32], const ipfsn_cid_t *cid,
                      const uint8_t *blob, uint32_t len)
{
    (void) ctx;
    if (n_swarm >= SW_MAX || len > sizeof swarm[0].blob || ipfsn_cid_verify(cid, blob, len) != 0)
        return -1;
    memcpy(swarm[n_swarm].topic, lookup, 32);
    memcpy(swarm[n_swarm].blob, blob, len);
    swarm[n_swarm].len = len;
    n_swarm++;
    return 0;
}

static int sw_fetch(void *ctx, const uint8_t lookup[32], uint32_t index, uint8_t *buf, uint32_t cap,
                    uint32_t *len)
{
    (void) ctx;
    uint32_t k = 0;
    for (int i = 0; i < n_swarm; i++) {
        if (memcmp(swarm[i].topic, lookup, 32) != 0) continue;
        if (k++ != index) continue;
        if (swarm[i].len > cap) return -1;
        memcpy(buf, swarm[i].blob, swarm[i].len);
        *len = swarm[i].len;
        return 0;
    }
    return -1;
}

static id_host_t host = {0, rnd, 0, 0, sw_publish, sw_fetch};

/* ---- storage for states ---- */
#define CAP 32
typedef struct {
    pp_earn_t earn[CAP];
    uint8_t earn_st[CAP];
    pp_spend_t spend[CAP];
    uint8_t spend_st[CAP];
    uint32_t order[CAP];
    pp_grant_rec_t grant[CAP];
    uint64_t used[CAP];
    pp_revoke_t revoke[CAP];
    id_cred_ref_t dev[CAP];
    pp_state_t st;
} box_t;

static void box_init(box_t *b, const uint8_t did[32], uint32_t cap)
{
    memset(b, 0, sizeof *b);
    pp_store_t s = {b->earn,  b->earn_st, cap, b->spend,  b->spend_st, b->order, cap,
                    b->grant, b->used,    cap, b->revoke, cap,         b->dev,   cap};
    pp_state_init(&b->st, did, &s);
}

static pp_earn_t earn(uint8_t tag, uint64_t amount)
{
    pp_earn_t e;
    memset(&e, 0, sizeof e);
    memset(e.receipt, tag, 32);
    memset(e.issuer, 0xE0, 32);
    e.amount = amount;
    e.t_ms = 1000u + tag;
    return e;
}

static int32_t spend_index(const pp_state_t *st, const pp_spend_t *sp)
{
    for (uint32_t i = 0; i < st->n_spend; i++)
        if (memcmp(&st->s.spend[i], sp, sizeof *sp) == 0) return (int32_t) i;
    return -1;
}

static uint8_t state_of(const pp_state_t *st, const pp_spend_t *sp)
{
    int32_t i = spend_index(st, sp);
    return i < 0 ? 0 : st->s.spend_st[i];
}

static bool contains(const uint8_t *h, uint32_t n, const uint8_t *nd, uint32_t m)
{
    for (uint32_t i = 0; i + m <= n; i++)
        if (memcmp(h + i, nd, m) == 0) return true;
    return false;
}

static uint8_t work[65536], work2[65536], blob[65536];
static uint64_t argon_mem[64 * 128]; /* 64 blocks of 1 KiB */

static void digest(const pp_state_t *st, uint8_t out[32])
{
    pp_state_digest(st, work2, sizeof work2, out);
}

static void hex(const char *h, uint8_t *out, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        unsigned v;
        sscanf(h + 2 * i, "%2x", &v);
        out[i] = (uint8_t) v;
    }
}

/* Known answers computed independently with Python's hashlib (SHAKE256 and
 * SHA3-256 over the documented inputs), so a change to any label, length
 * prefix, byte order or base32 table shows up here. */
static void test_kat(void)
{
    uint8_t seed[32], want[32], got[32];
    for (int i = 0; i < 32; i++) seed[i] = (uint8_t) (i * 37 + 5);
    char txt[PP_SEED_TEXT];
    pp_seed_to_text(seed, txt);
    CHECK(strcmp(txt, "0MN4Y-X4SQV-HGGBA-JEYEC-3SGB6-1AQN7-Y4X47-36P3X-MB3YR-49PBE-08XYG") == 0,
          "seed text known answer");
    pp_passport_t k;
    CHECK(pp_hydrate(&k, PQM_LEVEL_STANDARD, seed) == PP_OK, "hydrate the known seed");
    hex("3d4f0aefc19d647425acacefb41f9bec45a727bd2f4850b821a18dd18dcbc396", want, 32);
    CHECK(memcmp(k.vault_key, want, 32) == 0, "vault key known answer");
    hex("a2e3fabe9d6b8ed9f1ebcb7f162db13c4a1eb14d4cd41b899f3e3e9bfaf269ad", want, 32);
    CHECK(memcmp(k.rendezvous, want, 32) == 0, "rendezvous key known answer");
    hex("e1edb6bbcebc05f55aefaf86ac50f8b27013d3ffb95b7ff0c567017b5470c130", want, 32);
    CHECK(pp_topic(&k, 20000, got) == PP_OK && memcmp(got, want, 32) == 0, "topic known answer");
    hex("ef0560cc669ea659abd9b60b2d7442c2b2046b5c3ff8d623171c88bc02f109ea", want, 32);
    id_kdf_params_t kp = {64, 1, 1};
    const uint8_t pw[] = "golden fox 168 at the vineyard";
    uint8_t a[32], b[32];
    CHECK(pp_seed_from_passphrase(&kp, pw, sizeof pw - 1, (const uint8_t *) "michael", 7, argon_mem,
                                  64, a) == PP_OK &&
              id_argon2id(&kp, pw, sizeof pw - 1, want, 32, 0, 0, 0, 0, argon_mem, 64, b, 32) ==
                  ID_OK &&
              memcmp(a, b, 32) == 0,
          "passphrase seed = Argon2id with the known salt");
    pp_wipe(&k);

    /* the shared hash helper: length-prefixed label, then the parts */
    uint8_t in[600], h1[32], h2[32];
    memset(in, 7, sizeof in);
    in[0] = 3;
    memcpy(in + 1, "abc", 3);
    shake256(in, 1 + 3 + 5 + 6 + 7, h1, 32);
    CHECK(pp_shake(h2, 32, "abc", in + 4, 5, in + 9, 6, in + 15, 7) && memcmp(h1, h2, 32) == 0,
          "pp_shake = SHAKE256(len || label || a || b || c)");
    sha3_256(in, 1 + 3 + 5, h1);
    CHECK(pp_sha3(h2, "abc", in + 4, 5, 0, 9, 0, 9) && memcmp(h1, h2, 32) == 0,
          "pp_sha3 skips NULL parts");
    shake256(in, 1 + 3 + 6, h1, 32);
    CHECK(pp_shake(h2, 32, "abc", 0, 9, in + 4, 6, 0, 0) && memcmp(h1, h2, 32) == 0,
          "a NULL first part is skipped");
    shake256(in, 1 + 3 + 7, h1, 32);
    CHECK(pp_shake(h2, 32, "abc", 0, 0, 0, 0, in + 4, 7) && memcmp(h1, h2, 32) == 0,
          "only the third part");
    memset(h2, 0x5A, 32);
    CHECK(!pp_shake(h2, 32, "abc", in, 300, in, 200, in, 9) && !pp_sha3(h2, "abc", in, 509, 0, 0, 0, 0),
          "inputs over PP_HASH_IN are refused");
    CHECK(pp_shake(h2, 32, "abc", in, 300, in, 200, in, 8) && pp_sha3(h2, "abc", in, 508, 0, 0, 0, 0),
          "exactly PP_HASH_IN is accepted");

    /* byte helpers */
    uint8_t x[8] = {1, 2, 3, 4, 5, 6, 7, 8}, y[8] = {1, 2, 3, 4, 5, 6, 7, 9};
    CHECK(!pp_ct_eq(x, y, 8) && pp_ct_eq(x, y, 7), "pp_ct_eq looks at every byte");
    CHECK(pp_mcmp(x, y, 8) == -1 && pp_mcmp(y, x, 8) == 1 && pp_mcmp(x, y, 7) == 0, "pp_mcmp order");
    uint8_t z[4] = {0, 0, 0, 1};
    CHECK(!pp_is_zero(z, 4) && pp_is_zero(z, 3), "pp_is_zero looks at every byte");
    uint8_t g[6] = {9, 9, 9, 9, 9, 9};
    pp_mset(g + 1, 0, 4);
    CHECK(g[0] == 9 && g[1] == 0 && g[4] == 0 && g[5] == 9, "pp_mset writes exactly n bytes");
    pp_mcpy(g + 1, x, 4);
    CHECK(g[0] == 9 && g[1] == 1 && g[4] == 4 && g[5] == 9, "pp_mcpy copies exactly n bytes");
    pp_wipe_mem(g + 1, 4);
    CHECK(g[0] == 9 && g[1] == 0 && g[4] == 0 && g[5] == 9, "pp_wipe_mem wipes exactly n bytes");
    CHECK(pp_strlen("abcdef", 4) == 4 && pp_strlen("ab", 4) == 2, "pp_strlen stops at max or NUL");
    uint8_t w[8];
    pp_put64(w, 0x0102030405060708ull);
    CHECK(w[0] == 1 && w[7] == 8 && pp_get64(w) == 0x0102030405060708ull &&
              pp_get32(w + 4) == 0x05060708u,
          "big-endian put and get");
    uint8_t fp[32] = {0x12, 0x34, 0x56, 0x78};
    CHECK(pp_compare_code(fp) == 419896u && pp_compare_code(0) == 0, "compare code known answer");

    /* the voucher bytes */
    pp_spend_t sp;
    memset(&sp, 0, sizeof sp);
    memset(sp.signer, 0xA1, 16);
    sp.seq = 2;
    sp.lamport = 3;
    sp.amount = 0x0102;
    sp.t_ms = 5;
    memset(sp.payee, 0xB2, 32);
    memset(sp.grant, 0xC3, 16);
    uint8_t e[PP_SPEND_BYTES], back_e[PP_SPEND_BYTES];
    pp_spend_encode(&sp, e);
    CHECK(e[0] == 0xA1 && e[15] == 0xA1 && e[23] == 2 && e[31] == 3 && e[38] == 1 && e[39] == 2 &&
              e[47] == 5 && e[48] == 0xB2 && e[79] == 0xB2 && e[80] == 0xC3 && e[95] == 0xC3,
          "spend layout: signer, seq, lamport, amount, time, payee, grant");
    pp_spend_t sp2;
    pp_spend_decode(e, &sp2);
    pp_spend_encode(&sp2, back_e);
    CHECK(memcmp(e, back_e, sizeof e) == 0 && memcmp(&sp, &sp2, sizeof sp) == 0, "decode inverts encode");
    pp_earn_t er = earn(5, 0x0A0B);
    uint8_t ee[PP_EARN_BYTES];
    pp_earn_enc(&er, ee);
    CHECK(ee[0] == 5 && ee[31] == 5 && ee[32] == 0xE0 && ee[70] == 0x0A && ee[71] == 0x0B &&
              ee[79] == (uint8_t) (1000u + 5),
          "earn layout: receipt, issuer, amount, time");
    pp_grant_rec_t gr = {{0}, {0}, {0}, 0x01020304u, 0x0506, 7, 8};
    memset(gr.grant, 1, 16);
    memset(gr.session, 2, 16);
    memset(gr.device, 3, 16);
    uint8_t ge[PP_GRANT_BYTES];
    pp_grant_rec_enc(&gr, ge);
    CHECK(ge[15] == 1 && ge[16] == 2 && ge[47] == 3 && ge[48] == 1 && ge[51] == 4 && ge[58] == 5 &&
              ge[59] == 6 && ge[67] == 7 && ge[75] == 8,
          "grant record layout");
}

/* ---- seed ---- */
static void test_seed(void)
{
    uint8_t seed[32], back[32];
    char txt[PP_SEED_TEXT];
    for (int i = 0; i < 32; i++) seed[i] = (uint8_t) (i * 37 + 5);
    CHECK(pp_seed_to_text(seed, txt) == PP_OK, "seed to text");
    CHECK(strlen(txt) == 65 && txt[5] == '-' && txt[59] == '-' && txt[60] != '-',
          "55 characters in 11 groups of five");
    CHECK(pp_seed_from_text(txt, (uint32_t) strlen(txt), back) == PP_OK &&
              memcmp(seed, back, 32) == 0,
          "text back to the same seed");
    char low[PP_SEED_TEXT];
    for (int i = 0; i < 66; i++)
        low[i] = (char) ((txt[i] >= 'A' && txt[i] <= 'Z') ? txt[i] + 32 : txt[i]);
    for (int i = 0; i < 65; i++) {
        if (low[i] == '0') low[i] = 'o';
        if (low[i] == '1') low[i] = 'l';
    }
    memset(back, 0, 32);
    CHECK(pp_seed_from_text(low, 65, back) == PP_OK && memcmp(seed, back, 32) == 0,
          "lower case, o for 0 and l for 1 read the same");
    char typo[PP_SEED_TEXT];
    memcpy(typo, txt, sizeof typo);
    typo[2] = typo[2] == 'A' ? 'B' : 'A';
    CHECK(pp_seed_from_text(typo, 65, back) == PP_ERR_CHECKSUM, "one wrong character is caught");
    CHECK(pp_seed_from_text(txt, 64, back) == PP_ERR_FORMAT, "a missing character is refused");
    char pad[PP_SEED_TEXT];
    memcpy(pad, txt, sizeof pad);
    pad[64] = (char) (pad[64] == 'Z' ? 'Y' : 'Z'); /* the padding bits become nonzero */
    CHECK(pp_seed_from_text(pad, 65, back) == PP_ERR_FORMAT, "nonzero padding bits are refused");
    CHECK(pp_seed_from_text("UUUUU", 5, back) == PP_ERR_FORMAT, "U is not a seed character");
    char longer[80];
    snprintf(longer, sizeof longer, "%s0", txt);
    CHECK(pp_seed_from_text(longer, (uint32_t) strlen(longer), back) == PP_ERR_FORMAT,
          "an extra character is refused");

    id_kdf_params_t v1;
    pp_kdf_v1(&v1);
    CHECK(v1.m_kib == 65536u && v1.t == 3u && v1.p == 1u, "version-1 stretch is 64 MiB, 3 passes");
    id_kdf_params_t kp = {64, 1, 1};
    const uint8_t pw[] = "golden fox 168 at the vineyard";
    const uint8_t h1[] = "michael", h2[] = "ana";
    uint8_t a[32], b[32], c[32];
    CHECK(pp_seed_from_passphrase(&kp, pw, 15, h1, 7, argon_mem, 64, a) == PP_ERR_WEAK,
          "a passphrase under 16 bytes is refused");
    CHECK(pp_seed_from_passphrase(&kp, pw, 16, h1, 7, argon_mem, 64, a) == PP_OK,
          "16 bytes is enough");
    CHECK(pp_seed_from_passphrase(&kp, pw, sizeof pw - 1, h1, 0, argon_mem, 64, a) == PP_ERR_ARG,
          "a handle is needed");
    CHECK(pp_seed_from_passphrase(&kp, pw, sizeof pw - 1, h1, 7, argon_mem, 64, a) == PP_OK &&
              pp_seed_from_passphrase(&kp, pw, sizeof pw - 1, h1, 7, argon_mem, 64, b) == PP_OK &&
              memcmp(a, b, 32) == 0,
          "the same passphrase and handle give the same seed on any device");
    CHECK(pp_seed_from_passphrase(&kp, pw, sizeof pw - 1, h2, 3, argon_mem, 64, c) == PP_OK &&
              memcmp(a, c, 32) != 0,
          "another handle gives another seed");
    id_kdf_params_t big = {128, 1, 1};
    CHECK(pp_seed_from_passphrase(&big, pw, sizeof pw - 1, h1, 7, argon_mem, 64, c) == PP_ERR_KDF,
          "too little memory for the stretch is an error");
}

/* ---- identity ---- */
static pp_passport_t pa, pb, px;

static void test_identity(const uint8_t seed[32], const uint8_t other[32])
{
    CHECK(pp_hydrate(&pa, PQM_LEVEL_STANDARD, seed) == PP_OK, "hydrate on the phone");
    CHECK(pp_hydrate(&pb, PQM_LEVEL_STANDARD, seed) == PP_OK, "hydrate on the laptop");
    CHECK(memcmp(pa.root.did, pb.root.did, 32) == 0 &&
              memcmp(pa.vault_key, pb.vault_key, 32) == 0 &&
              memcmp(pa.rendezvous, pb.rendezvous, 32) == 0,
          "both devices rebuild the same identity and keys with no lookup");
    CHECK(memcmp(pa.vault_key, pa.rendezvous, 32) != 0, "the vault and rendezvous keys differ");
    CHECK(pp_hydrate(&px, PQM_LEVEL_STANDARD, other) == PP_OK &&
              memcmp(px.root.did, pa.root.did, 32) != 0 &&
              memcmp(px.vault_key, pa.vault_key, 32) != 0,
          "another seed is another person");
    CHECK(pp_hydrate(&px, (pqm_level_t) 9, other) == PP_ERR_ARG, "unknown level refused");
    CHECK(pp_hydrate(&px, PQM_LEVEL_STANDARD, other) == PP_OK, "rehydrate the other person");
    uint8_t t1[32], t2[32], t3[32], tx[32];
    pp_topic(&pa, 20000, t1);
    pp_topic(&pb, 20000, t2);
    pp_topic(&pa, 20001, t3);
    pp_topic(&px, 20000, tx);
    CHECK(memcmp(t1, t2, 32) == 0, "both devices compute the same topic");
    CHECK(memcmp(t1, t3, 32) != 0, "the topic changes every epoch");
    CHECK(memcmp(t1, tx, 32) != 0, "another person has another topic");
    CHECK(memcmp(t1, pa.root.did, 32) != 0, "the topic is not the DID");
}

/* ---- state, merge, replay ---- */
static box_t A, B, S;
static id_passkey_t phone, laptop;

static void test_merge(void)
{
    uint8_t d1[32], d2[32];
    uint32_t add, n;
    box_init(&A, pa.root.did, CAP);
    box_init(&B, pa.root.did, CAP);

    /* two devices of one account */
    const uint8_t user[16] = {1};
    uint8_t dev1[16], dev2[16];
    memset(dev1, 0xD1, 16);
    memset(dev2, 0xD2, 16);
    CHECK(id_passkey_create(&phone, &host, "zxv.id", dev1, user) == ID_OK &&
              id_passkey_create(&laptop, &host, "zxv.id", dev2, user) == ID_OK,
          "a passkey on each device");
    id_cred_ref_t r1, r2;
    memcpy(r1.cred_id, phone.cred_id, 16);
    memcpy(r1.dev_id, dev1, 16);
    memcpy(r2.cred_id, laptop.cred_id, 16);
    memcpy(r2.dev_id, dev2, 16);
    CHECK(pp_state_add_device(&A.st, &r1, &add) == PP_OK && add == 1, "phone enrolled");
    CHECK(pp_state_add_device(&A.st, &r1, &add) == PP_OK && add == 0, "enrolling twice is a no-op");
    CHECK(pp_state_add_device(&B.st, &r2, &add) == PP_OK, "laptop enrolled on its own");

    /* earnings made separately */
    pp_earn_t e1 = earn(1, 50), e2 = earn(2, 5), e0 = earn(3, 0);
    CHECK(pp_state_add_earn(&A.st, &e0, &add) == PP_ERR_ARG, "a zero receipt is refused");
    CHECK(pp_state_add_earn(&B.st, &e1, &add) == PP_OK && add == 1, "home server earns 50");
    CHECK(pp_state_add_earn(&A.st, &e2, &add) == PP_OK, "phone earns 5");

    /* merge both ways */
    CHECK(pp_state_encode(&B.st, work, sizeof work, &n) == PP_OK, "encode the laptop's state");
    CHECK(pp_state_merge_encoded(&A.st, work, n, &add) == PP_OK && add == 2,
          "phone takes the laptop's receipt and device");
    CHECK(pp_state_encode(&A.st, work, 10, &n) == PP_ERR_SIZE, "encode into too small a buffer");
    CHECK(pp_state_encode(&A.st, work, sizeof work, &n) == PP_OK &&
              pp_state_merge_encoded(&B.st, work, n, &add) == PP_OK && add == 2,
          "laptop takes the phone's");
    digest(&A.st, d1);
    digest(&B.st, d2);
    CHECK(memcmp(d1, d2, 32) == 0, "merge order does not matter: same digest");
    CHECK(pp_state_merge_encoded(&A.st, work, n, &add) == PP_OK && add == 0,
          "merging the same state again adds nothing");
    pp_state_replay(&A.st);
    CHECK(A.st.bal.earned == 55 && A.st.bal.available == 55,
          "both earnings count: 50 + 5, not the larger total");
    CHECK(A.st.s.earn_st[0] == PP_ST_COUNTED && pp_entry_tag(PP_ST_COUNTED) == LPRES_STATE_TRUE,
          "receipts counted, tagged TRUE");

    /* a spend on each device while they cannot see each other */
    uint8_t payee[32];
    memset(payee, 0xAB, 32);
    pp_spend_t sa, sb, sx;
    pp_state_replay(&B.st);
    CHECK(pp_state_spend(&A.st, phone.cred_id, 56, payee, 5000, &sx) == PP_ERR_FUNDS,
          "cannot spend more than the balance");
    CHECK(pp_state_spend(&A.st, phone.cred_id, 0, payee, 5000, &sx) == PP_ERR_ARG,
          "a zero spend is refused");
    uint8_t stranger[16];
    memset(stranger, 0x77, 16);
    CHECK(pp_state_spend(&A.st, stranger, 1, payee, 5000, &sx) == PP_ERR_NOTFOUND,
          "only an enrolled device spends");
    CHECK(pp_state_spend(&A.st, phone.cred_id, 40, payee, 5000, &sa) == PP_OK && sa.seq == 1 &&
              sa.lamport == 1,
          "phone spends 40 offline");
    CHECK(pp_state_spend(&B.st, laptop.cred_id, 30, payee, 5001, &sb) == PP_OK && sb.seq == 1,
          "laptop spends 30 offline from the same 55");
    CHECK(A.st.bal.available == 15 && B.st.bal.available == 25, "each device saw its own spend");

    pp_state_encode(&B.st, work, sizeof work, &n);
    pp_state_merge_encoded(&A.st, work, n, &add);
    pp_state_encode(&A.st, work, sizeof work, &n);
    pp_state_merge_encoded(&B.st, work, n, &add);
    pp_state_replay(&A.st);
    pp_state_replay(&B.st);
    /* funding order: Lamport 1 on both, then signer id decides */
    const pp_spend_t *first = memcmp(phone.cred_id, laptop.cred_id, 16) < 0 ? &sa : &sb;
    const pp_spend_t *second = first == &sa ? &sb : &sa;
    CHECK(state_of(&A.st, first) == PP_ST_SETTLED, "the first in the fixed order settles");
    CHECK(state_of(&A.st, second) == PP_ST_QUARANTINED &&
              pp_entry_tag(PP_ST_QUARANTINED) == LPRES_STATE_BOTH,
          "the spend the balance cannot cover is quarantined as BOTH");
    CHECK(state_of(&B.st, first) == PP_ST_SETTLED && state_of(&B.st, second) == PP_ST_QUARANTINED,
          "the laptop reaches the same verdict");
    CHECK(A.st.bal.settled == first->amount && A.st.bal.quarantined == second->amount &&
              A.st.bal.available == 55 - first->amount,
          "only the settled spend comes off the balance");

    /* the next spend counts both earlier ones */
    pp_spend_t sc;
    CHECK(pp_state_spend(&A.st, phone.cred_id, 1, payee, 6000, &sc) == PP_OK && sc.seq == 2 &&
              sc.lamport == 2,
          "next spend: phone's sequence 2, Lamport time after both");

    /* a cloned device signs two different spends on one slot */
    pp_spend_t e_a = sc, e_b = sc;
    e_b.amount = 3;
    e_b.t_ms = 6001;
    box_init(&S, pa.root.did, CAP);
    pp_state_add_device(&S.st, &r1, 0);
    pp_earn_t big = earn(9, 100);
    pp_state_add_earn(&S.st, &big, 0);
    pp_state_add_spend(&S.st, &e_a, 0);
    pp_state_add_spend(&S.st, &e_b, 0);
    pp_state_replay(&S.st);
    CHECK(state_of(&S.st, &e_a) == PP_ST_HELD && state_of(&S.st, &e_b) == PP_ST_HELD &&
              pp_entry_tag(PP_ST_HELD) == LPRES_STATE_BOTH,
          "two spends on one slot are both held");
    CHECK(S.st.bal.held == 3 && S.st.bal.settled == 0 && S.st.bal.available == 97,
          "the larger amount is kept back, nothing settles");

    /* a receipt seen with two contents */
    pp_earn_t c1 = earn(4, 10), c2 = earn(4, 11);
    pp_state_add_earn(&S.st, &c1, 0);
    pp_state_add_earn(&S.st, &c2, 0);
    pp_state_replay(&S.st);
    CHECK(S.st.bal.earned == 100, "a contested receipt does not count");
    uint32_t contested = 0;
    for (uint32_t i = 0; i < S.st.n_earn; i++) contested += S.st.s.earn_st[i] == PP_ST_CONTESTED;
    CHECK(contested == 2, "both contents are contested");

    /* a spend from a signer this state does not know yet */
    pp_spend_t u = sa;
    memset(u.signer, 0x55, 16);
    u.amount = 7;
    pp_state_add_spend(&S.st, &u, 0);
    pp_state_replay(&S.st);
    CHECK(state_of(&S.st, &u) == PP_ST_UNKNOWN && S.st.bal.unknown == 7 &&
              S.st.bal.available == 90 && pp_entry_tag(PP_ST_UNKNOWN) == LPRES_STATE_NEITHER,
          "an unknown signer's spend is NEITHER and kept back");
    id_cred_ref_t r5;
    memset(&r5, 0x55, sizeof r5);
    pp_state_add_device(&S.st, &r5, 0);
    pp_state_replay(&S.st);
    CHECK(state_of(&S.st, &u) == PP_ST_SETTLED && S.st.bal.available == 90 && S.st.bal.unknown == 0,
          "once the device is known the spend settles");

    /* kept-back amounts larger than the earnings */
    box_t *D = &B; /* reuse: fresh state */
    box_init(D, pa.root.did, CAP);
    pp_earn_t small = earn(8, 2);
    pp_state_add_earn(&D->st, &small, 0);
    pp_state_add_spend(&D->st, &u, 0); /* unknown signer, 7 */
    pp_state_replay(&D->st);
    CHECK(D->st.bal.available == 0 && D->st.bal.deficit == 5,
          "a deficit is reported, never negative");

    /* revocation */
    box_init(&S, pa.root.did, CAP);
    pp_state_add_device(&S.st, &r1, 0);
    pp_state_add_earn(&S.st, &big, 0);
    pp_spend_t v1, v2;
    pp_state_spend(&S.st, phone.cred_id, 10, payee, 7000, &v1);
    pp_state_spend(&S.st, phone.cred_id, 10, payee, 7001, &v2);
    pp_revoke_t rv;
    memcpy(rv.key, phone.cred_id, 16);
    rv.last_seq = 5;
    CHECK(pp_state_add_revoke(&S.st, &rv, &add) == PP_OK && add == 1, "phone lost: revoked");
    rv.last_seq = 1;
    CHECK(pp_state_add_revoke(&S.st, &rv, &add) == PP_OK && add == 1,
          "a lower last sequence replaces a higher one");
    rv.last_seq = 3;
    CHECK(pp_state_add_revoke(&S.st, &rv, &add) == PP_OK && add == 0, "a higher one does not");
    pp_state_replay(&S.st);
    CHECK(state_of(&S.st, &v1) == PP_ST_SETTLED && state_of(&S.st, &v2) == PP_ST_REFUSED &&
              pp_entry_tag(PP_ST_REFUSED) == LPRES_STATE_FALSE && S.st.bal.refused == 10,
          "spends up to the last sequence stand, later ones are refused");
    CHECK(pp_state_spend(&S.st, phone.cred_id, 1, payee, 7002, &sx) == PP_ERR_REVOKED,
          "a revoked device cannot spend");

    /* canonical encodings only */
    pp_state_encode(&A.st, work, sizeof work, &n);
    box_init(&S, pa.root.did, CAP);
    CHECK(pp_state_merge_encoded(&S.st, work, n - 1, &add) == PP_ERR_FORMAT, "truncated refused");
    uint8_t sw[96];
    memcpy(sw, work + 60 + 2 * 80, 96); /* swap the first two spends */
    memmove(work + 60 + 2 * 80, work + 60 + 2 * 80 + 96, 96);
    memcpy(work + 60 + 2 * 80 + 96, sw, 96);
    CHECK(pp_state_merge_encoded(&S.st, work, n, &add) == PP_ERR_FORMAT && S.st.n_earn == 0,
          "out of order refused, nothing merged");
    pp_state_encode(&A.st, work, sizeof work, &n);
    work[4] = 2;
    CHECK(pp_state_merge_encoded(&S.st, work, n, &add) == PP_ERR_FORMAT, "unknown version refused");
    work[4] = 1;
    box_init(&S, px.root.did, CAP);
    CHECK(pp_state_merge_encoded(&S.st, work, n, &add) == PP_ERR_MISMATCH,
          "another person's state is refused");
    box_init(&S, pa.root.did, 1);
    CHECK(pp_state_merge_encoded(&S.st, work, n, &add) == PP_ERR_FULL && S.st.n_earn == 0 &&
              S.st.n_dev == 0,
          "too much for the store: refused whole, nothing changed");
    box_init(&S, pa.root.did, CAP);
    CHECK(pp_state_merge_encoded(&S.st, work, n, &add) == PP_OK && add == 7,
          "a fresh device takes all");
    digest(&S.st, d1);
    digest(&A.st, d2);
    CHECK(memcmp(d1, d2, 32) == 0, "and holds exactly the same state");
}

/* ---- capsule ---- */
static void test_capsule(void)
{
    uint8_t salt[32], salt2[32];
    uint32_t n, n2, add, merged, skipped;
    memset(salt, 3, 32);
    memset(salt2, 4, 32);
    CHECK(pp_capsule_size(&A.st) == pp_state_encoded_size(&A.st) + 60,
          "capsule = state + 44 header + 16 tag");
    CHECK(pp_capsule_seal(&pa, &A.st, salt, blob, 50, &n) == PP_ERR_SIZE, "too small a buffer");
    CHECK(pp_capsule_seal(&px, &A.st, salt, blob, sizeof blob, &n) == PP_ERR_MISMATCH,
          "only the owner seals their state");
    CHECK(pp_capsule_seal(&pa, &A.st, salt, blob, sizeof blob, &n) == PP_OK, "seal");
    CHECK(!contains(blob, n, A.st.s.earn[0].receipt, 32) && !contains(blob, n, pa.root.did, 32),
          "a node pinning it sees no receipt and no DID");
    uint8_t other[65536];
    CHECK(pp_capsule_seal(&pa, &A.st, salt2, other, sizeof other, &n2) == PP_OK && n2 == n &&
              memcmp(other + 44, blob + 44, n - 44) != 0,
          "a new salt gives a new ciphertext");
    ipfsn_cid_t c1, c2;
    CHECK(pp_capsule_cid(blob, n, &c1) == PP_OK && ipfsn_cid_verify(&c1, blob, n) == 0 &&
              pp_capsule_cid(other, n2, &c2) == PP_OK && !ipfsn_cid_equal(&c1, &c2),
          "content-addressed: a raw CID per capsule");

    box_init(&S, pa.root.did, CAP);
    CHECK(pp_capsule_open(&pb, blob, n, work, sizeof work, &S.st, &add) == PP_OK && add == 7,
          "the laptop opens the phone's capsule");
    uint8_t d1[32], d2[32];
    digest(&S.st, d1);
    digest(&A.st, d2);
    CHECK(memcmp(d1, d2, 32) == 0, "and has the phone's exact state");
    box_init(&S, px.root.did, CAP);
    CHECK(pp_capsule_open(&px, blob, n, work, sizeof work, &S.st, &add) == PP_ERR_AUTH &&
              S.st.n_earn == 0,
          "another person's key does not open it");
    box_init(&S, pa.root.did, CAP);
    for (uint32_t bit = 0; bit < 3; bit++) {
        uint32_t at = bit == 0 ? 10 : bit == 1 ? 100 : n - 1;
        blob[at] ^= 1;
        CHECK(pp_capsule_open(&pb, blob, n, work, sizeof work, &S.st, &add) != PP_OK &&
                  S.st.n_earn == 0,
              "one changed bit (salt, body or tag) is refused");
        blob[at] ^= 1;
    }
    CHECK(pp_capsule_open(&pb, blob, n, work, 10, &S.st, &add) == PP_ERR_SIZE, "work too small");
    CHECK(pp_capsule_open(&pb, blob, n - 1, work, sizeof work, &S.st, &add) == PP_ERR_FORMAT,
          "truncated capsule refused");

    /* through the swarm */
    n_swarm = 0;
    CHECK(pp_capsule_publish(&pa, &host, &A.st, 20000, blob, sizeof blob) == PP_OK,
          "phone publishes under today's topic");
    CHECK(pp_capsule_publish(&px, &host, &S.st, 20000, blob, sizeof blob) == PP_ERR_MISMATCH,
          "nobody publishes a state that is not theirs");
    box_init(&S, px.root.did, CAP);
    uint8_t tx[32];
    pp_topic(&pa, 20000, tx);
    memcpy(swarm[n_swarm].topic, tx, 32); /* junk under the same topic */
    memset(swarm[n_swarm].blob, 0x42, 200);
    swarm[n_swarm].len = 200;
    n_swarm++;
    box_init(&S, pa.root.did, CAP);
    CHECK(pp_capsule_fetch(&pb, &host, 20000, blob, sizeof blob, work, sizeof work, &S.st, &merged,
                           &skipped) == PP_OK &&
              merged == 1 && skipped == 1,
          "a blank device finds the capsule by topic and skips junk");
    CHECK(S.st.bal.available == A.st.bal.available, "and shows the same balance");
    CHECK(pp_capsule_fetch(&pb, &host, 20001, blob, sizeof blob, work, sizeof work, &S.st, &merged,
                           &skipped) == PP_OK &&
              merged == 0,
          "nothing under tomorrow's topic yet");
}

/* ---- status list and device vouchers ---- */
static id_dev_cert_t cert_phone, cert_laptop;
static id_cred_status_t status;

static void test_vouchers(void)
{
    CHECK(id_dev_cert_issue(&pa.root, &host, &phone, 1000, &cert_phone) == ID_OK &&
              id_dev_cert_issue(&pa.root, &host, &laptop, 1000, &cert_laptop) == ID_OK,
          "the root certifies each device");
    CHECK(pp_status_rebuild(&pa, &host, &A.st, 9000, &status) == PP_OK && status.n == 2 &&
              id_status_verify(&pa.root.pk, &status) == ID_OK,
          "the status list is rebuilt from the device set and signed");
    CHECK(pp_status_rebuild(&px, &host, &A.st, 9000, &status) == PP_ERR_MISMATCH,
          "only the owner rebuilds it");
    pp_status_rebuild(&pa, &host, &A.st, 9000, &status);

    uint8_t payee[32], sig[ID_MLDSA65_SIG];
    memset(payee, 0xAB, 32);
    pp_spend_t sp;
    CHECK(pp_state_spend(&A.st, phone.cred_id, 2, payee, 9100, &sp) == PP_OK, "a new spend");
    CHECK(pp_voucher_sign(phone.sk, pa.root.did, &sp, 0, sig) == PP_OK, "phone signs the voucher");
    CHECK(pp_verify_device_spend(&pa.root.pk, &status, &cert_phone, phone.pk, &sp, sig) == PP_OK,
          "a peer checks root, status list, certificate and voucher");
    pp_spend_t t = sp;
    t.amount = 200;
    CHECK(pp_verify_device_spend(&pa.root.pk, &status, &cert_phone, phone.pk, &t, sig) ==
              PP_ERR_AUTH,
          "a changed amount fails");
    CHECK(pp_verify_device_spend(&pa.root.pk, &status, &cert_laptop, laptop.pk, &sp, sig) ==
              PP_ERR_MISMATCH,
          "another device's certificate does not fit");
    CHECK(pp_verify_device_spend(&px.root.pk, &status, &cert_phone, phone.pk, &sp, sig) ==
              PP_ERR_AUTH,
          "another root does not fit");
    t = sp;
    memset(t.grant, 1, 16);
    CHECK(pp_verify_device_spend(&pa.root.pk, &status, &cert_phone, phone.pk, &t, sig) ==
              PP_ERR_MISMATCH,
          "a café spend is not a device spend");
    uint8_t other[32];
    memset(other, 9, 32);
    CHECK(!pp_voucher_verify(phone.pk, other, &sp, sig), "the voucher is bound to the account");

    /* revoke the laptop: the next status list leaves it out */
    pp_revoke_t rv;
    memcpy(rv.key, laptop.cred_id, 16);
    rv.last_seq = 1;
    pp_state_add_revoke(&A.st, &rv, 0);
    id_cred_status_t st2;
    CHECK(pp_status_rebuild(&pa, &host, &A.st, 9500, &st2) == PP_OK && st2.n == 1 &&
              id_status_is_active(&st2, phone.cred_id) &&
              !id_status_is_active(&st2, laptop.cred_id),
          "a revoked device leaves the status list");
    pp_spend_t ls = sp;
    memcpy(ls.signer, laptop.cred_id, 16);
    pp_voucher_sign(laptop.sk, pa.root.did, &ls, 0, sig);
    CHECK(pp_verify_device_spend(&pa.root.pk, &st2, &cert_laptop, laptop.pk, &ls, sig) ==
              PP_ERR_REVOKED,
          "peers refuse the revoked device");
}

/* ---- café ---- */
static pp_cafe_t cafe;

static void test_cafe(void)
{
    uint8_t r[32], req[PP_REQUEST_MAX], env[16384], view[16384], payee[32];
    uint32_t req_len, env_len, n, add;
    memset(r, 0x11, 32);
    memset(payee, 0xCD, 32);
    CHECK(pp_cafe_begin(&cafe, r, 0, 10, 3600000, req, sizeof req, &req_len) == PP_ERR_ARG,
          "a request must ask for something");
    CHECK(pp_cafe_begin(&cafe, r, PP_RIGHTS_ALL, 10, 3600000, req, 100, &req_len) == PP_ERR_SIZE,
          "request buffer too small");
    CHECK(pp_cafe_begin(&cafe, r, PP_RIGHTS_ALL, 10, 24u * 3600000u, req, sizeof req, &req_len) ==
                  PP_OK &&
              req_len <= PP_REQUEST_MAX,
          "the café makes a session key and a request");
    uint32_t code = pp_compare_code(cafe.fp);
    CHECK(code < 1000000u, "a six-digit code to compare");

    pp_request_t q;
    uint8_t wrong[32];
    memcpy(wrong, cafe.fp, 32);
    wrong[0] ^= 1;
    CHECK(pp_request_parse(req, req_len, wrong, &q) == PP_ERR_MISMATCH,
          "a request that is not the one on the café screen is refused");
    CHECK(pp_request_parse(req, req_len, cafe.fp, &q) == PP_OK && q.rights == PP_RIGHTS_ALL &&
              q.cap == 10 && memcmp(q.session, cafe.id, 16) == 0 && pp_compare_code(q.fp) == code,
          "the phone reads it and shows the same code");
    req[8] ^= 0x80;
    uint8_t fp2[32];
    memcpy(fp2, cafe.fp, 32);
    CHECK(pp_request_parse(req, req_len, fp2, &q) == PP_ERR_MISMATCH,
          "any change to the request changes the fingerprint");
    req[8] ^= 0x80;
    pp_request_parse(req, req_len, cafe.fp, &q);

    pp_grant_t g;
    pp_state_replay(&A.st);
    uint64_t now = 100000;
    CHECK(pp_grant_issue(&pa, &phone, &q, PP_RIGHTS_ALL | 8u, 5, now, 0, &A.st, &g) ==
              PP_ERR_RIGHTS,
          "no rights beyond the known ones");
    CHECK(pp_grant_issue(&pa, &phone, &q, PP_RIGHTS_ALL, 11, now, 0, &A.st, &g) == PP_ERR_CAP,
          "no more than the café asked for");
    CHECK(pp_grant_issue(&pa, &phone, &q, PP_RIGHT_SPEND, 0, now, 0, &A.st, &g) == PP_ERR_CAP,
          "spending needs a cap above zero");
    CHECK(pp_grant_issue(&pa, &laptop, &q, PP_RIGHTS_ALL, 5, now, 0, &A.st, &g) == PP_ERR_REVOKED,
          "a revoked device cannot approve");
    CHECK(pp_grant_issue(&px, &phone, &q, PP_RIGHTS_ALL, 5, now, 0, &A.st, &g) == PP_ERR_MISMATCH,
          "a passport of another person cannot approve");
    uint32_t ng = A.st.n_grant;
    CHECK(pp_grant_issue(&pa, &phone, &q, PP_RIGHTS_ALL, 5, now, 0, &A.st, &g) == PP_OK &&
              g.expires_ms - g.issued_ms == PP_CAFE_MAX_MS && A.st.n_grant == ng + 1,
          "approved with a cap of 5; 24 hours asked, 12 given; recorded");
    CHECK(pp_grant_verify(&g, phone.pk), "the grant verifies under the phone's passkey");
    pp_grant_t gt = g;
    gt.cap = 1000;
    CHECK(!pp_grant_verify(&gt, phone.pk), "a raised cap breaks the grant");
    CHECK(!pp_grant_verify(&g, laptop.pk), "another device's key does not verify it");

    /* envelope */
    pp_state_encode(&A.st, view, sizeof view, &n);
    uint8_t coins[32];
    memset(coins, 0x21, 32);
    CHECK(pp_envelope_seal(&pa, &g, &q, view, n, coins, env, 100, &env_len) == PP_ERR_SIZE,
          "envelope buffer too small");
    CHECK(pp_envelope_seal(&pa, &g, &q, view, n, coins, env, sizeof env, &env_len) == PP_OK,
          "the phone seals the view to the session");
    pp_grant_t gv = g;
    gv.rights = PP_RIGHT_SPEND;
    CHECK(pp_envelope_seal(&pa, &gv, &q, view, n, coins, blob, sizeof blob, &env_len) ==
              PP_ERR_RIGHTS,
          "no view without the view right");
    pp_envelope_seal(&pa, &g, &q, view, n, coins, env, sizeof env, &env_len);
    uint8_t got[16384];
    uint32_t got_len;
    pp_cafe_t snoop = cafe;
    snoop.kem_sk.mlkem_dk[0] ^= 1;
    CHECK(pp_cafe_accept(&snoop, &g, env, env_len, got, sizeof got, &got_len) != PP_OK,
          "a different session key cannot open the view");
    env[env_len - 1] ^= 1;
    CHECK(pp_cafe_accept(&cafe, &g, env, env_len, got, sizeof got, &got_len) == PP_ERR_AUTH,
          "a changed envelope is refused");
    env[env_len - 1] ^= 1;
    CHECK(pp_cafe_accept(&cafe, &g, env, env_len, got, 10, &got_len) == PP_ERR_SIZE,
          "view buffer too small");
    CHECK(pp_cafe_spend(&cafe, 1, payee, now, 0, &S.st.s.spend[0], blob) == PP_ERR_STATE,
          "no spending before the grant arrives");
    CHECK(pp_cafe_accept(&cafe, &g, env, env_len, got, sizeof got, &got_len) == PP_OK &&
              got_len == n && memcmp(got, view, n) == 0,
          "the café opens the view");
    uint8_t rk[32];
    pp_report_key(&pa, g.grant, rk);
    CHECK(memcmp(rk, cafe.report_key, 32) == 0, "and holds the report key the phone can recompute");
    CHECK(pp_cafe_accept(&cafe, &g, env, env_len, got, sizeof got, &got_len) == PP_ERR_STATE,
          "a grant is taken once");

    /* spending at the café */
    pp_spend_t c1, c2, cx;
    uint8_t s1[ID_MLDSA65_SIG], s2[ID_MLDSA65_SIG], sx[ID_MLDSA65_SIG];
    CHECK(pp_cafe_spend(&cafe, 3, payee, now - 1, 0, &cx, sx) == PP_ERR_EXPIRED,
          "not before the grant");
    CHECK(pp_cafe_spend(&cafe, 3, payee, g.expires_ms + 1, 0, &cx, sx) == PP_ERR_EXPIRED,
          "not after it expires");
    CHECK(pp_cafe_spend(&cafe, 6, payee, now + 10, 0, &cx, sx) == PP_ERR_CAP, "not over the cap");
    CHECK(pp_cafe_spend(&cafe, 3, payee, now + 10, 0, &c1, s1) == PP_OK && c1.seq == 1 &&
              c1.lamport == g.lamport + 1,
          "a café spend of 3");
    CHECK(pp_cafe_spend(&cafe, 3, payee, now + 20, 0, &cx, sx) == PP_ERR_CAP,
          "the cap counts what was already spent");
    CHECK(pp_cafe_spend(&cafe, 2, payee, now + 20, 0, &c2, s2) == PP_OK, "2 more fits exactly");

    /* a peer checks a café voucher */
    pp_status_rebuild(&pa, &host, &A.st, 9600, &status);
    CHECK(pp_verify_cafe_spend(&pa.root.pk, &status, &cert_phone, phone.pk, &g, cafe.sig_pk, &c1,
                               s1, now + 30) == PP_OK,
          "a peer checks the whole chain down to the café spend");
    CHECK(pp_verify_cafe_spend(&pa.root.pk, &status, &cert_phone, phone.pk, &g, cafe.sig_pk, &c1,
                               s1, g.expires_ms + 1) == PP_ERR_EXPIRED,
          "after expiry peers refuse it");
    CHECK(pp_verify_cafe_spend(&pa.root.pk, &status, &cert_phone, phone.pk, &gt, cafe.sig_pk, &c1,
                               s1, now + 30) == PP_ERR_AUTH,
          "a forged grant fails");
    CHECK(pp_verify_cafe_spend(&pa.root.pk, &status, &cert_phone, phone.pk, &g, phone.pk, &c1, s1,
                               now + 30) == PP_ERR_MISMATCH,
          "the voucher key must be the granted session key");
    pp_spend_t big = c1;
    big.amount = 6;
    CHECK(pp_verify_cafe_spend(&pa.root.pk, &status, &cert_phone, phone.pk, &g, cafe.sig_pk, &big,
                               s1, now + 30) == PP_ERR_CAP,
          "one voucher over the cap is refused");
    big.amount = 4;
    CHECK(pp_verify_cafe_spend(&pa.root.pk, &status, &cert_phone, phone.pk, &g, cafe.sig_pk, &big,
                               s1, now + 30) == PP_ERR_AUTH,
          "a changed amount fails the signature");
    pp_grant_t gs = g;
    gs.rights = PP_RIGHT_VIEW;
    CHECK(pp_verify_cafe_spend(&pa.root.pk, &status, &cert_phone, phone.pk, &gs, cafe.sig_pk, &c1,
                               s1, now + 30) == PP_ERR_AUTH,
          "rights are covered by the grant signature");

    /* report back to the phone */
    pp_spend_t rep[2] = {c1, c2};
    uint8_t rb[1024];
    uint32_t rl;
    CHECK(pp_cafe_report(&cafe, rep, 2, rb, sizeof rb, &rl) == PP_OK, "the café reports");
    pp_grant_t gother = g;
    gother.grant[0] ^= 1;
    CHECK(pp_report_open(&pa, &gother, rb, rl, work, sizeof work, &A.st, &add) == PP_ERR_MISMATCH,
          "a report for another grant is refused");
    rb[rl - 1] ^= 1;
    CHECK(pp_report_open(&pa, &g, rb, rl, work, sizeof work, &A.st, &add) == PP_ERR_AUTH,
          "a changed report is refused");
    rb[rl - 1] ^= 1;
    uint64_t before_av = A.st.bal.available;
    CHECK(pp_report_open(&pa, &g, rb, rl, work, sizeof work, &A.st, &add) == PP_OK && add == 2,
          "the phone merges the café spends");
    CHECK(state_of(&A.st, &c1) == PP_ST_SETTLED && state_of(&A.st, &c2) == PP_ST_SETTLED &&
              A.st.bal.available == before_av - 5,
          "both settle and come off the balance");
    CHECK(pp_report_open(&pa, &g, rb, rl, work, sizeof work, &A.st, &add) == PP_OK && add == 0,
          "the same report twice adds nothing");

    /* the phone applies the grant again on merge */
    pp_spend_t over = c2, late = c2, early = c2;
    over.seq = 3;
    over.lamport = c2.lamport + 1;
    over.amount = 1;
    pp_state_add_spend(&A.st, &over, 0);
    late.seq = 4;
    late.lamport = c2.lamport + 2;
    late.amount = 1;
    late.t_ms = g.expires_ms + 1;
    pp_state_add_spend(&A.st, &late, 0);
    early.seq = 5;
    early.lamport = c2.lamport + 3;
    early.amount = 1;
    early.t_ms = g.issued_ms - 1;
    pp_state_add_spend(&A.st, &early, 0);
    pp_state_replay(&A.st);
    CHECK(state_of(&A.st, &over) == PP_ST_REFUSED, "a spend past the cap is refused on merge");
    CHECK(state_of(&A.st, &late) == PP_ST_REFUSED && state_of(&A.st, &early) == PP_ST_REFUSED,
          "spends outside the grant's time are refused");
    pp_revoke_t rv;
    memcpy(rv.key, g.grant, 16);
    rv.last_seq = 1;
    pp_state_add_revoke(&A.st, &rv, 0);
    pp_state_replay(&A.st);
    CHECK(state_of(&A.st, &c1) == PP_ST_SETTLED && state_of(&A.st, &c2) == PP_ST_REFUSED,
          "revoking the grant refuses its spends after the last sequence");
    pp_revoke_t rs;
    memcpy(rs.key, g.session, 16);
    rs.last_seq = 0;
    pp_state_add_revoke(&A.st, &rs, 0);
    pp_state_replay(&A.st);
    CHECK(state_of(&A.st, &c1) == PP_ST_REFUSED, "revoking the session key works the same");

    /* a grant without the spend right */
    box_init(&S, pa.root.did, CAP);
    pp_grant_rec_t gr = {{0}, {0}, {0}, PP_RIGHT_VIEW, 5, now, now + 1000};
    memcpy(gr.grant, g.grant, 16);
    memcpy(gr.session, g.session, 16);
    CHECK(pp_state_add_grant(&S.st, &gr, 0) == PP_OK, "record a view-only grant");
    pp_earn_t e = earn(7, 50);
    pp_state_add_earn(&S.st, &e, 0);
    pp_state_add_spend(&S.st, &c1, 0);
    pp_state_replay(&S.st);
    CHECK(state_of(&S.st, &c1) == PP_ST_REFUSED, "a spend under a view-only grant is refused");
    pp_grant_rec_t gr2 = gr;
    gr2.rights = PP_RIGHTS_ALL;
    pp_state_add_grant(&S.st, &gr2, 0);
    pp_state_replay(&S.st);
    CHECK(state_of(&S.st, &c1) == PP_ST_REFUSED, "two records claiming one grant id: refused");
    pp_grant_rec_t bad = gr;
    bad.expires_ms = bad.issued_ms + PP_CAFE_MAX_MS + 1;
    CHECK(pp_state_add_grant(&S.st, &bad, 0) == PP_ERR_ARG, "a grant over 12 hours is refused");
    pp_spend_t wrong_session = c1;
    memset(wrong_session.signer, 0x66, 16);
    box_init(&S, pa.root.did, CAP);
    gr.rights = PP_RIGHTS_ALL;
    pp_state_add_grant(&S.st, &gr, 0);
    pp_state_add_earn(&S.st, &e, 0);
    pp_state_add_spend(&S.st, &wrong_session, 0);
    pp_spend_t nog = c1;
    memset(nog.grant, 0x44, 16);
    pp_state_add_spend(&S.st, &nog, 0);
    pp_state_replay(&S.st);
    CHECK(state_of(&S.st, &wrong_session) == PP_ST_REFUSED,
          "a spend signed by another session under the grant is refused");
    CHECK(state_of(&S.st, &nog) == PP_ST_UNKNOWN, "a spend under an unseen grant waits as NEITHER");

    /* zero residue */
    pp_cafe_end(&cafe);
    static const pp_cafe_t zero;
    CHECK(memcmp(&cafe, &zero, sizeof cafe) == 0, "ending the session wipes every key");
    CHECK(pp_cafe_spend(&cafe, 1, payee, now + 40, 0, &cx, sx) == PP_ERR_STATE,
          "no spending after the end");
    CHECK(pp_cafe_report(&cafe, rep, 0, rb, sizeof rb, &rl) == PP_ERR_STATE,
          "no report after the end");
}

/* Every entry point refuses missing arguments without touching anything. */
static void test_args(void)
{
    uint8_t seed[32] = {1}, b32[32] = {0}, sig[ID_MLDSA65_SIG];
    char txt[PP_SEED_TEXT];
    id_kdf_params_t kp = {64, 1, 1};
    const uint8_t pw[] = "a long enough passphrase";
    uint32_t n, a, m, k;
    CHECK(pp_seed_to_text(0, txt) == PP_ERR_ARG && pp_seed_to_text(seed, 0) == PP_ERR_ARG,
          "seed_to_text args");
    CHECK(pp_seed_from_text(0, 5, seed) == PP_ERR_ARG && pp_seed_from_text("x", 1, 0) == PP_ERR_ARG,
          "seed_from_text args");
    CHECK(pp_seed_from_passphrase(0, pw, 20, pw, 3, argon_mem, 64, b32) == PP_ERR_ARG &&
              pp_seed_from_passphrase(&kp, 0, 20, pw, 3, argon_mem, 64, b32) == PP_ERR_ARG &&
              pp_seed_from_passphrase(&kp, pw, 20, 0, 3, argon_mem, 64, b32) == PP_ERR_ARG &&
              pp_seed_from_passphrase(&kp, pw, 20, pw, 3, 0, 64, b32) == PP_ERR_ARG &&
              pp_seed_from_passphrase(&kp, pw, 20, pw, 3, argon_mem, 64, 0) == PP_ERR_ARG,
          "seed_from_passphrase args");
    static uint8_t longpw[PP_PASS_MAX + 1];
    memset(longpw, 'x', sizeof longpw);
    CHECK(pp_seed_from_passphrase(&kp, longpw, PP_PASS_MAX + 1, pw, 3, argon_mem, 64, b32) ==
                  PP_ERR_ARG &&
              pp_seed_from_passphrase(&kp, longpw, PP_PASS_MAX, pw, 3, argon_mem, 64, b32) == PP_OK,
          "passphrase up to PP_PASS_MAX bytes");
    CHECK(pp_seed_from_passphrase(&kp, pw, 20, longpw, PP_HANDLE_MAX + 1, argon_mem, 64, b32) ==
                  PP_ERR_ARG &&
              pp_seed_from_passphrase(&kp, pw, 20, longpw, PP_HANDLE_MAX, argon_mem, 64, b32) ==
                  PP_OK,
          "handle up to PP_HANDLE_MAX bytes");
    pp_kdf_v1(0);
    pp_wipe(0);
    pp_cafe_end(0);
    pp_report_key(0, seed, b32);
    CHECK(pp_hydrate(0, PQM_LEVEL_STANDARD, seed) == PP_ERR_ARG &&
              pp_hydrate(&px, PQM_LEVEL_STANDARD, 0) == PP_ERR_ARG,
          "hydrate args");
    CHECK(pp_topic(0, 1, b32) == PP_ERR_ARG && pp_topic(&pa, 1, 0) == PP_ERR_ARG, "topic args");

    box_init(&S, pa.root.did, CAP);
    pp_store_t st0 = S.st.s;
    pp_state_t t;
    CHECK(pp_state_init(0, pa.root.did, &st0) == PP_ERR_ARG &&
              pp_state_init(&t, 0, &st0) == PP_ERR_ARG && pp_state_init(&t, pa.root.did, 0) == PP_ERR_ARG,
          "state_init args");
    void **slots[] = {(void **) &st0.earn,       (void **) &st0.earn_st, (void **) &st0.spend,
                      (void **) &st0.spend_st,   (void **) &st0.spend_order, (void **) &st0.grant,
                      (void **) &st0.grant_used, (void **) &st0.revoke,  (void **) &st0.dev};
    uint32_t refused = 0;
    for (uint32_t i = 0; i < 9; i++) {
        void *keep = *slots[i];
        *slots[i] = 0;
        refused += pp_state_init(&t, pa.root.did, &st0) == PP_ERR_ARG;
        *slots[i] = keep;
    }
    CHECK(refused == 9, "every store array is required");
    pp_earn_t e = earn(1, 1);
    pp_spend_t sp;
    memset(&sp, 0, sizeof sp);
    memset(sp.signer, 1, 16);
    sp.seq = sp.lamport = sp.amount = 1;
    pp_grant_rec_t g = {{1}, {1}, {1}, PP_RIGHT_VIEW, 0, 1, 2};
    pp_revoke_t r = {{1}, 0};
    id_cred_ref_t d = {{1}, {1}};
    CHECK(pp_state_add_earn(0, &e, &a) == PP_ERR_ARG && pp_state_add_earn(&S.st, 0, &a) == PP_ERR_ARG &&
              pp_state_add_spend(0, &sp, &a) == PP_ERR_ARG &&
              pp_state_add_spend(&S.st, 0, &a) == PP_ERR_ARG &&
              pp_state_add_grant(0, &g, &a) == PP_ERR_ARG &&
              pp_state_add_grant(&S.st, 0, &a) == PP_ERR_ARG &&
              pp_state_add_revoke(0, &r, &a) == PP_ERR_ARG &&
              pp_state_add_revoke(&S.st, 0, &a) == PP_ERR_ARG &&
              pp_state_add_device(0, &d, &a) == PP_ERR_ARG &&
              pp_state_add_device(&S.st, 0, &a) == PP_ERR_ARG,
          "add args");
    pp_spend_t bad = sp;
    bad.seq = 0;
    uint32_t nope = 0;
    nope += pp_state_add_spend(&S.st, &bad, 0) == PP_ERR_ARG;
    bad = sp;
    bad.lamport = 0;
    nope += pp_state_add_spend(&S.st, &bad, 0) == PP_ERR_ARG;
    bad = sp;
    bad.amount = 0;
    nope += pp_state_add_spend(&S.st, &bad, 0) == PP_ERR_ARG;
    bad = sp;
    memset(bad.signer, 0, 16);
    nope += pp_state_add_spend(&S.st, &bad, 0) == PP_ERR_ARG;
    CHECK(nope == 4, "a spend needs a sequence, a Lamport time, an amount and a signer");
    pp_grant_rec_t bg = g;
    nope = 0;
    bg.rights = 0;
    nope += pp_state_add_grant(&S.st, &bg, 0) == PP_ERR_ARG;
    bg = g;
    bg.rights = 8;
    nope += pp_state_add_grant(&S.st, &bg, 0) == PP_ERR_ARG;
    bg = g;
    bg.expires_ms = bg.issued_ms;
    nope += pp_state_add_grant(&S.st, &bg, 0) == PP_ERR_ARG;
    bg = g;
    memset(bg.grant, 0, 16);
    nope += pp_state_add_grant(&S.st, &bg, 0) == PP_ERR_ARG;
    bg = g;
    memset(bg.session, 0, 16);
    nope += pp_state_add_grant(&S.st, &bg, 0) == PP_ERR_ARG;
    bg = g;
    bg.expires_ms = bg.issued_ms + PP_CAFE_MAX_MS;
    nope += pp_state_add_grant(&S.st, &bg, 0) == PP_OK;
    CHECK(nope == 6, "a grant needs rights, an id, a session and a window up to 12 hours");
    pp_revoke_t r0 = {{0}, 0};
    id_cred_ref_t d0 = {{0}, {1}};
    CHECK(pp_state_add_revoke(&S.st, &r0, 0) == PP_ERR_ARG &&
              pp_state_add_device(&S.st, &d0, 0) == PP_ERR_ARG,
          "a revocation and a device need a key");
    box_init(&S, pa.root.did, 1);
    CHECK(pp_state_add_earn(&S.st, &e, 0) == PP_OK && pp_state_add_earn(&S.st, &(pp_earn_t){{2}, {0}, 1, 0}, 0) == PP_ERR_FULL &&
              pp_state_add_spend(&S.st, &sp, 0) == PP_OK &&
              pp_state_add_grant(&S.st, &g, 0) == PP_OK && pp_state_add_revoke(&S.st, &r, 0) == PP_OK &&
              pp_state_add_device(&S.st, &d, 0) == PP_OK,
          "one of each fits a store of one");
    pp_spend_t sp2 = sp;
    sp2.seq = 2;
    pp_grant_rec_t g2 = g;
    g2.grant[1] = 1;
    pp_revoke_t r2 = r;
    r2.key[1] = 1;
    id_cred_ref_t d2 = d;
    d2.cred_id[1] = 1;
    CHECK(pp_state_add_spend(&S.st, &sp2, 0) == PP_ERR_FULL &&
              pp_state_add_grant(&S.st, &g2, 0) == PP_ERR_FULL &&
              pp_state_add_revoke(&S.st, &r2, 0) == PP_ERR_FULL &&
              pp_state_add_device(&S.st, &d2, 0) == PP_ERR_FULL,
          "a second one of each is FULL");
    r.last_seq = 0;
    CHECK(pp_state_add_revoke(&S.st, &r, &a) == PP_OK && a == 0, "a full store still lowers nothing new");

    CHECK(pp_state_replay(0) == PP_ERR_ARG, "replay args");
    uint8_t payee[32] = {0};
    CHECK(pp_state_spend(0, seed, 1, payee, 1, &sp) == PP_ERR_ARG &&
              pp_state_spend(&S.st, 0, 1, payee, 1, &sp) == PP_ERR_ARG &&
              pp_state_spend(&S.st, seed, 1, 0, 1, &sp) == PP_ERR_ARG &&
              pp_state_spend(&S.st, seed, 1, payee, 1, 0) == PP_ERR_ARG,
          "state_spend args");
    CHECK(pp_state_encoded_size(0) == 0 && pp_state_encode(0, work, 10, &n) == PP_ERR_ARG &&
              pp_state_encode(&S.st, 0, 10, &n) == PP_ERR_ARG &&
              pp_state_encode(&S.st, work, 10, 0) == PP_ERR_ARG,
          "encode args");
    CHECK(pp_state_merge_encoded(0, work, 60, &a) == PP_ERR_ARG &&
              pp_state_merge_encoded(&S.st, 0, 60, &a) == PP_ERR_ARG,
          "merge args");
    CHECK(pp_state_digest(0, work, 10, b32) == PP_ERR_ARG &&
              pp_state_digest(&S.st, 0, 10, b32) == PP_ERR_ARG &&
              pp_state_digest(&S.st, work, 10, 0) == PP_ERR_ARG &&
              pp_state_digest(&S.st, work, 10, b32) == PP_ERR_SIZE,
          "digest args");
    id_cred_status_t cs;
    CHECK(pp_status_rebuild(0, &host, &S.st, 1, &cs) == PP_ERR_ARG &&
              pp_status_rebuild(&pa, 0, &S.st, 1, &cs) == PP_ERR_ARG &&
              pp_status_rebuild(&pa, &host, 0, 1, &cs) == PP_ERR_ARG &&
              pp_status_rebuild(&pa, &host, &S.st, 1, 0) == PP_ERR_ARG,
          "status_rebuild args");
    CHECK(pp_capsule_size(0) == 0 && pp_capsule_seal(0, &S.st, seed, blob, 10, &n) == PP_ERR_ARG &&
              pp_capsule_seal(&pa, 0, seed, blob, 10, &n) == PP_ERR_ARG &&
              pp_capsule_seal(&pa, &S.st, 0, blob, 10, &n) == PP_ERR_ARG &&
              pp_capsule_seal(&pa, &S.st, seed, 0, 10, &n) == PP_ERR_ARG &&
              pp_capsule_seal(&pa, &S.st, seed, blob, 10, 0) == PP_ERR_ARG,
          "capsule_seal args");
    CHECK(pp_capsule_open(0, blob, 10, work, 10, &S.st, &a) == PP_ERR_ARG &&
              pp_capsule_open(&pa, 0, 10, work, 10, &S.st, &a) == PP_ERR_ARG &&
              pp_capsule_open(&pa, blob, 10, 0, 10, &S.st, &a) == PP_ERR_ARG &&
              pp_capsule_open(&pa, blob, 10, work, 10, 0, &a) == PP_ERR_ARG,
          "capsule_open args");
    ipfsn_cid_t cid;
    CHECK(pp_capsule_cid(0, 1, &cid) == PP_ERR_ARG && pp_capsule_cid(blob, 1, 0) == PP_ERR_ARG,
          "capsule_cid args");
    id_host_t nopub = host, nofetch = host;
    nopub.publish = 0;
    nofetch.fetch = 0;
    CHECK(pp_capsule_publish(0, &host, &S.st, 1, blob, 10) == PP_ERR_ARG &&
              pp_capsule_publish(&pa, 0, &S.st, 1, blob, 10) == PP_ERR_ARG &&
              pp_capsule_publish(&pa, &nopub, &S.st, 1, blob, 10) == PP_ERR_ARG &&
              pp_capsule_publish(&pa, &host, 0, 1, blob, 10) == PP_ERR_ARG &&
              pp_capsule_publish(&pa, &host, &S.st, 1, 0, 10) == PP_ERR_ARG,
          "capsule_publish args");
    CHECK(pp_capsule_fetch(0, &host, 1, blob, 10, work, 10, &S.st, &m, &k) == PP_ERR_ARG &&
              pp_capsule_fetch(&pa, 0, 1, blob, 10, work, 10, &S.st, &m, &k) == PP_ERR_ARG &&
              pp_capsule_fetch(&pa, &nofetch, 1, blob, 10, work, 10, &S.st, &m, &k) == PP_ERR_ARG &&
              pp_capsule_fetch(&pa, &host, 1, 0, 10, work, 10, &S.st, &m, &k) == PP_ERR_ARG &&
              pp_capsule_fetch(&pa, &host, 1, blob, 10, 0, 10, &S.st, &m, &k) == PP_ERR_ARG &&
              pp_capsule_fetch(&pa, &host, 1, blob, 10, work, 10, 0, &m, &k) == PP_ERR_ARG,
          "capsule_fetch args");
    uint8_t e96[PP_SPEND_BYTES];
    CHECK(pp_spend_encode(0, e96) == PP_ERR_ARG && pp_spend_encode(&sp, 0) == PP_ERR_ARG,
          "spend_encode args");
    CHECK(pp_voucher_sign(0, seed, &sp, 0, sig) == PP_ERR_ARG &&
              pp_voucher_sign(phone.sk, 0, &sp, 0, sig) == PP_ERR_ARG &&
              pp_voucher_sign(phone.sk, seed, 0, 0, sig) == PP_ERR_ARG &&
              pp_voucher_sign(phone.sk, seed, &sp, 0, 0) == PP_ERR_ARG,
          "voucher_sign args");
    CHECK(!pp_voucher_verify(0, seed, &sp, sig) && !pp_voucher_verify(phone.pk, 0, &sp, sig) &&
              !pp_voucher_verify(phone.pk, seed, 0, sig) && !pp_voucher_verify(phone.pk, seed, &sp, 0),
          "voucher_verify args");
    CHECK(pp_verify_device_spend(&pa.root.pk, &status, &cert_phone, phone.pk, 0, sig) == PP_ERR_ARG &&
              pp_verify_device_spend(&pa.root.pk, &status, &cert_phone, phone.pk, &sp, 0) ==
                  PP_ERR_ARG &&
              pp_verify_device_spend(0, &status, &cert_phone, phone.pk, &sp, sig) == PP_ERR_ARG &&
              pp_verify_device_spend(&pa.root.pk, 0, &cert_phone, phone.pk, &sp, sig) == PP_ERR_ARG &&
              pp_verify_device_spend(&pa.root.pk, &status, 0, phone.pk, &sp, sig) == PP_ERR_ARG &&
              pp_verify_device_spend(&pa.root.pk, &status, &cert_phone, 0, &sp, sig) == PP_ERR_ARG,
          "verify_device_spend args");

    static pp_cafe_t c;
    static uint8_t req[PP_REQUEST_MAX];
    pp_request_t q;
    pp_grant_t gg;
    memset(&gg, 0, sizeof gg);
    CHECK(pp_cafe_begin(0, seed, 1, 1, 1, req, sizeof req, &n) == PP_ERR_ARG &&
              pp_cafe_begin(&c, 0, 1, 1, 1, req, sizeof req, &n) == PP_ERR_ARG &&
              pp_cafe_begin(&c, seed, 1, 1, 1, 0, sizeof req, &n) == PP_ERR_ARG &&
              pp_cafe_begin(&c, seed, 1, 1, 1, req, sizeof req, 0) == PP_ERR_ARG &&
              pp_cafe_begin(&c, seed, 8, 1, 1, req, sizeof req, &n) == PP_ERR_ARG &&
              pp_cafe_begin(&c, seed, 1, 1, 0, req, sizeof req, &n) == PP_ERR_ARG,
          "cafe_begin args");
    CHECK(pp_request_parse(0, 10, seed, &q) == PP_ERR_ARG &&
              pp_request_parse(req, 10, 0, &q) == PP_ERR_ARG &&
              pp_request_parse(req, 10, seed, 0) == PP_ERR_ARG &&
              pp_request_parse(req, 10, seed, &q) == PP_ERR_FORMAT,
          "request_parse args");
    CHECK(pp_grant_issue(0, &phone, &q, 1, 0, 1, 0, &A.st, &gg) == PP_ERR_ARG &&
              pp_grant_issue(&pa, 0, &q, 1, 0, 1, 0, &A.st, &gg) == PP_ERR_ARG &&
              pp_grant_issue(&pa, &phone, 0, 1, 0, 1, 0, &A.st, &gg) == PP_ERR_ARG &&
              pp_grant_issue(&pa, &phone, &q, 1, 0, 1, 0, 0, &gg) == PP_ERR_ARG &&
              pp_grant_issue(&pa, &phone, &q, 1, 0, 1, 0, &A.st, 0) == PP_ERR_ARG,
          "grant_issue args");
    CHECK(!pp_grant_verify(0, phone.pk) && !pp_grant_verify(&gg, 0), "grant_verify args");
    CHECK(pp_envelope_seal(0, &gg, &q, 0, 0, seed, blob, 10, &n) == PP_ERR_ARG &&
              pp_envelope_seal(&pa, 0, &q, 0, 0, seed, blob, 10, &n) == PP_ERR_ARG &&
              pp_envelope_seal(&pa, &gg, 0, 0, 0, seed, blob, 10, &n) == PP_ERR_ARG &&
              pp_envelope_seal(&pa, &gg, &q, 0, 1, seed, blob, 10, &n) == PP_ERR_ARG &&
              pp_envelope_seal(&pa, &gg, &q, 0, 0, 0, blob, 10, &n) == PP_ERR_ARG &&
              pp_envelope_seal(&pa, &gg, &q, 0, 0, seed, 0, 10, &n) == PP_ERR_ARG &&
              pp_envelope_seal(&pa, &gg, &q, 0, 0, seed, blob, 10, 0) == PP_ERR_ARG,
          "envelope_seal args");
    CHECK(pp_cafe_accept(0, &gg, blob, 10, work, 10, &n) == PP_ERR_ARG &&
              pp_cafe_accept(&c, 0, blob, 10, work, 10, &n) == PP_ERR_ARG &&
              pp_cafe_accept(&c, &gg, 0, 10, work, 10, &n) == PP_ERR_ARG &&
              pp_cafe_accept(&c, &gg, blob, 10, 0, 10, &n) == PP_ERR_ARG &&
              pp_cafe_accept(&c, &gg, blob, 10, work, 10, 0) == PP_ERR_ARG &&
              pp_cafe_accept(&c, &gg, blob, 10, work, 10, &n) == PP_ERR_STATE,
          "cafe_accept args; nothing to accept before begin");
    CHECK(pp_cafe_spend(0, 1, payee, 1, 0, &sp, sig) == PP_ERR_ARG &&
              pp_cafe_spend(&c, 1, 0, 1, 0, &sp, sig) == PP_ERR_ARG &&
              pp_cafe_spend(&c, 1, payee, 1, 0, 0, sig) == PP_ERR_ARG &&
              pp_cafe_spend(&c, 1, payee, 1, 0, &sp, 0) == PP_ERR_ARG &&
              pp_cafe_spend(&c, 0, payee, 1, 0, &sp, sig) == PP_ERR_ARG,
          "cafe_spend args");
    CHECK(pp_cafe_report(0, &sp, 1, blob, 10, &n) == PP_ERR_ARG &&
              pp_cafe_report(&c, 0, 1, blob, 10, &n) == PP_ERR_ARG &&
              pp_cafe_report(&c, &sp, 1, 0, 10, &n) == PP_ERR_ARG &&
              pp_cafe_report(&c, &sp, 1, blob, 10, 0) == PP_ERR_ARG,
          "cafe_report args");
    CHECK(pp_report_open(0, &gg, blob, 60, work, 10, &S.st, &a) == PP_ERR_ARG &&
              pp_report_open(&pa, 0, blob, 60, work, 10, &S.st, &a) == PP_ERR_ARG &&
              pp_report_open(&pa, &gg, 0, 60, work, 10, &S.st, &a) == PP_ERR_ARG &&
              pp_report_open(&pa, &gg, blob, 60, 0, 10, &S.st, &a) == PP_ERR_ARG &&
              pp_report_open(&pa, &gg, blob, 60, work, 10, 0, &a) == PP_ERR_ARG,
          "report_open args");
    CHECK(pp_verify_cafe_spend(&pa.root.pk, &status, &cert_phone, phone.pk, 0, phone.pk, &sp, sig, 1) ==
                  PP_ERR_ARG &&
              pp_verify_cafe_spend(&pa.root.pk, &status, &cert_phone, phone.pk, &gg, 0, &sp, sig, 1) ==
                  PP_ERR_ARG &&
              pp_verify_cafe_spend(&pa.root.pk, &status, &cert_phone, phone.pk, &gg, phone.pk, 0, sig,
                                   1) == PP_ERR_ARG &&
              pp_verify_cafe_spend(&pa.root.pk, &status, &cert_phone, phone.pk, &gg, phone.pk, &sp, 0,
                                   1) == PP_ERR_ARG,
          "verify_cafe_spend args");
}

/* ---- seed text edges, against an encoder written from the spec ---- */
static void enc34(const uint8_t b[34], char out[56])
{
    static const char T[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
    for (uint32_t c = 0; c < 55; c++) {
        uint32_t v = 0;
        for (uint32_t k = 0; k < 5; k++) {
            uint32_t bit = c * 5 + k;
            v = (v << 1) | (bit < 272 ? (uint32_t) (b[bit / 8] >> (7 - bit % 8)) & 1u : 0u);
        }
        out[c] = T[v];
    }
    out[55] = 0;
}

static void test_seed_edges(void)
{
    uint8_t b[34], seed[32], back[32], keep[32];
    char t[56], t2[80], txt[PP_SEED_TEXT];
    for (int i = 0; i < 32; i++) b[i] = (uint8_t) (i * 11 + 3);
    uint8_t h[32];
    pp_sha3(h, "zxv.passport.seed.v1", b, 32, 0, 0, 0, 0);
    b[32] = h[0];
    b[33] = h[1];
    enc34(b, t);
    CHECK(pp_seed_from_text(t, 55, back) == PP_OK && memcmp(back, b, 32) == 0,
          "the spec: 32 bytes, SHA3(len || label || seed)[0..2], base32, no dashes needed");
    pp_seed_to_text(b, txt);
    uint32_t j = 0;
    for (uint32_t i = 0; txt[i]; i++)
        if (txt[i] != '-') t2[j++] = txt[i];
    t2[j] = 0;
    CHECK(strcmp(t, t2) == 0, "seed_to_text writes the same characters");
    memset(keep, 0xC7, 32);
    for (int which = 32; which < 34; which++) {
        b[which] ^= 0x01;
        enc34(b, t);
        memcpy(seed, keep, 32);
        CHECK(pp_seed_from_text(t, 55, seed) == PP_ERR_CHECKSUM && memcmp(seed, keep, 32) == 0,
              "either checksum byte wrong: CHECKSUM, the output untouched");
        b[which] ^= 0x01;
    }
    enc34(b, t);
    t[54] = (char) (t[54] == 'Z' ? 'Y' : t[54] + 1); /* low padding bit */
    memcpy(seed, keep, 32);
    CHECK(pp_seed_from_text(t, 55, seed) == PP_ERR_FORMAT && memcmp(seed, keep, 32) == 0,
          "the lowest padding bit must be zero too");
    enc34(b, t);
    snprintf(t2, sizeof t2, "%.20sU%s", t, t + 20);
    CHECK(pp_seed_from_text(t2, 56, seed) == PP_ERR_FORMAT, "an invalid character inside is refused");
    snprintf(t2, sizeof t2, "%.20s -%s", t, t + 20);
    CHECK(pp_seed_from_text(t2, 57, back) == PP_OK && memcmp(back, b, 32) == 0,
          "spaces and dashes anywhere are skipped");
    CHECK(pp_seed_from_text(t, 54, seed) == PP_ERR_FORMAT, "len cuts the text");
    char nul[60];
    memcpy(nul, t, 56);
    nul[30] = 0;
    CHECK(pp_seed_from_text(nul, 55, seed) == PP_ERR_FORMAT, "and so does a NUL");

    memset(seed, 0xFF, 32);
    pp_seed_to_text(seed, txt);
    for (int i = 0; txt[i]; i++)
        if (txt[i] >= 'A' && txt[i] <= 'Z') txt[i] = (char) (txt[i] + 32);
    CHECK(strchr(txt, 'z') != 0 && pp_seed_from_text(txt, 65, back) == PP_OK &&
              memcmp(back, seed, 32) == 0,
          "lower-case z, the last character of the alphabet, reads");
    memset(seed, 0, 32);
    pp_seed_to_text(seed, txt);
    CHECK(strncmp(txt, "00000-00000", 11) == 0 && pp_seed_from_text(txt, 65, back) == PP_OK &&
              pp_is_zero(back, 32),
          "the zero seed round-trips");
    CHECK(pp_strlen("", 4) == 0 && pp_strlen("abc", 0) == 0, "pp_strlen of nothing");

    /* a long label is cut to 64 bytes, as ident's id_shake does */
    char lab[71];
    memset(lab, 'q', 70);
    lab[70] = 0;
    uint8_t in[70], h1[32], h2[32];
    in[0] = 64;
    memset(in + 1, 'q', 64);
    in[65] = 9;
    shake256(in, 66, h1, 32);
    uint8_t nine = 9;
    CHECK(pp_shake(h2, 32, lab, &nine, 1, 0, 0, 0, 0) && memcmp(h1, h2, 32) == 0,
          "labels are cut at 64 bytes");
}

/* ---- state edges: every field, every set, every bound ---- */
static pp_earn_t mk_earn(uint8_t k)
{
    pp_earn_t e;
    memset(&e, 0, sizeof e);
    memset(e.receipt, k, 32);
    memset(e.issuer, 0x10 + k, 32);
    e.amount = 100u + k;
    e.t_ms = 1000u + k;
    return e;
}

static pp_spend_t mk_spend(uint8_t signer, uint64_t seq, uint64_t lamport, uint64_t amount)
{
    pp_spend_t s;
    memset(&s, 0, sizeof s);
    memset(s.signer, signer, 16);
    s.seq = seq;
    s.lamport = lamport;
    s.amount = amount;
    s.t_ms = 40u + seq;
    memset(s.payee, 0x50 + signer, 32);
    return s;
}

static pp_grant_rec_t mk_grant(uint8_t k)
{
    pp_grant_rec_t g;
    memset(&g, 0, sizeof g);
    memset(g.grant, k, 16);
    memset(g.session, 0x20 + k, 16);
    memset(g.device, 0x30 + k, 16);
    g.rights = k; /* 1, 2, 3 */
    g.cap = 500u + k;
    g.issued_ms = 600u + k;
    g.expires_ms = 700u + k;
    return g;
}

static void fill3(box_t *b)
{
    static const uint8_t ord[3] = {3, 1, 2};
    for (int i = 0; i < 3; i++) {
        uint8_t k = ord[i];
        pp_earn_t e = mk_earn(k);
        pp_spend_t s = mk_spend(k, 10u + k, 20u + k, 30u + k);
        memset(s.grant, 0x60 + k, 16);
        pp_grant_rec_t g = mk_grant(k);
        pp_revoke_t r;
        memset(r.key, k, 16);
        r.last_seq = 800u + k;
        id_cred_ref_t d;
        memset(d.cred_id, k, 16);
        memset(d.dev_id, 0x40 + k, 16);
        pp_state_add_earn(&b->st, &e, 0);
        pp_state_add_spend(&b->st, &s, 0);
        pp_state_add_grant(&b->st, &g, 0);
        pp_state_add_revoke(&b->st, &r, 0);
        pp_state_add_device(&b->st, &d, 0);
    }
}

/* grant records have padding, so compare their encodings */
static bool same_grants(const pp_state_t *x, const pp_state_t *y)
{
    uint8_t a[PP_GRANT_BYTES], b[PP_GRANT_BYTES];
    for (uint32_t i = 0; i < x->n_grant; i++) {
        pp_grant_rec_enc(&x->s.grant[i], a);
        pp_grant_rec_enc(&y->s.grant[i], b);
        if (memcmp(a, b, sizeof a) != 0) return false;
    }
    return true;
}

static bool same_sets(const pp_state_t *x, const pp_state_t *y)
{
    return x->n_earn == y->n_earn && x->n_spend == y->n_spend && x->n_grant == y->n_grant &&
           x->n_revoke == y->n_revoke && x->n_dev == y->n_dev &&
           memcmp(x->s.earn, y->s.earn, x->n_earn * sizeof(pp_earn_t)) == 0 &&
           memcmp(x->s.spend, y->s.spend, x->n_spend * sizeof(pp_spend_t)) == 0 &&
           same_grants(x, y) &&
           memcmp(x->s.revoke, y->s.revoke, x->n_revoke * sizeof(pp_revoke_t)) == 0 &&
           memcmp(x->s.dev, y->s.dev, x->n_dev * sizeof(id_cred_ref_t)) == 0;
}

static box_t E;

/* earn `earned`, enrol signers 1..4, add the spends, replay */
static void fund(box_t *b, uint64_t earned, const pp_spend_t *sp, uint32_t n)
{
    box_init(b, pa.root.did, CAP);
    if (earned) {
        pp_earn_t e = earn(0x99, earned);
        pp_state_add_earn(&b->st, &e, 0);
    }
    for (uint8_t k = 1; k <= 4; k++) {
        id_cred_ref_t d;
        memset(&d, k, sizeof d);
        pp_state_add_device(&b->st, &d, 0);
    }
    for (uint32_t i = 0; i < n; i++) pp_state_add_spend(&b->st, &sp[i], 0);
    pp_state_replay(&b->st);
}

static void test_state_edges(void)
{
    uint32_t n, add;
    box_init(&S, pa.root.did, CAP);
    fill3(&S);
    bool sorted = true;
    for (uint32_t i = 0; i < 3; i++)
        sorted = sorted && S.st.s.earn[i].receipt[0] == i + 1 && S.st.s.spend[i].signer[0] == i + 1 &&
                 S.st.s.grant[i].grant[0] == i + 1 && S.st.s.revoke[i].key[0] == i + 1 &&
                 S.st.s.dev[i].cred_id[0] == i + 1;
    CHECK(sorted, "entries added out of order are kept sorted in every set");
    CHECK(pp_state_encoded_size(&S.st) == 60 + 3 * (80 + 96 + 76 + 24 + 32),
          "encoded size: header plus every entry");
    CHECK(pp_state_encode(&S.st, work, 983, &n) == PP_ERR_SIZE &&
              pp_state_encode(&S.st, work, 984, &n) == PP_OK && n == 984,
          "the encoding fits exactly its size");
    CHECK(memcmp(work, "ZPS1\x01\0\0\0", 8) == 0 && memcmp(work + 8, pa.root.did, 32) == 0,
          "state header: magic, version, DID");
    bool counts = true;
    for (int i = 0; i < 5; i++) counts = counts && pp_get32(work + 40 + 4 * i) == 3;
    CHECK(counts, "state header: five counts");
    CHECK(work[60] == 1 && work[60 + 32] == 0x11 && pp_get64(work + 60 + 64) == 101 &&
              pp_get64(work + 60 + 72) == 1001 && work[300] == 1 && pp_get64(work + 316) == 11 &&
              pp_get64(work + 324) == 21 && pp_get64(work + 332) == 31 && work[380] == 0x61,
          "earn and spend entries at their offsets");
    CHECK(work[588] == 1 && work[604] == 0x21 && work[620] == 0x31 && pp_get32(work + 636) == 1 &&
              pp_get64(work + 640) == 501 && pp_get64(work + 648) == 601 &&
              pp_get64(work + 656) == 701,
          "grant entries at their offsets");
    CHECK(work[816] == 1 && pp_get64(work + 832) == 801 && work[888] == 1 && work[904] == 0x41 &&
              work[983] == 0x43,
          "revocation and device entries at their offsets");
    box_init(&E, pa.root.did, CAP);
    CHECK(pp_state_merge_encoded(&E.st, work, n, &add) == PP_OK && add == 15 && same_sets(&E.st, &S.st),
          "a merge restores every field of every entry");
    CHECK(pp_state_merge_encoded(&E.st, work, n, &add) == PP_OK && add == 0 && same_sets(&E.st, &S.st),
          "merging again adds nothing");

    /* exactly full, one short, set by set */
    uint32_t *caps[5] = {&E.st.s.earn_cap, &E.st.s.spend_cap, &E.st.s.grant_cap, &E.st.s.revoke_cap,
                         &E.st.s.dev_cap};
    uint32_t fits = 0, full = 0;
    for (int set = 0; set < 5; set++) {
        box_init(&E, pa.root.did, CAP);
        *caps[set] = 2;
        full += pp_state_merge_encoded(&E.st, work, n, &add) == PP_ERR_FULL && E.st.n_earn == 0 &&
                E.st.n_dev == 0;
        box_init(&E, pa.root.did, CAP);
        *caps[set] = 3;
        fits += pp_state_merge_encoded(&E.st, work, n, &add) == PP_OK && add == 15;
    }
    CHECK(full == 5 && fits == 5, "each set: one short is FULL, exactly its size fits");
    box_init(&E, pa.root.did, CAP);
    pp_earn_t e1 = mk_earn(1);
    pp_state_add_earn(&E.st, &e1, 0);
    E.st.s.earn_cap = 3;
    CHECK(pp_state_merge_encoded(&E.st, work, n, &add) == PP_OK && add == 14 && E.st.n_earn == 3,
          "what a store already holds needs no room");

    /* every set validated */
    static const uint32_t off[5] = {60, 300, 588, 816, 888}, sz[5] = {80, 96, 76, 24, 32};
    static const uint32_t zero_at[5] = {64, 16, 48, 0, 0}, zero_n[5] = {8, 8, 4, 16, 16};
    uint32_t bad = 0, dup = 0, swap = 0;
    uint8_t tmp[96];
    for (int set = 0; set < 5; set++) {
        pp_state_encode(&S.st, work, sizeof work, &n);
        memset(work + off[set] + sz[set] + zero_at[set], 0, zero_n[set]); /* the middle entry */
        box_init(&E, pa.root.did, CAP);
        bad += pp_state_merge_encoded(&E.st, work, n, &add) == PP_ERR_FORMAT && E.st.n_earn == 0;
        pp_state_encode(&S.st, work, sizeof work, &n);
        memcpy(work + off[set] + sz[set], work + off[set], sz[set]);
        dup += pp_state_merge_encoded(&E.st, work, n, &add) == PP_ERR_FORMAT;
        pp_state_encode(&S.st, work, sizeof work, &n);
        memcpy(tmp, work + off[set] + sz[set], sz[set]);
        memcpy(work + off[set] + sz[set], work + off[set] + 2 * sz[set], sz[set]);
        memcpy(work + off[set] + 2 * sz[set], tmp, sz[set]);
        swap += pp_state_merge_encoded(&E.st, work, n, &add) == PP_ERR_FORMAT;
    }
    CHECK(bad == 5, "an invalid entry in any set is refused");
    CHECK(dup == 5, "a repeated entry in any set is refused");
    CHECK(swap == 5, "entries out of order in any set are refused");
    pp_state_encode(&S.st, work, sizeof work, &n);
    memcpy(work + 816 + 24, work + 816, 16); /* two revocations of one key, rising last_seq */
    CHECK(pp_state_merge_encoded(&E.st, work, n, &add) == PP_ERR_FORMAT,
          "one revocation per key in an encoding");

    pp_state_encode(&S.st, work, sizeof work, &n);
    uint32_t hdr = 0;
    for (int i = 0; i < 8; i++) {
        work[i] ^= 0x40;
        hdr += pp_state_merge_encoded(&E.st, work, n, &add) == PP_ERR_FORMAT;
        work[i] ^= 0x40;
    }
    CHECK(hdr == 8, "every header byte is checked");
    uint32_t cnt = 0;
    for (int i = 0; i < 5; i++) {
        work[43 + 4 * i]++;
        cnt += pp_state_merge_encoded(&E.st, work, n, &add) == PP_ERR_FORMAT;
        work[43 + 4 * i]--;
    }
    CHECK(cnt == 5, "every count must match the length");
    box_init(&E, pa.root.did, CAP);
    pp_state_encode(&E.st, work2, sizeof work2, &n);
    CHECK(n == 60 && pp_state_merge_encoded(&E.st, work2, 60, &add) == PP_OK && add == 0 &&
              pp_state_merge_encoded(&E.st, work2, 59, &add) == PP_ERR_FORMAT,
          "an empty state is exactly its header");

    /* revocations merge to the lower last sequence */
    pp_state_encode(&S.st, work, sizeof work, &n);
    box_init(&E, pa.root.did, CAP);
    pp_revoke_t r1 = {{0}, 900};
    memset(r1.key, 1, 16);
    pp_state_add_revoke(&E.st, &r1, 0);
    CHECK(pp_state_merge_encoded(&E.st, work, n, &add) == PP_OK && add == 15 &&
              E.st.s.revoke[0].last_seq == 801,
          "a merge lowers a revocation's last sequence and counts it");
    r1.last_seq = 700;
    box_init(&E, pa.root.did, CAP);
    pp_state_add_revoke(&E.st, &r1, 0);
    CHECK(pp_state_merge_encoded(&E.st, work, n, &add) == PP_OK && add == 14 &&
              E.st.s.revoke[0].last_seq == 700 && E.st.n_revoke == 3,
          "a higher one changes nothing");

    /* sizes that cannot be encoded */
    box_init(&E, pa.root.did, CAP);
    E.st.n_earn = 53687089u;
    CHECK(pp_state_encoded_size(&E.st) == 60u + 80u * 53687089u, "the largest encodable state");
    E.st.n_earn = 53687090u;
    CHECK(pp_state_encoded_size(&E.st) == 0 && pp_capsule_size(&E.st) == 0 &&
              pp_state_encode(&E.st, work, sizeof work, &n) == PP_ERR_OVERFLOW &&
              pp_capsule_seal(&pa, &E.st, work2, blob, sizeof blob, &n) == PP_ERR_OVERFLOW,
          "one more cannot be encoded or sealed");
    E.st.n_earn = 0;
    uint32_t big[5] = {0, 0, 0, 0, 0}, set_ok = 0;
    static const uint32_t per[5] = {80, 96, 76, 24, 32};
    for (int set = 0; set < 5; set++) {
        uint32_t *nn[5] = {&E.st.n_earn, &E.st.n_spend, &E.st.n_grant, &E.st.n_revoke, &E.st.n_dev};
        big[set] = (UINT32_MAX - 120u) / per[set] + 1u;
        *nn[set] = big[set];
        set_ok += pp_state_encoded_size(&E.st) == 0;
        *nn[set] = 0;
    }
    CHECK(set_ok == 5, "every set counts toward the size");

    /* replay: contested groups */
    box_init(&E, pa.root.did, CAP);
    pp_earn_t c1 = earn(5, 1), c2 = earn(5, 2), c3 = earn(5, 3), c4 = earn(6, 40), c0 = earn(4, 9);
    pp_state_add_earn(&E.st, &c1, 0);
    pp_state_add_earn(&E.st, &c2, 0);
    pp_state_add_earn(&E.st, &c3, 0);
    pp_state_add_earn(&E.st, &c4, 0);
    pp_state_add_earn(&E.st, &c0, 0);
    pp_state_replay(&E.st);
    CHECK(E.st.bal.earned == 49 && E.st.s.earn_st[0] == PP_ST_COUNTED &&
              E.st.s.earn_st[1] == PP_ST_CONTESTED && E.st.s.earn_st[3] == PP_ST_CONTESTED &&
              E.st.s.earn_st[4] == PP_ST_COUNTED,
          "three contents of one receipt are all contested, its neighbours count");
    pp_earn_t m1 = earn(7, UINT64_MAX), m2 = earn(8, 1);
    pp_state_add_earn(&E.st, &m1, 0);
    CHECK(pp_state_replay(&E.st) == PP_ERR_OVERFLOW, "earnings past 2^64 overflow");
    box_init(&E, pa.root.did, CAP);
    pp_state_add_earn(&E.st, &m1, 0);
    CHECK(pp_state_replay(&E.st) == PP_OK && E.st.bal.earned == UINT64_MAX, "2^64 - 1 is fine");
    pp_state_add_earn(&E.st, &m2, 0);
    CHECK(pp_state_replay(&E.st) == PP_ERR_OVERFLOW, "and one more overflows");

    /* equivocation: groups by signer and sequence only */
    pp_spend_t q[6] = {mk_spend(1, 1, 1, 9), mk_spend(1, 1, 2, 4), mk_spend(1, 1, 3, 7),
                       mk_spend(1, 2, 4, 5), mk_spend(2, 1, 5, 6), mk_spend(2, 2, 6, 1)};
    fund(&E, 100, q, 6);
    CHECK(state_of(&E.st, &q[0]) == PP_ST_HELD && state_of(&E.st, &q[1]) == PP_ST_HELD &&
              state_of(&E.st, &q[2]) == PP_ST_HELD && E.st.bal.held == 9,
          "three spends on one slot are held; the largest (listed first) is kept back");
    CHECK(state_of(&E.st, &q[3]) == PP_ST_SETTLED && state_of(&E.st, &q[4]) == PP_ST_SETTLED &&
              state_of(&E.st, &q[5]) == PP_ST_SETTLED && E.st.bal.settled == 12 &&
              E.st.bal.available == 79,
          "the same signer's next sequence and another signer's same sequence are not equivocations");

    /* funding order */
    pp_spend_t f1[2] = {mk_spend(1, 1, 5, 6), mk_spend(2, 1, 3, 6)};
    fund(&E, 10, f1, 2);
    CHECK(state_of(&E.st, &f1[1]) == PP_ST_SETTLED && state_of(&E.st, &f1[0]) == PP_ST_QUARANTINED,
          "the earlier Lamport time is funded first");
    pp_spend_t f2[2] = {mk_spend(1, 1, 3, 6), mk_spend(2, 1, 3, 6)};
    fund(&E, 10, f2, 2);
    CHECK(state_of(&E.st, &f2[0]) == PP_ST_SETTLED && state_of(&E.st, &f2[1]) == PP_ST_QUARANTINED,
          "on equal time the lower signer is funded first");
    pp_spend_t f3[2] = {mk_spend(1, 1, 3, 6), mk_spend(1, 2, 3, 6)};
    fund(&E, 10, f3, 2);
    CHECK(state_of(&E.st, &f3[0]) == PP_ST_SETTLED && state_of(&E.st, &f3[1]) == PP_ST_QUARANTINED,
          "then the lower sequence");
    pp_spend_t f4[3] = {mk_spend(1, 1, 3, 4), mk_spend(2, 1, 2, 4), mk_spend(3, 1, 1, 4)};
    fund(&E, 8, f4, 3);
    CHECK(state_of(&E.st, &f4[2]) == PP_ST_SETTLED && state_of(&E.st, &f4[1]) == PP_ST_SETTLED &&
              state_of(&E.st, &f4[0]) == PP_ST_QUARANTINED && E.st.bal.available == 0 &&
              E.st.bal.quarantined == 4,
          "a spend listed first but latest in time waits for the others");
    pp_spend_t f5[1] = {mk_spend(1, 1, 1, 8)};
    fund(&E, 8, f5, 1);
    CHECK(state_of(&E.st, &f5[0]) == PP_ST_SETTLED && E.st.bal.available == 0,
          "a spend of exactly the balance settles");
    pp_spend_t f6[2] = {mk_spend(1, 1, 1, 3), mk_spend(9, 1, 1, 5)};
    fund(&E, 8, f6, 2);
    CHECK(E.st.bal.unknown == 5 && E.st.bal.available == 0 && E.st.bal.deficit == 0 &&
              state_of(&E.st, &f6[0]) == PP_ST_SETTLED,
          "kept back exactly the rest: no deficit, nothing left");

    /* overflows in every total */
    pp_spend_t o1[2] = {mk_spend(9, 1, 1, UINT64_MAX), mk_spend(9, 2, 2, 1)};
    fund(&E, 1, o1, 2);
    CHECK(pp_state_replay(&E.st) == PP_ERR_OVERFLOW, "unknown amounts overflow");
    pp_spend_t o2[3] = {mk_spend(1, 1, 1, UINT64_MAX), mk_spend(1, 1, 2, 1), mk_spend(9, 1, 1, 1)};
    fund(&E, 1, o2, 3);
    CHECK(pp_state_replay(&E.st) == PP_ERR_OVERFLOW, "held plus unknown overflows");
    pp_spend_t o3[2] = {mk_spend(1, 1, 1, UINT64_MAX), mk_spend(1, 2, 2, 1)};
    fund(&E, 0, o3, 2);
    CHECK(pp_state_replay(&E.st) == PP_ERR_OVERFLOW, "quarantined amounts overflow");
    pp_revoke_t rv = {{0}, 0};
    memset(rv.key, 1, 16);
    pp_state_add_revoke(&E.st, &rv, 0);
    CHECK(pp_state_replay(&E.st) == PP_ERR_OVERFLOW, "refused amounts overflow");
    pp_spend_t o4[4] = {mk_spend(1, 1, 1, UINT64_MAX), mk_spend(1, 1, 2, 1),
                        mk_spend(2, 1, 1, UINT64_MAX), mk_spend(2, 1, 2, 1)};
    fund(&E, 0, o4, 4);
    CHECK(pp_state_replay(&E.st) == PP_ERR_OVERFLOW, "held amounts overflow");

    /* grants: authority, window, cap */
    pp_grant_rec_t g = mk_grant(1);
    g.rights = PP_RIGHT_SPEND;
    g.cap = 5;
    memset(g.session, 4, 16);
    pp_spend_t w[4] = {mk_spend(4, 1, 1, 2), mk_spend(4, 2, 2, 2), mk_spend(4, 3, 3, 1),
                       mk_spend(4, 4, 4, 1)};
    for (int i = 0; i < 4; i++) memset(w[i].grant, 1, 16);
    w[0].t_ms = g.issued_ms;
    w[1].t_ms = g.expires_ms;
    w[2].t_ms = g.issued_ms + 1;
    w[3].t_ms = g.issued_ms + 1;
    box_init(&E, pa.root.did, CAP);
    pp_earn_t big100 = earn(0x99, 100);
    pp_state_add_earn(&E.st, &big100, 0);
    pp_state_add_grant(&E.st, &g, 0);
    for (int i = 0; i < 4; i++) pp_state_add_spend(&E.st, &w[i], 0);
    pp_state_replay(&E.st);
    pp_state_replay(&E.st);
    CHECK(state_of(&E.st, &w[0]) == PP_ST_SETTLED && state_of(&E.st, &w[1]) == PP_ST_SETTLED &&
              state_of(&E.st, &w[2]) == PP_ST_SETTLED && state_of(&E.st, &w[3]) == PP_ST_REFUSED &&
              E.st.s.grant_used[0] == 5 && E.st.bal.refused == 1,
          "a grant's window includes both ends; its cap is exact; replay twice agrees");
    pp_grant_rec_t g2 = g;
    g2.cap = 6;
    pp_state_add_grant(&E.st, &g2, 0);
    pp_state_replay(&E.st);
    CHECK(state_of(&E.st, &w[0]) == PP_ST_REFUSED && E.st.bal.settled == 0,
          "two spend grants under one id with different caps: refused");
    pp_grant_rec_t g3 = mk_grant(2); /* sorts after: the id at the end stays unique */
    box_init(&E, pa.root.did, CAP);
    pp_state_add_earn(&E.st, &big100, 0);
    pp_state_add_grant(&E.st, &g, 0);
    pp_state_add_grant(&E.st, &g3, 0);
    pp_spend_t ov[2] = {mk_spend(4, 1, 1, UINT64_MAX), mk_spend(4, 2, 2, UINT64_MAX)};
    memset(ov[0].grant, 1, 16);
    memset(ov[1].grant, 1, 16);
    ov[0].t_ms = ov[1].t_ms = g.issued_ms;
    pp_state_add_spend(&E.st, &ov[0], 0);
    CHECK(pp_state_replay(&E.st) == PP_OK && state_of(&E.st, &ov[0]) == PP_ST_REFUSED,
          "the first grant's cap holds");
    pp_state_add_spend(&E.st, &ov[1], 0);
    CHECK(pp_state_replay(&E.st) == PP_ERR_OVERFLOW, "spends over a grant cap overflow the refused total");

    /* new spends */
    pp_spend_t sx, s9[1] = {mk_spend(2, 9, 7, 1)};
    fund(&E, 20, s9, 1);
    uint8_t payee[32] = {0}, me[16];
    memset(me, 1, 16);
    CHECK(pp_state_spend(&E.st, me, 19, payee, 1, &sx) == PP_OK && sx.seq == 1 && sx.lamport == 8,
          "a new spend: its own next sequence, the highest time seen plus one");
    CHECK(pp_state_spend(&E.st, me, 1, payee, 1, &sx) == PP_ERR_FUNDS && E.st.bal.available == 0,
          "nothing left");
    pp_spend_t s10[1] = {mk_spend(1, UINT64_MAX, 7, 1)};
    fund(&E, 20, s10, 1);
    CHECK(pp_state_spend(&E.st, me, 1, payee, 1, &sx) == PP_ERR_OVERFLOW, "sequence exhausted");
    pp_spend_t s11[1] = {mk_spend(2, 1, UINT64_MAX, 1)};
    fund(&E, 20, s11, 1);
    CHECK(pp_state_spend(&E.st, me, 1, payee, 1, &sx) == PP_ERR_OVERFLOW, "Lamport time exhausted");
    pp_spend_t s12[2] = {mk_spend(2, 1, 1, 1), mk_spend(3, 1, 1, 1)};
    fund(&E, 20, s12, 2);
    E.st.s.spend_cap = 2;
    CHECK(pp_state_spend(&E.st, me, 1, payee, 1, &sx) == PP_ERR_FULL && E.st.n_spend == 2,
          "no room for a new spend");
    pp_spend_t s13[1] = {mk_spend(9, 1, 1, UINT64_MAX)};
    fund(&E, 20, s13, 1);
    pp_spend_t s14 = mk_spend(9, 2, 2, 1);
    pp_state_add_spend(&E.st, &s14, 0);
    CHECK(pp_state_spend(&E.st, me, 1, payee, 1, &sx) == PP_ERR_OVERFLOW,
          "a replay that overflows stops the spend");
}

/* ---- capsule, fetch and device-chain edges ---- */
static int big_fetch(void *ctx, const uint8_t lookup[32], uint32_t index, uint8_t *buf, uint32_t cap,
                     uint32_t *len)
{
    (void) ctx;
    (void) lookup;
    (void) buf;
    if (index > 0) return -1;
    *len = cap + 1;
    return 0;
}

static void test_capsule_edges(void)
{
    uint8_t salt[32];
    uint32_t n, add, m, k;
    memset(salt, 0x5C, 32);
    uint32_t need = pp_capsule_size(&A.st);
    CHECK(pp_capsule_seal(&pa, &A.st, salt, blob, need - 1, &n) == PP_ERR_SIZE &&
              pp_capsule_seal(&pa, &A.st, salt, blob, need, &n) == PP_OK && n == need,
          "a capsule fits exactly its size");
    CHECK(memcmp(blob, "ZPC1\x01\0\0\0", 8) == 0 && memcmp(blob + 8, salt, 32) == 0 &&
              pp_get32(blob + 40) == need - 60,
          "capsule header: magic, version, salt, length");
    uint8_t key[32], nonce[12] = {0};
    pp_shake(key, 32, "zxv.passport.capsule.v1", pa.vault_key, 32, salt, 32, 0, 0);
    uint32_t sn = need - 60;
    CHECK(aead_open(key, nonce, blob, 44, blob + 44, work2, sn, blob + 44 + sn) &&
              pp_state_encode(&A.st, work, sizeof work, &n) == PP_OK && n == sn &&
              memcmp(work, work2, sn) == 0,
          "the capsule is the state under the documented key, nonce and header");
    box_init(&S, pa.root.did, CAP);
    uint32_t fmt = 0;
    for (int i = 0; i < 8; i++) {
        blob[i] ^= 0x20;
        fmt += pp_capsule_open(&pb, blob, need, work, sizeof work, &S.st, &add) == PP_ERR_FORMAT;
        blob[i] ^= 0x20;
    }
    blob[43]++;
    fmt += pp_capsule_open(&pb, blob, need, work, sizeof work, &S.st, &add) == PP_ERR_FORMAT;
    blob[43]--;
    CHECK(fmt == 9, "every header byte and the length are checked");
    CHECK(pp_capsule_open(&pb, blob, need, work, sn - 1, &S.st, &add) == PP_ERR_SIZE &&
              pp_capsule_open(&pb, blob, need, work, sn, &S.st, &add) == PP_OK && add > 0,
          "the work buffer needs exactly the state's size");
    CHECK(pp_is_zero(work, sn), "and is wiped after");
    uint8_t empty[60];
    memcpy(empty, "ZPC1\x01\0\0\0", 8);
    memset(empty + 8, 0, 52);
    CHECK(pp_capsule_open(&pb, empty, 60, work, sizeof work, &S.st, &add) == PP_ERR_AUTH &&
              pp_capsule_open(&pb, empty, 59, work, sizeof work, &S.st, &add) == PP_ERR_FORMAT,
          "the shortest capsule is a header and a tag");

    /* fetch */
    n_swarm = 0;
    pp_capsule_publish(&pa, &host, &A.st, 30000, blob, sizeof blob);
    pp_capsule_publish(&pa, &host, &S.st, 30000, blob, sizeof blob);
    box_init(&S, pa.root.did, CAP);
    CHECK(pp_capsule_fetch(&pb, &host, 30000, blob, sizeof blob, work, sizeof work, &S.st, &m, &k) ==
                  PP_OK &&
              m == 2 && k == 0,
          "every capsule under the topic is merged");
    box_init(&S, pa.root.did, 1);
    CHECK(pp_capsule_fetch(&pb, &host, 30000, blob, sizeof blob, work, sizeof work, &S.st, &m, &k) ==
                  PP_ERR_FULL,
          "a store too small stops the fetch");
    box_init(&S, pa.root.did, CAP);
    CHECK(pp_capsule_fetch(&pb, &host, 30000, blob, sizeof blob, work, 10, &S.st, &m, &k) ==
              PP_ERR_SIZE,
          "so does a work buffer too small");
    id_host_t liar = host;
    liar.fetch = big_fetch;
    CHECK(pp_capsule_fetch(&pb, &liar, 30000, blob, 100, work, sizeof work, &S.st, &m, &k) ==
              PP_ERR_SIZE,
          "a host claiming more than the buffer is refused");

    /* the device chain */
    id_cred_status_t st2 = status;
    st2.epoch_ms++;
    uint8_t payee[32] = {0}, sig[ID_MLDSA65_SIG];
    pp_spend_t sp = mk_spend(1, 1, 1, 1);
    memcpy(sp.signer, phone.cred_id, 16);
    pp_voucher_sign(phone.sk, pa.root.did, &sp, 0, sig);
    CHECK(pp_verify_device_spend(&pa.root.pk, &status, &cert_phone, phone.pk, &sp, sig) == PP_OK,
          "a good voucher");
    CHECK(pp_verify_device_spend(&pa.root.pk, &st2, &cert_phone, phone.pk, &sp, sig) == PP_ERR_AUTH,
          "a changed status list fails");
    id_dev_cert_t c2 = cert_phone;
    c2.issued_ms++;
    CHECK(pp_verify_device_spend(&pa.root.pk, &status, &c2, phone.pk, &sp, sig) == PP_ERR_AUTH,
          "a changed certificate fails");
    (void) payee;

    /* the status list from the device set */
    box_init(&S, pa.root.did, CAP);
    for (uint8_t i = 1; i <= ID_MAX_CREDS; i++) {
        id_cred_ref_t d;
        memset(&d, i, sizeof d);
        pp_state_add_device(&S.st, &d, 0);
    }
    id_cred_status_t cs;
    CHECK(pp_status_rebuild(&pa, &host, &S.st, 1, &cs) == PP_OK && cs.n == ID_MAX_CREDS,
          "sixteen devices fill the status list");
    id_cred_ref_t d17;
    memset(&d17, 17, sizeof d17);
    pp_state_add_device(&S.st, &d17, 0);
    CHECK(pp_status_rebuild(&pa, &host, &S.st, 1, &cs) == PP_ERR_FULL, "seventeen do not fit");
    box_init(&S, pa.root.did, CAP);
    id_cred_ref_t t1, t2;
    memset(&t1, 1, sizeof t1);
    t2 = t1;
    t2.dev_id[0] = 2;
    pp_state_add_device(&S.st, &t1, 0);
    pp_state_add_device(&S.st, &t2, 0);
    CHECK(pp_status_rebuild(&pa, &host, &S.st, 1, &cs) == PP_ERR_FORMAT,
          "one credential on two devices is refused");
}

/* ---- café edges ---- */
static pp_cafe_t cafe2;

static void seal_report(const uint8_t rk[32], const uint8_t grant[16], uint64_t seq,
                        const pp_spend_t *sp, uint32_t n, uint8_t *out, uint32_t *len)
{
    uint8_t nonce[12] = {0};
    memcpy(out, "ZPR1\x01\0\0\0", 8);
    memcpy(out + 8, grant, 16);
    pp_put64(out + 24, seq);
    pp_put32(out + 32, n);
    for (uint32_t i = 0; i < n; i++) pp_spend_encode(&sp[i], out + 36 + 96 * i);
    pp_put64(nonce + 4, seq);
    aead_seal(rk, nonce, out, 36, out + 36, out + 36, 96 * n, out + 36 + 96 * n);
    *len = 36 + 96 * n + 16;
}

static void refp(uint8_t *req, uint32_t len, uint8_t fp[32])
{
    sha3_256(req, len, fp);
}

static void test_cafe_edges(void)
{
    static uint8_t req[PP_REQUEST_MAX], req2[PP_REQUEST_MAX];
    static uint8_t env[16384], view[16384];
    uint8_t r[32], fp[32], payee[32], h[32];
    uint32_t len, len2, n, env_len, add;
    memset(r, 0x33, 32);
    memset(payee, 0xEE, 32);
    CHECK(pp_cafe_begin(&cafe2, r, PP_RIGHT_VIEW | PP_RIGHT_SPEND, 7, 1000, req, sizeof req, &len) ==
              PP_OK,
          "a second café session");
    CHECK(pp_cafe_begin(&cafe2, r, PP_RIGHT_VIEW | PP_RIGHT_SPEND, 7, 1000, req2, len - 1, &len2) ==
                  PP_ERR_SIZE &&
              pp_cafe_begin(&cafe2, r, PP_RIGHT_VIEW | PP_RIGHT_SPEND, 7, 1000, req2, len, &len2) ==
                  PP_OK &&
              len2 == len && memcmp(req, req2, len) == 0,
          "the request fits exactly; the same random gives the same request");
    sha3_256(cafe2.sig_pk, ID_MLDSA65_PK, h);
    refp(req, len, fp);
    CHECK(memcmp(req, "ZPQ1\x01\0\0\0", 8) == 0 && pp_get32(req + 8) == 3 &&
              pp_get64(req + 12) == 7 && pp_get64(req + 20) == 1000 &&
              memcmp(req + 28, cafe2.sig_pk, ID_MLDSA65_PK) == 0 &&
              pp_get32(req + 1980) == len - 1984 && memcmp(cafe2.id, h, 16) == 0 &&
              memcmp(cafe2.fp, fp, 32) == 0 && cafe2.live,
          "request layout, session id and fingerprint");
    pp_request_t q;
    CHECK(pp_request_parse(req, len, cafe2.fp, &q) == PP_OK && q.want_ms == 1000 && q.cap == 7 &&
              memcmp(q.session_pk_hash, h, 32) == 0 && q.kem_pk == req + 1984 &&
              q.kem_pk_len == len - 1984 && memcmp(q.fp, fp, 32) == 0,
          "the phone reads every field");

    /* requests refused even with a matching fingerprint */
    uint32_t refused = 0;
    pp_request_t bad;
    memcpy(req2, req, len);
    for (int i = 0; i < 8; i++) {
        req2[i] ^= 0x10;
        refp(req2, len, fp);
        refused += pp_request_parse(req2, len, fp, &bad) == PP_ERR_FORMAT;
        req2[i] ^= 0x10;
    }
    CHECK(refused == 8, "every request header byte is checked");
    refused = 0;
    pp_put32(req2 + 8, 0);
    refp(req2, len, fp);
    refused += pp_request_parse(req2, len, fp, &bad) == PP_ERR_FORMAT;
    pp_put32(req2 + 8, 8);
    refp(req2, len, fp);
    refused += pp_request_parse(req2, len, fp, &bad) == PP_ERR_FORMAT;
    pp_put32(req2 + 8, 3);
    pp_put64(req2 + 20, 0);
    refp(req2, len, fp);
    refused += pp_request_parse(req2, len, fp, &bad) == PP_ERR_FORMAT;
    pp_put64(req2 + 20, 1000);
    pp_put32(req2 + 1980, len - 1983);
    refp(req2, len, fp);
    refused += pp_request_parse(req2, len, fp, &bad) == PP_ERR_FORMAT;
    pp_put32(req2 + 1980, len - 1984);
    req2[1984] ^= 0xFF;
    refp(req2, len, fp);
    refused += pp_request_parse(req2, len, fp, &bad) == PP_ERR_FORMAT;
    req2[1984] ^= 0xFF;
    refp(req2, 1983, fp);
    refused += pp_request_parse(req2, 1983, fp, &bad) == PP_ERR_FORMAT;
    CHECK(refused == 6, "no rights, unknown rights, no duration, wrong key length, a bad key, short");
    refp(req2, len, fp);
    CHECK(pp_request_parse(req2, len, fp, &bad) == PP_OK, "the restored request parses");

    /* grants */
    pp_grant_t g, gq;
    box_init(&S, pa.root.did, CAP);
    CHECK(pp_grant_issue(&pa, &phone, &q, PP_RIGHT_VIEW, 0, 1, 0, &S.st, &g) == PP_ERR_NOTFOUND,
          "a device not in the state cannot approve");
    CHECK(pp_grant_issue(&pa, &phone, &q, PP_RIGHT_QUERY, 0, 1, 0, &A.st, &g) == PP_ERR_RIGHTS,
          "only rights the café asked for");
    CHECK(pp_grant_issue(&pa, &phone, &q, PP_RIGHT_VIEW, 0, UINT64_MAX - 999, 0, &A.st, &g) ==
                  PP_ERR_OVERFLOW &&
              pp_grant_issue(&pa, &phone, &q, PP_RIGHT_VIEW, 0, UINT64_MAX - 1000, 0, &A.st, &gq) ==
                  PP_OK &&
              gq.expires_ms == UINT64_MAX,
          "the end of the window may not pass 2^64");
    uint32_t keep = A.st.s.grant_cap;
    A.st.s.grant_cap = A.st.n_grant;
    CHECK(pp_grant_issue(&pa, &phone, &q, PP_RIGHT_VIEW, 0, 1, 0, &A.st, &g) == PP_ERR_FULL,
          "no room to record the grant");
    A.st.s.grant_cap = keep;
    uint64_t lam = 0;
    for (uint32_t i = 0; i < A.st.n_spend; i++)
        if (A.st.s.spend[i].lamport > lam) lam = A.st.s.spend[i].lamport;
    uint64_t now = 200000;
    CHECK(pp_grant_issue(&pa, &phone, &q, PP_RIGHT_VIEW | PP_RIGHT_SPEND, 7, now, 0, &A.st, &g) ==
                  PP_OK &&
              g.expires_ms == now + 1000 && g.lamport == lam && lam > 0 && g.cap == 7,
          "the full cap, the asked duration, the highest Lamport time");
    uint8_t body[172], gh[32];
    memcpy(body, "ZPG1\x01\0\0\0", 8);
    memcpy(body + 8, pa.root.did, 32);
    memcpy(body + 40, phone.cred_id, 16);
    memcpy(body + 56, q.session, 16);
    memcpy(body + 72, q.session_pk_hash, 32);
    memcpy(body + 104, q.fp, 32);
    pp_put32(body + 136, PP_RIGHT_VIEW | PP_RIGHT_SPEND);
    pp_put64(body + 140, 7);
    pp_put64(body + 148, now);
    pp_put64(body + 156, now + 1000);
    pp_put64(body + 164, lam);
    sha3_256(body, 172, gh);
    CHECK(memcmp(gh, g.grant, 16) == 0 &&
              pq_mldsa65_verify(phone.pk, body, 172, (const uint8_t *) PP_CTX_GRANT,
                                sizeof PP_CTX_GRANT - 1, g.sig),
          "the grant is the documented 172 bytes, signed under its context");
    pp_grant_t gx = g;
    gx.grant[15] ^= 1;
    CHECK(!pp_grant_verify(&gx, phone.pk), "a changed grant id fails even with the signature kept");

    /* envelope */
    uint32_t vn = 100;
    memset(view, 0x77, vn);
    CHECK(pp_envelope_seal(&pa, &g, &q, view, vn, r, env, sizeof env, &env_len) == PP_OK,
          "seal a 100-byte view");
    uint32_t ct_len = pp_get32(env + 24);
    CHECK(env_len == 32 + ct_len + 32 + vn + 16 && memcmp(env, "ZPE1\x01\0\0\0", 8) == 0 &&
              memcmp(env + 8, g.grant, 16) == 0 && pp_get32(env + 28) == 32 + vn,
          "envelope layout");
    CHECK(pp_envelope_seal(&pa, &g, &q, view, vn, r, blob, env_len - 1, &n) == PP_ERR_SIZE &&
              pp_envelope_seal(&pa, &g, &q, view, vn, r, blob, env_len, &n) == PP_OK && n == env_len,
          "the envelope fits exactly");
    uint32_t mm = 0;
    gx = g;
    gx.session[0] ^= 1;
    mm += pp_envelope_seal(&pa, &gx, &q, view, vn, r, blob, sizeof blob, &n) == PP_ERR_MISMATCH;
    gx = g;
    gx.request_fp[0] ^= 1;
    mm += pp_envelope_seal(&pa, &gx, &q, view, vn, r, blob, sizeof blob, &n) == PP_ERR_MISMATCH;
    mm += pp_envelope_seal(&px, &g, &q, view, vn, r, blob, sizeof blob, &n) == PP_ERR_MISMATCH;
    CHECK(mm == 3, "the envelope matches session, request and account");
    pp_grant_t gv = g;
    gv.rights = PP_RIGHT_SPEND;
    CHECK(pp_envelope_seal(&pa, &gv, &q, 0, 0, r, blob, sizeof blob, &n) == PP_OK &&
              n == 32 + ct_len + 32 + 16,
          "without the view right the envelope carries only the report key");
    pp_request_t qg = q;
    static uint8_t junk[4096];
    memset(junk, 0xFF, sizeof junk);
    qg.kem_pk = junk;
    CHECK(pp_envelope_seal(&pa, &g, &qg, view, vn, r, blob, sizeof blob, &n) == PP_ERR_FORMAT,
          "a bad session KEM key is refused");

    /* accept */
    uint32_t got;
    static pp_cafe_t c3;
    mm = 0;
    gx = g;
    gx.session_pk_hash[0] ^= 1;
    c3 = cafe2;
    mm += pp_cafe_accept(&c3, &gx, env, env_len, view, sizeof view, &got) == PP_ERR_MISMATCH;
    gx = g;
    gx.request_fp[0] ^= 1;
    mm += pp_cafe_accept(&c3, &gx, env, env_len, view, sizeof view, &got) == PP_ERR_MISMATCH;
    gx = g;
    gx.session[0] ^= 1;
    mm += pp_cafe_accept(&c3, &gx, env, env_len, view, sizeof view, &got) == PP_ERR_MISMATCH;
    env[8] ^= 1;
    mm += pp_cafe_accept(&c3, &g, env, env_len, view, sizeof view, &got) == PP_ERR_MISMATCH;
    env[8] ^= 1;
    CHECK(mm == 4, "the café takes only its own grant and envelope");
    uint32_t ff = 0;
    for (int i = 0; i < 8; i++) {
        env[i] ^= 0x10;
        ff += pp_cafe_accept(&c3, &g, env, env_len, view, sizeof view, &got) == PP_ERR_FORMAT;
        env[i] ^= 0x10;
    }
    ff += pp_cafe_accept(&c3, &g, env, 31, view, sizeof view, &got) == PP_ERR_FORMAT;
    ff += pp_cafe_accept(&c3, &g, env, env_len - 1, view, sizeof view, &got) == PP_ERR_FORMAT;
    pp_put32(env + 24, ct_len + 1);
    ff += pp_cafe_accept(&c3, &g, env, env_len, view, sizeof view, &got) == PP_ERR_FORMAT;
    pp_put32(env + 24, ct_len);
    pp_put32(env + 28, 31);
    ff += pp_cafe_accept(&c3, &g, env, env_len, view, sizeof view, &got) == PP_ERR_FORMAT;
    pp_put32(env + 28, 32 + vn);
    CHECK(ff == 12, "every envelope header field is checked");
    CHECK(pp_cafe_accept(&c3, &g, env, env_len, view, 32 + vn - 1, &got) == PP_ERR_SIZE,
          "the view buffer holds the key and the view");
    c3.kem_sk.mlkem_dk[5] ^= 1;
    CHECK(pp_cafe_accept(&c3, &g, env, env_len, view, sizeof view, &got) == PP_ERR_AUTH &&
              !c3.granted,
          "another KEM key derives another envelope key");
    memset(view, 0x99, sizeof view);
    CHECK(pp_cafe_accept(&cafe2, &g, env, env_len, view, 32 + vn, &got) == PP_OK && got == vn &&
              view[0] == 0x77 && view[vn - 1] == 0x77 && pp_is_zero(view + vn, 32) &&
              view[vn + 32] == 0x99 && cafe2.granted,
          "the view moves to the front; the key's old place is wiped");
    uint8_t rk[32];
    pp_report_key(&pa, g.grant, rk);
    CHECK(memcmp(rk, cafe2.report_key, 32) == 0, "the report key arrived");

    /* spending at the boundaries */
    pp_spend_t s1, s2, sx;
    uint8_t sig1[ID_MLDSA65_SIG], sig2[ID_MLDSA65_SIG], sigx[ID_MLDSA65_SIG];
    CHECK(pp_cafe_spend(&cafe2, 3, payee, now, 0, &s1, sig1) == PP_OK && s1.seq == 1 &&
              s1.lamport == lam + 1 && s1.t_ms == now && memcmp(s1.grant, g.grant, 16) == 0 &&
              memcmp(s1.signer, cafe2.id, 16) == 0 && memcmp(s1.payee, payee, 32) == 0,
          "a spend at the first moment of the grant");
    CHECK(pp_cafe_spend(&cafe2, 4, payee, now + 1000, 0, &s2, sig2) == PP_OK && s2.seq == 2 &&
              s2.lamport == lam + 2 && cafe2.spent == 7,
          "and at the last, up to the whole cap");
    CHECK(pp_cafe_spend(&cafe2, 1, payee, now + 5, 0, &sx, sigx) == PP_ERR_CAP, "the cap is spent");
    pp_status_rebuild(&pa, &host, &A.st, 9700, &status);
    CHECK(pp_verify_cafe_spend(&pa.root.pk, &status, &cert_phone, phone.pk, &g, cafe2.sig_pk, &s2,
                               sig2, now) == PP_OK &&
              pp_verify_cafe_spend(&pa.root.pk, &status, &cert_phone, phone.pk, &g, cafe2.sig_pk,
                                   &s2, sig2, now + 1000) == PP_OK,
          "a peer accepts a spend of the cap at both ends of the window");
    CHECK(pp_verify_cafe_spend(&pa.root.pk, &status, &cert_phone, phone.pk, &g, cafe2.sig_pk, &s2,
                               sig2, now - 1) == PP_ERR_EXPIRED,
          "not before the window");
    pp_spend_t st_ = s1;
    st_.t_ms = now - 1;
    uint32_t ex = pp_verify_cafe_spend(&pa.root.pk, &status, &cert_phone, phone.pk, &g, cafe2.sig_pk,
                                       &st_, sig1, now) == PP_ERR_EXPIRED;
    st_.t_ms = now + 1001;
    ex += pp_verify_cafe_spend(&pa.root.pk, &status, &cert_phone, phone.pk, &g, cafe2.sig_pk, &st_,
                               sig1, now) == PP_ERR_EXPIRED;
    CHECK(ex == 2, "a voucher stamped outside the window is refused");
    gx = g;
    memset(gx.root_did, 0x42, 32);
    CHECK(pp_verify_cafe_spend(&pa.root.pk, &status, &cert_phone, phone.pk, &gx, cafe2.sig_pk, &s1,
                               sig1, now) == PP_ERR_MISMATCH,
          "a grant for another account");
    st_ = s1;
    st_.grant[0] ^= 1;
    CHECK(pp_verify_cafe_spend(&pa.root.pk, &status, &cert_phone, phone.pk, &g, cafe2.sig_pk, &st_,
                               sig1, now) == PP_ERR_MISMATCH,
          "a voucher under another grant");
    st_ = s1;
    st_.signer[0] ^= 1;
    CHECK(pp_verify_cafe_spend(&pa.root.pk, &status, &cert_phone, phone.pk, &g, cafe2.sig_pk, &st_,
                               sig1, now) == PP_ERR_MISMATCH,
          "a voucher from another session");
    CHECK(pp_verify_cafe_spend(&pa.root.pk, &status, &cert_laptop, laptop.pk, &g, cafe2.sig_pk, &s1,
                               sig1, now) == PP_ERR_MISMATCH,
          "a grant from another device's chain");
    CHECK(pp_verify_cafe_spend(&pa.root.pk, &status, &cert_phone, phone.pk, &gq, cafe2.sig_pk, &s1,
                               sig1, now) == PP_ERR_MISMATCH,
          "a voucher shown under the wrong grant");
    /* a validly signed view-only grant */
    pp_request_t qv = q;
    pp_grant_t gview;
    pp_grant_issue(&pa, &phone, &qv, PP_RIGHT_VIEW, 0, now, 0, &A.st, &gview);
    st_ = s1;
    memcpy(st_.grant, gview.grant, 16);
    CHECK(pp_verify_cafe_spend(&pa.root.pk, &status, &cert_phone, phone.pk, &gview, cafe2.sig_pk,
                               &st_, sig1, now) == PP_ERR_RIGHTS,
          "a view-only grant spends nothing");
    static pp_cafe_t cv;
    pp_cafe_begin(&cv, r, PP_RIGHT_VIEW | PP_RIGHT_SPEND, 7, 1000, req2, sizeof req2, &len2);
    pp_envelope_seal(&pa, &gview, &q, 0, 0, r, blob, sizeof blob, &n);
    CHECK(pp_cafe_accept(&cv, &gview, blob, n, view, sizeof view, &got) == PP_OK && got == 0 &&
              pp_cafe_spend(&cv, 1, payee, now, 0, &sx, sigx) == PP_ERR_RIGHTS,
          "and the café refuses to spend under it");
    pp_cafe_end(&cv);

    /* reports */
    pp_spend_t two[2] = {s1, s2};
    uint8_t rb[1024], pt[512];
    uint32_t rl;
    CHECK(pp_cafe_report(&cafe2, two, 2, rb, 36 + 192 + 16 - 1, &rl) == PP_ERR_SIZE &&
              pp_cafe_report(&cafe2, two, 2, rb, 36 + 192 + 16, &rl) == PP_OK && rl == 244,
          "the report fits exactly");
    uint8_t nonce[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    CHECK(memcmp(rb, "ZPR1\x01\0\0\0", 8) == 0 && memcmp(rb + 8, g.grant, 16) == 0 &&
              pp_get64(rb + 24) == 1 && pp_get32(rb + 32) == 2 &&
              aead_open(rk, nonce, rb, 36, rb + 36, pt, 192, rb + 36 + 192),
          "report layout, nonce 0000 || sequence, header as AAD");
    uint8_t e0[96];
    pp_spend_encode(&s1, e0);
    CHECK(memcmp(pt, e0, 96) == 0, "the spends in order");
    CHECK(pp_cafe_report(&cafe2, two, 2, rb, sizeof rb, &rl) == PP_OK && pp_get64(rb + 24) == 2,
          "each report has the next sequence");
    pp_spend_t wrong = s1;
    wrong.signer[0] ^= 1;
    pp_spend_t two_w[2] = {s1, wrong};
    CHECK(pp_cafe_report(&cafe2, two_w, 2, blob, sizeof blob, &n) == PP_ERR_MISMATCH,
          "the café reports only its own spends");
    wrong = s1;
    wrong.grant[0] ^= 1;
    two_w[1] = wrong;
    CHECK(pp_cafe_report(&cafe2, two_w, 2, blob, sizeof blob, &n) == PP_ERR_MISMATCH,
          "under its own grant");

    /* the phone opens it */
    box_init(&S, px.root.did, CAP);
    CHECK(pp_report_open(&pa, &g, rb, rl, work, sizeof work, &S.st, &add) == PP_ERR_MISMATCH,
          "a report into another account's state");
    box_init(&S, pa.root.did, CAP);
    uint32_t rf = 0;
    for (int i = 0; i < 8; i++) {
        rb[i] ^= 0x10;
        rf += pp_report_open(&pa, &g, rb, rl, work, sizeof work, &S.st, &add) == PP_ERR_FORMAT;
        rb[i] ^= 0x10;
    }
    rb[35]++;
    rf += pp_report_open(&pa, &g, rb, rl, work, sizeof work, &S.st, &add) == PP_ERR_FORMAT;
    rb[35]--;
    rf += pp_report_open(&pa, &g, rb, 51, work, sizeof work, &S.st, &add) == PP_ERR_FORMAT;
    CHECK(rf == 10, "every report header field is checked");
    CHECK(pp_report_open(&pa, &g, rb, rl, work, 191, &S.st, &add) == PP_ERR_SIZE,
          "the work buffer holds every spend");
    S.st.s.spend_cap = 1;
    CHECK(pp_report_open(&pa, &g, rb, rl, work, 192, &S.st, &add) == PP_ERR_FULL &&
              S.st.n_spend == 0 && S.st.n_grant == 0,
          "no room for the spends: nothing changes");
    box_init(&S, pa.root.did, CAP);
    S.st.s.grant_cap = 0;
    CHECK(pp_report_open(&pa, &g, rb, rl, work, 192, &S.st, &add) == PP_ERR_FULL && S.st.n_spend == 0,
          "no room for the grant: nothing changes");
    box_init(&S, pa.root.did, CAP);
    S.st.s.spend_cap = 2;
    S.st.s.grant_cap = 1;
    CHECK(pp_report_open(&pa, &g, rb, rl, work, 192, &S.st, &add) == PP_OK && add == 2 &&
              S.st.n_grant == 1 && S.st.n_spend == 2 && pp_is_zero(work, 192),
          "exactly enough room: the grant and both spends, work wiped");
    CHECK(pp_report_open(&pa, &g, rb, rl, work, 192, &S.st, &add) == PP_OK && add == 0 &&
              S.st.n_grant == 1,
          "again: the grant is not recorded twice");
    uint32_t forged = 0;
    pp_spend_t f = s1;
    f.signer[0] ^= 1;
    seal_report(rk, g.grant, 9, &f, 1, blob, &n);
    box_init(&S, pa.root.did, CAP);
    forged += pp_report_open(&pa, &g, blob, n, work, sizeof work, &S.st, &add) == PP_ERR_MISMATCH;
    f = s1;
    f.grant[0] ^= 1;
    seal_report(rk, g.grant, 9, &f, 1, blob, &n);
    forged += pp_report_open(&pa, &g, blob, n, work, sizeof work, &S.st, &add) == PP_ERR_MISMATCH;
    f = s1;
    f.seq = 0;
    seal_report(rk, g.grant, 9, &f, 1, blob, &n);
    forged += pp_report_open(&pa, &g, blob, n, work, sizeof work, &S.st, &add) == PP_ERR_FORMAT;
    f = s1;
    f.lamport = 0;
    seal_report(rk, g.grant, 9, &f, 1, blob, &n);
    forged += pp_report_open(&pa, &g, blob, n, work, sizeof work, &S.st, &add) == PP_ERR_FORMAT;
    f = s1;
    f.amount = 0;
    seal_report(rk, g.grant, 9, &f, 1, blob, &n);
    forged += pp_report_open(&pa, &g, blob, n, work, sizeof work, &S.st, &add) == PP_ERR_FORMAT;
    CHECK(forged == 5 && S.st.n_spend == 0 && S.st.n_grant == 0,
          "a report key holder still cannot slip in a foreign or empty spend");
    pp_spend_t ok2[2] = {s2, s1};
    seal_report(rk, g.grant, 9, ok2, 2, blob, &n);
    CHECK(pp_report_open(&pa, &g, blob, n, work, sizeof work, &S.st, &add) == PP_OK && add == 2,
          "any report sequence opens under its own nonce");
    pp_cafe_end(&cafe2);
}

static void test_names(void)
{
    CHECK(strcmp(pp_status_name(PP_ERR_FUNDS), "insufficient balance") == 0 &&
              strcmp(pp_status_name((pp_status_t) 99), "unknown") == 0,
          "status names");
    CHECK(strcmp(pp_entry_state_name(PP_ST_QUARANTINED), "quarantined") == 0 &&
              strcmp(pp_entry_state_name(0), "none") == 0,
          "entry state names");
    CHECK(pp_entry_tag(0) == LPRES_STATE_NEITHER &&
              pp_entry_tag(PP_ST_CONTESTED) == LPRES_STATE_BOTH,
          "entry tags");
}

int main(void)
{
    printf("=== passport: log in from any device, no server ===\n");
    uint8_t seed[32], other[32];
    rnd(0, seed, 32);
    rnd(0, other, 32);
    test_kat();
    test_seed();
    test_seed_edges();
    test_identity(seed, other);
    test_merge();
    test_capsule();
    test_vouchers();
    test_cafe();
    test_state_edges();
    test_capsule_edges();
    test_cafe_edges();
    test_names();
    test_args();

    /* production level: the MATRIX root (ML-DSA-87 + SLH-DSA-256s) */
    pp_passport_t pm;
    CHECK(pp_hydrate(&pm, PQM_LEVEL_MATRIX, seed) == PP_OK &&
              pm.root.pk.level == PQM_LEVEL_MATRIX && memcmp(pm.vault_key, pa.vault_key, 32) == 0 &&
              memcmp(pm.root.did, pa.root.did, 32) != 0,
          "MATRIX root: same vault key, its own DID");
    pp_wipe(&pm);
    static const pp_passport_t zp;
    CHECK(memcmp(&pm, &zp, sizeof pm) == 0, "pp_wipe leaves nothing");
    pp_wipe(&pa);
    pp_wipe(&pb);
    pp_wipe(&px);
    printf("%d passed, %d failed\n", pass_n, fail_n);
    return fail_n ? 1 : 0;
}
