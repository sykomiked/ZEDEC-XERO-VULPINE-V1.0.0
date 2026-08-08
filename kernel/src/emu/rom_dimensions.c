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

/* The lift, done right. The rule of thumb for adding a dimension: you add a NEW
 * AXIS at a right angle to the image — and that perpendicular axis runs along the
 * IMAGINARY number line. It is NOT visible in the flat 2D (every object sits at
 * imaginary = 0), yet it IS the depth of the 3D. So we DON'T warp the picture:
 * every object keeps its original linear 2D mapping. We add only a per-object
 * rotation that is a SPIRAL SPIN INTO that imaginary axis —
 *
 *     x  +  i·0   --spin θ-->   x·cosθ  +  i·(x·sinθ)
 *
 * the real part (x·cosθ) stays in the visible plane; the imaginary part (x·sinθ)
 * becomes depth. Each object spins by its own θ, the angles PROGRESSING object to
 * object so the layers fan out in a spiral formation and pop out of the plane.
 * This is the system's complex-plane axiom (perpendicular real·imaginary) applied
 * to graphics. `i` selects the object. */
#define RD_THETA0  0.34     /* spin of the frontmost object                        */
#define RD_DTHETA  0.46     /* extra spin per object back — the spiral of angles   */
#define RD_DZ      0.58     /* per-object depth stagger (the pop-up-book layers)   */
#define RD_VIEW    0.64     /* camera yaw so the imaginary/depth axis reads on screen */
#define RD_NEAR    3.3

/* The scene decomposes into OBJECTS (a pop-up book's layers), back-to-front. */
enum { OBJ_SKY, OBJ_STARS, OBJ_GROUND, OBJ_BRICK, OBJ_BODY, OBJ_HEAD, N_OBJ };

/* a small procedural "game frame": tiles that read as a 2D sprite scene */
static uint32_t game_pixel(int u, int v){            /* u,v in 0..31 */
    if (v > 24) return ((u^v)&1) ? 0x2E7D32 : 0x1B5E20;         /* ground   */
    if (v > 20 && ((u+ (v)) % 7 < 3)) return 0x795548;          /* bricks   */
    if (u>=6 && u<=9 && v>=12 && v<=18) return 0xE53935;        /* a figure */
    if (u>=6 && u<=9 && v>=10 && v<=12) return 0xFFCC80;        /* its head */
    if (v < 6 && (u*3+v) % 11 < 2) return 0xFFFFFF;             /* stars    */
    return 0x0D1B3A;                                            /* sky      */
}
/* which OBJECT a pixel belongs to — SAME precedence as game_pixel so colours align */
static int game_object(int u, int v){
    if (v > 24) return OBJ_GROUND;
    if (v > 20 && ((u+v) % 7 < 3)) return OBJ_BRICK;
    if (u>=6 && u<=9 && v>=12 && v<=18) return OBJ_BODY;
    if (u>=6 && u<=9 && v>=10 && v<=12) return OBJ_HEAD;
    if (v < 6 && (u*3+v) % 11 < 2) return OBJ_STARS;
    return OBJ_SKY;
}
static double g_ocx[N_OBJ], g_ocy[N_OBJ]; static int g_obj_ready = 0;
static void obj_centroids(void){                     /* each object's 2D centre */
    if (g_obj_ready) return;
    double sx[N_OBJ]={0}, sy[N_OBJ]={0}; int n[N_OBJ]={0};
    for (int v=0;v<32;v++) for (int u=0;u<32;u++){ int k=game_object(u,v); sx[k]+=u; sy[k]+=v; n[k]++; }
    for (int k=0;k<N_OBJ;k++){ if(n[k]){ g_ocx[k]=sx[k]/n[k]; g_ocy[k]=sy[k]/n[k]; } else { g_ocx[k]=16; g_ocy[k]=16; } }
    g_obj_ready = 1;
}

void rom_lift(int32_t sx16, int32_t sy16, int i, int w, int h,
              int *out_x, int *out_y, int32_t *out_depth){
    obj_centroids();
    int k = i; if (k < 0) k = 0; if (k >= N_OBJ) k = N_OBJ-1;

    /* the object's own 2D frame — its linear mapping is left UNTOUCHED */
    double u  = (double)sx16 / 65536.0 * 32.0;
    double vv = (double)sy16 / 65536.0 * 32.0;
    double lx = (u  - g_ocx[k]) / 16.0;               /* real X, about the object centre */
    double ly = (vv - g_ocy[k]) / 16.0;               /* real Y — the axis left flat     */

    /* spiral spin of THIS object into the imaginary (perpendicular) axis */
    double theta = RD_THETA0 + (double)k * RD_DTHETA;
    double xr = lx * d_cos(theta);                    /* real part — stays in the plane  */
    double wr = lx * d_sin(theta);                    /* imaginary part — becomes depth  */

    /* place the object at its ORIGINAL 2D location; the depth is the new axis */
    double Xw = (g_ocx[k]-16.0)/16.0 + xr;
    double Yw = (g_ocy[k]-16.0)/16.0 + ly;
    double Zw = (double)(N_OBJ-1-k) * RD_DZ + wr;     /* SKY farthest, HEAD nearest + spun depth */

    /* a small camera yaw so the (invisible-in-2D) imaginary axis reads as depth */
    double Xc =  Xw * d_cos(RD_VIEW) + Zw * d_sin(RD_VIEW);
    double Zc = -Xw * d_sin(RD_VIEW) + Zw * d_cos(RD_VIEW);

    double d  = RD_NEAR + Zc;
    double sc = (double)h * 0.92;
    *out_x = (int)((double)w * 0.5 + Xc * sc / d);
    *out_y = (int)((double)h * 0.5 - Yw * sc / d);
    *out_depth = (int32_t)(-Zc * 22.0);               /* near (small Zc) warm / far cool */
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
    /* RIGHT: the SAME objects, each still its own flat 2D sprite, but spun into the
     * imaginary/perpendicular axis by its own angle — the layers fan out in a
     * spiral and pop out of the plane (the MegaROM holographic form). Draw the
     * objects back-to-front so nearer layers overlay the farther ones. */
    #define RD_SS 224
    for (int k=0; k<N_OBJ; k++){
        for (int su=0; su<RD_SS; su++){
            for (int sv=0; sv<RD_SS; sv++){
                int u = su*32/RD_SS, v = sv*32/RD_SS;
                if (game_object(u,v) != k) continue;   /* only this object's pixels */
                int32_t sx16 = (int32_t)(((int64_t)su*65536)/RD_SS);
                int32_t sy16 = (int32_t)(((int64_t)sv*65536)/RD_SS);
                int X,Y; int32_t dz; rom_lift(sx16, sy16, k, halfw, h, &X, &Y, &dz);
                uint32_t c = 0xFF000000u | holo_shade(game_pixel(u,v), 0, dz, 8, 150);
                for (int yy=0; yy<2; yy++) for (int xx=0; xx<2; xx++) px(halfw+X+xx, Y+yy, c);
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

    /* (3) the 2D->3D lift adds a PERPENDICULAR (imaginary) axis, invisible in the
     * flat image but the depth of the 3D, and spins each object into it —
     *   - different objects, spun in spiral formation, land at different depths
     *     (the scene pops out; the perpendicular axis is real in 3D);            */
    int W=240, HH=240;
    int32_t dSky, dHead; int xs,ys,xh,yh;
    rom_lift(32768,32768, OBJ_SKY,  W,HH, &xs,&ys,&dSky);
    rom_lift(32768,32768, OBJ_HEAD, W,HH, &xh,&yh,&dHead);
    int popped = (dSky != dHead);
    /*   - the spin sends REAL-X extent into the imaginary axis: within one object,
     *     left vs right differ in depth (x·sinθ);                                */
    int32_t dL, dR; int xl,yl,xr,yr;
    rom_lift(20000,32768, OBJ_BODY, W,HH, &xl,&yl,&dL);
    rom_lift(46000,32768, OBJ_BODY, W,HH, &xr,&yr,&dR);
    int spun = (dL != dR);
    /*   - while the vertical (the untouched real axis) adds NO depth: the object
     *     keeps its linear 2D mapping; only the perpendicular axis was added.    */
    int32_t dT, dB; int xt,yt,xb,yb;
    rom_lift(32768,20000, OBJ_BODY, W,HH, &xt,&yt,&dT);
    rom_lift(32768,46000, OBJ_BODY, W,HH, &xb,&yb,&dB);
    int planar_vertical = (dT == dB);

    int moved = popped && ((xs!=xh) || (ys!=yh));  /* objects separated in 3D      */
    int depth_varies = spun && planar_vertical;     /* spun into i, plane preserved */

    return rom2d == 7 && engines_high && anchors && moved && depth_varies;
}
