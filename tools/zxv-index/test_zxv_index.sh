#!/bin/sh
# test_zxv_index.sh BIN TMP CORE-CID — keygen, scan, build and check an
# overlay end to end, and the refusals a user meets first.
set -u
bin=$1; t=$2; core=$3
rm -rf "$t" && mkdir -p "$t/files" || exit 1
fail() { echo "[FAIL] zxv-index: $*"; exit 1; }
printf 'radar sweep 1\n' > "$t/files/a.txt"
printf 'radar sweep 2\n' > "$t/files/b.txt"
head -c 1048577 /dev/zero > "$t/big.bin"
"$bin" keygen "$t/pk" "$t/sk" 0101010101010101010101010101010101010101010101010101010101010101 || fail keygen
[ "$(wc -c < "$t/pk")" -eq 1952 ] && [ "$(wc -c < "$t/sk")" -eq 4032 ] || fail "key sizes"
[ "$(stat -c %a "$t/sk")" = 600 ] || fail "the secret key is readable by others"
"$bin" scan "$t/files" 520 sensor > "$t/map.tsv" || fail scan
grep -q '^0x5002080100000000	b' "$t/map.tsv" && grep -q '^0x5002080200000000	b' "$t/map.tsv" || fail "scan words: $(cat "$t/map.tsv")"
cid=$("$bin" build "$t/map.tsv" 520 "$core" "$t/pk" "$t/sk" "$t/w.idx") || fail build
case "$cid" in bafkrei*) ;; *) fail "overlay CID $cid" ;; esac
"$bin" check "$t/w.idx" | grep -q "^overlay $cid" || fail check
"$bin" check "$t/w.idx" | grep -q "2 entries" || fail "entry count"
# refusals
"$bin" scan "$t/files" 525 sensor >/dev/null 2>&1 && fail "division 525"
"$bin" scan "$t/files" 520 code >/dev/null 2>&1 && fail "the code domain"
mkdir -p "$t/bigdir" && cp "$t/big.bin" "$t/bigdir/" && "$bin" scan "$t/bigdir" 520 data >/dev/null 2>&1 && fail "a file over 1 MiB"
sed 's/^0x5002/0x1002/' "$t/map.tsv" > "$t/code.tsv"
"$bin" build "$t/code.tsv" 520 "$core" "$t/pk" "$t/sk" "$t/c.idx" >/dev/null 2>&1 && fail "code words"
"$bin" build "$t/map.tsv" 530 "$core" "$t/pk" "$t/sk" "$t/c.idx" >/dev/null 2>&1 && fail "the wrong division"
"$bin" build "$t/map.tsv" 520 bnotacid "$t/pk" "$t/sk" "$t/c.idx" >/dev/null 2>&1 && fail "a bad parent"
printf 'nonsense\n' > "$t/bad.tsv"
"$bin" build "$t/bad.tsv" 520 "$core" "$t/pk" "$t/sk" "$t/c.idx" >/dev/null 2>&1 && fail "a bad table"
cp "$t/w.idx" "$t/x.idx" && printf '\001' | dd of="$t/x.idx" bs=1 seek=100 conv=notrunc 2>/dev/null
"$bin" check "$t/x.idx" >/dev/null 2>&1 && fail "a changed overlay"
"$bin" >/dev/null 2>&1; [ $? -eq 2 ] || fail usage
echo "[PASS] zxv-index (keygen, scan, build, check and refusals)"
