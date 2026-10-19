// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

package zxv

/*
#include "zxv_api.h"
*/
import "C"

import (
	"fmt"
)

// Class is the status class of a libzxv status (zxv_status_class).
type Class int

const (
	// ClassOK is ZXV_OK.
	ClassOK Class = 0
	// ClassUsage is a caller bug: fix the arguments; retrying fails again.
	ClassUsage Class = 1
	// ClassRefused is a well-formed request the platform declined; a retry
	// may succeed after the situation changes.
	ClassRefused Class = 2
	// ClassFatal means the context halted after a broken invariant: never
	// retry.
	ClassFatal Class = 3
	// ClassSystem is an environmental failure (memory, I/O, internal).
	ClassSystem Class = 4
)

// String returns the class name.
func (c Class) String() string {
	switch c {
	case ClassOK:
		return "OK"
	case ClassUsage:
		return "USAGE"
	case ClassRefused:
		return "REFUSED"
	case ClassFatal:
		return "FATAL"
	default:
		return "SYSTEM"
	}
}

// ClassOf returns the class of any status code, as the library reports it.
func ClassOf(status int32) Class {
	return Class(C.zxv_status_class(C.int32_t(status)))
}

// StatusName returns the symbolic name of a status ("ZXV_E_FUNDS");
// "ZXV_E_UNKNOWN" for unknown codes.
func StatusName(status int32) string {
	return C.GoString(C.zxv_status_name(C.int32_t(status)))
}

// Status codes, for comparison with Error.Status.
const (
	StatusOK          int32 = 0
	StatusArg         int32 = -1
	StatusBuffer      int32 = -2
	StatusNotImpl     int32 = -3
	StatusNotFound    int32 = -4
	StatusCapacity    int32 = -5
	StatusFunds       int32 = -6
	StatusCurrency    int32 = -7
	StatusRange       int32 = -8
	StatusState       int32 = -9
	StatusInvariant   int32 = -10
	StatusUTF8        int32 = -11
	StatusTooLong     int32 = -12
	StatusDuplicate   int32 = -13
	StatusNoMem       int32 = -14
	StatusDeclined    int32 = -15
	StatusField       int32 = -16
	StatusCap         int32 = -17
	StatusPoisoned    int32 = -18
	StatusBudget      int32 = -19
	StatusRmagInvalid int32 = -20
	StatusVerify      int32 = -21
	StatusFormat      int32 = -22
	StatusIO          int32 = -23
	StatusFloat       int32 = -24
	StatusNoProvider  int32 = -25 // ABI 1.4: no proven implementation may serve the call
	StatusDispute     int32 = -26 // ABI 1.4: a shadow check found two implementations disagree
	StatusQuarantined int32 = -27 // ABI 1.4: image failed in hardware; rolled back, logged
	StatusRollback    int32 = -28 // ABI 1.4: image or contract not newer / below the floor
	StatusBlocked     int32 = -29 // ABI 1.4: image already quarantined; never reloaded
	StatusTarget      int32 = -30 // ABI 1.4: image is for other hardware (USAGE)
	StatusPolicy      int32 = -31 // ABI 1.4: refused by the function's placement policy
	StatusInternal    int32 = -99
)

// Error is every error this package returns from libzxv. Use errors.As to
// get it from any returned error; use errors.Is with the sentinels below to
// branch. Never branch on Detail.
type Error struct {
	// Status is the negative ZXV_E_* code.
	Status int32
	// Name is its symbolic name, e.g. "ZXV_E_BUDGET".
	Name string
	// Detail is the library's diagnostic text (zxv_last_error).
	Detail string
	// Class is the status class.
	Class Class
}

// Error implements the error interface.
func (e *Error) Error() string {
	if e.Detail == "" {
		return fmt.Sprintf("zxv %s: %s (%d)", e.Class, e.Name, e.Status)
	}
	return fmt.Sprintf("zxv %s: %s (%d): %s", e.Class, e.Name, e.Status, e.Detail)
}

// Is makes errors.Is match the class sentinels (ErrUsage, ErrRefused,
// ErrFatal, ErrSystem) and the specific sentinels (ErrBudgetExhausted, ...).
func (e *Error) Is(target error) bool {
	s, ok := target.(*sentinel)
	if !ok {
		return false
	}
	if s.status != 0 {
		return s.status == e.Status
	}
	return s.class == e.Class
}

// UsageError is an error of class USAGE (a caller bug). Err holds the
// status, name and detail.
type UsageError struct{ Err *Error }

// RefusedError is an error of class REFUSED (handle it).
type RefusedError struct{ Err *Error }

// FatalError is an error of class FATAL: the context is halted and every
// later call on it fails.
type FatalError struct{ Err *Error }

// SystemError is an error of class SYSTEM (environmental).
type SystemError struct{ Err *Error }

func (e *UsageError) Error() string   { return e.Err.Error() }
func (e *RefusedError) Error() string { return e.Err.Error() }
func (e *FatalError) Error() string   { return e.Err.Error() }
func (e *SystemError) Error() string  { return e.Err.Error() }

// Unwrap returns the underlying *Error.
func (e *UsageError) Unwrap() error { return e.Err }

// Unwrap returns the underlying *Error.
func (e *RefusedError) Unwrap() error { return e.Err }

// Unwrap returns the underlying *Error.
func (e *FatalError) Unwrap() error { return e.Err }

// Unwrap returns the underlying *Error.
func (e *SystemError) Unwrap() error { return e.Err }

type sentinel struct {
	name   string
	class  Class
	status int32
}

func (s *sentinel) Error() string { return "zxv: " + s.name }

// Class sentinels: errors.Is(err, zxv.ErrRefused).
var (
	// ErrUsage matches every USAGE error.
	ErrUsage error = &sentinel{name: "usage error", class: ClassUsage}
	// ErrRefused matches every REFUSED error.
	ErrRefused error = &sentinel{name: "refused", class: ClassRefused}
	// ErrFatal matches every FATAL error: the context is halted.
	ErrFatal error = &sentinel{name: "fatal: context halted", class: ClassFatal}
	// ErrSystem matches every SYSTEM error.
	ErrSystem error = &sentinel{name: "system error", class: ClassSystem}
)

// Specific sentinels: errors.Is(err, zxv.ErrBudgetExhausted).
var (
	// ErrCapabilityDenied: the capability lacks a bit, is revoked, or its
	// attestation is not TRUE (ZXV_E_CAP).
	ErrCapabilityDenied error = &sentinel{name: "capability denied", status: StatusCap}
	// ErrBudgetExhausted: an rmag or swarm budget is exhausted (ZXV_E_BUDGET).
	ErrBudgetExhausted error = &sentinel{name: "budget exhausted", status: StatusBudget}
	// ErrInsufficientFunds: a holder account would go below zero (ZXV_E_FUNDS).
	ErrInsufficientFunds error = &sentinel{name: "insufficient funds", status: StatusFunds}
	// ErrVerificationFailed: a signature did not verify (ZXV_E_VERIFY).
	ErrVerificationFailed error = &sentinel{name: "verification failed", status: StatusVerify}
	// ErrNotImplemented: the feature is not built into this library (ZXV_E_NOTIMPL).
	ErrNotImplemented error = &sentinel{name: "not implemented", status: StatusNotImpl}
	// ErrInvalidAmount: a malformed or overflowing budget amount
	// (ZXV_E_RMAG_INVALID, USAGE): never retry the same value.
	ErrInvalidAmount error = &sentinel{name: "invalid amount", status: StatusRmagInvalid}
	// ErrFloatRejected: NaN or infinity at the float boundary (ZXV_E_FLOAT).
	ErrFloatRejected error = &sentinel{name: "float rejected", status: StatusFloat}
	// ErrNotFound: unknown account, currency, index (ZXV_E_NOT_FOUND).
	ErrNotFound error = &sentinel{name: "not found", status: StatusNotFound}
	// ErrPoisoned: the context halted earlier (ZXV_E_POISONED, FATAL).
	ErrPoisoned error = &sentinel{name: "context poisoned", status: StatusPoisoned}
	// ErrNoProvider: no proven hardware implementation may serve the call
	// (ZXV_E_NOPROVIDER).
	ErrNoProvider error = &sentinel{name: "no provider", status: StatusNoProvider}
	// ErrDispute: a shadow check found two implementations disagree
	// (ZXV_E_DISPUTE).
	ErrDispute error = &sentinel{name: "implementations disagree", status: StatusDispute}
	// ErrQuarantined: a hardware image failed; rolled back and logged
	// (ZXV_E_QUARANTINED).
	ErrQuarantined error = &sentinel{name: "image quarantined", status: StatusQuarantined}
	// ErrRollback: an image or contract is not newer or is below the floor
	// (ZXV_E_ROLLBACK).
	ErrRollback error = &sentinel{name: "rollback refused", status: StatusRollback}
	// ErrBlocked: the image was quarantined before and is never reloaded
	// (ZXV_E_BLOCKED).
	ErrBlocked error = &sentinel{name: "image blocked", status: StatusBlocked}
	// ErrTarget: the image is for other hardware (ZXV_E_TARGET, USAGE).
	ErrTarget error = &sentinel{name: "wrong target hardware", status: StatusTarget}
	// ErrPolicy: refused by the function's placement policy (ZXV_E_POLICY).
	ErrPolicy error = &sentinel{name: "placement policy", status: StatusPolicy}
	// ErrClosed: the handle was closed (a USAGE error of this SDK).
	ErrClosed error = &sentinel{name: "handle closed", class: ClassUsage}
)

// NewError builds the error value for a status with your own detail text
// (useful in tests and fakes). It is wrapped in the class type.
func NewError(status int32, detail string) error {
	return newError(status, detail)
}

func newError(status int32, detail string) error {
	e := &Error{Status: status, Name: StatusName(status), Detail: detail, Class: ClassOf(status)}
	switch e.Class {
	case ClassUsage:
		return &UsageError{e}
	case ClassRefused:
		return &RefusedError{e}
	case ClassFatal:
		return &FatalError{e}
	default:
		e.Class = ClassSystem
		return &SystemError{e}
	}
}

// closedError is returned by calls on a closed handle.
type closedError struct{ what string }

func (c *closedError) Error() string { return "zxv: " + c.what + " is closed" }
func (c *closedError) Is(t error) bool {
	return t == ErrClosed || t == ErrUsage
}
