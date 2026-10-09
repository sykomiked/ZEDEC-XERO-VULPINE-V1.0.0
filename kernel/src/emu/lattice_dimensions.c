/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* lattice_dimensions.c — the 13-space desktop driven by the dimensional ladder.
 * See lattice_dimensions.h. The desktop (13d) is the MegaROM container. */
#include "lattice_dimensions.h"
#include "holo.h"
#include "megarom.h"
#include "dimensional_ladder.h"

#define GOLDEN_TURN 25028u   /* 360/PHI^2 as a 16-bit turn */

static int32_t fx_sin(uint16_t a){ int32_t x=a; if(x>=32768)x-=65536; x*=2; int32_t ax=x<0?-x:x;
    int64_t p=(int64_t)x*(65536-ax); return (int32_t)((4*p)>>16); }
static int32_t fx_cos(uint16_t a){ return fx_sin((uint16_t)(a+16384)); }
static int isqrt_(int v){ int s=0; while((s+1)*(s+1)<=v) s++; return s; }

/* The 13 lattice spaces bound to 0d..12d — the desktop itself is 13d, the
 * universal container / MegaROM. The domain<->essence correspondences: */
static const space_dim_t g_bind[LD_SPACES] = {
 { 0, "BASE",       "home plane <-> presence-at-zero (0d)",           0, 1 },
 { 1, "VINO",       "economy <-> line (1d)",                          1, 1 },
 { 2, "FLEET",      "work <-> plane (2d)",                            2, 1 },
 { 3, "STUDIO",     "art <-> space (3d)",                             3, 1 },
 { 4, "CHIGLET",    "AI <-> event space (4d)",                        4, 0 },
 { 5, "REFINERY",   "magitech <-> photonic reinforcement (5d)",       5, 1 },
 { 6, "CONCORD",    "social <-> timeline (6d)",                       6, 0 },
 { 7, "CROWN",      "governance <-> continuum of continuity (7d)",    7, 0 },
 { 8, "WYRMGATE",   "judgment <-> gravitation (8d, field 2)",         8, 1 },
 { 9, "RAM",        "markets <-> continuity of presence (9d)",        9, 0 },
 {10, "INTERSPACE", "jurisdiction <-> galactic/electron (10d)",      10, 0 },
 {11, "BADGE",      "reputation <-> 2nd-quant/supercluster (11d)",   11, 0 },
 {12, "LOGISTICS",  "supply <-> cosmos/string (12d)",                12, 0 },
};

/* colour per space domain (base tint before holo depth) */
static const uint32_t g_col[LD_SPACES] = {
 0x8090A8, 0x60A060, 0x5078B0, 0xB060A0, 0x40C0C0, 0xC0A040, 0xC07050,
 0xA0A060, 0xB04040, 0x50A0B0, 0x7060C0, 0xC0C080, 0x808090
};

const space_dim_t *lattice_dimensions(int *count_out){
    if (count_out) *count_out = LD_SPACES;
    return g_bind;
}

static int gw, gh; static uint32_t *gfb;
static void plot(int x,int y,uint32_t c){ if((unsigned)x<(unsigned)gw && (unsigned)y<(unsigned)gh) gfb[y*gw+x]=c; }
static void ring(int cx,int cy,int rad,uint32_t c){
    for (int a=0;a<65536;a+=256){ uint16_t ua=(uint16_t)a;
        int x=cx+(int)(((int64_t)fx_cos(ua)*rad)>>16), y=cy+(int)(((int64_t)fx_sin(ua)*rad)>>16);
        plot(x,y,c); plot(x+1,y,c); }
}
static void sphere(int cx,int cy,int rad,uint32_t base,int32_t dz){
    if(rad<1)rad=1;
    for(int dy=-rad;dy<=rad;dy++)for(int dx=-rad;dx<=rad;dx++){ int d2=dx*dx+dy*dy; if(d2>rad*rad)continue;
        int sh=255-(d2*150)/(rad*rad);
        int r=(((base>>16)&0xFF)*sh)/255,g=(((base>>8)&0xFF)*sh)/255,b=((base&0xFF)*sh)/255;
        plot(cx+dx,cy+dy,0xFF000000u|holo_shade((r<<16)|(g<<8)|b,0,dz,8,150)); }
}

void lattice_dim_render(uint32_t *fb, int w, int h){
    gfb=fb; gw=w; gh=h;
    for(int i=0;i<w*h;i++) fb[i]=0xFF000000u|holo_shade(0x080C16,0,-16,8,96);
    int cx=w/2, cy=h/2;
    /* the 13d container: the field that holds the 13 spaces (the MegaROM) */
    ring(cx,cy,(w<h?w:h)/2 - 8, 0xFF283040);
    ring(cx,cy,(w<h?w:h)/2 - 10, 0xFF202838);
    /* the 13 spaces at the golden angle, radius growing by dimension (phyllotaxis) */
    for(int i=0;i<LD_SPACES;i++){
        uint16_t ang=(uint16_t)((unsigned)(i+1)*GOLDEN_TURN);
        int rr=(((w<h?w:h)/2 - 26) * isqrt_(i+1)) / isqrt_(LD_SPACES);
        int px=cx+(int)(((int64_t)fx_cos(ang)*rr)>>16);
        int py=cy+(int)(((int64_t)fx_sin(ang)*rr)>>16);
        int32_t dz = 34 - i*3; if(dz<-16)dz=-16;
        int rad = g_bind[i].primary ? 11 : 8;
        if (g_bind[i].primary){                       /* primary (Fibonacci) space: gold ring */
            sphere(px,py,rad+3,0xF0C030,dz);
            sphere(px,py,rad,g_col[i],dz);
        } else {
            sphere(px,py,rad,g_col[i],dz);
        }
        /* a spoke back to the container centre (the space belongs to 13d) */
        for(int t=8;t<rr-rad;t+=6){ int lx=cx+(int)(((int64_t)fx_cos(ang)*t)>>16), ly=cy+(int)(((int64_t)fx_sin(ang)*t)>>16); plot(lx,ly,0xFF303848); }
    }
    /* the container core (13d / the MegaROM seed) */
    sphere(cx,cy,7,0xFFFFFF,40);
}

static void ldesk_boot(void){ }
void lattice_dim_register_megarom(void){
    static const megarom_t m = {
        "Dimensional Desktop", "the 13 lattice spaces as 0d-12d; the desktop is 13d — the universal container (MegaROM)",
        MR_KIND_DESKTOP, ldesk_boot
    };
    megarom_register(&m);
}

int lattice_dim_selfcheck(uint32_t *primaries_out){
    /* (1) 13 spaces bound to 13 DISTINCT dimensions 0..12 */
    int seen[LD_SPACES] = {0}; int distinct = 1, primaries = 0, flags_ok = 1;
    for (int i=0;i<LD_SPACES;i++){
        int d=g_bind[i].dim; if(d<0||d>=LD_SPACES||seen[d]) distinct=0; else seen[d]=1;
        if (g_bind[i].primary) primaries++;
        /* (2) primary flags are EXACTLY the Fibonacci dimensions */
        if ((int)g_bind[i].primary != dl_is_prime_dimension(g_bind[i].dim)) flags_ok=0;
    }
    if (primaries_out) *primaries_out = (uint32_t)primaries;
    /* (3) the container is 13d, and 13 is Fibonacci (the universal container is a
     * primary dimension) */
    int container_ok = dl_is_prime_dimension(13);

    /* (4) the desktop renders coherently */
    enum { W=200, H=200 }; static uint32_t fb[W*H];
    lattice_dim_render(fb, W, H);
    long var=0; int center=0; uint32_t first=fb[0];
    for (int i=0;i<W*H;i++) if((fb[i]&0xFFFFFF)!=(first&0xFFFFFF)) var++;
    int cx=W/2,cy=H/2; for(int dy=-6;dy<=6;dy++)for(int dx=-6;dx<=6;dx++) if((fb[(cy+dy)*W+cx+dx]&0xFFFFFF)>0x808080) center++;
    int renders = var > W && center > 20;   /* has structure + a bright container core */

    return distinct && flags_ok && container_ok && renders && primaries == 6;
}
