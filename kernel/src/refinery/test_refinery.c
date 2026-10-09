/* test_refinery.c — the Magitech Refinery.
 *
 * The headline fixture is byte-parity with the shipped deck: forging
 * "OLPIRT HPOU" must reproduce card 24525's gematria (54, printed §54 on
 * the physical card), root (9), star ({12/5}, measured on the raster),
 * and the EXACT 16-cell kamea path derived from SHA-256 by the deck's own
 * generator. The pixel extractor found only 14 of these 16 nodes — two
 * sit under star ink — which is the whole argument for deriving from
 * text and using imagery only as a check.
 */
#include <stdio.h>
#include <string.h>
#include "refinery.h"

static int failures = 0;
#define CHECK(c,m) do{ if(!(c)){printf("[FAIL] %s\n",m);failures++;} \
    else printf("[PASS] %s\n",m);}while(0)

/* SHA-256("OLPIRT HPOU") = 927de563..., hex digits taken in order, first
 * occurrence of each value, v -> (v%5, v/5). Computed by the generator's
 * own algorithm; frozen here as ground truth. */
static const uint8_t TRUTH[16][2] = {
    {4,1},{2,0},{2,1},{3,2},{4,2},{0,1},{1,1},{3,0},
    {0,2},{1,0},{0,3},{2,2},{0,0},{3,1},{4,0},{1,2}
};

int main(void) {
    printf("=== Magitech Refinery: text -> sigil -> shareable card ===\n");

    /* ---------------- the language layer ---------------- */
    CHECK(eno_gematria("OLPIRT HPOU", 11) == 54,
          "gematria(OLPIRT HPOU) = 54 — matches the printed section mark");
    CHECK(eno_root(54) == 9, "digital root 54 -> 9");
    CHECK(eno_root(0) == 9, "root of nothing is 9, matching the pipeline");
    CHECK(eno_gematria("olpirt hpou", 11) == 54, "case folds");
    /* Allographs: J->I, K->C, W->U. These three are UNATTESTED — J, K and
     * W occur zero times in the 52,095-card corpus — so this asserts the
     * kernel's reconstruction, not the deck's behaviour. */
    CHECK(eno_gematria("JKW", 3) == eno_gematria("ICU", 3),
          "allographs J,K,W fold to I,C,U (unattested by the deck)");
    /* Y is NOT an allograph. The deck is authoritative here and it speaks:
     * all 15 Y-bearing cards score Y as itself (zero), none score Y->I.
     * Card 17 'GNAY' is printed 15; under the old Y->I fold the kernel
     * said 18. G1+N8+A6+Y0 = 15. */
    CHECK(eno_gematria("GNAY", 4) == 15,
          "Y scores as itself: card 17 'GNAY' = 15, matching the printed deck");
    CHECK(eno_root(eno_gematria("GNAY", 4)) == 6,
          "and its root is 6, as printed on card 17");
    CHECK(eno_letter_value('Q') == 10, "Q carries 10 (Ger)");
    CHECK(eno_root_mirror(1) == 8 && eno_root_mirror(7) == 2 &&
          eno_root_mirror(9) == 9,
          "mirror pairs: 1<->8, 2<->7, 9 self-reflective");
    CHECK(strcmp(eno_root_domain(7), "Heptadic - Movement") == 0,
          "root 7 names Movement — OLPIRT 'light' is a root-7 seed in the grammar");

    /* the three voices carry the engine's law */
    CHECK(strstr(eno_law(ENO_VOICE_SOLAR), "covenant") != NULL,
          "SOLAR law ends in covenant (Wizard's Compendium)");
    CHECK(strstr(eno_law(ENO_VOICE_LUNAR), "union") != NULL,
          "LUNAR law ends in union (Witch's Grimoire)");
    CHECK(strstr(eno_law(ENO_VOICE_AEON), "transmutation") != NULL,
          "AEON law ends in transmutation (Alchemist's Tome)");

    /* ---------------- parity with the shipped deck ---------------- */
    ref_card_t c;
    CHECK(ref_forge("OLPIRT HPOU", 11, ENO_VOICE_AEON, &c) == REF_OK,
          "OLPIRT HPOU forges");
    printf("       gematria=%u root=%u star={%u/%u} path=%u nodes\n",
           c.gematria, c.root, c.sigil.fab_n, c.sigil.fab_k, c.path_len);
    CHECK(c.gematria == 54 && c.root == 9, "gematria 54, root 9");
    CHECK(c.sigil.fab_n == 12 && c.sigil.fab_k == 5,
          "root 9 -> star {12/5}, as measured on the physical card");
    CHECK(c.path_len == 16, "the true path has 16 nodes (pixel scan saw 14)");
    bool exact = true;
    for (uint32_t i = 0; i < 16u; i++) {
        const sig_pt_t *p = &c.sigil.node[c.path[i]];
        if (p->col != TRUTH[i][0] || p->row != TRUTH[i][1]) exact = false;
    }
    CHECK(exact, "BYTE PARITY: the kamea path equals the deck generator's, "
                 "cell for cell, IN ORDER");
    CHECK(c.sigil.n_edges == 15, "15 strokes joining 16 nodes");
    CHECK(sig_components(&c.sigil) == 1, "one continuous trace");

    /* the schedule exists and is entered at the path's start */
    sig_schedule_t sc;
    CHECK(sig_schedule(&c.sigil, c.path[0], &sc) && sc.n_scheduled == 16,
          "the circuit schedules from the trace's true entry");

    /* ---------------- determinism and sensitivity ---------------- */
    ref_card_t c2;
    ref_forge("OLPIRT HPOU", 11, ENO_VOICE_AEON, &c2);
    CHECK(memcmp(c.digest, c2.digest, 32) == 0 && c2.path_len == c.path_len,
          "same intent, same card — on any device");
    ref_forge("OLPIRT HPOV", 11, ENO_VOICE_AEON, &c2);
    bool differs = memcmp(c.digest, c2.digest, 32) != 0;
    CHECK(differs, "one letter changed -> a different circuit entirely");

    /* ---------------- any language in ---------------- */
    {
        ref_card_t k;
        /* Devanagari, no Latin letters at all: gematria 0 -> root 9 */
        const char *hindi = "\xE0\xA4\xB6\xE0\xA4\x95\xE0\xA5\x8D\xE0\xA4\xA4\xE0\xA4\xBF";
        CHECK(ref_forge(hindi, (uint32_t)strlen(hindi), ENO_VOICE_LUNAR, &k) == REF_OK,
              "Devanagari intent forges");
        CHECK(k.root == 9 && k.path_len >= 10,
              "no foldable letters -> root 9, and the hash still draws a full circuit");
        ref_card_t k2;
        ref_forge(hindi, (uint32_t)strlen(hindi), ENO_VOICE_LUNAR, &k2);
        CHECK(memcmp(k.digest, k2.digest, 32) == 0, "and it is deterministic too");
    }

    /* ---------------- fabric lanes from the root ---------------- */
    {
        /* root 6 -> {9/3}: gcd 3 — a three-lane fabric. The generator inks
         * one orbit (a triangle); the other two lanes exist unlit. */
        /* Fixture: shipped card 37009 'ARH ABL' (Conceptual Attacks),
         * printed gematria 33, printed root 6. It was 'GNAY VRS', which
         * reached root 6 only via the Y->I fold the deck refutes; with Y
         * scored as itself that string is 21 / root 3 and the assertion
         * below would have fallen through its own else-branch into a
         * vacuous pass. A real root-6 deck row keeps the coverage and
         * makes the number checkable against print. */
        ref_card_t r6;
        ref_forge("ARH ABL", 7, ENO_VOICE_SOLAR, &r6);
        printf("       '%s' -> gematria %u, root %u, star {%u/%u}, lanes %u\n",
               "ARH ABL", r6.gematria, r6.root, r6.sigil.fab_n, r6.sigil.fab_k,
               sig_fabric_lanes(&r6.sigil));
        CHECK(r6.gematria == 33 && r6.root == 6,
              "card 37009 'ARH ABL' = 33 / root 6, as printed");
        CHECK(r6.sigil.fab_n == 9 && r6.sigil.fab_k == 3 &&
              sig_fabric_lanes(&r6.sigil) == 3,
              "root 6 -> {9/3} -> three parallel lanes");
    }

    /* ---------------- rendering ---------------- */
    {
        ref_stroke_t s[REF_MAX_STROKES];
        uint32_t n = ref_render(&c, false, s, REF_MAX_STROKES);
        printf("       render: %u strokes (day)\n", n);
        CHECK(n >= 30, "the seal renders to a full stroke list");
        uint32_t star = 0, trace = 0, dots = 0;
        bool inbounds = true;
        for (uint32_t i = 0; i < n; i++) {
            if (s[i].layer == 1 && s[i].kind == REF_S_LINE) star++;
            if (s[i].layer == 2) trace++;
            if (s[i].layer == 3 && s[i].kind != REF_S_TEXT) dots++;
            if (s[i].x1 > 1000 || s[i].y1 > 1400) inbounds = false;
        }
        CHECK(star == 12, "the {12/5} star inks 12 chords (one closed orbit)");
        CHECK(trace == 15, "the circuit inks its 15 strokes");
        CHECK(dots == 2, "start dot + end circle mark the trace's direction");
        CHECK(inbounds, "every stroke fits the card");

        ref_stroke_t sn[REF_MAX_STROKES];
        uint32_t nn = ref_render(&c, true, sn, REF_MAX_STROKES);
        CHECK(nn == n, "night render emits the same geometry");
        CHECK(sn[4].rgb != s[4].rgb || sn[0].rgb != s[0].rgb,
              "night palette differs (midnight emerald field, gold fabric)");
        /* root 9 traces in tyrian purple — the house colour */
        bool tyrian = false;
        for (uint32_t i = 0; i < n; i++)
            if (s[i].layer == 2 && s[i].rgb == 0x66023Cu) tyrian = true;
        CHECK(tyrian, "root 9's spectrum hue is tyrian purple");
    }

    /* ---------------- the activation line, in voice ---------------- */
    {
        char line[220];
        uint32_t n = ref_activation_line(&c, line, sizeof line);
        printf("       %s\n", line);
        CHECK(n > 40 && strstr(line, "Activate") && strstr(line, "gematria 54"),
              "AEON voice: 'Activate... Vibrate... with presence'");
        ref_card_t sol; ref_forge("OLPIRT HPOU", 11, ENO_VOICE_SOLAR, &sol);
        ref_activation_line(&sol, line, sizeof line);
        CHECK(strstr(line, "Invoke") != NULL, "SOLAR voice says Invoke");
    }

    /* ---------------- the shareable AI preset ---------------- */
    {
        uint8_t blob[REF_PRESET_MAX];
        uint32_t bl = ref_preset_pack(&c, blob, sizeof blob);
        printf("       preset blob: %u bytes\n", bl);
        CHECK(bl > 0 && bl <= REF_PRESET_MAX, "the card packs into a preset");

        ref_card_t back;
        CHECK(ref_preset_unpack(blob, bl, &back) == REF_OK,
              "a peer unpacks it and re-derives the same card");
        CHECK(back.gematria == 54 && back.root == 9 && back.path_len == 16,
              "the re-derived card is identical");
        bool fx_same = true;
        for (uint32_t d = 0; d < CHG_DIM; d++)
            if (back.fx.evidence[d] != c.fx.evidence[d]) fx_same = false;
        CHECK(fx_same, "the Chiglet effect survives sharing exactly — "
                       "the preset IS the AI configuration");

        /* forgery: claim a different root */
        uint8_t evil[REF_PRESET_MAX]; memcpy(evil, blob, bl);
        evil[5] = 3;                                    /* claimed root */
        uint16_t s2 = 0xFFFF;                            /* re-seal it */
        for (uint32_t i = 0; i + 2u < bl; i++) {
            s2 ^= (uint16_t)evil[i] << 8;
            for (int b = 0; b < 8; b++)
                s2 = (s2 & 0x8000u) ? (uint16_t)((s2 << 1) ^ 0x1021u)
                                    : (uint16_t)(s2 << 1);
        }
        evil[bl-2] = (uint8_t)(s2 >> 8); evil[bl-1] = (uint8_t)s2;
        CHECK(ref_preset_unpack(evil, bl, &back) == REF_ERR_FORGERY,
              "a WELL-SEALED blob with a false claim is still refused — "
              "authority lives in the derivation, not the blob");

        /* corruption: flip one text byte, leave the seal */
        memcpy(evil, blob, bl); evil[14] ^= 1;
        CHECK(ref_preset_unpack(evil, bl, &back) == REF_ERR_SEAL,
              "one flipped byte and the seal refuses it");
        CHECK(ref_preset_unpack(blob, bl - 3u, &back) != REF_OK,
              "a truncated blob is refused");
    }


    /* two different intents with the same gematria+root must still be
     * DISTINCT cards (the synthetic id comes from the digest, and a
     * collision would block equipping the second preset) */
    {
        ref_card_t a, b;
        /* "AB" and "BA" share letters, hence gematria and root */
        ref_forge("AB", 2, ENO_VOICE_AEON, &a);
        ref_forge("BA", 2, ENO_VOICE_AEON, &b);
        CHECK(a.gematria == b.gematria && a.root == b.root,
              "AB and BA share gematria and root (the collision setup)");
        uint32_t ia = ((uint32_t)a.digest[3]<<24)|((uint32_t)a.digest[4]<<16)|((uint32_t)a.digest[5]<<8)|a.digest[6];
        uint32_t ib = ((uint32_t)b.digest[3]<<24)|((uint32_t)b.digest[4]<<16)|((uint32_t)b.digest[5]<<8)|b.digest[6];
        CHECK(ia != ib, "yet their card ids differ — both presets can be equipped");
    }

    /* ---------------- guards ---------------- */
    CHECK(ref_forge(NULL, 5, ENO_VOICE_AEON, &c) == REF_ERR_ARG, "null text refused");
    CHECK(ref_forge("x", 0, ENO_VOICE_AEON, &c) == REF_ERR_TEXT, "empty intent refused");
    {
        char big[REF_TEXT_MAX + 2];
        memset(big, 'A', sizeof big); big[sizeof big - 1] = 0;
        CHECK(ref_forge(big, REF_TEXT_MAX + 1, ENO_VOICE_AEON, &c) == REF_ERR_TEXT,
              "oversized intent refused");
    }

    printf("\n%s: %d failure(s)\n", failures?"*** FAILED ***":"ALL PASS", failures);
    return failures?1:0;
}
