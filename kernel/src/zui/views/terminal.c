/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* terminal.c — the Terminal's native screen (terminal.h). */
#include "terminal.h"
#include "zui_tokens.h"

enum { S_GROUND = 0, S_ESC, S_CSI, S_OSC, S_OSC_ESC, S_SKIP1 };

static uint32_t *line(term_view_t *t, uint32_t i)
{
    return t->cell[(t->head + i) % TERM_LINES];
}

static const uint32_t *cline(const term_view_t *t, uint32_t i)
{
    return t->cell[(t->head + i) % TERM_LINES];
}

static void blank(uint32_t *l, uint32_t from, uint32_t to)
{
    for (uint32_t c = from; c < to && c < TERM_COLS; c++) l[c] = 0;
}

static uint32_t screen_top(const term_view_t *t)
{
    return t->n > TERM_ROWS ? t->n - TERM_ROWS : 0;
}

/* T4: a new line at the bottom; the oldest falls off when full. */
static void append(term_view_t *t)
{
    if (t->n < TERM_LINES) {
        blank(line(t, t->n), 0, TERM_COLS);
        t->n++;
        return;
    }
    t->head = (t->head + 1u) % TERM_LINES;
    blank(line(t, t->n - 1u), 0, TERM_COLS);
    t->dropped++;
    if (t->row) t->row--;
}

static void newline(term_view_t *t)
{
    if (t->row + 1u < t->n) {
        t->row++;
        return;
    }
    append(t);
    t->row = t->n - 1u;
}

/* make line i exist (i on the screen) */
static void reach(term_view_t *t, uint32_t i)
{
    while (t->n <= i && t->n < TERM_LINES) append(t);
}

void term_reset(term_view_t *t)
{
    t->head = 0;
    t->n = 1;
    blank(line(t, 0), 0, TERM_COLS);
    t->row = t->col = 0;
    t->attr = 0;
    t->state = S_GROUND;
    t->u8_left = 0;
    t->follow = true;
}

static void put(term_view_t *t, uint32_t cp)
{
    if (t->col >= TERM_COLS) { /* wrap */
        t->col = 0;
        newline(t);
    }
    line(t, t->row)[t->col++] = (cp & 0x1FFFFFu) | t->attr;
}

static void control(term_view_t *t, uint8_t b)
{
    switch (b) {
    case '\r':
        t->col = 0;
        break;
    case '\n':
    case 0x0B: /* VT */
    case 0x0C: /* FF */
        newline(t);
        break;
    case '\b':
        if (t->col) t->col--;
        break;
    case '\t':
        t->col = (t->col / 8u + 1u) * 8u;
        if (t->col >= TERM_COLS) t->col = TERM_COLS - 1u;
        break;
    default: /* BEL and the rest: nothing */
        break;
    }
}

static uint32_t par(const term_view_t *t, uint32_t i, uint32_t def)
{
    return i < t->npar && t->par[i] ? t->par[i] : def;
}

static void sgr(term_view_t *t)
{
    if (!t->npar) t->attr = 0;
    for (uint32_t i = 0; i < t->npar; i++) {
        uint32_t p = t->par[i];
        uint32_t fg = TERM_FG(t->attr);
        uint32_t keep = t->attr & (TERM_BOLD | TERM_UNDER | TERM_INVERSE);
        if (p == 0) {
            t->attr = 0;
            continue;
        }
        if (p == 38 || p == 48) { /* extended colours: read, ignored (T2) */
            uint32_t mode = i + 1u < t->npar ? t->par[i + 1u] : 0;
            i += mode == 5 ? 2u : mode == 2 ? 4u : 0u;
            continue;
        }
        if (p == 1)
            keep |= TERM_BOLD;
        else if (p == 22)
            keep &= ~TERM_BOLD;
        else if (p == 4)
            keep |= TERM_UNDER;
        else if (p == 24)
            keep &= ~TERM_UNDER;
        else if (p == 7)
            keep |= TERM_INVERSE;
        else if (p == 27)
            keep &= ~TERM_INVERSE;
        else if (p >= 30 && p <= 37)
            fg = p - 29u;
        else if (p >= 90 && p <= 97)
            fg = p - 81u;
        else if (p == 39)
            fg = 0;
        t->attr = keep | (fg << 21);
    }
}

static void erase_line(term_view_t *t, uint32_t mode)
{
    uint32_t *l = line(t, t->row);
    if (mode == 0)
        blank(l, t->col, TERM_COLS);
    else if (mode == 1)
        blank(l, 0, t->col + 1u);
    else
        blank(l, 0, TERM_COLS);
}

static void erase_screen(term_view_t *t, uint32_t mode)
{
    uint32_t top = screen_top(t);
    if (mode == 3) { /* the scrollback too: keep only the screen */
        t->head = (t->head + top) % TERM_LINES;
        t->n -= top;
        t->row = t->row >= top ? t->row - top : 0;
        top = 0;
        mode = 2;
    }
    for (uint32_t i = top; i < t->n; i++) {
        if (mode == 0 && i < t->row) continue;
        if (mode == 1 && i > t->row) break;
        if (i == t->row && mode != 2)
            erase_line(t, mode);
        else
            blank(line(t, i), 0, TERM_COLS);
    }
}

static void csi(term_view_t *t, uint8_t f)
{
    if (t->priv) return; /* private modes (?25h and so on): not ours */
    uint32_t top = screen_top(t), k = par(t, 0, 1);
    switch (f) {
    case 'm':
        sgr(t);
        break;
    case 'K':
        erase_line(t, par(t, 0, 0));
        break;
    case 'J':
        erase_screen(t, par(t, 0, 0));
        break;
    case 'H':
    case 'f': {
        uint32_t r = par(t, 0, 1) - 1u, c = par(t, 1, 1) - 1u;
        if (r >= TERM_ROWS) r = TERM_ROWS - 1u;
        reach(t, top + r);
        top = screen_top(t);
        t->row = top + r < t->n ? top + r : t->n - 1u;
        t->col = c < TERM_COLS ? c : TERM_COLS - 1u;
        break;
    }
    case 'A':
        t->row = t->row >= top + k ? t->row - k : top;
        break;
    case 'B': {
        uint32_t want = t->row + k, last = top + TERM_ROWS - 1u;
        reach(t, want < last ? want : last);
        t->row = want < t->n ? want : t->n - 1u;
        break;
    }
    case 'C':
        t->col = t->col + k < TERM_COLS ? t->col + k : TERM_COLS - 1u;
        break;
    case 'D':
        t->col = t->col > k ? t->col - k : 0;
        break;
    case 'G':
        t->col = k - 1u < TERM_COLS ? k - 1u : TERM_COLS - 1u;
        break;
    default:
        break;
    }
}

static void byte(term_view_t *t, uint8_t b)
{
    if (b == 0x18 || b == 0x1A) { /* CAN, SUB: abandon a sequence */
        t->state = S_GROUND;
        return;
    }
    switch (t->state) {
    case S_ESC:
        t->state = S_GROUND;
        if (b == '[') {
            t->state = S_CSI;
            t->npar = 0;
            t->par_any = false;
            t->priv = false;
            for (uint32_t i = 0; i < 8u; i++) t->par[i] = 0;
        } else if (b == ']') {
            t->state = S_OSC;
        } else if (b == 'c') {
            term_reset(t);
        } else if (b == '(' || b == ')' || b == '#') {
            t->state = S_SKIP1; /* a character set: one more byte */
        }
        return;
    case S_SKIP1:
        t->state = S_GROUND;
        return;
    case S_CSI:
        if (b >= '0' && b <= '9') {
            if (!t->npar) t->npar = 1;
            uint32_t *p = &t->par[t->npar - 1u];
            if (*p < 10000u) *p = *p * 10u + (uint32_t) (b - '0');
        } else if (b == ';') {
            if (!t->npar) t->npar = 1;
            if (t->npar < 8u) t->npar++;
        } else if (b == '?' || b == '>' || b == '=' || b == '<') {
            t->priv = true;
        } else if (b >= 0x40 && b <= 0x7E) {
            t->state = S_GROUND;
            csi(t, b);
        } else if (b == 0x1B) {
            t->state = S_ESC;
        } else if (b < 0x20) {
            control(t, b);
        }
        return;
    case S_OSC:
        if (b == 0x07)
            t->state = S_GROUND;
        else if (b == 0x1B)
            t->state = S_OSC_ESC;
        return;
    case S_OSC_ESC:
        t->state = b == '\\' ? S_GROUND : S_OSC;
        return;
    default:
        break;
    }
    /* ground: UTF-8 text and controls */
    if (t->u8_left) {
        if ((b & 0xC0u) == 0x80u) {
            t->u8 = (t->u8 << 6) | (b & 0x3Fu);
            if (--t->u8_left == 0) put(t, t->u8 > 0x10FFFFu ? 0xFFFDu : t->u8);
            return;
        }
        t->u8_left = 0;
        put(t, 0xFFFDu); /* a cut sequence */
    }
    if (b == 0x1B) {
        t->state = S_ESC;
    } else if (b < 0x20 || b == 0x7F) {
        control(t, b);
    } else if (b < 0x80) {
        put(t, b);
    } else if ((b & 0xE0u) == 0xC0u) {
        t->u8 = b & 0x1Fu;
        t->u8_left = 1;
    } else if ((b & 0xF0u) == 0xE0u) {
        t->u8 = b & 0x0Fu;
        t->u8_left = 2;
    } else if ((b & 0xF8u) == 0xF0u) {
        t->u8 = b & 0x07u;
        t->u8_left = 3;
    } else {
        put(t, 0xFFFDu);
    }
}

void term_write(term_view_t *t, const char *bytes, uint32_t n)
{
    for (uint32_t i = 0; bytes && i < n; i++) byte(t, (uint8_t) bytes[i]);
    term_view_sync(t);
}

void term_puts(term_view_t *t, const char *s)
{
    uint32_t n = 0;
    while (s && s[n]) n++;
    term_write(t, s, n);
}

uint32_t term_lines(const term_view_t *t)
{
    return t->n;
}

uint32_t term_cell(const term_view_t *t, uint32_t i, uint32_t col)
{
    return i < t->n && col < TERM_COLS ? cline(t, i)[col] : 0;
}

static uint32_t utf8(uint32_t cp, char *o)
{
    if (cp < 0x80u) {
        o[0] = (char) cp;
        return 1;
    }
    if (cp < 0x800u) {
        o[0] = (char) (0xC0u | (cp >> 6));
        o[1] = (char) (0x80u | (cp & 0x3Fu));
        return 2;
    }
    if (cp < 0x10000u) {
        o[0] = (char) (0xE0u | (cp >> 12));
        o[1] = (char) (0x80u | ((cp >> 6) & 0x3Fu));
        o[2] = (char) (0x80u | (cp & 0x3Fu));
        return 3;
    }
    o[0] = (char) (0xF0u | (cp >> 18));
    o[1] = (char) (0x80u | ((cp >> 12) & 0x3Fu));
    o[2] = (char) (0x80u | ((cp >> 6) & 0x3Fu));
    o[3] = (char) (0x80u | (cp & 0x3Fu));
    return 4;
}

uint32_t term_line_text(const term_view_t *t, uint32_t i, char *out, uint32_t cap)
{
    if (!cap) return 0;
    out[0] = 0;
    if (i >= t->n) return 0;
    const uint32_t *l = cline(t, i);
    uint32_t end = TERM_COLS;
    while (end && (TERM_CP(l[end - 1u]) == 0 || TERM_CP(l[end - 1u]) == ' ')) end--;
    uint32_t n = 0;
    for (uint32_t c = 0; c < end; c++) {
        char u[4];
        uint32_t cp = TERM_CP(l[c]), k = utf8(cp ? cp : ' ', u);
        if (n + k + 1u > cap) break;
        for (uint32_t j = 0; j < k; j++) out[n++] = u[j];
    }
    out[n] = 0;
    return n;
}

zui_color_role_t term_role(uint32_t c)
{
    static const zui_color_role_t R[17] = {
        ZUI_C_TEXT,    ZUI_C_FAINT, ZUI_C_ALERT,     ZUI_C_OK,    ZUI_C_GOLD,  ZUI_C_INFO,
        ZUI_C_GOLD_HI, ZUI_C_INFO,  ZUI_C_TEXT,      ZUI_C_MUTED, ZUI_C_ALERT, ZUI_C_OK,
        ZUI_C_GOLD_HI, ZUI_C_INFO,  ZUI_C_GOLD_GLOW, ZUI_C_INFO,  ZUI_C_TEXT};
    uint32_t f = TERM_FG(c);
    return f <= 16u ? R[f] : ZUI_C_TEXT;
}

/* ---- the screen ---------------------------------------------------------------- */

static int32_t line_px(const zui_ui_t *ui)
{
    const zui_text_t *m = &ui->theme.mono;
    int32_t sz = m->size_px ? (int32_t) m->size_px : 14;
    if (m->font) return zui_text_ascent(m) - zui_text_descent(m) + 2;
    return (sz * 5 + 3) / 4;
}

static int32_t cell_px(const zui_ui_t *ui)
{
    const zui_text_t *m = &ui->theme.mono;
    int32_t sz = m->size_px ? (int32_t) m->size_px : 14;
    int32_t w = m->font ? zui_text_width(m, "0") : (sz * 3 + 2) / 5;
    return w > 0 ? w : 1;
}

static void paint(zui_ui_t *ui, zui_id_t w, zui_surface_t *s, zui_rect_t r, void *ctx)
{
    (void) w;
    term_view_t *t = ctx;
    const zui_text_t *m = &ui->theme.mono;
    int32_t lh = line_px(ui), cw = cell_px(ui), pad = zui_px(&ui->screen, ZUI_S2);
    int32_t asc = m->font ? zui_text_ascent(m) : lh * 4 / 5;
    zui_rect_t clip = s->clip;
    for (uint32_t i = 0; i < t->n; i++) {
        int32_t y = r.y + (int32_t) i * lh;
        if (y + lh <= clip.y || y >= clip.y + clip.h) continue;
        const uint32_t *l = cline(t, i);
        for (uint32_t c = 0; c < TERM_COLS; c++) {
            uint32_t cell = l[c], cp = TERM_CP(cell);
            int32_t x = r.x + pad + (int32_t) c * cw;
            if (x >= r.x + r.w) break;
            uint32_t rgb = zui_ui_color(ui, (cell & TERM_BOLD) && !TERM_FG(cell) ? ZUI_C_TEXT
                                                                                 : term_role(cell));
            if (cell & TERM_INVERSE) /* T3: a raised block, never a painted background */
                zui_fill(s, (zui_rect_t){x, y, cw, lh}, zui_ui_color(ui, ZUI_C_RAISED));
            if (cp && cp != ' ' && m->font) {
                char u[5];
                u[utf8(cp, u)] = 0;
                zui_text(s, m, x, y + asc, rgb, u);
                if (cell & TERM_BOLD) zui_text(s, m, x + 1, y + asc, rgb, u); /* overstrike */
            }
            if (cell & TERM_UNDER) zui_hline(s, x, y + asc + 2, cw, 1, rgb);
        }
    }
}

void term_view_build(term_view_t *t, zui_ui_t *ui, term_shell_fn shell, void *ctx)
{
    t->shell = shell;
    t->shell_ctx = ctx;
    t->in_buf[0] = 0;
    t->dropped = 0;
    term_reset(t);
    zv_shell_t *s = &t->sh;
    zv_shell_build(s, ui, "Terminal");
    zui_w(ui, s->body)->dir = ZUI_COL;
    t->scroll = zui_scroll(ui, s->body);
    zui_set_size(ui, t->scroll, ZUI_SIZE_FLEX, 1);
    zui_w(ui, t->scroll)->panel = true;
    t->screen = zui_canvas(ui, t->scroll, paint, t);
    zui_set_name(ui, t->screen, "Output");
    zui_id_t row = zui_box(ui, s->body, ZUI_ROW);
    zui_set_pad(ui, row, 0, ZUI_S2);
    t->prompt = zui_label(ui, row, "$");
    zui_set_font(ui, t->prompt, ZUI_TEXT_MONO, ZUI_MUTED);
    t->input = zui_field(ui, row, t->in_buf, sizeof t->in_buf, false);
    zui_set_name(ui, t->input, "Command");
    zui_set_size(ui, t->input, ZUI_SIZE_FLEX, 1);
    zui_w(ui, t->input)->font = ZUI_TEXT_MONO;
    zv_cat(zui_w(ui, t->input)->u.field.placeholder, ZUI_LABEL_MAX,
           shell ? "Type a command, then Enter" : "No shell is connected", 0);
    t->clear = zui_button(ui, row, "Clear", ZUI_BTN_GHOST);
    zui_set_tooltip(ui, t->clear, "Clear the scrollback");
    zui_focus(ui, t->input, false);
    term_view_sync(t);
}

void term_view_sync(term_view_t *t)
{
    zui_ui_t *ui = t->sh.ui;
    if (!ui) return;
    zui_widget_t *sc = zui_w(ui, t->scroll);
    int32_t lh = line_px(ui), scale = (int32_t) (ui->screen.scale_q8 ? ui->screen.scale_q8 : 256u);
    int32_t pt = ((int32_t) t->n * lh * 256 + scale - 1) / scale + 2 * ZUI_S2;
    if (sc) { /* T4: follow the newest line unless scrolled up */
        int32_t maxo = sc->u.scroll.content_h - sc->r.h;
        t->follow = maxo <= 0 || sc->u.scroll.offset >= maxo - lh;
    }
    zui_set_size(ui, t->screen, ZUI_SIZE_FIXED, pt);
    if (sc && t->follow) sc->u.scroll.offset = 1 << 28; /* clamped to the end by layout */
    zui_relayout(ui);
    zui_invalidate(ui, t->scroll);
    char st[48];
    uint32_t n = zv_u64(st, sizeof st, t->n);
    zv_cat(st + n, sizeof st - n, t->n == 1 ? " line" : " lines", 0);
    zv_shell_status(&t->sh, st);
}

bool term_view_action(term_view_t *t, zui_id_t w, zui_action_t a, int32_t arg)
{
    (void) arg;
    zui_ui_t *ui = t->sh.ui;
    if (w == t->input && a == ZUI_ACT_SUBMIT) {
        char cmd[TERM_INPUT_MAX];
        zv_cat(cmd, sizeof cmd, t->in_buf, 0);
        t->in_buf[0] = 0;
        zui_field_set(ui, t->input, 0);
        if (t->col) term_puts(t, "\r\n");
        term_puts(t, "\x1B[90m$\x1B[39m ");
        term_puts(t, cmd);
        term_puts(t, "\r\n");
        t->follow = true;
        if (t->shell)
            t->shell(t->shell_ctx, t, cmd);
        else
            term_puts(t, "\x1B[31mNo shell is connected.\x1B[39m\r\n");
        zui_set_hidden(ui, t->sh.error, true);
        term_view_sync(t);
        return true;
    }
    if (w == t->input && a == ZUI_ACT_CHANGE) return true;
    if (w == t->clear && a == ZUI_ACT_PRESS) {
        term_reset(t);
        term_view_sync(t);
        zui_focus(ui, t->input, false);
        return true;
    }
    return false;
}
