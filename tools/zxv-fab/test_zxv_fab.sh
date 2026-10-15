#!/bin/sh
# test_zxv_fab.sh — end-to-end checks of the zxv-fab tool.
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# usage: test_zxv_fab.sh ZXV_FAB WORKDIR
set -u
FAB=$1
D=$2
rm -rf "$D" && mkdir -p "$D/store" "$D/blocks" || exit 1
n=0
ok() { n=$((n + 1)); }
no() { echo "FAIL: $1"; exit 1; }

# keys from fixed seeds (reproducible)
S1=$(printf '%064d' 1)
S2=$(printf '%064d' 2)
K1=3$(printf '%0127d' 0)
K2=4$(printf '%0127d' 0)
"$FAB" keygen-sign "$D/a.pk" "$D/a.sk" "$S1" > "$D/a.id" || no keygen-sign; ok
"$FAB" keygen-sign "$D/b.pk" "$D/b.sk" "$S2" > /dev/null || no keygen-sign2; ok
"$FAB" keygen-kem "$D/r.ek" "$D/r.dk" "$K1" > /dev/null || no keygen-kem; ok
"$FAB" keygen-kem "$D/x.ek" "$D/x.dk" "$K2" > /dev/null || no keygen-kem2; ok
mode() { stat -c %a "$1" 2>/dev/null || stat -f %Lp "$1"; } # GNU, then BSD/macOS
[ "$(mode "$D/a.sk")" = 600 ] && [ "$(mode "$D/r.dk")" = 600 ] || no "secret key mode"; ok
"$FAB" keygen-sign "$D/c.pk" "$D/c.sk" "$S1" > "$D/c.id" && cmp -s "$D/a.pk" "$D/c.pk" || no "seeded keys not reproducible"; ok

# public blocks: three pseudo-random files; the target mixes them with private bytes
head -c 300000 /dev/urandom > "$D/blocks/b1"
head -c 100000 /dev/urandom > "$D/blocks/b2"
head -c 50000 /dev/urandom > "$D/blocks/decoy"
{ head -c 200000 "$D/blocks/b1"; printf 'private line one\n'; tail -c 60000 "$D/blocks/b2";
  printf 'private line two\n'; } > "$D/target"
"$FAB" put "$D/store" "$D/blocks/b1" "$D/blocks/b2" "$D/blocks/decoy" > /dev/null || no put; ok

# expected CID: Kubo's, when an ipfs binary is present
if command -v ipfs > /dev/null 2>&1; then
    [ "$("$FAB" cid "$D/target")" = "$(ipfs add -nQ --cid-version 1 "$D/target")" ] || no "cid differs from ipfs add"; ok
fi
CID=$("$FAB" cid "$D/target")

# public recipe (app install)
"$FAB" pack "$D/target" "$D/pub.fab" --pk "$D/a.pk" --sk "$D/a.sk" \
    "$D/blocks/b1" "$D/blocks/b2" "$D/blocks/decoy" > "$D/pack.txt" || no pack; ok
grep -q "target $CID" "$D/pack.txt" || no "pack target"; ok
# 34 private bytes; a random block byte can match a private one by chance and absorb it
DL=$(sed -n 's/^diff \([0-9]*\) bytes.*/\1/p' "$D/pack.txt")
[ "$DL" -ge 30 ] && [ "$DL" -le 34 ] || no "pack diff size: $(cat "$D/pack.txt")"; ok
grep -q "2 used + 1 decoy" "$D/pack.txt" || no "pack decoy count"; ok
[ "$(wc -c < "$D/pub.fab")" -lt 8000 ] || no "public recipe too large"; ok
"$FAB" inspect "$D/pub.fab" > "$D/insp.txt" || no inspect; ok
[ "$(grep -c '^block ' "$D/insp.txt")" = 3 ] || no "inspect blocks"; ok
"$FAB" unpack "$D/pub.fab" "$D/out" --store "$D/store" > /dev/null || no unpack; ok
cmp -s "$D/target" "$D/out" || no "public round trip"; ok
"$FAB" inspect "$D/pub.fab" --trust "$D/a.pk" > /dev/null || no "trust a"; ok
"$FAB" inspect "$D/pub.fab" --trust "$D/b.pk" > /dev/null 2>&1 && no "trust b accepted"; ok

# sealed recipe (private data)
"$FAB" pack "$D/target" "$D/sec.fab" --pk "$D/a.pk" --sk "$D/a.sk" --to "$D/r.ek" \
    "$D/blocks/b1" "$D/blocks/b2" > /dev/null || no "pack sealed"; ok
grep -q "private line" "$D/sec.fab" && no "private bytes in clear"; ok
"$FAB" inspect "$D/sec.fab" | grep -q '^block ' && no "sealed blocks listed without key"; ok
"$FAB" inspect "$D/sec.fab" --dk "$D/r.dk" | grep -q '^block ' || no "sealed inspect with key"; ok
rm -f "$D/out"
"$FAB" unpack "$D/sec.fab" "$D/out" --store "$D/store" --dk "$D/x.dk" > /dev/null 2>&1 && no "wrong key"; ok
[ -e "$D/out" ] && no "output written on failure"; ok
"$FAB" unpack "$D/sec.fab" "$D/out" --store "$D/store" > /dev/null 2>&1 && no "no key"; ok
"$FAB" unpack "$D/sec.fab" "$D/out" --store "$D/store" --dk "$D/r.dk" > /dev/null || no "unpack sealed"; ok
cmp -s "$D/target" "$D/out" || no "sealed round trip"; ok
[ "$(mode "$D/out")" = 600 ] || no "output mode"; ok

# a block that is missing or corrupted in the store
B1=$("$FAB" put "$D/store" "$D/blocks/b1" | cut -d' ' -f1)
cp "$D/store/$B1" "$D/b1.bak"
printf 'X' | dd of="$D/store/$B1" bs=1 seek=1000 conv=notrunc 2> /dev/null
rm -f "$D/out"
"$FAB" unpack "$D/pub.fab" "$D/out" --store "$D/store" > /dev/null 2> "$D/err" && no "corrupt block"; ok
grep -q "does not match its CID" "$D/err" || no "corrupt block message"; ok
[ -e "$D/out" ] && no "output after corrupt block"; ok
rm "$D/store/$B1"
"$FAB" unpack "$D/pub.fab" "$D/out" --store "$D/store" > /dev/null 2> "$D/err" && no "missing block"; ok
grep -q "not in the store" "$D/err" || no "missing block message"; ok
cp "$D/b1.bak" "$D/store/$B1"

# a flipped recipe byte fails the signature
cp "$D/sec.fab" "$D/bad.fab"
printf 'Z' | dd of="$D/bad.fab" bs=1 seek=3000 conv=notrunc 2> /dev/null
"$FAB" unpack "$D/bad.fab" "$D/out2" --store "$D/store" --dk "$D/r.dk" > /dev/null 2> "$D/err" && no "tampered"; ok
grep -q "signature does not verify" "$D/err" || no "tampered message"; ok
head -c 100 "$D/sec.fab" > "$D/short.fab"
"$FAB" inspect "$D/short.fab" > /dev/null 2>&1 && no "truncated"; ok

# air-gap mode refuses while any interface is up, and says so
if grep -qv '^down' /sys/class/net/*/operstate 2> /dev/null && ls /sys/class/net | grep -qv '^lo$'; then
    up=0
    for i in /sys/class/net/*; do
        [ "$(basename "$i")" = lo ] && continue
        case "$(cat "$i/operstate")" in down|lowerlayerdown|notpresent) ;; *) up=1 ;; esac
    done
    if [ $up = 1 ]; then
        "$FAB" unpack "$D/sec.fab" "$D/out3" --store "$D/store" --dk "$D/r.dk" --airgap > /dev/null 2> "$D/err" && no "airgap with network up"; ok
        grep -q "air-gap seal is not in place" "$D/err" || no "airgap message"; ok
    fi
fi
echo "$n checks"
