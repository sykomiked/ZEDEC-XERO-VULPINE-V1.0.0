// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

package zxv

/*
#include "zxv_api.h"
*/
import "C"

import (
	"runtime"
	"strings"
	"unsafe"
)

// Caps is a set of capability bits (ZXV_CAP_*). Combine with |.
type Caps uint64

// Capability bits.
const (
	CapLedgerRead  Caps = 1 << 0    // read balances, journals
	CapLedgerPost  Caps = 1 << 1    // post transfers
	CapCryptoSign  Caps = 1 << 2    // ML-DSA-87, Ed25519 signing
	CapCryptoKEM   Caps = 1 << 3    // ML-KEM-1024
	CapAIInference Caps = 1 << 4    // models and generation
	CapBudget      Caps = 1 << 5    // resource budgets
	CapPhase       Caps = 1 << 6    // phase clock admit/defer/advance
	CapAttest      Caps = 1 << 7    // attestations
	CapSwarm       Caps = 1 << 8    // swarm economy
	CapEVM         Caps = 1 << 9    // EVM state channels
	CapAll         Caps = 1<<10 - 1 // every ABI 1.1 bit; keeps this value forever
	CapHWMatrix    Caps = 1 << 10   // ABI 1.4: build and call the emulation matrix
	CapHWReconf    Caps = 1 << 11   // ABI 1.4: load hardware images and contracts
	CapAll14       Caps = 1<<12 - 1 // every bit as of ABI 1.4
)

// Has reports whether every bit of other is in c.
func (c Caps) Has(other Caps) bool { return c&other == other }

// String lists the bit names.
func (c Caps) String() string {
	names := []string{"LEDGER_READ", "LEDGER_POST", "CRYPTO_SIGN", "CRYPTO_KEM", "AI_INFERENCE",
		"BUDGET", "PHASE", "ATTEST", "SWARM", "EVM", "HW_MATRIX", "HW_RECONF"}
	var out []string
	for i, n := range names {
		if c&(1<<i) != 0 {
			out = append(out, n)
		}
	}
	if len(out) == 0 {
		return "NONE"
	}
	return strings.Join(out, "|")
}

// CapabilityInfo is what Capability.Info reports.
type CapabilityInfo struct {
	Caps        Caps
	ProcessID   uint32
	Attestation Truth
	Revoked     bool
}

// Capability is the right to make some gated calls on a Context. Every
// object that makes gated calls holds one and passes it on every call; the
// library checks it every time. Narrow it with Attenuate. A Capability is
// safe for concurrent use.
//
// Honest limit: inside one process this is least-privilege discipline,
// not a sandbox.
type Capability struct {
	h      *handle
	ctx    *Context
	parent *Capability
}

func newCapability(ctx *Context, parent *Capability, p *C.zxv_cap) *Capability {
	parents := []*handle{ctx.h}
	if parent != nil {
		parents = append(parents, parent.h)
	}
	c := &Capability{ctx: ctx, parent: parent}
	c.h = newHandle("capability", unsafe.Pointer(p), func(p unsafe.Pointer) { C.zxv_cap_destroy((*C.zxv_cap)(p)) }, parents...)
	runtime.SetFinalizer(c, func(c *Capability) { _ = c.h.close() })
	return c
}

// Close destroys the capability. It fails while children (attenuated
// capabilities, budgets, sessions, ...) are open.
func (c *Capability) Close() error { return c.h.close() }

// Context returns the context this capability governs.
func (c *Capability) Context() *Context { return c.ctx }

// Attenuate returns a child holding c's bits & keep. It never widens;
// revoking c revokes it.
func (c *Capability) Attenuate(keep Caps) (*Capability, error) {
	var out *C.zxv_cap
	err := c.gated(func(p *C.zxv_cap) C.int32_t { return C.zxv_cap_attenuate(p, C.uint64_t(keep), &out) })
	if err != nil {
		return nil, err
	}
	return newCapability(c.ctx, c, out), nil
}

// Revoke permanently revokes this capability and every descendant.
func (c *Capability) Revoke() error {
	return c.gated(func(p *C.zxv_cap) C.int32_t { return C.zxv_cap_revoke(p) })
}

// SetAttestation sets the attestation; anything but TruthTrue blocks gated
// calls (TruthBoth reports a contradictory attestation).
func (c *Capability) SetAttestation(t Truth) error {
	return c.gated(func(p *C.zxv_cap) C.int32_t { return C.zxv_cap_set_attestation(p, C.int32_t(t.Raw())) })
}

// Info returns the bits, process id, attestation and revocation state.
func (c *Capability) Info() (CapabilityInfo, error) {
	var caps C.uint64_t
	var pid C.uint32_t
	var att, rev C.int32_t
	err := c.gated(func(p *C.zxv_cap) C.int32_t { return C.zxv_cap_info(p, &caps, &pid, &att, &rev) })
	if err != nil {
		return CapabilityInfo{}, err
	}
	t, err := TruthFromRaw(int(att))
	return CapabilityInfo{Caps: Caps(caps), ProcessID: uint32(pid), Attestation: t, Revoked: rev != 0}, err
}

// Check returns nil if a call needing needed would pass the gate, else an
// error matching ErrCapabilityDenied.
func (c *Capability) Check(needed Caps) error {
	return c.gated(func(p *C.zxv_cap) C.int32_t { return C.zxv_cap_check(p, C.uint64_t(needed)) })
}

// gatedSeq runs several gated calls under one use of this capability:
// guard, f, fatal path. f maps statuses itself (call, readString).
func (c *Capability) gatedSeq(f func(p *C.zxv_cap) error) error {
	if err := c.ctx.guard(); err != nil {
		return err
	}
	err := c.h.use(func(p unsafe.Pointer) error { return f((*C.zxv_cap)(p)) })
	return c.ctx.fatal(err)
}

// gated runs a call with this capability: guard, call, fatal path.
func (c *Capability) gated(f func(p *C.zxv_cap) C.int32_t) error {
	if err := c.ctx.guard(); err != nil {
		return err
	}
	err := c.h.use(func(p unsafe.Pointer) error {
		return call(func() C.int32_t { return f((*C.zxv_cap)(p)) })
	})
	return c.ctx.fatal(err)
}
