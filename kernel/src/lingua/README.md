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
| FORTRAN 77 | `zl_fortran.c` | fixed form, `INTEGER`, arrays, `PARAMETER`, `DO`/`DO WHILE`, block and logical and arithmetic `IF`, computed `GO TO`, `PRINT *`/`WRITE (*,*)`/`READ *`, intrinsics `MOD ABS IABS MAX MIN ISIGN`; 32-bit | gfortran | Reckonwyrm |
| Minimal BASIC (ECMA-55) | `zl_basic.c` | integer values, exact `/`, `INT`, `DIM`, `OPTION BASE`, `FOR`/`NEXT`, `GOSUB`, `ON GOTO`, `READ`/`DATA`/`RESTORE`, `INPUT`, `DEF FN`, print zones; 32-bit | none maintained; goldens reviewed by hand | Hatchling |
| Forth-2012 core | `zl_forth.c` | definitions, `IF`/`ELSE`, `DO`/`LOOP`/`+LOOP`/`LEAVE`, `BEGIN` loops, `RECURSE`, `VARIABLE`/`CONSTANT`/`CREATE`/`ALLOT`, `."`, floored division, 64-bit cells | gforth | Cairndrake |
| ISO 7185 Pascal | `zl_pascal.c` | `integer` and `boolean`, `const`, one-dimensional arrays with any bounds, nested procedures and functions with value and `var` parameters and recursion, `if`/`while`/`repeat`/`for`/`case`, `div` and ISO `mod`, short-circuit `and`/`or`, `abs sqr odd succ pred ord maxint`, `write`/`writeln` with field widths, `read`/`readln`; 32-bit as `fpc -Miso` | fpc -Miso (goldens and `gen_pascal.c`) | Tutordrake |
| Delphi (Object Pascal, integer subset) | `zl_pascal.c` | the Pascal subset plus `//` comments, `$hex`, `Integer`/`LongInt`, declarations in any order, `Result`, `Exit`/`Exit(v)`, `Break`/`Continue`, `Inc`/`Dec`, case ranges and `else`, bitwise `and or xor not` on integers, truncating `mod`; integers print in as few columns as they need, Booleans as `TRUE`/`FALSE`; 32-bit, overflow trapped as under `{$Q+}` | fpc -Mdelphi -Co -Cr (goldens and `gen_pascal.c delphi`) | Formwyrm |
| ALGOL 60 (Revised Report) | `zl_algol.c` | MARST hardware representation; blocks, `integer`/`Boolean` variables and one-dimensional arrays with dynamic bounds, procedures (typed, recursive, nested) with value, name (Jensen's device) and array parameters, `if`/`for` (step-until, while, lists)/`goto`, `outinteger`/`outstring`/`ininteger`, `abs iabs sign entier`; exact `/`, `%` truncates; 64-bit | none packaged; goldens cross-checked with fpc against equivalent Pascal in `xref/` | Rootwyrm |
| Common Lisp core | `zl_lisp.c` | integers, quoted symbols and lists, `defun`/`defvar`/`setq`/`setf`/`incf`/`push`/`pop`, `let`/`let*`/`labels`, `if`/`cond`/`when`/`unless`/`and`/`or`, `do`/`dotimes`, list built-ins (`car`..`cddddr`, `cons`, `list`, `append`, `reverse`, `member`, `assoc`, `equal`), `print`/`prin1`/`princ`/`format t`; self tail calls run as loops; 62-bit | sbcl | Coilwyrm |
| Scheme (R7RS-small core) | `zl_lisp.c` | integers, quoted symbols and lists, `define` (internal too)/`set!`, `let`/`let*`/named `let`, `if`/`cond`/`when`/`unless`/`and`/`or`, `do`, list built-ins (`memq`/`member`/`assq`/`assoc`, `set-car!`), `quotient`/`modulo`/`floor-quotient`..., `display`/`write`/`newline`; unspecified values refused where used; 62-bit | guile | Spiraldrake |
| Logo (UCBLogo 6.2) | `zl_logo.c` | integers, words and lists; `to`/`end` procedures with dynamic scope (shallow binding), `output`/`stop`, `make`/`local`/`thing`, `repeat`/`repcount`, `for`, `while`/`until`, `if`/`ifelse`, infix and prefix arithmetic and comparisons, `first butfirst last butlast fput lput list sentence item count memberp emptyp`, `print`/`show`/`type`; exact `/`; 62-bit values | ucblogo (under xvfb-run) | Kiteling |

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

## Precompiled programs

`zl_pack` writes a verified program as "zlir" text (one line per
instruction, strings in hex, a digest) and `zl_unpack` reads it back with no
front-end, refusing text that is malformed, fails `zl_verify` or does not
match its digest. Apps ship legacy logic this way (`logic zlir ...`, run by
`kernel/src/apps/zxapp_lingua.c`); see `docs/LANGUAGE_CATALOGUE.md`.

## Commands

```
zxlang run   [--twin] prog.f [inputs...]   compile and run
zxlang twin  prog.bas                      print the twin
zxlang check prog.fs                       compile, round-trip the twin
zxlang ir    prog.f                        list the IR
zxlang pack  prog.f > prog.zlir            precompile; run and ir take .zlir too
```

`zxlang run` passes its inputs to `READ *` and `INPUT`; for Forth they are
pushed onto the data stack, first deepest.

## Tests

`make -C kernel test-lingua` runs the unit tests (`test_zl.c`), the
mutation fuzzer (`fuzz_zl.c`) with and without ASan/UBSan, the freestanding
build check and the catalogue check. `make -C kernel test-lingua-ref` runs
the golden programs in `golden/` and 100 random programs per language from
`gen_zl.c` through gfortran and gforth and requires identical output; it
skips when they are missing unless `ZXV_REQUIRE_LINGUA_REF=1` (set in CI).
`ref_logo.sh` (sourced by `ref_check.sh`) does the same for Logo with
ucblogo and the generator `gen_logo.c`; ucblogo writes to a window, so it
runs under `xvfb-run` with its output redirected to a file by `setwrite`.
`mut_check.sh` is the mutation harness target.

## Adding a language

1. Write `zl_<lang>.c`: a parser that emits IR through `zl_emit` and a twin
   printer, refusing what it does not support.
2. Add the language to `zl.h`, `zl_ir.c`'s dispatch, `zxlang.c`, and to
   `zxapp.h`/`zxapp_lingua.c` so apps can use it.
3. Add unit tests, goldens from a reference implementation, a generator
   mode, fuzz seeds and a mutation target.
4. Move its line in `catalogue.tsv` to `run` with its twin's name.
