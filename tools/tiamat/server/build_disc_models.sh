#!/usr/bin/env bash
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# S9: assemble Tiamat's disc set on the server and write its golden.models.
#
#   tools/tiamat/server/build_disc_models.sh /srv/tiamat
#
# 1. fetches the disc and disc_gated files at their pinned revisions
#    (fetch_pinned.py checks every SHA-256);
# 2. converts each tissue adapter to the engine's GGUF adapter format with
#    llama.cpp's convert_lora_to_gguf.py (LLAMA_CPP points at a checkout);
# 3. adds a disc_gated model only when its engine path is listed in
#    ZXV_ENGINE_PATHS (e.g. "bert t5 styletts2"), meaning that path's
#    engine test passes; everything else stays in the first-run pack;
# 4. writes $DEST/golden.models: the upstream-pinned lines from
#    tools/tiamat/golden.models.tiamat plus a line per built file with the
#    hash computed here, and copies built files into $DEST/cache (ZXV_MODEL_CACHE) for
#    mk_golden_disc.sh (GOLDEN_MODELS=$DEST/golden.models).
# It never writes weights into the repository.
set -euo pipefail
DEST=${1:?usage: build_disc_models.sh DEST}
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
case "$(cd "$DEST" 2>/dev/null && pwd || echo "$DEST")/" in "$ROOT"/*) echo "DEST must be outside the repository"; exit 1;; esac
LLAMA_CPP=${LLAMA_CPP:?set LLAMA_CPP to a llama.cpp checkout}
PATHS=" ${ZXV_ENGINE_PATHS:-} "
mkdir -p "$DEST/models" "$DEST/adapters" "$DEST/cache"

python3 "$ROOT/tools/tiamat/server/fetch_pinned.py" --dest "$DEST/models" --placement disc disc_gated --source

cp "$ROOT/tools/tiamat/golden.models.tiamat" "$DEST/golden.models"
add() { # file -> golden line; built files have no upstream URL, so they must be in the cache
  local f=$1 n; n=$(basename "$f" | tr 'A-Z' 'a-z')
  cp "$f" "$DEST/cache/$n"
  printf '%s %s %s - Apache-2.0 -\n' "$n" "$(sha256sum "$f" | cut -d' ' -f1)" "$(stat -c %s "$f")" >> "$DEST/golden.models"
}

# adapters, one tissue at a time (extract_adapters.py must have run: task S3)
python3 - "$ROOT" <<'PY' > "$DEST/tissues.txt"
import json, sys
cfg = json.load(open(sys.argv[1] + "/tools/tiamat/tiamat_swarm.json"))
for base, t in cfg["tissues"].items():
    for c in t["cells"]:
        print(t["trunk"], c)
PY
while read -r trunk cell; do
  ad="$DEST/adapters/$cell"
  if [ ! -f "$ad/adapter_model.safetensors" ]; then
    echo "skip $cell: no adapter (FULL in the extract report, or S3 not run); it ships in a pack"; continue
  fi
  python3 "$LLAMA_CPP/convert_lora_to_gguf.py" --base "$DEST/models/$trunk/src" --outtype f16 \
      --outfile "$DEST/adapters/$(echo "$cell" | tr 'A-Z' 'a-z')-lora-f16.gguf" "$ad"
  add "$DEST/adapters/$(echo "$cell" | tr 'A-Z' 'a-z')-lora-f16.gguf"
done < "$DEST/tissues.txt"

# disc_gated and F16-converted disc models: only with their engine path
python3 - "$ROOT" <<'PY' > "$DEST/gated.txt"
import json, sys
m = json.load(open(sys.argv[1] + "/docs/release/swarm_model_manifest.json"))
for x in m["models"]:
    if x["placement"] == "disc_gated" or (x["placement"] == "disc" and not x["shipped_files"] and not x["trunk"]):
        print(x["dragon_name"], x["engine"].replace(" ", "_"))
PY
while read -r cell engine; do
  key=$(echo "$engine" | sed -E 's/^needs_//; s/_.*//; s/^zt_//' | tr 'A-Z' 'a-z')
  case "$PATHS" in
    *" $key "*) ;;
    *) echo "hold $cell: engine path '$key' not in ZXV_ENGINE_PATHS; stays in the pack"; continue;;
  esac
  src="$DEST/models/$cell/src"
  if [ "$key" = whisper ]; then  # zt_whisper reads bench/tensor/convert_whisper.py's GGUF
    python3 "$ROOT/bench/tensor/convert_whisper.py" --any-sha "$src" "$DEST/models/$cell/$(echo "$cell" | tr 'A-Z' 'a-z')-f16.gguf" \
      || { echo "hold $cell: convert_whisper.py does not read this checkpoint layout yet"; continue; }
    add "$DEST/models/$cell/$(echo "$cell" | tr 'A-Z' 'a-z')-f16.gguf"; continue
  fi
  python3 "$LLAMA_CPP/convert_hf_to_gguf.py" --outtype f16 \
      --outfile "$DEST/models/$cell/$(echo "$cell" | tr 'A-Z' 'a-z')-f16.gguf" "$src" \
    || { echo "hold $cell: llama.cpp cannot convert this architecture; needs a zt converter"; continue; }
  add "$DEST/models/$cell/$(echo "$cell" | tr 'A-Z' 'a-z')-f16.gguf"
done < "$DEST/gated.txt"

awk '!/^#/ {n++; b+=$3} END {printf "%d models, %.2f GB\n", n, b/1e9}' "$DEST/golden.models"
echo "next: GOLDEN_MODELS=$DEST/golden.models ZXV_MODEL_CACHE=$DEST/cache build_system/mk_golden_disc.sh"
