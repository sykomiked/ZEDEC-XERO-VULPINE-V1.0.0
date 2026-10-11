// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

// Command budgets streams tokens from the conformance model under a token
// budget, waits on an exhausted compute budget, and drives the phase clock
// in real time.
//
//	go run ./examples/budgets
package main

import (
	"context"
	"fmt"
	"log"
	"path/filepath"
	"runtime"
	"time"

	"github.com/sykomiked/ZEDEC-XERO-VULPINE-V1.0.0/sdk/go/zxv"
)

func main() {
	if err := run(); err != nil {
		log.Fatal(err)
	}
}

func fixtures() string {
	_, file, _, _ := runtime.Caller(0)
	return filepath.Join(filepath.Dir(file), "..", "..", "..", "..", "conformance", "fixtures")
}

func run() error {
	bg := context.Background()
	ctx, err := zxv.NewContext()
	if err != nil {
		return err
	}
	defer ctx.Close()
	cap, err := ctx.RootCapability(1, nil, zxv.CapAIInference|zxv.CapBudget|zxv.CapPhase)
	if err != nil {
		return err
	}
	defer cap.Close()

	budgets, err := zxv.NewBudgets(cap)
	if err != nil {
		return err
	}
	defer budgets.Close()
	budgets.OnExhausted(func(id uint32) { fmt.Printf("[budget %d exhausted]\n", id) })

	// 1. Wait on an exhausted budget until another goroutine releases it.
	compute, err := budgets.Create("compute", zxv.ResComputeCycles, zxv.IntRational(10), true)
	if err != nil {
		return err
	}
	_ = compute.Consume(zxv.IntRational(10))
	go func() {
		time.Sleep(20 * time.Millisecond)
		_ = compute.Release(zxv.IntRational(5))
	}()
	wctx, cancel := context.WithTimeout(bg, time.Second)
	defer cancel()
	if err := compute.ConsumeWait(wctx, zxv.IntRational(4)); err != nil {
		return err
	}
	fmt.Println("got 4 compute cycles after the release")

	// 2. Stream generation under a token budget.
	model, err := zxv.OpenModel(cap, filepath.Join(fixtures(), "tiny-qwen2.gguf"), filepath.Join(fixtures(), "tiny-tokenizer.gguf"))
	if err != nil {
		return err
	}
	defer model.Close()
	session, err := model.NewSession(cap, 256, zxv.KVQ16)
	if err != nil {
		return err
	}
	defer session.Close()
	tokens, err := budgets.Create("tokens", zxv.ResTokens, zxv.IntRational(40), true)
	if err != nil {
		return err
	}
	if err := session.BindBudget(tokens); err != nil {
		return err
	}
	defer session.UnbindBudget()
	temp, _ := zxv.NewRational(7, 10)
	topP, _ := zxv.NewRational(95, 100)
	stream := session.GenerateStream(bg, "Hello, ZXV.", zxv.GenerateOptions{MaxTokens: 64, Temperature: temp, TopK: 40, TopP: topP, Seed: 42})
	for t := range stream.Tokens() {
		fmt.Print(string(t.Piece))
	}
	g, err := stream.Wait()
	if err != nil {
		return err
	}
	used, _ := tokens.Status()
	fmt.Printf("\n[%d tokens, stop %v, budget used %v]\n", len(g.Tokens), g.Stop, used.Used)

	// 3. A deferred S0 event cleared by the real-time driver.
	clock, err := zxv.NewPhaseClock(cap)
	if err != nil {
		return err
	}
	defer clock.Close()
	ev, err := clock.Defer(zxv.PhaseCycle / 100)
	if err != nil {
		return err
	}
	rctx, stop := context.WithCancel(bg)
	go func() { _ = clock.RunRealtime(rctx, 10000) }()
	st, err := ev.Wait(wctx)
	stop()
	if err != nil {
		return err
	}
	now, _ := clock.Now()
	fmt.Printf("deferred event %v at tick >= %d (now %d)\n", st == zxv.EventCleared, ev.DueTick(), now)
	return nil
}
