// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

package zxv

// The paradox engine (ABI 1.5). A PARADOX here is work whose four-valued
// state is Both (the evidence points both ways) or Neither (no evidence).
// This group does not decide which side is "really" true. It reads the
// work's Hexagram, gives the paradox one of four kinds, and runs a fixed,
// logged operator for that kind:
//
//	CONSTRUCTIVE  Both, margin growing     a bounded policy update (Constructive)
//	DESTRUCTIVE   Both, margin shrinking   contain, roll back, prove (Destructive)
//	NEUTRAL       steady Both, or Neither  hold every branch (Neutral)
//	SPECIAL       new, unregistered        a permanent frontier record (Special)
//
// A registered signature overrides the trend. True and False are not
// paradoxes (ParadoxKindNone). Every operator takes the ParadoxClass that
// Classify returned. A forbidden tag means a real check found a broken
// invariant: every operator refuses it and rolls back.
//
// THE BOOK. A ParadoxBook holds the operator log, the frontier log, the
// quarantine log and the signature registry. The logs are append-only and
// SHA-256 hash-chained; their records can be read out as bytes.
//
// OUTCOMES. An operator that ran returns a ParadoxOutcome and a nil error:
// REFUSED and PROPOSAL_FOR_OWNER are decisions, not errors. An error means
// it did not run and nothing changed (except: Destructive always restores
// the state, and GraphPass returns ErrParadoxStep after logging the pass
// that reached the step limit). A full log is ZXV_E_CAPACITY: halt the
// context; work that cannot be recorded must not continue.
//
// CAPABILITIES. Creating a book, policy, hold or graph, and every operator
// and registry add, needs CapParadox. The book's operators use the
// capability the book was created with; a policy, hold or graph used with
// a book must belong to the same context (ZXV_E_ARG otherwise). Reading,
// classifying, adding branches, nodes and updates need none. Nothing here
// signs: sign the bytes yourself (MLDSA87 or Ed25519, CapCryptoSign).
//
// THREADING. A book is safe for concurrent use; calls on one policy, hold
// or graph are serialized. Close them before their capability.
//
// Honest limits: automatic changes happen only inside published bounds; an
// update outside them changes nothing and becomes a proposal for the owner.
// Policies that touch money or devices are never changed automatically:
// they need the owner, and "ownerOK" is the caller's assertion (this
// library cannot check who the owner is). Detection is only by the checks
// listed and only when you call. Destructive work is contained, never
// answered: nothing replies to or acts against a sender. This is
// contradiction-tolerant bookkeeping; it never makes both sides true.
// Integers only.

/*
#include <stdlib.h>
#include "zxv_api.h"
*/
import "C"

import (
	"encoding/binary"
	"runtime"
	"unsafe"
)

// ParadoxKind is one of the four kinds, or ParadoxKindNone.
type ParadoxKind int32

// The kinds.
const (
	ParadoxConstructive ParadoxKind = C.ZXV_PDX_KIND_CONSTRUCTIVE // catalyst
	ParadoxDestructive  ParadoxKind = C.ZXV_PDX_KIND_DESTRUCTIVE  // immune response
	ParadoxNeutral      ParadoxKind = C.ZXV_PDX_KIND_NEUTRAL      // hold
	ParadoxSpecial      ParadoxKind = C.ZXV_PDX_KIND_SPECIAL      // the frontier
	ParadoxKindNone     ParadoxKind = C.ZXV_PDX_KIND_NONE         // True or False: not a paradox
)

// String returns the library's name, e.g. "constructive" ("INVALID" if unknown).
func (k ParadoxKind) String() string { return C.GoString(C.zxv_paradox_kind_name(C.int32_t(k))) }

// ParadoxDo is what to do with a classified paradox.
type ParadoxDo int32

// The actions.
const (
	ParadoxDoNone       ParadoxDo = C.ZXV_PDX_DO_NONE       // not a paradox: use Hexagram.Action
	ParadoxDoSynthesize ParadoxDo = C.ZXV_PDX_DO_SYNTHESIZE // constructive
	ParadoxDoContain    ParadoxDo = C.ZXV_PDX_DO_CONTAIN    // destructive
	ParadoxDoHold       ParadoxDo = C.ZXV_PDX_DO_HOLD       // neutral
	ParadoxDoRecord     ParadoxDo = C.ZXV_PDX_DO_RECORD     // special (sandbox)
	ParadoxDoRollback   ParadoxDo = C.ZXV_PDX_DO_ROLLBACK   // forbidden: refuse, roll back, quarantine
)

// String returns "none", "synthesize", "contain", "hold", "record" or "rollback".
func (d ParadoxDo) String() string {
	names := []string{"none", "synthesize", "contain", "hold", "record", "rollback"}
	if d >= 0 && int(d) < len(names) {
		return names[d]
	}
	return "INVALID"
}

// ParadoxOutcome is an operator's decision.
type ParadoxOutcome int32

// The outcomes.
const (
	ParadoxApplied          ParadoxOutcome = C.ZXV_PDX_OUT_APPLIED            // constructive update applied
	ParadoxKept             ParadoxOutcome = C.ZXV_PDX_OUT_KEPT               // an applied update survived evaluation
	ParadoxProposalForOwner ParadoxOutcome = C.ZXV_PDX_OUT_PROPOSAL_FOR_OWNER // out of bounds or unapproved: no change
	ParadoxRolledBack       ParadoxOutcome = C.ZXV_PDX_OUT_ROLLED_BACK        // tension grew, or forbidden: restored
	ParadoxContained        ParadoxOutcome = C.ZXV_PDX_OUT_CONTAINED          // destructive: quarantined, rolled back
	ParadoxDeferred         ParadoxOutcome = C.ZXV_PDX_OUT_DEFERRED           // neutral: still holding every branch
	ParadoxSettled          ParadoxOutcome = C.ZXV_PDX_OUT_SETTLED            // neutral: one branch chosen
	ParadoxRecorded         ParadoxOutcome = C.ZXV_PDX_OUT_RECORDED           // special: frontier record written
	ParadoxAccepted         ParadoxOutcome = C.ZXV_PDX_OUT_ACCEPTED           // reviewed draft joined the registry
	ParadoxPending          ParadoxOutcome = C.ZXV_PDX_OUT_PENDING            // graph: more passes needed
	ParadoxResolved         ParadoxOutcome = C.ZXV_PDX_OUT_RESOLVED           // graph: every node is final
	ParadoxEscalateToOwner  ParadoxOutcome = C.ZXV_PDX_OUT_ESCALATE_TO_OWNER  // graph: step limit (ErrParadoxStep)
	ParadoxRefused          ParadoxOutcome = C.ZXV_PDX_OUT_REFUSED            // wrong kind: nothing changed
	ParadoxLogFull          ParadoxOutcome = C.ZXV_PDX_OUT_LOG_FULL           // never reported: ZXV_E_CAPACITY instead
)

// String returns the library's name, e.g. "proposal-for-owner".
func (o ParadoxOutcome) String() string {
	return C.GoString(C.zxv_paradox_outcome_name(C.int32_t(o)))
}

// Action returns the HexAction the outcome amounts to (APPLIED -> PROCEED,
// DEFERRED -> WAIT, PROPOSAL_FOR_OWNER -> ESCALATE, CONTAINED -> ROLLBACK...).
func (o ParadoxOutcome) Action() (HexAction, error) {
	var a C.int32_t
	err := call(func() C.int32_t { return C.zxv_paradox_outcome_action(C.int32_t(o), &a) })
	return HexAction(a), err
}

// ParadoxNodeState is a graph node's state.
type ParadoxNodeState int32

// Node states. Final: Clear, Contained, RolledBack, Tainted, Applied.
const (
	ParadoxNodeOpen       ParadoxNodeState = C.ZXV_PDX_NS_OPEN
	ParadoxNodeClear      ParadoxNodeState = C.ZXV_PDX_NS_CLEAR
	ParadoxNodeContained  ParadoxNodeState = C.ZXV_PDX_NS_CONTAINED
	ParadoxNodeRolledBack ParadoxNodeState = C.ZXV_PDX_NS_ROLLED_BACK
	ParadoxNodeTainted    ParadoxNodeState = C.ZXV_PDX_NS_TAINTED
	ParadoxNodeSandboxed  ParadoxNodeState = C.ZXV_PDX_NS_SANDBOXED
	ParadoxNodeWaiting    ParadoxNodeState = C.ZXV_PDX_NS_WAITING
	ParadoxNodeHeld       ParadoxNodeState = C.ZXV_PDX_NS_HELD
	ParadoxNodeBlocked    ParadoxNodeState = C.ZXV_PDX_NS_BLOCKED
	ParadoxNodeApplied    ParadoxNodeState = C.ZXV_PDX_NS_APPLIED
)

// String returns the library's name, e.g. "sandboxed".
func (s ParadoxNodeState) String() string {
	return C.GoString(C.zxv_paradox_node_state_name(C.int32_t(s)))
}

// Flags of a policy, a registry entry or a draft.
const (
	ParadoxFlagMoney  uint8 = C.ZXV_PDX_FLAG_MONEY  // touches money: the owner decides
	ParadoxFlagDevice uint8 = C.ZXV_PDX_FLAG_DEVICE // touches devices: the owner decides
)

// Trips: which checks fired (RepairProof.Trips; the quarantine reason).
const (
	ParadoxTripSteps     uint32 = C.ZXV_PDX_TRIP_STEPS
	ParadoxTripBudget    uint32 = C.ZXV_PDX_TRIP_BUDGET
	ParadoxTripCycle     uint32 = C.ZXV_PDX_TRIP_CYCLE
	ParadoxTripInvariant uint32 = C.ZXV_PDX_TRIP_INVARIANT
	ParadoxTripForbidden uint32 = C.ZXV_PDX_TRIP_FORBIDDEN
)

// ParadoxLog names one of the book's three logs.
type ParadoxLog int32

// The logs.
const (
	ParadoxLogOps        ParadoxLog = C.ZXV_PDX_LOG_OPS        // operator log
	ParadoxLogFrontier   ParadoxLog = C.ZXV_PDX_LOG_FRONTIER   // special records, never edited
	ParadoxLogQuarantine ParadoxLog = C.ZXV_PDX_LOG_QUARANTINE // contained work
)

// Paradox sizes and limits.
const (
	ParadoxRecBytes     = int(C.ZXV_PDX_REC_BYTES)       // 144: an operator or frontier record
	ParadoxQRecBytes    = int(C.ZXV_PDX_QREC_BYTES)      // 110: a quarantine record
	ParadoxRepairBytes  = int(C.ZXV_PDX_REPAIR_BYTES)    // 160: a RepairProof
	ParadoxDraftBytes   = int(C.ZXV_PDX_DRAFT_BYTES)     // 71: a draft classification
	ParadoxLogMax       = uint32(C.ZXV_PDX_LOG_MAX)      // records per log
	ParadoxRegistryMax  = uint32(C.ZXV_PDX_REGISTRY_MAX) // registry entries
	ParadoxPolicyMax    = int(C.ZXV_PDX_POLICY_MAX)      // parameters per policy
	ParadoxHoldMax      = uint32(C.ZXV_PDX_HOLD_MAX)     // branches per hold
	ParadoxGraphMax     = int(C.ZXV_PDX_GRAPH_MAX)       // nodes per graph
	ParadoxMaxDeps      = int(C.ZXV_PDX_MAX_DEPS)        // dependencies per node
	ParadoxUpdatesMax   = int(C.ZXV_PDX_UPDATES_MAX)     // queued node updates per pass
	ParadoxTraceMax     = int(C.ZXV_PDX_TRACE_MAX)       // fingerprints per detection
	ParadoxSrcSeed      = ^uint64(0)                     // registry entry not learned from the frontier
	ParadoxNoBranch     = int32(-1)                      // no settled / decisive branch
	ParadoxNoFrontier   = ^uint32(0)                     // Special did not record
	paradoxProposalHead = 12
)

// ParadoxProposalBytes is the size of a policy proposal of n values.
func ParadoxProposalBytes(n int) int { return paradoxProposalHead + 32*n }

// ParadoxClass is a classified pair: the kind and the tag carrying the
// kind's polarity. Every operator takes the pair Classify returned; a pair
// that breaks the class rules is ZXV_E_ARG.
type ParadoxClass struct {
	Kind ParadoxKind // the kind
	Tag  Hexagram    // the tag, carrying the kind's polarity
}

// ParadoxSignature returns the signature of an anomaly:
// SHA-256("zxv/pdx-sig/v1" || data), from a canonical description you build.
func ParadoxSignature(data []byte) ([32]byte, error) {
	var s [32]byte
	err := call(func() C.int32_t { return C.zxv_paradox_signature(bytePtr(data), C.size_t(len(data)), ptr32(&s)) })
	return s, err
}

// ParadoxKindOfTag returns the kind a tag reads as on its own (a forbidden
// tag reads as ParadoxKindNone: forbidden is not a kind).
func ParadoxKindOfTag(tag Hexagram) (ParadoxKind, error) {
	var k C.int32_t
	err := call(func() C.int32_t { return C.zxv_paradox_kind_of_tag(C.uint8_t(tag.tag), &k) })
	return ParadoxKind(k), err
}

// Do returns what to do with the class. A forbidden tag gives
// ParadoxDoRollback whatever the kind. ZXV_E_ARG for an invalid pair.
func (c ParadoxClass) Do() (ParadoxDo, error) {
	var d C.int32_t
	err := call(func() C.int32_t { return C.zxv_paradox_action(C.int32_t(c.Kind), C.uint8_t(c.Tag.tag), &d) })
	return ParadoxDo(d), err
}

// ParadoxEvidence is a trend: the evidence counts before and after, and
// whether the newest evidence is past your freshness limit.
type ParadoxEvidence struct {
	Pos0, Neg0, Pos1, Neg1 uint32 // evidence counts before (for, against) and after (for, against)
	Stale                  bool   // the newest evidence is past your freshness limit
}

// ParadoxClassification is what Classify reports.
type ParadoxClassification struct {
	ParadoxClass
	// RegIndex is the registry entry that decided it, or -1.
	RegIndex int32
}

func paradoxClassify(book unsafe.Pointer, tag Hexagram, ev *ParadoxEvidence, sig *[32]byte) (ParadoxClassification, error) {
	var evp *C.uint32_t
	var e [4]C.uint32_t
	var stale C.int32_t
	if ev != nil {
		e = [4]C.uint32_t{C.uint32_t(ev.Pos0), C.uint32_t(ev.Neg0), C.uint32_t(ev.Pos1), C.uint32_t(ev.Neg1)}
		evp = &e[0]
		stale = boolInt(ev.Stale)
	}
	var sp *C.uint8_t
	var s [32]byte
	if sig != nil {
		s = *sig
		sp = ptr32(&s)
	}
	var k, ri C.int32_t
	var t C.uint8_t
	err := call(func() C.int32_t {
		return C.zxv_paradox_classify((*C.zxv_paradox)(book), C.uint8_t(tag.tag), evp, stale, sp, &k, &t, &ri)
	})
	if err != nil {
		return ParadoxClassification{}, err
	}
	return ParadoxClassification{ParadoxClass{ParadoxKind(k), Hexagram{uint8(t)}}, int32(ri)}, nil
}

// ParadoxClassify classifies a tag without a registry. ev may be nil (no
// trend: polarity neutral); sig is ignored without a book.
func ParadoxClassify(tag Hexagram, ev *ParadoxEvidence) (ParadoxClassification, error) {
	return paradoxClassify(nil, tag, ev, nil)
}

// RepairProof is the 160-byte proof Destructive writes: which checks
// fired, the state hashes before and after rollback, the signature to share
// with peers and a binding hash. It is a value type. Sign it yourself.
type RepairProof [160]byte

// Verify recomputes the binding hash (integrity only, not a signature).
// ErrVerificationFailed when it does not match.
func (p *RepairProof) Verify() error {
	q := *p
	return call(func() C.int32_t { return C.zxv_paradox_repair_verify((*C.uint8_t)(unsafe.Pointer(&q[0]))) })
}

// Trips returns which checks fired (ParadoxTrip* bits).
func (p *RepairProof) Trips() uint32 { return binary.LittleEndian.Uint32(p[0:4]) }

// Steps returns the step count reported to Destructive.
func (p *RepairProof) Steps() uint32 { return binary.LittleEndian.Uint32(p[4:8]) }

// Spent returns the budget spent reported to Destructive.
func (p *RepairProof) Spent() uint32 { return binary.LittleEndian.Uint32(p[8:12]) }

// TraceLen returns how many state fingerprints were checked.
func (p *RepairProof) TraceLen() uint32 { return binary.LittleEndian.Uint32(p[12:16]) }

// Submitter returns the submitter id.
func (p *RepairProof) Submitter() uint32 { return binary.LittleEndian.Uint32(p[16:20]) }

// StateLen returns the length of the restored state.
func (p *RepairProof) StateLen() uint32 { return binary.LittleEndian.Uint32(p[20:24]) }

// StateFound returns the hash of the state as found.
func (p *RepairProof) StateFound() (h [32]byte) { copy(h[:], p[24:56]); return }

// StateRestored returns the hash of the state after rollback.
func (p *RepairProof) StateRestored() (h [32]byte) { copy(h[:], p[56:88]); return }

// Signature returns the trap's signature to share with peers.
func (p *RepairProof) Signature() (h [32]byte) { copy(h[:], p[88:120]); return }

// Hash returns the proof's binding hash.
func (p *RepairProof) Hash() (h [32]byte) { copy(h[:], p[120:152]); return }

// QuarantineIndex returns the index of the quarantine record.
func (p *RepairProof) QuarantineIndex() uint64 { return binary.LittleEndian.Uint64(p[152:160]) }

// ParadoxDraft is a 71-byte draft classification of a frontier record, for
// review. It is a value type.
type ParadoxDraft [71]byte

// Rec returns the frontier record index.
func (d *ParadoxDraft) Rec() uint32 { return binary.LittleEndian.Uint32(d[0:4]) }

// RecordHash returns the frontier record's hash.
func (d *ParadoxDraft) RecordHash() (h [32]byte) { copy(h[:], d[4:36]); return }

// Signature returns the frontier record's signature.
func (d *ParadoxDraft) Signature() (h [32]byte) { copy(h[:], d[36:68]); return }

// Kind returns the proposed kind.
func (d *ParadoxDraft) Kind() ParadoxKind { return ParadoxKind(d[68]) }

// Reason returns the caller's reason code.
func (d *ParadoxDraft) Reason() uint8 { return d[69] }

// Flags returns the ParadoxFlag* bits.
func (d *ParadoxDraft) Flags() uint8 { return d[70] }

// ParadoxBook holds the three logs and the signature registry. It belongs
// to the context of the capability that created it, uses that capability
// for every operator, and fails closed with the context. Safe for
// concurrent use. Close it before its capability.
type ParadoxBook struct{ ctxHandle }

// NewParadoxBook creates a book with three empty logs of logCapacity
// records each (1..ParadoxLogMax) and a registry of registryCapacity
// entries (1..ParadoxRegistryMax). cap needs CapParadox.
func NewParadoxBook(cap *Capability, logCapacity, registryCapacity uint32) (*ParadoxBook, error) {
	var r *C.zxv_paradox
	if err := cap.gated(func(cp *C.zxv_cap) C.int32_t {
		return C.zxv_paradox_create(cp, C.uint32_t(logCapacity), C.uint32_t(registryCapacity), &r)
	}); err != nil {
		return nil, err
	}
	b := &ParadoxBook{newCtxHandle("paradox book", unsafe.Pointer(r), func(p unsafe.Pointer) { C.zxv_paradox_destroy((*C.zxv_paradox)(p)) }, cap, false)}
	runtime.SetFinalizer(b, func(b *ParadoxBook) { _ = b.h.close() })
	return b, nil
}

// Close frees the book. Closing twice is a no-op.
func (b *ParadoxBook) Close() error { return b.h.close() }

func bk(p unsafe.Pointer) *C.zxv_paradox { return (*C.zxv_paradox)(p) }

// Classify classifies a tag with this book's registry. ev may be nil (no
// trend); sig may be nil (no signature). A registered signature overrides
// the trend for Both and Neither. Reads only.
func (b *ParadoxBook) Classify(tag Hexagram, ev *ParadoxEvidence, sig *[32]byte) (ParadoxClassification, error) {
	var out ParadoxClassification
	err := b.localSeq(func(p unsafe.Pointer) error {
		var e error
		out, e = paradoxClassify(p, tag, ev, sig)
		return e
	})
	return out, err
}

// RegistryAdd adds a reviewed signature as kind constructive, destructive
// or neutral with your reason code and ParadoxFlag* flags, and returns its
// index. An entry flagged money or device needs ownerOK, else
// ErrParadoxOwner. ZXV_E_DUPLICATE: already registered.
func (b *ParadoxBook) RegistryAdd(sig [32]byte, kind ParadoxKind, reason, flags uint8, ownerOK bool) (uint32, error) {
	var i C.uint32_t
	err := b.gated(func(cp *C.zxv_cap, p unsafe.Pointer) C.int32_t {
		return C.zxv_paradox_registry_add(cp, bk(p), ptr32(&sig), C.int32_t(kind), C.uint8_t(reason), C.uint8_t(flags), boolInt(ownerOK), &i)
	})
	return uint32(i), err
}

// RegistryFind returns the index of a signature. ErrNotFound if absent.
func (b *ParadoxBook) RegistryFind(sig [32]byte) (uint32, error) {
	var i C.uint32_t
	err := b.local(func(p unsafe.Pointer) C.int32_t { return C.zxv_paradox_registry_find(bk(p), ptr32(&sig), &i) })
	return uint32(i), err
}

// RegistryCount returns the number of registry entries.
func (b *ParadoxBook) RegistryCount() (uint32, error) {
	var n C.uint32_t
	err := b.local(func(p unsafe.Pointer) C.int32_t { return C.zxv_paradox_registry_count(bk(p), &n) })
	return uint32(n), err
}

// ParadoxRegistryEntry is one registry entry.
type ParadoxRegistryEntry struct {
	Sig    [32]byte    // the signature
	Kind   ParadoxKind // its kind
	Reason uint8       // the caller's reason code
	Flags  uint8       // ParadoxFlag* bits
	// Src is the frontier record it was learned from, or ParadoxSrcSeed.
	Src uint64
}

// RegistryAt returns entry index. ErrNotFound past the last.
func (b *ParadoxBook) RegistryAt(index uint32) (ParadoxRegistryEntry, error) {
	var e ParadoxRegistryEntry
	var k C.int32_t
	var r, f C.uint8_t
	var s C.uint64_t
	err := b.local(func(p unsafe.Pointer) C.int32_t {
		return C.zxv_paradox_registry_at(bk(p), C.uint32_t(index), ptr32(&e.Sig), &k, &r, &f, &s)
	})
	if err != nil {
		return ParadoxRegistryEntry{}, err
	}
	e.Kind, e.Reason, e.Flags, e.Src = ParadoxKind(k), uint8(r), uint8(f), uint64(s)
	return e, nil
}

// Registry returns every entry, in index order.
func (b *ParadoxBook) Registry() ([]ParadoxRegistryEntry, error) {
	n, err := b.RegistryCount()
	if err != nil {
		return nil, err
	}
	out := make([]ParadoxRegistryEntry, 0, n)
	for i := uint32(0); i < n; i++ {
		e, err := b.RegistryAt(i)
		if err != nil {
			return nil, err
		}
		out = append(out, e)
	}
	return out, nil
}

// LogCount returns the record count of a log.
func (b *ParadoxBook) LogCount(which ParadoxLog) (uint32, error) {
	var n C.uint32_t
	err := b.local(func(p unsafe.Pointer) C.int32_t { return C.zxv_paradox_log_count(bk(p), C.int32_t(which), &n) })
	return uint32(n), err
}

// LogHead returns a log's chain head (32 zeros when empty).
func (b *ParadoxBook) LogHead(which ParadoxLog) ([32]byte, error) {
	var h [32]byte
	err := b.local(func(p unsafe.Pointer) C.int32_t { return C.zxv_paradox_log_head(bk(p), C.int32_t(which), ptr32(&h)) })
	return h, err
}

// LogRecord returns record index of a log as bytes (ParadoxRecBytes, or
// ParadoxQRecBytes for the quarantine log; layouts in zxv_api.h).
// ErrNotFound past the last record.
func (b *ParadoxBook) LogRecord(which ParadoxLog, index uint32) ([]byte, error) {
	var out []byte
	err := b.localSeq(func(p unsafe.Pointer) error {
		var e error
		out, e = readBytes(ParadoxRecBytes, func(buf *C.uint8_t, cp C.size_t, n *C.size_t) C.int32_t {
			return C.zxv_paradox_log_record(bk(p), C.int32_t(which), C.uint32_t(index), buf, cp, n)
		})
		return e
	})
	return out, err
}

// LogRecords returns every record of a log, in order.
func (b *ParadoxBook) LogRecords(which ParadoxLog) ([][]byte, error) {
	n, err := b.LogCount(which)
	if err != nil {
		return nil, err
	}
	out := make([][]byte, 0, n)
	for i := uint32(0); i < n; i++ {
		r, err := b.LogRecord(which, i)
		if err != nil {
			return nil, err
		}
		out = append(out, r)
	}
	return out, nil
}

// LogVerify recomputes every link of a log. ZXV_E_INVARIANT if any record no
// longer matches; that poisons the context (fail closed).
func (b *ParadoxBook) LogVerify(which ParadoxLog) error {
	return b.local(func(p unsafe.Pointer) C.int32_t { return C.zxv_paradox_log_verify(bk(p), C.int32_t(which)) })
}

// ===== constructive ========================================================

// ParadoxPolicy is a set of n bounded parameters (swarm parameters such as
// budget shares, routing weights or escrow timing; never balances). The
// bounds never change. A policy flagged money or device is never changed
// automatically. It belongs to its capability's context; calls on it are
// serialized. Close it before its capability.
type ParadoxPolicy struct {
	ctxHandle
	n int
}

// NewParadoxPolicy creates a policy with bounds lo[i] <= hi[i] and initial
// values inside them (1..ParadoxPolicyMax parameters, all three slices the
// same length). flags: ParadoxFlag* bits. cap needs CapParadox. ZXV_E_RANGE
// for lo > hi or an initial value outside its bounds.
func NewParadoxPolicy(cap *Capability, lo, hi, initial []int64, flags uint8) (*ParadoxPolicy, error) {
	n := len(lo)
	if n == 0 || len(hi) != n || len(initial) != n {
		return nil, newError(StatusArg, "lo, hi and initial must have the same nonzero length")
	}
	var r *C.zxv_paradox_policy
	if err := cap.gated(func(cp *C.zxv_cap) C.int32_t {
		return C.zxv_paradox_policy_create(cp, C.uint32_t(n), (*C.int64_t)(unsafe.Pointer(&lo[0])), (*C.int64_t)(unsafe.Pointer(&hi[0])),
			(*C.int64_t)(unsafe.Pointer(&initial[0])), C.uint32_t(flags), &r)
	}); err != nil {
		return nil, err
	}
	pl := &ParadoxPolicy{newCtxHandle("paradox policy", unsafe.Pointer(r), func(p unsafe.Pointer) {
		C.zxv_paradox_policy_destroy((*C.zxv_paradox_policy)(p))
	}, cap, true), n}
	runtime.SetFinalizer(pl, func(pl *ParadoxPolicy) { _ = pl.h.close() })
	return pl, nil
}

// Close frees the policy. Closing twice is a no-op.
func (pl *ParadoxPolicy) Close() error { return pl.h.close() }

// Len returns the number of parameters.
func (pl *ParadoxPolicy) Len() int { return pl.n }

func i64p(v []int64) *C.int64_t {
	if len(v) == 0 {
		return nil
	}
	return (*C.int64_t)(unsafe.Pointer(&v[0]))
}

// ParadoxPolicyValues is a policy's live state.
type ParadoxPolicyValues struct {
	Values    []int64 // the live values
	Prev      []int64 // the values before the last applied update
	Armed     bool    // an applied update awaits evaluation
	TensionAt uint32  // the tension it was applied at
}

// Values returns the live values and the update state.
func (pl *ParadoxPolicy) Values() (ParadoxPolicyValues, error) {
	v := ParadoxPolicyValues{Values: make([]int64, pl.n), Prev: make([]int64, pl.n)}
	var armed C.int32_t
	var ta C.uint32_t
	err := pl.local(func(p unsafe.Pointer) C.int32_t {
		return C.zxv_paradox_policy_values((*C.zxv_paradox_policy)(p), i64p(v.Values), i64p(v.Prev), C.uint32_t(pl.n), &armed, &ta)
	})
	if err != nil {
		return ParadoxPolicyValues{}, err
	}
	v.Armed, v.TensionAt = armed != 0, uint32(ta)
	return v, nil
}

// Bounds returns the published bounds and the flags.
func (pl *ParadoxPolicy) Bounds() (lo, hi []int64, flags uint8, err error) {
	lo, hi = make([]int64, pl.n), make([]int64, pl.n)
	var f C.uint32_t
	err = pl.local(func(p unsafe.Pointer) C.int32_t {
		return C.zxv_paradox_policy_bounds((*C.zxv_paradox_policy)(p), i64p(lo), i64p(hi), C.uint32_t(pl.n), &f)
	})
	if err != nil {
		return nil, nil, 0, err
	}
	return lo, hi, uint8(f), nil
}

// Proposal returns a proposed update as bytes for the owner or for signing
// (ParadoxProposalBytes(n): n, how many values are out of bounds, flags,
// then lo, hi, current, proposed per value). Reads only.
func (pl *ParadoxPolicy) Proposal(synth []int64) ([]byte, error) {
	if len(synth) != pl.n {
		return nil, newError(StatusArg, "synth must have one value per parameter")
	}
	var out []byte
	err := pl.localSeq(func(p unsafe.Pointer) error {
		var e error
		out, e = readBytes(ParadoxProposalBytes(pl.n), func(buf *C.uint8_t, cp C.size_t, n *C.size_t) C.int32_t {
			return C.zxv_paradox_policy_proposal((*C.zxv_paradox_policy)(p), i64p(synth), C.uint32_t(pl.n), buf, cp, n)
		})
		return e
	})
	return out, err
}

// Constructive runs one evaluation of policy and then, if synth is not nil,
// one proposed update (one value per parameter). tension is your integer
// measure of the paradox (higher is worse). Outcomes: ParadoxApplied,
// ParadoxKept, ParadoxProposalForOwner (flagged policy, or a value outside
// its bounds: nothing changes), ParadoxRolledBack (tension grew, or a
// forbidden tag), ParadoxRefused (kind not constructive).
func (b *ParadoxBook) Constructive(policy *ParadoxPolicy, c ParadoxClass, synth []int64, tension uint32) (ParadoxOutcome, error) {
	if err := sameContext(b.cap, policy.cap, "policy"); err != nil {
		return 0, err
	}
	n := 0
	if synth != nil {
		n = len(synth)
	}
	var o C.int32_t
	err := policy.h.use(func(pp unsafe.Pointer) error {
		return b.gated(func(cp *C.zxv_cap, p unsafe.Pointer) C.int32_t {
			return C.zxv_paradox_constructive(cp, bk(p), (*C.zxv_paradox_policy)(pp), C.int32_t(c.Kind), C.uint8_t(c.Tag.tag),
				i64p(synth), C.uint32_t(n), C.uint32_t(tension), &o)
		})
	})
	return ParadoxOutcome(o), err
}

// ===== destructive =========================================================

// ParadoxContainment is the input to Destructive.
type ParadoxContainment struct {
	Steps, StepLimit uint32   // trips when Steps >= StepLimit (0: no limit)
	Spent, Budget    uint32   // trips when Spent >= Budget (0: no budget)
	Trace            []uint64 // state fingerprints in visit order; a repeat trips
	InvariantOK      bool     // false trips: your invariant check failed
	// State is restored IN PLACE from Checkpoint (same length) when the
	// work is contained.
	State      []byte
	Checkpoint []byte // the known-good state
	Pattern    []byte // the canonical description of the trap (for its signature)
	Submitter  uint32 // who submitted the work (recorded, never contacted)
}

// Destructive detects and contains. When the kind is destructive or any
// check fires, the work is quarantined, State is restored from Checkpoint,
// and the outcome is ParadoxContained with a RepairProof. Otherwise the
// outcome is ParadoxRefused, nothing changes and the proof is zero. It has
// no way to reply to or act against the submitter.
func (b *ParadoxBook) Destructive(c ParadoxClass, in ParadoxContainment) (ParadoxOutcome, RepairProof, error) {
	var proof RepairProof
	if len(in.State) != len(in.Checkpoint) {
		return 0, proof, newError(StatusArg, "state and checkpoint must have the same length")
	}
	var tp *C.uint64_t
	if len(in.Trace) > 0 {
		tp = (*C.uint64_t)(unsafe.Pointer(&in.Trace[0]))
	}
	var o C.int32_t
	err := b.gated(func(cp *C.zxv_cap, p unsafe.Pointer) C.int32_t {
		return C.zxv_paradox_destructive(cp, bk(p), C.int32_t(c.Kind), C.uint8_t(c.Tag.tag), C.uint32_t(in.Steps), C.uint32_t(in.StepLimit),
			C.uint32_t(in.Spent), C.uint32_t(in.Budget), tp, C.uint32_t(len(in.Trace)), boolInt(in.InvariantOK),
			bytePtr(in.State), bytePtr(in.Checkpoint), C.uint32_t(len(in.State)), bytePtr(in.Pattern), C.uint32_t(len(in.Pattern)),
			C.uint32_t(in.Submitter), &o, (*C.uint8_t)(unsafe.Pointer(&proof[0])))
	})
	return ParadoxOutcome(o), proof, err
}

// ===== neutral =============================================================

// ParadoxHold keeps up to capacity candidate branches. A branch settles
// when its net evidence leads every other by at least decisive, or alone
// reaches it, or the owner picks it. It belongs to its capability's
// context; calls on it are serialized. Close it before its capability.
type ParadoxHold struct{ ctxHandle }

// NewParadoxHold creates a hold of capacity branches (1..ParadoxHoldMax);
// decisive 0 means 1. cap needs CapParadox.
func NewParadoxHold(cap *Capability, capacity, decisive uint32) (*ParadoxHold, error) {
	var r *C.zxv_paradox_hold
	if err := cap.gated(func(cp *C.zxv_cap) C.int32_t {
		return C.zxv_paradox_hold_create(cp, C.uint32_t(capacity), C.uint32_t(decisive), &r)
	}); err != nil {
		return nil, err
	}
	h := &ParadoxHold{newCtxHandle("paradox hold", unsafe.Pointer(r), func(p unsafe.Pointer) { C.zxv_paradox_hold_destroy((*C.zxv_paradox_hold)(p)) }, cap, true)}
	runtime.SetFinalizer(h, func(h *ParadoxHold) { _ = h.h.close() })
	return h, nil
}

// Close frees the hold. Closing twice is a no-op.
func (h *ParadoxHold) Close() error { return h.h.close() }

func hd(p unsafe.Pointer) *C.zxv_paradox_hold { return (*C.zxv_paradox_hold)(p) }

// Add adds a candidate branch (a 32-byte digest); the same id again is the
// same candidate. ZXV_E_STATE once settled; ZXV_E_CAPACITY when full.
func (h *ParadoxHold) Add(id [32]byte) error {
	return h.local(func(p unsafe.Pointer) C.int32_t { return C.zxv_paradox_hold_add(hd(p), ptr32(&id)) })
}

// Evidence records one piece of evidence for (positive) or against a
// candidate. ErrNotFound: unknown id. ZXV_E_STATE once settled.
func (h *ParadoxHold) Evidence(id [32]byte, positive bool) error {
	return h.local(func(p unsafe.Pointer) C.int32_t {
		return C.zxv_paradox_hold_evidence(hd(p), ptr32(&id), boolInt(positive))
	})
}

// ParadoxHoldInfo is a hold's state.
type ParadoxHoldInfo struct {
	Count    uint32 // branches
	Settled  int32  // the settled branch, or ParadoxNoBranch while deferred
	Decisive int32  // the branch with decisive evidence now, or ParadoxNoBranch
}

// Info returns the hold's state.
func (h *ParadoxHold) Info() (ParadoxHoldInfo, error) {
	var n C.uint32_t
	var s, d C.int32_t
	err := h.local(func(p unsafe.Pointer) C.int32_t { return C.zxv_paradox_hold_info(hd(p), &n, &s, &d) })
	return ParadoxHoldInfo{uint32(n), int32(s), int32(d)}, err
}

// ParadoxBranch is one candidate and its evidence.
type ParadoxBranch struct {
	ID       [32]byte // the candidate's digest
	Pos, Neg uint32   // evidence for and against
}

// Branch returns branch index.
func (h *ParadoxHold) Branch(index uint32) (ParadoxBranch, error) {
	var b ParadoxBranch
	var po, ne C.uint32_t
	err := h.local(func(p unsafe.Pointer) C.int32_t {
		return C.zxv_paradox_hold_branch(hd(p), C.uint32_t(index), ptr32(&b.ID), &po, &ne)
	})
	b.Pos, b.Neg = uint32(po), uint32(ne)
	return b, err
}

// Branches returns every branch, in order.
func (h *ParadoxHold) Branches() ([]ParadoxBranch, error) {
	info, err := h.Info()
	if err != nil {
		return nil, err
	}
	out := make([]ParadoxBranch, 0, info.Count)
	for i := uint32(0); i < info.Count; i++ {
		b, err := h.Branch(i)
		if err != nil {
			return nil, err
		}
		out = append(out, b)
	}
	return out, nil
}

// Neutral settles hold on decisive evidence or on ownerPick (a branch
// index, or ParadoxNoBranch for none); otherwise keeps every branch.
// Outcomes: ParadoxSettled, ParadoxDeferred, ParadoxRolledBack (forbidden:
// nothing settles), ParadoxRefused (kind not neutral). settled is the
// settled branch or ParadoxNoBranch.
func (b *ParadoxBook) Neutral(hold *ParadoxHold, c ParadoxClass, ownerPick int32) (outcome ParadoxOutcome, settled int32, err error) {
	if err := sameContext(b.cap, hold.cap, "hold"); err != nil {
		return 0, ParadoxNoBranch, err
	}
	var o C.int32_t
	s := C.int32_t(ParadoxNoBranch)
	err = hold.h.use(func(hp unsafe.Pointer) error {
		return b.gated(func(cp *C.zxv_cap, p unsafe.Pointer) C.int32_t {
			return C.zxv_paradox_neutral(cp, bk(p), hd(hp), C.int32_t(c.Kind), C.uint8_t(c.Tag.tag), C.int32_t(ownerPick), &o, &s)
		})
	})
	return ParadoxOutcome(o), int32(s), err
}

// ===== special =============================================================

// Special records an unclassified paradox: a permanent frontier record
// (sig, and subject = the work's digest) plus an operator record. Outcomes:
// ParadoxRecorded (rec = the frontier index), ParadoxRefused (kind not
// special) or ParadoxRolledBack (forbidden), with rec = ParadoxNoFrontier.
// The work stays sandboxed (not run) until a reviewed classification exists.
func (b *ParadoxBook) Special(c ParadoxClass, sig, subject [32]byte) (outcome ParadoxOutcome, rec uint32, err error) {
	var o C.int32_t
	r := C.uint32_t(ParadoxNoFrontier)
	err = b.gated(func(cp *C.zxv_cap, p unsafe.Pointer) C.int32_t {
		return C.zxv_paradox_special(cp, bk(p), C.int32_t(c.Kind), C.uint8_t(c.Tag.tag), ptr32(&sig), ptr32(&subject), &o, &r)
	})
	return ParadoxOutcome(o), uint32(r), err
}

// Draft drafts a classification of frontier record rec for review: kind
// constructive, destructive or neutral, your reason code and ParadoxFlag*
// flags. ErrNotFound: no such record.
func (b *ParadoxBook) Draft(rec uint32, kind ParadoxKind, reason, flags uint8) (ParadoxDraft, error) {
	var d ParadoxDraft
	err := b.gated(func(cp *C.zxv_cap, p unsafe.Pointer) C.int32_t {
		return C.zxv_paradox_draft(cp, bk(p), C.uint32_t(rec), C.int32_t(kind), C.uint8_t(reason), C.uint8_t(flags),
			(*C.uint8_t)(unsafe.Pointer(&d[0])))
	})
	return d, err
}

// Review reviews a draft (ours, or one a peer sent for the same frontier
// record). A money or device draft needs ownerOK; others ownerOK or
// consensusOK (your quorum result). Outcomes: ParadoxAccepted (a new
// registry entry), ParadoxProposalForOwner (not approved), ParadoxRefused
// (already registered, or the registry is full). ErrVerificationFailed: the draft does
// not match the record.
func (b *ParadoxBook) Review(d ParadoxDraft, ownerOK, consensusOK bool) (ParadoxOutcome, error) {
	var o C.int32_t
	err := b.gated(func(cp *C.zxv_cap, p unsafe.Pointer) C.int32_t {
		return C.zxv_paradox_review(cp, bk(p), (*C.uint8_t)(unsafe.Pointer(&d[0])), boolInt(ownerOK), boolInt(consensusOK), &o)
	})
	return ParadoxOutcome(o), err
}

// ===== complex paradoxes: a dependency graph ===============================

// ParadoxGraph is a dependency graph of up to ParadoxGraphMax nodes that
// escalates to the owner after stepLimit passes without resolving. It
// belongs to its capability's context; calls on it are serialized. Close
// it before its capability.
type ParadoxGraph struct{ ctxHandle }

// NewParadoxGraph creates a graph (stepLimit >= 1). cap needs CapParadox.
func NewParadoxGraph(cap *Capability, stepLimit uint32) (*ParadoxGraph, error) {
	var r *C.zxv_paradox_graph
	if err := cap.gated(func(cp *C.zxv_cap) C.int32_t { return C.zxv_paradox_graph_create(cp, C.uint32_t(stepLimit), &r) }); err != nil {
		return nil, err
	}
	g := &ParadoxGraph{newCtxHandle("paradox graph", unsafe.Pointer(r), func(p unsafe.Pointer) { C.zxv_paradox_graph_destroy((*C.zxv_paradox_graph)(p)) }, cap, true)}
	runtime.SetFinalizer(g, func(g *ParadoxGraph) { _ = g.h.close() })
	return g, nil
}

// Close frees the graph. Closing twice is a no-op.
func (g *ParadoxGraph) Close() error { return g.h.close() }

func gr(p unsafe.Pointer) *C.zxv_paradox_graph { return (*C.zxv_paradox_graph)(p) }

// AddNode adds a node before the first pass: id = the hash of its signed
// input (the sort key), a classified pair, tension magnitude mag and
// direction dir (-1, 0 or +1). ZXV_E_DUPLICATE, ZXV_E_CAPACITY, ZXV_E_STATE (after
// the first pass).
func (g *ParadoxGraph) AddNode(id [32]byte, c ParadoxClass, mag uint32, dir int32) error {
	return g.local(func(p unsafe.Pointer) C.int32_t {
		return C.zxv_paradox_graph_add_node(gr(p), ptr32(&id), C.int32_t(c.Kind), C.uint8_t(c.Tag.tag), C.uint32_t(mag), C.int32_t(dir))
	})
}

// AddDep makes node id depend on node dep (which may be added later).
// ErrNotFound: no node id. ZXV_E_CAPACITY: more than ParadoxMaxDeps.
func (g *ParadoxGraph) AddDep(id, dep [32]byte) error {
	return g.local(func(p unsafe.Pointer) C.int32_t { return C.zxv_paradox_graph_add_dep(gr(p), ptr32(&id), ptr32(&dep)) })
}

// Update queues new evidence for a node, applied at the start of the next
// pass ("worst wins" within a batch, so order never matters).
func (g *ParadoxGraph) Update(id [32]byte, c ParadoxClass, mag uint32, dir int32) error {
	return g.local(func(p unsafe.Pointer) C.int32_t {
		return C.zxv_paradox_graph_update(gr(p), ptr32(&id), C.int32_t(c.Kind), C.uint8_t(c.Tag.tag), C.uint32_t(mag), C.int32_t(dir))
	})
}

// State returns a node's state (ParadoxNodeOpen before the first pass).
func (g *ParadoxGraph) State(id [32]byte) (ParadoxNodeState, error) {
	var s C.int32_t
	err := g.local(func(p unsafe.Pointer) C.int32_t { return C.zxv_paradox_graph_state(gr(p), ptr32(&id), &s) })
	return ParadoxNodeState(s), err
}

// ParadoxNode is one graph node.
type ParadoxNode struct {
	ID    [32]byte         // the hash of its signed input
	Kind  ParadoxKind      // its current kind
	State ParadoxNodeState // its state
}

// NodeAt returns node index in id order (after the first pass, else ZXV_E_STATE).
func (g *ParadoxGraph) NodeAt(index uint32) (ParadoxNode, error) {
	var n ParadoxNode
	var k, s C.int32_t
	err := g.local(func(p unsafe.Pointer) C.int32_t {
		return C.zxv_paradox_graph_node_at(gr(p), C.uint32_t(index), ptr32(&n.ID), &k, &s)
	})
	n.Kind, n.State = ParadoxKind(k), ParadoxNodeState(s)
	return n, err
}

// Nodes returns every node in id order (after the first pass).
func (g *ParadoxGraph) Nodes() ([]ParadoxNode, error) {
	info, err := g.Info()
	if err != nil {
		return nil, err
	}
	out := make([]ParadoxNode, 0, info.Nodes)
	for i := uint32(0); i < info.Nodes; i++ {
		n, err := g.NodeAt(i)
		if err != nil {
			return nil, err
		}
		out = append(out, n)
	}
	return out, nil
}

// ParadoxGraphInfo is a graph's progress.
type ParadoxGraphInfo struct {
	Nodes     uint32   // node count
	Pass      uint32   // passes run
	TotalMag  uint32   // net tension of the open nodes after the last pass
	TotalDir  int32    // its direction, -1, 0 or +1
	Escalated bool     // the step limit was reached
	Chain     [32]byte // SHA-256 chain over every pass (zeros before the first)
}

// Info returns the graph's progress.
func (g *ParadoxGraph) Info() (ParadoxGraphInfo, error) {
	var i ParadoxGraphInfo
	var n, ps, m C.uint32_t
	var d, e C.int32_t
	err := g.local(func(p unsafe.Pointer) C.int32_t {
		return C.zxv_paradox_graph_info(gr(p), &n, &ps, &m, &d, &e, ptr32(&i.Chain))
	})
	i.Nodes, i.Pass, i.TotalMag, i.TotalDir, i.Escalated = uint32(n), uint32(ps), uint32(m), int32(d), e != 0
	return i, err
}

// GraphPass runs one pass of graph in the fixed order (contain, sandbox,
// hold, apply; nodes in id order) and logs it. ParadoxResolved or
// ParadoxPending with a nil error; when the pass count reaches the step
// limit unresolved it returns ParadoxEscalateToOwner and ErrParadoxStep
// (the pass is logged and the graph stays readable).
func (b *ParadoxBook) GraphPass(g *ParadoxGraph) (ParadoxOutcome, error) {
	if err := sameContext(b.cap, g.cap, "graph"); err != nil {
		return 0, err
	}
	var o C.int32_t = -1
	err := g.h.use(func(gp unsafe.Pointer) error {
		return b.gated(func(cp *C.zxv_cap, p unsafe.Pointer) C.int32_t {
			return C.zxv_paradox_graph_pass(cp, bk(p), gr(gp), &o)
		})
	})
	if err != nil && statusOf(err) == StatusParadoxStep {
		return ParadoxEscalateToOwner, err
	}
	return ParadoxOutcome(o), err
}
