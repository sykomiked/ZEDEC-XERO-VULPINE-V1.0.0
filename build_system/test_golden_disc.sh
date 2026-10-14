#!/usr/bin/env bash
# test_golden_disc.sh — prove the golden disc's boot-time model check, fast.
#
#   bash build_system/baremetal_images.sh <arch>
#   bash build_system/test_golden_disc.sh <arch> [seconds]
#
# Builds a small golden disc (mk_golden_disc.sh) whose one "model" is a 3 MiB
# test file, so no real weights are needed, then boots it twice under UEFI:
#
#   1. as built: the log must show [MODEL_OK] and [MODELS_OK] 1 verified
#   2. with one byte of the model changed inside the disc: the log must show
#      [MODEL_BAD] and [MODELS_FAIL], and never [MODELS_OK]
#
# Both boots must still reach [BOOT_OK] and [AI_OK]: a bad model is reported,
# it does not stop the OS. Runs in build_system/Dockerfile.baremetal.
set -euo pipefail
cd "$(dirname "$0")/.."

ARCH="${1:?usage: test_golden_disc.sh <x86_64|arm64> [seconds]}"
SECS="${2:-60}"
T="dist/discs/.golden-test-$ARCH"
NAME="zxv-$ARCH-golden-test"
IMG="dist/discs/$NAME.img"
rm -rf "$T"; mkdir -p "$T/cache"

# A deterministic 3 MiB file starting with the GGUF magic.
python3 - "$T/cache/test-model.gguf" <<'EOF'
import sys
data = bytearray(b"GGUF" + bytes(4))
x = 2463534242
while len(data) < 3 << 20:
    x ^= (x << 13) & 0xFFFFFFFF; x ^= x >> 17; x ^= (x << 5) & 0xFFFFFFFF
    data += x.to_bytes(4, "little")
open(sys.argv[1], "wb").write(data[:3 << 20])
EOF
sha=$(sha256sum "$T/cache/test-model.gguf" | cut -d' ' -f1)
bytes=$(stat -c %s "$T/cache/test-model.gguf")
echo "test-model.gguf $sha $bytes - Apache-2.0" > "$T/models"
printf 'LICENSE ZXV/LICENSES/LICENSE\n' > "$T/contents"

GOLDEN_MODELS="$T/models" GOLDEN_CONTENTS="$T/contents" GOLDEN_NAME="$NAME" \
  ZXV_MODEL_CACHE="$T/cache" GOLDEN_OFFLINE=1 bash build_system/mk_golden_disc.sh "$ARCH"

echo "==> boot 1: intact model"
bash build_system/uefi_boot_check.sh "$ARCH" "$IMG" "$SECS" "[AI_OK]" \
  "[MODEL_OK] test-model.gguf $bytes bytes, sha256 matches" "[MODELS_OK] 1 verified"

echo "==> boot 2: one byte of the model changed on the disc"
python3 - "$T/cache/test-model.gguf" "$T/bad.gguf" <<'EOF'
import sys
d = bytearray(open(sys.argv[1], "rb").read()); d[1 << 20] ^= 0x01
open(sys.argv[2], "wb").write(d)
EOF
mcopy -o -i "$IMG@@1M" "$T/bad.gguf" ::/ZXV/MODELS/test-model.gguf
bash build_system/uefi_boot_check.sh "$ARCH" "$IMG" "$SECS" "[AI_OK]" \
  "[MODEL_BAD] test-model.gguf $bytes bytes, sha256 or size differs" "[MODELS_FAIL] 1 of 1 models bad"
if grep -aqF '[MODELS_OK]' "uefi_boot_${ARCH}.log"; then
  echo "::error::$ARCH: the changed model still printed [MODELS_OK]"; exit 1
fi

rm -rf "$T" "$IMG" "dist/discs/$NAME.sizes"
echo "[golden-test] $ARCH: intact model verified on boot, changed model caught"
