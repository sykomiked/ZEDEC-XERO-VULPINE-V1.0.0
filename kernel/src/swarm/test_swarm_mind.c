/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* test_swarm_mind.c — Enochian core, logic spaces, self-awareness, evolution
 * and enterprises. Every expected value is worked by hand in the comments;
 * the Enochian ones are copied from the owner's dictionary.jsonl. */
#include <stdio.h>
#include <string.h>
#include "swarm_enochian.h"
#include "swarm_logic.h"
#include "swarm_self.h"
#include "swarm_evolve.h"
#include "swarm_enterprise.h"
#include "swarm_phase.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static void test_letters(void)
{
    /* dictionary: OL 14 root 5, NOAN 28 root 1, BABALON 44 root 8,
     * EOTLNUHAB 60 root 6, XCUTB 22 root 4, CFPAAABNSID 55 root 1,
     * VORS 12 (V = 0), GNAY 15 (Y = 0) */
    CHECK(swarm_en_gematria("OL", 2) == 14 && swarm_en_root(14) == 5);
    CHECK(swarm_en_gematria("NOAN", 4) == 28 && swarm_en_root(28) == 1);
    CHECK(swarm_en_gematria("BABALON", 7) == 44 && swarm_en_root(44) == 8);
    CHECK(swarm_en_gematria("EOTLNUHAB", 9) == 60 && swarm_en_root(60) == 6);
    CHECK(swarm_en_gematria("XCUTB", 5) == 22 && swarm_en_root(22) == 4);
    CHECK(swarm_en_gematria("CFPAAABNSID", 11) == 55 && swarm_en_root(55) == 1);
    CHECK(swarm_en_gematria("VORS", 4) == 12);
    CHECK(swarm_en_gematria("GNAY", 4) == 15);
    CHECK(swarm_en_gematria("jkw", 3) == 3 + 3 + 4);       /* allographs I C U */
    CHECK(swarm_en_root(0) == 9 && swarm_en_root(9) == 9 && swarm_en_root(10) == 1);
    CHECK(swarm_en_root(18446744073709551615ull) == 6);    /* 2^64-1: digit sum 87 -> 15 -> 6 */
    CHECK(strcmp(swarm_en_domain(5), "PENTADIC") == 0);

    char b[64];
    /* dictionary: OL "ओ (o)ल (la)", XCUTB starts "क्ष (kṣa)" */
    CHECK(swarm_en_sanskrit("OL", 2, b, sizeof b) == 6 && strcmp(b, "\xE0\xA4\x93\xE0\xA4\xB2") == 0);
    CHECK(swarm_en_iast("XCUTB", 5, b, sizeof b) > 0 && strcmp(b, "k\xE1\xB9\xA3" "acautaba") == 0);
    CHECK(swarm_en_iast("VORS", 4, b, sizeof b) > 0 && strcmp(b, "orasa") == 0);   /* glide silent */
    CHECK(swarm_en_sanskrit("OL", 2, b, 6) == -1);
}

static void test_numen(void)
{
    /* dictionary annotation for 22: root 4, master builder, loop, mod7 1 Sun,
     * mod3 1 Mercury, not prime, not Fibonacci */
    swarm_numen_t n = swarm_numen(22);
    CHECK(n.root == 4 && n.master && n.vortex == SWARM_VORTEX_LOOP && n.loop_step == 2);
    CHECK(strncmp(n.master_meaning, "Master Builder", 14) == 0);
    CHECK(strcmp(n.planet, "Sun (vitality)") == 0 && strcmp(n.element, "Mercury (mind)") == 0);
    CHECK(!n.prime && !n.fibonacci);
    /* 55: root 1, master liberator, Fibonacci, mod7 6 Venus */
    n = swarm_numen(55);
    CHECK(n.root == 1 && n.master && n.fibonacci && !n.prime && n.mod7 == 6);
    CHECK(strcmp(n.planet, "Venus (relation)") == 0);
    /* 60: root 6, axis, mod7 4 Mercury, mod3 0 Sulphur */
    n = swarm_numen(60);
    CHECK(n.root == 6 && n.vortex == SWARM_VORTEX_AXIS && n.loop_step == -1 && !n.master);
    CHECK(strcmp(n.planet, "Mercury (mind)") == 0 && strcmp(n.element, "Sulphur (soul)") == 0);
    /* 89: prime and Fibonacci; 99 is not in the owner's master list */
    n = swarm_numen(89);
    CHECK(n.prime && n.fibonacci && n.root == 8);
    CHECK(!swarm_numen(99).master);
    /* vortex: 1 2 4 8 7 5 1, and 3 6 9 stay on the axis (3 -> 6 -> 3, 9 -> 9) */
    uint32_t r = 1, seq[7];
    for (int i = 0; i < 7; i++) { seq[i] = r; r = swarm_vortex_next(r); }
    CHECK(seq[0] == 1 && seq[1] == 2 && seq[2] == 4 && seq[3] == 8 && seq[4] == 7 && seq[5] == 5 && seq[6] == 1);
    CHECK(swarm_vortex_next(3) == 6 && swarm_vortex_next(6) == 3 && swarm_vortex_next(9) == 9);
}

static void test_dimensions(void)
{
    /* "OL NOAN" as a phrase (3D), two phrases as a 4D unit, and on to 7D:
     * the summary is built from children only and matches the direct sums. */
    uint64_t u2a[1], u2b[1], u3[2], u4[3];
    swarm_en_unit_t w1, w2, p, q;
    swarm_en_unit_init(&w1, 2, u2a, 1);
    swarm_en_unit_init(&w2, 2, u2b, 1);
    CHECK(swarm_en_unit_word(&w1, "OL", 2));
    CHECK(swarm_en_unit_word(&w2, "NOAN", 4));
    swarm_en_unit_init(&p, 3, u3, 2);
    CHECK(swarm_en_unit_add(&p, &w1) && swarm_en_unit_add(&p, &w2));
    /* 14 + 28 = 42, root 6 = root(5 + 1); 6 letters, 2 words */
    CHECK(p.gematria == 42 && p.root == 6 && p.units[0] == 6 && p.units[1] == 2);
    CHECK(p.mod7 == 0 && p.mod3 == 0 && p.mod49 == 42);
    swarm_en_unit_init(&q, 4, u4, 3);
    CHECK(swarm_en_unit_add(&q, &p) && swarm_en_unit_add(&q, &p));
    CHECK(q.gematria == 84 && q.root == 3 && q.units[0] == 12 && q.units[1] == 4 && q.units[2] == 2);
    uint8_t c[8];
    CHECK(swarm_en_unit_coords(&q, c, 8) == 4);
    CHECK(c[0] == 84 % 49 && c[1] == 12 && c[2] == 4 && c[3] == 2);
    CHECK(!swarm_en_unit_add(&p, &q));               /* dimensions must step by one */

    /* climb to 13D and beyond: each level doubles the one below */
    uint64_t store[40][40];
    swarm_en_unit_t lv[40];
    swarm_en_unit_init(&lv[0], 2, store[0], 40);
    CHECK(swarm_en_unit_word(&lv[0], "OL", 2));
    for (uint32_t d = 1; d < 40; d++) {
        swarm_en_unit_init(&lv[d], d + 2, store[d], 40);
        CHECK(swarm_en_unit_add(&lv[d], &lv[d - 1]) && swarm_en_unit_add(&lv[d], &lv[d - 1]));
    }
    /* 41D unit: 14 * 2^39 gematria, root(14 * 2^39) = root(5 * root(2^39));
     * 2^39 mod 9: 2^6 = 1 mod 9, so 2^39 = 2^3 = 8; 5 * 8 = 40 -> 4 */
    CHECK(lv[39].dim == 41 && lv[39].gematria == 14ull << 39 && lv[39].root == 4);
    /* 2 * 2^39 letters, 2^39 words, ... one 40D unit */
    CHECK(lv[39].units[0] == 2ull << 39 && lv[39].units[1] == 1ull << 39 && lv[39].units[39] == 2);
    /* only the storage granted bounds the dimension (E6) */
    uint64_t small[1];
    swarm_en_unit_t tiny;
    swarm_en_unit_init(&tiny, 4, small, 1);
    CHECK(!swarm_en_unit_add(&tiny, &p));           /* not enough storage granted */
}

static void test_codec(void)
{
    const char *t = "OL SONF VORSG GOHO IAD BALT LANSH CALZ VONPHO SOBRA ZOL ROR I TA NAZPSAD";
    uint32_t len = (uint32_t)strlen(t);   /* 72 symbols: two frames */
    uint8_t buf[64];
    char back[128];
    CHECK(swarm_en_pack(t, len, buf, sizeof buf) == 42);
    CHECK(swarm_en_unpack(buf, 42, back, sizeof back) == (int32_t)len && strcmp(back, t) == 0);
    /* 36 letters in 21 bytes is 4.67 bits a letter */
    char full[37];
    for (int i = 0; i < 36; i++) full[i] = 'Z';
    full[36] = 0;
    CHECK(swarm_en_pack(full, 36, buf, sizeof buf) == 21);
    CHECK(swarm_en_unpack(buf, 21, back, sizeof back) == 36 && strcmp(back, full) == 0);
    CHECK(swarm_en_pack("ol", 2, buf, sizeof buf) == -1);   /* lowercase is not canonical */
    CHECK(swarm_en_pack("OK", 2, buf, sizeof buf) == -1);   /* K is an allograph */
    CHECK(swarm_en_pack("", 0, buf, sizeof buf) == 0);
}

static void test_langs(void)
{
    swarm_lang_table_t t;
    swarm_lang_init(&t);
    CHECK(t.count == 1 && strcmp(t.lang[t.operator_lang].code, "en") == 0);
    CHECK(swarm_lang_install(&t, "ja", "Japanese", 7) == 1);
    CHECK(swarm_lang_install(&t, "ja", "Japanese", 9) == 1 && t.lang[1].translator == 9);
    CHECK(swarm_lang_set_operator(&t, "ja") && t.operator_lang == 1);
    CHECK(!swarm_lang_set_operator(&t, "xx"));
    CHECK(strcmp(swarm_lang_core(), "enochian") == 0);
}

static void test_logic(void)
{
    swarm_claim_t p = { 7, SWARM_SPACE_POS, 2, false, 100 };
    swarm_claim_t n = { 7, SWARM_SPACE_NEG, 2, false, 60 };
    swarm_lg_result_t r = swarm_logic_meet(&p, &n);           /* L4 */
    CHECK(r.kind == SWARM_LG_ANNIHILATE && r.freed == 160 && r.out.space == SWARM_SPACE_NEU);
    CHECK(r.truth == SWARM_HK_NEUTRAL);
    swarm_claim_t p2 = p; p2.tokens = 40;
    r = swarm_logic_meet(&p, &p2);                            /* L2 */
    CHECK(r.kind == SWARM_LG_REINFORCE && r.out.level == 3 && r.freed == 40 && r.out.tokens == 100);
    swarm_claim_t z = { 7, SWARM_SPACE_NEU, 0, false, 5 };
    r = swarm_logic_meet(&z, &n);                             /* L3 */
    CHECK(r.kind == SWARM_LG_HOLD && r.out.space == SWARM_SPACE_NEG && r.freed == 0);
    swarm_claim_t pv = p; pv.verified = true;
    r = swarm_logic_meet(&pv, &n);                            /* L5 evidence */
    CHECK(r.kind == SWARM_LG_DOMINATE && r.out.space == SWARM_SPACE_POS && r.freed == 60 && r.truth == SWARM_HK_TRUE);
    swarm_claim_t nhi = n; nhi.level = 4;
    r = swarm_logic_meet(&p, &nhi);                           /* L5 level */
    CHECK(r.kind == SWARM_LG_DOMINATE && r.out.space == SWARM_SPACE_NEG && r.freed == 100);
    swarm_claim_t nv = n; nv.verified = true;
    r = swarm_logic_meet(&pv, &nv);                           /* L7 */
    CHECK(r.kind == SWARM_LG_PARADOX && r.escalate && r.freed == 160 && r.truth == SWARM_HK_PARADOX);
    nv.level = 3;
    r = swarm_logic_meet(&pv, &nv);                           /* L6 */
    CHECK(r.kind == SWARM_LG_GLUT && r.out.space == SWARM_SPACE_NEG && r.freed == 100 && r.truth == SWARM_HK_GLUT);
    swarm_claim_t other = n; other.topic = 8;
    CHECK(swarm_logic_meet(&p, &other).kind == SWARM_LG_UNRELATED);
}

static void test_self(void)
{
    swarm_self_t s;
    swarm_self_init(&s);
    CHECK(s.count == 7 && strcmp(s.ring[SWARM_RING_USER].name, "user") == 0);
    CHECK(swarm_self_ring_add(&s, "music studio") == 7);
    /* user ring: expected 900, saw 100 -> gap 800 -> stress 200; again -> 350; again -> 462 */
    swarm_self_observe(&s, SWARM_RING_USER, 900, 100);
    CHECK(s.ring[SWARM_RING_USER].stress_milli == 200);
    swarm_reflection_t r = swarm_self_reflect(&s, true);
    CHECK(r.focus == SWARM_RING_USER && r.action == SWARM_ACT_GROW && r.predicted_milli == 200);
    CHECK(r.numen.n == 1);
    swarm_self_observe(&s, SWARM_RING_USER, 900, 100);    /* 350 */
    swarm_self_observe(&s, SWARM_RING_USER, 900, 100);    /* 462 */
    CHECK(s.ring[SWARM_RING_USER].stress_milli == 462);
    r = swarm_self_reflect(&s, false);
    /* A3 first: predicted 200, actual 462 -> inner gap 262 -> inner stress 65 */
    CHECK(s.ring[SWARM_RING_INNER].stress_milli == 65 && s.ring[SWARM_RING_INNER].observations == 1);
    CHECK(r.focus == SWARM_RING_USER && r.action == SWARM_ACT_RECALIBRATE && r.predicted_milli == 231);
    CHECK(r.numen.n == 4);
    /* calm everywhere and no room: refine */
    swarm_self_init(&s);
    CHECK(swarm_self_reflect(&s, false).action == SWARM_ACT_REFINE);
}

static void test_evolve(void)
{
    swarm_model_state_t champ = { 1, 0, swarm_dna_from_seed(1), 2, 0, 800, 500 };
    swarm_model_state_t c = champ;
    c.id = 2; c.parent = 1;
    c.score_milli = 850; c.cost_tokens = 600;
    CHECK(swarm_evo_judge(&champ, &c, 1000) == SWARM_EVO_ACCEPT);         /* grow */
    CHECK(swarm_evo_judge(&champ, &c, 550) == SWARM_EVO_REJECT_BUDGET);   /* no room */
    c.score_milli = 800; c.cost_tokens = 450;
    CHECK(swarm_evo_judge(&champ, &c, 550) == SWARM_EVO_ACCEPT);          /* refine */
    c.cost_tokens = 500;
    CHECK(swarm_evo_judge(&champ, &c, 550) == SWARM_EVO_REJECT_NO_GAIN);
    c.score_milli = 799; c.cost_tokens = 10;
    CHECK(swarm_evo_judge(&champ, &c, 550) == SWARM_EVO_REJECT_WORSE);    /* never give back */

    CHECK(swarm_evo_outlier_share(2100) == 100);

    swarm_dna_t d[4];
    for (int i = 0; i < 4; i++) for (int g = 0; g < 8; g++) d[i].gene[g] = 10;
    d[2].gene[3] = 200;
    d[0].gene[0] = 255;                                   /* the companion is never the outlier */
    CHECK(swarm_evo_outlier(d, 4) == 2);

    /* levels 0 | 1 1 | 2 2 2, fitness: weakest at 1 is agent 2 (5), strongest at 2 is agent 5 (9) */
    uint8_t lvl[6] = { 0, 1, 1, 2, 2, 2 };
    uint64_t fit[6] = { 1, 7, 5, 3, 4, 9 };
    CHECK(swarm_evo_mobility(lvl, fit, 6, 3) == 1);
    CHECK(lvl[2] == 2 && lvl[5] == 1 && lvl[0] == 0);
    CHECK(swarm_evo_mobility(lvl, fit, 6, 3) == 0);       /* settled: 7, 9 above 5, 4, 3 */

    uint8_t rec[SWARM_EVO_RECORD];
    swarm_model_state_t back;
    c.score_milli = 850;
    swarm_evo_save(&c, rec);
    CHECK(swarm_evo_restore(rec, &back));
    CHECK(back.id == 2 && back.parent == 1 && back.score_milli == 850 && back.dna.seed == c.dna.seed);
    CHECK(memcmp(back.dna.gene, c.dna.gene, 8) == 0 && back.base_model == 2);
    rec[20] ^= 1;
    CHECK(!swarm_evo_restore(rec, &back));
    CHECK(swarm_evo_crc32((const uint8_t *)"123456789", 9) == 0xCBF43926u);
}

static void test_enterprise(void)
{
    uint64_t wallet[8] = { 1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000 };
    uint64_t pot = 210;
    swarm_economy_t e;
    swarm_ent_init(&e, wallet, 8);
    uint64_t total = swarm_ent_money(&e) + pot;
    CHECK(swarm_ent_find(&e, 42) == -1);

    uint32_t f[2] = { 1, 2 };
    uint64_t st[2] = { 300, 100 };
    int32_t corp = swarm_ent_found(&e, SWARM_ENT_CORPORATION, 42, 0xC0FFEE, f, st, 2, 10);
    CHECK(corp == 0 && e.ent[0].treasury == 400 && wallet[1] == 700);
    CHECK(swarm_ent_find(&e, 42) == 0);
    /* cap: 8 agents * 8/21 = 3 members */
    CHECK(swarm_ent_hire(&e, 0, 3));
    CHECK(!swarm_ent_hire(&e, 0, 4));

    int32_t pj = swarm_ent_project_open(&e, 0, 0xABCD, 11);
    CHECK(pj == 0);
    CHECK(swarm_ent_invest(&e, 0, 5, 200));
    CHECK(swarm_ent_invest(&e, 0, SWARM_ENT_AS_INVESTOR(0), 100));
    CHECK(!swarm_ent_invest(&e, 0, 6, 5000));
    CHECK(swarm_ent_work(&e, 0, 3, 30) && swarm_ent_work(&e, 0, 1, 10));
    CHECK(!swarm_ent_work(&e, 0, 7, 10));                 /* not an employee */
    CHECK(swarm_ent_money(&e) + pot == total);
    /* pool 300 + 210 = 510; wages 8/21 = 194 (rem 6): agent 3 145.5 -> 146? split
     * 194 by 30:10 -> 145.5, 48.5 -> 145, 48, one left to the lower index on a
     * tie of remainders -> 146, 48. investors 316 by 200:100 -> 210.67, 105.33
     * -> 211, 105. */
    CHECK(swarm_ent_project_close(&e, 0, true, &pot, 210));
    CHECK(pot == 0 && wallet[3] == 1146 && wallet[1] == 748);
    CHECK(wallet[5] == 1011 && e.ent[0].treasury == 405);
    CHECK(swarm_ent_money(&e) + pot == total);

    /* failure: stakes come back exactly */
    pj = swarm_ent_project_open(&e, 0, 0xBEEF, 12);
    CHECK(swarm_ent_invest(&e, pj, 6, 77));
    CHECK(swarm_ent_project_close(&e, pj, false, 0, 0) && wallet[6] == 1000);

    /* release with buyout: agent 2 holds 100 of 400 equity -> 405 * 1/4 = 101 */
    CHECK(swarm_ent_release(&e, 0, 2) && wallet[2] == 1001 && e.ent[0].treasury == 304);

    /* idle retirement after 5 cycles: 304 by equity 300:0 -> all to agent 1 */
    CHECK(swarm_ent_tick(&e, 16, 5) == 0);
    CHECK(swarm_ent_tick(&e, 17, 5) == 1);
    CHECK(e.ent[0].status == SWARM_ENT_RETIRED && wallet[1] == 1052 && e.ent[0].treasury == 0);
    CHECK(swarm_ent_find(&e, 42) == -2);                   /* reinstate index 0 */
    CHECK(swarm_ent_reinstate(&e, 0, 20) && e.ent[0].times_reinstated == 1 && e.ent[0].num_members == 2);
    CHECK(swarm_ent_money(&e) + pot == total);

    /* a collective shares equally */
    uint32_t g[3] = { 4, 5, 6 };
    uint64_t gs[3] = { 10, 10, 10 };
    int32_t col = swarm_ent_found(&e, SWARM_ENT_COLLECTIVE, 7, 1, g, gs, 3, 20);
    CHECK(col == 1);
    e.ent[1].treasury += 0;
    CHECK(swarm_ent_retire(&e, 1) && wallet[4] == 1000 && wallet[6] == 1000);
    CHECK(swarm_ent_money(&e) + pot == total);
}

static void test_phase(void)
{
    swarm_phase_t p;
    swarm_phase_init(&p, 27720);                 /* 2+7+7+2+0 = 18 -> 9 */
    CHECK(p.reduced == 9 && swarm_phase_check(&p));
    swarm_phase_step(&p, 1);                     /* 27721 -> 1 */
    CHECK(p.reduced == 1 && p.long_count == 27721);
    swarm_phase_step(&p, 4000000000u);           /* 4e9 -> 4, 1 + 4 = 5 */
    CHECK(p.reduced == 5 && swarm_phase_check(&p));
    for (uint32_t i = 0; i < 1000; i++) swarm_phase_step(&p, 7);
    CHECK(swarm_phase_check(&p));
    swarm_phase_reading_t r = swarm_phase_read(&p, true);
    CHECK(r.reading == 0);                       /* the point of action */
    r = swarm_phase_read(&p, false);
    CHECK(r.reading == p.reduced && r.reading >= 1 && r.reading <= 9);
    swarm_phase_init(&p, 6);
    r = swarm_phase_read(&p, false);
    CHECK(r.vortex == SWARM_VORTEX_AXIS && r.loop_step == -1);
    p.reduced = 4;
    CHECK(!swarm_phase_check(&p));               /* a tampered reduction is caught */

    /* Fibonacci lattice F(5) = 5, F(4) = 3: points (k, 3k mod 5) */
    swarm_geo_t g = swarm_geo_point(7, 5);
    CHECK(g.n == 5 && g.x == 2 && g.y == 1);
    /* F(10) = 55, F(9) = 34: k = 100 -> x 45, y 45 * 34 = 1530 mod 55 = 45 */
    g = swarm_geo_point(100, 10);
    CHECK(g.n == 55 && g.x == 45 && g.y == 45);
    swarm_claim_t a = { 100, SWARM_SPACE_POS, 5, false, 0 };
    swarm_claim_t b = { 100, SWARM_SPACE_NEG, 5, false, 0 };
    swarm_geo_t ga = swarm_geo_claim(&a), gb = swarm_geo_claim(&b);
    CHECK(ga.x == gb.x && ga.y == gb.y && ga.layer == 1 && gb.layer == -1);   /* mirror points */
}

int main(void)
{
    test_phase();
    test_letters();
    test_numen();
    test_dimensions();
    test_codec();
    test_langs();
    test_logic();
    test_self();
    test_evolve();
    test_enterprise();
    if (fails) { printf("%d check(s) failed\n", fails); return 1; }
    printf("test_swarm_mind: all checks passed\n");
    return 0;
}
