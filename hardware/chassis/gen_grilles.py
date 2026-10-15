#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2024-2026 Michael Laurence Curzi & 36N9 GENETICS LLC
# SPDX-License-Identifier: LicenseRef-ZXV-Hardware-1.1
"""gen_grilles.py: the honeycomb window drawings for every ZXV device.

Writes one SVG per device into hardware/chassis/grilles/, in millimetres
(1 SVG user unit = 1 mm), from the numbers in
hardware/docs/oem/HARDENING.md section 6: a clear window behind a
hexagonal grille in forest night #002911, with the emblem keep-out at the
centre where the ILLUMAHEART FOUNDATION badge sits (section 6.5).

Each drawing shows: the device outline, the window, the grille (as a
repeating hexagonal hole pattern of the given across-flats size and web
width), the emblem keep-out, and a title block with the dimensions. The
cells under the emblem are removed, the way a car's grille gives way to
its badge. Run from anywhere:

    python3 hardware/chassis/gen_grilles.py

Every dimension here is a drawing value for the industrial-design and
tooling vendors to confirm, not a toleranced production dimension.
"""
import math
import os

EXPLICIT_MAX = 20000  # cells drawn as real geometry up to this count
GRILLE = '#002911'   # forest night: the grille metal (HARDENING 6.5)
GLASS = '#dfeee8'    # the clear window, seen through the cells
LEAF = '#7bd43a'     # the emblem's backlit leaf green (badge outline only)
FRAME = '#6b7570'    # frame metal (titanium / aluminium / magnesium)

# name, title, outline W x H, corner radius, window inset, grille cell
# across flats, web width, emblem width, emblem keep-out margin
DEVICES = [
    ('signet_back', 'Signet (phone) back', 82, 172, 10, 5, 4.0, 0.8, 22, 1.5),
    ('folio_back', 'Folio (tablet) back', 185, 262, 12, 8, 5.0, 1.0, 34, 2.0),
    ('courier_lid', 'Courier (laptop) lid', 330, 240, 8, 10, 5.0, 1.0, 48, 2.5),
    ('briefcase_window', 'Briefcase vault window', 300, 180, 6, 8, 5.0, 1.0, 60, 3.0),
    ('hearth_door', 'Hearth (S pedestal) door', 380, 560, 6, 15, 5.0, 1.2, 80, 4.0),
    ('m_shell_door', 'M rack shell (24U) door', 700, 1250, 8, 30, 5.0, 1.5, 160, 6.0),
    ('l_shell_door', 'L rack shell (42U) door', 700, 2100, 8, 30, 5.0, 1.5, 160, 6.0),
]


def hex_points(cx, cy, flats):
    """A pointy-top hexagon of the given across-flats width, centred."""
    r = flats / math.sqrt(3)  # centre to vertex
    return ' '.join('%.3f,%.3f' % (cx + r * math.cos(math.radians(a)),
                                   cy + r * math.sin(math.radians(a)))
                    for a in (30, 90, 150, 210, 270, 330))


def svg(name, title, w, h, rad, inset, flats, web, emblem, margin):
    pitch_x = flats                      # centre-to-centre along a row
    pitch_y = flats * math.sqrt(3) / 2   # row to row
    hole = flats - web                   # the open cell, across flats
    ww, wh = w - 2 * inset, h - 2 * inset
    wr = max(rad - inset, 1.0)
    keep = emblem / 2 + margin           # emblem keep-out radius
    pad = max((w + 20) / 62, 1.6) * 1.35 * 5 + 8  # room for the title block
    out = []
    out.append('<svg xmlns="http://www.w3.org/2000/svg" width="%gmm" height="%gmm" '
               'viewBox="0 0 %g %g">' % (w + 20, h + 20 + pad, w + 20, h + 20 + pad))
    out.append('<title>%s: honeycomb window and emblem (ZXV)</title>' % title)
    out.append('<defs>')
    # one tile of the honeycomb: two holes, offset half a pitch every row
    out.append('<pattern id="cells" patternUnits="userSpaceOnUse" x="%.3f" y="%.3f" '
               'width="%.3f" height="%.3f">' % (10 + inset, 10 + inset, pitch_x, 2 * pitch_y))
    out.append('<rect width="%.3f" height="%.3f" fill="%s"/>' % (pitch_x, 2 * pitch_y, GRILLE))
    for cx, cy in ((0, 0), (pitch_x, 0), (pitch_x / 2, pitch_y), (0, 2 * pitch_y),
                   (pitch_x, 2 * pitch_y)):
        out.append('<polygon points="%s" fill="%s"/>' % (hex_points(cx, cy, hole), GLASS))
    out.append('</pattern>')
    out.append('<clipPath id="win"><rect x="%g" y="%g" width="%g" height="%g" rx="%g"/>'
               '</clipPath>' % (10 + inset, 10 + inset, ww, wh, wr))
    out.append('</defs>')
    cx, cy = 10 + w / 2, 10 + h / 2
    # frame, window grille, emblem keep-out (solid relief), badge outline
    out.append('<rect x="10" y="10" width="%g" height="%g" rx="%g" fill="%s"/>' % (w, h, rad, FRAME))
    ncells = (ww / pitch_x + 2) * (wh / pitch_y + 2)
    if ncells <= EXPLICIT_MAX:
        # every cell as real geometry, one path: no tile seams for tooling
        d = []
        rr = hole / math.sqrt(3)
        row = 0
        y = 10 + inset
        while y <= 10 + inset + wh + pitch_y:
            x = 10 + inset + (pitch_x / 2 if row % 2 else 0)
            while x <= 10 + inset + ww + pitch_x:
                pts = [(x + rr * math.cos(math.radians(a)), y + rr * math.sin(math.radians(a)))
                       for a in (30, 90, 150, 210, 270, 330)]
                d.append('M%.2f %.2f' % pts[0] + ''.join('L%.2f %.2f' % q for q in pts[1:]) + 'Z')
                x += pitch_x
            y += pitch_y
            row += 1
        out.append('<g clip-path="url(#win)"><rect x="%g" y="%g" width="%g" height="%g" '
                   'fill="%s"/><path fill="%s" d="%s"/></g>'
                   % (10 + inset, 10 + inset, ww, wh, GRILLE, GLASS, ''.join(d)))
    else:
        # very large doors: a repeating tile (viewers may show faint seams)
        out.append('<g clip-path="url(#win)"><rect x="%g" y="%g" width="%g" height="%g" '
                   'fill="url(#cells)"/></g>' % (10 + inset, 10 + inset, ww, wh))
    out.append('<circle cx="%g" cy="%g" r="%g" fill="%s"/>' % (cx, cy, keep, GRILLE))
    out.append('<circle cx="%g" cy="%g" r="%g" fill="none" stroke="%s" stroke-width="%g" '
               'stroke-dasharray="%g %g"/>' % (cx, cy, emblem / 2, LEAF, max(emblem / 60, 0.3),
                                               emblem / 20, emblem / 40))
    fs = max(min(w, h) / 40, 1.6)
    out.append('<text x="%g" y="%g" font-family="sans-serif" font-size="%g" fill="%s" '
               'text-anchor="middle">ILLUMAHEART emblem</text>' % (cx, cy - fs * 0.2, fs * 0.8, LEAF))
    out.append('<text x="%g" y="%g" font-family="sans-serif" font-size="%g" fill="%s" '
               'text-anchor="middle">%g mm, backlit</text>' % (cx, cy + fs, fs * 0.8, LEAF, emblem))
    # title block
    ty = h + 24
    tfs = max((w + 20) / 62, 1.6)
    lines = [
        '%s' % title,
        'honeycomb window, standard ZXV design (HARDENING.md 6)',
        'outline %g x %g mm, R%g; window inset %g mm' % (w, h, rad, inset),
        'grille %s: hex %g mm across flats, web %g mm, open %g mm' % (GRILLE, flats, web, hole),
        'emblem %g mm, keep-out %g mm, backlit' % (emblem, 2 * keep),
    ]
    for i, t in enumerate(lines):
        out.append('<text x="10" y="%g" font-family="sans-serif" font-size="%g" fill="#1d2b25">'
                   '%s</text>' % (ty + i * tfs * 1.35, tfs, t))
    out.append('</svg>')
    return '\n'.join(out) + '\n'


def main():
    here = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'grilles')
    os.makedirs(here, exist_ok=True)
    for d in DEVICES:
        with open(os.path.join(here, d[0] + '.svg'), 'w', encoding='utf-8') as f:
            f.write(svg(*d))
        print('wrote grilles/%s.svg' % d[0])


if __name__ == '__main__':
    main()
