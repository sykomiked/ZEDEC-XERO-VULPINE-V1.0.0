#!/bin/bash
# mk_roms.sh — assemble the BOOTABLE ROM deliverable, both carriers, per arch.
#
# The kernel integrates its TVUL MegaROM INTERNALLY at boot (tvl_rom_register_boot(),
# built from the same build_probe() that authors MEGAROM.TVL — "one derivation, two
# carriers"). So the two ROM forms we ship are:
#
#   1. FUSED ROM  (<arch>.rom): the arch kernel image with the authored MEGAROM.TVL
#      embedded as a non-alloc `.megarom` section. It is a SINGLE self-contained file
#      that (a) boots the OS whose UI *is* the MegaROM via the arch's VERIFIED boot
#      command, and (b) physically carries the world cartridge for external tools.
#      Non-alloc => the load image and entry point are unchanged; boot is byte-identical
#      to the plain kernel. The running kernel builds the same world internally; the
#      embedded copy is proven equal (sha256) to what mk_megarom authored.
#
#   2. CARTRIDGE  (megarom-world.rom == MEGAROM.TVL): the standalone, validated TVUL
#      world blob — the shareable/loadable "cartridge". Arch-independent (one world).
#
# HONESTY: this tool does NOT rebuild or relink kernels and invents no boot capability.
# It only carries an already-validated blob alongside already-verified kernels. Whether
# each fused ROM actually reaches BOOT_OK is decided by boot_roms_test (below / the Mac
# harness), not asserted here.
#
# Usage:  bash build_system/mk_roms.sh [DELIVERABLE_DIR]     (default: ./deliverable)
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEL="${1:-$ROOT/deliverable}"
OBJCOPY="${OBJCOPY:-/opt/homebrew/opt/llvm/bin/llvm-objcopy}"
[ -x "$OBJCOPY" ] || OBJCOPY="$(command -v llvm-objcopy || command -v gobjcopy || command -v objcopy)"
[ -n "$OBJCOPY" ] || { echo "no objcopy (need llvm-objcopy) — cannot fuse"; exit 2; }

TVL="$DEL/discs/MEGAROM.TVL"
[ -f "$TVL" ] || { echo "missing authored world: $TVL (run mk_efi_disc.sh first)"; exit 2; }
OUT="$DEL/roms"; rm -rf "$OUT"; mkdir -p "$OUT"

# arch  -> "kernel_elf|qemu|extra qemu args (for -kernel boot); RV uses OpenSBI -bios"
rows=(
  "arm64|kernel_arm64.elf|qemu-system-aarch64|-M virt,gic-version=3 -cpu cortex-a72"
  "x86_64|kernel_x86_64.elf|qemu-system-x86_64|"
  "arm32|zedec_xero_arm32.elf|qemu-system-arm|-M virt -cpu cortex-a15"
  "riscv64|kernel_riscv.elf|qemu-system-riscv64|-M virt -bios discs/firmware/opensbi-riscv64-generic-fw_dynamic.bin"
  "riscv32|kernel_riscv32.elf|qemu-system-riscv32|-M virt -bios discs/firmware/opensbi-riscv32-generic-fw_dynamic.bin"
)

tvl_sha=$(shasum -a 256 "$TVL" | awk '{print $1}')
echo "== mk_roms =="
echo "  world cartridge : MEGAROM.TVL ($(wc -c <"$TVL"|tr -d ' ') bytes)  sha256 $tvl_sha"
echo "  objcopy         : $OBJCOPY"
echo

# shared cartridge at the root of roms/
cp "$TVL" "$OUT/megarom-world.rom"

MAN="$OUT/ROMS_MANIFEST.txt"
{
  echo "ZXV / ZEDEC pqOS — bootable ROM deliverable"
  echo "world cartridge : megarom-world.rom  ($(wc -c <"$TVL"|tr -d ' ') bytes)  sha256 $tvl_sha"
  echo "  (byte-identical to the world each kernel builds internally at boot)"
  echo
  echo "ARCH      FUSED_ROM                 BYTES     SHA256                                                           EMBEDDED_WORLD_OK"
} > "$MAN"

for row in "${rows[@]}"; do
  IFS='|' read -r arch kelf qemu qargs <<<"$row"
  # PREFER the MegaROM-WIRED kernel. discs/<arch>/ holds the later build whose
  # kernel_main actually calls tvl_rom_register_boot() (it prints "[MEGAROM] slot N");
  # deliverable/ may hold an EARLIER kernel built before that wiring landed. Fusing the
  # blob onto a kernel that never registers a world would carry bytes and prove nothing,
  # so we pick by MEASURED wiring, not by directory convention.
  src=""
  for cand in "$DEL/discs/$arch/$kelf" "$DEL/$kelf"; do
    [ -f "$cand" ] || continue
    if strings -a "$cand" 2>/dev/null | grep -q 'MEGAROM'; then src="$cand"; break; fi
    [ -z "$src" ] && src="$cand"        # fallback: remember the first existing one
  done
  if [ -z "$src" ] || [ ! -f "$src" ]; then echo "  [$arch] SKIP — kernel not found: $kelf"; continue; fi
  wired=$(strings -a "$src" 2>/dev/null | grep -c 'MEGAROM')
  echo "  [$arch] source: ${src#$DEL/}  (megarom-wired strings=$wired)"
  d="$OUT/$arch"; mkdir -p "$d"
  fused="$d/$arch.rom"

  # entry BEFORE, to prove we didn't move it
  e0=$("$OBJCOPY" --dump-section .nonexistent=/dev/null "$src" /dev/null 2>/dev/null; /opt/homebrew/opt/llvm/bin/llvm-readelf -h "$src" 2>/dev/null | awk -F'0x' '/Entry/{print "0x"$2}')

  # fuse: add the authored world as a NON-ALLOC section (load image untouched)
  "$OBJCOPY" --add-section .megarom="$TVL" \
             --set-section-flags .megarom=readonly \
             "$src" "$fused" 2>/dev/null || { echo "  [$arch] objcopy FAILED"; continue; }

  # verify: section present, contents equal to the authored world, entry unchanged
  e1=$(/opt/homebrew/opt/llvm/bin/llvm-readelf -h "$fused" 2>/dev/null | awk -F'0x' '/Entry/{print "0x"$2}')
  "$OBJCOPY" --dump-section .megarom="$d/.check.tvl" "$fused" /dev/null 2>/dev/null
  got=$(shasum -a 256 "$d/.check.tvl" 2>/dev/null | awk '{print $1}'); rm -f "$d/.check.tvl"
  wok="NO"; [ "$got" = "$tvl_sha" ] && wok="YES"
  entryok="unchanged"; [ "$e0" != "$e1" ] && entryok="CHANGED($e0->$e1)"

  # per-arch cartridge copy + a boot script that uses the VERIFIED command
  cp "$TVL" "$d/megarom-world.rom"
  cat > "$d/boot.sh" <<SH
#!/bin/bash
# Boot the FUSED ROM for $arch with the verified command. The .megarom section is
# carried (non-alloc); the kernel builds the identical world internally at boot.
cd "\$(dirname "\$0")/../.."          # -> deliverable/
exec $qemu $qargs -m 512 -nographic -kernel roms/$arch/$arch.rom
SH
  chmod +x "$d/boot.sh"

  fsha=$(shasum -a 256 "$fused" | awk '{print $1}')
  # HONEST per-arch note: does THIS kernel register the world itself at boot?
  if [ "$wired" -gt 0 ]; then
    note="kernel registers world at boot ([MEGAROM] slot N)"
  else
    note="world registers via the UEFI path (BOOT*.EFI/boot_features_init), NOT this ELF"
  fi
  printf "%-9s %-25s %-9s %s  %s  %s\n" "$arch" "$arch.rom" "$(wc -c <"$fused"|tr -d ' ')" "$fsha" "$wok" "$note" >> "$MAN"
  echo "  [$arch] fused $arch.rom  entry=$entryok  embedded-world=$wok"
done

# ---- boot verification: run what CAN run here, record the real result -------------
# A fused ROM that does not boot is not a ROM. We boot each one with its own boot.sh
# and record BOOT_OK + whether the MegaROM registration line appeared. Anything this
# host cannot run is marked SKIP(host) — never PASS.
{
  echo
  echo "BOOT VERIFICATION (this host: $(uname -s) $(uname -m), qemu $(qemu-system-aarch64 --version 2>/dev/null | head -1 | grep -oE '[0-9]+\.[0-9]+' | head -1))"
  echo "ARCH      BOOT_OK  MEGAROM_LINE  NOTE"
} >> "$MAN"
for row in "${rows[@]}"; do
  IFS='|' read -r arch kelf qemu qargs <<<"$row"
  rom="$OUT/$arch/$arch.rom"; [ -f "$rom" ] || continue
  if ! command -v "$qemu" >/dev/null 2>&1 && [ ! -x "/opt/homebrew/bin/$qemu" ]; then
    printf "%-9s %-8s %-13s %s\n" "$arch" "SKIP" "SKIP" "$qemu not installed on this host" >> "$MAN"; continue
  fi
  Q="$(command -v "$qemu" 2>/dev/null || echo "/opt/homebrew/bin/$qemu")"
  o="$OUT/$arch/boot.log"
  ( cd "$DEL" && $Q $qargs -m 512 -nographic -kernel "roms/$arch/$arch.rom" > "$o" 2>&1 ) & p=$!
  ( sleep 15; kill -9 $p 2>/dev/null ) & k=$!; wait $p 2>/dev/null; kill -9 $k 2>/dev/null
  bo=$(grep -ac 'BOOT_OK' "$o" 2>/dev/null); ml=$(grep -ac 'MEGAROM' "$o" 2>/dev/null)
  n=""; [ "$bo" -eq 0 ] && n="did NOT reach BOOT_OK on this host — see roms/$arch/boot.log"
  printf "%-9s %-8s %-13s %s\n" "$arch" "$([ "$bo" -ge 1 ] && echo YES || echo NO)" "$([ "$ml" -ge 1 ] && echo YES || echo no)" "$n" >> "$MAN"
  printf "  [%-7s] boot: BOOT_OK=%s MEGAROM=%s\n" "$arch" "$([ "$bo" -ge 1 ] && echo YES || echo NO)" "$([ "$ml" -ge 1 ] && echo YES || echo no)"
done

echo
echo "== ROM deliverable at: $OUT =="
find "$OUT" -type f | sort | sed "s#$OUT/#    #"
echo
cat "$MAN"
