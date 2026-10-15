/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_alx_carto.c — the core index node the build script wrote, and
 * cartographers' overlays signed here with ML-DSA-65 (alx_carto.h).
 * argv[1]: the 05_alexandria directory. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "alx_carto.h"
#include "alx_carto_mldsa.h"
#include "../robin_debanks/sha256.h"

static unsigned checks, fails;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            fails++;                                                                               \
            printf("[FAIL] %s:%d: ", __FILE__, __LINE__);                                          \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

static uint8_t T[1u << 20], IDX[1u << 20], M[1u << 20], B510[ALX_BOOK_MAX], B930[ALX_BOOK_MAX];
static uint32_t TN, IN, B510N, B930N;
static alx_t X;
static alx_grid_t G;
static alx_carto_t C;

static uint32_t slurp(const char *dir, const char *name, uint8_t *buf, uint32_t cap)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) {
        printf("[FAIL] cannot open %s\n", path);
        exit(1);
    }
    uint32_t n = (uint32_t) fread(buf, 1, cap, f);
    fclose(f);
    return n;
}

static void put64(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; i++) p[i] = (uint8_t) (v >> (56 - 8 * i));
}
static void put32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) p[i] = (uint8_t) (v >> (24 - 8 * i));
}
static uint64_t get64(const uint8_t *p)
{
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v = v << 8 | p[i];
    return v;
}

static uint8_t *ent(uint8_t *b, uint32_t i)
{
    return b + ALX_IDX_HEAD + i * ALX_IDX_ENTRY;
}

/* ---- the core index node ---- */

static int core_copy(void (*edit)(uint8_t *, uint32_t *))
{
    uint32_t n = IN;
    memcpy(M, IDX, IN);
    edit(M, &n);
    sha256(M, n - 32u, M + n - 32u);
    alx_carto_t c;
    return alx_carto_core(&c, &G, M, n);
}

#define EDIT(name, body)                                                                           \
    static void name(uint8_t *b, uint32_t *n)                                                      \
    {                                                                                              \
        (void) b;                                                                                  \
        (void) n;                                                                                  \
        body;                                                                                      \
    }
EDIT(c_magic, b[3] = 'J')
EDIT(c_version, b[4] = 2)
EDIT(c_kind, b[5] = 1)
EDIT(c_div, b[7] = 10)
EDIT(c_rsv12, b[15] = 1)
EDIT(c_rsv52, b[63] = 1)
EDIT(c_parent_prefix, b[17] = 0x71)
EDIT(c_parent, b[30] ^= 1)
EDIT(c_count_up, b[11]++)
EDIT(c_count_zero, memset(b + 8, 0, 4))
EDIT(c_short, *n -= ALX_IDX_ENTRY; b[11]--) /* the last entry gone */
EDIT(c_order, uint8_t t[ALX_IDX_ENTRY]; memcpy(t, ent(b, 0), ALX_IDX_ENTRY);
     memcpy(ent(b, 0), ent(b, 1), ALX_IDX_ENTRY); memcpy(ent(b, 1), t, ALX_IDX_ENTRY))
EDIT(c_dup, memcpy(ent(b, 1), ent(b, 0), ALX_IDX_ENTRY))
EDIT(c_domain, ent(b, 0)[0] = 0x20)
EDIT(c_ext, ent(b, 0)[5] = 1)
EDIT(c_place_low, ent(b, 0)[1] = 0; ent(b, 0)[2] = 99)
EDIT(c_item0, ent(b, 0)[3] = 0)
EDIT(c_item100, ent(b, 0)[3] = 100)
EDIT(c_target_prefix, ent(b, 0)[9] = 0x70)
EDIT(c_target_hash, ent(b, 0)[20] ^= 1)
EDIT(c_len0, memset(ent(b, 0) + 48, 0, 4))
EDIT(c_len, ent(b, 0)[51]++)
EDIT(c_off, ent(b, 0)[55] += 64)
EDIT(c_container, ent(b, 0)[70] ^= 1)
EDIT(c_container_zero, memset(ent(b, 0) + 56, 0, 36))
EDIT(c_container_prefix, ent(b, 0)[57] = 0x71)
EDIT(c_rsv92, ent(b, 0)[95] = 1)
EDIT(c_lba, ent(b, 0)[47] = 9) /* a sector number is the disc builder's to give */
EDIT(c_not_in_library,
     put64(ent(b, (*n - ALX_IDX_HEAD - 32u) / ALX_IDX_ENTRY - 1u),
           get64(ent(b, (*n - ALX_IDX_HEAD - 32u) / ALX_IDX_ENTRY - 1u)) + 0x100000000ull))

static void test_core(void)
{
    CHECK(alx_carto_core(&C, &G, IDX, IN) == ALX_OK, "the core index node opens");
    uint32_t n = C.core.n;
    CHECK(n == 4u + X.targets, "one entry per manifest and language (%u)", n);
    /* every word: the library's bytes, and the target CID is their hash */
    for (uint32_t i = 0; i < n; i++) {
        uint64_t w = get64(ent(IDX, i));
        alx_carto_hit_t h;
        alx_grid_hit_t g;
        CHECK(alx_carto_resolve(&C, w, &h) == ALX_OK && h.trust == ALX_T_SEALED && h.from == 0 &&
                  alx_grid_resolve(&G, w, &g) == ALX_OK && h.off == g.off && h.len == g.len,
              "word %016llx", (unsigned long long) w);
        alx_booklet_t bk;
        alx_booklet(&X, g.booklet, &bk);
        const uint8_t *book = bk.division == 510 ? B510 : B930;
        uint8_t cid[36];
        alx_carto_cid(book + h.off, h.len, cid);
        CHECK(!memcmp(cid, h.target, 36) && !memcmp(h.container, bk.cid, 36) && h.lba == 0,
              "word %016llx names its bytes", (unsigned long long) w);
    }
    alx_carto_hit_t h;
    CHECK(alx_carto_resolve(&C, 0x1002000100000002ull, &h) == ALX_OK, "gcd in BASIC");
    CHECK(alx_carto_resolve(&C, 0x1002000100000004ull, &h) == ALX_E_NAME, "no gcd in 004");
    CHECK(alx_carto_resolve(&C, 0, &h) == ALX_E_NAME, "word 0");
    CHECK(alx_carto_resolve(&C, ~0ull, &h) == ALX_E_NAME, "the last word");
    CHECK(alx_carto_resolve(&C, 0x1002000100000002ull, 0) == ALX_E_FORM, "nowhere to answer");
    CHECK(alx_carto_resolve(0, 0x1002000100000002ull, &h) == ALX_E_FORM, "no cartographer");

    static const struct {
        void (*edit)(uint8_t *, uint32_t *);
        int want;
        const char *what;
    } c[] = {{c_magic, ALX_E_FORM, "magic"},
             {c_version, ALX_E_FORM, "version"},
             {c_kind, ALX_E_FORM, "an overlay's kind"},
             {c_div, ALX_E_FORM, "a core with a division"},
             {c_rsv12, ALX_E_FORM, "reserved 12"},
             {c_rsv52, ALX_E_FORM, "reserved 52"},
             {c_parent_prefix, ALX_E_FORM, "parent prefix"},
             {c_parent, ALX_E_LINEAGE, "another parent"},
             {c_count_up, ALX_E_FORM, "one entry too many"},
             {c_count_zero, ALX_E_FORM, "no entries"},
             {c_short, ALX_E_FORM, "an entry missing"},
             {c_order, ALX_E_FORM, "out of order"},
             {c_dup, ALX_E_FORM, "a word twice"},
             {c_domain, ALX_E_FORM, "a data word in the core"},
             {c_ext, ALX_E_FORM, "an extension in the core"},
             {c_place_low, ALX_E_FORM, "place 99"},
             {c_item0, ALX_E_FORM, "item 0"},
             {c_item100, ALX_E_FORM, "item 100"},
             {c_target_prefix, ALX_E_FORM, "a dag-pb target in the core"},
             {c_len0, ALX_E_FORM, "length 0"},
             {c_len, ALX_E_FORM, "another length"},
             {c_off, ALX_E_FORM, "another offset"},
             {c_container, ALX_E_FORM, "another booklet"},
             {c_container_zero, ALX_E_FORM, "no booklet"},
             {c_container_prefix, ALX_E_FORM, "booklet prefix"},
             {c_rsv92, ALX_E_FORM, "reserved 92"},
             {c_not_in_library, ALX_E_NAME, "a word the library lacks"}};
    for (uint32_t i = 0; i < sizeof c / sizeof c[0]; i++) {
        int s = core_copy(c[i].edit);
        CHECK(s == c[i].want, "%s: %d, want %d", c[i].what, s, c[i].want);
    }
    /* the target hash: a manifest's is checked against its content id;
     * a language's only when its booklet is read (above) */
    int s = core_copy(c_target_hash);
    CHECK(s == ALX_E_HASH || s == ALX_OK, "a changed target hash: %d", s);
    uint32_t mi = 0;
    while (mi < n && ((get64(ent(IDX, mi)) >> 8) & 0xFF) != ALX_G_MANIFEST) mi++;
    memcpy(M, IDX, IN);
    ent(M, mi)[20] ^= 1;
    sha256(M, IN - 32u, M + IN - 32u);
    alx_carto_t cc;
    CHECK(alx_carto_core(&cc, &G, M, IN) == ALX_E_HASH, "a manifest's target is its content id");
    CHECK(core_copy(c_lba) == ALX_OK, "a sector number is accepted");
    memcpy(M, IDX, IN);
    M[IN - 1] ^= 1;
    CHECK(alx_carto_core(&cc, &G, M, IN) == ALX_E_HASH, "the trailer");
    CHECK(alx_carto_core(&cc, &G, IDX, IN - 1) == ALX_E_FORM, "one byte short");
    CHECK(alx_carto_core(&cc, &G, IDX, 0) == ALX_E_FORM, "empty");
    CHECK(alx_carto_core(&cc, &G, 0, IN) == ALX_E_FORM, "no bytes");
    CHECK(alx_carto_core(0, &G, IDX, IN) == ALX_E_FORM, "no cartographer");
    CHECK(alx_carto_core(&cc, 0, IDX, IN) == ALX_E_FORM, "no grid");
    CHECK(alx_carto_core(&cc, &G, IDX, ALX_BOOK_MAX + 1u) == ALX_E_FORM, "over 1 MiB");
    CHECK(cc.core.b == 0 && cc.nover == 0, "a refused core leaves nothing open");
}

/* ---- overlays ---- */

static uint8_t PK[3][ALX_IDX_PK], SK[3][ALX_MLDSA_SK];
static uint8_t OV[32][1u << 16];
static uint32_t OVN[32];

typedef struct {
    uint64_t word;
    uint8_t fill; /* the target CID's digest byte */
    uint32_t len;
} row_t;

/* build and sign an overlay: kind, division, parent, entries */
static uint32_t overlay(uint8_t *b, uint32_t div, const uint8_t parent[36], const row_t *r,
                        uint32_t n, int key)
{
    memset(b, 0, ALX_IDX_HEAD + n * ALX_IDX_ENTRY);
    memcpy(b, "ALXI", 4);
    b[4] = 1;
    b[5] = ALX_IDX_OVERLAY;
    b[6] = (uint8_t) (div >> 8);
    b[7] = (uint8_t) div;
    put32(b + 8, n);
    memcpy(b + 16, parent, 36);
    for (uint32_t i = 0; i < n; i++) {
        uint8_t *e = ent(b, i);
        put64(e, r[i].word);
        e[8] = 0x01, e[9] = 0x70, e[10] = 0x12, e[11] = 0x20;
        memset(e + 12, r[i].fill, 32);
        put32(e + 48, r[i].len);
    }
    uint32_t body = ALX_IDX_HEAD + n * ALX_IDX_ENTRY;
    uint8_t h[32];
    sha256(b, body, h);
    memcpy(b + body, PK[key], ALX_IDX_PK);
    alx_mldsa_sign(SK[key], h, 32, 0, b + body + ALX_IDX_PK);
    return body + ALX_IDX_PK + ALX_IDX_SIG;
}

static uint64_t dw(uint32_t domain, uint32_t place, uint32_t item, uint32_t sub)
{
    return (uint64_t) domain << 56 | (uint64_t) place << 40 | (uint64_t) item << 32 | sub;
}

static int mount(const uint8_t *b, uint32_t n, int nkeys)
{
    return alx_carto_mount(&C, b, n, (const uint8_t(*)[ALX_IDX_PK]) PK, (uint32_t) nkeys,
                           alx_mldsa_verify, 0);
}

static bool never(void *ctx, const uint8_t pk[ALX_IDX_PK], const uint8_t *m, uint32_t len,
                  const uint8_t sig[ALX_IDX_SIG])
{
    (void) ctx, (void) pk, (void) m, (void) len, (void) sig;
    return false;
}

static void test_overlays(void)
{
    for (int k = 0; k < 3; k++) {
        uint8_t seed[32];
        memset(seed, 0x30 + k, 32);
        alx_mldsa_keypair(seed, PK[k], SK[k]);
    }
    CHECK(alx_carto_core(&C, &G, IDX, IN) == ALX_OK, "core");
    static uint8_t store[4][36];
    alx_carto_demoted(&C, store, 4);
    const row_t weather[] = {{dw(ALX_G_SENSOR, 520, 21, 0x0100), 0xA1, 4096},
                             {dw(ALX_G_SENSOR, 521, 1, 0), 0xA2, 512},
                             {dw(ALX_G_SENSOR, 529, 99, 0xFFFF), 0xA3, 1}};
    OVN[0] = overlay(OV[0], 520, C.core.cid, weather, 3, 0);
    CHECK(alx_carto_overlay_check(OV[0], OVN[0], alx_mldsa_verify, 0) == ALX_OK, "signed");
    CHECK(alx_carto_overlay_check(OV[0], OVN[0], never, 0) == ALX_E_SIGN, "a check that fails");
    CHECK(alx_carto_overlay_check(OV[0], OVN[0], 0, 0) == ALX_E_FORM, "no check");
    CHECK(mount(OV[0], OVN[0], 1) == ALX_OK && C.nover == 1 && C.over[0].key == 0,
          "a trusted cartographer's overlay mounts");
    CHECK(mount(OV[0], OVN[0], 1) == ALX_OK && C.nover == 1, "mounting twice changes nothing");
    alx_carto_hit_t h;
    CHECK(alx_carto_resolve(&C, weather[0].word, &h) == ALX_OK && h.trust == ALX_T_CLAIM &&
              h.from == 1 && h.len == 4096 && h.target[1] == 0x70 && h.target[20] == 0xA1,
          "a claim resolves, marked as a claim");
    CHECK(alx_carto_resolve(&C, weather[2].word, &h) == ALX_OK && h.len == 1, "the last entry");
    CHECK(alx_carto_resolve(&C, dw(ALX_G_SENSOR, 520, 22, 0), &h) == ALX_E_NAME, "no such word");

    /* the good-nature rule: contrary evidence demotes a claim */
    uint8_t t0[36];
    memcpy(t0, ent(OV[0], 0) + 8, 36);
    CHECK(alx_carto_demote(&C, t0) == ALX_OK && alx_carto_demote(&C, t0) == ALX_OK &&
              C.ndemoted == 1,
          "demoted once");
    CHECK(alx_carto_resolve(&C, weather[0].word, &h) == ALX_OK && h.trust == ALX_T_DEMOTED,
          "a demoted claim says so");
    CHECK(alx_carto_resolve(&C, weather[1].word, &h) == ALX_OK && h.trust == ALX_T_CLAIM,
          "its neighbours do not change");
    CHECK(alx_carto_demote(&C, ent(IDX, 0) + 8) == ALX_E_FORM, "a sealed target is never demoted");
    uint8_t t[36];
    memcpy(t, t0, 36);
    for (int i = 1; i <= 3; i++) {
        t[30] = (uint8_t) i;
        CHECK(alx_carto_demote(&C, t) == ALX_OK, "demote %d", i);
    }
    t[30] = 9;
    CHECK(alx_carto_demote(&C, t) == ALX_E_LIMIT, "the list is full");
    CHECK(alx_carto_demote(0, t) == ALX_E_FORM && alx_carto_demote(&C, 0) == ALX_E_FORM,
          "nothing to demote");

    /* who may mount what */
    OVN[1] = overlay(OV[1], 520, C.core.cid, weather, 3, 2);
    CHECK(mount(OV[1], OVN[1], 2) == ALX_E_TRUST, "an untrusted key");
    CHECK(mount(OV[1], OVN[1], 3) == ALX_OK && C.over[1].key == 2, "trusted once listed");
    memcpy(OV[2], OV[0], OVN[0]);
    OV[2][ALX_IDX_HEAD + 30] ^= 1; /* a changed body under the old signature */
    CHECK(mount(OV[2], OVN[0], 1) == ALX_E_SIGN, "a changed overlay");
    memcpy(OV[2], OV[0], OVN[0]);
    OV[2][OVN[0] - 5] ^= 1;
    CHECK(mount(OV[2], OVN[0], 1) == ALX_E_SIGN, "a changed signature");
    uint8_t stranger[36];
    memcpy(stranger, C.core.cid, 36);
    stranger[35] ^= 1;
    const row_t one[] = {{dw(ALX_G_EMBED, 640, 1, 0), 0xB1, 64}};
    OVN[3] = overlay(OV[3], 640, stranger, one, 1, 0);
    CHECK(mount(OV[3], OVN[3], 1) == ALX_E_LINEAGE, "a parent outside the lineage");
    OVN[4] = overlay(OV[4], 640, C.over[0].cid, one, 1, 1);
    CHECK(mount(OV[4], OVN[4], 2) == ALX_OK, "an overlay on an overlay");
    CHECK(alx_carto_resolve(&C, one[0].word, &h) == ALX_OK && h.from == 3, "found in the third");

    /* claims that disagree are BOTH; claims that agree are fine */
    const row_t other[] = {{weather[1].word, 0xC1, 512}};
    const row_t same[] = {{weather[1].word, 0xA2, 512}};
    OVN[5] = overlay(OV[5], 520, C.core.cid, same, 1, 1);
    CHECK(mount(OV[5], OVN[5], 2) == ALX_OK, "an agreeing overlay");
    CHECK(alx_carto_resolve(&C, weather[1].word, &h) == ALX_OK && h.from == 1, "agreement");
    OVN[6] = overlay(OV[6], 520, C.core.cid, other, 1, 1);
    CHECK(mount(OV[6], OVN[6], 2) == ALX_OK, "a disagreeing overlay mounts");
    CHECK(alx_carto_resolve(&C, weather[1].word, &h) == ALX_E_BOTH, "and the word is BOTH");
    CHECK(alx_carto_resolve(&C, weather[2].word, &h) == ALX_OK, "the rest still resolve");

    /* what an overlay may hold */
    const row_t code[] = {{dw(ALX_G_CODE, 520, 1, 1), 0xD1, 8}};
    OVN[7] = overlay(OV[7], 520, C.core.cid, code, 1, 0);
    CHECK(mount(OV[7], OVN[7], 1) == ALX_E_FORM, "never code");
    const row_t outside[] = {{dw(ALX_G_DATA, 531, 1, 0), 0xD2, 8}};
    OVN[8] = overlay(OV[8], 520, C.core.cid, outside, 1, 0);
    CHECK(mount(OV[8], OVN[8], 1) == ALX_E_FORM, "outside its division");
    const row_t zero[] = {{dw(ALX_G_DATA, 520, 1, 0), 0xD3, 0}};
    OVN[9] = overlay(OV[9], 520, C.core.cid, zero, 1, 0);
    CHECK(mount(OV[9], OVN[9], 1) == ALX_E_FORM, "length 0");
    const row_t backward[] = {{dw(ALX_G_DATA, 520, 2, 0), 1, 8}, {dw(ALX_G_DATA, 520, 1, 0), 2, 8}};
    OVN[10] = overlay(OV[10], 520, C.core.cid, backward, 2, 0);
    CHECK(mount(OV[10], OVN[10], 1) == ALX_E_FORM, "out of order");
    static const uint32_t baddiv[] = {0, 90, 525, 1000};
    for (uint32_t i = 0; i < 4; i++) {
        OVN[11] = overlay(OV[11], baddiv[i], C.core.cid, one, 1, 0);
        CHECK(mount(OV[11], OVN[11], 1) == ALX_E_FORM, "division %u", baddiv[i]);
    }
    CHECK(mount(IDX, IN, 1) == ALX_E_FORM, "a core is not an overlay");
    CHECK(mount(OV[0], OVN[0] - 1, 1) == ALX_E_FORM, "one byte short");
    CHECK(alx_carto_mount(&C, OV[0], OVN[0], (const uint8_t(*)[ALX_IDX_PK]) PK, 1, 0, 0) ==
              ALX_E_FORM,
          "no signature check");
    CHECK(alx_carto_mount(&C, OV[0], OVN[0], 0, 0, alx_mldsa_verify, 0) == ALX_E_TRUST,
          "no trusted keys");

    /* at most ALX_CARTO_MOUNT at once */
    uint32_t k = 12;
    int s = ALX_OK;
    while (s == ALX_OK && k < 32) {
        const row_t r[] = {{dw(ALX_G_ATTEST, 700, k, 0), (uint8_t) k, 8}};
        OVN[k] = overlay(OV[k], 700, C.core.cid, r, 1, 0);
        s = mount(OV[k], OVN[k], 1);
        k++;
    }
    CHECK(s == ALX_E_LIMIT && C.nover == ALX_CARTO_MOUNT, "the %uth overlay is refused (%d)",
          ALX_CARTO_MOUNT + 1, s);

    /* a new core forgets the overlays */
    CHECK(alx_carto_core(&C, &G, IDX, IN) == ALX_OK && C.nover == 0 &&
              alx_carto_resolve(&C, weather[0].word, &h) == ALX_E_NAME,
          "a new core starts clean");
    alx_carto_t none;
    memset(&none, 0, sizeof none);
    CHECK(mount(OV[0], OVN[0], 1) == ALX_OK, "remount");
    CHECK(alx_carto_mount(&none, OV[0], OVN[0], (const uint8_t(*)[ALX_IDX_PK]) PK, 1,
                          alx_mldsa_verify, 0) == ALX_E_FORM,
          "no core, no overlays");
}

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    TN = slurp(argv[1], "ALEXANDRIA.TBL", T, sizeof T);
    IN = slurp(argv[1], "ALEXANDRIA.IDX", IDX, sizeof IDX);
    B510N = slurp(argv[1], "B510.booklet", B510, sizeof B510);
    B930N = slurp(argv[1], "B930.booklet", B930, sizeof B930);
    if (alx_open(&X, T, TN) != ALX_OK || alx_grid_build(&G, &X) != ALX_OK) {
        printf("[FAIL] the built index does not open\n");
        return 1;
    }
    test_core();
    test_overlays();
    printf("%u checks, %u failures\n", checks, fails);
    return fails ? 1 : 0;
}
