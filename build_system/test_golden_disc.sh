#!/usr/bin/env bash
# test_golden_disc.sh — prove the golden disc's model check, fast.
#
#   bash build_system/baremetal_images.sh <arch>
#   bash build_system/test_golden_disc.sh <arch> [seconds]
#
# Builds small golden discs (mk_golden_disc.sh) with two test "models", so no
# real weights are needed: test-core.gguf (3 MiB) on the core disc and
# test-swarm.gguf (2 MiB) on a swarm disc, group "test". Then boots twice
# under UEFI:
#
#   x86_64, both discs on AHCI; the kernel reads them after boot:
#     1. as built: [SWARM_MOUNTED] ZXV_SWARM_TEST, [MODELS_INDEXED] 2 listed
#        (1 on the boot partition, 1 on 1 swarm disc), [MODEL_OK] for both,
#        [MODELS_OK] 2 verified
#     2. one byte of the swarm model changed on its disc: [MODEL_BAD] for it,
#        [MODELS_FAIL] 1 of 2, never [MODELS_OK]
#   arm64, whose loader checks the core disc (its kernel has no PCI storage
#   driver yet, so no swarm disc is attached):
#     1. as built: [MODEL_OK] test-core, [MODELS_OK] 1 verified
#     2. one byte of the core model changed: [MODEL_BAD], [MODELS_FAIL] 1 of 1
#
# The core disc carries the release layers L0 to L2 built for these lists
# (mk_layers.py, docs/LAYERS.md). On x86_64 boot 1 also shows the lineage
# checked before [BOOT_OK] ([LAYERS_OK]), and a third boot has an L1 that
# lists another boot file put on the disc: L2 no longer stands on it, so
# [LAYERS_FAIL] and both models are refused before anything hashes them.
#
# Every boot must still reach [BOOT_OK] and [AI_OK]: a bad model is reported,
# it does not stop the OS. Runs in build_system/Dockerfile.baremetal.
set -euo pipefail
cd "$(dirname "$0")/.."

ARCH="${1:?usage: test_golden_disc.sh <x86_64|arm64> [seconds]}"
SECS="${2:-60}"
T="dist/discs/.golden-test-$ARCH"
PREFIX="zxv-gtest-$ARCH"
IMG="dist/discs/$PREFIX-$ARCH-disc1-core.img"
SWARM="dist/discs/$PREFIX-swarm-disc2-test.img"
rm -rf "$T"; mkdir -p "$T/cache"

# Deterministic files starting with the GGUF magic.
python3 - "$T/cache" <<'PY'
import sys
def make(path, n, seed):
    data = bytearray(b"GGUF" + bytes(4))
    x = seed
    while len(data) < n:
        x ^= (x << 13) & 0xFFFFFFFF; x ^= x >> 17; x ^= (x << 5) & 0xFFFFFFFF
        data += x.to_bytes(4, "little")
    open(path, "wb").write(data[:n])
make(sys.argv[1] + "/test-core.gguf", 3 << 20, 2463534242)
make(sys.argv[1] + "/test-swarm.gguf", 2 << 20, 88675123)
PY
: > "$T/models"
for m in test-core test-swarm; do
  f="$T/cache/$m.gguf"
  echo "$m.gguf $(sha256sum "$f" | cut -d' ' -f1) $(stat -c %s "$f") - Apache-2.0" >> "$T/models"
done
core_bytes=$(stat -c %s "$T/cache/test-core.gguf")
swarm_bytes=$(stat -c %s "$T/cache/test-swarm.gguf")
echo "test-core.gguf" > "$T/core"
echo "test test-swarm.gguf" > "$T/swarm"
printf 'LICENSE ZXV/LICENSES/LICENSE\n' > "$T/contents"

make -s build-all-layers ARCHS="$ARCH" LAYERS="$T/layers" GOLDEN_MODELS="$T/models" \
  GOLDEN_CORE="$T/core" GOLDEN_SWARM="$T/swarm"
for target in "$ARCH" swarm; do
  GOLDEN_MODELS="$T/models" GOLDEN_CORE="$T/core" GOLDEN_SWARM="$T/swarm" \
    GOLDEN_CONTENTS="$T/contents" GOLDEN_PREFIX="$PREFIX" ZXV_MODEL_CACHE="$T/cache" \
    GOLDEN_LAYERS="$T/layers" GOLDEN_OFFLINE=1 bash build_system/mk_golden_disc.sh "$target"
done

# Change one byte of a model inside a disc image: $1 the model, $2 the
# image, $3 its path on the image's first partition.
corrupt(){
  python3 - "$T/cache/$1" "$T/bad.gguf" <<'PY'
import sys
d = bytearray(open(sys.argv[1], "rb").read()); d[1 << 20] ^= 0x01
open(sys.argv[2], "wb").write(d)
PY
  local first
  first=$(sgdisk -i 1 "$2" | awk '/^First sector:/ {print $3}')
  mcopy -o -i "$2@@$((first * 512))" "$T/bad.gguf" "::/$3"
}

case "$ARCH" in
  x86_64)
    export UEFI_DISK=ahci UEFI_EXTRA="$SWARM"
    echo "==> boot 1: intact models, read by the kernel from the core and swarm discs"
    bash build_system/uefi_boot_check.sh "$ARCH" "$IMG" "$SECS" "[AI_OK]" \
      "[LAYER] L0 HARDWARE_ENVELOPE $(cat "$T/layers/L0.cid")" \
      "[LAYER] L1 CORE_BOOT_ENGINE $(cat "$T/layers/L1.cid")" \
      "[LAYER] L2 SWARM $(cat "$T/layers/L2.cid")" \
      "[LAYERS_OK] L0 -> L1 -> L2: each names the one below; BOOTX64.EFI and 2 models match their layers" \
      "[SWARM_MOUNTED] ZXV_SWARM_TEST on " \
      "[MODELS_INDEXED] 2 listed (1 on the boot partition, 1 on 1 swarm disc)" \
      "[MODEL_OK] test-core.gguf $core_bytes bytes, sha256 matches" \
      "[MODEL_OK] test-swarm.gguf $swarm_bytes bytes, sha256 matches" "[MODELS_OK] 2 verified"
    echo "==> boot 2: one byte of the swarm model changed on its disc"
    corrupt test-swarm.gguf "$SWARM" MODELS/MDL001.GGU
    bash build_system/uefi_boot_check.sh "$ARCH" "$IMG" "$SECS" "[AI_OK]" \
      "[MODEL_OK] test-core.gguf $core_bytes bytes, sha256 matches" \
      "[MODEL_BAD] test-swarm.gguf (ZXV_SWARM_TEST:MDL001.GGU) sha256 differs from MODELS.SHA" \
      "[LAYERS_OK] L0 -> L1 -> L2" "[MODELS_FAIL] 1 of 2 models bad"
    echo "==> boot 3: an L1 for another boot file, so L2 no longer stands on it"
    printf 'another kernel' > "$T/BOOTX64.EFI"
    python3 build_system/mk_layers.py l1 --out "$T/l1b" --parent "$(cat "$T/layers/L0.cid")" \
      --boot "x86_64=$T/BOOTX64.EFI" --models "$T/models" --core "$T/core"
    first=$(sgdisk -i 1 "$IMG" | awk '/^First sector:/ {print $3}')
    mcopy -o -i "$IMG@@$((first * 512))" "$T/l1b/L1.json" "::/ZXV/LAYER1.JSN"
    bash build_system/uefi_boot_check.sh "$ARCH" "$IMG" "$SECS" "[AI_OK]" \
      "[LAYERS_FAIL] LAYER2.JSN: names a parent that is not the layer below" \
      "[MODEL_BAD] test-core.gguf (esp:MDL000.GGU) its disc's layers do not check out" \
      "[MODEL_BAD] test-swarm.gguf (ZXV_SWARM_TEST:MDL001.GGU) its disc's layers do not check out" \
      "[MODELS_FAIL] 2 of 2 models bad"
    if grep -aqF '[LAYERS_OK]' "uefi_boot_${ARCH}.log"; then
      echo "::error::$ARCH: the changed layer still printed [LAYERS_OK]"; exit 1
    fi ;;
  arm64)
    echo "==> boot 1: intact model, checked by the loader"
    bash build_system/uefi_boot_check.sh "$ARCH" "$IMG" "$SECS" "[AI_OK]" \
      "[MODEL_OK] test-core.gguf $core_bytes bytes, sha256 matches" "[MODELS_OK] 1 verified"
    echo "==> boot 2: one byte of the core model changed on the disc"
    corrupt test-core.gguf "$IMG" ZXV/MODELS/MDL000.GGU
    bash build_system/uefi_boot_check.sh "$ARCH" "$IMG" "$SECS" "[AI_OK]" \
      "[MODEL_BAD] test-core.gguf $core_bytes bytes, sha256 or size differs" \
      "[MODELS_FAIL] 1 of 1 models bad" ;;
  *) echo "test_golden_disc: unknown arch '$ARCH' (x86_64|arm64)"; exit 2 ;;
esac
if grep -aqF '[MODELS_OK]' "uefi_boot_${ARCH}.log"; then
  echo "::error::$ARCH: the changed model still printed [MODELS_OK]"; exit 1
fi

rm -rf "$T" "$IMG" "$SWARM" "${IMG%.img}.sizes" "${SWARM%.img}.sizes"
echo "[golden-test] $ARCH: intact models verified, changed model caught$([ "$ARCH" = x86_64 ] &&
  echo ", layers L0 -> L1 -> L2 verified before BOOT_OK, a changed layer refused")"
