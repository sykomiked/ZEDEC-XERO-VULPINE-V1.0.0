/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_present_host.h — which present layers a hosted build has, best first.
 *
 * Each platform compiles the backends it can run (zui_present.h lists them):
 *
 *   macOS     metal
 *   Windows   d3d11, then vulkan
 *   Linux     vulkan, then x11
 *
 * A backend can be left out of a build with -DZUI_HAVE_<NAME>=0 (for example
 * -DZUI_HAVE_VULKAN=0 for an X11-only build without the Vulkan headers).
 * The list is only an order of preference: the caller opens each candidate in
 * turn and keeps the first whose open() succeeds, since only opening tells
 * whether there is a display, a GPU or a driver.
 */
#ifndef ZXV_ZUI_PRESENT_HOST_H
#define ZXV_ZUI_PRESENT_HOST_H

#include <stdint.h>
#include "zui_present.h"

#if defined(__APPLE__)
#    ifndef ZUI_HAVE_METAL
#        define ZUI_HAVE_METAL 1
#    endif
#elif defined(_WIN32)
#    ifndef ZUI_HAVE_D3D11
#        define ZUI_HAVE_D3D11 1
#    endif
#    ifndef ZUI_HAVE_VULKAN
#        define ZUI_HAVE_VULKAN 1
#    endif
#else
#    ifndef ZUI_HAVE_VULKAN
#        define ZUI_HAVE_VULKAN 1
#    endif
#    ifndef ZUI_HAVE_X11
#        define ZUI_HAVE_X11 1
#    endif
#endif

#ifndef ZUI_HAVE_METAL
#    define ZUI_HAVE_METAL 0
#endif
#ifndef ZUI_HAVE_D3D11
#    define ZUI_HAVE_D3D11 0
#endif
#ifndef ZUI_HAVE_VULKAN
#    define ZUI_HAVE_VULKAN 0
#endif
#ifndef ZUI_HAVE_X11
#    define ZUI_HAVE_X11 0
#endif

#if ZUI_HAVE_METAL
extern const zui_present_ops_t zui_present_metal_ops;
/* The NSWindow of an open Metal present layer, unretained (bridge it:
 * (__bridge NSWindow *) ...), for a host app's sheets, minimum size and
 * saved frame; NULL for any other layer. */
void *zui_present_metal_window(const zui_present_t *p);
#endif
#if ZUI_HAVE_D3D11
extern const zui_present_ops_t zui_present_d3d11_ops;
#endif
#if ZUI_HAVE_VULKAN
extern const zui_present_ops_t zui_present_vulkan_ops;
#endif
#if ZUI_HAVE_X11
extern const zui_present_ops_t zui_present_x11_ops;
#endif

/* The backends of this build in order of preference; writes at most cap of
 * them to out and returns how many there are. */
uint32_t zui_present_candidates(const zui_present_ops_t **out, uint32_t cap);

/* The first candidate (NULL when the build has none). It is not opened. */
const zui_present_ops_t *zui_present_best(void);

/* The candidate called name ("x11", "vulkan", "d3d11", "metal"), or NULL. */
const zui_present_ops_t *zui_present_by_name(const char *name);

#endif /* ZXV_ZUI_PRESENT_HOST_H */
