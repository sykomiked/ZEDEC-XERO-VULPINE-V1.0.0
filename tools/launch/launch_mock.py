# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# ZXV app launch screens: the boot sequence made per-app. One sequence, many themes.
#
# Every app launch has the boot's grammar (owner's rules):
#   1. the app's mark sits dim at the top, its real load parts sit on carved tiles in a row;
#   2. two mirrored bars run inward, lighting each part as its head passes  -> one tick sound per part
#   3. the centre is loaded: the mark lights and warms, one sweep of light
#   4. the bar under the mark releases centre-out with the rest of the load (real progress, never faked)
#   5. the reveal: the screen opens onto the app in the app's own way        -> the end sound
# What changes per app: the mark, the parts, the ambient motion behind them, the reveal, the sounds.
import numpy as np, math, os, subprocess, sys, wave
from PIL import Image, ImageDraw, ImageFont, ImageFilter
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import launch_sounds as LS
SR = LS.SR

HERE = os.path.dirname(os.path.abspath(__file__))
# the system font (ASCW Golden, Latin II); ZXV_FONT overrides, DejaVu if neither is there
FONT = next((f for f in (os.environ.get('ZXV_FONT', ''), HERE + '/../../assets/fonts/ascw-golden/ASCW-Golden-Latin2.ttf',
             '/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf') if f and os.path.exists(f)), None)
W, H = 1280, 800
FPS = 30
GROUND = (0x00,0x29,0x11); PANEL = (0x04,0x33,0x1A); HAIR = (0x15,0x50,0x2F); DEEP = (0x00,0x14,0x08)
GOLD = (0xF2,0xC2,0x30); GLOW = (0xFF,0xE3,0x8A); MUTED = (0x84,0xB7,0x95); TXT = (0xD8,0xF5,0xE1)
font = lambda s: ImageFont.truetype(FONT, s) if FONT else ImageFont.load_default()
ease = lambda t: (lambda u: u*u*(3-2*u))(max(0.0, min(1.0, t)))
smoother = lambda t: (lambda u: u*u*u*(u*(u*6-15)+10))(max(0.0, min(1.0, t)))

# ---- marks and part glyphs: white line art on a transparent mask, drawn at 4x then reduced ------
SS = 4
def mask(size, fn):
    im = Image.new('L', (size*SS, size*SS), 0); d = ImageDraw.Draw(im); fn(d, size*SS)
    return im.resize((size, size), Image.LANCZOS)

def M_burrow(d, S):         # a mound with three nested tunnel arches and the ground line
    w = S*0.06
    d.arc((S*.08, S*.18, S*.92, S*1.02), 180, 360, fill=255, width=int(w))
    for k, r in enumerate((0.30, 0.21, 0.12)):
        x0, x1 = S*(0.5-r), S*(0.5+r); y0 = S*(0.80-r*1.25)
        d.arc((x0, y0, x1, y0+2*r*1.25*S), 180, 360, fill=255, width=int(w*(1-0.18*k)))
        d.line((x0, y0+r*1.25*S, x0, S*.80), fill=255, width=int(w*(1-0.18*k)))
        d.line((x1, y0+r*1.25*S, x1, S*.80), fill=255, width=int(w*(1-0.18*k)))
    d.line((S*.02, S*.80, S*.98, S*.80), fill=255, width=int(w))
    for x in (0.16, 0.30, 0.70, 0.84): d.line((S*x, S*.86, S*(x+0.05), S*.86), fill=255, width=int(w*.6))

def M_flint(d, S):          # a knapped flint with facets, and the spark leaving it
    P = [(.18,.62),(.30,.40),(.52,.30),(.70,.38),(.80,.58),(.66,.80),(.40,.86),(.22,.78)]
    pts = [(x*S, y*S) for x, y in P]; w = int(S*.05)
    d.line(pts + [pts[0]], fill=255, width=w, joint='curve')
    for a, b in [((.30,.40),(.44,.60)),((.52,.30),(.44,.60)),((.44,.60),(.40,.86)),((.44,.60),(.66,.80)),((.70,.38),(.58,.58))]:
        d.line((a[0]*S, a[1]*S, b[0]*S, b[1]*S), fill=255, width=int(w*.55))
    cx, cy = S*.80, S*.20
    for ang in range(0, 360, 45):
        r0, r1 = S*.03, S*(.12 if ang % 90 == 0 else .08); a = math.radians(ang)
        d.line((cx+r0*math.cos(a), cy+r0*math.sin(a), cx+r1*math.cos(a), cy+r1*math.sin(a)), fill=255, width=int(w*.6))

def M_murmuration(d, S):    # a flock in a turning ribbon: dots along a twisted spiral
    for k in range(70):
        u = k/69; a = u*math.pi*3.2; r = S*(0.08 + 0.34*u)
        x = S*.5 + r*math.cos(a)*1.05; y = S*.5 + r*math.sin(a)*0.62 + S*0.10*math.sin(u*9)
        rad = S*(0.006 + 0.02*math.sin(u*math.pi))
        d.ellipse((x-rad, y-rad, x+rad, y+rad), fill=255)

def M_coffer(d, S):         # a strongbox: domed lid, band, hinges, keyhole
    w = int(S*.05)
    d.rounded_rectangle((S*.14, S*.46, S*.86, S*.86), radius=int(S*.05), outline=255, width=w)
    d.chord((S*.14, S*.20, S*.86, S*.72), 180, 360, outline=255, width=w)
    d.line((S*.14, S*.46, S*.86, S*.46), fill=255, width=w)
    for x in (.30, .70): d.line((S*x, S*.24, S*x, S*.86), fill=255, width=int(w*.6))
    d.ellipse((S*.45, S*.56, S*.55, S*.66), outline=255, width=int(w*.7))
    d.polygon([(S*.48, S*.64), (S*.52, S*.64), (S*.54, S*.76), (S*.46, S*.76)], fill=255)

def M_cutwater(d, S):       # a bow cutting water, a film strip as the waterline
    w = int(S*.05)
    d.line([(S*.10, S*.56), (S*.62, S*.20), (S*.62, S*.56)], fill=255, width=w, joint='curve')
    d.line((S*.62, S*.20, S*.86, S*.56), fill=255, width=int(w*.7))
    for k in range(3):
        y = S*(.60 + .06*k)
        pts = [(S*(.06 + .02*j), y + S*.018*math.sin(j*1.1 + k)) for j in range(45)]
        d.line(pts, fill=255, width=int(w*.55))
    d.rectangle((S*.06, S*.80, S*.94, S*.92), outline=255, width=int(w*.6))
    for j in range(8): x = S*(.11 + .11*j); d.rectangle((x, S*.835, x+S*.04, S*.885), fill=255)

def M_commons(d, S):        # two speech rings, overlapping where people meet
    w = int(S*.055)
    d.ellipse((S*.08, S*.22, S*.62, S*.68), outline=255, width=w)
    d.polygon([(S*.20, S*.62), (S*.16, S*.80), (S*.32, S*.66)], fill=255)
    d.ellipse((S*.38, S*.32, S*.92, S*.78), outline=255, width=w)
    d.polygon([(S*.80, S*.72), (S*.86, S*.90), (S*.68, S*.76)], fill=255)
    for x in (.62, .65, .68): pass

# small glyphs for parts (S is the glyph box)
def G(fn): return fn
def g_grid(d,S): [d.rectangle((S*(.2+.22*i),S*(.2+.22*j),S*(.36+.22*i),S*(.36+.22*j)),outline=255,width=int(S*.05)) for i in range(3) for j in range(3)]
def g_layers(d,S): [d.polygon([(S*.5,S*(.18+.18*k)),(S*.86,S*(.34+.18*k)),(S*.5,S*(.50+.18*k)),(S*.14,S*(.34+.18*k))],outline=255,width=int(S*.05)) for k in range(3)]
def g_pin(d,S): d.ellipse((S*.3,S*.15,S*.7,S*.55),outline=255,width=int(S*.06)); d.line((S*.5,S*.55,S*.5,S*.88),fill=255,width=int(S*.06))
def g_trash(d,S): d.rectangle((S*.28,S*.32,S*.72,S*.86),outline=255,width=int(S*.06)); d.line((S*.2,S*.26,S*.8,S*.26),fill=255,width=int(S*.06)); d.line((S*.42,S*.16,S*.58,S*.16),fill=255,width=int(S*.06))
def g_sync(d,S): d.arc((S*.18,S*.18,S*.82,S*.82),200,340,fill=255,width=int(S*.06)); d.arc((S*.18,S*.18,S*.82,S*.82),20,160,fill=255,width=int(S*.06)); d.polygon([(S*.78,S*.30),(S*.90,S*.42),(S*.72,S*.46)],fill=255); d.polygon([(S*.22,S*.70),(S*.10,S*.58),(S*.28,S*.54)],fill=255)
def g_prompt(d,S): d.line([(S*.2,S*.3),(S*.45,S*.5),(S*.2,S*.7)],fill=255,width=int(S*.07)); d.line((S*.5,S*.72,S*.82,S*.72),fill=255,width=int(S*.07))
def g_lines(d,S): [d.line((S*.18,S*(.25+.17*k),S*(.82-.12*(k%2)),S*(.25+.17*k)),fill=255,width=int(S*.06)) for k in range(4)]
def g_key(d,S): d.ellipse((S*.14,S*.36,S*.42,S*.64),outline=255,width=int(S*.06)); d.line((S*.42,S*.5,S*.86,S*.5),fill=255,width=int(S*.06)); d.line((S*.72,S*.5,S*.72,S*.66),fill=255,width=int(S*.06)); d.line((S*.82,S*.5,S*.82,S*.62),fill=255,width=int(S*.06))
def g_path(d,S): d.line([(S*.15,S*.8),(S*.35,S*.5),(S*.55,S*.62),(S*.85,S*.2)],fill=255,width=int(S*.06),joint='curve'); [d.ellipse((S*x-S*.06,S*y-S*.06,S*x+S*.06,S*y+S*.06),fill=255) for x,y in((.15,.8),(.35,.5),(.55,.62),(.85,.2))]
def g_mesh(d,S):
    P=[(.5,.18),(.82,.42),(.70,.82),(.30,.82),(.18,.42)]
    for i in range(5):
        for j in range(i+1,5): d.line((S*P[i][0],S*P[i][1],S*P[j][0],S*P[j][1]),fill=255,width=int(S*.03))
    for x,y in P: d.ellipse((S*x-S*.07,S*y-S*.07,S*x+S*.07,S*y+S*.07),fill=255)
def g_agent(d,S): d.ellipse((S*.22,S*.22,S*.78,S*.78),outline=255,width=int(S*.06)); d.ellipse((S*.38,S*.40,S*.46,S*.48),fill=255); d.ellipse((S*.54,S*.40,S*.62,S*.48),fill=255); d.arc((S*.36,S*.44,S*.64,S*.66),20,160,fill=255,width=int(S*.05))
def g_card(d,S): d.rounded_rectangle((S*.12,S*.28,S*.88,S*.74),radius=int(S*.06),outline=255,width=int(S*.06)); d.line((S*.12,S*.40,S*.88,S*.40),fill=255,width=int(S*.08)); d.line((S*.22,S*.60,S*.46,S*.60),fill=255,width=int(S*.05))
def g_coin(d,S): d.ellipse((S*.18,S*.18,S*.82,S*.82),outline=255,width=int(S*.06)); d.ellipse((S*.32,S*.32,S*.68,S*.68),outline=255,width=int(S*.04))
def g_receipt(d,S): d.line([(S*.26,S*.14),(S*.26,S*.86),(S*.36,S*.80),(S*.46,S*.86),(S*.56,S*.80),(S*.66,S*.86),(S*.74,S*.80),(S*.74,S*.14),(S*.26,S*.14)],fill=255,width=int(S*.05)); [d.line((S*.34,S*(.3+.14*k),S*.66,S*(.3+.14*k)),fill=255,width=int(S*.04)) for k in range(3)]
def g_reel(d,S): d.ellipse((S*.14,S*.14,S*.86,S*.86),outline=255,width=int(S*.06)); [d.ellipse((S*(.5+.22*math.cos(a))-S*.08,S*(.5+.22*math.sin(a))-S*.08,S*(.5+.22*math.cos(a))+S*.08,S*(.5+.22*math.sin(a))+S*.08),outline=255,width=int(S*.04)) for a in np.linspace(0,2*math.pi,6)[:-1]]
def g_timeline(d,S): [d.rounded_rectangle((S*a,S*(.24+.2*k),S*b,S*(.36+.2*k)),radius=int(S*.03),outline=255,width=int(S*.04)) for k,(a,b) in enumerate(((.12,.56),(.30,.88),(.18,.70)))]; d.line((S*.5,S*.14,S*.5,S*.86),fill=255,width=int(S*.04))
def g_chip(d,S): d.rectangle((S*.28,S*.28,S*.72,S*.72),outline=255,width=int(S*.06)); [d.line((S*(.36+.14*k),S*.14,S*(.36+.14*k),S*.28),fill=255,width=int(S*.04)) or d.line((S*(.36+.14*k),S*.72,S*(.36+.14*k),S*.86),fill=255,width=int(S*.04)) for k in range(3)]
def g_wave(d,S): d.line([(S*(.1+.8*j/40),S*(.5+.28*math.sin(j/40*math.pi*4)*math.sin(j/40*math.pi))) for j in range(41)],fill=255,width=int(S*.06),joint='curve')
def g_people(d,S): [d.ellipse((S*(x-.1),S*.24,S*(x+.1),S*.44),outline=255,width=int(S*.05)) or d.arc((S*(x-.18),S*.48,S*(x+.18),S*.92),180,360,fill=255,width=int(S*.05)) for x in (.34,.66)]
def g_lock(d,S): d.rounded_rectangle((S*.24,S*.44,S*.76,S*.84),radius=int(S*.05),outline=255,width=int(S*.06)); d.arc((S*.32,S*.16,S*.68,S*.62),180,360,fill=255,width=int(S*.06))
def g_clock(d,S): d.ellipse((S*.16,S*.16,S*.84,S*.84),outline=255,width=int(S*.06)); d.line((S*.5,S*.5,S*.5,S*.28),fill=255,width=int(S*.05)); d.line((S*.5,S*.5,S*.66,S*.58),fill=255,width=int(S*.05))
def g_dot(d,S): d.ellipse((S*.3,S*.3,S*.7,S*.7),outline=255,width=int(S*.06)); d.ellipse((S*.44,S*.44,S*.56,S*.56),fill=255)

# ---- the six themes ----------------------------------------------------------------------------
THEMES = {
 'burrow':      dict(title='Burrow', role='Files', mark=M_burrow, reveal='iris', ambient='arches',
                     parts=[('Index',g_grid),('Spaces',g_layers),('Places',g_pin),('Trash',g_trash),('Sync',g_sync)],
                     status='indexing Documents  ·  1,204 files'),
 'flint':       dict(title='Flint', role='Terminal', mark=M_flint, reveal='scanline', ambient='sparks',
                     parts=[('Shell',g_prompt),('History',g_lines),('Keys',g_key),('Paths',g_path),('Mesh',g_mesh)],
                     status='zxsh  ·  loading history and keys'),
 'murmuration': dict(title='Murmuration', role='The live swarm', mark=M_murmuration, reveal='scatter', ambient='flock',
                     parts=[('Planner',g_agent),('Coder',g_agent),('Qwen 1.5B',g_chip),('Critic',g_agent),('Ledger',g_receipt)],
                     status='waking 4 agents  ·  budget 12,000 tokens a cycle'),
 'coffer':      dict(title='Coffer', role='Wallet and books', mark=M_coffer, reveal='lid', ambient='dust',
                     parts=[('Keys',g_key),('Ledger',g_receipt),('Cards',g_card),('V-Bills',g_coin),('Receipts',g_lines)],
                     status='verifying signatures  ·  opening the books'),
 'cutwater':    dict(title='Cutwater', role='Video editing', mark=M_cutwater, reveal='wake', ambient='water',
                     parts=[('Codecs',g_reel),('Media',g_grid),('Timeline',g_timeline),('GPU',g_chip),('Audio',g_wave)],
                     status='opening project  ·  37 clips, 4K'),
 'commons':     dict(title='Commons', role='Chat', mark=M_commons, reveal='petals', ambient='rings',
                     parts=[('Rooms',g_layers),('People',g_people),('Keys',g_lock),('History',g_clock),('Presence',g_dot)],
                     status='joining 6 rooms  ·  end-to-end keys'),
}

# ---- layout -------------------------------------------------------------------------------------
TW, GAP = 150, 22; N = 5
X0 = (W - (N*TW + (N-1)*GAP)) / 2; RY = 430
CX = [X0 + i*(TW+GAP) + TW/2 for i in range(N)]
BAR_Y = RY + TW + 34; MARK = (W/2, 215, 250)

def gold_from_mask(m, rgb, alpha):
    a = np.zeros((m.height, m.width, 4), np.uint8); a[..., :3] = rgb; a[..., 3] = (np.asarray(m) * alpha).astype(np.uint8)
    return Image.fromarray(a, 'RGBA')

def put(c, im, cx, cy): c.alpha_composite(im, (int(cx - im.width/2), int(cy - im.height/2)))

def glow(c, m, cx, cy, s):
    if s <= 0: return
    pad = 40; g = Image.new('L', (m.width + 2*pad, m.height + 2*pad), 0); g.paste(m, (pad, pad))
    g = g.filter(ImageFilter.GaussianBlur(14)); put(c, gold_from_mask(g, GLOW, 0.55*min(s, 1.6)), cx, cy)

def engraved(c, m, cx, cy, lit, dim=0.16, flash=0.0):
    glow(c, m, cx, cy, lit*(1 + 0.8*flash))
    put(c, gold_from_mask(m, GOLD, dim + (1-dim)*lit), cx, cy)
    if flash > 0: put(c, gold_from_mask(m, GLOW, flash), cx, cy)

def tile(c, x, y, w, h, lit):
    d = ImageDraw.Draw(c)
    d.rounded_rectangle((x, y, x+w, y+h), radius=14, fill=PANEL+(255,), outline=DEEP+(255,), width=2)
    if lit > 0: d.rounded_rectangle((x, y, x+w, y+h), radius=14, outline=GOLD+(int(150*lit),), width=1)

def text(c, s, x, y, size, rgb, alpha=1.0, anchor='mm'):
    ov = Image.new('RGBA', c.size, (0,0,0,0)); ImageDraw.Draw(ov).text((x, y), s, font=font(size), fill=rgb+(int(255*alpha),), anchor=anchor)
    c.alpha_composite(ov)

def bar(c, x0, x1, y, p, mode):
    d = ImageDraw.Draw(c); xm = (x0 + x1)/2
    d.rounded_rectangle((x0, y, x1, y+3), radius=1, fill=HAIR)
    if mode == 'gather': hl, hr = x0 + (xm-x0)*p, x1 - (x1-xm)*p; segs = [(x0, hl), (hr, x1)]; heads = (hl, hr)
    else: h = (xm-x0)*p; segs = [(xm-h, xm+h)]; heads = (xm-h, xm+h)
    if p > 0:
        for a, b in segs: d.rounded_rectangle((a, y, b, y+3), radius=1, fill=GOLD)
        if p < 1:
            for hx in heads: d.ellipse((hx-4, y-3, hx+4, y+6), fill=GLOW)
    return heads

# ---- ambient motion behind the screen: each app's own weather, kept faint ----------------------
def ambient(c, kind, t):
    ov = Image.new('RGBA', (W, H), (0,0,0,0)); d = ImageDraw.Draw(ov); A = 34
    if kind == 'arches':        # slow arches breathing outward from the ground, like a tunnel's rings
        for k in range(6):
            r = ((t*0.18 + k/6) % 1.0); R = 80 + r*900; a = int(A*(1-r))
            d.arc((W/2-R, H*0.98-R*0.9, W/2+R, H*0.98+R*0.9), 180, 360, fill=GOLD+(a,), width=2)
    elif kind == 'sparks':      # sparks rising and fading
        r = np.random.default_rng(5)
        for k in range(36):
            x0 = r.uniform(0, W); sp = r.uniform(60, 160); ph = r.uniform(0, 1)
            u = (t*sp/H + ph) % 1.0; y = H - u*H; x = x0 + 20*math.sin(u*7 + k)
            a = int(110*(1-u)*(0.4 + 0.6*abs(math.sin(t*9 + k)))); d.ellipse((x-1.6, y-1.6, x+1.6, y+1.6), fill=GLOW+(a,))
    elif kind == 'flock':       # a flock wheeling across the back of the screen
        for k in range(90):
            u = k/90; a = t*0.7 + u*2*math.pi
            x = W/2 + (420 + 120*math.sin(u*11))*math.cos(a)*math.cos(t*0.3 + u)
            y = 360 + 150*math.sin(a*1.5 + u*3)
            s = 1.2 + 1.5*abs(math.sin(u*17)); d.ellipse((x-s, y-s, x+s, y+s), fill=GOLD+(int(60 + 40*math.sin(u*9 + t)),))
    elif kind == 'dust':        # gold dust drifting down
        r = np.random.default_rng(9)
        for k in range(60):
            x0 = r.uniform(0, W); sp = r.uniform(20, 60); ph = r.uniform(0, 1)
            u = (t*sp/H + ph) % 1.0; y = u*H; x = x0 + 14*math.sin(t*1.3 + k)
            tw = 0.5 + 0.5*math.sin(t*6 + k*1.7); d.ellipse((x-1.3, y-1.3, x+1.3, y+1.3), fill=GLOW+(int(90*tw),))
    elif kind == 'water':       # long water lines flowing left
        for k in range(14):
            y = 120 + k*48; pts = [(x, y + 6*math.sin(x/90 + t*2.2 + k)) for x in range(0, W+20, 20)]
            d.line(pts, fill=GOLD+(18 + 3*(k % 3),), width=1)
    elif kind == 'rings':       # rings spreading from two points, like voices
        for cx, ph in ((W*0.3, 0.0), (W*0.7, 0.5)):
            for k in range(4):
                r = ((t*0.35 + k/4 + ph) % 1.0); R = 20 + r*560
                d.ellipse((cx-R, 380-R*0.7, cx+R, 380+R*0.7), outline=GOLD+(int(A*(1-r)),), width=2)
    c.alpha_composite(ov)

# ---- the destination: a stand-in for the app's first screen ------------------------------------
def destination(th):
    c = Image.new('RGBA', (W, H), GROUND+(255,)); d = ImageDraw.Draw(c)
    d.rectangle((0, 0, W, 44), fill=PANEL); d.line((0, 44, W, 44), fill=HAIR, width=1)
    m = mask(26, th['mark']); put(c, gold_from_mask(m, GOLD, 1), 30, 22)
    text(c, th['title'], 52, 22, 17, TXT, anchor='lm'); text(c, th['role'], W-24, 22, 14, MUTED, anchor='rm')
    d.rectangle((0, 45, 220, H), fill=(0x02, 0x2E, 0x15)); d.line((220, 45, 220, H), fill=HAIR)
    for i, (name, _) in enumerate(th['parts']):
        y = 80 + i*40
        if i == 0: d.rounded_rectangle((12, y-15, 208, y+15), radius=8, fill=PANEL, outline=GOLD)
        text(c, name, 30, y, 15, TXT if i == 0 else MUTED, anchor='lm')
    for r in range(3):
        for k in range(3):
            x = 250 + k*335; y = 70 + r*240
            d.rounded_rectangle((x, y, x+310, y+215), radius=14, fill=PANEL, outline=DEEP, width=2)
            d.line((x+20, y+170, x+200, y+170), fill=HAIR, width=6); d.line((x+20, y+190, x+140, y+190), fill=HAIR, width=6)
    return c

# ---- reveals: the face (launch screen) opening onto the destination, o in 0..1 -----------------
def lip(d, xy, w=3): d.line(xy, fill=GOLD+(255,), width=w)

def reveal(kind, face, dest, o):
    c = Image.new('RGBA', (W, H), DEEP+(255,))
    sc = 0.965 + 0.035*o
    dd = dest.resize((int(W*sc), int(H*sc)), Image.LANCZOS); c.alpha_composite(dd, ((W-dd.width)//2, (H-dd.height)//2))
    c.alpha_composite(Image.new('RGBA', (W, H), DEEP+(int(150*(1-o)**1.5),)))
    if kind == 'iris':          # the tunnel opens: a round hole grows from the centre, ringed in gold
        R = smoother(o) * math.hypot(W, H)/2 * 1.05; cx, cy = W/2, H/2
        m = Image.new('L', (W, H), 255); ImageDraw.Draw(m).ellipse((cx-R, cy-R, cx+R, cy+R), fill=0)
        sh = Image.new('L', (W, H), 0); ImageDraw.Draw(sh).ellipse((cx-R, cy-R, cx+R, cy+R), outline=180, width=40)
        c.paste(Image.new('RGBA', (W, H), (0,0,0,255)), (0, 0), sh.filter(ImageFilter.GaussianBlur(18)).point(lambda v: v*0.7))
        c.paste(face, (0, 0), m)
        if R > 1: d = ImageDraw.Draw(c); d.ellipse((cx-R, cy-R, cx+R, cy+R), outline=GOLD, width=3); d.ellipse((cx-R-9, cy-R-9, cx+R+9, cy+R+9), outline=HAIR, width=2)
    elif kind == 'scanline':    # CRT: the face folds to one bright line, the line opens into the app
        if o < 0.45:
            u = smoother(o/0.45); h = max(2, int(H*(1-u)))
            f = face.resize((W, h), Image.LANCZOS); c = Image.new('RGBA', (W, H), (0, 0x0A, 0x04, 255)); c.alpha_composite(f, (0, (H-h)//2))
            ov = Image.new('RGBA', (W, H), (0,0,0,0)); ImageDraw.Draw(ov).rectangle((0, H/2-3, W, H/2+3), fill=GLOW+(int(255*u),)); c.alpha_composite(ov.filter(ImageFilter.GaussianBlur(3*u+0.1)))
        else:
            u = smoother((o-0.45)/0.55); h = max(2, int(H*u)); c = Image.new('RGBA', (W, H), (0, 0x0A, 0x04, 255))
            c.alpha_composite(dest.crop((0, (H-h)//2, W, (H-h)//2+h)), (0, (H-h)//2))
            ov = Image.new('RGBA', (W, H), (0,0,0,0)); od = ImageDraw.Draw(ov)
            for y in ((H-h)//2, (H+h)//2): od.rectangle((0, y-2, W, y+2), fill=GLOW+(int(255*(1-u)),))
            c.alpha_composite(ov.filter(ImageFilter.GaussianBlur(2)))
    elif kind == 'scatter':     # the gold lifts off as a flock of fragments wheeling away; the ground melts from the centre
        T = 16; a = np.asarray(face).copy()
        diff = np.abs(a[..., :3].astype(int) - np.array(GROUND)).sum(2)
        a[..., 3] = np.clip((diff - 18)*4, 0, 255)                     # only what is drawn flies, not the ground
        u0 = smoother(o); yy, xx = np.mgrid[0:H, 0:W]
        dist = np.hypot(xx - W/2, (yy - H/2)*1.4)/(W*0.62)
        gm = np.clip((dist - u0*1.25)/0.12 + 1, 0, 1)                  # the ground: a soft hole opening outward
        g = Image.new('RGBA', (W, H), GROUND+(255,)); g.putalpha(Image.fromarray((gm*255).astype(np.uint8)))
        c.alpha_composite(g)
        for y in range(0, H, T):
            for x in range(0, W, T):
                blk = a[y:y+T, x:x+T]
                if blk[..., 3].max() < 8: continue
                cx, cy = x+T/2-W/2, y+T/2-H/2; dist = math.hypot(cx, cy)/math.hypot(W/2, H/2)
                u = max(0.0, min(1.0, (o - dist*0.4)/0.6)); u = u*u
                if u >= 1: continue
                ang = math.atan2(cy, cx) + 1.6*u; R = math.hypot(cx, cy) + u*600
                nx = W/2 + R*math.cos(ang) - T/2; ny = H/2 + R*math.sin(ang)*0.8 - T/2 - u*160
                s = max(2, int(T*(1-0.7*u)))
                t_ = Image.fromarray(blk).resize((s, s), Image.BILINEAR)
                if u > 0: t_.putalpha(t_.getchannel('A').point(lambda v: int(v*(1-u))))
                c.alpha_composite(t_, (int(nx + (T-s)/2), int(ny + (T-s)/2)))
    elif kind == 'lid':         # the strongbox: the top half lifts away, the bottom sinks, gold lips
        u = smoother(o); gap = int((H/2 + 40)*u)
        top = face.crop((0, 0, W, H//2)); bot = face.crop((0, H//2, W, H))
        ts = 1 - 0.06*u; top = top.resize((int(W*ts), int(H/2*ts)), Image.LANCZOS)
        c.alpha_composite(top, ((W-top.width)//2, -gap)); c.alpha_composite(bot, (0, H//2 + gap))
        d = ImageDraw.Draw(c); lip(d, ((W-top.width)//2, top.height-gap, (W+top.width)//2, top.height-gap)); lip(d, (0, H//2+gap, W, H//2+gap))
    elif kind == 'wake':        # the bow cuts: the screen parts along a diagonal and the halves slide apart
        u = smoother(o); off = u*(W*0.75)
        m = Image.new('L', (W, H), 0); ImageDraw.Draw(m).polygon([(0, 0), (W*0.62, 0), (W*0.38, H), (0, H)], fill=255)
        L = Image.new('RGBA', (W, H), (0,0,0,0)); L.paste(face, (0, 0), m)
        R = Image.new('RGBA', (W, H), (0,0,0,0)); R.paste(face, (0, 0), m.point(lambda v: 255-v))
        dx, dy = off*0.92, off*0.38
        c.alpha_composite(L, (int(-dx), int(-dy))); c.alpha_composite(R, (int(dx), int(dy)))
        d = ImageDraw.Draw(c)
        lip(d, (W*0.62-dx, -dy, W*0.38-dx, H-dy)); lip(d, (W*0.62+dx, dy, W*0.38+dx, H+dy))
        foam = Image.new('RGBA', (W, H), (0,0,0,0)); fd = ImageDraw.Draw(foam)
        for k in range(10):
            s = k/10; fd.line((W*0.62-dx*s, -dy*s, W*0.38-dx*s, H-dy*s), fill=GLOW+(int(50*(1-s)*(1-u)),), width=2)
            fd.line((W*0.62+dx*s, dy*s, W*0.38+dx*s, H+dy*s), fill=GLOW+(int(50*(1-s)*(1-u)),), width=2)
        c.alpha_composite(foam.filter(ImageFilter.GaussianBlur(2)))
    elif kind == 'petals':      # four rooms open: the quarters slide out to the corners on rounded edges
        u = smoother(o); gx, gy = int((W/2+30)*u), int((H/2+30)*u)
        for (x0, y0, sx, sy) in ((0, 0, -1, -1), (W//2, 0, 1, -1), (0, H//2, -1, 1), (W//2, H//2, 1, 1)):
            q = face.crop((x0, y0, x0+W//2, y0+H//2)); mm = Image.new('L', q.size, 0)
            ImageDraw.Draw(mm).rounded_rectangle((0, 0, q.width-1, q.height-1), radius=int(60*u), fill=255)
            px, py = x0 + sx*gx, y0 + sy*gy; c.paste(q, (px, py), mm)
            ImageDraw.Draw(c).rounded_rectangle((px, py, px+q.width-1, py+q.height-1), radius=int(60*u), outline=GOLD, width=2)
    return c

# ---- one launch ---------------------------------------------------------------------------------
MARKS = {}
def face(th, t, p_gather, hero, flash, sweep, p_rel, txt):
    c = Image.new('RGBA', (W, H), GROUND+(255,)); ambient(c, th['ambient'], t)
    xl, xr = X0, X0 + N*TW + (N-1)*GAP
    hl, hr = bar(c, xl, xr, BAR_Y, p_gather, 'gather')
    lits = []
    for i, (name, gf) in enumerate(th['parts']):
        if i < N//2: lit = ease((hl - CX[i])/50)
        elif i > N//2: lit = ease((CX[i] - hr)/50)
        else: lit = ease((p_gather - 0.97)/0.03)
        lits.append(lit); x = X0 + i*(TW+GAP)
        tile(c, x, RY, TW, TW, lit)
        key = (th['title'], i)
        if key not in MARKS: MARKS[key] = mask(76, gf)
        engraved(c, MARKS[key], CX[i], RY + TW/2 - 12, lit, 0.18)
        text(c, name, CX[i], RY + TW - 22, 15, GOLD if lit > 0.5 else MUTED, 0.45 + 0.55*lit)
    if th['title'] not in MARKS: MARKS[th['title']] = mask(MARK[2], th['mark'])
    m = MARKS[th['title']]; engraved(c, m, MARK[0], MARK[1], hero, 0.14, flash)
    if 0 <= sweep <= 1:
        a = np.asarray(m).astype(float); yy, xx = np.mgrid[0:m.height, 0:m.width]
        pos = (xx + yy*0.5)/(m.width + m.height*0.5); band = np.clip(1 - abs(pos - (sweep*1.3 - 0.15))/0.06, 0, 1)
        put(c, gold_from_mask(Image.fromarray((a*band).astype(np.uint8)), (255, 255, 255), 0.55), MARK[0], MARK[1])
    ty = MARK[1] + MARK[2]/2 + 30
    text(c, th['title'], W/2, ty, 30, TXT, 0.35 + 0.65*hero)
    if p_rel > 0 or hero >= 1: bar(c, W/2-210, W/2+210, ty + 32, p_rel, 'release')
    if txt > 0: text(c, th['status'], W/2, BAR_Y + 30, 14, MUTED, txt)
    return c, lits

# a load that behaves like a real one: uneven, with a stall where the heads wait
GATHER = [(0, 0.0), (180, 0.10), (420, 0.26), (560, 0.30), (820, 0.55), (1050, 0.80), (1250, 1.0)]
RELEASE = [(0, 0.0), (200, 0.22), (380, 0.35), (620, 0.38), (820, 0.70), (1000, 1.0)]
def keyed(K, t):
    for (t0, p0), (t1, p1) in zip(K, K[1:]):
        if t <= t1: return p0 + (p1 - p0)*ease((t - t0)/(t1 - t0))
    return 1.0

def run(app, out):
    th = THEMES[app]; dest = destination(th); b, tick, e = LS.RECIPES[app]()
    frames = []; events = [(0, b())]; prev = [0]*N; nlit = 0
    T_G, T_L, T_R, T_V = 1250, 520, 1000, 700        # gather, light, release, reveal (ms)
    t = 0; dt = 1000/FPS; t0 = 180                     # a breath of the dim screen first
    while t < t0: frames.append(face(th, t/1000, 0, 0, 0, -1, 0, 0)[0]); t += dt
    while t < t0 + T_G:
        f, lits = face(th, t/1000, keyed(GATHER, t - t0), 0, 0, -1, 0, ease((t - t0)/300))
        new = [i for i in range(N) if prev[i] < 0.5 <= lits[i]]
        for j, i in enumerate(new):     # the k-th part to light plays the k-th step up the scale; the centre the top
            step = 6 if i == N//2 else nlit; nlit += 1
            events.append((t + 70*j, tick(step, N)))
        prev = lits; frames.append(f); t += dt
    s0 = t
    while t < s0 + T_L:
        u = (t - s0)/T_L
        frames.append(face(th, t/1000, 1, ease(u/0.4), ease(u/0.3) if u < 0.3 else 1 - ease((u - 0.3)/0.35), (u - 0.45)/0.5 if u > 0.45 else -1, 0, 1)[0]); t += dt
    s1 = t
    while t < s1 + T_R:
        frames.append(face(th, t/1000, 1, 1, 0, -1, keyed(RELEASE, t - s1), 1)[0]); t += dt
    F = face(th, t/1000, 1, 1, 0, -1, 1, 1)[0]; events.append((t, e())); s2 = t
    while t < s2 + T_V:
        frames.append(reveal(th['reveal'], F, dest, (t - s2)/T_V)); t += dt
    while t < s2 + T_V + 1300: frames.append(dest); t += dt
    # audio: each cue placed at the frame where its event happened
    total = int(SR*(t/1000 + 2.2)); mix = np.zeros((total, 2))
    for ms, x in events:
        i = int(ms/1000*SR); mix[i:i+len(x)] += x[:total - i]
    mix = LS.finish(mix, 0.9, None, 0.2); LS.write(f'{out}/{app}.wav', mix)
    p = subprocess.Popen(['ffmpeg', '-y', '-loglevel', 'error', '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-s', f'{W}x{H}', '-r', str(FPS), '-i', '-',
                          '-i', f'{out}/{app}.wav', '-c:v', 'libx264', '-pix_fmt', 'yuv420p', '-crf', '20', '-preset', 'medium',
                          '-c:a', 'aac', '-b:a', '160k', '-shortest', f'{out}/{app}-launch.mp4'], stdin=subprocess.PIPE)
    for f in frames: p.stdin.write(f.convert('RGB').tobytes())
    p.stdin.close(); p.wait(); os.remove(f'{out}/{app}.wav')
    # stills for the contact sheet: loading, lit, opening
    n_g = int((t0 + T_G*0.6)/dt); n_l = int((s0 + T_L*0.35 - 0)/dt); n_v = int((s2 + T_V*0.45)/dt)
    for tag, k in (('a-loading', n_g), ('b-lit', n_l), ('c-opening', n_v)):
        frames[k].convert('RGB').save(f'{out}/stills/{app}-{tag}.png')
    print(app, len(frames), 'frames', [int(ms) for ms, _ in events])

if __name__ == '__main__':
    out = sys.argv[1]; os.makedirs(out + '/stills', exist_ok=True)
    for app in (sys.argv[2:] or THEMES): run(app, out)
