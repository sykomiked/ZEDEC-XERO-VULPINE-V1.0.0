/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zvt_vk.c — zvt_gpu.h's Vulkan backend: zvt_vk.comp's stages on any Vulkan
 * 1.1 device. The loader is opened at run time (no link to libvulkan); the
 * SPIR-V is made from zvt_vk.comp by gen_zvt_vk_spv.sh and included as
 * ZVT_VK_SPV_H. Without the Vulkan headers or the SPIR-V this file compiles
 * to stubs that return NULL, so every hosted build links. (The pattern of
 * kernel/src/tensor's zt_vk.c, much reduced: four host-visible buffers, one
 * descriptor set, one command buffer, a fence wait per stage.) */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#    define _POSIX_C_SOURCE 200809L
#endif
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "zvt_gpu.h"

#if defined(__has_include)
#    if __has_include(<vulkan/vulkan.h>) && defined(ZVT_VK_SPV_H)
#        define ZVT_VK_BUILD 1
#    endif
#endif

#ifndef ZVT_VK_BUILD

/* ---- stubs: built without the Vulkan headers or the SPIR-V ---- */
zvt_vk_t *zvt_vk_new(int index, char *err, uint32_t errcap)
{
    (void) index;
    if (err && errcap)
        snprintf(
            err, errcap,
            "built without Vulkan (needs the Vulkan headers and glslangValidator at build time)");
    return NULL;
}
void zvt_vk_free(zvt_vk_t *g)
{
    (void) g;
}
const char *zvt_vk_name(const zvt_vk_t *g)
{
    (void) g;
    return "";
}
void zvt_vk_backend(zvt_vk_t *g, zvt_backend_t *out)
{
    (void) g;
    *out = zvt_cpu_backend;
}
void zvt_vk_stats(const zvt_vk_t *g, zvt_gpu_stats_t *out)
{
    (void) g;
    if (out) memset(out, 0, sizeof *out);
}

#else /* ZVT_VK_BUILD */

#    define VK_NO_PROTOTYPES
#    include <vulkan/vulkan.h>
#    ifdef _WIN32
#        include <windows.h>
#    else
#        include <dlfcn.h>
#    endif

#    include ZVT_VK_SPV_H /* zvv_spv_<ZK>[] and zvv_mods[] */

#    define ZVV_NB 4u  /* bindings */
#    define ZVV_NK 6u  /* stages   */
#    define ZVV_WG 64u /* threads per workgroup (zvt_vk.comp) */

#    define ZVV_INSTANCE_FNS(X)                                                                    \
        X(DestroyInstance)                                                                         \
        X(EnumeratePhysicalDevices)                                                                \
        X(GetPhysicalDeviceProperties)                                                             \
        X(GetPhysicalDeviceMemoryProperties)                                                       \
        X(GetPhysicalDeviceQueueFamilyProperties)                                                  \
        X(CreateDevice)                                                                            \
        X(GetDeviceProcAddr)

#    define ZVV_DEVICE_FNS(X)                                                                      \
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
        X(CreateFence)                                                                             \
        X(DestroyFence)                                                                            \
        X(ResetFences)                                                                             \
        X(WaitForFences)                                                                           \
        X(QueueSubmit)

#    define ZVV_DECL(n) PFN_vk##n n;
typedef struct {
    PFN_vkGetInstanceProcAddr GetInstanceProcAddr;
    PFN_vkCreateInstance CreateInstance;
    ZVV_INSTANCE_FNS(ZVV_DECL)
    ZVV_DEVICE_FNS(ZVV_DECL)
} zvv_api_t;

typedef struct {
    VkBuffer b;
    VkDeviceMemory m;
    uint8_t *h;
    uint64_t cap;
} vbuf_t;

struct zvt_vk {
    void *lib;
    zvv_api_t vk;
    VkInstance inst;
    VkPhysicalDevice pd;
    VkDevice dev;
    VkQueue q;
    uint32_t qf;
    VkPhysicalDeviceMemoryProperties memp;
    uint32_t max_groups;
    uint64_t max_range;
    VkDescriptorSetLayout dsl;
    VkPipelineLayout pl;
    VkPipeline pipe[ZVV_NK];
    VkDescriptorPool dpool;
    VkDescriptorSet ds;
    VkCommandPool cpool;
    VkCommandBuffer cb;
    VkFence fence;
    vbuf_t buf[ZVV_NB];
    char name[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE];
    zvt_gpu_stats_t st;
};

static uint64_t now_ns(void)
{
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
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
    const char *env = getenv("ZVT_VK_LIB");
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

static int mem_type(const zvt_vk_t *g, uint32_t bits, VkMemoryPropertyFlags need)
{
    for (uint32_t i = 0; i < g->memp.memoryTypeCount; i++)
        if ((bits & (1u << i)) && (g->memp.memoryTypes[i].propertyFlags & need) == need)
            return (int) i;
    return -1;
}

static void vbuf_free(zvt_vk_t *g, vbuf_t *v)
{
    if (v->h) g->vk.UnmapMemory(g->dev, v->m);
    if (v->b) g->vk.DestroyBuffer(g->dev, v->b, NULL);
    if (v->m) g->vk.FreeMemory(g->dev, v->m, NULL);
    memset(v, 0, sizeof *v);
}

/* binding i: a host-visible, coherent buffer of at least need bytes */
static int grow(zvt_vk_t *g, uint32_t i, uint64_t need)
{
    vbuf_t *v = &g->buf[i];
    need = (need + 15u) & ~(uint64_t) 15u;
    if (need < 64) need = 64;
    if (v->cap >= need && v->b) return 0;
    if (need > g->max_range) return -1;
    vbuf_free(g, v);
    uint64_t size = need + need / 4u + 4096u;
    if (size > g->max_range) size = need;
    VkBufferCreateInfo bi = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = size;
    bi.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (g->vk.CreateBuffer(g->dev, &bi, NULL, &v->b)) return -1;
    VkMemoryRequirements mr;
    g->vk.GetBufferMemoryRequirements(g->dev, v->b, &mr);
    static const VkMemoryPropertyFlags prefs[] = {
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
            VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT};
    for (uint32_t p = 0; p < 2; p++) {
        int t = mem_type(g, mr.memoryTypeBits, prefs[p]);
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
        v->h = (uint8_t *) h;
        v->cap = size;
        return 0;
    }
    vbuf_free(g, v);
    return -1;
}

static int make_pipeline(zvt_vk_t *g, const uint32_t *code, size_t bytes, VkPipeline *out)
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

static int pick_device(zvt_vk_t *g, int index, char *err, uint32_t errcap)
{
    uint32_t n = 0;
    if (g->vk.EnumeratePhysicalDevices(g->inst, &n, NULL) || !n) {
        say(err, errcap, "no Vulkan device", 0);
        return -1;
    }
    VkPhysicalDevice *pd = (VkPhysicalDevice *) calloc(n, sizeof *pd);
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

void zvt_vk_free(zvt_vk_t *g)
{
    if (!g) return;
    if (g->dev) {
        for (uint32_t i = 0; i < ZVV_NB; i++) vbuf_free(g, &g->buf[i]);
        for (uint32_t k = 0; k < ZVV_NK; k++)
            if (g->pipe[k]) g->vk.DestroyPipeline(g->dev, g->pipe[k], NULL);
        if (g->dpool) g->vk.DestroyDescriptorPool(g->dev, g->dpool, NULL);
        if (g->pl) g->vk.DestroyPipelineLayout(g->dev, g->pl, NULL);
        if (g->dsl) g->vk.DestroyDescriptorSetLayout(g->dev, g->dsl, NULL);
        if (g->fence) g->vk.DestroyFence(g->dev, g->fence, NULL);
        if (g->cpool) g->vk.DestroyCommandPool(g->dev, g->cpool, NULL);
        g->vk.DestroyDevice(g->dev, NULL);
    }
    if (g->inst) g->vk.DestroyInstance(g->inst, NULL);
    lib_close(g->lib);
    free(g);
}

zvt_vk_t *zvt_vk_new(int index, char *err, uint32_t errcap)
{
    zvt_vk_t *g = (zvt_vk_t *) calloc(1, sizeof *g);
    if (!g) return NULL;
    VkResult r = VK_SUCCESS;
    const char *env = getenv("ZVT_VK_DEVICE");
    if (index < 0 && env && *env) index = atoi(env);
    g->lib = lib_open();
    if (!g->lib || !(g->vk.GetInstanceProcAddr = lib_gipa(g->lib))) {
        say(err, errcap, "no Vulkan loader (libvulkan)", 0);
        goto fail;
    }
    g->vk.CreateInstance =
        (PFN_vkCreateInstance) g->vk.GetInstanceProcAddr(NULL, "vkCreateInstance");
    VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "zxv-vtrace";
    app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo ii = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ii.pApplicationInfo = &app;
    if (!g->vk.CreateInstance || (r = g->vk.CreateInstance(&ii, NULL, &g->inst)) != VK_SUCCESS) {
        g->inst = VK_NULL_HANDLE;
        say(err, errcap, "cannot create a Vulkan 1.1 instance", g->vk.CreateInstance ? r : 0);
        goto fail;
    }
#    define ZVV_LOAD_I(n)                                                                          \
        if (!(g->vk.n = (PFN_vk##n) g->vk.GetInstanceProcAddr(g->inst, "vk" #n))) {                \
            say(err, errcap, "the Vulkan loader lacks vk" #n, 0);                                  \
            goto fail;                                                                             \
        }
    ZVV_INSTANCE_FNS(ZVV_LOAD_I)
    if (pick_device(g, index, err, errcap)) goto fail;
    VkPhysicalDeviceProperties pp;
    g->vk.GetPhysicalDeviceProperties(g->pd, &pp);
    snprintf(g->name, sizeof g->name, "%s", pp.deviceName);
    g->vk.GetPhysicalDeviceMemoryProperties(g->pd, &g->memp);
    g->max_range = pp.limits.maxStorageBufferRange;
    g->max_groups = pp.limits.maxComputeWorkGroupCount[0] < pp.limits.maxComputeWorkGroupCount[1]
                        ? pp.limits.maxComputeWorkGroupCount[0]
                        : pp.limits.maxComputeWorkGroupCount[1];
    if (g->max_groups > 65535u) g->max_groups = 65535u;
    uint32_t nq = 0;
    g->vk.GetPhysicalDeviceQueueFamilyProperties(g->pd, &nq, NULL);
    VkQueueFamilyProperties *qp = (VkQueueFamilyProperties *) calloc(nq ? nq : 1, sizeof *qp);
    if (!qp) goto fail;
    g->vk.GetPhysicalDeviceQueueFamilyProperties(g->pd, &nq, qp);
    g->qf = UINT32_MAX;
    for (uint32_t i = 0; i < nq && g->qf == UINT32_MAX; i++)
        if (qp[i].queueFlags & VK_QUEUE_COMPUTE_BIT) g->qf = i;
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
    VkDeviceCreateInfo di = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    di.queueCreateInfoCount = 1;
    di.pQueueCreateInfos = &qi;
    if ((r = g->vk.CreateDevice(g->pd, &di, NULL, &g->dev)) != VK_SUCCESS) {
        g->dev = VK_NULL_HANDLE;
        say(err, errcap, "cannot create the Vulkan device", r);
        goto fail;
    }
#    define ZVV_LOAD_D(n)                                                                          \
        if (!(g->vk.n = (PFN_vk##n) g->vk.GetDeviceProcAddr(g->dev, "vk" #n))) {                   \
            say(err, errcap, "the Vulkan device lacks vk" #n, 0);                                  \
            goto fail;                                                                             \
        }
    ZVV_DEVICE_FNS(ZVV_LOAD_D)
    g->vk.GetDeviceQueue(g->dev, g->qf, 0, &g->q);

    VkDescriptorSetLayoutBinding lb[ZVV_NB];
    for (uint32_t i = 0; i < ZVV_NB; i++)
        lb[i] = (VkDescriptorSetLayoutBinding){i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
                                               VK_SHADER_STAGE_COMPUTE_BIT, NULL};
    VkDescriptorSetLayoutCreateInfo li = {.sType =
                                              VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    li.bindingCount = ZVV_NB;
    li.pBindings = lb;
    VkPushConstantRange pr = {VK_SHADER_STAGE_COMPUTE_BIT, 0, 8u * 4u};
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
    for (uint32_t i = 0; i < sizeof zvv_mods / sizeof zvv_mods[0]; i++)
        if (zvv_mods[i].zk >= ZVV_NK ||
            make_pipeline(g, zvv_mods[i].code, zvv_mods[i].bytes, &g->pipe[zvv_mods[i].zk])) {
            say(err, errcap, "a Vulkan kernel did not build", 0);
            goto fail;
        }
    VkDescriptorPoolSize ps = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, ZVV_NB};
    VkDescriptorPoolCreateInfo dpi = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpi.maxSets = 1;
    dpi.poolSizeCount = 1;
    dpi.pPoolSizes = &ps;
    VkDescriptorSetAllocateInfo dai = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dai.descriptorSetCount = 1;
    dai.pSetLayouts = &g->dsl;
    VkCommandPoolCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    ci.queueFamilyIndex = g->qf;
    VkCommandBufferAllocateInfo ca = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ca.commandBufferCount = 1;
    VkFenceCreateInfo fi = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if (g->vk.CreateDescriptorPool(g->dev, &dpi, NULL, &g->dpool) ||
        (dai.descriptorPool = g->dpool, g->vk.AllocateDescriptorSets(g->dev, &dai, &g->ds)) ||
        g->vk.CreateCommandPool(g->dev, &ci, NULL, &g->cpool) ||
        (ca.commandPool = g->cpool, g->vk.AllocateCommandBuffers(g->dev, &ca, &g->cb)) ||
        g->vk.CreateFence(g->dev, &fi, NULL, &g->fence)) {
        say(err, errcap, "cannot create the command buffer", 0);
        goto fail;
    }
    for (uint32_t i = 0; i < ZVV_NB; i++)
        if (grow(g, i, 4096)) {
            say(err, errcap, "no host-visible memory for the buffers", 0);
            goto fail;
        }
    return g;
fail:
    zvt_vk_free(g);
    return NULL;
}

const char *zvt_vk_name(const zvt_vk_t *g)
{
    return g ? g->name : "";
}

void zvt_vk_stats(const zvt_vk_t *g, zvt_gpu_stats_t *out)
{
    if (!out) return;
    if (g)
        *out = g->st;
    else
        memset(out, 0, sizeof *out);
}

/* run stage k over count threads with push constants p[0..6]; the buffers
 * already hold the inputs. 0 on success. */
static int run(zvt_vk_t *g, uint32_t k, uint32_t count, const uint32_t p[7])
{
    if (!count) return 0;
    uint32_t groups = (count + ZVV_WG - 1u) / ZVV_WG;
    uint32_t gx = groups < g->max_groups ? groups : g->max_groups, gy = (groups + gx - 1u) / gx;
    if (gy > g->max_groups) return -1;
    uint32_t pc[8];
    for (int i = 0; i < 7; i++) pc[i] = p[i];
    pc[7] = gx;
    VkDescriptorBufferInfo bi[ZVV_NB];
    VkWriteDescriptorSet wr[ZVV_NB];
    for (uint32_t i = 0; i < ZVV_NB; i++) {
        bi[i] = (VkDescriptorBufferInfo){g->buf[i].b, 0, VK_WHOLE_SIZE};
        wr[i] = (VkWriteDescriptorSet){.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        wr[i].dstSet = g->ds;
        wr[i].dstBinding = i;
        wr[i].descriptorCount = 1;
        wr[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        wr[i].pBufferInfo = &bi[i];
    }
    g->vk.UpdateDescriptorSets(g->dev, ZVV_NB, wr, 0, NULL);
    if (g->vk.ResetCommandBuffer(g->cb, 0)) return -1;
    VkCommandBufferBeginInfo cbi = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    cbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (g->vk.BeginCommandBuffer(g->cb, &cbi)) return -1;
    VkMemoryBarrier mb = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    mb.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    g->vk.CmdPipelineBarrier(g->cb, VK_PIPELINE_STAGE_HOST_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &mb, 0, NULL, 0, NULL);
    g->vk.CmdBindPipeline(g->cb, VK_PIPELINE_BIND_POINT_COMPUTE, g->pipe[k]);
    g->vk.CmdBindDescriptorSets(g->cb, VK_PIPELINE_BIND_POINT_COMPUTE, g->pl, 0, 1, &g->ds, 0,
                                NULL);
    g->vk.CmdPushConstants(g->cb, g->pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof pc, pc);
    g->vk.CmdDispatch(g->cb, gx, gy, 1);
    mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    g->vk.CmdPipelineBarrier(g->cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &mb, 0, NULL, 0, NULL);
    if (g->vk.EndCommandBuffer(g->cb)) return -1;
    if (g->vk.ResetFences(g->dev, 1, &g->fence)) return -1;
    VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &g->cb;
    if (g->vk.QueueSubmit(g->q, 1, &si, g->fence)) return -1;
    if (g->vk.WaitForFences(g->dev, 1, &g->fence, VK_TRUE, UINT64_MAX)) return -1;
    return 0;
}

static int put(zvt_vk_t *g, uint32_t i, const void *src, uint64_t n)
{
    if (grow(g, i, n)) return -1;
    memcpy(g->buf[i].h, src, (size_t) n);
    g->st.bytes_in += n;
    return 0;
}

static void get_words(zvt_vk_t *g, uint32_t i, uint32_t *dst, uint64_t words)
{
    memcpy(dst, g->buf[i].h, (size_t) words * 4u);
    g->st.bytes_out += words * 4u;
}

static void get_bytes(zvt_vk_t *g, uint32_t i, uint8_t *dst, uint64_t n) /* a word an element */
{
    const uint32_t *s = (const uint32_t *) g->buf[i].h;
    for (uint64_t k = 0; k < n; k++) dst[k] = (uint8_t) s[k];
    g->st.bytes_out += n * 4u;
}

static int finish(zvt_vk_t *g, uint64_t t0, int rc)
{
    g->st.calls++;
    g->st.ns += now_ns() - t0;
    if (rc) g->st.fallbacks++;
    return rc;
}

static int vk_smooth(void *ctx, const uint32_t *in, uint32_t *out, uint32_t w, uint32_t h,
                     uint32_t thr)
{
    zvt_vk_t *g = (zvt_vk_t *) ctx;
    uint64_t t0 = now_ns(), n = (uint64_t) w * h;
    uint32_t p[7] = {w, h, thr, 0, 0, 0, 0};
    int rc = put(g, 0, in, n * 4u) || grow(g, 1, n * 4u) || run(g, 0, (uint32_t) n, p) ? -1 : 0;
    if (!rc) get_words(g, 1, out, n);
    return finish(g, t0, rc);
}

static int vk_quant(void *ctx, const uint32_t *px, uint32_t n, const uint32_t *pal, uint32_t npal,
                    uint8_t *idx)
{
    zvt_vk_t *g = (zvt_vk_t *) ctx;
    uint64_t t0 = now_ns();
    uint32_t p[7] = {n, npal, 0, 0, 0, 0, 0};
    int rc = put(g, 0, px, (uint64_t) n * 4u) || put(g, 1, pal, (uint64_t) npal * 4u) ||
                     grow(g, 2, (uint64_t) n * 4u) || run(g, 1, n, p)
                 ? -1
                 : 0;
    if (!rc) get_bytes(g, 2, idx, n);
    return finish(g, t0, rc);
}

static int vk_edges(void *ctx, const uint8_t *idx, uint32_t w, uint32_t h, uint8_t *edge)
{
    zvt_vk_t *g = (zvt_vk_t *) ctx;
    uint64_t t0 = now_ns(), n = (uint64_t) w * h;
    uint32_t p[7] = {w, h, 0, 0, 0, 0, 0};
    int rc = grow(g, 0, n + 4u) ? -1 : 0;
    if (!rc) {
        memset(g->buf[0].h, 0, (size_t) ((n + 4u) & ~(uint64_t) 3u));
        rc = put(g, 0, idx, n) || grow(g, 1, n * 4u) || run(g, 2, (uint32_t) n, p) ? -1 : 0;
    }
    if (!rc) get_bytes(g, 1, edge, n);
    return finish(g, t0, rc);
}

static int vk_metric(void *ctx, const uint32_t *a, const uint32_t *b, uint32_t w, uint32_t h,
                     uint32_t wx, uint32_t wy, uint32_t step, uint32_t *row_sse, uint32_t *win)
{
    zvt_vk_t *g = (zvt_vk_t *) ctx;
    uint64_t t0 = now_ns(), n = (uint64_t) w * h;
    uint32_t nwx = (w - wx) / step + 1u, nwy = (h - wy) / step + 1u, nwin = nwx * nwy;
    uint32_t p1[7] = {w, h, 0, 0, 0, 0, 0}, p2[7] = {w, wx, wy, step, nwx, nwin, 0};
    int rc = put(g, 0, a, n * 4u) || put(g, 1, b, n * 4u) ||
                     grow(g, 2, (uint64_t) (h > 5u * nwin ? h : 5u * nwin) * 4u) || run(g, 3, h, p1)
                 ? -1
                 : 0;
    if (!rc) {
        get_words(g, 2, row_sse, h);
        rc = run(g, 4, nwin, p2);
    }
    if (!rc) get_words(g, 2, win, 5ull * nwin);
    return finish(g, t0, rc);
}

static int vk_diff(void *ctx, const uint32_t *a, const uint32_t *b, uint32_t w, uint32_t h,
                   uint32_t thr, uint8_t *mask, uint32_t *row_count)
{
    zvt_vk_t *g = (zvt_vk_t *) ctx;
    uint64_t t0 = now_ns(), n = (uint64_t) w * h;
    uint32_t p[7] = {w, h, thr, 0, 0, 0, 0};
    int rc = put(g, 0, a, n * 4u) || put(g, 1, b, n * 4u) || grow(g, 2, n * 4u) ||
                     grow(g, 3, (uint64_t) h * 4u) || run(g, 5, h, p)
                 ? -1
                 : 0;
    if (!rc) {
        get_bytes(g, 2, mask, n);
        get_words(g, 3, row_count, h);
    }
    return finish(g, t0, rc);
}

void zvt_vk_backend(zvt_vk_t *g, zvt_backend_t *out)
{
    zvt_backend_t b = {"vulkan", g, vk_smooth, vk_quant, vk_edges, vk_metric, vk_diff};
    *out = b;
}

#endif /* ZVT_VK_BUILD */
