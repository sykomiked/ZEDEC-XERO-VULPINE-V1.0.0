<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
     SPDX-License-Identifier: Apache-2.0 -->
# fuzz/: parser fuzzers and ledger property harnesses

Every module that parses bytes from outside the machine has a libFuzzer harness here.
The four economic ledgers have state-machine property harnesses. Each harness is one C
file that defines `LLVMFuzzerTestOneInput`. It builds in two ways:

| build | compiler | what it is for |
|---|---|---|
| `build/fuzz/<name>` | clang, `-fsanitize=fuzzer,address,undefined` | coverage-guided fuzzing |
| `build/replay/<name>` | gcc (or any C11 compiler), ASan + UBSan, linked with `common/fuzz_main.c` | replays the committed corpus without clang; seeded random mode for the property harnesses |

`make -C kernel verify-all` runs `make -C fuzz replay` and a short seeded property run.
So every crash input committed as a regression seed stays fixed on machines that have no
clang.

## Targets

| harness | module and entry points | input |
|---|---|---|
| `fuzz_cobol_copybook` | legacy/cobol.c: `cobol_parse_copybook`, `cobol_get_int`/`set_int`/`get_text`, fixed and RDW record readers, COMP-3 / zoned / COMP decoders | u16 copybook length, copybook text, record image |
| `fuzz_ubh_frame` | ubh/ubh.c: `ubh_168_header_unpack`/`validate`/`pack`, octet/septet/sextet regrouping, `ubh_detect_format` | raw bytes, every 21-byte window |
| `fuzz_gguf_loader` | tensor/zt_gguf.c: `zt_gguf_open`, metadata/array/tensor walkers, `zt_gguf_dequant`, `zt_gguf_to_q8` | a GGUF file |
| `fuzz_fat32_mount` | fat32/fat32.c: `fat32_mount`, `read_dir`, `read_file`, `change_dir`, cluster chain | a disk image (≤ 256 KiB) |
| `fuzz_vinea` | vinea: frame unwrap/accept, schema unpack, agreement and command decoders | mode byte + frame |
| `fuzz_pay_xml` | pay/pay_iso*.c: `pay_iso_parse_pacs008`, `pay_iso_parse_camt053`, `pay_iso_check_xml` | XML text |
| `fuzz_tls_record` | tls/record.c: cleartext and AEAD-protected record reader | mode, key/IV, records |
| `fuzz_tls_handshake` | tls/handshake.c: client handshake against a hostile server flight | mode + server bytes |
| `fuzz_freight` | freight/freight.c and freight_ops.c: packet decoder, op stream expander | mode + packet |
| `fuzz_upcheck` | update/zx_upcheck.c, zx_upmanifest.c, zx_ipns.c: manifest, IPNS record and name, version, RFC 3339 | mode + text/bytes |
| `fuzz_bootlegger` | bootlegger/bootlegger.c: signed handshake, sealed-message opener, ML-KEM session acceptor | mode + message |
| `fuzz_ipfs_cid` | ipfs/ipfs.c `ipfs_cid_parse`; ipfs_node varints, binary/string CIDs, base32/base58, `ubh168:` form | text/bytes |
| `fuzz_ipfs_dag` | ipfs_node: dag-pb, UnixFS data and directories (plain and HAMT), CAR v1, UBH block envelope | mode + block |
| `fuzz_econ_vino` | vino/vino.c ledger (with the RMAG quota shadow) | op stream |
| `fuzz_econ_pay` | pay/pay_ledger.c double-entry ledger | op stream |
| `fuzz_econ_count_house` | count_house/count_house.c stash buckets and minting | op stream |
| `fuzz_econ_triple` | finance/triple_ledger.c (kernel Q32.32 build, exact integers) | op stream |

### Property harnesses

The input of an `fuzz_econ_*` harness is a sequence of operations: mint/issue,
burn/redeem, transfer, fee/tithe, refund/reverse, raw postings, flag changes and
idempotent replays. Amounts come from `fz_edge64` in `common/fuzz_in.h`, so they are
biased toward 0, 1, -1, 2^31, 2^32, 2^53, 2^62, INT64_MAX, INT64_MIN and UINT64_MAX.
After every operation the harness checks:

* conservation: sum(balances after) == sum(balances before) + minted - burned
* a refused operation changes nothing (all-or-nothing)
* the ledger's own rules: no negative balance where it is forbidden, the supply and
  capacity caps, the trial balance closes, and the hash chain and audit copy match

The invariants for each ledger are listed at the top of its file. A broken invariant
calls `abort()`, which `fuzz_in.h` routes through `fz_abort_at` so that the log names
the line.

Seeded mode (deterministic, no clang needed):

```
build/replay/fuzz_econ_pay --random SEED ITERS MAXLEN   # ITERS inputs from splitmix64(SEED)
FUZZ_PROP_STATS=1 ...                                   # also print how many ops were checked
make -C fuzz prop PROP_ITERS=8000                       # all four, seed 20261010
```

When a property breaks in seeded mode, the driver writes the input to
`crash-random-<seed>-<iteration>`. That file replays like any other corpus file.

## Commands

```
make -C fuzz replay                 # gcc replay of every committed corpus (verify-all runs this)
make -C fuzz prop                   # seeded property runs
make -C fuzz fuzzers                # clang libFuzzer binaries
make -C fuzz run SECS=60            # every fuzzer for SECS seconds
make -C fuzz run-one T=fuzz_gguf_loader SECS=600
make -C fuzz smoke SMOKE_SECS=30    # what CI runs
make -C fuzz coverage               # line/branch/MC/DC table (tools/coverage.sh)
make -C fuzz seeds                  # regenerate the binary seeds from the encoders
```

Fuzzing writes new corpus entries to `build/corpus/<name>`, logs to `build/logs/`, and
crashes to `build/artifacts/`. None of these are committed.

## Crashes

1. Reproduce: `build/replay/<name> build/artifacts/<name>-crash-...` (gcc) or
   `build/fuzz/<name> <file>` (clang).
2. Fix the module with the smallest change that keeps its contract. Add a unit test in
   the module's own test file.
3. Copy the input to `corpus/<name>/regress-<what-broke>`, so that replay keeps it fixed.
4. Record it in `FUZZ_REPORT.md`.

## Sanitizers

`sanitizer-ignorelist.txt` holds the clang ignorelist that the fuzzers and the CI
integer job share. It lists only code where the flagged behaviour is the algorithm:
modular arithmetic in hashes and ciphers, and the vendored ed25519 ref10 shifts. Ledger,
parser and economy files are never listed. `tools/san-cc` is the compiler wrapper the CI
sanitizer jobs put on `PATH` as `gcc`/`cc`. It lets the whole host test suite build with
sanitizer (or coverage) flags without editing the test recipes.

The results of the runs are in `FUZZ_REPORT.md`, and the coverage numbers are in
`docs/COVERAGE.md`.
