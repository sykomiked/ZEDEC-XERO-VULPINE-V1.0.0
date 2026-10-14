#!/bin/sh
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# build_edge.sh PROFILE OUTDIR [--smoke | --unit-only] — build the headless zxvd edge
# node package for one profile (packages/edge/profiles/PROFILE.conf: rpi5,
# jetson, minipc) and write OUTDIR/zxv-edge-PROFILE-VERSION-ARCH.tar.gz and
# its .sha256. Roadmap step R4.4.
#
# Rules:
#   1. zxvd is built by the existing kernel/arch/hosted/build_zxvd.sh,
#      unchanged; this script only chooses its environment (CC, ZXVD_AIPI).
#   2. aarch64 profiles cross-build with aarch64-linux-gnu-gcc unless CC is
#      set (on an aarch64 host, plain cc). Every link gets
#      -z separate-code -z noexecstack -z relro -z now: without
#      separate-code the aarch64 GNU linker puts zxvd in one RWX segment
#      (the writable .rodata.zxv_decl section lands beside the code), and
#      the unit's MemoryDenyWriteExecute needs W^X segments.
#   3. Edge nodes leave the AIPI sandbox out (ZXVD_AIPI=0) unless
#      EDGE_AIPI=1: an inference box does not need it, and less code is
#      less attack surface.
#   4. The tarball is packed to be reproducible: sorted names, mtime 0,
#      owner root, numeric ids, gzip -n. Byte-identical rebuilds of the
#      binaries themselves have not been verified.
#   5. --unit-only just renders OUTDIR/zxv-edge-PROFILE.service (tests).
#   6. --smoke runs the built zxvd (under qemu-ARCH for a foreign ARCH):
#      --version, then the daemon with its JSON-RPC socket, answering
#      zxvd.version, then stops it.
#
# Tarball layout (paths relative to /):
#   opt/zxv-edge/bin/zxvd, libzxv.so   the daemon and its ABI library (rpath $ORIGIN)
#   opt/zxv-edge/bin/zxv-edge-run      the launcher the unit runs
#   opt/zxv-edge/models/               put the profile's GGUF files here
#   opt/zxv-edge/install.sh            copies the tree into / (or $DESTDIR)
#   opt/zxv-edge/BUILDINFO             profile, arch, compiler, source revision
#   opt/zxv-edge/MANIFEST.sha256       hashes of every file above
#   opt/zxv-edge/EDGE_NODES.md         the guide (docs/retail/EDGE_NODES.md)
#   etc/zxv-edge/edge.conf             the profile config
#   lib/systemd/system/zxv-edge.service
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
R="$(cd "$HERE/../.." && pwd)"
PROFILE="${1:?usage: build_edge.sh PROFILE OUTDIR [--smoke]}"
OUT="${2:?usage: build_edge.sh PROFILE OUTDIR [--smoke]}"
SMOKE="${3:-}"
CONF="$HERE/profiles/$PROFILE.conf"
[ -f "$CONF" ] || { echo "[FAIL] no profile $CONF"; exit 2; }
sh "$HERE/zxv-edge-run" --check "$CONF" >/dev/null || { echo "[FAIL] profile $PROFILE does not validate"; exit 2; }

get() { sed -n "s/^$1=//p" "$CONF" | sed 's/^"//; s/"$//'; }
ARCH="$(get ARCH)"
MEMORY_MAX_MB="$(get MEMORY_MAX_MB)"
MEMORY_HIGH_MB="$(get MEMORY_HIGH_MB)"
THREADS="$(get THREADS)"
VERSION="$(sed -n 's/^#define ZXV_VERSION *"\(.*\)"/\1/p' "$R/kernel/arch/hosted/zxv_host.c")"
[ -n "$VERSION" ] || VERSION=unknown

render_unit() {
    sed -e "s/@PROFILE@/$PROFILE/g" -e "s/@MEMORY_MAX_MB@/$MEMORY_MAX_MB/g" \
        -e "s/@MEMORY_HIGH_MB@/$MEMORY_HIGH_MB/g" -e "s/@CPU_QUOTA@/$((THREADS * 100))/g" \
        "$HERE/zxv-edge.service.in" >"$1"
    ! grep -q '@[A-Z_]*@' "$1" || { echo "[FAIL] unit template not fully rendered"; exit 1; }
}
if [ "$SMOKE" = --unit-only ]; then
    mkdir -p "$OUT"
    render_unit "$OUT/zxv-edge-$PROFILE.service"
    exit 0
fi

HOSTARCH="$(uname -m)"
if [ -z "${CC:-}" ]; then
    if [ "$ARCH" = "$HOSTARCH" ]; then CC=cc; else CC="$ARCH-linux-gnu-gcc"; fi
fi
command -v "${CC%% *}" >/dev/null 2>&1 ||
    { echo "[FAIL] no compiler $CC (Debian/Ubuntu: apt-get install gcc-$ARCH-linux-gnu)"; exit 1; }
TRIPLE="$($CC -dumpmachine)"
case "$TRIPLE" in
    "$ARCH"-*) ;;
    *) echo "[FAIL] $CC builds for $TRIPLE, profile $PROFILE needs $ARCH"; exit 1 ;;
esac
STRIP="${STRIP:-}"
if [ -z "$STRIP" ]; then
    if command -v "$TRIPLE-strip" >/dev/null 2>&1; then STRIP="$TRIPLE-strip"; else STRIP=strip; fi
fi

mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"
W="$OUT/work-$PROFILE"
rm -rf "$W"
mkdir -p "$W"

# 1. zxvd through the unchanged build script (rules 1-3)
CC="$CC -Wl,-z,separate-code,-z,noexecstack,-z,relro,-z,now" ZXVD_AIPI="${EDGE_AIPI:-0}" \
    sh "$R/kernel/arch/hosted/build_zxvd.sh" "$W/build" >"$W/build.log" 2>&1 ||
    { tail -30 "$W/build.log"; echo "[FAIL] zxvd build for $PROFILE"; exit 1; }

# W^X: no segment may be both writable and executable
for f in zxvd libzxv.so; do
    if readelf -lW "$W/build/$f" | awk '$1 == "LOAD" && $(NF-1) ~ /W/ && $(NF-1) ~ /E/ { bad = 1 } END { exit !bad }'; then
        echo "[FAIL] $f has a writable and executable segment"; exit 1
    fi
done

# 2. stage the tree
S="$W/root"
B="$S/opt/zxv-edge/bin"
mkdir -p "$B" "$S/opt/zxv-edge/models" "$S/etc/zxv-edge" "$S/lib/systemd/system"
cp "$W/build/zxvd" "$W/build/libzxv.so" "$B/"
"$STRIP" --strip-unneeded "$B/zxvd" "$B/libzxv.so"
cp "$HERE/zxv-edge-run" "$B/zxv-edge-run"
cp "$CONF" "$S/etc/zxv-edge/edge.conf"
render_unit "$S/lib/systemd/system/zxv-edge.service"
cp "$HERE/install.sh" "$S/opt/zxv-edge/install.sh"
[ -f "$R/docs/retail/EDGE_NODES.md" ] && cp "$R/docs/retail/EDGE_NODES.md" "$S/opt/zxv-edge/EDGE_NODES.md"
REV="$(git -C "$R" rev-parse --short HEAD 2>/dev/null || echo unknown)"
DIRTY="$(git -C "$R" status --porcelain 2>/dev/null | head -1)"
{
    echo "profile=$PROFILE"
    echo "arch=$ARCH"
    echo "zxvd=$VERSION"
    echo "compiler=$($CC -dumpfullversion 2>/dev/null || $CC -dumpversion) ($TRIPLE)"
    echo "source=$REV${DIRTY:+ (uncommitted changes)}"
    echo "aipi=${EDGE_AIPI:-0}"
} >"$S/opt/zxv-edge/BUILDINFO"
chmod 0755 "$B/zxvd" "$B/zxv-edge-run" "$S/opt/zxv-edge/install.sh"
chmod 0644 "$B/libzxv.so" "$S/etc/zxv-edge/edge.conf" "$S/lib/systemd/system/zxv-edge.service" \
    "$S/opt/zxv-edge/BUILDINFO"
(cd "$S" && find . -type f ! -name MANIFEST.sha256 | LC_ALL=C sort | sed 's|^\./||' |
    xargs sha256sum >opt/zxv-edge/MANIFEST.sha256)

# 3. the tarball (rule 4)
NAME="zxv-edge-$PROFILE-$VERSION-$ARCH"
TAR="$OUT/$NAME.tar.gz"
(cd "$S" && LC_ALL=C tar --sort=name --mtime=@0 --owner=0 --group=0 --numeric-owner \
    -cf - opt etc lib) | gzip -n -9 >"$TAR"
(cd "$OUT" && sha256sum "$NAME.tar.gz" >"$NAME.tar.gz.sha256")
echo "[PASS] $TAR ($(wc -c <"$TAR") bytes)"

# 4. smoke test (rule 6)
[ "$SMOKE" = --smoke ] || exit 0
RUNNER=""
if [ "$ARCH" != "$HOSTARCH" ]; then
    command -v "qemu-$ARCH" >/dev/null 2>&1 || { echo "[SKIP] smoke: no qemu-$ARCH (apt-get install qemu-user)"; exit 0; }
    RUNNER="qemu-$ARCH -L /usr/$ARCH-linux-gnu"
fi
V="$($RUNNER "$B/zxvd" --version)"
[ "$V" = "zxvd $VERSION" ] || { echo "[FAIL] smoke: --version said '$V'"; exit 1; }
echo "[PASS] smoke: $V (${RUNNER:-native})"
T="$(mktemp -d "${TMPDIR:-/tmp}/zxv-edge-smoke.XXXXXX")"
mkdir -p "$T/models" "$T/run" "$T/state"
ZXV_EDGE_RUNTIME="$T/run" ZXV_EDGE_STATE="$T/state" sh "$B/zxv-edge-run" --print "$S/etc/zxv-edge/edge.conf" >"$T/argv"
grep -qx -- "--server" "$T/argv" && grep -qx -- "$T/run/zxvd.sock" "$T/argv" ||
    { cat "$T/argv"; echo "[FAIL] smoke: launcher command line"; exit 1; }
# the daemon itself: no network, no model, books in memory
$RUNNER "$B/zxvd" --server --notify off --rpc "$T/run/zxvd.sock" --books - --net off \
    --models-dir "$T/models" >"$T/zxvd.log" 2>&1 &
PID=$!
trap 'kill $PID 2>/dev/null || true; rm -rf "$T"' EXIT INT TERM
python3 - "$T/run/zxvd.sock" <<'EOF' || { cat "$T/zxvd.log"; echo "[FAIL] smoke: RPC"; exit 1; }
import json, socket, sys, time
path = sys.argv[1]
for _ in range(600):  # up to 60 s: qemu is slow to start a big binary
    try:
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.connect(path)
        break
    except OSError:
        time.sleep(0.1)
else:
    sys.exit("socket never appeared")
s.sendall(b'{"jsonrpc":"2.0","id":1,"method":"zxvd.version"}\n')
buf = b""
s.settimeout(30)
while not buf.endswith(b"\n"):
    chunk = s.recv(4096)
    if not chunk:
        break
    buf += chunk
r = json.loads(buf)
assert r.get("id") == 1 and "result" in r, r
assert r["result"]["protocol"] == "zxvd-rpc/1", r
print("[PASS] smoke: zxvd.version over the RPC socket ->", json.dumps(r["result"], sort_keys=True))
EOF
kill $PID 2>/dev/null || true
wait $PID 2>/dev/null || true
echo "[PASS] smoke: $PROFILE daemon started, answered and stopped"
