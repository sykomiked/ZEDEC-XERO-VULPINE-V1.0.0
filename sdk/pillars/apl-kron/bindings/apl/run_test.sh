#!/bin/sh
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# run_test.sh APL LIB TEST.apl - run the GNU APL binding test with the
# native library at LIB. GNU APL's exit status does not report a failed
# check, so the output is checked here: no FAIL line, and the summary.
# It runs in a temporary directory, where GNU APL leaves its .apl.history.
set -eu
apl=$1
lib=$2
test=$3
dir=$(mktemp -d "${TMPDIR:-/tmp}/zkron_apl.XXXXXX")
trap 'rm -rf "$dir"' EXIT
{
  printf "LIB←'%s'\n" "$lib"
  cat "$test"
} > "$dir/test.apl"
out=$(cd "$dir" && "$apl" --script --noSV --noColor -f test.apl 2>&1) || true
printf '%s\n' "$out"
if printf '%s\n' "$out" | grep -q 'FAIL'; then exit 1; fi
if printf '%s\n' "$out" | grep -q 'ERROR'; then echo "run_test.sh: an APL error escaped a check"; exit 1; fi
printf '%s\n' "$out" | grep -q '^test_kron.apl: [0-9]* checks, 0 failed$'
