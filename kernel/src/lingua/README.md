<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Lingua: legacy languages on one IR

Lingua compiles legacy programming languages to one small stack-machine IR
and runs it on one checked integer runtime. Each language also has an AI
twin, its canonical spelling, which compiles to the same IR. The language
catalogue and the tiers are in `docs/LANGUAGE_CATALOGUE.md`.

Freestanding C11 in libzxv: no libc, no allocation, no floating point. The
caller passes every buffer (`zl_work_t`, `zl_prog_t`, `zl_run_t`).

## Front-ends

| Language | File | Subset | Reference | Twin |
|---|---|---|---|---|
| FORTRAN 77 | `zl_fortran.c` | fixed form, `INTEGER`, arrays, `PARAMETER`, `DO`/`DO WHILE`, block and logical and arithmetic `IF`, computed `GO TO`, `PRINT *`/`WRITE (*,*)`/`READ *`, intrinsics `MOD ABS IABS MAX MIN ISIGN`; 32-bit | gfortran | Trebago statements |
| Minimal BASIC (ECMA-55) | `zl_basic.c` | integer values, exact `/`, `INT`, `DIM`, `OPTION BASE`, `FOR`/`NEXT`, `GOSUB`, `ON GOTO`, `READ`/`DATA`/`RESTORE`, `INPUT`, `DEF FN`, print zones; 32-bit | none maintained; goldens reviewed by hand | Bede |
| Forth-2012 core | `zl_forth.c` | definitions, `IF`/`ELSE`, `DO`/`LOOP`/`+LOOP`/`LEAVE`, `BEGIN` loops, `RECURSE`, `VARIABLE`/`CONSTANT`/`CREATE`/`ALLOT`, `."`, floored division, 64-bit cells | gforth | Quern |

Anything else in a language is refused with `ZL_E_UNSUPPORTED` and the line
and column; nothing is guessed.

## Runtime

`zl_run` executes the IR with a fuel budget (default 10^7 instructions) and
fixed limits (`ZL_MAX_*` in `zl.h`). It stops with a trap rather than
continue on overflow past the program's word size, division by zero, an
inexact division, an index out of bounds, stack or call depth, fuel, output
or input exhaustion, or a domain error. `zl_verify` checks any IR before it
runs: every jump lands inside the program and every slot, array and string
exists. Stack depth is checked as the program runs.

## Commands

```
zxlang run   [--twin] prog.f [inputs...]   compile and run
zxlang twin  prog.bas                      print the twin
zxlang check prog.fs                       compile, round-trip the twin
zxlang ir    prog.f                        list the IR
```

## Tests

`make -C kernel test-lingua` runs the unit tests (`test_zl.c`), the
mutation fuzzer (`fuzz_zl.c`) with and without ASan/UBSan, the freestanding
build check and the catalogue check. `make -C kernel test-lingua-ref` runs
the golden programs in `golden/` and 100 random programs per language from
`gen_zl.c` through gfortran and gforth and requires identical output; it
skips when they are missing unless `ZXV_REQUIRE_LINGUA_REF=1` (set in CI).
`mut_check.sh` is the mutation harness target.

## Adding a language

1. Write `zl_<lang>.c`: a parser that emits IR through `zl_emit` and a twin
   printer, refusing what it does not support.
2. Add the language to `zl.h`, `zl_ir.c`'s dispatch and `zxlang.c`.
3. Add unit tests, goldens from a reference implementation, a generator
   mode, fuzz seeds and a mutation target.
4. Move its line in `catalogue.tsv` to `run` with its twin's name.
