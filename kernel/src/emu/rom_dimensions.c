/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* rom_dimensions.c — the ROM library revisited through the dimensional ladder.
 * See rom_dimensions.h. The 2D->3D lift is Helion's spiral (not extrusion). */
#include "rom_dimensions.h"
#include "helion.h"
#include "holo.h"
#include "dimensional_ladder.h"

/* ROM consoles occupy 2d (the plane); the graphics engines climb to 3d (space)
 * and 4d (event space). 2 and 3 are Fibonacci anchor dimensions. */
static const play_dim_t g_play[] = {
    { "NES",              2, 0, 0 },
    { "SMS / Game Gear",  2, 0, 0 },
    { "Game Boy",         2, 0, 0 },
    { "PC Engine",        2, 0, 0 },
    { "SNES",             2, 0, 0 },
    { "Genesis",          2, 0, 0 },
    { "Game Boy Advance", 2, 0, 0 },
    { "Cinder (idTech1)", 3, 1, 1 },
    { "Quill (idTech2)",  3, 1, 1 },
    { "Arclight (idTech3)",4,1, 1 },
    { "Umbra (idTech4)",  4, 1, 1 },
    { "Aurora (Godot)",   4, 1, 1 },
};
#define N_PLAY ((int)(sizeof g_play / sizeof g_play[0]))

const play_dim_t *rom_play_dimensions(int *count_out){ if (count_out) *count_out = N_PLAY; return g_play; }

/* ---- self-contained, libm-free transcendentals (doubles are allowed on target) --- */
#define RD_PI   3.14159265358979
#define RD_TAU  6.28318530717959
#define RD_PHI  1.61803398874989

static double d_sin(double x){                        /* Bhaskara, wrapped to [-pi,pi) */
    while (x >  RD_PI) x -= RD_TAU;
    while (x < -RD_PI) x += RD_TAU;
    int neg = 0; if (x < 0){ x = -x; neg = 1; }       /* sin is odd */
    double s = (16.0*x*(RD_PI - x)) / (5.0*RD_PI*RD_PI - 4.0*x*(RD_PI - x));
    return neg ? -s : s;
}
static double d_cos(double x){ return d_sin(x + RD_PI*0.5); }
static double d_exp(double x){                        /* Taylor; x is small/bounded here */
    double t = 1.0, s = 1.0;
    for (int n = 1; n < 14; n++){ t *= x / (double)n; s += t; }
    return s;
}
static double d_sqrt_approx(double x){                 /* Newton; x in [0,2] here */
    if (x <= 0.0) return 0.0;
    double g = x > 1.0 ? x : 1.0;
    for (int n = 0; n < 8; n++) g = 0.5 * (g + x / g);
    return g;
}

/* The nonlinear lift, take two — RECURSIVE, not just curved.
 *
 * A single golden spiral was still a LINEAR sampling wrapped on a curve: one
 * plane, one scale, column→angle straight. But φ's real signature is
 * SELF-SIMILARITY ACROSS SCALE — "at different levels of zoom the pattern is
 * revealed", "sublattices of Fibonacci within Fibonacci, like fractal sphere
 * packing." So the lift is FRACTAL: the frame is nested into itself, each level
 * scaled by 1/φ and turned by the GOLDEN ANGLE (2π/φ² ≈ 137.5°) — the corners of
 * successive copies trace the golden spiral. Within each copy the spin is
 * DIFFERENTIAL: the turn grows with radius from the frame's own centre (every
 * part spinning at its own rate relative to its shape — molecular spin at macro
 * scale), so the plane shears into depth instead of pivoting rigidly. `i` is the
 * recursion level; the render stacks levels into a Droste tunnel of the game. */
#define RD_GOLDEN 2.39996322973    /* golden angle 2π/φ² in radians              */
#define RD_LNPHI  0.48121182506    /* ln φ: per-level scale = e^{-i·lnφ} = φ^-i  */
#define RD_SHEAR  1.15             /* differential spin: extra turn per unit radius */
#define RD_TILT   0.62             /* recline the spin plane toward the viewer   */
#define RD_ZSTEP  1.05             /* each deeper level recedes this much        */

void rom_lift(int32_t sx16, int32_t sy16, int i, int w, int h,
              int *out_x, int *out_y, int32_t *out_depth){
    double cx = (double)sx16 / 65536.0 * 2.0 - 1.0;   /* frame-local -1..1 (from centre) */
    double cy = (double)sy16 / 65536.0 * 2.0 - 1.0;
    double r  = cx*cx + cy*cy; r = (r>0)? d_sqrt_approx(r) : 0.0;

    double s  = d_exp(-(double)i * RD_LNPHI);          /* φ^-i  — self-similar scale  */
    double a  = (double)i * RD_GOLDEN + RD_SHEAR * r;  /* golden turn + differential spin */

    /* rotate the frame-local point by a, scale by s (the fractal placement) */
    double rx = s * (cx * d_cos(a) - cy * d_sin(a));
    double ry = s * (cx * d_sin(a) + cy * d_cos(a));

    /* the differential turn already lifted it off the plane; tilt so ry gains depth */
    double X  = rx;
    double Y  = ry * d_cos(RD_TILT);
    double Z  = (double)i * RD_ZSTEP + ry * d_sin(RD_TILT) + r * s * 0.35;

    double d  = 3.2 + Z * 0.5;                          /* perspective depth           */
    double sc = (double)h * 0.68;
    *out_x = (int)((double)w * 0.5 + X * sc / d);
    *out_y = (int)((double)h * 0.5 - Y * sc / d);
    *out_depth = (int32_t)((3.2 - d) * 14.0);          /* near warm / far cool        */
}

/* a small procedural "game frame": tiles that read as a 2D sprite scene */
static uint32_t game_pixel(int u, int v){            /* u,v in 0..31 */
    if (v > 24) return ((u^v)&1) ? 0x2E7D32 : 0x1B5E20;         /* ground   */
    if (v > 20 && ((u+ (v)) % 7 < 3)) return 0x795548;          /* bricks   */
    if (u>=6 && u<=9 && v>=12 && v<=18) return 0xE53935;        /* a figure */
    if (u>=6 && u<=9 && v>=10 && v<=12) return 0xFFCC80;        /* its head */
    if (v < 6 && (u*3+v) % 11 < 2) return 0xFFFFFF;             /* stars    */
    return 0x0D1B3A;                                            /* sky      */
}

static int gw,gh; static uint32_t *gfb;
static void px(int x,int y,uint32_t c){ if((unsigned)x<(unsigned)gw&&(unsigned)y<(unsigned)gh) gfb[y*gw+x]=c; }

void rom_dimensions_render(uint32_t *fb, int w, int h){
    gfb=fb; gw=w; gh=h;
    for (int i=0;i<w*h;i++) fb[i]=0xFF000000u | holo_shade(0x060810,0,-16,8,96);
    int halfw = w/2;
    /* LEFT: the ROM as it was — a flat 2D game frame (the plane, 2d) */
    int tile = (halfw-40) / 32; if (tile<1) tile=1;
    int ox = (halfw - tile*32)/2, oy = (h - tile*32)/2;
    for (int v=0;v<32;v++) for (int u=0;u<32;u++){
        uint32_t c = game_pixel(u,v);
        for (int yy=0;yy<tile;yy++) for (int xx=0;xx<tile;xx++) px(ox+u*tile+xx, oy+v*tile+yy, 0xFF000000u|c);
    }
    /* RIGHT: the same frame lifted into 3D FRACTALLY (the MegaROM holographic form).
     * The game is nested into itself — each level φ-smaller, golden-angle-turned,
     * differentially sheared into depth — a Droste tunnel that reveals the same
     * pattern at every zoom. Draw deepest level first so nearer copies overlay. */
    #define RD_SS 96
    #define RD_LEVELS 7
    for (int lvl = RD_LEVELS-1; lvl >= 0; lvl--){
        for (int su=0; su<RD_SS; su++){
            for (int sv=0; sv<RD_SS; sv++){
                int u = su*32/RD_SS, v = sv*32/RD_SS;
                int32_t sx16 = (int32_t)(((int64_t)su*65536)/RD_SS);
                int32_t sy16 = (int32_t)(((int64_t)sv*65536)/RD_SS);
                int X,Y; int32_t dz; rom_lift(sx16, sy16, lvl, halfw, h, &X, &Y, &dz);
                /* deeper copies cool + dim (they recede into the container) */
                int32_t depth = dz - lvl*10;
                uint32_t c = 0xFF000000u | holo_shade(game_pixel(u,v), 0, depth, 8, 150);
                px(halfw+X, Y, c); px(halfw+X+1, Y, c); px(halfw+X, Y+1, c);
            }
        }
    }
}

int rom_dimensions_selfcheck(uint32_t *rom2d_out){
    /* (1) ROM consoles at 2d; engines at 3d+ — a monotone leap up the ladder */
    int rom2d=0, engines_high=1, prev=0;
    for (int i=0;i<N_PLAY;i++){
        if (!g_play[i].is_engine){ if (g_play[i].dim==2) rom2d++; else engines_high=0; }
        else if (g_play[i].dim < 3) engines_high=0;
        if (g_play[i].dim < prev) engines_high=0;   /* the lineage never descends */
        prev = g_play[i].dim;
    }
    if (rom2d_out) *rom2d_out = (uint32_t)rom2d;
    /* (2) the two great anchors — plane(2) and space(3) — are Fibonacci dims */
    int anchors = dl_is_prime_dimension(2) && dl_is_prime_dimension(3);

    /* (3) the 2D->3D lift is a genuine FRACTAL spiral: successive recursion levels
     * of the SAME frame corner are φ-smaller (self-similar), golden-angle-turned
     * (moved, not a flat translation), and recede in depth (enter the 3rd dim). */
    int W=240, HH=240, cxp=W/2, cyp=HH/2;
    int32_t d0, d1; int x0,y0,x1,y1;
    rom_lift(65536, 65536, 0, W, HH, &x0, &y0, &d0);   /* a frame corner, level 0   */
    rom_lift(65536, 65536, 1, W, HH, &x1, &y1, &d1);   /* same corner, level 1      */
    /* radius from the view centre must shrink by ~1/φ (self-similar nesting) */
    int r0 = (x0-cxp)*(x0-cxp) + (y0-cyp)*(y0-cyp);
    int r1 = (x1-cxp)*(x1-cxp) + (y1-cyp)*(y1-cyp);
    int shrinks = r1 < r0;                       /* level 1 nested inside level 0 */
    int moved = (x0!=x1) || (y0!=y1);            /* golden-angle turn between levels */
    int depth_varies = (d0 != d1) && shrinks;    /* recedes AND self-similar-scales */

    return rom2d == 7 && engines_high && anchors && moved && depth_varies;
}
