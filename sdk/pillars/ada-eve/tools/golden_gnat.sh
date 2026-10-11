#!/bin/sh
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# golden_gnat.sh ZEVE OUTDIR FILE... - check libzeve's reading of Ada
# against GNAT's.
#
# For every FILE (an Ada spec or an Eve text):
#   1. the Ada form compiles with GNAT (an .ads as written; an .eve as
#      `zeve ada` prints it), so the printed Ada is real Ada;
#   2. `zeve probe` writes an Ada main that prints what GNAT knows about
#      every type: 'First, 'Last, 'Digits, 'Delta, literals, array bounds
#      and the extremes of every record. It is compiled, run, and its
#      output must equal `zeve probe-expect`, libzeve's reading of the
#      same file.
# Needs gnatmake. Exit status 1 on the first difference.
set -u
zeve=$1
out=$2
shift 2
command -v gnatmake >/dev/null 2>&1 || { echo "golden-gnat: gnatmake not found"; exit 1; }
pass=0
fail=0
for f in "$@"; do
  name=$("$zeve" name "$f") || { echo "FAIL $f: does not parse"; fail=$((fail + 1)); continue; }
  dir="$out/gnat/$(basename "$f" | tr '.' '_')"
  rm -rf "$dir"
  mkdir -p "$dir"
  case "$f" in
    *.ads) cp "$f" "$dir/$name.ads" ;;
    *) "$zeve" ada "$f" > "$dir/$name.ads" ;;
  esac
  "$zeve" probe "$f" > "$dir/probe_main.adb"
  "$zeve" probe-expect "$f" > "$dir/expect.txt"
  if ! (cd "$dir" && gnatmake -q -gnat2012 -gnatws probe_main.adb > build.log 2>&1); then
    echo "FAIL $f: GNAT does not compile it:"
    cat "$dir/build.log"
    fail=$((fail + 1))
    continue
  fi
  "$dir/probe_main" > "$dir/gnat.txt" 2>&1
  if cmp -s "$dir/gnat.txt" "$dir/expect.txt"; then
    pass=$((pass + 1))
    echo "ok   $f: GNAT agrees on $(wc -l < "$dir/expect.txt" | tr -d ' ') facts"
  else
    fail=$((fail + 1))
    echo "FAIL $f: GNAT and libzeve differ:"
    diff "$dir/expect.txt" "$dir/gnat.txt" | head -n 20
  fi
done
echo "golden-gnat: $pass passed, $fail failed"
[ "$fail" -eq 0 ] && [ "$pass" -gt 0 ]
