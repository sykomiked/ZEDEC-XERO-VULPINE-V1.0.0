#!/bin/sh
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# check_catalogue.sh — validate catalogue.tsv: six fields per line, a year
# from 1949 to now, a known tier, 108 lines, unique names, a twin exactly for the run
# and pillar tiers, every path it names present, and the run tier equal to
# the front-ends Lingua actually ships
# (zl_langs.def entries whose zl_<fe>.c is not a placeholder). Run from kernel/.
set -eu
cat=src/lingua/catalogue.tsv
root=..
fail=0
bad() { echo "catalogue: $*"; fail=1; }
tab=$(printf '\t')
n=0
names=""
while IFS="$tab" read -r year lang family tier twin where extra; do
    case "$year" in '#'*|'') continue ;; esac
    n=$((n + 1))
    [ -z "${extra:-}" ] || bad "$lang: more than six fields"
    [ -n "$where" ] || bad "$lang: fewer than six fields"
    case "$year" in [12][0-9][0-9][0-9]) ;; *) bad "$lang: bad year $year" ;; esac
    [ "$year" -ge 1949 ] && [ "$year" -le 2026 ] || bad "$lang: year $year out of range"
    case "$tier" in
    run|pillar|native) [ "$twin" != "-" ] || bad "$lang: tier $tier needs a twin" ;;
    ir|host|next1|next2|next3|catalogue) [ "$twin" = "-" ] || bad "$lang: tier $tier has a twin" ;;
    *) bad "$lang: unknown tier $tier" ;;
    esac
    case "$names" in *"|$lang|"*) bad "$lang: listed twice" ;; esac
    names="$names|$lang|"
    for p in $(printf '%s\n' "$where" | tr ',;' '\n\n' | sed -n 's/^ *\(\(kernel\|sdk\|bindings\)\/[^ ]*\).*/\1/p'); do
        [ -e "$root/$p" ] || bad "$lang: path $p does not exist"
    done
done < "$cat"
run=$(awk -F'\t' '!/^#/ && $4 == "run" { print $2 "=" $5 }' "$cat" | sort | tr '\n' ' ')
# A front-end ships once its zl_<fe>.c is no longer the placeholder.
want=$(sed -n 's/^ZL_LANG_X([A-Z0-9_]*, [0-9]*, "\([^"]*\)", "\([^"]*\)", \([a-z0-9]*\),.*/\3\t\1=\2/p' src/lingua/zl_langs.def |
    while IFS="$tab" read -r fe pair; do
        grep -q "front-end is not built yet" "src/lingua/zl_$fe.c" || echo "$pair"
    done | sort | tr '\n' ' ')
[ "$run" = "$want" ] || bad "run tier is '$run', front-ends ship '$want'"
[ "$n" -eq 108 ] || bad "$n languages, not 108 (104 base, 3 native, the IR)"
if [ "$fail" -ne 0 ]; then echo "[FAIL] lingua catalogue"; exit 1; fi
echo "[PASS] lingua catalogue ($n languages, run tier matches the front-ends)"
