/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_x11_window.c — the X11 window and its input, for the X11 and Vulkan
 * present layers (see zui_x11_window.h).
 *
 * Keys: the unshifted keysym of the key decides ZUI_KEY_* or the lower-case
 * ASCII value, so Ctrl-K and Ctrl-Shift-K are both 'k' with modifiers. Text
 * comes separately from the input context (Xutf8LookupString), which is
 * where compose sequences and input methods commit; while an input method is
 * composing, its pre-edit text arrives through callbacks as ZUI_EV_PREEDIT.
 */
#define _GNU_SOURCE
#include "zui_x11_window.h"

#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wchar.h>
#include <X11/XKBlib.h>
#include <X11/Xatom.h>
#include <X11/Xresource.h>
#include <X11/keysym.h>

uint32_t zui_x11_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t) ((uint64_t) ts.tv_sec * 1000u + (uint64_t) ts.tv_nsec / 1000000u);
}

/* ---- small helpers ------------------------------------------------------- */

static uint32_t utf8_put(char *out, uint32_t cp)
{
    if (cp < 0x80) {
        out[0] = (char) cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (char) (0xC0 | (cp >> 6));
        out[1] = (char) (0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp >= 0xD800 && cp < 0xE000) cp = 0xFFFD;
    if (cp < 0x10000) {
        out[0] = (char) (0xE0 | (cp >> 12));
        out[1] = (char) (0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char) (0x80 | (cp & 0x3F));
        return 3;
    }
    if (cp > 0x10FFFF) cp = 0xFFFD;
    out[0] = (char) (0xF0 | (cp >> 18));
    out[1] = (char) (0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char) (0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char) (0x80 | (cp & 0x3F));
    return 4;
}

/* Committed text can be longer than one event holds: send it in whole-char
 * pieces so nothing typed is lost. Control characters are not text. */
static void push_text_all(zui_x11_window_t *xw, const char *s, uint32_t n, uint32_t ms)
{
    while (n > 0) {
        uint32_t take = zui_utf8_fit(s, n, ZUI_TEXT_EV_MAX);
        if (take == 0) break;
        char piece[ZUI_TEXT_EV_MAX + 1];
        memcpy(piece, s, take);
        piece[take] = 0;
        zui_push_text(xw->q, ZUI_EV_TEXT, piece, ms);
        xw->pushed++;
        s += take;
        n -= take;
    }
}

static void push_plain(zui_x11_window_t *xw, uint8_t type, int32_t x, int32_t y, uint32_t ms)
{
    zui_event_t e;
    memset(&e, 0, sizeof e);
    e.type = type;
    e.x = x;
    e.y = y;
    e.ms = ms;
    e.mods = xw->q->mods;
    zui_push(xw->q, &e);
    xw->pushed++;
}

static uint8_t mods_from_state(unsigned int st)
{
    uint8_t m = 0;
    if (st & ShiftMask) m |= ZUI_MOD_SHIFT;
    if (st & ControlMask) m |= ZUI_MOD_CTRL | ZUI_MOD_CMD;
    if (st & Mod1Mask) m |= ZUI_MOD_ALT;
    if (st & Mod4Mask) m |= ZUI_MOD_META;
    return m;
}

/* The modifier a key press or release itself changes (the event's state is
 * from before it). */
static uint8_t mod_of_keysym(KeySym ks)
{
    switch (ks) {
    case XK_Shift_L:
    case XK_Shift_R:
        return ZUI_MOD_SHIFT;
    case XK_Control_L:
    case XK_Control_R:
        return ZUI_MOD_CTRL | ZUI_MOD_CMD;
    case XK_Alt_L:
    case XK_Alt_R:
    case XK_Meta_L:
    case XK_Meta_R:
        return ZUI_MOD_ALT;
    case XK_Super_L:
    case XK_Super_R:
        return ZUI_MOD_META;
    default:
        return 0;
    }
}

static uint16_t key_from_keysym(KeySym ks)
{
    if (ks >= XK_F1 && ks <= XK_F12) return (uint16_t) (ZUI_KEY_F1 + (ks - XK_F1));
    switch (ks) {
    case XK_BackSpace:
        return ZUI_KEY_BACKSPACE;
    case XK_Tab:
    case XK_ISO_Left_Tab:
        return ZUI_KEY_TAB;
    case XK_Return:
    case XK_KP_Enter:
        return ZUI_KEY_ENTER;
    case XK_Escape:
        return ZUI_KEY_ESCAPE;
    case XK_Delete:
    case XK_KP_Delete:
        return ZUI_KEY_DELETE;
    case XK_Left:
    case XK_KP_Left:
        return ZUI_KEY_LEFT;
    case XK_Right:
    case XK_KP_Right:
        return ZUI_KEY_RIGHT;
    case XK_Up:
    case XK_KP_Up:
        return ZUI_KEY_UP;
    case XK_Down:
    case XK_KP_Down:
        return ZUI_KEY_DOWN;
    case XK_Home:
    case XK_KP_Home:
        return ZUI_KEY_HOME;
    case XK_End:
    case XK_KP_End:
        return ZUI_KEY_END;
    case XK_Page_Up:
    case XK_KP_Page_Up:
        return ZUI_KEY_PAGE_UP;
    case XK_Page_Down:
    case XK_KP_Page_Down:
        return ZUI_KEY_PAGE_DOWN;
    case XK_Insert:
    case XK_KP_Insert:
        return ZUI_KEY_INSERT;
    case XK_Menu:
        return ZUI_KEY_MENU;
    case XK_Cancel:
        return ZUI_KEY_BACK;
    default:
        break;
    }
    if (ks >= 0x20 && ks <= 0x7E) {
        if (ks >= 'A' && ks <= 'Z') ks += 'a' - 'A';
        return (uint16_t) ks;
    }
    return ZUI_KEY_NONE;
}

/* ---- input method: pre-edit callbacks ------------------------------------ */

static void preedit_send(zui_x11_window_t *xw)
{
    if (!xw->q) return;
    char buf[ZUI_X11_PREEDIT_MAX * 4 + 1];
    uint32_t n = 0;
    for (uint32_t i = 0; i < xw->preedit_n; i++) n += utf8_put(buf + n, xw->preedit[i]);
    buf[n] = 0;
    zui_push_text(xw->q, ZUI_EV_PREEDIT, buf, zui_x11_now_ms());
    xw->pushed++;
}

static Bool preedit_start(XIC ic, XPointer client, XPointer call)
{
    (void) ic;
    (void) call;
    zui_x11_window_t *xw = (zui_x11_window_t *) client;
    xw->preedit_n = 0;
    return -1; /* no length limit */
}

static void preedit_done(XIM im, XPointer client, XPointer call)
{
    (void) im;
    (void) call;
    zui_x11_window_t *xw = (zui_x11_window_t *) client;
    xw->preedit_n = 0;
    preedit_send(xw);
}

static void preedit_draw(XIM im, XPointer client, XPointer call)
{
    (void) im;
    zui_x11_window_t *xw = (zui_x11_window_t *) client;
    XIMPreeditDrawCallbackStruct *d = (XIMPreeditDrawCallbackStruct *) call;
    if (!d) return;
    uint32_t first = d->chg_first < 0 ? 0 : (uint32_t) d->chg_first;
    uint32_t len = d->chg_length < 0 ? 0 : (uint32_t) d->chg_length;
    if (first > xw->preedit_n) first = xw->preedit_n;
    if (first + len > xw->preedit_n) len = xw->preedit_n - first;

    /* the new characters, decoded to code points */
    uint32_t ins[ZUI_X11_PREEDIT_MAX];
    uint32_t nins = 0;
    if (d->text) {
        if (d->text->encoding_is_wchar) {
            for (uint32_t i = 0; i < d->text->length && nins < ZUI_X11_PREEDIT_MAX; i++)
                ins[nins++] = (uint32_t) d->text->string.wide_char[i];
        } else if (d->text->string.multi_byte) {
            const char *s = d->text->string.multi_byte;
            size_t left = strlen(s);
            mbstate_t st;
            memset(&st, 0, sizeof st);
            while (left > 0 && nins < ZUI_X11_PREEDIT_MAX) {
                wchar_t wc;
                size_t r = mbrtowc(&wc, s, left, &st);
                if (r == (size_t) -1 || r == (size_t) -2) break;
                if (r == 0) break;
                ins[nins++] = (uint32_t) wc;
                s += r;
                left -= r;
            }
        }
    }
    /* replace [first, first + len) with ins */
    uint32_t tail = xw->preedit_n - first - len;
    if (first + nins + tail > ZUI_X11_PREEDIT_MAX) {
        if (first + nins > ZUI_X11_PREEDIT_MAX) nins = ZUI_X11_PREEDIT_MAX - first;
        tail = ZUI_X11_PREEDIT_MAX - first - nins;
    }
    memmove(xw->preedit + first + nins, xw->preedit + first + len, tail * sizeof(uint32_t));
    memcpy(xw->preedit + first, ins, nins * sizeof(uint32_t));
    xw->preedit_n = first + nins + tail;
    preedit_send(xw);
}

static void preedit_caret(XIM im, XPointer client, XPointer call)
{
    (void) im;
    (void) client;
    (void) call; /* the caret is drawn by zui at the end of the pre-edit */
}

static void open_input_method(zui_x11_window_t *xw)
{
    /* The locale's modifiers pick the input method (XMODIFIERS); when none
     * can be opened, fall back to the built-in one, which still does compose
     * sequences. */
    if (!XSetLocaleModifiers("")) XSetLocaleModifiers("@im=none");
    xw->xim = XOpenIM(xw->dpy, NULL, NULL, NULL);
    if (!xw->xim) {
        XSetLocaleModifiers("@im=none");
        xw->xim = XOpenIM(xw->dpy, NULL, NULL, NULL);
    }
    if (!xw->xim) return;

    XIMStyles *styles = NULL;
    bool have_cb = false, have_nothing = false, have_none = false;
    if (!XGetIMValues(xw->xim, XNQueryInputStyle, &styles, NULL) && styles) {
        for (unsigned i = 0; i < styles->count_styles; i++) {
            XIMStyle s = styles->supported_styles[i];
            if (s == (XIMPreeditCallbacks | XIMStatusNothing)) have_cb = true;
            if (s == (XIMPreeditNothing | XIMStatusNothing)) have_nothing = true;
            if (s == (XIMPreeditNone | XIMStatusNone)) have_none = true;
        }
        XFree(styles);
    }

    if (have_cb) {
        xw->cb_start.client_data = (XPointer) xw;
        xw->cb_start.callback = preedit_start;
        xw->cb_done.client_data = (XPointer) xw;
        xw->cb_done.callback = preedit_done;
        xw->cb_draw.client_data = (XPointer) xw;
        xw->cb_draw.callback = preedit_draw;
        xw->cb_caret.client_data = (XPointer) xw;
        xw->cb_caret.callback = preedit_caret;
        XVaNestedList attr = XVaCreateNestedList(
            0, XNPreeditStartCallback, &xw->cb_start, XNPreeditDoneCallback, &xw->cb_done,
            XNPreeditDrawCallback, &xw->cb_draw, XNPreeditCaretCallback, &xw->cb_caret, NULL);
        xw->xic =
            XCreateIC(xw->xim, XNInputStyle, XIMPreeditCallbacks | XIMStatusNothing, XNClientWindow,
                      xw->win, XNFocusWindow, xw->win, XNPreeditAttributes, attr, NULL);
        XFree(attr);
    }
    if (!xw->xic && have_nothing)
        xw->xic = XCreateIC(xw->xim, XNInputStyle, XIMPreeditNothing | XIMStatusNothing,
                            XNClientWindow, xw->win, XNFocusWindow, xw->win, NULL);
    if (!xw->xic && have_none)
        xw->xic = XCreateIC(xw->xim, XNInputStyle, XIMPreeditNone | XIMStatusNone, XNClientWindow,
                            xw->win, XNFocusWindow, xw->win, NULL);
    if (!xw->xic) {
        XCloseIM(xw->xim);
        xw->xim = NULL;
    }
}

/* ---- open / close -------------------------------------------------------- */

static uint32_t read_scale_q8(Display *dpy)
{
    uint32_t q8 = 256;
    char *rms = XResourceManagerString(dpy);
    if (!rms) return q8;
    XrmInitialize();
    XrmDatabase db = XrmGetStringDatabase(rms);
    if (!db) return q8;
    char *type = NULL;
    XrmValue v;
    if (XrmGetResource(db, "Xft.dpi", "Xft.Dpi", &type, &v) && v.addr) {
        double dpi = strtod(v.addr, NULL);
        if (dpi >= 24.0 && dpi <= 960.0) q8 = (uint32_t) (dpi * 256.0 / 96.0 + 0.5);
    }
    XrmDestroyDatabase(db);
    return q8;
}

int zui_x11_open(zui_x11_window_t *xw, zui_screen_t *scr, const char *title, int32_t w_pt,
                 int32_t h_pt)
{
    memset(xw, 0, sizeof *xw);
    if (w_pt <= 0 || h_pt <= 0) return ZUI_PRESENT_E_ARG;
    xw->dpy = XOpenDisplay(NULL);
    if (!xw->dpy) return ZUI_PRESENT_E_UNAVAILABLE;
    xw->screen = DefaultScreen(xw->dpy);
    xw->visual = DefaultVisual(xw->dpy, xw->screen);
    xw->depth = DefaultDepth(xw->dpy, xw->screen);

    scr->scale_q8 = read_scale_q8(xw->dpy);
    scr->input = ZUI_INPUT_POINTER;
    int32_t w = zui_px(scr, w_pt), h = zui_px(scr, h_pt);
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    scr->px_w = (uint32_t) w;
    scr->px_h = (uint32_t) h;

    XSetWindowAttributes swa;
    memset(&swa, 0, sizeof swa);
    swa.background_pixmap = None; /* no flash of another colour: zui paints */
    swa.event_mask = KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask |
                     PointerMotionMask | StructureNotifyMask | ExposureMask | FocusChangeMask;
    xw->win =
        XCreateWindow(xw->dpy, RootWindow(xw->dpy, xw->screen), 0, 0, (unsigned) w, (unsigned) h, 0,
                      xw->depth, InputOutput, xw->visual, CWBackPixmap | CWEventMask, &swa);
    if (!xw->win) {
        XCloseDisplay(xw->dpy);
        xw->dpy = NULL;
        return ZUI_PRESENT_E_UNAVAILABLE;
    }

    /* title (legacy and UTF-8), class "zxv", and the close button */
    const char *t = title ? title : "zxv";
    XStoreName(xw->dpy, xw->win, t);
    Atom net_name = XInternAtom(xw->dpy, "_NET_WM_NAME", False);
    Atom utf8 = XInternAtom(xw->dpy, "UTF8_STRING", False);
    XChangeProperty(xw->dpy, xw->win, net_name, utf8, 8, PropModeReplace, (const unsigned char *) t,
                    (int) strlen(t));
    XClassHint *ch = XAllocClassHint();
    if (ch) {
        ch->res_name = (char *) "zxv";
        ch->res_class = (char *) "zxv";
        XSetClassHint(xw->dpy, xw->win, ch);
        XFree(ch);
    }
    xw->wm_protocols = XInternAtom(xw->dpy, "WM_PROTOCOLS", False);
    xw->wm_delete = XInternAtom(xw->dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(xw->dpy, xw->win, &xw->wm_delete, 1);

    /* a held key repeats as presses only, so repeats can be marked */
    Bool supported = False;
    XkbSetDetectableAutoRepeat(xw->dpy, True, &supported);

    open_input_method(xw);
    if (xw->xic) {
        long im_mask = 0;
        if (!XGetICValues(xw->xic, XNFilterEvents, &im_mask, NULL) && im_mask)
            XSelectInput(xw->dpy, xw->win, swa.event_mask | im_mask);
    }

    XMapWindow(xw->dpy, xw->win);
    XFlush(xw->dpy);
    return ZUI_PRESENT_OK;
}

void zui_x11_close(zui_x11_window_t *xw)
{
    if (!xw->dpy) return;
    if (xw->xic) XDestroyIC(xw->xic);
    if (xw->xim) XCloseIM(xw->xim);
    if (xw->win) XDestroyWindow(xw->dpy, xw->win);
    XCloseDisplay(xw->dpy);
    memset(xw, 0, sizeof *xw);
}

/* ---- events -------------------------------------------------------------- */

static void on_key(zui_x11_window_t *xw, XKeyEvent *ke, bool down)
{
    uint32_t ms = (uint32_t) ke->time;
    KeySym ks0 = XLookupKeysym(ke, 0); /* the unshifted symbol: the key itself */
    uint16_t key = key_from_keysym(ks0);
    uint8_t mods = mods_from_state(ke->state);
    uint8_t own = mod_of_keysym(ks0);
    mods = down ? (uint8_t) (mods | own) : (uint8_t) (mods & ~own);

    unsigned kc = ke->keycode & 0xFF;
    bool repeat = false;
    if (down) {
        repeat = (xw->held[kc >> 3] >> (kc & 7)) & 1;
        xw->held[kc >> 3] |= (uint8_t) (1u << (kc & 7));
    } else {
        xw->held[kc >> 3] &= (uint8_t) ~(1u << (kc & 7));
    }

    if (key != ZUI_KEY_NONE || own) {
        zui_push_key(xw->q, down, key, mods, repeat, ms);
        xw->pushed++;
    }
    if (!down) return;

    /* committed text, from the input context when there is one */
    char sbuf[64];
    char *buf = sbuf;
    int n = 0;
    KeySym ks = NoSymbol;
    if (xw->xic) {
        Status st = 0;
        n = Xutf8LookupString(xw->xic, ke, buf, (int) sizeof sbuf - 1, &ks, &st);
        if (st == XBufferOverflow) {
            buf = malloc((size_t) n + 1);
            if (!buf) return;
            n = Xutf8LookupString(xw->xic, ke, buf, n, &ks, &st);
        }
        if (st != XLookupChars && st != XLookupBoth) n = 0;
    } else {
        /* no input method: Latin-1 from XLookupString, re-encoded as UTF-8 */
        char l1[32];
        int m = XLookupString(ke, l1, (int) sizeof l1, &ks, NULL);
        for (int i = 0; i < m && n < (int) sizeof sbuf - 5; i++)
            n += (int) utf8_put(sbuf + n, (uint8_t) l1[i]);
    }
    /* Shortcuts are keys, not text: with Ctrl held nothing is typed. */
    if (n > 0 && !(ke->state & ControlMask) && (uint8_t) buf[0] >= 0x20 && buf[0] != 0x7F)
        push_text_all(xw, buf, (uint32_t) n, ms);
    if (buf != sbuf) free(buf);
}

static void on_button(zui_x11_window_t *xw, const zui_screen_t *scr, XButtonEvent *be, bool down)
{
    uint32_t ms = (uint32_t) be->time;
    if (be->button >= 4 && be->button <= 7) {
        if (!down) return;
        /* one wheel notch is 40 px at 1x: +dy moves the content up */
        int32_t step = zui_px(scr, 40);
        int32_t dx = 0, dy = 0;
        if (be->button == 4) dy = -step;
        if (be->button == 5) dy = step;
        if (be->button == 6) dx = -step;
        if (be->button == 7) dx = step;
        zui_push_scroll(xw->q, be->x, be->y, dx, dy, ms);
        xw->pushed++;
        return;
    }
    uint8_t b = be->button == 1   ? ZUI_BTN_LEFT
                : be->button == 2 ? ZUI_BTN_MIDDLE
                : be->button == 3 ? ZUI_BTN_RIGHT
                                  : 0;
    if (!b) return;
    if (down)
        xw->buttons |= b;
    else
        xw->buttons &= (uint8_t) ~b;
    zui_push_ptr(xw->q, down ? ZUI_EV_PTR_DOWN : ZUI_EV_PTR_UP, be->x, be->y, b, ms);
    xw->pushed++;
}

static void add_expose(zui_x11_window_t *xw, int32_t x, int32_t y, int32_t w, int32_t h)
{
    if (!xw->exposed) {
        xw->exposed = true;
        xw->ex0 = x;
        xw->ey0 = y;
        xw->ex1 = x + w;
        xw->ey1 = y + h;
        return;
    }
    if (x < xw->ex0) xw->ex0 = x;
    if (y < xw->ey0) xw->ey0 = y;
    if (x + w > xw->ex1) xw->ex1 = x + w;
    if (y + h > xw->ey1) xw->ey1 = y + h;
}

bool zui_x11_take_expose(zui_x11_window_t *xw, zui_rect_t *r)
{
    if (!xw->exposed) return false;
    xw->exposed = false;
    r->x = xw->ex0;
    r->y = xw->ey0;
    r->w = xw->ex1 - xw->ex0;
    r->h = xw->ey1 - xw->ey0;
    return r->w > 0 && r->h > 0;
}

static void on_event(zui_x11_window_t *xw, zui_screen_t *scr, XEvent *ev)
{
    /* the input method sees every event first; what it keeps is not ours */
    if (XFilterEvent(ev, None)) return;
    switch (ev->type) {
    case KeyPress:
        on_key(xw, &ev->xkey, true);
        break;
    case KeyRelease:
        on_key(xw, &ev->xkey, false);
        break;
    case ButtonPress:
        on_button(xw, scr, &ev->xbutton, true);
        break;
    case ButtonRelease:
        on_button(xw, scr, &ev->xbutton, false);
        break;
    case MotionNotify:
        zui_push_ptr(xw->q, ZUI_EV_PTR_MOVE, ev->xmotion.x, ev->xmotion.y, xw->buttons,
                     (uint32_t) ev->xmotion.time);
        xw->pushed++;
        break;
    case FocusIn:
        if (ev->xfocus.mode == NotifyGrab || ev->xfocus.mode == NotifyUngrab) break;
        if (xw->xic) XSetICFocus(xw->xic);
        push_plain(xw, ZUI_EV_FOCUS_IN, 0, 0, zui_x11_now_ms());
        break;
    case FocusOut:
        if (ev->xfocus.mode == NotifyGrab || ev->xfocus.mode == NotifyUngrab) break;
        if (xw->xic) XUnsetICFocus(xw->xic);
        memset(xw->held, 0, sizeof xw->held);
        push_plain(xw, ZUI_EV_FOCUS_OUT, 0, 0, zui_x11_now_ms());
        break;
    case ConfigureNotify: {
        uint32_t w = (uint32_t) ev->xconfigure.width, h = (uint32_t) ev->xconfigure.height;
        if (w && h && (w != scr->px_w || h != scr->px_h)) {
            scr->px_w = w;
            scr->px_h = h;
            push_plain(xw, ZUI_EV_RESIZE, (int32_t) w, (int32_t) h, zui_x11_now_ms());
        }
        break;
    }
    case Expose:
        add_expose(xw, ev->xexpose.x, ev->xexpose.y, ev->xexpose.width, ev->xexpose.height);
        break;
    case ClientMessage:
        if (ev->xclient.message_type == xw->wm_protocols &&
            (Atom) ev->xclient.data.l[0] == xw->wm_delete)
            push_plain(xw, ZUI_EV_CLOSE, 0, 0, zui_x11_now_ms());
        break;
    case DestroyNotify:
        if (ev->xdestroywindow.window == xw->win) xw->closed = true;
        break;
    case MappingNotify:
        if (ev->xmapping.request == MappingKeyboard || ev->xmapping.request == MappingModifier)
            XRefreshKeyboardMapping(&ev->xmapping);
        break;
    default:
        break;
    }
}

int zui_x11_poll(zui_x11_window_t *xw, zui_screen_t *scr, zui_queue_t *q, uint32_t wait_ms)
{
    if (!xw->dpy || xw->closed) return ZUI_PRESENT_E_LOST;
    xw->q = q;
    xw->pushed = 0;
    if (wait_ms && !XPending(xw->dpy)) {
        struct pollfd pfd = {.fd = ConnectionNumber(xw->dpy), .events = POLLIN};
        int r = poll(&pfd, 1, (int) (wait_ms > 0x7FFFFFFF ? 0x7FFFFFFF : wait_ms));
        if (r > 0 && (pfd.revents & (POLLHUP | POLLERR))) {
            xw->q = NULL;
            xw->closed = true;
            return ZUI_PRESENT_E_LOST;
        }
    }
    while (XPending(xw->dpy)) {
        XEvent ev;
        XNextEvent(xw->dpy, &ev);
        on_event(xw, scr, &ev);
    }
    xw->q = NULL;
    if (xw->closed) return ZUI_PRESENT_E_LOST;
    return xw->pushed;
}
