/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* radial_map.c — radial prime x Fibonacci field with fractal sublattices. See
 * radial_map.h. Golden-angle phyllotaxis packing, holo depth, fixed-point. */
#include "radial_map.h"
#include "holo.h"
#include "megarom.h"
#include "dimensional_ladder.h"

#define GOLDEN_TURN 25028u   /* 0.381966 * 65536 = 360/PHI^2 as a 16-bit turn */

static int32_t fx_sin(uint16_t a){ int32_t x=a; if(x>=32768)x-=65536; x*=2; int32_t ax=x<0?-x:x;
    int64_t p=(int64_t)x*(65536-ax); return (int32_t)((4*p)>>16); }
static int32_t fx_cos(uint16_t a){ return fx_sin((uint16_t)(a+16384)); }
static int isqrt_(int v){ int s=0; while((s+1)*(s+1)<=v) s++; return s; }
static int is_prime_num(int n){ if(n<2)return 0; if(n%2==0)return n==2; for(int i=3;i*(int64_t)i<=n;i+=2) if(n%i==0)return 0; return 1; }

static int g_w, g_h; static uint32_t *g_fb;
static void plot(int x,int y,uint32_t c){ if((unsigned)x<(unsigned)g_w && (unsigned)y<(unsigned)g_h) g_fb[y*g_w+x]=c; }

/* a shaded disc (a packed sphere): radial gradient + holo depth */
static void sphere(int cx,int cy,int rad,uint32_t base,int32_t depth_z){
    if (rad < 1) rad = 1;
    for (int dy=-rad; dy<=rad; dy++) for (int dx=-rad; dx<=rad; dx++){
        int d2=dx*dx+dy*dy; if (d2>rad*rad) continue;
        int shade = 255 - (d2*160)/(rad*rad);            /* centre bright, rim dark */
        int r=(((base>>16)&0xFF)*shade)/255, g=(((base>>8)&0xFF)*shade)/255, b=((base&0xFF)*shade)/255;
        plot(cx+dx, cy+dy, 0xFF000000u | holo_shade((r<<16)|(g<<8)|b, 0, depth_z, 8, 150));
    }
}

/* one golden-angle phyllotaxis lattice; each seed marked by prime/Fibonacci and,
 * for Fibonacci seeds within the sub-depth budget, a nested SUBLATTICE (fractal). */
static void lattice(int cx, int cy, int n, int spacing, int seed_rad, int sub_depth){
    for (int i=1;i<=n;i++){
        uint16_t ang = (uint16_t)((unsigned)i * GOLDEN_TURN);
        int r = (spacing * isqrt_(i)) >> 4;
        int px = cx + (int)(((int64_t)fx_cos(ang) * r) >> 16);
        int py = cy + (int)(((int64_t)fx_sin(ang) * r) >> 16);
        int prime = is_prime_num(i), fib = dl_is_prime_dimension(i);
        uint32_t col; int32_t dz = 30 - r/6; if (dz<-18) dz=-18;
        if (fib && prime)      col = 0xFFFFFF;          /* resonance: prime AND Fibonacci */
        else if (fib)          col = 0xF0C030;          /* Fibonacci — gold */
        else if (prime)        col = 0x40C0F0;          /* prime — cyan     */
        else                   col = 0x50586A;          /* the field        */
        int rad = (fib||prime) ? seed_rad+1 : seed_rad;
        if (fib){                                        /* Fibonacci seed: prominent */
            sphere(px, py, rad+3, 0xF0C030, dz);         /* gold disc  */
            sphere(px, py, rad,   0xFFF0A0, dz);         /* bright core*/
        } else {
            sphere(px, py, rad, col, dz);
        }
        /* fractal sphere packing: nest a smaller golden lattice at each Fibonacci seed */
        if (fib && sub_depth > 0 && i > 1)
            lattice(px, py, 12, spacing/3 + 1, seed_rad>1?seed_rad-1:1, sub_depth-1);
    }
}

void radial_map_render(uint32_t *fb, int w, int h, int zoom_num, int zoom_den, int sub_depth){
    g_fb=fb; g_w=w; g_h=h;
    for (int i=0;i<w*h;i++) fb[i]=0xFF000000u | holo_shade(0x0A0E1A,0,-16,8,96);
    int spacing = (64 * zoom_num) / (zoom_den?zoom_den:1);
    lattice(w/2, h/2, 620, spacing, 3, sub_depth);
}

static void radial_boot(void){ }
void radial_map_register_megarom(void){
    static const megarom_t m = { "Vortex", "radial prime x Fibonacci field — golden-angle packing, fractal sublattices",
                                 MR_KIND_TOOL, radial_boot };
    megarom_register(&m);
}

int radial_map_selfcheck(void){
    enum { W=240, H=240 };
    static uint32_t fb[W*H];
    radial_map_render(fb, W, H, 1, 1, 1);
    /* coherence: filled seeds, distinct gold (Fibonacci) + cyan (prime) marks,
     * radial density (denser at centre than the corners), and variety. */
    int fibmark=0, primemark=0; long filled=0, center=0, corner=0; uint32_t first=fb[0];
    long var=0;
    for (int y=0;y<H;y++) for (int x=0;x<W;x++){
        uint32_t c=fb[y*W+x]&0xFFFFFF; if(c!=first) var++;
        int r=(c>>16)&0xFF, g=(c>>8)&0xFF, b=c&0xFF;
        if (c!=(fb[0]&0xFFFFFF)) filled++;
        if (r>180 && g>140 && b<120) fibmark++;          /* gold-ish */
        if (b>150 && r<120) primemark++;                 /* cyan-ish */
        int dx=x-W/2, dy=y-H/2; int d2=dx*dx+dy*dy;
        if (d2 < (W/6)*(W/6) && c!=(fb[0]&0xFFFFFF)) center++;
        if (d2 > (W/2-8)*(W/2-8) && c!=(fb[0]&0xFFFFFF)) corner++;
    }
    int has_fib   = fibmark   > 8;    /* Fibonacci seeds are sparse by nature (log density) */
    int has_prime = primemark > 40;
    int radial    = center > corner;                     /* denser toward the centre */
    return has_fib && has_prime && radial && filled > (W*H)/12 && var > W;
}
