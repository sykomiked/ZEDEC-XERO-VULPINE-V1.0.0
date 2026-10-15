#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Build the ASCW Golden members of scripts the ASCW source folder has no face
for, from the OFL Noto fonts in google/fonts.

    python3 tools/fonts/build_golden_from_noto.py [--cache DIR] [NAME ...]

For each member in MEMBERS (or only the NAMEs given), the regular font named
in google/fonts' METADATA.pb (or, for a font google/fonts does not carry, the
script subset of its Fontsource npm package) is downloaded (into ~/.cache/zxv-noto unless
--cache), a variable font is fixed at weight 400, the family is renamed into
ASCW Golden (the OFL's Reserved Font Name clause: no "Noto" in the new name),
the family's golden-section metrics are set (ascender 0.618 em, descender
0.382 em, no line gap), and the result is written to
assets/fonts/ascw-golden/ASCW-Golden-<Name>.ttf. Run
tools/fonts/gen_ascw_golden.py afterwards. The fonts stay under the OFL.
"""
import argparse
import io
import json
import os
import re
import sys
import tarfile
import urllib.request

from fontTools.ttLib import TTFont
from fontTools.varLib.instancer import instantiateVariableFont

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
FONTS = os.path.join(ROOT, "assets", "fonts", "ascw-golden")
RAW = "https://raw.githubusercontent.com/google/fonts/main/ofl/"

# ASCW name (file and family), google/fonts folder, ISO 15924 scripts
MEMBERS = [
    ("OldPersian", "notosansoldpersian", ["Xpeo"]),
    ("Ugaritic", "notosansugaritic", ["Ugar"]),
    ("LinearB", "notosanslinearb", ["Linb"]),
    ("LinearA", "notosanslineara", ["Lina"]),
    ("Phoenician", "notosansphoenician", ["Phnx"]),
    ("Ogham", "notosansogham", ["Ogam"]),
    ("Brahmi", "notosansbrahmi", ["Brah"]),
    ("Kharoshthi", "notosanskharoshthi", ["Khar"]),
    ("OldSouthArabian", "notosansoldsoutharabian", ["Sarb"]),
    ("OldNorthArabian", "notosansoldnortharabian", ["Narb"]),
    ("Samaritan", "notosanssamaritan", ["Samr"]),
    ("Nabataean", "notosansnabataean", ["Nbat"]),
    ("Palmyrene", "notosanspalmyrene", ["Palm"]),
    ("InscriptionalPahlavi", "notosansinscriptionalpahlavi", ["Phli"]),
    ("PsalterPahlavi", "notosanspsalterpahlavi", ["Phlp"]),
    ("InscriptionalParthian", "notosansinscriptionalparthian", ["Prti"]),
    ("Sogdian", "notosanssogdian", ["Sogd"]),
    ("OldSogdian", "notosansoldsogdian", ["Sogo"]),
    ("Lycian", "notosanslycian", ["Lyci"]),
    ("Lydian", "notosanslydian", ["Lydi"]),
    ("Cypriot", "notosanscypriot", ["Cprt"]),
    ("CyproMinoan", "notosanscyprominoan", ["Cpmn"]),
    ("Elbasan", "notosanselbasan", ["Elba"]),
    ("Manichaean", "notosansmanichaean", ["Mani"]),
    ("Chorasmian", "notosanschorasmian", ["Chrs"]),
    ("Elymaic", "notosanselymaic", ["Elym"]),
    ("Hatran", "notosanshatran", ["Hatr"]),
    ("Mahajani", "notosansmahajani", ["Mahj"]),
    ("Sharada", "notosanssharada", ["Shrd"]),
    ("Siddham", "notosanssiddham", ["Sidd"]),
    ("Tirhuta", "notosanstirhuta", ["Tirh"]),
    ("Grantha", "notosansgrantha", ["Gran"]),
    ("Ahom", "notoserifahom", ["Ahom"]),
    ("Dogra", "notoserifdogra", ["Dogr"]),
    ("Modi", "notosansmodi", ["Modi"]),
    ("Takri", "notosanstakri", ["Takr"]),
    ("Khojki", "notosanskhojki", ["Khoj"]),
    ("Khudawadi", "notosanskhudawadi", ["Sind"]),
    ("Tangut", "notoseriftangut", ["Tang"]),
    ("Duployan", "notosansduployan", ["Dupl"]),
    ("Mro", "notosansmro", ["Mroo"]),
    ("PauCinHau", "notosanspaucinhau", ["Pauc"]),
    ("Yezidi", "notoserifyezidi", ["Yezi"]),
    ("Vithkuqi", "notoserifvithkuqi", ["Vith"]),
    ("Kawi", "notosanskawi", ["Kawi"]),
    ("Nandinagari", "notosansnandinagari", ["Nand"]),
    ("ZanabazarSquare", "notosanszanabazarsquare", ["Zanb"]),
    ("Soyombo", "notosanssoyombo", ["Soyo"]),
    ("Mongolian", "notosansmongolian", ["Mong"]),
    ("PhagsPa", "notosansphagspa", ["Phag"]),
    ("Marchen", "notosansmarchen", ["Marc"]),
    ("Bhaiksuki", "notosansbhaiksuki", ["Bhks"]),
    ("DivesAkuru", "notoserifdivesakuru", ["Diak"]),
    ("Toto", "notoseriftoto", ["Toto"]),
    ("OldPermic", "notosansoldpermic", ["Perm"]),
    ("CaucasianAlbanian", "notosanscaucasianalbanian", ["Aghb"]),
    ("Medefaidrin", "notosansmedefaidrin", ["Medf"]),
    ("Makasar", "notoserifmakasar", ["Maka"]),
    ("SignWriting", "notosanssignwriting", ["Sgnw"]),
    ("NyiakengPuachueHmong", "notoserifnphmong", ["Hmnp"]),
    ("HanifiRohingya", "notosanshanifirohingya", ["Rohg"]),
    ("Chakma", "notosanschakma", ["Cakm"]),
    ("Deseret", "notosansdeseret", ["Dsrt"]),
    ("Shavian", "notosansshavian", ["Shaw"]),
    ("Saurashtra", "notosanssaurashtra", ["Saur"]),
    ("Rejang", "notosansrejang", ["Rjng"]),
    ("Kaithi", "notosanskaithi", ["Kthi"]),
    ("KayahLi", "notosanskayahli", ["Kali"]),
    ("Tangsa", "notosanstangsa", ["Tnsa"]),
    ("Sunuwar", "notosanssunuwar", ["Sunu"]),
    # not in google/fonts: the script subset of the Fontsource npm package (OFL)
    ("KhitanSmallScript", "npm:noto-serif-khitan-small-script:khitan-small-script", ["Kits"]),
]


def regular_file(meta):
    blocks = re.findall(r"fonts \{(.*?)\}", meta, re.S)
    for b in blocks:
        if 'style: "normal"' in b:
            return re.search(r'filename: "([^"]+)"', b).group(1), re.search(r'name: "([^"]+)"', b).group(1)
    sys.exit("no regular font in METADATA.pb")


def fetch(url, path):
    if not os.path.exists(path):
        print("fetching", url, file=sys.stderr)
        with open(path, "wb") as f:
            f.write(urllib.request.urlopen(url).read())
    return path


def from_npm(spec, d):
    """npm:<package>:<subset>: the regular WOFF of one subset of an
    @fontsource package, as a TrueType font."""
    _, pkg, subset = spec.split(":")
    info = json.load(urllib.request.urlopen("https://registry.npmjs.org/@fontsource/%s/latest" % pkg))
    tgz = fetch(info["dist"]["tarball"], os.path.join(d, "%s-%s.tgz" % (pkg, info["version"])))
    with tarfile.open(tgz) as t:
        m = t.extractfile("package/files/%s-%s-400-normal.woff" % (pkg, subset))
        f = TTFont(io.BytesIO(m.read()))
    f.flavor = None
    upstream = " ".join(w.capitalize() for w in pkg.split("-"))
    return f, upstream


def build(name, folder, cache):
    d = os.path.join(cache, folder.replace(":", "_"))
    os.makedirs(d, exist_ok=True)
    if folder.startswith("npm:"):
        f, upstream = from_npm(folder, d)
    else:
        meta = open(fetch(RAW + folder + "/METADATA.pb", os.path.join(d, "METADATA.pb")),
                    encoding="utf-8").read()
        fn, upstream = regular_file(meta)
        f = TTFont(fetch(RAW + folder + "/" + urllib.request.quote(fn), os.path.join(d, fn)))
    if "fvar" in f:
        axes = {a.axisTag: (400 if a.axisTag == "wght" else a.defaultValue) for a in f["fvar"].axes}
        f = instantiateVariableFont(f, axes)
    fam = "ASCW Golden " + re.sub(r"(?<=[a-z])(?=[A-Z])", " ", name)
    ps = "ASCWGolden" + name
    n = f["name"]
    for rec in list(n.names):
        if rec.nameID in (1, 2, 3, 4, 6, 16, 17, 21, 22, 25):
            n.removeNames(nameID=rec.nameID)
    n.setName(fam, 1, 3, 1, 0x409)
    n.setName("Regular", 2, 3, 1, 0x409)
    n.setName("ASCW;%s;2026; derived from %s" % (ps, upstream), 3, 3, 1, 0x409)
    n.setName(fam, 4, 3, 1, 0x409)
    n.setName(ps + "-Regular", 6, 3, 1, 0x409)
    upm = f["head"].unitsPerEm
    asc, desc = round(upm * 0.618), round(upm * 0.382)
    f["hhea"].ascent, f["hhea"].descent, f["hhea"].lineGap = asc, -desc, 0
    o = f["OS/2"]
    o.sTypoAscender, o.sTypoDescender, o.sTypoLineGap = asc, -desc, 0
    o.usWinAscent, o.usWinDescent = round(upm * 0.679), round(upm * 0.420)
    f.recalcTimestamp = False
    out = os.path.join(FONTS, "ASCW-Golden-%s.ttf" % name)
    f.save(out)
    print("%-24s %-32s %6d code points" % (name, upstream, len(f.getBestCmap() or {})))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cache", default=os.path.expanduser("~/.cache/zxv-noto"))
    ap.add_argument("names", nargs="*")
    a = ap.parse_args()
    for name, folder, _ in MEMBERS:
        if not a.names or name in a.names:
            build(name, folder, a.cache)


if __name__ == "__main__":
    main()
