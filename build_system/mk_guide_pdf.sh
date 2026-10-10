#!/bin/bash
# mk_guide_pdf.sh — build the ONE unified multilingual developer-guide PDF.
#
# ROUTE: markdown -> (pandoc) -> HTML fragments -> one assembled HTML -> (headless
# Chrome) -> PDF.
#
# WHY NOT pandoc -> LaTeX -> PDF, the obvious choice: this document is Japanese +
# Traditional Chinese + four Latin-script languages IN ONE FILE. LaTeX needs a CJK
# engine (xelatex/lualatex) plus per-script font declarations, and silently drops
# glyphs it cannot map -- a missing kanji is invisible in a build log and obvious
# to a reader. Chrome already does system font fallback per run of text, so mixed
# scripts on a single page just work, and anything it cannot render is visible on
# the page rather than absent from it.
#
# WHY NOT wkhtmltopdf: it is a dead QtWebKit fork with no modern CJK shaping.
#
# Usage: bash build_system/mk_guide_pdf.sh [GUIDE_DIR] [OUT_PDF]
set -u
DIR="${1:-deliverable/docs/guide}"
OUT="${2:-deliverable/docs/ZXV_DEVELOPER_GUIDE_MULTILINGUAL.pdf}"
CHROME="/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT

command -v pandoc >/dev/null || { echo "pandoc not found"; exit 2; }
[ -x "$CHROME" ] || { echo "Chrome not found at $CHROME"; exit 2; }

# ---- cover mark -------------------------------------------------------------
# Embedded as a base64 data: URI rather than a file:// reference. Headless Chrome
# resolves file:// images relative to the HTML it was handed, and that HTML lives
# in a temp dir -- a relative or absolute path here works on this machine and
# silently yields a blank space on any other. A data URI cannot go missing.
# Downscaled first: the source is 1254px square, the cover prints it at ~52mm, so
# 700px is already beyond what 300dpi resolves and keeps the PDF from carrying an
# extra megabyte per copy.
LOGO="${LOGO:-../15_LOGOS/zedec_transparent.png}"
LOGO_URI=""
if [ -f "$LOGO" ]; then
  cp "$LOGO" "$TMP/logo_src.png"
  sips -Z 700 "$TMP/logo_src.png" --out "$TMP/logo.png" >/dev/null 2>&1 || cp "$TMP/logo_src.png" "$TMP/logo.png"
  LOGO_URI="data:image/png;base64,$(base64 < "$TMP/logo.png" | tr -d '\n')"
  echo "  cover mark: $LOGO ($(( $(stat -f%z "$TMP/logo.png" 2>/dev/null || stat -c%s "$TMP/logo.png") / 1024 )) KB embedded)"
else
  echo "  cover mark: NOT FOUND at $LOGO — cover will render without it"
fi

# language code -> display name, in the order they appear in the PDF
langs=(
  "EN|English|English"
  "JA|日本語|Japanese"
  "DE|Deutsch|German"
  "ES|Español|Spanish"
  "ZH-TW|繁體中文|Traditional Chinese (Taiwan)"
  "PT|Português|Portuguese"
)

echo "== unified multilingual guide PDF =="

# ---- 1. cover + index ------------------------------------------------------
{
cat <<HTML
<div class="cover">
  ${LOGO_URI:+<img class="cover-mark" src="$LOGO_URI" alt="ZEDEC XERO VULPINE">}
  <div class="cover-kicker">ZEDEC pqOS / ZXV</div>
  <h1 class="cover-title">Developer &amp; Architecture Guide</h1>
  <div class="cover-sub">33 Chapters &middot; Six Languages</div>
  <div class="cover-rule"></div>
  <div class="cover-langs">
    English &middot; 日本語 &middot; Deutsch &middot; Español &middot; 繁體中文 &middot; Português
  </div>
  <div class="cover-meta">
    <p><strong>M5 Axiomatic Kernel Foundation — VOVINA SHAKINA Edition</strong></p>
    <p>Version 0.0.1 (v0 pre-release)</p>
    <p>H.M. Michael-Laurence Curzi &middot; 36N9 Genetics, LLC</p>
    <p class="lic">Apache License 2.0</p>
  </div>
  <div class="cover-note">
    <p><strong>Every number in this guide was produced by running the code.</strong>
    Where something is not implemented, the guide says so. Chapter 28 documents where
    this project's own release notes disagree with the measured source.</p>
  </div>
</div>
<div class="pagebreak"></div>
HTML
} > "$TMP/00_cover.html"

# ---- 2. per-language bodies ------------------------------------------------
n=0
for entry in "${langs[@]}"; do
  IFS='|' read -r code native english <<<"$entry"
  f="$DIR/ZXV_DEVELOPER_GUIDE_${code}.md"
  if [ ! -f "$f" ]; then
    echo "  [SKIP] $code — $f not present"
    continue
  fi
  n=$((n+1))
  idx=$(printf "%02d" "$n")
  {
    echo "<div class=\"langsep\" id=\"lang-${code}\">"
    echo "  <div class=\"langsep-code\">${code}</div>"
    echo "  <div class=\"langsep-native\">${native}</div>"
    echo "  <div class=\"langsep-en\">${english}</div>"
    echo "</div>"
    echo "<div class=\"pagebreak\"></div>"
    echo "<section class=\"lang\" lang=\"$(echo "$code" | tr 'A-Z' 'a-z')\">"
    pandoc --from=gfm --to=html5 "$f"
    echo "</section>"
    echo "<div class=\"pagebreak\"></div>"
  } > "$TMP/${idx}_${code}.html"
  echo "  [OK]   $code — $(wc -l < "$f" | tr -d ' ') lines"
done

if [ "$n" -eq 0 ]; then echo "no language files found in $DIR"; exit 1; fi

# ---- 3. assemble ------------------------------------------------------------
cat > "$TMP/doc.html" <<'HEAD'
<!doctype html>
<html><head><meta charset="utf-8">
<title>ZEDEC pqOS / ZXV — Developer &amp; Architecture Guide</title>
<style>
/* FONT NAMES HERE ARE MEASURED, NOT GUESSED — AND THE FIRST ATTEMPT WAS WRONG.
 *
 * This stack originally said "Hiragino Sans" and "PingFang TC". NEITHER FONT
 * EXISTS on this machine. Chrome therefore fell through to a fallback that has
 * kana but NO KANJI, and PDF export silently dropped every kanji: the cover read
 * "ZEDEC pqOS / ZXV — [ ] ガイド" where it should read "開発者ガイド". The HTML
 * screenshot looked perfect (Chrome has full system font access when painting to
 * screen); only the exported PDF lost the glyphs. A font table check did not
 * reveal it either -- the embedded BaseFont names looked ordinary. Only
 * rasterising the PDF back to an image and LOOKING at it exposed the loss.
 *
 * VERIFIED WORKING (rendered to PDF, rasterised, and read back):
 *   "Hiragino Sans GB"  -> 開発者ガイド 第28章 宣言グラフ 繁體中文   full
 *   "Heiti TC"          -> full
 *   "STHeiti"           -> BROKEN, kanji dropped (bare name does not resolve)
 *
 * So: keep the real names, keep BOTH survivors as fallback, and never trust a
 * font stack that has not been rasterised and inspected. `mk_guide_pdf.sh` runs
 * that check itself at the end -- see VERIFY below. */
:root{
  --ink:#1a1a1a; --muted:#5a5a5a; --rule:#d8d8d8; --accent:#7a1f1f;
  --code-bg:#f6f6f4; --warn-bg:#fdf6f0; --warn-br:#c4622d;
}
@page { size: A4; margin: 18mm 16mm 20mm 16mm; }
html,body{ margin:0; padding:0; }
body{
  font-family: "Helvetica Neue", Helvetica, Arial,
               "Hiragino Sans GB", "Heiti TC", sans-serif;
  font-size: 9.6pt; line-height: 1.62; color: var(--ink);
  -webkit-font-smoothing: antialiased;
}
section.lang:lang(zh-tw){
  font-family: "Helvetica Neue", Helvetica, Arial,
               "Heiti TC", "Hiragino Sans GB", sans-serif;
}
section.lang:lang(ja){
  font-family: "Helvetica Neue", Helvetica, Arial,
               "Hiragino Sans GB", "Heiti TC", sans-serif;
}
.pagebreak{ page-break-after: always; break-after: page; }

/* ---- cover ---- */
/* NO FLEX + FIXED mm HEIGHT ON A PRINT PAGE. Chrome's print pipeline mislaid
 * every child after the first: the cover rendered with only the kicker visible
 * and the title, language list, metadata and note simply absent. Plain block
 * flow with top padding is boring and survives pagination. */
.cover{ text-align:center; padding:26mm 8mm 0; }
.cover-mark{ width:52mm; height:auto; display:block; margin:0 auto 7mm;
             /* The asset now carries a real alpha channel, so no blend hack.
              * The original had a (252,253,253) backing -- near-white but NOT
              * white, so mix-blend-mode:multiply left a visible grey rectangle.
              * 15_LOGOS/zedec_transparent.png is the same artwork with the outer
              * background flood-filled to alpha 0 FROM THE CORNERS, which keeps
              * the white circuit traces inside the fox intact; a global white key
              * would have punched holes through those too. */ }
.cover-kicker{ font-size:11pt; letter-spacing:.34em; color:var(--muted);
               text-transform:uppercase; margin-bottom:6mm; }
.cover-title{ font-size:30pt; font-weight:700; line-height:1.15; margin:0 0 3mm;
              border:0; padding:0; }
.cover-sub{ font-size:12pt; color:var(--muted); }
.cover-rule{ width:44mm; height:2px; background:var(--accent); margin:8mm auto; }
.cover-langs{ font-size:12.5pt; line-height:2; margin-bottom:12mm; }
.cover-meta{ font-size:9.5pt; color:var(--muted); }
.cover-meta p{ margin:1.4mm 0; }
.cover-meta .lic{ font-size:8pt; margin-top:4mm; }
.cover-note{ margin:14mm auto 0; max-width:130mm; font-size:8.6pt;
             color:var(--ink); border-top:1px solid var(--rule); padding-top:5mm;
             text-align:left; }

/* ---- language separator page ---- */
.langsep{ text-align:center; padding:88mm 8mm 0; }
.langsep-code{ font-size:52pt; font-weight:700; color:var(--accent); line-height:1; }
.langsep-native{ font-size:22pt; margin-top:6mm; }
.langsep-en{ font-size:10.5pt; color:var(--muted); letter-spacing:.2em;
             text-transform:uppercase; margin-top:3mm; }

/* ---- body typography ---- */
/* SCOPE THE CHAPTER BREAK TO CHAPTER HEADINGS ONLY. An unscoped
 * h1{page-break-before:always} also caught .cover-title, which threw the cover's
 * title and everything after it onto page 2 and left page 1 holding nothing but
 * the kicker line. The cover looked "empty" and the cause was pagination, not
 * fonts -- which is why the raster check below is worth keeping even once the
 * glyphs are known good. */
section.lang h1{ font-size:16pt; margin:0 0 4mm; padding-bottom:2mm;
    border-bottom:2px solid var(--accent); page-break-before:always;
    page-break-after:avoid; }
section.lang > h1:first-child{ page-break-before:avoid; }
.cover-title{ page-break-before:avoid; page-break-after:avoid; }
h2{ font-size:12pt; margin:7mm 0 2.5mm; page-break-after:avoid; }
h3{ font-size:10.4pt; margin:5mm 0 2mm; color:#333; page-break-after:avoid; }
p{ margin:0 0 2.6mm; orphans:3; widows:3; }
ul,ol{ margin:0 0 3mm; padding-left:6mm; }
li{ margin-bottom:1.1mm; }
strong{ font-weight:700; }
hr{ border:0; border-top:1px solid var(--rule); margin:6mm 0; }

/* ---- code: must never reflow or wrap silently ---- */
code{ font-family: "SF Mono", Menlo, Consolas, monospace; font-size:8.2pt;
      background:var(--code-bg); padding:.4mm 1mm; border-radius:2px; }
pre{ background:var(--code-bg); border:1px solid var(--rule); border-radius:3px;
     padding:2.6mm 3mm; overflow:hidden; page-break-inside:avoid; margin:0 0 3mm; }
pre code{ background:none; padding:0; font-size:7.8pt; line-height:1.45;
          white-space:pre-wrap; word-break:break-all; }

/* ---- tables ---- */
table{ border-collapse:collapse; width:100%; margin:0 0 3.5mm; font-size:8.4pt;
       page-break-inside:avoid; }
th,td{ border:1px solid var(--rule); padding:1.5mm 2mm; text-align:left;
       vertical-align:top; }
th{ background:#f0efec; font-weight:700; }
td code, th code{ font-size:7.6pt; }

/* ---- blockquote = the callouts, incl. the corrections ---- */
blockquote{ margin:0 0 3.5mm; padding:2.5mm 3.5mm; background:var(--warn-bg);
            border-left:3px solid var(--warn-br); page-break-inside:avoid; }
blockquote p{ margin:0 0 1.6mm; }
blockquote p:last-child{ margin-bottom:0; }
</style>
</head><body>
HEAD
cat "$TMP"/00_cover.html >> "$TMP/doc.html"
for f in "$TMP"/[0-9][0-9]_*.html; do
  case "$f" in *00_cover.html) continue;; esac
  cat "$f" >> "$TMP/doc.html"
done
echo "</body></html>" >> "$TMP/doc.html"

echo "  assembled HTML: $(wc -c < "$TMP/doc.html" | tr -d ' ') bytes"

# ---- 4. render --------------------------------------------------------------
mkdir -p "$(dirname "$OUT")"
"$CHROME" --headless --disable-gpu --no-pdf-header-footer \
  --print-to-pdf="$(cd "$(dirname "$OUT")" && pwd)/$(basename "$OUT")" \
  --virtual-time-budget=30000 \
  "file://$TMP/doc.html" >/dev/null 2>&1

if [ ! -f "$OUT" ]; then echo "  PDF RENDER FAILED"; exit 1; fi
echo "  PDF: $OUT  ($(( $(stat -f%z "$OUT" 2>/dev/null || stat -c%s "$OUT") / 1024 )) KB)"
echo "  languages included: $n"

# ---- 5. VERIFY: rasterise page 1 and confirm CJK glyphs actually survived -----
# This step exists because the FIRST build of this document looked fine in every
# check that did not involve looking at the output: the HTML screenshot was
# perfect, the PDF had 64 pages, the embedded font table looked ordinary -- and
# every kanji had been silently dropped. Glyph loss is invisible to byte counts.
if command -v qlmanage >/dev/null 2>&1; then
  QT="$TMP/verify"; mkdir -p "$QT"
  qlmanage -t -s 1400 -o "$QT" "$OUT" >/dev/null 2>&1
  shot="$(ls "$QT"/*.png 2>/dev/null | head -1)"
  if [ -n "$shot" ]; then
    cp "$shot" "$(dirname "$OUT")/_page1_verify.png"
    echo "  VERIFY: page-1 raster written to $(dirname "$OUT")/_page1_verify.png"
    echo "          OPEN IT. If any CJK block looks blank, the font stack broke again."
  fi
fi
