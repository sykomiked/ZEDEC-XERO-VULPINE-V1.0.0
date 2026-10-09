/* test_icon.c — themed vector icons: rendering, theming, and per-system uniqueness. */
#include <stdio.h>
#include <string.h>
#include "icon.h"

static int failures = 0;
#define CHECK(c,m) do{ if(!(c)){printf("[FAIL] %s\n",m);failures++;} \
    else printf("[PASS] %s\n",m);}while(0)

#define SZ 32u
static uint32_t buf[SZ*SZ];

static uint32_t count_color(const uint32_t *b, uint32_t rgb) {
    uint32_t n = 0; for (uint32_t i = 0; i < SZ*SZ; i++) if (b[i]==rgb) n++; return n;
}
static uint32_t sig(const uint32_t *b) { /* cheap render signature */
    uint32_t h = 2166136261u; for (uint32_t i=0;i<SZ*SZ;i++){h^=b[i];h*=16777619u;} return h;
}

int main(void) {
    printf("=== themed vector icons ===\n");
    theme_t th; theme_init(&th);

    /* ---- a hand-drawn icon renders its expected colours ---- */
    icon_t ic;
    icon_for("kernel", &ic);
    CHECK(icon_is_builtin("kernel"), "kernel has a hand-drawn icon");
    icon_render(&ic, &th, buf, SZ);
    CHECK(count_color(buf, theme_get(&th, THEME_GOLD)) > 0,
          "the dragon-eye kernel icon paints GOLD (the iris)");
    CHECK(count_color(buf, theme_get(&th, THEME_SCALE)) > 0, "and SCALE (the outer eye)");
    CHECK(buf[0] == theme_get(&th, THEME_VOID), "the corner is the VOID background");

    /* ---- the customization mechanism: retheme recolours the icon ---- */
    {
        uint32_t gold_before = count_color(buf, 0xFFD700);
        theme_set(&th, THEME_GOLD, 0x00FF88);          /* user recolours GOLD */
        icon_render(&ic, &th, buf, SZ);
        CHECK(count_color(buf, 0xFFD700) == 0 && count_color(buf, 0x00FF88) == gold_before,
              "recolouring the GOLD token restyles the icon's gold pixels — the "
              "whole interface is customizable because icons draw through tokens");
        theme_reset(&th, THEME_GOLD);
        CHECK(theme_get(&th, THEME_GOLD) == 0xFFD700, "and a reset restores the default");
    }

    /* ---- every system has an icon; two systems differ ---- */
    {
        icon_t a, b;
        icon_for("mage", &a);       CHECK(a.n_ops > 0, "mage has an icon");
        icon_for("trispace", &b);   CHECK(b.n_ops > 0, "trispace has an icon");
        uint32_t sa, sb;
        icon_render(&a, &th, buf, SZ); sa = sig(buf);
        icon_render(&b, &th, buf, SZ); sb = sig(buf);
        CHECK(sa != sb, "the mage and trispace icons render differently");
    }

    /* ---- EVERYTHING has an icon: a module with no hand-drawn one still gets a
     *      distinct procedural icon, and two names differ ---- */
    {
        icon_t p, q;
        CHECK(!icon_is_builtin("some_obscure_module"), "an obscure module has no hand-drawn icon");
        icon_for("some_obscure_module", &p);
        CHECK(p.n_ops >= 2, "but icon_for still returns a real icon for it (procedural)");
        icon_for("another_module", &q);
        uint32_t sp, sq;
        icon_render(&p, &th, buf, SZ); sp = sig(buf);
        icon_render(&q, &th, buf, SZ); sq = sig(buf);
        CHECK(sp != sq, "two different module names get two different icons");

        /* deterministic: the same name always yields the same icon */
        icon_t p2; icon_for("some_obscure_module", &p2);
        CHECK(memcmp(&p, &p2, sizeof p) == 0, "the same name yields an identical icon");

        /* every op paints a visible ink over the panel bg — nothing invisible */
        icon_render(&p, &th, buf, SZ);
        CHECK(count_color(buf, theme_get(&th, THEME_PANEL)) < SZ*SZ,
              "a procedural icon actually paints something over its tile");
    }

    /* ---- hat pigments ---- */
    CHECK(theme_hat_color(HATCOL_RED) == 0xCC2233, "the red hat has its red pigment");
    CHECK(theme_hat_color(HATCOL_PURPLE) != theme_hat_color(HATCOL_BLUE),
          "purple and blue hats are distinct colours");

    /* ---- render at multiple sizes without overrunning (ASan gate) ---- */
    {
        static uint32_t big[128*128];
        for (uint32_t s = 1; s <= 128; s += 7) {
            icon_t k; icon_for("net", &k);
            icon_render(&k, &th, big, s);
        }
        CHECK(1, "rendering at sizes 1..128 never overruns the buffer");
    }

    /* ---- a run over EVERY hand-drawn icon renders cleanly ---- */
    {
        const char *names[] = {"kernel","net","tls","mage","reality","trispace",
                               "chiglet","cards","holodeck","wallet","denconnect","browser"};
        bool all = true;
        for (unsigned i = 0; i < sizeof names/sizeof names[0]; i++) {
            icon_t k; icon_for(names[i], &k);
            icon_render(&k, &th, buf, SZ);
            if (count_color(buf, theme_get(&th, THEME_VOID)) == SZ*SZ) all = false; /* all-bg = empty */
        }
        CHECK(all, "all 12 hand-drawn icons render non-empty");
    }

    printf("\n%s: %d failure(s)\n", failures?"*** FAILED ***":"ALL PASS", failures);
    return failures?1:0;
}
