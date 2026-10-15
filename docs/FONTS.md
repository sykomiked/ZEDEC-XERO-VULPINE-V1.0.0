<!--
Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
SPDX-License-Identifier: Apache-2.0
-->

# ASCW Golden: ZXV's UI and system font

Every ZXV surface draws text in **ASCW Golden**. **ASCW Golden Latin II** is
the primary face, used for English and every other Latin-script language.
Each other script gets the Golden member made for it.
**ASCW Golden Mono** (role `mono` in `fontmap.json`, `ASCW_ROLE_MONO`) is
the fixed-width member for code, paths, JSON, terminals and the bare-metal
shell: Latin, Greek and Cyrillic, box drawing, block elements, arrows and the
key symbols ⌘ ⌥ ⇧ ⌃ ⏎ ⌫ ⌦ ⎋ ⇪. It is chosen by role, not by script, and only
draws ordinary text for symbols no other face has. It is built from Noto Sans
Mono and Noto Sans Symbols (OFL) by `tools/fonts/build_golden_mono.py`.

| Where | How |
|---|---|
| Font files | `assets/fonts/ascw-golden/`: 157 faces, `OFL.txt`, `FONTLOG.md`, `fontmap.json` |
| Kernel and native UI (`zui`) | `kernel/src/font/ascw_golden.h`: face per locale, per script and per code point; `ascw_install()` fills a `font_registry_t` |
| Browser and desktop window (zxvd) | `@font-face` block in `kernel/arch/hosted/zxv_ui.html`; zxvd serves `/fonts/<file>` |
| Mac app (ZXV Studio) | `ASCWFont.swift`: registers the faces from `Contents/Resources/Fonts`; every text style is a CoreText font with the family as its cascade list |
| iPhone (ZXVKit) | The same `ASCWFont.swift`; the app target bundles the folder as `Fonts` |
| Android | `AscwGolden.kt`: a custom fallback chain (Android 10 and later) applied to every Material text style |

The map is generated from the fonts' own character maps by
`tools/fonts/gen_ascw_golden.py` (needs fontTools and gcc). It writes every
copy listed above, plus `docs/FONTS_COVERAGE.md`. Run it again after
changing a font. `test_ascw_golden` (in `make verify-all`) checks the map
against the real files and every CLDR locale.

## How a face is chosen

1. **By language.** `ascw_face_chain(tag, script)` returns the locale's face,
   then Latin II. Han text follows the CLDR script: `zh` and `zh-Hans` use
   Chinese Simplified; `zh-Hant`, `zh-TW`, `zh-HK` and `yue` use Chinese
   Traditional; `ja` uses Japanese; `ko` uses Korean. Persian (`fa`) uses
   Golden Persian. Every other Arabic-script locale uses Golden Arabic.
2. **By character.** For text whose language is unknown,
   `ascw_face_for_codepoint(cp)` names the one face that draws that code
   point by default. Latin II takes digits, punctuation and symbols it has;
   each script's own face takes that script.
3. **Fallback.** The Golden set has no Adlam, N'Ko, Vai, Tifinagh or Osmanya
   member. For those scripts the ASCW sibling faces (ASCW Adlam and the
   others) are used. Anything not covered after that falls to the
   platform's fonts.

## Coverage

These figures are measured against the characters each CLDR locale needs.
The per-locale table is in [FONTS_COVERAGE.md](FONTS_COVERAGE.md).

- Every one of the 778 locales has an ASCW face for its script.
- 744 locales are fully covered by their own face plus Latin II.
- 755 locales are covered by the ASCW set as a whole.

**Historical and minority scripts.** `tools/fonts/build_golden_from_noto.py`
builds 72 faces from the OFL Noto fonts in google/fonts (Khitan Small Script
from its Fontsource package), with the family's name and golden-section
metrics. They cover the scripts of ancient and historical languages (Old
Persian, Ugaritic, Linear A and B, Phoenician, Imperial and Nabataean
Aramaic, Pahlavi, Sogdian, Brahmi, Kharoshthi, Ogham, Tangut, Khitan and
others) and minority scripts CLDR lists (Mongolian, Chakma, Hanifi
Rohingya, Nyiakeng Puachue Hmong, Sunuwar, Deseret, Shavian and others).
Run it, then `gen_ascw_golden.py`.

**Still no face.** Old Uyghur (Ougr): no OFL font for it was reachable from
the build machine. Its text falls to the platform's fonts until a face is
added.

**Partial coverage.**

- The four CJK faces are subsets. ASCW Golden Japanese and Korean lack
  some full-width punctuation (U+FF03 # and similar). Chinese Traditional
  lacks about 60 of the characters CLDR lists for zh-Hant. The browser and
  the platforms fill these from system fonts.
- Yi lacks CJK punctuation.

## Licence: what this means for shipping

The fonts are **not** under Apache-2.0. Each face is a renamed, modified
build of an upstream font under the **SIL Open Font License 1.1**:

- Noto (Google)
- Source Han Sans (Adobe)
- Amiri
- Crimson Text

The OFL allows commercial use, embedding and bundling with software that
is sold. It has four conditions, and this repository meets each one:

- **The licence and copyright notices travel with the fonts.** `OFL.txt`
  and `FONTLOG.md` sit in the font folder. Every package copies them with
  the faces: the Mac app's `Resources/Fonts`, the Linux and Windows
  packages' `fonts/`, and the APK's `fonts/`.
- **The fonts are not sold on their own.** Selling ZXV with the fonts
  bundled is allowed. Selling the font files by themselves is not.
- **A modified font may not use a Reserved Font Name.** Adobe reserves
  "Source", and no ASCW name uses it. Noto, Amiri and Crimson declare no
  reserved name.
- **Modified versions stay under the OFL.** The fonts cannot be relicensed
  under Apache. `NOTICE` and `REUSE.toml` record the split.

**Two faces from the source folder were left out:**

- **ASCW Golden Cherokee** in the source folder is Plantagenet Cherokee
  (Tiro Typeworks, "All rights reserved"). It is licensed with Windows and
  may not be redistributed. The Cherokee face here was rebuilt from Noto
  Sans Cherokee (OFL), renamed, and given the family's metrics.
- **ASCW Golden Enochian** is a Fontello build by "Glumbosch" with no
  licence stated. It stays out until its author grants terms. Enochian
  text keeps the existing PUA routing (`SCRIPT_ENOCHIAN`) with no face.

## Known limits

- **One weight.** The family has Regular only. Semibold and bold requests
  draw Regular on the Mac and iPhone. The browser synthesises bold.
- **Golden-section metrics.** Most faces declare a 0.618 em ascender and a
  0.382 em descender. The Arabic, CJK, Myanmar and hieroglyph faces draw
  above that, so give those scripts a line height of 1.4 or more, and do
  not clip text to the font's own ascender.
- **The kernel rasteriser can't read some faces.** `truetype.c` reads 146
  of the 152 TrueType faces; the rest need cmap format 12 or other tables it
  does not read. The five `.otf` faces are CFF outlines, which it does not
  rasterise. Host platforms draw all of them.
- **Itemization has gaps.** `script.c`'s enum still names only 27 scripts.
  The per-code-point table covers the rest until the enum grows.
- **The Flutter phone companion is not on main yet.** When it lands, add to
  its `pubspec.yaml` a `fonts:` entry with family `ASCW Golden` and asset
  `assets/fonts/ascw-golden/ASCW-Golden-Latin2.ttf`. Set
  `fontFamilyFallback` from `fontmap.json`'s `scripts` map.
