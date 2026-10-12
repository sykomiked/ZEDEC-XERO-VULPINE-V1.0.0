// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

// Package zxv is the Go SDK of the ZXV platform: a cgo binding to libzxv
// (sdk/libzxv/include/zxv_api.h, ABI 1.4).
//
// The essentials:
//
//   - Handles (Context, Capability, Budgets, PhaseClock, Swarm,
//     Attestations, Model, Session, Message, SecretKey, QuarantineLog,
//     HwProfile, HwMatrix, HwRegion) have Close(), with a
//     runtime.SetFinalizer backstop. A parent refuses to close while a child
//     made from it is open, so the C rule "destroy children first" holds.
//   - Errors are values: errors.Is(err, zxv.ErrBudgetExhausted), the class
//     sentinels ErrUsage, ErrRefused, ErrFatal, ErrSystem, and errors.As to
//     *zxv.Error for Status, Name and Detail.
//   - A FATAL status poisons the context: every later call fails with
//     ErrFatal. WithFatalPolicy(FatalExit) exits the process instead.
//   - Numbers are exact: Rational and integers. The only float input is
//     FromFloat64Exact.
//   - Truth is four-valued and is not a bool.
//   - Budget waits, phase waits and generation take a context.Context.
//
// Quickstart:
//
//	ctx, _ := zxv.NewContext()
//	defer ctx.Close()
//	root, _ := ctx.RootCapability(1, nil, zxv.CapAll)
//	defer root.Close()
//	_ = ctx.EnableCurrency("NGN")
//	ledger := zxv.NewLedger(root)
//	bank, _ := ledger.OpenAccount("bank", "NGN", zxv.Issuer)
//	alice, _ := ledger.OpenAccount("alice", "NGN", zxv.Holder)
//	_, _ = ledger.Post(bank, alice, 12550, "NGN", "E2E-0001")
package zxv

/*
#include "zxv_api.h"
*/
import "C"

import (
	"fmt"
	"sync"
)

// Version returns the ABI version of the loaded library.
func Version() (major, minor, patch uint32) {
	var a, b, c C.uint32_t
	C.zxv_version(&a, &b, &c)
	return uint32(a), uint32(b), uint32(c)
}

// Features is the bit mask of feature groups live in the loaded library.
type Features uint32

// Feature bits.
const (
	FeatLedger    Features = 0x00000001
	FeatPacs008   Features = 0x00000002
	FeatCamt053   Features = 0x00000004
	FeatISO4217   Features = 0x00000008
	FeatNetting   Features = 0x00000200
	FeatCards     Features = 0x00000400
	FeatCardCheck Features = 0x00000800
	FeatRational  Features = 0x00002000
	FeatLPRes     Features = 0x00004000
	FeatCaps      Features = 0x00008000
	FeatRMAG      Features = 0x00010000
	FeatPhase     Features = 0x00020000
	FeatWire      Features = 0x00040000
	FeatSwarm     Features = 0x00080000
	FeatHash      Features = 0x00100000
	FeatMLKEM1024 Features = 0x00200000
	FeatMLDSA87   Features = 0x00400000
	FeatEd25519   Features = 0x00800000
	FeatTensor    Features = 0x01000000
	FeatLoRA      Features = 0x02000000 // not built yet
	FeatEVM       Features = 0x04000000
	FeatHex       Features = 0x08000000 // ABI 1.3: Hexagram, QuarantineLog
	FeatHW        Features = 0x10000000 // ABI 1.4: HwProfile, HwMatrix, HwRegion
)

// Has reports whether every bit of f is live.
func (m Features) Has(f Features) bool { return m&f == f }

// LiveFeatures returns the feature groups live in the loaded library.
func LiveFeatures() Features {
	var m C.uint32_t
	C.zxv_features(&m)
	return Features(m)
}

var (
	versionOnce sync.Once
	versionErr  error
)

// checkVersion refuses a library whose major ABI differs or whose minor is
// below 4.
func checkVersion() error {
	versionOnce.Do(func() {
		major, minor, _ := Version()
		if major != C.ZXV_ABI_VERSION_MAJOR || minor < 4 {
			versionErr = newError(StatusInternal, fmt.Sprintf("libzxv ABI %d.%d is not compatible with this SDK (needs 1.4+)", major, minor))
		}
	})
	return versionErr
}
