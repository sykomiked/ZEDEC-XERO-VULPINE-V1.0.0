#!/usr/bin/env bash
# preflight_all_targets.sh — COMPILE-CHECK every target before you rent a server.
#
# WHY THIS EXISTS
# ---------------
# build_all_targets.sh reports SKIP when a GNU cross-toolchain is absent. On a
# developer machine that is usually four of the five targets — which means four
# targets would be compiled for the very first time on rented hardware, on the
# clock. That is the worst possible place to discover that a header is missing
# or a symbol is undefined.
#
# clang is a native cross-compiler: one binary targets every architecture. It
# cannot LINK a freestanding image without the target's libgcc and linker
# script, but it CAN compile every translation unit, and compilation is where
# essentially all portability defects live — missing headers, undeclared
# functions, type-width assumptions, arch-specific intrinsics, files listed in
# a Makefile that do not exist.
#
# So this script compiles every source of every target with clang and reports
# an honest per-target matrix. It is a smoke test, not a substitute for the
# real build. A target that passes here can still fail to LINK. A target that
# fails here WILL fail on the server.
#
#   bash build_system/preflight_all_targets.sh
#   TARGETS="x86_64 riscv" bash build_system/preflight_all_targets.sh
#   VERBOSE=1 bash build_system/preflight_all_targets.sh    # show every error
#
# Exit status is non-zero if any target has compile errors.
#
# Run from 05_KERNEL.

set -uo pipefail
cd "$(dirname "$0")/.." || exit 1

TARGETS="${TARGETS:-arm64 x86_64 riscv riscv32 arm32}"
VERBOSE="${VERBOSE:-0}"
MAXERR="${MAXERR:-6}"
# Known-good baseline: arm64 links and boots with this many apparently-undefined
# symbols, all of them in code --gc-sections discards. Counts at or below this
# are noise; well above it is signal.
UNDEF_FLOOR="${UNDEF_FLOOR:-8}"

# Prefer a clang that supports RISC-V. Apple's system clang does not.
pick_clang() {
  for c in /opt/homebrew/opt/llvm/bin/clang /usr/local/opt/llvm/bin/clang clang; do
    command -v "$c" >/dev/null 2>&1 || continue
    if "$c" --print-targets 2>/dev/null | grep -qi riscv; then echo "$c"; return; fi
  done
  command -v clang >/dev/null 2>&1 && echo clang || echo ""
}
CLANG="$(pick_clang)"
if [ -z "$CLANG" ]; then
  echo "preflight: no clang found — cannot compile-check. Install LLVM." >&2
  exit 2
fi

NM="$(dirname "$CLANG")/llvm-nm"
[ -x "$NM" ] || NM="llvm-nm"
command -v "$NM" >/dev/null 2>&1 || NM="nm"

has_riscv=0
"$CLANG" --print-targets 2>/dev/null | grep -qi riscv && has_riscv=1

triple_for() {
  case "$1" in
    arm64)   echo "aarch64-unknown-none-elf" ;;
    x86_64)  echo "x86_64-unknown-none-elf" ;;
    riscv)   echo "riscv64-unknown-none-elf" ;;
    riscv32) echo "riscv32-unknown-none-elf" ;;
    arm32)   echo "armv7-unknown-none-eabi" ;;
    *)       echo "" ;;
  esac
}

# Extra flags a target needs to compile at all.
extra_for() {
  case "$1" in
    riscv)   echo "-march=rv64imac -mabi=lp64" ;;
    riscv32) echo "-march=rv32imac -mabi=ilp32" ;;
    arm32)   echo "-march=armv7-a -marm" ;;
    x86_64)  echo "-mno-sse -mno-mmx -mno-red-zone" ;;
    *)       echo "" ;;
  esac
}

# Pull the source list out of a Makefile: everything between `SRCS :=` (or
# KERNEL_SRCS/COMMON_SRCS) and the first line that does not end in a backslash.
srcs_from_mk() {
  # Sources arrive in a Makefile in two shapes and BOTH must be collected:
  #   KERNEL_SRCS = a.c b.c ...        (source list)
  #   ARCH_OBJS   = a.o b.o ...        (object list — map .o back to .c)
  # Missing the second is how this script once reported "118 compiled / OK"
  # while silently skipping kernel_main_arm64.c and every other arch file.
  awk '
    /^[A-Za-z_0-9]*(SRCS?|OBJS?)[ \t]*[:+]?=/ { collecting=1 }
    collecting {
      line=$0
      sub(/^[A-Za-z_0-9]*(SRCS?|OBJS?)[ \t]*[:+]?=/, "", line)
      gsub(/\\[ \t]*$/, "", line)
      n=split(line, a, /[ \t]+/)
      for (i=1;i<=n;i++) {
        f=a[i]
        if (f ~ /\.o$/) sub(/\.o$/, ".c", f)
        # boot stubs come from .S/.asm and have no .c counterpart
        if (f ~ /\/boot\.c$/) continue
        if (f ~ /\.c$/ && f ~ /^kernel\//) print f
      }
      if ($0 !~ /\\[ \t]*$/) collecting=0
    }
  ' "$1" | sort -u
}

# Self-check: every kernel .c/.o path mentioned anywhere in the Makefile should
# appear in the parsed list. If it does not, the parser is under-inspecting and
# a green result would be meaningless.
parser_coverage_check() {
  local mk="$1" parsed="$2"
  local mentioned
  mentioned="$(grep -oE 'kernel/[A-Za-z0-9_./-]+\.[co]' "$mk" | sed 's/\.o$/.c/' | grep -v '/boot\.c$' | sort -u)"
  comm -23 <(printf '%s\n' "$mentioned") <(printf '%s\n' "$parsed" | sort -u)
}

includes_from_mk() {
  grep -o '\-I[^ \\]*' "$1" 2>/dev/null | sort -u | tr '\n' ' '
}

defines_from_mk() {
  # Skip any -DFOO=$(BAR): make would expand it, the shell will not, and the
  # literal "$(BAR)" reaches the preprocessor as a stray '$' macro.
  grep -o '\-D[A-Za-z_][A-Za-z0-9_]*\(=[^ \\]*\)\?' "$1" 2>/dev/null \
    | grep -v '\$' | sort -u | tr '\n' ' '
}

printf '\033[1mZXV PREFLIGHT — compile-check every target before the server\033[0m\n'
printf 'clang: %s\n' "$($CLANG --version 2>/dev/null | head -1)"
[ "$has_riscv" = 1 ] || printf '\033[1;33mWARNING: this clang has no RISC-V backend; riscv targets will be SKIPped\033[0m\n'
printf '\n'

ROWS=()
FAILED=0
TMPO="$(mktemp -d)"
trap 'rm -rf "$TMPO"' EXIT

for T in $TARGETS; do
  MK="build_system/Makefile.${T}"
  if [ ! -f "$MK" ]; then ROWS+=("$T|NO-MAKEFILE|0|0|-"); continue; fi

  TRIPLE="$(triple_for "$T")"
  case "$T" in
    riscv|riscv32) if [ "$has_riscv" != 1 ]; then ROWS+=("$T|SKIP(no riscv backend)|0|0|-"); continue; fi ;;
  esac

  SRCS=()
  while IFS= read -r _l; do [ -n "$_l" ] && SRCS+=("$_l"); done < <(srcs_from_mk "$MK")
  INC="$(includes_from_mk "$MK")"
  DEF="$(defines_from_mk "$MK")"
  # the freestanding prelude every target uses
  PRELUDE=""
  [ -f kernel/include/freestanding.h ] && PRELUDE="-include kernel/include/freestanding.h"

  MISSED="$(parser_coverage_check "$MK" "$(printf '%s\n' ${SRCS[@]+"${SRCS[@]}"})")"
  MISSED_N="$(printf '%s' "$MISSED" | grep -c . || true)"

  printf '\033[1;36m==> %s (%s): %d source file(s)\033[0m\n' "$T" "$TRIPLE" "${#SRCS[@]}"
  if [ "$MISSED_N" -gt 0 ]; then
    printf '\033[1;31m    PARSER UNDER-INSPECTED: %d file(s) named in %s were not\n' "$MISSED_N" "$MK"
    printf '    collected, so a green result here would be meaningless:\033[0m\n'
    printf '%s\n' "$MISSED" | head -8 | sed 's/^/      /'
    FAILED=1
  fi

  ERRS=0; MISSING=0; OK=0; IDX=0; ERRLOG="$TMPO/$T.log"
  rm -rf "$TMPO/obj_$T"; mkdir -p "$TMPO/obj_$T"
  : > "$ERRLOG"

  for S in ${SRCS[@]+"${SRCS[@]}"}; do
    [ -z "$S" ] && continue
    case "$S" in *.S) continue ;; esac      # assembly: not clang-checkable portably
    IDX=$((IDX+1))
    if [ ! -f "$S" ]; then
      MISSING=$((MISSING+1))
      echo "MISSING FILE: $S  (listed in $MK but not present)" >> "$ERRLOG"
      continue
    fi
    if "$CLANG" --target="$TRIPLE" $(extra_for "$T") \
         -std=c11 -ffreestanding -nostdlib -fno-builtin -fno-stack-protector \
         -Wno-everything $DEF $PRELUDE $INC \
         -c "$S" -o "$TMPO/obj_$T/u${IDX}.o" 2>>"$ERRLOG"; then
      OK=$((OK+1))
    else
      ERRS=$((ERRS+1))
      echo "--- above errors from: $S" >> "$ERRLOG"
    fi
  done

  # ---- LINK CHECK WITHOUT A LINKER ----
  # Compiling proves each translation unit is well formed. It does NOT prove
  # the image links: a function that is DECLARED in a header and defined
  # nowhere compiles perfectly in every file that calls it, and only fails at
  # link time. That is precisely how a target ends up with ~110 undefined
  # symbols and nobody notices until the toolchain is on the clock.
  #
  # So: collect every symbol DEFINED by the target's objects, collect every
  # symbol they leave UNDEFINED, and report the difference.
  UNDEF_N=0; UNDEF_LIST=""
  if [ "$ERRS" -eq 0 ] && [ "${#SRCS[@]}" -gt 0 ] && command -v "$NM" >/dev/null 2>&1; then
    DEFS="$TMPO/$T.defs"; UNDS="$TMPO/$T.unds"
    "$NM" --defined-only "$TMPO/obj_$T/"*.o 2>/dev/null \
      | awk '{print $NF}' | sort -u > "$DEFS"
    "$NM" --undefined-only "$TMPO/obj_$T/"*.o 2>/dev/null \
      | awk '{print $NF}' | sort -u > "$UNDS"
    # Symbols the assembly boot stub or libgcc legitimately provides.
    # Filter what the TOOLCHAIN legitimately supplies. libgcc/compiler-rt
    # provide the soft-float, integer-division and complex helpers; the boot
    # assembly provides the entry point; the C library shim provides mem*.
    # Anything left is a symbol the project itself declared and never defined.
    UNDEF_LIST="$(comm -23 "$UNDS" "$DEFS" \
      | grep -vE '^(__aeabi_|__truncdf|__extendsf|__fixdf|__fixsf|__fixuns|__float(un)?(si|di)(sf|df)|__(add|sub|mul|div)(sf|df|sc|dc|tf)3|__(eq|ne|lt|le|gt|ge|unord)(sf|df)2|__(neg|cmp)(sf|df)2|__powi|__(u)?div(si|di|ti)3|__(u)?mod(si|di|ti)3|__mul(si|di|ti)3|__(ash|lsh)[lr](si|di|ti)3|__clz|__ctz|__ffs|__paritysi|__popcount|__bswap|__sync_|__atomic_|__stack_chk|__gnu_)' \
      | grep -vE '^(memcpy|memset|memmove|memcmp|strlen|strcmp|strcpy|_start|_bss_start|_bss_end|_stack_top|__bss_|__data_|__text_|_end)$' || true)"
    UNDEF_N="$(printf '%s' "$UNDEF_LIST" | grep -c . || true)"
  fi

  if [ "${#SRCS[@]}" -eq 0 ]; then
    # A check that inspected nothing must never report green. This is the same
    # hollow-pass shape the codebase audit was built to catch.
    STATUS="NO-SOURCES-PARSED"
    FAILED=1
    printf '\033[1;31m    parsed 0 sources from %s — the parser does not understand\n' "$MK"
    printf '    this Makefile, so THIS TARGET WAS NOT CHECKED AT ALL.\033[0m\n'
  elif [ $((ERRS+MISSING)) -eq 0 ] && [ "$UNDEF_N" -gt "$UNDEF_FLOOR" ]; then
    # ADVISORY, NOT A GATE. The real link passes -Wl,--gc-sections, which
    # DISCARDS unreferenced dead code before resolving it — so a symbol that
    # is declared, never defined, and only reachable from dead code links
    # fine in reality and shows up here as a false positive. A symbol diff
    # cannot model section garbage collection.
    #
    # Calibration: arm64 links and boots today with a handful of such
    # symbols, so UNDEF_FLOOR is set from that known-good baseline. A count
    # well ABOVE the floor is a strong signal of genuinely missing
    # definitions; a count near it is noise.
    STATUS="OK (link advisory)"
    printf '\033[1;33m    ADVISORY: %s symbol(s) declared and never defined.\n' "$UNDEF_N"
    printf '    Some of these are dead code the linker will discard (--gc-sections),\n'
    printf '    but a large count means real missing definitions:\033[0m\n'
    printf '%s\n' "$UNDEF_LIST" | head -12 | sed 's/^/      /'
    [ "$UNDEF_N" -gt 12 ] && printf '      ... and %s more\n' "$((UNDEF_N-12))"
  elif [ $((ERRS+MISSING)) -eq 0 ]; then
    STATUS="OK"
  else
    STATUS="FAIL"
    FAILED=1
  fi
  ROWS+=("$T|$STATUS|$OK|$((ERRS+MISSING))|$ERRLOG")

  if [ $((ERRS+MISSING)) -gt 0 ]; then
    printf '\033[1;31m    %d file(s) failed, %d missing\033[0m\n' "$ERRS" "$MISSING"
    if [ "$VERBOSE" = 1 ]; then
      cat "$ERRLOG"
    else
      grep -E "MISSING FILE|error:" "$ERRLOG" | head -"$MAXERR" | sed 's/^/    /'
      echo "    (full log: $ERRLOG — rerun with VERBOSE=1 for everything)"
    fi
  else
    printf '\033[1;32m    all %d compiled\033[0m\n' "$OK"
  fi
  printf '\n'
done

printf '\033[1m%-10s %-26s %-8s %-8s\033[0m\n' TARGET STATUS COMPILED PROBLEMS
printf -- '----------------------------------------------------------------------\n'
for R in ${ROWS[@]+"${ROWS[@]}"}; do
  IFS='|' read -r t s o e _ <<< "$R"
  C=""; case "$s" in OK) C="\033[1;32m";; FAIL) C="\033[1;31m";; *) C="\033[1;33m";; esac
  printf "%-10s ${C}%-26s\033[0m %-8s %-8s\n" "$t" "$s" "$o" "$e"
done
printf -- '----------------------------------------------------------------------\n'

if [ "$FAILED" -eq 1 ]; then
  printf '\n\033[1;31mPREFLIGHT FAILED — fix these BEFORE the server session.\033[0m\n'
  printf 'Every one of these would have cost you paid time to discover.\n'
  exit 1
fi
printf '\n\033[1;32mPREFLIGHT CLEAN — every target compiles.\033[0m\n'
printf 'NOTE: this checks COMPILATION only. Linking still needs the real\n'
printf '      cross-toolchains; run build_all_targets.sh on the server.\n'
exit 0
