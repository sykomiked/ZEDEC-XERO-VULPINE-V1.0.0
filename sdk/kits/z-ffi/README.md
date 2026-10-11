<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# z-ffi: a typed boundary for calling C libraries

z-ffi lets ZXV code call functions in ordinary C libraries without giving
up exact arithmetic or capability checks. You declare the functions in a
small **spec**; `zf-gen` generates one C **wrapper** per function; the
wrappers call the library through a checked, integer-only runtime.

```text
# libm.zf
library libm;
include <math.h>;
fn sqrt(x: f64 from rat) -> f64 as rat;
fn ldexp(x: f64 from rat, e: int[-1100..1100]) -> f64 as rat needs BUDGET;
```

```c
int64_t n, d;
zf_libm_sqrt(cap, 9, 4, &n, &d);   /* ZXV_OK, n/d = 3/2                         */
zf_libm_sqrt(cap, 1, 10, &n, &d);  /* ZF_E_INEXACT: 0.1 is not a double         */
zf_libm_sqrt(cap, -1, 1, &n, &d);  /* ZXV_E_FLOAT: the result is NaN            */
zf_libc_abs(cap, INT_MIN, &r);     /* ZF_E_RANGE: undefined in C, never called  */
zf_libc_abs(NULL, -42, &r);        /* ZXV_E_CAP: no capability, no call         */
```

Every wrapper:

1. **requires a capability.** `zxv_cap_check(cap, needs)` must pass, where
   `needs` is the `ZXV_CAP_*` bits named after `needs` in the spec. With no
   `needs`, the cap must still be live, unrevoked and attested `TRUE`.
2. **checks integer arguments** against a declared range. The range
   defaults to the C type's range and can only be narrowed. `abs(INT_MIN)`
   and `toupper(300)` are undefined behaviour in C; the example specs
   declare `int[-INT_MAX..INT_MAX]` and `int[-1..UCHAR_MAX]`, so those calls
   are refused before the C function runs.
3. **takes float arguments as exact rationals** (`f64 from rat`, `f32 from
   rat`) and converts them only when the float type holds the value
   exactly: 3/4 yes, 1/10 no (`ZF_E_INEXACT`). Nothing is rounded.
4. **refuses float results by default.** A spec must opt in with `as rat`,
   and then the returned bits are converted exactly with libzxv's
   `zxv_rat_from_ieee754`: `sqrt(2)` is reported as
   6369051672525773/4503599627370496, the exact value of the double. NaN
   and infinities give `ZXV_E_FLOAT`; a finite value with no `int64`
   rational form (2^-80) gives `ZXV_E_RANGE`.
5. **checks integer results** against a declared range (`ZF_E_RESULT`).
6. **copies strings.** A `cstr` argument is given as bytes and a length,
   checked against declared bounds, refused if it contains a NUL, and
   passed to C as a NUL-terminated copy. The C function never reads the
   caller's buffer.
7. **proves its declaration at compile time.** Each wrapper is preceded by
   `_Static_assert(_Generic(&sqrt, double (*)(double): 1, default: 0), ...)`
   and, for each declared range, a `_Static_assert` that the range fits the
   C type. A spec that disagrees with the library's own header does not
   compile (`make check-signatures` proves this on three bad specs).

> **z-ffi is not a sandbox.** It is a typed wrapper generator plus runtime
> checks. The C function runs in your process with your privileges. A bug
> in the library, or a hostile library, can read and write anything the
> process can, whatever the spec says. The capability check is least
> privilege inside a trusted process, the same honest limit libzxv states
> for `zxv_cap`. To run code you do not trust, use
> [Z-Wasm](../z-wasm/). See [Threat model](#threat-model).

Kit API version 0.1 (`include/zffi.h`). It calls libzxv ABI 1.2 only.

## Quickstart

```sh
make -C ../../libzxv all     # once
make test
make demo
```

`make demo` generates wrappers from [`examples/libc.zf`](examples/libc.zf)
and [`examples/libm.zf`](examples/libm.zf) and runs
[`examples/ffi_demo.c`](examples/ffi_demo.c), which holds no float type:

```text
libm (spec 279abe418ffa7c9b...):
  sqrt(9/4)              = 3/2 = 1.5
  sqrt(2)                = 6369051672525773/4503599627370496
  sqrt(1/10)             refused, ZF_E_INEXACT
                           libm.sqrt: argument 0 'x' = 1/10 has no exact binary64 (double) value (it would be rounded)
  sqrt(-1)               refused, ZXV_E_FLOAT
                           libm.sqrt: the result is NaN (bits 0xfff8000000000000), which is not a number
  ldexp(3/4, 4)          = 12
  ldexp(1, 1024)         refused, ZXV_E_FLOAT
                           libm.ldexp: the result is an infinity (bits 0x7ff0000000000000), which is not a number
libc (spec abf639ce7296a657...):
  abs(-42)               = 42
  abs(INT_MIN)           refused, ZF_E_RANGE
                           libc.abs: argument 0 'x' = -2147483648 is below -2147483647
  toupper(300)           refused, ZF_E_RANGE
                           libc.toupper: argument 0 'c' = 300 is above 255
  strlen("ZEDEC")        = 5
  abs(-42), no cap       refused, ZXV_E_CAP
                           libc.abs: no capability (every call needs one)
```

(The NaN's sign bit depends on the platform's `sqrt`.)

### In your own build

```sh
build/zf-gen mylib.zf gen/mylib_zf.h gen/mylib_zf.c
cc -std=c11 -c gen/mylib_zf.c -Igen -I<z-ffi>/include -I<libzxv>/include
cc app.c gen/mylib_zf.o <z-ffi>/build/libzffi.a -lzxv -lm
```

The header names each wrapper `zf_<library>_<function>`, documents its
C signature, ranges and capability, and records the spec's SHA3-256 as
`ZF_<LIBRARY>_SPEC_SHA3`, so a build can be traced to the exact spec that
produced it.

## Spec language

```text
library NAME;                                  # first, once: the wrapper prefix
include <header.h>;                            # or "header.h"; any number
fn NAME(PARAM, ...) -> RESULT [needs CAPS];
```

| Parameter | Wrapper takes | C receives |
|---|---|---|
| `x: int` (or another integer type) | `int64_t x` (`uint64_t` if unsigned) | `(int) x` after the range check |
| `x: int[-INT_MAX..INT_MAX]` | the same, narrowed | |
| `x: f64 from rat` / `f32 from rat` | `int64_t x_num, int64_t x_den` | the exactly equal `double` / `float` |
| `s: cstr` / `cstr[1..64]` | `const char *s, size_t s_len` | a NUL-terminated copy (default bounds 0..65536) |

| Result | Wrapper gives |
|---|---|
| `void` | nothing |
| `int`, `size`, ... with an optional range | `int64_t *zf_out` (`uint64_t *` if unsigned) |
| `f64 as rat` / `f32 as rat` | `int64_t *zf_out_num, int64_t *zf_out_den` |
| `f64` without `as rat` | refused by `zf-gen` |
| `cstr` (any pointer) | refused by `zf-gen`: z-ffi cannot know who owns it |

Integer types: `schar uchar short ushort int uint long ulong llong
ullong size i8 i16 i32 i64 u8 u16 u32 u64`. A range bound is a decimal
number or an upper-case macro from the C headers (`INT_MAX`, `SIZE_MAX`),
optionally negated. `needs` takes `none` or `|`-separated names of the
`ZXV_CAP_*` bits: `LEDGER_READ LEDGER_POST CRYPTO_SIGN CRYPTO_KEM
AI_INFERENCE BUDGET PHASE ATTEST SWARM EVM`. Which bit gates which C
function is the spec author's decision; z-ffi enforces it.

`zf-gen` refuses a spec with the line, column and reason:

```text
mylib.zf:4:29: a result of type f64 is refused unless the spec says 'as rat' (exact conversion with zxv_rat_from_ieee754)
  fn cbrt(x: f64 from rat) -> f64;
                              ^
```

What `zf-gen` cannot see (the C headers) the compiler checks: the
signature, and that each range fits its C type with lo <= hi.

## R11 and generated code

The runtime (`src/zf_runtime.c`), the generator (`src/zf_gen.c`), the
tool, the tests and the demo hold no float type: `check-nofloat` passes
over them and they are compiled with `-mgeneral-regs-only` where the
compiler has it. Exact rational to IEEE-754 conversion is done in integer
arithmetic (`zf_rat_to_f64_bits`, `zf_rat_to_f32_bits`), and the way back
is `zxv_rat_from_ieee754`.

The **generated** wrappers necessarily hold the C call's `double` and
`float` values for the length of the call. They are generated, never
edited by hand, and are outside the no-float check; every value enters and
leaves them through the integer-only runtime.

## Threat model

**Assets.** The exactness of values that cross into C and back, the
capability policy on who may call what, and the process's memory safety
where the C standard leaves behaviour undefined for out-of-range inputs.

**Adversaries.** Callers inside the host that pass out-of-range values or
hold the wrong capability, and spec mistakes.

| Protection | Mechanism |
|---|---|
| No call without a capability | `zf_enter` runs `zxv_cap_check(cap, needs)` before anything else |
| No out-of-range integer reaches C | declared ranges, checked in `int64`/`uint64` before the cast to the C type |
| No silent rounding into C | float arguments must be exactly representable |
| No float out of C unless asked | results need `as rat`; NaN and infinities are refused |
| No out-of-bounds string read | `cstr` arguments are copied with an explicit length; NUL is refused |
| Spec matches the library | `_Generic` signature assertions and range assertions, at compile time |
| Generator robustness | bounded spec (1 MiB, 256 functions, 8 parameters), fuzzed under ASan + UBSan |

**Out of scope, and the honest limits:**

- **Not a sandbox.** The C library runs with the process's privileges and
  memory. Nothing stops a library from crashing the process, writing out
  of bounds, or ignoring its documented contract.
- **Global state and side effects.** `errno`, locale (`toupper` depends on
  `LC_CTYPE`), floating-point environment and rounding mode, signals and
  threads are not managed. A wrapper is as thread-safe as the function it
  calls.
- **The spec is trusted.** It is the spec author's job to declare the
  ranges where the C function's behaviour is defined. z-ffi enforces the
  spec; it does not know the C standard. Macro bounds are copied into the
  generated C.
- **Types.** No pointers other than input `cstr`, no structs, arrays,
  callbacks or variadic functions, no pointer or float-as-float results.
- **Exactness, not accuracy.** `sqrt(2)` is reported exactly as the double
  the library returned; whether that double is correctly rounded is the
  library's business.
- **Kit API 0.x.** It is not yet covered by the libzxv stability promise.

## Limits

| Limit | Value |
|---|---|
| spec text | 1 MiB |
| functions per spec | 256 |
| parameters per function | 8 |
| identifier | 63 bytes (library name 24) |
| includes | 32, relative paths without `..` |
| `cstr` length bound | 2^31 - 1 (default 0..65536) |
| float arguments | values `n/d` with `d` a power of two and the odd part of `n` at most 53 (`f64`) or 24 (`f32`) bits |

## Tests

```sh
make test                          # everything below except the long runs
make SANITIZE=1 OUT=build/san test # the same under ASan + UBSan
make fuzz FUZZ_ITERS=500000        # deterministic spec fuzzer
ZF_FUZZ_SEED=3 make fuzz           # another deterministic stream
make fuzz-libfuzzer                # libFuzzer, if clang ships its runtime
```

| Suite | What |
|---|---|
| `test_runtime` | 81 checks: binary64 and binary32 encodings of exact rationals written out as bits (zero, signs, 2^53 - 1, 2^53, 2^53 + 1 refused, 2^62, 2^-62, the double nearest 1/3, 1/10 refused, `INT64_MAX` refused), 200,000 random exact values round-tripped through `zxv_rat_from_ieee754` with their one-bit-wider neighbours judged correctly, capability entry (none, needed bit missing, unattested, revoked ancestor), argument and result range messages, `cstr` copies, NaN, infinities and 2^-80 results |
| `test_gen` | 249 checks: 51 refused specs, each with its exact line, column and message and the offset agreeing with them; an accepted spec against the exact text of its prototypes, signature assertions, range assertions, capability masks and conversions; determinism; the spec digest; 256/257 functions, 8 parameters, 63/64-byte names, 1 MiB; message truncation and API misuse |
| `test_wrappers` | 39 checks on wrappers generated from `examples/libc.zf` and `examples/libm.zf`, calling the real libc and libm: `abs`, `llabs`, `strlen`, `strncmp`, `toupper`, `sqrt`, `floor`, `ilogb`, `fabsf`, `ldexp`, with refusals for `INT_MIN`, `LLONG_MIN`, `toupper(300)`, embedded NUL, 4097-byte strings, 1/10, 2^24 + 1 as a float, NaN, infinity, 2^-80, a missing capability and a cap without `BUDGET` |
| `check-signatures` | `test/bad_signature.zf` (`long abs(long)`), `test/bad_float.zf` (`sqrt` taking a float) and `test/bad_range.zf` (an `int` range up to 3000000000) are generated and must be refused by the C compiler with z-ffi's own static assertion |
| `fuzz_gen` | for every input: deterministic status, position, message and output; the position inside the input and consistent with line and column; one prototype and one signature check per accepted function |
| `ffi_demo` | the demo runs as part of `make test` |
| `check-nofloat` | R11 over the runtime, generator, tool, tests and demo |

Clean runs on this branch: the suites under gcc and clang, the same under
ASan + UBSan with leak detection, and 4 x 250,000 sanitized fuzz mutants
(seeds 1-4).

## Files

| Path | |
|---|---|
| `include/zffi.h` | the kit API: runtime checks, exact conversions, the generator |
| `src/zf_runtime.c` | capability entry, range checks, rational <-> IEEE-754 bits (R11) |
| `src/zf_gen.c` | the spec parser and wrapper generator (R11) |
| `tools/zf_gen_tool.c` | `zf-gen SPEC.zf OUT.h OUT.c` |
| `examples/libc.zf`, `examples/libm.zf` | example specs |
| `examples/ffi_demo.c` | the demo above |
| `test/bad_*.zf` | specs that must not compile |

## Licence

Apache-2.0. Registered to 36N9 Genetics LLC, Michael Laurence Curzi.
Provided without warranty.
