#!/usr/bin/env bash
# ipfs_dist.sh — the release bucket, built twice from clean and compared.
#
#   make ipfs-dist [ARCHS="x86_64 arm64"] [ARCH_DOC=docs/ARCHITECTURE.md]
#   bash build_system/ipfs_dist.sh [arch ...]
#
# Each pass removes the release discs (dist/discs/zxv-*), clean-builds the
# kernel and EFI loader for every arch (baremetal_images.sh), builds the core
# disc per arch and the swarm discs (mk_golden_disc.sh), and lays them out as
# the bucket with mk_bucket.py, the architecture document's distribution
# section copied in as 00_base/ARCHITECTURE.md. The run fails unless both
# passes give the same SHA-256 for every Disc 1 and the same root CID: a
# release whose CID depends on when it was built cannot be checked by anyone
# else. The second pass's bucket is the result:
#
#   build/ipfs-dist/bucket/         the directory to add and pin
#   build/ipfs-dist/bucket.cid      its root CID, as `ipfs add -r --cid-version=1` gives
#   build/ipfs-dist/pass{1,2}.sums  "sha256 file" per core disc, then "root <cid>"
#
# Model weights come from ZXV_MODEL_CACHE (dist/models), downloaded once from
# their pinned URLs when missing; GOLDEN_OFFLINE=1 forbids that. Nothing is
# added to an IPFS node, uploaded or pinned: that stays the owner's step.
# Runs in build_system/Dockerfile.baremetal.
set -euo pipefail
cd "$(dirname "$0")/.."

ARCHS=("$@")
[ ${#ARCHS[@]} -gt 0 ] || ARCHS=(x86_64 arm64)
DOC="${ARCH_DOC:-docs/ARCHITECTURE.md}"
OUT=build/ipfs-dist
log(){ echo "[ipfs-dist] $*"; }
die(){ echo "::error::ipfs-dist: $*" >&2; exit 1; }
for a in "${ARCHS[@]}"; do
  case "$a" in x86_64|arm64) ;; *) die "no core disc for '$a' (x86_64|arm64)" ;; esac
done
[ -f "$DOC" ] || die "$DOC missing"
grep -qF '<!-- zxv:distribution:start -->' "$DOC" ||
  die "$DOC has no distribution section (the zxv:distribution markers); set ARCH_DOC"

rm -rf "$OUT"; mkdir -p "$OUT"
for pass in 1 2; do
  log "pass $pass: clean build"
  rm -f dist/discs/zxv-*-disc1-core.img dist/discs/zxv-*-disc1-core.sizes \
        dist/discs/zxv-swarm-disc*.img dist/discs/zxv-swarm-disc*.sizes
  bash build_system/baremetal_images.sh "${ARCHS[@]}"
  for a in "${ARCHS[@]}"; do bash build_system/mk_golden_disc.sh "$a"; done
  bash build_system/mk_golden_disc.sh swarm
  python3 build_system/mk_bucket.py --discs dist/discs --out "$OUT/pass$pass" \
    --architecture "$DOC"
  { for a in "${ARCHS[@]}"; do
      (cd dist/discs && sha256sum "zxv-$a-disc1-core.img"); done
    echo "root $(cat "$OUT/pass$pass.cid")"
  } > "$OUT/pass$pass.sums"
done

if ! cmp -s "$OUT/pass1.sums" "$OUT/pass2.sums"; then
  diff "$OUT/pass1.sums" "$OUT/pass2.sums" || true
  die "the two clean passes differ: the build is not reproducible"
fi
rm -rf "$OUT/pass1" "$OUT/pass1.cid"
mv "$OUT/pass2" "$OUT/bucket"
mv "$OUT/pass2.cid" "$OUT/bucket.cid"
cat "$OUT/pass2.sums"
log "two clean passes agree; bucket $OUT/bucket, root $(cat "$OUT/bucket.cid")"
log "nothing was added to IPFS, uploaded or pinned"
