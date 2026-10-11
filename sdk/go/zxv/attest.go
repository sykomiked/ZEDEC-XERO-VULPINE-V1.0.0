// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

package zxv

/*
#include <stdlib.h>
#include "zxv_api.h"
*/
import "C"

import (
	"runtime"
	"unsafe"
)

// AttestationState is the state of one attestation.
type AttestationState struct {
	State    Truth
	Positive uint32
	Negative uint32
}

// Attestations is a registry of four-valued attestations. Recording needs
// CapAttest. Safe for concurrent use.
type Attestations struct {
	h   *handle
	cap *Capability
}

// NewAttestations creates an empty registry in cap's context (cap needs
// CapAttest).
func NewAttestations(cap *Capability) (*Attestations, error) {
	var r *C.zxv_lpres
	if err := cap.gated(func(p *C.zxv_cap) C.int32_t { return C.zxv_lpres_create_cap(p, &r) }); err != nil {
		return nil, err
	}
	a := &Attestations{cap: cap}
	a.h = newHandle("attestations", unsafe.Pointer(r), func(p unsafe.Pointer) { C.zxv_lpres_destroy((*C.zxv_lpres)(p)) }, cap.h)
	runtime.SetFinalizer(a, func(a *Attestations) { _ = a.h.close() })
	return a, nil
}

// Close destroys the registry.
func (a *Attestations) Close() error { return a.h.close() }

func (a *Attestations) gated(f func(cp *C.zxv_cap, r *C.zxv_lpres) C.int32_t) error {
	return a.h.use(func(r unsafe.Pointer) error {
		return a.cap.gated(func(cp *C.zxv_cap) C.int32_t { return f(cp, (*C.zxv_lpres)(r)) })
	})
}

func (a *Attestations) local(f func(r *C.zxv_lpres) C.int32_t) error {
	if err := a.cap.ctx.guard(); err != nil {
		return err
	}
	err := a.h.use(func(r unsafe.Pointer) error { return call(func() C.int32_t { return f((*C.zxv_lpres)(r)) }) })
	return a.cap.ctx.fatal(err)
}

// Attest records an attestation and returns its index. name and attester
// are at most 31 bytes, digest at most 32 (may be nil). subject is the
// kernel lpres_subject_type_t (0 SOURCE .. 14 FIRMWARE). Attesting
// TruthTrue counts as one positive piece of evidence.
func (a *Attestations) Attest(name string, subject int32, state Truth, attester string, digest []byte) (uint32, error) {
	n, err := cstring(name)
	if err != nil {
		return 0, err
	}
	defer freeC(n)
	at, err := cstring(attester)
	if err != nil {
		return 0, err
	}
	defer freeC(at)
	var idx C.uint32_t
	err = a.gated(func(cp *C.zxv_cap, r *C.zxv_lpres) C.int32_t {
		return C.zxv_lpres_attest(cp, r, n, C.int32_t(subject), C.int32_t(state.Raw()), at, bytePtr(digest), C.uint32_t(len(digest)), &idx)
	})
	return uint32(idx), err
}

// Evidence adds one piece of evidence; the state follows TruthFromEvidence.
func (a *Attestations) Evidence(index uint32, positive bool) error {
	pos := C.int32_t(0)
	if positive {
		pos = 1
	}
	return a.gated(func(cp *C.zxv_cap, r *C.zxv_lpres) C.int32_t {
		return C.zxv_lpres_evidence(cp, r, C.uint32_t(index), pos)
	})
}

// Find returns the index of a named attestation (ErrNotFound if none).
func (a *Attestations) Find(name string) (uint32, error) {
	n, err := cstring(name)
	if err != nil {
		return 0, err
	}
	defer freeC(n)
	var idx C.uint32_t
	err = a.local(func(r *C.zxv_lpres) C.int32_t { return C.zxv_lpres_find(r, n, &idx) })
	return uint32(idx), err
}

// State returns the state and evidence counts of an attestation.
func (a *Attestations) State(index uint32) (AttestationState, error) {
	var st C.int32_t
	var p, n C.uint32_t
	if err := a.local(func(r *C.zxv_lpres) C.int32_t { return C.zxv_lpres_state(r, C.uint32_t(index), &st, &p, &n) }); err != nil {
		return AttestationState{}, err
	}
	t, err := TruthFromRaw(int(st))
	return AttestationState{t, uint32(p), uint32(n)}, err
}

// GateClear reports whether every attestation is certain (no Both, no
// Neither). Actuator authority requires a clear gate.
func (a *Attestations) GateClear() (bool, error) {
	var c C.int32_t
	err := a.local(func(r *C.zxv_lpres) C.int32_t { return C.zxv_lpres_gate(r, &c) })
	return c != 0, err
}
