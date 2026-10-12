// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

package zxv

// The assurance fee (ABI 1.5): 0.08889% of a transaction's gross and its
// 50/25/15/10 split, exact and integer-only.
//
//	fee(g) = floor(g * 8889 / 10,000,000)
//
// g is in the unit's own minor measure (money minor units, compute
// token-cycles, bandwidth bytes, storage byte-hours). The product is formed
// in 128 bits, so every g up to math.MaxUint64 is exact; fee(1124) = 0 and
// fee(1125) = 1. The fee is charged once per transaction, on its gross:
// never on a fee, never compounded.
//
// CARRY. The sub-unit remainder of an account (0 <= r < FeeDen) is carried
// to its next charge (AssuranceFeeCarry), so many small charges pay exactly
// what one charge of their sum pays. The caller stores r per account.
//
// THE FOUR BUCKETS. A fee is split 50% reserve floor (backs Vino), 25%
// V-Bill dividend pool, 15% infrastructure / node bounties, 10%
// regenerative capital. Buckets 1-3 get floor(fee * pct / 100); the reserve
// floor gets the rest, so the four parts always sum to the fee exactly.
//
// Everything here is pure: no context, no capability. Honest limits: these
// are the arithmetic rules only. How a fee, a reserve or a dividend pool is
// treated for tax, securities or consumer law is a question for counsel;
// nothing here is a licence or a certification.

/*
#include "zxv_api.h"
*/
import "C"

import "unsafe"

// The fee rate: FeeNum / FeeDen = 0.08889%.
const (
	FeeNum = uint64(C.ZXV_FEE_NUM) // 8889
	FeeDen = uint64(C.ZXV_FEE_DEN) // 10^7; a carry is always below this
)

// FeeBucket indexes a FeeSplit.
type FeeBucket int

// The four buckets.
const (
	FeeReserveFloor  FeeBucket = C.ZXV_FEE_RESERVE_FLOOR  // 50% and every remainder unit
	FeeVBillDividend FeeBucket = C.ZXV_FEE_VBILL_DIVIDEND // 25%
	FeeInfraBounty   FeeBucket = C.ZXV_FEE_INFRA_BOUNTY   // 15%
	FeeRegenCapital  FeeBucket = C.ZXV_FEE_REGEN_CAPITAL  // 10%
)

// String returns the bucket's name, e.g. "reserve_floor".
func (b FeeBucket) String() string {
	names := []string{"reserve_floor", "vbill_dividend", "infra_bounty", "regen_capital"}
	if b >= 0 && int(b) < len(names) {
		return names[b]
	}
	return "FeeBucket(?)"
}

// FeeUnit is the kind of unit a gross is measured in.
type FeeUnit int

// Unit kinds.
const (
	FeeUnitMoney     FeeUnit = C.ZXV_FEE_UNIT_MONEY     // minor units of a ledger asset
	FeeUnitCompute   FeeUnit = C.ZXV_FEE_UNIT_COMPUTE   // token-cycles
	FeeUnitBandwidth FeeUnit = C.ZXV_FEE_UNIT_BANDWIDTH // bytes
	FeeUnitStorage   FeeUnit = C.ZXV_FEE_UNIT_STORAGE   // byte-hours
)

// FeeSplit is a fee split into its four buckets, indexed by FeeBucket. The
// parts always sum to the fee exactly.
type FeeSplit [C.ZXV_FEE_BUCKETS]uint64

// Total returns the sum of the four parts (the fee that was split).
func (s FeeSplit) Total() uint64 { return s[0] + s[1] + s[2] + s[3] }

// AssuranceFee returns fee(gross) with no carry. It fails only when the
// fee group is not built into the library (ErrNotImplemented).
func AssuranceFee(gross uint64) (uint64, error) {
	var f C.uint64_t
	err := call(func() C.int32_t { return C.zxv_fee_assure(C.uint64_t(gross), &f) })
	return uint64(f), err
}

// AssuranceFeeCarry returns the fee on gross for an account whose stored
// remainder is remIn, and the remainder to store for its next charge.
// ZXV_E_RANGE (nothing computed) when remIn >= FeeDen: a corrupt carry.
func AssuranceFeeCarry(remIn, gross uint64) (fee, remOut uint64, err error) {
	var f, r C.uint64_t
	err = call(func() C.int32_t { return C.zxv_fee_assure_carry(C.uint64_t(remIn), C.uint64_t(gross), &f, &r) })
	return uint64(f), uint64(r), err
}

// SplitFee splits fee into the four buckets (the reserve floor takes every
// remainder unit). The partition is re-checked before it is returned.
func SplitFee(fee uint64) (FeeSplit, error) {
	var s FeeSplit
	err := call(func() C.int32_t { return C.zxv_fee_split(C.uint64_t(fee), (*C.uint64_t)(unsafe.Pointer(&s[0]))) })
	return s, err
}

// FeeRate is an exact rate Num/Den.
type FeeRate struct {
	Num uint64 // numerator
	Den uint64 // denominator
}

// FeePolicy is the default fee policy (FeePolicyDefault).
type FeePolicy struct {
	// Percent is each bucket's share in percent: 50, 25, 15, 10.
	Percent [4]uint32
	// AllowBelow is false: a contribution below the fee is raised to it.
	AllowBelow bool
	// CreditRate is the posted VFV store-credit rate per FeeUnit for a
	// voluntary contribution above the fee: money 1/1, the others 0/1
	// (unset: the excess earns no credit until a rate is posted).
	CreditRate [4]FeeRate
	// Multiplier is the operator's credit multiplier, 1/1.
	Multiplier FeeRate
	// CommonsCap is the no-monopoly cap on one recipient's share of the
	// non-money commons pool, 8/21.
	CommonsCap FeeRate
}

// FeePolicyDefault returns the default policy. It fails only when the fee
// group is not built into the library (ErrNotImplemented).
func FeePolicyDefault() (FeePolicy, error) {
	var pct [4]C.uint32_t
	var below C.int32_t
	var rn, rd [4]C.uint64_t
	var mn, md, cn, cd C.uint64_t
	if err := call(func() C.int32_t {
		return C.zxv_fee_policy_default(&pct[0], &below, &rn[0], &rd[0], &mn, &md, &cn, &cd)
	}); err != nil {
		return FeePolicy{}, err
	}
	p := FeePolicy{AllowBelow: below != 0, Multiplier: FeeRate{uint64(mn), uint64(md)}, CommonsCap: FeeRate{uint64(cn), uint64(cd)}}
	for i := 0; i < 4; i++ {
		p.Percent[i] = uint32(pct[i])
		p.CreditRate[i] = FeeRate{uint64(rn[i]), uint64(rd[i])}
	}
	return p, nil
}
