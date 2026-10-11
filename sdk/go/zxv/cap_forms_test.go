// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

package zxv_test

import (
	"errors"
	"strings"
	"testing"

	"github.com/sykomiked/ZEDEC-XERO-VULPINE-V1.0.0/sdk/go/zxv"
)

// ABI 1.2: every gated call goes through a capability (the zxv_*_cap forms).

func denied(t *testing.T, what string, err error) {
	t.Helper()
	var ze *zxv.Error
	if !errors.Is(err, zxv.ErrCapabilityDenied) || !errors.As(err, &ze) || ze.Name != "ZXV_E_CAP" {
		t.Fatalf("%s: want ZXV_E_CAP, got %v", what, err)
	}
}

func TestCapFormsNeedTheRightBit(t *testing.T) {
	ctx, root := env(t)
	post := must[*zxv.Capability](t)(root.Attenuate(zxv.CapLedgerPost))
	defer post.Close()
	read := must[*zxv.Capability](t)(root.Attenuate(zxv.CapLedgerRead))
	defer read.Close()

	// LEDGER_POST: currencies and opening accounts.
	ok(t, ctx.EnableCurrencyCap(post, "NGN"))
	ok(t, ctx.EnableCurrencyWithMinorCap(post, "GHS", 2))
	ok(t, ctx.RegisterPrivateCurrencyCap(post, "VFV", 2))
	if got := strings.Join(must[[]string](t)(ctx.EnabledCurrencies()), ","); got != "NGN,GHS,VFV" {
		t.Fatal(got)
	}
	denied(t, "enable", ctx.EnableCurrencyCap(read, "KES"))
	denied(t, "enable minor", ctx.EnableCurrencyWithMinorCap(read, "KES", 2))
	denied(t, "register", ctx.RegisterPrivateCurrencyCap(read, "VFX", 2))
	writer, reader := zxv.NewLedger(post), zxv.NewLedger(read)
	bank := must[zxv.AccountID](t)(writer.OpenAccount("bank", "NGN", zxv.Issuer))
	ada := must[zxv.AccountID](t)(writer.OpenAccount("ada", "NGN", zxv.Holder))
	_, err := reader.OpenAccount("x", "NGN", zxv.Holder)
	denied(t, "open", err)
	must[uint64](t)(writer.Post(bank, ada, 7, "NGN", "R-1"))

	// LEDGER_READ: account metadata, journals and messages.
	if n := must[uint32](t)(reader.AccountCount()); n != 2 {
		t.Fatal(n)
	}
	if a := must[zxv.AccountInfo](t)(reader.Account(ada)); a.Name != "ada" || a.Currency != "NGN" || a.Kind != zxv.Holder {
		t.Fatalf("%+v", a)
	}
	if j := must[[]zxv.JournalLine](t)(reader.Journal(ada)); len(j) != 1 || j[0].Reference != "R-1" {
		t.Fatalf("%+v", j)
	}
	_, err = writer.AccountCount()
	denied(t, "count", err)
	_, err = writer.Account(ada)
	denied(t, "account", err)
	_, err = writer.Journal(ada)
	denied(t, "journal", err)
	_, err = post.NewMessage(zxv.Pacs008)
	denied(t, "message", err)
	m := must[*zxv.Message](t)(read.NewMessage(zxv.Pacs008))
	ok(t, m.SetText(zxv.FieldMsgID, "M-1"))
	// The message re-checks its capability on every call, and keeps it open.
	if err := read.Close(); err == nil {
		t.Fatal("a capability with an open message must not close")
	}
	ok(t, read.SetAttestation(zxv.TruthNeither))
	denied(t, "message call", m.SetText(zxv.FieldMsgID, "M-2"))
	ok(t, m.Close())

	// Handle creation: BUDGET, PHASE, ATTEST, SWARM.
	_, err = zxv.NewBudgets(post)
	denied(t, "budgets", err)
	_, err = zxv.NewPhaseClock(post)
	denied(t, "phase", err)
	_, err = zxv.NewAttestations(post)
	denied(t, "attest", err)
	_, err = zxv.NewSwarm(post, 2, 10)
	denied(t, "swarm", err)
	for _, bit := range []zxv.Caps{zxv.CapBudget, zxv.CapPhase, zxv.CapAttest, zxv.CapSwarm} {
		c := must[*zxv.Capability](t)(root.Attenuate(bit))
		var closer interface{ Close() error }
		switch bit {
		case zxv.CapBudget:
			closer = must[*zxv.Budgets](t)(zxv.NewBudgets(c))
		case zxv.CapPhase:
			closer = must[*zxv.PhaseClock](t)(zxv.NewPhaseClock(c))
		case zxv.CapAttest:
			closer = must[*zxv.Attestations](t)(zxv.NewAttestations(c))
		default:
			closer = must[*zxv.Swarm](t)(zxv.NewSwarm(c, 2, 10))
		}
		ok(t, closer.Close())
		ok(t, c.Close())
	}
}

func TestCapFromAnotherContextIsRefused(t *testing.T) {
	ctx, _ := env(t)
	other, foreign := env(t)
	for what, err := range map[string]error{
		"enable":       ctx.EnableCurrencyCap(foreign, "NGN"),
		"enable minor": ctx.EnableCurrencyWithMinorCap(foreign, "NGN", 2),
		"register":     ctx.RegisterPrivateCurrencyCap(foreign, "VFV", 2),
	} {
		denied(t, what, err)
		if !strings.Contains(err.Error(), "different context") {
			t.Fatalf("%s: %v", what, err)
		}
	}
	if n := len(must[[]string](t)(ctx.EnabledCurrencies())); n != 0 {
		t.Fatal(n)
	}
	if n := len(must[[]string](t)(other.EnabledCurrencies())); n != 0 {
		t.Fatal("nothing leaked into the capability's own context")
	}
}

func TestDeprecatedNoCapFormsStillWork(t *testing.T) {
	ctx, _ := env(t)
	ok(t, ctx.EnableCurrency("NGN"))                        //nolint:staticcheck // the 1.0 form stays supported
	ok(t, ctx.EnableCurrencyWithMinor("GHS", 2))            //nolint:staticcheck
	ok(t, ctx.RegisterPrivateCurrency("VFV", 2))            //nolint:staticcheck
	m := must[*zxv.Message](t)(ctx.NewMessage(zxv.Pacs008)) //nolint:staticcheck
	ok(t, m.Close())
	if got := strings.Join(must[[]string](t)(ctx.EnabledCurrencies()), ","); got != "NGN,GHS,VFV" {
		t.Fatal(got)
	}
}
