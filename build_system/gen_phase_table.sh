#!/bin/sh
# gen_phase_table.sh — DERIVE the l13 phase of every declared module. GENERATED
# OUTPUT ONLY: nothing here is a list of chosen numbers, and adding a module
# tomorrow yields its phase without anyone picking one.
#
# WHY PHASE IS THE MODULE'S DRC LAYER
# -----------------------------------
# modbind's coupling law is P = V*I*cos(dphase) over 13 phases. Until now every
# declaration expanded phase to 0, so cos was always 1000 permille and the
# physics reproduced name-equality exactly. To differentiate the phases, the
# number has to come from a property the module ALREADY HAS.
#
# Three candidates were measured before this one was written, and two of them
# are dead by measurement rather than by taste:
#
#   CAPABILITY-DERIVED (any f of the capability token) is DEGENERATE. Provider
#   and requirer compute the same number from the same token, so dphase is
#   identically 0 on every edge and nothing differentiates. That is not a
#   weakness of one hash choice, it is structural for the whole family, and it
#   proves the general lemma: PHASE IS A PROPERTY OF THE MODULE, NEVER OF THE
#   CAPABILITY.
#
#   REQUIRES-GRAPH DEPTH is self-referential -- phase gates the requires-graph,
#   so deriving phase from that same graph feeds back -- and its own numbers
#   condemn it: a provider is always at strictly lower depth than its requirer,
#   so dphase is never 0 and NO EDGE IN THE SYSTEM could ever couple at full
#   power. That is an artefact of the derivation, not a fact about the code.
#
#   NAME LENGTH / NAME HASH is total, computable and MEANINGLESS: it makes
#   coupling strength a function of SPELLING, so renaming a module changes the
#   system's physics. It satisfies the letter of "derived, never assigned" and
#   violates its entire purpose. It was built and measured purely as a control.
#
# Layer is the one candidate whose number already states something about the
# system: L0..L12 in PROVENANCE/THIRTEEN_LAYERS.md is thirteen-valued, which is
# this system's own arity (COS13_PERMILLE, l13_phase_t, cyc13_t, MIXMAT_MAX),
# and it is the exact quantity the static DRC already enforces. The rigid gate
# and the fluid law then measure the same distance in two registers.
#
# WHY THIS SCRIPT AND NOT A HAND-WRITTEN HEADER
# ---------------------------------------------
# verify_layers.sh's layer_of() is the single authority for layer and stays so:
# this script SLICES that function out of verify_layers.sh and calls it. It does
# not restate one line of it. A second hand-maintained copy of the layer map
# would drift -- verify_layers.sh's own preamble records that conventions in
# this tree have a measured survival time of HOURS.
#
# MAKING LAYER TOTAL WITHOUT TYPING MORE NUMBERS
# ----------------------------------------------
# layer_of() classifies most modules and returns -1 for the rest, and the DRC
# only ever walks kernel/src -- so the four kernel/arch modules that provide
# mm_ready and blockdev_ready, the graph's most load-bearing roots, have no
# layer at all. Defaulting those to a number would make that invented number the
# most load-bearing constant in the system.
#
# So the unclassified ones are COMPUTED, by the DRC's own rule read backwards.
# Rule 1 of verify_layers.sh is "a module may not include a header from a HIGHER
# layer". Inverted, that says: a module including an L5 header is AT LEAST L5.
# The floor
#
#     layer(m) = max over m's local includes of layer(that header)
#
# is therefore not a new convention -- it is the strongest statement the DRC's
# existing rule licenses, on the DRC's own scale, so no two scales are mixed.
# A module that includes no classified local header stands on nothing, and
# standing on nothing is the definition of L0 substrate (the seat verify_layers
# gives zphi, rat and sha256 for exactly that reason). The empty max is 0
# because the set is empty, not because 0 was chosen as a default.
#
# OUTPUT: one #define per declared module. A module the generator cannot see
# gets NO entry, and ZXV_DECLARE's token paste then fails to compile with
# "'ZXV_PHASE_OF_<name>' undeclared". That is the intended fallback: a module
# with no derivable phase must not silently acquire one.
#
# POSIX sh + awk on purpose: it runs in a Makefile on five targets and must not
# need bash 4 associative arrays (the dev host ships bash 3.2).
#
# Author: H.M. Michael-Laurence: Curzi (c)  (ZXV composition slice)
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT" || exit 2

OUT=${1:-/dev/stdout}
TMP=${TMPDIR:-/tmp}/zxvphase.$$
mkdir -p "$TMP" || exit 2
trap 'rm -rf "$TMP"' EXIT INT TERM

# ---- 1. the ONE authority for layer: verify_layers.sh's own layer_of() -------
# Sliced out and sourced, never transcribed.
awk '/^layer_of\(\) \{/,/^\}/' build_system/verify_layers.sh > "$TMP/layer_of.sh"
if [ ! -s "$TMP/layer_of.sh" ]; then
  echo "gen_phase_table: could not slice layer_of() out of verify_layers.sh" >&2
  exit 2
fi
. "$TMP/layer_of.sh"

# ---- 2. every source and header in the tree, and who owns each header --------
# SOURCES are searched across all of kernel/ -- wider than the DRC, deliberately,
# because verify_layers.sh only ever walks kernel/src and the four modules that
# provide mm_ready and blockdev_ready live in kernel/arch. Those are the graph's
# most-required roots and they must not be the ones without a phase.
find kernel -name '*.c' ! -name 'test_*' ! -name 'debug_*' > "$TMP/c.list"

# HEADER OWNERSHIP is resolved with the DRC's OWN scope and no wider:
# `find kernel/src -name "$base.h" -maxdepth 3 | head -1`, verify_layers.sh:282.
#
# THIS BOUND IS LOAD-BEARING AND IT WAS MEASURED, NOT ASSUMED. Resolving headers
# across all of kernel/ instead pulled kernel/include/zxv_decl.h into the graph,
# zxv_decl.h includes modbind.h, and modbind is L3 -- so EVERY module that
# declares anything (which is every module here, by definition) inherited a floor
# of 3, and all 23 previously-unassigned modules came out at exactly 3. That is
# the declaration machinery measuring itself: including the header you declare
# WITH says nothing about what layer you are. Keeping the DRC's scope excludes
# kernel/include, which is exactly why the DRC never had this problem.
find kernel/src -maxdepth 3 -name '*.h' > "$TMP/h.list"

# base<TAB>path for headers (first wins, same as the DRC's `head -1`)
awk -F/ '{ b=$NF; sub(/\.h$/,"",b); if (!(b in seen)) { seen[b]=1; print b "\t" $0 } }' \
    "$TMP/h.list" > "$TMP/hown.tab"

# ---- 3. the DECLARED layer of every file and directory the tree contains -----
# One layer_of call per distinct basename/dirname, cached into a flat table that
# awk can index. Shell function -> table, so awk never re-enters the shell.
{
  # file basenames (both .c and .h) and directory names
  sed 's:.*/::; s:\.c$::' "$TMP/c.list"
  sed 's:.*/::; s:\.h$::' "$TMP/h.list"
  sed 's:/[^/]*$::; s:.*/::' "$TMP/c.list"
  sed 's:/[^/]*$::; s:.*/::' "$TMP/h.list"
} | sort -u > "$TMP/keys"

while IFS= read -r k; do
  [ -z "$k" ] && continue
  printf '%s\t%s\n' "$k" "$(layer_of "$k")"
done < "$TMP/keys" > "$TMP/layer.tab"

# ---- 4. every ZXV_DECLARE site: module name and the file it lives in ---------
# ANCHORED AT THE START OF THE LINE (blanks only), which is not cosmetic:
# kernel/src/apps/apps.c:309 contains a WORKED EXAMPLE of ZXV_DECLARE inside a
# block comment, and an unanchored grep phases a module named `apps` that does
# not exist. The anchor rejects it because the comment's leading ` * ` is not
# blank. The failure mode in the other direction is safe by construction: a real
# declaration this misses simply gets no #define, and its ZXV_DECLARE then fails
# to compile by name. Silent-wrong is impossible here; loud-wrong is the worst
# case.
grep -n 'ZXV_DECLARE(' $(cat "$TMP/c.list") 2>/dev/null |
  sed -n 's/^\([^:]*\):[0-9]*:[[:blank:]]*ZXV_DECLARE(\([A-Za-z_][A-Za-z0-9_]*\).*/\2\t\1/p' |
  sort -u > "$TMP/decl.tab"

# ---- 5. the derivation, in awk: declared layer, else the include floor -------
awk -v layerfile="$TMP/layer.tab" -v hownfile="$TMP/hown.tab" '
function declared_layer(key,   v) {
    v = DECL[key];
    return (v == "" ? -1 : v + 0);
}
# layer of a FILE path: per-file basename first, then its owning directory --
# exactly verify_layers.sh resolution order, which exists because one directory
# can hold modules at two layers (zxvfs vs zxvfs_tri, sha256 inside
# robin_debanks). Reading only the directory throws that away.
function file_declared(path,   b, d, l) {
    b = path; sub(/.*\//, "", b); sub(/\.[ch]$/, "", b);
    l = declared_layer(b);
    if (l >= 0) return l;
    d = path; sub(/\/[^\/]*$/, "", d); sub(/.*\//, "", d);
    return declared_layer(d);
}
# THE FLOOR. Recursive over local includes, memoised, cycle-guarded. A file
# already on the stack contributes nothing rather than looping: an include cycle
# cannot raise a floor it is itself part of.
function layer_of_file(path,   l, cmd, line, inc, b, own, sub_l, best, n, i, arr) {
    if (path in MEMO) return MEMO[path];
    if (path in ONSTACK) return -1;
    ONSTACK[path] = 1;
    l = file_declared(path);
    if (l >= 0) { delete ONSTACK[path]; MEMO[path] = l; return l; }
    best = 0;                       # empty max over an empty include set
    n = 0;
    while ((getline line < path) > 0) {
        if (line !~ /^[ \t]*#[ \t]*include[ \t]*"/) continue;
        inc = line;
        sub(/^[^"]*"/, "", inc); sub(/".*$/, "", inc);
        arr[++n] = inc;
    }
    close(path);
    for (i = 1; i <= n; i++) {
        b = arr[i]; sub(/.*\//, "", b); sub(/\.h$/, "", b);
        own = HOWN[b];
        if (own == "") continue;    # not a header this tree owns
        sub_l = layer_of_file(own);
        if (sub_l > best) best = sub_l;
    }
    delete ONSTACK[path];
    MEMO[path] = best;
    return best;
}
BEGIN {
    FS = "\t";
    while ((getline line < layerfile) > 0) { split(line, f, "\t"); DECL[f[1]] = f[2]; }
    close(layerfile);
    while ((getline line < hownfile) > 0)  { split(line, f, "\t"); HOWN[f[1]] = f[2]; }
    close(hownfile);
}
{
    mod = $1; path = $2;
    l = layer_of_file(path);
    src = (file_declared(path) >= 0) ? "DRC" : "FLOOR";
    printf "%s\t%d\t%s\t%s\n", mod, l % 13, src, path;
}
' "$TMP/decl.tab" | sort -u > "$TMP/phase.tab"

# ---- 6. emit the header -----------------------------------------------------
{
  echo '/* zxv_phase_table.h — GENERATED by build_system/gen_phase_table.sh.'
  echo ' * DO NOT EDIT AND DO NOT COMMIT A HAND-WRITTEN COPY.'
  echo ' *'
  echo ' * One #define per declared module: its l13 phase, derived from its DRC'
  echo ' * layer (verify_layers.sh layer_of), and where layer_of is silent, from'
  echo ' * the floor its own local includes impose -- the DRC layer rule read'
  echo ' * backwards. No number in this file was chosen by a person.'
  echo ' *'
  echo ' * A module with NO entry here fails to compile at its ZXV_DECLARE with'
  echo " * \"'ZXV_PHASE_OF_<name>' undeclared\". That is deliberate: a module whose"
  echo ' * phase cannot be derived must not silently acquire one.'
  echo ' *'
  echo ' * src=DRC   layer_of() classified the file or its directory'
  echo ' * src=FLOOR layer_of() was silent; this is max(layer of local includes)'
  echo ' */'
  echo '#ifndef ZXV_PHASE_TABLE_H'
  echo '#define ZXV_PHASE_TABLE_H'
  echo
  awk -F'\t' '{ printf "#define ZXV_PHASE_OF_%-28s %-3s /* %-5s %s */\n", $1, $2, $3, $4 }' \
      "$TMP/phase.tab"
  echo
  echo '#endif /* ZXV_PHASE_TABLE_H */'
} > "$OUT"

# ---- 7. census to stderr, so the build log carries the derivation -----------
awk -F'\t' '
{ n++; c[$2]++; s[$3]++ }
END {
  printf "gen_phase_table: %d declared modules phased\n", n > "/dev/stderr";
  printf "  by source:";
  for (k in s) printf " %s=%d", k, s[k];
  printf "\n";
  printf "  by phase :";
  for (i = 0; i < 13; i++) if (c[i]) printf " L%d=%d", i, c[i];
  printf "\n";
}' "$TMP/phase.tab" >&2
