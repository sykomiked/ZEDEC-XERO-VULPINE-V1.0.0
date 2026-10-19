#!/usr/bin/env bash

# PORTABLE SIZE AND HASH. The builders run on macOS as well as Linux; GNU's
# `stat -c %s` and `sha256sum` do not exist on BSD. One wrapper each keeps a
# single script for both.
fsize() { stat -c %s "$1" 2>/dev/null || stat -f %z "$1"; }
shash() {
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$1" | cut -d' ' -f1
  else
    shasum -a 256 "$1" | cut -d' ' -f1
  fi
}
# mk_golden_disc.sh — the golden discs: a core disc per architecture plus
# swarm discs shared by every architecture, with the AI models, fonts, music,
# brand and app assets on them, nothing to download.
#
#   bash build_system/baremetal_images.sh <arch>    # kernel + BOOT<arch>.EFI first
#   bash build_system/mk_golden_disc.sh <x86_64|arm64>   # the core disc
#   bash build_system/mk_golden_disc.sh swarm            # every swarm disc
#
# Machine code and model weights are kept apart. Weights hold no CPU
# instructions, so one set of swarm discs serves every architecture, and a
# kernel fix rebuilds only the small core disc while the swarm discs keep
# their SHA-256 sums.
#
#   zxv-<arch>-disc1-core.img        architecture: <arch>
#     GPT, one FAT32 EFI System Partition, as mk_efi_disc.sh, plus
#     /ZXV/MODELS/MDLnnn.GGU          the models named in build_system/golden.core
#     /ZXV/MODELS.SHA                 "<sha256> <bytes> <name> esp <8.3 file>"
#     /ZXV/...                        the asset folders in build_system/golden.contents
#     /ZXV/MANIFEST.SHA256            sha256 of every file on it (sha256sum -c)
#     /ZXV/README.TXT                 what is on the disc and the models' licences
#     /ZXV/UPSTREAM.CID               build_system/zxv_upstream.cid: the bucket
#                                     the swarm discs can be fetched from
#     /ZXV/SWARM.LST                  "<disc> <GROUP> <image>" per swarm disc
#                                     this core expects
#     /ZXV/LAYER0.JSN .. LAYER2.JSN   with GOLDEN_LAYERS: the release layers L0
#                                     to L2 the core stands on, checked by the
#                                     kernel before [BOOT_OK] (docs/LAYERS.md)
#   zxv-swarm-disc<N>-<group>.img    architecture: all; N from 2, one per group
#     GPT, one FAT32 partition of type 5A585653-5741-4D53-9000-000000000001
#     named ZXV_SWARM_<GROUP>, holding
#     /MODELS/MDLnnn.GGU              the group's models (build_system/golden.swarm)
#     /MODELS.SHA                     "<sha256> <bytes> <name> swarm <8.3 file>"
#     /MANIFEST.SHA256, /README.TXT
#
# Every model in golden.models is on exactly one disc: the core, or the one
# swarm group golden.swarm puts it in. Model files get 8.3 names (MDL000.GGU,
# numbered in golden.models order, unique across all discs) because the
# kernel's FAT32 driver reads short names only; each MODELS.SHA maps them to
# the real names. The x86_64 kernel finds the core disc and every ZXV_SWARM_*
# disc attached, over AHCI or NVMe after ExitBootServices, and hashes each
# model when it is first used or in the idle loop after [BOOT_OK]
# (kernel/src/hwdrv/hw_models.h). The arm64 loader checks the core disc's
# models before ExitBootServices, streamed 4 MiB at a time
# (kernel/arch/efi_models.h); its kernel cannot reach swarm discs yet.
#
# Limits, checked before anything is written: no disc image over 1.8 GB
# (1,800,000,000 bytes), so each fits a DVD-R or a cheap stick and stays clear
# of 2 GB offset limits; no file of 3.8 GiB or more, since FAT32 stops at
# 4 GiB per file.
#
# Models come from ZXV_MODEL_CACHE (default dist/models) when a file there
# already has the pinned size and sha256; otherwise they are downloaded from
# the pinned URL and refused unless both match. GOLDEN_OFFLINE=1 forbids the
# download. Weights never enter git: the cache and the discs live under dist/.
# The discs are reproducible: FAT timestamps follow SOURCE_DATE_EPOCH, and
# volume serials and GPT GUIDs are fixed per arch or per swarm group.
#
# Output in dist/discs, each image with a .sizes breakdown. GOLDEN_MODELS,
# GOLDEN_CORE, GOLDEN_SWARM, GOLDEN_CONTENTS and GOLDEN_PREFIX (default zxv)
# swap the lists and the name prefix (test_golden_disc.sh builds small discs
# with test files as models that way). Nothing here writes to a physical
# disk; writing an image to a USB stick is a separate, deliberate step.
set -euo pipefail
cd "$(dirname "$0")/.."

TARGET="${1:?usage: mk_golden_disc.sh <x86_64|arm64|swarm>}"
case "$TARGET" in
  x86_64) EFI="BOOTX64.EFI" ;;
  arm64)  EFI="BOOTAA64.EFI" ;;
  swarm)  EFI="" ;;
  *) echo "mk_golden_disc: unknown target '$TARGET' (x86_64|arm64|swarm)"; exit 2 ;;
esac

OUTDIR=dist/discs
PREFIX="${GOLDEN_PREFIX:-zxv}"
MODELS_LIST="${GOLDEN_MODELS:-build_system/golden.models}"
CORE_LIST="${GOLDEN_CORE:-build_system/golden.core}"
SWARM_LIST="${GOLDEN_SWARM:-build_system/golden.swarm}"
CONTENTS_LIST="${GOLDEN_CONTENTS:-build_system/golden.contents}"
UPSTREAM="${GOLDEN_UPSTREAM:-build_system/zxv_upstream.cid}"
ROM="$OUTDIR/MEGAROM.TVL"
CACHE="${ZXV_MODEL_CACHE:-dist/models}"
WORK="$OUTDIR/.$PREFIX-golden-$TARGET"
# FAT32 per-file limit is 4 GiB - 1; refuse 3.8 GiB (38/10 GiB) and up.
MAX_FILE_X10=$((38 << 30))
MAX_DISC=1800000000

log(){ printf '\033[1;36m==> %s\033[0m\n' "$*"; }
die(){ printf '\033[1;31mERR: %s\033[0m\n' "$*" >&2; exit 1; }

for t in mformat mmd mcopy sgdisk sha256sum; do
  command -v "$t" >/dev/null 2>&1 || die "missing tool: $t"
done
for l in "$MODELS_LIST" "$CORE_LIST" "$SWARM_LIST" "$UPSTREAM"; do [ -f "$l" ] || die "$l missing"; done
if [ -n "$EFI" ]; then
  [ -f "$EFI" ] || die "$EFI not built: run build_system/baremetal_images.sh $TARGET first"
  [ -f "$ROM" ] || die "$ROM not built: run build_system/baremetal_images.sh $TARGET first"
  # The model check must be in the loader (arm64) or the kernel it carries (x86_64).
  case "$TARGET" in
    arm64)  grep -qa 'MODELS.SHA' "$EFI" || die "$EFI has no model check (kernel/arch/efi_models.h): rebuild it" ;;
    x86_64) grep -qa 'SWARM_MOUNTED' "$EFI" kernel_x86_64.elf 2>/dev/null \
              || die "the x86_64 kernel has no model check (kernel/src/hwdrv/hw_models.c): rebuild it" ;;
  esac
fi

rm -rf "$WORK"; mkdir -p "$WORK" "$CACHE"
export SOURCE_DATE_EPOCH="${SOURCE_DATE_EPOCH:-1767225600}"

# ---- 1. where each model goes ------------------------------------------------
lines(){ grep -vE '^[[:space:]]*(#|$)' "$1" || true; }
# "<model> <group>" for every model; group "core" for the core disc.
PLACE="$WORK/place"; : > "$PLACE"
while read -r name _; do printf '%s core\n' "$name" >> "$PLACE"; done < <(lines "$CORE_LIST")
while read -r group name rest; do
  [ -z "${rest:-}" ] || die "golden.swarm: '$group $name $rest' has extra fields"
  [[ "$group" =~ ^[a-z0-9]{1,16}$ ]] || die "golden.swarm: group '$group' is not 1-16 of a-z0-9"
  [ "$group" != core ] || die "golden.swarm: 'core' is the core disc, not a swarm group"
  printf '%s %s\n' "$name" "$group" >> "$PLACE"
done < <(lines "$SWARM_LIST")
dupl=$(cut -d' ' -f1 "$PLACE" | sort | uniq -d | head -1)
[ -z "$dupl" ] || die "$dupl is placed twice (golden.core and golden.swarm put each model on one disc)"
GROUPS_IN_ORDER=$(lines "$SWARM_LIST" | awk '!seen[$1]++ {print $1}')

# ---- 2. models: pinned size + sha256, from the cache or the pinned URL ------
# Fills $WORK/<group>.list ("<source>\t<path on the disc>"), $WORK/<group>.sha
# (MODELS.SHA lines) and $WORK/<group>.lic for every group with a model.
nmodels=0
while read -r name sha bytes _cid lic urls; do
  [[ "$name" =~ ^[A-Za-z0-9._-]+\.gguf$ ]] || die "golden.models: bad name '$name'"
  [ "${#name}" -le 63 ] || die "golden.models: name '$name' longer than 63 characters"
  [[ "$sha" =~ ^[0-9a-f]{64}$ ]] || die "golden.models: bad sha256 for $name"
  [[ "$bytes" =~ ^[0-9]+$ ]] || die "golden.models: bad size for $name"
  [ $((bytes * 10)) -lt "$MAX_FILE_X10" ] \
    || die "model $name is $bytes bytes: no file on a disc may reach 3.8 GiB (FAT32 stops at 4 GiB)"
  group=$(awk -v n="$name" '$1 == n {print $2}' "$PLACE")
  [ -n "$group" ] || die "$name is on no disc: add it to golden.core or golden.swarm"
  file=$(printf 'MDL%03d.GGU' "$nmodels")
  nmodels=$((nmodels + 1))
  # The core disc of one arch needs only its own models; the swarm target all
  # the swarm groups' models.
  if [ "$TARGET" = swarm ]; then [ "$group" != core ] || continue
  else [ "$group" = core ] || continue; fi
  f="$CACHE/$name"
  ok(){ [ -f "$1" ] && [ "$(fsize "$1")" = "$bytes" ] && [ "$(shash "$1")" = "$sha" ]; }
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
  if [ "$group" = core ]; then
    printf '%s %s %s esp %s\n' "$sha" "$bytes" "$name" "$file" >> "$WORK/core.sha"
    printf '%s\t%s\n' "$f" "ZXV/MODELS/$file" >> "$WORK/core.list"
  else
    printf '%s %s %s swarm %s\n' "$sha" "$bytes" "$name" "$file" >> "$WORK/$group.sha"
    printf '%s\t%s\n' "$f" "MODELS/$file" >> "$WORK/$group.list"
  fi
  printf '%s\t%s\t%s\n' "$lic" "$name" "$file" >> "$WORK/$group.lic"
done < <(lines "$MODELS_LIST")
[ "$nmodels" -gt 0 ] || die "$MODELS_LIST lists no models"
[ "$nmodels" -le 32 ] || die "$MODELS_LIST lists $nmodels models; the kernel tracks 32 (HW_MODELS_MAX)"
while read -r name _; do
  grep -qE "^$name[[:space:]]" "$MODELS_LIST" || die "$name is placed on a disc but $MODELS_LIST does not list it"
done < "$PLACE"

# ---- 3. building blocks ---------------------------------------------------------
# Hash the files of list $1 into manifest $2 (paths as on the disc).
manifest(){ while IFS=$'\t' read -r src dst; do
  printf '%s  %s\n' "$(sha256sum "$src" | cut -d' ' -f1)" "$dst"; done < "$1" > "$2"; }

# Refuse any file of 3.8 GiB or more in list $1.
file_cap(){ while IFS=$'\t' read -r src dst; do
  sz=$(fsize "$src")
  [ $((sz * 10)) -lt "$MAX_FILE_X10" ] \
    || die "$dst is $sz bytes: no file on a disc may reach 3.8 GiB (FAT32 stops at 4 GiB)"
done < "$1"; }

# Format FAT32 image $1 (label $2) for list $3 (directories first, parents
# before children, then the files), serial $4, at least $5 MiB. Prints the
# size in MiB.
fat_image(){
  local img=$1 label=$2 list=$3 serial=$4 min=$5 clusters=0 nfiles mb csec
  nfiles=$(wc -l < "$list")
  # 4 KiB clusters: round each file up, add the FATs (4 bytes per cluster,
  # two copies), directory slack, and 16 MiB of headroom.
  while IFS=$'\t' read -r src _; do clusters=$((clusters + ($(fsize "$src") + 4095) / 4096)); done < "$list"
  mb=$(( (clusters * 4096 + clusters * 8 + nfiles * 8192 + (16 << 20) + (1 << 20) - 1) >> 20 ))
  [ "$mb" -ge "$min" ] || mb=$min
  truncate -s "${mb}M" "$img"
  # FAT32 needs at least 65525 clusters: 4 KiB clusters from 300 MiB up, 512 B below.
  # 512B clusters cap FAT32 at ~32 MB (65525 clusters): anything larger
  # must use 4KiB clusters, which need a 256+ MB volume
  csec=8; [ "$mb" -ge 300 ] || csec=1
  if [ "$csec" = 1 ] && [ "$mb" -gt 33 ]; then mb=300; csec=8; fi
  mformat -i "$img" -F -c "$csec" -N "$serial" -v "$label" :: >/dev/null 2>&1 \
    || die "mformat (FAT32) $label failed"
  cut -f2 "$list" | while read -r d; do
    # a top-level file (MEGAROM.TVL) has no parent to make: without this guard
    # ${d%/*} is the file name itself and mmd turned it into a directory
    case "$d" in */*) ;; *) continue ;; esac
    d="${d%/*}"; p=""
    IFS=/ read -ra parts <<<"$d"
    for part in "${parts[@]}"; do p="$p/$part"; echo "$p"; done
  done | awk '!seen[$0]++' | while read -r d; do
    mmd -i "$img" "::$d" >&2 2>/dev/null || true
  done
  while IFS=$'\t' read -r src dst; do
    mcopy -i "$img" -o "$src" "::/$dst" >&2 || die "mcopy $src -> $dst failed"
  done < "$list"
  echo "$mb"
}

# Wrap FAT image $2 ($3 MiB) as the single GPT partition of disc $1: type $4,
# name $5, disk GUID $6. Fails over the 1.8 GB cap.
gpt_disc(){
  local out=$1 fat=$2 mb=$3 type=$4 name=$5 guid=$6 start=2048 end sectors
  end=$((start + mb * 2048 - 1))
  sectors=$((end + 1 + 2048))
  [ $((sectors * 512)) -le "$MAX_DISC" ] \
    || die "$out would be $((sectors * 512)) bytes: a disc may not pass 1.8 GB; move models to another swarm group"
  rm -f "$out"
  truncate -s $((sectors * 512)) "$out"
  sgdisk -o -n "1:${start}:${end}" -t "1:$type" -c "1:$name" \
         -u "1:${guid%????????????}00000000E5F1" -U "$guid" "$out" >/dev/null 2>&1 \
    || die "sgdisk failed for $out"
  DD=$(command -v gdd 2>/dev/null || echo dd)
  "$DD" if="$fat" of="$out" bs=1M seek=1 conv=notrunc status=none
}

# Size breakdown of list $1 for disc $2 into $3.
sizes(){
  local img
  img=$(fsize "$2")
  {
    echo "$(basename "$2"): $img bytes ($((img >> 20)) MiB), $(wc -l < "$1") files"
    while IFS=$'\t' read -r src dst; do
      case "$dst" in
        ZXV/MODELS/*|MODELS/*) k="$dst" ;;
        ZXV/*/*) k="${dst%%/*}/$(cut -d/ -f2 <<<"$dst")/" ;;
        *) k="$dst" ;;
      esac
      printf '%s\t%s\n' "$k" "$(fsize "$src")"
    done < "$1" | awk -F'\t' '{s[$1]+=$2} END{for(k in s) printf "  %12d  %s\n", s[k], k}' | sort -k2
  } > "$3"
  cat "$3"
}

licences(){ while IFS=$'\t' read -r lic name file; do echo "  $name  ($lic; $file)"; done < "$1"; }

# ---- 4a. the swarm discs --------------------------------------------------------
if [ "$TARGET" = swarm ]; then
  [ -n "$GROUPS_IN_ORDER" ] || die "$SWARM_LIST names no swarm groups"
  n=2
  for group in $GROUPS_IN_ORDER; do
    [ -s "$WORK/$group.sha" ] || die "swarm group $group has no model in $MODELS_LIST"
    GROUP=$(tr a-z A-Z <<<"$group")
    out="$OUTDIR/$PREFIX-swarm-disc$n-$group.img"
    list="$WORK/$group.list"
    { echo "# arch all -- swarm disc ZXV_SWARM_$GROUP; sha256 bytes name partition file"
      cat "$WORK/$group.sha"; } > "$WORK/$group.MODELS.SHA"
    printf '%s\t%s\n' "$WORK/$group.MODELS.SHA" "MODELS.SHA" >> "$list"
    {
      echo "ZXV swarm disc $n: $GROUP (architecture: all)"
      echo
      echo "AI models for every ZXV core disc (x86_64, arm64). Attach it next to the"
      echo "core disc: the x86_64 kernel finds the partition named ZXV_SWARM_$GROUP,"
      echo "lists its models from MODELS.SHA and checks each one's sha256 when it is"
      echo "first used. To check the disc from another computer:"
      echo "  cd <mounted disc> && sha256sum -c MANIFEST.SHA256"
      echo
      echo "Models (weights licensed for redistribution and commercial use):"
      licences "$WORK/$group.lic"
      echo
      echo "Software: Apache-2.0, 36N9 Genetics LLC. No warranty."
    } > "$WORK/$group.README.TXT"
    printf '%s\t%s\n' "$WORK/$group.README.TXT" "README.TXT" >> "$list"
    manifest "$list" "$WORK/$group.MANIFEST"
    printf '%s\t%s\n' "$WORK/$group.MANIFEST" "MANIFEST.SHA256" >> "$list"
    file_cap "$list"
    # fixed per group: serial and GUID from the group's name
    h=$(printf '%s' "$GROUP" | sha256sum | cut -c1-20)
    log "formatting swarm disc $n ($GROUP)"
    # at least 300 MiB, so it gets the 4 KiB clusters the kernel reads fastest
    mb=$(fat_image "$WORK/$group.fat" "SW_${GROUP:0:8}" "$list" "${h:0:8}" 300)
    gpt_disc "$out" "$WORK/$group.fat" "$mb" 5A585653-5741-4D53-9000-000000000001 \
      "ZXV_SWARM_$GROUP" "5A58560C-${h:8:4}-4000-8000-${h:12:8}0000"
    rm -f "$WORK/$group.fat"
    log "DISC READY: $out"
    sizes "$list" "$out" "${out%.img}.sizes"
    n=$((n + 1))
  done
  rm -rf "$WORK"
  exit 0
fi

# ---- 4b. the core disc ------------------------------------------------------------
ARCH=$TARGET
LIST="$WORK/core.list"; touch "$LIST"
{ echo "# arch $ARCH -- core disc; sha256 bytes name partition file -- kernel/src/hwdrv/hw_models.h"
  cat "$WORK/core.sha" 2>/dev/null || true; } > "$WORK/MODELS.SHA"
printf '%s\t%s\n' "$WORK/MODELS.SHA" "ZXV/MODELS.SHA" >> "$LIST"

missing=()
while read -r src dst; do
  if [ -f "$src" ]; then
    printf '%s\t%s\n' "$src" "$dst" >> "$LIST"
  elif [ -d "$src" ]; then
    while IFS= read -r -d '' f; do
      printf '%s\t%s\n' "$f" "$dst/${f#"$src"/}" >> "$LIST"
    done < <(find "$src" -type f -print0 | sort -z)
  else
    missing+=("$src")
  fi
done < <(lines "$CONTENTS_LIST")

{
  echo "ZXV core disc 1 ($ARCH)"
  echo
  echo "Boots under UEFI from /EFI/BOOT/$EFI. The models below are on this disc;"
  echo "the swarm discs (zxv-swarm-disc<N>-<group>.img, the same for every"
  echo "architecture) carry the rest. ZXV/MODELS.SHA lists this disc's models with"
  echo "their sha256. The x86_64 kernel checks every model on the core and the"
  echo "attached swarm discs when it is first used and prints [MODELS_OK] on the"
  echo "serial console once all are checked; the arm64 loader checks this disc's"
  echo "models on every boot. To check the disc from another computer:"
  echo "  cd <mounted disc> && sha256sum -c ZXV/MANIFEST.SHA256"
  echo
  echo "Models (weights licensed for redistribution and commercial use):"
  licences "$WORK/core.lic" 2>/dev/null || true
  echo "  Qwen2.5 by the Qwen team, Alibaba Cloud. Apache-2.0 text: ZXV/LICENSES/LICENSE"
  echo
  echo "Software: Apache-2.0, 36N9 Genetics LLC. No warranty."
} > "$WORK/README.TXT"
printf '%s\t%s\n' "$WORK/README.TXT" "ZXV/README.TXT" >> "$LIST"

# Where the rest of the release is: the upstream bucket (UPSTREAM.CID, the
# design's /etc/zxv_upstream.cid) and the swarm discs this core expects
# (SWARM.LST: "<disc> <GROUP> <image>"), so the kernel can name the missing
# ones and fetch them over the network (kernel/src/swarmnet), then seal the
# network as the "seal" line says.
while read -r key val rest; do
  case "$key" in
    peer)
      # peer <ipv4> <port> — a Vinea seed, so the key itself takes two values
      read -r port extra <<<"$rest"
      [ -n "${port:-}" ] && [ -z "${extra:-}" ] || die "$UPSTREAM: 'peer $val $rest' is not <ipv4> <port>"
      [[ "$val" =~ ^([0-9]{1,3}\.){3}[0-9]{1,3}$ ]] || die "$UPSTREAM: peer '$val' is not an IPv4 address"
      [[ "$port" =~ ^[0-9]{1,5}$ && "$port" -ge 1 && "$port" -le 65535 ]] || die "$UPSTREAM: peer port '$port' is not 1-65535"
      continue ;;
    *) [ -z "${rest:-}" ] || die "$UPSTREAM: '$key $val $rest' has extra fields" ;;
  esac
  case "$key" in
    cid)  [[ "$val" =~ ^b[a-z2-7]{20,120}$ ]] || die "$UPSTREAM: '$val' is not a base32 CIDv1" ;;
    ipns) [[ "$val" =~ ^k[0-9a-z]{20,120}$ ]] || die "$UPSTREAM: '$val' is not an IPNS name (k51...)" ;;
    gateway) [[ "$val" =~ ^https?://[[:print:]]+$ && ${#val} -lt 80 ]] || die "$UPSTREAM: '$val' is not an http:// or https:// URL under 80 characters" ;;
    seal) [[ "$val" =~ ^(POWERDOWN_NIC|LOCAL_MESH_ONLY|PROMPT_USER)$ ]] || die "$UPSTREAM: seal '$val' is not POWERDOWN_NIC, LOCAL_MESH_ONLY or PROMPT_USER" ;;
    *) die "$UPSTREAM: unknown key '$key' (cid|ipns|gateway|seal|peer)" ;;
  esac
done < <(lines "$UPSTREAM")
lines "$UPSTREAM" | grep -q '^cid ' || die "$UPSTREAM has no cid line"
[ "$(lines "$UPSTREAM" | grep -c '^seal ')" -le 1 ] || die "$UPSTREAM has more than one seal line"
cp "$UPSTREAM" "$WORK/UPSTREAM.CID"
printf '%s\t%s\n' "$WORK/UPSTREAM.CID" "ZXV/UPSTREAM.CID" >> "$LIST"
{ echo "# disc GROUP image -- swarm discs this core disc expects (golden.swarm)"
  n=2; for g in $GROUPS_IN_ORDER; do
    echo "$n $(tr a-z A-Z <<<"$g") $PREFIX-swarm-disc$n-$g.img"; n=$((n + 1)); done
} > "$WORK/SWARM.LST"
printf '%s\t%s\n' "$WORK/SWARM.LST" "ZXV/SWARM.LST" >> "$LIST"
# The release layers this core disc stands on (docs/LAYERS.md):
# GOLDEN_LAYERS is the directory `make build-all-layers` wrote. They must form
# a chain, and L1 must list this disc's boot file and core models exactly, or
# the kernel would refuse every model at boot; checked here first.
if [ -n "${GOLDEN_LAYERS:-}" ]; then
  python3 build_system/mk_layers.py check "$GOLDEN_LAYERS/L0.json" "$GOLDEN_LAYERS/L1.json" \
    "$GOLDEN_LAYERS/L2.json" > /dev/null || die "$GOLDEN_LAYERS: L0, L1, L2 do not form a chain"
  python3 - "$GOLDEN_LAYERS/L1.json" "$ARCH" "$EFI" "$WORK/core.sha" <<'PY' ||
import hashlib, json, os, sys
l1, arch, efi, core = sys.argv[1:]
objs = json.load(open(l1))["objects"]
d = open(efi, "rb").read()
boot = [(o["name"], o["bytes"], o["sha256"]) for o in objs if o["kind"] == "boot" and o["arch"] == arch]
if boot != [(os.path.basename(efi), len(d), hashlib.sha256(d).hexdigest())]:
    sys.exit("L1 does not list this %s boot file %s (%d bytes); rebuild the layers" % (arch, efi, len(d)))
disc = set()
if os.path.exists(core):
    for line in open(core):
        f = line.split()
        if f and not f[0].startswith("#"):
            disc.add((f[2], int(f[1]), f[0]))
listed = {(o["name"], o["bytes"], o["sha256"]) for o in objs if o["kind"] == "model"}
if disc != listed:
    sys.exit("L1's core models differ from this disc's: %s" % sorted(disc ^ listed))
PY
    die "$GOLDEN_LAYERS/L1.json is not the layer of this core disc"
  for i in 0 1 2; do printf '%s\t%s\n' "$GOLDEN_LAYERS/L$i.json" "ZXV/LAYER$i.JSN" >> "$LIST"; done
  log "layers L0, L1 and L2 on the disc: L2 $(cat "$GOLDEN_LAYERS/L2.cid")"
fi
printf '%s\t%s\n' "$EFI" "EFI/BOOT/$EFI" >> "$LIST"
# the same kernel again as a plain file: the legacy-BIOS stage multiboots it
# off this ESP (mk_bios_stage.sh) — a memdisk would cap the kernel's size
[ "$ARCH" = x86_64 ] && printf '%s\t%s\n' "kernel_x86_64.elf" "ZXV64.ELF" >> "$LIST"
printf '%s\t%s\n' "$ROM" "MEGAROM.TVL" >> "$LIST"
log "hashing $(wc -l < "$LIST") files for ZXV/MANIFEST.SHA256"
manifest "$LIST" "$WORK/MANIFEST.SHA256"
printf '%s\t%s\n' "$WORK/MANIFEST.SHA256" "ZXV/MANIFEST.SHA256" >> "$LIST"
file_cap "$LIST"

case "$ARCH" in
  x86_64) SERIAL=5A5800A6 DISK_GUID=5A58560B-0000-4000-8000-000000086064 ;;
  arm64)  SERIAL=5A5800A4 DISK_GUID=5A58560B-0000-4000-8000-0000000A6400 ;;
esac
OUT="$OUTDIR/$PREFIX-$ARCH-disc1-core.img"
log "formatting the core disc's FAT32 ESP for $(wc -l < "$LIST") files"
mb=$(fat_image "$WORK/esp.img" ZXVGOLDEN "$LIST" "$SERIAL" 64)
gpt_disc "$OUT" "$WORK/esp.img" "$mb" EF00 "ZXV core $ARCH" "$DISK_GUID"
log "DISC READY: $OUT"
sizes "$LIST" "$OUT" "${OUT%.img}.sizes"
if [ ${#missing[@]} -gt 0 ]; then
  echo "  not in this tree yet (skipped): ${missing[*]}" | tee -a "${OUT%.img}.sizes"
fi
rm -rf "$WORK"
