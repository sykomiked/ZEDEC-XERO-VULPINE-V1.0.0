/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_present_x11.c — the X11 present layer: Linux without a GPU.
 *
 * The window and its input are zui_x11_window.c. Pixels go through one
 * window-sized XImage that this backend owns: each present copies only the
 * damaged rectangles from the app's surface into it (P1) and sends those
 * rectangles to the server. With MIT-SHM (a local display) the image lives
 * in shared memory and the server reads it in place; when shared memory is
 * not available (a remote display, ssh -X) the same image goes over the wire
 * with XPutImage. Because the image holds the last frame, an Expose is
 * answered from it without asking the app to repaint.
 *
 * The visual must be 24- or 32-bit TrueColor with the 0xRRGGBB masks, which
 * is XRGB8888 itself, so pixels are copied unchanged (P2).
 *
 * Link with -lX11 -lXext.
 */
#define _GNU_SOURCE
#include "zui_present.h"
#include "zui_x11_window.h"

#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <X11/extensions/XShm.h>

typedef struct {
    zui_x11_window_t xw;
    GC gc;
    XImage *img;
    XShmSegmentInfo shm;
    bool use_shm; /* shared memory works on this display */
    bool shm_attached;
    int32_t iw, ih; /* image size in pixels */
} x11_state_t;

/* ---- shared memory, with a fallback ------------------------------------- */

static int shm_error;
static int on_shm_error(Display *d, XErrorEvent *e)
{
    (void) d;
    (void) e;
    shm_error = 1;
    return 0;
}

static void free_image(x11_state_t *st)
{
    Display *dpy = st->xw.dpy;
    if (!st->img) return;
    if (st->shm_attached) {
        XShmDetach(dpy, &st->shm);
        XSync(dpy, False);
        st->shm_attached = false;
    }
    if (st->shm.shmaddr) {
        shmdt(st->shm.shmaddr);
        st->shm.shmaddr = NULL;
        st->img->data = NULL;
    }
    XDestroyImage(st->img); /* frees data when it was malloc'ed */
    st->img = NULL;
    st->iw = st->ih = 0;
}

static bool make_shm_image(x11_state_t *st, int32_t w, int32_t h)
{
    Display *dpy = st->xw.dpy;
    memset(&st->shm, 0, sizeof st->shm);
    XImage *img = XShmCreateImage(dpy, st->xw.visual, (unsigned) st->xw.depth, ZPixmap, NULL,
                                  &st->shm, (unsigned) w, (unsigned) h);
    if (!img) return false;
    st->shm.shmid =
        shmget(IPC_PRIVATE, (size_t) img->bytes_per_line * (size_t) h, IPC_CREAT | 0600);
    if (st->shm.shmid < 0) {
        XDestroyImage(img);
        return false;
    }
    st->shm.shmaddr = img->data = shmat(st->shm.shmid, NULL, 0);
    if (st->shm.shmaddr == (char *) -1) {
        shmctl(st->shm.shmid, IPC_RMID, NULL);
        st->shm.shmaddr = NULL;
        img->data = NULL;
        XDestroyImage(img);
        return false;
    }
    st->shm.readOnly = False;

    /* A remote server accepts the request and fails it later: catch that. */
    XSync(dpy, False);
    shm_error = 0;
    int (*old)(Display *, XErrorEvent *) = XSetErrorHandler(on_shm_error);
    Status ok = XShmAttach(dpy, &st->shm);
    XSync(dpy, False);
    XSetErrorHandler(old);
    shmctl(st->shm.shmid, IPC_RMID, NULL); /* goes away once both detach */
    if (!ok || shm_error) {
        shmdt(st->shm.shmaddr);
        st->shm.shmaddr = NULL;
        img->data = NULL;
        XDestroyImage(img);
        return false;
    }
    st->shm_attached = true;
    st->img = img;
    return true;
}

static bool make_plain_image(x11_state_t *st, int32_t w, int32_t h)
{
    Display *dpy = st->xw.dpy;
    char *data = calloc((size_t) w * (size_t) h, 4);
    if (!data) return false;
    XImage *img = XCreateImage(dpy, st->xw.visual, (unsigned) st->xw.depth, ZPixmap, 0, data,
                               (unsigned) w, (unsigned) h, 32, w * 4);
    if (!img) {
        free(data);
        return false;
    }
    st->img = img;
    return true;
}

static bool ensure_image(x11_state_t *st, int32_t w, int32_t h)
{
    if (st->img && st->iw == w && st->ih == h) return true;
    free_image(st);
    if (st->use_shm && !make_shm_image(st, w, h)) st->use_shm = false;
    if (!st->img && !make_plain_image(st, w, h)) return false;
    if (st->img->bits_per_pixel != 32 || st->img->red_mask != 0xFF0000 ||
        st->img->green_mask != 0xFF00 || st->img->blue_mask != 0xFF) {
        free_image(st);
        return false;
    }
    st->iw = w;
    st->ih = h;
    return true;
}

static void put_rect(x11_state_t *st, zui_rect_t r)
{
    Display *dpy = st->xw.dpy;
    if (st->shm_attached)
        XShmPutImage(dpy, st->xw.win, st->gc, st->img, r.x, r.y, r.x, r.y, (unsigned) r.w,
                     (unsigned) r.h, False);
    else
        XPutImage(dpy, st->xw.win, st->gc, st->img, r.x, r.y, r.x, r.y, (unsigned) r.w,
                  (unsigned) r.h);
}

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

/* ---- ops ----------------------------------------------------------------- */

static int x11_open(zui_present_t *p, const char *title, int32_t w_pt, int32_t h_pt)
{
    x11_state_t *st = calloc(1, sizeof *st);
    if (!st) return ZUI_PRESENT_E_UNAVAILABLE;
    int rc = zui_x11_open(&st->xw, &p->screen, title, w_pt, h_pt);
    if (rc != ZUI_PRESENT_OK) {
        free(st);
        return rc;
    }
    Visual *v = st->xw.visual;
    if ((st->xw.depth != 24 && st->xw.depth != 32) || v->red_mask != 0xFF0000 ||
        v->green_mask != 0xFF00 || v->blue_mask != 0xFF) {
        zui_x11_close(&st->xw);
        free(st);
        return ZUI_PRESENT_E_UNAVAILABLE;
    }
    st->gc = XCreateGC(st->xw.dpy, st->xw.win, 0, NULL);
    st->use_shm = XShmQueryExtension(st->xw.dpy) && !getenv("ZXV_X11_NO_SHM");
    p->impl = st;
    p->frames = 0;
    p->px_copied = 0;
    return ZUI_PRESENT_OK;
}

static void x11_close(zui_present_t *p)
{
    x11_state_t *st = p->impl;
    if (!st) return;
    free_image(st);
    if (st->gc) XFreeGC(st->xw.dpy, st->gc);
    zui_x11_close(&st->xw);
    free(st);
    p->impl = NULL;
}

static int x11_present(zui_present_t *p, const zui_surface_t *s, const zui_damage_t *d)
{
    x11_state_t *st = p->impl;
    if (!st || !s || !s->px || !d || s->w <= 0 || s->h <= 0) return ZUI_PRESENT_E_ARG;
    if (st->xw.closed) return ZUI_PRESENT_E_LOST;

    /* A new size means a new image, whose old contents are gone: send all. */
    bool fresh = !st->img || st->iw != s->w || st->ih != s->h;
    if (!ensure_image(st, s->w, s->h)) return ZUI_PRESENT_E_UNAVAILABLE;
    zui_damage_t all;
    if (fresh) {
        zui_damage_clear(&all);
        zui_damage_all(&all, s->w, s->h);
        d = &all;
    }

    uint64_t copied = 0;
    for (uint32_t i = 0; i < d->n; i++) {
        zui_rect_t r = d->r[i];
        if (!clip_rect(&r, s->w, s->h)) continue;
        for (int32_t y = r.y; y < r.y + r.h; y++) {
            const uint32_t *src = s->px + (size_t) y * (size_t) s->stride + (size_t) r.x;
            char *dst =
                st->img->data + (size_t) y * (size_t) st->img->bytes_per_line + (size_t) r.x * 4u;
            memcpy(dst, src, (size_t) r.w * 4u);
        }
        put_rect(st, r);
        copied += (uint64_t) r.w * (uint64_t) r.h;
    }
    if (copied) {
        /* The server reads shared memory after the request: wait for it so
         * the next present may write the image again. */
        if (st->shm_attached)
            XSync(st->xw.dpy, False);
        else
            XFlush(st->xw.dpy);
        p->frames++;
        p->px_copied += copied;
    }
    return ZUI_PRESENT_OK;
}

static int x11_poll(zui_present_t *p, zui_queue_t *q, uint32_t wait_ms)
{
    x11_state_t *st = p->impl;
    if (!st || !q) return ZUI_PRESENT_E_ARG;
    int n = zui_x11_poll(&st->xw, &p->screen, q, wait_ms);
    zui_rect_t r;
    if (n >= 0 && zui_x11_take_expose(&st->xw, &r) && st->img && clip_rect(&r, st->iw, st->ih)) {
        put_rect(st, r); /* the last frame, from our own copy */
        XFlush(st->xw.dpy);
    }
    return n;
}

static uint32_t x11_now_ms(zui_present_t *p)
{
    (void) p;
    return zui_x11_now_ms();
}

const zui_present_ops_t zui_present_x11_ops = {
    .name = "x11",
    .open = x11_open,
    .close = x11_close,
    .present = x11_present,
    .poll = x11_poll,
    .now_ms = x11_now_ms,
};
