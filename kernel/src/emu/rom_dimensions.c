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

/* The nonlinear lift. The old approach spun the plane RIGIDLY by a LINEAR angle
 * (angle proportional to the column) — a flat plane pivoting edge-on, which
 * projects to a streak. The spiral is not linear: it is the GOLDEN LOGARITHMIC
 * spiral, the φ recurrence of the whole ladder. The frame's width is ROLLED into
 * it — radius grows by φ per quarter turn (ρ = ρ0·e^{bθ}, b = lnφ/(π/2)) while the
 * angle sweeps continuously — so the plane winds into a self-similar scroll with
 * genuine volume. The row (sy16) is the preserved vertical; the whole scroll is
 * tilted so its coils recede as real depth (perpendicular to the spin plane). */
#define RD_TURNS 1.35
#define RD_R0    0.14
#define RD_BGROW 0.30634963      /* lnφ / (π/2): radius ×φ every quarter turn */
#define RD_TILT  0.62            /* recline the spin plane toward the viewer   */

void rom_lift(int32_t sx16, int32_t sy16, int i, int w, int h,
              int *out_x, int *out_y, int32_t *out_depth){
    (void)i;
    double t   = (double)sx16 / 65536.0;              /* 0..1 across the frame width  */
    double vh  = (double)sy16 / 65536.0 - 0.5;        /* -0.5..0.5 up the frame       */
    double ang = t * RD_TURNS * RD_TAU;               /* continuous sweep (nonlinear) */
    double rho = RD_R0 * d_exp(RD_BGROW * ang);       /* golden-spiral radius (×φ/qtr) */

    /* the roll lives in the X–Z (ground) plane; the row is the vertical Y */
    double X = rho * d_cos(ang);
    double Zr = rho * d_sin(ang);
    double Y0 = vh * 1.7;
    /* tilt the spin plane about X so its coils lift up-screen and recede in depth */
    double Y = Y0 * d_cos(RD_TILT) - Zr * d_sin(RD_TILT);
    double Z = Y0 * d_sin(RD_TILT) + Zr * d_cos(RD_TILT);

    double d  = 4.2 + Z * 0.55;                        /* perspective depth            */
    double sc = (double)h * 0.62;
    *out_x = (int)((double)w * 0.5 + X * sc / d);
    *out_y = (int)((double)h * 0.5 - Y * sc / d);
    *out_depth = (int32_t)((2.6 - d) * 16.0);          /* near warm / far cool         */
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
    /* RIGHT: the same frame ROLLED into 3D by the golden spiral (the MegaROM form).
     * The frame's width winds into a self-similar scroll (radius ×φ per quarter
     * turn); the outer coils spread, so oversample the width to keep them filled. */
    #define RD_SS 128
    for (int su=0; su<RD_SS; su++){
        for (int sv=0; sv<RD_SS; sv++){
            int u = su*32/RD_SS, v = sv*32/RD_SS;
            int32_t sx16 = (int32_t)(((int64_t)su*65536)/RD_SS);
            int32_t sy16 = (int32_t)(((int64_t)sv*65536)/RD_SS);
            int X,Y; int32_t dz; rom_lift(sx16, sy16, u, halfw, h, &X, &Y, &dz);
            uint32_t c = 0xFF000000u | holo_shade(game_pixel(u,v), 0, dz, 8, 150);
            px(halfw+X, Y, c); px(halfw+X+1, Y, c); px(halfw+X, Y+1, c);   /* fill gaps */
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

    /* (3) the 2D->3D lift is a genuine spiral: a 2D screen row lifted over steps
     * gains real 3D spread (not a flat translation) and its depth cue varies. */
    int32_t d0; int x0,y0,xn,yn; int32_t dn;
    rom_lift( 8192, 32768, 0, 240, 240, &x0, &y0, &d0);   /* an inner-coil column  */
    rom_lift(57344, 32768, 0, 240, 240, &xn, &yn, &dn);   /* an outer-coil column  */
    int moved = (x0!=xn) || (y0!=yn);           /* the width wound (golden roll) */
    int depth_varies = (d0 != dn);              /* coils recede in depth (3rd d) */

    return rom2d == 7 && engines_high && anchors && moved && depth_varies;
}
