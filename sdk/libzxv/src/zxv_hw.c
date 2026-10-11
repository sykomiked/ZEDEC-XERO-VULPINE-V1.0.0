/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_hw.c — ABI 1.4: hardware profiles, the emulation matrix, the
 * standard engines and signed hardware updates, over kernel/src/hwemu
 * (hwp.h, hwm.h, hwe.h, hwr.h, hwa.h). */
#if !defined(_WIN32) && !defined(__APPLE__)
#    define _GNU_SOURCE /* sysconf cache-line names, getauxval */
#endif
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "zxv_internal.h"
#include "hwa.h"
#include "hwe.h"
#include "hwm.h"
#include "hwp.h"
#include "hwr.h"
#include "lpres_hex.h"
#include "sha256.h"
#ifndef ZXV_NO_PQC
#    include "hwr_pq.h"
#    include "pq_matrix_algs.h"
#endif

#if defined(_WIN32)
#    include <windows.h>
#elif defined(__APPLE__)
#    include <sys/sysctl.h>
#    include <sys/types.h>
#    include <unistd.h>
#else
#    include <unistd.h>
#    if defined(__aarch64__)
#        include <sys/auxv.h>
#    endif
#endif

/* The matrix lock is recursive: a provider may call its dependencies back
 * through the matrix. libzxv's Windows pthread shim has only SRWLOCKs, which
 * cannot recurse, so Windows uses a CRITICAL_SECTION (recursive by design). */
#if defined(_WIN32)
typedef CRITICAL_SECTION rmutex_t;
static void rmutex_init(rmutex_t *m) { InitializeCriticalSection(m); }
static void rmutex_destroy(rmutex_t *m) { DeleteCriticalSection(m); }
static void rmutex_lock(rmutex_t *m) { EnterCriticalSection(m); }
static void rmutex_unlock(rmutex_t *m) { LeaveCriticalSection(m); }
#else
typedef pthread_mutex_t rmutex_t;
static void rmutex_init(rmutex_t *m) {
    pthread_mutexattr_t a;
    pthread_mutexattr_init(&a);
    pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(m, &a);
    pthread_mutexattr_destroy(&a);
}
static void rmutex_destroy(rmutex_t *m) { pthread_mutex_destroy(m); }
static void rmutex_lock(rmutex_t *m) { pthread_mutex_lock(m); }
static void rmutex_unlock(rmutex_t *m) { pthread_mutex_unlock(m); }
#endif

/* The kernel and ABI numbers are the same values; keep them so. */
_Static_assert(ZXV_HW_CAP_ISA == HWP_CAP_ISA, "cap key");
_Static_assert(ZXV_HW_CAP_PEAK_OPS_MHZ == HWP_CAP_PEAK_OPS_MHZ, "cap key");
_Static_assert(ZXV_HW_CAP_TICK_NS == HWP_CAP_TICK_NS, "cap key");
_Static_assert(ZXV_HW_CAP_INT_WIDTHS == HWP_CAP_INT_WIDTHS, "cap key");
_Static_assert(ZXV_HW_CAP_CONTRACT_ID == HWP_CAP_CONTRACT_ID, "cap key");
_Static_assert(ZXV_HW_CAP_CONTRACT_VER == HWP_CAP_CONTRACT_VER, "cap key");
_Static_assert(ZXV_HW_CAP_KIND == HWP_CAP_KIND, "cap key");
_Static_assert(ZXV_HW_CAP_POWER_BUCKET == HWP_CAP_POWER_BUCKET, "cap key");
_Static_assert(ZXV_HW_CLASS_GPU == HWP_CLASS_GPU, "class");
_Static_assert(ZXV_HW_CLASS_IO == HWP_CLASS_IO && ZXV_HW_CLASS_IO == HWP_CLASS_PUBLIC_MAX, "class");
_Static_assert(ZXV_HW_EXT_MAX == HWP_MAX_EXT, "ext");
_Static_assert(ZXV_HW_KIND_HARD == HWM_KIND_HARD && ZXV_HW_KIND_REMOTE == HWM_KIND_REMOTE, "kind");
_Static_assert(ZXV_HW_KIND_SOFT == HWM_KIND_SOFT && ZXV_HW_KIND_SOFT == HWP_KIND_SOFT, "kind");
_Static_assert(ZXV_HW_POLICY_KEY_BEARING == HWM_F_KEY_BEARING &&
                   ZXV_HW_POLICY_LOCAL_ONLY == HWM_F_LOCAL_ONLY, "policy");
_Static_assert(ZXV_HW_PF_FAULT == HWM_PF_FAULT && ZXV_HW_PF_SERVING == HWM_PF_SERVING, "pf");
_Static_assert(ZXV_HW_FN_SHA256 == HWE_FN_SHA256 && ZXV_HW_FN_TENSOR == HWE_FN_TENSOR, "engine");
_Static_assert(ZXV_HW_EV_STAGED == HWA_EV_STAGED && ZXV_HW_EV_CONTRACT == HWA_EV_CONTRACT, "event");
_Static_assert(ZXV_HW_IMG_VECTORS == HWR_KIND_VECTORS, "image kind");
_Static_assert(ZXV_HW_PK_BYTES == HWR_DUAL_PK && ZXV_HW_SIG_BYTES == HWR_DUAL_SIG, "sizes");
_Static_assert(ZXV_HW_HEADER_BYTES == HWR_HEADER, "sizes");
_Static_assert(ZXV_HW_RES_OFFLINE == HWM_RES_OFFLINE, "res state");
_Static_assert(ZXV_HW_REGION_HALTED == HWR_ST_HALTED, "region state");
_Static_assert(ZXV_LP_TRUE == LPRES_STATE_TRUE && ZXV_LP_BOTH == LPRES_STATE_BOTH, "lp");

/* ===================== profiles ===================== */

struct zxv_hw_profile {
    hwp_profile_t p;
};

static int32_t hwp_status(int rc, const char *what)
{
    switch (rc) {
    case HWP_OK: return zxvi_ok();
    case HWP_E_FULL: return zxvi_fail(ZXV_E_CAPACITY, "%s: profile table full", what);
    case HWP_E_FORMAT: return zxvi_fail(ZXV_E_FORMAT, "%s: not a ZHP-1 profile", what);
    case HWP_E_CANONICAL: return zxvi_fail(ZXV_E_FORMAT, "%s: not canonical (ZHP-1 P1-P3)", what);
    case HWP_E_BUFFER: return zxvi_fail(ZXV_E_BUFFER, "%s: buffer too small", what);
    case HWP_E_DUPLICATE: return zxvi_fail(ZXV_E_DUPLICATE, "%s: duplicate cap id", what);
    case HWP_E_NOT_FOUND: return zxvi_fail(ZXV_E_NOT_FOUND, "%s: no such node", what);
    default: return zxvi_fail(ZXV_E_ARG, "%s: invalid argument", what);
    }
}

int32_t zxv_hw_profile_create(zxv_hw_profile **out)
{
    if (!out) return zxvi_fail(ZXV_E_ARG, "out is NULL");
    *out = NULL;
    zxv_hw_profile *p = calloc(1, sizeof *p);
    if (!p) return zxvi_fail(ZXV_E_NOMEM, "profile");
    hwp_init(&p->p);
    *out = p;
    return zxvi_ok();
}

void zxv_hw_profile_destroy(zxv_hw_profile *p)
{
    free(p);
}

int32_t zxv_hw_profile_reference(zxv_hw_profile **out)
{
    int32_t rc = zxv_hw_profile_create(out);
    if (rc) return rc;
    static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&mu); /* hwp_reference_r1 builds once */
    memcpy(&(*out)->p, hwp_reference_r1(), sizeof(hwp_profile_t));
    pthread_mutex_unlock(&mu);
    return zxvi_ok();
}

int32_t zxv_hw_profile_present(const zxv_hw_profile *host, const zxv_hw_profile *target,
                               zxv_hw_profile **out)
{
    if (!out) return zxvi_fail(ZXV_E_ARG, "out is NULL");
    *out = NULL;
    if (!host || !target) return zxvi_fail(ZXV_E_ARG, "host or target is NULL");
    zxv_hw_profile *p = calloc(1, sizeof *p);
    if (!p) return zxvi_fail(ZXV_E_NOMEM, "profile");
    static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&mu); /* hwp_present uses one static match table */
    int rc = hwp_present(&host->p, &target->p, &p->p);
    pthread_mutex_unlock(&mu);
    if (rc != HWP_OK) {
        free(p);
        return hwp_status(rc, "profile_present");
    }
    *out = p;
    return zxvi_ok();
}

/* ---- host probe ---- */

typedef struct {
    uint64_t cores, mem, line, simd;
    uint32_t isa;
} host_t;

#if defined(__APPLE__)
static uint64_t sysctl_u64(const char *name)
{
    uint64_t v = 0;
    size_t n = sizeof v;
    if (sysctlbyname(name, &v, &n, NULL, 0) != 0) return 0;
    if (n == 4) return (uint32_t) v;
    return v;
}
#endif

static void probe(host_t *h)
{
    memset(h, 0, sizeof *h);
#if defined(__x86_64__) || defined(_M_X64)
    h->isa = HWP_ISA_X86_64;
#elif defined(__aarch64__) || defined(_M_ARM64)
    h->isa = HWP_ISA_ARM64;
    h->simd |= HWP_SIMD_NEON; /* mandatory in AArch64 */
#elif defined(__riscv) && __riscv_xlen == 64
    h->isa = HWP_ISA_RISCV64;
#elif defined(__wasm32__)
    h->isa = HWP_ISA_WASM32;
#endif
#if (defined(__x86_64__)) && (defined(__GNUC__) || defined(__clang__))
    __builtin_cpu_init();
    if (__builtin_cpu_supports("sse4.2")) h->simd |= HWP_SIMD_SSE42;
    if (__builtin_cpu_supports("avx2")) h->simd |= HWP_SIMD_AVX2;
    if (__builtin_cpu_supports("avx512f")) h->simd |= HWP_SIMD_AVX512;
#    if defined(__GNUC__) && !defined(__clang__) && __GNUC__ >= 11
    if (__builtin_cpu_supports("avx512vnni")) h->simd |= HWP_SIMD_VNNI;
#    endif
#endif
#if defined(_WIN32)
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    h->cores = si.dwNumberOfProcessors;
    MEMORYSTATUSEX ms;
    ms.dwLength = sizeof ms;
    if (GlobalMemoryStatusEx(&ms)) h->mem = ms.ullTotalPhys;
#elif defined(__APPLE__)
    h->cores = sysctl_u64("hw.ncpu");
    h->mem = sysctl_u64("hw.memsize");
    h->line = sysctl_u64("hw.cachelinesize");
#    if defined(__aarch64__)
    if (sysctl_u64("hw.optional.arm.FEAT_DotProd")) h->simd |= HWP_SIMD_DOTPROD;
    if (sysctl_u64("hw.optional.arm.FEAT_I8MM")) h->simd |= HWP_SIMD_I8MM;
#    endif
#else
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    if (n > 0) h->cores = (uint64_t) n;
    long pages = sysconf(_SC_PHYS_PAGES), psz = sysconf(_SC_PAGESIZE);
    if (pages > 0 && psz > 0) h->mem = (uint64_t) pages * (uint64_t) psz;
#    ifdef _SC_LEVEL1_DCACHE_LINESIZE
    long line = sysconf(_SC_LEVEL1_DCACHE_LINESIZE);
    if (line > 0) h->line = (uint64_t) line;
#    endif
#    if defined(__aarch64__) && defined(AT_HWCAP)
    unsigned long hw = getauxval(AT_HWCAP);
#        ifdef HWCAP_ASIMDDP
    if (hw & HWCAP_ASIMDDP) h->simd |= HWP_SIMD_DOTPROD;
#        endif
#        ifdef HWCAP_SVE
    if (hw & HWCAP_SVE) h->simd |= HWP_SIMD_SVE;
#        endif
#        if defined(AT_HWCAP2) && defined(HWCAP2_I8MM)
    if (getauxval(AT_HWCAP2) & HWCAP2_I8MM) h->simd |= HWP_SIMD_I8MM;
#        endif
    (void) hw;
#    endif
#endif
}

int32_t zxv_hw_profile_probe_host(zxv_hw_profile **out)
{
    int32_t rc = zxv_hw_profile_create(out);
    if (rc) return rc;
    hwp_profile_t *p = &(*out)->p;
    host_t h;
    probe(&h);
    uint16_t i;
    hwp_node_add(p, 1, HWP_ROOT, HWP_CLASS_MACHINE, &i);
    hwp_node_add(p, 2, 1, HWP_CLASS_CPU_CLUSTER, &i);
    hwp_cap_set(p, i, HWP_CAP_WORD_BITS, sizeof(void *) * 8u);
    if (h.cores) hwp_cap_set(p, i, HWP_CAP_COUNT, h.cores);
    if (h.isa) hwp_cap_set(p, i, HWP_CAP_ISA, h.isa);
    if (h.mem) {
        hwp_node_add(p, 3, 1, HWP_CLASS_MEMORY, &i);
        hwp_cap_set(p, i, HWP_CAP_BYTES, h.mem);
    }
    if (h.simd) {
        hwp_node_add(p, 4, 2, HWP_CLASS_VECTOR_UNIT, &i);
        hwp_cap_set(p, i, HWP_CAP_INT_WIDTHS, HWP_INT_8 | HWP_INT_16 | HWP_INT_32 | HWP_INT_64);
        hwp_cap_set(p, i, HWP_CAP_SIMD, h.simd);
    }
    if (h.line) {
        hwp_node_add(p, 5, 2, HWP_CLASS_CACHE, &i);
        hwp_cap_set(p, i, HWP_CAP_LINE_BYTES, h.line);
    }
    return zxvi_ok();
}

int32_t zxv_hw_profile_decode(const uint8_t *buf, size_t len, zxv_hw_profile **out)
{
    if (!buf || !out) return zxvi_fail(ZXV_E_ARG, "buf or out is NULL");
    *out = NULL;
    if (len > UINT32_MAX) return zxvi_fail(ZXV_E_FORMAT, "profile too large");
    zxv_hw_profile *p = calloc(1, sizeof *p);
    if (!p) return zxvi_fail(ZXV_E_NOMEM, "profile");
    int rc = hwp_decode(buf, (uint32_t) len, &p->p);
    if (rc != HWP_OK) {
        free(p);
        return hwp_status(rc, "profile_decode");
    }
    *out = p;
    return zxvi_ok();
}

int32_t zxv_hw_profile_encode(const zxv_hw_profile *p, uint8_t *buf, size_t cap, size_t *out_len)
{
    if (!p || !out_len) return zxvi_fail(ZXV_E_ARG, "p or out_len is NULL");
    uint32_t n = 0;
    int rc = hwp_encode(&p->p, buf, cap > UINT32_MAX ? UINT32_MAX : (uint32_t) cap, &n);
    *out_len = n;
    return hwp_status(rc, "profile_encode");
}

int32_t zxv_hw_profile_digest(const zxv_hw_profile *p, uint8_t out[32])
{
    if (!p || !out) return zxvi_fail(ZXV_E_ARG, "p or out is NULL");
    hwp_digest(&p->p, out);
    return zxvi_ok();
}

int32_t zxv_hw_profile_node_add(zxv_hw_profile *p, uint32_t id, uint32_t parent, uint32_t cls)
{
    if (!p) return zxvi_fail(ZXV_E_ARG, "p is NULL");
    if (id >= HWP_ROOT || parent > HWP_ROOT) return zxvi_fail(ZXV_E_RANGE, "node ids are 16-bit");
    int rc = hwp_node_add(&p->p, (uint16_t) id, (uint16_t) parent, cls, NULL);
    if (rc == HWP_E_CANONICAL)
        return zxvi_fail(ZXV_E_ARG, "node ids must increase and parents come first");
    return hwp_status(rc, "profile_node_add");
}

static int32_t node_index(const zxv_hw_profile *p, uint32_t node_id, uint16_t *idx)
{
    if (node_id >= HWP_ROOT || hwp_node_find(&p->p, (uint16_t) node_id, idx) != HWP_OK)
        return zxvi_fail(ZXV_E_NOT_FOUND, "no node %u", node_id);
    return ZXV_OK;
}

int32_t zxv_hw_profile_cap_set(zxv_hw_profile *p, uint32_t node_id, uint32_t key, uint64_t value)
{
    if (!p) return zxvi_fail(ZXV_E_ARG, "p is NULL");
    if (key > 0xFFFFu) return zxvi_fail(ZXV_E_RANGE, "cap keys are 16-bit");
    uint16_t idx;
    int32_t rc = node_index(p, node_id, &idx);
    if (rc) return rc;
    return hwp_status(hwp_cap_set(&p->p, idx, (uint16_t) key, value), "profile_cap_set");
}

int32_t zxv_hw_profile_cap_get(const zxv_hw_profile *p, uint32_t node_id, uint32_t key,
                               uint64_t *out_value)
{
    if (!p || !out_value) return zxvi_fail(ZXV_E_ARG, "p or out_value is NULL");
    uint16_t idx;
    int32_t rc = node_index(p, node_id, &idx);
    if (rc) return rc;
    if (key > 0xFFFFu || !hwp_cap_get(&p->p.node[idx], (uint16_t) key, out_value))
        return zxvi_fail(ZXV_E_NOT_FOUND, "node %u has no cap 0x%x", node_id, key);
    return zxvi_ok();
}

int32_t zxv_hw_profile_ext_set(zxv_hw_profile *p, uint32_t node_id, const uint8_t *ext, size_t len)
{
    if (!p || (len && !ext)) return zxvi_fail(ZXV_E_ARG, "p or ext is NULL");
    if (len > HWP_MAX_EXT) return zxvi_fail(ZXV_E_RANGE, "extension is at most %u bytes", HWP_MAX_EXT);
    uint16_t idx;
    int32_t rc = node_index(p, node_id, &idx);
    if (rc) return rc;
    return hwp_status(hwp_ext_set(&p->p, idx, ext, (uint16_t) len), "profile_ext_set");
}

int32_t zxv_hw_profile_ext_get(const zxv_hw_profile *p, uint32_t node_id, uint8_t *buf, size_t cap,
                               size_t *out_len)
{
    if (!p || !out_len) return zxvi_fail(ZXV_E_ARG, "p or out_len is NULL");
    uint16_t idx;
    int32_t rc = node_index(p, node_id, &idx);
    if (rc) return rc;
    const hwp_node_t *n = &p->p.node[idx];
    *out_len = n->ext_len;
    if (n->ext_len && (!buf || cap < n->ext_len))
        return zxvi_fail(ZXV_E_BUFFER, "extension needs %u bytes", n->ext_len);
    if (n->ext_len) memcpy(buf, n->ext, n->ext_len);
    return zxvi_ok();
}

int32_t zxv_hw_profile_count(const zxv_hw_profile *p, uint32_t *out_nodes)
{
    if (!p || !out_nodes) return zxvi_fail(ZXV_E_ARG, "p or out_nodes is NULL");
    *out_nodes = p->p.n;
    return zxvi_ok();
}

int32_t zxv_hw_profile_node(const zxv_hw_profile *p, uint32_t index, uint32_t *out_id,
                            uint32_t *out_parent, uint32_t *out_cls, uint32_t *out_ncaps)
{
    if (!p) return zxvi_fail(ZXV_E_ARG, "p is NULL");
    if (index >= p->p.n) return zxvi_fail(ZXV_E_NOT_FOUND, "no node at index %u", index);
    const hwp_node_t *n = &p->p.node[index];
    if (out_id) *out_id = n->id;
    if (out_parent) *out_parent = n->parent;
    if (out_cls) *out_cls = n->cls;
    if (out_ncaps) *out_ncaps = n->ncap;
    return zxvi_ok();
}

int32_t zxv_hw_profile_match(const zxv_hw_profile *host, const zxv_hw_profile *target,
                             uint32_t *out_native, uint32_t *out_emulated)
{
    if (!host || !target) return zxvi_fail(ZXV_E_ARG, "host or target is NULL");
    hwp_match_t *m = malloc(sizeof *m);
    if (!m) return zxvi_fail(ZXV_E_NOMEM, "match");
    hwp_match(&host->p, &target->p, m);
    if (out_native) *out_native = m->native;
    if (out_emulated) *out_emulated = m->emulated;
    free(m);
    return zxvi_ok();
}

int32_t zxv_hw_profile_pair(const zxv_hw_profile *host, const zxv_hw_profile *target,
                            uint32_t index, uint32_t *out_need_id, int32_t *out_kind,
                            uint32_t *out_have_id, uint32_t *out_unmet_key)
{
    if (!host || !target) return zxvi_fail(ZXV_E_ARG, "host or target is NULL");
    if (index >= target->p.n) return zxvi_fail(ZXV_E_NOT_FOUND, "no target node at index %u", index);
    hwp_match_t *m = malloc(sizeof *m);
    if (!m) return zxvi_fail(ZXV_E_NOMEM, "match");
    hwp_match(&host->p, &target->p, m);
    const hwp_pair_t *pr = &m->pair[index];
    if (out_need_id) *out_need_id = pr->need_id;
    if (out_kind) *out_kind = (int32_t) pr->kind;
    if (out_have_id) *out_have_id = pr->have_id;
    if (out_unmet_key) *out_unmet_key = pr->unmet_key;
    free(m);
    return zxvi_ok();
}

/* ===================== the matrix ===================== */

#define MX_LOG_CAP 4096u /* contract changes a matrix can record */

typedef struct {
    zxv_hw_call_fn call;
    void *user;
    struct zxv_hw_matrix *mx;
} prov_t;

struct zxv_hw_matrix {
    zxv_ctx *ctx;
    rmutex_t mu; /* recursive: providers may call their dependencies */
    hwm_t m;
    prov_t prov[HWM_MAX_PROVIDERS];
    uint8_t *vec_bytes[HWM_MAX_FUNCS][HWM_MAX_VECTORS]; /* copies we own */
    hwm_memo_entry_t *memo[HWM_MAX_FUNCS];
    uint32_t floor[HWM_MAX_FUNCS]; /* contract packages' svn floor */
    int32_t provider_rc;           /* last negative status a provider returned */
    uint8_t keys[HWR_MAX_KEYS][HWR_DUAL_PK];
    uint32_t nkeys;
    hwa_rec_t *logrec;
    hwa_log_t log;
};

static int trampoline(void *inst, hwm_t *m, uint32_t op, const uint8_t *in, uint32_t in_len,
                      uint8_t *out, uint32_t out_cap, uint32_t *out_len)
{
    (void) m;
    prov_t *p = inst;
    size_t got = 0;
    char saved[ZXVI_ERR_MAX];
    zxvi_err_save(saved);
    int32_t rc = p->call(p->user, op, in, in_len, out, out_cap, &got);
    zxvi_err_restore(saved);
    if (rc < 0) {
        /* A nested refusal from zxv_hw_call keeps its meaning (M5). */
        if (rc == ZXV_E_NOPROVIDER) return HWM_E_NOPROVIDER;
        p->mx->provider_rc = rc;
        return -1;
    }
    if (got > out_cap) return HWM_E_BUFFER;
    *out_len = (uint32_t) got;
    return 0;
}

static int32_t hwm_to_zxv(zxv_hw_matrix *mx, int rc, const char *what)
{
    switch (rc) {
    case HWM_OK: return zxvi_ok();
    case HWM_E_FULL: return zxvi_fail(ZXV_E_CAPACITY, "%s: matrix table full", what);
    case HWM_E_NOT_FOUND: return zxvi_fail(ZXV_E_NOT_FOUND, "%s: not found", what);
    case HWM_E_DUPLICATE: return zxvi_fail(ZXV_E_DUPLICATE, "%s: already present", what);
    case HWM_E_NOPROVIDER:
        return zxvi_fail(ZXV_E_NOPROVIDER, "%s: no proven implementation may serve it", what);
    case HWM_E_DEP:
        return zxvi_fail(ZXV_E_STATE, "%s: undeclared or circular dependency", what);
    case HWM_E_DEPTH: return zxvi_fail(ZXV_E_STATE, "%s: dependencies nest too deep", what);
    case HWM_E_DISPUTE: return zxvi_fail(ZXV_E_DISPUTE, "%s: implementations disagree", what);
    case HWM_E_BUFFER: return zxvi_fail(ZXV_E_BUFFER, "%s: output does not fit", what);
    case HWM_E_POLICY: return zxvi_fail(ZXV_E_POLICY, "%s: refused by the placement policy", what);
    case HWM_E_PROVIDER: {
        int32_t prc = mx->provider_rc;
        mx->provider_rc = 0;
        if (prc < 0) return zxvi_fail(prc, "%s: implementation returned %s", what, zxv_status_name(prc));
        return zxvi_fail(ZXV_E_INTERNAL, "%s: implementation failed", what);
    }
    default: return zxvi_fail(ZXV_E_ARG, "%s: invalid argument", what);
    }
}

static void free_vectors(zxv_hw_matrix *mx, uint32_t f)
{
    for (uint32_t k = 0; k < HWM_MAX_VECTORS; k++) {
        free(mx->vec_bytes[f][k]);
        mx->vec_bytes[f][k] = NULL;
    }
}

void zxv_hw_matrix_destroy(zxv_hw_matrix *mx)
{
    if (!mx) return;
    for (uint32_t f = 0; f < HWM_MAX_FUNCS; f++) {
        free_vectors(mx, f);
        if (mx->memo[f]) zxvi_wipe(mx->memo[f], mx->m.fn[f].memo_cap * sizeof *mx->memo[f]);
        free(mx->memo[f]);
    }
    free(mx->logrec);
    rmutex_destroy(&mx->mu);
    free(mx);
}

int32_t zxv_hw_matrix_create_cap(const zxv_cap *cap, zxv_hw_matrix **out)
{
    if (!out) return zxvi_fail(ZXV_E_ARG, "out is NULL");
    *out = NULL;
    zxv_ctx *ctx;
    int32_t rc = zxvi_cap_gate(cap, ZXV_CAP_HW_MATRIX, "hw_matrix_create", &ctx);
    if (rc) return rc;
    zxv_hw_matrix *mx = calloc(1, sizeof *mx);
    if (!mx) return zxvi_fail(ZXV_E_NOMEM, "matrix");
    mx->logrec = calloc(MX_LOG_CAP, sizeof *mx->logrec);
    if (!mx->logrec) {
        free(mx);
        return zxvi_fail(ZXV_E_NOMEM, "matrix log");
    }
    hwa_init(&mx->log, mx->logrec, MX_LOG_CAP);
    rmutex_init(&mx->mu);
    mx->ctx = ctx;
    hwm_init(&mx->m);
    /* Every matrix starts with the standard engines and their SOFT
     * implementations on the host CPU, so every engine call works. */
    uint32_t host;
    if (hwm_resource_add(&mx->m, ZXV_HW_RES_HOST, &host) != HWM_OK ||
        hwe_install(&mx->m, host, HWE_ALL, NULL) != HWM_OK) {
        zxv_hw_matrix_destroy(mx);
        return zxvi_fail(ZXV_E_INTERNAL, "standard engines failed their own golden vectors");
    }
    *out = mx;
    return zxvi_ok();
}

#define MX_ENTER_CAP(mx, what, bit)                                              \
    if (!(mx)) return zxvi_fail(ZXV_E_ARG, "matrix is NULL");                   \
    int32_t rc_ = zxvi_cap_gate_ctx(cap, bit, what, (mx)->ctx);                 \
    if (rc_) return rc_;                                                         \
    rmutex_lock(&(mx)->mu)
#define MX_ENTER(mx, what) MX_ENTER_CAP(mx, what, ZXV_CAP_HW_MATRIX)
#define MX_LEAVE(mx, value)                                                      \
    do {                                                                         \
        int32_t v_ = (value);                                                    \
        rmutex_unlock(&(mx)->mu);                                                \
        return v_;                                                               \
    } while (0)

static int32_t find_fn(zxv_hw_matrix *mx, uint32_t fn_id, uint32_t *idx)
{
    if (hwm_func_find(&mx->m, fn_id, idx) != HWM_OK)
        return zxvi_fail(ZXV_E_NOT_FOUND, "no function 0x%x", fn_id);
    return ZXV_OK;
}

static int32_t find_res(zxv_hw_matrix *mx, uint32_t res_id, uint32_t *idx)
{
    if (hwm_resource_find(&mx->m, res_id, idx) != HWM_OK)
        return zxvi_fail(ZXV_E_NOT_FOUND, "no resource %u", res_id);
    return ZXV_OK;
}

int32_t zxv_hw_function_add(const zxv_cap *cap, zxv_hw_matrix *mx, uint32_t fn_id, const char *name)
{
    MX_ENTER(mx, "hw_function_add");
    int32_t rc = name ? zxvi_check_str(name, HWM_NAME_LEN - 1, "name", true) : ZXV_OK;
    if (rc) MX_LEAVE(mx, rc);
    MX_LEAVE(mx, hwm_to_zxv(mx, hwm_func_add(&mx->m, fn_id, name, NULL), "hw_function_add"));
}

/* Copy one vector into storage the matrix owns and add it. */
static int32_t add_vector(zxv_hw_matrix *mx, uint32_t f, uint32_t op, const uint8_t *in,
                          size_t in_len, const uint8_t *expect, size_t expect_len)
{
    uint32_t k = mx->m.fn[f].nvec;
    if (k >= HWM_MAX_VECTORS)
        return zxvi_fail(ZXV_E_CAPACITY, "function 0x%x has %u vectors", mx->m.fn[f].id, HWM_MAX_VECTORS);
    uint8_t *b = malloc(in_len + expect_len + 1);
    if (!b) return zxvi_fail(ZXV_E_NOMEM, "vector");
    if (in_len) memcpy(b, in, in_len);
    if (expect_len) memcpy(b + in_len, expect, expect_len);
    int rc = hwm_vector_add(&mx->m, f, op, b, (uint32_t) in_len, b + in_len, (uint32_t) expect_len);
    if (rc != HWM_OK) {
        free(b);
        return hwm_to_zxv(mx, rc, "hw_vector_add");
    }
    free(mx->vec_bytes[f][k]);
    mx->vec_bytes[f][k] = b;
    return zxvi_ok();
}

int32_t zxv_hw_vector_add(const zxv_cap *cap, zxv_hw_matrix *mx, uint32_t fn_id, uint32_t op,
                          const uint8_t *in, size_t in_len, const uint8_t *expect,
                          size_t expect_len)
{
    MX_ENTER(mx, "hw_vector_add");
    uint32_t f;
    int32_t rc = find_fn(mx, fn_id, &f);
    if (rc) MX_LEAVE(mx, rc);
    if ((in_len && !in) || (expect_len && !expect) || in_len > 65536 || expect_len > 512)
        MX_LEAVE(mx, zxvi_fail(ZXV_E_ARG, "vector: input <= 65536 bytes, output <= 512 bytes"));
    MX_LEAVE(mx, add_vector(mx, f, op, in, in_len, expect, expect_len));
}

static int32_t begin_contract(zxv_hw_matrix *mx, uint32_t f, uint32_t version, const char *what)
{
    if (mx->m.depth != 0) return zxvi_fail(ZXV_E_STATE, "%s from inside an implementation", what);
    if (version <= mx->m.fn[f].contract)
        return zxvi_fail(ZXV_E_ROLLBACK, "%s: contract %u is not newer than %u", what, version,
                         mx->m.fn[f].contract);
    int rc = hwm_contract_begin(&mx->m, f, version);
    if (rc != HWM_OK) return hwm_to_zxv(mx, rc, what);
    free_vectors(mx, f);
    return zxvi_ok();
}

int32_t zxv_hw_function_contract(const zxv_cap *cap, zxv_hw_matrix *mx, uint32_t fn_id,
                                 uint32_t version)
{
    MX_ENTER(mx, "hw_function_contract");
    uint32_t f;
    int32_t rc = find_fn(mx, fn_id, &f);
    if (rc) MX_LEAVE(mx, rc);
    MX_LEAVE(mx, begin_contract(mx, f, version, "hw_function_contract"));
}

int32_t zxv_hw_policy_set(const zxv_cap *cap, zxv_hw_matrix *mx, uint32_t fn_id, uint32_t flags,
                          uint32_t max_latency_ns, uint32_t power_budget_mw)
{
    MX_ENTER(mx, "hw_policy_set");
    uint32_t f;
    int32_t rc = find_fn(mx, fn_id, &f);
    if (rc) MX_LEAVE(mx, rc);
    if (flags & ~(uint32_t) HWM_F_MASK)
        MX_LEAVE(mx, zxvi_fail(ZXV_E_ARG, "flags 0x%x: only ZXV_HW_POLICY_* bits", flags));
    if (mx->m.depth != 0) MX_LEAVE(mx, zxvi_fail(ZXV_E_STATE, "policy change from inside an implementation"));
    bool had_memo = mx->m.fn[f].memo != NULL;
    rc = hwm_to_zxv(mx, hwm_policy_set(&mx->m, f, flags, max_latency_ns, power_budget_mw), "hw_policy_set");
    if (!rc && had_memo && !mx->m.fn[f].memo) {
        free(mx->memo[f]); /* wiped by the kernel when the function became key-bearing */
        mx->memo[f] = NULL;
    }
    MX_LEAVE(mx, rc);
}

int32_t zxv_hw_function_info(const zxv_cap *cap, zxv_hw_matrix *mx, uint32_t fn_id,
                             uint32_t *out_contract, uint32_t *out_policy, uint32_t *out_vectors)
{
    MX_ENTER(mx, "hw_function_info");
    uint32_t f;
    int32_t rc = find_fn(mx, fn_id, &f);
    if (rc) MX_LEAVE(mx, rc);
    const hwm_func_t *fn = &mx->m.fn[f];
    if (out_contract) *out_contract = fn->contract;
    if (out_policy) *out_policy = fn->flags;
    if (out_vectors) *out_vectors = fn->nvec;
    MX_LEAVE(mx, zxvi_ok());
}

int32_t zxv_hw_resource_add(const zxv_cap *cap, zxv_hw_matrix *mx, uint32_t res_id)
{
    MX_ENTER(mx, "hw_resource_add");
    MX_LEAVE(mx, hwm_to_zxv(mx, hwm_resource_add(&mx->m, res_id, NULL), "hw_resource_add"));
}

int32_t zxv_hw_resource_set(const zxv_cap *cap, zxv_hw_matrix *mx, uint32_t res_id, int32_t state)
{
    MX_ENTER(mx, "hw_resource_set");
    uint32_t r;
    int32_t rc = find_res(mx, res_id, &r);
    if (rc) MX_LEAVE(mx, rc);
    if (state < ZXV_HW_RES_OK || state > ZXV_HW_RES_OFFLINE)
        MX_LEAVE(mx, zxvi_fail(ZXV_E_ARG, "state %d is not a ZXV_HW_RES_* value", state));
    MX_LEAVE(mx, hwm_to_zxv(mx, hwm_resource_set(&mx->m, r, (hwm_res_state_t) state), "hw_resource_set"));
}

int32_t zxv_hw_provider_add(const zxv_cap *cap, zxv_hw_matrix *mx, uint32_t fn_id, uint32_t res_id,
                            int32_t kind, uint32_t cost, zxv_hw_call_fn call, void *user,
                            uint32_t *out_provider)
{
    MX_ENTER(mx, "hw_provider_add");
    if (!call || !out_provider) MX_LEAVE(mx, zxvi_fail(ZXV_E_ARG, "call or out_provider is NULL"));
    if (kind < ZXV_HW_KIND_HARD || kind > ZXV_HW_KIND_REMOTE)
        MX_LEAVE(mx, zxvi_fail(ZXV_E_ARG, "kind %d is not a ZXV_HW_KIND_* value", kind));
    uint32_t f, r, pv;
    int32_t rc = find_fn(mx, fn_id, &f);
    if (!rc) rc = find_res(mx, res_id, &r);
    if (rc) MX_LEAVE(mx, rc);
    if (mx->m.npv >= HWM_MAX_PROVIDERS)
        MX_LEAVE(mx, zxvi_fail(ZXV_E_CAPACITY, "matrix has %u implementations", HWM_MAX_PROVIDERS));
    prov_t *p = &mx->prov[mx->m.npv];
    p->call = call;
    p->user = user;
    p->mx = mx;
    rc = hwm_to_zxv(mx, hwm_provider_add(&mx->m, f, r, (hwm_kind_t) kind, cost, trampoline, p, &pv),
                    "hw_provider_add");
    if (!rc) *out_provider = pv;
    MX_LEAVE(mx, rc);
}

static int32_t check_pv(zxv_hw_matrix *mx, uint32_t provider)
{
    if (provider >= mx->m.npv) return zxvi_fail(ZXV_E_NOT_FOUND, "no implementation %u", provider);
    return ZXV_OK;
}

int32_t zxv_hw_provider_traits(const zxv_cap *cap, zxv_hw_matrix *mx, uint32_t provider,
                               uint32_t latency_ns, uint32_t power_mw, uint32_t throughput_class)
{
    MX_ENTER(mx, "hw_provider_traits");
    int32_t rc = check_pv(mx, provider);
    if (rc) MX_LEAVE(mx, rc);
    if (throughput_class > 15) MX_LEAVE(mx, zxvi_fail(ZXV_E_RANGE, "throughput class is 0..15"));
    MX_LEAVE(mx, hwm_to_zxv(mx, hwm_provider_traits(&mx->m, provider, latency_ns, power_mw, throughput_class),
                            "hw_provider_traits"));
}

int32_t zxv_hw_provider_dep(const zxv_cap *cap, zxv_hw_matrix *mx, uint32_t provider,
                            uint32_t dep_fn_id)
{
    MX_ENTER(mx, "hw_provider_dep");
    uint32_t f;
    int32_t rc = check_pv(mx, provider);
    if (!rc) rc = find_fn(mx, dep_fn_id, &f);
    if (rc) MX_LEAVE(mx, rc);
    MX_LEAVE(mx, hwm_to_zxv(mx, hwm_provider_dep(&mx->m, provider, f), "hw_provider_dep"));
}

int32_t zxv_hw_provider_enable(const zxv_cap *cap, zxv_hw_matrix *mx, uint32_t provider, int32_t on)
{
    MX_ENTER(mx, "hw_provider_enable");
    int32_t rc = check_pv(mx, provider);
    if (rc) MX_LEAVE(mx, rc);
    MX_LEAVE(mx, hwm_to_zxv(mx, hwm_provider_enable(&mx->m, provider, on != 0), "hw_provider_enable"));
}

int32_t zxv_hw_attest(const zxv_cap *cap, zxv_hw_matrix *mx, uint32_t provider, int32_t *out_lp)
{
    MX_ENTER(mx, "hw_attest");
    int32_t rc = check_pv(mx, provider);
    if (rc) MX_LEAVE(mx, rc);
    if (mx->m.depth != 0) MX_LEAVE(mx, zxvi_fail(ZXV_E_STATE, "attest from inside an implementation"));
    lpres_state_t st = hwm_attest(&mx->m, provider, NULL);
    mx->provider_rc = 0;
    if (out_lp) *out_lp = (int32_t) st;
    MX_LEAVE(mx, zxvi_ok());
}

int32_t zxv_hw_provider_count(const zxv_cap *cap, zxv_hw_matrix *mx, uint32_t *out_count)
{
    MX_ENTER(mx, "hw_provider_count");
    if (!out_count) MX_LEAVE(mx, zxvi_fail(ZXV_E_ARG, "out_count is NULL"));
    *out_count = mx->m.npv;
    MX_LEAVE(mx, zxvi_ok());
}

int32_t zxv_hw_provider_info(const zxv_cap *cap, zxv_hw_matrix *mx, uint32_t provider,
                             uint32_t *out_fn_id, int32_t *out_kind, int32_t *out_lp,
                             uint32_t *out_contract, uint32_t *out_flags,
                             uint32_t *out_throughput_class, uint32_t *out_power_class,
                             uint64_t *out_calls, uint64_t *out_cost_spent)
{
    MX_ENTER(mx, "hw_provider_info");
    int32_t rc = check_pv(mx, provider);
    if (rc) MX_LEAVE(mx, rc);
    const hwm_provider_t *p = &mx->m.pv[provider];
    if (out_fn_id) *out_fn_id = mx->m.fn[p->fn].id;
    if (out_kind) *out_kind = (int32_t) p->kind;
    if (out_lp) *out_lp = (int32_t) p->attest;
    if (out_contract) *out_contract = p->attest == LPRES_STATE_TRUE ? p->proven_contract : 0;
    if (out_flags) *out_flags = hwm_provider_flags(&mx->m, provider);
    if (out_throughput_class) *out_throughput_class = p->throughput_class;
    if (out_power_class) *out_power_class = hwm_power_class(p->power_mw);
    if (out_calls) *out_calls = p->calls;
    if (out_cost_spent) *out_cost_spent = p->cost_spent;
    MX_LEAVE(mx, zxvi_ok());
}

int32_t zxv_hw_bound(const zxv_cap *cap, zxv_hw_matrix *mx, uint32_t fn_id, uint32_t *out_provider)
{
    MX_ENTER(mx, "hw_bound");
    uint32_t f, pv = HWM_NONE;
    int32_t rc = find_fn(mx, fn_id, &f);
    if (rc) MX_LEAVE(mx, rc);
    rc = hwm_to_zxv(mx, hwm_bind(&mx->m, f, &pv), "hw_bound");
    if (out_provider) *out_provider = pv;
    MX_LEAVE(mx, rc);
}

int32_t zxv_hw_call(const zxv_cap *cap, zxv_hw_matrix *mx, uint32_t fn_id, uint32_t op,
                    const uint8_t *in, size_t in_len, uint8_t *out, size_t out_cap, size_t *out_len)
{
    MX_ENTER(mx, "hw_call");
    uint32_t f, got = 0;
    int32_t rc = find_fn(mx, fn_id, &f);
    if (rc) MX_LEAVE(mx, rc);
    if ((in_len && !in) || (out_cap && !out) || in_len > UINT32_MAX)
        MX_LEAVE(mx, zxvi_fail(ZXV_E_ARG, "in/out buffers"));
    uint32_t oc = out_cap > UINT32_MAX ? UINT32_MAX : (uint32_t) out_cap;
    rc = hwm_to_zxv(mx, hwm_call(&mx->m, f, op, in, (uint32_t) in_len, out, oc, &got), "hw_call");
    if (out_len) *out_len = got;
    MX_LEAVE(mx, rc);
}

int32_t zxv_hw_shadow(const zxv_cap *cap, zxv_hw_matrix *mx, uint32_t fn_id, uint32_t op,
                      const uint8_t *in, size_t in_len, uint8_t *out, size_t out_cap,
                      size_t *out_len, int32_t *out_checked)
{
    MX_ENTER(mx, "hw_shadow");
    uint32_t f, got = 0;
    int32_t rc = find_fn(mx, fn_id, &f);
    if (rc) MX_LEAVE(mx, rc);
    if ((in_len && !in) || (out_cap && !out) || in_len > UINT32_MAX || out_cap > (1u << 24))
        MX_LEAVE(mx, zxvi_fail(ZXV_E_ARG, "in/out buffers"));
    if (mx->m.depth != 0) MX_LEAVE(mx, zxvi_fail(ZXV_E_STATE, "shadow from inside an implementation"));
    uint8_t *scratch = out_cap ? malloc(out_cap) : NULL;
    if (out_cap && !scratch) MX_LEAVE(mx, zxvi_fail(ZXV_E_NOMEM, "shadow scratch"));
    bool checked = false;
    rc = hwm_to_zxv(mx,
                    hwm_shadow(&mx->m, f, op, in, (uint32_t) in_len, out, scratch, (uint32_t) out_cap,
                               &got, &checked),
                    "hw_shadow");
    if (scratch) zxvi_wipe(scratch, out_cap); /* may hold key-bearing output */
    free(scratch);
    if (out_len) *out_len = got;
    if (out_checked) *out_checked = checked;
    MX_LEAVE(mx, rc);
}

int32_t zxv_hw_memo(const zxv_cap *cap, zxv_hw_matrix *mx, uint32_t fn_id, uint32_t entries,
                    uint64_t *out_hits, uint64_t *out_misses)
{
    MX_ENTER(mx, "hw_memo");
    uint32_t f;
    int32_t rc = find_fn(mx, fn_id, &f);
    if (rc) MX_LEAVE(mx, rc);
    hwm_func_t *fn = &mx->m.fn[f];
    if (out_hits) *out_hits = fn->memo_hits;
    if (out_misses) *out_misses = fn->memo_misses;
    if (entries == fn->memo_cap) MX_LEAVE(mx, zxvi_ok()); /* a query */
    if (entries > 65536 || (entries & (entries - 1u)) != 0)
        MX_LEAVE(mx, zxvi_fail(ZXV_E_ARG, "entries must be 0 or a power of two <= 65536"));
    if (entries && (fn->flags & HWM_F_KEY_BEARING))
        MX_LEAVE(mx, zxvi_fail(ZXV_E_POLICY, "a key-bearing function keeps no memo"));
    hwm_memo_entry_t *e = NULL;
    if (entries && !(e = calloc(entries, sizeof *e))) MX_LEAVE(mx, zxvi_fail(ZXV_E_NOMEM, "memo"));
    rc = hwm_to_zxv(mx, hwm_memo_attach(&mx->m, f, e, entries), "hw_memo");
    if (rc) {
        free(e);
        MX_LEAVE(mx, rc);
    }
    free(mx->memo[f]);
    mx->memo[f] = e;
    MX_LEAVE(mx, zxvi_ok());
}

/* ---- audit logs ---- */

static int32_t log_verify(zxv_ctx *ctx, const hwa_log_t *log, const char *what)
{
    if (hwa_verify(log)) return ZXV_OK;
    int32_t rc = ZXV_E_INVARIANT;
    if (zxvi_ctx_enter(ctx) == ZXV_OK) {
        rc = zxvi_ctx_poison(ctx, "hardware audit log chain broken");
        zxvi_ctx_leave(ctx);
    }
    return zxvi_fail(rc, "%s: audit log chain does not verify", what);
}

static int32_t log_entry(const hwa_log_t *log, uint32_t index, int32_t *out_event,
                         uint32_t *out_reason, uint32_t *out_id, uint32_t *out_version,
                         uint32_t *out_tag, uint8_t subject[32], uint8_t hash[32])
{
    const hwa_rec_t *r = hwa_get(log, index);
    if (!r) return zxvi_fail(ZXV_E_NOT_FOUND, "no audit record %u (log holds %u)", index, log->n);
    if (out_event) *out_event = r->event;
    if (out_reason) *out_reason = r->reason;
    if (out_id) *out_id = r->region;
    if (out_version) *out_version = r->version;
    if (out_tag) *out_tag = r->tag;
    if (subject) memcpy(subject, r->subject, 32);
    if (hash) memcpy(hash, r->hash, 32);
    return zxvi_ok();
}

int32_t zxv_hw_matrix_log(const zxv_cap *cap, zxv_hw_matrix *mx, uint8_t head[32], uint32_t *out_count)
{
    MX_ENTER(mx, "hw_matrix_log");
    int32_t rc = log_verify(mx->ctx, &mx->log, "hw_matrix_log");
    if (rc) MX_LEAVE(mx, rc);
    if (head) memcpy(head, mx->log.head, 32);
    if (out_count) *out_count = mx->log.n;
    MX_LEAVE(mx, zxvi_ok());
}

int32_t zxv_hw_matrix_log_entry(const zxv_cap *cap, zxv_hw_matrix *mx, uint32_t index,
                                int32_t *out_event, uint32_t *out_reason, uint32_t *out_id,
                                uint32_t *out_version, uint32_t *out_tag, uint8_t subject[32],
                                uint8_t hash[32])
{
    MX_ENTER(mx, "hw_matrix_log_entry");
    MX_LEAVE(mx, log_entry(&mx->log, index, out_event, out_reason, out_id, out_version, out_tag,
                           subject, hash));
}

/* ===================== signatures ===================== */

#ifndef ZXV_NO_PQC
/* The vendored ML-DSA / SLH-DSA code is not re-entrant-safe here. */
static pthread_mutex_t g_hwpq = PTHREAD_MUTEX_INITIALIZER;
static bool pq_verify(void *ctx, uint16_t alg, const uint8_t *pk, uint32_t pk_len,
                      const uint8_t *msg, uint32_t msg_len, const uint8_t *sig, uint32_t sig_len)
{
    pthread_mutex_lock(&g_hwpq);
    bool ok = hwr_pq_verify(ctx, alg, pk, pk_len, msg, msg_len, sig, sig_len);
    pthread_mutex_unlock(&g_hwpq);
    return ok;
}
#endif

int32_t zxv_hw_keygen(const zxv_cap *cap, uint8_t pk[2656], uint8_t mldsa_sk[4896], uint8_t slhdsa_sk[128])
{
    int32_t rc = zxvi_cap_gate(cap, ZXV_CAP_CRYPTO_SIGN, "hw_keygen", NULL);
    if (rc) return rc;
    if (!pk || !mldsa_sk || !slhdsa_sk) return zxvi_fail(ZXV_E_ARG, "key buffers are NULL");
#ifdef ZXV_NO_PQC
    return zxvi_fail(ZXV_E_NOTIMPL, "built without ML-DSA / SLH-DSA (ZXV_NO_PQC)");
#else
    uint8_t seed[32 + 3 * PQM_SLH256S_N];
    rc = zxv_random(seed, sizeof seed);
    if (rc) return rc;
    pthread_mutex_lock(&g_hwpq);
    pqm_mldsa87_keygen(seed, pk, mldsa_sk);
    pqm_slh256s_keygen(seed + 32, pk + HWR_MLDSA87_PK, slhdsa_sk);
    pthread_mutex_unlock(&g_hwpq);
    zxvi_wipe(seed, sizeof seed);
    return zxvi_ok();
#endif
}

int32_t zxv_hw_image_build(const zxv_cap *cap, uint32_t kind, uint32_t target_class,
                           const uint8_t *target_digest, uint32_t fn_id, uint32_t version,
                           uint32_t svn, const uint8_t *payload, size_t payload_len,
                           const uint8_t mldsa_sk[4896], const uint8_t slhdsa_sk[128], uint8_t *buf,
                           size_t cap_bytes, size_t *out_len)
{
    int32_t rc = zxvi_cap_gate(cap, ZXV_CAP_CRYPTO_SIGN, "hw_image_build", NULL);
    if (rc) return rc;
    if (!payload || !mldsa_sk || !slhdsa_sk || !out_len)
        return zxvi_fail(ZXV_E_ARG, "payload, keys or out_len is NULL");
    if (kind < ZXV_HW_IMG_BITSTREAM || kind > ZXV_HW_IMG_VECTORS)
        return zxvi_fail(ZXV_E_ARG, "kind %u is not a ZXV_HW_IMG_* value", kind);
    if (payload_len == 0 || payload_len > HWR_MAX_PAYLOAD)
        return zxvi_fail(ZXV_E_RANGE, "payload must be 1..%u bytes", HWR_MAX_PAYLOAD);
    size_t total = HWR_HEADER + payload_len + HWR_DUAL_SIG;
    *out_len = total;
    if (!buf || cap_bytes < total) return zxvi_fail(ZXV_E_BUFFER, "package needs %zu bytes", total);
#ifdef ZXV_NO_PQC
    (void) target_class; (void) target_digest; (void) fn_id; (void) version; (void) svn;
    return zxvi_fail(ZXV_E_NOTIMPL, "built without ML-DSA / SLH-DSA (ZXV_NO_PQC)");
#else
    if (kind == ZXV_HW_IMG_VECTORS) {
        /* The payload must parse as a vector list before anything is signed. */
        uint32_t off = 0, op, il, ol, n = 0;
        const uint8_t *vi, *vo;
        int prc;
        while ((prc = hwr_vectors_next(payload, (uint32_t) payload_len, &off, &op, &vi, &il, &vo,
                                       &ol)) == HWR_OK)
            n++;
        if (prc != 1 || n == 0 || n > HWM_MAX_VECTORS)
            return zxvi_fail(ZXV_E_FORMAT, "a VECTORS payload is 1..%u well-formed vectors", HWM_MAX_VECTORS);
    }
    hwr_image_t im;
    memset(&im, 0, sizeof im);
    im.alg = HWR_ALG_DUAL;
    im.kind = kind;
    im.target_class = target_class;
    im.fn_id = fn_id;
    im.version = version;
    im.svn = svn;
    im.payload_len = (uint32_t) payload_len;
    if (target_digest) memcpy(im.target_digest, target_digest, 32);
    sha256(payload, payload_len, im.payload_sha256);
    hwr_build_header(&im, buf);
    memcpy(buf + HWR_HEADER, payload, payload_len);
    uint8_t rnd[32];
    rc = zxv_random(rnd, sizeof rnd);
    if (rc) return rc;
    pthread_mutex_lock(&g_hwpq);
    hwr_dual_sign(mldsa_sk, slhdsa_sk, buf, rnd, buf + HWR_HEADER + payload_len);
    pthread_mutex_unlock(&g_hwpq);
    zxvi_wipe(rnd, sizeof rnd);
    return zxvi_ok();
#endif
}

/* ---- contract packages ---- */

#ifndef ZXV_NO_PQC
static int32_t mx_log_full(zxv_hw_matrix *mx)
{
    if (zxvi_ctx_enter(mx->ctx) == ZXV_OK) {
        zxvi_ctx_poison(mx->ctx, "hardware audit log full");
        zxvi_ctx_leave(mx->ctx);
    }
    return zxvi_fail(ZXV_E_POISONED, "hw_contract_apply: audit log full; context poisoned");
}
#endif

int32_t zxv_hw_matrix_trust_key(const zxv_cap *cap, zxv_hw_matrix *mx, const uint8_t pk[2656])
{
    MX_ENTER_CAP(mx, "hw_matrix_trust_key", ZXV_CAP_HW_RECONF);
    if (!pk) MX_LEAVE(mx, zxvi_fail(ZXV_E_ARG, "pk is NULL"));
    if (mx->nkeys >= HWR_MAX_KEYS)
        MX_LEAVE(mx, zxvi_fail(ZXV_E_CAPACITY, "matrix trusts %u keys already", HWR_MAX_KEYS));
    memcpy(mx->keys[mx->nkeys++], pk, HWR_DUAL_PK);
    MX_LEAVE(mx, zxvi_ok());
}

int32_t zxv_hw_contract_apply(const zxv_cap *cap, zxv_hw_matrix *mx, const uint8_t *pkg, size_t len,
                              uint32_t *out_proven)
{
    MX_ENTER_CAP(mx, "hw_contract_apply", ZXV_CAP_HW_RECONF);
    if (out_proven) *out_proven = 0;
    if (!pkg) MX_LEAVE(mx, zxvi_fail(ZXV_E_ARG, "pkg is NULL"));
    if (len > UINT32_MAX) MX_LEAVE(mx, zxvi_fail(ZXV_E_FORMAT, "package too large"));
    if (mx->m.depth != 0) MX_LEAVE(mx, zxvi_fail(ZXV_E_STATE, "contract change from inside an implementation"));
#ifdef ZXV_NO_PQC
    MX_LEAVE(mx, zxvi_fail(ZXV_E_NOTIMPL, "built without ML-DSA / SLH-DSA (ZXV_NO_PQC)"));
#else
    const uint8_t *keys[HWR_MAX_KEYS];
    for (uint32_t k = 0; k < mx->nkeys; k++) keys[k] = mx->keys[k];
    hwr_image_t im;
    int rc = hwr_package_verify(pkg, (uint32_t) len, HWR_ALG_DUAL, pq_verify, NULL, keys, mx->nkeys, &im);
    uint8_t subject[32];
    if (rc == HWR_E_VERIFY) {
        sha256(pkg, HWR_HEADER, subject);
        if (!hwa_append(&mx->log, lpx_make(LPX_B_FALSE, LPX_P_FORBIDDEN, LPX_PH_EMERGING),
                        HWA_EV_REJECTED, 1, im.fn_id, im.version, subject))
            MX_LEAVE(mx, mx_log_full(mx));
        MX_LEAVE(mx, zxvi_fail(ZXV_E_VERIFY, "hw_contract_apply: no trusted key signed it (logged)"));
    }
    if (rc == HWR_E_KIND) MX_LEAVE(mx, zxvi_fail(ZXV_E_FORMAT, "hw_contract_apply: not a VECTORS package"));
    if (rc != HWR_OK) MX_LEAVE(mx, zxvi_fail(ZXV_E_FORMAT, "hw_contract_apply: not a valid package"));
    uint32_t f;
    int32_t zrc = find_fn(mx, im.fn_id, &f);
    if (zrc) MX_LEAVE(mx, zrc);
    sha256(im.header, HWR_HEADER, subject);
    if (im.version <= mx->m.fn[f].contract || im.svn < mx->floor[f]) {
        if (!hwa_append(&mx->log, lpx_make(LPX_B_FALSE, LPX_P_FORBIDDEN, LPX_PH_EMERGING),
                        HWA_EV_REJECTED, 2, im.fn_id, im.version, subject))
            MX_LEAVE(mx, mx_log_full(mx));
        MX_LEAVE(mx, zxvi_fail(ZXV_E_ROLLBACK, "hw_contract_apply: contract %u is not newer than %u "
                                               "or below the floor (logged)",
                               im.version, mx->m.fn[f].contract));
    }
    /* Parse everything before changing anything. */
    uint32_t off = 0, op[HWM_MAX_VECTORS], il[HWM_MAX_VECTORS], ol[HWM_MAX_VECTORS], n = 0;
    const uint8_t *vi[HWM_MAX_VECTORS], *vo[HWM_MAX_VECTORS];
    for (;;) {
        uint32_t o, a, b;
        const uint8_t *x, *y;
        int prc = hwr_vectors_next(im.payload, im.payload_len, &off, &o, &x, &a, &y, &b);
        if (prc == 1) break;
        if (prc != HWR_OK || n >= HWM_MAX_VECTORS)
            MX_LEAVE(mx, zxvi_fail(ZXV_E_FORMAT, "hw_contract_apply: malformed vector list"));
        op[n] = o;
        vi[n] = x;
        il[n] = a;
        vo[n] = y;
        ol[n] = b;
        n++;
    }
    if (n == 0) MX_LEAVE(mx, zxvi_fail(ZXV_E_FORMAT, "hw_contract_apply: no vectors"));
    /* Record first: a change that cannot be recorded is not made. */
    if (!hwa_append(&mx->log, lpx_make(LPX_B_TRUE, LPX_P_NEGENTROPIC, LPX_PH_MANIFEST), HWA_EV_CONTRACT, 0,
                    im.fn_id, im.version, subject))
        MX_LEAVE(mx, mx_log_full(mx));
    zrc = begin_contract(mx, f, im.version, "hw_contract_apply");
    for (uint32_t k = 0; !zrc && k < n; k++) zrc = add_vector(mx, f, op[k], vi[k], il[k], vo[k], ol[k]);
    if (zrc) MX_LEAVE(mx, zrc);
    if (im.svn > mx->floor[f]) mx->floor[f] = im.svn;
    uint32_t proven = 0;
    for (uint32_t i = 0; i < mx->m.npv; i++)
        if (mx->m.pv[i].fn == f && hwm_attest(&mx->m, i, NULL) == LPRES_STATE_TRUE) proven++;
    mx->provider_rc = 0;
    if (out_proven) *out_proven = proven;
    MX_LEAVE(mx, zxvi_ok());
#endif
}

/* ===================== regions ===================== */

typedef struct {
    int32_t ev;
    uint32_t reason, version;
} pend_t;

#define RG_PEND 8u

struct zxv_hw_region {
    zxv_ctx *ctx;
    pthread_mutex_t mu;
    zxv_hw_matrix *mx;
    hwr_region_t r;
    hwr_ops_t ops;
    zxv_hw_load_fn load;
    zxv_hw_readback_fn readback;
    zxv_hw_activate_fn activate;
    void *user;
    hwa_rec_t *logrec;
    hwa_log_t log;
    uint8_t keys[HWR_MAX_KEYS][HWR_DUAL_PK];
    zxv_hw_event_fn on_event;
    void *event_user;
    pend_t pend[RG_PEND];
    uint32_t npend;
};

#ifndef ZXV_NO_PQC
static int dev_load(void *d, uint32_t s, const uint8_t *p, uint32_t n)
{
    zxv_hw_region *rg = d;
    char saved[ZXVI_ERR_MAX];
    zxvi_err_save(saved);
    int32_t rc = rg->load(rg->user, s, p, n);
    zxvi_err_restore(saved);
    return rc < 0 ? -1 : 0;
}
static int dev_readback(void *d, uint32_t s, uint8_t dg[32])
{
    zxv_hw_region *rg = d;
    char saved[ZXVI_ERR_MAX];
    zxvi_err_save(saved);
    int32_t rc = rg->readback(rg->user, s, dg);
    zxvi_err_restore(saved);
    return rc < 0 ? -1 : 0;
}
static int dev_activate(void *d, uint32_t s)
{
    zxv_hw_region *rg = d;
    char saved[ZXVI_ERR_MAX];
    zxvi_err_save(saved);
    int32_t rc = rg->activate(rg->user, s);
    zxvi_err_restore(saved);
    return rc < 0 ? -1 : 0;
}
/* Queued under the lock, delivered after it (rg_leave). */
static void region_event(void *ctx, uint32_t region, uint32_t ev, uint32_t reason, uint32_t version)
{
    (void) region;
    zxv_hw_region *rg = ctx;
    if (rg->npend < RG_PEND) rg->pend[rg->npend++] = (pend_t){(int32_t) ev, reason, version};
}
#endif

static int32_t hwr_to_zxv(zxv_hw_region *rg, int rc, const char *what)
{
    switch (rc) {
    case HWR_OK: return zxvi_ok();
    case HWR_E_FORMAT: return zxvi_fail(ZXV_E_FORMAT, "%s: not a valid dual-signed ZXHR image", what);
    case HWR_E_KIND: return zxvi_fail(ZXV_E_FORMAT, "%s: a VECTORS package goes to a matrix", what);
    case HWR_E_TARGET: return zxvi_fail(ZXV_E_TARGET, "%s: image is for other hardware", what);
    case HWR_E_VERIFY: return zxvi_fail(ZXV_E_VERIFY, "%s: no trusted key signed it (logged)", what);
    case HWR_E_ROLLBACK:
        return zxvi_fail(ZXV_E_ROLLBACK, "%s: not newer than the active image or below the floor (logged)", what);
    case HWR_E_BLOCKED: return zxvi_fail(ZXV_E_BLOCKED, "%s: image is quarantined", what);
    case HWR_E_READBACK:
        return zxvi_fail(ZXV_E_QUARANTINED, "%s: read-back mismatch; quarantined", what);
    case HWR_E_ACTIVATE:
        return zxvi_fail(ZXV_E_QUARANTINED, "%s: device refused to switch; rolled back, quarantined", what);
    case HWR_E_CONFORMANCE:
        return zxvi_fail(ZXV_E_QUARANTINED, "%s: image did not prove itself; rolled back, quarantined", what);
    case HWR_E_STATE: return zxvi_fail(ZXV_E_STATE, "%s: wrong step for region state %s", what,
                                       hwr_state_name(rg->r.state));
    case HWR_E_FULL: return zxvi_fail(ZXV_E_CAPACITY, "%s: table full", what);
    case HWR_E_FUNCTION: return zxvi_fail(ZXV_E_NOT_FOUND, "%s: image's function is not in the matrix", what);
    case HWR_E_DEVICE: return zxvi_fail(ZXV_E_IO, "%s: device load failed; nothing changed", what);
    case HWR_E_HALTED: {
        if (zxvi_ctx_enter(rg->ctx) == ZXV_OK) {
            zxvi_ctx_poison(rg->ctx, "hardware audit log full");
            zxvi_ctx_leave(rg->ctx);
        }
        return zxvi_fail(ZXV_E_POISONED, "%s: audit log full; region halted, context poisoned", what);
    }
    default: return zxvi_fail(ZXV_E_ARG, "%s: invalid argument", what);
    }
}

int32_t zxv_hw_region_create_cap(const zxv_cap *cap, zxv_hw_matrix *mx, uint32_t provider,
                                 uint32_t region_id, uint32_t target_class,
                                 const uint8_t *target_digest, zxv_hw_load_fn load,
                                 zxv_hw_readback_fn readback, zxv_hw_activate_fn activate,
                                 void *user, uint32_t log_cap, zxv_hw_region **out)
{
    if (!out) return zxvi_fail(ZXV_E_ARG, "out is NULL");
    *out = NULL;
    zxv_ctx *ctx;
    int32_t rc = zxvi_cap_gate(cap, ZXV_CAP_HW_RECONF, "hw_region_create", &ctx);
    if (rc) return rc;
#ifdef ZXV_NO_PQC
    (void) mx; (void) provider; (void) region_id; (void) target_class; (void) target_digest;
    (void) load; (void) readback; (void) activate; (void) user; (void) log_cap;
    return zxvi_fail(ZXV_E_NOTIMPL, "built without ML-DSA / SLH-DSA (ZXV_NO_PQC)");
#else
    if (!load || !readback || !activate) return zxvi_fail(ZXV_E_ARG, "device callbacks are NULL");
    if (log_cap == 0 || log_cap > 1u << 20) return zxvi_fail(ZXV_E_ARG, "log_cap must be 1..1048576");
    if (mx && mx->ctx != ctx) return zxvi_fail(ZXV_E_CAP, "matrix belongs to another context");
    zxv_hw_region *rg = calloc(1, sizeof *rg);
    if (!rg) return zxvi_fail(ZXV_E_NOMEM, "region");
    rg->logrec = calloc(log_cap, sizeof *rg->logrec);
    if (!rg->logrec) {
        free(rg);
        return zxvi_fail(ZXV_E_NOMEM, "audit log");
    }
    rg->ctx = ctx;
    rg->mx = mx;
    rg->load = load;
    rg->readback = readback;
    rg->activate = activate;
    rg->user = user;
    rg->ops.load = dev_load;
    rg->ops.readback = dev_readback;
    rg->ops.activate = dev_activate;
    hwa_init(&rg->log, rg->logrec, log_cap);
    hwr_region_init(&rg->r, region_id, target_class, target_digest, HWR_ALG_DUAL, pq_verify, NULL,
                    &rg->ops, rg, &rg->log);
    hwr_set_event(&rg->r, region_event, rg);
    if (mx) {
        rmutex_lock(&mx->mu);
        int lrc = provider < mx->m.npv
                      ? hwr_link(&rg->r, &mx->m, mx->m.pv[provider].resource, provider)
                      : HWR_E_ARG;
        rmutex_unlock(&mx->mu);
        if (lrc != HWR_OK) {
            free(rg->logrec);
            free(rg);
            return zxvi_fail(ZXV_E_ARG, "implementation %u is not a FABRIC or MICRO implementation of this matrix",
                             provider);
        }
    }
    pthread_mutex_init(&rg->mu, NULL);
    *out = rg;
    return zxvi_ok();
#endif
}

void zxv_hw_region_destroy(zxv_hw_region *rg)
{
    if (!rg) return;
    pthread_mutex_destroy(&rg->mu);
    free(rg->logrec);
    free(rg);
}

/* Lock order: region, then its matrix (the matrix is touched by trial,
 * rollback and tick). Events queued during the call are delivered after
 * both locks are released, so a handler may call into the region. */
#define RG_ENTER(rg, what)                                                       \
    if (!(rg)) return zxvi_fail(ZXV_E_ARG, "region is NULL");                   \
    int32_t rc_ = zxvi_cap_gate_ctx(cap, ZXV_CAP_HW_RECONF, what, (rg)->ctx);   \
    if (rc_) return rc_;                                                         \
    pthread_mutex_lock(&(rg)->mu);                                               \
    if ((rg)->mx) rmutex_lock(&(rg)->mx->mu)
#define RG_LEAVE(rg, value) return rg_leave((rg), (value))

static int32_t rg_leave(zxv_hw_region *rg, int32_t v)
{
    pend_t pend[RG_PEND];
    uint32_t n = rg->npend;
    zxv_hw_event_fn fn = rg->on_event;
    void *user = rg->event_user;
    uint32_t id = rg->r.id;
    memcpy(pend, rg->pend, n * sizeof *pend);
    rg->npend = 0;
    if (rg->mx) rmutex_unlock(&rg->mx->mu);
    pthread_mutex_unlock(&rg->mu);
    if (fn && n) {
        char saved[ZXVI_ERR_MAX];
        zxvi_err_save(saved); /* the caller reads this call's error, not the handler's */
        for (uint32_t i = 0; i < n; i++) fn(user, id, pend[i].ev, pend[i].reason, pend[i].version);
        zxvi_err_restore(saved);
    }
    return v;
}

int32_t zxv_hw_region_events(const zxv_cap *cap, zxv_hw_region *rg, zxv_hw_event_fn fn, void *user)
{
    RG_ENTER(rg, "hw_region_events");
    rg->on_event = fn;
    rg->event_user = user;
    RG_LEAVE(rg, zxvi_ok());
}

int32_t zxv_hw_region_trust_key(const zxv_cap *cap, zxv_hw_region *rg, const uint8_t pk[2656])
{
    RG_ENTER(rg, "hw_region_trust_key");
    if (!pk) RG_LEAVE(rg, zxvi_fail(ZXV_E_ARG, "pk is NULL"));
    if (rg->r.nkeys >= HWR_MAX_KEYS)
        RG_LEAVE(rg, zxvi_fail(ZXV_E_CAPACITY, "region trusts %u keys already", HWR_MAX_KEYS));
    memcpy(rg->keys[rg->r.nkeys], pk, HWR_DUAL_PK);
    RG_LEAVE(rg, hwr_to_zxv(rg, hwr_trust_key(&rg->r, rg->keys[rg->r.nkeys]), "hw_region_trust_key"));
}

int32_t zxv_hw_region_stage(const zxv_cap *cap, zxv_hw_region *rg, const uint8_t *image, size_t len)
{
    RG_ENTER(rg, "hw_region_stage");
    if (!image) RG_LEAVE(rg, zxvi_fail(ZXV_E_ARG, "image is NULL"));
    if (len > UINT32_MAX) RG_LEAVE(rg, zxvi_fail(ZXV_E_FORMAT, "image too large"));
    RG_LEAVE(rg, hwr_to_zxv(rg, hwr_stage(&rg->r, image, (uint32_t) len), "hw_region_stage"));
}

int32_t zxv_hw_region_trial(const zxv_cap *cap, zxv_hw_region *rg, uint32_t ticks)
{
    RG_ENTER(rg, "hw_region_trial");
    if (rg->mx && rg->mx->m.depth != 0)
        RG_LEAVE(rg, zxvi_fail(ZXV_E_STATE, "trial from inside an implementation"));
    RG_LEAVE(rg, hwr_to_zxv(rg, hwr_trial(&rg->r, ticks), "hw_region_trial"));
}

int32_t zxv_hw_region_commit(const zxv_cap *cap, zxv_hw_region *rg)
{
    RG_ENTER(rg, "hw_region_commit");
    RG_LEAVE(rg, hwr_to_zxv(rg, hwr_commit(&rg->r), "hw_region_commit"));
}

int32_t zxv_hw_region_rollback(const zxv_cap *cap, zxv_hw_region *rg)
{
    RG_ENTER(rg, "hw_region_rollback");
    RG_LEAVE(rg, hwr_to_zxv(rg, hwr_rollback(&rg->r), "hw_region_rollback"));
}

int32_t zxv_hw_region_tick(const zxv_cap *cap, zxv_hw_region *rg)
{
    RG_ENTER(rg, "hw_region_tick");
    RG_LEAVE(rg, hwr_to_zxv(rg, hwr_tick(&rg->r), "hw_region_tick"));
}

int32_t zxv_hw_region_info(const zxv_cap *cap, zxv_hw_region *rg, int32_t *out_state,
                           uint32_t *out_version, uint32_t *out_floor, uint32_t *out_quarantined)
{
    RG_ENTER(rg, "hw_region_info");
    if (out_state) *out_state = (int32_t) rg->r.state;
    if (out_version) {
        uint32_t s = rg->r.state == HWR_ST_PROBATION ? rg->r.staged : rg->r.active;
        *out_version = s == HWR_NO_SLOT ? 0 : rg->r.slot[s].version;
    }
    if (out_floor) *out_floor = rg->r.floor;
    if (out_quarantined) *out_quarantined = rg->r.n_quarantined;
    RG_LEAVE(rg, zxvi_ok());
}

int32_t zxv_hw_region_log(const zxv_cap *cap, zxv_hw_region *rg, uint8_t head[32], uint32_t *out_count)
{
    RG_ENTER(rg, "hw_region_log");
    int32_t rc = log_verify(rg->ctx, &rg->log, "hw_region_log");
    if (rc) RG_LEAVE(rg, rc);
    if (head) memcpy(head, rg->log.head, 32);
    if (out_count) *out_count = rg->log.n;
    RG_LEAVE(rg, zxvi_ok());
}

int32_t zxv_hw_region_log_entry(const zxv_cap *cap, zxv_hw_region *rg, uint32_t index,
                                int32_t *out_event, uint32_t *out_reason, uint32_t *out_id,
                                uint32_t *out_version, uint32_t *out_tag, uint8_t subject[32],
                                uint8_t hash[32])
{
    RG_ENTER(rg, "hw_region_log_entry");
    RG_LEAVE(rg, log_entry(&rg->log, index, out_event, out_reason, out_id, out_version, out_tag,
                           subject, hash));
}
