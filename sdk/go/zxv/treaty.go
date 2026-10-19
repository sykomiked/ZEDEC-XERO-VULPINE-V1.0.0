// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

package zxv

// Treaties (ABI 1.5): COBOL copybook treaties and Sutra text.
//
// A treaty is one batch of settlement work: who takes part, what each party
// deposits and may claim back, how the batch's dividend is shared (each
// party's weight num/den), and who owes whom. It has two exact spellings
// that hold the same treaty:
//
//   - RECORD: the fixed TreatyRecordLen-byte COBOL copybook record
//     (ZXVTRTY.cpy), all DISPLAY text, so plain ASCII on every machine.
//   - SUTRA: compact text, for example
//
//     TREATY CORRIDOR-7 USD SETL
//     PARTY ACME 5000 4000 1/2
//     PARTY BETA 3000 3000 1/2
//     OWE ACME BETA 1200
//     END
//
//     (single spaces, "\n" line ends, no leading zeros, fractions in lowest
//     terms, nothing after "END\n").
//
// Each spelling is canonical: a parse followed by an emit gives the input
// back byte for byte, and the record and Sutra forms of one treaty compare
// Equal. Every parse and emit checks the rules R1-R4 (weights in lowest
// terms that add up to exactly 1, claim <= deposit, every amount within
// PIC S9(18), 1..16 distinct parties, 0..64 obligations between different
// known parties). A refusal is a *TreatyError (errors.Is(err, ErrTreaty))
// carrying the TreatyFault and, for a parse, the byte offset of the first
// fault.
//
// Everything here is pure: no context and no capability. Run executes a
// treaty in a private scratch engine that is freed before it returns; its
// numbers are a deterministic report, not a payment.
//
// Honest limits: a treaty is a batch description, not a contract in law;
// signatures, consent and authority live elsewhere. One asset per treaty.
// The dividend shares are reported only; nothing is paid out.

/*
#include <stdlib.h>
#include "zxv_api.h"
*/
import "C"

import (
	"errors"
	"fmt"
	"runtime"
	"unsafe"
)

// Treaty sizes and limits.
const (
	TreatyRecordLen  = int(C.ZXV_TREATY_REC_LEN)     // bytes in a ZXVTRTY.cpy treaty record
	TreatyResultLen  = int(C.ZXV_TREATY_RESULT_LEN)  // bytes in a ZXVRSLT.cpy result record
	TreatySutraMax   = int(C.ZXV_TREATY_SUTRA_MAX)   // longest canonical Sutra text, in bytes
	TreatyMaxParties = int(C.ZXV_TREATY_MAX_PARTIES) // parties per treaty
	TreatyMaxOwes    = int(C.ZXV_TREATY_MAX_OWES)    // obligations per treaty
	TreatyNameMax    = int(C.ZXV_TREATY_NAME_MAX)    // treaty name: A-Z 0-9 '-', starts with a letter
	TreatyIDMax      = int(C.ZXV_TREATY_ID_MAX)      // party id: same alphabet
	TreatyWeightMax  = uint32(C.ZXV_TREATY_WEIGHT_MAX)
	TreatyPIC18Max   = int64(999999999999999999) // largest |amount| in PIC S9(18)
)

// TreatyOp is a treaty's operation.
type TreatyOp int32

// Treaty operations.
const (
	TreatyOpNone TreatyOp = 0                    // an empty treaty
	TreatyOpNet  TreatyOp = C.ZXV_TREATY_OP_NET  // net the obligations only
	TreatyOpSetl TreatyOp = C.ZXV_TREATY_OP_SETL // net, post the legs with the fee, pay claims
)

// String returns "NET", "SETL" or "NONE".
func (o TreatyOp) String() string {
	switch o {
	case TreatyOpNet:
		return "NET"
	case TreatyOpSetl:
		return "SETL"
	case TreatyOpNone:
		return "NONE"
	}
	return fmt.Sprintf("TreatyOp(%d)", int32(o))
}

// TreatyFault says why a treaty was refused.
type TreatyFault int32

// Treaty faults.
const (
	TreatyFaultNone   TreatyFault = C.ZXV_TREATY_FAULT_NONE
	TreatyFaultMagic  TreatyFault = C.ZXV_TREATY_FAULT_MAGIC  // record does not start with "ZXT1"
	TreatyFaultSyntax TreatyFault = C.ZXV_TREATY_FAULT_SYNTAX // Sutra text outside the grammar
	TreatyFaultName   TreatyFault = C.ZXV_TREATY_FAULT_NAME   // name not 1..30 of A-Z 0-9 '-'
	TreatyFaultAsset  TreatyFault = C.ZXV_TREATY_FAULT_ASSET  // asset not three capital letters
	TreatyFaultOp     TreatyFault = C.ZXV_TREATY_FAULT_OP     // operation not NET or SETL
	TreatyFaultCount  TreatyFault = C.ZXV_TREATY_FAULT_COUNT  // party or obligation count out of range
	TreatyFaultNumber TreatyFault = C.ZXV_TREATY_FAULT_NUMBER // malformed number or outside PIC S9(18)
	TreatyFaultParty  TreatyFault = C.ZXV_TREATY_FAULT_PARTY  // bad, duplicate or unknown party id
	TreatyFaultWeight TreatyFault = C.ZXV_TREATY_FAULT_WEIGHT // R1: the weights
	TreatyFaultClaim  TreatyFault = C.ZXV_TREATY_FAULT_CLAIM  // R2: a claim above its deposit
	TreatyFaultOwe    TreatyFault = C.ZXV_TREATY_FAULT_OWE    // obligation to self, or not above zero
	TreatyFaultPad    TreatyFault = C.ZXV_TREATY_FAULT_PAD    // unused record slot or field not blank
)

// String returns the fault's name, e.g. "WEIGHT".
func (f TreatyFault) String() string {
	names := map[TreatyFault]string{TreatyFaultNone: "NONE", TreatyFaultMagic: "MAGIC",
		TreatyFaultSyntax: "SYNTAX", TreatyFaultName: "NAME", TreatyFaultAsset: "ASSET",
		TreatyFaultOp: "OP", TreatyFaultCount: "COUNT", TreatyFaultNumber: "NUMBER",
		TreatyFaultParty: "PARTY", TreatyFaultWeight: "WEIGHT", TreatyFaultClaim: "CLAIM",
		TreatyFaultOwe: "OWE", TreatyFaultPad: "PAD"}
	if n, ok := names[f]; ok {
		return n
	}
	return fmt.Sprintf("TreatyFault(%d)", int32(f))
}

// TreatyError is the error of a refused treaty (ZXV_E_TREATY, class USAGE).
// errors.Is(err, ErrTreaty) and errors.Is(err, ErrUsage) match it, and
// errors.As still reaches the *Error underneath.
type TreatyError struct {
	// Fault is the first rule the treaty broke.
	Fault TreatyFault
	// Offset is the byte offset of the first fault in the parsed text or
	// record (0 for Validate and for emits).
	Offset uint32
	// Err is the class error (*UsageError) with the status and detail.
	Err error
}

// Error implements the error interface.
func (e *TreatyError) Error() string {
	return fmt.Sprintf("%v (fault %s at byte %d)", e.Err, e.Fault, e.Offset)
}

// Unwrap returns the class error.
func (e *TreatyError) Unwrap() error { return e.Err }

// treatyErr wraps a ZXV_E_TREATY error with its fault and offset.
func treatyErr(err error, fault C.int32_t, off C.uint32_t) error {
	if err == nil || !errors.Is(err, ErrTreaty) {
		return err
	}
	return &TreatyError{Fault: TreatyFault(fault), Offset: uint32(off), Err: err}
}

// TreatyParty is one party of a treaty.
type TreatyParty struct {
	ID        string // 1..8 of A-Z 0-9 '-'
	Deposit   int64  // minor units of the asset it puts in
	Claim     int64  // what it may claim back (<= Deposit)
	WeightNum uint32 // its dividend weight is WeightNum/WeightDen
	WeightDen uint32 // denominator of the dividend weight
}

// TreatyOwe is one obligation: party From owes party To Amount (0-based
// party indices, in treaty order).
type TreatyOwe struct {
	From, To uint32 // indices of the party that owes and the party owed
	Amount   int64  // minor units of the asset
}

// Treaty is one treaty (zxv_treaty). It holds no context. It is not safe
// for concurrent mutation; this type serializes every call, so it may be
// shared. Close it when done (a finalizer is the backstop).
type Treaty struct{ h *handle }

// NewTreaty returns an empty treaty: it fails validation (TreatyFaultName)
// until a parse fills it.
func NewTreaty() (*Treaty, error) {
	var p *C.zxv_treaty
	if err := call(func() C.int32_t { return C.zxv_treaty_create(&p) }); err != nil {
		return nil, err
	}
	t := &Treaty{h: newHandle("treaty", unsafe.Pointer(p), func(q unsafe.Pointer) { C.zxv_treaty_destroy((*C.zxv_treaty)(q)) })}
	t.h.exclusive = true
	runtime.SetFinalizer(t, func(t *Treaty) { _ = t.h.close() })
	return t, nil
}

// ParseTreatySutra returns a new treaty parsed from canonical Sutra text.
// A refusal is a *TreatyError with the fault and its byte offset.
func ParseTreatySutra(text string) (*Treaty, error) {
	t, err := NewTreaty()
	if err != nil {
		return nil, err
	}
	if err := t.ParseSutra(text); err != nil {
		_ = t.Close()
		return nil, err
	}
	return t, nil
}

// ParseTreatyRecord returns a new treaty parsed from a TreatyRecordLen-byte
// copybook record.
func ParseTreatyRecord(rec []byte) (*Treaty, error) {
	t, err := NewTreaty()
	if err != nil {
		return nil, err
	}
	if err := t.ParseRecord(rec); err != nil {
		_ = t.Close()
		return nil, err
	}
	return t, nil
}

// Close frees the treaty. Later calls fail with ErrClosed.
func (t *Treaty) Close() error { return t.h.close() }

func (t *Treaty) do(f func(p *C.zxv_treaty) C.int32_t) error {
	return t.h.use(func(q unsafe.Pointer) error { return call(func() C.int32_t { return f((*C.zxv_treaty)(q)) }) })
}

func (t *Treaty) text(f func(p *C.zxv_treaty, b *C.char, c C.size_t, l *C.size_t) C.int32_t) (string, error) {
	var s string
	err := t.h.use(func(q unsafe.Pointer) error {
		var e error
		s, e = readString(func(b *C.char, c C.size_t, l *C.size_t) C.int32_t { return f((*C.zxv_treaty)(q), b, c, l) })
		return e
	})
	return s, err
}

// Clear empties the treaty again.
func (t *Treaty) Clear() error {
	return t.do(func(p *C.zxv_treaty) C.int32_t { return C.zxv_treaty_clear(p) })
}

// ParseSutra replaces the treaty with one parsed from Sutra text (at most
// TreatySutraMax bytes). On any failure the treaty is unchanged. Non-ASCII
// text is a TreatyFaultSyntax at its offset; text that is not UTF-8 is
// ZXV_E_UTF8 and longer text ZXV_E_TOO_LONG.
func (t *Treaty) ParseSutra(text string) error {
	cs, err := cstring(text)
	if err != nil {
		return err
	}
	defer freeC(cs)
	var off C.uint32_t
	var fault C.int32_t
	err = t.do(func(p *C.zxv_treaty) C.int32_t { return C.zxv_treaty_parse_sutra(p, cs, &off, &fault) })
	return treatyErr(err, fault, off)
}

// ParseRecord replaces the treaty with one parsed from a copybook record of
// exactly TreatyRecordLen bytes (another length is ZXV_E_ARG). Unused slots
// must be exactly what COBOL's INITIALIZE leaves. On any failure the treaty
// is unchanged.
func (t *Treaty) ParseRecord(rec []byte) error {
	var off C.uint32_t
	var fault C.int32_t
	err := t.do(func(p *C.zxv_treaty) C.int32_t {
		return C.zxv_treaty_parse_record(p, bytePtr(rec), C.size_t(len(rec)), &off, &fault)
	})
	return treatyErr(err, fault, off)
}

// Sutra returns the canonical Sutra text (a *TreatyError if the treaty does
// not validate, an empty one say).
func (t *Treaty) Sutra() (string, error) {
	s, err := t.text(func(p *C.zxv_treaty, b *C.char, c C.size_t, l *C.size_t) C.int32_t {
		return C.zxv_treaty_emit_sutra(p, b, c, l)
	})
	if err != nil && errors.Is(err, ErrTreaty) {
		f, _ := t.Validate()
		return "", &TreatyError{Fault: f, Err: err}
	}
	return s, err
}

// Record returns the canonical TreatyRecordLen-byte copybook record (a
// *TreatyError if the treaty does not validate).
func (t *Treaty) Record() ([]byte, error) {
	var out []byte
	err := t.h.use(func(q unsafe.Pointer) error {
		var e error
		out, e = readBytes(TreatyRecordLen, func(b *C.uint8_t, c C.size_t, l *C.size_t) C.int32_t {
			return C.zxv_treaty_emit_record((*C.zxv_treaty)(q), b, c, l)
		})
		return e
	})
	if err != nil && errors.Is(err, ErrTreaty) {
		f, _ := t.Validate()
		return nil, &TreatyError{Fault: f, Err: err}
	}
	return out, err
}

// Validate checks R1-R4 and the spelling rules. It returns
// (TreatyFaultNone, nil) for a valid treaty, else the fault and a
// *TreatyError. A parsed treaty always validates; an empty one fails with
// TreatyFaultName.
func (t *Treaty) Validate() (TreatyFault, error) {
	var fault C.int32_t
	err := t.do(func(p *C.zxv_treaty) C.int32_t { return C.zxv_treaty_validate(p, &fault) })
	if err != nil {
		return TreatyFault(fault), treatyErr(err, fault, 0)
	}
	return TreatyFaultNone, nil
}

// Equal reports whether t and o hold the same treaty (every field, every
// slot). Two empty treaties are equal.
func (t *Treaty) Equal(o *Treaty) (bool, error) {
	var eq C.int32_t
	cmp := func(a, b unsafe.Pointer) error {
		return call(func() C.int32_t {
			return C.zxv_treaty_equal((*C.zxv_treaty)(a), (*C.zxv_treaty)(b), &eq)
		})
	}
	var err error
	switch {
	case t == o:
		err = t.h.use(func(a unsafe.Pointer) error { return cmp(a, a) })
	case uintptr(unsafe.Pointer(t.h)) < uintptr(unsafe.Pointer(o.h)):
		// Lock the two handles in a fixed order so that a.Equal(b) and
		// b.Equal(a) on two goroutines cannot deadlock.
		err = t.h.use(func(a unsafe.Pointer) error {
			return o.h.use(func(b unsafe.Pointer) error { return cmp(a, b) })
		})
	default:
		err = o.h.use(func(b unsafe.Pointer) error {
			return t.h.use(func(a unsafe.Pointer) error { return cmp(a, b) })
		})
	}
	return eq != 0, err
}

// Name returns the treaty name ("" when empty).
func (t *Treaty) Name() (string, error) {
	return t.text(func(p *C.zxv_treaty, b *C.char, c C.size_t, l *C.size_t) C.int32_t {
		return C.zxv_treaty_name(p, b, c, l)
	})
}

// Asset returns the asset: three capital letters ("" when empty).
func (t *Treaty) Asset() (string, error) {
	return t.text(func(p *C.zxv_treaty, b *C.char, c C.size_t, l *C.size_t) C.int32_t {
		return C.zxv_treaty_asset(p, b, c, l)
	})
}

// Header returns the operation and the party and obligation counts.
func (t *Treaty) Header() (op TreatyOp, parties, owes uint32, err error) {
	var o C.int32_t
	var np, no C.uint32_t
	err = t.do(func(p *C.zxv_treaty) C.int32_t { return C.zxv_treaty_header(p, &o, &np, &no) })
	return TreatyOp(o), uint32(np), uint32(no), err
}

// Party returns party index (0-based, in treaty order); ErrNotFound past
// the last one.
func (t *Treaty) Party(index uint32) (TreatyParty, error) {
	var dep, clm C.int64_t
	var wn, wd C.uint32_t
	var id string
	err := t.h.use(func(q unsafe.Pointer) error {
		p := (*C.zxv_treaty)(q)
		var e error
		id, e = readString(func(b *C.char, c C.size_t, l *C.size_t) C.int32_t {
			return C.zxv_treaty_party_id(p, C.uint32_t(index), b, c, l)
		})
		if e != nil {
			return e
		}
		return call(func() C.int32_t { return C.zxv_treaty_party(p, C.uint32_t(index), &dep, &clm, &wn, &wd) })
	})
	if err != nil {
		return TreatyParty{}, err
	}
	return TreatyParty{ID: id, Deposit: int64(dep), Claim: int64(clm), WeightNum: uint32(wn), WeightDen: uint32(wd)}, nil
}

// Parties returns every party in treaty order.
func (t *Treaty) Parties() ([]TreatyParty, error) {
	_, n, _, err := t.Header()
	if err != nil {
		return nil, err
	}
	out := make([]TreatyParty, 0, n)
	for i := uint32(0); i < n; i++ {
		p, err := t.Party(i)
		if err != nil {
			return nil, err
		}
		out = append(out, p)
	}
	return out, nil
}

// Owe returns obligation index (0-based); ErrNotFound past the last one.
func (t *Treaty) Owe(index uint32) (TreatyOwe, error) {
	var fr, to C.uint32_t
	var amt C.int64_t
	err := t.do(func(p *C.zxv_treaty) C.int32_t { return C.zxv_treaty_owe(p, C.uint32_t(index), &fr, &to, &amt) })
	return TreatyOwe{From: uint32(fr), To: uint32(to), Amount: int64(amt)}, err
}

// Owes returns every obligation in treaty order.
func (t *Treaty) Owes() ([]TreatyOwe, error) {
	_, _, n, err := t.Header()
	if err != nil {
		return nil, err
	}
	out := make([]TreatyOwe, 0, n)
	for i := uint32(0); i < n; i++ {
		o, err := t.Owe(i)
		if err != nil {
			return nil, err
		}
		out = append(out, o)
	}
	return out, nil
}

// Run executes the treaty in a fresh private engine for its asset: it
// funds each deposit, nets the obligations multilaterally (at most
// parties-1 legs), and for SETL posts the legs with the 0.08889% assurance
// fee (all or nothing), pays each claim back out, and reports each party's
// weight share of what the fee put in the dividend bucket (reported, not
// paid). Nothing persists and no capability is needed; the same treaty
// always gives the same result. It takes about 500 KB of heap for the call.
//
// Errors: a *TreatyError (does not validate), ErrInsufficientFunds (a payer
// cannot cover its legs plus the fee, or a claim exceeds what is left),
// ZXV_E_RANGE (a net position leaves PIC S9(18), or an amount above the
// engine's per-line limit of 2^59).
func (t *Treaty) Run() (*TreatyResult, error) {
	var r *C.zxv_treaty_result
	err := t.do(func(p *C.zxv_treaty) C.int32_t { return C.zxv_treaty_run(p, &r) })
	if err != nil {
		if errors.Is(err, ErrTreaty) {
			f, _ := t.Validate()
			return nil, &TreatyError{Fault: f, Err: err}
		}
		return nil, err
	}
	res := &TreatyResult{h: newHandle("treaty result", unsafe.Pointer(r), func(q unsafe.Pointer) {
		C.zxv_treaty_result_destroy((*C.zxv_treaty_result)(q))
	})}
	runtime.SetFinalizer(res, func(r *TreatyResult) { _ = r.h.close() })
	return res, nil
}

// TreatyLeg is one netted leg: party From pays party To Amount (0-based
// party indices).
type TreatyLeg struct {
	From, To uint32 // indices of the paying and the receiving party
	Amount   int64  // minor units moved
}

// TreatyPartyResult is one party's line of a TreatyResult.
type TreatyPartyResult struct {
	// Net is what the party is owed minus what it owes (gross).
	Net int64
	// Balance is its deposit for NET; for SETL what it holds after the
	// legs, the fee and its claim.
	Balance int64
	// Dividend is its reported dividend share (0 for NET).
	Dividend int64
}

// TreatyResult is what running a treaty returned (zxv_treaty_result). It is
// immutable and safe for concurrent use, and independent of the treaty it
// came from.
type TreatyResult struct{ h *handle }

// Close frees the result.
func (r *TreatyResult) Close() error { return r.h.close() }

func (r *TreatyResult) do(f func(p *C.zxv_treaty_result) C.int32_t) error {
	return r.h.use(func(q unsafe.Pointer) error {
		return call(func() C.int32_t { return f((*C.zxv_treaty_result)(q)) })
	})
}

// Summary returns the party and leg counts and the total fee charged (0
// for NET).
func (r *TreatyResult) Summary() (parties, legs uint32, fees int64, err error) {
	var np, nl C.uint32_t
	var f C.int64_t
	err = r.do(func(p *C.zxv_treaty_result) C.int32_t { return C.zxv_treaty_result_summary(p, &np, &nl, &f) })
	return uint32(np), uint32(nl), int64(f), err
}

// Party returns party index's line (treaty order).
func (r *TreatyResult) Party(index uint32) (TreatyPartyResult, error) {
	var n, b, d C.int64_t
	err := r.do(func(p *C.zxv_treaty_result) C.int32_t {
		return C.zxv_treaty_result_party(p, C.uint32_t(index), &n, &b, &d)
	})
	return TreatyPartyResult{Net: int64(n), Balance: int64(b), Dividend: int64(d)}, err
}

// Parties returns every party's line.
func (r *TreatyResult) Parties() ([]TreatyPartyResult, error) {
	n, _, _, err := r.Summary()
	if err != nil {
		return nil, err
	}
	out := make([]TreatyPartyResult, 0, n)
	for i := uint32(0); i < n; i++ {
		p, err := r.Party(i)
		if err != nil {
			return nil, err
		}
		out = append(out, p)
	}
	return out, nil
}

// Leg returns netted leg index.
func (r *TreatyResult) Leg(index uint32) (TreatyLeg, error) {
	var fr, to C.uint32_t
	var a C.int64_t
	err := r.do(func(p *C.zxv_treaty_result) C.int32_t {
		return C.zxv_treaty_result_leg(p, C.uint32_t(index), &fr, &to, &a)
	})
	return TreatyLeg{From: uint32(fr), To: uint32(to), Amount: int64(a)}, err
}

// Legs returns every netted leg.
func (r *TreatyResult) Legs() ([]TreatyLeg, error) {
	_, n, _, err := r.Summary()
	if err != nil {
		return nil, err
	}
	out := make([]TreatyLeg, 0, n)
	for i := uint32(0); i < n; i++ {
		l, err := r.Leg(i)
		if err != nil {
			return nil, err
		}
		out = append(out, l)
	}
	return out, nil
}

// Record returns the TreatyResultLen-byte ZXVRSLT.cpy result record (party
// numbers in it are 1-based, as the copybook says).
func (r *TreatyResult) Record() ([]byte, error) {
	var out []byte
	err := r.h.use(func(q unsafe.Pointer) error {
		var e error
		out, e = readBytes(TreatyResultLen, func(b *C.uint8_t, c C.size_t, l *C.size_t) C.int32_t {
			return C.zxv_treaty_result_record((*C.zxv_treaty_result)(q), b, c, l)
		})
		return e
	})
	return out, err
}

// PIC S9(18) arithmetic, as COBOL does it with ON SIZE ERROR: exact on
// integers in [-TreatyPIC18Max, TreatyPIC18Max]; an operand or exact result
// outside that range, or a division by zero, is ZXV_E_RANGE.

// PicAdd returns a + b.
func PicAdd(a, b int64) (int64, error) { return pic(a, b, picAdd) }

// PicSub returns a - b.
func PicSub(a, b int64) (int64, error) { return pic(a, b, picSub) }

// PicMul returns a * b.
func PicMul(a, b int64) (int64, error) { return pic(a, b, picMul) }

// PicDiv returns a / b truncated toward zero and the remainder with the
// dividend's sign (COBOL DIVIDE ... REMAINDER): -7 / 2 = -3 rem -1.
func PicDiv(a, b int64) (q, rem int64, err error) {
	var cq, cr C.int64_t
	err = call(func() C.int32_t { return C.zxv_treaty_pic_div(C.int64_t(a), C.int64_t(b), &cq, &cr) })
	return int64(cq), int64(cr), err
}

const (
	picAdd = iota
	picSub
	picMul
)

func pic(a, b int64, op int) (int64, error) {
	var o C.int64_t
	err := call(func() C.int32_t {
		switch op {
		case picAdd:
			return C.zxv_treaty_pic_add(C.int64_t(a), C.int64_t(b), &o)
		case picSub:
			return C.zxv_treaty_pic_sub(C.int64_t(a), C.int64_t(b), &o)
		default:
			return C.zxv_treaty_pic_mul(C.int64_t(a), C.int64_t(b), &o)
		}
	})
	return int64(o), err
}
