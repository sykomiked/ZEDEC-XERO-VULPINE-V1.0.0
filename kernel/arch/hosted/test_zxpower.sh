#!/bin/sh
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# test_zxpower.sh — build the hosted power reader and the zxpower tool with
# this machine's compiler, run the fixture test, then read the live machine
# once in text and once in JSON (checked with python3). On macOS this is the
# build of the IOKit and proc_pid_rusage readers.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
K="$(cd "$HERE/../.." && pwd)"
CC="${CC:-cc}"
T="$(mktemp -d "${ZXV_TMP:-${TMPDIR:-/tmp}}/zxpower.XXXXXX")"
trap 'rm -rf "$T"' EXIT INT TERM
FW=""
[ "$(uname -s)" = Darwin ] && FW="-framework IOKit -framework CoreFoundation"
FLAGS="-std=c11 -O1 -Wall -Wextra -Werror -I$K/src/power -I$HERE"
# shellcheck disable=SC2086
"$CC" $FLAGS "$HERE/test_zxv_power_host.c" "$HERE/zxv_power_host.c" "$K/src/power/power_ledger.c" \
    $FW -o "$T/test_zxv_power_host"
"$T/test_zxv_power_host"
# shellcheck disable=SC2086
"$CC" $FLAGS "$HERE/zxpower.c" "$HERE/zxv_power_host.c" "$K/src/power/power_ledger.c" $FW \
    -o "$T/zxpower"
"$T/zxpower" --interval 300 --top 10
"$T/zxpower" --interval 300 --json > "$T/power.json"
python3 - "$T/power.json" <<'PY'
import json, sys
d = json.load(open(sys.argv[1]))
assert d["host"] in ("linux", "macos", "windows"), d["host"]
assert d["source"] in ("none", "metered", "conflict"), d["source"]
assert d["consumers"][0]["kind"] == "system"
total = sum(c["uj"] for c in d["consumers"])
assert d["source"] == "none" or total == d["total_uj"], (total, d["total_uj"])
print("zxpower json ok:", d["device"], d["host"], d["source"], len(d["consumers"]), "consumers")
PY
