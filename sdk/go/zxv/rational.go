// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

package zxv

/*
#include "zxv_api.h"
*/
import "C"

import (
	"math"
	"math/big"
)

// Rational is an exact rational number num/den, always reduced with
// den > 0. It is an immutable value type (two int64); the zero value is 0.
// Arithmetic goes through libzxv and is checked: overflow is an error
// (ZXV_E_RANGE), never a wrap or a rounding.
//
// No method takes a float64 except FromFloat64Exact.
type Rational struct {
	num, den int64
}

// Common values.
var (
	RatZero = Rational{0, 1}
	RatOne  = Rational{1, 1}
)

// NewRational returns num/den reduced. A zero denominator is ZXV_E_ARG;
// math.MinInt64 in either place is ZXV_E_RANGE.
func NewRational(num, den int64) (Rational, error) {
	var n, d C.int64_t
	if err := call(func() C.int32_t { return C.zxv_rat_normalize(C.int64_t(num), C.int64_t(den), &n, &d) }); err != nil {
		return Rational{}, err
	}
	return Rational{int64(n), int64(d)}, nil
}

// IntRational returns the integer n (always representable).
func IntRational(n int32) Rational { return Rational{int64(n), 1} }

// ParseRational parses "12.75", "-0.1", "3/4" or "42" exactly. Exponents,
// spaces and "NaN" are ZXV_E_FORMAT.
func ParseRational(s string) (Rational, error) {
	cs, err := cstring(s)
	if err != nil {
		return Rational{}, err
	}
	defer freeC(cs)
	var n, d C.int64_t
	if err := call(func() C.int32_t { return C.zxv_rat_parse(cs, &n, &d) }); err != nil {
		return Rational{}, err
	}
	return Rational{int64(n), int64(d)}, nil
}

// FromFloat64Exact is THE float boundary: it returns the exact value of a
// binary64 float, so 0.1 becomes 3602879701896397/36028797018963968, not
// 1/10. NaN and infinities match ErrFloatRejected; a value that needs more
// than 63 bits (e.g. 1e-300) is ZXV_E_RANGE.
func FromFloat64Exact(x float64) (Rational, error) {
	return FromIEEE754Bits(math.Float64bits(x))
}

// FromIEEE754Bits is FromFloat64Exact for raw IEEE-754 binary64 bits.
func FromIEEE754Bits(bits uint64) (Rational, error) {
	var n, d C.int64_t
	if err := call(func() C.int32_t { return C.zxv_rat_from_ieee754(C.uint64_t(bits), &n, &d) }); err != nil {
		return Rational{}, err
	}
	return Rational{int64(n), int64(d)}, nil
}

// Num returns the numerator (carries the sign).
func (r Rational) Num() int64 { return r.num }

// Den returns the denominator (always > 0).
func (r Rational) Den() int64 {
	if r.den == 0 {
		return 1
	}
	return r.den
}

type ratOp func(an, ad, bn, bd C.int64_t, on, od *C.int64_t) C.int32_t

func (r Rational) op(o Rational, f ratOp) (Rational, error) {
	var n, d C.int64_t
	err := call(func() C.int32_t {
		return f(C.int64_t(r.num), C.int64_t(r.Den()), C.int64_t(o.num), C.int64_t(o.Den()), &n, &d)
	})
	if err != nil {
		return Rational{}, err
	}
	return Rational{int64(n), int64(d)}, nil
}

// Add returns r + o exactly.
func (r Rational) Add(o Rational) (Rational, error) {
	return r.op(o, func(a, b, c, d C.int64_t, n, m *C.int64_t) C.int32_t { return C.zxv_rat_add(a, b, c, d, n, m) })
}

// Sub returns r - o exactly.
func (r Rational) Sub(o Rational) (Rational, error) {
	return r.op(o, func(a, b, c, d C.int64_t, n, m *C.int64_t) C.int32_t { return C.zxv_rat_sub(a, b, c, d, n, m) })
}

// Mul returns r * o exactly.
func (r Rational) Mul(o Rational) (Rational, error) {
	return r.op(o, func(a, b, c, d C.int64_t, n, m *C.int64_t) C.int32_t { return C.zxv_rat_mul(a, b, c, d, n, m) })
}

// Div returns r / o exactly; division by zero is ZXV_E_ARG.
func (r Rational) Div(o Rational) (Rational, error) {
	return r.op(o, func(a, b, c, d C.int64_t, n, m *C.int64_t) C.int32_t { return C.zxv_rat_div(a, b, c, d, n, m) })
}

// Neg returns -r (cannot overflow: the numerator is never MinInt64).
func (r Rational) Neg() Rational { return Rational{-r.num, r.Den()} }

// Cmp returns -1, 0 or +1, exactly (128-bit cross multiplication).
func (r Rational) Cmp(o Rational) int {
	a := new(big.Int).Mul(big.NewInt(r.num), big.NewInt(o.Den()))
	b := new(big.Int).Mul(big.NewInt(o.num), big.NewInt(r.Den()))
	return a.Cmp(b)
}

// Equal reports exact equality.
func (r Rational) Equal(o Rational) bool { return r.num == o.num && r.Den() == o.Den() }

// Sign returns -1, 0 or +1.
func (r Rational) Sign() int {
	switch {
	case r.num < 0:
		return -1
	case r.num > 0:
		return 1
	}
	return 0
}

// ToQ16 rounds to Q16.16 fixed point (half away from zero), as the engine's
// sampling parameters use. ZXV_E_RANGE outside int32.
func (r Rational) ToQ16() (int32, error) {
	var q C.int32_t
	if err := call(func() C.int32_t { return C.zxv_rat_to_q16(C.int64_t(r.num), C.int64_t(r.Den()), &q) }); err != nil {
		return 0, err
	}
	return int32(q), nil
}

// String renders exactly: a terminating decimal when one exists ("12.75"),
// otherwise "n/d". It never rounds.
func (r Rational) String() string {
	s, err := readString(func(b *C.char, c C.size_t, l *C.size_t) C.int32_t {
		return C.zxv_rat_format(C.int64_t(r.num), C.int64_t(r.Den()), b, c, l)
	})
	if err != nil {
		return big.NewRat(r.num, r.Den()).String()
	}
	return s
}

// BigRat returns the value as a *big.Rat (exact).
func (r Rational) BigRat() *big.Rat { return big.NewRat(r.num, r.Den()) }
