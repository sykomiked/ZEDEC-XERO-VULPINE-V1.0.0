// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

package zxv

/*
#include "zxv_api.h"
*/
import "C"

import "fmt"

// Truth is a four-valued (paraconsistent) truth value: TruthNeither,
// TruthTrue, TruthFalse or TruthBoth, numbered as the kernel numbers them
// (0..3). TruthBoth is a held contradiction, not an error.
//
// Truth is a small struct, not a bool: converting needs an explicit
// policy (IsCertainlyTrue, ToBool, RequireTrue). Compare values with ==.
type Truth struct {
	state uint8
}

// The four truth values.
var (
	TruthNeither = Truth{0} // S0: unknown, unattested, pending
	TruthTrue    = Truth{1} // S+: attested present and valid
	TruthFalse   = Truth{2} // S-: attested absent or invalid
	TruthBoth    = Truth{3} // contradictory evidence, held
)

// AllTruths lists the four values in kernel order.
var AllTruths = [4]Truth{TruthNeither, TruthTrue, TruthFalse, TruthBoth}

// TruthFromRaw returns the value with kernel number v (0..3).
func TruthFromRaw(v int) (Truth, error) {
	if v < 0 || v > 3 {
		return TruthNeither, newError(StatusArg, fmt.Sprintf("%d is not a truth value", v))
	}
	return Truth{uint8(v)}, nil
}

// TruthFromEvidence: both kinds give TruthBoth, only positive TruthTrue,
// only negative TruthFalse, none TruthNeither.
func TruthFromEvidence(positive, negative uint32) Truth {
	var r C.int32_t
	C.zxv_lp_from_evidence(C.uint32_t(positive), C.uint32_t(negative), &r)
	return Truth{uint8(r)}
}

// Raw returns the kernel number.
func (t Truth) Raw() int { return int(t.state) }

// Not negates (True <-> False; Both and Neither are fixed).
func (t Truth) Not() Truth {
	var r C.int32_t
	C.zxv_lp_not(C.int32_t(t.state), &r)
	return Truth{uint8(r)}
}

// And is the four-valued conjunction (the C tables).
func (t Truth) And(o Truth) Truth {
	var r C.int32_t
	C.zxv_lp_and(C.int32_t(t.state), C.int32_t(o.state), &r)
	return Truth{uint8(r)}
}

// Or is the four-valued disjunction (the C tables).
func (t Truth) Or(o Truth) Truth {
	var r C.int32_t
	C.zxv_lp_or(C.int32_t(t.state), C.int32_t(o.state), &r)
	return Truth{uint8(r)}
}

// IsCertainlyTrue is true only for TruthTrue.
func (t Truth) IsCertainlyTrue() bool { return t == TruthTrue }

// IsCertainlyFalse is true only for TruthFalse.
func (t Truth) IsCertainlyFalse() bool { return t == TruthFalse }

// IsCertain is true for TruthTrue and TruthFalse.
func (t Truth) IsCertain() bool { return t == TruthTrue || t == TruthFalse }

// ToBool converts with an explicit policy for the two non-classical values.
func (t Truth) ToBool(onBoth, onNeither bool) bool {
	switch t {
	case TruthTrue:
		return true
	case TruthFalse:
		return false
	case TruthBoth:
		return onBoth
	default:
		return onNeither
	}
}

// RequireTrue returns nil for TruthTrue, else an error matching
// ErrVerificationFailed naming the value.
func (t Truth) RequireTrue() error {
	if t == TruthTrue {
		return nil
	}
	return newError(StatusVerify, "required TRUE, got "+t.String())
}

// String returns "NEITHER", "TRUE", "FALSE" or "BOTH".
func (t Truth) String() string {
	return C.GoString(C.zxv_lp_name(C.int32_t(t.state)))
}
