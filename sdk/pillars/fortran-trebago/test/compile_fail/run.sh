#!/bin/sh
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# run.sh DIR - compile-fail tests for the Fortran modules.
#
# Every DIR/*.f90 except ok_*.f90 must be REJECTED by the compiler, and the
# diagnostic must match the file's "! expect: REGEX" line (grep -E), so a
# case cannot pass by failing for an unrelated reason. Every ok_*.f90 must
# compile: it is the exact counterpart of the cases, proving the setup works.
#
# Environment: FC (default gfortran), FFLAGS (must include -I for zxv.mod).
set -u
dir=${1:-test/compile_fail}
FC=${FC:-gfortran}
FFLAGS=${FFLAGS:-}
tmp=${TMPDIR:-/tmp}/zxv-compile-fail.$$
pass=0
fail=0
for f in "$dir"/*.f90; do
  base=$(basename "$f" .f90)
  # shellcheck disable=SC2086
  $FC $FFLAGS -fsyntax-only "$f" > "$tmp" 2>&1
  rc=$?
  case "$base" in
    ok_*)
      if [ $rc -eq 0 ]; then
        pass=$((pass + 1))
        echo "ok   $base compiles"
      else
        fail=$((fail + 1))
        echo "FAIL $base: the control program did not compile:"
        cat "$tmp"
      fi
      ;;
    *)
      want=$(sed -n 's/^! expect: //p' "$f" | head -n 1)
      if [ -z "$want" ]; then
        fail=$((fail + 1))
        echo "FAIL $base: no '! expect:' line"
      elif [ $rc -eq 0 ]; then
        fail=$((fail + 1))
        echo "FAIL $base: compiled, but must be rejected"
      elif grep -Eq "$want" "$tmp"; then
        pass=$((pass + 1))
        echo "ok   $base rejected: $(grep -E "$want" "$tmp" | head -n 1 | sed 's/^ *//')"
      else
        fail=$((fail + 1))
        echo "FAIL $base: rejected, but not with /$want/:"
        cat "$tmp"
      fi
      ;;
  esac
done
rm -f "$tmp"
echo "compile-fail: $pass passed, $fail failed"
[ $fail -eq 0 ] && [ $pass -gt 0 ]
