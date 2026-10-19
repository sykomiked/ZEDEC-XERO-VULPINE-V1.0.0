/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_present_vulkan.c — the Vulkan present layer: Linux and Windows with a
 * GPU.
 *
 * zui draws on the CPU, so the GPU's whole job is to move pixels: no shaders,
 * no pipeline, only transfer commands. Each present
 *
 *   1. copies the damaged rectangles of the app's surface into a
 *      host-visible staging buffer laid out like the frame (P1),
 *   2. vkCmdCopyBufferToImage's those rectangles into a persistent,
 *      frame-sized image (the last frame stays there, because swapchain
 *      images come back with old or undefined contents),
 *   3. vkCmdCopyImage's the persistent image into the acquired swapchain
 *      image and presents it.
 *
 * The swapchain is BGRA8 where offered, which is XRGB8888's memory order, so
 * the bytes are copied unchanged (P2); on an RGBA8-only surface the red and
 * blue bytes are swapped while staging. The fourth byte is written as 0xFF
 * so a compositor that honours alpha does not see a clear window. A swapchain
 * that goes VK_ERROR_OUT_OF_DATE_KHR (resize, display change) is recreated.
 *
 * The loader is opened at run time (libvulkan.so.1 / vulkan-1.dll) and every
 * function comes from vkGetInstanceProcAddr, so the program starts on a
 * machine without Vulkan and open() returns ZUI_PRESENT_E_UNAVAILABLE.
 * A CPU-only Vulkan device (lavapipe, SwiftShader) is skipped, because the
 * X11 layer is faster there, unless ZXV_VULKAN_CPU=1 is set (the tests use
 * it).
 *
 * Window and input: zui_x11_window.c on Linux (VK_KHR_xlib_surface),
 * zui_win32_window.c on Windows (VK_KHR_win32_surface). Only the headers are
 * needed to build: Vulkan-Headers' vulkan/vulkan.h.
 */
#if defined(_WIN32)
#    include "zui_win32_window.h" /* first: it sets up <windows.h> */
#    define VK_USE_PLATFORM_WIN32_KHR
#else
#    define _GNU_SOURCE
#    include <dlfcn.h>
#    define VK_USE_PLATFORM_XLIB_KHR
#    include "zui_x11_window.h"
#endif
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include <stdlib.h>
#include <string.h>
#include "zui_present.h"

#define VK_MAX_IMAGES 16u

/* ---- the functions we use, loaded at run time ---------------------------- */

#define VK_GLOBAL_FNS(X)                                                                           \
    X(vkCreateInstance)                                                                            \
    X(vkEnumerateInstanceExtensionProperties)

#if defined(_WIN32)
#    define VK_PLATFORM_FNS(X)      X(vkCreateWin32SurfaceKHR)
#    define VK_PLATFORM_SURFACE_EXT VK_KHR_WIN32_SURFACE_EXTENSION_NAME
#else
#    define VK_PLATFORM_FNS(X)      X(vkCreateXlibSurfaceKHR)
#    define VK_PLATFORM_SURFACE_EXT VK_KHR_XLIB_SURFACE_EXTENSION_NAME
#endif

#define VK_INSTANCE_FNS(X)                                                                         \
    X(vkDestroyInstance)                                                                           \
    X(vkEnumeratePhysicalDevices)                                                                  \
    X(vkGetPhysicalDeviceProperties)                                                               \
    X(vkGetPhysicalDeviceQueueFamilyProperties)                                                    \
    X(vkGetPhysicalDeviceMemoryProperties)                                                         \
    X(vkEnumerateDeviceExtensionProperties)                                                        \
    X(vkCreateDevice)                                                                              \
    X(vkGetDeviceProcAddr)                                                                         \
    X(vkDestroySurfaceKHR)                                                                         \
    X(vkGetPhysicalDeviceSurfaceSupportKHR)                                                        \
    X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR)                                                   \
    X(vkGetPhysicalDeviceSurfaceFormatsKHR)                                                        \
    VK_PLATFORM_FNS(X)

#define VK_DEVICE_FNS(X)                                                                           \
    X(vkDestroyDevice)                                                                             \
    X(vkGetDeviceQueue)                                                                            \
    X(vkDeviceWaitIdle)                                                                            \
    X(vkCreateSwapchainKHR)                                                                        \
    X(vkDestroySwapchainKHR)                                                                       \
    X(vkGetSwapchainImagesKHR)                                                                     \
    X(vkAcquireNextImageKHR)                                                                       \
    X(vkQueuePresentKHR)                                                                           \
    X(vkQueueSubmit)                                                                               \
    X(vkCreateCommandPool)                                                                         \
    X(vkDestroyCommandPool)                                                                        \
    X(vkAllocateCommandBuffers)                                                                    \
    X(vkBeginCommandBuffer)                                                                        \
    X(vkEndCommandBuffer)                                                                          \
    X(vkCmdPipelineBarrier)                                                                        \
    X(vkCmdCopyBufferToImage)                                                                      \
    X(vkCmdCopyImage)                                                                              \
    X(vkCmdClearColorImage)                                                                        \
    X(vkCreateBuffer)                                                                              \
    X(vkDestroyBuffer)                                                                             \
    X(vkGetBufferMemoryRequirements)                                                               \
    X(vkBindBufferMemory)                                                                          \
    X(vkCreateImage)                                                                               \
    X(vkDestroyImage)                                                                              \
    X(vkGetImageMemoryRequirements)                                                                \
    X(vkBindImageMemory)                                                                           \
    X(vkAllocateMemory)                                                                            \
    X(vkFreeMemory)                                                                                \
    X(vkMapMemory)                                                                                 \
    X(vkUnmapMemory)                                                                               \
    X(vkCreateSemaphore)                                                                           \
    X(vkDestroySemaphore)                                                                          \
    X(vkCreateFence)                                                                               \
    X(vkDestroyFence)                                                                              \
    X(vkWaitForFences)                                                                             \
    X(vkResetFences)

typedef struct {
#if defined(_WIN32)
    zui_win32_window_t win;
    HMODULE lib;
#else
    zui_x11_window_t win;
    void *lib;
#endif
    PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;
#define VK_DECL(fn) PFN_##fn fn;
    VK_GLOBAL_FNS(VK_DECL)
    VK_INSTANCE_FNS(VK_DECL)
    VK_DEVICE_FNS(VK_DECL)
#undef VK_DECL

    VkInstance inst;
    VkSurfaceKHR surf;
    VkPhysicalDevice phys;
    VkPhysicalDeviceMemoryProperties memp;
    VkDevice dev;
    bool dev_fns; /* every device function loaded */
    uint32_t qfam;
    VkQueue queue;
    VkCommandPool pool;
    VkCommandBuffer cmd;
    VkFence fence;        /* the last submit; waited before staging again */
    VkSemaphore acquired; /* acquire -> copy                             */

    /* swapchain */
    VkSwapchainKHR sc;
    VkFormat fmt;
    bool swap_rb; /* the swapchain is RGBA: swap red and blue */
    VkExtent2D ext;
    uint32_t nimg;
    VkImage img[VK_MAX_IMAGES];
    VkSemaphore copied[VK_MAX_IMAGES]; /* copy -> present, per image */
    bool stale;                        /* recreate before the next acquire */

    /* the persistent frame and its staging buffer */
    VkImage frame;
    VkDeviceMemory frame_mem;
    VkFormat frame_fmt;
    int32_t fw, fh;
    bool frame_undefined; /* contents never written: next upload is whole */
    VkBuffer stage;
    VkDeviceMemory stage_mem;
    uint8_t *stage_map;
} vk_state_t;

/* ---- loader -------------------------------------------------------------- */

static bool load_loader(vk_state_t *st)
{
    void *sym = NULL;
#if defined(_WIN32)
    st->lib = LoadLibraryA("vulkan-1.dll");
    if (!st->lib) return false;
    FARPROC fp = GetProcAddress(st->lib, "vkGetInstanceProcAddr");
    memcpy(&sym, &fp, sizeof sym);
#else
    st->lib = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!st->lib) st->lib = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
    if (!st->lib) return false;
    sym = dlsym(st->lib, "vkGetInstanceProcAddr");
#endif
    if (!sym) return false;
    memcpy(&st->vkGetInstanceProcAddr, &sym, sizeof sym);
    return true;
}

static void unload_loader(vk_state_t *st)
{
    if (!st->lib) return;
#if defined(_WIN32)
    FreeLibrary(st->lib);
#else
    dlclose(st->lib);
#endif
    st->lib = NULL;
}

static bool load_global(vk_state_t *st)
{
#define VK_LOAD(fn)                                                                                \
    st->fn = (PFN_##fn) st->vkGetInstanceProcAddr(VK_NULL_HANDLE, #fn);                            \
    if (!st->fn) return false;
    VK_GLOBAL_FNS(VK_LOAD)
#undef VK_LOAD
    return true;
}

static bool load_instance(vk_state_t *st)
{
#define VK_LOAD(fn)                                                                                \
    st->fn = (PFN_##fn) st->vkGetInstanceProcAddr(st->inst, #fn);                                  \
    if (!st->fn) return false;
    VK_INSTANCE_FNS(VK_LOAD)
#undef VK_LOAD
    return true;
}

static bool load_device(vk_state_t *st)
{
#define VK_LOAD(fn)                                                                                \
    st->fn = (PFN_##fn) st->vkGetDeviceProcAddr(st->dev, #fn);                                     \
    if (!st->fn) return false;
    VK_DEVICE_FNS(VK_LOAD)
#undef VK_LOAD
    return true;
}

/* ---- window glue --------------------------------------------------------- */

static int win_open(vk_state_t *st, zui_screen_t *scr, const char *title, int32_t w, int32_t h)
{
#if defined(_WIN32)
    return zui_win32_open(&st->win, scr, title, w, h);
#else
    return zui_x11_open(&st->win, scr, title, w, h);
#endif
}

static void win_close(vk_state_t *st)
{
#if defined(_WIN32)
    zui_win32_close(&st->win);
#else
    zui_x11_close(&st->win);
#endif
}

static VkResult make_surface(vk_state_t *st)
{
#if defined(_WIN32)
    VkWin32SurfaceCreateInfoKHR ci = {.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
                                      .hinstance = st->win.inst,
                                      .hwnd = st->win.hwnd};
    return st->vkCreateWin32SurfaceKHR(st->inst, &ci, NULL, &st->surf);
#else
    VkXlibSurfaceCreateInfoKHR ci = {.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR,
                                     .dpy = st->win.dpy,
                                     .window = st->win.win};
    return st->vkCreateXlibSurfaceKHR(st->inst, &ci, NULL, &st->surf);
#endif
}

/* ---- instance, device ---------------------------------------------------- */

static bool has_ext(const VkExtensionProperties *e, uint32_t n, const char *name)
{
    for (uint32_t i = 0; i < n; i++)
        if (!strcmp(e[i].extensionName, name)) return true;
    return false;
}

static bool make_instance(vk_state_t *st)
{
    uint32_t n = 0;
    if (st->vkEnumerateInstanceExtensionProperties(NULL, &n, NULL) != VK_SUCCESS || !n)
        return false;
    VkExtensionProperties *e = calloc(n, sizeof *e);
    if (!e) return false;
    bool ok = st->vkEnumerateInstanceExtensionProperties(NULL, &n, e) == VK_SUCCESS &&
              has_ext(e, n, VK_KHR_SURFACE_EXTENSION_NAME) &&
              has_ext(e, n, VK_PLATFORM_SURFACE_EXT);
    free(e);
    if (!ok) return false;

    const char *exts[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_PLATFORM_SURFACE_EXT};
    VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                             .pApplicationName = "zxv",
                             .pEngineName = "zui",
                             .apiVersion = VK_API_VERSION_1_0};
    VkInstanceCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                               .pApplicationInfo = &app,
                               .enabledExtensionCount = 2,
                               .ppEnabledExtensionNames = exts};
    return st->vkCreateInstance(&ci, NULL, &st->inst) == VK_SUCCESS;
}

static int device_rank(VkPhysicalDeviceType t)
{
    switch (t) {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
        return 4;
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
        return 3;
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
        return 2;
    case VK_PHYSICAL_DEVICE_TYPE_OTHER:
        return 1;
    default:
        return 0; /* CPU */
    }
}

/* A queue family that can copy and present to our surface, or UINT32_MAX. */
static uint32_t pick_queue(vk_state_t *st, VkPhysicalDevice pd)
{
    uint32_t n = 0;
    st->vkGetPhysicalDeviceQueueFamilyProperties(pd, &n, NULL);
    if (!n) return UINT32_MAX;
    VkQueueFamilyProperties *f = calloc(n, sizeof *f);
    if (!f) return UINT32_MAX;
    st->vkGetPhysicalDeviceQueueFamilyProperties(pd, &n, f);
    uint32_t pick = UINT32_MAX;
    for (uint32_t i = 0; i < n && pick == UINT32_MAX; i++) {
        VkQueueFlags fl = f[i].queueFlags;
        if (!f[i].queueCount ||
            !(fl & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT)))
            continue;
        VkBool32 present = VK_FALSE;
        if (st->vkGetPhysicalDeviceSurfaceSupportKHR(pd, i, st->surf, &present) == VK_SUCCESS &&
            present)
            pick = i;
    }
    free(f);
    return pick;
}

static bool has_swapchain_ext(vk_state_t *st, VkPhysicalDevice pd)
{
    uint32_t n = 0;
    if (st->vkEnumerateDeviceExtensionProperties(pd, NULL, &n, NULL) != VK_SUCCESS || !n)
        return false;
    VkExtensionProperties *e = calloc(n, sizeof *e);
    if (!e) return false;
    bool ok = st->vkEnumerateDeviceExtensionProperties(pd, NULL, &n, e) == VK_SUCCESS &&
              has_ext(e, n, VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    free(e);
    return ok;
}

static bool make_device(vk_state_t *st)
{
    uint32_t n = 0;
    if (st->vkEnumeratePhysicalDevices(st->inst, &n, NULL) != VK_SUCCESS || !n) return false;
    VkPhysicalDevice *pd = calloc(n, sizeof *pd);
    if (!pd) return false;
    if (st->vkEnumeratePhysicalDevices(st->inst, &n, pd) < 0) n = 0;
    const char *cpu_ok = getenv("ZXV_VULKAN_CPU");
    bool allow_cpu = cpu_ok && cpu_ok[0] == '1';
    int best = -1;
    for (uint32_t i = 0; i < n; i++) {
        VkPhysicalDeviceProperties pr;
        st->vkGetPhysicalDeviceProperties(pd[i], &pr);
        int rank = device_rank(pr.deviceType);
        if (rank == 0 && !allow_cpu) continue;
        if (rank <= best || !has_swapchain_ext(st, pd[i])) continue;
        uint32_t q = pick_queue(st, pd[i]);
        if (q == UINT32_MAX) continue;
        best = rank;
        st->phys = pd[i];
        st->qfam = q;
    }
    free(pd);
    if (best < 0) return false;

    st->vkGetPhysicalDeviceMemoryProperties(st->phys, &st->memp);
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                   .queueFamilyIndex = st->qfam,
                                   .queueCount = 1,
                                   .pQueuePriorities = &prio};
    const char *exts[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo dci = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                              .queueCreateInfoCount = 1,
                              .pQueueCreateInfos = &qci,
                              .enabledExtensionCount = 1,
                              .ppEnabledExtensionNames = exts};
    if (st->vkCreateDevice(st->phys, &dci, NULL, &st->dev) != VK_SUCCESS) return false;
    if (!load_device(st)) return false;
    st->dev_fns = true;
    st->vkGetDeviceQueue(st->dev, st->qfam, 0, &st->queue);

    VkCommandPoolCreateInfo pci = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                                   .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
                                   .queueFamilyIndex = st->qfam};
    if (st->vkCreateCommandPool(st->dev, &pci, NULL, &st->pool) != VK_SUCCESS) return false;
    VkCommandBufferAllocateInfo cai = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                                       .commandPool = st->pool,
                                       .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                       .commandBufferCount = 1};
    if (st->vkAllocateCommandBuffers(st->dev, &cai, &st->cmd) != VK_SUCCESS) return false;
    VkFenceCreateInfo fci = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                             .flags = VK_FENCE_CREATE_SIGNALED_BIT};
    if (st->vkCreateFence(st->dev, &fci, NULL, &st->fence) != VK_SUCCESS) return false;
    VkSemaphoreCreateInfo sci = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    return st->vkCreateSemaphore(st->dev, &sci, NULL, &st->acquired) == VK_SUCCESS;
}

static uint32_t find_memory(vk_state_t *st, uint32_t bits, VkMemoryPropertyFlags want)
{
    for (uint32_t i = 0; i < st->memp.memoryTypeCount; i++)
        if ((bits & (1u << i)) && (st->memp.memoryTypes[i].propertyFlags & want) == want) return i;
    return UINT32_MAX;
}

/* ---- swapchain ----------------------------------------------------------- */

static void destroy_swapchain_parts(vk_state_t *st)
{
    for (uint32_t i = 0; i < VK_MAX_IMAGES; i++) {
        if (st->copied[i]) st->vkDestroySemaphore(st->dev, st->copied[i], NULL);
        st->copied[i] = VK_NULL_HANDLE;
        st->img[i] = VK_NULL_HANDLE;
    }
    st->nimg = 0;
}

/* (Re)create the swapchain for the window's current size. Returns false on
 * an error; a zero-sized (minimised) window leaves st->sc null, which is not
 * an error: nothing is shown until it has a size again. */
static bool make_swapchain(vk_state_t *st, uint32_t want_w, uint32_t want_h)
{
    if (st->dev) st->vkDeviceWaitIdle(st->dev);
    VkSurfaceCapabilitiesKHR caps;
    if (st->vkGetPhysicalDeviceSurfaceCapabilitiesKHR(st->phys, st->surf, &caps) != VK_SUCCESS)
        return false;
    if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT)) return false;

    VkExtent2D ext = caps.currentExtent;
    if (ext.width == UINT32_MAX) {
        ext.width = want_w;
        ext.height = want_h;
        if (ext.width < caps.minImageExtent.width) ext.width = caps.minImageExtent.width;
        if (ext.height < caps.minImageExtent.height) ext.height = caps.minImageExtent.height;
        if (ext.width > caps.maxImageExtent.width) ext.width = caps.maxImageExtent.width;
        if (ext.height > caps.maxImageExtent.height) ext.height = caps.maxImageExtent.height;
    }

    VkSwapchainKHR old = st->sc;
    destroy_swapchain_parts(st);
    st->sc = VK_NULL_HANDLE;
    st->stale = false;
    if (!ext.width || !ext.height) {
        if (old) st->vkDestroySwapchainKHR(st->dev, old, NULL);
        return true;
    }

    /* BGRA8 is XRGB8888's byte order; RGBA8 needs red and blue swapped */
    uint32_t nf = 0;
    if (st->vkGetPhysicalDeviceSurfaceFormatsKHR(st->phys, st->surf, &nf, NULL) != VK_SUCCESS ||
        !nf)
        return false;
    VkSurfaceFormatKHR *f = calloc(nf, sizeof *f);
    if (!f) return false;
    if (st->vkGetPhysicalDeviceSurfaceFormatsKHR(st->phys, st->surf, &nf, f) < 0) nf = 0;
    int best = -1;
    VkSurfaceFormatKHR pick = {VK_FORMAT_UNDEFINED, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
    for (uint32_t i = 0; i < nf; i++) {
        int score = -1;
        if (f[i].format == VK_FORMAT_UNDEFINED) { /* "anything": take BGRA */
            f[i].format = VK_FORMAT_B8G8R8A8_UNORM;
            score = 4;
        } else if (f[i].format == VK_FORMAT_B8G8R8A8_UNORM)
            score = 4;
        else if (f[i].format == VK_FORMAT_B8G8R8A8_SRGB)
            score = 3;
        else if (f[i].format == VK_FORMAT_R8G8B8A8_UNORM)
            score = 2;
        else if (f[i].format == VK_FORMAT_R8G8B8A8_SRGB)
            score = 1;
        if (f[i].colorSpace != VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) score = -1;
        if (score > best) {
            best = score;
            pick = f[i];
        }
    }
    free(f);
    if (best < 0) return false;
    st->fmt = pick.format;
    st->swap_rb = pick.format == VK_FORMAT_R8G8B8A8_UNORM || pick.format == VK_FORMAT_R8G8B8A8_SRGB;

    uint32_t count = caps.minImageCount + 1;
    if (caps.maxImageCount && count > caps.maxImageCount) count = caps.maxImageCount;
    VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if (!(caps.supportedCompositeAlpha & alpha)) {
        for (uint32_t b = 1; b; b <<= 1)
            if (caps.supportedCompositeAlpha & b) {
                alpha = (VkCompositeAlphaFlagBitsKHR) b;
                break;
            }
    }
    VkSwapchainCreateInfoKHR ci = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = st->surf,
        .minImageCount = count,
        .imageFormat = pick.format,
        .imageColorSpace = pick.colorSpace,
        .imageExtent = ext,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = caps.currentTransform,
        .compositeAlpha = alpha,
        .presentMode = VK_PRESENT_MODE_FIFO_KHR, /* always offered; no tearing */
        .clipped = VK_TRUE,
        .oldSwapchain = old,
    };
    VkResult r = st->vkCreateSwapchainKHR(st->dev, &ci, NULL, &st->sc);
    if (old) st->vkDestroySwapchainKHR(st->dev, old, NULL);
    if (r != VK_SUCCESS) {
        st->sc = VK_NULL_HANDLE;
        return false;
    }
    st->ext = ext;

    uint32_t n = 0;
    if (st->vkGetSwapchainImagesKHR(st->dev, st->sc, &n, NULL) != VK_SUCCESS || !n ||
        n > VK_MAX_IMAGES)
        return false;
    if (st->vkGetSwapchainImagesKHR(st->dev, st->sc, &n, st->img) != VK_SUCCESS) return false;
    st->nimg = n;
    VkSemaphoreCreateInfo sci = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for (uint32_t i = 0; i < n; i++)
        if (st->vkCreateSemaphore(st->dev, &sci, NULL, &st->copied[i]) != VK_SUCCESS) return false;
    return true;
}

/* ---- the persistent frame ------------------------------------------------ */

static void destroy_frame(vk_state_t *st)
{
    if (st->stage_map) st->vkUnmapMemory(st->dev, st->stage_mem);
    if (st->stage) st->vkDestroyBuffer(st->dev, st->stage, NULL);
    if (st->stage_mem) st->vkFreeMemory(st->dev, st->stage_mem, NULL);
    if (st->frame) st->vkDestroyImage(st->dev, st->frame, NULL);
    if (st->frame_mem) st->vkFreeMemory(st->dev, st->frame_mem, NULL);
    st->stage_map = NULL;
    st->stage = VK_NULL_HANDLE;
    st->stage_mem = VK_NULL_HANDLE;
    st->frame = VK_NULL_HANDLE;
    st->frame_mem = VK_NULL_HANDLE;
    st->fw = st->fh = 0;
}

static bool make_frame(vk_state_t *st, int32_t w, int32_t h)
{
    st->vkDeviceWaitIdle(st->dev);
    destroy_frame(st);
    VkImageCreateInfo ici = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = st->fmt,
        .extent = {(uint32_t) w, (uint32_t) h, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    if (st->vkCreateImage(st->dev, &ici, NULL, &st->frame) != VK_SUCCESS) return false;
    VkMemoryRequirements mr;
    st->vkGetImageMemoryRequirements(st->dev, st->frame, &mr);
    uint32_t mt = find_memory(st, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (mt == UINT32_MAX) mt = find_memory(st, mr.memoryTypeBits, 0);
    VkMemoryAllocateInfo mai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                                .allocationSize = mr.size,
                                .memoryTypeIndex = mt};
    if (mt == UINT32_MAX ||
        st->vkAllocateMemory(st->dev, &mai, NULL, &st->frame_mem) != VK_SUCCESS ||
        st->vkBindImageMemory(st->dev, st->frame, st->frame_mem, 0) != VK_SUCCESS)
        return false;

    VkBufferCreateInfo bci = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                              .size = (VkDeviceSize) w * (VkDeviceSize) h * 4u,
                              .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                              .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    if (st->vkCreateBuffer(st->dev, &bci, NULL, &st->stage) != VK_SUCCESS) return false;
    st->vkGetBufferMemoryRequirements(st->dev, st->stage, &mr);
    mt = find_memory(st, mr.memoryTypeBits,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    mai.allocationSize = mr.size;
    mai.memoryTypeIndex = mt;
    void *map = NULL;
    if (mt == UINT32_MAX ||
        st->vkAllocateMemory(st->dev, &mai, NULL, &st->stage_mem) != VK_SUCCESS ||
        st->vkBindBufferMemory(st->dev, st->stage, st->stage_mem, 0) != VK_SUCCESS ||
        st->vkMapMemory(st->dev, st->stage_mem, 0, VK_WHOLE_SIZE, 0, &map) != VK_SUCCESS)
        return false;
    st->stage_map = map;
    st->frame_fmt = st->fmt;
    st->fw = w;
    st->fh = h;
    st->frame_undefined = true;
    return true;
}

/* ---- one frame ----------------------------------------------------------- */

static bool clip_rect(zui_rect_t *r, int32_t w, int32_t h)
{
    int32_t x0 = r->x < 0 ? 0 : r->x, y0 = r->y < 0 ? 0 : r->y;
    int32_t x1 = r->x + r->w > w ? w : r->x + r->w;
    int32_t y1 = r->y + r->h > h ? h : r->y + r->h;
    if (x1 <= x0 || y1 <= y0) return false;
    r->x = x0;
    r->y = y0;
    r->w = x1 - x0;
    r->h = y1 - y0;
    return true;
}

static void barrier(vk_state_t *st, VkImage img, VkImageLayout from, VkImageLayout to,
                    VkAccessFlags src_access, VkAccessFlags dst_access,
                    VkPipelineStageFlags src_stage, VkPipelineStageFlags dst_stage)
{
    VkImageMemoryBarrier b = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = src_access,
        .dstAccessMask = dst_access,
        .oldLayout = from,
        .newLayout = to,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = img,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
    };
    st->vkCmdPipelineBarrier(st->cmd, src_stage, dst_stage, 0, 0, NULL, 0, NULL, 1, &b);
}

/* Upload the damage of s (may be NULL: no upload) and show the frame. */
static int show(vk_state_t *st, const zui_screen_t *scr, const zui_surface_t *s,
                const zui_damage_t *d, uint64_t *copied_px)
{
    *copied_px = 0;
    if (st->vkWaitForFences(st->dev, 1, &st->fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS)
        return ZUI_PRESENT_E_LOST;

    uint32_t ww = scr->px_w, wh = scr->px_h;
    if (!st->sc || st->stale || st->ext.width != ww || st->ext.height != wh) {
        if (!make_swapchain(st, ww, wh)) return ZUI_PRESENT_E_LOST;
    }

    /* the frame follows the surface; a new one is uploaded whole */
    zui_damage_t all;
    if (s) {
        if (!st->frame || st->fw != s->w || st->fh != s->h || st->frame_fmt != st->fmt) {
            if (!make_frame(st, s->w, s->h)) return ZUI_PRESENT_E_UNAVAILABLE;
        }
        if (st->frame_undefined) {
            zui_damage_clear(&all);
            zui_damage_all(&all, s->w, s->h);
            d = &all;
        }
    } else if (!st->frame || st->frame_undefined || st->frame_fmt != st->fmt) {
        return ZUI_PRESENT_OK; /* nothing to show yet */
    }

    /* stage the damaged rectangles where they sit in the frame */
    VkBufferImageCopy regions[ZUI_DAMAGE_MAX];
    uint32_t nreg = 0;
    if (s) {
        for (uint32_t i = 0; i < d->n && nreg < ZUI_DAMAGE_MAX; i++) {
            zui_rect_t r = d->r[i];
            if (!clip_rect(&r, s->w, s->h)) continue;
            for (int32_t y = r.y; y < r.y + r.h; y++) {
                const uint32_t *src = s->px + (size_t) y * (size_t) s->stride + (size_t) r.x;
                uint32_t *dst = (uint32_t *) (void *) st->stage_map + (size_t) y * (size_t) st->fw +
                                (size_t) r.x;
                if (st->swap_rb) {
                    for (int32_t x = 0; x < r.w; x++) {
                        uint32_t p = src[x];
                        dst[x] =
                            0xFF000000u | ((p & 0xFFu) << 16) | (p & 0xFF00u) | ((p >> 16) & 0xFFu);
                    }
                } else {
                    for (int32_t x = 0; x < r.w; x++) dst[x] = src[x] | 0xFF000000u;
                }
            }
            VkBufferImageCopy c = {
                .bufferOffset =
                    ((VkDeviceSize) r.y * (VkDeviceSize) st->fw + (VkDeviceSize) r.x) * 4u,
                .bufferRowLength = (uint32_t) st->fw,
                .bufferImageHeight = 0,
                .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                .imageOffset = {r.x, r.y, 0},
                .imageExtent = {(uint32_t) r.w, (uint32_t) r.h, 1},
            };
            regions[nreg++] = c;
            *copied_px += (uint64_t) r.w * (uint64_t) r.h;
        }
        if (!nreg) return ZUI_PRESENT_OK; /* P1: nothing changed */
    }

    /* the swapchain image to fill */
    uint32_t idx = 0;
    bool have_img = false;
    if (st->sc) {
        VkResult r = st->vkAcquireNextImageKHR(st->dev, st->sc, UINT64_MAX, st->acquired,
                                               VK_NULL_HANDLE, &idx);
        if (r == VK_ERROR_OUT_OF_DATE_KHR) {
            if (!make_swapchain(st, ww, wh)) return ZUI_PRESENT_E_LOST;
            r = st->sc ? st->vkAcquireNextImageKHR(st->dev, st->sc, UINT64_MAX, st->acquired,
                                                   VK_NULL_HANDLE, &idx)
                       : VK_ERROR_OUT_OF_DATE_KHR;
        }
        if (r == VK_SUBOPTIMAL_KHR) st->stale = true;
        if (r == VK_SUCCESS || r == VK_SUBOPTIMAL_KHR)
            have_img = true;
        else if (r != VK_ERROR_OUT_OF_DATE_KHR)
            return ZUI_PRESENT_E_LOST; /* device or surface lost */
    }
    if (!nreg && !have_img) return ZUI_PRESENT_OK;

    VkCommandBufferBeginInfo bi = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                   .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    if (st->vkBeginCommandBuffer(st->cmd, &bi) != VK_SUCCESS) return ZUI_PRESENT_E_LOST;
    if (nreg) {
        barrier(st, st->frame,
                st->frame_undefined ? VK_IMAGE_LAYOUT_UNDEFINED
                                    : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT,
                VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT);
        st->vkCmdCopyBufferToImage(st->cmd, st->stage, st->frame,
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, nreg, regions);
        barrier(st, st->frame, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT);
    }
    if (have_img) {
        VkImage dst = st->img[idx];
        barrier(st, dst, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
                VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT);
        uint32_t cw = st->ext.width < (uint32_t) st->fw ? st->ext.width : (uint32_t) st->fw;
        uint32_t ch = st->ext.height < (uint32_t) st->fh ? st->ext.height : (uint32_t) st->fh;
        if (cw != st->ext.width || ch != st->ext.height) {
            /* the window is bigger than the frame for a moment: ground colour */
            VkClearColorValue g = {.float32 = {0x00 / 255.0f, 0x29 / 255.0f, 0x11 / 255.0f, 1.0f}};
            VkImageSubresourceRange rr = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            st->vkCmdClearColorImage(st->cmd, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &g, 1,
                                     &rr);
            barrier(st, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT);
        }
        VkImageCopy ic = {
            .srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
            .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
            .extent = {cw, ch, 1},
        };
        st->vkCmdCopyImage(st->cmd, st->frame, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &ic);
        barrier(st, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                VK_ACCESS_TRANSFER_WRITE_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    }
    if (st->vkEndCommandBuffer(st->cmd) != VK_SUCCESS) return ZUI_PRESENT_E_LOST;

    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSubmitInfo si = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = have_img ? 1u : 0u,
        .pWaitSemaphores = &st->acquired,
        .pWaitDstStageMask = &wait_stage,
        .commandBufferCount = 1,
        .pCommandBuffers = &st->cmd,
        .signalSemaphoreCount = have_img ? 1u : 0u,
        .pSignalSemaphores = have_img ? &st->copied[idx] : NULL,
    };
    if (st->vkResetFences(st->dev, 1, &st->fence) != VK_SUCCESS ||
        st->vkQueueSubmit(st->queue, 1, &si, st->fence) != VK_SUCCESS)
        return ZUI_PRESENT_E_LOST;
    if (nreg) st->frame_undefined = false;

    if (have_img) {
        VkPresentInfoKHR pi = {.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
                               .waitSemaphoreCount = 1,
                               .pWaitSemaphores = &st->copied[idx],
                               .swapchainCount = 1,
                               .pSwapchains = &st->sc,
                               .pImageIndices = &idx};
        VkResult r = st->vkQueuePresentKHR(st->queue, &pi);
        if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR)
            st->stale = true; /* recreated before the next acquire */
        else if (r != VK_SUCCESS)
            return ZUI_PRESENT_E_LOST;
    }
    return ZUI_PRESENT_OK;
}

/* ---- ops ----------------------------------------------------------------- */

static void vk_close(zui_present_t *p)
{
    vk_state_t *st = p->impl;
    if (!st) return;
    if (st->dev && st->dev_fns) {
        st->vkDeviceWaitIdle(st->dev);
        destroy_frame(st);
        destroy_swapchain_parts(st);
        if (st->sc) st->vkDestroySwapchainKHR(st->dev, st->sc, NULL);
        if (st->acquired) st->vkDestroySemaphore(st->dev, st->acquired, NULL);
        if (st->fence) st->vkDestroyFence(st->dev, st->fence, NULL);
        if (st->pool) st->vkDestroyCommandPool(st->dev, st->pool, NULL);
    }
    if (st->dev && st->vkDestroyDevice) st->vkDestroyDevice(st->dev, NULL);
    if (st->surf && st->vkDestroySurfaceKHR) st->vkDestroySurfaceKHR(st->inst, st->surf, NULL);
    if (st->inst && st->vkDestroyInstance) st->vkDestroyInstance(st->inst, NULL);
    win_close(st);
    unload_loader(st);
    free(st);
    p->impl = NULL;
}

static int vk_open(zui_present_t *p, const char *title, int32_t w_pt, int32_t h_pt)
{
    vk_state_t *st = calloc(1, sizeof *st);
    if (!st) return ZUI_PRESENT_E_UNAVAILABLE;
    p->impl = st;
    p->frames = 0;
    p->px_copied = 0;
    /* the loader first: no Vulkan means no window flashes up and away */
    if (!load_loader(st) || !load_global(st) || !make_instance(st) || !load_instance(st)) {
        vk_close(p);
        return ZUI_PRESENT_E_UNAVAILABLE;
    }
    int rc = win_open(st, &p->screen, title, w_pt, h_pt);
    if (rc != ZUI_PRESENT_OK) {
        vk_close(p);
        return rc;
    }
    if (make_surface(st) != VK_SUCCESS || !make_device(st) ||
        !make_swapchain(st, p->screen.px_w, p->screen.px_h)) {
        vk_close(p);
        return ZUI_PRESENT_E_UNAVAILABLE;
    }
    return ZUI_PRESENT_OK;
}

static int vk_present(zui_present_t *p, const zui_surface_t *s, const zui_damage_t *d)
{
    vk_state_t *st = p->impl;
    if (!st || !s || !s->px || !d || s->w <= 0 || s->h <= 0) return ZUI_PRESENT_E_ARG;
    uint64_t copied = 0;
    int rc = show(st, &p->screen, s, d, &copied);
    if (rc == ZUI_PRESENT_OK && copied) {
        p->frames++;
        p->px_copied += copied;
    }
    return rc;
}

static int vk_poll(zui_present_t *p, zui_queue_t *q, uint32_t wait_ms)
{
    vk_state_t *st = p->impl;
    if (!st || !q) return ZUI_PRESENT_E_ARG;
    uint64_t unused;
#if defined(_WIN32)
    int n = zui_win32_poll(&st->win, &p->screen, q, wait_ms);
    if (n >= 0 && zui_win32_take_redraw(&st->win)) show(st, &p->screen, NULL, NULL, &unused);
#else
    int n = zui_x11_poll(&st->win, &p->screen, q, wait_ms);
    zui_rect_t r;
    if (n >= 0 && zui_x11_take_expose(&st->win, &r)) show(st, &p->screen, NULL, NULL, &unused);
#endif
    return n;
}

static uint32_t vk_now_ms(zui_present_t *p)
{
    (void) p;
#if defined(_WIN32)
    return zui_win32_now_ms();
#else
    return zui_x11_now_ms();
#endif
}

const zui_present_ops_t zui_present_vulkan_ops = {
    .name = "vulkan",
    .open = vk_open,
    .close = vk_close,
    .present = vk_present,
    .poll = vk_poll,
    .now_ms = vk_now_ms,
};
