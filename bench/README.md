<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
     SPDX-License-Identifier: Apache-2.0 -->
# Benchmarks

Two harnesses. Neither downloads anything, and **no performance results are
claimed here**. A number means something only for the machine, compiler, build
flags and model it was measured on, so the scripts print those details with
every result. Nothing in this directory has been run on real target hardware
yet.

| Command | What it does |
|---|---|
| `make -C bench legacy` | Golden-trace check of the legacy transcoders (pass/fail, bit-exact) |
| `make -C bench legacy-bench` | The same check, then a conversions-per-second figure |
| `make -C bench smoke` | Offline checks: `legacy`, plus `zt_bench` refusing a weightless GGUF, plus the Python parser self-test |
| `make -C bench tensor MODEL=m.gguf [N=32] [PROMPT="..."]` | Tensor benchmark on a GGUF model you supply |

Build output goes to `/tmp/zxv_bench` (override with `OUT=dir`).
`make -C kernel verify-all` runs the `legacy` golden check; CI runs it in the
`kat-cross` job.

## Tensor benchmark (`bench/tensor/`)

- `zt_bench.c` is a hosted driver for the integer engine in `kernel/src/tensor`.
  It loads the GGUF, tokenizes the prompt, runs prefill, generates N tokens
  greedily (argmax, lowest id on ties) and prints `zt.prefill_tps`,
  `zt.gen_tps`, the generated token ids and the stop reason.
- `run_tensor_bench.py` runs `zt_bench`, then, **only if llama.cpp is on PATH**:
  `llama-bench` on the same model with the same prompt length and N (tokens/s),
  and `llama-completion` (or `llama-cli`) greedy (`--temp 0 --top-k 1`) on the
  same prompt. It reports whether the first N generated tokens match. The match
  is checked on the decoded text, byte for byte, because the llama.cpp tools do
  not print token ids. A mismatch is reported with the offset of the first
  differing byte. `--json out.json` writes the summary.
- `make_tiny_model.py TOKENIZER_ONLY.gguf OUT.gguf` (needs the `gguf` and
  `numpy` Python packages) writes a tiny qwen2 GGUF with random weights. It lets
  you run the whole pipeline offline (prompt, prefill, generation). Its output
  text is meaningless and its speed says nothing about real models:

      make -C bench smoke                     # also writes /tmp/zxv_bench/tok_only.gguf
      python3 bench/tensor/make_tiny_model.py /tmp/zxv_bench/tok_only.gguf /tmp/zxv_bench/tiny.gguf
      make -C bench tensor MODEL=/tmp/zxv_bench/tiny.gguf N=8

To reproduce a real comparison, pick a model the engine supports (see
`docs/SYSTEM_REFERENCE.md`, tensor entry), use the same GGUF file for both
engines, pin the CPU frequency governor if you can, and run each benchmark
several times. Report the full printed output, not only the tokens/s line.

## Legacy transcoder benchmark (`bench/legacy/`)

`legacy_bench.c` runs every row of `golden_legacy.tsv` through the converter
under test and compares the result byte for byte. The converters are:

- COBOL COMP-3 (packed decimal), zoned decimal (EBCDIC DISPLAY) and COMP
  (big-endian binary) encode/decode in `kernel/src/legacy/cobol.c`,
- packed and zoned decimal with implied decimals to exact rationals in
  `kernel/src/lightningrod/lightningrod.c`,
- IBM System/360 hexadecimal floating point (single and double) to and from
  IEEE 754 in `kernel/src/legacy/fortran.c`.

Any mismatch fails the run (exit 1). `--iters N` adds a throughput loop.

### Sources of the golden traces

No expected value was produced by running the code under test. Each group in
the TSV names its source in a comment:

- **[A]** IBM Enterprise COBOL Programming Guide, "Examples of numeric data and
  internal representation": the value 1234 and -1234 as DISPLAY (zoned),
  PACKED-DECIMAL (COMP-3) and BINARY (COMP).
- **[B]** Worked by hand from the packed and zoned rules: two digits per byte,
  sign nibble C, D or F last for packed; zone F with the sign in the last byte's
  zone for zoned. The working is in the comments (for example
  `PIC S9(7)V99 COMP-3` 1234567.89 is `12 34 56 78 9C`). The set includes a value
  too large for its field, which the encoder must refuse.
- **[C]** IBM hexadecimal floating point. The `-118.625 = C276A000` single
  example is the one given in the Wikipedia article "IBM hexadecimal
  floating-point". The IEEE side and the other values (1.0, 0.5, -1.0, 100.0,
  0.0, and the double-precision forms) are worked by hand in the comments, for
  example 118.625 = 1.110110101b x 2^6, so the IEEE single is `C2ED4000`.

To add a case, write the expected bytes from a published source or by hand,
write the working in a comment next to the row, then run `make -C bench legacy`.
