// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

package zxv

/*
#include <stdlib.h>
#include "zxv_api.h"
*/
import "C"

import (
	"errors"
	"fmt"
	"os"
	"runtime"
	"sync/atomic"
	"unsafe"
)

// FatalPolicy chooses what happens when a call reports a FATAL status.
type FatalPolicy int

const (
	// FatalReturn returns the error (errors.Is(err, ErrFatal)) and poisons
	// the Go handle: every later call on the context, and on every handle
	// derived from it, fails with ErrFatal without reaching the library.
	FatalReturn FatalPolicy = iota
	// FatalExit logs the error and calls os.Exit(70): the only form that
	// cannot be recovered or ignored.
	FatalExit
)

// ContextOption configures NewContext.
type ContextOption func(*Context)

// WithFatalPolicy sets the context's FatalPolicy.
func WithFatalPolicy(p FatalPolicy) ContextOption {
	return func(c *Context) { c.policy = p }
}

// Context is one platform instance: a ledger, its currencies and
// everything created from it. It is safe for concurrent use (the C context
// is serialized by a mutex). Close it after every handle derived from it.
type Context struct {
	h        *handle
	poisoned atomic.Bool
	policy   FatalPolicy
}

// EnabledCurrency describes a currency enabled in a context.
type EnabledCurrency struct {
	MinorUnits uint8
	Flags      uint32
}

// NewContext creates a context. It fails if the loaded libzxv is not ABI
// 1.x with x >= 2.
func NewContext(opts ...ContextOption) (*Context, error) {
	if err := checkVersion(); err != nil {
		return nil, err
	}
	var p *C.zxv_ctx
	if err := call(func() C.int32_t { return C.zxv_ctx_create(&p) }); err != nil {
		return nil, err
	}
	c := &Context{}
	for _, o := range opts {
		o(c)
	}
	c.h = newHandle("context", unsafe.Pointer(p), func(p unsafe.Pointer) { C.zxv_ctx_destroy((*C.zxv_ctx)(p)) })
	runtime.SetFinalizer(c, func(c *Context) { _ = c.h.close() })
	return c, nil
}

// Close destroys the context. It fails (ZXV_E_STATE) while capabilities,
// messages or other handles made from it are still open.
func (c *Context) Close() error { return c.h.close() }

// Poisoned reports whether the context has halted.
func (c *Context) Poisoned() bool {
	if c.poisoned.Load() {
		return true
	}
	var p C.int32_t
	err := c.h.use(func(ptr unsafe.Pointer) error {
		return call(func() C.int32_t { return C.zxv_ctx_is_poisoned((*C.zxv_ctx)(ptr), &p) })
	})
	return err == nil && p != 0
}

// Halt stops the context on purpose (an operator kill switch). Afterwards
// every call fails with ErrFatal. reason is at most 150 bytes.
func (c *Context) Halt(reason string) error {
	cr, err := cstring(reason)
	if err != nil {
		return err
	}
	defer freeC(cr)
	err = c.ctxCall(func(p *C.zxv_ctx) C.int32_t { return C.zxv_ctx_halt(p, cr) })
	if err == nil {
		c.poisoned.Store(true)
	}
	return err
}

// RootCapability creates a capability holding caps for processID, with an
// optional 21-octet identity (nil = zeros). Its attestation starts TRUE.
func (c *Context) RootCapability(processID uint32, identity *[21]byte, caps Caps) (*Capability, error) {
	if err := c.guard(); err != nil {
		return nil, err
	}
	var out *C.zxv_cap
	var idp *C.uint8_t
	if identity != nil {
		idp = (*C.uint8_t)(unsafe.Pointer(&identity[0]))
	}
	err := c.h.use(func(p unsafe.Pointer) error {
		return call(func() C.int32_t {
			return C.zxv_cap_create((*C.zxv_ctx)(p), C.uint32_t(processID), idp, C.uint64_t(caps), &out)
		})
	})
	if err != nil {
		return nil, c.fatal(err)
	}
	return newCapability(c, nil, out), nil
}

// EnableCurrencyCap enables an ISO 4217 currency for ledger accounts
// (minor units from the ISO table) through cap, which needs CapLedgerPost.
// A capability of another context is refused with ErrCapabilityDenied.
func (c *Context) EnableCurrencyCap(cap *Capability, alpha string) error {
	if err := c.ownCap(cap); err != nil {
		return err
	}
	a, err := cstring(alpha)
	if err != nil {
		return err
	}
	defer freeC(a)
	return cap.gated(func(p *C.zxv_cap) C.int32_t { return C.zxv_ccy_enable_cap(p, a) })
}

// EnableCurrencyWithMinorCap enables an ISO 4217 currency asserting its
// minor units, through cap (CapLedgerPost).
func (c *Context) EnableCurrencyWithMinorCap(cap *Capability, alpha string, minorUnits uint8) error {
	if err := c.ownCap(cap); err != nil {
		return err
	}
	a, err := cstring(alpha)
	if err != nil {
		return err
	}
	defer freeC(a)
	return cap.gated(func(p *C.zxv_cap) C.int32_t {
		return C.zxv_ccy_enable_with_minor_cap(p, a, C.uint8_t(minorUnits))
	})
}

// RegisterPrivateCurrencyCap registers a platform-private unit (e.g.
// "VFV") through cap (CapLedgerPost): usable in ledger accounts, refused in
// ISO 20022 messages.
func (c *Context) RegisterPrivateCurrencyCap(cap *Capability, code string, minorUnits uint8) error {
	if err := c.ownCap(cap); err != nil {
		return err
	}
	a, err := cstring(code)
	if err != nil {
		return err
	}
	defer freeC(a)
	return cap.gated(func(p *C.zxv_cap) C.int32_t {
		return C.zxv_ccy_register_private_cap(p, a, C.uint8_t(minorUnits))
	})
}

// ownCap refuses a nil capability or one of another context: the C cap
// forms act on the capability's own context, not on c.
func (c *Context) ownCap(cap *Capability) error {
	if err := c.guard(); err != nil {
		return err
	}
	if cap == nil {
		return newError(StatusArg, "nil capability")
	}
	if cap.ctx != c {
		return newError(StatusCap, "the capability belongs to a different context")
	}
	return nil
}

// EnableCurrency enables an ISO 4217 currency for ledger accounts (minor
// units from the ISO table).
//
// Deprecated: use EnableCurrencyCap with a CapLedgerPost capability.
func (c *Context) EnableCurrency(alpha string) error {
	a, err := cstring(alpha)
	if err != nil {
		return err
	}
	defer freeC(a)
	return c.ctxCall(func(p *C.zxv_ctx) C.int32_t { return C.zxv_ccy_enable(p, a) })
}

// EnableCurrencyWithMinor enables an ISO 4217 currency asserting its minor units.
//
// Deprecated: use EnableCurrencyWithMinorCap with a CapLedgerPost capability.
func (c *Context) EnableCurrencyWithMinor(alpha string, minorUnits uint8) error {
	a, err := cstring(alpha)
	if err != nil {
		return err
	}
	defer freeC(a)
	return c.ctxCall(func(p *C.zxv_ctx) C.int32_t { return C.zxv_ccy_enable_with_minor(p, a, C.uint8_t(minorUnits)) })
}

// RegisterPrivateCurrency registers a platform-private unit (e.g. "VFV"):
// usable in ledger accounts, refused in ISO 20022 messages.
//
// Deprecated: use RegisterPrivateCurrencyCap with a CapLedgerPost capability.
func (c *Context) RegisterPrivateCurrency(code string, minorUnits uint8) error {
	a, err := cstring(code)
	if err != nil {
		return err
	}
	defer freeC(a)
	return c.ctxCall(func(p *C.zxv_ctx) C.int32_t { return C.zxv_ccy_register_private(p, a, C.uint8_t(minorUnits)) })
}

// Currency returns the minor units and flags of an enabled currency.
func (c *Context) Currency(code string) (EnabledCurrency, error) {
	a, err := cstring(code)
	if err != nil {
		return EnabledCurrency{}, err
	}
	defer freeC(a)
	var mu C.uint8_t
	var fl C.uint32_t
	err = c.ctxCall(func(p *C.zxv_ctx) C.int32_t { return C.zxv_ccy_get(p, a, &mu, &fl) })
	return EnabledCurrency{MinorUnits: uint8(mu), Flags: uint32(fl)}, err
}

// EnabledCurrencies lists the enabled currency codes in enabling order.
func (c *Context) EnabledCurrencies() ([]string, error) {
	var n C.uint32_t
	if err := c.ctxCall(func(p *C.zxv_ctx) C.int32_t { return C.zxv_ccy_enabled_count(p, &n) }); err != nil {
		return nil, err
	}
	out := make([]string, 0, int(n))
	for i := C.uint32_t(0); i < n; i++ {
		var s string
		err := c.h.use(func(p unsafe.Pointer) error {
			var e error
			s, e = readString(func(b *C.char, cp C.size_t, l *C.size_t) C.int32_t {
				return C.zxv_ccy_enabled_at((*C.zxv_ctx)(p), i, b, cp, l)
			})
			return e
		})
		if err != nil {
			return nil, c.fatal(err)
		}
		out = append(out, s)
	}
	return out, nil
}

// guard fails fast when the Go handle is poisoned.
func (c *Context) guard() error {
	if c.poisoned.Load() {
		return c.fatal(newError(StatusPoisoned, "context halted earlier; this handle refuses every call"))
	}
	return nil
}

// fatal applies the fatal path to err if it is FATAL and returns it.
func (c *Context) fatal(err error) error {
	if err != nil && errors.Is(err, ErrFatal) {
		c.poisoned.Store(true)
		if c.policy == FatalExit {
			fmt.Fprintf(os.Stderr, "zxv: %v; FatalExit policy: exiting with status 70\n", err)
			os.Exit(70)
		}
	}
	return err
}

// ctxCall runs a context-level call: guard, call, fatal path.
func (c *Context) ctxCall(f func(p *C.zxv_ctx) C.int32_t) error {
	if err := c.guard(); err != nil {
		return err
	}
	err := c.h.use(func(p unsafe.Pointer) error {
		return call(func() C.int32_t { return f((*C.zxv_ctx)(p)) })
	})
	return c.fatal(err)
}

// rawCtx returns the C pointer for children that keep the context alive.
func (c *Context) rawCtx() *C.zxv_ctx {
	c.h.mu.RLock()
	defer c.h.mu.RUnlock()
	return (*C.zxv_ctx)(c.h.ptr)
}
