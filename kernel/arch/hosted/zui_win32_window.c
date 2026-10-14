/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_win32_window.c — the Win32 window and its input, for the Direct3D 11
 * and Vulkan present layers (see zui_win32_window.h).
 *
 * Keys: WM_KEYDOWN's virtual key decides ZUI_KEY_* or the lower-case ASCII
 * value; text comes separately from WM_CHAR / WM_UNICHAR, and from the IME
 * (WM_IME_COMPOSITION) as committed text and as pre-edit, which zui draws
 * itself at the caret instead of the system's composition window. Touch comes
 * from WM_POINTER (Windows 8 and later); the functions that are newer than
 * Windows 7 are looked up at run time so the program still starts there.
 */
#include "zui_win32_window.h"

#include <imm.h>
#include <string.h>
#include <windowsx.h>

#define ZUI_W32_CLASS L"zxv"

/* ---- functions looked up at run time ------------------------------------- */

typedef BOOL(WINAPI *set_dpi_ctx_fn)(HANDLE);
typedef UINT(WINAPI *dpi_for_window_fn)(HWND);
typedef UINT(WINAPI *dpi_for_system_fn)(void);
typedef BOOL(WINAPI *adjust_for_dpi_fn)(LPRECT, DWORD, BOOL, DWORD, UINT);
typedef BOOL(WINAPI *pointer_type_fn)(UINT32, DWORD *);

static dpi_for_window_fn p_dpi_for_window;
static dpi_for_system_fn p_dpi_for_system;
static adjust_for_dpi_fn p_adjust_for_dpi;
static pointer_type_fn p_pointer_type;

static void *user32_fn(const char *name)
{
    HMODULE u = GetModuleHandleW(L"user32.dll");
    if (!u) return NULL;
    FARPROC fp = GetProcAddress(u, name);
    void *p = NULL;
    memcpy(&p, &fp, sizeof p);
    return p;
}

static void load_functions(void)
{
    static bool done;
    if (done) return;
    done = true;
    void *p = user32_fn("SetProcessDpiAwarenessContext");
    if (p) {
        set_dpi_ctx_fn set_ctx;
        memcpy(&set_ctx, &p, sizeof set_ctx);
        set_ctx((HANDLE) (intptr_t) -4); /* DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 */
    } else if ((p = user32_fn("SetProcessDPIAware")) != NULL) {
        BOOL(WINAPI * aware)(void);
        memcpy(&aware, &p, sizeof aware);
        aware();
    }
    if ((p = user32_fn("GetDpiForWindow")) != NULL)
        memcpy(&p_dpi_for_window, &p, sizeof p_dpi_for_window);
    if ((p = user32_fn("GetDpiForSystem")) != NULL)
        memcpy(&p_dpi_for_system, &p, sizeof p_dpi_for_system);
    if ((p = user32_fn("AdjustWindowRectExForDpi")) != NULL)
        memcpy(&p_adjust_for_dpi, &p, sizeof p_adjust_for_dpi);
    if ((p = user32_fn("GetPointerType")) != NULL)
        memcpy(&p_pointer_type, &p, sizeof p_pointer_type);
}

static UINT window_dpi(HWND hwnd)
{
    if (hwnd && p_dpi_for_window) return p_dpi_for_window(hwnd);
    if (p_dpi_for_system) return p_dpi_for_system();
    HDC dc = GetDC(NULL);
    UINT dpi = dc ? (UINT) GetDeviceCaps(dc, LOGPIXELSX) : 96;
    if (dc) ReleaseDC(NULL, dc);
    return dpi ? dpi : 96;
}

static uint32_t q8_of_dpi(UINT dpi)
{
    return (uint32_t) ((dpi * 256u + 48u) / 96u);
}

uint32_t zui_win32_now_ms(void)
{
    return (uint32_t) GetTickCount64();
}

/* ---- the event ring ------------------------------------------------------ */

static zui_event_t *ev_new(zui_win32_window_t *ww, uint8_t type)
{
    if (ww->head - ww->tail >= ZUI_W32_RING) ww->tail++; /* full: lose the oldest */
    zui_event_t *e = &ww->ring[ww->head++ % ZUI_W32_RING];
    memset(e, 0, sizeof *e);
    e->type = type;
    e->ms = (uint32_t) GetMessageTime();
    return e;
}

static uint8_t mods_now(void)
{
    uint8_t m = 0;
    if (GetKeyState(VK_SHIFT) & 0x8000) m |= ZUI_MOD_SHIFT;
    if (GetKeyState(VK_CONTROL) & 0x8000) m |= ZUI_MOD_CTRL | ZUI_MOD_CMD;
    if (GetKeyState(VK_MENU) & 0x8000) m |= ZUI_MOD_ALT;
    if ((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000) m |= ZUI_MOD_META;
    return m;
}

static void ev_ptr(zui_win32_window_t *ww, uint8_t type, int32_t x, int32_t y, uint8_t button)
{
    zui_event_t *e = ev_new(ww, type);
    e->x = x;
    e->y = y;
    e->button = button;
    e->mods = mods_now();
}

/* UTF-16 -> UTF-8 text events, cut on whole characters to fit each event */
static void ev_text_w(zui_win32_window_t *ww, uint8_t type, const WCHAR *w, int wn)
{
    char buf[512];
    int n =
        wn > 0 ? WideCharToMultiByte(CP_UTF8, 0, w, wn, buf, (int) sizeof buf - 1, NULL, NULL) : 0;
    if (n < 0) n = 0;
    buf[n] = 0;
    if (type == ZUI_EV_PREEDIT || n == 0) {
        zui_event_t *e = ev_new(ww, type);
        uint32_t k = zui_utf8_fit(buf, (uint32_t) n, ZUI_TEXT_EV_MAX);
        memcpy(e->text, buf, k);
        e->text[k] = 0;
        return;
    }
    uint32_t at = 0, left = (uint32_t) n;
    while (left > 0) {
        uint32_t k = zui_utf8_fit(buf + at, left, ZUI_TEXT_EV_MAX);
        if (!k) break;
        zui_event_t *e = ev_new(ww, ZUI_EV_TEXT);
        e->mods = mods_now();
        memcpy(e->text, buf + at, k);
        e->text[k] = 0;
        at += k;
        left -= k;
    }
}

static void ev_codepoint(zui_win32_window_t *ww, uint32_t cp)
{
    if (cp < 0x20 || cp == 0x7F || (cp >= 0x80 && cp < 0xA0)) return; /* not text */
    WCHAR w[2];
    int n = 1;
    if (cp >= 0x10000) {
        cp -= 0x10000;
        w[0] = (WCHAR) (0xD800 + (cp >> 10));
        w[1] = (WCHAR) (0xDC00 + (cp & 0x3FF));
        n = 2;
    } else {
        w[0] = (WCHAR) cp;
    }
    ev_text_w(ww, ZUI_EV_TEXT, w, n);
}

static uint16_t key_from_vk(WPARAM vk)
{
    if (vk >= VK_F1 && vk <= VK_F12) return (uint16_t) (ZUI_KEY_F1 + (vk - VK_F1));
    if (vk >= 'A' && vk <= 'Z') return (uint16_t) (vk - 'A' + 'a');
    if (vk >= '0' && vk <= '9') return (uint16_t) vk;
    switch (vk) {
    case VK_BACK:
        return ZUI_KEY_BACKSPACE;
    case VK_TAB:
        return ZUI_KEY_TAB;
    case VK_RETURN:
        return ZUI_KEY_ENTER;
    case VK_ESCAPE:
        return ZUI_KEY_ESCAPE;
    case VK_SPACE:
        return ZUI_KEY_SPACE;
    case VK_DELETE:
        return ZUI_KEY_DELETE;
    case VK_LEFT:
        return ZUI_KEY_LEFT;
    case VK_RIGHT:
        return ZUI_KEY_RIGHT;
    case VK_UP:
        return ZUI_KEY_UP;
    case VK_DOWN:
        return ZUI_KEY_DOWN;
    case VK_HOME:
        return ZUI_KEY_HOME;
    case VK_END:
        return ZUI_KEY_END;
    case VK_PRIOR:
        return ZUI_KEY_PAGE_UP;
    case VK_NEXT:
        return ZUI_KEY_PAGE_DOWN;
    case VK_INSERT:
        return ZUI_KEY_INSERT;
    case VK_APPS:
        return ZUI_KEY_MENU;
    case VK_BROWSER_BACK:
        return ZUI_KEY_BACK;
    default:
        break;
    }
    /* punctuation: the character the key makes without modifiers */
    UINT ch = MapVirtualKeyW((UINT) vk, MAPVK_VK_TO_CHAR) & 0x7FFFu;
    if (ch >= 0x20 && ch < 0x7F) {
        if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
        return (uint16_t) ch;
    }
    return ZUI_KEY_NONE;
}

static bool is_modifier_vk(WPARAM vk)
{
    return vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU || vk == VK_LWIN || vk == VK_RWIN;
}

static void on_key(zui_win32_window_t *ww, WPARAM vk, LPARAM lp, bool down)
{
    uint16_t key = key_from_vk(vk);
    if (key == ZUI_KEY_NONE && !is_modifier_vk(vk)) return;
    zui_event_t *e = ev_new(ww, down ? ZUI_EV_KEY_DOWN : ZUI_EV_KEY_UP);
    e->key = key;
    e->mods = mods_now();
    e->repeat = down && ((lp >> 30) & 1) ? 1 : 0;
}

/* Mouse messages Windows makes up from touch: the touch already came. */
static bool from_touch(void)
{
    return (GetMessageExtraInfo() & 0xFFFFFF00u) == 0xFF515700u;
}

static void on_button(zui_win32_window_t *ww, HWND hwnd, LPARAM lp, uint8_t b, bool down)
{
    if (from_touch()) return;
    if (down) {
        if (!ww->buttons) SetCapture(hwnd);
        ww->buttons |= b;
    } else {
        ww->buttons &= (uint8_t) ~b;
        if (!ww->buttons) ReleaseCapture();
    }
    ev_ptr(ww, down ? ZUI_EV_PTR_DOWN : ZUI_EV_PTR_UP, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), b);
}

static void on_wheel(zui_win32_window_t *ww, HWND hwnd, WPARAM wp, LPARAM lp, bool horizontal)
{
    POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
    ScreenToClient(hwnd, &pt);
    int32_t delta = (int16_t) HIWORD(wp);
    /* one notch (120) is 40 points; +dy moves the content up */
    int32_t step = zui_px(&ww->scr, 40);
    int32_t v = delta * step / WHEEL_DELTA;
    zui_event_t *e = ev_new(ww, ZUI_EV_SCROLL);
    e->x = pt.x;
    e->y = pt.y;
    e->mods = mods_now();
    if (horizontal)
        e->dx = v; /* tilt right is positive */
    else
        e->dy = -v; /* wheel away from you is positive: content down */
}

/* ---- touch through WM_POINTER -------------------------------------------- */

static int touch_slot(zui_win32_window_t *ww, UINT32 pid, bool primary, bool make)
{
    for (uint32_t i = 0; i < ZUI_W32_TOUCH; i++)
        if (ww->touch_used[i] && ww->touch_ptr[i] == pid) return (int) i;
    if (!make) return -1;
    /* the primary finger is touch 0, which is also the pointer (I2) */
    uint32_t from = primary ? 0 : 1;
    for (uint32_t i = from; i < ZUI_W32_TOUCH; i++)
        if (!ww->touch_used[i]) {
            ww->touch_used[i] = true;
            ww->touch_ptr[i] = pid;
            return (int) i;
        }
    return -1;
}

static bool on_pointer(zui_win32_window_t *ww, HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (!p_pointer_type) return false;
    UINT32 pid = GET_POINTERID_WPARAM(wp);
    DWORD type = 0;
    if (!p_pointer_type(pid, &type) || type != PT_TOUCH) return false; /* mouse, pen: default */
    POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
    ScreenToClient(hwnd, &pt);
    bool primary = IS_POINTER_PRIMARY_WPARAM(wp);
    uint8_t t;
    int slot;
    if (msg == WM_POINTERDOWN) {
        t = ZUI_EV_TOUCH_BEGIN;
        slot = touch_slot(ww, pid, primary, true);
    } else if (msg == WM_POINTERUPDATE) {
        t = ZUI_EV_TOUCH_MOVE;
        slot = touch_slot(ww, pid, primary, false);
    } else {
        t = msg == WM_POINTERCAPTURECHANGED ? ZUI_EV_TOUCH_CANCEL : ZUI_EV_TOUCH_END;
        slot = touch_slot(ww, pid, primary, false);
    }
    if (slot < 0) return true;
    zui_event_t *e = ev_new(ww, t);
    e->touch_id = (uint16_t) slot;
    e->x = pt.x;
    e->y = pt.y;
    if (t == ZUI_EV_TOUCH_END || t == ZUI_EV_TOUCH_CANCEL) ww->touch_used[slot] = false;
    return true;
}

/* ---- IME ----------------------------------------------------------------- */

static void ime_string(zui_win32_window_t *ww, HIMC imc, DWORD which, uint8_t type)
{
    LONG bytes = ImmGetCompositionStringW(imc, which, NULL, 0);
    if (bytes < 0) return;
    WCHAR w[256];
    if (bytes > (LONG) sizeof w) bytes = (LONG) sizeof w;
    bytes = ImmGetCompositionStringW(imc, which, w, (DWORD) bytes);
    if (bytes < 0) return;
    ev_text_w(ww, type, w, (int) (bytes / (LONG) sizeof(WCHAR)));
}

/* ---- the window procedure ------------------------------------------------ */

static void set_size(zui_win32_window_t *ww, uint32_t w, uint32_t h)
{
    if (!w || !h || (w == ww->scr.px_w && h == ww->scr.px_h)) return;
    ww->scr.px_w = w;
    ww->scr.px_h = h;
    zui_event_t *e = ev_new(ww, ZUI_EV_RESIZE);
    e->x = (int32_t) w;
    e->y = (int32_t) h;
}

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    zui_win32_window_t *ww = (zui_win32_window_t *) GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (msg == WM_NCCREATE) {
        CREATESTRUCTW *cs = (CREATESTRUCTW *) lp;
        ww = (zui_win32_window_t *) cs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR) ww);
    }
    if (!ww) return DefWindowProcW(hwnd, msg, wp, lp);

    switch (msg) {
    case WM_KEYDOWN:
        on_key(ww, wp, lp, true);
        return 0;
    case WM_KEYUP:
        on_key(ww, wp, lp, false);
        return 0;
    case WM_SYSKEYDOWN:
        on_key(ww, wp, lp, true);
        break; /* Alt-F4 still closes */
    case WM_SYSKEYUP:
        on_key(ww, wp, lp, false);
        break;
    case WM_SYSCHAR:
        return 0; /* Alt shortcuts are keys; no beep */
    case WM_CHAR: {
        WCHAR c = (WCHAR) wp;
        if (c >= 0xD800 && c < 0xDC00) {
            ww->high = c;
            return 0;
        }
        if (c >= 0xDC00 && c < 0xE000) {
            if (ww->high)
                ev_codepoint(ww, 0x10000u + (((uint32_t) ww->high - 0xD800u) << 10) +
                                     ((uint32_t) c - 0xDC00u));
            ww->high = 0;
            return 0;
        }
        ww->high = 0;
        ev_codepoint(ww, c);
        return 0;
    }
    case WM_UNICHAR:
        if (wp == UNICODE_NOCHAR) return TRUE;
        ev_codepoint(ww, (uint32_t) wp);
        return FALSE;

    case WM_IME_SETCONTEXT:
        lp &= ~(LPARAM) ISC_SHOWUICOMPOSITIONWINDOW; /* zui draws the pre-edit */
        break;
    case WM_IME_STARTCOMPOSITION:
        return 0;
    case WM_IME_COMPOSITION: {
        HIMC imc = ImmGetContext(hwnd);
        if (!imc) break;
        if (lp & GCS_RESULTSTR) {
            ime_string(ww, imc, GCS_RESULTSTR, ZUI_EV_TEXT);
            ev_text_w(ww, ZUI_EV_PREEDIT, NULL, 0);
        }
        if (lp & GCS_COMPSTR) ime_string(ww, imc, GCS_COMPSTR, ZUI_EV_PREEDIT);
        ImmReleaseContext(hwnd, imc);
        return 0; /* handled: no WM_IME_CHAR / WM_CHAR copy of the result */
    }
    case WM_IME_ENDCOMPOSITION:
        ev_text_w(ww, ZUI_EV_PREEDIT, NULL, 0);
        return 0;

    case WM_LBUTTONDOWN:
        on_button(ww, hwnd, lp, ZUI_BTN_LEFT, true);
        return 0;
    case WM_LBUTTONUP:
        on_button(ww, hwnd, lp, ZUI_BTN_LEFT, false);
        return 0;
    case WM_RBUTTONDOWN:
        on_button(ww, hwnd, lp, ZUI_BTN_RIGHT, true);
        return 0;
    case WM_RBUTTONUP:
        on_button(ww, hwnd, lp, ZUI_BTN_RIGHT, false);
        return 0;
    case WM_MBUTTONDOWN:
        on_button(ww, hwnd, lp, ZUI_BTN_MIDDLE, true);
        return 0;
    case WM_MBUTTONUP:
        on_button(ww, hwnd, lp, ZUI_BTN_MIDDLE, false);
        return 0;
    case WM_MOUSEMOVE:
        if (!from_touch())
            ev_ptr(ww, ZUI_EV_PTR_MOVE, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), ww->buttons);
        return 0;
    case WM_CAPTURECHANGED:
        ww->buttons = 0;
        break;
    case WM_MOUSEWHEEL:
        on_wheel(ww, hwnd, wp, lp, false);
        return 0;
    case WM_MOUSEHWHEEL:
        on_wheel(ww, hwnd, wp, lp, true);
        return 0;

    case WM_POINTERDOWN:
    case WM_POINTERUPDATE:
    case WM_POINTERUP:
    case WM_POINTERCAPTURECHANGED:
        if (on_pointer(ww, hwnd, msg, wp, lp)) return 0;
        break;

    case WM_SIZE:
        if (wp != SIZE_MINIMIZED) set_size(ww, LOWORD(lp), HIWORD(lp));
        return 0;
    case WM_DPICHANGED: {
        uint32_t q8 = q8_of_dpi(LOWORD(wp));
        if (q8 != ww->scr.scale_q8) {
            ww->scr.scale_q8 = q8;
            zui_event_t *e = ev_new(ww, ZUI_EV_SCALE);
            e->x = (int32_t) q8;
        }
        const RECT *r = (const RECT *) lp;
        SetWindowPos(hwnd, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1; /* zui paints every pixel */
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        ww->redraw = true;
        return 0;
    }
    case WM_SETFOCUS:
        ev_new(ww, ZUI_EV_FOCUS_IN);
        return 0;
    case WM_KILLFOCUS:
        ev_new(ww, ZUI_EV_FOCUS_OUT);
        ww->high = 0;
        return 0;
    case WM_CLOSE:
        ev_new(ww, ZUI_EV_CLOSE);
        return 0; /* the app decides */
    case WM_DESTROY:
        ww->closed = true;
        ww->hwnd = NULL;
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ---- open / close / poll ------------------------------------------------- */

int zui_win32_open(zui_win32_window_t *ww, zui_screen_t *scr, const char *title, int32_t w_pt,
                   int32_t h_pt)
{
    memset(ww, 0, sizeof *ww);
    if (w_pt <= 0 || h_pt <= 0) return ZUI_PRESENT_E_ARG;
    load_functions();
    ww->inst = GetModuleHandleW(NULL);

    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = wndproc;
    wc.hInstance = ww->inst;
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR) IDC_ARROW);
    wc.hIcon = LoadIconW(NULL, (LPCWSTR) IDI_APPLICATION);
    wc.lpszClassName = ZUI_W32_CLASS;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return ZUI_PRESENT_E_UNAVAILABLE;

    WCHAR wtitle[256];
    if (!MultiByteToWideChar(CP_UTF8, 0, title ? title : "zxv", -1, wtitle, 256)) wtitle[0] = 0;

    /* size the client area for the DPI the window is likely to open on */
    UINT dpi = window_dpi(NULL);
    ww->scr.scale_q8 = q8_of_dpi(dpi);
    ww->scr.input = ZUI_INPUT_POINTER;
    DWORD style = WS_OVERLAPPEDWINDOW, exstyle = WS_EX_APPWINDOW;
    int32_t cw = zui_px(&ww->scr, w_pt), ch = zui_px(&ww->scr, h_pt);
    RECT r = {0, 0, cw, ch};
    if (p_adjust_for_dpi)
        p_adjust_for_dpi(&r, style, FALSE, exstyle, dpi);
    else
        AdjustWindowRectEx(&r, style, FALSE, exstyle);
    ww->hwnd = CreateWindowExW(exstyle, ZUI_W32_CLASS, wtitle, style, CW_USEDEFAULT, CW_USEDEFAULT,
                               r.right - r.left, r.bottom - r.top, NULL, NULL, ww->inst, ww);
    if (!ww->hwnd) return ZUI_PRESENT_E_UNAVAILABLE;

    /* the monitor it landed on may have another DPI */
    UINT wdpi = window_dpi(ww->hwnd);
    if (wdpi != dpi) {
        dpi = wdpi;
        ww->scr.scale_q8 = q8_of_dpi(dpi);
        cw = zui_px(&ww->scr, w_pt);
        ch = zui_px(&ww->scr, h_pt);
        RECT r2 = {0, 0, cw, ch};
        if (p_adjust_for_dpi)
            p_adjust_for_dpi(&r2, style, FALSE, exstyle, dpi);
        else
            AdjustWindowRectEx(&r2, style, FALSE, exstyle);
        SetWindowPos(ww->hwnd, NULL, 0, 0, r2.right - r2.left, r2.bottom - r2.top,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    RECT cr;
    GetClientRect(ww->hwnd, &cr);
    ww->scr.px_w = (uint32_t) (cr.right > 0 ? cr.right : 1);
    ww->scr.px_h = (uint32_t) (cr.bottom > 0 ? cr.bottom : 1);
    ww->head = ww->tail = 0; /* creation-time sizes are already in scr */

    ShowWindow(ww->hwnd, SW_SHOWNORMAL);
    UpdateWindow(ww->hwnd);
    *scr = ww->scr;
    return ZUI_PRESENT_OK;
}

void zui_win32_close(zui_win32_window_t *ww)
{
    if (ww->hwnd) {
        SetWindowLongPtrW(ww->hwnd, GWLP_USERDATA, 0);
        DestroyWindow(ww->hwnd);
    }
    ww->hwnd = NULL;
    ww->closed = true;
}

int zui_win32_poll(zui_win32_window_t *ww, zui_screen_t *scr, zui_queue_t *q, uint32_t wait_ms)
{
    if (ww->closed) return ZUI_PRESENT_E_LOST;
    if (wait_ms && ww->head == ww->tail)
        MsgWaitForMultipleObjectsEx(0, NULL, wait_ms, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    MSG m;
    while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) {
        if (m.message == WM_QUIT) {
            /* someone asked the whole program to stop: pass it on */
            PostQuitMessage((int) m.wParam);
            ev_new(ww, ZUI_EV_CLOSE);
            break;
        }
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    int n = 0;
    while (ww->tail != ww->head) {
        zui_push(q, &ww->ring[ww->tail++ % ZUI_W32_RING]);
        n++;
    }
    *scr = ww->scr;
    if (ww->closed) return ZUI_PRESENT_E_LOST;
    return n;
}

bool zui_win32_take_redraw(zui_win32_window_t *ww)
{
    bool r = ww->redraw;
    ww->redraw = false;
    return r;
}
