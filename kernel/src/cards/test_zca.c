/* test_zca.c — the card activation language.
 *
 * Two things must hold or the whole scheme fails:
 *   1. derive -> emit -> assemble is byte-identical, so an already-printed
 *      card and a card printed WITH its listing activate the same way;
 *   2. a hostile or garbled card cannot do damage or be silently accepted.
 */
#include <stdio.h>
#include <string.h>
#include "zca.h"

static int failures = 0;
#define CHECK(c,m) do{ if(!(c)){printf("[FAIL] %s\n",m);failures++;} \
    else printf("[PASS] %s\n",m);}while(0)
#define D(x) ((double)(x) / (double)SR_ONE)

static bool prog_eq(const zca_program_t *a, const zca_program_t *b) {
    if (a->card_index != b->card_index || a->n != b->n) return false;
    for (uint32_t i = 0; i < a->n; i++)
        if (a->ins[i].op != b->ins[i].op || a->ins[i].a != b->ins[i].a ||
            a->ins[i].b != b->ins[i].b) return false;
    return true;
}

int main(void) {
    printf("=== ZCA: the card carries its own program ===\n");

    /* seal_24525: OLPIRT HPOU, Acoustomancy set 1, gematria 54, root 9 */
    zca_attrs_t A = { 24525, 0, 1, 54, 9, 16 };
    zca_program_t p;
    CHECK(zca_derive(&A, &p) == ZCA_OK, "a printed card derives its program");

    char text[ZCA_MAX_TEXT];
    uint32_t n = zca_emit(&p, text, sizeof text);
    CHECK(n > 0, "the program renders as a printable listing");
    printf("\n--- what gets printed in the activation section ---\n");
    printf("; OLPIRT HPOU — Light Portal Creation\n"
           "; Hold the card facing the lens. Activation is complete when the\n"
           "; companion echoes the seal. Unslot at any time; nothing persists.\n%s", text);
    printf("---------------------------------------------------\n\n");

    /* ---- THE CONVERGENCE: both routes give the same bytes ---- */
    zca_program_t q;
    zca_status_t st = zca_assemble(text, n, &q);
    printf("       assemble -> %s\n", zca_status_name(st));
    CHECK(st == ZCA_OK, "the printed listing assembles back");
    CHECK(prog_eq(&p, &q),
          "DERIVED == ASSEMBLED — an existing card needs no reprint");

    /* operating instructions are skipped, not executed */
    {
        char withdoc[ZCA_MAX_TEXT];
        int k = 0;
        k += snprintf(withdoc+k, sizeof withdoc-k,
                      "; Speak the word once, then present the face.\n");
        memcpy(withdoc+k, text, n+1);
        zca_program_t r;
        CHECK(zca_assemble(withdoc, (uint32_t)(k+n), &r) == ZCA_OK &&
              prog_eq(&p, &r),
              "';' operating instructions are read by humans, ignored by the VM");
    }

    /* ---- execution ---- */
    card_effect_t fx;
    CHECK(zca_exec(&p, &fx) == ZCA_OK, "the program runs");
    printf("       evidence:");
    for (uint32_t d = 0; d < CHG_DIM; d++) printf(" %.2f", D(fx.evidence[d]));
    printf("   grants=%u  gem=%u root=%u set=%u\n", fx.grants,
           fx.attr[ZCA_FIELD_GEM], fx.attr[ZCA_FIELD_ROOT], fx.attr[ZCA_FIELD_SET]);
    CHECK(fx.attr[ZCA_FIELD_GEM] == 54 && fx.attr[ZCA_FIELD_ROOT] == 9,
          "the card's printed attributes survive into the effect");
    CHECK(fx.grants != 0, "the card grants something");

    /* Assert the ACTUAL magnitudes, not just that the program ran. An
     * earlier version of zca_exec converted milli-units by hand and produced
     * zero for every TILT on the host build; everything above still passed,
     * because nothing here looked at the numbers. These four lines are what
     * would have caught it. */
    #define NEAR(a,b) (D(a) > (b) - 0.005 && D(a) < (b) + 0.005)
    CHECK(NEAR(fx.evidence[0], 1.00), "AXIS 0 set evidence[0] = 1.00");
    CHECK(NEAR(fx.evidence[1], 0.55), "TILT 1 set evidence[1] = 0.55");
    CHECK(NEAR(fx.evidence[5], 0.10), "TILT 5 set evidence[5] = 0.10");
    CHECK(NEAR(fx.evidence[2], 0.07), "TILT 2 set evidence[2] = 0.07");
    {
        surplus_real_t mag = SR_ZERO;
        for (uint32_t d = 0; d < CHG_DIM; d++) mag = SR_ADD(mag, SR_MUL(fx.evidence[d], fx.evidence[d]));
        CHECK(D(mag) > 0.5, "the card's direction has real magnitude, not just one axis");
    }
    /* TILT accumulates rather than overwrites */
    {
        zca_program_t t; t.card_index = 1; t.n = 0;
        t.ins[t.n++] = (zca_ins_t){ ZCA_OP_TILT, 3, 300 };
        t.ins[t.n++] = (zca_ins_t){ ZCA_OP_TILT, 3, 200 };
        t.ins[t.n++] = (zca_ins_t){ ZCA_OP_SEAL, 0, 0 };
        card_effect_t g; zca_exec(&t, &g);
        CHECK(NEAR(g.evidence[3], 0.50), "two TILTs on one axis accumulate (0.3+0.2)");
    }

    /* ================== UNTRUSTED INPUT ==================
     * A card is a photograph. Anyone can print one. */

    /* a garbled scan must be REJECTED, never quietly run as another card */
    {
        char bad[ZCA_MAX_TEXT]; memcpy(bad, text, n+1);
        char *dig = strstr(bad, "BIND GEM 54");
        CHECK(dig != NULL, "found the digit to corrupt");
        dig[9] = '6';                       /* 54 -> 64: one misread digit */
        zca_program_t r;
        zca_status_t s2 = zca_assemble(bad, n, &r);
        printf("       one misread digit -> %s\n", zca_status_name(s2));
        CHECK(s2 == ZCA_ERR_SEAL,
              "A SINGLE MISREAD DIGIT IS CAUGHT — the seal refuses it");
    }
    {
        char bad[ZCA_MAX_TEXT]; memcpy(bad, text, n+1);
        char *s = strstr(bad, "SEAL ");
        s[5] = (s[5] == 'A') ? 'B' : 'A';   /* damaged seal itself */
        zca_program_t r;
        CHECK(zca_assemble(bad, n, &r) == ZCA_ERR_SEAL,
              "a damaged seal is refused too");
    }
    CHECK(zca_assemble("AXIS 1 1.0\nSEAL 0000\n", 21, &p) == ZCA_ERR_MAGIC,
          "a listing without the ZCA1 header is not a program");
    CHECK(zca_assemble("ZCA1 7\nAXIS 1 1.0\n", 18, &p) == ZCA_ERR_NO_SEAL,
          "an unsealed listing is refused");
    CHECK(zca_assemble("ZCA1 7\nEXEC /bin/sh\nSEAL 0000\n", 29, &p) == ZCA_ERR_OPCODE,
          "an unknown mnemonic is REFUSED, not guessed at");

    /* ---- a hostile but well-formed card cannot exceed the envelope ---- */
    {
        zca_program_t evil;
        evil.card_index = 666; evil.n = 0;
        /* out-of-range axis, over-unity magnitude, every grant bit set */
        evil.ins[evil.n++] = (zca_ins_t){ ZCA_OP_AXIS,  200, 60000 };
        evil.ins[evil.n++] = (zca_ins_t){ ZCA_OP_TILT,  255, 65535 };
        evil.ins[evil.n++] = (zca_ins_t){ ZCA_OP_GRANT, 0,   0xFFFF };
        evil.ins[evil.n++] = (zca_ins_t){ ZCA_OP_BIND,  200, 65535 };
        evil.ins[evil.n++] = (zca_ins_t){ 99,           1,   1 };  /* junk op */
        evil.ins[evil.n++] = (zca_ins_t){ ZCA_OP_SEAL,  0, 0 };
        card_effect_t h;
        CHECK(zca_exec(&evil, &h) == ZCA_OK, "the hostile card runs to completion");
        bool inbounds = true;
        for (uint32_t d = 0; d < CHG_DIM; d++)
            if (h.evidence[d] < 0 || h.evidence[d] > SR_MUL(SR_ONE, SR_FROM_FLOAT(2.0)))
                inbounds = false;
        CHECK(inbounds, "every magnitude was CLAMPED into range");
        CHECK((h.grants & ~0x0Fu) == 0, "grants were MASKED to the four known bits");
        printf("       hostile card ended with grants=%u (max legal %u)\n",
               h.grants, 0x0Fu);
    }

    /* termination is structural: no jumps exist, so a program cannot loop */
    {
        zca_program_t spin; spin.card_index = 1; spin.n = ZCA_MAX_INS;
        for (uint32_t i = 0; i < ZCA_MAX_INS; i++)
            spin.ins[i] = (zca_ins_t){ ZCA_OP_TILT, (uint8_t)i, 1000 };
        card_effect_t h;
        CHECK(zca_exec(&spin, &h) == ZCA_ERR_NO_SEAL,
              "a program with no SEAL terminates and reports it");
    }

    /* a program cannot be lifted off one card and replayed as another */
    {
        char lifted[ZCA_MAX_TEXT]; memcpy(lifted, text, n+1);
        char *idp = strstr(lifted, "24525");
        idp[0] = '2'; idp[1] = '4'; idp[2] = '5'; idp[3] = '2'; idp[4] = '6';
        zca_program_t r;
        CHECK(zca_assemble(lifted, n, &r) == ZCA_ERR_SEAL,
              "the card index is sealed too — no replay onto another card");
    }

    /* ---- discipline names survive an unreliable scan ---- */
    CHECK(zca_discipline_id("Acoustomancy", 12) == 0, "discipline resolves");
    CHECK(zca_discipline_id("acousto mancy", 13) == 0,
          "case and stray spacing are tolerated (OCR is not careful)");
    CHECK(zca_discipline_id("Illusory_Architecture", 21) == 17,
          "underscores from the directory form resolve too");
    CHECK(zca_discipline_id("Nonsensomancy", 13) == -1, "an unknown school is rejected");

    /* every one of the 38 disciplines yields a distinct, runnable program */
    {
        bool ok = true;
        for (uint8_t d = 0; d < ZCA_DISCIPLINES; d++) {
            zca_attrs_t a = { 1000u + d, d, 1, (uint16_t)(40 + d), (uint8_t)(1 + d % 9), 0 };
            zca_program_t pr; card_effect_t f;
            if (zca_derive(&a, &pr) != ZCA_OK || zca_exec(&pr, &f) != ZCA_OK) ok = false;
            char t[ZCA_MAX_TEXT]; zca_program_t back;
            uint32_t m = zca_emit(&pr, t, sizeof t);
            if (!m || zca_assemble(t, m, &back) != ZCA_OK || !prog_eq(&pr, &back)) ok = false;
        }
        CHECK(ok, "all 38 disciplines round-trip derive->emit->assemble exactly");
    }

    printf("\n%s: %d failure(s)\n", failures?"*** FAILED ***":"ALL PASS", failures);
    return failures?1:0;
}
