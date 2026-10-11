// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

package zxv_test

import (
	"context"
	"errors"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"slices"
	"strings"
	"sync"
	"sync/atomic"
	"testing"
	"time"

	"github.com/sykomiked/ZEDEC-XERO-VULPINE-V1.0.0/sdk/go/zxv"
)

func TestFatalPathPoisonsContextAndHandles(t *testing.T) {
	ctx, cap := env(t)
	ok(t, ctx.EnableCurrencyWithMinorCap(cap, "NGN", 2))
	l := zxv.NewLedger(cap)
	bank := must[zxv.AccountID](t)(l.OpenAccount("bank", "NGN", zxv.Issuer))
	alice := must[zxv.AccountID](t)(l.OpenAccount("alice", "NGN", zxv.Holder))
	must[uint64](t)(l.Post(bank, alice, 500, "NGN", "r1"))

	ok(t, ctx.Halt("test halt"))
	if !ctx.Poisoned() {
		t.Fatal("poisoned")
	}
	_, err := l.Post(bank, alice, 1, "NGN", "r2")
	var fe *zxv.FatalError
	if !errors.Is(err, zxv.ErrFatal) || !errors.As(err, &fe) || fe.Err.Name != "ZXV_E_POISONED" {
		t.Fatalf("post after halt: %v", err)
	}
	if _, err := l.Balance(alice); !errors.Is(err, zxv.ErrFatal) {
		t.Fatal(err)
	}
	if _, err := cap.Attenuate(zxv.CapLedgerRead); !errors.Is(err, zxv.ErrFatal) {
		t.Fatal(err)
	}
	if err := ctx.EnableCurrencyCap(cap, "USD"); !errors.Is(err, zxv.ErrFatal) {
		t.Fatal(err)
	}
	if _, err := zxv.NewBudgets(cap); !errors.Is(err, zxv.ErrFatal) {
		t.Fatal(err)
	}
	// Pure functions keep working.
	if r := must[zxv.Rational](t)(zxv.NewRational(1, 2)); r.String() != "0.5" {
		t.Fatal(r)
	}
}

func TestFatalExitPolicy(t *testing.T) {
	if os.Getenv("ZXV_EXIT_CHILD") == "1" {
		ctx, err := zxv.NewContext(zxv.WithFatalPolicy(zxv.FatalExit))
		if err != nil {
			os.Exit(1)
		}
		cap, _ := ctx.RootCapability(1, nil, zxv.CapAll)
		_ = ctx.Halt("exit test")
		_ = cap.Check(zxv.CapLedgerRead)
		os.Exit(3) // unreachable with FatalExit
	}
	cmd := exec.Command(os.Args[0], "-test.run=^TestFatalExitPolicy$")
	cmd.Env = append(os.Environ(), "ZXV_EXIT_CHILD=1")
	out, err := cmd.CombinedOutput()
	var ee *exec.ExitError
	if !errors.As(err, &ee) || ee.ExitCode() != 70 {
		t.Fatalf("want exit 70, got %v: %s", err, out)
	}
	if !strings.Contains(string(out), "FatalExit") {
		t.Fatalf("no log line: %s", out)
	}
}

func TestCapabilityDenialAttenuationRevocation(t *testing.T) {
	ctx, root := env(t)
	ro := must[*zxv.Capability](t)(root.Attenuate(zxv.CapLedgerRead))
	defer ro.Close()
	ok(t, ro.Check(zxv.CapLedgerRead))
	err := ro.Check(zxv.CapLedgerPost)
	if !errors.Is(err, zxv.ErrCapabilityDenied) || !errors.Is(err, zxv.ErrRefused) {
		t.Fatalf("%v", err)
	}
	wider := must[*zxv.Capability](t)(ro.Attenuate(zxv.CapAll))
	if err := wider.Check(zxv.CapLedgerPost); !errors.Is(err, zxv.ErrCapabilityDenied) {
		t.Fatal("attenuation must never widen")
	}
	ok(t, wider.Close())

	ok(t, root.SetAttestation(zxv.TruthBoth))
	err = root.Check(zxv.CapLedgerRead)
	var e *zxv.Error
	if !errors.As(err, &e) || !strings.Contains(e.Detail, "contradictory") {
		t.Fatalf("detail: %v", err)
	}
	ok(t, root.SetAttestation(zxv.TruthTrue))
	info := must[zxv.CapabilityInfo](t)(root.Info())
	if info.Caps != zxv.CapAll || info.Attestation != zxv.TruthTrue || info.ProcessID != 1 {
		t.Fatalf("%+v", info)
	}

	ok(t, ctx.EnableCurrencyWithMinorCap(root, "NGN", 2))
	l, rl := zxv.NewLedger(root), zxv.NewLedger(ro)
	a := must[zxv.AccountID](t)(l.OpenAccount("issuer", "NGN", zxv.Issuer))
	b := must[zxv.AccountID](t)(l.OpenAccount("alice", "NGN", zxv.Holder))
	must[uint64](t)(l.Post(a, b, 500, "NGN", "r1"))
	if _, err := rl.Post(a, b, 1, "NGN", "r2"); !errors.Is(err, zxv.ErrCapabilityDenied) {
		t.Fatal(err)
	}
	if _, err := rl.OpenAccount("x", "NGN", zxv.Holder); !errors.Is(err, zxv.ErrCapabilityDenied) {
		t.Fatal(err)
	}
	if bal := must[zxv.Balance](t)(rl.Balance(b)); bal.Equity != 500 {
		t.Fatal(bal)
	}
	ok(t, root.Revoke())
	if err := ro.Check(zxv.CapLedgerRead); !errors.Is(err, zxv.ErrCapabilityDenied) {
		t.Fatal("revoking the parent revokes the child")
	}
	if _, err := ctx.RootCapability(1, nil, zxv.Caps(1<<40)); !errors.Is(err, zxv.ErrUsage) {
		t.Fatal(err)
	}
}

func TestCloseOrderIsEnforced(t *testing.T) {
	ctx := must[*zxv.Context](t)(zxv.NewContext())
	cap := must[*zxv.Capability](t)(ctx.RootCapability(1, nil, zxv.CapAll))
	if err := ctx.Close(); err == nil || !errors.Is(err, zxv.ErrRefused) {
		t.Fatalf("closing a context with an open capability: %v", err)
	}
	ok(t, cap.Close())
	ok(t, cap.Close()) // idempotent
	if err := cap.Check(zxv.CapLedgerRead); !errors.Is(err, zxv.ErrClosed) {
		t.Fatalf("use after close: %v", err)
	}
	ok(t, ctx.Close())
}

func TestLedgerAndCurrencies(t *testing.T) {
	ctx, cap := env(t)
	ok(t, ctx.EnableCurrencyCap(cap, "NGN"))
	ok(t, ctx.RegisterPrivateCurrencyCap(cap, "VFV", 2))
	if c := must[zxv.EnabledCurrency](t)(ctx.Currency("VFV")); c.Flags&zxv.CcyPrivate == 0 {
		t.Fatal("VFV private")
	}
	if cs := must[[]string](t)(ctx.EnabledCurrencies()); !slices.Equal(cs, []string{"NGN", "VFV"}) {
		t.Fatal(cs)
	}
	l := zxv.NewLedger(cap)
	bank := must[zxv.AccountID](t)(l.OpenAccount("bank", "NGN", zxv.Issuer))
	alice := must[zxv.AccountID](t)(l.OpenAccount("alice", "NGN", zxv.Holder))
	bob := must[zxv.AccountID](t)(l.OpenAccount("bob", "NGN", zxv.Holder))
	must[uint64](t)(l.Post(bank, alice, 10000, "NGN", "E1"))
	must[uint64](t)(l.Post(alice, bob, 2500, "NGN", "E2"))
	if _, err := l.Post(bob, alice, 1000000, "NGN", "E3"); !errors.Is(err, zxv.ErrInsufficientFunds) {
		t.Fatal(err)
	}
	if _, err := l.Post(alice, bob, 1, "NGN", "E2"); statusOf(err) != zxv.StatusDuplicate {
		t.Fatal(err)
	}
	if b := must[zxv.Balance](t)(l.Balance(alice)); b != (zxv.Balance{Debit: 10000, Credit: 2500, Equity: 7500}) {
		t.Fatal(b)
	}
	ok(t, l.Check())
	if info := must[zxv.AccountInfo](t)(l.Account(alice)); info != (zxv.AccountInfo{Name: "alice", Currency: "NGN", Kind: zxv.Holder}) {
		t.Fatal(info)
	}
	if n := must[uint32](t)(l.AccountCount()); n != 3 {
		t.Fatal(n)
	}
	j := must[[]zxv.JournalLine](t)(l.Journal(alice))
	if len(j) != 2 || j[0].Amount != 10000 || j[0].Counterparty != bank || j[1].Amount != -2500 || j[0].Reference != "E1" {
		t.Fatalf("%+v", j)
	}
}

func TestISOReferenceDataAndMessages(t *testing.T) {
	ngn := must[zxv.Currency](t)(zxv.CurrencyByAlpha("NGN"))
	if ngn.Numeric != 566 || ngn.MinorUnits != 2 {
		t.Fatal(ngn)
	}
	if c := must[zxv.Currency](t)(zxv.CurrencyByNumeric(566)); c.Alpha != "NGN" {
		t.Fatal(c)
	}
	if _, err := zxv.CurrencyByAlpha("VFV"); !errors.Is(err, zxv.ErrNotFound) {
		t.Fatal(err)
	}
	if _, err := zxv.CurrencyByNumeric(777); !errors.Is(err, zxv.ErrNotFound) {
		t.Fatal(err)
	}
	if n := must[string](t)(zxv.CurrencyName("NGN")); n != "Naira" {
		t.Fatal(n)
	}
	if all := must[[]string](t)(zxv.Currencies()); len(all) < 100 {
		t.Fatal(len(all))
	}
	must[string](t)(zxv.ISO4217Published())
	if c := must[string](t)(zxv.CurrencyCaveat(777)); c == "" {
		t.Fatal("caveat")
	}
	if !must[bool](t)(zxv.IsAUMember("NG")) {
		t.Fatal("NG is AU")
	}
	if n := must[uint16](t)(zxv.RailCredit.Numeric()); n != 777 {
		t.Fatal(n)
	}
	if !must[bool](t)(zxv.Dragon.CheckDigitValid("4111111111111111")) {
		t.Fatal("luhn")
	}

	_, cap := env(t)
	m := must[*zxv.Message](t)(cap.NewMessage(zxv.Pacs008))
	defer m.Close()
	for f, v := range map[zxv.TextField]string{
		zxv.FieldMsgID: "MSG-1", zxv.FieldCreDtTm: "2026-10-11T12:00:00Z", zxv.FieldDebtorName: "Ada & Co",
		zxv.FieldDebtorAccount: "NG001", zxv.FieldCreditorName: "Bola", zxv.FieldCreditorAccount: "NG002",
		zxv.FieldEndToEndID: "E2E-1", zxv.FieldCurrency: "NGN",
	} {
		ok(t, m.SetText(f, v))
	}
	ok(t, m.SetAmount(zxv.AmountSettlement, 12550, 2))
	xml := must[string](t)(m.Render())
	if !strings.Contains(xml, "125.50") || !strings.Contains(xml, "Ada &amp; Co") {
		t.Fatal(xml)
	}
	if _, err := cap.NewMessage(zxv.Pacs002); !errors.Is(err, zxv.ErrNotImplemented) {
		t.Fatal(err)
	}
}

func TestAttestationsHoldContradictions(t *testing.T) {
	_, cap := env(t)
	reg := must[*zxv.Attestations](t)(zxv.NewAttestations(cap))
	defer reg.Close()
	i := must[uint32](t)(reg.Attest("model.gguf", 6, zxv.TruthTrue, "loader", make([]byte, 32)))
	if !must[bool](t)(reg.GateClear()) {
		t.Fatal("gate")
	}
	ok(t, reg.Evidence(i, true))
	ok(t, reg.Evidence(i, false))
	if st := must[zxv.AttestationState](t)(reg.State(i)); st != (zxv.AttestationState{State: zxv.TruthBoth, Positive: 2, Negative: 1}) {
		t.Fatal(st)
	}
	if must[bool](t)(reg.GateClear()) {
		t.Fatal("BOTH blocks the gate")
	}
	if must[uint32](t)(reg.Find("model.gguf")) != i {
		t.Fatal("find")
	}
}

func TestBudgetExhaustionAndInvalid(t *testing.T) {
	_, cap := env(t)
	bs := must[*zxv.Budgets](t)(zxv.NewBudgets(cap))
	defer bs.Close()
	var denied atomic.Int32
	bs.OnExhausted(func(uint32) { denied.Add(1) })
	b := must[*zxv.Budget](t)(bs.Create("tokens", zxv.ResTokens, zxv.IntRational(10), true))
	ok(t, b.Consume(rat(t, [2]int64{7, 2})))
	ok(t, b.Consume(rat(t, [2]int64{13, 2})))
	err := b.Consume(rat(t, [2]int64{1, 3}))
	if !errors.Is(err, zxv.ErrBudgetExhausted) || !errors.Is(err, zxv.ErrRefused) || denied.Load() != 1 {
		t.Fatalf("%v (denied %d)", err, denied.Load())
	}
	if err := b.Consume(zxv.IntRational(-1)); !errors.Is(err, zxv.ErrInvalidAmount) || !errors.Is(err, zxv.ErrUsage) {
		t.Fatal(err)
	}
	st := must[zxv.BudgetStatus](t)(b.Status())
	if !st.Remaining.Equal(zxv.RatZero) || !st.Used.Equal(zxv.IntRational(10)) || st.Epoch != 0 {
		t.Fatalf("%+v", st)
	}
	ok(t, b.Release(rat(t, [2]int64{1, 2})))
	if st := must[zxv.BudgetStatus](t)(b.Status()); !st.Remaining.Equal(rat(t, [2]int64{1, 2})) || st.Epoch != 1 {
		t.Fatalf("%+v", st)
	}
	// Creating a budget set needs CapBudget (ABI 1.2), and so does using it.
	ro := must[*zxv.Capability](t)(cap.Attenuate(zxv.CapLedgerRead))
	defer ro.Close()
	if _, err := zxv.NewBudgets(ro); !errors.Is(err, zxv.ErrCapabilityDenied) {
		t.Fatal(err)
	}
	bc := must[*zxv.Capability](t)(cap.Attenuate(zxv.CapBudget))
	defer bc.Close()
	nb := must[*zxv.Budgets](t)(zxv.NewBudgets(bc))
	defer nb.Close()
	ok(t, bc.Revoke())
	if _, err := nb.Create("x", zxv.ResEnergy, zxv.RatOne, true); !errors.Is(err, zxv.ErrCapabilityDenied) {
		t.Fatal(err)
	}
}

func TestConsumeWaitAsync(t *testing.T) {
	_, cap := env(t)
	bs := must[*zxv.Budgets](t)(zxv.NewBudgets(cap))
	defer bs.Close()
	b := must[*zxv.Budget](t)(bs.Create("compute", zxv.ResComputeCycles, zxv.IntRational(5), true))
	ok(t, b.Consume(zxv.IntRational(5)))
	done := b.ConsumeAsync(context.Background(), zxv.IntRational(3))
	select {
	case err := <-done:
		t.Fatalf("must wait while exhausted: %v", err)
	case <-time.After(50 * time.Millisecond):
	}
	ok(t, b.Release(zxv.IntRational(1))) // not enough: it retries and waits again
	select {
	case err := <-done:
		t.Fatalf("still not enough: %v", err)
	case <-time.After(50 * time.Millisecond):
	}
	ok(t, b.SetLimit(zxv.IntRational(7))) // a larger limit replenishes
	select {
	case err := <-done:
		ok(t, err)
	case <-time.After(2 * time.Second):
		t.Fatal("not woken")
	}
	// Timeout and cancellation.
	tctx, cancel := context.WithTimeout(context.Background(), 30*time.Millisecond)
	defer cancel()
	if err := b.ConsumeWait(tctx, zxv.IntRational(5)); !errors.Is(err, context.DeadlineExceeded) {
		t.Fatal(err)
	}
	// INVALID fails at once.
	if err := b.ConsumeWait(context.Background(), zxv.IntRational(-2)); !errors.Is(err, zxv.ErrInvalidAmount) {
		t.Fatal(err)
	}
}

func TestPhaseDeferralManualDriver(t *testing.T) {
	_, cap := env(t)
	pc := must[*zxv.PhaseClock](t)(zxv.NewPhaseClock(cap))
	defer pc.Close()
	ev := must[*zxv.DeferredEvent](t)(pc.Defer(zxv.PhaseCycle))
	if due, has, err := pc.NextDue(); err != nil || !has || due != zxv.PhaseCycle {
		t.Fatal(due, has, err)
	}
	now, cleared, err := pc.Advance(zxv.PhaseCycle - 1)
	ok(t, err)
	if now != zxv.PhaseCycle-1 || cleared != 0 {
		t.Fatal(now, cleared)
	}
	res := make(chan zxv.EventState, 1)
	go func() {
		st, _ := ev.Wait(context.Background())
		res <- st
	}()
	select {
	case <-res:
		t.Fatal("S0 one tick early")
	case <-time.After(30 * time.Millisecond):
	}
	now, cleared, err = pc.Advance(1)
	ok(t, err)
	if now != 27720 || cleared != 1 {
		t.Fatal(now, cleared)
	}
	if st := <-res; st != zxv.EventCleared {
		t.Fatal(st)
	}
	if _, has, _ := pc.NextDue(); has {
		t.Fatal("nothing pending")
	}
	ev2 := must[*zxv.DeferredEvent](t)(pc.After(10))
	ok(t, ev2.Cancel())
	if st, err := ev2.Wait(context.Background()); err != nil || st != zxv.EventCancelled {
		t.Fatal(st, err)
	}
	if err := ev2.Cancel(); statusOf(err) != zxv.StatusState {
		t.Fatal(err)
	}
	if _, err := pc.Defer(1); !errors.Is(err, zxv.ErrUsage) {
		t.Fatal(err)
	}
	ev3 := must[*zxv.DeferredEvent](t)(pc.After(5))
	cctx, cancel := context.WithCancel(context.Background())
	cancel()
	if _, err := ev3.Wait(cctx); !errors.Is(err, context.Canceled) {
		t.Fatal(err)
	}

	if d, tok, err := pc.Admit(6, 1, zxv.StepS0Pending); err != nil || d != zxv.Defer || tok != 0 {
		t.Fatal(d, tok, err)
	}
	ok(t, pc.SetHealth(6, zxv.HealthHealthy))
	if d, tok, err := pc.Admit(6, 2, zxv.StepRequiresHealth); err != nil || d != zxv.Admit || tok == 0 {
		t.Fatal(d, tok, err)
	}
	ro := must[*zxv.Capability](t)(cap.Attenuate(zxv.CapLedgerRead))
	defer ro.Close()
	if _, err := zxv.NewPhaseClock(ro); !errors.Is(err, zxv.ErrCapabilityDenied) {
		t.Fatal(err)
	}
	ph := must[*zxv.Capability](t)(cap.Attenuate(zxv.CapPhase))
	defer ph.Close()
	pc2 := must[*zxv.PhaseClock](t)(zxv.NewPhaseClock(ph))
	defer pc2.Close()
	ok(t, ph.Revoke())
	if _, _, err := pc2.Advance(1); !errors.Is(err, zxv.ErrCapabilityDenied) {
		t.Fatal(err)
	}
}

func TestPhaseRealtimeDriver(t *testing.T) {
	_, cap := env(t)
	pc := must[*zxv.PhaseClock](t)(zxv.NewPhaseClock(cap))
	defer pc.Close()
	ev := must[*zxv.DeferredEvent](t)(pc.Defer(50))
	rctx, stop := context.WithCancel(context.Background())
	var wg sync.WaitGroup
	wg.Add(1)
	go func() {
		defer wg.Done()
		if err := pc.RunRealtime(rctx, 1000); err != nil {
			t.Error(err)
		}
	}()
	wctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	st, err := ev.Wait(wctx)
	stop()
	wg.Wait()
	if err != nil || st != zxv.EventCleared {
		t.Fatal(st, err)
	}
}

func TestEngineCallbackStreamBudgetLoRA(t *testing.T) {
	if !zxv.LiveFeatures().Has(zxv.FeatTensor) {
		t.Skip("engine not built")
	}
	greedy := loadVectors(t).Generate.Greedy.Tokens
	_, cap := env(t)
	noai := must[*zxv.Capability](t)(cap.Attenuate(zxv.CapLedgerRead))
	defer noai.Close()
	mp, tp := filepath.Join(fixtures(), "tiny-qwen2.gguf"), filepath.Join(fixtures(), "tiny-tokenizer.gguf")
	if _, err := zxv.OpenModel(noai, mp, tp); !errors.Is(err, zxv.ErrCapabilityDenied) {
		t.Fatal(err)
	}
	if _, err := zxv.OpenModel(cap, "/nonexistent.gguf", ""); !errors.Is(err, zxv.ErrSystem) || statusOf(err) != zxv.StatusIO {
		t.Fatal(err)
	}
	m := must[*zxv.Model](t)(zxv.OpenModel(cap, mp, tp))
	defer m.Close()
	if n := must[int64](t)(m.Info(zxv.InfoVocab)); n != 1200 {
		t.Fatal(n)
	}
	prompt := string(must[[]byte](t)(os.ReadFile(filepath.Join(fixtures(), "prompt.txt"))))
	bg := context.Background()

	mem := must[*zxv.Model](t)(zxv.OpenModelMemory(cap, must[[]byte](t)(os.ReadFile(mp)), must[[]byte](t)(os.ReadFile(tp))))
	ms := must[*zxv.Session](t)(mem.NewSession(cap, 128, zxv.KVQ16))
	if g := must[*zxv.Generation](t)(ms.Generate(bg, prompt, zxv.Greedy(16))); !slices.Equal(g.Tokens, greedy) {
		t.Fatal("from memory")
	}
	if err := mem.Close(); err == nil {
		t.Fatal("model close must fail while a session is open")
	}
	ok(t, ms.Close())
	ok(t, mem.Close())

	s := must[*zxv.Session](t)(m.NewSession(cap, 128, zxv.KVQ16))
	defer s.Close()
	// Callback stop.
	var seen []int32
	g := must[*zxv.Generation](t)(s.GenerateFunc(bg, prompt, zxv.Greedy(16), func(tk zxv.Token) bool {
		seen = append(seen, tk.ID)
		return len(seen) < 3
	}))
	if g.Stop != zxv.StopCallback || !slices.Equal(seen, greedy[:3]) || !slices.Equal(g.Tokens, greedy[:3]) {
		t.Fatal(g.Stop, seen)
	}
	// Streaming via a channel.
	ok(t, s.Reset())
	st := s.GenerateStream(bg, prompt, zxv.Greedy(16))
	var ids []int32
	for tk := range st.Tokens() {
		ids = append(ids, tk.ID)
	}
	g = must[*zxv.Generation](t)(st.Wait())
	if !slices.Equal(ids, greedy) || g.Stop != zxv.StopMax {
		t.Fatal(ids, g.Stop)
	}
	// Closing a stream early stops generation.
	ok(t, s.Reset())
	st = s.GenerateStream(bg, prompt, zxv.Greedy(16))
	first := <-st.Tokens()
	st.Close()
	g, err := st.Wait()
	ok(t, err)
	if first.ID != greedy[0] || len(g.Tokens) >= 16 || g.Stop != zxv.StopCallback {
		t.Fatal(first, len(g.Tokens), g.Stop)
	}
	// Context cancellation stops generation and reports ctx.Err().
	ok(t, s.Reset())
	cctx, cancel := context.WithCancel(bg)
	n := 0
	g, err = s.GenerateFunc(cctx, prompt, zxv.Greedy(16), func(zxv.Token) bool {
		n++
		if n == 2 {
			cancel()
		}
		return true
	})
	if !errors.Is(err, context.Canceled) || g == nil || len(g.Tokens) != 3 {
		t.Fatalf("cancel: %v %v", err, g)
	}
	// A panic in the callback is re-raised on the caller.
	ok(t, s.Reset())
	func() {
		defer func() {
			if recover() == nil {
				t.Fatal("panic not propagated")
			}
		}()
		_, _ = s.GenerateFunc(bg, prompt, zxv.Greedy(4), func(zxv.Token) bool { panic("boom") })
	}()

	// Budget binding.
	bs := must[*zxv.Budgets](t)(zxv.NewBudgets(cap))
	nPrompt := len(must[[]int32](t)(m.Tokenize(prompt, true)))
	b := must[*zxv.Budget](t)(bs.Create("gen", zxv.ResTokens, zxv.IntRational(int32(nPrompt+8)), true))
	ok(t, s.Reset())
	ok(t, s.BindBudget(b))
	if err := bs.Close(); err == nil {
		t.Fatal("a bound budget set must not close")
	}
	g = must[*zxv.Generation](t)(s.Generate(bg, prompt, zxv.Greedy(16)))
	if g.Stop != zxv.StopBudget || len(g.Tokens) != 8 {
		t.Fatal(g.Stop, len(g.Tokens))
	}
	ok(t, s.Reset())
	if _, err := s.Generate(bg, prompt, zxv.Greedy(16)); !errors.Is(err, zxv.ErrBudgetExhausted) {
		t.Fatal(err)
	}
	ok(t, s.UnbindBudget())
	ok(t, bs.Close())
	ok(t, s.Reset())
	if g := must[*zxv.Generation](t)(s.Generate(bg, prompt, zxv.Greedy(16))); !slices.Equal(g.Tokens, greedy) {
		t.Fatal("after unbind")
	}

	// Concurrent sessions over one model.
	var wg sync.WaitGroup
	for i := 0; i < 4; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			ss, err := m.NewSession(cap, 128, zxv.KVQ8)
			if err != nil {
				t.Error(err)
				return
			}
			defer ss.Close()
			if _, err := ss.Generate(bg, prompt, zxv.Greedy(4)); err != nil {
				t.Error(err)
			}
		}()
	}
	wg.Wait()

	if zxv.LiveFeatures().Has(zxv.FeatLoRA) {
		t.Fatal("LoRA reported built")
	}
	if err := m.LoadLoRA(cap, "x", zxv.RatOne); !errors.Is(err, zxv.ErrNotImplemented) {
		t.Fatal(err)
	}
	s2 := must[*zxv.Session](t)(m.NewSession(noai, 128, zxv.KVQ16))
	defer s2.Close()
	if _, err := s2.Generate(bg, prompt, zxv.Greedy(1)); !errors.Is(err, zxv.ErrCapabilityDenied) {
		t.Fatal(err)
	}
}

func TestSecretsAndSigning(t *testing.T) {
	_, cap := env(t)
	kem := must[*zxv.Capability](t)(cap.Attenuate(zxv.CapCryptoKEM))
	defer kem.Close()
	sk := zxv.SecretKeyFromBytes(make([]byte, 32))
	if _, err := zxv.NewEd25519(kem).Sign(sk, []byte("x")); !errors.Is(err, zxv.ErrCapabilityDenied) {
		t.Fatal(err)
	}
	if sk.String() != "SecretKey(32 bytes, redacted)" {
		t.Fatal(sk.String())
	}
	sk.Close()
	if _, err := sk.ExportBytesUnsafe(); !errors.Is(err, zxv.ErrClosed) {
		t.Fatal("use after wipe")
	}
	ed := zxv.NewEd25519(cap)
	pk, sk2, err := ed.KeyGen()
	ok(t, err)
	defer sk2.Close()
	sig := must[[64]byte](t)(ed.Sign(sk2, []byte("hello")))
	ok(t, ed.Verify(pk, []byte("hello"), sig))
	if err := zxv.NewMLDSA87(cap).Verify(make([]byte, 3), nil, nil, make([]byte, 3)); !errors.Is(err, zxv.ErrUsage) {
		t.Fatal(err)
	}
	ek, dk, err := zxv.NewMLKEM1024(cap).KeyGen()
	ok(t, err)
	defer dk.Close()
	ct, a, err := zxv.NewMLKEM1024(kem).Encapsulate(ek)
	ok(t, err)
	defer a.Close()
	b := must[*zxv.SecretKey](t)(zxv.NewMLKEM1024(kem).Decapsulate(dk, ct))
	defer b.Close()
	if !a.Equal(b) {
		t.Fatal("shared secrets differ")
	}
	r1, r2 := make([]byte, 32), make([]byte, 32)
	ok(t, zxv.Random(r1))
	ok(t, zxv.Random(r2))
	if slices.Equal(r1, r2) {
		t.Fatal("random")
	}
}

func TestRationalAndTruthSemantics(t *testing.T) {
	a := must[zxv.Rational](t)(zxv.ParseRational("0.1"))
	b := must[zxv.Rational](t)(zxv.ParseRational("0.2"))
	if s := must[zxv.Rational](t)(a.Add(b)); s.String() != "0.3" {
		t.Fatal(s)
	}
	max := must[zxv.Rational](t)(zxv.NewRational(1<<62, 1))
	if _, err := max.Mul(zxv.IntRational(4)); statusOf(err) != zxv.StatusRange {
		t.Fatal(err)
	}
	if _, err := zxv.RatOne.Div(zxv.RatZero); statusOf(err) != zxv.StatusArg {
		t.Fatal(err)
	}
	third := must[zxv.Rational](t)(zxv.NewRational(1, 3))
	if third.Cmp(must[zxv.Rational](t)(zxv.ParseRational("0.333333"))) != 1 {
		t.Fatal("cmp")
	}
	if third.String() != "1/3" {
		t.Fatal(third)
	}
	if zxv.TruthBoth.ToBool(false, false) || !zxv.TruthNeither.ToBool(false, true) {
		t.Fatal("ToBool")
	}
	if zxv.TruthTrue.RequireTrue() != nil || zxv.TruthBoth.RequireTrue() == nil {
		t.Fatal("RequireTrue")
	}
	var zero zxv.Rational
	if !zero.Equal(zxv.RatZero) || zero.String() != "0" {
		t.Fatal("zero value")
	}
}

func TestFinalizersBackstop(t *testing.T) {
	for i := 0; i < 50; i++ {
		ctx, _ := zxv.NewContext()
		cap, _ := ctx.RootCapability(1, nil, zxv.CapAll)
		_, _ = zxv.NewBudgets(cap)
		_ = zxv.SecretKeyFromBytes([]byte("secret"))
	}
	runtime.GC()
	runtime.GC()
	runtime.GC()
}
