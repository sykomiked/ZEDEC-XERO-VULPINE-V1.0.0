/* zxv_gpu_run.c — the non-linear build driver: run the D-dimensional
 * scaffold kernel on a real GPU through the CUDA driver API (dlopen'd
 * libcuda.so — no toolkit, no nvrtc: the driver JITs the clang-emitted PTX
 * in-process), verify the device result is bit-identical to the CPU
 * reference, and check the committed lattice routing table against the
 * device-computed rings.
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 *
 * Usage:
 *   zxv_gpu_run <file.ptx> <kernel> <D> [route.bin]
 *
 * D=2 sweeps the whole 144x144 module lattice (20,736 cells, one thread per
 * cell). With route.bin (the canonical image gen_lattice_routing.py emits)
 * the runner additionally proves every module's committed ring equals the
 * ring the GPU computed for its cell — the schedule the parallel build
 * executes is the schedule the ELF carries.
 *
 * D=144 switches to the full-manifold sweep: the same 20,736 threads, but
 * each cell's vector carries a non-zero coordinate on EVERY axis (a
 * diagonal lift, coord[a] = (cell + a*377) mod 144) — all 144 tensor
 * planes lit in one launch, verified bit-identical against the CPU.
 */
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- the CPU reference is the same kernel body --------------------------- */
typedef unsigned int zu;
typedef int zi;
typedef unsigned char zb;
#define ZK_DEV
#define ZVT_KSRC(...) __VA_ARGS__
#include "zvt_seed_dkern.h"
#undef ZVT_KSRC

#define SIDE  144u
#define CELLS (SIDE * SIDE)

/* ---- the CUDA driver API, resolved by hand ------------------------------- */
typedef int CUresult;
typedef int CUdevice;
typedef void *CUcontext;
typedef void *CUmodule;
typedef void *CUfunction;
typedef unsigned long long CUdeviceptr;

static struct {
    CUresult (*init)(unsigned);
    CUresult (*dev_get)(CUdevice *, int);
    CUresult (*prim_retain)(CUcontext *, CUdevice);
    CUresult (*ctx_set)(CUcontext);
    CUresult (*mod_load)(CUmodule *, const void *);
    CUresult (*mod_func)(CUfunction *, CUmodule, const char *);
    CUresult (*mem_alloc)(CUdeviceptr *, size_t);
    CUresult (*mem_free)(CUdeviceptr);
    CUresult (*htod)(CUdeviceptr, const void *, size_t);
    CUresult (*dtoh)(void *, CUdeviceptr, size_t);
    CUresult (*launch)(CUfunction, unsigned, unsigned, unsigned, unsigned, unsigned, unsigned,
                       unsigned, void *, void **, void **);
    CUresult (*sync)(void);
    CUresult (*get_name)(const char **, CUresult);
} cu;

static void *lib;

static int cu_bind(void)
{
    lib = dlopen("libcuda.so.1", RTLD_NOW | RTLD_GLOBAL);
    if (!lib) lib = dlopen("libcuda.so", RTLD_NOW | RTLD_GLOBAL);
    if (!lib) return -1;
#define SYM(field, name)                                                                           \
    do {                                                                                           \
        *(void **) &cu.field = dlsym(lib, name);                                                   \
        if (!cu.field) {                                                                           \
            fprintf(stderr, "missing %s\n", name);                                                 \
            return -1;                                                                             \
        }                                                                                          \
    } while (0)
    SYM(init, "cuInit");
    SYM(dev_get, "cuDeviceGet");
    SYM(prim_retain, "cuDevicePrimaryCtxRetain");
    SYM(ctx_set, "cuCtxSetCurrent");
    SYM(mod_load, "cuModuleLoadData");
    SYM(mod_func, "cuModuleGetFunction");
    SYM(mem_alloc, "cuMemAlloc_v2");
    SYM(mem_free, "cuMemFree_v2");
    SYM(htod, "cuMemcpyHtoD_v2");
    SYM(dtoh, "cuMemcpyDtoH_v2");
    SYM(launch, "cuLaunchKernel");
    SYM(sync, "cuCtxSynchronize");
    cu.get_name = dlsym(lib, "cuGetErrorName"); /* optional */
    return 0;
}

static void cu_die(const char *what, CUresult r)
{
    const char *name = NULL;
    if (cu.get_name) cu.get_name(&name, r);
    fprintf(stderr, "[FAIL] %s: CUresult %d%s%s\n", what, (int) r, name ? " " : "",
            name ? name : "");
    exit(1);
}

#define CU(expr)                                                                                   \
    do {                                                                                           \
        CUresult _r = (expr);                                                                      \
        if (_r) cu_die(#expr, _r);                                                                 \
    } while (0)

/* ---- CPU reference for one lattice cell ----------------------------------- */
static void cpu_cell(zu D, const zu *c, zu *stage, zu *ndep, zu *dia, zu *djc, zu *state)
{
    zu dep[144];
    zu a;
    *stage = zk_d_stage(D, c);
    *ndep = zk_d_deps(D, c, *stage, dia, djc);
    for (a = 0; a < *ndep; a++) dep[a] = (djc[a] + dia[a]) % 4u;
    *state = zk_d_lpres(*ndep, dep);
}

int main(int argc, char **argv)
{
    const char *ptx_path, *kname;
    zu D;
    uint8_t *img = NULL;
    long img_len = 0;
    zu *co, *stage, *ndep, *state, *dia, *djc;
    CUcontext ctx;
    CUmodule mod;
    CUfunction fn;
    CUdevice dev;
    CUdeviceptr d_co, d_stage, d_ndep, d_state, d_dia, d_djc;
    unsigned bad = 0, maxstage = 0;

    if (argc < 4) {
        fprintf(stderr, "usage: %s <file.ptx> <kernel> <D> [route.bin]\n", argv[0]);
        return 2;
    }
    ptx_path = argv[1];
    kname = argv[2];
    D = (zu) strtoul(argv[3], NULL, 10);
    if (D < 2 || D > 144) {
        fprintf(stderr, "[FAIL] D must be 2..144\n");
        return 2;
    }

    if (argc > 4) {
        FILE *f = fopen(argv[4], "rb");
        if (!f) {
            perror("route.bin");
            return 2;
        }
        fseek(f, 0, SEEK_END);
        img_len = ftell(f);
        fseek(f, 0, SEEK_SET);
        img = malloc((size_t) img_len);
        if (fread(img, 1, (size_t) img_len, f) != (size_t) img_len) return 2;
        fclose(f);
    }

    /* host-side inputs: all 144x144 cells, coords at stride 144 */
    co = calloc(CELLS * 144, sizeof(zu));
    stage = malloc(CELLS * sizeof(zu));
    ndep = malloc(CELLS * sizeof(zu));
    state = malloc(CELLS * sizeof(zu));
    dia = malloc(CELLS * 144 * sizeof(zu));
    djc = malloc(CELLS * 144 * sizeof(zu));
    for (zu i = 0; i < CELLS; i++) {
        co[i * 144 + 0] = i % SIDE; /* axis 0 = column */
        co[i * 144 + 1] = i / SIDE; /* axis 1 = row    */
        if (D > 2)
            for (zu a = 2; a < D; a++) co[i * 144 + a] = (i + a * 377u) % SIDE; /* diagonal lift */
    }

    /* CPU reference */
    {
        zu c[144], cdia[144], cdjc[144], st, nd, cs;
        memset(c, 0, sizeof(c));
        memset(cdia, 0, sizeof(cdia));
        memset(cdjc, 0, sizeof(cdjc));
        for (zu i = 0; i < CELLS; i++) {
            cpu_cell(D, co + i * 144, &st, &nd, cdia, cdjc, &cs);
            stage[i] = st;
            ndep[i] = nd;
            state[i] = cs;
            memcpy(dia + i * 144, cdia, sizeof(cdia));
            memcpy(djc + i * 144, cdjc, sizeof(cdjc));
        }
    }

    /* device run */
    if (cu_bind()) {
        fprintf(stderr, "[FAIL] libcuda.so not loadable\n");
        return 1;
    }
    CU(cu.init(0));
    CU(cu.dev_get(&dev, 0));
    CU(cu.prim_retain(&ctx, dev));
    CU(cu.ctx_set(ctx));

    {
        FILE *f = fopen(ptx_path, "rb");
        long n;
        char *ptx;
        if (!f) {
            perror(ptx_path);
            return 2;
        }
        fseek(f, 0, SEEK_END);
        n = ftell(f);
        fseek(f, 0, SEEK_SET);
        ptx = malloc((size_t) n + 1);
        if (fread(ptx, 1, (size_t) n, f) != (size_t) n) return 2;
        ptx[n] = 0;
        fclose(f);
        CU(cu.mod_load(&mod, ptx));
        free(ptx);
    }
    CU(cu.mod_func(&fn, mod, kname));

    CU(cu.mem_alloc(&d_co, CELLS * 144 * sizeof(zu)));
    CU(cu.mem_alloc(&d_stage, CELLS * sizeof(zu)));
    CU(cu.mem_alloc(&d_ndep, CELLS * sizeof(zu)));
    CU(cu.mem_alloc(&d_state, CELLS * sizeof(zu)));
    CU(cu.mem_alloc(&d_dia, CELLS * 144 * sizeof(zu)));
    CU(cu.mem_alloc(&d_djc, CELLS * 144 * sizeof(zu)));
    CU(cu.htod(d_co, co, CELLS * 144 * sizeof(zu)));

    {
        zu n = CELLS;
        void *params[] = {&d_co, &n, (void *) &D, &d_stage, &d_ndep, &d_dia, &d_djc, &d_state};
        CU(cu.launch(fn, (CELLS + 255) / 256, 1, 1, 256, 1, 1, 0, NULL, params, NULL));
    }
    CU(cu.sync());

    {
        zu *g_stage = malloc(CELLS * sizeof(zu));
        zu *g_ndep = malloc(CELLS * sizeof(zu));
        zu *g_state = malloc(CELLS * sizeof(zu));
        zu *g_dia = malloc(CELLS * 144 * sizeof(zu));
        zu *g_djc = malloc(CELLS * 144 * sizeof(zu));
        CU(cu.dtoh(g_stage, d_stage, CELLS * sizeof(zu)));
        CU(cu.dtoh(g_ndep, d_ndep, CELLS * sizeof(zu)));
        CU(cu.dtoh(g_state, d_state, CELLS * sizeof(zu)));
        CU(cu.dtoh(g_dia, d_dia, CELLS * 144 * sizeof(zu)));
        CU(cu.dtoh(g_djc, d_djc, CELLS * 144 * sizeof(zu)));

        for (zu i = 0; i < CELLS; i++) {
            int bad_stage = g_stage[i] != stage[i];
            int bad_ndep = g_ndep[i] != ndep[i];
            int bad_state = g_state[i] != state[i];
            int bad_dep = 0;
            for (zu a = 0; a < ndep[i]; a++)
                if (g_dia[i * 144 + a] != dia[i * 144 + a] ||
                    g_djc[i * 144 + a] != djc[i * 144 + a])
                    bad_dep = 1;
            if (bad_stage || bad_ndep || bad_state || bad_dep) {
                if (bad++ < 5)
                    fprintf(stderr,
                            "cell %u: stage %u/%u ndep %u/%u "
                            "state %u/%u depbad %d\n",
                            i, g_stage[i], stage[i], g_ndep[i], ndep[i], g_state[i], state[i],
                            bad_dep);
            }
            if (g_stage[i] > maxstage) maxstage = g_stage[i];
        }
        free(g_stage);
        free(g_ndep);
        free(g_state);
        free(g_dia);
        free(g_djc);
    }

    cu.mem_free(d_co);
    cu.mem_free(d_stage);
    cu.mem_free(d_ndep);
    cu.mem_free(d_state);
    cu.mem_free(d_dia);
    cu.mem_free(d_djc);

    if (bad) {
        fprintf(stderr, "[FAIL] device/CPU mismatch on %u of %u cells\n", bad, CELLS);
        return 1;
    }
    printf("[PASS] zvk_%s on device: %u cells bit-identical, max stage %u\n", kname, CELLS,
           maxstage);

    /* cross-check the committed routing table against device rings */
    if (img) {
        static const size_t HDR = 4 + 1 + 3 + 4 + 4 + 3 * 4;
        uint32_t nmods = ((uint32_t) img[12] << 24) | ((uint32_t) img[13] << 16) |
                         ((uint32_t) img[14] << 8) | img[15];
        uint32_t ring_bad = 0;
        for (uint32_t m = 0; m < nmods; m++) {
            size_t off = HDR + (size_t) m * 10;
            uint8_t ring = img[off + 5], ri = img[off + 6], rj = img[off + 7];
            zu cell = (zu) rj * SIDE + ri;
            if (stage[cell] != ring) ring_bad++;
        }
        if (ring_bad) {
            fprintf(stderr,
                    "[FAIL] %u module records disagree with the "
                    "device-computed ring\n",
                    ring_bad);
            return 1;
        }
        printf("[PASS] route.bin: %u module rings agree with the device "
               "schedule\n",
               nmods);
    }
    return 0;
}
