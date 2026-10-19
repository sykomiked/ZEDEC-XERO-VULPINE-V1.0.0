<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Exact numbers: `Rational`, and why floats are refused

**Who this is for:** anyone who handles money, prices, rates or measurements
with ZXV.
**What you will be able to do:** do exact arithmetic, read numbers from text,
convert a float on purpose when you must, and understand each refusal.

## The problem

Computers usually store fractions as **floats** (IEEE-754 binary floating
point). A float cannot hold 0.1 exactly, so small errors creep in:

```python
>>> 0.1 + 0.2
0.30000000000000004
```

For money this is a bug. ZXV removes the problem at the boundary: every
number that enters the library is an integer or an exact fraction. A float
is refused unless you convert it explicitly.

## `Rational`: an exact fraction

A `Rational` is a numerator and a denominator, both 64-bit signed integers,
always kept in lowest terms. All examples below are Python and were run
against the SDK; every other SDK has the same type with the same name (see
[SDK_DESIGN §3](../SDK_DESIGN.md#3-exactness-no-floats-at-the-boundary)).

```python
from decimal import Decimal
from fractions import Fraction
from zxv import Rational

Rational.parse("12.75")                         # 12.75  (51/4 inside)
Rational.of(1, 3)                               # 1/3
Rational.parse("0.1") + Rational.parse("0.2") == Rational.parse("0.3")   # True
Rational.of(1, 3) * 3                           # 1
Rational.from_decimal(Decimal("19.99"))         # 19.99
Rational.from_fraction(Fraction(2, 7))          # 2/7
```

When printed, a value with a terminating decimal form is written as a
decimal (`12.75`), and anything else as a fraction (`1/3`). The C function
behind this is `zxv_rat_format`.

## What gets refused, and why

| You try | You get | Why |
|---|---|---|
| `Rational.parse("0.1") + 0.2` | `FloatRejectedError` (`ZXV_E_FLOAT`) | a float slipped in; ZXV will not guess what you meant |
| `float(Rational.of(1, 2))` | `TypeError` | turning an exact value into a float silently loses exactness |
| `Rational.parse("1e-3")` | `ZXV_E_FORMAT` | exponents are not accepted; write `0.001` |
| `Rational.of(2**62) * 4` | `ZXV_E_RANGE` | the result does not fit in 64 bits; ZXV never wraps around |

Overflow is an error, not a silent wrap. If a result does not fit, you find
out at once.

## When you really have a float

Sometimes a float arrives from outside: a sensor, a spreadsheet, a JSON
library. Convert it on purpose with `from_float_exact`:

```python
x = Rational.from_float_exact(0.1)
print(x.numerator, x.denominator)               # 3602879701896397 36028797018963968
print(x == Rational.parse("0.1"))               # False
```

This gives the exact value the float really holds, which is not 0.1. That
surprise is the point: you see the error instead of carrying it. If you
meant "the decimal 0.1", read the number as text (`Rational.parse("0.1")`)
or as a `Decimal`. A NaN or an infinity is refused with `ZXV_E_FLOAT`.

In C, a float crosses the boundary only as its raw 64-bit pattern (ABI rule
R11, "no floats"):

```c
double x = 0.1;
uint64_t bits;
int64_t n, d;
memcpy(&bits, &x, sizeof bits);
zxv_rat_from_ieee754(bits, &n, &d);   /* n = 3602879701896397, d = 36028797018963968 */
```

## Fixed point for engines

Inference code inside ZXV uses **Q16.16**: an integer that counts 1/65536
steps. `Rational.parse("0.7").to_q16()` gives `45875`, rounded the same way
in every SDK (C: `zxv_rat_to_q16(7, 10, &q)`).

## Money is always an integer

Ledger amounts are not `Rational`. They are integers in the currency's
**minor unit** (kobo for NGN, cents for USD). 150.00 NGN is `15000`. See
[Ledger and currencies](ledger-and-currencies.md).

## Next

- [Four-valued logic](four-valued-logic.md): the same "do not guess" idea,
  applied to true and false.
- [Errors and fail-closed](errors-and-fail-closed.md): what each status class
  means.
