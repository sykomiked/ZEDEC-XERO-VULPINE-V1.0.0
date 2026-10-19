// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

package zxv

// ABI 1.5: the second feature mask, the new capability bits and status
// codes, and the helpers every 1.5 group shares.
//
// The 32 bits of zxv_features are nearly used up, so the 1.5 groups report
// through a second mask, Features2 (LiveFeatures2). A group that is not
// built into the loaded library returns ErrNotImplemented from every call
// and leaves its Feat2* bit clear.

/*
#include "zxv_api.h"
*/
import "C"

import (
	"runtime"
	"unsafe"
)

// Features2 is the second feature mask (zxv_features2, ABI 1.5): which of
// the 1.5 groups are live in the loaded library.
type Features2 uint32

// Feature bits of Features2.
const (
	Feat2Treaty  Features2 = C.ZXV_FEAT2_TREATY  // treaties: copybook records, Sutra text
	Feat2AIPI    Features2 = C.ZXV_FEAT2_AIPI    // content-addressed functions and CIDs
	Feat2Coin    Features2 = C.ZXV_FEAT2_COIN    // Fibonacci coins and token octane
	Feat2Fee     Features2 = C.ZXV_FEAT2_FEE     // the assurance fee and its split
	Feat2VHub    Features2 = C.ZXV_FEAT2_VHUB    // V-Bill hub pegs, quotes and swaps
	Feat2Freight Features2 = C.ZXV_FEAT2_FREIGHT // erasure-coded freight packets
	Feat2Web4    Features2 = C.ZXV_FEAT2_WEB4    // agent manifests, directory, consent
	Feat2Paradox Features2 = C.ZXV_FEAT2_PARADOX // the paradox engine
)

// Has reports whether every bit of f is live.
func (m Features2) Has(f Features2) bool { return m&f == f }

// LiveFeatures2 returns the ABI 1.5 groups live in the loaded library.
func LiveFeatures2() Features2 {
	var m C.uint32_t
	C.zxv_features2(&m)
	return Features2(m)
}

// Status codes added in ABI 1.5. Compare them with Error.Status, or use the
// sentinels below with errors.Is.
const (
	StatusDenom        int32 = -32 // not a Fibonacci ladder face (USAGE)
	StatusTreaty       int32 = -33 // a treaty fails validation (USAGE); see TreatyError
	StatusCID          int32 = -34 // content does not match its CID, or a bad CID (REFUSED)
	StatusConsent      int32 = -35 // no valid consent for this agent action (REFUSED)
	StatusErasure      int32 = -36 // too few freight rows to rebuild the packet (REFUSED)
	StatusPeg          int32 = -37 // no valid signed peg for this currency pair (REFUSED)
	StatusLimit        int32 = -38 // refused by a hub limit rule, not a fault (REFUSED)
	StatusParadoxStep  int32 = -39 // a paradox graph reached its step limit (REFUSED)
	StatusParadoxOwner int32 = -40 // needs the owner's approval (REFUSED)
)

// Sentinels for the ABI 1.5 statuses: errors.Is(err, zxv.ErrConsent).
var (
	// ErrDenom: a face that is not on the Fibonacci ladder (ZXV_E_DENOM, USAGE).
	ErrDenom error = &sentinel{name: "not a ladder face", status: StatusDenom}
	// ErrTreaty: a treaty fails validation (ZXV_E_TREATY, USAGE). errors.As
	// to *TreatyError gives the fault and its byte offset.
	ErrTreaty error = &sentinel{name: "treaty refused", status: StatusTreaty}
	// ErrCID: bytes that do not match their CID, or bytes or text that are
	// not a canonical CID (ZXV_E_CID, REFUSED).
	ErrCID error = &sentinel{name: "content id mismatch", status: StatusCID}
	// ErrConsent: no valid consent token for the agent action (ZXV_E_CONSENT).
	ErrConsent error = &sentinel{name: "no valid consent", status: StatusConsent}
	// ErrErasure: fewer freight rows arrived than are needed to rebuild the
	// payload (ZXV_E_ERASURE). Wait for more rows and decode again.
	ErrErasure error = &sentinel{name: "too few freight rows", status: StatusErasure}
	// ErrPeg: a currency pair has no valid signed peg, or a peg is stale or
	// replayed (ZXV_E_PEG).
	ErrPeg error = &sentinel{name: "no valid peg", status: StatusPeg}
	// ErrLimit: a hub limit rule refused the swap (ZXV_E_LIMIT; not a fault).
	ErrLimit error = &sentinel{name: "limit rule refused", status: StatusLimit}
	// ErrParadoxStep: a paradox graph reached its step limit unresolved; it
	// goes to the owner (ZXV_E_PARADOX_STEP).
	ErrParadoxStep error = &sentinel{name: "paradox step limit", status: StatusParadoxStep}
	// ErrParadoxOwner: the change touches money or devices and needs the
	// owner's approval (ZXV_E_PARADOX_OWNER).
	ErrParadoxOwner error = &sentinel{name: "needs the owner", status: StatusParadoxOwner}
)

// ===== helpers shared by the 1.5 groups ===================================

// readBytes runs an "R4 for bytes" call (no NUL is added) on a locked
// thread: first into a buffer of hint bytes, then, on ZXV_E_BUFFER, into
// one of exactly the length the library asked for.
func readBytes(hint int, f func(buf *C.uint8_t, cap C.size_t, outLen *C.size_t) C.int32_t) ([]byte, error) {
	runtime.LockOSThread()
	defer runtime.UnlockOSThread()
	if hint < 1 {
		hint = 1
	}
	buf := make([]byte, hint)
	for i := 0; i < 3; i++ {
		var n C.size_t
		rc := f((*C.uint8_t)(unsafe.Pointer(&buf[0])), C.size_t(len(buf)), &n)
		if rc == C.ZXV_OK {
			return buf[:int(n):int(n)], nil
		}
		if rc == C.ZXV_E_BUFFER && int(n) > len(buf) {
			buf = make([]byte, int(n))
			continue
		}
		return nil, newError(int32(rc), lastError())
	}
	return nil, newError(StatusInternal, "the library kept asking for a larger buffer")
}

// ctxHandle is a C handle that belongs to a capability's context: every
// call on it fails closed with the context, and its gated calls pass the
// capability it was created with.
type ctxHandle struct {
	h   *handle
	cap *Capability
}

func newCtxHandle(what string, ptr unsafe.Pointer, destroy func(unsafe.Pointer), cap *Capability, exclusive bool) ctxHandle {
	h := newHandle(what, ptr, destroy, cap.h)
	h.exclusive = exclusive
	return ctxHandle{h: h, cap: cap}
}

// local runs a call that takes only the handle: guard, call, fatal path.
func (o *ctxHandle) local(f func(p unsafe.Pointer) C.int32_t) error {
	return o.localSeq(func(p unsafe.Pointer) error { return call(func() C.int32_t { return f(p) }) })
}

// localSeq is local for a body that maps its own statuses (readBytes).
func (o *ctxHandle) localSeq(f func(p unsafe.Pointer) error) error {
	if err := o.cap.ctx.guard(); err != nil {
		return err
	}
	return o.cap.ctx.fatal(o.h.use(f))
}

// gated runs a call that takes the capability and the handle.
func (o *ctxHandle) gated(f func(cp *C.zxv_cap, p unsafe.Pointer) C.int32_t) error {
	return o.h.use(func(p unsafe.Pointer) error {
		return o.cap.gated(func(cp *C.zxv_cap) C.int32_t { return f(cp, p) })
	})
}

// gatedSeq is gated for a body that maps its own statuses.
func (o *ctxHandle) gatedSeq(f func(cp *C.zxv_cap, p unsafe.Pointer) error) error {
	return o.h.use(func(p unsafe.Pointer) error {
		return o.cap.gatedSeq(func(cp *C.zxv_cap) error { return f(cp, p) })
	})
}

// sameContext refuses a handle of another context (ZXV_E_ARG, as the C
// groups do for two handles used together).
func sameContext(a, b *Capability, what string) error {
	if a.ctx != b.ctx {
		return newError(StatusArg, what+" belongs to a different context")
	}
	return nil
}

func boolInt(b bool) C.int32_t {
	if b {
		return 1
	}
	return 0
}

func ptr16(b *[16]byte) *C.uint8_t { return (*C.uint8_t)(unsafe.Pointer(&b[0])) }
