#!/usr/bin/env bash
# test_golden_disc.sh — prove the golden disc's boot path, fast: the loader
# verifies the model, hands it to the kernel, and the kernel runs it.
#
#   bash build_system/baremetal_images.sh <arch>
#   bash build_system/test_golden_disc.sh <arch> [seconds]
#
# Builds a small golden disc (mk_golden_disc.sh) whose one model is the tiny
# real qwen2 GGUF from mk_boot_test_model.py (120 KB, generated, so no real
# weights are needed), then boots it twice under UEFI:
#
#   1. as built: [MODEL_OK] and [MODELS_OK] 1 verified, [AI_HANDOFF], and
#      the kernel's [AI_OK] line with the answer's token digest that
#      test_swarm_bootmodel pins on the host (TEST_MODEL_DIGEST): bare metal
#      computed the same tokens, bit for bit;
#   2. with one byte of the model changed inside the disc: [MODEL_BAD] and
#      [MODELS_FAIL], no handoff, the kernel says [AI_NO_MODEL], and never
#      [MODELS_OK] or [AI_OK].
#
# Both boots must still reach [BOOT_OK] and [AI_SELFCHECK_OK]: a bad model is
# reported, it does not stop the OS. Runs in build_system/Dockerfile.baremetal.
set -euo pipefail
cd "$(dirname "$0")/.."

ARCH="${1:?usage: test_golden_disc.sh <x86_64|arm64> [seconds]}"
SECS="${2:-60}"
T="dist/discs/.golden-test-$ARCH"
NAME="zxv-$ARCH-golden-test"
IMG="dist/discs/$NAME.img"
MODEL=zxv-boot-test.gguf
rm -rf "$T"; mkdir -p "$T/cache"

python3 build_system/mk_boot_test_model.py "$T/cache/$MODEL"
digest=$(sed -n 's/^#define TEST_MODEL_DIGEST *\(0x[0-9a-f]*\)u$/\1/p' kernel/src/swarm/test_swarm_bootmodel.c)
gen=$(sed -n 's/^#define TEST_MODEL_GEN *\([0-9]*\)u$/\1/p' kernel/src/swarm/test_swarm_bootmodel.c)
[ -n "$digest" ] && [ -n "$gen" ] || { echo "::error::no pinned digest in test_swarm_bootmodel.c"; exit 1; }
sha=$(sha256sum "$T/cache/$MODEL" | cut -d' ' -f1)
bytes=$(stat -c %s "$T/cache/$MODEL")
echo "$MODEL $sha $bytes - Apache-2.0" > "$T/models"
printf 'LICENSE ZXV/LICENSES/LICENSE\n' > "$T/contents"

GOLDEN_MODELS="$T/models" GOLDEN_CONTENTS="$T/contents" GOLDEN_NAME="$NAME" \
  ZXV_MODEL_CACHE="$T/cache" GOLDEN_OFFLINE=1 bash build_system/mk_golden_disc.sh "$ARCH"

echo "==> boot 1: intact model, the kernel runs it"
bash build_system/uefi_boot_check.sh "$ARCH" "$IMG" "$SECS" "[AI_SELFCHECK_OK]" \
  "[MODEL_OK] $MODEL $bytes bytes, sha256 matches" "[MODELS_OK] 1 verified" \
  "[AI_HANDOFF] $MODEL in memory for the kernel" \
  "[AI_MODEL] $MODEL $bytes bytes, kernel sha256 ${sha:0:16} agrees with the loader" \
  "[AI_OK] $MODEL answered" "+ $gen tokens, digest $digest)"

echo "==> boot 2: one byte of the model changed on the disc"
python3 - "$T/cache/$MODEL" "$T/bad.gguf" <<'EOF'
import sys
d = bytearray(open(sys.argv[1], "rb").read()); d[len(d) // 2] ^= 0x01
open(sys.argv[2], "wb").write(d)
EOF
mcopy -o -i "$IMG@@1M" "$T/bad.gguf" ::/ZXV/MODELS/$MODEL
bash build_system/uefi_boot_check.sh "$ARCH" "$IMG" "$SECS" "[AI_SELFCHECK_OK]" \
  "[MODEL_BAD] $MODEL $bytes bytes, sha256 or size differs" "[MODELS_FAIL] 1 of 1 models bad" \
  "[AI_NO_MODEL] no model handed over by the loader; the AI is not running"
for m in '[MODELS_OK]' '[AI_OK]' '[AI_HANDOFF]'; do
  if grep -aqF "$m" "uefi_boot_${ARCH}.log"; then
    echo "::error::$ARCH: the changed model still printed $m"; exit 1
  fi
done

rm -rf "$T" "$IMG" "dist/discs/$NAME.sizes"
echo "[golden-test] $ARCH: intact model handed over and answered (digest $digest), changed model caught"
