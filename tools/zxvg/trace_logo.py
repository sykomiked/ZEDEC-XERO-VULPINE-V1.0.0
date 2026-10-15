#!/usr/bin/env python3
"""trace_logo.py - raster logo -> clean vector SVG (ZXV native vector rule).

Every graphic in ZXV is native vector.  This turns the raster brand logos
into flat-colour SVG made only of <path fill-rule="evenodd"> layers whose
viewBox is the source image in pixels, so the result overlays the source
exactly and can be compared against it (see check_svg.py).

Pipeline, per colour layer:
  1. a scalar field in [0,1] whose 0.5 iso-line is the layer's edge
     (alpha for one-colour logos; a darkness/label field for stacked layers),
     optionally upscaled (bicubic) so thin strokes keep their shape;
  2. marching squares with linear interpolation (sub-pixel contours,
     holes included, saddles resolved by the cell centre);
  3. corners found by turning angle over a short arc; each corner-to-corner
     run is simplified with Douglas-Peucker (a run that collapses to its two
     ends is a straight line), otherwise fitted with least-squares cubic
     Beziers (Schneider) with Newton reparameterisation, split at the worst
     point until every contour sample is within --tol pixels;
  4. near-straight cubics are written as lines; tiny specks are dropped.

Multi-colour logos are written as STACKED layers: layer k covers every pixel
whose label rank is >= k, so each later layer paints over the earlier ones
and no hairline gaps can open between neighbouring colours.

Modes:
  mono    one colour, field = alpha (or keyed darkness with --key-white)
  levels  N nested luminance levels (duotones, one-ink line art)
  palette N colours by k-means in RGB, stacked by luminance

Usage:
  python3 tools/zxvg/trace_logo.py in.png out.svg [--mode mono|levels|palette]
        [--colors N] [--fill #RRGGBB] [--tol 0.35] [--upscale N]
        [--min-area 2] [--key-white] [--blur 0.8]

stdlib + Pillow + numpy only.
SPDX-License-Identifier: Apache-2.0
"""
import argparse
import math
import sys

import numpy as np
from PIL import Image

# ----------------------------------------------------------------- fields

def gauss_blur(a, sigma):
    """separable gaussian on a float array (edge-replicated)."""
    if sigma <= 0:
        return a
    r = max(1, int(math.ceil(sigma * 3)))
    k = np.exp(-0.5 * (np.arange(-r, r + 1) / sigma) ** 2)
    k /= k.sum()
    out = a.astype(np.float32)
    for axis in (0, 1):
        p = np.pad(out, [(r, r) if ax == axis else (0, 0) for ax in (0, 1)], mode='edge')
        acc = np.zeros_like(out)
        n = out.shape[axis]
        for i, w in enumerate(k):
            sl = [slice(None), slice(None)]
            sl[axis] = slice(i, i + n)
            acc += w * p[tuple(sl)]
        out = acc
    return out


def upscale(field, s):
    if s == 1:
        return field.astype(np.float32)
    h, w = field.shape
    im = Image.fromarray(field.astype(np.float32), 'F')
    return np.asarray(im.resize((w * s, h * s), Image.BICUBIC), dtype=np.float32)


def load_rgba(path, key_white=False, unpremul=False):
    """RGBA float arrays (rgb 0..255, alpha 0..1).  key_white: ink on white
    paper -> alpha from darkness (and, with unpremul, the colour separated
    from the white; otherwise the ink keeps its printed colour)."""
    im = Image.open(path).convert('RGBA')
    a = np.asarray(im).astype(np.float32)
    rgb, al = a[..., :3], a[..., 3] / 255.0
    if key_white and al.min() >= 1.0:
        lum = rgb.mean(axis=2)
        t = np.clip((245.0 - lum) / (245.0 - 120.0), 0, 1)
        if unpremul:
            tt = np.maximum(t, 1e-3)[..., None]
            rgb = np.clip((rgb - 255.0 * (1 - tt)) / tt, 0, 255)
        al = t
    return rgb, al


def luminance(rgb):
    return (rgb[..., 0] * 0.3 + rgb[..., 1] * 0.59 + rgb[..., 2] * 0.11) / 255.0

# --------------------------------------------------------- marching squares

# corners: a=tl(8) b=tr(4) c=br(2) d=bl(1); edges T R B L
_MID = {'T': (0.5, 0.0), 'R': (1.0, 0.5), 'B': (0.5, 1.0), 'L': (0.0, 0.5)}
_CORNER = {'a': (0.0, 0.0), 'b': (1.0, 0.0), 'c': (1.0, 1.0), 'd': (0.0, 1.0)}
_CUT = {'a': ('T', 'L'), 'b': ('T', 'R'), 'c': ('R', 'B'), 'd': ('B', 'L')}
_OPP = {'a': 'c', 'b': 'd', 'c': 'a', 'd': 'b'}


def _orient(e1, e2, k):
    """order (e1,e2) so the inside point k is on the left (cross > 0)."""
    p, q = _MID[e1], _MID[e2]
    cr = (q[0] - p[0]) * (k[1] - p[1]) - (q[1] - p[1]) * (k[0] - p[0])
    return (e1, e2) if cr > 0 else (e2, e1)


def _table():
    tab = {}
    for case in range(1, 15):
        ins = {n: bool(case & bit) for n, bit in zip('abcd', (8, 4, 2, 1))}
        if case in (5, 10):
            for centre_in in (False, True):
                segs = []
                if centre_in:     # cut the two outside corners
                    for n in 'abcd':
                        if not ins[n]:
                            segs.append(_orient(*_CUT[n], (0.5, 0.5)))
                else:             # cut the two inside corners
                    for n in 'abcd':
                        if ins[n]:
                            segs.append(_orient(*_CUT[n], _CORNER[n]))
                tab[(case, centre_in)] = segs
            continue
        cross = []
        if ins['a'] != ins['b']: cross.append('T')
        if ins['b'] != ins['c']: cross.append('R')
        if ins['c'] != ins['d']: cross.append('B')
        if ins['d'] != ins['a']: cross.append('L')
        k = next(_CORNER[n] for n in 'abcd' if ins[n])
        tab[(case, None)] = [_orient(cross[0], cross[1], k)]
    return tab


_TAB = _table()


def marching_squares(field, iso=0.5, scale=1):
    """closed contours of field>iso as a list of (n,2) float arrays in
    source-pixel coordinates (pixel centres at +0.5).  Inside is on the left
    in y-down coordinates, so outer and hole contours wind oppositely."""
    F = np.pad(field.astype(np.float32), 1, mode='constant', constant_values=0.0)
    H2, W2 = F.shape
    ins = F > iso
    a, b, c, d = ins[:-1, :-1], ins[:-1, 1:], ins[1:, 1:], ins[1:, :-1]
    case = (a.astype(np.uint8) << 3) | (b.astype(np.uint8) << 2) | (c.astype(np.uint8) << 1) | d.astype(np.uint8)
    NH = H2 * W2
    froms, tos = [], []
    sad = None
    for (cs, cin), segs in _TAB.items():
        m = case == cs
        if cin is not None:
            if sad is None:
                sad = (F[:-1, :-1] + F[:-1, 1:] + F[1:, 1:] + F[1:, :-1]) * 0.25 > iso
            m = m & (sad if cin else ~sad)
        I, J = np.nonzero(m)
        if I.size == 0:
            continue
        I = I.astype(np.int64); J = J.astype(np.int64)
        ids = {'T': I * W2 + J, 'B': (I + 1) * W2 + J, 'L': NH + I * W2 + J, 'R': NH + I * W2 + J + 1}
        for e1, e2 in segs:
            froms.append(ids[e1]); tos.append(ids[e2])
    if not froms:
        return []
    fr = np.concatenate(froms); to = np.concatenate(tos)
    order = np.argsort(fr, kind='stable')
    fs = fr[order]; ts = to[order]
    nxt = np.searchsorted(fs, ts)
    # coordinates of each (sorted) edge crossing
    isv = fs >= NH
    e = np.where(isv, fs - NH, fs)
    i = e // W2; j = e % W2
    f0 = F[i, j]
    f1 = np.where(isv, F[np.minimum(i + 1, H2 - 1), j], F[i, np.minimum(j + 1, W2 - 1)])
    den = f1 - f0
    den[den == 0] = 1e-9
    t = np.clip((iso - f0) / den, 0.0, 1.0)
    x = np.where(isv, j, j + t)
    y = np.where(isv, i + t, i)
    X = ((x - 1.0) + 0.5) / scale
    Y = ((y - 1.0) + 0.5) / scale
    n = fs.size
    seen = np.zeros(n, dtype=bool)
    nxt_l = nxt.tolist()
    out = []
    for s in range(n):
        if seen[s]:
            continue
        idx = []
        k = s
        while not seen[k]:
            seen[k] = True
            idx.append(k)
            k = nxt_l[k]
        ia = np.asarray(idx)
        out.append(np.stack([X[ia], Y[ia]], axis=1))
    return out


def poly_area(P):
    x, y = P[:, 0], P[:, 1]
    return 0.5 * float(np.dot(x, np.roll(y, -1)) - np.dot(y, np.roll(x, -1)))

# --------------------------------------------------------------- fitting

def _dedupe(P, eps=1e-4):
    if len(P) < 2:
        return P
    d = np.hypot(*(np.roll(P, -1, axis=0) - P).T)
    return P[d > eps]


def find_corners(P, win=1.5, thr_deg=60.0):
    """indices of corners on a closed dense contour."""
    n = len(P)
    if n < 6:
        return list(range(n))
    seg = np.hypot(*(np.roll(P, -1, axis=0) - P).T)
    s = np.concatenate([[0.0], np.cumsum(seg)])
    L = s[-1]
    s = s[:-1]
    ext_s = np.concatenate([s - L, s, s + L])
    ext_i = np.concatenate([np.arange(n)] * 3)
    w = min(win, L / 6.0)
    ib = ext_i[np.clip(np.searchsorted(ext_s, s - w, side='right') - 1, 0, 3 * n - 1)]
    ia = ext_i[np.clip(np.searchsorted(ext_s, s + w, side='left'), 0, 3 * n - 1)]
    v1 = P - P[ib]
    v2 = P[ia] - P
    n1 = np.hypot(*v1.T); n2 = np.hypot(*v2.T)
    cosang = np.einsum('ij,ij->i', v1, v2) / np.maximum(n1 * n2, 1e-12)
    ang = np.degrees(np.arccos(np.clip(cosang, -1, 1)))
    cand = np.nonzero(ang > thr_deg)[0]
    if cand.size == 0:
        return []
    cand = cand[np.argsort(-ang[cand])]
    taken = []
    for c in cand.tolist():
        ok = True
        for t in taken:
            dd = abs(s[c] - s[t])
            if min(dd, L - dd) < w:
                ok = False
                break
        if ok:
            taken.append(c)
    return sorted(taken)


def douglas_peucker(P, eps):
    """indices kept by Douglas-Peucker on an open polyline."""
    n = len(P)
    keep = np.zeros(n, dtype=bool)
    keep[0] = keep[-1] = True
    stack = [(0, n - 1)]
    while stack:
        i0, i1 = stack.pop()
        if i1 - i0 < 2:
            continue
        a, b = P[i0], P[i1]
        ab = b - a
        L = math.hypot(ab[0], ab[1])
        q = P[i0 + 1:i1] - a
        if L < 1e-9:
            dist = np.hypot(q[:, 0], q[:, 1])
        else:
            dist = np.abs(ab[0] * q[:, 1] - ab[1] * q[:, 0]) / L
        k = int(np.argmax(dist))
        if dist[k] > eps:
            m = i0 + 1 + k
            keep[m] = True
            stack.append((i0, m)); stack.append((m, i1))
    return np.nonzero(keep)[0]


def _unit(v):
    n = math.hypot(v[0], v[1])
    return v / n if n > 1e-12 else np.array([1.0, 0.0])


def _bez_eval(B, u):
    u = u[:, None]; m = 1 - u
    return m ** 3 * B[0] + 3 * m * m * u * B[1] + 3 * m * u * u * B[2] + u ** 3 * B[3]


def _generate(P, u, t1, t2):
    p0, p3 = P[0], P[-1]
    m = 1 - u
    b1 = 3 * m * m * u; b2 = 3 * m * u * u
    A1 = t1[None, :] * b1[:, None]
    A2 = t2[None, :] * b2[:, None]
    C00 = float(np.sum(A1 * A1)); C01 = float(np.sum(A1 * A2)); C11 = float(np.sum(A2 * A2))
    base = _bez_eval(np.array([p0, p0, p3, p3]), u)
    tmp = P - base
    X0 = float(np.sum(A1 * tmp)); X1 = float(np.sum(A2 * tmp))
    det = C00 * C11 - C01 * C01
    seg = math.hypot(*(p3 - p0))
    if abs(det) > 1e-12:
        al = (X0 * C11 - X1 * C01) / det
        ar = (C00 * X1 - C01 * X0) / det
    else:
        al = ar = 0.0
    eps = 1e-6 * seg
    if al < eps or ar < eps or al > 3 * seg + 1 or ar > 3 * seg + 1:
        al = ar = seg / 3.0
    return np.array([p0, p0 + t1 * al, p3 + t2 * ar, p3])


def _reparam(P, u, B):
    d1 = 3 * (B[1:] - B[:-1])
    d2 = 2 * (d1[1:] - d1[:-1])
    uu = u[:, None]; m = 1 - uu
    Q = _bez_eval(B, u)
    Q1 = m * m * d1[0] + 2 * m * uu * d1[1] + uu * uu * d1[2]
    Q2 = m * d2[0] + uu * d2[1]
    diff = Q - P
    num = np.sum(diff * Q1, axis=1)
    den = np.sum(Q1 * Q1, axis=1) + np.sum(diff * Q2, axis=1)
    with np.errstate(divide='ignore', invalid='ignore'):
        nu = np.where(np.abs(den) > 1e-12, u - num / den, u)
    nu = np.clip(nu, 0, 1)
    nu[0], nu[-1] = 0.0, 1.0
    return np.maximum.accumulate(nu)


def _max_err(P, B, u):
    d = _bez_eval(B, u) - P
    e = np.sum(d * d, axis=1)
    k = int(np.argmax(e))
    return float(e[k]), k


def fit_cubic(P, t1, t2, tol):
    """Schneider fit of an open run P with end tangents t1 (out of P[0])
    and t2 (into P[-1], pointing backwards).  Returns list of 4x2 arrays."""
    err2 = tol * tol
    out = []
    stack = [(P, t1, t2)]
    while stack:
        Q, a, b = stack.pop()
        n = len(Q)
        if n == 2:
            dd = math.hypot(*(Q[1] - Q[0])) / 3.0
            out.append(np.array([Q[0], Q[0] + a * dd, Q[1] + b * dd, Q[1]]))
            continue
        cl = np.concatenate([[0.0], np.cumsum(np.hypot(*np.diff(Q, axis=0).T))])
        u = cl / cl[-1] if cl[-1] > 0 else np.linspace(0, 1, n)
        B = _generate(Q, u, a, b)
        e, k = _max_err(Q, B, u)
        if e < err2:
            out.append(B); continue
        if e < 16 * err2:
            for _ in range(6):
                u = _reparam(Q, u, B)
                B = _generate(Q, u, a, b)
                e, k = _max_err(Q, B, u)
                if e < err2:
                    break
            if e < err2:
                out.append(B); continue
        k = min(max(k, 1), n - 2)
        tc = _unit(Q[k - 1] - Q[k + 1])
        # push right first so left is emitted first
        stack.append((Q[k:], -tc, b))
        stack.append((Q[:k + 1], a, tc))
    return out


def _end_tangent(P, from_start, reach=1.2):
    """unit tangent at an end of an open run, from points within `reach`."""
    Q = P if from_start else P[::-1]
    d = np.hypot(*(Q - Q[0]).T)
    j = int(np.searchsorted(d > reach, True)) if np.any(d > reach) else len(Q) - 1
    j = max(1, min(j, len(Q) - 1))
    return _unit(Q[j] - Q[0])


def fit_contour(P, tol, corner_deg=60.0, corner_win=1.5):
    """closed contour -> (start point, list of segments); a segment is
    ('L', p1) or ('C', c1, c2, p3)."""
    P = _dedupe(P)
    n = len(P)
    if n < 3:
        return None
    if n < 8:
        idx = douglas_peucker(np.vstack([P, P[:1]]), tol)
        pts = np.vstack([P, P[:1]])[idx]
        return pts[0], [('L', p) for p in pts[1:]]
    corners = find_corners(P, corner_win, corner_deg)
    if not corners:
        # smooth loop: split in two at the far point so both halves fit
        # independently, with a shared tangent at both joints
        Pc = P
        tan = _unit(Pc[1] - Pc[-1])
        far = int(np.argmax(np.hypot(*(Pc - Pc[0]).T)))
        far = min(max(far, 2), n - 2)
        tanf = _unit(Pc[far + 1] - Pc[far - 1])
        runs = [(Pc[:far + 1], tan, -tanf), (np.vstack([Pc[far:], Pc[:1]]), tanf, -tan)]
    else:
        start = corners[0]
        Pc = np.roll(P, -start, axis=0)
        cs = [c - start for c in corners] + [n]
        runs = []
        for c0, c1 in zip(cs[:-1], cs[1:]):
            run = Pc[c0:c1 + 1] if c1 < n else np.vstack([Pc[c0:], Pc[:1]])
            if len(run) < 2:
                continue
            runs.append((run, _end_tangent(run, True), _end_tangent(run, False)))
    segs = []
    for run, t1, t2 in runs:
        # Douglas-Peucker decides straight runs (kept as one line); curved
        # runs get cubics whose joints share tangents (G1).
        keep = douglas_peucker(run, tol)
        if len(keep) == 2:
            segs.append(('L', run[-1]))
            continue
        for B in fit_cubic(run, t1, t2, tol):
            segs.append(_as_seg(B, tol))
    return Pc[0], segs


def _as_seg(B, tol):
    p0, c1, c2, p3 = B
    ab = p3 - p0
    L = math.hypot(ab[0], ab[1])
    if L > 1e-9:
        def dist(q):
            v = q - p0
            return abs(ab[0] * v[1] - ab[1] * v[0]) / L
        def along(q):
            v = q - p0
            return (ab[0] * v[0] + ab[1] * v[1]) / (L * L)
        if dist(c1) < tol * 0.25 and dist(c2) < tol * 0.25 and -0.05 < along(c1) < 1.05 and -0.05 < along(c2) < 1.05:
            return ('L', p3)
    return ('C', c1, c2, p3)

# ------------------------------------------------------------------ output

def _fmt(v, nd):
    s = ('%.*f' % (nd, v))
    if '.' in s:
        s = s.rstrip('0').rstrip('.')
    if s in ('-0', ''):
        s = '0'
    return s


def path_data(fitted, nd=1, relative=True):
    """fitted: list of (start, segs).  Coordinates are rounded to nd decimals
    and relative offsets are taken from the ROUNDED previous point, so error
    never accumulates along a contour."""
    q = 10 ** nd
    def r(p):
        return (round(float(p[0]) * q) / q, round(float(p[1]) * q) / q)
    parts = []
    for start, segs in fitted:
        cur = r(start)
        parts.append('M%s %s' % (_fmt(cur[0], nd), _fmt(cur[1], nd)))
        for sg in segs:
            if sg[0] == 'L':
                p = r(sg[1])
                if p == cur:
                    continue
                if relative:
                    parts.append('l%s %s' % (_fmt(p[0] - cur[0], nd), _fmt(p[1] - cur[1], nd)))
                else:
                    parts.append('L%s %s' % (_fmt(p[0], nd), _fmt(p[1], nd)))
                cur = p
            else:
                c1, c2, p = r(sg[1]), r(sg[2]), r(sg[3])
                if relative:
                    parts.append('c%s %s %s %s %s %s' % tuple(_fmt(v, nd) for v in (
                        c1[0] - cur[0], c1[1] - cur[1], c2[0] - cur[0], c2[1] - cur[1], p[0] - cur[0], p[1] - cur[1])))
                else:
                    parts.append('C%s %s %s %s %s %s' % tuple(_fmt(v, nd) for v in (c1 + c2 + p)))
                cur = p
        parts.append('z')
    return ''.join(parts)


def trace_field(field, scale=1, tol=0.35, min_area=2.0, corner_deg=60.0, iso=0.5):
    """field (already upscaled by `scale`) -> list of fitted contours."""
    out = []
    for P in marching_squares(field, iso, scale):
        if abs(poly_area(P)) < min_area:
            continue
        f = fit_contour(P, tol, corner_deg)
        if f is not None and f[1]:
            out.append(f)
    return out

# ------------------------------------------------------------------ layers

def hexcol(rgb):
    return '#%02X%02X%02X' % tuple(int(round(max(0, min(255, v)))) for v in rgb)


def kmeans(X, k, iters=25, seed=1):
    rng = np.random.default_rng(seed)
    C = X[rng.choice(len(X), size=k, replace=False)].astype(np.float64)
    # k-means++ style spread
    for i in range(1, k):
        d = np.min(((X[:, None, :] - C[None, :i, :]) ** 2).sum(-1), axis=1)
        C[i] = X[int(np.argmax(d))]
    for _ in range(iters):
        lab = np.argmin(((X[:, None, :] - C[None, :, :]) ** 2).sum(-1), axis=1)
        for i in range(k):
            m = lab == i
            if m.any():
                C[i] = X[m].mean(axis=0)
    return C


def levels_1d(v, k, iters=40):
    """k brightness levels by 1-D k-means; returns the k-1 thresholds
    (midpoints between neighbouring level centres)."""
    v = np.asarray(v, dtype=np.float64)
    if v.size > 400000:
        v = v[np.random.default_rng(0).choice(v.size, 400000, replace=False)]
    c = np.quantile(v, (np.arange(k) + 0.5) / k)
    c = np.linspace(v.min(), v.max(), k) * 0.5 + c * 0.5
    for _ in range(iters):
        t = (c[1:] + c[:-1]) / 2
        lab = np.digitize(v, t)
        for i in range(k):
            m = lab == i
            if m.any():
                c[i] = v[m].mean()
        c.sort()
    return list((c[1:] + c[:-1]) / 2)


def build_layers(rgb, alpha, mode='mono', colors=1, fill=None, blur=0.8,
                 thresholds=None):
    """-> list of (fill_hex, field_in_source_px, truth_mask).  Layer 0 is the
    whole ink; layer k (k>=1) is every ink pixel ranked >= k (stacked)."""
    ink = alpha >= 0.5
    if mode == 'mono' or colors <= 1:
        col = fill or hexcol(np.median(rgb[ink], axis=0) if ink.any() else (0, 0, 0))
        return [(col, alpha, ink)]
    if mode == 'levels':
        bright = luminance(rgb)
        if thresholds is None:
            thresholds = levels_1d(bright[ink], colors)
        layers = []
        bounds = [-1.0] + list(thresholds) + [2.0]
        lab = np.digitize(bright, thresholds)
        sm = gauss_blur(bright, blur * 0.75) if blur > 0 else bright
        for k in range(colors):
            sel = ink & (lab == k)
            col = hexcol(rgb[sel].mean(axis=0)) if sel.any() else '#000000'
            if k == 0:
                layers.append((col, alpha, ink))
            else:
                w = 0.02
                g = np.clip(0.5 + (sm - bounds[k]) / w, 0, 1)
                layers.append((col, np.minimum(alpha, g), ink & (lab >= k)))
        return layers
    # palette: k-means on ink colours, ranks by luminance
    X = rgb[ink].reshape(-1, 3)
    samp = X[np.random.default_rng(0).choice(len(X), size=min(len(X), 60000), replace=False)]
    C = kmeans(samp, colors)
    C = C[np.argsort(C @ np.array([0.3, 0.59, 0.11]))]
    lab = np.full(alpha.shape, -1, dtype=np.int16)
    flat = rgb.reshape(-1, 3)
    lab_flat = lab.reshape(-1)
    ink_idx = np.nonzero(ink.reshape(-1))[0]
    for s in range(0, len(ink_idx), 1 << 20):
        ii = ink_idx[s:s + (1 << 20)]
        lab_flat[ii] = np.argmin(((flat[ii][:, None, :] - C[None]) ** 2).sum(-1), axis=1)
    layers = []
    for k in range(colors):
        sel = lab == k
        col = hexcol(rgb[sel].mean(axis=0)) if sel.any() else hexcol(C[k])
        if k == 0:
            layers.append((col, alpha, ink))
        else:
            m = (lab >= k).astype(np.float32)
            g = gauss_blur(m, blur) if blur > 0 else m
            layers.append((col, np.minimum(alpha, g), lab >= k))
    return layers


def trace_layers(layers, size, scale=1, tol=0.35, min_area=2.0, nd=1,
                 relative=True, corner_deg=60.0, smooth=0.0, log=None):
    """-> list of (fill, path d) for each layer, bottom first.  smooth: gaussian
    sigma (source px) applied to the field before contouring; it irons out
    resampling jaggies so edges come out as clean curves."""
    out = []
    for i, (col, field, _truth) in enumerate(layers):
        f = upscale(field, scale)
        if smooth > 0:
            f = gauss_blur(f, smooth * scale)
        fitted = trace_field(f, scale, tol, min_area, corner_deg)
        d = path_data(fitted, nd, relative)
        if log:
            log('  layer %d %s: %d contours, %d bytes' % (i, col, len(fitted), len(d)))
        out.append((col, d))
    return out


def svg_document(paths, w, h, title=None, extra_attrs='', group_attrs=''):
    lines = ['<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %d %d" width="%d" height="%d"%s>'
             % (w, h, w, h, extra_attrs)]
    if title:
        lines.append('<title>%s</title>' % title)
    if group_attrs:
        lines.append('<g %s>' % group_attrs)
    for col, d in paths:
        if d:
            lines.append('<path fill="%s" fill-rule="evenodd" d="%s"/>' % (col, d))
    if group_attrs:
        lines.append('</g>')
    lines.append('</svg>')
    return '\n'.join(lines) + '\n'


def trace_file(src, dst, mode='mono', colors=1, fill=None, tol=0.35, scale=None,
               min_area=2.0, key_white=False, blur=0.8, nd=None, relative=True,
               title=None, smooth=0.0, corner_deg=60.0, log=print):
    rgb, alpha = load_rgba(src, key_white)
    h, w = alpha.shape
    if scale is None:
        scale = 2 if max(w, h) <= 1600 else 1
    if nd is None:
        nd = 1 if max(w, h) >= 400 else 2
    layers = build_layers(rgb, alpha, mode, colors, fill, blur)
    paths = trace_layers(layers, (w, h), scale, tol, min_area, nd, relative, corner_deg, smooth, log=log)
    doc = svg_document(paths, w, h, title)
    with open(dst, 'w') as fh:
        fh.write(doc)
    return layers, paths


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('src'); ap.add_argument('dst')
    ap.add_argument('--mode', choices=['mono', 'levels', 'palette'], default='mono')
    ap.add_argument('--colors', type=int, default=1)
    ap.add_argument('--fill', default=None, help='fill colour for mono (default: median ink colour)')
    ap.add_argument('--tol', type=float, default=0.35, help='max fit error in source pixels')
    ap.add_argument('--upscale', type=int, default=None, help='field upscale (default 2 if <=1600px else 1)')
    ap.add_argument('--min-area', type=float, default=2.0, help='drop contours smaller than this (px^2)')
    ap.add_argument('--key-white', action='store_true', help='source is ink on white paper')
    ap.add_argument('--blur', type=float, default=0.8, help='label smoothing sigma (levels/palette)')
    ap.add_argument('--smooth', type=float, default=0.0, help='pre-contour gaussian sigma in source px')
    ap.add_argument('--corner', type=float, default=60.0, help='corner turning angle, degrees')
    ap.add_argument('--decimals', type=int, default=None)
    ap.add_argument('--absolute', action='store_true', help='absolute path commands (M L C)')
    ap.add_argument('--title', default=None)
    a = ap.parse_args(argv)
    trace_file(a.src, a.dst, a.mode, a.colors, a.fill, a.tol, a.upscale, a.min_area,
               a.key_white, a.blur, a.decimals, not a.absolute, a.title, a.smooth, a.corner)
    return 0


if __name__ == '__main__':
    sys.exit(main())
