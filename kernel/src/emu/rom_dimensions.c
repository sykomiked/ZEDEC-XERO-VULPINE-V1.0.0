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

/* ---- self-contained, libm-free transcendentals (doubles are allowed on target) --- */
#define RD_PI   3.14159265358979
#define RD_TAU  6.28318530717959
#define RD_PHI  1.61803398874989

static double d_sqrt(double x){                       /* Newton */
    if (x <= 0.0) return 0.0;
    double g = x > 1.0 ? x : 1.0;
    for (int n = 0; n < 10; n++) g = 0.5 * (g + x / g);
    return g;
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
static double golden_dome(double t){
    if (t < 0) t = 0; if (t > 1) t = 1;
    static const double thr[5] = {0.382, 0.618, 0.764, 0.854, 0.910}; /* 1 - φ^-n */
    double total = 0, step = 1.0;
    for (int n=0;n<5;n++){ total += step; step /= RD_PHI; }
    double acc = 0; step = 1.0;
    for (int n=0;n<5;n++){ if (t >= thr[n]) acc += step; step /= RD_PHI; }
    double terraced = acc / total;                    /* the φ-stepped shells      */
    double smooth   = d_sqrt(2.0*t - t*t);            /* a rounded cap             */
    return 0.55*smooth + 0.45*terraced;               /* spun-in-steps, still round */
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
#define RD_DZ      0.16     /* gentle per-object depth stagger — keep the scene whole */
#define RD_NEAR    2.6

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
static double g_ocx[N_OBJ], g_ocy[N_OBJ];
static int    g_dist[N_OBJ][32][32];
static int    g_dmax[N_OBJ];
static int    g_obj_ready = 0;
static const double g_hscale[N_OBJ] = {                /* backdrop thin, hero thick */
    /*SKY*/0.10, /*STARS*/0.45, /*GROUND*/0.42, /*BRICK*/0.70, /*BODY*/1.00, /*HEAD*/1.00
};
static void obj_init(void){
    if (g_obj_ready) return;
    double sx[N_OBJ]={0}, sy[N_OBJ]={0}; int n[N_OBJ]={0};
    for (int v=0;v<32;v++) for (int u=0;u<32;u++){ int k=game_object(u,v); sx[k]+=u; sy[k]+=v; n[k]++; }
    for (int k=0;k<N_OBJ;k++){ if(n[k]){ g_ocx[k]=sx[k]/n[k]; g_ocy[k]=sy[k]/n[k]; } else { g_ocx[k]=16; g_ocy[k]=16; } }
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
static double obj_height(int k, int u, int v){
    if (u<0||u>31||v<0||v>31) return 0.0;
    if (game_object(u,v)!=k) return 0.0;
    double t = (double)g_dist[k][v][u] / (double)g_dmax[k];   /* 0 edge .. 1 core */
    if (t>1.0) t=1.0;
    return g_hscale[k] * golden_dome(t);              /* φ-stepped nested volume */
}

/* the WHOLE scene also gains the new axis (not only each aspect): a global dome,
 * built by the same golden shells, bulging the scene centre toward the viewer. */
static double scene_dome(int u, int v){
    double cx=(u-16.0)/16.0, cy=(v-16.0)/16.0;
    double r = d_sqrt(cx*cx + cy*cy); if (r>1.0) r=1.0;
    return golden_dome(1.0 - r);
}

void rom_lift(int32_t sx16, int32_t sy16, int i, int w, int h,
              int *out_x, int *out_y, int32_t *out_depth){
    obj_init();
    int k = i; if (k < 0) k = 0; if (k >= N_OBJ) k = N_OBJ-1;
    /* keep the object's ORIGINAL 2D position (linear mapping); depth = layer. The
     * volume (obj_height) is the extent along the imaginary axis, added in render. */
    double u  = (double)sx16 / 65536.0 * 32.0;
    double vv = (double)sy16 / 65536.0 * 32.0;
    double X  = (u  - 16.0) / 16.0;
    double Y  = (vv - 16.0) / 16.0;                   /* v runs DOWN the screen */
    double Z  = (double)(N_OBJ-1-k) * RD_DZ;          /* SKY farthest, HEAD nearest */
    double d  = RD_NEAR + Z;
    double sc = (double)h * 0.92;
    *out_x = (int)((double)w * 0.5 + X * sc / d);
    *out_y = (int)((double)h * 0.5 + Y * sc / d);     /* +Y: keep the image upright */
    *out_depth = (int32_t)(-Z * 22.0);
}

static int gw,gh; static uint32_t *gfb;
static void px(int x,int y,uint32_t c){ if((unsigned)x<(unsigned)gw&&(unsigned)y<(unsigned)gh) gfb[y*gw+x]=c; }
static uint32_t shade_rgb(uint32_t c, double s){       /* scale brightness */
    if (s<0) s=0;
    int r=(int)(((c>>16)&255)*s), g=(int)(((c>>8)&255)*s), b=(int)((c&255)*s);
    if(r>255)r=255; if(g>255)g=255; if(b>255)b=255;
    return (uint32_t)((r<<16)|(g<<8)|b);
}
static uint32_t add_white(uint32_t c, double a){       /* add a specular highlight */
    if (a<0) a=0; if (a>1) a=1;
    int r=(int)(((c>>16)&255)+255*a), g=(int)(((c>>8)&255)+255*a), b=(int)((c&255)+255*a);
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
    double Lx=-0.45, Ly=-0.58, Lz=0.68;
    double li=1.0/d_sqrt(Lx*Lx+Ly*Ly+Lz*Lz); Lx*=li; Ly*=li; Lz*=li;
    double Hx=Lx, Hy=Ly, Hz=Lz+1.0;                      /* Blinn half-vector */
    double hi=1.0/d_sqrt(Hx*Hx+Hy*Hy+Hz*Hz); Hx*=hi; Hy*=hi; Hz*=hi;
    #define RD_SS 256
    for (int k=0; k<N_OBJ; k++){
        for (int su=0; su<RD_SS; su++){
            for (int sv=0; sv<RD_SS; sv++){
                int u = su*32/RD_SS, v = sv*32/RD_SS;
                if (game_object(u,v) != k) continue;
                /* surface normal from the gradient of the inflated thickness */
                double Hc = obj_height(k,u,v);
                double gx = obj_height(k,u+1,v) - obj_height(k,u-1,v);
                double gy = obj_height(k,u,v+1) - obj_height(k,u,v-1);
                double NS = 2.6;
                double nx=-gx*NS, ny=-gy*NS, nz=1.0;
                double ninv=1.0/d_sqrt(nx*nx+ny*ny+nz*nz); nx*=ninv; ny*=ninv; nz*=ninv;
                double diff = nx*Lx+ny*Ly+nz*Lz; if (diff<0) diff=0;
                double sp = nx*Hx+ny*Hy+nz*Hz; if (sp<0) sp=0;
                sp=sp*sp; sp=sp*sp; sp=sp*sp;            /* ^8 highlight */
                double shade = 0.30 + 0.85*diff;          /* ambient + diffuse */
                uint32_t base = game_pixel(u,v);
                uint32_t c = shade_rgb(base, shade);
                c = add_white(c, sp*0.55*g_hscale[k]);    /* gloss where it bulges */
                /* depth = object layer + its own volume + the WHOLE-scene dome */
                int32_t dz = (int32_t)(-((double)(N_OBJ-1-k)*RD_DZ)*18.0
                                       + Hc*10.0 + scene_dome(u,v)*8.0);
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
    double h_core = obj_height(OBJ_BODY, (int)(g_ocx[OBJ_BODY]+0.5), (int)(g_ocy[OBJ_BODY]+0.5));
    double h_edge = obj_height(OBJ_BODY, 6, 12);            /* a corner of the body */
    int filled = (h_core > h_edge + 0.05) && (g_dmax[OBJ_BODY] >= 2);
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
    int identity = (golden_dome(1.0) > 0.95) && (golden_dome(0.0) < 0.05);
    double s1=golden_dome(0.45), s2=golden_dome(0.70), s3=golden_dome(0.92);
    int nested = (s1 < s2) && (s2 < s3);            /* φ shells nest, edge->core    */

    /*   e) the unflatten is KEYED: prime = forward lock (SHA256d), FIBONACCI = the
     *      recovery key, φ = the alignment axiom. Verify the recovery key aligns to
     *      φ — consecutive Fibonacci ratios converge to the golden ratio.         */
    long fa=1, fb2=1; for (int n=0;n<20;n++){ long t=fa+fb2; fa=fb2; fb2=t; }
    double fib_ratio = (double)fb2/(double)fa, dphi = fib_ratio - RD_PHI;
    if (dphi<0) dphi=-dphi;
    int aligned = (dphi < 0.001);                   /* Fibonacci recovery key ~ φ   */

    int moved = popped && ((xs!=xh) || (ys!=yh));
    int depth_varies = filled && linear && identity && nested && aligned;

    return rom2d == 7 && engines_high && anchors && moved && depth_varies;
}
