#!/bin/sh
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# test_zxvc.sh — end-to-end test of the model converter (tools/zxvc.c) on the
# small files gen_zxvc_fixtures.py writes. Run from kernel/:
#   sh tools/test_zxvc.sh [TMPDIR]
# Every line that should fail is checked to fail; prints one summary line.
set -u
# A one-shot command-line tool: memory is returned at exit.
export ASAN_OPTIONS=detect_leaks=0
T=$(mktemp -d "${1:-/tmp}/zxvc.XXXXXX") || exit 1
trap 'rm -rf "$T"' EXIT
pass=0
fail=0
ok() { pass=$((pass + 1)); }
bad() { fail=$((fail + 1)); echo "FAIL: $*"; }
yes_() { if "$@" > "$T/out" 2>&1; then ok; else bad "$*"; cat "$T/out"; fi; }
no_() { if "$@" > "$T/out" 2>&1; then bad "should fail: $*"; cat "$T/out"; else ok; fi; }
has() { if grep -q -- "$1" "$T/out"; then ok; else bad "output lacks '$1'"; cat "$T/out"; fi; }

gcc -std=c11 -O2 -Wall -Werror -Wextra -fsanitize=address,undefined -fno-sanitize-recover=all \
    -Iinclude -Isrc/modbind -Isrc/tensor -Isrc/pqsec tools/zxvc.c src/tensor/zt_zxvc.c \
    src/tensor/zt_gguf.c src/tensor/zt.c src/robin_debanks/sha256.c src/pqsec/pq_mldsa87.c \
    src/pqsec/mldsa/fips202.c -o "$T/zxvc" 2> "$T/build.log" || { cat "$T/build.log"; exit 1; }
Z="$T/zxvc"
python3 -I tools/gen_zxvc_fixtures.py "$T" || exit 1

# GGUF: convert, bit-exact check, rebuild the exact source file.
yes_ "$Z" convert "$T/g.zxvc" "$T/model.gguf"
has "3 exact, 2 native"
yes_ "$Z" check "$T/g.zxvc" "$T/model.gguf"
has "bit-exact: 5 tensors"
yes_ "$Z" export "$T/g.zxvc" "$T/back.gguf"
has "identical to the source file"
yes_ cmp "$T/back.gguf" "$T/model.gguf"
yes_ "$Z" verify "$T/g.zxvc"
has "hashes OK: 3 parts, 5 tensors"
# Converting twice gives the same package (deterministic, unsigned).
yes_ "$Z" convert "$T/g2.zxvc" "$T/model.gguf"
yes_ cmp "$T/g.zxvc" "$T/g2.zxvc"

# safetensors, one file and two shards.
yes_ "$Z" convert "$T/s.zxvc" "$T/model.safetensors" --card tools/gen_zxvc_fixtures.py
has "licence: mit"
yes_ "$Z" check "$T/s.zxvc" "$T/model.safetensors"
has "bit-exact: 3 tensors"
yes_ "$Z" info "$T/s.zxvc"
has "part card"
no_ "$Z" export "$T/s.zxvc" "$T/x.gguf"
no_ "$Z" convert "$T/sh.zxvc" "$T/shard1.safetensors" "$T/shard2.safetensors"
yes_ "$Z" convert "$T/sh.zxvc" "$T/shard1.safetensors" "$T/shard2.safetensors" --license apache-2.0
yes_ "$Z" check "$T/sh.zxvc" "$T/shard1.safetensors" "$T/shard2.safetensors"
no_ "$Z" check "$T/sh.zxvc" "$T/shard1.safetensors"
no_ "$Z" check "$T/sh.zxvc" "$T/shard2.safetensors" "$T/shard1.safetensors"

# Nothing is rounded: NaN and a 2^-149 next to 1.0 stop the conversion.
no_ "$Z" convert "$T/x.zxvc" "$T/nan.safetensors"
has "cannot be stored exactly"
no_ "$Z" convert "$T/x.zxvc" "$T/wide.safetensors"

# Licence gate.
no_ "$Z" convert "$T/x.zxvc" "$T/nolicence.gguf"
has "no licence"
yes_ "$Z" convert "$T/x.zxvc" "$T/nolicence.gguf" --license apache-2.0
no_ "$Z" convert "$T/x.zxvc" "$T/model.gguf" --license mit
has "disagrees"
no_ "$Z" convert "$T/x.zxvc" "$T/llama.gguf"
has "not on the commercial-use allow-list"
yes_ "$Z" convert "$T/l.zxvc" "$T/llama.gguf" --license-override "owner accepts the Llama 3 terms"
yes_ "$Z" verify "$T/l.zxvc"
has "owner override"
yes_ "$Z" info "$T/l.zxvc"
has "owner override: owner accepts"

# Signing: a good signature, a wrong key, a tampered table, a tampered payload.
yes_ "$Z" keygen "$T/a.pk" "$T/a.sk"
yes_ "$Z" keygen "$T/b.pk" "$T/b.sk"
yes_ "$Z" convert "$T/sg.zxvc" "$T/model.gguf" --sign "$T/a.sk" --pub "$T/a.pk"
yes_ "$Z" verify "$T/sg.zxvc" --pub "$T/a.pk"
has "signature OK"
no_ "$Z" verify "$T/sg.zxvc" --pub "$T/b.pk"
has "different key"
no_ "$Z" verify "$T/g.zxvc" --pub "$T/a.pk"
has "not signed"
no_ "$Z" convert "$T/x.zxvc" "$T/model.gguf" --sign "$T/a.sk"
yes_ "$Z" check "$T/sg.zxvc" "$T/model.gguf"

flip() { # file offset: xor one byte with 0x01
    python3 -I -c "import sys
p,o=sys.argv[1],int(sys.argv[2])
b=bytearray(open(p,'rb').read()); b[o]^=1; open(p,'wb').write(b)" "$1" "$2"
}
# Byte 300 is in the tensor table (header 80 + 3 parts x 80 = 320 is where it
# starts; 300 is the licence part's hash): the digest changes, the signature fails.
cp "$T/sg.zxvc" "$T/t1.zxvc"
flip "$T/t1.zxvc" 300
no_ "$Z" verify "$T/t1.zxvc" --pub "$T/a.pk"
# The last tensor payload byte before the signature: its hash fails.
SIGOFF=$(python3 -I -c "import struct,sys;print(struct.unpack_from('<Q',open(sys.argv[1],'rb').read(),56)[0])" "$T/sg.zxvc")
cp "$T/sg.zxvc" "$T/t2.zxvc"
flip "$T/t2.zxvc" $((SIGOFF - 1))
no_ "$Z" verify "$T/t2.zxvc"
has "SHA-256 mismatch"
no_ "$Z" check "$T/t2.zxvc" "$T/model.gguf"
# A signature byte: the signature fails, the hashes still pass.
cp "$T/sg.zxvc" "$T/t3.zxvc"
flip "$T/t3.zxvc" $((SIGOFF + 100))
yes_ "$Z" verify "$T/t3.zxvc"
no_ "$Z" verify "$T/t3.zxvc" --pub "$T/a.pk"
has "does not verify"
# An exact payload byte in the unsigned package: check names the tensor.
cp "$T/g.zxvc" "$T/t4.zxvc"
OFF=$(python3 -I -c "import struct,sys
b=open(sys.argv[1],'rb').read(); to=struct.unpack_from('<Q',b,32)[0]
print(struct.unpack_from('<Q',b,to+248+152)[0]+5)" "$T/g.zxvc")
flip "$T/t4.zxvc" "$OFF"
no_ "$Z" check "$T/t4.zxvc" "$T/model.gguf"
has "MISMATCH blk.0.attn_q.weight"
# The source changed after conversion: check catches it.
cp "$T/model.gguf" "$T/m2.gguf"
flip "$T/m2.gguf" 1000
no_ "$Z" check "$T/g.zxvc" "$T/m2.gguf"
no_ "$Z" verify "$T/model.gguf"

echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
