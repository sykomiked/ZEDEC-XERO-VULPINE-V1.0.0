// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

package zxv_test

import (
	"bytes"
	"errors"
	"slices"
	"testing"

	"github.com/sykomiked/ZEDEC-XERO-VULPINE-V1.0.0/sdk/go/zxv"
)

// These tests replay the ABI 1.6 keys of vectors.json (status_abi16 and
// vino) through the Go binding, plus lifetimes, the voucher buffer path,
// capability refusals and fail-closed handles.

// env16 opens a context with every ABI 1.6 capability bit.
func env16(t *testing.T) (*zxv.Context, *zxv.Capability) {
	t.Helper()
	ctx := must[*zxv.Context](t)(zxv.NewContext())
	cap := must[*zxv.Capability](t)(ctx.RootCapability(1, nil, zxv.CapAll16))
	t.Cleanup(func() {
		if err := cap.Close(); err != nil {
			t.Errorf("cap close: %v", err)
		}
		if err := ctx.Close(); err != nil {
			t.Errorf("ctx close: %v", err)
		}
	})
	return ctx, cap
}

func vinoVec(t *testing.T) map[string]any { return v15(t, "vino") }

func TestABI16StatusesCapsFeatures(t *testing.T) {
	if _, minor, _ := zxv.Version(); minor < 6 {
		t.Fatalf("minor %d", minor)
	}
	sentinels := map[int32]error{
		zxv.StatusRail: zxv.ErrRail, zxv.StatusCoverage: zxv.ErrCoverage, zxv.StatusLane: zxv.ErrLane,
		zxv.StatusExpired: zxv.ErrExpired, zxv.StatusChance: zxv.ErrChance,
	}
	v15(t, "vino")
	rows := ja(v15Data["status_abi16"])
	if len(rows) != 5 {
		t.Fatalf("%d status rows", len(rows))
	}
	for _, r := range rows {
		m := jm(r)
		code, name, class := int32(ji(m["code"])), js(m["name"]), int(ji(m["class"]))
		if zxv.StatusName(code) != name || int(zxv.ClassOf(code)) != class {
			t.Fatalf("%d: %s %v", code, zxv.StatusName(code), zxv.ClassOf(code))
		}
		err := zxv.NewError(code, "detail")
		var e *zxv.Error
		if !errors.As(err, &e) || e.Status != code || int(e.Class) != class || e.Name != name {
			t.Fatalf("NewError(%d) = %v", code, err)
		}
		classSentinel := map[int]error{1: zxv.ErrUsage, 2: zxv.ErrRefused}[class]
		if !errors.Is(err, classSentinel) || !errors.Is(err, sentinels[code]) {
			t.Fatalf("%s sentinels", name)
		}
		switch class {
		case 1:
			var u *zxv.UsageError
			if !errors.As(err, &u) {
				t.Fatalf("%s is not a *UsageError", name)
			}
		case 2:
			var u *zxv.RefusedError
			if !errors.As(err, &u) {
				t.Fatalf("%s is not a *RefusedError", name)
			}
		}
		for other, s := range sentinels {
			if other != code && errors.Is(err, s) {
				t.Fatalf("%s matches the sentinel of %d", name, other)
			}
		}
	}
	if zxv.CapVino != 1<<15 || zxv.CapAll16 != 1<<16-1 || !zxv.CapAll16.Has(zxv.CapAll15|zxv.CapVino) || zxv.CapAll15.Has(zxv.CapVino) {
		t.Fatal("cap bits")
	}
	if s := (zxv.CapParadox | zxv.CapVino).String(); s != "PARADOX|VINO" {
		t.Fatalf("caps string %q", s)
	}
	if zxv.Feat2Vino != 0x100 || !zxv.LiveFeatures2().Has(zxv.Feat2Vino) {
		t.Fatalf("Feat2Vino %#x not live in %#x", zxv.Feat2Vino, zxv.LiveFeatures2())
	}
	ctx := must[*zxv.Context](t)(zxv.NewContext())
	defer ctx.Close()
	c := must[*zxv.Capability](t)(ctx.RootCapability(1, nil, zxv.CapAll15|zxv.CapVino))
	ok(t, c.Check(zxv.CapVino))
	ok(t, c.Close())
	if zxv.VinoRequestBytes != 160 || zxv.VinoReceiptMsgBytes != 88 || zxv.VinoMemoMax != 64 ||
		zxv.VinoAmountMax != 1<<59 || zxv.VinoMaxAccounts != 256 || zxv.VinoMaxVouchers != 4096 {
		t.Fatal("vino sizes")
	}
	if zxv.VinoBearer.String() != "BEARER" || zxv.VinoGhost.String() != "GHOST" || zxv.VinoLaneLive.String() != "LIVE" ||
		zxv.VinoRedeemed.String() != "REDEEMED" || zxv.VinoIssuer.String() != "ISSUER" {
		t.Fatal("enum names")
	}
}

// ===== requests ============================================================

func vinoReq(t *testing.T, m map[string]any) zxv.VinoRequest {
	t.Helper()
	r := zxv.VinoRequest{Lane: zxv.VinoLane(ji(m["lane"])), Flags: zxv.VinoRequestFlags(ji(m["flags"])),
		Amount: ju(m["amount"]), CreatedAt: ju(m["created_at"]), ExpiresAt: ju(m["expires_at"]), Memo: js(m["memo"])}
	copy(r.Merchant[:], jhex(t, m["merchant"]))
	copy(r.Nonce[:], jhex(t, m["nonce"]))
	return r
}

func TestVinoRequestVectors(t *testing.T) {
	v := vinoVec(t)
	rows := ja(v["requests"])
	if len(rows) < 10 {
		t.Fatalf("%d request rows", len(rows))
	}
	for i, row := range rows {
		m := jm(row)
		req := vinoReq(t, m)
		rec, id, err := req.Encode()
		wantSt(t, "encode "+itoa(i), err, m["status"])
		if err != nil {
			if rec != (zxv.VinoRequestRecord{}) || id != (zxv.VinoRequestID{}) {
				t.Fatalf("encode %d: refusal returned bytes", i)
			}
			continue
		}
		if !bytes.Equal(rec[:], jhex(t, m["bytes"])) || !bytes.Equal(id[:], jhex(t, m["id"])) {
			t.Fatalf("encode %d: bytes or id differ", i)
		}
		if id2 := must[zxv.VinoRequestID](t)(req.ID()); id2 != id {
			t.Fatalf("ID %d", i)
		}
		back, id3, err := zxv.DecodeVinoRequest(rec)
		ok(t, err)
		if back != req || id3 != id {
			t.Fatalf("decode %d = %+v", i, back)
		}
	}
	// Each changed byte of requests[0] is refused with its status.
	var base zxv.VinoRequestRecord
	copy(base[:], jhex(t, jm(rows[0])["bytes"]))
	for i, row := range ja(v["decode"]) {
		m := jm(row)
		b := base
		b[ji(m["offset"])] = byte(ji(m["value"]))
		r, id, err := zxv.DecodeVinoRequest(b)
		wantSt(t, "decode refusal "+itoa(i), err, m["status"])
		if r != (zxv.VinoRequest{}) || id != (zxv.VinoRequestID{}) {
			t.Fatalf("decode refusal %d returned values", i)
		}
	}
	// The class mapping of the request refusals.
	live := vinoReq(t, jm(rows[0]))
	live.Lane = 3
	if _, _, err := live.Encode(); !errors.Is(err, zxv.ErrLane) || !errors.Is(err, zxv.ErrUsage) {
		t.Fatalf("lane: %v", err)
	}
	live.Lane, live.Flags = zxv.VinoLaneLive, zxv.VinoFlagChance
	if _, _, err := live.Encode(); !errors.Is(err, zxv.ErrChance) || !errors.Is(err, zxv.ErrRefused) {
		t.Fatalf("chance: %v", err)
	}
	live.Lane = zxv.VinoLanePlay // a chance outcome is allowed on the play lane
	ok(t, func() error { _, _, err := live.Encode(); return err }())
	// A NUL in the memo is refused rather than silently cutting the memo.
	live.Memo = "Sword\x00of Dawn"
	if _, _, err := live.Encode(); statusOf(err) != zxv.StatusFormat {
		t.Fatalf("NUL memo: %v", err)
	}
}

// ===== receipts ============================================================

func TestVinoReceiptVectors(t *testing.T) {
	_, cap := env16(t)
	v := vinoVec(t)
	rv := jm(v["receipt"])
	req0 := jm(ja(v["requests"])[0])
	var rec zxv.VinoRequestRecord
	copy(rec[:], jhex(t, req0["bytes"]))
	var id zxv.VinoRequestID
	copy(id[:], jhex(t, req0["id"]))

	var seed [32]byte
	copy(seed[:], jhex(t, rv["seed"]))
	dsa := zxv.NewMLDSA87(cap)
	pk, sk := must2(t)(dsa.KeyGenFromSeed(seed))
	defer sk.Close()
	if h := zxv.SHA3256(pk); !bytes.Equal(h[:], jhex(t, rv["pk_sha3_256"])) {
		t.Fatal("wallet pk")
	}
	at := ju(rv["approved_at"])
	msg := must[zxv.VinoReceiptMessage](t)(zxv.VinoReceiptMessageFor(id, pk, at))
	if !bytes.Equal(msg[:], jhex(t, rv["msg"])) {
		t.Fatalf("receipt message %x", msg)
	}
	sig := must[[]byte](t)(dsa.SignDeterministic(sk, msg[:], nil))
	if !bytes.Equal(sig, jhex(t, rv["sig"])) {
		t.Fatal("deterministic receipt signature")
	}
	for i, c := range ja(rv["checks"]) {
		m := jm(c)
		s := slices.Clone(sig)
		if f := ji(m["flip_sig"]); f >= 0 {
			s[f] ^= 1
		}
		err := zxv.VerifyVinoReceipt(rec, pk, ju(m["approved_at"]), s, ju(m["now"]))
		wantSt(t, "receipt check "+itoa(i), err, m["status"])
	}

	// The wallet side signs (hedged) a receipt the app then verifies.
	wsig := must[[]byte](t)(zxv.SignVinoReceipt(cap, rec, sk, pk, at))
	ok(t, zxv.VerifyVinoReceipt(rec, pk, at, wsig, at+100))
	ok(t, dsa.Verify(pk, msg[:], nil, wsig))
	if _, err := zxv.SignVinoReceipt(cap, rec, sk, pk, 1700000601); !errors.Is(err, zxv.ErrExpired) {
		t.Fatalf("sign after expiry: %v", err)
	}
	// Another wallet's key does not verify.
	pk2, sk2 := must2(t)(dsa.KeyGenFromSeed([32]byte{0x32}))
	sk2.Close()
	if err := zxv.VerifyVinoReceipt(rec, pk2, at, wsig, at); !errors.Is(err, zxv.ErrVerificationFailed) {
		t.Fatalf("other wallet: %v", err)
	}
	// A changed request byte (amount) does not verify.
	other := rec
	other[56] ^= 1
	if err := zxv.VerifyVinoReceipt(other, pk, at, wsig, at); !errors.Is(err, zxv.ErrVerificationFailed) {
		t.Fatalf("changed request: %v", err)
	}
	// A malformed request is a decode refusal, not a verify failure.
	bad := rec
	bad[0] = 'Y'
	if err := zxv.VerifyVinoReceipt(bad, pk, at, wsig, at); statusOf(err) != zxv.StatusFormat {
		t.Fatalf("bad magic: %v", err)
	}
	// Signing needs CRYPTO_SIGN; verifying needs no capability at all.
	nosign := must[*zxv.Capability](t)(cap.Attenuate(zxv.CapAll16 &^ zxv.CapCryptoSign))
	defer nosign.Close()
	if _, err := zxv.SignVinoReceipt(nosign, rec, sk, pk, at); !errors.Is(err, zxv.ErrCapabilityDenied) {
		t.Fatalf("sign without CRYPTO_SIGN: %v", err)
	}
	// Wrong-length inputs are usage errors before any C call.
	if _, err := zxv.VinoReceiptMessageFor(id, pk[:10], at); !errors.Is(err, zxv.ErrUsage) {
		t.Fatalf("short pk: %v", err)
	}
	if err := zxv.VerifyVinoReceipt(rec, pk, at, wsig[:100], at); !errors.Is(err, zxv.ErrUsage) {
		t.Fatalf("short sig: %v", err)
	}
}

func must2(t *testing.T) func([]byte, *zxv.SecretKey, error) ([]byte, *zxv.SecretKey) {
	return func(a []byte, b *zxv.SecretKey, err error) ([]byte, *zxv.SecretKey) {
		t.Helper()
		if err != nil {
			t.Fatal(err)
		}
		return a, b
	}
}

// ===== play lane ===========================================================

func TestVinoPlayVectors(t *testing.T) {
	_, cap := env16(t)
	pv := jm(vinoVec(t)["play"])
	p := must[*zxv.VinoPlay](t)(zxv.NewVinoPlay(cap))
	defer p.Close()
	for i, s := range ja(pv["steps"]) {
		m := jm(s)
		label := js(m["op"]) + " step " + itoa(i)
		var err error
		switch js(m["op"]) {
		case "open":
			var a uint32
			a, err = p.Open(uint32(ji(m["owner"])), zxv.VinoAccountKind(ji(m["is_issuer"])))
			if err == nil && int64(a) != ji(m["account"]) {
				t.Fatalf("%s: account %d", label, a)
			}
		case "back":
			err = p.Back(uint32(ji(m["issuer"])), ju(m["amount"]))
		case "unback":
			err = p.Unback(uint32(ji(m["issuer"])), ju(m["amount"]))
		case "issue":
			var id uint64
			id, err = p.Issue(uint32(ji(m["issuer"])), uint32(ji(m["holder"])), ju(m["face"]), zxv.VinoRail(ji(m["rail"])))
			if err == nil && id != ju(m["id"]) {
				t.Fatalf("%s: id %d", label, id)
			}
		case "transfer":
			err = p.Transfer(ju(m["voucher"]), uint32(ji(m["from"])), uint32(ji(m["to"])), zxv.VinoRail(ji(m["rail"])))
		case "set_rail":
			err = p.SetRail(ju(m["voucher"]), uint32(ji(m["holder"])), zxv.VinoRail(ji(m["rail"])))
		case "redeem":
			err = p.Redeem(ju(m["voucher"]), uint32(ji(m["holder"])))
		default:
			t.Fatalf("unknown op %q", js(m["op"]))
		}
		wantSt(t, label, err, m["status"])
	}
	for _, a := range ja(pv["accounts"]) {
		m := jm(a)
		n := uint32(ji(m["account"]))
		got := must[zxv.VinoAccount](t)(p.Account(n))
		want := zxv.VinoAccount{Owner: uint32(ji(m["owner"])), Kind: zxv.VinoAccountKind(ji(m["is_issuer"])),
			Backing: ju(m["backing"]), Faces: ju(m["faces"]), Count: uint32(ji(m["count"]))}
		if got != want {
			t.Fatalf("account %d = %+v, want %+v", n, got, want)
		}
		ids := must[[]uint64](t)(p.Vouchers(n))
		var wantIDs []uint64
		for _, x := range ja(m["vouchers"]) {
			wantIDs = append(wantIDs, ju(x))
		}
		if ids == nil || !slices.Equal(ids, wantIDs) {
			t.Fatalf("account %d vouchers %v, want %v", n, ids, wantIDs)
		}
	}
	for _, x := range ja(pv["vouchers"]) {
		m := jm(x)
		got := must[zxv.VinoVoucher](t)(p.Voucher(ju(m["id"])))
		want := zxv.VinoVoucher{Face: ju(m["face"]), Issuer: uint32(ji(m["issuer"])), Holder: uint32(ji(m["holder"])),
			Rail: zxv.VinoRail(ji(m["rail"])), State: zxv.VinoVoucherState(ji(m["state"]))}
		if got != want {
			t.Fatalf("voucher %s = %+v, want %+v", js(m["id"]), got, want)
		}
	}
	ok(t, p.Check())
	if _, err := p.Account(99); !errors.Is(err, zxv.ErrNotFound) {
		t.Fatalf("account 99: %v", err)
	}
	if _, err := p.Vouchers(99); !errors.Is(err, zxv.ErrNotFound) {
		t.Fatalf("vouchers 99: %v", err)
	}
}

func itoa(i int) string {
	if i == 0 {
		return "0"
	}
	var b []byte
	for ; i > 0; i /= 10 {
		b = append([]byte{byte('0' + i%10)}, b...)
	}
	return string(b)
}

// TestVinoVoucherBuffer drives the two-call buffer path past its first
// guess (8 ids) and back to zero.
func TestVinoVoucherBuffer(t *testing.T) {
	_, cap := env16(t)
	p := must[*zxv.VinoPlay](t)(zxv.NewVinoPlay(cap))
	defer p.Close()
	iss := must[uint32](t)(p.Open(1, zxv.VinoIssuer))
	h := must[uint32](t)(p.Open(2, zxv.VinoHolder))
	ok(t, p.Back(iss, 10000))
	var want []uint64
	for i := 0; i < 37; i++ {
		want = append(want, must[uint64](t)(p.Issue(iss, h, 1, zxv.VinoGhost)))
	}
	for _, acct := range []uint32{iss, h} {
		got := must[[]uint64](t)(p.Vouchers(acct))
		if !slices.Equal(got, want) {
			t.Fatalf("account %d: %v", acct, got)
		}
	}
	a := must[zxv.VinoAccount](t)(p.Account(iss))
	if a.Count != 37 || a.Faces != 37 || a.Backing != 10000 || a.Kind != zxv.VinoIssuer {
		t.Fatalf("issuer %+v", a)
	}
	for _, id := range want {
		ok(t, p.Redeem(id, h))
	}
	if got := must[[]uint64](t)(p.Vouchers(h)); got == nil || len(got) != 0 {
		t.Fatalf("after redeeming all: %#v", got)
	}
	// Redeeming retired the vouchers and paid nothing: backing is unchanged.
	if a := must[zxv.VinoAccount](t)(p.Account(iss)); a.Backing != 10000 || a.Faces != 0 || a.Count != 0 {
		t.Fatalf("issuer after redeem %+v", a)
	}
	ok(t, p.Check())
}

// ===== meter ===============================================================

func TestVinoMeterVectors(t *testing.T) {
	_, cap := env16(t)
	mv := jm(vinoVec(t)["meter"])
	m := must[*zxv.VinoMeter](t)(zxv.NewVinoMeter(cap, ju(mv["daily_cap"])))
	defer m.Close()
	for i, s := range ja(mv["steps"]) {
		st := jm(s)
		left, err := m.Admit(ju(st["day"]), ju(st["amount"]))
		wantSt(t, "meter step "+itoa(i), err, st["status"])
		if l, has := st["left"]; has && left != ju(l) {
			t.Fatalf("meter step %d: left %d, want %s", i, left, js(l))
		}
	}
	if _, err := zxv.NewVinoMeter(cap, 0); statusOf(err) != zxv.StatusRange {
		t.Fatalf("cap 0: %v", err)
	}
	if _, err := zxv.NewVinoMeter(cap, zxv.VinoAmountMax+1); statusOf(err) != zxv.StatusRange {
		t.Fatalf("cap above max: %v", err)
	}
	if _, err := m.Admit(100, 1); err != nil {
		t.Fatal(err)
	}
	if _, err := m.Admit(100, 101); !errors.Is(err, zxv.ErrLimit) || !errors.Is(err, zxv.ErrRefused) {
		t.Fatalf("over cap: %v", err)
	}
}

// ===== behaviour: errors, caps, lifetimes, fail closed =====================

func TestVinoErrorMapping(t *testing.T) {
	_, cap := env16(t)
	p := must[*zxv.VinoPlay](t)(zxv.NewVinoPlay(cap))
	defer p.Close()
	iss := must[uint32](t)(p.Open(1, zxv.VinoIssuer))
	h1 := must[uint32](t)(p.Open(2, zxv.VinoHolder))
	h2 := must[uint32](t)(p.Open(3, zxv.VinoHolder))
	// ErrCoverage (REFUSED): 1.8x of face 1 needs backing 2.
	ok(t, p.Back(iss, 1))
	_, err := p.Issue(iss, h1, 1, zxv.VinoBearer)
	var r *zxv.RefusedError
	if !errors.Is(err, zxv.ErrCoverage) || !errors.As(err, &r) || r.Err.Name != "ZXV_E_COVERAGE" || r.Err.Detail == "" {
		t.Fatalf("coverage: %v", err)
	}
	ok(t, p.Back(iss, 1))
	id := must[uint64](t)(p.Issue(iss, h1, 1, zxv.VinoBearer))
	// ErrRail (REFUSED): spend on the inactive rail, or switch to the same rail.
	if err := p.Transfer(id, h1, h2, zxv.VinoGhost); !errors.Is(err, zxv.ErrRail) || !errors.Is(err, zxv.ErrRefused) {
		t.Fatalf("rail: %v", err)
	}
	if err := p.SetRail(id, h1, zxv.VinoBearer); !errors.Is(err, zxv.ErrRail) {
		t.Fatalf("same rail: %v", err)
	}
	ok(t, p.SetRail(id, h1, zxv.VinoGhost))
	ok(t, p.Transfer(id, h1, h2, zxv.VinoGhost))
	if v := must[zxv.VinoVoucher](t)(p.Voucher(id)); v.Holder != h2 || v.Rail != zxv.VinoGhost || v.State != zxv.VinoActive {
		t.Fatalf("voucher %+v", v)
	}
	// ErrDenom (USAGE) for a face off the ladder.
	if _, err := p.Issue(iss, h1, 4, zxv.VinoBearer); !errors.Is(err, zxv.ErrDenom) || !errors.Is(err, zxv.ErrUsage) {
		t.Fatalf("denom: %v", err)
	}
	// ErrInsufficientFunds for unbacking more than the backing.
	if err := p.Unback(iss, 1000); !errors.Is(err, zxv.ErrInsufficientFunds) {
		t.Fatalf("funds: %v", err)
	}
	// A bad rail value is a usage error.
	if _, err := p.Issue(iss, h1, 1, zxv.VinoRail(7)); !errors.Is(err, zxv.ErrUsage) {
		t.Fatalf("bad rail: %v", err)
	}
	// ErrExpired, ErrLane and ErrChance are checked in the receipt and
	// request tests.
}

func TestVinoCapabilityRefusal(t *testing.T) {
	_, root := env16(t)
	noVino := must[*zxv.Capability](t)(root.Attenuate(zxv.CapAll16 &^ zxv.CapVino))
	defer noVino.Close()
	if _, err := zxv.NewVinoPlay(noVino); !errors.Is(err, zxv.ErrCapabilityDenied) || !errors.Is(err, zxv.ErrRefused) {
		t.Fatalf("play without VINO: %v", err)
	}
	if _, err := zxv.NewVinoMeter(noVino, 10); !errors.Is(err, zxv.ErrCapabilityDenied) {
		t.Fatalf("meter without VINO: %v", err)
	}
	// A sandbox keeps the capability it was made with: revoke it and every
	// call is refused.
	sub := must[*zxv.Capability](t)(root.Attenuate(zxv.CapAll16))
	p := must[*zxv.VinoPlay](t)(zxv.NewVinoPlay(sub))
	m := must[*zxv.VinoMeter](t)(zxv.NewVinoMeter(sub, 10))
	ok(t, sub.Revoke())
	if _, err := p.Open(1, zxv.VinoIssuer); !errors.Is(err, zxv.ErrCapabilityDenied) {
		t.Fatalf("revoked play: %v", err)
	}
	if _, err := p.Vouchers(0); !errors.Is(err, zxv.ErrCapabilityDenied) {
		t.Fatalf("revoked vouchers: %v", err)
	}
	if _, err := m.Admit(1, 1); !errors.Is(err, zxv.ErrCapabilityDenied) {
		t.Fatalf("revoked meter: %v", err)
	}
	ok(t, p.Close())
	ok(t, m.Close())
	ok(t, sub.Close())
}

func TestVinoHandleLifetimes(t *testing.T) {
	_, cap := env16(t)
	sub := must[*zxv.Capability](t)(cap.Attenuate(zxv.CapAll16))
	p := must[*zxv.VinoPlay](t)(zxv.NewVinoPlay(sub))
	m := must[*zxv.VinoMeter](t)(zxv.NewVinoMeter(sub, 10))
	// The capability refuses to close while handles made with it are open.
	if err := sub.Close(); statusOf(err) != zxv.StatusState {
		t.Fatalf("cap closed under open vino handles: %v", err)
	}
	ok(t, p.Close())
	ok(t, p.Close()) // twice is a no-op
	ok(t, m.Close())
	ok(t, m.Close())
	for name, err := range map[string]error{
		"open":     func() error { _, err := p.Open(1, zxv.VinoIssuer); return err }(),
		"vouchers": func() error { _, err := p.Vouchers(0); return err }(),
		"check":    p.Check(),
		"admit":    func() error { _, err := m.Admit(1, 1); return err }(),
	} {
		if !errors.Is(err, zxv.ErrClosed) || !errors.Is(err, zxv.ErrUsage) {
			t.Fatalf("%s after close: %v", name, err)
		}
	}
	ok(t, sub.Close())
}

func TestVinoFailClosed(t *testing.T) {
	ctx, cap := env16(t)
	p := must[*zxv.VinoPlay](t)(zxv.NewVinoPlay(cap))
	defer p.Close()
	m := must[*zxv.VinoMeter](t)(zxv.NewVinoMeter(cap, 10))
	defer m.Close()
	iss := must[uint32](t)(p.Open(1, zxv.VinoIssuer))
	ok(t, ctx.Halt("test halt"))
	for name, err := range map[string]error{
		"back":     p.Back(iss, 5),
		"account":  func() error { _, err := p.Account(iss); return err }(),
		"vouchers": func() error { _, err := p.Vouchers(iss); return err }(),
		"check":    p.Check(),
		"admit":    func() error { _, err := m.Admit(1, 1); return err }(),
		"new play": func() error { _, err := zxv.NewVinoPlay(cap); return err }(),
	} {
		if !errors.Is(err, zxv.ErrFatal) || !errors.Is(err, zxv.ErrPoisoned) {
			t.Fatalf("%s after halt: %v", name, err)
		}
	}
	// Pure functions need no context and keep working.
	if _, err := zxv.VinoReceiptMessageFor(zxv.VinoRequestID{}, make([]byte, zxv.MLDSA87PKBytes), 1); err != nil {
		t.Fatalf("pure receipt message after halt: %v", err)
	}
}
