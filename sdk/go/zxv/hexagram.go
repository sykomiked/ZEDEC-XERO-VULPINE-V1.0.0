// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

package zxv

/*
#include "zxv_api.h"
*/
import "C"

import (
	"fmt"
	"runtime"
	"unsafe"
)

// HexBase is the four-valued base of a Hexagram (bits 5-4), numbered by
// line pattern. This is NOT Truth's numbering (TRUE is 1 there and 3
// here): convert only with Hexagram.ToTruth and HexagramFromTruth.
type HexBase uint8

// The four bases.
const (
	HexBaseFalse   HexBase = C.ZXV_HEX_BASE_FALSE
	HexBaseNeither HexBase = C.ZXV_HEX_BASE_NEITHER
	HexBaseBoth    HexBase = C.ZXV_HEX_BASE_BOTH
	HexBaseTrue    HexBase = C.ZXV_HEX_BASE_TRUE
)

// HexPolarity is the polarity of a Hexagram's evidence (bits 3-2).
// HexForbidden is absorbing.
type HexPolarity uint8

// The four polarities.
const (
	HexEntropic    HexPolarity = C.ZXV_HEX_POL_ENTROPIC    // margin shrinking, or stale
	HexNeutral     HexPolarity = C.ZXV_HEX_POL_NEUTRAL     // margin unchanged
	HexForbidden   HexPolarity = C.ZXV_HEX_POL_FORBIDDEN   // rolled back, quarantined, never replayed
	HexNegentropic HexPolarity = C.ZXV_HEX_POL_NEGENTROPIC // margin growing
)

// HexPhase is the lifecycle phase of a Hexagram (bits 1-0). The cycle is
// Potential -> Emerging -> Manifest -> Dissolving -> Potential.
type HexPhase uint8

// The four phases.
const (
	HexPotential  HexPhase = C.ZXV_HEX_PHASE_POTENTIAL
	HexEmerging   HexPhase = C.ZXV_HEX_PHASE_EMERGING
	HexDissolving HexPhase = C.ZXV_HEX_PHASE_DISSOLVING
	HexManifest   HexPhase = C.ZXV_HEX_PHASE_MANIFEST
)

// HexAction is what happens next to work with a given Hexagram.
type HexAction uint8

// The five actions.
const (
	HexProceed  HexAction = C.ZXV_HEX_ACT_PROCEED
	HexWait     HexAction = C.ZXV_HEX_ACT_WAIT
	HexRefuse   HexAction = C.ZXV_HEX_ACT_REFUSE
	HexEscalate HexAction = C.ZXV_HEX_ACT_ESCALATE
	HexRollback HexAction = C.ZXV_HEX_ACT_ROLLBACK
)

var (
	hexBaseNames     = [4]string{"false", "neither", "both", "true"}
	hexPolarityNames = [4]string{"entropic", "neutral", "forbidden", "negentropic"}
	hexPhaseNames    = [4]string{"potential", "emerging", "dissolving", "manifest"}
)

// String returns "false", "neither", "both" or "true".
func (b HexBase) String() string { return enumName(hexBaseNames[:], uint8(b)) }

// String returns "entropic", "neutral", "forbidden" or "negentropic".
func (p HexPolarity) String() string { return enumName(hexPolarityNames[:], uint8(p)) }

// String returns "potential", "emerging", "dissolving" or "manifest".
func (p HexPhase) String() string { return enumName(hexPhaseNames[:], uint8(p)) }

// String returns "proceed", "wait", "refuse", "escalate" or "rollback".
func (a HexAction) String() string {
	return C.GoString(C.zxv_hex_action_name(C.int32_t(a)))
}

func enumName(names []string, v uint8) string {
	if int(v) < len(names) {
		return names[v]
	}
	return fmt.Sprintf("INVALID(%d)", v)
}

// Next returns the next phase on the cycle (ErrUsage for a value above 3).
func (p HexPhase) Next() (HexPhase, error) {
	var o C.uint8_t
	err := call(func() C.int32_t { return C.zxv_hex_phase_next(C.uint8_t(p), &o) })
	return HexPhase(o), err
}

// Hexagram is the 64-state hexagram tag of ABI 1.3 (the kernel's
// lpres_hex): an immutable value tagging one unit of work with a
// four-valued HexBase, the HexPolarity of its evidence and the HexPhase of
// its lifecycle, packed as tag = base<<4 | polarity<<2 | phase (0..63).
// The zero value is tag 0 (false/entropic/potential). Compare with ==.
//
// Every rule is computed by libzxv. There is no conversion to bool, and
// the only conversion to Truth is ToTruth.
type Hexagram struct {
	tag uint8
}

// HexStates is the number of tags.
const HexStates = C.ZXV_HEX_STATES

// NewHexagram packs three fields (ErrUsage, ZXV_E_ARG, for a field above 3).
func NewHexagram(base HexBase, polarity HexPolarity, phase HexPhase) (Hexagram, error) {
	return hex8(func(o *C.uint8_t) C.int32_t {
		return C.zxv_hex_pack(C.uint8_t(base), C.uint8_t(polarity), C.uint8_t(phase), o)
	})
}

// HexagramFromTag returns the tag with this byte value (ErrUsage above 63).
func HexagramFromTag(tag uint8) (Hexagram, error) {
	if err := call(func() C.int32_t { return C.zxv_hex_unpack(C.uint8_t(tag), nil, nil, nil) }); err != nil {
		return Hexagram{}, err
	}
	return Hexagram{tag}, nil
}

// AllHexagrams returns the 64 tags in tag order.
func AllHexagrams() []Hexagram {
	all := make([]Hexagram, HexStates)
	for i := range all {
		all[i] = Hexagram{uint8(i)}
	}
	return all
}

// HexagramFromTruth returns a tag whose base is t (zxv_hex_from_lp).
func HexagramFromTruth(t Truth, polarity HexPolarity, phase HexPhase) (Hexagram, error) {
	var b C.uint8_t
	if err := call(func() C.int32_t { return C.zxv_hex_from_lp(C.int32_t(t.Raw()), &b) }); err != nil {
		return Hexagram{}, err
	}
	return NewHexagram(HexBase(b), polarity, phase)
}

// HexPolarityFromEvidence derives a polarity from evidence counts before
// (pos0, neg0) and after (pos1, neg1): stale gives HexEntropic; otherwise
// the margin |pos-neg| growing gives HexNegentropic, shrinking HexEntropic,
// unchanged HexNeutral. Never HexForbidden.
func HexPolarityFromEvidence(pos0, neg0, pos1, neg1 uint32, stale bool) HexPolarity {
	s := C.int32_t(0)
	if stale {
		s = 1
	}
	var o C.uint8_t
	C.zxv_hex_polarity(C.uint32_t(pos0), C.uint32_t(neg0), C.uint32_t(pos1), C.uint32_t(neg1), s, &o)
	return HexPolarity(o)
}

// Tag returns the byte value, 0..63.
func (h Hexagram) Tag() uint8 { return h.tag }

// Base returns bits 5-4.
func (h Hexagram) Base() HexBase { return HexBase(h.tag >> 4) }

// Polarity returns bits 3-2.
func (h Hexagram) Polarity() HexPolarity { return HexPolarity(h.tag >> 2 & 3) }

// Phase returns bits 1-0.
func (h Hexagram) Phase() HexPhase { return HexPhase(h.tag & 3) }

// IsForbidden reports whether the polarity is HexForbidden (absorbing).
func (h Hexagram) IsForbidden() bool { return h.Polarity() == HexForbidden }

// Action is what happens next: one lookup in the kernel's 64-entry table.
func (h Hexagram) Action() HexAction {
	var a C.int32_t
	C.zxv_hex_action(C.uint8_t(h.tag), &a)
	return HexAction(a)
}

// String returns "base/polarity/phase", e.g. "true/negentropic/manifest".
func (h Hexagram) String() string { return C.GoString(C.zxv_hex_name(C.uint8_t(h.tag))) }

// GoString returns "zxv.Hexagram(true/negentropic/manifest, tag=63)".
func (h Hexagram) GoString() string { return fmt.Sprintf("zxv.Hexagram(%s, tag=%d)", h, h.tag) }

// ToTruth returns the base as a Truth: the explicit conversion.
func (h Hexagram) ToTruth() Truth {
	var t C.int32_t
	C.zxv_hex_to_lp(C.uint8_t(h.tag>>4), &t)
	return Truth{uint8(t)}
}

// Step moves the same work to phase: the same phase or the next one only
// (ErrRefused, ZXV_E_STATE, for a jump).
func (h Hexagram) Step(phase HexPhase) (Hexagram, error) {
	return hex8(func(o *C.uint8_t) C.int32_t { return C.zxv_hex_step(C.uint8_t(h.tag), C.uint8_t(phase), o) })
}

// Advance moves one step along the cycle.
func (h Hexagram) Advance() Hexagram {
	next, _ := h.Phase().Next()
	r, _ := h.Step(next)
	return r
}

// WithState sets a new base and polarity, keeping the phase. A forbidden
// tag asked for another polarity is ErrRefused (ZXV_E_STATE): forbidden is
// absorbing.
func (h Hexagram) WithState(base HexBase, polarity HexPolarity) (Hexagram, error) {
	return hex8(func(o *C.uint8_t) C.int32_t {
		return C.zxv_hex_set(C.uint8_t(h.tag), C.uint8_t(base), C.uint8_t(polarity), o)
	})
}

// Forbid returns the same tag with polarity HexForbidden.
func (h Hexagram) Forbid() Hexagram {
	var r C.uint8_t
	C.zxv_hex_forbid(C.uint8_t(h.tag), &r)
	return Hexagram{uint8(r)}
}

// And is the conjunction: four-valued AND on the base, worst polarity,
// least advanced phase.
func (h Hexagram) And(o Hexagram) Hexagram {
	var r C.uint8_t
	C.zxv_hex_and(C.uint8_t(h.tag), C.uint8_t(o.tag), &r)
	return Hexagram{uint8(r)}
}

// Or is the disjunction: four-valued OR on the base, worst polarity, least
// advanced phase.
func (h Hexagram) Or(o Hexagram) Hexagram {
	var r C.uint8_t
	C.zxv_hex_or(C.uint8_t(h.tag), C.uint8_t(o.tag), &r)
	return Hexagram{uint8(r)}
}

// Not negates the base; polarity and phase are kept.
func (h Hexagram) Not() Hexagram {
	var r C.uint8_t
	C.zxv_hex_not(C.uint8_t(h.tag), &r)
	return Hexagram{uint8(r)}
}

func hex8(f func(o *C.uint8_t) C.int32_t) (Hexagram, error) {
	var o C.uint8_t
	if err := call(func() C.int32_t { return f(&o) }); err != nil {
		return Hexagram{}, err
	}
	return Hexagram{uint8(o)}, nil
}

// QuarantineMaxRecords is the largest QuarantineLog capacity.
const QuarantineMaxRecords = C.ZXV_HEXLOG_MAX_RECORDS

// QuarantineRecord is one record of a QuarantineLog.
type QuarantineRecord struct {
	Seq       uint64
	Tag       Hexagram
	Reason    uint8
	Submitter uint32
	Subject   [32]byte
	Prev      [32]byte // the previous record's hash (zeros for the first)
	Hash      [32]byte
}

// QuarantineLog is the quarantine log of ABI 1.3 (zxv_hexlog): an
// append-only, SHA-256 hash-chained record of forbidden work, read for
// audit and never replayed. Creating and appending need CapAttest. Safe
// for concurrent use.
//
// Record k's hash is SHA-256("zxv/lpx-qlog/v1" || seq (u64 LE) || tag ||
// reason || submitter (u32 LE) || subject || prev). A full log refuses the
// append with ZXV_E_CAPACITY (ErrRefused); the caller must then halt the
// context (Context.Halt), since forbidden work it cannot record must not
// continue. Signing Head is the caller's job.
type QuarantineLog struct {
	h   *handle
	cap *Capability
}

// NewQuarantineLog creates a log of at most capacity (1..QuarantineMaxRecords)
// records in cap's context (cap needs CapAttest).
func NewQuarantineLog(cap *Capability, capacity uint32) (*QuarantineLog, error) {
	var r *C.zxv_hexlog
	if err := cap.gated(func(p *C.zxv_cap) C.int32_t { return C.zxv_hexlog_create(p, C.uint32_t(capacity), &r) }); err != nil {
		return nil, err
	}
	q := &QuarantineLog{cap: cap}
	q.h = newHandle("quarantine log", unsafe.Pointer(r), func(p unsafe.Pointer) { C.zxv_hexlog_destroy((*C.zxv_hexlog)(p)) }, cap.h)
	runtime.SetFinalizer(q, func(q *QuarantineLog) { _ = q.h.close() })
	return q, nil
}

// Close destroys the log.
func (q *QuarantineLog) Close() error { return q.h.close() }

func (q *QuarantineLog) local(f func(r *C.zxv_hexlog) C.int32_t) error {
	if err := q.cap.ctx.guard(); err != nil {
		return err
	}
	err := q.h.use(func(r unsafe.Pointer) error { return call(func() C.int32_t { return f((*C.zxv_hexlog)(r)) }) })
	return q.cap.ctx.fatal(err)
}

// Append quarantines forbidden work (tag must be forbidden, else ErrUsage)
// and returns the record's sequence number.
func (q *QuarantineLog) Append(tag Hexagram, reason uint8, submitter uint32, subject [32]byte) (uint64, error) {
	var seq C.uint64_t
	err := q.h.use(func(r unsafe.Pointer) error {
		return q.cap.gated(func(cp *C.zxv_cap) C.int32_t {
			return C.zxv_hexlog_append(cp, (*C.zxv_hexlog)(r), C.uint8_t(tag.tag), C.uint8_t(reason),
				C.uint32_t(submitter), (*C.uint8_t)(unsafe.Pointer(&subject[0])), &seq)
		})
	})
	return uint64(seq), err
}

// Count returns the number of records.
func (q *QuarantineLog) Count() (uint32, error) {
	var n C.uint32_t
	err := q.local(func(r *C.zxv_hexlog) C.int32_t { return C.zxv_hexlog_count(r, &n) })
	return uint32(n), err
}

// Head returns the chain head: the last record's hash (zeros when empty).
func (q *QuarantineLog) Head() ([32]byte, error) {
	var h [32]byte
	err := q.local(func(r *C.zxv_hexlog) C.int32_t {
		return C.zxv_hexlog_head(r, (*C.uint8_t)(unsafe.Pointer(&h[0])))
	})
	return h, err
}

// Record returns record index, oldest first.
func (q *QuarantineLog) Record(index uint32) (QuarantineRecord, error) {
	var (
		seq    C.uint64_t
		tag    C.uint8_t
		reason C.uint8_t
		who    C.uint32_t
		rec    QuarantineRecord
	)
	err := q.local(func(r *C.zxv_hexlog) C.int32_t {
		return C.zxv_hexlog_at(r, C.uint32_t(index), &seq, &tag, &reason, &who,
			(*C.uint8_t)(unsafe.Pointer(&rec.Subject[0])), (*C.uint8_t)(unsafe.Pointer(&rec.Prev[0])),
			(*C.uint8_t)(unsafe.Pointer(&rec.Hash[0])))
	})
	if err != nil {
		return QuarantineRecord{}, err
	}
	rec.Seq, rec.Tag, rec.Reason, rec.Submitter = uint64(seq), Hexagram{uint8(tag)}, uint8(reason), uint32(who)
	return rec, nil
}

// Records returns every record, oldest first.
func (q *QuarantineLog) Records() ([]QuarantineRecord, error) {
	n, err := q.Count()
	if err != nil {
		return nil, err
	}
	out := make([]QuarantineRecord, 0, n)
	for i := uint32(0); i < n; i++ {
		r, err := q.Record(i)
		if err != nil {
			return nil, err
		}
		out = append(out, r)
	}
	return out, nil
}

// Verify recomputes every link; a broken chain is ZXV_E_INVARIANT
// (ErrFatal: the context halts).
func (q *QuarantineLog) Verify() error {
	return q.local(func(r *C.zxv_hexlog) C.int32_t { return C.zxv_hexlog_verify(r) })
}
