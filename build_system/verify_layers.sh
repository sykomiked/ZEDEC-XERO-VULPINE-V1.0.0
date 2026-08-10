#!/bin/bash
# verify_layers.sh — DRC for the kernel. Four checks, all mechanical.
#
# In silicon nobody eyeballs metal spacing; a design-rule checker does it. The
# layer discipline needs the same treatment, because conventions in this tree
# have a measured survival time of HOURS -- ROADMAP_TO_COMPLETE.md contradicted
# SERVER_HANDOVER.md within a day of being written.
#
#   1 LAYER      a module may not include a header from a HIGHER layer
#   2 ACYCLIC    the requires-graph must have no cycle (a cycle never boots)
#   3 PROVIDED   every REQUIRES must be PROVIDED by something
#   4 CONTRACT   two providers of one capability must agree on version
#
# Checks 2-4 are enforced at runtime by modbind_verify_graph(); this is the
# static half, so a violation fails the BUILD rather than the boot.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT" || exit 2
fail=0

# Layer map. A module not listed is UNASSIGNED -- reported, not failed, because
# assigning 278 files by guess would be worse than admitting the gap.
layer_of() {
  case "$1" in
    # sha256 is a PURE FUNCTION over bytes with no dependencies -- substrate,
    # not trust. L3 is where POLICY about hashes lives (capabilities,
    # verification, signing); the primitive itself belongs beside zphi and rat.
    # The DRC found this on its first run: alloc(L2), zxvfs_tri(L2) and
    # invproof(L1) all include it, and all three were correct. The layer
    # assignment was wrong, not the code.
    zphi|rat|rational|e8|mixmat|surplus|crit168|sha256) echo 0 ;;
    oseq|identity|invproof)                      echo 1 ;;
    mm|alloc|zxvfs)                              echo 2 ;;
    # NOTE: zxvfs_tri is NOT L2. Plain zxvfs is storage; zxvfs_tri is
    # CAPABILITY-GATED storage -- it calls zab to decide who may execute a
    # triad, which makes it a trust-layer module that happens to persist. The
    # DRC caught this by finding it including zab.h from L2. The distinction is
    # real and I had collapsed it; per-FILE classification is needed here
    # because one directory holds modules at two different layers.
    zxvfs_tri)                                   echo 3 ;;
    mlkem|zab|modbind|tls|decent)                echo 3 ;;
    virtio|video|input|net)                      echo 4 ;;
    display|codec|bombsquad|rce|chiglet)         echo 5 ;;
    vino|vino_stores|finance_markets|license|iso20022|crown|ministry) echo 6 ;;
    desktop|shell|apps|sutra|appkit|xedit)       echo 7 ;;
    *) echo -1 ;;
  esac
}

echo "verify_layers: DRC over kernel/src"
unassigned=0
while IFS= read -r f; do
  fb=$(basename "$f" .c)
  ml=$(layer_of "$fb")                      # per-file first
  [ "$ml" = "-1" ] && ml=$(layer_of "$(basename "$(dirname "$f")")")
  [ "$ml" = "-1" ] && { unassigned=$((unassigned+1)); continue; }
  # every local include, resolved to its owning directory
  grep -oE '#include[[:space:]]*"[^"]+\.h"' "$f" 2>/dev/null |
  sed 's/.*"\(.*\)"/\1/' | while IFS= read -r inc; do
    base=$(basename "$inc" .h)
    # find which module directory actually owns that header
    owner=$(find kernel/src -name "$base.h" -maxdepth 3 2>/dev/null | head -1)
    [ -z "$owner" ] && continue
    om=$(basename "$(dirname "$owner")")
    ol=$(layer_of "$om")
    [ "$ol" = "-1" ] && continue
    if [ "$ol" -gt "$ml" ]; then
      echo "  LAYER VIOLATION  $f (L$ml) includes $inc (L$ol)"
      echo "violation" >> /tmp/vl_fail.$$
    fi
  done
done < <(find kernel/src -name '*.c' ! -name 'test_*' ! -name 'debug_*' 2>/dev/null)

[ -f /tmp/vl_fail.$$ ] && { fail=$(wc -l < /tmp/vl_fail.$$); rm -f /tmp/vl_fail.$$; }
echo "  unassigned modules (reported, not failed): $unassigned"
echo
if [ "${fail:-0}" -gt 0 ]; then
  echo "FAIL: $fail layer violation(s). A module may not depend upward."
  exit 1
fi
echo "PASS: no module includes from a higher layer."
echo "NOTE: checks 2-4 (acyclic / provided / contract) run at boot via"
echo "      modbind_verify_graph(). Static enforcement of those needs the"
echo "      PROVIDES/REQUIRES declarations to exist in source first."
