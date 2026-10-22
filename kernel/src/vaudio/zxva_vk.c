/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zxva_vk.c — zxva_vk.h. Hosted; plain C11. Follows
 * kernel/src/tensor/zt_vk.c's pattern: the loader is opened with dlopen, the
 * SPIR-V comes from the build (ZXVA_VK_SPV_H, made by gen_zxva_vk_spv.sh),
 * and without either the file compiles to stubs. */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#    define _POSIX_C_SOURCE 200809L
#endif
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zxva_vk.h"

#if defined(__has_include)
#    if __has_include(<vulkan/vulkan.h>) && defined(ZXVA_VK_SPV_H)
#        define ZXVA_VK_BUILD 1
#    endif
#endif

#ifndef ZXVA_VK_BUILD

/* ---- stubs: built without the Vulkan headers or the SPIR-V ---- */
zxva_vk_t *zxva_vk_new(int index, char *err, uint32_t errcap)
{
    (void) index;
    if (err && errcap)
        snprintf(
            err, errcap,
            "built without Vulkan (needs the Vulkan headers and glslangValidator at build time: "
            "gen_zxva_vk_spv.sh, -DZXVA_VK_SPV_H)");
    return NULL;
}
void zxva_vk_free(zxva_vk_t *g)
{
    (void) g;
}
const char *zxva_vk_name(const zxva_vk_t *g)
{
    (void) g;
    return "";
}
void zxva_vk_backend(zxva_vk_t *g, zxva_backend_t *b)
{
    (void) g;
    memset(b, 0, sizeof *b);
    zxva_backend_cpu(b);
}
double zxva_vk_kernel_ms(const zxva_vk_t *g)
{
    (void) g;
    return 0;
}

#else /* ZXVA_VK_BUILD */

#    define VK_NO_PROTOTYPES
#    include <vulkan/vulkan.h>
#    ifdef _WIN32
#        include <windows.h>
#    else
#        include <dlfcn.h>
#    endif

#    include ZXVA_VK_SPV_H /* zxva_vk_spv[] */

#    define ZXV_WG    256u
#    define ZXV_BATCH 256u
#    define ZXV_NB    9u

typedef struct {
    uint32_t n, N, L, num, den, c0lo, c0hi, lenlo, lenhi, k0lo, k0hi, floor_mag, rel, nf;
} zxv_pc_t;

#    define ZXV_INSTANCE_FNS(X)                                                                    \
        X(DestroyInstance)                                                                         \
        X(EnumeratePhysicalDevices)                                                                \
        X(GetPhysicalDeviceProperties)                                                             \
        X(GetPhysicalDeviceFeatures)                                                               \
        X(GetPhysicalDeviceMemoryProperties)                                                       \
        X(GetPhysicalDeviceQueueFamilyProperties)                                                  \
        X(CreateDevice)                                                                            \
        X(GetDeviceProcAddr)

#    define ZXV_DEVICE_FNS(X)                                                                      \
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

#    define ZXV_DECL(n) PFN_vk##n n;
typedef struct {
    PFN_vkGetInstanceProcAddr GetInstanceProcAddr;
    PFN_vkCreateInstance CreateInstance;
    ZXV_INSTANCE_FNS(ZXV_DECL)
    ZXV_DEVICE_FNS(ZXV_DECL)
} zxv_api_t;

typedef struct {
    VkBuffer b;
    VkDeviceMemory m;
    uint8_t *h;
    uint64_t cap;
    void *shadow; /* inputs: what the buffer holds */
    size_t valid;
} vbuf_t;

enum { B_X, B_WIN, B_TW, B_SCR, B_RE, B_IM, B_MAG, B_MASK, B_TOP };

struct zxva_vk {
    void *lib;
    zxv_api_t vk;
    VkInstance inst;
    VkPhysicalDevice pd;
    VkDevice dev;
    VkQueue q;
    uint32_t qf;
    VkPhysicalDeviceMemoryProperties memp;
    VkDescriptorSetLayout dsl;
    VkPipelineLayout pl;
    VkPipeline pipe;
    VkDescriptorPool dpool;
    VkDescriptorSet ds;
    bool ds_dirty;
    VkCommandPool cpool;
    VkCommandBuffer cb;
    VkFence fence;
    VkQueryPool qpool;
    double ts_ns;
    double kern_ms;
    uint64_t max_range;
    uint32_t max_groups;
    vbuf_t b[ZXV_NB];
    char name[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE];
};

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
    const char *env = getenv("ZXVA_VK_LIB");
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

static int mem_type(const zxva_vk_t *g, uint32_t bits, VkMemoryPropertyFlags need)
{
    for (uint32_t i = 0; i < g->memp.memoryTypeCount; i++)
        if ((bits & (1u << i)) && (g->memp.memoryTypes[i].propertyFlags & need) == need)
            return (int) i;
    return -1;
}

static void vbuf_free(zxva_vk_t *g, vbuf_t *v)
{
    if (v->h) g->vk.UnmapMemory(g->dev, v->m);
    if (v->b) g->vk.DestroyBuffer(g->dev, v->b, NULL);
    if (v->m) g->vk.FreeMemory(g->dev, v->m, NULL);
    free(v->shadow);
    memset(v, 0, sizeof *v);
}

static const VkMemoryPropertyFlags HOST_MEM[] = {
    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
        VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT};

/* a host-visible storage buffer of at least need bytes (contents not kept) */
static int grow(zxva_vk_t *g, vbuf_t *v, uint64_t need)
{
    need = (need + 3) & ~(uint64_t) 3;
    if (need < 16) need = 16;
    if (v->b && v->cap >= need) return 0;
    if (need > g->max_range) return -1;
    vbuf_free(g, v);
    g->ds_dirty = true;
    VkBufferCreateInfo bi = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = need;
    bi.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (g->vk.CreateBuffer(g->dev, &bi, NULL, &v->b)) return -1;
    VkMemoryRequirements mr;
    g->vk.GetBufferMemoryRequirements(g->dev, v->b, &mr);
    for (uint32_t p = 0; p < 2; p++) {
        int t = mem_type(g, mr.memoryTypeBits, HOST_MEM[p]);
        if (t < 0) continue;
        VkMemoryAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        ai.allocationSize = mr.size;
        ai.memoryTypeIndex = (uint32_t) t;
        if (g->vk.AllocateMemory(g->dev, &ai, NULL, &v->m)) continue;
        void *h = NULL;
        if (g->vk.BindBufferMemory(g->dev, v->b, v->m, 0) ||
            g->vk.MapMemory(g->dev, v->m, 0, VK_WHOLE_SIZE, 0, &h)) {
            g->vk.FreeMemory(g->dev, v->m, NULL);
            v->m = VK_NULL_HANDLE;
            continue;
        }
        v->h = h;
        v->cap = need;
        return 0;
    }
    vbuf_free(g, v);
    return -1;
}

static int put(zxva_vk_t *g, vbuf_t *v, const void *src, size_t bytes)
{
    if (v->b && v->valid == bytes && v->shadow && memcmp(v->shadow, src, bytes) == 0) return 0;
    if (grow(g, v, bytes)) return -1;
    void *sh = realloc(v->shadow, bytes ? bytes : 1);
    if (!sh) return -1;
    v->shadow = sh;
    memcpy(sh, src, bytes);
    memcpy(v->h, src, bytes);
    v->valid = bytes;
    return 0;
}

void zxva_vk_free(zxva_vk_t *g)
{
    if (!g) return;
    if (g->dev) {
        for (uint32_t i = 0; i < ZXV_NB; i++) vbuf_free(g, &g->b[i]);
        if (g->pipe) g->vk.DestroyPipeline(g->dev, g->pipe, NULL);
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

static int pick_device(zxva_vk_t *g, int index, char *err, uint32_t errcap)
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

zxva_vk_t *zxva_vk_new(int index, char *err, uint32_t errcap)
{
    if (err && errcap) err[0] = 0;
    zxva_vk_t *g = calloc(1, sizeof *g);
    if (!g) return NULL;
    VkResult r = VK_SUCCESS;
    const char *env = getenv("ZXVA_VK_DEVICE");
    if (index < 0 && env && *env) index = atoi(env);
    g->lib = lib_open();
    if (!g->lib || !(g->vk.GetInstanceProcAddr = lib_gipa(g->lib))) {
        say(err, errcap, "no Vulkan loader (libvulkan)", 0);
        goto fail;
    }
    g->vk.CreateInstance =
        (PFN_vkCreateInstance) g->vk.GetInstanceProcAddr(NULL, "vkCreateInstance");
    VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "zxva";
    app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo ii = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ii.pApplicationInfo = &app;
    if (!g->vk.CreateInstance || (r = g->vk.CreateInstance(&ii, NULL, &g->inst)) != VK_SUCCESS) {
        say(err, errcap, "cannot create a Vulkan 1.1 instance", g->vk.CreateInstance ? r : 0);
        goto fail;
    }
#    define ZXV_LOAD_I(n)                                                                          \
        if (!(g->vk.n = (PFN_vk##n) g->vk.GetInstanceProcAddr(g->inst, "vk" #n))) {                \
            say(err, errcap, "the Vulkan loader lacks vk" #n, 0);                                  \
            goto fail;                                                                             \
        }
    ZXV_INSTANCE_FNS(ZXV_LOAD_I)
    if (pick_device(g, index, err, errcap)) goto fail;
    VkPhysicalDeviceProperties pp;
    g->vk.GetPhysicalDeviceProperties(g->pd, &pp);
    snprintf(g->name, sizeof g->name, "%s", pp.deviceName);
    VkPhysicalDeviceFeatures pf;
    g->vk.GetPhysicalDeviceFeatures(g->pd, &pf);
    if (!pf.shaderInt64) {
        say(err, errcap, "this device has no 64-bit integers in shaders (shaderInt64)", 0);
        goto fail;
    }
    if (pp.limits.maxComputeWorkGroupSize[0] < ZXV_WG ||
        pp.limits.maxComputeWorkGroupInvocations < ZXV_WG) {
        say(err, errcap, "this device's workgroups are smaller than 256", 0);
        goto fail;
    }
    g->vk.GetPhysicalDeviceMemoryProperties(g->pd, &g->memp);
    g->max_range = pp.limits.maxStorageBufferRange;
    g->max_groups = pp.limits.maxComputeWorkGroupCount[0];
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
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qi = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qi.queueFamilyIndex = g->qf;
    qi.queueCount = 1;
    qi.pQueuePriorities = &prio;
    VkPhysicalDeviceFeatures want = {0};
    want.shaderInt64 = VK_TRUE;
    VkDeviceCreateInfo di = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    di.queueCreateInfoCount = 1;
    di.pQueueCreateInfos = &qi;
    di.pEnabledFeatures = &want;
    if ((r = g->vk.CreateDevice(g->pd, &di, NULL, &g->dev)) != VK_SUCCESS) {
        g->dev = VK_NULL_HANDLE;
        say(err, errcap, "cannot create the Vulkan device", r);
        goto fail;
    }
#    define ZXV_LOAD_D(n)                                                                          \
        if (!(g->vk.n = (PFN_vk##n) g->vk.GetDeviceProcAddr(g->dev, "vk" #n))) {                   \
            say(err, errcap, "the Vulkan device lacks vk" #n, 0);                                  \
            goto fail;                                                                             \
        }
    ZXV_DEVICE_FNS(ZXV_LOAD_D)
    g->vk.GetDeviceQueue(g->dev, g->qf, 0, &g->q);

    VkDescriptorSetLayoutBinding lb[ZXV_NB];
    for (uint32_t i = 0; i < ZXV_NB; i++)
        lb[i] = (VkDescriptorSetLayoutBinding){i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
                                               VK_SHADER_STAGE_COMPUTE_BIT, NULL};
    VkDescriptorSetLayoutCreateInfo li = {.sType =
                                              VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    li.bindingCount = ZXV_NB;
    li.pBindings = lb;
    VkPushConstantRange pr = {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(zxv_pc_t)};
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
    VkShaderModuleCreateInfo si = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    si.codeSize = sizeof zxva_vk_spv;
    si.pCode = zxva_vk_spv;
    VkShaderModule sm;
    if (g->vk.CreateShaderModule(g->dev, &si, NULL, &sm)) {
        say(err, errcap, "the kernel's SPIR-V was refused", 0);
        goto fail;
    }
    VkComputePipelineCreateInfo pi = {.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    pi.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pi.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pi.stage.module = sm;
    pi.stage.pName = "main";
    pi.layout = g->pl;
    r = g->vk.CreateComputePipelines(g->dev, VK_NULL_HANDLE, 1, &pi, NULL, &g->pipe);
    g->vk.DestroyShaderModule(g->dev, sm, NULL);
    if (r) {
        say(err, errcap, "the kernel did not build on this device", r);
        goto fail;
    }
    VkDescriptorPoolSize ps = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, ZXV_NB};
    VkDescriptorPoolCreateInfo dpi = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpi.maxSets = 1;
    dpi.poolSizeCount = 1;
    dpi.pPoolSizes = &ps;
    VkDescriptorSetAllocateInfo dai = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dai.descriptorSetCount = 1;
    dai.pSetLayouts = &g->dsl;
    if (g->vk.CreateDescriptorPool(g->dev, &dpi, NULL, &g->dpool) ||
        (dai.descriptorPool = g->dpool, g->vk.AllocateDescriptorSets(g->dev, &dai, &g->ds))) {
        say(err, errcap, "cannot allocate the descriptor set", 0);
        goto fail;
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
    zxva_vk_free(g);
    return NULL;
}

const char *zxva_vk_name(const zxva_vk_t *g)
{
    return g ? g->name : "";
}

double zxva_vk_kernel_ms(const zxva_vk_t *g)
{
    return g ? g->kern_ms : 0;
}

static int vk_stft(zxva_backend_t *be, const zxva_stft_plan_t *p, uint64_t k0, uint32_t nf,
                   int32_t *re, int32_t *im, uint32_t *mag, uint32_t *mask, uint32_t *top)
{
    zxva_vk_t *g = be->ctx;
    if (!g || !p || nf == 0 || nf > g->max_groups || p->N < 2 || p->len > 0xFFFFFFFFull) return -1;
    uint32_t N = p->N, H = N / 2 + 1, mw = zxva_mask_words(N);
    if (put(g, &g->b[B_X], p->x, (size_t) p->len * 4) ||
        put(g, &g->b[B_WIN], p->win, (size_t) p->n * 4) ||
        put(g, &g->b[B_TW], p->tw, (size_t) N * 4) ||
        grow(g, &g->b[B_SCR], (uint64_t) nf * 2 * N * 4) ||
        grow(g, &g->b[B_RE], (uint64_t) nf * H * 4) ||
        grow(g, &g->b[B_IM], (uint64_t) nf * H * 4) ||
        grow(g, &g->b[B_MAG], (uint64_t) nf * H * 4) ||
        grow(g, &g->b[B_MASK], (uint64_t) nf * mw * 4) || grow(g, &g->b[B_TOP], (uint64_t) nf * 4))
        return -1;
    if (g->ds_dirty) {
        VkDescriptorBufferInfo bi[ZXV_NB];
        VkWriteDescriptorSet wr[ZXV_NB];
        for (uint32_t i = 0; i < ZXV_NB; i++) {
            bi[i] = (VkDescriptorBufferInfo){g->b[i].b, 0, VK_WHOLE_SIZE};
            wr[i] = (VkWriteDescriptorSet){.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            wr[i].dstSet = g->ds;
            wr[i].dstBinding = i;
            wr[i].descriptorCount = 1;
            wr[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            wr[i].pBufferInfo = &bi[i];
        }
        g->vk.UpdateDescriptorSets(g->dev, ZXV_NB, wr, 0, NULL);
        g->ds_dirty = false;
    }
    zxv_pc_t pc = {p->n,
                   N,
                   p->L,
                   p->num,
                   p->den,
                   (uint32_t) (uint64_t) p->c0,
                   (uint32_t) ((uint64_t) p->c0 >> 32),
                   (uint32_t) p->len,
                   (uint32_t) (p->len >> 32),
                   (uint32_t) k0,
                   (uint32_t) (k0 >> 32),
                   p->floor_mag,
                   p->rel,
                   nf};
    if (g->vk.ResetCommandBuffer(g->cb, 0)) return -1;
    VkCommandBufferBeginInfo cbi = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    cbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (g->vk.BeginCommandBuffer(g->cb, &cbi)) return -1;
    VkMemoryBarrier mb = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    mb.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    g->vk.CmdPipelineBarrier(g->cb, VK_PIPELINE_STAGE_HOST_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &mb, 0, NULL, 0, NULL);
    if (g->qpool) {
        g->vk.CmdResetQueryPool(g->cb, g->qpool, 0, 2);
        g->vk.CmdWriteTimestamp(g->cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, g->qpool, 0);
    }
    g->vk.CmdBindPipeline(g->cb, VK_PIPELINE_BIND_POINT_COMPUTE, g->pipe);
    g->vk.CmdBindDescriptorSets(g->cb, VK_PIPELINE_BIND_POINT_COMPUTE, g->pl, 0, 1, &g->ds, 0,
                                NULL);
    g->vk.CmdPushConstants(g->cb, g->pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof pc, &pc);
    g->vk.CmdDispatch(g->cb, nf, 1, 1);
    if (g->qpool) g->vk.CmdWriteTimestamp(g->cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, g->qpool, 1);
    mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    g->vk.CmdPipelineBarrier(g->cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &mb, 0, NULL, 0, NULL);
    if (g->vk.EndCommandBuffer(g->cb) || g->vk.ResetFences(g->dev, 1, &g->fence)) return -1;
    VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &g->cb;
    if (g->vk.QueueSubmit(g->q, 1, &si, g->fence) ||
        g->vk.WaitForFences(g->dev, 1, &g->fence, VK_TRUE, UINT64_MAX))
        return -1;
    if (g->qpool) {
        uint64_t ts[2];
        if (g->vk.GetQueryPoolResults(g->dev, g->qpool, 0, 2, sizeof ts, ts, sizeof ts[0],
                                      VK_QUERY_RESULT_64_BIT) == VK_SUCCESS &&
            ts[1] > ts[0])
            g->kern_ms += (double) (ts[1] - ts[0]) * g->ts_ns / 1e6;
    }
    memcpy(re, g->b[B_RE].h, (size_t) nf * H * 4);
    memcpy(im, g->b[B_IM].h, (size_t) nf * H * 4);
    memcpy(mag, g->b[B_MAG].h, (size_t) nf * H * 4);
    memcpy(mask, g->b[B_MASK].h, (size_t) nf * mw * 4);
    memcpy(top, g->b[B_TOP].h, (size_t) nf * 4);
    return 0;
}

void zxva_vk_backend(zxva_vk_t *g, zxva_backend_t *b)
{
    memset(b, 0, sizeof *b);
    b->name = "vulkan";
    b->ctx = g;
    b->stft = vk_stft;
    b->batch = ZXV_BATCH;
}

#endif /* ZXVA_VK_BUILD */
