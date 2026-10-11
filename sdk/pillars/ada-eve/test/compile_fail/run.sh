#!/bin/sh
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# run.sh DIR - compile-fail tests for the Ada binding.
#
# Every DIR/*.adb except ok_*.adb must be REJECTED by GNAT, and the
# diagnostic must match the file's "--  expect: REGEX" line (grep -E), so
# a case cannot pass by failing for an unrelated reason. Every ok_*.adb
# must compile: it is the counterpart of the cases, proving the setup
# works. Semantic checks only (-gnatc); nothing is linked.
#
# Environment: GNATCC (default gcc), ADAINC (the binding's ada/ folder).
set -u
dir=${1:-test/compile_fail}
GNATCC=${GNATCC:-gcc}
ADAINC=${ADAINC:-ada}
tmp=${TMPDIR:-/tmp}/zxv-ada-compile-fail.$$
mkdir -p "$tmp"
pass=0
fail=0
for f in "$dir"/*.adb; do
  base=$(basename "$f" .adb)
  (cd "$tmp" && $GNATCC -c -gnat2012 -gnatc -gnatf -I"$ADAINC" "$f") > "$tmp/out" 2>&1
  rc=$?
  case "$base" in
    ok_*)
      if [ $rc -eq 0 ]; then
        pass=$((pass + 1))
        echo "ok   $base compiles"
      else
        fail=$((fail + 1))
        echo "FAIL $base: the control program did not compile:"
        cat "$tmp/out"
      fi
      ;;
    *)
      want=$(sed -n 's/^--  expect: //p' "$f" | head -n 1)
      if [ -z "$want" ]; then
        fail=$((fail + 1))
        echo "FAIL $base: no '--  expect:' line"
      elif [ $rc -eq 0 ]; then
        fail=$((fail + 1))
        echo "FAIL $base: compiled, but must be rejected"
      elif grep -Eq "$want" "$tmp/out"; then
        pass=$((pass + 1))
        echo "ok   $base rejected: $(grep -E "$want" "$tmp/out" | head -n 1 | sed 's/^[^:]*:[0-9]*:[0-9]*: //')"
      else
        fail=$((fail + 1))
        echo "FAIL $base: rejected, but not with /$want/:"
        cat "$tmp/out"
      fi
      ;;
  esac
done
rm -rf "$tmp"
echo "compile-fail: $pass passed, $fail failed"
[ $fail -eq 0 ] && [ $pass -gt 0 ]
