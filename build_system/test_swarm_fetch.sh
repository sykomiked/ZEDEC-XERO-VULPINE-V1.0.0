#!/usr/bin/env bash
# test_swarm_fetch.sh — prove the bare-metal network fetch of a swarm disc.
#
#   bash build_system/test_swarm_fetch.sh [seconds]
#
# Builds the release leaves first, small (docs/BUCKET.md): the swarm disc (a
# 2 MiB model in group "test"), then the x86_64 kernel with that disc's CID
# baked in (baremetal_images.sh with ZXV_SWARM_DISCS, mk_table.py), then the
# core disc (a 3 MiB core model); lays them out as a bucket with mk_bucket.py
# --blocks, serves the blocks from a local gateway on the host
# (zxv_test_gateway.py; the guest sees it as 10.0.2.2), and rebuilds the core
# disc so its /ZXV/UPSTREAM.CID names that bucket and gateway. Then boots the
# core disc under UEFI with a blank 400 MiB AHCI drive (sparse) and no swarm disc:
#
#   1. network up, honest gateway: [NET] link up, DHCP lease, [SWARM_FETCH],
#      [SWARM_FETCHED] (sha256 matches the table; committed), then the disc
#      is indexed again: [SWARM_MOUNTED] ZXV_SWARM_TEST. Then the seal, with
#      no "seal" line (PROMPT_USER) and nobody at the console: the question,
#      no answer in 30 s, the NIC powered down and read back, the hydration
#      code overwritten, and the receipt:
#        [SEAL] Swarm Discs 2..2 successfully hydrated and verified.
#        [SEAL] Master CID: <the bucket>
#        [SEAL] Network hydration channel severed: NIC bus mastering disabled.
#        [SEAL] Operating system is now immutable and air-gapped.
#      after which both [MODEL_OK] and [MODELS_OK] 2 verified still come
#   2. a gateway that changes every image block: it is not asked again, and
#      [SWARM_FETCH_FAIL] ... nothing committed; no [SWARM_FETCHED], no
#      [SWARM_MOUNTED], no seal
#   3. link up, nobody answers DHCP: three attempts, then the air-gap lines
#      [NET] Link down or no DHCP response.
#      [SWARM] Falling back to local storage scan: Attach Disc 2 USB / drive.
#   4. no NIC at all: the same two lines
#   5. "seal LOCAL_MESH_ONLY" on the core disc: fetched again, and the seal
#      keeps the NIC for the local mesh and says what it removed
#
# Then elastic (swarmnet.h, ELASTIC), on a blank drive the guest may write:
#   6. the gateway drops out mid-download for 20 s: the fetch waits for peers,
#      1 s, 2 s, 4 s ... apart, and finishes once it is back, all after
#      [BOOT_OK], with the progress bar
#   7. the gateway drops out for good; d at the console stops the fetch and
#      says what stays on the drive
#   8. a reboot with the gateway back: the fetch resumes where the drive's
#      record says, re-reads and hashes what is there, finishes and seals
#
# Every boot reaches [BOOT_OK] and [AI_OK]. Publishes nothing: the bucket and
# the gateway exist only on this machine. Runs in Dockerfile.baremetal.
set -euo pipefail
cd "$(dirname "$0")/.."

SECS="${1:-150}"
ARCH=x86_64
T="dist/discs/.swarm-fetch-test"
PREFIX="zxv-sftest"
IMG="dist/discs/$PREFIX-$ARCH-disc1-core.img"
SWARM="dist/discs/$PREFIX-swarm-disc2-test.img"
PORT="${SWARM_FETCH_PORT:-18$(( RANDOM % 900 + 100 ))}"
rm -rf "$T"; mkdir -p "$T/cache" "$T/discs"
GW=

cleanup(){ [ -n "$GW" ] && kill "$GW" 2>/dev/null || true; }
trap cleanup EXIT

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

golden(){ # $1 target, $2 upstream file
  GOLDEN_MODELS="$T/models" GOLDEN_CORE="$T/core" GOLDEN_SWARM="$T/swarm" \
    GOLDEN_CONTENTS="$T/contents" GOLDEN_PREFIX="$PREFIX" ZXV_MODEL_CACHE="$T/cache" \
    GOLDEN_UPSTREAM="$2" GOLDEN_OFFLINE=1 bash build_system/mk_golden_disc.sh "$1"
}

# Leaves first: the swarm disc, then a kernel that carries its CID.
golden swarm build_system/zxv_upstream.cid
ZXV_SWARM_DISCS="$SWARM" bash build_system/baremetal_images.sh "$ARCH"
grep -q "^#define ZXV_TABLE_SWARM_N 1u" build/include/manifest_hashes.h
# The bucket: the swarm disc and a first core disc (the bucket's own core disc
# cannot name the bucket that holds it; mk_bucket.py says why).
golden "$ARCH" build_system/zxv_upstream.cid
cp "$IMG" "$SWARM" "$T/discs/"
python3 build_system/mk_bucket.py --discs "$T/discs" --prefix "$PREFIX" --out "$T/bucket" \
  --blocks "$T/blocks"
ROOT=$(cat "$T/bucket.cid")
grep -q "^swarm all 2 TEST " "$T"/bucket/b*.install

# The core disc under test names that bucket and the local gateway.
printf 'cid %s\ngateway http://10.0.2.2:%s\n' "$ROOT" "$PORT" > "$T/upstream"
golden "$ARCH" "$T/upstream"
truncate -s 400M "$T/blank.img"

gateway(){ # extra args for zxv_test_gateway.py
  cleanup
  python3 build_system/zxv_test_gateway.py "$T/blocks" "$PORT" "$@" 2> "$T/gateway.log" &
  GW=$!
  for _ in $(seq 50); do
    python3 -c "import socket,sys; socket.create_connection(('127.0.0.1', $PORT), 1)" 2>/dev/null && return
    sleep 0.1
  done
  echo "::error::the local gateway did not start"; cat "$T/gateway.log"; exit 1
}
boot(){ bash build_system/uefi_boot_check.sh "$ARCH" "$IMG" "$SECS" "[AI_OK]" "$@"; }
never(){ for m in "$@"; do
  if grep -aqF -- "$m" "uefi_boot_${ARCH}.log"; then echo "::error::printed $m"; exit 1; fi
done; }
export UEFI_DISK=ahci UEFI_EXTRA="$T/blank.img"

echo "==> boot 1: the swarm disc fetched from the local gateway onto the blank drive"
gateway
UEFI_NET=user boot \
  "[SWARM_MISSING] disc 2 ZXV_SWARM_TEST" \
  "[NET] DHCP discover, attempt 1 of 3" \
  "[NET] DHCP lease 10.0.2.15 mask 255.255.255.0 gateway 10.0.2.2 dns 10.0.2.3" \
  "[SWARM_FETCH] disc 2 ZXV_SWARM_TEST -> " \
  "[SWARM_FETCHED] disc 2 ZXV_SWARM_TEST on " \
  "[SWARM_MOUNTED] ZXV_SWARM_TEST on " \
  "[MODEL_OK] test-core.gguf $core_bytes bytes, sha256 matches" \
  "[MODEL_OK] test-swarm.gguf $swarm_bytes bytes, sha256 matches" "[MODELS_OK] 2 verified" \
  "[SEAL] Swarm discs hydrated. Seal the network: press p to power the NIC down" \
  "[SEAL] No answer: powering the NIC down." \
  ": PHY powered down; PCI Bus Master and Memory Space cleared and read back" \
  "bytes overwritten with INT3, read back; fetch buffers and network state: " \
  "[SEAL] Swarm Discs 2..2 successfully hydrated and verified." \
  "[SEAL] Master CID: $ROOT" \
  "[SEAL] Network hydration channel severed: NIC bus mastering disabled." \
  "[SEAL] Operating system is now immutable and air-gapped."
never "[SEAL_FAIL]"
grep -aE '\[NET\] eth0 = .*, link up, MAC ' "uefi_boot_${ARCH}.log" > /dev/null || {
  echo "::error::no [NET] link-up line"; exit 1; }

echo "==> boot 2: a gateway that changes every image block"
gateway --tamper-over 65536
UEFI_NET=user boot \
  "[SWARM_FETCH] gateway 10.0.2.2: sent a block that does not match its CID; not asked again this boot" \
  "[SWARM_FETCH_FAIL] disc 2 ZXV_SWARM_TEST: every gateway sent a block that does not match its CID; nothing committed"
never "[SWARM_FETCHED]" "[SWARM_MOUNTED]" "[MODELS_OK] 2" "[SEAL"

echo "==> boot 3: link up, no DHCP answer"
cleanup; GW=
UEFI_NET=silent boot "[NET] DHCP discover, attempt 3 of 3" \
  "[NET] Link down or no DHCP response." \
  "[SWARM] Falling back to local storage scan: Attach Disc 2 USB / drive."
never "[NET] DHCP lease" "[SWARM_FETCH" "[SEAL"

echo "==> boot 4: no NIC"
UEFI_NET=none boot "[NET] no NIC the kernel drives" \
  "[NET] Link down or no DHCP response." \
  "[SWARM] Falling back to local storage scan: Attach Disc 2 USB / drive."
never "[SEAL"

echo "==> boot 5: seal LOCAL_MESH_ONLY"
printf 'cid %s\ngateway http://10.0.2.2:%s\nseal LOCAL_MESH_ONLY\n' "$ROOT" "$PORT" > "$T/upstream"
golden "$ARCH" "$T/upstream"
gateway
UEFI_NET=user boot "[SWARM_FETCHED] disc 2 ZXV_SWARM_TEST on " \
  "[SEAL] Swarm Discs 2..2 successfully hydrated and verified." \
  "[SEAL] Network hydration channel severed: WAN routes, DHCP and the fetcher removed; eth0 kept for the local mesh." \
  "[SEAL] Operating system is now immutable; its network is the local mesh only." \
  "[MODELS_OK] 2 verified"
never "[SEAL_NIC]" "air-gapped" "[SEAL_FAIL]" "Seal the network: press"

printf 'cid %s\ngateway http://10.0.2.2:%s\n' "$ROOT" "$PORT" > "$T/upstream"
golden "$ARCH" "$T/upstream"
rm -f "$T/blank.img"; truncate -s 400M "$T/blank.img"
export UEFI_EXTRA_RW=1

echo "==> boot 6: the gateway drops out for 20 s mid-download"
gateway --down-after 2000000 --down-for 20
UEFI_NET=user boot \
  "[SWARM] The core is up; the swarm discs fill in the background. Press d at any time to attach them by hand instead." \
  "[SWARM_FETCH] disc 2: waiting for peers (no gateway served the next block); retry 1 in 1 s; " \
  "retry 2 in 2 s; " "retry 3 in 4 s; " \
  "[SWARM_FETCH] disc 2 [#" " KB/s)" \
  "[SWARM_FETCHED] disc 2 ZXV_SWARM_TEST on " "[SWARM_MOUNTED] ZXV_SWARM_TEST on " \
  "[SEAL] Operating system is now immutable and air-gapped."
never "[SWARM_FETCH_FAIL]" "[SEAL_FAIL]"
grep -aq "back up" "$T/gateway.log" || { echo "::error::the gateway never came back"; exit 1; }
LOGF="uefi_boot_${ARCH}.log"
[ "$(grep -anF '[BOOT_OK]' "$LOGF" | head -1 | cut -d: -f1)" -lt \
  "$(grep -anF '[SWARM_FETCHED]' "$LOGF" | head -1 | cut -d: -f1)" ] || {
  echo "::error::the disc was not fetched after the boot finished"; exit 1; }

echo "==> boot 7: the gateway drops out for good; d stops the fetch"
rm -f "$T/blank.img"; truncate -s 400M "$T/blank.img"
gateway --down-after 3000000 # of the ~4 MB of distinct blocks the test disc has
rm -f "$T/keys"; mkfifo "$T/keys"
: > "$LOGF"
( exec 3> "$T/keys"
  for _ in $(seq "$SECS"); do grep -aq "retry 3 in 4 s" "$LOGF" 2>/dev/null && break; sleep 1; done
  printf d >&3; sleep "$SECS" ) &
KEYS=$!
UEFI_STDIN="$T/keys" UEFI_NET=user boot \
  "[SWARM_FETCH] disc 2: waiting for peers" \
  "[SWARM] Hydration stopped at your request: Attach Disc 2 USB / drive." \
  " bytes of disc 2 stay on " " and resume on a later boot with a network"
kill "$KEYS" 2>/dev/null || true
never "[SWARM_FETCHED]" "[SEAL"
KEPT=$(grep -aoE '\[SWARM\] [0-9]+ bytes of disc 2 stay on' "$LOGF" | grep -oE '[0-9]+' | head -1)
[ "$KEPT" -ge 1048576 ] || { echo "::error::only $KEPT bytes kept"; exit 1; }

echo "==> boot 8: a reboot with the gateway back resumes from the drive"
gateway
UEFI_NET=user boot \
  "[SWARM_FETCH] disc 2 ZXV_SWARM_TEST: resuming on " " at $KEPT of " \
  "[SWARM_FETCH] disc 2 ZXV_SWARM_TEST: $KEPT bytes re-read from " \
  "[SWARM_FETCHED] disc 2 ZXV_SWARM_TEST on " "[MODEL_OK] test-swarm.gguf $swarm_bytes bytes" \
  "[SEAL] Operating system is now immutable and air-gapped."
never "[SWARM_FETCH_FAIL]" "downloading it again from the start" "[SEAL_FAIL]"
unset UEFI_EXTRA_RW

rm -rf "$T" "$IMG" "$SWARM" "${IMG%.img}.sizes" "${SWARM%.img}.sizes"
echo "[swarm-fetch] fetched and committed on a match, sealed (NIC off, or mesh only), nothing committed from a lying gateway, air-gap lines without DHCP or NIC; waited out a gateway that dropped, stopped on d, resumed after a reboot"
