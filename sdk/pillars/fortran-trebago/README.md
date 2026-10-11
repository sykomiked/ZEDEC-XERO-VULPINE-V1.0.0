<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# fortran-trebago: exact arithmetic for Fortran, and Trebago

This is the second legacy-language pillar. It is written for people who
already write numerical Fortran and sometimes need an answer that is
**exact**. That covers a checksum over money, a reference value for a
regression test, a rational matrix from a proof, or a sum whose terms
cancel. It has two parts:

- **module `zxv`** (`fortran/zxv.f90`). A Fortran 2008+ module that uses
  ISO_C_BINDING over libzxv. It provides:
  - an exact `rational` type with the usual operators;
  - four-valued `truth` and the 64 `hexagram` tags;
  - capabilities, the ledger and the quarantine log.

  It has no REAL anywhere except one marked, explicit conversion.
- **Trebago** (`src/`, `fortran/trebago.f90`). A short syntax for the
  kernels a Fortran program would otherwise write with REAL arrays: sum,
  dot product, scale, prefix sum and matrix-vector product. A C library,
  libztrebago, parses it. libztrebago runs the program exactly on your
  `INTEGER(INT64)` numerator and denominator arrays, in place and in
  column-major order. A result that does not fit is refused with
  `ZXV_E_RANGE`, never rounded.

```fortran
use zxv
use trebago
type(trebago_program) :: p
integer(int64), target :: xn(3), xd(3), yn(3), yd(3), sn, sd

call p%load('examples/dot.tb')         ! s = sum(i, x(i) * y(i))
xn = [2_int64**53, 1_int64, -(2_int64**53)];  xd = 1
yn = 1;                                        yd = 1
call p%bind('x', xn, xd)
call p%bind('y', yn, yd)
call p%bind('s', sn, sd)
call p%run()                           ! sn/sd = 1/1; REAL(REAL64) says 0.0
```

Pillar API 0.1 (`include/ztrebago.h`; it may change before 1.0) over the
libzxv ABI 1.3 ([ABI.md](../../docs/ABI.md)). The pillar status codes run
from -2101 to -2199.

## Why exact

Floating point is the right tool for most numerical work. It is fast,
hardware-supported, and its error is well understood. Some answers,
though, are wrong if they are only close:

| What `make demo` shows | REAL(REAL64) | exact |
|---|---|---|
| ten times 0.1 | 0.99999999999999989 | 1 |
| dot([2^53, 1, -2^53], [1, 1, 1]) | 0.0 | 1 |
| row sums of the 8x8 Hilbert matrix | none equal the true value | 761/280, 4609/2520, ... |
| the double nearest 0.1 | 0.1 | 3602879701896397/36028797018963968 |

The REAL-to-exact comparison in the demo goes through
`rational_from_ieee754`, which reads a double's exact value. The
comparison itself therefore adds no rounding.

## The cost of exactness

Be clear about what you are buying:

- **It is slow.** Each operation is a libzxv call that does a 128-bit
  multiply and a gcd reduction. On the development machine (x86-64, gcc
  13, -O2), a dot product of 10^6 elements took:
  - about 2 ms with REAL(REAL64) `dot_product`;
  - about 290 ms with Trebago on integer values;
  - about 370 ms with Trebago on small fractions.

  That is two orders of magnitude, and more on wider data.
- **Denominators grow.** Adding a/b and c/d makes the denominator
  lcm(b, d). The harmonic numbers H(n) = 1 + 1/2 + ... + 1/n carry
  lcm(1..n):

  | n | H(n) |
  |---|---|
  | 10 | 7381/2520 |
  | 22 | 19093197/5173168 |
  | 34 | 54062195834749/13127595717600 |
  | 46 | 5943339269060627227/1345655451257488800, the last that fits int64/int64 |
  | 47 | refused: `ZXV_E_RANGE` at i=47, never rounded |

  A long sum of unrelated fractions overflows quickly. Sums of integers,
  of values with a common denominator (cents, 1/1000s), or of short
  vectors stay small.
- **int64/int64 is the ceiling.** There are no big integers. When an
  exact result does not fit, the run stops with `ZXV_E_RANGE`, and the
  output arrays keep what they held.

**Use it** in these cases:
- reference values for tests;
- reductions over money or counts;
- checksums that must match across machines and compilers;
- small exact linear algebra (Hilbert and Vandermonde matrices, rational
  coefficients);
- cases where you need to know whether cancellation happened;
- inputs that are already exact (integers, decimals as text).

**Do not use it** in these cases:
- simulation inner loops;
- anything with sqrt, exp or trig (Trebago has none);
- large dense linear algebra;
- iterative solvers;
- GPU or SIMD work;
- any place where a relative error of 1e-15 is fine. It almost always
  is.

## Honest limits

- **It does not replace floating-point codes.** It is meant to sit next
  to them, for the few values that must be exact.
- **It is far slower than REAL.** See the measurements above. Trebago
  interprets an expression tree, with no compilation, vectorization or
  fusion.
- **No GPU, no SIMD, no threads inside one run.** Separate programs can
  run on separate threads (`test_exec` runs 4), and each thread has its
  own `tb_last_error` and `tb_last_work`.
- **Values are int64/int64.** There are no big integers and no
  irrational functions. Exponents must be integers.
- **Array rank is limited to 2**, with at most 2^26 elements per array.
  Each run has a work limit (2^32 rational operations by default) so that
  a program cannot run for hours.
- **gfortran only.** The modules are standard Fortran 2018. The tests and
  CI use gfortran 13 or later; Intel ifx and LLVM flang are not tested.

## Quickstart

```sh
make -C sdk/libzxv all                               # the C library
make -C sdk/pillars/fortran-trebago test demo        # C + Fortran tests, then the demo
make -C sdk/pillars/fortran-trebago FORTRAN=0 test   # the C half, without gfortran
sdk/pillars/fortran-trebago/build/ztrebago run sdk/pillars/fortran-trebago/examples/matvec.tb \
    'a(2,2)=1,1/2,1/2,1/3' x=1,1
```

Build your program against the modules:

```sh
gfortran -std=f2018 -I sdk/pillars/fortran-trebago/build/mod prog.f90 \
  sdk/pillars/fortran-trebago/build/libzxvf.a sdk/pillars/fortran-trebago/build/libztrebago.a \
  -L sdk/libzxv/build -lzxv -Wl,-rpath,$PWD/sdk/libzxv/build
```

## Trebago

A program has a name, its declarations, its statements, and `end`:

```text
trebago kernels
  in    x(n), y(n), alpha
  inout z(n)
  out   total, dot, prefix(n), h(n, n), hx(n)
  const third = 1/3

  total = sum(i, x(i))
  dot = sum(i, x(i) * y(i))
  z(i) = alpha * x(i) + z(i)          ! axpy: z = alpha*x + z
  prefix(i) = scan(x(i) * third)      ! running sum of x/3
  h(i, j) = 1 / (i + j - 1)           ! the Hilbert matrix, exactly
  hx(i) = sum(j, h(i, j) * x(j))
end
```

| Element | Meaning |
|---|---|
| `in x(n), a(m, n), s` | Inputs: scalars, vectors or matrices. A dimension is a name, bound by the arrays you pass (every use of `n` must agree), or a fixed integer such as `x(3)`. Inputs cannot be assigned. |
| `out`, `inout` | Results. Written back only if the whole run succeeds. `inout` is read and then written. |
| `var t(n)` | A temporary that is not bound. Its dimensions must appear in a parameter. |
| `const c = 1/3` | A named exact constant, folded at parse time. |
| `y(i) = e` | A statement. The target's subscripts are its loop indices and range over the target's extent. The right side is evaluated in full before any element is stored, so `r(i) = r(n - i + 1)` reverses `r`. Statements run in order. |
| `sum(i, e)`, `sum(i = lo:hi, e)` | An exact sum over a new index. Without bounds, the range comes from the first array that `i` subscripts directly. |
| `scan(e)` | The running sum along a vector statement's index (prefix sum). Allowed only as the right side of a rank-1 statement. |
| `abs(e)`, `min(a, b, ...)`, `max(a, b, ...)` | Exact. |
| `+ - * /`, `**` | The usual precedence: `**` binds tightest and needs an integer exponent (`2 ** -3` is 1/8). Unary minus binds below `**`. |
| `i`, `n` in expressions | Loop indices and dimension names are exact integers (`1 / (i + j - 1)`). |
| numbers | `3`, `-7`, `0.1` (exactly 1/10), `1/3`. Exponent notation (`1.0e-3`, `1d0`) and kind suffixes (`1.0_dp`) are refused, because they name binary approximations. |
| text | `!` starts a comment and `&` continues a line. Names are case-insensitive, at most 31 characters, and may not be reserved words (`trebago end in out inout var const sum scan abs min max`). |

A parse error, or a refusal at run time, reports the line and column and a
reason, for example:

```text
line 9, column 10: 5943339269060627227/1345655451257488800 + 1/47 does not fit int64/int64 exactly (refused, never rounded) at i=47 [ZXV_E_RANGE]
```

**Identity.** Every program has a canonical text, with one declaration
per line, lower-case names, constants written as their exact value, and
minimal parentheses. `tb_program_digest` is the SHA3-256 of that text, so
`examples/dot.tb` and `test/golden/dot_spelled.tb`, which is the same
kernel in capitals with continuation lines, have the same digest
(`ztrebago same`).

**Status codes.**

| Code | Name | Class | When |
|---|---|---|---|
| -2101 | `TB_E_SYNTAX` | USAGE | the text does not parse |
| -2102 | `TB_E_NAME` | USAGE | an unknown, duplicate or reserved name; a missing or extra binding |
| -2103 | `TB_E_SHAPE` | USAGE | a rank or extent mismatch, or a range that cannot be inferred |
| -2104 | `TB_E_BOUNDS` | USAGE | a subscript outside its array |
| -2105 | `TB_E_DIVZERO` | USAGE | division by an exact zero |
| -2106 | `TB_E_LIMIT` | REFUSED | the work limit or a size limit; a larger limit may succeed |
| -1..-99 | libzxv's | | `ZXV_E_RANGE` (does not fit), `ZXV_E_ARG` (an input with a denominator of 0), ... |

## The C API (`include/ztrebago.h`)

| Call | |
|---|---|
| `tb_parse(text, len, &prog, &diag)` | Parse text into an immutable program. `diag` gets the line, column and message. |
| `tb_program_text`, `tb_program_digest`, `tb_program_name` | The canonical text (two-call buffer, R4), its SHA3-256, and the name. |
| `tb_param_count`, `tb_param_at` | The parameters: name, role, rank, dimension names and fixed extents. |
| `tb_exec(prog, arrays, count, work_limit, &diag)` | Run on `tb_array {name, num, den, rank, extent[2]}`, which is column-major and BIND(C)-compatible. Outputs are written only on success. |
| `tb_last_work`, `tb_last_error`, `tb_status_name`, `tb_status_class` | Per thread. |

`build/ztrebago` provides the same operations from a shell:
- `check` and `canon` parse a file and print its canonical form;
- `params` lists a program's parameters;
- `same` compares two programs by digest;
- `run` runs a program on values given as arguments;
- `golden` runs the `!>` cases in a `.tb` file.

## Module `zxv`

| | |
|---|---|
| `type(rational)` | `rational(n)`, `rational(n, d)` from INTEGER(INT32 or INT64). `+ - * / **`, comparisons and `abs`, mixed with integers. Exact and reduced. There is **no REAL constructor or operator**: passing a REAL does not compile (`test/compile_fail`). The comparisons are `pure`. |
| `rational_from_ieee754(x)` | The one REAL path. It takes `REAL(REAL64)` only and gives that double's exact value (0.1d0 gives 3602879701896397/36028797018963968). NaN and infinities are `ZXV_E_FLOAT`; a value that needs more than 63 bits (2^-80) is `ZXV_E_RANGE`. It sits between the `! R11-BOUNDARY` markers, and `make check-noreal` fails on any other REAL in the modules. |
| `rational_parse`, `to_string`, `numerator`, `denominator`, `rational_add/sub/mul/div(a, b, r, stat)`, `rational_sum`, `rational_dot`, `rational_split`, `rational_join`, `%to_q16` | Parsing, printing, stat forms, and the bridge to Trebago's arrays. |
| `type(truth)` | `TRUTH_NEITHER`, `TRUTH_TRUE`, `TRUTH_FALSE`, `TRUTH_BOTH`, with `.and. .or. .not.` from the kernel's tables. It is not a LOGICAL: `if (t)` does not compile. Use `t%to_logical(on_both=..., on_neither=...)`. |
| `type(hexagram)` | `hexagram(base, polarity, phase)`, with `.and. .or. .not.`, `%step`, `%advance`, `%with_state`, `%forbid` and `%action`, all from the kernel's tables. |
| `type(context)`, `type(capability)`, `type(ledger)`, `type(quarantine_log)` | Handle types with final procedures. |

**Errors.** Every call that can fail takes an optional `stat=`:
- With `stat=`, a refusal comes back as a status code.
- Without it, a refusal prints the reason and stops with `ERROR STOP 1`.
- A **FATAL** status (`ZXV_E_POISONED`, `ZXV_E_INVARIANT`) always stops
  with `ERROR STOP 70`, even when `stat=` is present.

The FATAL rule exists for this reason: a poisoned context has seen a
broken invariant, and no call on it can succeed again. If a caller kept
going after a recoverable-looking status, it would be computing with
books whose state is no longer known. Stopping is the only safe default
in a language without exceptions. 70 is `EX_SOFTWARE`. `make fatal`
checks the exit code, the message on stderr, and that nothing after the
stop runs.

**Handles and R6.** A handle variable holds a reference-counted node.
Each child node holds a reference to its parent. A C handle is destroyed
when its last reference goes, so children always go before parents (R6),
whatever order Fortran finalizes or `%destroy`s them in. Assigning a
handle shares it.

**gfortran's `-Wextra`** warns when an impure function appears in
`.and.`/`.or.`, because it might be short-circuited. Arithmetic and
handle methods are impure on purpose, since they can stop the program.
Either put the call in its own statement or use
`-Wno-function-elimination`. The tests and the demo do the latter.

## Tests

| Suite | What | Count |
|---|---|---|
| `test_parse` (C) | Accepted programs with their exact canonical text, refusals with line and column, limits, parameters, R4 buffers | 578 checks |
| `test_exec` (C) | 4,000 random kernels compared with direct libzxv arithmetic (3,182 agree exactly, 818 refused alike), layout, bindings, work limit, 4 threads | 43,602 checks |
| golden (`ztrebago golden`) | 20 programs and 26 cases with `!>` expectations, including H(46) and the refusal of H(47); the canonical text must be a fixed point | 123 values and refusals |
| `fuzz_parse` | Mutation fuzzer: deterministic parse, canonical fixed point and digest, the canonical program runs identically, outputs untouched on refusal; also a libFuzzer entry point | 3,000 inputs per `make test`; 4 x 250k under ASan + UBSan in CI |
| `test_zxv` (Fortran) | Every table in `sdk/conformance/vectors.json` (status codes, rational operations, parse, IEEE-754, Q16, four-valued logic, all 64x64 hexagram tables, the quarantine log chain), 20,000 random comparisons against `zxv_rat_cmp`, capabilities, ledger, R6 lifetimes | 30,312 checks |
| `test_trebago` (Fortran) | Dot and sum against `rational_dot` and `rational_sum` (600 random cases, overflow refused alike), matvec against a Fortran loop, `examples/kernels.tb`, Hilbert 10x10, H(47) refused with outputs untouched, every refusal, digest equality, copies | 687 checks |
| `fatal` | A poisoned context stops with 70 even with `stat=` | 1 |
| `compile-fail` | 13 programs that must not compile (REAL in every operator, constructor, comparison and assignment, REAL arrays in `bind`, default REAL in the IEEE boundary, `if (truth)`, truth = hexagram, rational to INTEGER), and one control program that must compile | 14 |
| `check-nofloat`, `check-noreal` | No float type in the C sources; no REAL in the modules outside the boundary | |

The C sources also build with `-mgeneral-regs-only` where the compiler
supports it, which turns any floating-point instruction into a compile
error. CI (`.github/workflows/sdk-pillars.yml`, jobs `fortran-trebago*`)
runs the suites on:
- ubuntu-24.04 with gcc and with clang, both using gfortran;
- macos-14 with clang and Homebrew gfortran;
- ASan + UBSan, using gcc with the Fortran half and clang with the C
  half;
- the fuzzer, and libFuzzer for 60 s.

## Layout

```text
include/ztrebago.h       the pillar API
src/                     lexer and parser, canonical printer, executor
fortran/zxv.f90          module zxv
fortran/trebago.f90      module trebago
tools/ztrebago_tool.c    the ztrebago command
tools/gen_vectors_f90.py vectors.json -> a Fortran module of constants
tools/check_noreal.py    R11 for Fortran
examples/                dot, matvec, kernels, broken.tb, trebago_demo.f90
test/                    C, Fortran, golden, fuzz and compile-fail tests
```

## Licence

Apache-2.0. Registered to 36N9 Genetics LLC, Michael Laurence Curzi.
Provided without warranty.
