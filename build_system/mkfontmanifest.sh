#!/usr/bin/env bash
# mkfontmanifest.sh — catalog the ASCW font set into a manifest the OS installs.
#
# The ASCW faces encode their family and script in the filename:
#     ASCW-<Family>-<Script>.<ttf|otf>      e.g. ASCW-Golden-Bengali.ttf
# so the manifest is derived directly, no font parsing required. The output is
# a table the font installer reads to populate the registry (script.h/font.h):
#     Family|ScriptToken|Style|Filename
# and a report of which scripts the kernel's script table already knows vs which
# are carried-but-unclassified (so nothing is silently mis-routed).
#
#   bash build_system/mkfontmanifest.sh <fonts-dir> [out-manifest]
set -uo pipefail
SRC="${1:-}"
OUT="${2:-dist/fonts.manifest}"
[ -n "$SRC" ] && [ -d "$SRC" ] || { echo "usage: $0 <fonts-dir> [out-manifest]" >&2; exit 2; }
mkdir -p "$(dirname "$OUT")"

# scripts the kernel's font_script_from_name() recognises (keep in sync)
known="Latin Greek Cyrillic Armenian Hebrew Arabic Syriac Thaana NKo \
Devanagari Bengali Tamil Thai Georgian Hangul Ethiopic Cherokee \
CanadianAboriginal Tifinagh Vai Adlam Han Hiragana Katakana Avestan Enochian \
Aramaic Kana Sanskrit Roman"

is_known() { case " $known " in *" $1 "*) return 0;; *) return 1;; esac; }

: > "$OUT"
total=0; classified=0; unknown_list=""
shopt -s nullglob
for f in "$SRC"/*.ttf "$SRC"/*.otf; do
  base=$(basename "$f"); name="${base%.*}"
  case "$name" in ASCW-*) ;; *) continue;; esac
  rest="${name#ASCW-}"
  tok1="${rest%%-*}"
  tok2="${rest#*-}"; tok2="${tok2%%-*}"
  # ASCW filenames come in two shapes:
  #   ASCW-<Family>-<Script>   (Golden/Dragon families: family first)
  #   ASCW-<Script>-Regular    (standalone faces: script first, style second)
  # so if the SECOND token is a style word, the FIRST is the script.
  case "$tok2" in
    Regular|Display|Bold|Italic|Medium|Light|Thin|Black|SemiBold|Book|Roman)
      family="ASCW"; script="$tok1" ;;
    *)
      family="$tok1"; script="$tok2" ;;
  esac
  style="regular"
  case "$name" in
    *Display*|ASCW-Dragon-*) style="display";;
    *Enochian*) style="ceremonial";;
    *Mono*) style="mono";;
  esac
  printf '%s|%s|%s|%s\n' "$family" "$script" "$style" "$base" >> "$OUT"
  total=$((total+1))
  if is_known "$script"; then classified=$((classified+1))
  else unknown_list="$unknown_list $script"; fi
done
shopt -u nullglob

echo "wrote $OUT"
echo "  faces cataloged : $total"
echo "  script-classified: $classified"
if [ -n "$unknown_list" ]; then
  u=$(echo "$unknown_list" | tr ' ' '\n' | sort -u | grep -c . )
  echo "  carried but not yet in the kernel script table: $u distinct"
  echo "$unknown_list" | tr ' ' '\n' | sort -u | grep . | sed 's/^/    /'
  echo "  (these faces install and render via their own face; itemization"
  echo "   routes their codepoints to Unknown->fallback until added to script.c)"
else
  echo "  every cataloged script is recognised by the kernel"
fi
exit 0
