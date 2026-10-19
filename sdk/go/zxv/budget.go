// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

package zxv

/*
#include <stdlib.h>
#include "zxv_api.h"
#include "shim.h"
*/
import "C"

import (
	"context"
	"errors"
	"runtime"
	"runtime/cgo"
	"sync"
	"unsafe"
)

// Resource is what a budget measures (the kernel's rmag_resource_t).
type Resource int32

// Resources.
const (
	ResComputeCycles Resource = 0
	ResMemory        Resource = 1
	ResBandwidth     Resource = 2
	ResEnergy        Resource = 3
	ResStorage       Resource = 4
	ResTokens        Resource = 12 // one model token = one event
)

// BudgetEvent is reported to OnEvent callbacks.
type BudgetEvent int32

// Budget events.
const (
	BudgetDenied      BudgetEvent = 1 // a consume was refused
	BudgetInvalid     BudgetEvent = 2 // a consume/release was malformed
	BudgetReplenished BudgetEvent = 3 // a release or larger limit freed budget
)

// BudgetStatus is a budget's remaining and used amounts.
type BudgetStatus struct {
	Remaining Rational
	Used      Rational
	Epoch     uint64 // replenishments so far
}

// budgetShared is what the C listener reaches through a cgo.Handle.
type budgetShared struct {
	mu        sync.Mutex
	wake      chan struct{} // closed and replaced on every replenishment
	callbacks []func(budget uint32, ev BudgetEvent)
}

func (s *budgetShared) event(budget uint32, ev int32) {
	s.mu.Lock()
	if BudgetEvent(ev) == BudgetReplenished {
		close(s.wake)
		s.wake = make(chan struct{})
	}
	cbs := append([]func(uint32, BudgetEvent){}, s.callbacks...)
	s.mu.Unlock()
	for _, cb := range cbs {
		cb(budget, BudgetEvent(ev))
	}
}

func (s *budgetShared) waitChan() <-chan struct{} {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.wake
}

// Budgets is a set of exact rational budgets (one zxv_rmag) used through a
// capability with CapBudget. Safe for concurrent use.
type Budgets struct {
	h      *handle
	cap    *Capability
	shared *budgetShared
	cgoh   cgo.Handle
}

// NewBudgets creates an empty budget set in cap's context (cap needs
// CapBudget).
func NewBudgets(cap *Capability) (*Budgets, error) {
	var r *C.zxv_rmag
	if err := cap.gated(func(p *C.zxv_cap) C.int32_t { return C.zxv_rmag_create_cap(p, &r) }); err != nil {
		return nil, err
	}
	b := &Budgets{cap: cap, shared: &budgetShared{wake: make(chan struct{})}}
	b.cgoh = cgo.NewHandle(b.shared)
	if err := call(func() C.int32_t { return C.zxv_go_rmag_listen(r, C.uintptr_t(b.cgoh)) }); err != nil {
		C.zxv_rmag_destroy(r)
		b.cgoh.Delete()
		return nil, err
	}
	b.h = newHandle("budgets", unsafe.Pointer(r), func(p unsafe.Pointer) {
		C.zxv_go_rmag_unlisten((*C.zxv_rmag)(p))
		C.zxv_rmag_destroy((*C.zxv_rmag)(p))
		b.cgoh.Delete()
	}, cap.h)
	runtime.SetFinalizer(b, func(b *Budgets) { _ = b.h.close() })
	return b, nil
}

// Close destroys the set. It fails while a session is bound to it.
func (b *Budgets) Close() error { return b.h.close() }

// OnEvent registers a callback for every event of this set. Callbacks run
// synchronously inside the call that caused the event; keep them short.
func (b *Budgets) OnEvent(f func(budget uint32, ev BudgetEvent)) {
	b.shared.mu.Lock()
	b.shared.callbacks = append(b.shared.callbacks, f)
	b.shared.mu.Unlock()
}

// OnExhausted registers a callback for denied consumes.
func (b *Budgets) OnExhausted(f func(budget uint32)) {
	b.OnEvent(func(id uint32, ev BudgetEvent) {
		if ev == BudgetDenied {
			f(id)
		}
	})
}

func (b *Budgets) gated(f func(cp *C.zxv_cap, r *C.zxv_rmag) C.int32_t) error {
	return b.h.use(func(r unsafe.Pointer) error {
		return b.cap.gated(func(cp *C.zxv_cap) C.int32_t { return f(cp, (*C.zxv_rmag)(r)) })
	})
}

// Create makes a budget with an exact limit (>= 0). With enforce false it
// only counts.
func (b *Budgets) Create(name string, res Resource, limit Rational, enforce bool) (*Budget, error) {
	n, err := cstring(name)
	if err != nil {
		return nil, err
	}
	defer freeC(n)
	e := C.int32_t(0)
	if enforce {
		e = 1
	}
	var id C.uint32_t
	err = b.gated(func(cp *C.zxv_cap, r *C.zxv_rmag) C.int32_t {
		return C.zxv_rmag_budget(cp, r, n, C.int32_t(res), C.int64_t(limit.Num()), C.int64_t(limit.Den()), e, &id)
	})
	if err != nil {
		return nil, err
	}
	return &Budget{set: b, id: uint32(id)}, nil
}

// Budget returns the budget with id (as Budget.ID reports).
func (b *Budgets) Budget(id uint32) *Budget { return &Budget{set: b, id: id} }

// Budget is one budget of a Budgets set.
type Budget struct {
	set *Budgets
	id  uint32
}

// ID returns the budget id inside its set.
func (b *Budget) ID() uint32 { return b.id }

// Consume takes amount (> 0). It matches ErrBudgetExhausted (REFUSED) if it
// would pass an enforced limit (nothing is consumed), and ErrInvalidAmount
// (USAGE) for a zero, negative or overflowing amount: never retry that.
func (b *Budget) Consume(amount Rational) error {
	return b.set.gated(func(cp *C.zxv_cap, r *C.zxv_rmag) C.int32_t {
		return C.zxv_rmag_consume(cp, r, C.uint32_t(b.id), C.int64_t(amount.Num()), C.int64_t(amount.Den()))
	})
}

// Release gives back amount (clamped at zero used) and wakes waiters.
func (b *Budget) Release(amount Rational) error {
	return b.set.gated(func(cp *C.zxv_cap, r *C.zxv_rmag) C.int32_t {
		return C.zxv_rmag_release(cp, r, C.uint32_t(b.id), C.int64_t(amount.Num()), C.int64_t(amount.Den()))
	})
}

// SetLimit replaces the limit; a larger limit wakes waiters.
func (b *Budget) SetLimit(limit Rational) error {
	return b.set.gated(func(cp *C.zxv_cap, r *C.zxv_rmag) C.int32_t {
		return C.zxv_rmag_set_limit(cp, r, C.uint32_t(b.id), C.int64_t(limit.Num()), C.int64_t(limit.Den()))
	})
}

// Status returns the remaining and used amounts and the replenish epoch.
func (b *Budget) Status() (BudgetStatus, error) {
	ctx := b.set.cap.ctx
	if err := ctx.guard(); err != nil {
		return BudgetStatus{}, err
	}
	var rn, rd, un, ud C.int64_t
	var ep C.uint64_t
	err := b.set.h.use(func(r unsafe.Pointer) error {
		return call(func() C.int32_t {
			return C.zxv_rmag_status((*C.zxv_rmag)(r), C.uint32_t(b.id), &rn, &rd, &un, &ud, &ep)
		})
	})
	if err != nil {
		return BudgetStatus{}, ctx.fatal(err)
	}
	return BudgetStatus{Rational{int64(rn), int64(rd)}, Rational{int64(un), int64(ud)}, uint64(ep)}, nil
}

// ConsumeWait consumes amount, waiting while the budget is exhausted until
// it is replenished (a Release or larger SetLimit through this SDK), then
// retrying. It returns ctx.Err() when ctx is cancelled or times out, and
// any other error (an invalid amount, a denied capability, a fatal halt)
// at once.
func (b *Budget) ConsumeWait(ctx context.Context, amount Rational) error {
	for {
		wake := b.set.shared.waitChan()
		before, err := b.Status()
		if err != nil {
			return err
		}
		err = b.Consume(amount)
		if err == nil || !errors.Is(err, ErrBudgetExhausted) {
			return err
		}
		if s, err := b.Status(); err != nil {
			return err
		} else if s.Epoch != before.Epoch {
			continue
		}
		select {
		case <-wake:
		case <-ctx.Done():
			return ctx.Err()
		}
	}
}

// ConsumeAsync runs ConsumeWait in a goroutine and delivers its result on
// the returned channel (buffered; it receives exactly one value).
func (b *Budget) ConsumeAsync(ctx context.Context, amount Rational) <-chan error {
	ch := make(chan error, 1)
	go func() { ch <- b.ConsumeWait(ctx, amount) }()
	return ch
}
