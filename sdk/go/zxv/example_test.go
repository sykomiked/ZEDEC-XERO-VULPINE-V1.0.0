// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

package zxv_test

import (
	"context"
	"errors"
	"fmt"
	"time"

	"github.com/sykomiked/ZEDEC-XERO-VULPINE-V1.0.0/sdk/go/zxv"
)

func Example() {
	ctx, _ := zxv.NewContext()
	defer ctx.Close()
	root, _ := ctx.RootCapability(1, nil, zxv.CapAll)
	defer root.Close()
	_ = ctx.EnableCurrencyCap(root, "NGN")
	ledger := zxv.NewLedger(root)
	bank, _ := ledger.OpenAccount("bank", "NGN", zxv.Issuer)
	alice, _ := ledger.OpenAccount("alice", "NGN", zxv.Holder)
	_, _ = ledger.Post(bank, alice, 12550, "NGN", "E2E-0001")
	bal, _ := ledger.Balance(alice)
	fmt.Println(bal.Equity)
	// Output: 12550
}

func ExampleRational() {
	a, _ := zxv.ParseRational("0.1")
	b, _ := zxv.ParseRational("0.2")
	sum, _ := a.Add(b)
	exact, _ := zxv.FromFloat64Exact(0.1)
	fmt.Println(sum, exact)
	// Output: 0.3 3602879701896397/36028797018963968
}

func ExampleTruth() {
	t := zxv.TruthFromEvidence(2, 1)
	fmt.Println(t, t.IsCertain(), t.ToBool(false, false))
	// Output: BOTH false false
}

func ExampleBudget_ConsumeWait() {
	ctx, _ := zxv.NewContext()
	defer ctx.Close()
	cap, _ := ctx.RootCapability(1, nil, zxv.CapBudget)
	defer cap.Close()
	budgets, _ := zxv.NewBudgets(cap)
	defer budgets.Close()
	b, _ := budgets.Create("compute", zxv.ResComputeCycles, zxv.IntRational(2), true)
	_ = b.Consume(zxv.IntRational(2))
	go func() {
		time.Sleep(10 * time.Millisecond)
		_ = b.Release(zxv.IntRational(1))
	}()
	err := b.ConsumeWait(context.Background(), zxv.IntRational(1)) // waits for the release
	fmt.Println(err, errors.Is(b.Consume(zxv.IntRational(1)), zxv.ErrBudgetExhausted))
	// Output: <nil> true
}

func ExampleDeferredEvent_Wait() {
	ctx, _ := zxv.NewContext()
	defer ctx.Close()
	cap, _ := ctx.RootCapability(1, nil, zxv.CapPhase)
	defer cap.Close()
	clock, _ := zxv.NewPhaseClock(cap)
	defer clock.Close()
	ev, _ := clock.Defer(zxv.PhaseCycle)
	go func() { _, _, _ = clock.Advance(zxv.PhaseCycle) }()
	st, _ := ev.Wait(context.Background())
	fmt.Println(st == zxv.EventCleared)
	// Output: true
}

func ExamplePackFrame() {
	tag, _ := zxv.WireTag(3, 2, 1)
	f := zxv.PackFrame(tag, [3]uint32{0x01020304, 0x05060708, 0x090A0B0C}, [2]uint32{0x11121314, 0x15161718})
	fmt.Println(f)
	// Output: 7b040302011112131408070605151617180c0b0a09
}
