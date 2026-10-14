# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# ZXV launch catalogue: one launch theme per app in the catalogue (407 apps in three tiers), every one with its own sound
# sequence in the same begin / middle / end pattern (owner, 2026-10-12 10:54Z and 11:00Z).
#
#   python3 tools/launch/launch_catalogue.py table  <APP_CATALOG_407.csv>   # writes kernel/src/launch/zxl_apps.inc
#   python3 tools/launch/launch_catalogue.py render <APP_CATALOG_407.csv> <out> [--new] [app ...]
#
# How each app gets its own sound, deterministically:
#   group     timbre and the faint motion behind the tiles (the family resemblance)
#   key       the 11 keys that are not the boot's E, a different one per app in a family (stepping round
#             a hand-made app's), then an octave away; only a family of more than 44 apps shares a scale,
#             and then never a phrase
#   motif     one of 12 begin/end phrase shapes; chosen so no two of the 144 apps share key, octave,
#             timbre and motif
# The six hand-made apps (Burrow, Flint, Commons, Cutwater, Murmuration, Coffer) keep their entries and
# their own recipes in launch_sounds.py; every other app's three cues come from launch_sounds.generic().
#
# render picks up each app's real icon from ZXV_APP_ICONS (NNN-name.png) as icons are imported; --new
# re-renders only apps whose icon is newer than their video, so it can run after every import.
import csv, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
INC = os.path.join(HERE, '../../kernel/src/launch/zxl_apps.inc')

PC = dict(C=0, Db=1, D=2, Eb=3, E=4, F=5, Fs=6, G=7, Ab=8, A=9, Bb=10, B=11)
CNAME = {v: k.upper() for k, v in PC.items()}
CNAME[6] = 'FS'
KEYS = [0, 1, 2, 3, 5, 6, 7, 8, 9, 10, 11]                  # every key but E (the boot's)
# group defaults, the same as GROUPS[] in zxl_launch.c: key, octave, timbre, ambient
GROUPS = [('D', 4, 'MARIMBA', 'ARCHES'), ('A', 4, 'CHIP', 'SPARKS'), ('Fs', 5, 'PLUCK', 'RINGS'),
          ('Bb', 5, 'GLASS', 'DUST'), ('C', 5, 'STRINGS', 'WATER'), ('B', 5, 'FLUTE', 'FLOCK'),
          ('G', 5, 'COIN', 'DUST'), ('F', 5, 'PLUCK', 'RINGS'), ('Eb', 4, 'MARIMBA', 'SPARKS'),
          ('Ab', 5, 'GLASS', 'DUST'), ('Db', 4, 'CHIP', 'SPARKS'), ('F', 5, 'GLASS', 'RINGS')]
# the six hand-made apps: key, octave, timbre, ambient (zxl_launch.c's first entries)
HAND = {'Burrow': ('D', 4, 'MARIMBA', 'ARCHES'), 'Flint': ('A', 4, 'CHIP', 'SPARKS'),
        'Commons': ('F', 5, 'PLUCK', 'RINGS'), 'Cutwater': ('C', 5, 'STRINGS', 'WATER'),
        'Murmuration': ('E', 5, 'FLUTE', 'FLOCK'), 'Coffer': ('G', 5, 'COIN', 'DUST')}
MOTIFS = 12


# Tiers 2 and 3 of the 407-app catalogue sort into the twelve families by what they do (owner 2026-10-14:
# 407 apps in three tiers); tier 1's categories are the twelve groups themselves.
FAMILY = {1: 'System Computing Records', 2: 'Security', 3: 'Network Transport Logistics Travel Marine',
          4: 'Writing Productivity Legal Government Civic', 5: 'Media Creative Design Craft Culture Events',
          6: 'AI Research Science', 7: 'Finance Commerce',
          8: 'Home Land Geography Agriculture Food Environment Energy Community',
          9: 'Learning Family Fitness', 10: 'Industry', 11: 'Developer', 12: 'Health Safety Access Language'}
FAMILY_OF = {c: g for g, cs in FAMILY.items() for c in cs.split()}


def apps(csv_path):
    rows = list(csv.DictReader(open(csv_path)))
    col = 'group' if 'group' in rows[0] else 'category'
    names = []
    for r in rows:
        if r.get('layer', 'L1') == 'L1' and r[col] not in names: names.append(r[col])
    for r in rows:
        r['n'] = int(r['number'])
        if r.get('layer', 'L1') == 'L1': r['g'] = names.index(r[col]) + 1
        elif r[col] in FAMILY_OF: r['g'] = FAMILY_OF[r[col]]
        else: sys.exit(f"launch_catalogue: no family for category {r[col]!r} ({r['app']}); add it to FAMILY")
    return rows


def themes(rows):
    """[(row, key pc, octave, timbre, ambient, motif)] for all apps, every sound tuple distinct"""
    used, out, slot = set(), [], {}
    for r in rows:                                     # hand-made first, so the rest step round them
        if r['app'] in HAND:
            k, o, tb, am = HAND[r['app']]; t = (PC[k], o, tb, 0); used.add(t); r['_t'] = (PC[k], o, tb, am, 0)
    taken = {}                                         # (group) -> {(key, octave)} already sounding there
    for r in rows:
        if r['app'] in HAND: k, o, _, _ = HAND[r['app']]; taken.setdefault(r['g'], set()).add((PC[k], o))
    for r in rows:
        if r['app'] in HAND: continue
        g = r['g']; gk, go, tb, am = GROUPS[g - 1]; tk = taken.setdefault(g, set())
        start = KEYS.index(PC[gk]); alt = go + (1 if go < 5 else -1)
        octs = (go, alt) + tuple(oc for oc in (3, 4, 5, 6) if oc not in (go, alt))   # a big family spreads out
        free = [(KEYS[(start + j) % len(KEYS)], oc) for oc in octs for j in range(len(KEYS))
                if (KEYS[(start + j) % len(KEYS)], oc) not in tk]
        # a family bigger than its 44 scales shares a scale, never a phrase (the motif below differs)
        over = slot.get(g, 0); slot[g] = over + (0 if free else 1)
        k, o = free[0] if free else (KEYS[(start + over) % len(KEYS)], octs[(over // len(KEYS)) % len(octs)])
        tk.add((k, o))
        m = (r['n'] * 5) % MOTIFS
        for _ in range(MOTIFS):
            if (k, o, tb, m) not in used: break
            m = (m + 1) % MOTIFS
        else: sys.exit(f"launch_catalogue: no free phrase for {r['app']}")
        used.add((k, o, tb, m)); r['_t'] = (k, o, tb, am, m)
    for r in rows: out.append((r,) + r['_t'])
    return out


def table(csv_path):
    rows = apps(csv_path); w = max(len(r['app']) for r in rows) + 3
    L = ['/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */',
         '/* SPDX-License-Identifier: Apache-2.0 */',
         '/* zxl_apps.inc: every app in the catalogue, its own launch theme. Generated by',
         ' * tools/launch/launch_catalogue.py from APP_CATALOG_407.csv; do not edit by hand. */',
         '/* clang-format off */']
    for r, k, o, tb, am, m in themes(rows):
        nm = ('"%s",' % r['app']).ljust(w)
        L.append('    { %s %2d, %-7s %d, %-19s ZXL_REVEAL_VAULT, %-15s %2d }, /* %3d */'
                 % (nm, r['g'], 'ZXL_%s,' % CNAME[k], o, 'ZXL_TIMBRE_%s,' % tb, 'ZXL_AMB_%s,' % am, m, r['n']))
    L.append('/* clang-format on */')
    open(INC, 'w').write('\n'.join(L) + '\n'); print('wrote', os.path.relpath(INC), len(rows), 'apps')


def slug(s): return re.sub(r'[^a-z0-9]', '', s.lower())


def render(csv_path, out, only_new, pick):
    import launch_mock as LM, launch_sounds as LS
    rows = apps(csv_path); os.makedirs(out + '/stills', exist_ok=True)
    for r, k, o, tb, am, m in themes(rows):
        key = slug(r['app'])
        if pick and key not in pick: continue
        icon = f"{LM.ICONS}/{r['n']:03d}-{key}.png"; vid = f'{out}/{key}-launch.mp4'
        if only_new and os.path.exists(vid) and (not os.path.exists(icon) or os.path.getmtime(icon) < os.path.getmtime(vid)):
            continue
        if key not in LM.THEMES:
            LM.THEMES[key] = LM.generic_theme(r, am.lower())
            LS.RECIPES[key] = (lambda k=k, o=o, tb=tb, m=m: LS.generic(k, o, tb.lower(), m))
        LM.run(key, out)


if __name__ == '__main__':
    sys.path.insert(0, HERE)
    cmd, src = sys.argv[1], sys.argv[2]
    if cmd == 'table': table(src)
    else:
        rest = sys.argv[4:]; new = '--new' in rest
        render(src, sys.argv[3], new, [a for a in rest if a != '--new'])
