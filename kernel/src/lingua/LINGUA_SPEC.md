<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Lingua front-end specification

What a Lingua front-end must do, and what it must prove, before its
language moves from a placeholder to the **run** tier of
`catalogue.tsv`. Every placeholder `zl_<lang>.c` points here. The IR and
runtime are in `zl.h`; the tiers in `docs/LANGUAGE_CATALOGUE.md`; the
step-by-step list in `README.md` ("Adding a language").

## 1. The contract

A front-end is one function, declared by its line in `zl_langs.def`:

```c
int zl_<fe>_compile(const char *src, size_t len, zl_cc_t *c, zl_out_t *tw, void *work);
```

- `src`/`len`: the program text. It is not NUL-terminated.
- `c`: the compiler context. Emit with `zl_emit`, patch jumps with
  `zl_patch`, reserve memory with `zl_slots`, intern strings with
  `zl_addstr`. Record the first failure with `zl_fail` and return its
  status; set `c->line` and `c->col` first so the error points at the text.
  `c->p->word_bits` starts at 64. A front-end may set 32 or 62 (lists) with
  `zl_cc_init` before it emits anything.
- `tw`: when not NULL, write the canonical twin text into it with
  `zl_put`/`zl_puts`/`zl_putc`/`zl_puti`. In twin mode (`zl_compile` with
  `ZL_TWIN`) `tw` is in compare mode, and the caller refuses the program
  unless the text written equals the source byte for byte.
- `work`: `sizeof(((zl_work_t *) 0)->fe)` bytes of scratch (256 KB),
  uninitialised. A front-end puts all its tables in one struct, checks it
  with `ZL_STATIC_ASSERT(sizeof(T) <= sizeof(((zl_work_t *) 0)->fe), ...)`
  and zeroes it with `zl_bzero` before use.

The function returns `ZL_OK` or a negative `zl_status_t`. `zl_compile` runs
`zl_verify` on the result and zeroes the program on any failure.

## 2. Freestanding rules

- C11, no libc, no allocation, no floating point, no 64-bit division
  helpers on 32-bit targets: the module must link with nothing outside
  `zl_`. `make -C kernel test-lingua` builds every `zl_*.c` for
  aarch64-none-elf and x86_64 freestanding and fails on any undefined
  symbol other than `zl_`.
- Use `zl_bzero`/`zl_bcopy`, never loops the compiler can turn into
  `memset`/`memcpy` calls.
- Every table is a fixed array. A program that needs more is refused with
  `ZL_E_LIMIT`, never truncated.
- Recursion in the parser is bounded (`ZL_MAX_DEPTH` is the usual limit),
  so hostile input cannot exhaust the C stack.

## 3. Semantics: same output, or a refusal, or a trap

For every program the front-end accepts, `zl_run`'s output must be
byte-identical to the output of a maintained reference implementation of
the language (named in `README.md`'s front-end table). Where that cannot
hold, the front-end does one of two things, never a third:

1. **Refuse at compile time** (`ZL_E_UNSUPPORTED` or `ZL_E_SYNTAX`, with
   line, column and a reason in plain English) for anything outside the
   subset: real numbers, a construct not built, a value the subset cannot
   represent. Nothing is guessed.
2. **Trap at run time** with the IR's checked status when the reference
   would raise an error (division by zero, a nil value, an out-of-range
   index), or where the integer runtime cannot follow the reference
   (overflow that the reference wraps, call depth past `ZL_MAX_CALLS`).
   The second kind is listed as **[ref differs]** in the language's tests
   and README entry. Output written before a trap is a prefix of what the
   reference writes.

Exact integers only: a division that leaves a remainder, a power that is
not an integer, or a literal with a fraction or exponent is refused or
trapped, never rounded.

## 4. The twin

Every language gets a twin: one canonical spelling of the language
itself, named in `zl_langs.def` (a purpose word joined to a kind of
dragon). The five twin rules are in `docs/LANGUAGE_CATALOGUE.md`; in
short, for every accepted program:

- the twin is a valid program in the original language and runs the same
  under its usual implementation;
- the twin compiles to the same IR (`zl_prog_equal`; source line numbers
  are not compared);
- the twin of the twin is the twin, byte for byte;
- comments are kept;
- in twin mode only canonical text is accepted.

A front-end documents its canonical layout (case, spacing, indentation,
one statement per line, how literals are spelled, where comments and blank
lines go) at the top of its source file.

## 5. Proof before the run tier

A language moves to **run** only when all of these pass in CI
(`make -C kernel test-lingua test-lingua-ref`):

| Proof | Where |
|---|---|
| Unit tests for every statement, every refusal (status and line) and every trap | `test_zl_<lang>.inc`, included from `test_zl_langs.inc` |
| Golden programs, including `collatz.<ext>` printing the same 40 numbers as every other language, whose output matches the reference byte for byte | `golden/*.<ext>` and `golden/*.<ext>.out` |
| 100 random well-defined programs per run, generated with the reference's rules, byte-identical through Lingua and the reference | `gen_<lang>.c` and `ref_<lang>.sh` (sourced by `ref_check.sh`) |
| The twin rules on every golden, every unit-test program and every mutated program the fuzzer finds accepted, under ASan and UBSan | `test_zl.c` (`twin_props`), `fuzz_zl.c` (goldens join its seeds automatically) |
| A mutation target that breaks the front-end on purpose and requires a test to notice | `tests/harness/mutator/targets.json` |
| The freestanding build | `make -C kernel test-lingua` |

Then: the language's line in `catalogue.tsv` moves to `run` with its
twin's name (`check_catalogue.sh` requires the run tier to equal the
front-ends that are no longer placeholders), its row joins the front-end
table in `README.md`, and `docs/LANGUAGE_CATALOGUE.md` lists it under
**run**.

## 6. Apps

An app manifest names a Lingua language by its runtime name (the last
field of its `zl_langs.def` line), its catalogue name or its twin's name.
`kernel/src/apps/zxapp_lingua.c` compiles each entry once when the app
loads; a placeholder front-end refuses by name. Once a front-end ships,
`tools/apps/lang_coverage.py --retrofit <language>` writes the server tasks
for the app entries that were waiting on it.
