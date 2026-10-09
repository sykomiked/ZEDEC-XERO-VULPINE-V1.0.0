/* icon.c — vector icons. See icon.h. */
#include "icon.h"

/* ---- tiny integer rasteriser into a size x size RGB buffer ---- */
static uint32_t sc(uint32_t g, uint32_t size) { return g * size / ICON_GRID; }

static void px(uint32_t *b, uint32_t size, uint32_t x, uint32_t y, uint32_t rgb) {
    if (x < size && y < size) b[y * size + x] = rgb;
}
static void fill_rect(uint32_t *b, uint32_t size, uint32_t x, uint32_t y,
                      uint32_t w, uint32_t h, uint32_t rgb) {
    for (uint32_t j = 0; j < h; j++) for (uint32_t i = 0; i < w; i++) px(b, size, x+i, y+j, rgb);
}
static void fill_disc(uint32_t *b, uint32_t size, int cx, int cy, int r, uint32_t rgb) {
    if (r <= 0) return;
    for (int y = cy - r; y <= cy + r; y++)
        for (int x = cx - r; x <= cx + r; x++) {
            int dx = x - cx, dy = y - cy;
            if (dx*dx + dy*dy <= r*r && x >= 0 && y >= 0) px(b, size, (uint32_t)x, (uint32_t)y, rgb);
        }
}
static void draw_line(uint32_t *b, uint32_t size, int x0, int y0, int x1, int y1, uint32_t rgb) {
    int dx = x1 - x0, dy = y1 - y0;
    int adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
    int sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1;
    int err = (adx > ady ? adx : -ady) / 2, e2;
    int th = (int)(size / 32); if (th < 1) th = 1;   /* a little thickness */
    for (;;) {
        for (int j = -th; j <= th; j++) for (int i = -th; i <= th; i++)
            if (x0+i >= 0 && y0+j >= 0) px(b, size, (uint32_t)(x0+i), (uint32_t)(y0+j), rgb);
        if (x0 == x1 && y0 == y1) break;
        e2 = err;
        if (e2 > -adx) { err -= ady; x0 += sx; }
        if (e2 <  ady) { err += adx; y0 += sy; }
    }
}
static int edge(int ax,int ay,int bx,int by,int px_,int py_){return (bx-ax)*(py_-ay)-(by-ay)*(px_-ax);}
static void fill_tri(uint32_t *b, uint32_t size, int x0,int y0,int x1,int y1,int x2,int y2,uint32_t rgb){
    int minx=x0<x1?(x0<x2?x0:x2):(x1<x2?x1:x2);
    int maxx=x0>x1?(x0>x2?x0:x2):(x1>x2?x1:x2);
    int miny=y0<y1?(y0<y2?y0:y2):(y1<y2?y1:y2);
    int maxy=y0>y1?(y0>y2?y0:y2):(y1>y2?y1:y2);
    if (minx < 0) minx = 0;
    if (miny < 0) miny = 0;
    for (int y=miny;y<=maxy;y++) for(int x=minx;x<=maxx;x++){
        int w0=edge(x1,y1,x2,y2,x,y), w1=edge(x2,y2,x0,y0,x,y), w2=edge(x0,y0,x1,y1,x,y);
        if ((w0>=0&&w1>=0&&w2>=0)||(w0<=0&&w1<=0&&w2<=0)) px(b,size,(uint32_t)x,(uint32_t)y,rgb);
    }
}

void icon_render(const icon_t *ic, const theme_t *theme, uint32_t *buf, uint32_t size) {
    if (!ic || !theme || !buf || size == 0) return;
    uint32_t bg = theme_get(theme, (theme_color_t)ic->bg);
    for (uint32_t i = 0; i < size*size; i++) buf[i] = bg;
    for (uint32_t k = 0; k < ic->n_ops && k < ICON_MAX_OPS; k++) {
        const icon_op_t *o = &ic->op[k];
        uint32_t rgb = theme_get(theme, (theme_color_t)o->color);
        switch (o->shape) {
        case ICON_RECT: fill_rect(buf,size, sc(o->c[0],size),sc(o->c[1],size),
                                  sc(o->c[2],size),sc(o->c[3],size), rgb); break;
        case ICON_DISC: fill_disc(buf,size, (int)sc(o->c[0],size),(int)sc(o->c[1],size),
                                  (int)sc(o->c[2],size), rgb); break;
        case ICON_LINE: draw_line(buf,size, (int)sc(o->c[0],size),(int)sc(o->c[1],size),
                                  (int)sc(o->c[2],size),(int)sc(o->c[3],size), rgb); break;
        case ICON_TRI:  fill_tri(buf,size, (int)sc(o->c[0],size),(int)sc(o->c[1],size),
                                 (int)sc(o->c[2],size),(int)sc(o->c[3],size),
                                 (int)sc(o->c[4],size),(int)sc(o->c[5],size), rgb); break;
        default: break;
        }
    }
}

/* ---- op builders ---- */
#define OP_RECT(x,y,w,h,col) { ICON_RECT, {x,y,w,h,0,0}, col }
#define OP_DISC(cx,cy,r,col) { ICON_DISC, {cx,cy,r,0,0,0}, col }
#define OP_LINE(a,b,c,d,col) { ICON_LINE, {a,b,c,d,0,0}, col }
#define OP_TRI(a,b,c,d,e,f,col) { ICON_TRI, {a,b,c,d,e,f}, col }

/* ---- hand-drawn headline icons (house style) ---- */
struct named_icon { const char *name; icon_t ic; };
static const struct named_icon BUILTINS[] = {
  /* kernel — a dragon's slit eye */
  { "kernel", { THEME_VOID, 4, {
      OP_DISC(32,32,28,THEME_SCALE), OP_DISC(32,32,20,THEME_EMBER),
      OP_DISC(32,32,12,THEME_GOLD),  OP_RECT(29,10,6,44,THEME_VOID) } } },
  /* net — three linked nodes */
  { "net", { THEME_VOID, 6, {
      OP_LINE(16,18,50,22,THEME_ACCENT), OP_LINE(50,22,32,50,THEME_ACCENT),
      OP_LINE(32,50,16,18,THEME_ACCENT),
      OP_DISC(16,18,7,THEME_AZURE), OP_DISC(50,22,7,THEME_AZURE), OP_DISC(32,50,7,THEME_AZURE) } } },
  /* tls — a shield with a keyhole */
  { "tls", { THEME_VOID, 4, {
      OP_RECT(18,12,28,22,THEME_BORDER), OP_TRI(18,34,46,34,32,56,THEME_BORDER),
      OP_DISC(32,26,4,THEME_GOLD), OP_RECT(30,26,4,10,THEME_GOLD) } } },
  /* mage — a wizard's hat with a star */
  { "mage", { THEME_VOID, 3, {
      OP_TRI(32,6,14,46,50,46,THEME_ARCANE), OP_RECT(10,45,44,7,THEME_GOLD),
      OP_DISC(30,26,3,THEME_GOLD) } } },
  /* reality — concentric ripples over a live point */
  { "reality", { THEME_VOID, 5, {
      OP_DISC(32,32,28,THEME_AZURE), OP_DISC(32,32,21,THEME_VOID),
      OP_DISC(32,32,14,THEME_AZURE), OP_DISC(32,32,7,THEME_VOID),
      OP_DISC(32,32,3,THEME_ACCENT) } } },
  /* trispace — S+/S-/S0 as three overlapping fields */
  { "trispace", { THEME_VOID, 3, {
      OP_DISC(24,26,15,THEME_JADE), OP_DISC(40,26,15,THEME_DANGER),
      OP_DISC(32,42,15,THEME_AZURE) } } },
  /* chiglet — a hatchling egg */
  { "chiglet", { THEME_VOID, 3, {
      OP_DISC(32,36,20,THEME_GOLD), OP_TRI(24,30,40,30,32,20,THEME_ACCENT),
      OP_DISC(32,40,4,THEME_EMBER) } } },
  /* cards — a card bearing a sigil */
  { "cards", { THEME_VOID, 5, {
      OP_RECT(18,10,28,44,THEME_PANEL_HI), OP_RECT(18,10,28,44,THEME_BORDER),
      OP_LINE(24,20,40,44,THEME_ARCANE), OP_LINE(40,20,24,44,THEME_ARCANE),
      OP_DISC(32,32,3,THEME_GOLD) } } },
  /* holodeck — a play glyph in a frame */
  { "holodeck", { THEME_VOID, 2, {
      OP_RECT(12,16,40,32,THEME_BORDER), OP_TRI(26,24,26,40,44,32,THEME_ACCENT) } } },
  /* wallet — a small hoard */
  { "wallet", { THEME_VOID, 3, {
      OP_DISC(24,40,12,THEME_GOLD), OP_DISC(40,40,12,THEME_GOLD),
      OP_DISC(32,28,12,THEME_GOLD) } } },
  /* denconnect — a cave mouth */
  { "denconnect", { THEME_VOID, 3, {
      OP_DISC(32,40,22,THEME_SCALE), OP_DISC(32,44,14,THEME_VOID),
      OP_DISC(32,30,4,THEME_ACCENT) } } },
  /* browser — a compass globe */
  { "browser", { THEME_VOID, 4, {
      OP_DISC(32,32,26,THEME_AZURE), OP_LINE(8,32,56,32,THEME_TEXT),
      OP_LINE(32,8,32,56,THEME_TEXT), OP_DISC(32,32,4,THEME_GOLD) } } },
};
#define NBUILTIN (sizeof BUILTINS / sizeof BUILTINS[0])

static bool streq(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *a == *b) { a++; b++; }
    return *a == 0 && *b == 0;
}

bool icon_is_builtin(const char *name) {
    for (uint32_t i = 0; i < NBUILTIN; i++) if (streq(BUILTINS[i].name, name)) return true;
    return false;
}

/* ---- procedural icons: a distinct on-palette glyph from any name ---- */
static uint32_t fnv1a(const char *s) {
    uint32_t h = 2166136261u;
    if (s) while (*s) { h ^= (uint8_t)*s++; h *= 16777619u; }
    return h;
}

void icon_procedural(const char *name, icon_t *out) {
    if (!out) return;
    for (uint32_t i = 0; i < sizeof(*out); i++) ((uint8_t *)out)[i] = 0; /* determinism */
    uint32_t h = fnv1a(name);
    /* on-palette ink choices (never VOID, so a shape is always visible) */
    static const uint8_t inks[] = {
        THEME_ACCENT, THEME_GOLD, THEME_AZURE, THEME_JADE, THEME_ARCANE,
        THEME_EMBER, THEME_DANGER, THEME_TEXT
    };
    out->bg = THEME_PANEL;               /* a dark tile so any ink reads */
    uint32_t n = 2u + (h % 3u);          /* 2..4 shapes */
    out->n_ops = (uint8_t)n;
    for (uint32_t i = 0; i < n; i++) {
        h = h * 1103515245u + 12345u;    /* advance */
        uint32_t r = h;
        icon_op_t *o = &out->op[i];
        uint8_t kind = (uint8_t)((r >> 3) % 4u);
        uint8_t col = inks[(r >> 5) % (sizeof inks)];
        uint8_t cx = (uint8_t)(12 + (r >> 7) % 40);
        uint8_t cy = (uint8_t)(12 + (r >> 13) % 40);
        uint8_t s  = (uint8_t)(8 + (r >> 19) % 18);
        o->color = col;
        switch (kind) {
        case 0: o->shape = ICON_DISC; o->c[0]=cx; o->c[1]=cy; o->c[2]=(uint8_t)(s/2+4); break;
        case 1: o->shape = ICON_RECT; o->c[0]=cx; o->c[1]=cy; o->c[2]=s; o->c[3]=s; break;
        case 2: o->shape = ICON_TRI;  o->c[0]=cx; o->c[1]=(uint8_t)(cy-s/2);
                o->c[2]=(uint8_t)(cx-s/2); o->c[3]=(uint8_t)(cy+s/2);
                o->c[4]=(uint8_t)(cx+s/2); o->c[5]=(uint8_t)(cy+s/2); break;
        default:o->shape = ICON_LINE; o->c[0]=cx; o->c[1]=cy;
                o->c[2]=(uint8_t)((cx+s)%64); o->c[3]=(uint8_t)((cy+s)%64); break;
        }
    }
}

void icon_for(const char *name, icon_t *out) {
    if (!out) return;
    for (uint32_t i = 0; i < NBUILTIN; i++)
        if (streq(BUILTINS[i].name, name)) { *out = BUILTINS[i].ic; return; }
    icon_procedural(name, out);
}
