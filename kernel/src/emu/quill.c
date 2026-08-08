/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* quill.c — Quill: an id Tech 2-class polygonal renderer. See quill.h.
 * Fixed-point 16.16, z-buffer, perspective-correct texturing, lightmaps, holo. */
#include "quill.h"
#include "holo.h"
#include "megarom.h"

#define QW 320
#define QH 200

static int32_t fx_sin(uint16_t a){
    int32_t x = a; if (x >= 32768) x -= 65536; x *= 2;
    int32_t ax = x < 0 ? -x : x;
    int64_t p = (int64_t)x * (65536 - ax);
    return (int32_t)((4 * p) >> 16);
}
static int32_t fx_cos(uint16_t a){ return fx_sin((uint16_t)(a + 16384)); }

/* procedural textures (shared idea with Cinder) */
static uint32_t tex_sample(int id, int u, int v){
    u &= 63; v &= 63;
    switch (id){
        case 1: { int row=v/16, off=(row&1)?32:0; int b=((u+off)%32<2)||(v%16<2); return b?0x2A1A12:0xA05038; }
        case 2: return (((u>>3)^(v>>3))&1)?0x384048:0x98A0A8;
        default:{ int n=((u*37+v*101+u*v)&0x1F); int g=0x58+n; return (g<<16)|(g<<8)|g; }
    }
}

/* ---- world: a room + a central pillar, as textured lit quads --------------- */
typedef struct { int32_t x,y,z, u,v, l; } vtx_t;   /* pos 16.16, uv 16.16(0..1), light 0..255 */
typedef struct { vtx_t v[3]; int tex; } tri_t;

#define MAXTRI 64
static tri_t   g_tri[MAXTRI];
static int     g_ntri = 0;
static int     g_built = 0;

static int32_t C(int cells){ return cells << 16; }

/* baked lighting: brighter high (near a ceiling light), dimmer low/far corners */
static int32_t bake(int32_t x, int32_t y, int32_t z){
    int32_t yc = y >> 16;           /* height in cells 0..6 */
    int lum = 90 + yc * 22;         /* rises with height    */
    int32_t dx = (x >> 16) - 4, dz = (z >> 16) - 4;
    int d2 = (int)(dx*dx + dz*dz);
    lum -= d2 * 3;                   /* falloff from room centre */
    if (lum < 40) lum = 40; if (lum > 255) lum = 255;
    return lum;
}

static void add_quad(int32_t x0,int32_t y0,int32_t z0, int32_t x1,int32_t y1,int32_t z1,
                     int32_t x2,int32_t y2,int32_t z2, int32_t x3,int32_t y3,int32_t z3, int tex){
    if (g_ntri + 2 > MAXTRI) return;
    vtx_t a={x0,y0,z0,0,0,bake(x0,y0,z0)}, b={x1,y1,z1,C(1),0,bake(x1,y1,z1)},
          c={x2,y2,z2,C(1),C(1),bake(x2,y2,z2)}, d={x3,y3,z3,0,C(1),bake(x3,y3,z3)};
    g_tri[g_ntri].v[0]=a; g_tri[g_ntri].v[1]=b; g_tri[g_ntri].v[2]=c; g_tri[g_ntri].tex=tex; g_ntri++;
    g_tri[g_ntri].v[0]=a; g_tri[g_ntri].v[1]=c; g_tri[g_ntri].v[2]=d; g_tri[g_ntri].tex=tex; g_ntri++;
}

static void build_world(void){
    if (g_built) return; g_built = 1; g_ntri = 0;
    /* room 8 wide (x), 6 high (y), 8 deep (z) */
    add_quad(C(0),C(0),C(8), C(8),C(0),C(8), C(8),C(6),C(8), C(0),C(6),C(8), 1); /* far wall  */
    add_quad(C(0),C(0),C(0), C(0),C(0),C(8), C(0),C(6),C(8), C(0),C(6),C(0), 3); /* left wall */
    add_quad(C(8),C(0),C(8), C(8),C(0),C(0), C(8),C(6),C(0), C(8),C(6),C(8), 3); /* right wall*/
    add_quad(C(0),C(0),C(0), C(8),C(0),C(0), C(8),C(0),C(8), C(0),C(0),C(8), 2); /* floor     */
    add_quad(C(0),C(6),C(8), C(8),C(6),C(8), C(8),C(6),C(0), C(0),C(6),C(0), 2); /* ceiling   */
    /* central pillar (front + two sides face the camera) */
    add_quad(C(3),C(0),C(4), C(5),C(0),C(4), C(5),C(6),C(4), C(3),C(6),C(4), 1); /* pillar front */
    add_quad(C(3),C(0),C(6), C(3),C(0),C(4), C(3),C(6),C(4), C(3),C(6),C(6), 3); /* pillar left  */
    add_quad(C(5),C(0),C(4), C(5),C(0),C(6), C(5),C(6),C(6), C(5),C(6),C(4), 3); /* pillar right */
}

/* projected vertex */
typedef struct { int sx, sy; int32_t iz; int32_t s, t; int l; int ok; } pv_t;

static int32_t g_zbuf[QW*QH];

static void project(pv_t *o, vtx_t *v, int32_t cx,int32_t cy,int32_t cz,
                    int32_t sy_, int32_t cy_, int32_t sp, int32_t cp, int w, int h){
    int32_t tx=v->x-cx, ty=v->y-cy, tz=v->z-cz;      /* 16.16 */
    /* yaw around Y */
    int32_t rx = (int32_t)(((int64_t)tx*cy_ - (int64_t)tz*sy_) >> 16);
    int32_t rz = (int32_t)(((int64_t)tx*sy_ + (int64_t)tz*cy_) >> 16);
    /* pitch around X */
    int32_t ry = (int32_t)(((int64_t)ty*cp - (int64_t)rz*sp) >> 16);
    int32_t rz2= (int32_t)(((int64_t)ty*sp + (int64_t)rz*cp) >> 16);
    if (rz2 < (6553)){ o->ok = 0; return; }           /* near-plane clip */
    int focal = h;
    o->sx = w/2 + (int)(((int64_t)rx * focal) / rz2);
    o->sy = h/2 - (int)(((int64_t)ry * focal) / rz2);
    o->iz = (int32_t)(((int64_t)1 << 30) / rz2);       /* ~ 1/w, scaled */
    o->s  = (int32_t)(((int64_t)v->u * o->iz) >> 16);  /* u/w */
    o->t  = (int32_t)(((int64_t)v->v * o->iz) >> 16);  /* v/w */
    o->l  = v->l;
    o->ok = 1;
}

static int64_t edge(pv_t *a, pv_t *b, int px, int py){
    return (int64_t)(b->sx - a->sx) * (py - a->sy) - (int64_t)(b->sy - a->sy) * (px - a->sx);
}

void quill_render(uint32_t *fb, int w, int h, int32_t cx,int32_t cy,int32_t cz,
                  uint16_t yaw, uint16_t pitch){
    build_world();
    int32_t sy_=fx_sin(yaw), cy_=fx_cos(yaw), sp=fx_sin(pitch), cp=fx_cos(pitch);
    /* clear to cool background + reset z-buffer */
    for (int i=0;i<w*h;i++){ fb[i]=0xFF000000u | holo_shade(0x0C1424,0,-16,8,96); g_zbuf[i]=0; }

    for (int t=0;t<g_ntri;t++){
        pv_t p[3]; int good=1;
        for (int i=0;i<3;i++){ project(&p[i], &g_tri[t].v[i], cx,cy,cz, sy_,cy_,sp,cp, w,h); if(!p[i].ok) good=0; }
        if (!good) continue;
        int64_t area = edge(&p[0],&p[1],p[2].sx,p[2].sy);
        if (area == 0) continue;                        /* degenerate */
        int neg = area < 0; if (neg) area = -area;      /* render both windings (interior scene) */
        int minx=p[0].sx, maxx=p[0].sx, miny=p[0].sy, maxy=p[0].sy;
        for (int i=1;i<3;i++){ if(p[i].sx<minx)minx=p[i].sx; if(p[i].sx>maxx)maxx=p[i].sx;
                               if(p[i].sy<miny)miny=p[i].sy; if(p[i].sy>maxy)maxy=p[i].sy; }
        if (minx<0)minx=0; if (miny<0)miny=0; if (maxx>=w)maxx=w-1; if (maxy>=h)maxy=h-1;
        int tex = g_tri[t].tex;
        for (int y=miny;y<=maxy;y++){
            for (int x=minx;x<=maxx;x++){
                int64_t w0=edge(&p[1],&p[2],x,y), w1=edge(&p[2],&p[0],x,y), w2=edge(&p[0],&p[1],x,y);
                if (neg){ w0=-w0; w1=-w1; w2=-w2; }     /* match the winding */
                if ((w0|w1|w2) < 0) continue;           /* outside (all must be >=0) */
                int32_t iz = (int32_t)((w0*p[0].iz + w1*p[1].iz + w2*p[2].iz) / area);
                int idx = y*w + x;
                if (iz <= g_zbuf[idx]) continue;        /* z-test: larger iz = nearer */
                g_zbuf[idx] = iz;
                int32_t s = (int32_t)((w0*p[0].s + w1*p[1].s + w2*p[2].s) / area);
                int32_t tt= (int32_t)((w0*p[0].t + w1*p[1].t + w2*p[2].t) / area);
                int lum   = (int)((w0*p[0].l + w1*p[1].l + w2*p[2].l) / area);
                int u = iz ? (int)(((int64_t)s << 16) / iz) >> 10 : 0;   /* u/w /w -> texel (0..) */
                int v = iz ? (int)(((int64_t)tt << 16) / iz) >> 10 : 0;
                uint32_t base = tex_sample(tex, u, v);
                /* lightmap: multiply texture by baked light */
                int r=((base>>16)&0xFF)*lum/255, g=((base>>8)&0xFF)*lum/255, b=(base&0xFF)*lum/255;
                uint32_t lit=(r<<16)|(g<<8)|b;
                /* holo depth cue from 1/w (near = big iz = warm) */
                int32_t depth_z = (iz >> 9) - 8; if (depth_z>44) depth_z=44; if (depth_z<-20) depth_z=-20;
                fb[idx] = 0xFF000000u | holo_shade(lit, 0, depth_z, 8, 128);
            }
        }
    }
}

static void quill_boot(void){ }

void quill_register_megarom(void){
    static const megarom_t m = {
        "Quill", "id Tech 2-class polygonal renderer — z-buffer, lightmaps, perspective 3D",
        MR_KIND_GAME, quill_boot
    };
    megarom_register(&m);
}

int quill_selfcheck(void){
    static uint32_t fb[QW*QH];
    /* camera just outside the open front, looking +z down the room at the pillar
     * and far wall (whole room in front of the near plane) */
    quill_render(fb, QW, QH, C(4), C(3), -(2<<16), 0, 0);

    /* coherence: many distinct depths (real 3D + occlusion), textured variety,
     * a lightmap brightness gradient, and a holo depth (red) range. */
    int filled=0, depths=0; int32_t lastz=-1;
    long var=0; uint32_t first=fb[0];
    int minl=255,maxl=0, minr=255,maxr=0;
    for (int i=0;i<QW*QH;i++){
        if (g_zbuf[i] != 0){ filled++; if (g_zbuf[i]!=lastz){ depths++; lastz=g_zbuf[i]; } }
        uint32_t c=fb[i]; if (c!=first) var++;
        int r=(c>>16)&0xFF, g=(c>>8)&0xFF, b=c&0xFF; int lum=(r+g+b)/3;
        if (g_zbuf[i]!=0){ if(lum<minl)minl=lum; if(lum>maxl)maxl=lum; if(r<minr)minr=r; if(r>maxr)maxr=r; }
    }
    int has_geometry = filled > (QW*QH)/8;      /* geometry covers a good area   */
    int has_depth    = depths > 50;             /* many distinct z values (3D)   */
    int has_lightmap = (maxl - minl) >= 30;     /* baked-light gradient present  */
    int has_holo     = (maxr - minr) >= 15;     /* near-warm / far-cool gradient */
    return has_geometry && has_depth && has_lightmap && has_holo && var > QW;
}
