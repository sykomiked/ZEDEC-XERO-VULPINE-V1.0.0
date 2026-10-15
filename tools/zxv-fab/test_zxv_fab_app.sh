#!/bin/sh
# test_zxv_fab_app.sh — the program fabricator end to end with real apps
# (kernel/src/apps): pack-app makes a signed PUBLIC recipe from a manifest,
# unpack-app rebuilds it on a clean store and accepts it only through the
# gate (kernel/src/appfab), with the real KRON, Eve, Sutra, Trebago and
# MYRA runtimes. Every way to break it must refuse or hold, and write nothing.
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# usage: test_zxv_fab_app.sh ZXV_FAB WORKDIR   (run from anywhere)
set -u
FAB=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
D=$2
SRC=$(cd "$(dirname "$0")/../../kernel/src" && pwd)
rm -rf "$D" && mkdir -p "$D/store" "$D/b" "$D/r/x" || exit 1
D=$(cd "$D" && pwd)
n=0
ok() { n=$((n + 1)); }
no() { echo "FAIL: $1"; [ -f "$D/err" ] && cat "$D/err"; exit 1; }
# rc CMD...: run, keep stderr in $D/err, print the exit status
rc() { "$@" > "$D/out.txt" 2> "$D/err"; echo $?; }
cd "$SRC" || exit 1
"$FAB" keygen-sign "$D/a.pk" "$D/a.sk" "$(printf '%064d' 1)" > /dev/null || no keygen; ok
"$FAB" keygen-sign "$D/o.pk" "$D/o.sk" "$(printf '%064d' 2)" > /dev/null || no keygen2; ok
K="--pk $D/a.pk --sk $D/a.sk"
T="$D/FABRICATED.tsv"

# ---- a five-language app, the whole way --------------------------------------
[ "$(rc "$FAB" pack-app apps/fee_check.zxapp "$D/fee.fab" $K --form "$D/fee.form" --store "$D/store" --tsv "$T")" = 0 ] || no "pack fee_check"; ok
MC=$(sed -n 's/^app fee_check //p' "$D/out.txt")
[ -n "$MC" ] || no "pack prints the manifest CID"; ok
grep -q '^logic    sutra   fee <<$' "$D/fee.form" || no "sources are inline in the form"; ok
grep -q 'apps/blocks' "$D/fee.form" && no "a disk path left in the form"; ok
[ "$("$FAB" cid "$D/fee.form")" = "$MC" ] || no "the manifest CID is the form's CID"; ok
grep -q "^fee_check	-	$MC	bafk.*	-\$" "$T" || no "FABRICATED.tsv row"; ok
cp "$D/fee.fab" "$D/fee1.fab"
[ "$(rc "$FAB" pack-app apps/fee_check.zxapp "$D/fee.fab" $K --tsv "$T")" = 0 ] || no "pack again"; ok
cmp -s "$D/fee.fab" "$D/fee1.fab" || no "the pack is deterministic"; ok
[ "$(grep -c '^fee_check' "$T")" = 1 ] || no "packing again replaces the row"; ok

cd "$D" || exit 1
CK1="fee amount=100000,rate=150 = 1500"
CK2="fee amount=1000,rate=150 = 50"
CK3="fee_ok amount=100000,fee=1500 = true"
[ "$(rc "$FAB" unpack-app fee.fab acc --store store --trust a.pk --check "$CK1" --check "$CK2" --check "$CK3")" = 0 ] || no "accept fee_check"; ok
grep -q "^verdict TRUE $MC" out.txt || no "verdict line"; ok
cmp -s "acc/$MC.zxapp" fee.form || no "the accepted manifest is the form"; ok
# held or refused: nothing is written
[ "$(rc "$FAB" unpack-app fee.fab o1 --store store --check "fee amount=100000,rate=150 = 1501")" = 1 ] || no "a wrong answer is refused"; ok
grep -q "check fails: fee" out.txt || no "and says which check"; ok
[ -e o1 ] && no "written after a refusal"; ok
[ "$(rc "$FAB" unpack-app fee.fab o2 --store store)" = 4 ] || no "no checks: held"; ok
grep -q "unwitnessed" out.txt || no "unwitnessed message"; ok
[ "$(rc "$FAB" unpack-app fee.fab o3 --store store --check "total amount=100,fee=50 = 150")" = 4 ] || no "one language: held"; ok
grep -q "only one language" out.txt || no "one-language message"; ok
[ "$(rc "$FAB" unpack-app fee.fab o4 --store store --check "$CK1" --check "fee amount=1,rate=1 = both")" = 1 ] || no "= both, but they agree"; ok
[ "$(rc "$FAB" unpack-app fee.fab o5 --store store --check "nope a=1 = 1")" = 1 ] || no "a missing entry"; ok
[ -e o2 ] || [ -e o3 ] || [ -e o4 ] || [ -e o5 ] && no "written while held"; ok
# forged, tampered
[ "$(rc "$FAB" unpack-app fee.fab o6 --store store --trust o.pk --check "$CK1")" = 2 ] || no "another signer"; ok
grep -q "not trusted" err || no "trust message"; ok
cp fee.fab bad.fab
printf 'Z' | dd of=bad.fab bs=1 seek=300 conv=notrunc 2> /dev/null
[ "$(rc "$FAB" unpack-app bad.fab o7 --store store --check "$CK1")" = 2 ] || no "a tampered recipe"; ok
"$FAB" pack "$SRC/apps/fee_check.zxapp" file.fab --pk a.pk --sk a.sk > /dev/null || no "file pack"; ok
[ "$(rc "$FAB" unpack-app file.fab o8 --store store --check "$CK1")" = 1 ] || no "a file recipe whose target is not the form"; ok
grep -q "not read from a part" out.txt || no "disk paths do not fit: $(cat out.txt)"; ok
[ -e o6 ] || [ -e o7 ] || [ -e o8 ] && no "written after a forged or tampered recipe"; ok

# ---- an app over a block: sealed inside out -------------------------------------
cd "$SRC" || exit 1
[ "$(rc "$FAB" pack-app apps/murmuration.zxapp "$D/mur.fab" $K --form "$D/mur.form" --store "$D/store")" = 1 ] || no "an unpinned use with no blocks"; ok
grep -q "no manifest for use swarm_shares" "$D/err" || no "unresolved use message"; ok
[ -e "$D/mur.fab" ] && no "a recipe for an unresolved app"; ok
[ "$(rc "$FAB" pack-app apps/murmuration.zxapp "$D/mur.fab" $K --blocks apps/blocks --form "$D/mur.form" --store "$D/store")" = 0 ] || no "pack murmuration"; ok
PIN=$(sed -En 's/^use    swarm_shares 1 (sha256:[0-9a-f]+)( .*)?$/\1/p' "$D/mur.form")
[ -n "$PIN" ] || no "the use is pinned"; ok
SS=$(grep -l '^id       swarm_shares' "$D"/store/* | head -1)
[ -n "$SS" ] && grep -q '^logic    kron cycle_share <<$' "$SS" || no "the block is sealed (inline) in the store"; ok
cd "$D" || exit 1
[ "$(rc "$FAB" unpack-app mur.fab m1 --store store)" = 4 ] || no "murmuration with no checks is held"; ok
grep -q "fit TRUE" out.txt || no "murmuration fits: $(cat out.txt)"; ok
mv "$SS" ss.bak
[ "$(rc "$FAB" unpack-app mur.fab m2 --store store)" = 2 ] || no "a part missing from the store"; ok
grep -q "not in the store" err || no "missing part message"; ok
sed 's/kron cycle_share/kron  cycle_share/' ss.bak > "$SS"
[ "$(rc "$FAB" unpack-app mur.fab m3 --store store)" = 2 ] || no "a part that lies"; ok
grep -q "does not match its CID" err || no "lying part message"; ok
mv ss.bak "$SS"
[ -e m2 ] || [ -e m3 ] && no "written after a bad part"; ok

# ---- what pack-app refuses ------------------------------------------------------------
cd "$SRC" || exit 1
# two different manifests at one id and version: never picked between
mkdir -p "$D/b2"
cp apps/blocks/swarm_shares.zxblock "$D/b2/a.zxblock"
sed 's/^name     Swarm shares$/name     Swarm shares too/' apps/blocks/swarm_shares.zxblock > "$D/b2/b.zxblock"
[ "$(rc "$FAB" pack-app apps/murmuration.zxapp "$D/x.fab" $K --blocks "$D/b2")" = 1 ] || no "two candidates"; ok
grep -q "name one by its pin" "$D/err" || no "ambiguous message"; ok
# a pin to a block that is not sealed (its sources are paths)
RAWPIN=sha256:$(sha256sum apps/blocks/swarm_shares.zxblock | cut -c1-64)
sed -E "s/^(use    swarm_shares 1)( |\$)/\1 $RAWPIN\2/" apps/murmuration.zxapp > "$D/r/m.zxapp"
[ "$(rc "$FAB" pack-app "$D/r/m.zxapp" "$D/x.fab" $K --blocks apps/blocks)" = 1 ] || no "a pin to an unsealed block"; ok
grep -q "pinned but not sealed" "$D/err" || no "unsealed message: $(cat "$D/err")"; ok
# a pin to the sealed form is carried as it is
sed -E "s/^(use    swarm_shares 1)( |\$)/\1 $PIN\2/" apps/murmuration.zxapp > "$D/r/m2.zxapp"
cp "$SS" "$D/b/ss.zxblock"
[ "$(rc "$FAB" pack-app "$D/r/m2.zxapp" "$D/x.fab" $K --blocks "$D/b")" = 0 ] || no "a pin to a sealed block"; ok
# a source that is missing, or too big
printf 'zxapp 1\nid gone\nprovides f num a\nlogic kron f nothing/here.kron\n' > "$D/r/gone.zxapp"
[ "$(rc "$FAB" pack-app "$D/r/gone.zxapp" "$D/x.fab" $K --root "$D/r")" = 1 ] || no "a missing source"; ok
head -c 70000 /dev/zero | tr '\0' 'a' > "$D/r/x/big.kron"
printf 'zxapp 1\nid big\nprovides f num a\nlogic kron f x/big.kron\n' > "$D/r/big.zxapp"
[ "$(rc "$FAB" pack-app "$D/r/big.zxapp" "$D/x.fab" $K --root "$D/r")" = 1 ] || no "a source over 64 KiB"; ok
# an empty scaffold is held, and no recipe is signed for it
rm -f "$D/x.fab"
printf 'zxapp 1\nid glue\nversion 1.0.0\n' > "$D/r/glue.zxapp"
[ "$(rc "$FAB" pack-app "$D/r/glue.zxapp" "$D/x.fab" $K)" = 4 ] || no "an empty scaffold"; ok
grep -q "empty scaffold" "$D/err" || no "scaffold message"; ok
[ -e "$D/x.fab" ] && no "a recipe for an empty scaffold"; ok
# a source that cannot be inline goes as a cid: part, and still runs
head -c -1 apps/blocks/fee_check/fee.kron > "$D/r/x/fee.kron"
printf 'zxapp 1\nid feek\nversion 1.0.0\nprovides fee num amount rate\nlogic kron fee x/fee.kron\n' > "$D/r/feek.zxapp"
[ "$(rc "$FAB" pack-app "$D/r/feek.zxapp" "$D/feek.fab" $K --root "$D/r" --form "$D/feek.form" --store "$D/store")" = 0 ] || no "pack a cid: source"; ok
grep -q '^logic kron fee cid:bafkrei' "$D/feek.form" || no "the source is a cid: part"; ok
cd "$D" || exit 1
[ "$(rc "$FAB" unpack-app feek.fab k1 --store store --check "$CK1")" = 4 ] || no "a cid: source runs: $(cat out.txt)"; ok
grep -q "only one language witnesses fee" out.txt || no "and is witnessed by its one language"; ok
cd "$SRC" || exit 1

# ---- no floating point, anywhere ------------------------------------------------------
# the C an app names (view, model) is checked before anything is signed
mkdir -p "$D/f/m"
fl() { # fl NAME C-TEXT: a model with that text; prints pack-app's exit status
    printf '%s\n' "$2" > "$D/f/m/$1.c"
    printf 'zxapp 1\nid fl\nversion 1.0.0\nmodel m/%s.c\nprovides fee num amount rate\nlogic kron fee <<\n%s\n.\n' \
        "$1" "$(cat "$SRC/apps/blocks/fee_check/fee.kron")" > "$D/f/$1.zxapp"
    rm -f "$D/f/x.fab"
    rc "$FAB" pack-app "$D/f/$1.zxapp" "$D/f/x.fab" $K --root "$D/f"
}
for c in 'double x;' 'static float f(void);' 'int x = 1.5;' 'long y = 3e2;' 'int z = .5 > 0;' \
         'unsigned h = 0x1p3;' '#include <math.h>' '# include "tgmath.h"' '_Float64 q;' 'long double r;'; do
    [ "$(fl bad "$c")" = 1 ] || no "a float in a model: $c"; ok
    grep -q "floating point in the model" "$D/err" || no "float message: $c"; ok
    [ -e "$D/f/x.fab" ] && no "a recipe signed over a float: $c"; ok
done
for c in '/* 1.5 double */ int x;' '// float 2.0' 'const char *s = "3.25 float";' "char c = '.';" \
         'unsigned h = 0x1e5;' 'unsigned long long u = 10ULL;' 'struct p { int x; } a[2]; int b = a[1].x;' \
         '#include <stdint.h>'; do
    [ "$(fl good "$c")" = 0 ] || no "no float, yet refused: $c ($(cat "$D/err"))"; ok
done
printf 'double h;\n' > "$D/f/m/good.h"
[ "$(fl good 'int x;')" = 1 ] && grep -q "header" "$D/err" || no "a float in the model's header"; ok
# every zxvd method the C calls is in the zxvd spec
API="$SRC/../../api/zxvd.openrpc.json"
[ "$(fl good 'int f(zxm_link_t *l) { zxm_begin(l, "swarm.state"); return 0; }')" = 1 ] && grep -q "give --api" "$D/err" || no "a zxvd call with no spec to check it"; ok
rm -f "$D/f/m/good.h"
K0=$K; K="$K --api $API"
[ "$(fl good 'int f(zxm_link_t *l) { zxm_begin(l, "swarm.state"); return 0; }')" = 0 ] || no "a method in the spec: $(cat "$D/err")"; ok
[ "$(fl bad 'int f(zxm_link_t *l) { zxm_begin(l, "swarm.state"); zxm_begin(l,
    "place.teleport"); zxm_begin(l, "screen.melt"); return 0; }')" = 1 ] || no "methods not in the spec"; ok
grep -q "calls zxvd method place.teleport" "$D/err" && grep -q "calls zxvd method screen.melt" "$D/err" || no "every missing method is named: $(cat "$D/err")"; ok
grep -q "swarm.state" "$D/err" && no "a method in the spec reported missing"; ok
[ "$(fl good 'void zxm_begin(zxm_link_t *l, const char *method); /* zxm_begin(l, "x.y") */')" = 0 ] || no "a declaration is not a call"; ok
K=$K0
# the runtimes refuse floats in logic (R11): Eve has no float type, and Z-Wasm
# refuses an f32 module that loads as i32
sed 's/^cents = 0 .. 100000000$/cents = float/' "$SRC/apps/blocks/fee_check/fee_ok.eve" > "$D/f/m/e.eve"
printf 'zxapp 1\nid fe\nversion 1.0.0\nprovides fee_ok truth amount fee\nlogic eve fee_ok m/e.eve\n' > "$D/f/fe.zxapp"
[ "$(rc "$FAB" pack-app "$D/f/fe.zxapp" "$D/f/fe.fab" $K --root "$D/f" --store "$D/store")" = 0 ] || no "pack an Eve float"; ok
(cd "$D" && "$FAB" unpack-app f/fe.fab f/o1 --store store > out.txt 2>&1); [ $? = 1 ] || no "an Eve float fits"; ok
grep -q "is not an Eve type" "$D/out.txt" || no "Eve float message: $(cat "$D/out.txt")"; ok
for t in 7d 7f; do
    python3 -I -c '
import sys
v = int(sys.argv[1], 16)
t = bytes([0x60, 1, v, 1, v])
m = b"\0asm\x01\0\0\0" + bytes([1, len(t) + 1, 1]) + t + bytes([3, 2, 1, 0])
ex = bytes([1, 1]) + b"f" + bytes([0, 0])
body = bytes([0, 0x20, 0, 0x0B])
m += bytes([7, len(ex)]) + ex + bytes([10, len(body) + 2, 1, len(body)]) + body
open(sys.argv[2], "wb").write(m)' "$t" "$D/f/m/f$t.wasm"
    printf 'zxapp 1\nid fw\nversion 1.0.0\nprovides f num a\nlogic wasm f m/f%s.wasm\n' "$t" > "$D/f/w$t.zxapp"
    [ "$(rc "$FAB" pack-app "$D/f/w$t.zxapp" "$D/f/w$t.fab" $K --root "$D/f" --store "$D/store")" = 0 ] || no "pack wasm $t"; ok
    (cd "$D" && "$FAB" unpack-app "f/w$t.fab" "f/ow$t" --store store > out.txt 2>&1)
    if [ $t = 7d ]; then grep -q "^fit FALSE: .*Z-Wasm refused" "$D/out.txt" || no "an f32 module fits: $(cat "$D/out.txt")"; ok
    else grep -q "^fit TRUE" "$D/out.txt" || no "the same module in i32 does not fit: $(cat "$D/out.txt")"; ok; fi
done

# ---- FABRICATED.tsv and the matrix --------------------------------------------------
sed 's/^name   Fee Check$/name   Fee Check 2/' apps/fee_check.zxapp > "$D/r/fee2.zxapp"
cp "$T" "$D/t.bak"
[ "$(rc "$FAB" pack-app "$D/r/fee2.zxapp" "$D/x.fab" $K --root . --tsv "$T")" = 1 ] || no "a new manifest at a fabricated version"; ok
grep -q "needs a new version" "$D/err" || no "version message"; ok
cmp -s "$T" "$D/t.bak" || no "the table is untouched after a refusal"; ok
# a new version slices the old form: only the change travels
sed 's/^name   Fee Check$/name   Fee Check 2/' "$D/fee.form" > "$D/r/fee2.form"
"$FAB" pack-app "$D/r/fee2.form" "$D/fee2.fab" $K --prev "$D/fee.form" --store "$D/store" > "$D/p2.txt" 2> "$D/err" || no "pack with --prev"; ok
DL=$(sed -n 's/.*diff \([0-9]*\) bytes/\1/p' "$D/p2.txt")
[ "$DL" -lt 100 ] || no "the diff carries only the change ($DL bytes)"; ok
cd "$D" || exit 1
[ "$(rc "$FAB" unpack-app fee2.fab f2 --store store --check "$CK1")" = 0 ] || no "the new version is accepted: $(cat out.txt)"; ok
cd "$SRC" || exit 1
# layers: every part needs one, from its row, inside the Zeckendorf ancestors
L="$D/L.tsv"
SSC=$(basename "$SS")
printf '# id\tversion\tmanifest_cid\trecipe_cid\tlayer\n' > "$L"
[ "$(rc "$FAB" pack-app apps/murmuration.zxapp "$D/x.fab" $K --blocks apps/blocks --tsv "$L" --layer 12)" = 1 ] || no "a part with no layer"; ok
grep -q "no matrix layer" "$D/err" || no "no-layer message"; ok
printf 'swarm_shares\t1.0.0\t%s\t-\t7\n' "$SSC" >> "$L"
[ "$(rc "$FAB" pack-app apps/murmuration.zxapp "$D/x.fab" $K --blocks apps/blocks --tsv "$L" --layer 12)" = 1 ] || no "layer 12 using layer 7"; ok
grep -q "Zeckendorf" "$D/err" || no "ancestor message"; ok
sed -i 's/\t7$/\t8/' "$L"
[ "$(rc "$FAB" pack-app apps/murmuration.zxapp "$D/mL.fab" $K --blocks apps/blocks --tsv "$L" --layer 12)" = 0 ] || no "layer 12 using layer 8"; ok
grep -q "^murmuration	1.0.0	bafk.*	12\$" "$L" || no "the row records the layer"; ok
cd "$D" || exit 1
[ "$(rc "$FAB" unpack-app mL.fab l1 --store store --tsv "$L" --layer 12)" = 4 ] && grep -q "fit TRUE" out.txt || no "layered fit on unpack: $(cat out.txt)"; ok
[ "$(rc "$FAB" unpack-app mL.fab l2 --store store --tsv "$L" --layer 7)" = 1 ] || no "the same app at layer 7"; ok
[ "$(rc "$FAB" unpack-app mL.fab l3 --store store --layer 12)" = 1 ] || no "--layer needs --tsv"; ok
echo "$n checks"
