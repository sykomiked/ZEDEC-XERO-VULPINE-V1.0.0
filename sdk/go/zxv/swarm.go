// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

package zxv

/*
#include "zxv_api.h"
*/
import "C"

import (
	"errors"
	"runtime"
	"unsafe"
)

// Feeling is an emoji of the kernel palette with intensity 0..5 (charge 0,
// 1, 2, 3, 5, 8).
type Feeling struct {
	Emoji     rune
	Intensity uint32
}

// Allotment is one model's share of the current cycle.
type Allotment struct {
	Allotted  uint64 // real + imaginary
	Imaginary uint64 // the emotional part
	Used      uint64
}

// Grant is the outcome of Swarm.Consume.
type Grant struct {
	Requested uint64
	Granted   uint64
}

// Partial reports whether fewer tokens than requested were granted.
func (g Grant) Partial() bool { return g.Granted < g.Requested }

// EmotionProfile is an emotion's behaviour profile.
type EmotionProfile struct {
	Valence          int32
	TemperatureMilli uint32
	ExplorePermille  uint32
	VerifyPasses     uint32
	Priority         uint32
}

// Swarm is the Fibonacci + emotional token economy (zxv_swarm), used
// through a capability with CapSwarm. Safe for concurrent use.
type Swarm struct {
	h   *handle
	cap *Capability
}

// NewSwarm creates a swarm of levels Fibonacci levels sharing
// tokensPerCycle each cycle (cap needs CapSwarm).
func NewSwarm(cap *Capability, levels uint32, tokensPerCycle uint64) (*Swarm, error) {
	var s *C.zxv_swarm
	if err := cap.gated(func(p *C.zxv_cap) C.int32_t {
		return C.zxv_swarm_create_cap(p, C.uint32_t(levels), C.uint64_t(tokensPerCycle), &s)
	}); err != nil {
		return nil, err
	}
	sw := &Swarm{cap: cap}
	sw.h = newHandle("swarm", unsafe.Pointer(s), func(p unsafe.Pointer) { C.zxv_swarm_destroy((*C.zxv_swarm)(p)) }, cap.h)
	runtime.SetFinalizer(sw, func(s *Swarm) { _ = s.h.close() })
	return sw, nil
}

// Close destroys the swarm.
func (s *Swarm) Close() error { return s.h.close() }

func (s *Swarm) gated(f func(cp *C.zxv_cap, p *C.zxv_swarm) C.int32_t) error {
	return s.h.use(func(p unsafe.Pointer) error {
		return s.cap.gated(func(cp *C.zxv_cap) C.int32_t { return f(cp, (*C.zxv_swarm)(p)) })
	})
}

// Register adds a model at level (0-based). Level d holds at most F(d+2)
// models; a full level is REFUSED (ZXV_E_CAPACITY).
func (s *Swarm) Register(modelID, level uint32) error {
	return s.gated(func(cp *C.zxv_cap, p *C.zxv_swarm) C.int32_t {
		return C.zxv_swarm_register(cp, p, C.uint32_t(modelID), C.uint32_t(level))
	})
}

// SetActive marks a model active or idle.
func (s *Swarm) SetActive(modelID uint32, active bool) error {
	a := C.int32_t(0)
	if active {
		a = 1
	}
	return s.gated(func(cp *C.zxv_cap, p *C.zxv_swarm) C.int32_t {
		return C.zxv_swarm_set_active(cp, p, C.uint32_t(modelID), a)
	})
}

// SetRate changes the tokens shared each cycle.
func (s *Swarm) SetRate(tokensPerCycle uint64) error {
	return s.gated(func(cp *C.zxv_cap, p *C.zxv_swarm) C.int32_t {
		return C.zxv_swarm_set_rate(cp, p, C.uint64_t(tokensPerCycle))
	})
}

// SetMood sets the swarm's mood. An emoji outside the palette matches ErrNotFound.
func (s *Swarm) SetMood(f Feeling) error {
	return s.gated(func(cp *C.zxv_cap, p *C.zxv_swarm) C.int32_t {
		return C.zxv_swarm_set_mood(cp, p, C.uint32_t(f.Emoji), C.uint32_t(f.Intensity))
	})
}

// SetFeeling sets one model's feeling.
func (s *Swarm) SetFeeling(modelID uint32, f Feeling) error {
	return s.gated(func(cp *C.zxv_cap, p *C.zxv_swarm) C.int32_t {
		return C.zxv_swarm_set_feeling(cp, p, C.uint32_t(modelID), C.uint32_t(f.Emoji), C.uint32_t(f.Intensity))
	})
}

// BeginCycle starts a cycle and returns its number.
func (s *Swarm) BeginCycle() (uint64, error) {
	var n C.uint64_t
	err := s.gated(func(cp *C.zxv_cap, p *C.zxv_swarm) C.int32_t { return C.zxv_swarm_begin_cycle(cp, p, &n) })
	return uint64(n), err
}

// Consume grants up to requested tokens. A partial grant is not an error:
// check Grant.Partial. The granted tokens are used.
func (s *Swarm) Consume(modelID uint32, requested uint64) (Grant, error) {
	var g C.uint64_t
	err := s.gated(func(cp *C.zxv_cap, p *C.zxv_swarm) C.int32_t {
		return C.zxv_swarm_consume(cp, p, C.uint32_t(modelID), C.uint64_t(requested), &g)
	})
	if errors.Is(err, ErrBudgetExhausted) {
		err = nil
	}
	return Grant{requested, uint64(g)}, err
}

// Allotment returns a model's share of the current cycle.
func (s *Swarm) Allotment(modelID uint32) (Allotment, error) {
	if err := s.cap.ctx.guard(); err != nil {
		return Allotment{}, err
	}
	var a, i, u C.uint64_t
	err := s.h.use(func(p unsafe.Pointer) error {
		return call(func() C.int32_t { return C.zxv_swarm_allotment((*C.zxv_swarm)(p), C.uint32_t(modelID), &a, &i, &u) })
	})
	return Allotment{uint64(a), uint64(i), uint64(u)}, s.cap.ctx.fatal(err)
}

// EndCycle closes the cycle and returns the unused tokens (they never
// carry over).
func (s *Swarm) EndCycle() (uint64, error) {
	var e C.uint64_t
	err := s.gated(func(cp *C.zxv_cap, p *C.zxv_swarm) C.int32_t { return C.zxv_swarm_end_cycle(cp, p, &e) })
	return uint64(e), err
}

// SwarmLevelCapacity returns the capacity of a level: F(level+2).
func SwarmLevelCapacity(level uint32) (uint32, error) {
	var c C.uint32_t
	err := call(func() C.int32_t { return C.zxv_swarm_level_capacity(C.uint32_t(level), &c) })
	return uint32(c), err
}

// SwarmEmotionProfile returns the behaviour profile of a palette emoji.
func SwarmEmotionProfile(emoji rune) (EmotionProfile, error) {
	var v C.int32_t
	var t, x, p, pr C.uint32_t
	err := call(func() C.int32_t { return C.zxv_swarm_emotion_profile(C.uint32_t(emoji), &v, &t, &x, &p, &pr) })
	return EmotionProfile{int32(v), uint32(t), uint32(x), uint32(p), uint32(pr)}, err
}
