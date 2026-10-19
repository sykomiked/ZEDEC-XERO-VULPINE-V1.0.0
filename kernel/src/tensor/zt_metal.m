/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_metal.m — zt_metal.h on Metal. Hosted macOS only; Objective-C without
 * ARC (objects are released by hand), so it builds with any clang:
 *
 *   clang -std=c11 -O2 -fno-objc-arc -c zt_metal.m
 *   ... -framework Metal -framework Foundation
 *
 * One kernel per weight type and token tile: a threadgroup of ZM_R simd
 * groups takes ZM_R rows, a simd group one row; lane l of it sums the
 * zt_metal_math.h terms of sub-blocks l, l + 32, ... for each token of its
 * tile (1 when decoding, ZM_T for a prompt batch), then the lanes' exact
 * 64-bit sums are added and rounded once, as zt_kern_round does.
 *
 * The whole forward pass (zt_gpu.h, zt_kern_metal_resident): every other
 * kernel of zt_metal_math.h gets a wrapper, one thread per output; the op
 * list of zt_gpu.c is encoded into one serial compute encoder (each
 * dispatch sees the previous ones' writes) and committed once per pass. */
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "zt_gpu.h"
#include "zt_metal.h"

#if __has_feature(objc_arc)
#    error "zt_metal.m releases its objects by hand: build it with -fno-objc-arc"
#endif

#define ZM_R       4u /* rows (simd groups) per threadgroup */
#define ZM_T       8u /* tokens per tile when B > 1 */
#define ZM_REGIONS 16u

#define ZT_METAL_SRC(...) #__VA_ARGS__
static const char k_math[] =
#include "zt_metal_math.h"
    ;

static const char k_prelude[] = "#include <metal_stdlib>\n"
                                "using namespace metal;\n"
                                "typedef long zl;\n"
                                "typedef ulong zw;\n"
                                "typedef int zi;\n"
                                "typedef uint zu;\n"
                                "typedef short zs;\n"
                                "typedef uchar zb;\n"
                                "typedef char zc;\n"
                                "#define ZM_DEV device\n"
                                "typedef atomic_uint zm_ctr;\n"
                                "static inline void zm_count(device zm_ctr *c, uint n)\n"
                                "{\n"
                                "    if (n) atomic_fetch_add_explicit(c, n, memory_order_relaxed);\n"
                                "}\n"
                                "#define ZK_A uint i, zg_p p, device zb *b0, device zb *b1, "
                                "device zb *b2, device zb *b3, device zb *b4, device zb *b5, "
                                "device zm_ctr *ctr\n";

/* one wrapper per zt_gpu.h kernel: thread i of p.u[0] */
static const char k_wrap[] =
    "kernel void %s_k(device zb *b0 [[buffer(0)]], device zb *b1 [[buffer(1)]],\n"
    "    device zb *b2 [[buffer(2)]], device zb *b3 [[buffer(3)]], device zb *b4 [[buffer(4)]],\n"
    "    device zb *b5 [[buffer(5)]], device zm_ctr *ctr [[buffer(6)]],\n"
    "    constant zg_p &p [[buffer(7)]], uint i [[thread_position_in_grid]])\n"
    "{\n"
    "    if (i < p.u[0]) %s(i, p, b0, b1, b2, b3, b4, b5, ctr);\n"
    "}\n";

static const char k_kernels[] =
    "\n"
    "struct zm_p { uint rows, nb, astride, B, rowbytes; };\n"
    "\n"
    "/* lane 0 gets the sum of all 32 lanes' values (exact: integers) */\n"
    "static inline zl zm_simd_sum(zl v)\n"
    "{\n"
    "    for (ushort off = 16; off > 0; off >>= 1) {\n"
    "        uint2 u = as_type<uint2>(v);\n"
    "        uint2 w = uint2(simd_shuffle_down(u.x, off), simd_shuffle_down(u.y, off));\n"
    "        v += as_type<zl>(w);\n"
    "    }\n"
    "    return v;\n"
    "}\n"
    "\n"
    "template <uint QT, uint T>\n"
    "static inline void zm_body(device const uchar *w, device const short *a,\n"
    "                           device const uchar *ash, device const int *asum, device int *y,\n"
    "                           constant zm_p &p, uint2 tg, uint sg, uint lane)\n"
    "{\n"
    "    uint r = tg.x * 4u + sg;\n"
    "    if (r >= p.rows) return; /* the whole simd group: r depends on sg only */\n"
    "    uint t0 = tg.y * T, nt = min(T, p.B - t0), sb = p.astride / 32u;\n"
    "    device const uchar *row = w + (ulong) r * p.rowbytes;\n"
    "    zl acc[T];\n"
    "    for (uint t = 0; t < T; t++) acc[t] = 0;\n"
    "    for (uint b = lane; b < p.nb; b += 32u) {\n"
    "        for (uint t = 0; t < T; t++) {\n"
    "            if (t < nt) {\n"
    "                ulong tok = t0 + t;\n"
    "                acc[t] += zm_sub(QT, row, b, a + tok * p.astride + b * 32u,\n"
    "                                 (zi) ash[tok * sb + b], asum[tok * sb + b]);\n"
    "            }\n"
    "        }\n"
    "    }\n"
    "    for (uint t = 0; t < T; t++) {\n"
    "        zl v = zm_simd_sum(acc[t]);\n"
    "        if (lane == 0 && t < nt) y[(ulong) (t0 + t) * p.rows + r] = zm_round(v);\n"
    "    }\n"
    "}\n"
    "\n"
    "#define ZM_ARGS device const uchar *w [[buffer(0)]], device const short *a [[buffer(1)]], \\\n"
    "    device const uchar *ash [[buffer(2)]], device const int *asum [[buffer(3)]], \\\n"
    "    device int *y [[buffer(4)]], constant zm_p &p [[buffer(5)]], \\\n"
    "    uint2 tg [[threadgroup_position_in_grid]], uint sg [[simdgroup_index_in_threadgroup]], "
    "\\\n"
    "    uint lane [[thread_index_in_simdgroup]]\n"
    "#define ZM_KERNEL(name, QT, T) \\\n"
    "    kernel void name(ZM_ARGS) { zm_body<QT, T>(w, a, ash, asum, y, p, tg, sg, lane); }\n"
    "ZM_KERNEL(zm_q4k_1, 12u, 1u)\n"
    "ZM_KERNEL(zm_q6k_1, 14u, 1u)\n"
    "ZM_KERNEL(zm_q80_1, 8u, 1u)\n"
    "ZM_KERNEL(zm_q4k_t, 12u, 8u)\n"
    "ZM_KERNEL(zm_q6k_t, 14u, 8u)\n"
    "ZM_KERNEL(zm_q80_t, 8u, 8u)\n";

typedef struct {
    uint32_t rows, nb, astride, B, rowbytes;
} zm_p_t;

typedef struct {
    const uint8_t *base;
    uint64_t len;
    id<MTLBuffer> buf;
    bool nocopy; /* the caller's memory itself (else a copy made at map time) */
} region_t;

struct zt_metal {
    id<MTLDevice> dev;
    id<MTLCommandQueue> q;
    id<MTLComputePipelineState> pso[2][3]; /* [tiled][q4k, q6k, q80] */
    region_t reg[ZM_REGIONS];
    uint32_t n_reg;
    id<MTLBuffer> in, out;
    id<MTLComputePipelineState> zk[ZG_NK]; /* zt_gpu.h kernels (ZG_K_MM uses pso) */
    id<MTLBuffer> work, cst;               /* zt_gpu.h work and constants */
    char name[128];
    zt_metal_stats_t st;
    uint32_t min_tokens; /* fewer tokens than this: declined (the CPU decodes) */
};

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t) ts.tv_sec * 1000000000ull + (uint64_t) ts.tv_nsec;
}

static void say(char *err, uint32_t cap, const char *what, NSError *e)
{
    if (!err || !cap) return;
    snprintf(err, cap, "%s%s%s", what, e ? ": " : "",
             e ? [[e localizedDescription] UTF8String] : "");
}

zt_metal_t *zt_metal_new(char *err, uint32_t errcap)
{
    @autoreleasepool {
        id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
        if (!dev) {
            say(err, errcap, "no Metal device", nil);
            return NULL;
        }
        zt_metal_t *mt = calloc(1, sizeof *mt);
        if (!mt) {
            [dev release];
            return NULL;
        }
        mt->dev = dev;
        snprintf(mt->name, sizeof mt->name, "%s", [[dev name] UTF8String]);
        mt->q = [dev newCommandQueue];
        size_t n = strlen(k_prelude) + strlen(k_math) + strlen(k_kernels) + 1;
        for (int k = 0; k < ZG_NK; k++) n += sizeof k_wrap + 2 * strlen(zg_kernel_name[k]);
        char *src = malloc(n);
        if (!mt->q || !src) {
            free(src);
            zt_metal_free(mt);
            say(err, errcap, "out of memory", nil);
            return NULL;
        }
        size_t at = (size_t) snprintf(src, n, "%s%s%s", k_prelude, k_math, k_kernels);
        for (int k = 0; k < ZG_NK; k++)
            at += (size_t) snprintf(src + at, n - at, k_wrap, zg_kernel_name[k], zg_kernel_name[k]);
        NSError *e = nil;
        MTLCompileOptions *opt = [[MTLCompileOptions alloc] init];
        id<MTLLibrary> lib = [dev newLibraryWithSource:[NSString stringWithUTF8String:src]
                                               options:opt
                                                 error:&e];
        [opt release];
        free(src);
        if (!lib) {
            say(err, errcap, "the Metal kernels did not compile", e);
            zt_metal_free(mt);
            return NULL;
        }
        static const char *names[2][3] = {{"zm_q4k_1", "zm_q6k_1", "zm_q80_1"},
                                          {"zm_q4k_t", "zm_q6k_t", "zm_q80_t"}};
        for (int t = 0; t < 2; t++)
            for (int k = 0; k < 3; k++) {
                id<MTLFunction> f = [lib newFunctionWithName:@(names[t][k])];
                mt->pso[t][k] = f ? [dev newComputePipelineStateWithFunction:f error:&e] : nil;
                [f release];
                /* the kernels assume 32-lane simd groups and ZM_R of them per group */
                if (!mt->pso[t][k] || mt->pso[t][k].threadExecutionWidth != 32 ||
                    mt->pso[t][k].maxTotalThreadsPerThreadgroup < 32 * ZM_R) {
                    say(err, errcap, "a Metal kernel cannot run on this GPU", e);
                    [lib release];
                    zt_metal_free(mt);
                    return NULL;
                }
            }
        for (int k = 0; k < ZG_NK; k++) {
            char fn[64];
            snprintf(fn, sizeof fn, "%s_k", zg_kernel_name[k]);
            id<MTLFunction> f = [lib newFunctionWithName:@(fn)];
            mt->zk[k] = f ? [dev newComputePipelineStateWithFunction:f error:&e] : nil;
            [f release];
            if (!mt->zk[k]) {
                say(err, errcap, "a Metal layer kernel cannot run on this GPU", e);
                [lib release];
                zt_metal_free(mt);
                return NULL;
            }
        }
        [lib release];
        return mt;
    }
}

void zt_metal_free(zt_metal_t *mt)
{
    if (!mt) return;
    for (uint32_t i = 0; i < mt->n_reg; i++) [mt->reg[i].buf release];
    for (int t = 0; t < 2; t++)
        for (int k = 0; k < 3; k++) [mt->pso[t][k] release];
    for (int k = 0; k < ZG_NK; k++) [mt->zk[k] release];
    [mt->work release];
    [mt->cst release];
    [mt->in release];
    [mt->out release];
    [mt->q release];
    [mt->dev release];
    free(mt);
}

const char *zt_metal_name(const zt_metal_t *mt)
{
    return mt ? mt->name : "";
}

int zt_metal_map(zt_metal_t *mt, const void *base, uint64_t len)
{
    if (!mt || !base || !len || mt->n_reg >= ZM_REGIONS) return -1;
    if (len > [mt->dev maxBufferLength]) return -1;
    uint64_t pg = (uint64_t) getpagesize();
    id<MTLBuffer> b = nil;
    if ((uintptr_t) base % pg == 0) {
        uint64_t rl = (len + pg - 1) / pg * pg;
        b = [mt->dev newBufferWithBytesNoCopy:(void *) base
                                       length:rl
                                      options:MTLResourceStorageModeShared
                                  deallocator:nil];
        if (b) mt->st.nocopy++;
    }
    bool nocopy = b != nil;
    if (!b) b = [mt->dev newBufferWithBytes:base length:len options:MTLResourceStorageModeShared];
    if (!b) return -1;
    mt->reg[mt->n_reg].nocopy = nocopy;
    mt->reg[mt->n_reg].base = base;
    mt->reg[mt->n_reg].len = len;
    mt->reg[mt->n_reg].buf = b;
    mt->n_reg++;
    return 0;
}

static int find_region(const zt_metal_t *mt, const uint8_t *p, uint64_t len, uint64_t *off)
{
    for (uint32_t i = 0; i < mt->n_reg; i++) {
        const region_t *r = &mt->reg[i];
        if (p >= r->base && (uint64_t) (p - r->base) <= r->len &&
            len <= r->len - (uint64_t) (p - r->base)) {
            *off = (uint64_t) (p - r->base);
            return (int) i;
        }
    }
    return -1;
}

static int grow(zt_metal_t *mt, id<MTLBuffer> *b, uint64_t need)
{
    if (*b && [*b length] >= need) return 0;
    [*b release];
    uint64_t sz = need + need / 4 + 4096;
    *b = [mt->dev newBufferWithLength:sz options:MTLResourceStorageModeShared];
    return *b ? 0 : -1;
}

#define AL(x) (((x) + 255u) & ~(uint64_t) 255u)

int zt_metal_mm(void *fctx, const zt_mmf_t *jobs, uint32_t n)
{
    zt_metal_t *mt = fctx;
    if (!mt || n == 0 || n > 8) return 1;
    uint64_t t_in = now_ns();
    mt->st.calls++;
    if (jobs[0].B < mt->min_tokens) {
        mt->st.declined++;
        return 1;
    }
    int ri[8];
    uint64_t woff[8], aoff[8], soff[8], hoff[8], yoff[8];
    uint64_t in_bytes = 0, out_bytes = 0;
    for (uint32_t i = 0; i < n; i++) {
        const zt_mmf_t *j = &jobs[i];
        uint32_t nb = j->cols / 32;
        bool ok = j->raw && j->B && j->cols % 32 == 0 && j->astride >= j->cols &&
                  j->astride % 32 == 0 &&
                  (j->qtype == 8 || ((j->qtype == 12 || j->qtype == 14) && j->cols % 256 == 0));
        uint64_t rb = j->qtype == 12   ? (uint64_t) (nb / 8) * 144
                      : j->qtype == 14 ? (uint64_t) (nb / 8) * 210
                                       : (uint64_t) nb * 34;
        if (ok) ri[i] = find_region(mt, j->raw, rb * j->rows, &woff[i]);
        if (!ok || ri[i] < 0) {
            mt->st.declined++;
            return 1;
        }
        /* inputs: one copy per distinct activation set (q/k/v share theirs) */
        if (i && j->a == jobs[i - 1].a && j->ash == jobs[i - 1].ash &&
            j->asum == jobs[i - 1].asum && j->astride == jobs[i - 1].astride &&
            j->B == jobs[i - 1].B) {
            aoff[i] = aoff[i - 1], hoff[i] = hoff[i - 1], soff[i] = soff[i - 1];
        } else {
            uint64_t nbk = (uint64_t) j->B * (j->astride / 32);
            aoff[i] = in_bytes;
            soff[i] = aoff[i] + AL((uint64_t) j->B * j->astride * 2);
            hoff[i] = soff[i] + AL(nbk * 4);
            in_bytes = hoff[i] + AL(nbk);
        }
        yoff[i] = out_bytes;
        out_bytes += AL((uint64_t) j->B * j->rows * 4);
    }
    @autoreleasepool {
        if (grow(mt, &mt->in, in_bytes) || grow(mt, &mt->out, out_bytes)) {
            mt->st.declined++;
            return 1;
        }
        uint8_t *in = [mt->in contents], *out = [mt->out contents];
        for (uint32_t i = 0; i < n; i++) {
            const zt_mmf_t *j = &jobs[i];
            if (i && aoff[i] == aoff[i - 1]) continue;
            uint64_t nbk = (uint64_t) j->B * (j->astride / 32);
            memcpy(in + aoff[i], j->a, (size_t) j->B * j->astride * 2);
            memcpy(in + soff[i], j->asum, (size_t) nbk * 4);
            memcpy(in + hoff[i], j->ash, (size_t) nbk);
        }
        id<MTLCommandBuffer> cb = [mt->q commandBufferWithUnretainedReferences];
        id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];
        for (uint32_t i = 0; i < n; i++) {
            const zt_mmf_t *j = &jobs[i];
            uint32_t nb = j->cols / 32, tiled = j->B > 1;
            int k = j->qtype == 12 ? 0 : j->qtype == 14 ? 1 : 2;
            zm_p_t p = {j->rows, nb, j->astride, j->B,
                        k == 0   ? (nb / 8) * 144
                        : k == 1 ? (nb / 8) * 210
                                 : nb * 34};
            [enc setComputePipelineState:mt->pso[tiled][k]];
            [enc setBuffer:mt->reg[ri[i]].buf offset:woff[i] atIndex:0];
            [enc setBuffer:mt->in offset:aoff[i] atIndex:1];
            [enc setBuffer:mt->in offset:hoff[i] atIndex:2];
            [enc setBuffer:mt->in offset:soff[i] atIndex:3];
            [enc setBuffer:mt->out offset:yoff[i] atIndex:4];
            [enc setBytes:&p length:sizeof p atIndex:5];
            uint32_t T = tiled ? ZM_T : 1;
            [enc dispatchThreadgroups:MTLSizeMake((j->rows + ZM_R - 1) / ZM_R, (j->B + T - 1) / T,
                                                  1)
                threadsPerThreadgroup:MTLSizeMake(32 * ZM_R, 1, 1)];
        }
        [enc endEncoding];
        [cb commit];
        [cb waitUntilCompleted];
        if ([cb status] != MTLCommandBufferStatusCompleted) {
            mt->st.declined++;
            return 1;
        }
        mt->st.gpu_ns += (uint64_t) (([cb GPUEndTime] - [cb GPUStartTime]) * 1e9);
        for (uint32_t i = 0; i < n; i++) {
            const zt_mmf_t *j = &jobs[i];
            const zt_fx *src = (const zt_fx *) (out + yoff[i]);
            for (uint32_t t = 0; t < j->B; t++)
                memcpy(j->y + (uint64_t) t * j->ystride, src + (uint64_t) t * j->rows,
                       (size_t) j->rows * 4);
        }
    }
    mt->st.products += n;
    mt->st.wall_ns += now_ns() - t_in;
    return 0;
}

void zt_kern_metal(zt_kern_t *out, const zt_kern_t *base, zt_metal_t *mt)
{
    *out = *base;
    out->mm_full = mt ? zt_metal_mm : 0;
    out->fwd_full = 0;
    out->fctx = mt;
}

/* ---- the whole forward pass (zt_gpu.h) ---- */

static void *be_work(void *ctx, uint64_t bytes)
{
    zt_metal_t *mt = ctx;
    return grow(mt, &mt->work, bytes) ? NULL : [mt->work contents];
}

static void *be_cst(void *ctx, uint64_t bytes)
{
    zt_metal_t *mt = ctx;
    return grow(mt, &mt->cst, bytes ? bytes : 4) ? NULL : [mt->cst contents];
}

static int be_bind(zt_metal_t *mt, id<MTLComputeCommandEncoder> enc, const zg_buf_t *b, int idx)
{
    switch (b->kind) {
    case ZG_B_WORK: [enc setBuffer:mt->work offset:b->off atIndex:idx]; return 0;
    case ZG_B_CONST: [enc setBuffer:mt->cst offset:b->off atIndex:idx]; return 0;
    case ZG_B_HOST: {
        uint64_t off;
        int ri = find_region(mt, b->host, b->len, &off);
        if (ri < 0 || (b->inplace && !mt->reg[ri].nocopy)) return 1;
        [enc setBuffer:mt->reg[ri].buf offset:off atIndex:idx];
        return 0;
    }
    default: [enc setBuffer:mt->work offset:0 atIndex:idx]; return 0; /* unused argument */
    }
}

static int be_run(void *ctx, const zg_op_t *ops, uint32_t n, const void *work, const void *cst)
{
    zt_metal_t *mt = ctx;
    (void) work, (void) cst;
    @autoreleasepool {
        id<MTLCommandBuffer> cb = [mt->q commandBufferWithUnretainedReferences];
        id<MTLComputeCommandEncoder> enc =
            [cb computeCommandEncoderWithDispatchType:MTLDispatchTypeSerial];
        int bad = 0;
        for (uint32_t i = 0; i < n && !bad; i++) {
            const zg_op_t *o = &ops[i];
            if (!o->u[0]) continue;
            if (o->k == ZG_K_MM) {
                int k = o->u[4] == 12 ? 0 : o->u[4] == 14 ? 1 : o->u[4] == 8 ? 2 : -1;
                uint32_t B = o->u[6], tiled = B > 1, T = tiled ? ZM_T : 1;
                if (k < 0) {
                    bad = 1;
                    break;
                }
                zm_p_t p = {o->u[1], o->u[2], o->u[3], B, o->u[5]};
                [enc setComputePipelineState:mt->pso[tiled][k]];
                for (int j = 0; j < 5 && !bad; j++) bad = be_bind(mt, enc, &o->b[j], j);
                [enc setBytes:&p length:sizeof p atIndex:5];
                [enc dispatchThreadgroups:MTLSizeMake((o->u[1] + ZM_R - 1) / ZM_R, (B + T - 1) / T, 1)
                    threadsPerThreadgroup:MTLSizeMake(32 * ZM_R, 1, 1)];
                continue;
            }
            if (o->k >= ZG_NK) {
                bad = 1;
                break;
            }
            id<MTLComputePipelineState> ps = mt->zk[o->k];
            [enc setComputePipelineState:ps];
            for (int j = 0; j < 6 && !bad; j++) bad = be_bind(mt, enc, &o->b[j], j);
            [enc setBuffer:mt->work offset:0 atIndex:6]; /* the saturation counter */
            [enc setBytes:o->u length:sizeof o->u atIndex:7];
            NSUInteger tg = ps.maxTotalThreadsPerThreadgroup < 256 ? ps.maxTotalThreadsPerThreadgroup : 256;
            [enc dispatchThreadgroups:MTLSizeMake((o->u[0] + tg - 1) / tg, 1, 1)
                threadsPerThreadgroup:MTLSizeMake(tg, 1, 1)];
        }
        [enc endEncoding];
        if (bad) return 1; /* never committed: nothing ran */
        [cb commit];
        [cb waitUntilCompleted];
        if ([cb status] != MTLCommandBufferStatusCompleted) return 1;
        mt->st.gpu_ns += (uint64_t) (([cb GPUEndTime] - [cb GPUStartTime]) * 1e9);
    }
    return 0;
}

int zt_metal_fwd(void *fctx, const void *m, void *s, uint32_t B, zt_fx *logits, uint32_t first,
                 uint32_t *n_sat)
{
    zt_metal_t *mt = fctx;
    if (!mt) return 1;
    uint64_t t0 = now_ns();
    zg_backend_t be = {mt, be_work, be_cst, be_run};
    int r = zg_forward(&be, m, s, B, logits, first, n_sat);
    if (r) {
        mt->st.passes_declined++;
        return r;
    }
    mt->st.passes++;
    mt->st.pass_ops += zg_last_ops();
    mt->st.wall_ns += now_ns() - t0;
    return 0;
}

void zt_kern_metal_resident(zt_kern_t *out, const zt_kern_t *base, zt_metal_t *mt)
{
    zt_kern_metal(out, base, mt);
    out->fwd_full = mt ? zt_metal_fwd : 0;
}

void zt_metal_set_min_tokens(zt_metal_t *mt, uint32_t n)
{
    if (mt) mt->min_tokens = n;
}

void zt_metal_stats(const zt_metal_t *mt, zt_metal_stats_t *out)
{
    if (mt) *out = mt->st;
}
