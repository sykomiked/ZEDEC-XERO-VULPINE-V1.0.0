#!/bin/bash
# measure_reachability.sh — how much of kernel/src is ACTUALLY in the binary.
#
# Three metrics have been used on this project, each less wrong than the last:
#   1. Makefile membership      -- WRONG. --gc-sections discards unreferenced
#                                  sections, so being in KERNEL_SRCS proves
#                                  nothing. Said 50 files absent.
#   2. per-file symbol presence -- BETTER, still overstates. A file counts as
#                                  "present" if ANY one of its symbols survives,
#                                  so a 1,571-line file with one live function
#                                  scores the same as one fully wired. Said 123.
#   3. per-SYMBOL reachability  -- this script. The only one that answers the
#                                  question actually being asked.
#
# Under --gc-sections presence is per-FUNCTION. "This file is in the binary" is
# not a statement that can be made; only "this symbol is reachable" is.
set -u
ELF=${1:-kernel_arm64.elf}
NM=${NM:-aarch64-linux-gnu-nm}
[ -f "$ELF" ] || { echo "no $ELF — build first"; exit 2; }
python3 - "$ELF" "$NM" <<'PY'
import sys,os,glob,subprocess
elf_path,NM=sys.argv[1],sys.argv[2]
def nm(p,flt=None):
    try: out=subprocess.run([NM,p],capture_output=True,text=True,timeout=90).stdout
    except Exception: return set()
    return {f[2] for f in (L.split() for L in out.split("\n"))
            if len(f)>=3 and (flt is None or f[1] in flt)}
elf=nm(elf_path)
srcs=[p for p in glob.glob("kernel/src/**/*.c",recursive=True)
      if "third_party" not in p and not os.path.basename(p).startswith(("test_","debug_"))
      and not p.endswith("_validate.c")]
d_tot=l_tot=0; full=part=none=noobj=0
for c in srcs:
    o=c[:-2]+".o"
    if not os.path.exists(o): noobj+=1; continue
    d=nm(o,{"T","D","R","B"})
    if not d: noobj+=1; continue
    live=len(d&elf); d_tot+=len(d); l_tot+=live
    if live==len(d): full+=1
    elif live: part+=1
    else: none+=1
pct=100*l_tot//max(1,d_tot)
print(f"symbols defined   : {d_tot}")
print(f"symbols reachable : {l_tot}  ({pct}%)")
print(f"symbols discarded : {d_tot-l_tot}")
print(f"files fully live {full} / partial {part} / nothing live {none} / never compiled {noobj}")
PY
