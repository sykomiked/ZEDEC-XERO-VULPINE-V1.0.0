#!/bin/bash
# riscv32_sbi.sh — locate (or build, or install) the rv32 SBI firmware that
# qemu-system-riscv32 -M virt requires BEFORE it will load our kernel.
#
# WHY THIS EXISTS
# ---------------
# The long-standing matrix entry "riscv32: 97 bytes of output -- DEAD" was never
# a kernel defect. Those 97 bytes are QEMU's own message, printed before the ELF
# is opened:
#     qemu-system-riscv32: Unable to load the RISC-V firmware
#     "opensbi-riscv32-generic-fw_dynamic.bin"
# QEMU's riscv32 virt machine boots an SBI firmware and then hands control to the
# supervisor payload. Ubuntu 22.04's `opensbi` package and `qemu-system-data`
# both ship riscv64 firmware ONLY, so the default -bios cannot resolve and QEMU
# aborts with exit 1. Not one kernel instruction ever runs.
#
# NO FIXED VALUES
# ---------------
# This script hardcodes neither the firmware filename nor the directory. It ASKS
# the tools:
#   * the FILENAME comes from QEMU itself -- we run it once with no firmware and
#     read the name out of its own error message;
#   * the DIRECTORIES come from `qemu-system-riscv32 -L help`, which prints the
#     data path this specific build searches;
#   * the CROSS COMPILER comes from $CROSS_COMPILE (same variable the Makefile
#     uses), and the OpenSBI SOURCE tree is searched for, not assumed.
# Every candidate is VERIFIED to be a 32-bit RISC-V object before it is accepted,
# so a riscv64 firmware sitting under a riscv32 name cannot be mistaken for one.
#
# MODES
#   path     print an absolute path to a usable rv32 firmware (building it if a
#            source tree is available). Prints nothing and exits 0 if QEMU's own
#            default already resolves -- caller then needs no -bios at all.
#   force    like `path` but always prints a path, even when the default works.
#   install  additionally copy it into QEMU's data directory under the exact name
#            QEMU asked for, so the plain documented command
#              qemu-system-riscv32 -M virt -m 512 -nographic -kernel k.elf
#            works with no flags. Needs write access there (uses sudo -n if the
#            directory is not writable and passwordless sudo is available).
#   check    report status only; exit 0 if the default already resolves.
#
# Author: H.M. Michael-Laurence: Curzi (c)
# License: Apache-2.0
set -u

MODE=${1:-path}
QEMU=${QEMU_RISCV32:-qemu-system-riscv32}
CROSS_COMPILE=${CROSS_COMPILE:-riscv64-linux-gnu-}

say() { echo "riscv32_sbi: $*" >&2; }
die() { say "$*"; exit 1; }

command -v "$QEMU" >/dev/null 2>&1 || die "no $QEMU on this host"

# ---------------------------------------------------------------- ask QEMU ----
# Start the machine with no kernel and paused (-S). If the firmware is missing
# QEMU exits 1 immediately and names the file it wanted. If it is present QEMU
# sits paused until the timeout kills it (exit 124) -- that IS the success probe.
probe() {
    timeout 5 "$QEMU" -M virt -display none -serial none -monitor none -S 2>&1
}
PROBE_OUT=$(probe); PROBE_RC=$?

# The name QEMU asked for, quoted in its own diagnostic. Never assumed.
WANT=$(printf '%s\n' "$PROBE_OUT" | sed -n 's/.*firmware "\([^"]*\)".*/\1/p' | head -1)

DEFAULT_OK=0
if [ "$PROBE_RC" -eq 124 ] || [ "$PROBE_RC" -eq 0 ]; then
    DEFAULT_OK=1
fi

if [ "$MODE" = check ]; then
    if [ "$DEFAULT_OK" = 1 ]; then
        say "OK: $QEMU resolves its default SBI firmware; no -bios needed"
        exit 0
    fi
    say "MISSING: $QEMU wants '${WANT:-<unknown>}' and cannot find it (exit $PROBE_RC)"
    exit 1
fi

if [ "$DEFAULT_OK" = 1 ] && [ "$MODE" = path ]; then
    exit 0        # nothing to print: the bare command already works
fi

# The name is only unknown when QEMU did not complain, i.e. the default worked.
# Fall back to QEMU's documented naming scheme only for the install target name.
[ -n "$WANT" ] || WANT="opensbi-riscv32-generic-fw_dynamic.bin"

# QEMU's own search path, straight from the binary.
DATADIRS=$("$QEMU" -L help 2>/dev/null | sed 's/[[:space:]]*$//' | grep -v '^$')

# ------------------------------------------------------------- verify a bin ---
# Accept a firmware only if it really is 32-bit RISC-V. A raw .bin has no ELF
# header, so we check the sibling .elf the OpenSBI build emits; if there is no
# sibling we refuse rather than guess -- a riscv64 firmware under a riscv32 name
# would fail in a far more confusing way than "not found".
is_rv32_fw() {
    local bin=$1 elf
    [ -r "$bin" ] || return 1
    elf="${bin%.bin}.elf"
    [ -r "$elf" ] || return 1
    "${CROSS_COMPILE}readelf" -h "$elf" 2>/dev/null \
        | awk '/Class:/{c=$2} /Machine:/{m=$0} END{exit !(c=="ELF32" && m ~ /RISC-V/)}'
}

# --------------------------------------------------------------- find one -----
FOUND=""
try() { [ -z "$FOUND" ] && is_rv32_fw "$1" && FOUND=$1; return 0; }

# 1) explicit override wins (an operator who knows their host)
[ -n "${ZXV_RV32_SBI:-}" ] && try "$ZXV_RV32_SBI"

# 2) already installed under QEMU's own name in QEMU's own search path
for d in $DATADIRS; do try "$d/$WANT"; done

# 3) distro layout for a genuinely 32-bit opensbi package, whatever it is called
for d in /usr/lib/riscv32*/opensbi/generic /usr/lib/*/opensbi/generic \
         /usr/share/opensbi/*/generic/firmware; do
    try "$d/fw_dynamic.bin"
done

# 4) any OpenSBI build tree on this host
for d in ${OPENSBI_SRC:-} "$PWD/opensbi" "$PWD/../opensbi" "$HOME/opensbi" /opt/opensbi; do
    [ -n "$d" ] && try "$d/build/platform/generic/firmware/fw_dynamic.bin"
done

# --------------------------------------------------------------- build one ----
if [ -z "$FOUND" ]; then
    SRC=""
    for d in ${OPENSBI_SRC:-} "$PWD/opensbi" "$PWD/../opensbi" "$HOME/opensbi" /opt/opensbi; do
        [ -n "$d" ] && [ -r "$d/Makefile" ] && [ -d "$d/platform/generic" ] && { SRC=$d; break; }
    done
    [ -n "$SRC" ] || die "no rv32 SBI firmware and no OpenSBI source tree found.
  Looked for an installed firmware in: $(echo $DATADIRS | tr '\n' ' ')
  Set ZXV_RV32_SBI=/path/to/fw_dynamic.bin, or OPENSBI_SRC=/path/to/opensbi and
  re-run. Without it qemu-system-riscv32 -M virt cannot start AT ALL, and the
  97-byte 'Unable to load the RISC-V firmware' abort is NOT a kernel failure."

    say "building rv32 OpenSBI from $SRC (PLATFORM_RISCV_XLEN=32)"
    # XLEN=32 is the whole point; the ABI/ISA are OpenSBI's own defaults for a
    # 32-bit generic platform, and FW_TEXT_START is left at the platform default
    # so the firmware lands where QEMU's virt machine puts DRAM.
    ( cd "$SRC" && make -j"$(nproc 2>/dev/null || echo 2)" \
        PLATFORM=generic PLATFORM_RISCV_XLEN=32 \
        CROSS_COMPILE="$CROSS_COMPILE" >/dev/null ) \
        || die "OpenSBI rv32 build failed in $SRC"
    try "$SRC/build/platform/generic/firmware/fw_dynamic.bin"
    [ -n "$FOUND" ] || die "OpenSBI built but produced no 32-bit fw_dynamic.bin"
fi

# --------------------------------------------------------------- install -----
if [ "$MODE" = install ]; then
    TARGET_DIR=""
    for d in $DATADIRS; do [ -d "$d" ] && { TARGET_DIR=$d; break; }; done
    [ -n "$TARGET_DIR" ] || die "QEMU reported no usable data directory"
    DEST="$TARGET_DIR/$WANT"
    if [ -w "$TARGET_DIR" ]; then
        cp -f "$FOUND" "$DEST" && chmod 0644 "$DEST" || die "install to $DEST failed"
    elif sudo -n true 2>/dev/null; then
        sudo install -m 0644 "$FOUND" "$DEST" || die "sudo install to $DEST failed"
    else
        say "cannot write $TARGET_DIR and no passwordless sudo."
        say "run manually:  sudo install -m644 '$FOUND' '$DEST'"
        say "or boot with:  -bios '$FOUND'"
        echo "$FOUND"
        exit 0
    fi
    say "installed $FOUND -> $DEST"
    probe >/dev/null 2>&1; RC2=$?
    if [ "$RC2" -eq 124 ] || [ "$RC2" -eq 0 ]; then
        say "verified: $QEMU now resolves its default firmware with no -bios"
    else
        say "WARNING: installed, but $QEMU still does not resolve the default"
    fi
    echo "$DEST"
    exit 0
fi

echo "$FOUND"
