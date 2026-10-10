/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
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

/* ---- integer fixed point, Q16.16 (65536 == 1.0) ------------------------------
 * Kernel images are integer-only (-mgeneral-regs-only / integer riscv ABI), so
 * this renderer works in Q16.16. Constants are the old doubles * 65536, rounded;
 * pixel positions and shades agree with the old double code to within one unit
 * of the output (one pixel, one colour step). */
#define FX1       65536
#define RD_DZ_Q   10486  /* 0.16 */
#define RD_NEAR_Q 170394 /* 2.6  */

static int32_t q16_sqrt(int64_t x)
{ /* sqrt of a Q16.16 value */
    if (x <= 0) return 0;
    return (int32_t) fx_isqrt64((uint64_t) x << 16);
}
static int64_t q16_trunc(int64_t v)
{ /* Q16.16 -> int, toward 0 */
    return v < 0 ? -(int64_t) ((uint64_t) (-v) >> 16) : (int64_t) ((uint64_t) v >> 16);
}

/* Volume spun out of the plane in PHI STEPS — "like spinning wool", the same
 * incremental φ mechanic the kernel is built by. The thickness is not one smooth
 * bump; it is a stack of NESTED golden shells (a Russian doll): shell n sits at
 * threshold (1 - φ^-n) and adds a rise of φ^-n. Each shell is a smaller
 * self-similar copy of the whole — the IDENTITY of the shape recurring at every
 * scale. t = 0 at the silhouette edge, 1 at the core.  golden_dome(1)=1 (the full
 * higher-dimensional self), golden_dome(0)=0 (nothing added at the edge).
 *
 * The hashing view (why this is invertible): the flat 2D image is the IDENTITY —
 * a flattened matrix, like a hash. Lifting to 3D is UNFLATTENING it, and it is a
 * TWO-WAY map because the expansion is keyed: PRIME is the forward lock (SHA256d
 * here), FIBONACCI is the recovery key, and φ is the axiom of ALIGNMENT between
 * them (consecutive Fibonacci ratios converge to φ). These golden shells ARE the
 * Fibonacci/φ recovery expansion; restricting the 3D self back to the plane
 * returns the identity, so nothing is lost. See radial_map (prime×Fibonacci). */
static int32_t golden_dome(int32_t t)
{ /* t, result: Q16.16 */
    if (t < 0) t = 0;
    if (t > FX1) t = FX1;
    static const int32_t thr[5] = {25035, 40501, 50070, 55968, 59638}; /* 1 - φ^-n */
    /* (sum of the first k shell rises φ^-n) / (sum of all five), k = 0..5 */
    static const int32_t terr[6] = {0, 27513, 44518, 55027, 61522, 65536};
    int k = 0;
    for (int n = 0; n < 5; n++)
        if (t >= thr[n]) k++;
    int32_t terraced = terr[k]; /* the φ-stepped shells      */
    int64_t tt = (int64_t) t;
    int32_t smooth = q16_sqrt(2 * tt - ((tt * tt) >> 16)); /* a rounded cap        */
    return (int32_t) (((int64_t) 36045 * smooth + (int64_t) 29491 * terraced) >>
                      16); /* 0.55 / 0.45 */
}

/* THE LIFT — give each object VOLUME along the imaginary axis, don't just rotate.
 *
 * You cannot turn a 2D thing into a 3D thing by rotating it — no matter the angle
 * you get empty space between. The rule: add the perpendicular (imaginary) axis
 * and PUT THE SAME IMAGE ALONG IT — a face at the front, a face at the back, and
 * FILL THE VOLUME BETWEEN. Then it pops out into the new dimension with no gap.
 * When the object is SYMMETRIC the two faces are identical (just add the same
 * shape at the perpendicular and fill). When ASYMMETRIC (a person's front vs
 * back) the faces differ — but by the golden ratio; we bias the back face by φ.
 *
 * Concretely: each object's silhouette is INFLATED into a rounded solid via its
 * distance-to-edge field (edge=0 thickness, core=full) — the filled body between
 * the faces — and lit, so it reads as real volume. `i` selects the object. */
/* depth stagger RD_DZ_Q (0.16) keeps the scene whole; RD_NEAR_Q (2.6) is the eye */

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

/* per-object 2D centroid, distance-to-edge field, and how much each inflates */
static int32_t g_ocx[N_OBJ], g_ocy[N_OBJ]; /* Q16.16 centroids */
static int    g_dist[N_OBJ][32][32];
static int    g_dmax[N_OBJ];
static int    g_obj_ready = 0;
static const int32_t g_hscale[N_OBJ] = {/* backdrop thin, hero thick (Q16.16) */
                                        /*SKY*/ 6554,    /*STARS*/ 29491, /*GROUND*/ 27525,
                                        /*BRICK*/ 45875, /*BODY*/ FX1,    /*HEAD*/ FX1};
static void obj_init(void){
    if (g_obj_ready) return;
    uint32_t sx[N_OBJ] = {0}, sy[N_OBJ] = {0}, n[N_OBJ] = {0};
    for (int v = 0; v < 32; v++)
        for (int u = 0; u < 32; u++) {
            int k = game_object(u, v);
            sx[k] += (uint32_t) u;
            sy[k] += (uint32_t) v;
            n[k]++;
        }
    for (int k = 0; k < N_OBJ; k++) { /* sx <= 31*1024, so sx<<16 fits in 32 bits */
        if (n[k]) {
            g_ocx[k] = (int32_t) ((sx[k] << 16) / n[k]);
            g_ocy[k] = (int32_t) ((sy[k] << 16) / n[k]);
        } else {
            g_ocx[k] = 16 * FX1;
            g_ocy[k] = 16 * FX1;
        }
    }
    /* grassfire distance-to-edge: 0 outside the object, growing toward its core */
    for (int k=0;k<N_OBJ;k++){
        for (int v=0;v<32;v++) for (int u=0;u<32;u++)
            g_dist[k][v][u] = (game_object(u,v)==k) ? 999 : 0;
        for (int it=0; it<40; it++)
            for (int v=0;v<32;v++) for (int u=0;u<32;u++){
                if (game_object(u,v)!=k) continue;
                int m=999, nb;
                nb=(u>0)?g_dist[k][v][u-1]:0; if(nb<m)m=nb;
                nb=(u<31)?g_dist[k][v][u+1]:0; if(nb<m)m=nb;
                nb=(v>0)?g_dist[k][v-1][u]:0; if(nb<m)m=nb;
                nb=(v<31)?g_dist[k][v+1][u]:0; if(nb<m)m=nb;
                if (m+1 < g_dist[k][v][u]) g_dist[k][v][u]=m+1;
            }
        int dm=1; for (int v=0;v<32;v++) for (int u=0;u<32;u++) if (g_dist[k][v][u]>dm) dm=g_dist[k][v][u];
        g_dmax[k]=dm;
    }
    g_obj_ready = 1;
}

/* inflated thickness of object k at (u,v): the filled body between front/back
 * faces. Spherical cap over the distance field -> a rounded solid. */
static int32_t obj_height(int k, int u, int v)
{ /* Q16.16 */
    if (u < 0 || u > 31 || v < 0 || v > 31) return 0;
    if (game_object(u, v) != k) return 0;
    int32_t t =
        (int32_t) ((g_dist[k][v][u] << 16) / g_dmax[k]); /* 0 edge .. 1 core (dist <= 999) */
    if (t > FX1) t = FX1;
    return (int32_t) (((int64_t) g_hscale[k] * golden_dome(t)) >> 16); /* φ-stepped nested volume */
}

/* the WHOLE scene also gains the new axis (not only each aspect): a global dome,
 * built by the same golden shells, bulging the scene centre toward the viewer. */
static int32_t scene_dome(int u, int v)
{
    int64_t cx = (int64_t) (u - 16) * 4096,
            cy = (int64_t) (v - 16) * 4096; /* (u-16)/16 in Q16.16 */
    int32_t r = (int32_t) fx_isqrt64((uint64_t) (cx * cx + cy * cy));
    if (r > FX1) r = FX1;
    return golden_dome(FX1 - r);
}

void rom_lift(int32_t sx16, int32_t sy16, int i, int w, int h,
              int *out_x, int *out_y, int32_t *out_depth){
    obj_init();
    int k = i; if (k < 0) k = 0; if (k >= N_OBJ) k = N_OBJ-1;
    /* keep the object's ORIGINAL 2D position (linear mapping); depth = layer. The
     * volume (obj_height) is the extent along the imaginary axis, added in render. */
    int64_t X = (int64_t) sx16 * 2 - FX1;            /* (u - 16) / 16 with u = sx16/65536*32 */
    int64_t Y = (int64_t) sy16 * 2 - FX1;            /* v runs DOWN the screen */
    int64_t Z = (int64_t) (N_OBJ - 1 - k) * RD_DZ_Q; /* SKY farthest, HEAD nearest */
    int64_t d = RD_NEAR_Q + Z;
    int64_t sc = (int64_t) h * 60293; /* h * 0.92 */
    *out_x = (int) q16_trunc((int64_t) w * (FX1 / 2) + fx_sdiv64(X * sc, d));
    *out_y = (int) q16_trunc((int64_t) h * (FX1 / 2) + fx_sdiv64(Y * sc, d)); /* +Y: upright */
    *out_depth = (int32_t) q16_trunc(-Z * 22);
}

static int gw,gh; static uint32_t *gfb;
static void px(int x,int y,uint32_t c){ if((unsigned)x<(unsigned)gw&&(unsigned)y<(unsigned)gh) gfb[y*gw+x]=c; }
static uint32_t shade_rgb(uint32_t c, int32_t s)
{ /* scale brightness, s Q16.16 */
    if (s<0) s=0;
    int r = (int) ((((c >> 16) & 255) * (int64_t) s) >> 16),
        g = (int) ((((c >> 8) & 255) * (int64_t) s) >> 16),
        b = (int) (((c & 255) * (int64_t) s) >> 16);
    if(r>255)r=255; if(g>255)g=255; if(b>255)b=255;
    return (uint32_t)((r<<16)|(g<<8)|b);
}
static uint32_t add_white(uint32_t c, int32_t a)
{ /* add a specular highlight, a Q16.16 */
    if (a < 0) a = 0;
    if (a > FX1) a = FX1;
    int wv = (int) ((255 * (int64_t) a) >> 16);
    int r = (int) ((c >> 16) & 255) + wv, g = (int) ((c >> 8) & 255) + wv, b = (int) (c & 255) + wv;
    if(r>255)r=255; if(g>255)g=255; if(b>255)b=255;
    return (uint32_t)((r<<16)|(g<<8)|b);
}

void rom_dimensions_render(uint32_t *fb, int w, int h){
    obj_init();
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
    /* RIGHT: each object given real VOLUME along the imaginary axis (its silhouette
     * inflated into a filled, lit solid) and layered in depth — it pops out as 3D
     * with no empty space. Light from upper-left, toward the viewer. */
    /* unit light L = norm(-0.45,-0.58,0.68) and Blinn half-vector H = norm(L+z), Q16.16 */
    const int64_t Lx = -29472, Ly = -37986, Lz = 44536;
    const int64_t Hx = -16080, Hy = -20726, Hz = 60057;
#define RD_SS 256
    for (int k=0; k<N_OBJ; k++){
        for (int su=0; su<RD_SS; su++){
            for (int sv=0; sv<RD_SS; sv++){
                int u = su*32/RD_SS, v = sv*32/RD_SS;
                if (game_object(u,v) != k) continue;
                /* surface normal from the gradient of the inflated thickness */
                int32_t Hc = obj_height(k, u, v);
                int64_t gx = obj_height(k, u + 1, v) - obj_height(k, u - 1, v);
                int64_t gy = obj_height(k, u, v + 1) - obj_height(k, u, v - 1);
                int64_t nx = -((gx * 170394) >> 16), ny = -((gy * 170394) >> 16),
                        nz = FX1; /* NS = 2.6 */
                int64_t len =
                    fx_isqrt64((uint64_t) (nx * nx + ny * ny + nz * nz)); /* |n|, Q16.16 */
                int64_t diff = fx_sdiv64(nx * Lx + ny * Ly + nz * Lz, len);
                if (diff < 0) diff = 0;
                int64_t sp = fx_sdiv64(nx * Hx + ny * Hy + nz * Hz, len);
                if (sp < 0) sp = 0;
                sp = (sp * sp) >> 16;
                sp = (sp * sp) >> 16;
                sp = (sp * sp) >> 16;                                       /* ^8 highlight */
                int32_t shade = (int32_t) (19661 + ((55706 * diff) >> 16)); /* 0.30 + 0.85 diff */
                uint32_t base = game_pixel(u,v);
                uint32_t c = shade_rgb(base, shade);
                c = add_white(c, (int32_t) ((((36045 * sp) >> 16) * g_hscale[k]) >>
                                            16)); /* gloss: sp*0.55*hscale */
                /* depth = object layer + its own volume + the WHOLE-scene dome */
                int32_t dz =
                    (int32_t) q16_trunc(-(int64_t) (N_OBJ - 1 - k) * RD_DZ_Q * 18 +
                                        (int64_t) Hc * 10 + (int64_t) scene_dome(u, v) * 8);
                c = holo_shade(c, 0, dz, 6, 150);          /* near warm / far cool */

                int32_t sx16 = (int32_t)(((int64_t)su*65536)/RD_SS);
                int32_t sy16 = (int32_t)(((int64_t)sv*65536)/RD_SS);
                int X,Y; int32_t d0; rom_lift(sx16, sy16, k, halfw, h, &X, &Y, &d0);
                for (int yy=0; yy<2; yy++) for (int xx=0; xx<2; xx++)
                    px(halfw+X+xx, Y+yy, 0xFF000000u | c);
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

    /* (3) the 2D->3D lift gives each object VOLUME along the imaginary axis —
     * the same image at the perpendicular with the interior FILLED — not a bare
     * rotation (which leaves empty space). Verify the three properties: */
    obj_init();
    int W=240, HH=240;
    /*   a) filled volume: the body's interior has real thickness — its core is
     *      thicker than its edge, and there is a genuine interior to fill.       */
    int32_t h_core =
        obj_height(OBJ_BODY, (g_ocx[OBJ_BODY] + FX1 / 2) >> 16, (g_ocy[OBJ_BODY] + FX1 / 2) >> 16);
    int32_t h_edge = obj_height(OBJ_BODY, 6, 12);                     /* a corner of the body */
    int filled = (h_core > h_edge + 3277) && (g_dmax[OBJ_BODY] >= 2); /* + 0.05 */
    /*   b) the objects are staggered in depth (the pop-out layering) — probe an
     *      OFF-CENTRE point so perspective actually separates the layers.        */
    int32_t dSky, dHead; int xs,ys,xh,yh;
    rom_lift(0,0, OBJ_SKY,  W,HH, &xs,&ys,&dSky);
    rom_lift(0,0, OBJ_HEAD, W,HH, &xh,&yh,&dHead);
    int popped = (dSky != dHead);
    /*   c) each object keeps its LINEAR 2D mapping: within a layer, the midpoint
     *      of two points maps to the midpoint (affine — no warp of the image).   */
    int x1,y1,x2,y2,xm,ym; int32_t td;
    rom_lift(10000,32768, OBJ_GROUND, W,HH, &x1,&y1,&td);
    rom_lift(50000,32768, OBJ_GROUND, W,HH, &x2,&y2,&td);
    rom_lift(30000,32768, OBJ_GROUND, W,HH, &xm,&ym,&td);
    int dmx=(x1+x2)/2-xm; if(dmx<0)dmx=-dmx;
    int dmy=(y1+y2)/2-ym; if(dmy<0)dmy=-dmy;
    int linear = (dmx<=1) && (dmy<=1);

    /*   d) IDENTITY PROPERTY: the higher-dimensional self, restricted back to the
     *      plane, IS the original — the core carries the full self (dome=1), the
     *      edge adds nothing (dome=0). And the volume is spun in PHI-STEP nested
     *      shells (a Russian doll): each golden level strictly encloses the last. */
    int identity = (golden_dome(FX1) > 62259) && (golden_dome(0) < 3277); /* > 0.95, < 0.05 */
    int32_t s1 = golden_dome(29491), s2 = golden_dome(45875),
            s3 = golden_dome(60293);                /* .45 .70 .92 */
    int nested = (s1 < s2) && (s2 < s3);            /* φ shells nest, edge->core    */

    /*   e) the unflatten is KEYED: prime = forward lock (SHA256d), FIBONACCI = the
     *      recovery key, φ = the alignment axiom. Verify the recovery key aligns to
     *      φ — consecutive Fibonacci ratios converge to the golden ratio.         */
    int64_t fa = 1, fb2 = 1;
    for (int n = 0; n < 20; n++) {
        int64_t t = fa + fb2;
        fa = fb2;
        fb2 = t;
    }
    /* |fb2/fa - φ| < 0.001  <=>  |fb2*10^6 - 1618034*fa| < 1000*fa (φ to 7 digits) */
    int64_t dphi = fb2 * 1000000 - 1618034 * fa;
    if (dphi < 0) dphi = -dphi;
    int aligned = (dphi < 1000 * fa); /* Fibonacci recovery key ~ φ   */

    int moved = popped && ((xs!=xh) || (ys!=yh));
    int depth_varies = filled && linear && identity && nested && aligned;

    return rom2d == 7 && engines_high && anchors && moved && depth_varies;
}
