// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

package zxv

// The V-Bill hub (ABI 1.5): a demonstration of cross-currency clearing
// through V-Bills, with test currencies, signed exact pegs, quotes, swaps
// and limit rules.
//
// Each test currency has ONE exact peg against the V-Bill, set only by its
// issuer with an ML-DSA-87 signature. A swap from currency A to currency B
// goes A -> V-Bill -> B as one balanced, all-or-nothing posting on the
// hub's own in-process ledger:
//
//	sender -a, A pool +a;  A V-Bill float -v, B V-Bill float +v;
//	B pool -b, receiver +b;   v = a*denA/numA,  b = v*numB/denB.
//
// EXACT. A peg is the rational num/den: num minor units of the currency
// per den V-Bills. Both divisions must be exact or the swap is refused
// (ZXV_E_RANGE); nothing is rounded and there is no float rate anywhere.
// Quote gives the largest exact amount not above a requested one.
//
// BACKING. A swap that would take the sender, A's V-Bill float or B's pool
// below zero is refused (ErrInsufficientFunds). An exact peg removes
// rounding, not exchange-rate risk.
//
// RULES. Every rule of the sending AND the receiving currency must allow a
// swap. A currency with no rule refuses every swap through it (fail closed,
// ZXV_E_STATE), so add a rule to each currency before swapping. Only the
// built-in limit rule is exposed (AddLimit); a refusal is ErrLimit.
//
// CAPABILITIES. Creating a hub, adding currencies and rules, setting pegs,
// opening and funding accounts and swapping need CapLedgerPost; quotes,
// balances, currency info and the check need CapLedgerRead. A VHub uses the
// capability it was created with for every call, so give it both bits.
//
// Honest limits: a demo hub with test currencies only (ISO 4217 codes are
// refused) and no real money: its ledger lives in process memory, separate
// from the context's ledger, and is lost on Close. No network: it is
// connected to no settlement rail, central bank or issuer. Peg signatures
// use ML-DSA-87 (not FIPS 140-3 validated). V-Bills are likely securities
// in many jurisdictions; real use needs legal review and a participating
// issuer. A rule reports what it decided; it is not itself a legal check.

/*
#include <stdlib.h>
#include "zxv_api.h"
*/
import "C"

import (
	"runtime"
	"unsafe"
)

// Hub limits.
const (
	VHubMaxCurrencies = int(C.ZXV_VHUB_MAX_CCY)         // currency slots 0..7
	VHubMaxRules      = int(C.ZXV_VHUB_MAX_RULES)       // limit rules per currency
	VHubLimitSenders  = int(C.ZXV_VHUB_LIMIT_SENDERS)   // senders one limit tracks per epoch
	VHubPegMsgBytes   = int(C.ZXV_VHUB_PEG_MSG_BYTES)   // VHubPegMessage size
	VHubCodeMax       = int(C.ZXV_VHUB_CODE_MAX)        // test currency code length
	VHubAmountMax     = uint64(1) << 59                 // any one amount; balances stay below 2^62
	VHubVBillIssuer   = uint32(C.ZXV_VHUB_VBILL_ISSUER) // the V-Bill issuer: always account 0
)

// VHubCurrency is a currency's accounts and current peg.
type VHubCurrency struct {
	IssuerAccount uint32 // the currency issuer's account
	PoolAccount   uint32 // the hub's pool of the currency
	FloatAccount  uint32 // the currency's V-Bill float
	PegNum        uint64 // the peg: PegNum minor units per PegDen V-Bills
	PegDen        uint64 // 0 while the peg is unset
	PegEpoch      uint64 // 0 while the peg is unset
}

// VHubBalance is one account's rails: Debit (what it holds), Credit (what
// it owes; issuers only) and Equity = Debit - Credit.
type VHubBalance struct {
	Debit  uint64 // what the account holds
	Credit uint64 // what it owes (issuers only)
	Equity int64  // Debit - Credit
}

// VHubQuote is what a swap of Amount would carry.
type VHubQuote struct {
	Amount   uint64 // the largest exact amount not above the request (0 if none)
	VBills   uint64 // V-Bills it would move
	Received uint64 // units of the target currency it would pay out
}

// VHubSwap is what a swap moved.
type VHubSwap struct {
	VBills   uint64 // V-Bills moved between the two floats
	Received uint64 // units the receiver got
}

// VHubPegMessage returns the exact 40 bytes an issuer signs to set a peg:
// "zxv/vhub-peg/v1" || slot || num || den || epoch (little-endian u64s).
// Sign them with MLDSA87.SignDeterministic or Sign and an empty context.
func VHubPegMessage(slot uint8, num, den, epoch uint64) ([40]byte, error) {
	var m [40]byte
	err := call(func() C.int32_t {
		return C.zxv_vhub_peg_msg(C.uint8_t(slot), C.uint64_t(num), C.uint64_t(den), C.uint64_t(epoch),
			(*C.uint8_t)(unsafe.Pointer(&m[0])))
	})
	return m, err
}

// VHub is one demo hub and its ledger (zxv_vhub). Safe for concurrent use
// (the C hub serializes its calls). Close it before its capability.
type VHub struct{ ctxHandle }

// NewVHub creates a hub on cap's context (cap needs CapLedgerPost; give it
// CapLedgerRead too for quotes and balances). Its ledger starts with the
// V-Bill asset "VBL" and the V-Bill issuer's account (VHubVBillIssuer).
// seed (32 bytes) derives every posting's idempotency key and UETR; nil
// draws a fresh random seed. The rule epoch starts at 0.
func NewVHub(cap *Capability, seed *[32]byte) (*VHub, error) {
	var sp *C.uint8_t
	if seed != nil {
		sp = ptr32(seed)
	}
	var r *C.zxv_vhub
	if err := cap.gated(func(p *C.zxv_cap) C.int32_t { return C.zxv_vhub_create(p, sp, &r) }); err != nil {
		return nil, err
	}
	h := &VHub{newCtxHandle("vhub", unsafe.Pointer(r), func(p unsafe.Pointer) { C.zxv_vhub_destroy((*C.zxv_vhub)(p)) }, cap, false)}
	runtime.SetFinalizer(h, func(h *VHub) { _ = h.h.close() })
	return h, nil
}

// Close destroys the hub and its in-memory ledger.
func (h *VHub) Close() error { return h.h.close() }

func (h *VHub) do(f func(cp *C.zxv_cap, p *C.zxv_vhub) C.int32_t) error {
	return h.gated(func(cp *C.zxv_cap, p unsafe.Pointer) C.int32_t { return f(cp, (*C.zxv_vhub)(p)) })
}

// AddCurrency registers test currency slot (0..7): its code (3..12 of A-Z
// and 0-9; not an ISO 4217 code, not "VBL" or "VFV"), its minor units
// (0..4) and its issuer's ML-DSA-87 public key (MLDSA87PKBytes; it alone
// may set the peg). It opens the issuer's account, the hub's pool and the
// currency's V-Bill float, funds the pool with poolFunding and the float
// with vbillBacking (either may be 0). The peg starts unset. Errors:
// ErrUsage (bad code, slot, minor units, amount), ZXV_E_CURRENCY (an ISO
// 4217 code), ZXV_E_DUPLICATE (slot or code taken). A failure registers
// nothing.
func (h *VHub) AddCurrency(slot uint8, code string, minorUnits uint8, issuerPK []byte, poolFunding, vbillBacking uint64) error {
	if err := need(issuerPK, MLDSA87PKBytes, "issuer public key"); err != nil {
		return err
	}
	cs, err := cstring(code)
	if err != nil {
		return err
	}
	defer freeC(cs)
	return h.do(func(cp *C.zxv_cap, p *C.zxv_vhub) C.int32_t {
		return C.zxv_vhub_add_ccy(cp, p, C.uint8_t(slot), cs, C.uint8_t(minorUnits), bytePtr(issuerPK),
			C.uint64_t(poolFunding), C.uint64_t(vbillBacking))
	})
}

// Currency returns a currency's accounts and current peg (ErrNotFound for
// an unregistered slot).
func (h *VHub) Currency(slot uint8) (VHubCurrency, error) {
	var ia, pa, va C.uint32_t
	var n, d, e C.uint64_t
	err := h.do(func(cp *C.zxv_cap, p *C.zxv_vhub) C.int32_t {
		return C.zxv_vhub_ccy_info(cp, p, C.uint8_t(slot), &ia, &pa, &va, &n, &d, &e)
	})
	return VHubCurrency{IssuerAccount: uint32(ia), PoolAccount: uint32(pa), FloatAccount: uint32(va),
		PegNum: uint64(n), PegDen: uint64(d), PegEpoch: uint64(e)}, err
}

// SetPeg sets a currency's peg to num/den at epoch, which must be above
// the current one, given the issuer's ML-DSA-87 signature (empty context)
// over VHubPegMessage(slot, num, den, epoch). The new peg applies to the
// next quote and swap. Errors: ErrPeg (a stale or replayed epoch),
// ErrVerificationFailed (not the issuer's signature over these numbers),
// ZXV_E_RANGE (num or den 0). A refused peg changes nothing.
func (h *VHub) SetPeg(slot uint8, num, den, epoch uint64, sig []byte) error {
	if err := need(sig, MLDSA87SigBytes, "peg signature"); err != nil {
		return err
	}
	return h.do(func(cp *C.zxv_cap, p *C.zxv_vhub) C.int32_t {
		return C.zxv_vhub_set_peg(cp, p, C.uint8_t(slot), C.uint64_t(num), C.uint64_t(den), C.uint64_t(epoch), bytePtr(sig))
	})
}

// OpenAccount opens a holder account in currency slot for owner (an id of
// the caller's choosing) and returns its number. ZXV_E_CAPACITY when the
// ledger holds 512 accounts.
func (h *VHub) OpenAccount(slot uint8, owner uint32) (uint32, error) {
	var a C.uint32_t
	err := h.do(func(cp *C.zxv_cap, p *C.zxv_vhub) C.int32_t {
		return C.zxv_vhub_open_account(cp, p, C.uint8_t(slot), C.uint32_t(owner), &a)
	})
	return uint32(a), err
}

// Issue is the test faucet: it issues amount to a holder account or a
// currency's pool (by that currency's issuer), or V-Bills to a V-Bill float
// (by the V-Bill issuer); the issuer's credit rises by the same amount.
// ZXV_E_RANGE for 0, an amount above VHubAmountMax, or a balance that would
// reach 2^62.
func (h *VHub) Issue(account uint32, amount uint64) error {
	return h.do(func(cp *C.zxv_cap, p *C.zxv_vhub) C.int32_t {
		return C.zxv_vhub_issue(cp, p, C.uint32_t(account), C.uint64_t(amount))
	})
}

// Balance returns one account's rails (ErrNotFound past the last account).
func (h *VHub) Balance(account uint32) (VHubBalance, error) {
	var d, c C.uint64_t
	var e C.int64_t
	err := h.do(func(cp *C.zxv_cap, p *C.zxv_vhub) C.int32_t {
		return C.zxv_vhub_balance(cp, p, C.uint32_t(account), &d, &c, &e)
	})
	return VHubBalance{Debit: uint64(d), Credit: uint64(c), Equity: int64(e)}, err
}

// AddLimit adds the built-in limit rule to currency slot: at most limit
// minor units of the currency out per sender per rule epoch (inflows are
// not counted). One limit tracks VHubLimitSenders distinct senders per
// epoch and refuses the next (fail closed). Several limits on one currency
// all apply. ZXV_E_CAPACITY after VHubMaxRules rules.
func (h *VHub) AddLimit(slot uint8, limit uint64) error {
	return h.do(func(cp *C.zxv_cap, p *C.zxv_vhub) C.int32_t {
		return C.zxv_vhub_add_limit(cp, p, C.uint8_t(slot), C.uint64_t(limit))
	})
}

// NextEpoch starts the next rule epoch (every limit starts every sender at
// zero) and returns it.
func (h *VHub) NextEpoch() (uint64, error) {
	var e C.uint64_t
	err := h.do(func(cp *C.zxv_cap, p *C.zxv_vhub) C.int32_t { return C.zxv_vhub_next_epoch(cp, p, &e) })
	return uint64(e), err
}

// Quote returns the largest exact amount of currency from not above want,
// and the V-Bills and to-units it would carry. It does not run rules or
// check balances, so a quoted swap may still be refused. ErrPeg when either
// peg is unset; ErrUsage for from == to.
func (h *VHub) Quote(from, to uint8, want uint64) (VHubQuote, error) {
	var a, v, r C.uint64_t
	err := h.do(func(cp *C.zxv_cap, p *C.zxv_vhub) C.int32_t {
		return C.zxv_vhub_quote(cp, p, C.uint8_t(from), C.uint8_t(to), C.uint64_t(want), &a, &v, &r)
	})
	return VHubQuote{Amount: uint64(a), VBills: uint64(v), Received: uint64(r)}, err
}

// Swap moves amount minor units of currency from out of holder account
// sender, and pays holder account receiver in currency to, as one six-line
// posting at tick (a caller-supplied time, never the wall clock). Errors:
// ZXV_E_STATE (a currency has no rule), ErrLimit (a limit rule refused),
// ErrInsufficientFunds (the sender, A's float or B's pool would go below
// zero), ErrPeg (a peg unset), ZXV_E_RANGE (not exact under the pegs, or
// out of range), ZXV_E_CURRENCY (an account of another currency),
// ErrFatal (the ledger broke an invariant; the context is poisoned). A
// refused swap changes nothing.
func (h *VHub) Swap(from, to uint8, sender, receiver uint32, amount, tick uint64) (VHubSwap, error) {
	var v, r C.uint64_t
	err := h.do(func(cp *C.zxv_cap, p *C.zxv_vhub) C.int32_t {
		return C.zxv_vhub_swap(cp, p, C.uint8_t(from), C.uint8_t(to), C.uint32_t(sender), C.uint32_t(receiver),
			C.uint64_t(amount), C.uint64_t(tick), &v, &r)
	})
	if err != nil {
		return VHubSwap{}, err
	}
	return VHubSwap{VBills: uint64(v), Received: uint64(r)}, nil
}

// Check verifies the whole hub ledger (per account A = L + E with no
// negative rail and no credit outside an issuer, per asset sum DEBIT == sum
// CREDIT, and the posting hash chain) and returns the postings made so far.
// A failure is ErrFatal and poisons the context.
func (h *VHub) Check() (uint64, error) {
	var n C.uint64_t
	err := h.do(func(cp *C.zxv_cap, p *C.zxv_vhub) C.int32_t { return C.zxv_vhub_check(cp, p, &n) })
	return uint64(n), err
}
