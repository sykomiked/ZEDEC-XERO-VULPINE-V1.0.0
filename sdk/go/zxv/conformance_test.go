// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

package zxv_test

import (
	"bytes"
	"context"
	"errors"
	"math"
	"os"
	"path/filepath"
	"slices"
	"strconv"
	"testing"

	"github.com/sykomiked/ZEDEC-XERO-VULPINE-V1.0.0/sdk/go/zxv"
)

// These tests check the SDK against sdk/conformance/vectors.json, the
// answer key shared by every ZXV SDK.

func statusOf(err error) int32 {
	var e *zxv.Error
	if errors.As(err, &e) {
		return e.Status
	}
	if err == nil {
		return 0
	}
	return math.MinInt32
}

func TestABIVersion(t *testing.T) {
	v := loadVectors(t)
	major, minor, _ := zxv.Version()
	if int(major) != v.ABI[0] || minor < 4 {
		t.Fatalf("ABI %d.%d", major, minor)
	}
}

func TestStatusNamesAndClasses(t *testing.T) {
	for _, s := range loadVectors(t).Status {
		if got := zxv.StatusName(s.Code); got != s.Name {
			t.Errorf("name %d = %s, want %s", s.Code, got, s.Name)
		}
		if got := zxv.ClassOf(s.Code); int(got) != s.Class {
			t.Errorf("class %s = %v, want %d", s.Name, got, s.Class)
		}
		if s.Code == 0 {
			continue
		}
		err := zxv.NewError(s.Code, "")
		var e *zxv.Error
		if !errors.As(err, &e) || int(e.Class) != s.Class || e.Name != s.Name {
			t.Errorf("NewError(%d) = %v", s.Code, err)
		}
		sentinel := map[int]error{1: zxv.ErrUsage, 2: zxv.ErrRefused, 3: zxv.ErrFatal, 4: zxv.ErrSystem}[s.Class]
		if !errors.Is(err, sentinel) {
			t.Errorf("%s does not match its class sentinel", s.Name)
		}
		switch s.Class {
		case 1:
			var u *zxv.UsageError
			if !errors.As(err, &u) {
				t.Errorf("%s is not a *UsageError", s.Name)
			}
		case 2:
			var r *zxv.RefusedError
			if !errors.As(err, &r) {
				t.Errorf("%s is not a *RefusedError", s.Name)
			}
		case 3:
			var f *zxv.FatalError
			if !errors.As(err, &f) {
				t.Errorf("%s is not a *FatalError", s.Name)
			}
		case 4:
			var y *zxv.SystemError
			if !errors.As(err, &y) {
				t.Errorf("%s is not a *SystemError", s.Name)
			}
		}
	}
	if !errors.Is(zxv.NewError(zxv.StatusBudget, ""), zxv.ErrBudgetExhausted) {
		t.Error("ErrBudgetExhausted")
	}
}

func TestRationalOps(t *testing.T) {
	for _, c := range loadVectors(t).RationalOps {
		a, b := rat(t, c.A), rat(t, c.B)
		for name, pair := range map[string]struct {
			f    func(zxv.Rational) (zxv.Rational, error)
			want [2]int64
		}{"add": {a.Add, c.Add}, "sub": {a.Sub, c.Sub}, "mul": {a.Mul, c.Mul}, "div": {a.Div, c.Div}} {
			got, err := pair.f(b)
			if err != nil || !got.Equal(rat(t, pair.want)) {
				t.Errorf("%v %s %v = %v, %v; want %v", a, name, b, got, err, pair.want)
			}
		}
		if a.Cmp(b) != c.Cmp {
			t.Errorf("cmp %v %v", a, b)
		}
	}
}

func TestRationalParseAndFormat(t *testing.T) {
	for _, c := range loadVectors(t).RationalParse {
		r, err := zxv.ParseRational(c.Text)
		if statusOf(err) != c.Status {
			t.Errorf("parse %q: status %d, want %d (%v)", c.Text, statusOf(err), c.Status, err)
			continue
		}
		if err != nil {
			if !errors.Is(err, zxv.ErrUsage) {
				t.Errorf("parse %q: %v is not USAGE", c.Text, err)
			}
			continue
		}
		if !r.Equal(rat(t, c.Value)) || r.String() != c.Format {
			t.Errorf("parse %q = %v (%s)", c.Text, r, r.String())
		}
	}
}

func TestIEEE754Boundary(t *testing.T) {
	for _, c := range loadVectors(t).IEEE754 {
		bits, _ := strconv.ParseUint(c.Bits, 16, 64)
		r, err := zxv.FromFloat64Exact(math.Float64frombits(bits))
		if statusOf(err) != c.Status {
			t.Errorf("%s: status %d want %d", c.Bits, statusOf(err), c.Status)
			continue
		}
		if err == nil && !r.Equal(rat(t, c.Value)) {
			t.Errorf("%s = %v", c.Bits, r)
		}
	}
	tenth := must[zxv.Rational](t)(zxv.FromFloat64Exact(0.1))
	if tenth.Equal(rat(t, [2]int64{1, 10})) {
		t.Error("0.1 must not be 1/10")
	}
	if _, err := zxv.FromFloat64Exact(math.NaN()); !errors.Is(err, zxv.ErrFloatRejected) {
		t.Errorf("NaN: %v", err)
	}
}

func TestQ16(t *testing.T) {
	for _, c := range loadVectors(t).Q16 {
		q, err := rat(t, c.Value).ToQ16()
		if statusOf(err) != c.Status || (err == nil && q != c.Q16) {
			t.Errorf("%v -> %d, %v", c.Value, q, err)
		}
	}
}

func TestLPResTables(t *testing.T) {
	l := loadVectors(t).LPRes
	for i, a := range zxv.AllTruths {
		if a.String() != l.Names[i] || a.Not().Raw() != l.Not[i] {
			t.Errorf("%v", a)
		}
		for j, b := range zxv.AllTruths {
			if a.And(b).Raw() != l.And[i][j] {
				t.Errorf("%v and %v = %v", a, b, a.And(b))
			}
			if a.Or(b).Raw() != l.Or[i][j] {
				t.Errorf("%v or %v = %v", a, b, a.Or(b))
			}
		}
	}
	if zxv.TruthFromEvidence(2, 1) != zxv.TruthBoth || zxv.TruthFromEvidence(0, 0) != zxv.TruthNeither {
		t.Error("evidence")
	}
}

func TestUBH168FramesByteForByte(t *testing.T) {
	v := loadVectors(t)
	for _, c := range v.UBH168 {
		tag := must[uint8](t)(zxv.WireTag(c.Trunk, c.Shell, c.Sync))
		if tag != c.Tag {
			t.Errorf("tag %d want %d", tag, c.Tag)
		}
		pure := zxv.PackFrame(tag, c.SPlus, c.SMinus)
		native := must[zxv.Ubh168Frame](t)(zxv.PackFrameNative(tag, c.SPlus, c.SMinus))
		if !bytes.Equal(pure[:], unhex(t, c.Frame)) {
			t.Errorf("pure Go frame %s want %s", pure, c.Frame)
		}
		if pure != native {
			t.Errorf("pure %s != native %s", pure, native)
		}
		gt, sp, sm := pure.Unpack()
		if gt != tag || sp != c.SPlus || sm != c.SMinus {
			t.Error("unpack")
		}
		if tr := must[zxv.WireTruth](t)(pure.Resolve()); int32(tr) != c.Truth {
			t.Errorf("truth %d want %d", tr, c.Truth)
		}
	}
	for trunk, want := range v.UBH168Idle {
		f := must[zxv.Ubh168Frame](t)(zxv.IdleFrame(uint32(trunk)))
		if f.String() != want {
			t.Errorf("idle %d = %s", trunk, f)
		}
	}
	if _, err := zxv.WireTag(10, 0, 0); !errors.Is(err, zxv.ErrUsage) {
		t.Error("tag range")
	}
}

func TestHashes(t *testing.T) {
	for _, c := range loadVectors(t).Hash {
		m := []byte(c.UTF8)
		k, s := zxv.Keccak256(m), zxv.SHA3256(m)
		if !bytes.Equal(k[:], unhex(t, c.Keccak256)) || !bytes.Equal(s[:], unhex(t, c.SHA3256)) ||
			!bytes.Equal(zxv.SHAKE256(m, 64), unhex(t, c.SHAKE64)) {
			t.Errorf("hash %q", c.UTF8)
		}
	}
}

func TestEd25519Vector(t *testing.T) {
	v := loadVectors(t).Ed25519
	_, cap := env(t)
	ed := zxv.NewEd25519(cap)
	sk := zxv.SecretKeyFromBytes(unhex(t, v.SK))
	defer sk.Close()
	pk := must[[32]byte](t)(ed.PublicKey(sk))
	if !bytes.Equal(pk[:], unhex(t, v.PK)) {
		t.Fatal("public key")
	}
	sig := must[[64]byte](t)(ed.Sign(sk, []byte(v.Msg)))
	if !bytes.Equal(sig[:], unhex(t, v.Sig)) {
		t.Fatal("signature")
	}
	ok(t, ed.Verify(pk, []byte(v.Msg), sig))
	sig[0] ^= 1
	if err := ed.Verify(pk, []byte(v.Msg), sig); !errors.Is(err, zxv.ErrVerificationFailed) {
		t.Fatalf("tampered: %v", err)
	}
}

func TestMLDSA87Vector(t *testing.T) {
	v := loadVectors(t).MLDSA87
	_, cap := env(t)
	d := zxv.NewMLDSA87(cap)
	pk, sk, err := d.KeyGenFromSeed(arr32(t, v.Seed))
	ok(t, err)
	defer sk.Close()
	skb := must[[]byte](t)(sk.ExportBytesUnsafe())
	if h := zxv.SHA3256(pk); !bytes.Equal(h[:], unhex(t, v.PKHash)) {
		t.Fatal("pk digest")
	}
	if h := zxv.SHA3256(skb); !bytes.Equal(h[:], unhex(t, v.SKHash)) {
		t.Fatal("sk digest")
	}
	sig := must[[]byte](t)(d.SignDeterministic(sk, []byte(v.Msg), []byte(v.Ctx)))
	if h := zxv.SHA3256(sig); !bytes.Equal(h[:], unhex(t, v.SigHash)) {
		t.Fatal("sig digest")
	}
	ok(t, d.Verify(pk, []byte(v.Msg), []byte(v.Ctx), sig))
	hedged := must[[]byte](t)(d.Sign(sk, []byte(v.Msg), []byte(v.Ctx)))
	ok(t, d.Verify(pk, []byte(v.Msg), []byte(v.Ctx), hedged))
	if err := d.Verify(pk, []byte(v.Msg), []byte("zxw"), sig); !errors.Is(err, zxv.ErrVerificationFailed) {
		t.Fatalf("wrong context: %v", err)
	}
}

func TestMLKEM1024Vector(t *testing.T) {
	v := loadVectors(t).MLKEM1024
	_, cap := env(t)
	k := zxv.NewMLKEM1024(cap)
	ek, dk, err := k.KeyGenDerand(arr32(t, v.D), arr32(t, v.Z))
	ok(t, err)
	defer dk.Close()
	if h := zxv.SHA3256(ek); !bytes.Equal(h[:], unhex(t, v.EKHash)) {
		t.Fatal("ek digest")
	}
	ct, ss, err := k.EncapsulateDerand(ek, arr32(t, v.M))
	ok(t, err)
	defer ss.Close()
	if h := zxv.SHA3256(ct); !bytes.Equal(h[:], unhex(t, v.CTHash)) {
		t.Fatal("ct digest")
	}
	if b := must[[]byte](t)(ss.ExportBytesUnsafe()); !bytes.Equal(b, unhex(t, v.SS)) {
		t.Fatal("shared secret")
	}
	ss2 := must[*zxv.SecretKey](t)(k.Decapsulate(dk, ct))
	defer ss2.Close()
	if !ss.Equal(ss2) {
		t.Fatal("decapsulation")
	}
	bad := slices.Clone(ek)
	for i := 0; i < 32; i++ {
		bad[i] = 0xFF
	}
	if _, _, err := k.Encapsulate(bad); zxv.StatusName(statusOf(err)) != "ZXV_E_FORMAT" {
		t.Fatalf("malformed ek: %v", err)
	}
}

func TestSwarmAllotments(t *testing.T) {
	v := loadVectors(t).Swarm
	_, cap := env(t)
	s := must[*zxv.Swarm](t)(zxv.NewSwarm(cap, v.Levels, v.TokensPerCycle))
	defer s.Close()
	for _, m := range v.Models {
		ok(t, s.Register(m[0], m[1]))
	}
	ok(t, s.SetMood(zxv.Feeling{Emoji: rune(v.Mood[0]), Intensity: v.Mood[1]}))
	for _, f := range v.Feelings {
		ok(t, s.SetFeeling(f[0], zxv.Feeling{Emoji: rune(f[1]), Intensity: f[2]}))
	}
	must[uint64](t)(s.BeginCycle())
	var total uint64
	for _, a := range v.Allotments {
		al := must[zxv.Allotment](t)(s.Allotment(uint32(a[0])))
		if al.Allotted != a[1] || al.Imaginary != a[2] {
			t.Errorf("model %d: %+v want %v", a[0], al, a)
		}
		total += al.Allotted
	}
	if total != v.TokensPerCycle {
		t.Errorf("total %d", total)
	}
	for lvl, c := range v.LevelCapacity {
		if got := must[uint32](t)(zxv.SwarmLevelCapacity(uint32(lvl))); got != c {
			t.Errorf("level %d capacity %d want %d", lvl, got, c)
		}
	}
	al := must[zxv.Allotment](t)(s.Allotment(100))
	if g := must[zxv.Grant](t)(s.Consume(100, 10)); g.Partial() {
		t.Error("10 should be granted in full")
	}
	g := must[zxv.Grant](t)(s.Consume(100, al.Allotted))
	if !g.Partial() || g.Granted != al.Allotted-10 {
		t.Errorf("over-ask grant %+v", g)
	}
	must[uint64](t)(s.EndCycle())
	if err := s.Register(999, 0); !errors.Is(err, zxv.ErrRefused) || statusOf(err) != zxv.StatusCapacity {
		t.Errorf("full level: %v", err)
	}
	if err := s.SetMood(zxv.Feeling{Emoji: 0x1F600, Intensity: 1}); !errors.Is(err, zxv.ErrNotFound) {
		t.Errorf("emoji outside palette: %v", err)
	}
	must[zxv.EmotionProfile](t)(zxv.SwarmEmotionProfile(0x1F914))
}

func TestEVMStateEncoding(t *testing.T) {
	v := loadVectors(t).EVM
	ctx, cap := env(t)
	ch := zxv.NewStateChannel(cap)
	var parties []zxv.Address
	for _, a := range v.Addresses {
		var ad zxv.Address
		copy(ad[:], unhex(t, a))
		parties = append(parties, ad)
	}
	st := must[zxv.EVMState](t)(ch.EncodeState(arr32(t, v.ChannelID), v.Nonce, parties, v.Balances))
	if !bytes.Equal(st.Encoding, unhex(t, v.Encoding)) || !bytes.Equal(st.Digest[:], unhex(t, v.Digest)) {
		t.Fatal("encoding/digest")
	}
	if zxv.Keccak256(st.Encoding) != st.Digest {
		t.Fatal("digest is keccak256(encoding)")
	}
	ok(t, ctx.EnableCurrencyWithMinorCap(cap, "USD", 2))
	l := zxv.NewLedger(cap)
	a := must[zxv.AccountID](t)(l.OpenAccount("a", "USD", zxv.Issuer))
	b := must[zxv.AccountID](t)(l.OpenAccount("b", "USD", zxv.Holder))
	must[uint64](t)(l.Post(a, b, 250, "USD", "x"))
	addr := []zxv.Address{{1}, {2}}
	fromLedger := must[zxv.EVMState](t)(ch.LedgerState([32]byte{}, 1, "USD", []zxv.AccountID{a, b}, addr))
	explicit := must[zxv.EVMState](t)(ch.EncodeState([32]byte{}, 1, addr, []int64{-250, 250}))
	if !bytes.Equal(fromLedger.Encoding, explicit.Encoding) || len(explicit.Encoding) != 320 {
		t.Fatal("ledger state == explicit balances")
	}
}

func TestGenerationMatchesReferenceTokens(t *testing.T) {
	if !zxv.LiveFeatures().Has(zxv.FeatTensor) {
		t.Skip("engine not built")
	}
	v := loadVectors(t).Generate
	_, cap := env(t)
	m := must[*zxv.Model](t)(zxv.OpenModel(cap, filepath.Join(fixtures(), v.Model), filepath.Join(fixtures(), v.Tokenizer)))
	defer m.Close()
	prompt := string(must[[]byte](t)(os.ReadFile(filepath.Join(fixtures(), v.Prompt))))
	ids := must[[]int32](t)(m.Tokenize(prompt, true))
	if !slices.Equal(ids, v.PromptTokens) {
		t.Fatalf("prompt tokens %v", ids)
	}
	if back := must[string](t)(m.Detokenize(ids)); back != prompt {
		t.Fatalf("detokenize %q", back)
	}
	s := must[*zxv.Session](t)(m.NewSession(cap, v.NCtx, zxv.KVQ16))
	defer s.Close()
	g := must[*zxv.Generation](t)(s.Generate(context.Background(), prompt, zxv.Greedy(v.Greedy.MaxTokens)))
	if !slices.Equal(g.Tokens, v.Greedy.Tokens) || g.Stop != zxv.StopMax || int(g.PromptTokens) != len(ids) {
		t.Fatalf("greedy %v stop %v", g.Tokens, g.Stop)
	}
	q := func(x int64) zxv.Rational { return rat(t, [2]int64{x, 65536}) }
	sm := v.Sampled
	opts := zxv.GenerateOptions{MaxTokens: sm.MaxTokens, Temperature: q(sm.TemperatureQ16), TopK: sm.TopK,
		TopP: q(sm.TopPQ16), RepeatPenalty: q(sm.RepeatPenaltyQ16), RepeatLastN: sm.RepeatLastN, Seed: sm.Seed}
	ok(t, s.Reset())
	g = must[*zxv.Generation](t)(s.Generate(context.Background(), prompt, opts))
	if !slices.Equal(g.Tokens, sm.Tokens) {
		t.Fatalf("sampled %v", g.Tokens)
	}
	if q16 := must[int32](t)(rat(t, [2]int64{7, 10}).ToQ16()); int64(q16) != sm.TemperatureQ16 {
		t.Fatal("0.7 -> Q16")
	}
}
