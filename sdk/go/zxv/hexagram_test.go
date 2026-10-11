// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

package zxv_test

// ABI 1.3: the 64-state hexagram tag and quarantine log against
// vectors.json "hex".

import (
	"crypto/sha256"
	"encoding/binary"
	"encoding/json"
	"errors"
	"os"
	"path/filepath"
	"reflect"
	"testing"

	"github.com/sykomiked/ZEDEC-XERO-VULPINE-V1.0.0/sdk/go/zxv"
)

type hexVectors struct {
	BaseNames     []string `json:"base_names"`
	PolarityNames []string `json:"polarity_names"`
	PhaseNames    []string `json:"phase_names"`
	ActionNames   []string `json:"action_names"`
	LPToBase      []int    `json:"lp_to_base"`
	BaseToLP      []int    `json:"base_to_lp"`
	PhaseNext     []int    `json:"phase_next"`
	Tags          []struct {
		Tag      uint8  `json:"tag"`
		Name     string `json:"name"`
		Base     int    `json:"base"`
		Polarity int    `json:"polarity"`
		Phase    int    `json:"phase"`
		Action   int    `json:"action"`
	} `json:"tags"`
	Not      []int   `json:"not"`
	And      [][]int `json:"and"`
	Or       [][]int `json:"or"`
	Step     [][]int `json:"step"`
	Set      [][]int `json:"set"`
	Polarity []struct {
		Pos0, Neg0, Pos1, Neg1 uint32
		Stale                  bool
		Polarity               int
	} `json:"polarity"`
	Qlog struct {
		Records []struct {
			Seq       uint64 `json:"seq"`
			Tag       uint8  `json:"tag"`
			Reason    uint8  `json:"reason"`
			Submitter uint32 `json:"submitter"`
			Subject   string `json:"subject"`
			Prev      string `json:"prev"`
			Hash      string `json:"hash"`
		} `json:"records"`
		Head string `json:"head"`
	} `json:"qlog"`
}

func loadHex(t *testing.T) *hexVectors {
	t.Helper()
	b, err := os.ReadFile(filepath.Join(conformanceDir, "vectors.json"))
	ok(t, err)
	var v struct {
		Hex hexVectors `json:"hex"`
	}
	ok(t, json.Unmarshal(b, &v))
	return &v.Hex
}

func TestHexTagsMatchVectors(t *testing.T) {
	v := loadHex(t)
	if !zxv.LiveFeatures().Has(zxv.FeatHex) {
		t.Fatal("FeatHex not live")
	}
	var bases, pols, phases, acts []string
	for i := 0; i < 4; i++ {
		bases = append(bases, zxv.HexBase(i).String())
		pols = append(pols, zxv.HexPolarity(i).String())
		phases = append(phases, zxv.HexPhase(i).String())
	}
	for i := 0; i < 5; i++ {
		acts = append(acts, zxv.HexAction(i).String())
	}
	for _, c := range []struct{ got, want []string }{
		{bases, v.BaseNames}, {pols, v.PolarityNames}, {phases, v.PhaseNames}, {acts, v.ActionNames},
	} {
		if !reflect.DeepEqual(c.got, c.want) {
			t.Errorf("names %v, want %v", c.got, c.want)
		}
	}
	for _, x := range v.Tags {
		h := must[zxv.Hexagram](t)(zxv.HexagramFromTag(x.Tag))
		if h.String() != x.Name || int(h.Base()) != x.Base || int(h.Polarity()) != x.Polarity ||
			int(h.Phase()) != x.Phase || int(h.Action()) != x.Action {
			t.Errorf("tag %d: %#v action %v", x.Tag, h, h.Action())
		}
		if h2 := must[zxv.Hexagram](t)(zxv.NewHexagram(h.Base(), h.Polarity(), h.Phase())); h2 != h {
			t.Errorf("pack %d = %d", x.Tag, h2.Tag())
		}
	}
	for p := zxv.HexPhase(0); p < 4; p++ {
		if n := must[zxv.HexPhase](t)(p.Next()); int(n) != v.PhaseNext[p] {
			t.Errorf("next %v = %v", p, n)
		}
	}
	if len(zxv.AllHexagrams()) != zxv.HexStates {
		t.Error("AllHexagrams")
	}
}

func TestHexLogicTables(t *testing.T) {
	v := loadHex(t)
	for _, a := range zxv.AllHexagrams() {
		if int(a.Not().Tag()) != v.Not[a.Tag()] {
			t.Errorf("not %d", a.Tag())
		}
		for _, b := range zxv.AllHexagrams() {
			if int(a.And(b).Tag()) != v.And[a.Tag()][b.Tag()] || int(a.Or(b).Tag()) != v.Or[a.Tag()][b.Tag()] {
				t.Errorf("and/or %d %d", a.Tag(), b.Tag())
			}
		}
	}
}

func TestHexStepAndWithState(t *testing.T) {
	v := loadHex(t)
	for _, h := range zxv.AllHexagrams() {
		for p := zxv.HexPhase(0); p < 4; p++ {
			want := v.Step[h.Tag()][p]
			n, err := h.Step(p)
			if want < 0 {
				if !errors.Is(err, zxv.ErrRefused) || statusOf(err) != zxv.StatusState {
					t.Errorf("step %d -> %v: %v", h.Tag(), p, err)
				}
			} else if err != nil || int(n.Tag()) != want {
				t.Errorf("step %d -> %v = %d, %v", h.Tag(), p, n.Tag(), err)
			}
		}
		for b := zxv.HexBase(0); b < 4; b++ {
			for pol := zxv.HexPolarity(0); pol < 4; pol++ {
				want := v.Set[h.Tag()][int(b)*4+int(pol)]
				n, err := h.WithState(b, pol)
				if want < 0 {
					if !errors.Is(err, zxv.ErrRefused) {
						t.Errorf("set %d %v %v: %v", h.Tag(), b, pol, err)
					}
				} else if err != nil || int(n.Tag()) != want {
					t.Errorf("set %d %v %v = %d, %v", h.Tag(), b, pol, n.Tag(), err)
				}
			}
		}
		if f := must[zxv.Hexagram](t)(zxv.NewHexagram(h.Base(), zxv.HexForbidden, h.Phase())); h.Forbid() != f {
			t.Errorf("forbid %d", h.Tag())
		}
		if next, _ := h.Phase().Next(); h.Advance().Phase() != next {
			t.Errorf("advance %d", h.Tag())
		}
	}
}

func TestHexPolarityAndTruthBridge(t *testing.T) {
	v := loadHex(t)
	for _, c := range v.Polarity {
		if p := zxv.HexPolarityFromEvidence(c.Pos0, c.Neg0, c.Pos1, c.Neg1, c.Stale); int(p) != c.Polarity {
			t.Errorf("polarity %+v = %v", c, p)
		}
	}
	for _, tr := range zxv.AllTruths {
		h := must[zxv.Hexagram](t)(zxv.HexagramFromTruth(tr, zxv.HexNeutral, zxv.HexPotential))
		if int(h.Base()) != v.LPToBase[tr.Raw()] || h.ToTruth() != tr {
			t.Errorf("truth %v -> %#v", tr, h)
		}
	}
	for b := zxv.HexBase(0); b < 4; b++ {
		h := must[zxv.Hexagram](t)(zxv.NewHexagram(b, zxv.HexNeutral, zxv.HexPotential))
		if h.ToTruth().Raw() != v.BaseToLP[b] {
			t.Errorf("base %v -> %v", b, h.ToTruth())
		}
	}
	// The numbering differs on purpose: TruthTrue is 1, HexBaseTrue is 3.
	if zxv.TruthTrue.Raw() != 1 || zxv.HexBaseTrue != 3 {
		t.Error("numbering")
	}
}

func TestHexRefusalsAndValueSemantics(t *testing.T) {
	for _, bad := range []uint8{64, 100, 255} {
		if _, err := zxv.HexagramFromTag(bad); !errors.Is(err, zxv.ErrUsage) || statusOf(err) != zxv.StatusArg {
			t.Errorf("tag %d: %v", bad, err)
		}
	}
	if _, err := zxv.NewHexagram(4, 0, 0); !errors.Is(err, zxv.ErrUsage) {
		t.Errorf("pack base 4: %v", err)
	}
	if _, err := zxv.HexPhase(4).Next(); !errors.Is(err, zxv.ErrUsage) {
		t.Errorf("next 4: %v", err)
	}
	h := must[zxv.Hexagram](t)(zxv.NewHexagram(zxv.HexBaseTrue, zxv.HexNegentropic, zxv.HexManifest))
	if h.Tag() != 63 || h.String() != "true/negentropic/manifest" {
		t.Errorf("%#v", h)
	}
	var zero zxv.Hexagram
	if zero.String() != "false/entropic/potential" {
		t.Errorf("zero value %v", zero)
	}
	f := must[zxv.Hexagram](t)(zxv.NewHexagram(zxv.HexBaseTrue, zxv.HexForbidden, zxv.HexPotential))
	if f.Action() != zxv.HexRollback {
		t.Error("forbidden action")
	}
	for i := 0; i < 4; i++ {
		if f = f.Advance(); !f.IsForbidden() {
			t.Error("forbidden is absorbing")
		}
	}
	if !f.And(h).IsForbidden() || !f.Or(h).IsForbidden() || !f.Not().IsForbidden() {
		t.Error("forbidden dominates")
	}
	if _, err := f.WithState(zxv.HexBaseTrue, zxv.HexNeutral); !errors.Is(err, zxv.ErrRefused) {
		t.Errorf("unforbid: %v", err)
	}
}

func TestQuarantineChainMatchesVectors(t *testing.T) {
	q := loadHex(t).Qlog
	_, cap := env(t)
	log := must[*zxv.QuarantineLog](t)(zxv.NewQuarantineLog(cap, 3))
	defer func() { ok(t, log.Close()) }()
	if n := must[uint32](t)(log.Count()); n != 0 {
		t.Fatal(n)
	}
	var prev [32]byte
	if h := must[[32]byte](t)(log.Head()); h != prev {
		t.Fatal("empty head")
	}
	for _, r := range q.Records {
		subj := arr32(t, r.Subject)
		tag := must[zxv.Hexagram](t)(zxv.HexagramFromTag(r.Tag))
		seq := must[uint64](t)(log.Append(tag, r.Reason, r.Submitter, subj))
		if seq != r.Seq {
			t.Fatalf("seq %d", seq)
		}
		buf := []byte("zxv/lpx-qlog/v1")
		buf = binary.LittleEndian.AppendUint64(buf, seq)
		buf = append(buf, r.Tag, r.Reason)
		buf = binary.LittleEndian.AppendUint32(buf, r.Submitter)
		buf = append(append(buf, subj[:]...), prev[:]...)
		want := sha256.Sum256(buf)
		if want != arr32(t, r.Hash) {
			t.Fatalf("vector hash %d", seq)
		}
		rec := must[zxv.QuarantineRecord](t)(log.Record(uint32(seq)))
		if rec.Hash != want || rec.Prev != prev || rec.Subject != subj || rec.Tag != tag ||
			rec.Reason != r.Reason || rec.Submitter != r.Submitter || rec.Seq != seq {
			t.Fatalf("record %d: %+v", seq, rec)
		}
		prev = want
	}
	if h := must[[32]byte](t)(log.Head()); h != arr32(t, q.Head) {
		t.Fatal("head")
	}
	if rs := must[[]zxv.QuarantineRecord](t)(log.Records()); len(rs) != 3 || rs[2].Seq != 2 {
		t.Fatal("records")
	}
	ok(t, log.Verify())
	_, err := log.Append(must[zxv.Hexagram](t)(zxv.HexagramFromTag(0x3B)), 0, 0, [32]byte{})
	if !errors.Is(err, zxv.ErrRefused) || statusOf(err) != zxv.StatusCapacity {
		t.Fatalf("full: %v", err)
	}
}

func TestQuarantineRefusals(t *testing.T) {
	forb := must[zxv.Hexagram](t)(zxv.NewHexagram(zxv.HexBaseBoth, zxv.HexForbidden, zxv.HexEmerging))
	ctx, cap := env(t)
	noAttest := must[*zxv.Capability](t)(cap.Attenuate(zxv.CapAll &^ zxv.CapAttest))
	defer func() { _ = noAttest.Close() }()
	if _, err := zxv.NewQuarantineLog(noAttest, 4); !errors.Is(err, zxv.ErrCapabilityDenied) {
		t.Errorf("no attest: %v", err)
	}
	for _, c := range []uint32{0, zxv.QuarantineMaxRecords + 1} {
		if _, err := zxv.NewQuarantineLog(cap, c); !errors.Is(err, zxv.ErrUsage) {
			t.Errorf("capacity %d: %v", c, err)
		}
	}
	log := must[*zxv.QuarantineLog](t)(zxv.NewQuarantineLog(cap, 4))
	defer func() { _ = log.Close() }()
	plain := must[zxv.Hexagram](t)(zxv.NewHexagram(zxv.HexBaseTrue, zxv.HexNeutral, zxv.HexEmerging))
	if _, err := log.Append(plain, 1, 1, [32]byte{}); !errors.Is(err, zxv.ErrUsage) {
		t.Errorf("not forbidden: %v", err)
	}
	if seq := must[uint64](t)(log.Append(forb, 9, 0xFFFFFFFF, [32]byte{})); seq != 0 {
		t.Error(seq)
	}
	if r := must[zxv.QuarantineRecord](t)(log.Record(0)); r.Submitter != 0xFFFFFFFF {
		t.Error(r.Submitter)
	}
	ok(t, ctx.Halt("test"))
	if _, err := log.Count(); !errors.Is(err, zxv.ErrFatal) {
		t.Errorf("after halt: %v", err)
	}
}
