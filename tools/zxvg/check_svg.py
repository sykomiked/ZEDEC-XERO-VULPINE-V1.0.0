#!/usr/bin/env python3
"""check_svg.py - rasterise ZXV-subset SVG (numpy scanline, Pillow I/O) and score it.

Two jobs:
  * IoU check of a traced logo against its source raster:
        python3 tools/zxvg/check_svg.py logo.svg src.png [--mode mono|levels|palette]
              [--colors N] [--key-white] [--blur 0.8]
    Each <path> layer is flattened and filled (even-odd, exact point sampling
    by a vectorised scanline, 2x2 supersampled) at the source's own pixel
    grid and compared
    with the mask trace_logo.build_layers() derives from the source.  Layer 0
    is the whole ink silhouette; layer k is the stacked "rank >= k" region.
  * preview render of any file in the subset (paths, g/transform, opacity,
    fill-opacity, fill-rule, rect/circle/ellipse/polygon/line, linear and
    radial gradients with userSpaceOnUse coordinates):
        python3 tools/zxvg/check_svg.py frame.svg --render out.png [--width 1440]
        [--compare reference.png]   (reports mean absolute RGB error)

Group opacity is composited properly (each <g opacity> renders to its own
layer).  stdlib + Pillow + numpy.
SPDX-License-Identifier: Apache-2.0
"""
import argparse
import math
import os
import re
import sys
import xml.etree.ElementTree as ET

import numpy as np
from PIL import Image

NS = '{http://www.w3.org/2000/svg}'

# ------------------------------------------------------------- path parsing

_TOK = re.compile(r'[MmLlHhVvCcSsQqTtZzAa]|[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?')


def parse_path(d):
    """-> list of subpaths; each is a list of segments in absolute coords:
    ('M',p) ('L',p) ('C',c1,c2,p) ('Q',c,p), closed implicitly."""
    toks = _TOK.findall(d)
    i = 0
    subs = []
    cur = (0.0, 0.0); start = cur; cmd = None; last_c = None; last_q = None
    sub = None

    def num():
        nonlocal i
        v = float(toks[i]); i += 1
        return v
    while i < len(toks):
        t = toks[i]
        if re.match(r'[A-Za-z]', t):
            cmd = t; i += 1
            if cmd in 'Zz':
                if sub:
                    subs.append(sub)
                sub = None; cur = start; last_c = last_q = None
                continue
        elif cmd is None:
            raise ValueError('path data starts with a number')
        rel = cmd.islower()
        C = cmd.upper()
        ox, oy = cur if rel else (0.0, 0.0)
        if C == 'M':
            p = (num() + ox, num() + oy)
            if sub:
                subs.append(sub)
            sub = [('M', p)]; cur = start = p
            cmd = 'l' if rel else 'L'
            last_c = last_q = None
        elif C == 'L':
            p = (num() + ox, num() + oy); sub.append(('L', p)); cur = p; last_c = last_q = None
        elif C == 'H':
            p = (num() + ox, cur[1]); sub.append(('L', p)); cur = p; last_c = last_q = None
        elif C == 'V':
            p = (cur[0], num() + (oy if rel else 0.0)); sub.append(('L', p)); cur = p; last_c = last_q = None
        elif C == 'C':
            c1 = (num() + ox, num() + oy); c2 = (num() + ox, num() + oy); p = (num() + ox, num() + oy)
            sub.append(('C', c1, c2, p)); cur = p; last_c = c2; last_q = None
        elif C == 'S':
            c1 = (2 * cur[0] - last_c[0], 2 * cur[1] - last_c[1]) if last_c else cur
            c2 = (num() + ox, num() + oy); p = (num() + ox, num() + oy)
            sub.append(('C', c1, c2, p)); cur = p; last_c = c2; last_q = None
        elif C == 'Q':
            c = (num() + ox, num() + oy); p = (num() + ox, num() + oy)
            sub.append(('Q', c, p)); cur = p; last_q = c; last_c = None
        elif C == 'T':
            c = (2 * cur[0] - last_q[0], 2 * cur[1] - last_q[1]) if last_q else cur
            p = (num() + ox, num() + oy); sub.append(('Q', c, p)); cur = p; last_q = c; last_c = None
        elif C == 'A':
            raise ValueError('arcs are outside the ZXV subset; use cubics')
        else:
            raise ValueError('bad command ' + cmd)
    if sub:
        subs.append(sub)
    return subs


def flatten(subs, M, px_tol=0.25):
    """subpaths -> list of (n,2) polygons in output pixels (affine M applied)."""
    a, b, c, d, e, f = M
    sc = math.sqrt(abs(a * d - b * c)) or 1.0
    polys = []
    for sub in subs:
        pts = []
        cur = None
        for sg in sub:
            if sg[0] in 'ML':
                pts.append(sg[1]); cur = sg[1]
            elif sg[0] == 'C':
                p0, c1, c2, p3 = cur, sg[1], sg[2], sg[3]
                L = (math.dist(p0, c1) + math.dist(c1, c2) + math.dist(c2, p3)) * sc
                n = max(2, min(96, int(math.ceil(math.sqrt(L / px_tol) * 0.6))))
                t = np.linspace(0, 1, n + 1)[1:, None]
                m = 1 - t
                q = (m ** 3 * np.array(p0) + 3 * m * m * t * np.array(c1) + 3 * m * t * t * np.array(c2) + t ** 3 * np.array(p3))
                pts.extend(map(tuple, q)); cur = p3
            elif sg[0] == 'Q':
                p0, c1, p2 = cur, sg[1], sg[2]
                L = (math.dist(p0, c1) + math.dist(c1, p2)) * sc
                n = max(2, min(64, int(math.ceil(math.sqrt(L / px_tol) * 0.6))))
                t = np.linspace(0, 1, n + 1)[1:, None]; m = 1 - t
                q = m * m * np.array(p0) + 2 * m * t * np.array(c1) + t * t * np.array(p2)
                pts.extend(map(tuple, q)); cur = p2
        if len(pts) >= 3:
            P = np.asarray(pts, dtype=np.float64)
            X = a * P[:, 0] + c * P[:, 1] + e
            Y = b * P[:, 0] + d * P[:, 1] + f
            polys.append(np.stack([X, Y], axis=1))
    return polys


def coverage(polys, W, H, ss=2, evenodd=True):
    """fractional coverage (H,W) float32 of closed polygons given in output
    px.  Exact point sampling at ss x ss sub-pixel centres by a vectorised
    scanline: every edge crossing of a sample row toggles (even-odd) or adds
    its direction (non-zero) at the first sample centre right of it."""
    Hs, Ws = H * ss, W * ss
    if not polys:
        return np.zeros((H, W), dtype=np.float32)
    P0 = np.concatenate([P for P in polys]) * ss
    P1 = np.concatenate([np.roll(P, -1, axis=0) for P in polys]) * ss
    y0, y1 = P0[:, 1], P1[:, 1]
    keep = y0 != y1
    P0, P1, y0, y1 = P0[keep], P1[keep], y0[keep], y1[keep]
    up = y1 > y0
    ymin = np.minimum(y0, y1); ymax = np.maximum(y0, y1)
    j0 = np.clip(np.ceil(ymin - 0.5), 0, Hs).astype(np.int64)
    j1 = np.clip(np.ceil(ymax - 0.5), 0, Hs).astype(np.int64)
    cnt = j1 - j0
    ok = cnt > 0
    if not ok.any():
        return np.zeros((H, W), dtype=np.float32)
    P0, P1, j0, cnt, up = P0[ok], P1[ok], j0[ok], cnt[ok], up[ok]
    e = np.repeat(np.arange(len(cnt)), cnt)
    off = np.arange(cnt.sum()) - np.repeat(np.cumsum(cnt) - cnt, cnt)
    j = j0[e] + off
    yc = j + 0.5
    x0, yy0, x1, yy1 = P0[e, 0], P0[e, 1], P1[e, 0], P1[e, 1]
    x = x0 + (yc - yy0) * (x1 - x0) / (yy1 - yy0)
    col = np.clip(np.ceil(x - 0.5), 0, Ws).astype(np.int64)
    idx = j * (Ws + 1) + col
    if evenodd:
        t = (np.bincount(idx, minlength=Hs * (Ws + 1)) & 1).astype(np.uint8).reshape(Hs, Ws + 1)
        inside = np.bitwise_xor.accumulate(t, axis=1)[:, :Ws]
    else:
        w = np.where(up[e], 1.0, -1.0)
        t = np.bincount(idx, weights=w, minlength=Hs * (Ws + 1)).reshape(Hs, Ws + 1)
        inside = (np.cumsum(t, axis=1)[:, :Ws] != 0).astype(np.uint8)
    return inside.reshape(H, ss, W, ss).mean(axis=(1, 3)).astype(np.float32)


# --------------------------------------------------------------- transforms

def mat_mul(A, B):
    a1, b1, c1, d1, e1, f1 = A
    a2, b2, c2, d2, e2, f2 = B
    return (a1 * a2 + c1 * b2, b1 * a2 + d1 * b2, a1 * c2 + c1 * d2, b1 * c2 + d1 * d2,
            a1 * e2 + c1 * f2 + e1, b1 * e2 + d1 * f2 + f1)


def parse_transform(s):
    M = (1, 0, 0, 1, 0, 0)
    for name, args in re.findall(r'(\w+)\s*\(([^)]*)\)', s or ''):
        v = [float(x) for x in re.split(r'[\s,]+', args.strip()) if x]
        if name == 'translate':
            T = (1, 0, 0, 1, v[0], v[1] if len(v) > 1 else 0)
        elif name == 'scale':
            T = (v[0], 0, 0, v[1] if len(v) > 1 else v[0], 0, 0)
        elif name == 'matrix':
            T = tuple(v)
        elif name == 'rotate':
            r = math.radians(v[0]); cs, sn = math.cos(r), math.sin(r)
            T = (cs, sn, -sn, cs, 0, 0)
            if len(v) == 3:
                T = mat_mul(mat_mul((1, 0, 0, 1, v[1], v[2]), T), (1, 0, 0, 1, -v[1], -v[2]))
        else:
            raise ValueError('transform ' + name)
        M = mat_mul(M, T)
    return M

# ----------------------------------------------------------------- shapes

K = 0.5522847498


def ellipse_path(cx, cy, rx, ry):
    return [[('M', (cx + rx, cy)),
             ('C', (cx + rx, cy + K * ry), (cx + K * rx, cy + ry), (cx, cy + ry)),
             ('C', (cx - K * rx, cy + ry), (cx - rx, cy + K * ry), (cx - rx, cy)),
             ('C', (cx - rx, cy - K * ry), (cx - K * rx, cy - ry), (cx, cy - ry)),
             ('C', (cx + K * rx, cy - ry), (cx + rx, cy - K * ry), (cx + rx, cy))]]


def rect_path(x, y, w, h, rx, ry):
    if rx <= 0 and ry <= 0:
        return [[('M', (x, y)), ('L', (x + w, y)), ('L', (x + w, y + h)), ('L', (x, y + h))]]
    rx = min(rx or ry, w / 2); ry = min(ry or rx, h / 2)
    return [[('M', (x + rx, y)), ('L', (x + w - rx, y)),
             ('C', (x + w - rx + K * rx, y), (x + w, y + ry - K * ry), (x + w, y + ry)),
             ('L', (x + w, y + h - ry)),
             ('C', (x + w, y + h - ry + K * ry), (x + w - rx + K * rx, y + h), (x + w - rx, y + h)),
             ('L', (x + rx, y + h)),
             ('C', (x + rx - K * rx, y + h), (x, y + h - ry + K * ry), (x, y + h - ry)),
             ('L', (x, y + ry)),
             ('C', (x, y + ry - K * ry), (x + rx - K * rx, y), (x + rx, y))]]


def line_path(x1, y1, x2, y2, w):
    dx, dy = x2 - x1, y2 - y1
    L = math.hypot(dx, dy) or 1.0
    nx, ny = -dy / L * w / 2, dx / L * w / 2
    return [[('M', (x1 + nx, y1 + ny)), ('L', (x2 + nx, y2 + ny)), ('L', (x2 - nx, y2 - ny)), ('L', (x1 - nx, y1 - ny))]]

# ------------------------------------------------------------------ paint

def parse_color(s):
    s = s.strip()
    if s.startswith('#'):
        h = s[1:]
        if len(h) == 3:
            h = ''.join(ch * 2 for ch in h)
        return tuple(int(h[i:i + 2], 16) / 255.0 for i in (0, 2, 4))
    m = re.match(r'rgb\(([^)]*)\)', s)
    if m:
        return tuple(float(v) / 255.0 for v in m.group(1).split(','))
    named = {'black': (0, 0, 0), 'white': (1, 1, 1)}
    return named.get(s, (0, 0, 0))


def _float(v, default=0.0):
    if v is None:
        return default
    v = v.strip()
    if v.endswith('%'):
        return float(v[:-1]) / 100.0
    return float(v)


class Renderer:
    def __init__(self, root, W, H, ss=2):
        self.root = root
        self.W, self.H, self.ss = W, H, ss
        self.grads = {}
        for el in root.iter():
            tag = el.tag.replace(NS, '')
            if tag in ('linearGradient', 'radialGradient') and el.get('id'):
                self.grads[el.get('id')] = el
        vb = [float(v) for v in re.split(r'[\s,]+', root.get('viewBox').strip())]
        sx, sy = W / vb[2], H / vb[3]
        self.base = (sx, 0, 0, sy, -vb[0] * sx, -vb[1] * sy)
        self.yy, self.xx = np.mgrid[0:H, 0:W].astype(np.float32) + 0.5

    def _stops(self, g):
        st = []
        for s in g:
            if s.tag.replace(NS, '') != 'stop':
                continue
            st.append((_float(s.get('offset'), 0), parse_color(s.get('stop-color', '#000')),
                       _float(s.get('stop-opacity'), 1.0)))
        if not st and (g.get('href') or g.get('{http://www.w3.org/1999/xlink}href')):
            ref = (g.get('href') or g.get('{http://www.w3.org/1999/xlink}href'))[1:]
            return self._stops(self.grads[ref])
        return st

    def paint(self, fill, M):
        """-> (rgb (H,W,3) or (3,), alpha multiplier (H,W) or scalar)."""
        m = re.match(r'url\(#([^)]+)\)', fill)
        if not m:
            return np.array(parse_color(fill), dtype=np.float32), 1.0
        g = self.grads[m.group(1)]
        tag = g.tag.replace(NS, '')
        GM = mat_mul(M, parse_transform(g.get('gradientTransform')))
        a, b, c, d, e, f = GM
        det = a * d - b * c
        # inverse map output px -> gradient user space
        X = self.xx - e; Y = self.yy - f
        ux = (d * X - c * Y) / det; uy = (-b * X + a * Y) / det
        if tag == 'linearGradient':
            x1, y1 = _float(g.get('x1'), 0), _float(g.get('y1'), 0)
            x2, y2 = _float(g.get('x2'), 1), _float(g.get('y2'), 0)
            dx, dy = x2 - x1, y2 - y1
            t = ((ux - x1) * dx + (uy - y1) * dy) / max(dx * dx + dy * dy, 1e-12)
        else:
            cx, cy, r = _float(g.get('cx'), 0.5), _float(g.get('cy'), 0.5), _float(g.get('r'), 0.5)
            t = np.hypot(ux - cx, uy - cy) / max(r, 1e-12)
        t = np.clip(t, 0, 1)
        st = self._stops(g)
        offs = np.array([s[0] for s in st]); cols = np.array([s[1] for s in st]); ops = np.array([s[2] for s in st])
        rgb = np.stack([np.interp(t, offs, cols[:, k]) for k in range(3)], axis=-1).astype(np.float32)
        al = np.interp(t, offs, ops).astype(np.float32)
        return rgb, al

    def render(self):
        buf = np.zeros((self.H, self.W, 4), dtype=np.float32)   # premultiplied
        self._group(self.root, self.base, {'fill': '#000', 'fill-rule': 'nonzero'}, buf)
        return buf

    def _attrs(self, el, inh):
        st = dict(inh)
        for k in ('fill', 'fill-rule', 'fill-opacity', 'stroke', 'stroke-width', 'stroke-opacity'):
            if el.get(k) is not None:
                st[k] = el.get(k)
        sty = el.get('style')
        if sty:
            for kv in sty.split(';'):
                if ':' in kv:
                    k, v = kv.split(':', 1); st[k.strip()] = v.strip()
        st.pop('opacity', None)
        return st

    def _group(self, el, M, inh, buf):
        for ch in el:
            tag = ch.tag.replace(NS, '')
            if tag in ('defs', 'title', 'desc', 'linearGradient', 'radialGradient', 'metadata'):
                continue
            CM = mat_mul(M, parse_transform(ch.get('transform')))
            st = self._attrs(ch, inh)
            op = _float(ch.get('opacity'), 1.0)
            if tag == 'g':
                if op < 1.0:
                    sub = np.zeros_like(buf)
                    self._group(ch, CM, st, sub)
                    buf *= (1 - sub[..., 3:4] * op)
                    buf += sub * op
                else:
                    self._group(ch, CM, st, buf)
                continue
            if tag == 'path':
                subs = parse_path(ch.get('d', ''))
            elif tag == 'rect':
                subs = rect_path(_float(ch.get('x')), _float(ch.get('y')), _float(ch.get('width')),
                                 _float(ch.get('height')), _float(ch.get('rx')), _float(ch.get('ry')))
            elif tag == 'circle':
                r = _float(ch.get('r')); subs = ellipse_path(_float(ch.get('cx')), _float(ch.get('cy')), r, r)
            elif tag == 'ellipse':
                subs = ellipse_path(_float(ch.get('cx')), _float(ch.get('cy')), _float(ch.get('rx')), _float(ch.get('ry')))
            elif tag == 'polygon':
                v = [float(x) for x in re.split(r'[\s,]+', ch.get('points').strip()) if x]
                subs = [[('M', (v[0], v[1]))] + [('L', (v[k], v[k + 1])) for k in range(2, len(v), 2)]]
            elif tag == 'line':
                subs = None
            else:
                raise ValueError('element <%s> is outside the ZXV subset' % tag)
            # fill
            fill = st.get('fill', '#000')
            if tag != 'line' and fill != 'none':
                cov = coverage(flatten(subs, CM), self.W, self.H, self.ss, st.get('fill-rule') == 'evenodd')
                self._comp(buf, cov, fill, CM, op * _float(st.get('fill-opacity'), 1.0))
            stroke = st.get('stroke')
            if stroke and stroke != 'none':
                w = _float(st.get('stroke-width'), 1.0)
                if tag == 'line':
                    polys = flatten(line_path(_float(ch.get('x1')), _float(ch.get('y1')),
                                              _float(ch.get('x2')), _float(ch.get('y2')), w), CM)
                    cov = coverage(polys, self.W, self.H, self.ss, False)
                else:
                    raise ValueError('stroke on <%s>: the subset strokes only <line>' % tag)
                self._comp(buf, cov, stroke, CM, op * _float(st.get('stroke-opacity'), 1.0))

    def _comp(self, buf, cov, fill, M, op):
        rgb, al = self.paint(fill, M)
        A = cov * al * op
        if np.ndim(rgb) == 1:
            src = A[..., None] * rgb[None, None, :]
        else:
            src = A[..., None] * rgb
        buf[..., :3] = buf[..., :3] * (1 - A[..., None]) + src
        buf[..., 3] = buf[..., 3] * (1 - A) + A


def render_file(path, width=None, ss=2):
    root = ET.parse(path).getroot()
    vb = [float(v) for v in re.split(r'[\s,]+', root.get('viewBox').strip())]
    W = int(width or vb[2]); H = int(round(W * vb[3] / vb[2]))
    buf = Renderer(root, W, H, ss).render()
    return buf


def to_image(buf, bg=None):
    a = buf[..., 3:4]
    if bg is not None:
        rgb = buf[..., :3] + (1 - a) * np.array(bg, dtype=np.float32) / 255.0
        return Image.fromarray((np.clip(rgb, 0, 1) * 255 + 0.5).astype(np.uint8), 'RGB')
    rgb = np.where(a > 0, buf[..., :3] / np.maximum(a, 1e-6), 0)
    out = np.concatenate([rgb, a], axis=-1)
    return Image.fromarray((np.clip(out, 0, 1) * 255 + 0.5).astype(np.uint8), 'RGBA')

# --------------------------------------------------------------------- IoU

def layer_coverages(svg_path, W, H, ss=2):
    """coverage of each top-level-or-grouped <path>, in document order, at the
    viewBox's own pixel grid (viewBox must be 0 0 W H)."""
    root = ET.parse(svg_path).getroot()
    out = []
    for el in root.iter(NS + 'path'):
        polys = flatten(parse_path(el.get('d', '')), (1, 0, 0, 1, 0, 0))
        out.append((el.get('fill'), coverage(polys, W, H, ss, el.get('fill-rule') == 'evenodd')))
    return out


def iou(a, b):
    u = np.logical_or(a, b).sum()
    return float(np.logical_and(a, b).sum() / u) if u else 1.0


def check_logo(svg_path, src, mode='mono', colors=1, key_white=False, blur=0.8, ss=2):
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import trace_logo as tl
    rgb, alpha = tl.load_rgba(src, key_white)
    H, W = alpha.shape
    layers = tl.build_layers(rgb, alpha, mode, colors, None, blur)
    covs = layer_coverages(svg_path, W, H, ss)
    res = []
    for k, ((col, _f, truth), (fill, cov)) in enumerate(zip(layers, covs)):
        res.append((k, fill, iou(cov >= 0.5, truth)))
    # composite colour error over the ink, against the source colours
    comp = np.zeros((H, W, 3), dtype=np.float32); cova = np.zeros((H, W), dtype=np.float32)
    for fill, cov in covs:
        c = np.array(parse_color(fill), dtype=np.float32) * 255
        comp = comp * (1 - cov[..., None]) + cov[..., None] * c
        cova = cova * (1 - cov) + cov
    ink = alpha >= 0.5
    rgb_err = float(np.abs(comp[ink] - rgb[ink]).mean()) if ink.any() else 0.0
    return res, rgb_err


def main(argv=None):
    ap = argparse.ArgumentParser(description='rasterise ZXV-subset SVG; IoU vs source or preview render')
    ap.add_argument('svg'); ap.add_argument('src', nargs='?')
    ap.add_argument('--mode', default='mono'); ap.add_argument('--colors', type=int, default=1)
    ap.add_argument('--key-white', action='store_true'); ap.add_argument('--blur', type=float, default=0.8)
    ap.add_argument('--render'); ap.add_argument('--width', type=int)
    ap.add_argument('--bg', default=None, help='flatten preview on this #RRGGBB')
    ap.add_argument('--compare', help='reference PNG for a mean-abs-error report')
    a = ap.parse_args(argv)
    if a.src:
        res, err = check_logo(a.svg, a.src, a.mode, a.colors, a.key_white, a.blur)
        for k, fill, v in res:
            print('layer %d %s IoU %.4f' % (k, fill, v))
        print('mean |rgb error| over ink: %.1f/255' % err)
    if a.render or a.compare:
        buf = render_file(a.svg, a.width)
        bg = tuple(int(255 * x) for x in parse_color(a.bg)) if a.bg else None
        im = to_image(buf, bg)
        if a.render:
            im.save(a.render)
        if a.compare:
            ref = np.asarray(Image.open(a.compare).convert('RGB').resize(im.size), dtype=np.float32)
            got = np.asarray(im.convert('RGB'), dtype=np.float32)
            print('mean |rgb diff| vs %s: %.2f/255' % (os.path.basename(a.compare), float(np.abs(ref - got).mean())))
    return 0


if __name__ == '__main__':
    sys.exit(main())
