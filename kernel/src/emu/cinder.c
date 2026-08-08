/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* cinder.c — Cinder: an id Tech 1-class column raycaster with holo depth shading.
 * See cinder.h. Fixed-point 16.16, parabolic sine, procedural textures. */
#include "cinder.h"
#include "holo.h"
#include "megarom.h"

/* ---- fixed-point trig (16.16); angle is a 16-bit turn ---------------------- */
static int32_t fx_sin(uint16_t angle){
    int32_t a = angle;                 /* 0..65535 */
    if (a >= 32768) a -= 65536;        /* -32768..32767 = turn in [-0.5,0.5)     */
    int32_t x = a * 2;                 /* 16.16 in [-65536,65534] ~ [-1,1]       */
    int32_t ax = x < 0 ? -x : x;
    int64_t prod = (int64_t)x * (65536 - ax);   /* 32.32 */
    return (int32_t)((4 * prod) >> 16);          /* ~ sin, 16.16 in [-65536,65536] */
}
static int32_t fx_cos(uint16_t angle){ return fx_sin((uint16_t)(angle + 16384)); }

/* ---- the test world: a 12x12 grid, 0 = open, >0 = wall texture id ---------- */
#define MW 12
#define MH 12
static const uint8_t g_map[MH][MW] = {
    {1,1,1,1,1,1,1,1,1,1,1,1},
    {1,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,2,2,0,0,0,0,3,3,0,1},
    {1,0,2,0,0,0,0,0,0,3,0,1},
    {1,0,0,0,0,1,1,0,0,0,0,1},
    {1,0,0,0,0,1,1,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,3,0,0,0,0,0,0,2,0,1},
    {1,0,3,3,0,0,0,0,2,2,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,1},
    {1,1,1,1,1,1,1,1,1,1,1,1},
};

/* procedural 64x64 textures per id — brick / checker / stone */
static uint32_t tex_sample(int id, int u, int v){
    u &= 63; v &= 63;
    switch (id){
        case 1: { /* brick: offset rows of bricks */
            int row = v / 16; int off = (row & 1) ? 32 : 0;
            int brick = ((u + off) % 32 < 2) || (v % 16 < 2);
            return brick ? 0x2A1A12 : 0xA05038;
        }
        case 2: { /* checker */
            return (((u >> 3) ^ (v >> 3)) & 1) ? 0x303840 : 0x9098A0;
        }
        default: { /* stone: noisy grey */
            int n = ((u * 37 + v * 101 + u * v) & 0x1F);
            int g = 0x60 + n; return (g << 16) | (g << 8) | g;
        }
    }
}

void cinder_render(uint32_t *fb, int w, int h, int32_t px, int32_t py, uint16_t angle){
    const int fov = 9000;                 /* ~ 50 deg in 16-bit turns (65536/360*50) */
    const int32_t proj = h;               /* projection plane height in pixels        */
    for (int x = 0; x < w; x++){
        /* ray angle across the field of view */
        uint16_t ra = (uint16_t)(angle + (int)(((int64_t)(x - w/2) * fov) / w));
        int32_t rdx = fx_cos(ra), rdy = fx_sin(ra);
        /* march the ray in 1/16-cell steps until a wall or the bound */
        int32_t rx = px, ry = py; int steps = 0; int tex = 0;
        for (; steps < 12 * 16 * 4; steps++){
            rx += rdx >> 4; ry += rdy >> 4;
            int cx = rx >> 16, cy = ry >> 16;
            if (cx < 0 || cy < 0 || cx >= MW || cy >= MH){ tex = 1; break; }
            if (g_map[cy][cx]){ tex = g_map[cy][cx]; break; }
        }
        /* distance (16.16), fisheye-corrected so walls stay flat */
        int32_t dist = steps * 4096;
        int32_t corr = fx_cos((uint16_t)(ra - angle));
        dist = (int32_t)(((int64_t)dist * corr) >> 16);
        if (dist < 4096) dist = 4096;
        /* projected wall height in pixels */
        int32_t wallh = (int32_t)(((int64_t)proj << 16) / dist);
        int top = h/2 - wallh/2, bot = h/2 + wallh/2;
        int drawtop = top < 0 ? 0 : top, drawbot = bot > h ? h : bot;
        /* texture column coordinate */
        int u = (((rdx > 0 ? rdx : -rdx) > (rdy > 0 ? rdy : -rdy)) ? (ry >> 10) : (rx >> 10)) & 63;
        /* holo depth cue: near -> warm, far -> cool */
        int32_t dcells = dist >> 16;
        int32_t depth_z = 44 - dcells * 6; if (depth_z < -20) depth_z = -20;
        for (int y = 0; y < h; y++){
            uint32_t col;
            if (y < drawtop)      col = holo_shade(0x101828, 0, -18, 8, 96);        /* ceiling (cool) */
            else if (y >= drawbot) col = holo_shade(0x181410, 0,  10, 8, 96);        /* floor (warm)   */
            else {
                int tv = ((y - top) * 64) / (wallh ? wallh : 1);
                col = holo_shade(tex_sample(tex, u, tv), 0, depth_z, 8, 128);
            }
            fb[y * w + x] = 0xFF000000u | col;
        }
    }
}

static void cinder_boot(void){ /* activation hook — the console front-end draws it */ }

void cinder_register_megarom(void){
    static const megarom_t m = {
        "Cinder", "id Tech 1-class raycaster — the graphics evolution, running",
        MR_KIND_GAME, cinder_boot
    };
    megarom_register(&m);
}

int cinder_selfcheck(void){
    enum { W = 200, H = 120 };
    static uint32_t fb[W * H];
    /* camera near the middle of the room, facing +x into walls at varied depth */
    cinder_render(fb, W, H, 6 * 65536, 6 * 65536, 0);

    /* coherence checks */
    int ceiling = 0, floor_ = 0, wall = 0; long var = 0; uint32_t first = fb[0];
    int mid = H/2; int minr = 255, maxr = 0;
    for (int x = 0; x < W; x++){
        if ((fb[(2)*W + x] & 0xFFFFFF) != 0) ceiling++;             /* top band drawn    */
        if ((fb[(H-3)*W + x] & 0xFFFFFF) != 0) floor_++;           /* bottom band drawn */
        uint32_t c = fb[mid*W + x];
        if (c != first) var++;
        if ((c & 0xFFFFFF) != (fb[1*W+x] & 0xFFFFFF)) wall++;       /* wall != ceiling   */
        int r = (c >> 16) & 0xFF;                                   /* mid-band red      */
        if (r < minr) minr = r; if (r > maxr) maxr = r;
    }
    /* holo depth cue active: across the wall band the red channel spans a range
     * (near walls warm / far walls cool), so the depth shading is producing
     * chromostereopsis rather than a flat tint. */
    int depth_ok = (maxr - minr) >= 20;

    return (ceiling > W/2) && (floor_ > W/2) && (wall > W/2) && (var > W/2) && depth_ok;
}
