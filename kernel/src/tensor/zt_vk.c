/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_vk.c — zt_vk.h: zt_vk.comp's kernels on a Vulkan GPU. The loader is
 * opened at run time (no link to libvulkan); the SPIR-V is built from
 * zt_vk.comp by glslangValidator and included as ZT_VK_SPV_H. A build
 * without the Vulkan headers or the SPIR-V compiles to stubs that return
 * null, so every hosted build links. */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#    define _POSIX_C_SOURCE 200809L
#endif
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "zt_gpu.h"
#include "zt_vk.h"

#if defined(__has_include)
#    if __has_include(<vulkan/vulkan.h>) && defined(ZT_VK_SPV_H)
#        define ZT_VK_BUILD 1
#    endif
#endif

#ifndef ZT_VK_BUILD

/* ---- stubs: built without the Vulkan headers or the SPIR-V ---- */

zt_vk_t *zt_vk_new(int index, char *err, uint32_t errcap)
{
    (void) index;
    if (err && errcap)
        snprintf(err, errcap,
                 "built without Vulkan (needs the Vulkan headers and "
                 "glslangValidator at build time)");
    return NULL;
}
void zt_vk_free(zt_vk_t *g)
{
    (void) g;
}
const char *zt_vk_name(const zt_vk_t *g)
{
    (void) g;
    return "";
}
int zt_vk_map(zt_vk_t *g, const void *base, uint64_t len)
{
    (void) g, (void) base, (void) len;
    return -1;
}
int zt_vk_map_shared(zt_vk_t *g, void *base, uint64_t len)
{
    (void) g, (void) base, (void) len;
    return -1;
}
int zt_vk_mm(void *fctx, const zt_mmf_t *jobs, uint32_t n)
{
    (void) fctx, (void) jobs, (void) n;
    return 1;
}
void zt_kern_vk(zt_kern_t *out, const zt_kern_t *base, zt_vk_t *g)
{
    (void) g;
    *out = *base;
}
void zt_kern_vk_resident(zt_kern_t *out, const zt_kern_t *base, zt_vk_t *g)
{
    (void) g;
    *out = *base;
}
int zt_vk_fwd(void *fctx, const void *m, void *s, uint32_t B, zt_fx *logits, uint32_t first,
              uint32_t *n_sat)
{
    (void) fctx, (void) m, (void) s, (void) B, (void) logits, (void) first, (void) n_sat;
    return 1;
}
void zt_vk_set_min_tokens(zt_vk_t *g, uint32_t n)
{
    (void) g, (void) n;
}
void zt_vk_stats(const zt_vk_t *g, zt_vk_stats_t *out)
{
    (void) g;
    if (out) memset(out, 0, sizeof *out);
}

#else /* ZT_VK_BUILD */

#    define VK_NO_PROTOTYPES
#    include <vulkan/vulkan.h>
#    ifdef _WIN32
#        include <windows.h>
#    else
#        include <dlfcn.h>
#    endif

#    include ZT_VK_SPV_H /* zv_spv_<ZK>[]: one SPIR-V module per kernel */

#    define ZV_REGIONS 16u
#    define ZV_R       4u   /* rows per product workgroup */
#    define ZV_T       8u   /* tokens per tile when B > 1 */
#    define ZV_WG      256u /* threads per layer-kernel workgroup */
#    define ZV_NB      7u   /* bindings: b0..b5 and the saturation counter */

typedef struct {
    uint32_t u[16];
    uint32_t d[6];
    uint32_t gx;
} zv_pc_t;

/* ---- the loader's functions ---- */

#    define ZV_INSTANCE_FNS(X)                                                                     \
        X(DestroyInstance)                                                                         \
        X(EnumeratePhysicalDevices)                                                                \
        X(GetPhysicalDeviceProperties)                                                             \
        X(GetPhysicalDeviceProperties2)                                                            \
        X(GetPhysicalDeviceFeatures)                                                               \
        X(GetPhysicalDeviceMemoryProperties)                                                       \
        X(GetPhysicalDeviceQueueFamilyProperties)                                                  \
        X(EnumerateDeviceExtensionProperties)                                                      \
        X(CreateDevice)                                                                            \
        X(GetDeviceProcAddr)

#    define ZV_DEVICE_FNS(X)                                                                       \
        X(DestroyDevice)                                                                           \
        X(GetDeviceQueue)                                                                          \
        X(CreateBuffer)                                                                            \
        X(DestroyBuffer)                                                                           \
        X(GetBufferMemoryRequirements)                                                             \
        X(AllocateMemory)                                                                          \
        X(FreeMemory)                                                                              \
        X(BindBufferMemory)                                                                        \
        X(MapMemory)                                                                               \
        X(UnmapMemory)                                                                             \
        X(CreateShaderModule)                                                                      \
        X(DestroyShaderModule)                                                                     \
        X(CreateDescriptorSetLayout)                                                               \
        X(DestroyDescriptorSetLayout)                                                              \
        X(CreatePipelineLayout)                                                                    \
        X(DestroyPipelineLayout)                                                                   \
        X(CreateComputePipelines)                                                                  \
        X(DestroyPipeline)                                                                         \
        X(CreateDescriptorPool)                                                                    \
        X(DestroyDescriptorPool)                                                                   \
        X(ResetDescriptorPool)                                                                     \
        X(AllocateDescriptorSets)                                                                  \
        X(UpdateDescriptorSets)                                                                    \
        X(CreateCommandPool)                                                                       \
        X(DestroyCommandPool)                                                                      \
        X(AllocateCommandBuffers)                                                                  \
        X(BeginCommandBuffer)                                                                      \
        X(EndCommandBuffer)                                                                        \
        X(ResetCommandBuffer)                                                                      \
        X(CmdBindPipeline)                                                                         \
        X(CmdBindDescriptorSets)                                                                   \
        X(CmdPushConstants)                                                                        \
        X(CmdDispatch)                                                                             \
        X(CmdPipelineBarrier)                                                                      \
        X(CmdCopyBuffer)                                                                           \
        X(CmdResetQueryPool)                                                                       \
        X(CmdWriteTimestamp)                                                                       \
        X(CreateQueryPool)                                                                         \
        X(DestroyQueryPool)                                                                        \
        X(GetQueryPoolResults)                                                                     \
        X(CreateFence)                                                                             \
        X(DestroyFence)                                                                            \
        X(ResetFences)                                                                             \
        X(WaitForFences)                                                                           \
        X(QueueSubmit)

#    define ZV_DECL(n) PFN_vk##n n;
typedef struct {
    PFN_vkGetInstanceProcAddr GetInstanceProcAddr;
    PFN_vkCreateInstance CreateInstance;
    ZV_INSTANCE_FNS(ZV_DECL)
    ZV_DEVICE_FNS(ZV_DECL)
    PFN_vkGetMemoryHostPointerPropertiesEXT GetMemoryHostPointerPropertiesEXT; /* may be null */
} zv_api_t;

/* a buffer and its memory; h is its mapping when host-visible */
typedef struct {
    VkBuffer b;
    VkDeviceMemory m;
    uint8_t *h;
    uint64_t cap, used;
} vbuf_t;

/* a mapped region: host [base, base + len) lives in buf at offset 0 */
typedef struct {
    const uint8_t *base;
    uint64_t len;
    vbuf_t buf;
    bool shared;   /* zt_vk_map_shared: written by the GPU */
    bool mirrored; /* shared, but a copy: copied in before and out after a pass */
} region_t;

struct zt_vk {
    void *lib;
    zv_api_t vk;
    VkInstance inst;
    VkPhysicalDevice pd;
    VkDevice dev;
    VkQueue q;
    uint32_t qf;
    VkPhysicalDeviceMemoryProperties memp;
    uint64_t max_range;    /* maxStorageBufferRange */
    uint64_t align;        /* minStorageBufferOffsetAlignment */
    uint32_t max_groups;   /* maxComputeWorkGroupCount[0] and [1] (at least 65535) */
    bool host_import;      /* VK_EXT_external_memory_host */
    uint64_t import_align; /* minImportedHostPointerAlignment */
    double ts_ns;          /* nanoseconds per timestamp tick; 0: no timestamps */
    VkDescriptorSetLayout dsl;
    VkPipelineLayout pl;
    VkPipeline zk[ZG_NK]; /* ZG_K_MM unused: products use mm */
    VkPipeline mm[2][3];  /* [tiled][q4k, q6k, q80] */
    VkCommandPool cpool;
    VkCommandBuffer cb;
    VkFence fence;
    VkQueryPool qpool;
    VkDescriptorPool dpool;
    uint32_t dpool_cap, dpool_used;
    region_t reg[ZV_REGIONS];
    uint32_t n_reg;
    vbuf_t in, out, work, cst;
    char name[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE];
    zt_vk_stats_t st;
    uint32_t min_tokens;
};

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t) ts.tv_sec * 1000000000ull + (uint64_t) ts.tv_nsec;
}

static void say(char *err, uint32_t cap, const char *what, long code)
{
    if (!err || !cap) return;
    if (code)
        snprintf(err, cap, "%s (VkResult %ld)", what, code);
    else
        snprintf(err, cap, "%s", what);
}

static void *lib_open(void)
{
    const char *env = getenv("ZT_VK_LIB");
#    ifdef _WIN32
    return (void *) LoadLibraryA(env && *env ? env : "vulkan-1.dll");
#    else
    const char *names[] = {env, "libvulkan.so.1", "libvulkan.so", "libvulkan.1.dylib"};
    for (unsigned i = 0; i < sizeof names / sizeof names[0]; i++) {
        if (!names[i] || !*names[i]) continue;
        void *h = dlopen(names[i], RTLD_NOW | RTLD_LOCAL);
        if (h) return h;
    }
    return NULL;
#    endif
}

static void lib_close(void *h)
{
    if (!h) return;
#    ifdef _WIN32
    FreeLibrary((HMODULE) h);
#    else
    dlclose(h);
#    endif
}

static PFN_vkGetInstanceProcAddr lib_gipa(void *h)
{
    PFN_vkGetInstanceProcAddr f;
#    ifdef _WIN32
    FARPROC s = GetProcAddress((HMODULE) h, "vkGetInstanceProcAddr");
#    else
    void *s = dlsym(h, "vkGetInstanceProcAddr");
#    endif
    memcpy(&f, &s, sizeof f);
    return f;
}

/* ---- memory ---- */

static int mem_type(const zt_vk_t *g, uint32_t bits, VkMemoryPropertyFlags need)
{
    for (uint32_t i = 0; i < g->memp.memoryTypeCount; i++)
        if ((bits & (1u << i)) && (g->memp.memoryTypes[i].propertyFlags & need) == need)
            return (int) i;
    return -1;
}

static void vbuf_free(zt_vk_t *g, vbuf_t *v)
{
    if (v->h) g->vk.UnmapMemory(g->dev, v->m);
    if (v->b) g->vk.DestroyBuffer(g->dev, v->b, NULL);
    if (v->m) g->vk.FreeMemory(g->dev, v->m, NULL);
    memset(v, 0, sizeof *v);
}

/* a storage buffer of size bytes in the first memory type that has one of
 * prefs[0..np) (in that order); mapped when host-visible. 0 on success. */
static int vbuf_make(zt_vk_t *g, uint64_t size, const VkMemoryPropertyFlags *prefs, uint32_t np,
                     vbuf_t *v)
{
    memset(v, 0, sizeof *v);
    size = (size + 3) & ~(uint64_t) 3; /* the kernels read whole 32-bit words */
    VkBufferCreateInfo bi = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = size;
    bi.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
               VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (g->vk.CreateBuffer(g->dev, &bi, NULL, &v->b)) return -1;
    VkMemoryRequirements mr;
    g->vk.GetBufferMemoryRequirements(g->dev, v->b, &mr);
    for (uint32_t p = 0; p < np; p++) {
        int t = mem_type(g, mr.memoryTypeBits, prefs[p]);
        if (t < 0) continue;
        VkMemoryAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        ai.allocationSize = mr.size;
        ai.memoryTypeIndex = (uint32_t) t;
        if (g->vk.AllocateMemory(g->dev, &ai, NULL, &v->m)) continue;
        if (g->vk.BindBufferMemory(g->dev, v->b, v->m, 0)) {
            g->vk.FreeMemory(g->dev, v->m, NULL);
            v->m = VK_NULL_HANDLE;
            continue;
        }
        if (g->memp.memoryTypes[t].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
            void *h = NULL;
            if (g->vk.MapMemory(g->dev, v->m, 0, VK_WHOLE_SIZE, 0, &h)) {
                vbuf_free(g, v);
                return -1;
            }
            v->h = h;
        }
        v->cap = size;
        return 0;
    }
    vbuf_free(g, v);
    return -1;
}

static const VkMemoryPropertyFlags HOST_MEM[] = {
    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
        VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT};

/* make v a host-visible buffer of at least need bytes (contents not kept) */
static int grow(zt_vk_t *g, vbuf_t *v, uint64_t need)
{
    if (need < 16) need = 16;
    v->used = need;
    if (v->cap >= need && v->b) return 0;
    vbuf_free(g, v);
    if (vbuf_make(g, need + need / 4 + 4096, HOST_MEM, 2, v)) return -1;
    v->used = need;
    return 0;
}

/* ---- one command buffer at a time: begin, record, submit and wait ---- */

static int cb_begin(zt_vk_t *g, bool timed)
{
    if (g->vk.ResetCommandBuffer(g->cb, 0)) return -1;
    VkCommandBufferBeginInfo bi = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (g->vk.BeginCommandBuffer(g->cb, &bi)) return -1;
    if (timed && g->qpool) {
        g->vk.CmdResetQueryPool(g->cb, g->qpool, 0, 2);
        g->vk.CmdWriteTimestamp(g->cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, g->qpool, 0);
    }
    return 0;
}

/* every earlier write visible to every later read or write */
static void cb_barrier(zt_vk_t *g, VkPipelineStageFlags src, VkPipelineStageFlags dst,
                       VkAccessFlags sa, VkAccessFlags da)
{
    VkMemoryBarrier mb = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    mb.srcAccessMask = sa;
    mb.dstAccessMask = da;
    g->vk.CmdPipelineBarrier(g->cb, src, dst, 0, 1, &mb, 0, NULL, 0, NULL);
}

static int cb_submit(zt_vk_t *g, bool timed)
{
    if (timed && g->qpool)
        g->vk.CmdWriteTimestamp(g->cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, g->qpool, 1);
    cb_barrier(g, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_HOST_BIT,
               VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT);
    if (g->vk.EndCommandBuffer(g->cb)) return -1;
    if (g->vk.ResetFences(g->dev, 1, &g->fence)) return -1;
    VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &g->cb;
    if (g->vk.QueueSubmit(g->q, 1, &si, g->fence)) return -1;
    if (g->vk.WaitForFences(g->dev, 1, &g->fence, VK_TRUE, UINT64_MAX)) return -1;
    if (timed && g->qpool) {
        uint64_t ts[2];
        if (g->vk.GetQueryPoolResults(g->dev, g->qpool, 0, 2, sizeof ts, ts, sizeof ts[0],
                                      VK_QUERY_RESULT_64_BIT) == VK_SUCCESS &&
            ts[1] > ts[0])
            g->st.gpu_ns += (uint64_t) ((double) (ts[1] - ts[0]) * g->ts_ns);
    }
    return 0;
}

/* ---- creating and freeing ---- */

static int make_pipeline(zt_vk_t *g, const uint32_t *code, size_t bytes, VkPipeline *out)
{
    VkShaderModuleCreateInfo si = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    si.codeSize = bytes;
    si.pCode = code;
    VkShaderModule sm;
    if (g->vk.CreateShaderModule(g->dev, &si, NULL, &sm)) return -1;
    VkComputePipelineCreateInfo pi = {.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    pi.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pi.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pi.stage.module = sm;
    pi.stage.pName = "main";
    pi.layout = g->pl;
    VkResult r = g->vk.CreateComputePipelines(g->dev, VK_NULL_HANDLE, 1, &pi, NULL, out);
    g->vk.DestroyShaderModule(g->dev, sm, NULL);
    return r ? -1 : 0;
}

static bool has_ext(zt_vk_t *g, const char *name)
{
    uint32_t n = 0;
    if (g->vk.EnumerateDeviceExtensionProperties(g->pd, NULL, &n, NULL) || !n) return false;
    VkExtensionProperties *e = calloc(n, sizeof *e);
    bool found = false;
    if (e && !g->vk.EnumerateDeviceExtensionProperties(g->pd, NULL, &n, e))
        for (uint32_t i = 0; i < n && !found; i++) found = !strcmp(e[i].extensionName, name);
    free(e);
    return found;
}

/* pick the physical device: index, or (index < 0) the first discrete GPU,
 * else the first integrated one, else the first */
static int pick_device(zt_vk_t *g, int index, char *err, uint32_t errcap)
{
    uint32_t n = 0;
    if (g->vk.EnumeratePhysicalDevices(g->inst, &n, NULL) || !n) {
        say(err, errcap, "no Vulkan device", 0);
        return -1;
    }
    VkPhysicalDevice *pd = calloc(n, sizeof *pd);
    if (!pd || g->vk.EnumeratePhysicalDevices(g->inst, &n, pd)) {
        free(pd);
        return -1;
    }
    int pick = -1;
    if (index >= 0) {
        if ((uint32_t) index < n) pick = index;
    } else {
        for (int want = 0; want < 3 && pick < 0; want++)
            for (uint32_t i = 0; i < n && pick < 0; i++) {
                VkPhysicalDeviceProperties p;
                g->vk.GetPhysicalDeviceProperties(pd[i], &p);
                if ((want == 0 && p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) ||
                    (want == 1 && p.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) ||
                    want == 2)
                    pick = (int) i;
            }
    }
    if (pick >= 0) g->pd = pd[pick];
    free(pd);
    if (pick < 0) say(err, errcap, "no Vulkan device with that number", 0);
    return pick < 0 ? -1 : 0;
}

void zt_vk_free(zt_vk_t *g)
{
    if (!g) return;
    if (g->dev) {
        for (uint32_t i = 0; i < g->n_reg; i++) vbuf_free(g, &g->reg[i].buf);
        vbuf_free(g, &g->in);
        vbuf_free(g, &g->out);
        vbuf_free(g, &g->work);
        vbuf_free(g, &g->cst);
        for (uint32_t k = 0; k < ZG_NK; k++)
            if (g->zk[k]) g->vk.DestroyPipeline(g->dev, g->zk[k], NULL);
        for (uint32_t t = 0; t < 2; t++)
            for (uint32_t q = 0; q < 3; q++)
                if (g->mm[t][q]) g->vk.DestroyPipeline(g->dev, g->mm[t][q], NULL);
        if (g->dpool) g->vk.DestroyDescriptorPool(g->dev, g->dpool, NULL);
        if (g->pl) g->vk.DestroyPipelineLayout(g->dev, g->pl, NULL);
        if (g->dsl) g->vk.DestroyDescriptorSetLayout(g->dev, g->dsl, NULL);
        if (g->qpool) g->vk.DestroyQueryPool(g->dev, g->qpool, NULL);
        if (g->fence) g->vk.DestroyFence(g->dev, g->fence, NULL);
        if (g->cpool) g->vk.DestroyCommandPool(g->dev, g->cpool, NULL);
        g->vk.DestroyDevice(g->dev, NULL);
    }
    if (g->inst) g->vk.DestroyInstance(g->inst, NULL);
    lib_close(g->lib);
    free(g);
}

zt_vk_t *zt_vk_new(int index, char *err, uint32_t errcap)
{
    zt_vk_t *g = calloc(1, sizeof *g);
    if (!g) return NULL;
    VkResult r;
    const char *env = getenv("ZT_VK_DEVICE");
    if (index < 0 && env && *env) index = atoi(env);
    g->lib = lib_open();
    if (!g->lib || !(g->vk.GetInstanceProcAddr = lib_gipa(g->lib))) {
        say(err, errcap, "no Vulkan loader (libvulkan)", 0);
        goto fail;
    }
    g->vk.CreateInstance =
        (PFN_vkCreateInstance) g->vk.GetInstanceProcAddr(NULL, "vkCreateInstance");
    VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "zxv";
    app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo ii = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ii.pApplicationInfo = &app;
    if (!g->vk.CreateInstance || (r = g->vk.CreateInstance(&ii, NULL, &g->inst)) != VK_SUCCESS) {
        say(err, errcap, "cannot create a Vulkan 1.1 instance", g->vk.CreateInstance ? r : 0);
        goto fail;
    }
#    define ZV_LOAD_I(n)                                                                           \
        if (!(g->vk.n = (PFN_vk##n) g->vk.GetInstanceProcAddr(g->inst, "vk" #n))) {                \
            say(err, errcap, "the Vulkan loader lacks vk" #n, 0);                                  \
            goto fail;                                                                             \
        }
    ZV_INSTANCE_FNS(ZV_LOAD_I)
    if (pick_device(g, index, err, errcap)) goto fail;

    VkPhysicalDeviceProperties pp;
    g->vk.GetPhysicalDeviceProperties(g->pd, &pp);
    snprintf(g->name, sizeof g->name, "%s", pp.deviceName);
    VkPhysicalDeviceFeatures pf;
    g->vk.GetPhysicalDeviceFeatures(g->pd, &pf);
    if (!pf.shaderInt64) {
        say(err, errcap, "this GPU has no 64-bit integers in shaders (shaderInt64)", 0);
        goto fail;
    }
    g->vk.GetPhysicalDeviceMemoryProperties(g->pd, &g->memp);
    g->max_range = pp.limits.maxStorageBufferRange;
    g->align =
        pp.limits.minStorageBufferOffsetAlignment ? pp.limits.minStorageBufferOffsetAlignment : 4;
    g->max_groups = pp.limits.maxComputeWorkGroupCount[0] < pp.limits.maxComputeWorkGroupCount[1]
                        ? pp.limits.maxComputeWorkGroupCount[0]
                        : pp.limits.maxComputeWorkGroupCount[1];

    uint32_t nq = 0;
    g->vk.GetPhysicalDeviceQueueFamilyProperties(g->pd, &nq, NULL);
    VkQueueFamilyProperties *qp = calloc(nq ? nq : 1, sizeof *qp);
    if (!qp) goto fail;
    g->vk.GetPhysicalDeviceQueueFamilyProperties(g->pd, &nq, qp);
    g->qf = UINT32_MAX;
    for (uint32_t i = 0; i < nq && g->qf == UINT32_MAX; i++)
        if (qp[i].queueFlags & VK_QUEUE_COMPUTE_BIT) g->qf = i;
    bool ts = g->qf != UINT32_MAX && qp[g->qf].timestampValidBits > 0 &&
              pp.limits.timestampComputeAndGraphics;
    free(qp);
    if (g->qf == UINT32_MAX) {
        say(err, errcap, "no compute queue", 0);
        goto fail;
    }

    /* test knobs: ZT_VK_NO_IMPORT=1 mirrors shared regions; ZT_VK_MAX_RANGE=<bytes> lowers
     * the binding limit (to take the row-slicing path a phone takes) */
    const char *ni = getenv("ZT_VK_NO_IMPORT"), *mr = getenv("ZT_VK_MAX_RANGE");
    if (mr && *mr && strtoull(mr, NULL, 10) >= 4096 && strtoull(mr, NULL, 10) < g->max_range)
        g->max_range = strtoull(mr, NULL, 10);
    g->host_import =
        !(ni && ni[0] == '1') && has_ext(g, VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME);
    if (g->host_import) {
        VkPhysicalDeviceExternalMemoryHostPropertiesEXT hp = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_MEMORY_HOST_PROPERTIES_EXT};
        VkPhysicalDeviceProperties2 p2 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        p2.pNext = &hp;
        g->vk.GetPhysicalDeviceProperties2(g->pd, &p2);
        g->import_align = hp.minImportedHostPointerAlignment;
        if (!g->import_align) g->host_import = false;
    }
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qi = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qi.queueFamilyIndex = g->qf;
    qi.queueCount = 1;
    qi.pQueuePriorities = &prio;
    VkPhysicalDeviceFeatures want = {0};
    want.shaderInt64 = VK_TRUE;
    const char *exts[] = {VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME};
    VkDeviceCreateInfo di = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    di.queueCreateInfoCount = 1;
    di.pQueueCreateInfos = &qi;
    di.pEnabledFeatures = &want;
    di.enabledExtensionCount = g->host_import ? 1 : 0;
    di.ppEnabledExtensionNames = exts;
    if ((r = g->vk.CreateDevice(g->pd, &di, NULL, &g->dev)) != VK_SUCCESS) {
        g->dev = VK_NULL_HANDLE;
        say(err, errcap, "cannot create the Vulkan device", r);
        goto fail;
    }
#    define ZV_LOAD_D(n)                                                                           \
        if (!(g->vk.n = (PFN_vk##n) g->vk.GetDeviceProcAddr(g->dev, "vk" #n))) {                   \
            say(err, errcap, "the Vulkan device lacks vk" #n, 0);                                  \
            goto fail;                                                                             \
        }
    ZV_DEVICE_FNS(ZV_LOAD_D)
    if (g->host_import)
        g->vk.GetMemoryHostPointerPropertiesEXT =
            (PFN_vkGetMemoryHostPointerPropertiesEXT) g->vk.GetDeviceProcAddr(
                g->dev, "vkGetMemoryHostPointerPropertiesEXT");
    if (!g->vk.GetMemoryHostPointerPropertiesEXT) g->host_import = false;
    g->vk.GetDeviceQueue(g->dev, g->qf, 0, &g->q);

    VkDescriptorSetLayoutBinding lb[ZV_NB];
    for (uint32_t i = 0; i < ZV_NB; i++)
        lb[i] = (VkDescriptorSetLayoutBinding){i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
                                               VK_SHADER_STAGE_COMPUTE_BIT, NULL};
    VkDescriptorSetLayoutCreateInfo li = {.sType =
                                              VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    li.bindingCount = ZV_NB;
    li.pBindings = lb;
    VkPushConstantRange pr = {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(zv_pc_t)};
    VkPipelineLayoutCreateInfo pli = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &g->dsl;
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges = &pr;
    if (g->vk.CreateDescriptorSetLayout(g->dev, &li, NULL, &g->dsl) ||
        g->vk.CreatePipelineLayout(g->dev, &pli, NULL, &g->pl)) {
        say(err, errcap, "cannot create the pipeline layout", 0);
        goto fail;
    }
    for (uint32_t i = 0; i < sizeof zv_mods / sizeof zv_mods[0]; i++) {
        uint32_t k = zv_mods[i].zk;
        VkPipeline *pp_ = k >= 100 ? &g->mm[(k - 100) / 3][(k - 100) % 3] : &g->zk[k];
        if (make_pipeline(g, zv_mods[i].code, zv_mods[i].bytes, pp_)) {
            say(err, errcap, "a Vulkan kernel did not build", 0);
            goto fail;
        }
    }

    VkCommandPoolCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    ci.queueFamilyIndex = g->qf;
    VkCommandBufferAllocateInfo ca = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ca.commandBufferCount = 1;
    VkFenceCreateInfo fi = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if (g->vk.CreateCommandPool(g->dev, &ci, NULL, &g->cpool) ||
        (ca.commandPool = g->cpool, g->vk.AllocateCommandBuffers(g->dev, &ca, &g->cb)) ||
        g->vk.CreateFence(g->dev, &fi, NULL, &g->fence)) {
        say(err, errcap, "cannot create the command buffer", 0);
        goto fail;
    }
    if (ts) {
        VkQueryPoolCreateInfo qpi = {.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
        qpi.queryType = VK_QUERY_TYPE_TIMESTAMP;
        qpi.queryCount = 2;
        if (g->vk.CreateQueryPool(g->dev, &qpi, NULL, &g->qpool) == VK_SUCCESS)
            g->ts_ns = pp.limits.timestampPeriod;
        else
            g->qpool = VK_NULL_HANDLE;
    }
    return g;
fail:
    zt_vk_free(g);
    return NULL;
}

const char *zt_vk_name(const zt_vk_t *g)
{
    return g ? g->name : "";
}

/* ---- regions ---- */

/* copy len bytes from host src into device buffer v (not host-visible) */
static int upload(zt_vk_t *g, vbuf_t *v, const uint8_t *src, uint64_t len)
{
    vbuf_t stg;
    uint64_t chunk = len < ((uint64_t) 32 << 20) ? len : ((uint64_t) 32 << 20);
    if (vbuf_make(g, chunk, HOST_MEM, 2, &stg)) return -1;
    int rc = 0;
    for (uint64_t o = 0; o < len && !rc; o += chunk) {
        uint64_t n = len - o < chunk ? len - o : chunk;
        memcpy(stg.h, src + o, (size_t) n);
        VkBufferCopy bc = {0, o, n};
        if (cb_begin(g, false)) rc = -1;
        if (!rc) g->vk.CmdCopyBuffer(g->cb, stg.b, v->b, 1, &bc);
        if (!rc && cb_submit(g, false)) rc = -1;
    }
    vbuf_free(g, &stg);
    return rc;
}

int zt_vk_map(zt_vk_t *g, const void *base, uint64_t len)
{
    if (!g || !base || !len || g->n_reg >= ZV_REGIONS) return -1;
    region_t *rg = &g->reg[g->n_reg];
    memset(rg, 0, sizeof *rg);
    static const VkMemoryPropertyFlags vis[] = {VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
                                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT};
    static const VkMemoryPropertyFlags dev[] = {VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT};
    if (vbuf_make(g, len, vis, 1, &rg->buf) == 0) { /* unified memory, or a large BAR */
        memcpy(rg->buf.h, base, (size_t) len);
    } else if (vbuf_make(g, len, dev, 1, &rg->buf) == 0) { /* VRAM, through a staging copy */
        if (rg->buf.h)
            memcpy(rg->buf.h, base, (size_t) len);
        else if (upload(g, &rg->buf, base, len)) {
            vbuf_free(g, &rg->buf);
            return -1;
        }
    } else if (vbuf_make(g, len, HOST_MEM, 2, &rg->buf) == 0) { /* system memory */
        memcpy(rg->buf.h, base, (size_t) len);
    } else {
        return -1;
    }
    rg->base = base;
    rg->len = len;
    g->n_reg++;
    return 0;
}

/* import [base, base + len) as GPU memory in place; 0 on success */
static int import_host(zt_vk_t *g, void *base, uint64_t len, vbuf_t *v)
{
    if (!g->host_import || ((uintptr_t) base % g->import_align) || (len % g->import_align))
        return -1;
    memset(v, 0, sizeof *v);
    VkMemoryHostPointerPropertiesEXT hp = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_HOST_POINTER_PROPERTIES_EXT};
    if (g->vk.GetMemoryHostPointerPropertiesEXT(
            g->dev, VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT, base, &hp))
        return -1;
    VkExternalMemoryBufferCreateInfo eb = {
        .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO};
    eb.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT;
    VkBufferCreateInfo bi = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.pNext = &eb;
    bi.size = len;
    bi.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
               VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (g->vk.CreateBuffer(g->dev, &bi, NULL, &v->b)) return -1;
    VkMemoryRequirements mr;
    g->vk.GetBufferMemoryRequirements(g->dev, v->b, &mr);
    int t = mem_type(g, mr.memoryTypeBits & hp.memoryTypeBits,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (t < 0 || mr.size > len) {
        vbuf_free(g, v);
        return -1;
    }
    VkImportMemoryHostPointerInfoEXT imp = {
        .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT};
    imp.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT;
    imp.pHostPointer = base;
    VkMemoryAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.pNext = &imp;
    ai.allocationSize = len;
    ai.memoryTypeIndex = (uint32_t) t;
    if (g->vk.AllocateMemory(g->dev, &ai, NULL, &v->m) ||
        g->vk.BindBufferMemory(g->dev, v->b, v->m, 0)) {
        vbuf_free(g, v);
        return -1;
    }
    v->cap = len;
    return 0;
}

int zt_vk_map_shared(zt_vk_t *g, void *base, uint64_t len)
{
    if (!g || !base || !len || g->n_reg >= ZV_REGIONS) return -1;
    region_t *rg = &g->reg[g->n_reg];
    memset(rg, 0, sizeof *rg);
    if (import_host(g, base, len, &rg->buf) == 0) {
        g->st.nocopy++;
    } else if (vbuf_make(g, len, HOST_MEM, 2, &rg->buf) == 0) {
        rg->mirrored = true; /* copied in and out around each pass */
    } else {
        return -1;
    }
    rg->base = base;
    rg->len = len;
    rg->shared = true;
    g->n_reg++;
    return 0;
}

static int find_region(const zt_vk_t *g, const uint8_t *p, uint64_t len, uint64_t *off)
{
    for (uint32_t i = 0; i < g->n_reg; i++) {
        const region_t *r = &g->reg[i];
        if (p >= r->base && (uint64_t) (p - r->base) <= r->len &&
            len <= r->len - (uint64_t) (p - r->base)) {
            *off = (uint64_t) (p - r->base);
            return (int) i;
        }
    }
    return -1;
}

/* ---- dispatches ---- */

/* a buffer argument: the buffer, the byte offset, and the bytes the kernel
 * may read or write from there */
typedef struct {
    VkBuffer b;
    uint64_t off, len;
    uint64_t cap; /* the buffer's size: a binding may reach to its end */
} vref_t;

/* room for n descriptor sets in the pool (emptied: call before recording) */
static int sets_reserve(zt_vk_t *g, uint32_t n)
{
    g->dpool_used = 0;
    if (g->dpool && g->dpool_cap >= n)
        return g->vk.ResetDescriptorPool(g->dev, g->dpool, 0) ? -1 : 0;
    if (g->dpool) g->vk.DestroyDescriptorPool(g->dev, g->dpool, NULL);
    g->dpool = VK_NULL_HANDLE;
    uint32_t cap = n < 256 ? 256 : n + n / 4;
    VkDescriptorPoolSize ps = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, cap * ZV_NB};
    VkDescriptorPoolCreateInfo pi = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pi.maxSets = cap;
    pi.poolSizeCount = 1;
    pi.pPoolSizes = &ps;
    if (g->vk.CreateDescriptorPool(g->dev, &pi, NULL, &g->dpool)) return -1;
    g->dpool_cap = cap;
    return 0;
}

/* record one dispatch of pipe over gx x gy workgroups, with bindings r[0..6)
 * and the saturation counter at work offset 0. 0 on success, 1 when a
 * binding cannot be expressed (too large for one binding on this GPU). */
static int dispatch(zt_vk_t *g, VkPipeline pipe, const vref_t *r, zv_pc_t *pc, uint32_t gx,
                    uint32_t gy)
{
    if (g->dpool_used >= g->dpool_cap) return 1;
    VkDescriptorBufferInfo bi[ZV_NB];
    for (uint32_t j = 0; j < 6; j++) {
        /* bind from the aligned offset as far as the GPU allows (whole words:
         * a kernel reading an argument's last byte reads its whole word) */
        uint64_t base = r[j].off - r[j].off % g->align, d = r[j].off - base;
        uint64_t range = r[j].cap - base, lim = g->max_range & ~(uint64_t) 3;
        if (range > lim) range = lim;
        if (r[j].off > r[j].cap || r[j].len + d > range || d > UINT32_MAX) return 1;
        bi[j] = (VkDescriptorBufferInfo){r[j].b, base, range};
        pc->d[j] = (uint32_t) d;
    }
    bi[6] = (VkDescriptorBufferInfo){g->work.b, 0, 4};
    VkDescriptorSet set;
    VkDescriptorSetAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool = g->dpool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &g->dsl;
    if (g->vk.AllocateDescriptorSets(g->dev, &ai, &set)) return 1;
    g->dpool_used++;
    VkWriteDescriptorSet w[ZV_NB];
    for (uint32_t j = 0; j < ZV_NB; j++) {
        w[j] = (VkWriteDescriptorSet){.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w[j].dstSet = set;
        w[j].dstBinding = j;
        w[j].descriptorCount = 1;
        w[j].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        w[j].pBufferInfo = &bi[j];
    }
    g->vk.UpdateDescriptorSets(g->dev, ZV_NB, w, 0, NULL);
    pc->gx = gx;
    g->vk.CmdBindPipeline(g->cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
    g->vk.CmdBindDescriptorSets(g->cb, VK_PIPELINE_BIND_POINT_COMPUTE, g->pl, 0, 1, &set, 0, NULL);
    g->vk.CmdPushConstants(g->cb, g->pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof *pc, pc);
    g->vk.CmdDispatch(g->cb, gx, gy, 1);
    cb_barrier(g, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
               VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
    return 0;
}

/* a layer kernel over n threads (a 2-D grid past max_groups workgroups) */
static int dispatch_zk(zt_vk_t *g, uint32_t k, const vref_t *r, zv_pc_t *pc)
{
    uint32_t groups = (pc->u[0] + ZV_WG - 1) / ZV_WG;
    uint32_t gx = groups < g->max_groups ? groups : g->max_groups;
    uint32_t gy = (groups + gx - 1) / gx;
    if (gy > g->max_groups) return 1;
    return dispatch(g, g->zk[k], r, pc, gx, gy);
}

/* how many dispatches a product of rows x rowbytes takes (row slices that
 * each fit one binding and the grid) */
static uint32_t mm_slice_rows(const zt_vk_t *g, uint32_t rowbytes)
{
    uint64_t fit = (g->max_range - g->align) / rowbytes, grid = (uint64_t) g->max_groups * ZV_R;
    if (fit > grid) fit = grid;
    return (uint32_t) (fit > UINT32_MAX ? UINT32_MAX : fit);
}

/* a product y[B x rows] = w (q type: 0 Q4_K, 1 Q6_K, 2 Q8_0) times the
 * activations a / shifts / sums; slices the rows when the weights are
 * larger than one binding */
static int dispatch_mm(zt_vk_t *g, int q, uint32_t rows, uint32_t nb, uint32_t astride, uint32_t B,
                       uint32_t rowbytes, vref_t w, vref_t a, vref_t ash, vref_t asum, vref_t y)
{
    uint32_t tiled = B > 1, T = tiled ? ZV_T : 1, sb = astride / 32;
    uint32_t per = mm_slice_rows(g, rowbytes);
    if (!per || (B + T - 1) / T > g->max_groups) return 1;
    a.len = (uint64_t) B * astride * 2;
    ash.len = (uint64_t) B * sb;
    asum.len = (uint64_t) B * sb * 4;
    y.len = (uint64_t) B * rows * 4;
    for (uint32_t r0 = 0; r0 < rows; r0 += per) {
        uint32_t n = rows - r0 < per ? rows - r0 : per;
        vref_t ref[6] = {{w.b, w.off + (uint64_t) r0 * rowbytes, (uint64_t) n * rowbytes, w.cap},
                         a,
                         ash,
                         asum,
                         y,
                         {g->work.b, 0, 4, g->work.cap}};
        zv_pc_t pc = {{n, nb, astride, B, rowbytes, rows, r0}, {0}, 0};
        if (dispatch(g, g->mm[tiled][q], ref, &pc, (n + ZV_R - 1) / ZV_R, (B + T - 1) / T))
            return 1;
    }
    return 0;
}

/* ---- the per-product path (zt_kern_t.mm_full) ---- */

#    define AL(x) (((x) + 255u) & ~(uint64_t) 255u)

static int qindex(uint32_t qtype)
{
    return qtype == 12 ? 0 : qtype == 14 ? 1 : qtype == 8 ? 2 : -1;
}

static uint64_t row_bytes(uint32_t qtype, uint32_t nb)
{
    return qtype == 12   ? (uint64_t) (nb / 8) * 144
           : qtype == 14 ? (uint64_t) (nb / 8) * 210
                         : (uint64_t) nb * 34;
}

int zt_vk_mm(void *fctx, const zt_mmf_t *jobs, uint32_t n)
{
    zt_vk_t *g = fctx;
    if (!g || n == 0 || n > 8) return 1;
    uint64_t t_in = now_ns();
    g->st.calls++;
    if (jobs[0].B < g->min_tokens) {
        g->st.declined++;
        return 1;
    }
    int ri[8];
    uint64_t woff[8], aoff[8], soff[8], hoff[8], yoff[8];
    uint64_t in_bytes = 0, out_bytes = 0;
    uint32_t sets = 0;
    for (uint32_t i = 0; i < n; i++) {
        const zt_mmf_t *j = &jobs[i];
        uint32_t nb = j->cols / 32;
        bool ok = j->raw && j->B && j->cols % 32 == 0 && j->astride >= j->cols &&
                  j->astride % 32 == 0 && qindex(j->qtype) >= 0 &&
                  (j->qtype == 8 || j->cols % 256 == 0);
        uint64_t rb = row_bytes(j->qtype, nb);
        if (ok) ri[i] = find_region(g, j->raw, rb * j->rows, &woff[i]);
        if (!ok || ri[i] < 0 || !mm_slice_rows(g, (uint32_t) rb)) {
            g->st.declined++;
            return 1;
        }
        sets += (j->rows + mm_slice_rows(g, (uint32_t) rb) - 1) / mm_slice_rows(g, (uint32_t) rb);
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
    if (grow(g, &g->in, in_bytes) || grow(g, &g->out, out_bytes) || grow(g, &g->work, 4) ||
        sets_reserve(g, sets)) {
        g->st.declined++;
        return 1;
    }
    for (uint32_t i = 0; i < n; i++) { /* a mirrored region's latest bytes */
        region_t *r = &g->reg[ri[i]];
        if (r->mirrored && (i == 0 || ri[i] != ri[i - 1]))
            memcpy(r->buf.h, r->base, (size_t) r->len);
    }
    for (uint32_t i = 0; i < n; i++) {
        const zt_mmf_t *j = &jobs[i];
        if (i && aoff[i] == aoff[i - 1]) continue;
        uint64_t nbk = (uint64_t) j->B * (j->astride / 32);
        memcpy(g->in.h + aoff[i], j->a, (size_t) j->B * j->astride * 2);
        memcpy(g->in.h + soff[i], j->asum, (size_t) nbk * 4);
        memcpy(g->in.h + hoff[i], j->ash, (size_t) nbk);
    }
    int rc = cb_begin(g, true) ? 1 : 0;
    for (uint32_t i = 0; i < n && !rc; i++) {
        const zt_mmf_t *j = &jobs[i];
        uint32_t nb = j->cols / 32;
        vref_t w = {g->reg[ri[i]].buf.b, woff[i], 0, g->reg[ri[i]].buf.cap};
        vref_t a = {g->in.b, aoff[i], 0, g->in.cap}, ash = {g->in.b, hoff[i], 0, g->in.cap};
        vref_t asum = {g->in.b, soff[i], 0, g->in.cap}, y = {g->out.b, yoff[i], 0, g->out.cap};
        rc = dispatch_mm(g, qindex(j->qtype), j->rows, nb, j->astride, j->B,
                         (uint32_t) row_bytes(j->qtype, nb), w, a, ash, asum, y);
    }
    if (!rc && cb_submit(g, true)) rc = 1;
    if (rc) {
        g->vk.ResetCommandBuffer(g->cb, 0);
        g->st.declined++;
        return 1;
    }
    for (uint32_t i = 0; i < n; i++) {
        const zt_mmf_t *j = &jobs[i];
        const zt_fx *src = (const zt_fx *) (g->out.h + yoff[i]);
        for (uint32_t t = 0; t < j->B; t++)
            memcpy(j->y + (uint64_t) t * j->ystride, src + (uint64_t) t * j->rows,
                   (size_t) j->rows * 4);
    }
    g->st.products += n;
    g->st.wall_ns += now_ns() - t_in;
    return 0;
}

void zt_kern_vk(zt_kern_t *out, const zt_kern_t *base, zt_vk_t *g)
{
    *out = *base;
    out->mm_full = g ? zt_vk_mm : 0;
    out->fwd_full = 0;
    out->fctx = g;
}

/* ---- the whole forward pass (zt_gpu.h) ---- */

static void *be_work(void *ctx, uint64_t bytes)
{
    zt_vk_t *g = ctx;
    return grow(g, &g->work, bytes) ? NULL : g->work.h;
}

static void *be_cst(void *ctx, uint64_t bytes)
{
    zt_vk_t *g = ctx;
    return grow(g, &g->cst, bytes ? bytes : 4) ? NULL : g->cst.h;
}

/* an op's buffer argument; 1 if it cannot be bound */
static int be_ref(const zt_vk_t *g, const zg_buf_t *b, vref_t *out)
{
    switch (b->kind) {
    case ZG_B_WORK:
        if (b->off >= g->work.used) return 1;
        *out = (vref_t){g->work.b, b->off, g->work.used - b->off, g->work.cap};
        return 0;
    case ZG_B_CONST:
        if (b->off >= g->cst.used) return 1;
        *out = (vref_t){g->cst.b, b->off, g->cst.used - b->off, g->cst.cap};
        return 0;
    case ZG_B_HOST: {
        uint64_t off;
        int ri = find_region(g, b->host, b->len, &off);
        if (ri < 0 || (b->inplace && !g->reg[ri].shared)) return 1;
        *out = (vref_t){g->reg[ri].buf.b, off, b->len, g->reg[ri].buf.cap};
        return 0;
    }
    default:
        *out = (vref_t){g->work.b, 0, 4, g->work.cap}; /* unused argument */
        return 0;
    }
}

static void mirror(zt_vk_t *g, bool in)
{
    for (uint32_t i = 0; i < g->n_reg; i++) {
        region_t *r = &g->reg[i];
        if (!r->mirrored) continue;
        if (in)
            memcpy(r->buf.h, r->base, (size_t) r->len);
        else
            memcpy((void *) r->base, r->buf.h, (size_t) r->len);
    }
}

static int be_run(void *ctx, const zg_op_t *ops, uint32_t n, const void *work, const void *cst)
{
    zt_vk_t *g = ctx;
    (void) work, (void) cst;
    /* check every op before anything runs: a pass is all or nothing */
    uint32_t sets = 0;
    for (uint32_t i = 0; i < n; i++) {
        const zg_op_t *o = &ops[i];
        vref_t r;
        if (o->k >= ZG_NK) return 1;
        if (!o->u[0]) continue;
        if (o->k == ZG_K_MM) {
            if (qindex(o->u[4]) < 0 || !mm_slice_rows(g, o->u[5])) return 1;
            sets += (o->u[1] + mm_slice_rows(g, o->u[5]) - 1) / mm_slice_rows(g, o->u[5]);
        } else {
            sets++;
        }
        for (int j = 0; j < 6; j++)
            if (be_ref(g, &o->b[j], &r)) return 1;
    }
    if (sets_reserve(g, sets)) return 1;
    mirror(g, true);
    int rc = cb_begin(g, true) ? 1 : 0;
    for (uint32_t i = 0; i < n && !rc; i++) {
        const zg_op_t *o = &ops[i];
        if (!o->u[0]) continue;
        vref_t r[6];
        for (int j = 0; j < 6; j++) be_ref(g, &o->b[j], &r[j]);
        if (o->k == ZG_K_MM) {
            rc = dispatch_mm(g, qindex(o->u[4]), o->u[1], o->u[2], o->u[3], o->u[6], o->u[5], r[0],
                             r[1], r[2], r[3], r[4]);
        } else {
            zv_pc_t pc;
            memset(&pc, 0, sizeof pc);
            memcpy(pc.u, o->u, sizeof pc.u);
            rc = dispatch_zk(g, o->k, r, &pc);
        }
    }
    if (!rc && cb_submit(g, true)) rc = 1;
    if (rc) {
        g->vk.ResetCommandBuffer(g->cb, 0);
        return 1;
    }
    mirror(g, false);
    return 0;
}

int zt_vk_fwd(void *fctx, const void *m, void *s, uint32_t B, zt_fx *logits, uint32_t first,
              uint32_t *n_sat)
{
    zt_vk_t *g = fctx;
    if (!g) return 1;
    uint64_t t0 = now_ns();
    zg_backend_t be = {g, be_work, be_cst, be_run};
    int r = zg_forward(&be, m, s, B, logits, first, n_sat);
    if (r) {
        g->st.passes_declined++;
        return r;
    }
    g->st.passes++;
    g->st.pass_ops += zg_last_ops();
    g->st.wall_ns += now_ns() - t0;
    return 0;
}

void zt_kern_vk_resident(zt_kern_t *out, const zt_kern_t *base, zt_vk_t *g)
{
    zt_kern_vk(out, base, g);
    out->fwd_full = g ? zt_vk_fwd : 0;
}

void zt_vk_set_min_tokens(zt_vk_t *g, uint32_t n)
{
    if (g) g->min_tokens = n;
}

void zt_vk_stats(const zt_vk_t *g, zt_vk_stats_t *out)
{
    if (g && out) *out = g->st;
}

#endif /* ZT_VK_BUILD */
