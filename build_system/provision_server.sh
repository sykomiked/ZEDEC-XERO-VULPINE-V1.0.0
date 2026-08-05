#!/usr/bin/env bash
# provision_server.sh — bring a blank Linux box up to a full ZXV build host.
#
# Idempotent: safe to re-run. Designed for a time-boxed session, so it
# installs everything needed for BOTH targets in one apt transaction and
# verifies each tool before we depend on it.
#
#   scp this file to the server (or paste it), then:  bash provision_server.sh
#
# Verified afterwards by verify_toolchain.sh.
set -euo pipefail

log() { printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }

log "OS / arch"
uname -a
. /etc/os-release 2>/dev/null && echo "distro: ${PRETTY_NAME:-unknown}"

SUDO=""
if [ "$(id -u)" -ne 0 ]; then SUDO="sudo"; fi

log "apt update"
$SUDO apt-get update -y

log "install toolchains + emulation + build tooling (one transaction)"
DEBIAN_FRONTEND=noninteractive $SUDO apt-get install -y --no-install-recommends \
  build-essential \
  gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu \
  gcc-x86-64-linux-gnu binutils-x86-64-linux-gnu \
  gcc-riscv64-linux-gnu binutils-riscv64-linux-gnu \
  qemu-system-arm qemu-system-x86 qemu-system-misc qemu-utils \
  nasm \
  clang llvm lld \
  make cmake pkg-config \
  git python3 python3-pip \
  xxd file bc \
  grub-common grub-pc-bin xorriso mtools \
  ca-certificates curl

log "versions"
gcc --version | head -1
aarch64-linux-gnu-gcc --version | head -1
x86_64-linux-gnu-gcc --version | head -1 || true
riscv64-linux-gnu-gcc --version | head -1
clang --version | head -1
qemu-system-aarch64 --version | head -1
qemu-system-x86_64 --version | head -1
qemu-system-riscv64 --version | head -1
nasm --version
python3 --version
git --version

log "sanity: cross-compile + link a freestanding AArch64 object"
tmp=$(mktemp -d)
cat > "$tmp/t.c" <<'EOF'
int _start(void){ return 0; }
EOF
aarch64-linux-gnu-gcc -ffreestanding -nostdlib -c "$tmp/t.c" -o "$tmp/t.o"
aarch64-linux-gnu-objdump -d "$tmp/t.o" | head -5
rm -rf "$tmp"

log "sanity: QEMU aarch64 virt machine + GICv3 available"
qemu-system-aarch64 -M virt,gic-version=3 -cpu cortex-a53 -nographic -kernel /dev/null 2>&1 | head -3 || true

log "PROVISION COMPLETE"
echo "Next: bash build_system/verify_toolchain.sh"
