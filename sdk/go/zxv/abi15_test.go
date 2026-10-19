// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

package zxv_test

import (
	"bytes"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"strconv"
	"sync"
	"testing"

	"github.com/sykomiked/ZEDEC-XERO-VULPINE-V1.0.0/sdk/go/zxv"
)

// The ABI 1.5 sections of vectors.json are read as plain JSON: 64-bit
// numbers are decimal strings.

var (
	v15Once sync.Once
	v15Data map[string]any
	v15Err  error
)

func v15(t *testing.T, key string) map[string]any {
	t.Helper()
	v15Once.Do(func() {
		b, err := os.ReadFile(filepath.Join(conformanceDir, "vectors.json"))
		if err != nil {
			v15Err = err
			return
		}
		v15Err = json.Unmarshal(b, &v15Data)
	})
	if v15Err != nil {
		t.Fatal(v15Err)
	}
	m, isMap := v15Data[key].(map[string]any)
	if !isMap {
		t.Fatalf("vectors.json has no %q section", key)
	}
	return m
}

func jm(x any) map[string]any { m, _ := x.(map[string]any); return m }
func ja(x any) []any          { a, _ := x.([]any); return a }
func js(x any) string         { s, _ := x.(string); return s }
func jb(x any) bool           { b, _ := x.(bool); return b }

func ji(x any) int64 {
	switch v := x.(type) {
	case float64:
		return int64(v)
	case string:
		n, err := strconv.ParseInt(v, 10, 64)
		if err != nil {
			panic(err)
		}
		return n
	}
	panic(fmt.Sprintf("not a number: %v", x))
}

func ju(x any) uint64 {
	switch v := x.(type) {
	case float64:
		return uint64(v)
	case string:
		n, err := strconv.ParseUint(v, 10, 64)
		if err != nil {
			panic(err)
		}
		return n
	}
	panic(fmt.Sprintf("not a number: %v", x))
}

func jhex(t *testing.T, x any) []byte { t.Helper(); return unhex(t, js(x)) }

func i64s(x any) []int64 {
	var out []int64
	for _, e := range ja(x) {
		out = append(out, ji(e))
	}
	return out
}

// stName names an error's status ("ZXV_OK" for nil).
func stName(err error) string {
	if err == nil {
		return "ZXV_OK"
	}
	return zxv.StatusName(statusOf(err))
}

func wantSt(t *testing.T, label string, err error, want any) {
	t.Helper()
	if got := stName(err); got != js(want) {
		t.Fatalf("%s: status %s (%v), want %s", label, got, err, js(want))
	}
}

func ramp(n int) []byte {
	b := make([]byte, n)
	for i := range b {
		b[i] = byte(i)
	}
	return b
}

// env15 opens a context with every ABI 1.5 capability bit.
func env15(t *testing.T) (*zxv.Context, *zxv.Capability) {
	t.Helper()
	ctx := must[*zxv.Context](t)(zxv.NewContext())
	cap := must[*zxv.Capability](t)(ctx.RootCapability(1, nil, zxv.CapAll15))
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

// ===== statuses, caps, features ============================================

func TestABI15StatusesCapsFeatures(t *testing.T) {
	if _, minor, _ := zxv.Version(); minor < 5 {
		t.Fatalf("minor %d", minor)
	}
	sentinels := map[int32]error{
		zxv.StatusDenom: zxv.ErrDenom, zxv.StatusTreaty: zxv.ErrTreaty, zxv.StatusCID: zxv.ErrCID,
		zxv.StatusConsent: zxv.ErrConsent, zxv.StatusErasure: zxv.ErrErasure, zxv.StatusPeg: zxv.ErrPeg,
		zxv.StatusLimit: zxv.ErrLimit, zxv.StatusParadoxStep: zxv.ErrParadoxStep, zxv.StatusParadoxOwner: zxv.ErrParadoxOwner,
	}
	rows := ja(v15Data0(t)["status_abi15"])
	if len(rows) != 9 {
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
		if !errors.As(err, &e) || e.Status != code || int(e.Class) != class {
			t.Fatalf("NewError(%d) = %v", code, err)
		}
		classSentinel := map[int]error{1: zxv.ErrUsage, 2: zxv.ErrRefused}[class]
		if !errors.Is(err, classSentinel) || !errors.Is(err, sentinels[code]) {
			t.Fatalf("%s sentinels", name)
		}
		for other, s := range sentinels {
			if other != code && errors.Is(err, s) {
				t.Fatalf("%s matches the sentinel of %d", name, other)
			}
		}
	}
	if zxv.CapAIPI != 1<<12 || zxv.CapMint != 1<<13 || zxv.CapParadox != 1<<14 || zxv.CapAll15 != 1<<15-1 {
		t.Fatal("cap bits")
	}
	if !zxv.CapAll15.Has(zxv.CapAll14|zxv.CapAIPI|zxv.CapMint|zxv.CapParadox) || zxv.CapAll14.Has(zxv.CapAIPI) {
		t.Fatal("cap sets")
	}
	if s := (zxv.CapAIPI | zxv.CapParadox).String(); s != "AIPI|PARADOX" {
		t.Fatalf("caps string %q", s)
	}
	f2 := zxv.LiveFeatures2()
	for _, f := range []zxv.Features2{zxv.Feat2Treaty, zxv.Feat2AIPI, zxv.Feat2Coin, zxv.Feat2Fee, zxv.Feat2VHub,
		zxv.Feat2Freight, zxv.Feat2Web4, zxv.Feat2Paradox} {
		if !f2.Has(f) {
			t.Fatalf("feature2 %#x missing from %#x", f, f2)
		}
	}
	ctx := must[*zxv.Context](t)(zxv.NewContext())
	defer ctx.Close()
	c := must[*zxv.Capability](t)(ctx.RootCapability(1, nil, zxv.CapAll15))
	ok(t, c.Check(zxv.CapAIPI|zxv.CapMint|zxv.CapParadox))
	ok(t, c.Close())
	if _, err := ctx.RootCapability(1, nil, zxv.CapAll16+1); statusOf(err) != zxv.StatusArg {
		t.Fatalf("unknown bit 16 accepted: %v", err)
	}
}

func v15Data0(t *testing.T) map[string]any {
	v15(t, "fee")
	return v15Data
}

// ===== treaty ==============================================================

func TestTreatyVectors(t *testing.T) {
	v := v15(t, "treaty")
	if int(ji(v["rec_len"])) != zxv.TreatyRecordLen || int(ji(v["result_len"])) != zxv.TreatyResultLen {
		t.Fatal("lengths")
	}
	for k, name := range jm(v["op_names"]) {
		if zxv.TreatyOp(ji(k)).String() != js(name) {
			t.Fatalf("op %s", k)
		}
	}
	var rec0 []byte
	for i, s := range ja(v["sutra"]) {
		m := jm(s)
		tr, err := zxv.ParseTreatySutra(js(m["sutra"]))
		wantSt(t, fmt.Sprint("sutra ", i), err, m["status"])
		rec := must[[]byte](t)(tr.Record())
		if !bytes.Equal(rec, jhex(t, m["record"])) {
			t.Fatalf("sutra %d record", i)
		}
		if i == 0 {
			rec0 = rec
		}
		u := must[*zxv.Treaty](t)(zxv.ParseTreatyRecord(rec))
		if eq := must[bool](t)(tr.Equal(u)); eq != jb(m["record_parses_equal"]) {
			t.Fatalf("sutra %d equal", i)
		}
		if got := must[string](t)(u.Sutra()); got != js(m["reemit"]) {
			t.Fatalf("sutra %d reemit %q", i, got)
		}
		if must[string](t)(tr.Name()) != js(m["name"]) || must[string](t)(tr.Asset()) != js(m["asset"]) {
			t.Fatalf("sutra %d name/asset", i)
		}
		op, np, no, err := tr.Header()
		ok(t, err)
		if int64(op) != ji(m["op"]) || int64(np) != ji(m["party_count"]) || int64(no) != ji(m["owe_count"]) {
			t.Fatalf("sutra %d header", i)
		}
		parties := must[[]zxv.TreatyParty](t)(tr.Parties())
		for p, pp := range ja(m["parties"]) {
			pm := jm(pp)
			w := ja(pm["weight"])
			got := parties[p]
			if got.ID != js(pm["id"]) || got.Deposit != ji(pm["deposit"]) || got.Claim != ji(pm["claim"]) ||
				int64(got.WeightNum) != ji(w[0]) || int64(got.WeightDen) != ji(w[1]) {
				t.Fatalf("sutra %d party %d: %+v", i, p, got)
			}
		}
		owes := must[[]zxv.TreatyOwe](t)(tr.Owes())
		for o, oo := range ja(m["owes"]) {
			om := jm(oo)
			if int64(owes[o].From) != ji(om["from"]) || int64(owes[o].To) != ji(om["to"]) || owes[o].Amount != ji(om["amount"]) {
				t.Fatalf("sutra %d owe %d", i, o)
			}
		}
		if f, err := tr.Validate(); err != nil || f != zxv.TreatyFaultNone {
			t.Fatalf("validate %v %v", f, err)
		}
		ok(t, u.Close())
		ok(t, tr.Close())
	}
	for i, r := range ja(v["reject"]) {
		m := jm(r)
		_, err := zxv.ParseTreatySutra(js(m["sutra"]))
		wantSt(t, fmt.Sprint("reject ", i), err, m["status"])
		var te *zxv.TreatyError
		if !errors.As(err, &te) || int64(te.Fault) != ji(m["fault"]) || int64(te.Offset) != ji(m["offset"]) {
			t.Fatalf("reject %d: %v", i, err)
		}
		if !errors.Is(err, zxv.ErrUsage) {
			t.Fatalf("reject %d class", i)
		}
	}
	for i, r := range ja(v["reject_record"]) {
		m := jm(r)
		rec := append([]byte(nil), rec0...)
		rec[ji(m["patch_offset"])] = jhex(t, m["patch_byte"])[0]
		_, err := zxv.ParseTreatyRecord(rec)
		wantSt(t, fmt.Sprint("reject_record ", i), err, m["status"])
		var te *zxv.TreatyError
		if !errors.As(err, &te) || int64(te.Fault) != ji(m["fault"]) || int64(te.Offset) != ji(m["offset"]) {
			t.Fatalf("reject_record %d: %v", i, err)
		}
	}
	for i, p := range ja(v["pic"]) {
		m := jm(p)
		a, b := ji(m["a"]), ji(m["b"])
		var out, rem int64
		var err error
		switch js(m["op"]) {
		case "add":
			out, err = zxv.PicAdd(a, b)
		case "sub":
			out, err = zxv.PicSub(a, b)
		case "mul":
			out, err = zxv.PicMul(a, b)
		default:
			out, rem, err = zxv.PicDiv(a, b)
		}
		wantSt(t, fmt.Sprint("pic ", i), err, m["status"])
		if err == nil && (out != ji(m["out"]) || (m["rem"] != nil && rem != ji(m["rem"]))) {
			t.Fatalf("pic %d: %d %d", i, out, rem)
		}
	}
	for i, r := range ja(v["run"]) {
		m := jm(r)
		tr := must[*zxv.Treaty](t)(zxv.ParseTreatySutra(js(m["sutra"])))
		res, err := tr.Run()
		wantSt(t, fmt.Sprint("run ", i), err, m["status"])
		if err == nil {
			np, nl, fees, err := res.Summary()
			ok(t, err)
			if int64(np) != ji(m["parties"]) || int64(nl) != ji(m["legs"]) || fees != ji(m["fees"]) {
				t.Fatalf("run %d summary", i)
			}
			ps := must[[]zxv.TreatyPartyResult](t)(res.Parties())
			for p, pp := range ja(m["party"]) {
				pm := jm(pp)
				if ps[p].Net != ji(pm["net"]) || ps[p].Balance != ji(pm["balance"]) || ps[p].Dividend != ji(pm["dividend"]) {
					t.Fatalf("run %d party %d", i, p)
				}
			}
			legs := must[[]zxv.TreatyLeg](t)(res.Legs())
			for l, ll := range ja(m["leg"]) {
				lm := jm(ll)
				if int64(legs[l].From) != ji(lm["from"]) || int64(legs[l].To) != ji(lm["to"]) || legs[l].Amount != ji(lm["amount"]) {
					t.Fatalf("run %d leg %d", i, l)
				}
			}
			if !bytes.Equal(must[[]byte](t)(res.Record()), jhex(t, m["result_record"])) {
				t.Fatalf("run %d result record", i)
			}
			ok(t, res.Close())
		}
		ok(t, tr.Close())
	}
}

// ===== cid and aipi ========================================================

func checkCIDText(t *testing.T, label string, c zxv.CID, base zxv.CIDBase, want any) {
	t.Helper()
	got, err := c.Text(base)
	w := js(want)
	if len(w) > 4 && w[:4] == "ZXV_" {
		wantSt(t, label, err, w)
		return
	}
	if err != nil || got != w {
		t.Fatalf("%s: %q %v, want %q", label, got, err, w)
	}
}

func checkCIDInfo(t *testing.T, label string, c zxv.CID, m map[string]any) {
	t.Helper()
	info := must[zxv.CIDInfo](t)(c.Info())
	if int64(info.Version) != ji(m["version"]) || int64(info.Codec) != ji(m["codec"]) || int64(info.Hash) != ji(m["hash"]) ||
		!bytes.Equal(info.Digest, jhex(t, m["digest"])) {
		t.Fatalf("%s info %+v", label, info)
	}
}

func TestCIDVectors(t *testing.T) {
	v := v15(t, "cid")
	for i, e := range ja(v["compute"]) {
		m := jm(e)
		c, err := zxv.ComputeCID(uint32(ji(m["codec"])), []byte(js(m["utf8"])))
		wantSt(t, fmt.Sprint("compute ", i), err, m["status"])
		if !bytes.Equal(c.Bytes(), jhex(t, m["cid"])) {
			t.Fatalf("compute %d cid", i)
		}
		checkCIDText(t, fmt.Sprint("compute default ", i), c, zxv.CIDBaseDefault, m["text_default"])
		checkCIDText(t, fmt.Sprint("compute 58btc ", i), c, zxv.CIDBase58BTC, m["text_58btc"])
		checkCIDInfo(t, fmt.Sprint("compute ", i), c, m)
		ok(t, c.Verify([]byte(js(m["utf8"]))))
	}
	for i, e := range ja(v["unixfs"]) {
		m := jm(e)
		data := []byte(js(m["utf8"]))
		if jb(m["ramp"]) {
			data = ramp(int(ji(m["len"])))
		}
		c, err := zxv.UnixFSCID(data)
		wantSt(t, fmt.Sprint("unixfs ", i), err, m["status"])
		if !bytes.Equal(c.Bytes(), jhex(t, m["cid"])) {
			t.Fatalf("unixfs %d", i)
		}
		checkCIDText(t, fmt.Sprint("unixfs text ", i), c, zxv.CIDBaseDefault, m["text_default"])
		if m["codec"] != nil && int64(must[zxv.CIDInfo](t)(c.Info()).Codec) != ji(m["codec"]) {
			t.Fatalf("unixfs %d codec", i)
		}
	}
	for i, e := range ja(v["text"]) {
		m := jm(e)
		c, err := zxv.ParseCID(js(m["text"]))
		wantSt(t, fmt.Sprint("text ", i), err, m["status"])
		if !bytes.Equal(c.Bytes(), jhex(t, m["cid"])) {
			t.Fatalf("text %d cid", i)
		}
		checkCIDText(t, fmt.Sprint("text default ", i), c, zxv.CIDBaseDefault, m["text_default"])
		checkCIDText(t, fmt.Sprint("text 58btc ", i), c, zxv.CIDBase58BTC, m["text_58btc"])
		checkCIDInfo(t, fmt.Sprint("text ", i), c, m)
		v1 := must[zxv.CID](t)(c.ToV1())
		if !bytes.Equal(v1.Bytes(), jhex(t, m["v1"])) {
			t.Fatalf("text %d v1", i)
		}
		checkCIDText(t, fmt.Sprint("text v1 ", i), v1, zxv.CIDBaseDefault, m["v1_text"])
		pk := must[[32]byte](t)(c.ProviderKey())
		if !bytes.Equal(pk[:], jhex(t, m["provider_key"])) {
			t.Fatalf("text %d provider key", i)
		}
		if c != zxv.MustParseCID(js(m["text"])) {
			t.Fatal("CID is not comparable by value")
		}
	}
	for i, e := range ja(v["text_reject"]) {
		m := jm(e)
		_, err := zxv.ParseCID(js(m["text"]))
		wantSt(t, fmt.Sprint("text_reject ", i), err, m["status"])
		if js(m["status"]) == "ZXV_E_CID" && !errors.Is(err, zxv.ErrCID) {
			t.Fatalf("text_reject %d sentinel", i)
		}
	}
	for i, e := range ja(v["verify"]) {
		m := jm(e)
		c, err := zxv.CIDFromBytes(jhex(t, m["cid"]))
		if err == nil {
			err = c.Verify([]byte(js(m["utf8"])))
		}
		wantSt(t, fmt.Sprint("verify ", i), err, m["status"])
	}
}

func TestAipiVectors(t *testing.T) {
	v := v15(t, "aipi")
	mod := must[zxv.CID](t)(zxv.CIDFromBytes(jhex(t, v["module_cid"])))
	con := must[zxv.CID](t)(zxv.CIDFromBytes(jhex(t, v["contract_cid"])))
	if must[zxv.CID](t)(zxv.ComputeCID(zxv.CIDCodecRaw, []byte(js(v["module_utf8"])))) != mod {
		t.Fatal("module cid")
	}
	var fn0 zxv.CID
	for i, e := range ja(v["fn"]) {
		m := jm(e)
		f := zxv.AipiFunction{
			Intent: zxv.AipiIntent(ji(m["intent"])), Verify: zxv.AipiVerify(ji(m["verify"])),
			Privacy: zxv.AipiPrivacy(ji(m["privacy"])), Determinism: zxv.AipiDeterminism(ji(m["determinism"])),
			QuorumN: uint32(ji(m["quorum_n"])), QuorumK: uint32(ji(m["quorum_k"])), FuelMax: ju(m["fuel_max"]),
			Module: mod, Entry: js(m["entry"]), Checker: js(m["checker"]), Certifier: js(m["certifier"]),
		}
		if jb(m["contract"]) {
			f.Contract = con
		}
		rec, err := f.Encode()
		wantSt(t, fmt.Sprint("fn ", i), err, m["status"])
		if err != nil {
			continue
		}
		if !bytes.Equal(rec, jhex(t, m["record"])) {
			t.Fatalf("fn %d record", i)
		}
		back := must[zxv.AipiFunction](t)(zxv.DecodeAipiFunction(rec))
		if back != f {
			t.Fatalf("fn %d round trip: %+v", i, back)
		}
		c := must[zxv.CID](t)(zxv.AipiFunctionCID(rec))
		if !bytes.Equal(c.Bytes(), jhex(t, m["fn_cid"])) {
			t.Fatalf("fn %d cid", i)
		}
		checkCIDText(t, fmt.Sprint("fn text ", i), c, zxv.CIDBase58BTC, m["fn_cid_text"])
		if i == 0 {
			fn0 = c
		}
	}
	var call1 zxv.CID
	for i, e := range ja(v["call"]) {
		m := jm(e)
		if js(m["fn"]) == "module_cid" {
			_, err := zxv.EncodeAipiCall(mod, nil)
			wantSt(t, "call with a module cid", err, m["status"])
			continue
		}
		in := []byte(js(m["input_utf8"]))
		rec := must[[]byte](t)(zxv.EncodeAipiCall(fn0, in))
		if !bytes.Equal(rec, jhex(t, m["record"])) {
			t.Fatalf("call %d record", i)
		}
		f, input, err := zxv.DecodeAipiCall(rec)
		if err != nil || f != fn0 || !bytes.Equal(input, in) {
			t.Fatalf("call %d decode", i)
		}
		c := must[zxv.CID](t)(zxv.AipiCallCID(fn0, in))
		if !bytes.Equal(c.Bytes(), jhex(t, m["call_cid"])) {
			t.Fatalf("call %d cid", i)
		}
		checkCIDText(t, fmt.Sprint("call text ", i), c, zxv.CIDBase58BTC, m["call_cid_text"])
		if i == 1 {
			call1 = c
		}
	}
	for i, e := range ja(v["claim"]) {
		m := jm(e)
		cl := zxv.AipiClaim{Call: call1, Executor: arr32(t, js(m["executor"])), Output: []byte(js(m["output_utf8"])), Cert: []byte(js(m["cert_utf8"]))}
		rec := must[[]byte](t)(cl.Encode())
		if !bytes.Equal(rec, jhex(t, m["record"])) {
			t.Fatalf("claim %d record", i)
		}
		back := must[zxv.AipiClaim](t)(zxv.DecodeAipiClaim(rec))
		if back.Call != cl.Call || back.Executor != cl.Executor || !bytes.Equal(back.Output, cl.Output) || !bytes.Equal(back.Cert, cl.Cert) {
			t.Fatalf("claim %d decode", i)
		}
	}
}

func TestAipiNode(t *testing.T) {
	_, cap := env15(t)
	key := [32]byte{9}
	n := must[*zxv.AipiNode](t)(zxv.NewAipiNode(cap, zxv.AipiStoreMin, 64, &key))
	defer n.Close()
	data := []byte("zxv module bytes")
	c := must[zxv.CID](t)(n.Put(data, false))
	if c != must[zxv.CID](t)(zxv.ComputeCID(zxv.CIDCodecRaw, data)) {
		t.Fatal("put cid")
	}
	if !bytes.Equal(must[[]byte](t)(n.Get(c)), data) {
		t.Fatal("get")
	}
	if _, err := n.Put(data, true); statusOf(err) != zxv.StatusState {
		t.Fatalf("public then private: %v", err)
	}
	missing := must[zxv.CID](t)(zxv.ComputeCID(zxv.CIDCodecRaw, []byte("absent")))
	if _, err := n.Get(missing); !errors.Is(err, zxv.ErrNotFound) {
		t.Fatalf("get missing: %v", err)
	}
	f := zxv.AipiFunction{Intent: 0, Verify: 1, Determinism: 1, FuelMax: 10, Module: c, Entry: "main", Checker: "check"}
	rec := must[[]byte](t)(f.Encode())
	fc := must[zxv.CID](t)(n.PublishFunction(rec))
	if !bytes.Equal(must[[]byte](t)(n.LoadFunction(fc)), rec) {
		t.Fatal("load fn")
	}
	if r := must[zxv.AipiRoute](t)(n.Plan(fc, nil, false)); r.String() == "" {
		t.Fatal("route name")
	}
	info := must[zxv.AipiNodeInfo](t)(n.Info())
	if info.Blocks < 2 || info.GetsVerified < 1 {
		t.Fatalf("info %+v", info)
	}
}

// ===== coin ================================================================

func holdingOf(x any) zxv.TokenHolding {
	var h zxv.TokenHolding
	for _, p := range ja(x) {
		pair := ja(p)
		h[ji(pair[0])] = ju(pair[1])
	}
	return h
}

func TestCoinVectors(t *testing.T) {
	v := v15(t, "coin")
	if int64(zxv.CoinRungs) != ji(v["rungs"]) || zxv.CoinTopFace != ju(v["top_face"]) {
		t.Fatal("constants")
	}
	for i, e := range ja(v["face"]) {
		m := jm(e)
		f, err := zxv.CoinFace(uint32(ji(m["rung"])))
		wantSt(t, fmt.Sprint("face ", i), err, m["status"])
		if err == nil && f != ju(m["face"]) {
			t.Fatalf("face %d", i)
		}
	}
	for i, e := range ja(v["rung_of"]) {
		m := jm(e)
		r, err := zxv.CoinRung(ju(m["face"]))
		wantSt(t, fmt.Sprint("rung_of ", i), err, m["status"])
		if err == nil && int64(r) != ji(m["rung"]) {
			t.Fatalf("rung_of %d", i)
		}
		if err != nil && !errors.Is(err, zxv.ErrDenom) && statusOf(err) == zxv.StatusDenom {
			t.Fatal("ErrDenom")
		}
	}
	for i, e := range ja(v["split"]) {
		m := jm(e)
		n, err := zxv.CoinSplitCount(ju(m["amount"]))
		wantSt(t, fmt.Sprint("split count ", i), err, m["status"])
		if err != nil {
			continue
		}
		if n != ju(m["count"]) {
			t.Fatalf("split %d count %d", i, n)
		}
		if m["rungs"] != nil {
			rungs := must[[]uint32](t)(zxv.CoinSplit(ju(m["amount"])))
			want := i64s(m["rungs"])
			if len(rungs) != len(want) {
				t.Fatalf("split %d rungs %v", i, rungs)
			}
			for k := range want {
				if int64(rungs[k]) != want[k] {
					t.Fatalf("split %d rungs %v", i, rungs)
				}
			}
		}
	}
	for i, e := range ja(v["token_face"]) {
		m := jm(e)
		f, super, err := zxv.TokenFace(uint32(ji(m["tier"])))
		wantSt(t, fmt.Sprint("token_face ", i), err, m["status"])
		if err == nil && (f != ju(m["face"]) || super != jb(m["super"])) {
			t.Fatalf("token_face %d", i)
		}
	}
	for i, e := range ja(v["token_tier"]) {
		m := jm(e)
		tr, err := zxv.TokenTier(ju(m["face"]))
		wantSt(t, fmt.Sprint("token_tier ", i), err, m["status"])
		if err == nil && int64(tr) != ji(m["tier"]) {
			t.Fatalf("token_tier %d", i)
		}
	}
	for i, e := range ja(v["token_value"]) {
		m := jm(e)
		val, pieces, err := holdingOf(m["counts"]).Value()
		wantSt(t, fmt.Sprint("token_value ", i), err, m["status"])
		if err == nil && (val != ju(m["value"]) || pieces != ju(m["pieces"])) {
			t.Fatalf("token_value %d", i)
		}
	}
	for i, e := range ja(v["token_canonical"]) {
		m := jm(e)
		h, err := zxv.CanonicalHolding(ju(m["value"]), zxv.Octane(ji(m["octane"])))
		wantSt(t, fmt.Sprint("token_canonical ", i), err, m["status"])
		if err != nil {
			continue
		}
		if h != holdingOf(m["counts"]) {
			t.Fatalf("token_canonical %d: %v", i, h)
		}
		if _, p, _ := h.Value(); p != ju(m["pieces"]) {
			t.Fatalf("token_canonical %d pieces", i)
		}
	}
	for i, e := range ja(v["token_is_canonical"]) {
		m := jm(e)
		c, err := holdingOf(m["counts"]).IsCanonical(zxv.Octane(ji(m["octane"])))
		wantSt(t, fmt.Sprint("token_is_canonical ", i), err, m["status"])
		if err == nil && c != jb(m["canonical"]) {
			t.Fatalf("token_is_canonical %d", i)
		}
	}
	for i, e := range ja(v["token_compress"]) {
		m := jm(e)
		h := holdingOf(m["counts"])
		err := h.Compress(zxv.Octane(ji(m["octane"])))
		wantSt(t, fmt.Sprint("token_compress ", i), err, m["status"])
		if h != holdingOf(m["out"]) {
			t.Fatalf("token_compress %d: %v", i, h)
		}
	}
	for i, e := range ja(v["token_fuse_split"]) {
		m := jm(e)
		h := holdingOf(m["counts"])
		var err error
		if js(m["op"]) == "fuse" {
			err = h.Fuse(zxv.Octane(ji(m["octane"])), uint32(ji(m["tier"])))
		} else {
			err = h.Split(uint32(ji(m["tier"])))
		}
		wantSt(t, fmt.Sprint("token_fuse_split ", i), err, m["status"])
		if h != holdingOf(m["out"]) {
			t.Fatalf("token_fuse_split %d: %v", i, h)
		}
	}
	for i, e := range ja(v["token_code"]) {
		m := jm(e)
		bits, err := zxv.TokenCodeBits(ju(m["value"]))
		wantSt(t, fmt.Sprint("token_code ", i), err, m["status"])
		if err != nil {
			continue
		}
		if int64(bits) != ji(m["bits"]) || len(js(m["code"])) != int(bits) {
			t.Fatalf("token_code %d bits", i)
		}
		b, nbits, err := zxv.EncodeTokenCodes([]uint64{ju(m["value"])})
		ok(t, err)
		if nbits != bits || !bytes.Equal(b, jhex(t, m["bytes"])) {
			t.Fatalf("token_code %d bytes %x", i, b)
		}
		got, next, err := zxv.GetTokenCode(b, 0)
		if err != nil || got != ju(m["value"]) || next != bits {
			t.Fatalf("token_code %d get", i)
		}
	}
	st := jm(v["token_code_stream"])
	var vals []uint64
	for _, x := range ja(st["values"]) {
		vals = append(vals, ju(x))
	}
	b, bits, err := zxv.EncodeTokenCodes(vals)
	ok(t, err)
	if int64(bits) != ji(st["bits"]) || !bytes.Equal(b, jhex(t, st["bytes"])) {
		t.Fatalf("token stream %x", b)
	}
	back := must[[]uint64](t)(zxv.DecodeTokenCodes(b, len(vals)))
	for k := range vals {
		if back[k] != vals[k] {
			t.Fatal("token stream decode")
		}
	}
	for i, e := range ja(v["token_code_get_reject"]) {
		m := jm(e)
		_, _, err := zxv.GetTokenCode(jhex(t, m["bytes"]), uint32(ji(m["bit"])))
		wantSt(t, fmt.Sprint("token_code_get_reject ", i), err, m["status"])
	}
}

func TestTokenWallet(t *testing.T) {
	_, cap := env15(t)
	a := must[*zxv.TokenWallet](t)(zxv.NewTokenWallet(cap, 1, zxv.OctaneMidgrade))
	defer a.Close()
	b := must[*zxv.TokenWallet](t)(zxv.NewTokenWallet(cap, 2, zxv.OctanePlain))
	defer b.Close()
	ok(t, a.Mint(100))
	ok(t, a.Transfer(b, 30))
	ok(t, a.Spend(20))
	if err := b.Spend(31); !errors.Is(err, zxv.ErrInsufficientFunds) {
		t.Fatalf("overspend: %v", err)
	}
	ia := must[zxv.TokenWalletInfo](t)(a.Info())
	ib := must[zxv.TokenWalletInfo](t)(b.Info())
	if ia.Value != 50 || ib.Value != 30 || ib.Pieces != 30 || ia.Octane != zxv.OctaneMidgrade {
		t.Fatalf("%+v %+v", ia, ib)
	}
	h := must[zxv.TokenHolding](t)(a.Counts())
	if c := must[bool](t)(h.IsCanonical(zxv.OctaneMidgrade)); !c {
		t.Fatal("wallet not canonical")
	}
}

// ===== fee =================================================================

func TestFeeVectors(t *testing.T) {
	v := v15(t, "fee")
	if zxv.FeeNum != ju(v["num"]) || zxv.FeeDen != ju(v["den"]) {
		t.Fatal("rate")
	}
	for i, e := range ja(v["assure"]) {
		m := jm(e)
		if f := must[uint64](t)(zxv.AssuranceFee(ju(m["gross"]))); f != ju(m["fee"]) {
			t.Fatalf("assure %d: %d", i, f)
		}
	}
	for i, e := range ja(v["carry"]) {
		m := jm(e)
		rem := ju(m["rem_in"])
		for k, s := range ja(m["steps"]) {
			sm := jm(s)
			fee, r, err := zxv.AssuranceFeeCarry(rem, ju(sm["gross"]))
			wantSt(t, fmt.Sprint("carry ", i, k), err, sm["status"])
			if fee != ju(sm["fee"]) || r != ju(sm["rem"]) {
				t.Fatalf("carry %d/%d: %d %d", i, k, fee, r)
			}
			rem = r
		}
	}
	for i, e := range ja(v["carry_reject"]) {
		m := jm(e)
		_, _, err := zxv.AssuranceFeeCarry(ju(m["rem_in"]), ju(m["gross"]))
		wantSt(t, fmt.Sprint("carry_reject ", i), err, m["status"])
	}
	for i, n := range ja(v["buckets"]) {
		if zxv.FeeBucket(i).String() != js(n) {
			t.Fatalf("bucket %d", i)
		}
	}
	for i, e := range ja(v["split"]) {
		m := jm(e)
		s := must[zxv.FeeSplit](t)(zxv.SplitFee(ju(m["fee"])))
		for k, p := range ja(m["parts"]) {
			if s[k] != ju(p) {
				t.Fatalf("split %d: %v", i, s)
			}
		}
		if s.Total() != ju(m["fee"]) {
			t.Fatalf("split %d total", i)
		}
	}
	p := must[zxv.FeePolicy](t)(zxv.FeePolicyDefault())
	if p.Percent != [4]uint32{50, 25, 15, 10} || p.AllowBelow || p.CommonsCap != (zxv.FeeRate{Num: 8, Den: 21}) || p.Multiplier != (zxv.FeeRate{Num: 1, Den: 1}) {
		t.Fatalf("policy %+v", p)
	}
}

// ===== vhub ================================================================

func TestVHubVectors(t *testing.T) {
	v := v15(t, "vhub")
	for i, e := range ja(v["peg_msg"]) {
		m := jm(e)
		msg := must[[40]byte](t)(zxv.VHubPegMessage(uint8(ji(m["slot"])), ju(m["num"]), ju(m["den"]), ju(m["epoch"])))
		if !bytes.Equal(msg[:], jhex(t, m["msg"])) {
			t.Fatalf("peg_msg %d", i)
		}
	}
	sc := jm(v["scenario"])
	_, cap := env15(t)
	dsa := zxv.NewMLDSA87(cap)
	var pks [][]byte
	var sks []*zxv.SecretKey
	for k, e := range ja(sc["keys"]) {
		m := jm(e)
		pk, sk, err := dsa.KeyGenFromSeed(arr32(t, js(m["seed"])))
		ok(t, err)
		if h := zxv.SHA3256(pk); !bytes.Equal(h[:], jhex(t, m["pk_sha3_256"])) {
			t.Fatalf("key %d", k)
		}
		pks = append(pks, pk)
		sks = append(sks, sk)
	}
	defer func() {
		for _, sk := range sks {
			sk.Close()
		}
	}()
	seed := arr32(t, js(sc["hub_seed"]))
	hub := must[*zxv.VHub](t)(zxv.NewVHub(cap, &seed))
	defer hub.Close()
	for i, e := range ja(sc["currencies"]) {
		m := jm(e)
		slot := uint8(ji(m["slot"]))
		var err error
		if m["issuer_key"] != nil {
			err = hub.AddCurrency(slot, js(m["code"]), uint8(ji(m["minor_units"])), pks[ji(m["issuer_key"])], ju(m["pool_funding"]), ju(m["vbill_backing"]))
		} else {
			err = hub.AddCurrency(slot, js(m["code"]), 2, pks[0], 0, 0)
		}
		wantSt(t, fmt.Sprint("currency ", i), err, m["status"])
		if err == nil {
			c := must[zxv.VHubCurrency](t)(hub.Currency(slot))
			if int64(c.IssuerAccount) != ji(m["issuer_account"]) || int64(c.PoolAccount) != ji(m["pool_account"]) || int64(c.FloatAccount) != ji(m["vfloat_account"]) {
				t.Fatalf("currency %d: %+v", i, c)
			}
		}
	}
	for i, e := range ja(sc["pegs"]) {
		m := jm(e)
		slot := uint8(ji(m["slot"]))
		msg := must[[40]byte](t)(zxv.VHubPegMessage(slot, ju(m["num"]), ju(m["den"]), ju(m["epoch"])))
		sig := must[[]byte](t)(dsa.SignDeterministic(sks[ji(m["signer"])], msg[:], nil))
		if h := zxv.SHA3256(sig); !bytes.Equal(h[:], jhex(t, m["sig_sha3_256"])) {
			t.Fatalf("peg %d signature", i)
		}
		err := hub.SetPeg(slot, ju(m["num"]), ju(m["den"]), ju(m["epoch"]), sig)
		wantSt(t, fmt.Sprint("peg ", i), err, m["status"])
	}
	for i, e := range ja(sc["accounts"]) {
		m := jm(e)
		a := must[uint32](t)(hub.OpenAccount(uint8(ji(m["slot"])), uint32(ji(m["owner"]))))
		if int64(a) != ji(m["account"]) {
			t.Fatalf("account %d = %d", i, a)
		}
		if m["issued"] != nil {
			ok(t, hub.Issue(a, ju(m["issued"])))
		}
	}
	for i, e := range ja(sc["quotes"]) {
		m := jm(e)
		q, err := hub.Quote(uint8(ji(m["from"])), uint8(ji(m["to"])), ju(m["want"]))
		wantSt(t, fmt.Sprint("quote ", i), err, m["status"])
		if err == nil && (q.Amount != ju(m["amount"]) || q.VBills != ju(m["vbills"]) || q.Received != ju(m["received"])) {
			t.Fatalf("quote %d: %+v", i, q)
		}
	}
	for i, e := range ja(sc["swaps"]) {
		m := jm(e)
		if al := jm(m["add_limit"]); al != nil {
			ok(t, hub.AddLimit(uint8(ji(al["slot"])), ju(al["limit"])))
		}
		s, err := hub.Swap(uint8(ji(m["from"])), uint8(ji(m["to"])), uint32(ji(m["sender"])), uint32(ji(m["receiver"])), ju(m["amount"]), ju(m["tick"]))
		wantSt(t, fmt.Sprint("swap ", i), err, m["status"])
		if err == nil && (s.VBills != ju(m["vbills"]) || s.Received != ju(m["received"])) {
			t.Fatalf("swap %d: %+v", i, s)
		}
		switch js(m["status"]) {
		case "ZXV_E_LIMIT":
			if !errors.Is(err, zxv.ErrLimit) {
				t.Fatal("ErrLimit")
			}
		case "ZXV_E_PEG":
			if !errors.Is(err, zxv.ErrPeg) {
				t.Fatal("ErrPeg")
			}
		}
	}
	postings, err := hub.Check()
	wantSt(t, "check", err, sc["check"])
	if postings != ju(sc["postings"]) {
		t.Fatalf("postings %d", postings)
	}
	bals := ja(sc["balances"])
	for _, e := range bals {
		m := jm(e)
		b := must[zxv.VHubBalance](t)(hub.Balance(uint32(ji(m["account"]))))
		if b.Debit != ju(m["debit"]) || b.Credit != ju(m["credit"]) || b.Equity != ji(m["equity"]) {
			t.Fatalf("balance %v: %+v", m["account"], b)
		}
	}
	if _, err := hub.Balance(uint32(len(bals))); err == nil {
		t.Fatal("balance past the last account")
	}
}

// ===== freight =============================================================

func TestFreightVectors(t *testing.T) {
	v := v15(t, "freight")
	for i, e := range ja(v["header_serialize"]) {
		m := jm(e)
		f := zxv.FreightHeaderFields{
			K: uint8(ji(m["k"])), Flags: uint8(ji(m["flags"])), PayloadLen: uint16(ji(m["payload_len"])),
			Seq: uint16(ji(m["seq"])), SeqCount: uint16(ji(m["seq_count"])), FreightID: ju(m["freight_id"]),
			ExpandLimit: ju(m["expand_limit"]), PayloadSHA256: arr32(t, js(m["payload_sha256"])),
		}
		h, err := f.Serialize()
		wantSt(t, fmt.Sprint("header_serialize ", i), err, m["status"])
		if err != nil {
			continue
		}
		if !bytes.Equal(h[:], jhex(t, m["header"])) {
			t.Fatalf("header %d", i)
		}
		back := must[zxv.FreightHeaderFields](t)(h.Fields())
		f.Version = zxv.FreightVersion
		if back != f {
			t.Fatalf("header %d parse %+v", i, back)
		}
		ok(t, h.Check())
	}
	for i, e := range ja(v["codec"]) {
		m := jm(e)
		payload := jhex(t, m["payload"])
		h := must[zxv.FreightHeader](t)(zxv.NewFreightHeader(ju(m["freight_id"]), uint8(ji(m["k"])), 0, 0, payload))
		if !bytes.Equal(h[:], jhex(t, m["header"])) {
			t.Fatalf("codec %d header", i)
		}
		packets := must[[]byte](t)(zxv.FreightEncode(h, payload))
		if !bytes.Equal(packets, jhex(t, m["packets"])) {
			t.Fatalf("codec %d packets", i)
		}
		for d, de := range ja(m["decode"]) {
			dm := jm(de)
			var sel []byte
			for _, r := range ja(dm["rows"]) {
				row := ji(r)
				sel = append(sel, packets[row*21:row*21+21]...)
			}
			if fl := jm(dm["flip"]); fl != nil {
				sel[ji(fl["index_in_rows"])*21+ji(fl["byte"])] ^= jhex(t, fl["xor"])[0]
			}
			out, err := zxv.FreightDecode(h, sel)
			wantSt(t, fmt.Sprint("decode ", i, d), err, dm["status"])
			if jb(dm["matches_payload"]) && !bytes.Equal(out, payload) {
				t.Fatalf("decode %d/%d payload", i, d)
			}
			if js(dm["status"]) == "ZXV_E_ERASURE" && !errors.Is(err, zxv.ErrErasure) {
				t.Fatal("ErrErasure")
			}
		}
	}
	for i, e := range ja(v["header_init_reject"]) {
		m := jm(e)
		var lim uint64
		if m["expand_limit"] != nil {
			lim = ju(m["expand_limit"])
		}
		_, err := zxv.NewFreightHeader(0, uint8(ji(m["k"])), uint8(ji(orZero(m["flags"]))), lim, make([]byte, ji(m["len"])))
		wantSt(t, fmt.Sprint("header_init_reject ", i), err, m["status"])
	}
	for i, e := range ja(v["set_count"]) {
		m := jm(e)
		n, err := zxv.FreightSetCount(ju(m["len"]), uint8(ji(m["k"])))
		wantSt(t, fmt.Sprint("set_count ", i), err, m["status"])
		if err == nil && int64(n) != ji(m["count"]) {
			t.Fatalf("set_count %d", i)
		}
	}
	sp := jm(v["split"])
	data := ramp(int(ji(sp["len"])))
	k := uint8(ji(sp["k"]))
	members := ja(sp["members"])
	join := must[*zxv.FreightJoin](t)(zxv.NewFreightJoin(uint64(len(data))))
	defer join.Close()
	for j := len(members) - 1; j >= 0; j-- { // any order
		m := jm(members[j])
		h, packets, err := zxv.FreightSplit(ju(sp["freight_id"]), k, 0, 0, data, uint32(ji(m["seq"])))
		wantSt(t, fmt.Sprint("split ", j), err, m["status"])
		if err != nil {
			continue
		}
		if !bytes.Equal(h[:], jhex(t, m["header"])) {
			t.Fatalf("split %d header", j)
		}
		if s := zxv.SHA3256(packets); !bytes.Equal(s[:], jhex(t, m["packets_sha3_256"])) {
			t.Fatalf("split %d packets", j)
		}
		ok(t, join.Add(h, packets[21:])) // drop row 0: erasure coding recovers it
		ok(t, join.Add(h, packets))      // a member again is a no-op
	}
	js0 := must[zxv.FreightJoinStatus](t)(join.Status())
	if !js0.Complete || js0.Total != uint64(len(data)) {
		t.Fatalf("join %+v", js0)
	}
	if !bytes.Equal(must[[]byte](t)(join.Result()), data) {
		t.Fatal("join result")
	}
	for i, e := range ja(v["ops_encode"]) {
		m := jm(e)
		in := []byte(js(m["utf8"]))
		ops, err := zxv.FreightOpsEncode(in, jb(m["with_sha"]))
		wantSt(t, fmt.Sprint("ops_encode ", i), err, m["status"])
		if !bytes.Equal(ops, jhex(t, m["ops"])) {
			t.Fatalf("ops_encode %d: %x", i, ops)
		}
		if b := must[uint64](t)(zxv.FreightOpsBound(uint64(len(in)), jb(m["with_sha"]))); b != ju(m["bound"]) {
			t.Fatalf("ops bound %d: %d", i, b)
		}
		out, err := zxv.FreightOpsExpand(ops, zxv.FreightOpsMax, nil)
		wantSt(t, fmt.Sprint("ops_encode expand ", i), err, m["expand_status"])
		if !bytes.Equal(out, in) {
			t.Fatalf("ops_encode %d expand", i)
		}
	}
	for i, e := range ja(v["ops_writer"]) {
		m := jm(e)
		total := ju(m["declared_total"])
		w := must[*zxv.FreightOpsWriter](t)(zxv.NewFreightOpsWriter(total, nil, 4096))
		var cid []byte
		var resolvesTo []byte
		for o, oe := range ja(m["ops_list"]) {
			om := jm(oe)
			var err error
			switch js(om["op"]) {
			case "literal":
				err = w.PutLiteral([]byte(js(om["utf8"])))
			case "copy":
				err = w.PutCopy(ju(om["distance"]), ju(om["len"]))
			case "seed":
				err = w.PutSeed(zxv.FreightGen(ji(om["gen"])), jhex(t, om["params"]), ju(om["len"]))
			case "cidref":
				cid = jhex(t, om["cid"])
				resolvesTo = []byte(js(om["resolves_to_utf8"]))
				err = w.PutCIDRef(cid, ju(om["len"]))
			}
			if om["status"] != nil {
				wantSt(t, fmt.Sprint("ops_writer ", i, o), err, om["status"])
			} else {
				ok(t, err)
			}
		}
		ops, err := w.Finish()
		if m["finish_status"] != nil {
			wantSt(t, fmt.Sprint("ops_writer finish ", i), err, m["finish_status"])
		} else {
			ok(t, err)
		}
		ok(t, w.Close())
		if !bytes.Equal(ops, jhex(t, m["ops"])) {
			t.Fatalf("ops_writer %d: %x", i, ops)
		}
		if m["expand_status"] != nil {
			out, err := zxv.FreightOpsExpand(ops, total, nil)
			wantSt(t, fmt.Sprint("ops_writer expand ", i), err, m["expand_status"])
			if !bytes.Equal(out, jhex(t, m["expanded"])) {
				t.Fatalf("ops_writer %d expanded", i)
			}
			tot, hasSHA, err := zxv.FreightOpsPeek(ops)
			if err != nil || tot != ju(m["peek_total"]) || hasSHA != jb(m["peek_has_sha"]) {
				t.Fatalf("ops_writer %d peek", i)
			}
			_, err = zxv.FreightOpsExpand(ops, total-1, nil)
			wantSt(t, "expand limit too small", err, m["expand_limit_too_small"])
			_, err = zxv.FreightOpsExpand(ops[:len(ops)-1], total, nil)
			wantSt(t, "expand truncated", err, m["expand_truncated"])
		}
		if m["expand_without_resolver"] != nil {
			_, err := zxv.FreightOpsExpand(ops, total, nil)
			wantSt(t, "expand without resolver", err, m["expand_without_resolver"])
			resolver := func(c, out []byte) error {
				if !bytes.Equal(c, cid) || len(out) != len(resolvesTo) {
					return errors.New("unknown")
				}
				copy(out, resolvesTo)
				return nil
			}
			out := must[[]byte](t)(zxv.FreightOpsExpand(ops, total, resolver))
			if string(out) != js(m["expanded_with_resolver_utf8"]) {
				t.Fatalf("expanded %q", out)
			}
			// A resolver that fails or panics is "not resolved", never a crash.
			_, err = zxv.FreightOpsExpand(ops, total, func(c, out []byte) error { return errors.New("no") })
			if !errors.Is(err, zxv.ErrNotFound) {
				t.Fatalf("failing resolver: %v", err)
			}
			_, err = zxv.FreightOpsExpand(ops, total, func(c, out []byte) error { panic("boom") })
			if err == nil {
				t.Fatal("panicking resolver accepted")
			}
		}
	}
	for i, e := range ja(v["gen_run"]) {
		m := jm(e)
		out, err := zxv.FreightGenRun(zxv.FreightGen(ji(m["gen"])), jhex(t, m["params"]), int(ji(m["len"])))
		wantSt(t, fmt.Sprint("gen_run ", i), err, m["status"])
		if err == nil && !bytes.Equal(out, jhex(t, m["out"])) {
			t.Fatalf("gen_run %d", i)
		}
	}
}

func orZero(x any) any {
	if x == nil {
		return float64(0)
	}
	return x
}

// ===== web4 ================================================================

func signedBody(b []byte) []byte { return b[:len(b)-zxv.W4SigBytes] }

func TestWeb4Vectors(t *testing.T) {
	v := v15(t, "web4")
	if int(ji(v["sig_bytes"])) != zxv.W4SigBytes {
		t.Fatal("sig bytes")
	}
	_, cap := env15(t)
	agents := map[string]*zxv.W4Agent{}
	infos := map[string]zxv.W4AgentInfo{}
	var order []*zxv.W4Agent
	defer func() {
		for i := len(order) - 1; i >= 0; i-- {
			ok(t, order[i].Close())
		}
	}()
	for _, e := range ja(v["agents"]) {
		m := jm(e)
		var seed [32]byte
		for i := range seed {
			seed[i] = jhex(t, m["seed_byte"])[0]
		}
		var node *zxv.W4Agent
		if m["node"] != nil {
			node = agents[js(m["node"])]
		}
		name := js(m["name"])
		a, err := zxv.NewW4Agent(cap, &seed, name, node, ju(m["now_ms"]))
		wantSt(t, "agent "+name, err, m["status"])
		order = append(order, a)
		agents[name] = a
		info := must[zxv.W4AgentInfo](t)(a.Info())
		infos[name] = info
		if !bytes.Equal(info.AgentID[:], jhex(t, m["agent_id"])) || !bytes.Equal(info.PeerID[:], jhex(t, m["peer_id"])) {
			t.Fatalf("agent %s ids", name)
		}
		if h := zxv.SHA3256(info.PublicKey); !bytes.Equal(h[:], jhex(t, m["pk_sha3_256"])) {
			t.Fatalf("agent %s pk", name)
		}
		card := must[[]byte](t)(a.Card())
		if int64(len(card)) != ji(m["card_len"]) {
			t.Fatalf("agent %s card len %d", name, len(card))
		}
		if h := zxv.SHA3256(signedBody(card)); !bytes.Equal(h[:], jhex(t, m["card_signed_body_sha3_256"])) {
			t.Fatalf("agent %s card body", name)
		}
		ci, err := zxv.VerifyW4Card(card)
		wantSt(t, "card verify "+name, err, m["card_verify"])
		if ci.Name != name || ci.AgentID != info.AgentID || ci.PeerID != info.PeerID || ci.BoundMs != ju(m["now_ms"]) {
			t.Fatalf("card info %+v", ci)
		}
		card[len(card)-1] ^= 1
		_, err = zxv.VerifyW4Card(card)
		wantSt(t, "card flipped "+name, err, m["card_verify_flipped_last_byte"])
	}
	// The directory holds verified cards.
	dir := must[*zxv.W4Directory](t)(zxv.NewW4Directory(4))
	defer dir.Close()
	for _, a := range order {
		ok(t, dir.Add(must[[]byte](t)(a.Card())))
	}
	if n := must[uint32](t)(dir.Count()); n != 4 {
		t.Fatalf("dir count %d", n)
	}
	if c := must[[]byte](t)(dir.Find(infos["bob"].AgentID)); !bytes.Equal(c, must[[]byte](t)(agents["bob"].Card())) {
		t.Fatal("dir find")
	}
	if _, err := dir.Find([32]byte{}); !errors.Is(err, zxv.ErrNotFound) {
		t.Fatalf("dir find missing: %v", err)
	}

	ad := jm(v["action_digest"])
	from, to := arr32(t, js(ad["from"])), arr32(t, js(ad["to"]))
	var dig1 [32]byte
	for i, e := range ja(ad["cases"]) {
		m := jm(e)
		payload := []byte(js(m["payload_utf8"]))
		if m["payload_ramp_len"] != nil {
			payload = ramp(int(ji(m["payload_ramp_len"])))
		}
		d, err := zxv.W4ActionDigest(js(m["tool"]), from, to, ju(m["req_id"]), payload)
		wantSt(t, fmt.Sprint("action digest ", i), err, m["status"])
		if err == nil && !bytes.Equal(d[:], jhex(t, m["digest"])) {
			t.Fatalf("action digest %d", i)
		}
		if i == 1 {
			dig1 = d
		}
	}

	mf := jm(v["manifest"])
	man := must[*zxv.W4Manifest](t)(zxv.NewW4Manifest(uint32(ji(mf["version"])), ju(mf["issued_ms"]), ju(mf["expires_ms"])))
	defer man.Close()
	var tools []zxv.W4Tool
	for i, e := range ja(mf["tools"]) {
		m := jm(e)
		tl := zxv.W4Tool{Name: js(m["name"]), PriceVFV: ju(m["price_vfv"]), RatePerMin: uint32(ji(m["rate_per_min"])),
			Burst: uint32(ji(m["burst"])), Flags: uint32(ji(m["flags"])), MaxInput: uint32(ji(m["max_input"]))}
		wantSt(t, fmt.Sprint("tool ", i), man.AddTool(tl), m["status"])
		tools = append(tools, tl)
	}
	wantSt(t, "add duplicate", man.AddTool(zxv.W4Tool{Name: "pay", PriceVFV: 1, RatePerMin: 1, Burst: 1}), mf["add_duplicate"])
	_, err := man.Encode()
	wantSt(t, "encode unsigned", err, mf["encode_unsigned"])
	ok(t, man.Sign(agents[js(mf["signer"])]))
	enc := must[[]byte](t)(man.Encode())
	if int64(len(enc)) != ji(mf["encoding_len"]) || !bytes.Equal(signedBody(enc), jhex(t, mf["encoding_signed_body"])) {
		t.Fatal("manifest encoding")
	}
	if must[string](t)(man.JSON()) != js(mf["json"]) {
		t.Fatal("manifest json")
	}
	dec, err := zxv.DecodeW4Manifest(enc)
	wantSt(t, "manifest decode", err, mf["decode"])
	defer dec.Close()
	got := must[[]zxv.W4Tool](t)(dec.Tools())
	if len(got) != len(tools) {
		t.Fatal("decoded tools")
	}
	for i := range tools {
		if got[i] != tools[i] {
			t.Fatalf("tool %d: %+v", i, got[i])
		}
	}
	if idx := must[uint32](t)(dec.Find("stream.feed")); idx != 2 {
		t.Fatalf("find %d", idx)
	}
	mi := must[zxv.W4ManifestInfo](t)(dec.Info())
	if !mi.Signed || mi.AgentID != infos["alice"].AgentID || mi.ToolCount != 3 {
		t.Fatalf("manifest info %+v", mi)
	}
	for i, e := range ja(mf["verify"]) {
		m := jm(e)
		err := dec.Verify(infos[js(m["key"])].PublicKey, ju(m["now_ms"]))
		wantSt(t, fmt.Sprint("manifest verify ", i), err, m["status"])
	}

	cs := jm(v["consent"])
	nonce := [16]byte{}
	copy(nonce[:], jhex(t, cs["nonce"]))
	human := agents[js(cs["human"])]
	req := zxv.W4ConsentRequest{AgentID: infos[js(cs["agent"])].AgentID, Action: dig1, Scope: uint32(ji(cs["scope"])),
		MaxVFV: ju(cs["max_vfv"]), IssuedMs: ju(cs["issued_ms"]), ExpiresMs: ju(cs["expires_ms"]), Nonce: &nonce}
	tok, err := zxv.IssueW4Consent(cap, human, req)
	wantSt(t, "consent issue", err, cs["status"])
	if int64(len(tok)) != ji(cs["token_len"]) || !bytes.Equal(signedBody(tok[:]), jhex(t, cs["token_signed_body"])) {
		t.Fatal("token body")
	}
	ti := must[zxv.W4ConsentInfo](t)(tok.Info())
	if ti.HumanID != infos["human"].AgentID || ti.Nonce != nonce || ti.MaxVFV != req.MaxVFV {
		t.Fatalf("token info %+v", ti)
	}
	nonce2 := nonce
	nonce2[0] ^= 0xFF
	req.Nonce = &nonce2
	tok2 := must[zxv.W4ConsentToken](t)(zxv.IssueW4Consent(cap, human, req))
	log := must[*zxv.W4ConsentLog](t)(zxv.NewW4ConsentLog(16))
	defer log.Close()
	checks := ja(cs["check"])
	for i, e := range checks {
		m := jm(e)
		tk := &tok
		if i == len(checks)-1 {
			tk = &tok2
			ok(t, log.Revoke(&tok2, ju(m["now_ms"])))
		}
		err := log.Check(tk, infos["human"].PublicKey, zxv.W4ConsentNeed{AgentID: infos[js(m["agent"])].AgentID, Action: dig1,
			Need: uint32(ji(m["need"])), Amount: ju(m["amount"]), NowMs: ju(m["now_ms"])})
		wantSt(t, "consent check "+js(m["what"]), err, m["status"])
		if err != nil && !errors.Is(err, zxv.ErrConsent) {
			t.Fatal("ErrConsent")
		}
	}
	if err := log.Check(nil, infos["human"].PublicKey, zxv.W4ConsentNeed{Action: dig1, Need: 1, NowMs: 1}); !errors.Is(err, zxv.ErrConsent) {
		t.Fatalf("no token: %v", err)
	}
	if n := must[uint32](t)(log.Prune(ju(cs["expires_ms"]))); n != 0 {
		t.Fatalf("prune left %d", n)
	}

	var request []byte
	for i, e := range ja(v["envelopes"]) {
		m := jm(e)
		sender, recv := agents[js(m["sender"])], infos[js(m["to"])]
		var env []byte
		var err error
		if js(m["op"]) == "seal" {
			env, err = sender.Seal(zxv.W4Message{Kind: zxv.W4MsgKind(ji(m["kind"])), To: recv.AgentID, ReqID: ju(m["req_id"]),
				Chunk: uint32(ji(m["chunk"])), Tool: js(m["tool"]), Flags: uint32(ji(m["flags"])), Payload: []byte(js(m["payload_utf8"]))}, ju(m["now_ms"]))
			request = env
		} else {
			env, err = sender.Reply(request, zxv.W4MsgKind(ji(m["kind"])), uint32(ji(m["chunk"])), uint32(ji(m["flags"])), []byte(js(m["payload_utf8"])), ju(m["now_ms"]))
		}
		wantSt(t, fmt.Sprint("envelope ", i), err, m["status"])
		if int64(len(env)) != ji(m["env_len"]) || !bytes.Equal(signedBody(env), jhex(t, m["env_signed_body"])) {
			t.Fatalf("envelope %d body", i)
		}
		wantSt(t, fmt.Sprint("envelope verify ", i), zxv.VerifyW4Envelope(env, infos[js(m["sender"])].PublicKey), m["verify_sender"])
		if m["verify_other"] != nil {
			wantSt(t, "envelope verify other", zxv.VerifyW4Envelope(env, recv.PublicKey), m["verify_other"])
		}
		d := must[zxv.W4Envelope](t)(zxv.DecodeW4Envelope(env))
		if d.Kind != zxv.W4MsgKind(ji(m["kind"])) || d.To != recv.AgentID || d.From != infos[js(m["sender"])].AgentID {
			t.Fatalf("envelope %d decode %+v", i, d)
		}
		if p := must[[]byte](t)(zxv.W4EnvelopePayload(env)); string(p) != js(m["payload_utf8"]) {
			t.Fatalf("envelope %d payload", i)
		}
		if _, err := zxv.W4EnvelopeConsent(env); !errors.Is(err, zxv.ErrNotFound) {
			t.Fatalf("envelope %d consent: %v", i, err)
		}
	}
	// A token attached by sealing comes back out.
	env := must[[]byte](t)(agents["alice"].Seal(zxv.W4Message{Kind: zxv.W4MsgRequest, To: infos["bob"].AgentID, ReqID: 9,
		Tool: "pay", Payload: []byte("amount=1"), Consent: &tok}, 1700000001000))
	if got := must[zxv.W4ConsentToken](t)(zxv.W4EnvelopeConsent(env)); got != tok {
		t.Fatal("attached token")
	}
	if d := must[zxv.W4Envelope](t)(zxv.DecodeW4Envelope(env)); d.Flags&zxv.W4FlagConsent == 0 {
		t.Fatal("consent flag")
	}
}

// ===== paradox =============================================================

func hexTag(t *testing.T, x any) zxv.Hexagram {
	t.Helper()
	return must[zxv.Hexagram](t)(zxv.HexagramFromTag(uint8(ji(x))))
}

func class(t *testing.T, m map[string]any) zxv.ParadoxClass {
	return zxv.ParadoxClass{Kind: zxv.ParadoxKind(ji(m["kind"])), Tag: hexTag(t, m["tag"])}
}

func heads(t *testing.T, b *zxv.ParadoxBook, want any) {
	t.Helper()
	for w, name := range []string{"ops", "frontier", "quarantine"} {
		m := jm(jm(want)[name])
		n := must[uint32](t)(b.LogCount(zxv.ParadoxLog(w)))
		h := must[[32]byte](t)(b.LogHead(zxv.ParadoxLog(w)))
		if int64(n) != ji(m["count"]) || !bytes.Equal(h[:], jhex(t, m["head"])) {
			t.Fatalf("log %s: %d %x", name, n, h)
		}
	}
}

func TestParadoxVectors(t *testing.T) {
	v := v15(t, "paradox")
	for i, e := range ja(v["signature"]) {
		m := jm(e)
		s := must[[32]byte](t)(zxv.ParadoxSignature([]byte(js(m["utf8"]))))
		if !bytes.Equal(s[:], jhex(t, m["sig"])) {
			t.Fatalf("signature %d", i)
		}
	}
	for i, n := range ja(v["kind_names"]) {
		if zxv.ParadoxKind(i).String() != js(n) {
			t.Fatalf("kind name %d", i)
		}
	}
	for i, n := range ja(v["outcome_names"]) {
		if zxv.ParadoxOutcome(i).String() != js(n) {
			t.Fatalf("outcome name %d", i)
		}
	}
	for i, n := range ja(v["node_state_names"]) {
		if zxv.ParadoxNodeState(i).String() != js(n) {
			t.Fatalf("node state name %d", i)
		}
	}
	for tg, k := range ja(v["kind_of_tag"]) {
		got := must[zxv.ParadoxKind](t)(zxv.ParadoxKindOfTag(hexTag(t, float64(tg))))
		if int64(got) != ji(k) {
			t.Fatalf("kind_of_tag %d", tg)
		}
	}
	for k, row := range ja(v["action"]) {
		for tg, d := range ja(row) {
			got, err := zxv.ParadoxClass{Kind: zxv.ParadoxKind(k), Tag: hexTag(t, float64(tg))}.Do()
			if ji(d) == -1 {
				if statusOf(err) != zxv.StatusArg {
					t.Fatalf("action[%d][%d] accepted", k, tg)
				}
			} else if err != nil || int64(got) != ji(d) {
				t.Fatalf("action[%d][%d] = %d %v", k, tg, got, err)
			}
		}
	}
	for o, a := range ja(v["outcome_action"]) {
		if got := must[zxv.HexAction](t)(zxv.ParadoxOutcome(o).Action()); int64(got) != ji(a) {
			t.Fatalf("outcome_action %d", o)
		}
	}

	_, cap := env15(t)
	b := must[*zxv.ParadoxBook](t)(zxv.NewParadoxBook(cap, 64, 16))
	defer b.Close()
	heads(t, b, v["heads_fresh"])
	sigL := must[[32]byte](t)(zxv.ParadoxSignature([]byte("loop:A->B->A")))
	for i, e := range ja(v["classify"]) {
		m := jm(e)
		if ra := jm(m["registry_add"]); ra != nil {
			idx, err := b.RegistryAdd(sigL, zxv.ParadoxKind(ji(ra["kind"])), uint8(ji(ra["reason"])), uint8(ji(ra["flags"])), jb(ra["owner_ok"]))
			wantSt(t, "registry_add", err, ra["status"])
			if int64(idx) != ji(ra["index"]) {
				t.Fatalf("registry index %d", idx)
			}
			continue
		}
		var ev *zxv.ParadoxEvidence
		if e := i64s(m["evidence"]); e != nil {
			ev = &zxv.ParadoxEvidence{Pos0: uint32(e[0]), Neg0: uint32(e[1]), Pos1: uint32(e[2]), Neg1: uint32(e[3]), Stale: jb(m["stale"])}
		}
		var sig *[32]byte
		if m["sig"] != nil {
			sig = &sigL
		}
		c, err := b.Classify(hexTag(t, m["tag"]), ev, sig)
		wantSt(t, fmt.Sprint("classify ", i), err, m["status"])
		if int64(c.Kind) != ji(m["kind"]) || int64(c.Tag.Tag()) != ji(m["out_tag"]) || int64(c.RegIndex) != ji(m["reg_index"]) {
			t.Fatalf("classify %d: %+v", i, c)
		}
		if sig == nil {
			c2 := must[zxv.ParadoxClassification](t)(zxv.ParadoxClassify(hexTag(t, m["tag"]), ev))
			if c2.ParadoxClass != c.ParadoxClass {
				t.Fatalf("classify %d without book", i)
			}
		}
	}
	sigM := must[[32]byte](t)(zxv.ParadoxSignature([]byte("money:drain")))
	for i, e := range ja(v["registry"]) {
		m := jm(e)
		idx, err := b.RegistryAdd(sigM, zxv.ParadoxKind(ji(m["kind"])), 2, uint8(ji(m["flags"])), jb(m["owner_ok"]))
		wantSt(t, fmt.Sprint("registry ", i), err, m["status"])
		if m["index"] != nil && int64(idx) != ji(m["index"]) {
			t.Fatalf("registry %d index", i)
		}
		if js(m["status"]) == "ZXV_E_PARADOX_OWNER" && !errors.Is(err, zxv.ErrParadoxOwner) {
			t.Fatal("ErrParadoxOwner")
		}
	}
	if e := must[zxv.ParadoxRegistryEntry](t)(b.RegistryAt(1)); e.Sig != sigM || e.Src != zxv.ParadoxSrcSeed || e.Flags != zxv.ParadoxFlagMoney {
		t.Fatalf("registry entry %+v", e)
	}
	if i := must[uint32](t)(b.RegistryFind(sigL)); i != 0 {
		t.Fatal("registry find")
	}
	heads(t, b, v["heads_after_classify"])

	cs := jm(v["constructive"])
	lo, hi, init := i64s(cs["lo"]), i64s(cs["hi"]), i64s(cs["initial"])
	plain := must[*zxv.ParadoxPolicy](t)(zxv.NewParadoxPolicy(cap, lo, hi, init, 0))
	defer plain.Close()
	money := must[*zxv.ParadoxPolicy](t)(zxv.NewParadoxPolicy(cap, lo, hi, init, zxv.ParadoxFlagMoney))
	defer money.Close()
	for i, e := range ja(cs["steps"]) {
		m := jm(e)
		p := plain
		if js(m["policy"]) == "money" {
			p = money
		}
		synth := i64s(m["synth"])
		o, err := b.Constructive(p, class(t, m), synth, uint32(ji(m["tension"])))
		wantSt(t, fmt.Sprint("constructive ", i), err, m["status"])
		if m["outcome"] != nil && int64(o) != ji(m["outcome"]) {
			t.Fatalf("constructive %d outcome %v", i, o)
		}
		vals := must[zxv.ParadoxPolicyValues](t)(p.Values())
		if fmt.Sprint(vals.Values) != fmt.Sprint(i64s(m["values"])) || fmt.Sprint(vals.Prev) != fmt.Sprint(i64s(m["prev"])) ||
			vals.Armed != jb(m["armed"]) || int64(vals.TensionAt) != ji(m["tension_at"]) {
			t.Fatalf("constructive %d values %+v", i, vals)
		}
		if m["proposal"] != nil {
			if !bytes.Equal(must[[]byte](t)(p.Proposal(synth)), jhex(t, m["proposal"])) {
				t.Fatalf("constructive %d proposal", i)
			}
		}
	}
	l2, h2, fl, err := money.Bounds()
	if err != nil || fmt.Sprint(l2) != fmt.Sprint(lo) || fmt.Sprint(h2) != fmt.Sprint(hi) || fl != zxv.ParadoxFlagMoney {
		t.Fatal("bounds")
	}

	for i, e := range ja(v["destructive"]) {
		m := jm(e)
		var trace []uint64
		for _, x := range ja(m["trace"]) {
			trace = append(trace, ju(x))
		}
		state := []byte(js(m["state_utf8"]))
		o, proof, err := b.Destructive(class(t, m), zxv.ParadoxContainment{
			Steps: uint32(ji(m["steps"])), StepLimit: uint32(ji(m["step_limit"])), Spent: uint32(ji(m["spent"])), Budget: uint32(ji(m["budget"])),
			Trace: trace, InvariantOK: jb(m["invariant_ok"]), State: state, Checkpoint: []byte(js(m["checkpoint_utf8"])),
			Pattern: []byte(js(m["pattern_utf8"])), Submitter: uint32(ji(m["submitter"])),
		})
		wantSt(t, fmt.Sprint("destructive ", i), err, m["status"])
		if int64(o) != ji(m["outcome"]) || !bytes.Equal(state, jhex(t, m["state_after"])) {
			t.Fatalf("destructive %d: %v %q", i, o, state)
		}
		if m["repair_proof"] != nil {
			if !bytes.Equal(proof[:], jhex(t, m["repair_proof"])) {
				t.Fatalf("destructive %d proof", i)
			}
			wantSt(t, "repair verify", proof.Verify(), m["repair_verify"])
			if proof.Signature() != sigL || proof.Submitter() != 7 || proof.Trips()&zxv.ParadoxTripSteps == 0 {
				t.Fatal("proof fields")
			}
			proof[0] ^= 1
			wantSt(t, "repair verify flipped", proof.Verify(), m["repair_verify_flipped_byte0"])
		}
	}

	nt := jm(v["neutral"])
	hold := must[*zxv.ParadoxHold](t)(zxv.NewParadoxHold(cap, uint32(ji(nt["capacity"])), uint32(ji(nt["decisive"]))))
	defer hold.Close()
	br := ja(nt["branches"])
	b0, b1 := arr32(t, js(br[0])), arr32(t, js(br[1]))
	ok(t, hold.Add(b0))
	ok(t, hold.Add(b1))
	ok(t, hold.Evidence(b0, true))
	ok(t, hold.Evidence(b1, true))
	for i, e := range ja(nt["steps"]) {
		m := jm(e)
		if m["before"] != nil {
			continue
		}
		for k := int64(0); k < ji(m["add_for_branch0"]); k++ {
			ok(t, hold.Evidence(b0, true))
		}
		o, settled, err := b.Neutral(hold, class(t, m), int32(ji(m["owner_pick"])))
		wantSt(t, fmt.Sprint("neutral ", i), err, m["status"])
		if int64(o) != ji(m["outcome"]) || int64(settled) != ji(m["settled"]) {
			t.Fatalf("neutral %d: %v %d", i, o, settled)
		}
	}
	if brs := must[[]zxv.ParadoxBranch](t)(hold.Branches()); len(brs) != 2 || brs[0].ID != b0 || brs[0].Pos != 3 {
		t.Fatalf("branches %+v", brs)
	}

	sp := jm(v["special"])
	sg, subj := arr32(t, js(sp["sig"])), arr32(t, js(sp["subject"]))
	o, rec, err := b.Special(class(t, sp), sg, subj)
	wantSt(t, "special", err, sp["status"])
	if int64(o) != ji(sp["outcome"]) || int64(rec) != ji(sp["rec"]) {
		t.Fatalf("special %v %d", o, rec)
	}
	drafts := map[string]zxv.ParadoxDraft{}
	for _, name := range []string{"draft", "draft_money"} {
		m := jm(sp[name])
		d, err := b.Draft(rec, zxv.ParadoxKind(ji(m["kind"])), uint8(ji(m["reason"])), uint8(ji(m["flags"])))
		wantSt(t, name, err, m["status"])
		if !bytes.Equal(d[:], jhex(t, m["bytes"])) || d.Rec() != rec || d.Signature() != sg {
			t.Fatalf("%s bytes", name)
		}
		drafts[name] = d
	}
	for i, e := range ja(sp["review"]) {
		m := jm(e)
		o, err := b.Review(drafts[js(m["draft"])], jb(m["owner_ok"]), jb(m["consensus_ok"]))
		wantSt(t, fmt.Sprint("review ", i), err, m["status"])
		if int64(o) != ji(m["outcome"]) {
			t.Fatalf("review %d: %v", i, o)
		}
	}
	if n := must[uint32](t)(b.RegistryCount()); int64(n) != ji(sp["registry_count_after"]) {
		t.Fatalf("registry count %d", n)
	}

	for gi, ge := range ja(v["graphs"]) {
		gm := jm(ge)
		g := must[*zxv.ParadoxGraph](t)(zxv.NewParadoxGraph(cap, uint32(ji(gm["step_limit"]))))
		id := func(x any) (a [32]byte) {
			for i := range a {
				a[i] = js(x)[0]
			}
			return
		}
		for _, ne := range ja(gm["nodes"]) {
			m := jm(ne)
			wantSt(t, "add node", g.AddNode(id(m["id"]), class(t, m), uint32(ji(m["mag"])), int32(ji(m["dir"]))), m["status"])
		}
		for _, de := range ja(gm["deps"]) {
			m := jm(de)
			wantSt(t, "add dep", g.AddDep(id(m["id"]), id(m["dep"])), m["status"])
		}
		for pi, pe := range ja(gm["passes"]) {
			m := jm(pe)
			o, err := b.GraphPass(g)
			wantSt(t, fmt.Sprint("graph ", gi, " pass ", pi), err, m["status"])
			if err == nil && int64(o) != ji(m["outcome"]) {
				t.Fatalf("graph %d pass %d outcome %v", gi, pi, o)
			}
			if err != nil && (!errors.Is(err, zxv.ErrParadoxStep) || o != zxv.ParadoxEscalateToOwner) {
				t.Fatalf("graph %d pass %d: %v %v", gi, pi, o, err)
			}
			info := must[zxv.ParadoxGraphInfo](t)(g.Info())
			if int64(info.Pass) != ji(m["pass"]) || int64(info.TotalMag) != ji(m["total_mag"]) || int64(info.TotalDir) != ji(m["total_dir"]) ||
				info.Escalated != jb(m["escalated"]) || !bytes.Equal(info.Chain[:], jhex(t, m["chain"])) {
				t.Fatalf("graph %d pass %d info %+v", gi, pi, info)
			}
			for name, s := range jm(m["states"]) {
				if st := must[zxv.ParadoxNodeState](t)(g.State(id(name))); int64(st) != ji(s) {
					t.Fatalf("graph %d node %s state %v", gi, name, st)
				}
			}
		}
		nodes := must[[]zxv.ParadoxNode](t)(g.Nodes())
		for i := 1; i < len(nodes); i++ {
			if bytes.Compare(nodes[i-1].ID[:], nodes[i].ID[:]) >= 0 {
				t.Fatal("nodes not in id order")
			}
		}
		ok(t, g.Close())
	}
	heads(t, b, v["heads_final"])
	for w, name := range []string{"ops", "frontier", "quarantine"} {
		recs := must[[][]byte](t)(b.LogRecords(zxv.ParadoxLog(w)))
		want := ja(jm(v["logs"])[name])
		if len(recs) != len(want) {
			t.Fatalf("log %s length", name)
		}
		for i := range want {
			if !bytes.Equal(recs[i], jhex(t, want[i])) {
				t.Fatalf("log %s record %d", name, i)
			}
		}
		wantSt(t, "log verify "+name, b.LogVerify(zxv.ParadoxLog(w)), ja(v["log_verify"])[w])
	}
}

// ===== behaviour: capabilities, lifetimes, buffers, fail closed ============

func TestABI15CapabilityRefusals(t *testing.T) {
	ctx, root := env15(t)
	_ = ctx
	for _, c := range []struct {
		name string
		drop zxv.Caps
		try  func(cap *zxv.Capability) error
	}{
		{"AIPI", zxv.CapAIPI, func(c *zxv.Capability) error { _, err := zxv.NewAipiNode(c, zxv.AipiStoreMin, 4, nil); return err }},
		{"MINT", zxv.CapMint, func(c *zxv.Capability) error { _, err := zxv.NewTokenWallet(c, 1, zxv.OctanePlain); return err }},
		{"PARADOX book", zxv.CapParadox, func(c *zxv.Capability) error { _, err := zxv.NewParadoxBook(c, 4, 4); return err }},
		{"PARADOX hold", zxv.CapParadox, func(c *zxv.Capability) error { _, err := zxv.NewParadoxHold(c, 4, 1); return err }},
		{"PARADOX graph", zxv.CapParadox, func(c *zxv.Capability) error { _, err := zxv.NewParadoxGraph(c, 1); return err }},
		{"PARADOX policy", zxv.CapParadox, func(c *zxv.Capability) error {
			_, err := zxv.NewParadoxPolicy(c, []int64{0}, []int64{1}, []int64{0}, 0)
			return err
		}},
		{"CRYPTO_SIGN", zxv.CapCryptoSign, func(c *zxv.Capability) error { _, err := zxv.NewW4Agent(c, nil, "a", nil, 1); return err }},
		{"LEDGER_POST", zxv.CapLedgerPost, func(c *zxv.Capability) error { _, err := zxv.NewVHub(c, nil); return err }},
	} {
		narrow := must[*zxv.Capability](t)(root.Attenuate(zxv.CapAll15 &^ c.drop))
		err := c.try(narrow)
		if !errors.Is(err, zxv.ErrCapabilityDenied) || !errors.Is(err, zxv.ErrRefused) {
			t.Fatalf("%s without its bit: %v", c.name, err)
		}
		ok(t, narrow.Close())
	}
	// Operators on a book use the book's capability: a book made with a cap
	// that later lost PARADOX (revoked) refuses every write.
	sub := must[*zxv.Capability](t)(root.Attenuate(zxv.CapAll15))
	b := must[*zxv.ParadoxBook](t)(zxv.NewParadoxBook(sub, 4, 4))
	ok(t, sub.Revoke())
	if _, err := b.RegistryAdd([32]byte{1}, zxv.ParadoxDestructive, 0, 0, false); !errors.Is(err, zxv.ErrCapabilityDenied) {
		t.Fatalf("revoked cap: %v", err)
	}
	ok(t, b.Close())
	ok(t, sub.Close())
	// Consent needs ATTEST, and a cap of the human key's context.
	seed := [32]byte{4}
	human := must[*zxv.W4Agent](t)(zxv.NewW4Agent(root, &seed, "human", nil, 1))
	defer human.Close()
	noAttest := must[*zxv.Capability](t)(root.Attenuate(zxv.CapAll15 &^ zxv.CapAttest))
	defer noAttest.Close()
	req := zxv.W4ConsentRequest{Scope: zxv.W4ConsentMoney, IssuedMs: 1, ExpiresMs: 2}
	if _, err := zxv.IssueW4Consent(noAttest, human, req); !errors.Is(err, zxv.ErrCapabilityDenied) {
		t.Fatalf("consent without ATTEST: %v", err)
	}
	_, other := env15(t)
	if _, err := zxv.IssueW4Consent(other, human, req); statusOf(err) != zxv.StatusArg {
		t.Fatalf("consent with another context's cap: %v", err)
	}
	ob := must[*zxv.ParadoxBook](t)(zxv.NewParadoxBook(other, 4, 4))
	defer ob.Close()
	g := must[*zxv.ParadoxGraph](t)(zxv.NewParadoxGraph(root, 1))
	defer g.Close()
	if _, err := ob.GraphPass(g); statusOf(err) != zxv.StatusArg {
		t.Fatalf("graph of another context: %v", err)
	}
}

func TestABI15HandleLifetimes(t *testing.T) {
	_, cap := env15(t)
	tr := must[*zxv.Treaty](t)(zxv.NewTreaty())
	ok(t, tr.Close())
	ok(t, tr.Close()) // twice is a no-op
	if _, err := tr.Name(); !errors.Is(err, zxv.ErrClosed) {
		t.Fatalf("treaty after close: %v", err)
	}
	j := must[*zxv.FreightJoin](t)(zxv.NewFreightJoin(10))
	ok(t, j.Close())
	if _, err := j.Status(); !errors.Is(err, zxv.ErrClosed) {
		t.Fatalf("join after close: %v", err)
	}
	w := must[*zxv.FreightOpsWriter](t)(zxv.NewFreightOpsWriter(1, nil, 64))
	ok(t, w.Close())
	if err := w.PutLiteral([]byte{1}); !errors.Is(err, zxv.ErrClosed) {
		t.Fatalf("writer after close: %v", err)
	}
	a := must[*zxv.W4Agent](t)(zxv.NewW4Agent(cap, nil, "agent", nil, 1))
	// The capability refuses to close while a handle made with it is open.
	if err := cap.Close(); statusOf(err) != zxv.StatusState {
		t.Fatalf("cap closed under an open agent: %v", err)
	}
	ok(t, a.Close())
	if _, err := a.Card(); !errors.Is(err, zxv.ErrClosed) {
		t.Fatalf("agent after close (key wiped): %v", err)
	}
	m := must[*zxv.W4Manifest](t)(zxv.NewW4Manifest(1, 1, 2))
	ok(t, m.Close())
	if err := m.Sign(a); !errors.Is(err, zxv.ErrClosed) {
		t.Fatalf("manifest after close: %v", err)
	}
	b := must[*zxv.ParadoxBook](t)(zxv.NewParadoxBook(cap, 4, 4))
	h := must[*zxv.ParadoxHold](t)(zxv.NewParadoxHold(cap, 2, 1))
	ok(t, h.Close())
	if _, _, err := b.Neutral(h, zxv.ParadoxClass{Kind: zxv.ParadoxNeutral}, -1); !errors.Is(err, zxv.ErrClosed) {
		t.Fatalf("hold after close: %v", err)
	}
	ok(t, b.Close())
	if _, err := b.LogCount(zxv.ParadoxLogOps); !errors.Is(err, zxv.ErrClosed) {
		t.Fatalf("book after close: %v", err)
	}
	hub := must[*zxv.VHub](t)(zxv.NewVHub(cap, nil))
	ok(t, hub.Close())
	if _, err := hub.NextEpoch(); !errors.Is(err, zxv.ErrClosed) {
		t.Fatalf("hub after close: %v", err)
	}
}

func TestABI15BufferGrowth(t *testing.T) {
	// Random bytes encode LARGER than themselves: the encoder's first
	// buffer is too small and the SDK must grow it to the exact length.
	in := ramp(70000)
	for i := range in {
		in[i] = byte(uint32(i)*2654435761>>13) ^ byte(i>>8)
	}
	ops := must[[]byte](t)(zxv.FreightOpsEncode(in, true))
	if len(ops) <= len(in)/2+64 {
		t.Skipf("encoding (%d bytes) did not exercise growth", len(ops))
	}
	out := must[[]byte](t)(zxv.FreightOpsExpand(ops, uint64(len(in)), nil))
	if !bytes.Equal(out, in) {
		t.Fatal("round trip")
	}
	// A join result larger than the first buffer.
	data := ramp(5000)
	j := must[*zxv.FreightJoin](t)(zxv.NewFreightJoin(uint64(len(data))))
	defer j.Close()
	n := must[uint32](t)(zxv.FreightSetCount(uint64(len(data)), 168))
	for s := uint32(0); s < n; s++ {
		h, p, err := zxv.FreightSplit(1, 168, 0, 0, data, s)
		ok(t, err)
		ok(t, j.Add(h, p))
	}
	if !bytes.Equal(must[[]byte](t)(j.Result()), data) {
		t.Fatal("join result")
	}
	// A paradox proposal of 64 values is longer than any small first guess.
	_, cap := env15(t)
	lo, hi, v := make([]int64, 64), make([]int64, 64), make([]int64, 64)
	for i := range hi {
		hi[i] = 100
	}
	p := must[*zxv.ParadoxPolicy](t)(zxv.NewParadoxPolicy(cap, lo, hi, v, 0))
	defer p.Close()
	if pr := must[[]byte](t)(p.Proposal(hi)); len(pr) != zxv.ParadoxProposalBytes(64) {
		t.Fatalf("proposal %d bytes", len(pr))
	}
}

func TestABI15FailClosed(t *testing.T) {
	ctx, cap := env15(t)
	b := must[*zxv.ParadoxBook](t)(zxv.NewParadoxBook(cap, 4, 4))
	defer b.Close()
	g := must[*zxv.ParadoxGraph](t)(zxv.NewParadoxGraph(cap, 1))
	defer g.Close()
	seed := [32]byte{7}
	a := must[*zxv.W4Agent](t)(zxv.NewW4Agent(cap, &seed, "a", nil, 1))
	defer a.Close()
	ok(t, ctx.Halt("test halt"))
	for name, err := range map[string]error{
		"book read":   func() error { _, err := b.LogCount(zxv.ParadoxLogOps); return err }(),
		"book write":  func() error { _, err := b.RegistryAdd([32]byte{1}, zxv.ParadoxNeutral, 0, 0, false); return err }(),
		"graph local": g.AddNode([32]byte{1}, zxv.ParadoxClass{Kind: zxv.ParadoxKindNone}, 0, 0),
		"agent card":  func() error { _, err := a.Card(); return err }(),
		"agent seal": func() error {
			_, err := a.Seal(zxv.W4Message{Kind: zxv.W4MsgRequest, Tool: "pay"}, 1)
			return err
		}(),
		"new book": func() error { _, err := zxv.NewParadoxBook(cap, 4, 4); return err }(),
	} {
		if !errors.Is(err, zxv.ErrFatal) || !errors.Is(err, zxv.ErrPoisoned) {
			t.Fatalf("%s after halt: %v", name, err)
		}
	}
	// Pure functions need no context and keep working.
	if f := must[uint64](t)(zxv.AssuranceFee(1125)); f != 1 {
		t.Fatal("pure fee")
	}
}

// TestABI15HexHelpers keeps the vectors' hex helper honest.
func TestABI15HexHelpers(t *testing.T) {
	if hex.EncodeToString(ramp(3)) != "000102" {
		t.Fatal("ramp")
	}
}
