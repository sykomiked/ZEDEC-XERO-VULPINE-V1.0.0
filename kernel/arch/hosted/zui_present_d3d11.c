/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_present_d3d11.c — the Direct3D 11 present layer: Windows.
 *
 * Plain C through COBJMACROS. The window and input are zui_win32_window.c.
 * zui draws on the CPU, so Direct3D only moves pixels:
 *
 *   1. UpdateSubresource copies each damaged rectangle of the app's surface
 *      into a persistent frame-sized texture (P1). The source rows are the
 *      app's own memory, read in place.
 *   2. CopySubresourceRegion copies that texture into the swapchain's back
 *      buffer, and Present shows it.
 *
 * The persistent texture is there because a flip-model swapchain
 * (DXGI_SWAP_EFFECT_FLIP_DISCARD, or FLIP_SEQUENTIAL before Windows 10) does
 * not keep the back buffer's contents between frames, so damage-only
 * uploads straight into it would show stale pixels. The format is
 * DXGI_FORMAT_B8G8R8A8_UNORM, which is XRGB8888's byte order, with the alpha
 * byte ignored (DXGI_ALPHA_MODE_IGNORE): nothing is converted (P2).
 *
 * A hardware device is used when there is one, else WARP (the system's
 * software rasteriser). Link with -ld3d11 -ldxgi -ldxguid plus what
 * zui_win32_window.c needs (-luser32 -limm32 -lgdi32).
 */
#define COBJMACROS
#include "zui_win32_window.h"

#include <d3d11.h>
#include <dxgi1_2.h>
#include <stdlib.h>
#include <string.h>
#include "zui_present.h"

typedef struct {
    zui_win32_window_t win;
    ID3D11Device *dev;
    ID3D11DeviceContext *ctx;
    IDXGISwapChain1 *sc;
    UINT bw, bh; /* back buffer size */
    ID3D11Texture2D *frame;
    int32_t fw, fh;
    bool frame_undefined; /* never written: the next upload is whole */
} d3d_state_t;

static void release(IUnknown **u)
{
    if (*u) IUnknown_Release(*u);
    *u = NULL;
}
#define RELEASE(p) release((IUnknown **) (void *) &(p))

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

/* ---- device and swapchain ------------------------------------------------ */

static bool make_device(d3d_state_t *st)
{
    static const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                                               D3D_FEATURE_LEVEL_10_0};
    static const D3D_DRIVER_TYPE kinds[] = {D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP};
    for (unsigned i = 0; i < sizeof kinds / sizeof kinds[0]; i++) {
        D3D_FEATURE_LEVEL got;
        HRESULT hr = D3D11CreateDevice(NULL, kinds[i], NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                       levels, sizeof levels / sizeof levels[0], D3D11_SDK_VERSION,
                                       &st->dev, &got, &st->ctx);
        if (SUCCEEDED(hr)) return true;
    }
    return false;
}

static bool make_swapchain(d3d_state_t *st, UINT w, UINT h)
{
    IDXGIDevice *xdev = NULL;
    IDXGIAdapter *adapter = NULL;
    IDXGIFactory2 *factory = NULL;
    bool ok = false;
    if (FAILED(ID3D11Device_QueryInterface(st->dev, &IID_IDXGIDevice, (void **) &xdev)) ||
        FAILED(IDXGIDevice_GetAdapter(xdev, &adapter)) ||
        FAILED(IDXGIAdapter_GetParent(adapter, &IID_IDXGIFactory2, (void **) &factory)))
        goto out; /* no DXGI 1.2: Windows 7 without the platform update */

    DXGI_SWAP_CHAIN_DESC1 d;
    memset(&d, 0, sizeof d);
    d.Width = w;
    d.Height = h;
    d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    d.BufferCount = 2;
    d.Scaling = DXGI_SCALING_NONE;
    d.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    HRESULT hr = IDXGIFactory2_CreateSwapChainForHwnd(factory, (IUnknown *) st->dev, st->win.hwnd,
                                                      &d, NULL, NULL, &st->sc);
    if (FAILED(hr)) {
        d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL; /* Windows 8 */
        hr = IDXGIFactory2_CreateSwapChainForHwnd(factory, (IUnknown *) st->dev, st->win.hwnd, &d,
                                                  NULL, NULL, &st->sc);
    }
    if (FAILED(hr)) {
        d.Scaling = DXGI_SCALING_STRETCH;
        hr = IDXGIFactory2_CreateSwapChainForHwnd(factory, (IUnknown *) st->dev, st->win.hwnd, &d,
                                                  NULL, NULL, &st->sc);
    }
    if (FAILED(hr)) goto out;
    IDXGIFactory2_MakeWindowAssociation(factory, st->win.hwnd, DXGI_MWA_NO_ALT_ENTER);
    st->bw = w;
    st->bh = h;
    ok = true;
out:
    RELEASE(factory);
    RELEASE(adapter);
    RELEASE(xdev);
    return ok;
}

static bool make_frame(d3d_state_t *st, int32_t w, int32_t h)
{
    RELEASE(st->frame);
    D3D11_TEXTURE2D_DESC td;
    memset(&td, 0, sizeof td);
    td.Width = (UINT) w;
    td.Height = (UINT) h;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(ID3D11Device_CreateTexture2D(st->dev, &td, NULL, &st->frame))) return false;
    st->fw = w;
    st->fh = h;
    st->frame_undefined = true;
    return true;
}

static int lost_or(HRESULT hr, int otherwise)
{
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) return ZUI_PRESENT_E_LOST;
    return otherwise;
}

/* Upload the damage of s (NULL: none) and show the frame. */
static int show(d3d_state_t *st, const zui_screen_t *scr, const zui_surface_t *s,
                const zui_damage_t *d, uint64_t *copied)
{
    *copied = 0;
    zui_damage_t all;
    if (s) {
        if (!st->frame || st->fw != s->w || st->fh != s->h)
            if (!make_frame(st, s->w, s->h)) return ZUI_PRESENT_E_UNAVAILABLE;
        if (st->frame_undefined) {
            zui_damage_clear(&all);
            zui_damage_all(&all, s->w, s->h);
            d = &all;
        }
        for (uint32_t i = 0; i < d->n; i++) {
            zui_rect_t r = d->r[i];
            if (!clip_rect(&r, s->w, s->h)) continue;
            D3D11_BOX box = {(UINT) r.x, (UINT) r.y, 0, (UINT) (r.x + r.w), (UINT) (r.y + r.h), 1};
            const uint32_t *src = s->px + (size_t) r.y * (size_t) s->stride + (size_t) r.x;
            ID3D11DeviceContext_UpdateSubresource(st->ctx, (ID3D11Resource *) st->frame, 0, &box,
                                                  src, (UINT) s->stride * 4u, 0);
            *copied += (uint64_t) r.w * (uint64_t) r.h;
        }
        if (!*copied) return ZUI_PRESENT_OK; /* P1: nothing changed */
        st->frame_undefined = false;
    } else if (!st->frame || st->frame_undefined) {
        return ZUI_PRESENT_OK;
    }

    /* the back buffer follows the window */
    if (scr->px_w && scr->px_h && (scr->px_w != st->bw || scr->px_h != st->bh)) {
        ID3D11DeviceContext_ClearState(st->ctx);
        HRESULT hr =
            IDXGISwapChain1_ResizeBuffers(st->sc, 0, scr->px_w, scr->px_h, DXGI_FORMAT_UNKNOWN, 0);
        if (FAILED(hr)) return lost_or(hr, ZUI_PRESENT_E_LOST);
        st->bw = scr->px_w;
        st->bh = scr->px_h;
    }

    ID3D11Texture2D *bb = NULL;
    HRESULT hr = IDXGISwapChain1_GetBuffer(st->sc, 0, &IID_ID3D11Texture2D, (void **) &bb);
    if (FAILED(hr)) return lost_or(hr, ZUI_PRESENT_E_LOST);
    UINT cw = st->bw < (UINT) st->fw ? st->bw : (UINT) st->fw;
    UINT ch = st->bh < (UINT) st->fh ? st->bh : (UINT) st->fh;
    if (cw != st->bw || ch != st->bh) {
        /* the window is bigger than the frame for a moment: ground colour */
        ID3D11RenderTargetView *rtv = NULL;
        if (SUCCEEDED(
                ID3D11Device_CreateRenderTargetView(st->dev, (ID3D11Resource *) bb, NULL, &rtv))) {
            const FLOAT ground[4] = {0x00 / 255.0f, 0x29 / 255.0f, 0x11 / 255.0f, 1.0f};
            ID3D11DeviceContext_ClearRenderTargetView(st->ctx, rtv, ground);
            RELEASE(rtv);
        }
    }
    D3D11_BOX box = {0, 0, 0, cw, ch, 1};
    ID3D11DeviceContext_CopySubresourceRegion(st->ctx, (ID3D11Resource *) bb, 0, 0, 0, 0,
                                              (ID3D11Resource *) st->frame, 0, &box);
    RELEASE(bb);
    hr = IDXGISwapChain1_Present(st->sc, 1, 0);
    if (FAILED(hr)) return lost_or(hr, ZUI_PRESENT_E_LOST);
    return ZUI_PRESENT_OK;
}

/* ---- ops ----------------------------------------------------------------- */

static void d3d_close(zui_present_t *p)
{
    d3d_state_t *st = p->impl;
    if (!st) return;
    if (st->ctx) ID3D11DeviceContext_ClearState(st->ctx);
    RELEASE(st->frame);
    RELEASE(st->sc);
    RELEASE(st->ctx);
    RELEASE(st->dev);
    zui_win32_close(&st->win);
    free(st);
    p->impl = NULL;
}

static int d3d_open(zui_present_t *p, const char *title, int32_t w_pt, int32_t h_pt)
{
    d3d_state_t *st = calloc(1, sizeof *st);
    if (!st) return ZUI_PRESENT_E_UNAVAILABLE;
    p->impl = st;
    p->frames = 0;
    p->px_copied = 0;
    if (!make_device(st)) {
        d3d_close(p);
        return ZUI_PRESENT_E_UNAVAILABLE;
    }
    int rc = zui_win32_open(&st->win, &p->screen, title, w_pt, h_pt);
    if (rc != ZUI_PRESENT_OK) {
        d3d_close(p);
        return rc;
    }
    if (!make_swapchain(st, p->screen.px_w, p->screen.px_h)) {
        d3d_close(p);
        return ZUI_PRESENT_E_UNAVAILABLE;
    }
    return ZUI_PRESENT_OK;
}

static int d3d_present(zui_present_t *p, const zui_surface_t *s, const zui_damage_t *d)
{
    d3d_state_t *st = p->impl;
    if (!st || !s || !s->px || !d || s->w <= 0 || s->h <= 0) return ZUI_PRESENT_E_ARG;
    if (st->win.closed) return ZUI_PRESENT_E_LOST;
    uint64_t copied = 0;
    int rc = show(st, &p->screen, s, d, &copied);
    if (rc == ZUI_PRESENT_OK && copied) {
        p->frames++;
        p->px_copied += copied;
    }
    return rc;
}

static int d3d_poll(zui_present_t *p, zui_queue_t *q, uint32_t wait_ms)
{
    d3d_state_t *st = p->impl;
    if (!st || !q) return ZUI_PRESENT_E_ARG;
    int n = zui_win32_poll(&st->win, &p->screen, q, wait_ms);
    uint64_t unused;
    if (n >= 0 && zui_win32_take_redraw(&st->win)) show(st, &p->screen, NULL, NULL, &unused);
    return n;
}

static uint32_t d3d_now_ms(zui_present_t *p)
{
    (void) p;
    return zui_win32_now_ms();
}

const zui_present_ops_t zui_present_d3d11_ops = {
    .name = "d3d11",
    .open = d3d_open,
    .close = d3d_close,
    .present = d3d_present,
    .poll = d3d_poll,
    .now_ms = d3d_now_ms,
};
