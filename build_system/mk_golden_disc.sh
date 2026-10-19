#!/usr/bin/env bash
# mk_golden_disc.sh — the golden disc: the bare-metal UEFI disc with the AI
# models, fonts, music, brand and app assets on it, nothing to download.
#
#   bash build_system/baremetal_images.sh <arch>    # kernel + BOOT<arch>.EFI first
#   bash build_system/mk_golden_disc.sh <x86_64|arm64>
#
# Same shape as mk_efi_disc.sh (GPT, one FAT32 EFI System Partition, the
# loader at /EFI/BOOT/BOOT<arch>.EFI, /MEGAROM.TVL), plus:
#
#   /ZXV/MODELS/<name>.gguf   every model in build_system/golden.models
#   /ZXV/MODELS.SHA           "<sha256> <bytes> <name>" per model; the EFI stub
#                             re-hashes each model on boot (efi_models.h)
#   /ZXV/...                  the asset folders in build_system/golden.contents
#   /ZXV/MANIFEST.SHA256      sha256 of every file on the disc (sha256sum -c)
#   /ZXV/README.TXT           what is on the disc and the models' licences
#
# Models come from ZXV_MODEL_CACHE (default dist/models) when a file there
# already has the pinned size and sha256; otherwise they are downloaded from
# the pinned URL and refused unless both match. GOLDEN_OFFLINE=1 forbids the
# download. Weights never enter git: the cache and the disc live under dist/.
#
# Output: dist/discs/zxv-<arch>-uefi-golden.img and a size breakdown in
# dist/discs/zxv-<arch>-uefi-golden.sizes. GOLDEN_MODELS, GOLDEN_CONTENTS and
# GOLDEN_NAME swap the two lists and the output name (test_golden_disc.sh
# builds a small disc with a test file as its model that way). Nothing here writes to a physical
# disk; writing the image to a USB stick is a separate, deliberate step.
set -euo pipefail
cd "$(dirname "$0")/.."

ARCH="${1:?usage: mk_golden_disc.sh <x86_64|arm64>}"
case "$ARCH" in
  x86_64) EFI="BOOTX64.EFI" ;;
  arm64)  EFI="BOOTAA64.EFI" ;;
  *) echo "mk_golden_disc: unknown arch '$ARCH' (x86_64|arm64)"; exit 2 ;;
esac

OUTDIR=dist/discs
NAME="${GOLDEN_NAME:-zxv-${ARCH}-uefi-golden}"
MODELS_LIST="${GOLDEN_MODELS:-build_system/golden.models}"
CONTENTS_LIST="${GOLDEN_CONTENTS:-build_system/golden.contents}"
OUT="$OUTDIR/$NAME.img"
SIZES="$OUTDIR/$NAME.sizes"
ROM="$OUTDIR/MEGAROM.TVL"
CACHE="${ZXV_MODEL_CACHE:-dist/models}"
WORK="$OUTDIR/.$NAME"
ESP="$WORK/esp.img"

log(){ printf '\033[1;36m==> %s\033[0m\n' "$*"; }
die(){ printf '\033[1;31mERR: %s\033[0m\n' "$*" >&2; exit 1; }

for t in mformat mmd mcopy sgdisk sha256sum; do
  command -v "$t" >/dev/null 2>&1 || die "missing tool: $t"
done
[ -f "$EFI" ] || die "$EFI not built: run build_system/baremetal_images.sh $ARCH first"
[ -f "$ROM" ] || die "$ROM not built: run build_system/baremetal_images.sh $ARCH first"
grep -qa 'MODELS.SHA' "$EFI" \
  || die "$EFI has no model check (kernel/arch/efi_models.h): rebuild it"

rm -rf "$WORK"; mkdir -p "$WORK" "$CACHE"
LIST="$WORK/files"   # "<source>\t<disc path>" per file
: > "$LIST"

# ---- 1. models: pinned size + sha256, from the cache or the pinned URL ------
MODELS_SHA="$WORK/MODELS.SHA"
{ echo "# sha256 bytes name -- checked by the EFI stub on every boot"; } > "$MODELS_SHA"
nmodels=0
while read -r name sha bytes _cid lic urls; do
  case "$name" in ''|'#'*) continue ;; esac
  [[ "$name" =~ ^[A-Za-z0-9._-]+\.gguf$ ]] || die "golden.models: bad name '$name'"
  [[ "$sha" =~ ^[0-9a-f]{64}$ ]] || die "golden.models: bad sha256 for $name"
  [[ "$bytes" =~ ^[0-9]+$ ]] || die "golden.models: bad size for $name"
  f="$CACHE/$name"
  ok(){ [ -f "$1" ] && [ "$(stat -c %s "$1")" = "$bytes" ] && [ "$(sha256sum "$1" | cut -d' ' -f1)" = "$sha" ]; }
  if ok "$f"; then
    log "model $name: cached copy matches"
  else
    [ "${GOLDEN_OFFLINE:-0}" = 1 ] && die "model $name not in $CACHE and GOLDEN_OFFLINE=1"
    got=0
    for u in $urls; do
      log "model $name: downloading $u"
      if curl -fL --retry 4 --retry-delay 2 -o "$f.part" "$u" && ok "$f.part"; then
        mv "$f.part" "$f"; got=1; break
      fi
      echo "   size or sha256 differs from $MODELS_LIST, or the download failed"
      rm -f "$f.part"
    done
    [ "$got" = 1 ] || die "model $name: no source gave the pinned file"
  fi
  printf '%s %s %s\n' "$sha" "$bytes" "$name" >> "$MODELS_SHA"
  printf '%s\t%s\n' "$f" "ZXV/MODELS/$name" >> "$LIST"
  printf '%s\t%s\n' "$lic" "$name" >> "$WORK/licences"
  nmodels=$((nmodels + 1))
done < "$MODELS_LIST"
[ "$nmodels" -gt 0 ] || die "$MODELS_LIST lists no models"
printf '%s\t%s\n' "$MODELS_SHA" "ZXV/MODELS.SHA" >> "$LIST"

# ---- 2. assets from golden.contents ------------------------------------------
missing=()
while read -r src dst; do
  case "$src" in ''|'#'*) continue ;; esac
  if [ -f "$src" ]; then
    printf '%s\t%s\n' "$src" "$dst" >> "$LIST"
  elif [ -d "$src" ]; then
    while IFS= read -r -d '' f; do
      printf '%s\t%s\n' "$f" "$dst/${f#"$src"/}" >> "$LIST"
    done < <(find "$src" -type f -print0 | sort -z)
  else
    missing+=("$src")
  fi
done < "$CONTENTS_LIST"

# ---- 3. README, then a manifest of every file -------------------------------
{
  echo "ZXV golden disc ($ARCH)"
  echo
  echo "Boots under UEFI from /EFI/BOOT/$EFI. Everything below is on the disc;"
  echo "nothing is downloaded. On every boot the loader re-checks each model"
  echo "against ZXV/MODELS.SHA and prints [MODELS_OK] on the serial console."
  echo "To check the whole disc from another computer:"
  echo "  cd <mounted disc> && sha256sum -c ZXV/MANIFEST.SHA256"
  echo
  echo "Models (weights licensed for redistribution and commercial use):"
  while IFS=$'\t' read -r lic name; do echo "  $name  ($lic)"; done < "$WORK/licences"
  echo "  Qwen2.5 by the Qwen team, Alibaba Cloud. Apache-2.0 text: ZXV/LICENSES/LICENSE"
  echo
  echo "Software: Apache-2.0, 36N9 Genetics LLC. No warranty."
} > "$WORK/README.TXT"
printf '%s\t%s\n' "$WORK/README.TXT" "ZXV/README.TXT" >> "$LIST"
printf '%s\t%s\n' "$EFI" "EFI/BOOT/$EFI" >> "$LIST"
printf '%s\t%s\n' "$ROM" "MEGAROM.TVL" >> "$LIST"

log "hashing $(wc -l < "$LIST") files for ZXV/MANIFEST.SHA256"
while IFS=$'\t' read -r src dst; do
  printf '%s  %s\n' "$(sha256sum "$src" | cut -d' ' -f1)" "$dst"
done < "$LIST" > "$WORK/MANIFEST.SHA256"
printf '%s\t%s\n' "$WORK/MANIFEST.SHA256" "ZXV/MANIFEST.SHA256" >> "$LIST"

# ---- 4. FAT32 ESP sized to the contents --------------------------------------
total=0
while IFS=$'\t' read -r src _; do total=$((total + $(stat -c %s "$src"))); done < "$LIST"
nfiles=$(wc -l < "$LIST")
# 4 KiB clusters: round each file up, add the FATs (4 bytes per cluster, two
# copies), directory slack, and 16 MiB of headroom; at least 64 MiB.
clusters=0
while IFS=$'\t' read -r src _; do clusters=$((clusters + ($(stat -c %s "$src") + 4095) / 4096)); done < "$LIST"
esp_bytes=$(( clusters * 4096 + clusters * 8 + nfiles * 8192 + (16 << 20) ))
ESP_MB=$(( (esp_bytes + (1 << 20) - 1) >> 20 )); [ "$ESP_MB" -ge 64 ] || ESP_MB=64
log "formatting a ${ESP_MB} MiB FAT32 ESP for $nfiles files ($total bytes)"
truncate -s "${ESP_MB}M" "$ESP"
# FAT32 needs at least 65525 clusters: 4 KiB clusters from 300 MiB up, 512 B below.
CSEC=8; [ "$ESP_MB" -ge 300 ] || CSEC=1
mformat -i "$ESP" -F -c "$CSEC" -v ZXVGOLDEN :: || die "mformat (FAT32) failed"

# Directories first (parents before children), then the files.
cut -f2 "$LIST" | while read -r d; do
  # a top-level file (MEGAROM.TVL) has no parent to make: without this guard
  # ${d%/*} is the file name itself and mmd turned it into a directory
  case "$d" in */*) ;; *) continue ;; esac
  d="${d%/*}"; p=""
  IFS=/ read -ra parts <<<"$d"
  for part in "${parts[@]}"; do p="$p/$part"; echo "$p"; done
done | awk '!seen[$0]++' | while read -r d; do
  mmd -i "$ESP" "::$d" 2>/dev/null || true
done
while IFS=$'\t' read -r src dst; do
  mcopy -i "$ESP" -o "$src" "::/$dst" || die "mcopy $src -> $dst failed"
done < "$LIST"

# ---- 5. wrap it in a GPT disc (as mk_efi_disc.sh does) -----------------------
PART_START=2048
ESP_SECTORS=$((ESP_MB * 2048))
PART_END=$((PART_START + ESP_SECTORS - 1))
DISK_SECTORS=$((PART_END + 1 + 2048))
rm -f "$OUT"
truncate -s $((DISK_SECTORS * 512)) "$OUT"
sgdisk -o -n "1:${PART_START}:${PART_END}" -t 1:EF00 \
       -c 1:"ZXV golden ESP" "$OUT" >/dev/null 2>&1 || die "sgdisk failed"
dd if="$ESP" of="$OUT" bs=1M seek=1 conv=notrunc,sparse status=none

# ---- 6. size breakdown, by top folder on the disc -----------------------------
{
  img=$(stat -c %s "$OUT")
  echo "golden disc $ARCH: $img bytes ($((img >> 20)) MiB; ESP ${ESP_MB} MiB, $nfiles files, $total bytes of files)"
  while IFS=$'\t' read -r src dst; do
    case "$dst" in
      ZXV/MODELS/*) k="$dst" ;;
      ZXV/*/*) k="${dst%%/*}/$(cut -d/ -f2 <<<"$dst")/" ;;
      *) k="$dst" ;;
    esac
    printf '%s\t%s\n' "$k" "$(stat -c %s "$src")"
  done < "$LIST" | awk -F'\t' '{s[$1]+=$2} END{for(k in s) printf "  %12d  %s\n", s[k], k}' | sort -k2
  if [ ${#missing[@]} -gt 0 ]; then echo "  not in this tree yet (skipped): ${missing[*]}"; fi
} > "$SIZES"
rm -rf "$WORK"
log "DISC READY: $OUT"
cat "$SIZES"
