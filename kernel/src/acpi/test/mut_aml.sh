#!/usr/bin/env bash
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# mut_aml.sh — the AML host tests as one fast command for the mutation
# harness (tests/harness/mutator/mutate.py, target "aml_core"):
#
#   python3 tests/harness/mutator/mutate.py --config tests/harness/mutator/targets.json --only aml_core
#
# Same tests and sanitizers as `make -C kernel test-aml`, but every source
# the harness did not mutate is compiled once into ZXV_OBJ_CACHE (keyed by
# content), and the fuzz pass runs a short fixed number of iterations.
# Exit 3 = did not build (the harness's build code), 1 = a test failed.
set -u
cd "$(dirname "$0")/../../.." || exit 3
T=${ZXV_TMP:-/tmp}/mut_aml.$$
C=${ZXV_OBJ_CACHE:-${ZXV_TMP:-/tmp}/mut_aml_cache}
mkdir -p "$T" "$C" || exit 3
trap 'rm -rf "$T"' EXIT
FL=(-std=c11 -O1 -w -DTEST_HOST -fsanitize=address,undefined -fno-sanitize-recover=all
    -Iinclude -Isrc -Isrc/acpi -Isrc/modbind)
obj(){ # <source> -> prints the object path
  local h; h=$(cat "$1" src/acpi/*.h src/acpi/test/*.h | sha1sum | cut -c1-20)
  local o="$C/$(basename "$1" .c).$h.o"
  if [ ! -f "$o" ]; then
    gcc "${FL[@]}" -c "$1" -o "$T/tmp.o" || return 3
    mv -f "$T/tmp.o" "$o"
  fi
  echo "$o"
}
LIB=()
for s in src/acpi/aml_core.c src/acpi/aml_region.c src/acpi/aml_dev.c src/acpi/acpi_pm.c \
         src/acpi/acpi.c src/acpi/test/amlfake.c; do
  o=$(obj "$s") || exit 3
  LIB+=("$o")
done
rc=0
for t in src/acpi/test/test_aml_*.c; do
  b=$(basename "$t" .c)
  o=$(obj "$t") || exit 3
  gcc -fsanitize=address,undefined "$o" "${LIB[@]}" -o "$T/$b" || exit 3
  AML_FUZZ_ITERS=25 "$T/$b" > "$T/$b.log" 2>&1 || rc=1
done
exit $rc
