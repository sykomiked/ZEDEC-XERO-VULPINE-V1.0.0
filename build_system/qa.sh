#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Focused host regression gate; does not replace the legacy verify-all suite.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/qa build/arm64/gen
PYTHONDONTWRITEBYTECODE=1 python3 tests/qa/test_qa_tools.py
PYTHONDONTWRITEBYTECODE=1 python3 tests/qa/test_agent_manifest.py
bash build_system/gen_phase_table.sh build/arm64/gen/zxv_phase_table.h
cc -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined \
  -Ikernel/include -Ikernel/src/lpres -Ikernel/src/event_space \
  -Ikernel/src/surplus -Ikernel/src/modbind -Ibuild/arm64/gen \
  tests/qa/test_paraconsistent.c kernel/src/lpres/lpres.c \
  kernel/src/lpres/lpres_core.c -o build/qa/test_paraconsistent
UBSAN_OPTIONS=halt_on_error=1 build/qa/test_paraconsistent
cc -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined \
  -Ikernel/include -Ikernel/src/modbind -Ibuild/arm64/gen \
  -Ikernel/src/tls -Ikernel/src/robin_debanks tests/qa/test_crypto.c \
  kernel/src/tls/hkdf.c kernel/src/tls/aead.c kernel/src/tls/x25519.c \
  kernel/src/robin_debanks/sha256.c -o build/qa/test_crypto
UBSAN_OPTIONS=halt_on_error=1 build/qa/test_crypto
cc -shared -fPIC -O2 -Ikernel/include -Ikernel/src/modbind -Ibuild/arm64/gen \
  kernel/src/tls/hkdf.c kernel/src/robin_debanks/sha256.c -o build/qa/libcrypto_oracle.so
PYTHONDONTWRITEBYTECODE=1 python3 tests/qa/test_crypto_differential.py "$PWD/build/qa/libcrypto_oracle.so"
for backend in wide narrow; do
  rational_flags=()
  if [[ "$backend" == narrow ]]; then rational_flags=(-U__SIZEOF_INT128__); fi
  cc -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined \
    "${rational_flags[@]}" -Ikernel/src/rational tests/qa/test_rational_bounds.c \
    kernel/src/rational/rational.c -o "build/qa/test_rational_$backend"
  UBSAN_OPTIONS=halt_on_error=1 "build/qa/test_rational_$backend"
  cc -shared -fPIC -O2 "${rational_flags[@]}" -Ikernel/src/rational \
    kernel/src/rational/rational.c -o "build/qa/librational_$backend.so"
  python3 tests/qa/test_rational_differential.py "$PWD/build/qa/librational_$backend.so"
done
cc -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined \
  -Ikernel/src/legacy tests/qa/test_corridor_copybook.c kernel/src/legacy/cobol.c \
  -o build/qa/test_corridor_copybook
cobc -x -free -Ibindings/cobol tests/qa/corridor_record.cob -o build/qa/corridor_record
build/qa/corridor_record
UBSAN_OPTIONS=halt_on_error=1 build/qa/test_corridor_copybook build/qa/corridor-cobol.bin
cc -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -DTEST_HOST \
  -Ikernel/include -Ikernel/src/modbind -Ikernel/src/pay -Ikernel/src/tensor \
  -Ikernel/src/mlkem -Ikernel/src/swarm -Ikernel/src/robin_debanks \
  -Ikernel/src/zcapital -Ikernel/src/surplus tests/qa/test_dividend.c \
  kernel/src/pay/pay_dividend.c kernel/src/pay/pay_ledger.c kernel/src/pay/pay_assure.c \
  kernel/src/pay/pay_util.c kernel/src/tensor/zt.c kernel/src/mlkem/keccak.c \
  kernel/src/swarm/swarm_market.c kernel/src/swarm/swarm_budget.c \
  kernel/src/swarm/swarm_emotion.c -o build/qa/test_dividend
UBSAN_OPTIONS=halt_on_error=1 build/qa/test_dividend
for target in aarch64-none-elf armv7-none-eabi x86_64-none-elf riscv64-none-elf riscv32-none-elf; do
  clang --target="$target" -ffreestanding -std=c11 -O2 -Wall -Wextra -Werror \
    -Ikernel/src/pay -c kernel/src/pay/pay_dividend.c -o "build/qa/pay_dividend_$target.o"
  if nm -u "build/qa/pay_dividend_$target.o" | grep -v ' pay_' | grep -q .; then
    nm -u "build/qa/pay_dividend_$target.o"
    exit 1
  fi
done
PYTHONDONTWRITEBYTECODE=1 python3 install/install_zxv.py --selftest
cc -O1 -g -fsanitize=address,undefined -Ikernel/src/loader \
  kernel/tests/fuzz/fuzz_elf.c kernel/src/loader/elf.c -o build/qa/fuzz_elf
UBSAN_OPTIONS=halt_on_error=1 build/qa/fuzz_elf "${FUZZ_ELF_ITERS:-200000}"
cc -O1 -g -fsanitize=address,undefined -DZXVFS_HOST -Ikernel/include \
  -Ikernel/src/zxvfs -Ikernel/src/modbind -Ibuild/arm64/gen \
  kernel/tests/fuzz/fuzz_zxvfs.c kernel/src/zxvfs/zxvfs.c -o build/qa/fuzz_zxvfs
UBSAN_OPTIONS=halt_on_error=1 build/qa/fuzz_zxvfs "${FUZZ_ZXVFS_ITERS:-60000}"
