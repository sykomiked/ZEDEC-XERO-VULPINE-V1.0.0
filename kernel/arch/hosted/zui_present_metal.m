/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_present_metal.m — the Metal present layer: macOS.
 *
 * An NSWindow whose content view is backed by a CAMetalLayer. zui draws on
 * the CPU, so Metal only moves pixels:
 *
 *   1. replaceRegion: copies each damaged rectangle of the app's surface into
 *      a persistent frame-sized MTLTexture (P1). The texture keeps the last
 *      frame, because a drawable comes back with undefined contents.
 *   2. A blit encoder copies that texture into the layer's next drawable,
 *      which is then presented.
 *
 * Texture, layer and drawable are MTLPixelFormatBGRA8Unorm, which is
 * XRGB8888's byte order, and the layer is opaque with no colour space set
 * (no colour matching), so the bytes reach the display unchanged (P2).
 * scale_q8 is backingScaleFactor * 256: 512 on a Retina panel (P4).
 *
 * Input: keyDown gives ZUI_EV_KEY_DOWN and then goes through
 * interpretKeyEvents, so the system input methods work: insertText: commits
 * ZUI_EV_TEXT, setMarkedText: shows ZUI_EV_PREEDIT, unmarkText ends it.
 * Command is ZUI_MOD_META | ZUI_MOD_CMD. Mouse, precise scroll deltas,
 * resize, backing-scale changes, key-window focus and the close button are
 * also turned into zui events. Events are kept in a ring here (Cocoa calls
 * the view whenever it likes) and handed to the caller's queue by poll(),
 * which drains NSApp's event queue with nextEventMatchingMask:untilDate:.
 *
 * Memory: ARC. Build with -fobjc-arc (checked below); link with
 * -framework Cocoa -framework Metal -framework QuartzCore. Plain Objective-C,
 * Cocoa, Metal and QuartzCore only. Call everything from the main thread.
 */
#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <math.h>
#include <string.h>
#include "zui_present.h"

#if !__has_feature(objc_arc)
#    error "zui_present_metal.m uses ARC: build it with -fobjc-arc"
#endif

#define ZUI_MTL_RING 512u

/* ---- state --------------------------------------------------------------- */

@class ZuiMetalState;

@interface ZuiMetalView : NSView <NSTextInputClient>
@property(nonatomic, weak) ZuiMetalState *state;
@end

@interface ZuiWindowDelegate : NSObject <NSWindowDelegate>
@property(nonatomic, weak) ZuiMetalState *state;
@end

@interface ZuiMetalState : NSObject {
  @public
    zui_event_t ring[ZUI_MTL_RING];
    uint32_t head, tail;
    zui_screen_t scr; /* the window's facts as of the last event */
    uint8_t buttons;
    NSEventModifierFlags lastFlags;
    BOOL closed;
    BOOL redraw;
    BOOL marked; /* an input method is composing */
    int32_t fw, fh;
    BOOL frameUndefined;
}
@property(nonatomic, strong) NSWindow *window;
@property(nonatomic, strong) ZuiMetalView *view;
@property(nonatomic, strong) ZuiWindowDelegate *delegate;
@property(nonatomic, strong) CAMetalLayer *layer;
@property(nonatomic, strong) id<MTLDevice> device;
@property(nonatomic, strong) id<MTLCommandQueue> queue;
@property(nonatomic, strong) id<MTLTexture> frame;
@property(nonatomic, strong) id<MTLCommandBuffer> lastCommands;
@end

@implementation ZuiMetalState
@end

static uint32_t ms_of(NSTimeInterval t)
{
    return (uint32_t) (uint64_t) (t * 1000.0);
}

static zui_event_t *ev_new(ZuiMetalState *st, uint8_t type, NSTimeInterval t)
{
    if (st->head - st->tail >= ZUI_MTL_RING) st->tail++; /* full: lose the oldest */
    zui_event_t *e = &st->ring[st->head++ % ZUI_MTL_RING];
    memset(e, 0, sizeof *e);
    e->type = type;
    e->ms = ms_of(t);
    return e;
}

static NSTimeInterval now_s(void)
{
    return [[NSProcessInfo processInfo] systemUptime]; /* NSEvent.timestamp's clock */
}

static uint8_t mods_of(NSEventModifierFlags f)
{
    uint8_t m = 0;
    if (f & NSEventModifierFlagShift) m |= ZUI_MOD_SHIFT;
    if (f & NSEventModifierFlagControl) m |= ZUI_MOD_CTRL;
    if (f & NSEventModifierFlagOption) m |= ZUI_MOD_ALT;
    if (f & NSEventModifierFlagCommand) m |= ZUI_MOD_META | ZUI_MOD_CMD;
    return m;
}

/* UTF-8 text events, cut on whole characters to fit each one. */
static void ev_text(ZuiMetalState *st, uint8_t type, NSString *s, NSTimeInterval t)
{
    const char *u = s ? [s UTF8String] : "";
    if (!u) u = "";
    uint32_t n = (uint32_t) strlen(u);
    if (type == ZUI_EV_PREEDIT || n == 0) {
        zui_event_t *e = ev_new(st, type, t);
        uint32_t k = zui_utf8_fit(u, n, ZUI_TEXT_EV_MAX);
        memcpy(e->text, u, k);
        e->text[k] = 0;
        return;
    }
    while (n > 0) {
        uint32_t k = zui_utf8_fit(u, n, ZUI_TEXT_EV_MAX);
        if (!k) break;
        zui_event_t *e = ev_new(st, ZUI_EV_TEXT, t);
        memcpy(e->text, u, k);
        e->text[k] = 0;
        u += k;
        n -= k;
    }
}

static uint32_t q8_of_scale(CGFloat f)
{
    return (uint32_t) lround(f * 256.0);
}

/* Device pixels of the view, from its size in points and the scale. */
static void update_size(ZuiMetalState *st, BOOL announce)
{
    NSView *v = st.view;
    CGFloat f = st.window.backingScaleFactor;
    if (f <= 0) f = 1;
    uint32_t q8 = q8_of_scale(f);
    NSSize sz = v.bounds.size;
    uint32_t w = (uint32_t) lround(sz.width * f), h = (uint32_t) lround(sz.height * f);
    if (q8 != st->scr.scale_q8) {
        st->scr.scale_q8 = q8;
        st.layer.contentsScale = f;
        if (announce) ev_new(st, ZUI_EV_SCALE, now_s())->x = (int32_t) q8;
    }
    if (w && h && (w != st->scr.px_w || h != st->scr.px_h)) {
        st->scr.px_w = w;
        st->scr.px_h = h;
        st.layer.drawableSize = CGSizeMake(w, h);
        if (announce) {
            zui_event_t *e = ev_new(st, ZUI_EV_RESIZE, now_s());
            e->x = (int32_t) w;
            e->y = (int32_t) h;
        }
    }
}

/* ---- keys ---------------------------------------------------------------- */

/* Virtual key codes of the non-text keys (the values of Carbon's kVK_*,
 * written out so only Cocoa is needed). */
static uint16_t key_of_code(unsigned short code)
{
    switch (code) {
    case 0x24:
    case 0x4C:
        return ZUI_KEY_ENTER; /* Return, keypad Enter */
    case 0x30:
        return ZUI_KEY_TAB;
    case 0x31:
        return ZUI_KEY_SPACE;
    case 0x33:
        return ZUI_KEY_BACKSPACE; /* "Delete" on a Mac keyboard */
    case 0x35:
        return ZUI_KEY_ESCAPE;
    case 0x75:
        return ZUI_KEY_DELETE; /* forward delete */
    case 0x73:
        return ZUI_KEY_HOME;
    case 0x77:
        return ZUI_KEY_END;
    case 0x74:
        return ZUI_KEY_PAGE_UP;
    case 0x79:
        return ZUI_KEY_PAGE_DOWN;
    case 0x7B:
        return ZUI_KEY_LEFT;
    case 0x7C:
        return ZUI_KEY_RIGHT;
    case 0x7D:
        return ZUI_KEY_DOWN;
    case 0x7E:
        return ZUI_KEY_UP;
    case 0x72:
        return ZUI_KEY_INSERT; /* Help, where Insert sits */
    case 0x6E:
        return ZUI_KEY_MENU; /* contextual menu key */
    case 0x7A:
        return ZUI_KEY_F1;
    case 0x78:
        return ZUI_KEY_F1 + 1;
    case 0x63:
        return ZUI_KEY_F1 + 2;
    case 0x76:
        return ZUI_KEY_F1 + 3;
    case 0x60:
        return ZUI_KEY_F1 + 4;
    case 0x61:
        return ZUI_KEY_F1 + 5;
    case 0x62:
        return ZUI_KEY_F1 + 6;
    case 0x64:
        return ZUI_KEY_F1 + 7;
    case 0x65:
        return ZUI_KEY_F1 + 8;
    case 0x6D:
        return ZUI_KEY_F1 + 9;
    case 0x67:
        return ZUI_KEY_F1 + 10;
    case 0x6F:
        return ZUI_KEY_F1 + 11;
    default:
        return ZUI_KEY_NONE;
    }
}

static uint16_t key_of_event(NSEvent *ev)
{
    uint16_t k = key_of_code(ev.keyCode);
    if (k != ZUI_KEY_NONE) return k;
    /* the character the key makes with no modifiers at all */
    NSString *c = nil;
    if (@available(macOS 10.15, *)) c = [ev charactersByApplyingModifiers:0];
    if (!c.length) c = ev.charactersIgnoringModifiers;
    if (!c.length) return ZUI_KEY_NONE;
    unichar u = [c characterAtIndex:0];
    if (u >= 'A' && u <= 'Z') u = (unichar) (u + ('a' - 'A'));
    return (u >= 0x20 && u < 0x7F) ? (uint16_t) u : ZUI_KEY_NONE;
}

/* ---- the view ------------------------------------------------------------ */

@implementation ZuiMetalView

- (BOOL)isFlipped
{
    return YES; /* y grows down, as in zui */
}

- (BOOL)acceptsFirstResponder
{
    return YES;
}

- (BOOL)acceptsFirstMouse:(NSEvent *)event
{
    (void) event;
    return YES;
}

- (BOOL)wantsUpdateLayer
{
    return YES;
}

- (void)updateLayer
{
    ZuiMetalState *st = self.state;
    if (st) st->redraw = YES;
}

- (void)pointOf:(NSEvent *)ev x:(int32_t *)x y:(int32_t *)y
{
    NSPoint p = [self convertPoint:ev.locationInWindow fromView:nil];
    CGFloat f = self.window.backingScaleFactor;
    if (f <= 0) f = 1;
    *x = (int32_t) floor(p.x * f);
    *y = (int32_t) floor(p.y * f);
}

- (void)pointer:(NSEvent *)ev type:(uint8_t)type button:(uint8_t)b
{
    ZuiMetalState *st = self.state;
    if (!st) return;
    if (type == ZUI_EV_PTR_DOWN) st->buttons |= b;
    if (type == ZUI_EV_PTR_UP) st->buttons &= (uint8_t) ~b;
    zui_event_t *e = ev_new(st, type, ev.timestamp);
    [self pointOf:ev x:&e->x y:&e->y];
    e->button = type == ZUI_EV_PTR_MOVE ? st->buttons : b;
    e->mods = mods_of(ev.modifierFlags);
}

static uint8_t other_button(NSEvent *ev)
{
    return ev.buttonNumber == 2 ? ZUI_BTN_MIDDLE : 0;
}

- (void)mouseDown:(NSEvent *)ev
{
    [self pointer:ev type:ZUI_EV_PTR_DOWN button:ZUI_BTN_LEFT];
}
- (void)mouseUp:(NSEvent *)ev
{
    [self pointer:ev type:ZUI_EV_PTR_UP button:ZUI_BTN_LEFT];
}
- (void)rightMouseDown:(NSEvent *)ev
{
    [self pointer:ev type:ZUI_EV_PTR_DOWN button:ZUI_BTN_RIGHT];
}
- (void)rightMouseUp:(NSEvent *)ev
{
    [self pointer:ev type:ZUI_EV_PTR_UP button:ZUI_BTN_RIGHT];
}
- (void)otherMouseDown:(NSEvent *)ev
{
    if (other_button(ev)) [self pointer:ev type:ZUI_EV_PTR_DOWN button:other_button(ev)];
}
- (void)otherMouseUp:(NSEvent *)ev
{
    if (other_button(ev)) [self pointer:ev type:ZUI_EV_PTR_UP button:other_button(ev)];
}
- (void)mouseMoved:(NSEvent *)ev
{
    [self pointer:ev type:ZUI_EV_PTR_MOVE button:0];
}
- (void)mouseDragged:(NSEvent *)ev
{
    [self pointer:ev type:ZUI_EV_PTR_MOVE button:0];
}
- (void)rightMouseDragged:(NSEvent *)ev
{
    [self pointer:ev type:ZUI_EV_PTR_MOVE button:0];
}
- (void)otherMouseDragged:(NSEvent *)ev
{
    [self pointer:ev type:ZUI_EV_PTR_MOVE button:0];
}

- (void)scrollWheel:(NSEvent *)ev
{
    ZuiMetalState *st = self.state;
    if (!st) return;
    CGFloat f = self.window.backingScaleFactor;
    if (f <= 0) f = 1;
    /* precise (trackpad) deltas are points; a wheel's are lines of 40 pt */
    CGFloat unit = ev.hasPreciseScrollingDeltas ? f : 40.0 * f;
    /* Cocoa's +deltaY moves the content down; zui's +dy moves it up */
    int32_t dx = (int32_t) lround(-ev.scrollingDeltaX * unit);
    int32_t dy = (int32_t) lround(-ev.scrollingDeltaY * unit);
    if (!dx && !dy) return;
    zui_event_t *e = ev_new(st, ZUI_EV_SCROLL, ev.timestamp);
    [self pointOf:ev x:&e->x y:&e->y];
    e->dx = dx;
    e->dy = dy;
    e->mods = mods_of(ev.modifierFlags);
}

- (void)keyDown:(NSEvent *)ev
{
    ZuiMetalState *st = self.state;
    if (!st) return;
    zui_event_t *e = ev_new(st, ZUI_EV_KEY_DOWN, ev.timestamp);
    e->key = key_of_event(ev);
    e->mods = mods_of(ev.modifierFlags);
    e->repeat = [ev isARepeat] ? 1 : 0;
    /* Command shortcuts are keys only; everything else may be text */
    if (!(ev.modifierFlags & NSEventModifierFlagCommand)) [self interpretKeyEvents:@[ ev ]];
}

- (void)keyUp:(NSEvent *)ev
{
    ZuiMetalState *st = self.state;
    if (!st) return;
    zui_event_t *e = ev_new(st, ZUI_EV_KEY_UP, ev.timestamp);
    e->key = key_of_event(ev);
    e->mods = mods_of(ev.modifierFlags);
}

- (void)flagsChanged:(NSEvent *)ev
{
    ZuiMetalState *st = self.state;
    if (!st) return;
    NSEventModifierFlags mask = NSEventModifierFlagShift | NSEventModifierFlagControl |
                                NSEventModifierFlagOption | NSEventModifierFlagCommand;
    NSEventModifierFlags now = ev.modifierFlags & mask;
    BOOL down = (now & ~st->lastFlags) != 0;
    st->lastFlags = now;
    zui_event_t *e = ev_new(st, down ? ZUI_EV_KEY_DOWN : ZUI_EV_KEY_UP, ev.timestamp);
    e->key = ZUI_KEY_NONE; /* a modifier alone: only the mods change */
    e->mods = mods_of(now);
}

- (BOOL)performKeyEquivalent:(NSEvent *)ev
{
    (void) ev;
    return NO; /* no menu: Command keys come to keyDown */
}

/* NSTextInputClient */

- (void)insertText:(id)string replacementRange:(NSRange)range
{
    (void) range;
    ZuiMetalState *st = self.state;
    if (!st) return;
    NSString *s = [string isKindOfClass:[NSAttributedString class]]
                      ? [(NSAttributedString *) string string]
                      : (NSString *) string;
    if (st->marked) {
        st->marked = NO;
        ev_text(st, ZUI_EV_PREEDIT, @"", now_s());
    }
    /* keys like Return come here as control characters: they are keys */
    if (s.length == 1 && [s characterAtIndex:0] < 0x20) return;
    if (s.length) ev_text(st, ZUI_EV_TEXT, s, now_s());
}

- (void)doCommandBySelector:(SEL)selector
{
    (void) selector; /* Return, arrows, ... already went out as keys */
}

- (void)setMarkedText:(id)string
        selectedRange:(NSRange)selectedRange
     replacementRange:(NSRange)replacementRange
{
    (void) selectedRange;
    (void) replacementRange;
    ZuiMetalState *st = self.state;
    if (!st) return;
    NSString *s = [string isKindOfClass:[NSAttributedString class]]
                      ? [(NSAttributedString *) string string]
                      : (NSString *) string;
    st->marked = s.length > 0;
    ev_text(st, ZUI_EV_PREEDIT, s ? s : @"", now_s());
}

- (void)unmarkText
{
    ZuiMetalState *st = self.state;
    if (!st) return;
    st->marked = NO;
    ev_text(st, ZUI_EV_PREEDIT, @"", now_s());
}

- (NSRange)selectedRange
{
    return NSMakeRange(0, 0);
}

- (NSRange)markedRange
{
    ZuiMetalState *st = self.state;
    return (st && st->marked) ? NSMakeRange(0, 1) : NSMakeRange(NSNotFound, 0);
}

- (BOOL)hasMarkedText
{
    ZuiMetalState *st = self.state;
    return st && st->marked;
}

- (NSAttributedString *)attributedSubstringForProposedRange:(NSRange)range
                                                actualRange:(NSRangePointer)actualRange
{
    (void) range;
    (void) actualRange;
    return nil;
}

- (NSArray<NSAttributedStringKey> *)validAttributesForMarkedText
{
    return @[];
}

- (NSRect)firstRectForCharacterRange:(NSRange)range actualRange:(NSRangePointer)actualRange
{
    (void) range;
    (void) actualRange;
    /* zui does not report its caret here yet: put the candidate window at
     * the view's lower-left corner */
    NSRect r = NSMakeRect(0, self.bounds.size.height - 20, 1, 20);
    r = [self convertRect:r toView:nil];
    return [self.window convertRectToScreen:r];
}

- (NSUInteger)characterIndexForPoint:(NSPoint)point
{
    (void) point;
    return NSNotFound;
}

@end

/* ---- the window delegate ------------------------------------------------- */

@implementation ZuiWindowDelegate

- (BOOL)windowShouldClose:(NSWindow *)sender
{
    (void) sender;
    ZuiMetalState *st = self.state;
    if (st) ev_new(st, ZUI_EV_CLOSE, now_s());
    return NO; /* the app decides */
}

- (void)windowWillClose:(NSNotification *)n
{
    (void) n;
    ZuiMetalState *st = self.state;
    if (st) st->closed = YES;
}

- (void)windowDidResize:(NSNotification *)n
{
    (void) n;
    ZuiMetalState *st = self.state;
    if (st) update_size(st, YES);
}

- (void)windowDidChangeBackingProperties:(NSNotification *)n
{
    (void) n;
    ZuiMetalState *st = self.state;
    if (st) update_size(st, YES);
}

- (void)windowDidBecomeKey:(NSNotification *)n
{
    (void) n;
    ZuiMetalState *st = self.state;
    if (st) ev_new(st, ZUI_EV_FOCUS_IN, now_s());
}

- (void)windowDidResignKey:(NSNotification *)n
{
    (void) n;
    ZuiMetalState *st = self.state;
    if (st) {
        st->lastFlags = 0;
        ev_new(st, ZUI_EV_FOCUS_OUT, now_s());
    }
}

@end

/* ---- frames -------------------------------------------------------------- */

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

static BOOL make_frame(ZuiMetalState *st, int32_t w, int32_t h)
{
    MTLTextureDescriptor *td =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                           width:(NSUInteger) w
                                                          height:(NSUInteger) h
                                                       mipmapped:NO];
    td.storageMode = MTLStorageModeManaged; /* CPU writes, GPU copies */
    td.usage = MTLTextureUsageShaderRead;
    st.frame = [st.device newTextureWithDescriptor:td];
    if (!st.frame) return NO;
    st->fw = w;
    st->fh = h;
    st->frameUndefined = YES;
    return YES;
}

/* Upload the damage of s (nil: none) and show the frame. */
static int show(ZuiMetalState *st, const zui_surface_t *s, const zui_damage_t *d, uint64_t *copied)
{
    *copied = 0;
    /* the GPU may still be reading the frame from the last present */
    if (st.lastCommands) {
        [st.lastCommands waitUntilCompleted];
        st.lastCommands = nil;
    }
    zui_damage_t all;
    if (s) {
        if (!st.frame || st->fw != s->w || st->fh != s->h)
            if (!make_frame(st, s->w, s->h)) return ZUI_PRESENT_E_UNAVAILABLE;
        if (st->frameUndefined) {
            zui_damage_clear(&all);
            zui_damage_all(&all, s->w, s->h);
            d = &all;
        }
        for (uint32_t i = 0; i < d->n; i++) {
            zui_rect_t r = d->r[i];
            if (!clip_rect(&r, s->w, s->h)) continue;
            const uint32_t *src = s->px + (size_t) r.y * (size_t) s->stride + (size_t) r.x;
            [st.frame replaceRegion:MTLRegionMake2D((NSUInteger) r.x, (NSUInteger) r.y,
                                                    (NSUInteger) r.w, (NSUInteger) r.h)
                        mipmapLevel:0
                          withBytes:src
                        bytesPerRow:(NSUInteger) s->stride * 4u];
            *copied += (uint64_t) r.w * (uint64_t) r.h;
        }
        if (!*copied) return ZUI_PRESENT_OK; /* P1: nothing changed */
        st->frameUndefined = NO;
    } else if (!st.frame || st->frameUndefined) {
        return ZUI_PRESENT_OK;
    }

    id<CAMetalDrawable> drawable = [st.layer nextDrawable];
    if (!drawable) return ZUI_PRESENT_OK; /* hidden or minimised: try next frame */
    id<MTLTexture> dst = drawable.texture;
    id<MTLCommandBuffer> cb = [st.queue commandBuffer];
    NSUInteger cw = MIN(dst.width, (NSUInteger) st->fw);
    NSUInteger ch = MIN(dst.height, (NSUInteger) st->fh);
    if (cw != dst.width || ch != dst.height) {
        /* the drawable is bigger than the frame for a moment: ground colour */
        MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
        rp.colorAttachments[0].texture = dst;
        rp.colorAttachments[0].loadAction = MTLLoadActionClear;
        rp.colorAttachments[0].storeAction = MTLStoreActionStore;
        rp.colorAttachments[0].clearColor =
            MTLClearColorMake(0x00 / 255.0, 0x29 / 255.0, 0x11 / 255.0, 1.0);
        [[cb renderCommandEncoderWithDescriptor:rp] endEncoding];
    }
    id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
    [blit copyFromTexture:st.frame
              sourceSlice:0
              sourceLevel:0
             sourceOrigin:MTLOriginMake(0, 0, 0)
               sourceSize:MTLSizeMake(cw, ch, 1)
                toTexture:dst
         destinationSlice:0
         destinationLevel:0
        destinationOrigin:MTLOriginMake(0, 0, 0)];
    [blit endEncoding];
    [cb presentDrawable:drawable];
    [cb commit];
    st.lastCommands = cb;
    return ZUI_PRESENT_OK;
}

/* ---- ops ----------------------------------------------------------------- */

static int mtl_open(zui_present_t *p, const char *title, int32_t w_pt, int32_t h_pt)
{
    if (w_pt <= 0 || h_pt <= 0) return ZUI_PRESENT_E_ARG;
    if (![NSThread isMainThread]) return ZUI_PRESENT_E_UNAVAILABLE; /* AppKit is main-only */
    @autoreleasepool {
        id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
        if (!dev) return ZUI_PRESENT_E_UNAVAILABLE;

        static BOOL launched;
        [NSApplication sharedApplication];
        if (!launched) {
            [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
            [NSApp finishLaunching];
            launched = YES;
        }

        ZuiMetalState *st = [[ZuiMetalState alloc] init];
        st.device = dev;
        st.queue = [dev newCommandQueue];
        if (!st.queue) return ZUI_PRESENT_E_UNAVAILABLE;

        NSWindowStyleMask style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                  NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;
        NSWindow *win = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, w_pt, h_pt)
                                                    styleMask:style
                                                      backing:NSBackingStoreBuffered
                                                        defer:NO];
        if (!win) return ZUI_PRESENT_E_UNAVAILABLE;
        win.releasedWhenClosed = NO; /* ARC owns it through st.window */
        NSString *t = [NSString stringWithUTF8String:title ? title : "zxv"];
        win.title = t ? t : @"zxv";
        win.acceptsMouseMovedEvents = YES;
        win.backgroundColor = [NSColor colorWithSRGBRed:0x00 / 255.0
                                                  green:0x29 / 255.0
                                                   blue:0x11 / 255.0
                                                  alpha:1.0];
        st.window = win;

        CAMetalLayer *layer = [CAMetalLayer layer];
        layer.device = dev;
        layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        layer.framebufferOnly = NO; /* the blit writes the drawable */
        layer.opaque = YES;
        layer.colorspace = NULL; /* no colour matching: the bytes as drawn (P2) */
        layer.contentsGravity = kCAGravityTopLeft;
        st.layer = layer;

        ZuiMetalView *view = [[ZuiMetalView alloc] initWithFrame:NSMakeRect(0, 0, w_pt, h_pt)];
        view.state = st;
        view.layer = layer; /* layer-hosting: set the layer before wantsLayer */
        view.wantsLayer = YES;
        view.layerContentsRedrawPolicy = NSViewLayerContentsRedrawDuringViewResize;
        st.view = view;
        win.contentView = view;

        ZuiWindowDelegate *del = [[ZuiWindowDelegate alloc] init];
        del.state = st;
        st.delegate = del; /* NSWindow's delegate is weak: st keeps it */
        win.delegate = del;

        st->scr.input = ZUI_INPUT_POINTER;
        update_size(st, NO);
        if (!st->scr.px_w || !st->scr.px_h) return ZUI_PRESENT_E_UNAVAILABLE;

        [win center];
        [win makeKeyAndOrderFront:nil];
        [win makeFirstResponder:view];
        [NSApp activateIgnoringOtherApps:YES];

        p->screen = st->scr;
        p->frames = 0;
        p->px_copied = 0;
        p->impl = (__bridge_retained void *) st;
    }
    return ZUI_PRESENT_OK;
}

static void mtl_close(zui_present_t *p)
{
    if (!p->impl) return;
    @autoreleasepool {
        ZuiMetalState *st = (__bridge_transfer ZuiMetalState *) p->impl;
        p->impl = NULL;
        [st.lastCommands waitUntilCompleted];
        st.window.delegate = nil;
        st.view.state = nil;
        [st.window orderOut:nil];
        [st.window close];
        st.window = nil;
        st.view = nil;
        st.delegate = nil;
        st.frame = nil;
        st.layer = nil;
        st.lastCommands = nil;
        st.queue = nil;
        st.device = nil;
    } /* st is released here */
}

static int mtl_present(zui_present_t *p, const zui_surface_t *s, const zui_damage_t *d)
{
    if (!p->impl || !s || !s->px || !d || s->w <= 0 || s->h <= 0) return ZUI_PRESENT_E_ARG;
    ZuiMetalState *st = (__bridge ZuiMetalState *) p->impl;
    if (st->closed) return ZUI_PRESENT_E_LOST;
    int rc;
    uint64_t copied = 0;
    @autoreleasepool {
        rc = show(st, s, d, &copied);
    }
    if (rc == ZUI_PRESENT_OK && copied) {
        p->frames++;
        p->px_copied += copied;
    }
    return rc;
}

static int mtl_poll(zui_present_t *p, zui_queue_t *q, uint32_t wait_ms)
{
    if (!p->impl || !q) return ZUI_PRESENT_E_ARG;
    ZuiMetalState *st = (__bridge ZuiMetalState *) p->impl;
    @autoreleasepool {
        NSDate *until = (wait_ms && st->head == st->tail)
                            ? [NSDate dateWithTimeIntervalSinceNow:wait_ms / 1000.0]
                            : [NSDate distantPast];
        for (;;) {
            NSEvent *ev = [NSApp nextEventMatchingMask:NSEventMaskAny
                                             untilDate:until
                                                inMode:NSDefaultRunLoopMode
                                               dequeue:YES];
            if (!ev) break;
            [NSApp sendEvent:ev];
            until = [NSDate distantPast]; /* waited once; now only drain */
        }
        if (st->redraw) {
            uint64_t unused;
            st->redraw = NO;
            show(st, NULL, NULL, &unused);
        }
    }
    int n = 0;
    while (st->tail != st->head) {
        zui_push(q, &st->ring[st->tail++ % ZUI_MTL_RING]);
        n++;
    }
    p->screen = st->scr;
    if (st->closed) return ZUI_PRESENT_E_LOST;
    return n;
}

static uint32_t mtl_now_ms(zui_present_t *p)
{
    (void) p;
    return ms_of(now_s());
}

const zui_present_ops_t zui_present_metal_ops = {
    .name = "metal",
    .open = mtl_open,
    .close = mtl_close,
    .present = mtl_present,
    .poll = mtl_poll,
    .now_ms = mtl_now_ms,
};
