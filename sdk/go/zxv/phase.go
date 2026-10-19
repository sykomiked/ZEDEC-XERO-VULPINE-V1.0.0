// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

package zxv

/*
#include "zxv_api.h"
*/
import "C"

import (
	"context"
	"errors"
	"math"
	"math/big"
	"runtime"
	"sync"
	"time"
	"unsafe"
)

// PhaseCycle is the number of ticks in one harmonic fundamental (27,720 =
// lcm(1..11)).
const PhaseCycle uint64 = 27720

// Decision is the coordinator's answer to an admission request.
type Decision int32

// Decisions.
const (
	Admit Decision = 0
	Veto  Decision = 1
	Defer Decision = 2
	Retry Decision = 3
)

// StepFlags describe a step submitted to Admit.
type StepFlags uint32

// Step flags.
const (
	StepRequiresHealth StepFlags = 0x1
	StepHardware       StepFlags = 0x2
	StepS0Pending      StepFlags = 0x4
)

// Health of a phase.
type Health int32

// Health values.
const (
	HealthUnknown   Health = 0
	HealthHealthy   Health = 1
	HealthDegraded  Health = 2
	HealthUnhealthy Health = 3
)

// EventState is the state of a deferred event.
type EventState int32

// Event states.
const (
	EventPending   EventState = 0 // S0: waiting for its tick
	EventCleared   EventState = 1
	EventCancelled EventState = 2
)

// PhaseClock is a phase coordinator (zxv_phase) used through a capability
// with CapPhase. ZXV time is a logical tick count. Drive it manually with
// Advance or in real time with RunRealtime; wait for deferred events with
// DeferredEvent.Wait. Safe for concurrent use. Only advances made through
// this value wake waiters.
type PhaseClock struct {
	h    *handle
	cap  *Capability
	mu   sync.Mutex
	wake chan struct{}
}

// NewPhaseClock creates a clock at tick 0 in cap's context (cap needs
// CapPhase).
func NewPhaseClock(cap *Capability) (*PhaseClock, error) {
	var p *C.zxv_phase
	if err := cap.gated(func(c *C.zxv_cap) C.int32_t { return C.zxv_phase_create_cap(c, &p) }); err != nil {
		return nil, err
	}
	pc := &PhaseClock{cap: cap, wake: make(chan struct{})}
	pc.h = newHandle("phase clock", unsafe.Pointer(p), func(p unsafe.Pointer) { C.zxv_phase_destroy((*C.zxv_phase)(p)) }, cap.h)
	runtime.SetFinalizer(pc, func(pc *PhaseClock) { _ = pc.h.close() })
	return pc, nil
}

// Close destroys the clock.
func (pc *PhaseClock) Close() error { return pc.h.close() }

func (pc *PhaseClock) gated(f func(cp *C.zxv_cap, p *C.zxv_phase) C.int32_t) error {
	return pc.h.use(func(p unsafe.Pointer) error {
		return pc.cap.gated(func(cp *C.zxv_cap) C.int32_t { return f(cp, (*C.zxv_phase)(p)) })
	})
}

func (pc *PhaseClock) local(f func(p *C.zxv_phase) C.int32_t) error {
	if err := pc.cap.ctx.guard(); err != nil {
		return err
	}
	err := pc.h.use(func(p unsafe.Pointer) error { return call(func() C.int32_t { return f((*C.zxv_phase)(p)) }) })
	return pc.cap.ctx.fatal(err)
}

func (pc *PhaseClock) broadcast() {
	pc.mu.Lock()
	close(pc.wake)
	pc.wake = make(chan struct{})
	pc.mu.Unlock()
}

func (pc *PhaseClock) waitChan() <-chan struct{} {
	pc.mu.Lock()
	defer pc.mu.Unlock()
	return pc.wake
}

// Now returns the current tick.
func (pc *PhaseClock) Now() (uint64, error) {
	var t C.uint64_t
	err := pc.local(func(p *C.zxv_phase) C.int32_t { return C.zxv_phase_now(p, &t) })
	return uint64(t), err
}

// Advance moves the clock by delta ticks and returns the new tick and how
// many deferred events cleared; their waiters wake.
func (pc *PhaseClock) Advance(delta uint64) (now uint64, cleared uint32, err error) {
	var n C.uint64_t
	var c C.uint32_t
	err = pc.gated(func(cp *C.zxv_cap, p *C.zxv_phase) C.int32_t {
		return C.zxv_phase_advance(cp, p, C.uint64_t(delta), &n, &c)
	})
	if err == nil {
		pc.broadcast()
	}
	return uint64(n), uint32(c), err
}

// Admit asks to admit step stepID of phase phaseID (1..13). token is
// non-zero only for Admit.
func (pc *PhaseClock) Admit(phaseID, stepID uint32, flags StepFlags) (d Decision, token uint64, err error) {
	var dec C.int32_t
	var tok C.uint64_t
	err = pc.gated(func(cp *C.zxv_cap, p *C.zxv_phase) C.int32_t {
		return C.zxv_phase_admit(cp, p, C.uint32_t(phaseID), C.uint32_t(stepID), C.uint32_t(flags), &dec, &tok)
	})
	return Decision(dec), uint64(tok), err
}

// SetHealth reports the health of phase phaseID (1..13).
func (pc *PhaseClock) SetHealth(phaseID uint32, h Health) error {
	return pc.gated(func(cp *C.zxv_cap, p *C.zxv_phase) C.int32_t {
		return C.zxv_phase_set_health(cp, p, C.uint32_t(phaseID), C.int32_t(h))
	})
}

// Defer parks an S0 event until absolute tick dueTick (> now).
func (pc *PhaseClock) Defer(dueTick uint64) (*DeferredEvent, error) {
	var id C.uint64_t
	err := pc.gated(func(cp *C.zxv_cap, p *C.zxv_phase) C.int32_t {
		return C.zxv_phase_defer(cp, p, C.uint64_t(dueTick), &id)
	})
	if err != nil {
		return nil, err
	}
	return &DeferredEvent{clock: pc, id: uint64(id), due: dueTick}, nil
}

// UntilTick is Defer under the name other ZXV SDKs use.
func (pc *PhaseClock) UntilTick(dueTick uint64) (*DeferredEvent, error) { return pc.Defer(dueTick) }

// After defers an event delta ticks after now.
func (pc *PhaseClock) After(delta uint64) (*DeferredEvent, error) {
	now, err := pc.Now()
	if err != nil {
		return nil, err
	}
	if delta > math.MaxUint64-now {
		return nil, newError(StatusRange, "tick overflow")
	}
	return pc.Defer(now + delta)
}

// NextDue returns the earliest due tick of a pending event; ok is false
// when nothing waits.
func (pc *PhaseClock) NextDue() (tick uint64, ok bool, err error) {
	var t C.uint64_t
	err = pc.local(func(p *C.zxv_phase) C.int32_t { return C.zxv_phase_next_due(p, &t) })
	if errors.Is(err, ErrNotFound) {
		return 0, false, nil
	}
	return uint64(t), err == nil, err
}

// RunRealtime advances the clock at ticksPerSecond until ctx is done,
// waking deferred events as they clear. It blocks; run it in a goroutine.
// The tick count is derived from elapsed time, so it does not drift. It
// returns nil when ctx is cancelled.
func (pc *PhaseClock) RunRealtime(ctx context.Context, ticksPerSecond uint64) error {
	if ticksPerSecond == 0 {
		return newError(StatusArg, "ticksPerSecond must be > 0")
	}
	step := time.Second / time.Duration(ticksPerSecond)
	if step < time.Millisecond {
		step = time.Millisecond
	}
	if step > 10*time.Millisecond {
		step = 10 * time.Millisecond
	}
	t := time.NewTicker(step)
	defer t.Stop()
	start := time.Now()
	var issued uint64
	tps := new(big.Int).SetUint64(ticksPerSecond)
	for {
		select {
		case <-ctx.Done():
			return nil
		case <-t.C:
			el := new(big.Int).Mul(big.NewInt(int64(time.Since(start))), tps)
			due := el.Div(el, big.NewInt(int64(time.Second))).Uint64()
			if due > issued {
				if _, _, err := pc.Advance(due - issued); err != nil {
					return err
				}
				issued = due
			}
		}
	}
}

// DeferredEvent is an S0 event parked on a PhaseClock until its tick.
type DeferredEvent struct {
	clock *PhaseClock
	id    uint64
	due   uint64
}

// ID returns the event id.
func (e *DeferredEvent) ID() uint64 { return e.id }

// DueTick returns the absolute tick it waits for.
func (e *DeferredEvent) DueTick() uint64 { return e.due }

// State returns the event's current state.
func (e *DeferredEvent) State() (EventState, error) {
	var st C.int32_t
	var due C.uint64_t
	err := e.clock.local(func(p *C.zxv_phase) C.int32_t { return C.zxv_phase_event(p, C.uint64_t(e.id), &st, &due) })
	return EventState(st), err
}

// Cancel cancels the event (ZXV_E_STATE if it is no longer pending).
func (e *DeferredEvent) Cancel() error {
	err := e.clock.gated(func(cp *C.zxv_cap, p *C.zxv_phase) C.int32_t { return C.zxv_phase_cancel(cp, p, C.uint64_t(e.id)) })
	if err == nil {
		e.clock.broadcast()
	}
	return err
}

// Wait blocks until the clock reaches the event's tick (EventCleared), the
// event is cancelled (EventCancelled), or ctx is done (ctx.Err()).
func (e *DeferredEvent) Wait(ctx context.Context) (EventState, error) {
	for {
		wake := e.clock.waitChan()
		st, err := e.State()
		if err != nil || st != EventPending {
			return st, err
		}
		select {
		case <-wake:
		case <-ctx.Done():
			return EventPending, ctx.Err()
		}
	}
}
