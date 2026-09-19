#!/bin/bash
# gamemaster_campaign.sh — run the Game Master fault-stress campaign over the ROM
# corpus, SHARDED across workers (each ROM is independent), resumable, honest.
#
# It refuses to start unless gamedrive.py --preflight PASSES (no bomb-squad margin
# markers => every run would falsely classify CLEAN). It never counts an
# unsupported-format SKIP as a pass. Logs are JSONL, one record per ROM event.
#
# Usage (on the Linux box, from ~/zxv):
#   bash build_system/gamemaster_campaign.sh CORPUS_DIR ELF OUT_DIR [WORKERS] [TIMEOUT_S]
# Example:
#   bash build_system/gamemaster_campaign.sh ~/gamemaster/GAMEMASTER kernel_arm64.elf \
#        ~/gamemaster/run 10 25
#
# Review afterwards:   python3 build_system/gamedrive.py --report OUT_DIR
set -u
CORPUS="${1:?need CORPUS_DIR}"
ELF="${2:-kernel_arm64.elf}"
OUT="${3:-gamemaster_run}"
WORKERS="${4:-$(( $(nproc 2>/dev/null || echo 4) - 2 ))}"
TIMEOUT="${5:-25}"
[ "$WORKERS" -lt 1 ] && WORKERS=1
HERE="$(cd "$(dirname "$0")/.." && pwd)"   # repo root
GD="$HERE/build_system/gamedrive.py"

echo "== Game Master campaign =="
echo "  corpus : $CORPUS"
echo "  elf    : $ELF"
echo "  out    : $OUT"
echo "  workers: $WORKERS   timeout/rom: ${TIMEOUT}s"
echo

# ---- 1. HONESTY GATE: preflight must PASS or we do not run --------------------
echo "[preflight] $GD --preflight"
if ! python3 "$GD" --preflight --corpus "$CORPUS" --elf "$ELF" ; then
  echo "PREFLIGHT FAILED — refusing to start (would produce false results). Fix the above."
  exit 1
fi
echo

mkdir -p "$OUT"
SHARDROOT="$OUT/shards"; rm -rf "$SHARDROOT"; mkdir -p "$SHARDROOT"

# ---- 2. build the candidate list (supported exts + zip containers) -----------
echo "[shard] enumerating candidates..."
python3 - "$CORPUS" "$SHARDROOT" "$WORKERS" <<'PY'
import os, sys
corpus, shardroot, workers = sys.argv[1], sys.argv[2], int(sys.argv[3])
EXT = {'.nes','.gb','.pce','.smc','.sfc','.gba','.md','.gen','.zip'}  # zip resolved at run time
cands = []
for root,_,fs in os.walk(corpus):
    for f in fs:
        if os.path.splitext(f)[1].lower() in EXT:
            cands.append(os.path.join(root, f))
cands.sort()
# round-robin into WORKERS shard dirs of symlinks (stable => resumable by re-run)
dirs = []
for i in range(workers):
    d = os.path.join(shardroot, f'shard_{i:02d}'); os.makedirs(d, exist_ok=True); dirs.append(d)
for n,p in enumerate(cands):
    d = dirs[n % workers]
    link = os.path.join(d, f'{n:07d}_' + os.path.basename(p))
    try:
        if not os.path.lexists(link): os.symlink(os.path.abspath(p), link)
    except OSError: pass
print(f'  candidates: {len(cands)}   shards: {workers}')
PY
echo

# ---- 3. launch one gamedrive worker per shard, in parallel -------------------
echo "[run] launching $WORKERS workers ($(date +%H:%M:%S))..."
pids=()
for d in "$SHARDROOT"/shard_*; do
  s="$(basename "$d")"
  ( python3 "$GD" --corpus "$d" --elf "$ELF" --out "$OUT/$s" --era all --timeout "$TIMEOUT" \
       > "$OUT/$s.worker.log" 2>&1 ) &
  pids+=($!)
done
echo "  worker pids: ${pids[*]}"
echo "  live progress:  tail -f $OUT/shard_00/gamedrive.jsonl"
wait "${pids[@]}"
echo "[run] all workers finished ($(date +%H:%M:%S))"
echo

# ---- 4. merge JSONL + report -------------------------------------------------
cat "$OUT"/shard_*/gamedrive.jsonl > "$OUT/campaign.jsonl" 2>/dev/null
echo "[merge] $OUT/campaign.jsonl  ($(wc -l < "$OUT/campaign.jsonl" 2>/dev/null || echo 0) records)"
echo
echo "== REPORT =="
# report reads a run dir with a gamedrive.jsonl; point it at a merged copy
mkdir -p "$OUT/_merged"; cp "$OUT/campaign.jsonl" "$OUT/_merged/gamedrive.jsonl"
python3 "$GD" --report "$OUT/_merged"
echo
echo "Full JSONL: $OUT/campaign.jsonl  — re-run this script to RESUME (stable shard symlinks)."
