# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# ZXV app launch screens: the boot screen itself, played inside the app's own window.
#
# Owner (2026-10-12): every app launch is the boot sequence screen itself, played inside the window the
# person opened for the app. 09:25Z: the big top-centre logo changes from the OS fox to the app's icon and
# the vault opens inside the app's window. 10:53Z: keep the boot's same five logos on the tiles underneath
# (36N9, ACOTO, the ASCW crest, SMIC, CIS) so it is streamlined and easy to replicate; only the icon at the
# top changes. 10:54Z: each app keeps its own sound sequence, following the same begin/middle/end pattern.
#   1. the app's icon sits dim at the top, the five logos dim on their tiles   -> the begin cue
#   2. two mirrored bars run inward with the app's real load, lighting each tile -> one tick per tile
#   3. everything loaded: the icon lights and warms, one sweep of light
#   4. the bar under the icon grows centre-out with the rest of the real load while the vault's bolts
#      lock across the seam one by one
#   5. the bar fills: the bolts throw back and the window's face splits down the middle and slides
#      to its edges onto the app                                                 -> the end cue
# What stays the app's own: its icon at the top and its three sounds (and a faint motion behind).
import numpy as np, math, os, subprocess, sys, wave
from PIL import Image, ImageDraw, ImageFont, ImageFilter
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import launch_sounds as LS
SR = LS.SR

HERE = os.path.dirname(os.path.abspath(__file__))
# the system font (ASCW Golden, Latin II); ZXV_FONT overrides, DejaVu if neither is there
FONT = next((f for f in (os.environ.get('ZXV_FONT', ''), HERE + '/../../assets/fonts/ascw-golden/ASCW-Golden-Latin2.ttf',
             '/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf') if f and os.path.exists(f)), None)
W, H = 1440, 900
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
 'burrow':      dict(num=1, title='Burrow', role='Files', mark=M_burrow, ambient='arches',
                     parts=[('Index',g_grid),('Spaces',g_layers),('Places',g_pin),('Trash',g_trash),('Sync',g_sync)],
                     status='indexing Documents  ·  1,204 files'),
 'flint':       dict(num=13, title='Flint', role='Terminal', mark=M_flint, ambient='sparks',
                     parts=[('Shell',g_prompt),('History',g_lines),('Keys',g_key),('Paths',g_path),('Mesh',g_mesh)],
                     status='zxsh  ·  loading history and keys'),
 'murmuration': dict(num=61, title='Murmuration', role='The live swarm', mark=M_murmuration, ambient='flock',
                     parts=[('Planner',g_agent),('Coder',g_agent),('Qwen 1.5B',g_chip),('Critic',g_agent),('Ledger',g_receipt)],
                     status='waking 4 agents  ·  budget 12,000 tokens a cycle'),
 'coffer':      dict(num=73, title='Coffer', role='Wallet and books', mark=M_coffer, ambient='dust',
                     parts=[('Keys',g_key),('Ledger',g_receipt),('Cards',g_card),('V-Bills',g_coin),('Receipts',g_lines)],
                     status='verifying signatures  ·  opening the books'),
 'cutwater':    dict(num=56, title='Cutwater', role='Video editing', mark=M_cutwater, ambient='water',
                     parts=[('Codecs',g_reel),('Media',g_grid),('Timeline',g_timeline),('GPU',g_chip),('Audio',g_wave)],
                     status='opening project  ·  37 clips, 4K'),
 'commons':     dict(num=27, title='Commons', role='Chat', mark=M_commons, ambient='rings',
                     parts=[('Rooms',g_layers),('People',g_people),('Keys',g_lock),('History',g_clock),('Presence',g_dot)],
                     status='joining 6 rooms  ·  end-to-end keys'),
}

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

# ---- the boot's geometry (boot_mock.py), in the face's own 1440 x 900 space ----------------------
N = 5; TW, GAP, RY = 210, 24, 500
X0 = (W - (N*TW + (N-1)*GAP)) / 2
CX = [X0 + i*(TW+GAP) + TW/2 for i in range(N)]
BAR_Y = RY + TW + 30
HERO = (W/2, 250, (380, 400))
BOLTS = [95, 280, 465, 650, 835]; BL, BH = 86, 22
ICONS = os.environ.get('ZXV_APP_ICONS', HERE + '/../../assets/app-icons')
# the boot's five logos, gold (zxv-brand/gold/*-gold.png); ZXV_LOGOS points at that folder
LOGOS = os.environ.get('ZXV_LOGOS', HERE + '/../../assets/brand/gold')
LOGO_KEYS = ['36n9', 'acoto', 'ascw', 'smic', 'cis']
ease = lambda t: (lambda u: u*u*(3-2*u))(max(0.0, min(1.0, t)))

def gold_from_mask(m, rgb, alpha):
    a = np.zeros((m.height, m.width, 4), np.uint8); a[..., :3] = rgb; a[..., 3] = (np.asarray(m) * alpha).astype(np.uint8)
    return Image.fromarray(a, 'RGBA')

def put(c, im, cx, cy): c.alpha_composite(im, (int(cx - im.width/2), int(cy - im.height/2)))

def glow(c, m, cx, cy, s):
    if s <= 0: return
    pad = 40; g = Image.new('L', (m.width + 2*pad, m.height + 2*pad), 0); g.paste(m, (pad, pad))
    g = g.filter(ImageFilter.GaussianBlur(14)); put(c, gold_from_mask(g, GLOW, 0.55*min(s, 1.6)), cx, cy)

def text(c, s, x, y, size, rgb, alpha=1.0, anchor='mm'):
    ov = Image.new('RGBA', c.size, (0,0,0,0)); ImageDraw.Draw(ov).text((x, y), s, font=font(size), fill=rgb+(int(255*alpha),), anchor=anchor)
    c.alpha_composite(ov)

def tile(c, x, y, w, h, lit):
    d = ImageDraw.Draw(c)
    d.rounded_rectangle((x, y, x+w, y+h), radius=14, fill=PANEL+(255,), outline=DEEP+(255,), width=2)
    if lit > 0: d.rounded_rectangle((x, y, x+w, y+h), radius=14, outline=GOLD+(int(150*lit),), width=1)

# ---- the app's icon: the owner's drawing when it is there, a stand-in mark until then ------------
def icon_layers(th):
    """(mask L, colour RGBA or None) fitted to the hero box"""
    box = HERO[2]
    fn = f"{ICONS}/{th['num']:03d}-{th['title'].lower()}.png"
    if os.path.exists(fn):
        im = Image.open(fn).convert('RGB'); a = np.asarray(im).astype(int)
        dist = np.abs(a - np.array(GROUND)).sum(2)                 # drawn on the ZXV green: key it out
        al = np.clip((dist - 30) * 3, 0, 255).astype(np.uint8)
        rgba = Image.fromarray(np.dstack([a.astype(np.uint8), al]), 'RGBA'); rgba = rgba.crop(rgba.getbbox())
        s = min(box[0]/rgba.width, box[1]/rgba.height); rgba = rgba.resize((int(rgba.width*s), int(rgba.height*s)), Image.LANCZOS)
        return rgba.getchannel('A'), rgba
    return mask(min(box), th['mark']), None

def hero_draw(c, th, lit, flash, sweep):
    m, col = th['_icon']; hx, hy = HERO[0], HERO[1]
    glow(c, m, hx, hy, lit*(0.6 + 0.8*flash))
    put(c, gold_from_mask(m, GOLD, 0.14*(1-lit)), hx, hy)                  # the dim engraving
    if col is not None:
        if lit > 0:
            a = np.asarray(col).copy(); a[..., 3] = (a[..., 3]*lit).astype(np.uint8); put(c, Image.fromarray(a, 'RGBA'), hx, hy)
    else: put(c, gold_from_mask(m, GOLD, lit), hx, hy)
    if flash > 0: put(c, gold_from_mask(m, GLOW, flash), hx, hy)
    if 0 <= sweep <= 1:                                                   # one diagonal band of light
        a = np.asarray(m).astype(float); yy, xx = np.mgrid[0:m.height, 0:m.width]
        pos = (xx + yy*0.5)/(m.width + m.height*0.5); band = np.clip(1 - abs(pos - (sweep*1.3 - 0.15))/0.06, 0, 1)
        put(c, gold_from_mask(Image.fromarray((a*band).astype(np.uint8)), (255, 255, 255), 0.55), hx, hy)

# ---- the five logos, exactly as on the boot ------------------------------------------------------
_LOGO = {}
def logo(i, th):
    """(mask L, colour RGBA) for tile i: the boot's logo, or the app's part glyph if the logos are absent"""
    k = LOGO_KEYS[i]
    if k not in _LOGO:
        fn = f'{LOGOS}/{k}-gold.png'
        if os.path.exists(fn):
            im = Image.open(fn).convert('RGBA'); im = im.crop(im.getbbox()); box = TW - 30
            sc = min(box/im.width, box/im.height); im = im.resize((int(im.width*sc), int(im.height*sc)), Image.LANCZOS)
            _LOGO[k] = (im.getchannel('A'), im)
        else:
            _LOGO[k] = None
    return _LOGO[k] or (mask(104, th['parts'][i][1]), None)

# ---- the face: the boot screen with the app in it ------------------------------------------------
def face(th, t, p, hero=0.0, flash=0.0, sweep=-1.0, rule=0.0, txt=0.0):
    c = Image.new('RGBA', (W, H), GROUND+(255,)); ambient(c, th['ambient'], t)
    d = ImageDraw.Draw(c); xl, xr, xm = X0, X0 + N*TW + (N-1)*GAP, W/2
    d.rounded_rectangle((xl, BAR_Y, xr, BAR_Y+3), radius=1, fill=HAIR)
    hl, hr = xl + (xm-xl)*p, xr - (xr-xm)*p
    if p > 0:
        d.rounded_rectangle((xl, BAR_Y, hl, BAR_Y+3), radius=1, fill=GOLD); d.rounded_rectangle((hr, BAR_Y, xr, BAR_Y+3), radius=1, fill=GOLD)
        if p < 1:
            for hx in (hl, hr): d.ellipse((hx-4, BAR_Y-3, hx+4, BAR_Y+6), fill=GLOW)
    lits = []
    for i in range(N):
        lit = ease((hl - CX[i])/60) if i < 2 else ease((CX[i] - hr)/60) if i > 2 else ease((p - 0.97)/0.03)
        lits.append(lit); x = X0 + i*(TW+GAP); tile(c, x, RY, TW, TW, lit)
        m, col = logo(i, th); cy = RY + TW/2; glow(c, m, CX[i], cy, lit)
        if col is None: put(c, gold_from_mask(m, GOLD, 0.18 + 0.82*lit), CX[i], cy)
        else:                                       # dim engraving, then the logo's own gold
            a = np.asarray(col).copy(); a[..., 3] = (a[..., 3]*(0.18 + 0.82*lit)).astype(np.uint8); put(c, Image.fromarray(a, 'RGBA'), CX[i], cy)
    hero_draw(c, th, hero, flash, sweep)
    if rule > 0:                                    # the top bar, centre-out under the icon
        d = ImageDraw.Draw(c); half = 230; y = HERO[1] + HERO[2][1]/2 + 26
        d.rounded_rectangle((xm-half, y, xm+half, y+3), radius=1, fill=HAIR); h = half*rule
        d.rounded_rectangle((xm-h, y, xm+h, y+3), radius=1, fill=GOLD)
        if rule < 1:
            for hx in (xm-h, xm+h): d.ellipse((hx-4, y-3, hx+4, y+6), fill=GLOW)
    if txt > 0: text(c, th['status'], W/2, BAR_Y + 34, 15, MUTED, txt)
    return c, lits

def bolt_pair(c, y, show, throw):
    """the boot's bolts: a keeper on the seam, two gold bars sliding in to lock, then thrown back"""
    if show <= 0: return
    ov = Image.new('RGBA', (W, H), (0,0,0,0)); d = ImageDraw.Draw(ov)
    al = int(255*min(1, show*1.6)); s = ease(show); t = ease(throw)
    kx, kw, kh = W/2, 58, BH+18
    d.rounded_rectangle((kx-kw/2-4, y-kh/2-4, kx+kw/2+4, y+kh/2+4), radius=10, fill=(0,0x14,0x08,int(al*0.75)))
    d.rounded_rectangle((kx-kw/2, y-kh/2, kx+kw/2, y+kh/2), radius=8, fill=PANEL+(al,), outline=GOLD+(al,), width=2)
    for side in (-1, 1):
        tip = kx + side*(2 + (1-s)*BL*0.9 + t*BL*0.95); tail = tip + side*BL; x0, x1 = min(tip, tail), max(tip, tail)
        d.rounded_rectangle((x0, y-BH/2, x1, y+BH/2), radius=BH//2, fill=GOLD+(al,))
        d.rounded_rectangle((x0+4, y-BH/2+3, x1-4, y-BH/2+8), radius=3, fill=GLOW+(int(al*0.9),))
        d.line((x0+BH/2, y+BH/2-2, x1-BH/2, y+BH/2-2), fill=(0x9A,0x6E,0x10,al), width=2)
        for k in (0.33, 0.66):
            gx = x0 + (x1-x0)*k; d.line((gx, y-BH/2+2, gx, y+BH/2-2), fill=(0xB8,0x8A,0x1C,al), width=2)
        d.ellipse((tail-15, y-15, tail+15, y+15), fill=(0xB8,0x8A,0x1C,al), outline=GOLD+(al,), width=2)
        d.ellipse((tail-6, y-6, tail+6, y+6), fill=GLOW+(al,))
    c.alpha_composite(ov)

def door_face(th, t, rule, locks, throw, seam):
    c = face(th, t, 1, hero=1, rule=rule, txt=1)[0]
    if seam > 0:
        d = ImageDraw.Draw(c); h = H/2*ease(seam); d.line((W/2, H/2-h, W/2, H/2+h), fill=GOLD+(int(255*seam),), width=2)
    for i, y in enumerate(BOLTS): bolt_pair(c, y, locks[i], throw)
    return c

def split(face_im, dest, o):
    """the face parts at the centre; each half slides to its edge onto the app"""
    gapx = int((W/2 + 30)*o); c = Image.new('RGBA', (W, H), DEEP+(255,))
    sc = 0.97 + 0.03*o; dd = dest.resize((int(W*sc), int(H*sc)), Image.LANCZOS); c.alpha_composite(dd, ((W-dd.width)//2, (H-dd.height)//2))
    c.alpha_composite(Image.new('RGBA', (W, H), DEEP+(int(150*(1-o)**1.5),)))
    sp = Image.new('RGBA', (W, H), (0,0,0,0)); ImageDraw.Draw(sp).rectangle((W//2-gapx, 0, W//2+gapx, H), fill=GLOW+(int(60*(1-o)),))
    c.alpha_composite(sp.filter(ImageFilter.GaussianBlur(30)))
    L = face_im.crop((0, 0, W//2, H)); R = face_im.crop((W//2, 0, W, H))
    for img, x, edge, side in ((L, -gapx, W//2-gapx, -1), (R, W//2+gapx, W//2+gapx, 1)):
        sh = Image.new('RGBA', (W, H), (0,0,0,0)); lo, hi = sorted((edge, edge - side*46))
        ImageDraw.Draw(sh).rectangle((lo, 0, hi, H), fill=(0,0,0,110)); c.alpha_composite(sh.filter(ImageFilter.GaussianBlur(16)))
        c.alpha_composite(img, (x, 0)); dd2 = ImageDraw.Draw(c); t0 = edge-10 if side < 0 else edge
        dd2.rectangle((t0, 0, t0+10, H), fill=(0x0B,0x40,0x26,255)); dd2.line((edge, 0, edge, H), fill=GOLD+(255,), width=3)
    return c

# ---- the window: the launch plays inside the window the person opened ---------------------------
DESK_FILE = os.environ.get('ZXV_DESKTOP', '')
WX, WY, WW, WH = 136, 78, 1168, 760          # window outer rect on the 1440 x 900 desktop
TB = 34                                      # title bar
CW, CH = WW - 16, WH - TB - 8                # content: the face is drawn at 1440 x 900 and fitted here
def desktop():
    if DESK_FILE and os.path.exists(DESK_FILE): dk = Image.open(DESK_FILE).convert('RGBA').resize((W, H))
    else:
        dk = Image.new('RGBA', (W, H), GROUND+(255,)); d = ImageDraw.Draw(dk)
        d.rectangle((0, 0, W, 44), fill=PANEL); d.rectangle((0, 44, 72, H), fill=(0x02, 0x2E, 0x15))
    return Image.alpha_composite(dk, Image.new('RGBA', (W, H), (0, 0x14, 0x08, 120)))

def window(desk, th, content, appear=1.0):
    c = desk.copy(); u = ease(appear); s = 0.94 + 0.06*u
    win = Image.new('RGBA', (WW, WH), (0,0,0,0)); d = ImageDraw.Draw(win)
    d.rounded_rectangle((0, 0, WW-1, WH-1), radius=14, fill=PANEL+(255,), outline=HAIR+(255,), width=1)
    d.line((14, TB-1, WW-14, TB-1), fill=GOLD+(160,), width=1)
    for k in range(3): d.ellipse((16+k*20, 11, 28+k*20, 23), outline=GOLD+(150,), width=1)      # close, shrink, fill: engraved, ZXV's own
    m, colr = th['_icon']; ic = (colr if colr is not None else gold_from_mask(m, GOLD, 1)).copy(); ic.thumbnail((20, 20), Image.LANCZOS)
    tw = font(15).getlength(th['title']); x0 = WW/2 - (tw + 26)/2
    win.alpha_composite(ic, (int(x0), int(TB/2 - ic.height/2))); ImageDraw.Draw(win).text((x0 + 26, TB/2), th['title'], font=font(15), fill=TXT+(255,), anchor='lm')
    cm = Image.new('L', (CW, CH), 0); ImageDraw.Draw(cm).rounded_rectangle((0, 0, CW-1, CH-1), radius=10, fill=255)
    win.paste(content.resize((CW, CH), Image.LANCZOS), (8, TB), cm)
    sh = Image.new('RGBA', (W, H), (0,0,0,0)); ImageDraw.Draw(sh).rounded_rectangle((WX+10, WY+18, WX+WW+10, WY+WH+18), radius=18, fill=(0,0,0,int(120*u)))
    c.alpha_composite(sh.filter(ImageFilter.GaussianBlur(18)))
    if s < 1: win = win.resize((int(WW*s), int(WH*s)), Image.LANCZOS)
    if u < 1: win.putalpha(win.getchannel('A').point(lambda v: int(v*u)))
    c.alpha_composite(win, (int(WX + (WW - win.width)/2), int(WY + (WH - win.height)/2)))
    return c

# ---- one launch -------------------------------------------------------------------------------------
GATHER = [(0, 0.0), (180, 0.10), (420, 0.26), (560, 0.30), (820, 0.55), (1050, 0.80), (1250, 1.0)]
RELEASE = [(0, 0.02), (200, 0.12), (460, 0.30), (700, 0.36), (960, 0.62), (1200, 0.85), (1400, 1.0)]
LOCK_AT = [0.10, 0.28, 0.46, 0.64, 0.82]
def keyed(K, t):
    for (t0, p0), (t1, p1) in zip(K, K[1:]):
        if t <= t1: return p0 + (p1 - p0)*ease((t - t0)/(t1 - t0))
    return 1.0

def run(app, out):
    th = THEMES[app]; th['_icon'] = icon_layers(th); dest = destination(th); desk = desktop()
    b, tick, e = LS.RECIPES[app](); dt = 1000/FPS
    frames = []; events = [(0, b())]; t = 0.0
    def emit(content, appear=1.0): frames.append(window(desk, th, content, appear))
    while t < 300: emit(face(th, t/1000, 0)[0], t/300); t += dt                 # the window opens on the dim screen
    g0 = t; prev = [0]*N; nlit = 0
    while t < g0 + 1250:                                                         # the bars gather, lighting each part
        f, lits = face(th, t/1000, keyed(GATHER, t - g0), txt=ease((t - g0)/300))
        new = [i for i in range(N) if prev[i] < 0.5 <= lits[i]]
        for j, i in enumerate(new):
            step = 6 if i == N//2 else nlit; nlit += i != N//2
            events.append((t + 70*j, tick(step, N)))
        prev = lits; emit(f); t += dt
    for k in range(1, 7): emit(face(th, t/1000, 1, hero=ease(k/6), flash=ease(k/6), txt=1)[0]); t += dt        # the icon lights
    for k in range(1, 7): emit(face(th, t/1000, 1, hero=1, flash=1 - ease(k/6), txt=1)[0]); t += dt
    for k in range(0, 9): emit(face(th, t/1000, 1, hero=1, sweep=k/8, txt=1)[0]); t += dt                     # one sweep
    r0 = t; lockt = [None]*5                                                     # the top bar, the bolts locking
    while t < r0 + RELEASE[-1][0]:
        p = keyed(RELEASE, t - r0); locks = []
        for i, th_ in enumerate(LOCK_AT):
            if p >= th_ and lockt[i] is None: lockt[i] = t
            locks.append(0 if lockt[i] is None else min(1, (t - lockt[i])/240))
        emit(door_face(th, t/1000, p, locks, 0, 0)); t += dt
    events.append((t, e()))                                                      # bar full: the end cue, bolts throw
    for k in range(1, 7): emit(door_face(th, t/1000, 1, [1]*5, k/6, k/6)); t += dt
    F = door_face(th, t/1000, 1, [1]*5, 1, 1); n = 27
    for k in range(1, n+1): u = k/n; emit(split(F, dest, u*u*u*(u*(u*6 - 15) + 10))); t += dt
    still = window(desk, th, dest)
    for _ in range(int(1300/dt)): frames.append(still); t += dt
    total = int(SR*(t/1000 + 2.2)); mix = np.zeros((total, 2))
    for ms, x in events: i = int(ms/1000*SR); mix[i:i+len(x)] += x[:total - i]
    LS.write(f'{out}/{app}.wav', LS.finish(mix, 0.9, None, 0.2))
    p = subprocess.Popen(['ffmpeg', '-y', '-loglevel', 'error', '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-s', f'{W}x{H}', '-r', str(FPS), '-i', '-',
                          '-i', f'{out}/{app}.wav', '-c:v', 'libx264', '-pix_fmt', 'yuv420p', '-crf', '20', '-preset', 'medium',
                          '-c:a', 'aac', '-b:a', '160k', '-shortest', f'{out}/{app}-launch.mp4'], stdin=subprocess.PIPE)
    for f in frames: p.stdin.write(f.convert('RGB').tobytes())
    p.stdin.close(); p.wait(); os.remove(f'{out}/{app}.wav')
    nf = len(frames)
    for tag, k in (('a-loading', int((300 + 750)/dt)), ('b-bolts', int((r0 + 1100)/dt)), ('c-opening', nf - int(1300/dt) - 14)):
        frames[k].convert('RGB').save(f'{out}/stills/{app}-{tag}.png')
    print(app, nf, 'frames', [int(ms) for ms, _ in events])

if __name__ == '__main__':
    out = sys.argv[1]; os.makedirs(out + '/stills', exist_ok=True)
    for app in (sys.argv[2:] or THEMES): run(app, out)
