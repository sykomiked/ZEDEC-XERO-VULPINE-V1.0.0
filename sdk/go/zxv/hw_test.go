// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

package zxv_test

// ABI 1.4: hardware emulation and hardware updates, mirroring
// sdk/libzxv/test/test_zxv_abi14.c.

import (
	"bytes"
	"encoding/binary"
	"encoding/hex"
	"errors"
	"sync"
	"testing"
	"unsafe"

	"github.com/sykomiked/ZEDEC-XERO-VULPINE-V1.0.0/sdk/go/zxv"
)

// hwEnv opens a context with every 1.4 bit and a 1.1-only child.
func hwEnv(t *testing.T) (*zxv.Context, *zxv.Capability, *zxv.Capability) {
	t.Helper()
	ctx := must[*zxv.Context](t)(zxv.NewContext())
	cap := must[*zxv.Capability](t)(ctx.RootCapability(1, nil, zxv.CapAll14))
	nohw := must[*zxv.Capability](t)(cap.Attenuate(zxv.CapAll))
	t.Cleanup(func() {
		ok(t, nohw.Close())
		ok(t, cap.Close())
		ok(t, ctx.Close())
	})
	return ctx, cap, nohw
}

func wantStatus(t *testing.T, err error, want int32) {
	t.Helper()
	if got := statusOf(err); got != want {
		t.Fatalf("status %s (%v), want %s", zxv.StatusName(got), err, zxv.StatusName(want))
	}
}

func TestHwVersionStatusesCaps(t *testing.T) {
	if _, minor, _ := zxv.Version(); minor < 4 {
		t.Fatalf("minor %d", minor)
	}
	if !zxv.LiveFeatures().Has(zxv.FeatHW) {
		t.Fatal("FeatHW missing")
	}
	for _, c := range []struct {
		s     int32
		name  string
		class zxv.Class
		sent  error
	}{
		{zxv.StatusNoProvider, "ZXV_E_NOPROVIDER", zxv.ClassRefused, zxv.ErrNoProvider},
		{zxv.StatusDispute, "ZXV_E_DISPUTE", zxv.ClassRefused, zxv.ErrDispute},
		{zxv.StatusQuarantined, "ZXV_E_QUARANTINED", zxv.ClassRefused, zxv.ErrQuarantined},
		{zxv.StatusRollback, "ZXV_E_ROLLBACK", zxv.ClassRefused, zxv.ErrRollback},
		{zxv.StatusBlocked, "ZXV_E_BLOCKED", zxv.ClassRefused, zxv.ErrBlocked},
		{zxv.StatusTarget, "ZXV_E_TARGET", zxv.ClassUsage, zxv.ErrTarget},
		{zxv.StatusPolicy, "ZXV_E_POLICY", zxv.ClassRefused, zxv.ErrPolicy},
	} {
		if zxv.StatusName(c.s) != c.name || zxv.ClassOf(c.s) != c.class {
			t.Fatalf("%d: %s %v", c.s, zxv.StatusName(c.s), zxv.ClassOf(c.s))
		}
		if !errors.Is(zxv.NewError(c.s, ""), c.sent) {
			t.Fatalf("%s sentinel", c.name)
		}
	}
	if zxv.CapAll14 != 1<<12-1 || !zxv.CapAll14.Has(zxv.CapHWMatrix|zxv.CapHWReconf|zxv.CapAll) || zxv.CapAll.Has(zxv.CapHWMatrix) {
		t.Fatal("cap bits")
	}
	ctx := must[*zxv.Context](t)(zxv.NewContext())
	defer ctx.Close()
	if _, err := ctx.RootCapability(1, nil, zxv.CapAll14+1); statusOf(err) != zxv.StatusArg {
		t.Fatalf("unknown bit accepted: %v", err)
	}
}

func TestHwProfiles(t *testing.T) {
	host := must[*zxv.HwProfile](t)(zxv.HwProbeHost())
	defer host.Close()
	r1 := must[*zxv.HwProfile](t)(zxv.HwReference())
	defer r1.Close()
	if n := must[uint32](t)(r1.NodeCount()); n != 16 {
		t.Fatalf("R1 has %d nodes", n)
	}
	if n := must[uint32](t)(host.NodeCount()); n < 2 {
		t.Fatalf("host has %d nodes", n)
	}
	if v := must[uint64](t)(host.Cap(2, zxv.HwCapWordBits)); v != uint64(unsafe.Sizeof(uintptr(0))*8) {
		t.Fatalf("word bits %d", v)
	}
	m := must[zxv.HwMatch](t)(zxv.HwMatchProfiles(host, r1))
	if m.Native < 1 || m.Native+m.Emulated != 14 {
		t.Fatalf("match %+v", m)
	}
	if p := must[zxv.HwPair](t)(zxv.HwPairNode(host, r1, 1)); p.NeedID != 2 || p.Kind != zxv.HwGroup {
		t.Fatalf("package %+v", p)
	}
	if p := must[zxv.HwPair](t)(zxv.HwPairNode(host, r1, 2)); p.NeedID != 3 || p.Kind != zxv.HwNative || p.HostID != 2 {
		t.Fatalf("cpu %+v", p)
	}
	if p := must[zxv.HwPair](t)(zxv.HwPairNode(host, r1, 7)); p.Kind != zxv.HwEmulate {
		t.Fatalf("ternary %+v", p)
	}
	_, err := zxv.HwPairNode(host, r1, 99)
	wantStatus(t, err, zxv.StatusNotFound)
	if v := must[uint64](t)(r1.Cap(6, zxv.HwCapContractID)); v != uint64(zxv.HwFnTensor) {
		t.Fatalf("tensor contract %#x", v)
	}
	pres := must[*zxv.HwProfile](t)(zxv.HwPresent(host, r1))
	defer pres.Close()
	if v := must[uint64](t)(pres.Cap(3, zxv.HwCapKind)); v != uint64(zxv.HwKindHard) {
		t.Fatalf("cpu kind %d", v)
	}
	if v := must[uint64](t)(pres.Cap(8, zxv.HwCapKind)); v != uint64(zxv.HwKindSoft) {
		t.Fatalf("ternary kind %d", v)
	}
	_, err = pres.Cap(1, zxv.HwCapKind)
	wantStatus(t, err, zxv.StatusNotFound)

	enc := must[[]byte](t)(r1.Encode())
	cp := must[*zxv.HwProfile](t)(zxv.DecodeHwProfile(enc))
	defer cp.Close()
	if must[[32]byte](t)(cp.Digest()) != must[[32]byte](t)(r1.Digest()) {
		t.Fatal("digest differs after round trip")
	}
	_, err = zxv.DecodeHwProfile(enc[:len(enc)-1])
	wantStatus(t, err, zxv.StatusFormat)

	mine := must[*zxv.HwProfile](t)(zxv.NewHwProfile())
	defer mine.Close()
	ok(t, mine.AddNode(1, zxv.HwRoot, zxv.HwClassMachine))
	ok(t, mine.AddNode(2, 1, zxv.HwClassTernaryUnit))
	ok(t, mine.SetCap(2, zxv.HwCapRadix, 3))
	wantStatus(t, mine.AddNode(2, 1, zxv.HwClassCache), zxv.StatusArg)
	wantStatus(t, mine.SetCap(9, zxv.HwCapRadix, 3), zxv.StatusNotFound)
	ok(t, mine.AddNode(3, 1, zxv.HwClassPrivate|1))
	if p := must[zxv.HwPair](t)(zxv.HwPairNode(mine, r1, 7)); p.Kind != zxv.HwNative || p.HostID != 2 {
		t.Fatalf("ternary pairs natively: %+v", p)
	}
	d1 := must[[32]byte](t)(mine.Digest())
	ok(t, mine.SetExt(3, []byte{0xA5, 0x00, 0x5A}))
	if must[[32]byte](t)(mine.Digest()) == d1 {
		t.Fatal("extension is part of the identity")
	}
	if e := must[[]byte](t)(mine.Ext(3)); !bytes.Equal(e, []byte{0xA5, 0x00, 0x5A}) {
		t.Fatalf("ext %x", e)
	}
	wantStatus(t, mine.SetExt(3, make([]byte, zxv.HwExtMax+1)), zxv.StatusRange)
	nodes := must[[]zxv.HwNode](t)(mine.Nodes())
	if len(nodes) != 3 || nodes[2].Class != zxv.HwClassPrivate|1 || nodes[1].Caps != 1 {
		t.Fatalf("nodes %+v", nodes)
	}
}

func TestHwStandardEnginesAndPlacement(t *testing.T) {
	_, cap, nohw := hwEnv(t)
	_, err := zxv.NewHwMatrix(nohw)
	if !errors.Is(err, zxv.ErrCapabilityDenied) {
		t.Fatalf("no HW_MATRIX: %v", err)
	}
	mx := must[*zxv.HwMatrix](t)(zxv.NewHwMatrix(cap))
	defer func() { ok(t, mx.Close()) }()

	d := must[[]byte](t)(mx.Call(zxv.HwFnSHA256, 0, []byte("abc")))
	if hex.EncodeToString(d) != "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad" {
		t.Fatalf("sha256(abc) %x", d)
	}
	macIn := append([]byte{4, 0}, []byte("Jefewhat do ya want for nothing?")...)
	mac := must[[]byte](t)(mx.Call(zxv.HwFnMAC, 0, macIn))
	if hex.EncodeToString(mac) != "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843" {
		t.Fatalf("HMAC %x", mac)
	}
	g := make([]byte, 16)
	g[0], g[8] = 12, 18
	if out := must[[]byte](t)(mx.Call(zxv.HwFnGCD, 0, g)); binary.LittleEndian.Uint64(out) != 6 {
		t.Fatalf("gcd %x", out)
	}

	all := must[[]zxv.HwProviderInfo](t)(mx.Providers())
	if len(all) < 5 {
		t.Fatalf("%d engines", len(all))
	}
	for i, p := range all {
		if p.Kind != zxv.HwKindSoft || p.Proof != zxv.TruthTrue || p.Contract != 1 || p.Flags.Has(zxv.HwPFFault) {
			t.Fatalf("engine %d: %+v", i, p)
		}
	}
	macPv, found, err := mx.FindProvider(zxv.HwFnMAC)
	if err != nil || !found {
		t.Fatalf("MAC listed: %v", err)
	}
	_, err = mx.ProviderInfo(uint32(len(all)))
	wantStatus(t, err, zxv.StatusNotFound)
	fi := must[zxv.HwFunctionInfo](t)(mx.FunctionInfo(zxv.HwFnMAC))
	if fi.Contract != 1 || !fi.Policy.Has(zxv.HwPolicyKeyBearing) || fi.Vectors < 1 {
		t.Fatalf("MAC info %+v", fi)
	}

	// Key-bearing: a cheaper REMOTE implementation never serves it.
	var hits int
	var mu sync.Mutex
	ok(t, mx.AddResource(7))
	prem := must[uint32](t)(mx.AddProvider(zxv.HwFnMAC, 7, zxv.HwKindRemote, 1, func(op uint32, in, out []byte) (int, error) {
		mu.Lock()
		hits++
		mu.Unlock()
		return 0, zxv.NewError(zxv.StatusState, "must never serve")
	}))
	_, _ = mx.Attest(prem)
	if out := must[[]byte](t)(mx.Call(zxv.HwFnMAC, 0, macIn)); !bytes.Equal(out, mac) {
		t.Fatal("MAC output changed")
	}
	if pv := must[uint32](t)(mx.Bound(zxv.HwFnMAC)); pv != macPv {
		t.Fatalf("MAC stays local (%d)", pv)
	}
	if pi := must[zxv.HwProviderInfo](t)(mx.ProviderInfo(prem)); !pi.Flags.Has(zxv.HwPFPolicy) {
		t.Fatalf("remote MAC flags %#x", pi.Flags)
	}
	if err := mx.SetPolicy(zxv.HwFnMAC, 0, 0, 0); !errors.Is(err, zxv.ErrPolicy) {
		t.Fatalf("clear key-bearing: %v", err)
	}
	_, _, err = mx.Memo(zxv.HwFnMAC, 16)
	wantStatus(t, err, zxv.StatusPolicy)
	wantStatus(t, mx.SetPolicy(zxv.HwFnMAC, 0x80, 0, 0), zxv.StatusArg)

	// A power budget: undeclared traits never qualify, declared ones do.
	ok(t, mx.SetPolicy(zxv.HwFnGCD, 0, 0, 500))
	_, err = mx.Call(zxv.HwFnGCD, 0, g)
	if !errors.Is(err, zxv.ErrNoProvider) {
		t.Fatalf("budget: %v", err)
	}
	gcdPv, _, _ := mx.FindProvider(zxv.HwFnGCD)
	ok(t, mx.SetTraits(gcdPv, 2000, 300, 4))
	wantStatus(t, mx.SetTraits(gcdPv, 2000, 300, 16), zxv.StatusRange)
	_ = must[[]byte](t)(mx.Call(zxv.HwFnGCD, 0, g))
	if pi := must[zxv.HwProviderInfo](t)(mx.ProviderInfo(gcdPv)); pi.ThroughputClass != 4 || pi.Power != zxv.HwPower1W {
		t.Fatalf("traits %+v", pi)
	}
	ok(t, mx.SetPolicy(zxv.HwFnGCD, zxv.HwPolicyLocalOnly, 1000, 0))
	_, err = mx.Call(zxv.HwFnGCD, 0, g)
	wantStatus(t, err, zxv.StatusNoProvider)
	ok(t, mx.SetPolicy(zxv.HwFnGCD, 0, 0, 0))
}

type adder struct {
	mu  sync.Mutex
	k   byte
	bad int
}

func (a *adder) set(bad int) { a.mu.Lock(); a.bad = bad; a.mu.Unlock() }

func (a *adder) fn(op uint32, in, out []byte) (int, error) {
	a.mu.Lock()
	bad := a.bad
	a.mu.Unlock()
	if bad == 2 {
		return 0, zxv.NewError(zxv.StatusIO, "device error")
	}
	if len(in) > len(out) {
		return 0, zxv.NewError(zxv.StatusBuffer, "")
	}
	for i, b := range in {
		out[i] = b + a.k
		if bad == 1 && b == 9 {
			out[i]++
		}
	}
	return len(in), nil
}

func TestHwMatrixProofFailoverShadowMemoContract(t *testing.T) {
	_, cap, _ := hwEnv(t)
	mx := must[*zxv.HwMatrix](t)(zxv.NewHwMatrix(cap))
	defer func() { ok(t, mx.Close()) }()
	ok(t, mx.AddFunction(0x100, "add8"))
	ok(t, mx.AddFunction(0x101, "add16"))
	wantStatus(t, mx.AddFunction(0x100, "again"), zxv.StatusDuplicate)
	ok(t, mx.AddResource(1))
	ok(t, mx.AddResource(2))
	in, ex8, ex16 := []byte{0, 1, 9, 200}, []byte{8, 9, 17, 208}, []byte{16, 17, 25, 216}
	ok(t, mx.AddVector(0x100, 0, in, ex8))
	ok(t, mx.AddVector(0x101, 0, in, ex16))

	fx, sw := &adder{k: 8}, &adder{k: 8}
	pfx := must[uint32](t)(mx.AddProvider(0x100, 2, zxv.HwKindHard, 1, fx.fn))
	psw := must[uint32](t)(mx.AddProvider(0x100, 1, zxv.HwKindSoft, 10, sw.fn))
	// add16 through two calls back into the matrix (a declared dependency).
	p16 := must[uint32](t)(mx.AddProvider(0x101, 1, zxv.HwKindSoft, 20, func(op uint32, in, out []byte) (int, error) {
		tmp, err := mx.Call(0x100, op, in)
		if err != nil {
			return 0, err
		}
		return mx.CallInto(0x100, op, tmp, out)
	}))
	panicky := must[uint32](t)(mx.AddProvider(0x100, 1, zxv.HwKindSoft, 30, func(uint32, []byte, []byte) (int, error) {
		panic("boom")
	}))
	_, err := mx.Call(0x100, 0, in)
	wantStatus(t, err, zxv.StatusNoProvider)
	for _, p := range []uint32{pfx, psw} {
		if lp := must[zxv.Truth](t)(mx.Attest(p)); lp != zxv.TruthTrue {
			t.Fatalf("attest %d: %v", p, lp)
		}
	}
	if lp := must[zxv.Truth](t)(mx.Attest(panicky)); lp != zxv.TruthFalse {
		t.Fatalf("a panic must fail the proof: %v", lp)
	}
	if lp := must[zxv.Truth](t)(mx.Attest(p16)); lp != zxv.TruthFalse {
		t.Fatalf("undeclared dependency must not prove: %v", lp)
	}
	ok(t, mx.AddDependency(p16, 0x100))
	if lp := must[zxv.Truth](t)(mx.Attest(p16)); lp != zxv.TruthTrue {
		t.Fatalf("chained attest %v", lp)
	}
	if out := must[[]byte](t)(mx.Call(0x101, 0, in)); !bytes.Equal(out, ex16) {
		t.Fatalf("add16 %v", out)
	}
	if pv := must[uint32](t)(mx.Bound(0x100)); pv != pfx {
		t.Fatalf("fixed unit serves (%d)", pv)
	}
	ok(t, mx.SetResource(2, zxv.HwResFaulted))
	if out := must[[]byte](t)(mx.Call(0x100, 0, in)); !bytes.Equal(out, ex8) || must[uint32](t)(mx.Bound(0x100)) != psw {
		t.Fatal("failover to the emulator")
	}
	ok(t, mx.SetResource(2, zxv.HwResOK))

	fx.set(2)
	_, err = mx.Call(0x100, 0, in)
	wantStatus(t, err, zxv.StatusIO)
	fx.set(0)

	out, checked, err := mx.Shadow(0x100, 0, in)
	if err != nil || !checked || !bytes.Equal(out, ex8) {
		t.Fatalf("shadow %v %v %v", out, checked, err)
	}
	sw.set(1)
	if _, _, err = mx.Shadow(0x100, 0, in); !errors.Is(err, zxv.ErrDispute) {
		t.Fatalf("dispute: %v", err)
	}
	_, err = mx.Call(0x100, 0, in)
	wantStatus(t, err, zxv.StatusNoProvider)
	if must[zxv.Truth](t)(mx.Attest(pfx)) != zxv.TruthTrue || must[zxv.Truth](t)(mx.Attest(psw)) != zxv.TruthFalse {
		t.Fatal("re-proofs")
	}

	_, _, err = mx.Memo(0x100, 6)
	wantStatus(t, err, zxv.StatusArg)
	_, _, err = mx.Memo(0x100, 16)
	ok(t, err)
	calls0 := must[zxv.HwProviderInfo](t)(mx.ProviderInfo(pfx)).Calls
	_ = must[[]byte](t)(mx.Call(0x100, 0, in))
	_ = must[[]byte](t)(mx.Call(0x100, 0, in))
	calls1 := must[zxv.HwProviderInfo](t)(mx.ProviderInfo(pfx)).Calls
	hits, misses, err := mx.Memo(0x100, 16)
	ok(t, err)
	if calls1 != calls0+1 || hits != 1 || misses != 1 {
		t.Fatalf("memo: calls %d->%d hits %d misses %d", calls0, calls1, hits, misses)
	}

	fi := must[zxv.HwFunctionInfo](t)(mx.FunctionInfo(0x100))
	if fi.Contract != 0 || fi.Policy != 0 || fi.Vectors != 1 {
		t.Fatalf("add8 info %+v", fi)
	}
	ok(t, mx.SetContract(0x100, 2))
	if err := mx.SetContract(0x100, 2); !errors.Is(err, zxv.ErrRollback) {
		t.Fatalf("contract replay: %v", err)
	}
	ok(t, mx.AddVector(0x100, 0, in, ex8))
	_ = must[zxv.Truth](t)(mx.Attest(pfx))
	if pi := must[zxv.HwProviderInfo](t)(mx.ProviderInfo(pfx)); pi.Proof != zxv.TruthTrue || pi.Contract != 2 {
		t.Fatalf("proved %+v", pi)
	}
	if pi := must[zxv.HwProviderInfo](t)(mx.ProviderInfo(psw)); !pi.Flags.Has(zxv.HwPFUnproven) {
		t.Fatalf("emulator flags %#x", pi.Flags)
	}
}

// sim is a two-slot device whose active slot is a byte lookup table. Its
// read-back digest comes from the SHA-256 engine of a separate matrix.
type sim struct {
	mu     sync.Mutex
	slot   [2][]byte
	active int
	oracle *zxv.HwMatrix
}

func (s *sim) Load(slot uint32, p []byte) error {
	if slot > 1 || len(p) > 256 {
		return zxv.NewError(zxv.StatusRange, "slot")
	}
	s.mu.Lock()
	s.slot[slot] = append([]byte(nil), p...)
	s.mu.Unlock()
	return nil
}

func (s *sim) Readback(slot uint32) ([32]byte, error) {
	s.mu.Lock()
	b := append([]byte(nil), s.slot[slot]...)
	s.mu.Unlock()
	var d [32]byte
	out, err := s.oracle.Call(zxv.HwFnSHA256, 0, b)
	copy(d[:], out)
	return d, err
}

func (s *sim) Activate(slot uint32) error {
	s.mu.Lock()
	s.active = int(slot)
	s.mu.Unlock()
	return nil
}

func (s *sim) call(op uint32, in, out []byte) (int, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.active < 0 || len(in) > len(out) || len(s.slot[s.active]) < 256 {
		return 0, zxv.NewError(zxv.StatusState, "empty")
	}
	for i, b := range in {
		out[i] = s.slot[s.active][b]
	}
	return len(in), nil
}

func TestHwPackagesRegionsContracts(t *testing.T) {
	ctx, cap, nohw := hwEnv(t)
	rel := zxv.NewHwRelease(cap)
	key := must[*zxv.HwReleaseKey](t)(rel.KeyGen())
	defer key.Close()
	if len(key.Public) != zxv.HwPKBytes || key.MLDSA.Len() != zxv.HwMLDSASKBytes || key.SLHDSA.Len() != zxv.HwSLHDSASKBytes {
		t.Fatal("key sizes")
	}
	oracle := must[*zxv.HwMatrix](t)(zxv.NewHwMatrix(cap))
	defer func() { ok(t, oracle.Close()) }()
	mx := must[*zxv.HwMatrix](t)(zxv.NewHwMatrix(cap))
	defer func() { ok(t, mx.Close()) }()
	ok(t, mx.AddFunction(0x200, "inc"))
	ok(t, mx.AddResource(1))
	ok(t, mx.AddResource(2))
	vin, inc := make([]byte, 256), make([]byte, 256)
	for i := range vin {
		vin[i], inc[i] = byte(i), byte(i+1)
	}
	wrong := append([]byte(nil), inc...)
	wrong[77] = 0
	ok(t, mx.AddVector(0x200, 0, vin, inc))
	dev := &sim{active: -1, oracle: oracle}
	psoft := must[uint32](t)(mx.AddProvider(0x200, 1, zxv.HwKindSoft, 50, (&adder{k: 1}).fn))
	preg := must[uint32](t)(mx.AddProvider(0x200, 2, zxv.HwKindFabric, 1, dev.call))
	_ = must[zxv.Truth](t)(mx.Attest(psoft))

	if _, err := zxv.NewHwRegion(nohw, mx, preg, 1, 0xD0, nil, dev, 12); !errors.Is(err, zxv.ErrCapabilityDenied) {
		t.Fatalf("no HW_RECONF: %v", err)
	}
	_, err := zxv.NewHwRegion(cap, mx, psoft, 1, 0xD0, nil, dev, 12)
	wantStatus(t, err, zxv.StatusArg)
	rg := must[*zxv.HwRegion](t)(zxv.NewHwRegion(cap, mx, preg, 1, 0xD0, nil, dev, 12))
	defer func() { ok(t, rg.Close()) }()
	if err := mx.Close(); err == nil {
		t.Fatal("matrix closed under its region")
	}
	ok(t, rg.TrustKey(key.Public))
	var evMu sync.Mutex
	var events []zxv.HwEvent
	rg.OnEvent(func(e zxv.HwEvent) {
		evMu.Lock()
		events = append(events, e)
		evMu.Unlock()
	})

	// Image build 1 of 3: the good update.
	spec := zxv.HwImageSpec{Kind: zxv.HwImgPartial, TargetClass: 0xD0, FnID: 0x200, Version: 1, SVN: 1}
	good := must[[]byte](t)(rel.Build(spec, inc, key))
	if len(good) != zxv.HwPackageLen(256) {
		t.Fatalf("image length %d", len(good))
	}
	// One of the two signatures broken: refused, recorded.
	img := append([]byte(nil), good...)
	img[len(img)-1] ^= 1
	if err := rg.Stage(img); !errors.Is(err, zxv.ErrVerificationFailed) {
		t.Fatalf("broken SLH-DSA half: %v", err)
	}
	// Wrong hardware (the class is checked before the signature).
	img = append([]byte(nil), good...)
	binary.LittleEndian.PutUint32(img[12:], 0xD1)
	if err := rg.Stage(img); !errors.Is(err, zxv.ErrTarget) || !errors.Is(err, zxv.ErrUsage) {
		t.Fatalf("target: %v", err)
	}

	ok(t, rg.Stage(good))
	wantStatus(t, rg.Commit(), zxv.StatusState)
	ok(t, rg.Trial(5))
	if pv := must[uint32](t)(mx.Bound(0x200)); pv != preg {
		t.Fatalf("the region serves (%d)", pv)
	}
	ok(t, rg.Commit())
	if info := must[zxv.HwRegionInfo](t)(rg.Info()); info.State != zxv.HwRegionActive || info.Version != 1 || info.Floor != 1 {
		t.Fatalf("info %+v", info)
	}
	evMu.Lock()
	kinds := ""
	for _, e := range events {
		kinds += e.Kind.String() + " "
		if e.RegionID != 1 {
			t.Fatalf("event region %d", e.RegionID)
		}
	}
	lastVer := events[len(events)-1].Version
	evMu.Unlock()
	if kinds != "REJECTED STAGED TESTED ACTIVATED " || lastVer != 1 {
		t.Fatalf("events %q v%d", kinds, lastVer)
	}

	if err := rg.Stage(good); !errors.Is(err, zxv.ErrRollback) {
		t.Fatalf("replay: %v", err)
	}

	// Image build 2 of 3: signed, wrong gate. Quarantined, then blocked.
	spec2 := spec
	spec2.Version = 2
	bad := must[[]byte](t)(rel.Build(spec2, wrong, key))
	ok(t, rg.Stage(bad))
	if err := rg.Trial(5); !errors.Is(err, zxv.ErrQuarantined) {
		t.Fatalf("trial: %v", err)
	}
	if info := must[zxv.HwRegionInfo](t)(rg.Info()); info.State != zxv.HwRegionActive || info.Version != 1 {
		t.Fatalf("after rollback %+v", info)
	}
	img = append([]byte(nil), bad...)
	binary.LittleEndian.PutUint32(img[20:], 3)
	if err := rg.Stage(img); !errors.Is(err, zxv.ErrBlocked) {
		t.Fatalf("blocked: %v", err)
	}

	lg := must[zxv.HwLog](t)(rg.Log())
	entries := must[[]zxv.HwLogEntry](t)(rg.LogEntries())
	evMu.Lock()
	nev := len(events)
	evMu.Unlock()
	if uint32(len(entries)) != lg.Count || uint32(nev) != lg.Count {
		t.Fatalf("records %d events %d count %d", len(entries), nev, lg.Count)
	}
	last := entries[len(entries)-1]
	if last.Event != zxv.HwEvQuarantined || last.ID != 1 || last.Version != 2 || last.Hash != lg.Head || !last.Tag.IsForbidden() {
		t.Fatalf("last record %+v", last)
	}
	_, err = rg.LogEntry(lg.Count)
	wantStatus(t, err, zxv.StatusNotFound)

	// Image build 3 of 3: a newer golden-vector contract for "inc".
	pay := binary.LittleEndian.AppendUint32(nil, 0)
	pay = binary.LittleEndian.AppendUint32(pay, 2)
	pay = append(pay, 5, 250)
	pay = binary.LittleEndian.AppendUint32(pay, 2)
	pay = append(pay, 6, 251)
	pkg := must[[]byte](t)(rel.Build(zxv.HwImageSpec{Kind: zxv.HwImgVectors, FnID: 0x200, Version: 7, SVN: 3}, pay, key))
	_, err = mx.ApplyContract(pkg)
	wantStatus(t, err, zxv.StatusVerify) // no key trusted yet
	wantStatus(t, mx.TrustKey(key.Public[1:]), zxv.StatusArg)
	ok(t, mx.TrustKey(key.Public))
	_, err = mx.ApplyContract(good)
	wantStatus(t, err, zxv.StatusFormat)
	wantStatus(t, rg.Stage(pkg), zxv.StatusFormat)
	if proven := must[uint32](t)(mx.ApplyContract(pkg)); proven != 2 {
		t.Fatalf("proven %d", proven)
	}
	if fi := must[zxv.HwFunctionInfo](t)(mx.FunctionInfo(0x200)); fi.Contract != 7 || fi.Vectors != 1 {
		t.Fatalf("contract %+v", fi)
	}
	if _, err := mx.ApplyContract(pkg); !errors.Is(err, zxv.ErrRollback) {
		t.Fatalf("contract replay: %v", err)
	}
	found := false
	for _, e := range must[[]zxv.HwLogEntry](t)(mx.LogEntries()) {
		if e.Event == zxv.HwEvContract && e.ID == 0x200 && e.Version == 7 {
			found = true
		}
	}
	if !found {
		t.Fatal("contract record missing")
	}

	// Fill the log (12) with refusals; the next one halts the region and
	// poisons the context: fail closed.
	tampered := append([]byte(nil), good...)
	tampered[len(tampered)-1] ^= 1
	left := 12 - must[zxv.HwLog](t)(rg.Log()).Count
	for i := uint32(0); i < left; i++ {
		wantStatus(t, rg.Stage(tampered), zxv.StatusVerify)
	}
	if err := rg.Stage(tampered); !errors.Is(err, zxv.ErrFatal) {
		t.Fatalf("full log: %v", err)
	}
	if !ctx.Poisoned() {
		t.Fatal("context not poisoned")
	}
	_, err = mx.Call(0x200, 0, vin[:4])
	if !errors.Is(err, zxv.ErrFatal) {
		t.Fatalf("after poison: %v", err)
	}
}
