#!/usr/bin/env bash
# golden_login_check.sh — the golden disc's whole path on arm64, under stress:
# boot, the boot model answers, log in (or create a passport) at the console,
# ask the model, log out.
#
#   bash build_system/baremetal_images.sh arm64
#   bash build_system/golden_login_check.sh [seconds per boot]
#
# Builds the same small golden disc as test_golden_disc.sh (the tiny real
# qwen2 GGUF from mk_boot_test_model.py) and boots it under UEFI four times,
# QEMU's serial wired to build_system/golden_login_drive.py, which types at
# the EL0 shell and checks every answer:
#
#   full      512 MB: refused before login, a typo refused, logged in as the
#             DID the host's test_pp_console builds from the same seed, the
#             boot question answered with the digest test_swarm_bootmodel
#             pins, logged out; a passport created, a wrong confirmation
#             refused, the right one logged in and answered; then hostile
#             lines, mistyped seed positions and an overlong line refused,
#             five logins and ten asks repeated with the same digest
#   nomodel   the plain disc (no model): login works, ask says so, never answers
#   lowmem    256 MB, just above the kernel's own floor: the boot reaches the
#             shell and says why the AI is off if it is
#   toosmall  128 MB, below the kernel's image + BSS: the loader says so
#             ([BOOT_FAIL]) and stops instead of handing off
#
# No typed seed may reach the serial log (the shell echoes seeds as '*'; the
# new seed is shown once, by create), and no boot may print a fault marker.
# create needs the CPU's RNDR, so QEMU runs -cpu max. Runs in
# build_system/Dockerfile.baremetal.
set -euo pipefail
cd "$(dirname "$0")/.."

ARCH=arm64
SECS="${1:-300}"
T="dist/discs/.golden-login-$ARCH"
NAME="zxv-$ARCH-golden-login"
IMG="dist/discs/$NAME.img"
MODEL=zxv-boot-test.gguf
LOG="golden_login_${ARCH}.log"
rm -rf "$T"; mkdir -p "$T/cache"

make -s -C kernel test-pp-console ZXV_TMP="$PWD/$T" > /dev/null
seed=$(sed -n 's/^  seed text \(.*\)$/\1/p' "$T/test_pp_console.log")
did=$(sed -n 's/^  did \(did:zxv:.*\)$/\1/p' "$T/test_pp_console.log")
[ -n "$seed" ] && [ -n "$did" ] || { echo "::error::test_pp_console printed no seed or DID"; exit 1; }
digest=$(sed -n 's/^#define TEST_MODEL_DIGEST *\(0x[0-9a-f]*\)u$/\1/p' kernel/src/swarm/test_swarm_bootmodel.c)
question=$(sed -n 's/^#define SWARM_BOOTMODEL_QUESTION *"\(.*\)"$/\1/p' kernel/src/swarm/swarm_bootmodel.h)
[ -n "$digest" ] && [ -n "$question" ] || { echo "::error::no pinned digest or boot question"; exit 1; }

python3 build_system/mk_boot_test_model.py "$T/cache/$MODEL"
sha=$(sha256sum "$T/cache/$MODEL" | cut -d' ' -f1)
bytes=$(stat -c %s "$T/cache/$MODEL")
echo "$MODEL $sha $bytes - Apache-2.0" > "$T/models"
printf 'LICENSE ZXV/LICENSES/LICENSE\n' > "$T/contents"
GOLDEN_MODELS="$T/models" GOLDEN_CONTENTS="$T/contents" GOLDEN_NAME="$NAME" \
  ZXV_MODEL_CACHE="$T/cache" GOLDEN_OFFLINE=1 bash build_system/mk_golden_disc.sh "$ARCH"

FW=
for f in "${AAVMF:-}" /usr/share/qemu-efi-aarch64/QEMU_EFI.fd /usr/share/AAVMF/AAVMF_CODE.fd; do
  [ -n "$f" ] && [ -f "$f" ] && { FW=$f; break; }
done
[ -n "$FW" ] || { echo "::error::no arm64 UEFI firmware (install qemu-efi-aarch64)"; exit 1; }

QEMU=(qemu-system-aarch64 -M virt,gic-version=3 -cpu max -bios "$FW"
      -net none -display none -serial stdio -monitor none)
disc(){ echo -drive "if=none,id=disc,format=raw,snapshot=on,file=$1" \
             -device virtio-blk-pci,drive=disc,bootindex=0; }
drive(){ # scenario image memory
  python3 build_system/golden_login_drive.py "$1" "$SECS" "golden_login_${ARCH}_$1.log" \
    "$seed" "$did" "$digest" "$question" -- "${QEMU[@]}" -m "$3" $(disc "$2")
}

drive full "$IMG" 512M
# The plain disc (baremetal_images.sh) carries no model.
drive nomodel "dist/discs/zxv-$ARCH-uefi.img" 512M
# Just above the kernel's own floor (about 235 MB on arm64), then below it.
drive lowmem "$IMG" 256M
drive toosmall "$IMG" 128M

rm -rf "$T" "$IMG" "dist/discs/$NAME.sizes"
