/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_present_host.c — the hosted present layers in order of preference
 * (see zui_present_host.h). Choosing among them is the caller's job: this
 * only lists what the build has.
 */
#include "zui_present_host.h"

#include <stddef.h>

static const zui_present_ops_t *const order[] = {
#if ZUI_HAVE_METAL
    &zui_present_metal_ops,
#endif
#if ZUI_HAVE_D3D11
    &zui_present_d3d11_ops,
#endif
#if ZUI_HAVE_VULKAN
    &zui_present_vulkan_ops,
#endif
#if ZUI_HAVE_X11
    &zui_present_x11_ops,
#endif
    NULL /* keeps the array non-empty in a build with no backend */
};

uint32_t zui_present_candidates(const zui_present_ops_t **out, uint32_t cap)
{
    uint32_t n = (uint32_t) (sizeof order / sizeof order[0]) - 1u;
    for (uint32_t i = 0; i < n && i < cap && out; i++) out[i] = order[i];
    return n;
}

const zui_present_ops_t *zui_present_best(void)
{
    return order[0];
}

static int same(const char *a, const char *b)
{
    while (*a && *a == *b) a++, b++;
    return *a == *b;
}

const zui_present_ops_t *zui_present_by_name(const char *name)
{
    if (!name) return NULL;
    for (uint32_t i = 0; order[i]; i++)
        if (same(order[i]->name, name)) return order[i];
    return NULL;
}
