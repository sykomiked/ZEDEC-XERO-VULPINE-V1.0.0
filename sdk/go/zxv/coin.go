// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

package zxv

// Fibonacci coins and AI tokens (ABI 1.5).
//
// Two Fibonacci ladders share the faces 1, 2, 3, 5, 8, 13, 21, ... and the
// same unique fewest-pieces (Zeckendorf) form; both are integer-only.
//
// VFV COINS. A coin's face is a rung of the ladder: rung r (1..CoinRungs)
// has face F(r+1), so rung 1 is 1, rung 4 is 5 and rung 45 is
// 1,836,311,903 (CoinTopFace). Rung 0 is the ceremonial V0 note, worth
// nothing. Any other face is not a coin (ErrDenom). To pay an arbitrary
// amount, split it with CoinSplit. Minting VFV coins is not in this ABI:
// the kernel's mint needs a coin store bound to a ledger and an integer
// mint path that do not exist yet.
//
// AI TOKENS. Tier k (0..TokenTiers-1) has face F(k+2): 1, 2, 3, 5, 8, ...
// Tier 0 is the plain token; tiers TokenSuperTier (face 144) and up are
// super tokens. A TokenHolding counts tokens per tier. OCTANE is the
// densest tier a holding may use (named grades like a fuel pump's; any
// octane 0..63 is allowed). The canonical form of a value at octane o puts
// value / face(o) tokens on tier o and the rest in Zeckendorf form below.
//
// NO PREMIUM. A dense token is worth exactly its face in plain tokens.
// Density earns no interest, costs no fee and buys no extra allotment or
// priority; changing octane re-expresses the same value and never changes
// it. Density only means fewer pieces (1,000,000 tokens are 1,000,000
// pieces at plain, 17 at racing), which helps wherever tokens are handled
// one piece at a time. It does not make a model faster or an amount
// shorter on the wire.
//
// The pure functions need no capability. A TokenWallet changes only with
// CapMint.

/*
#include "zxv_api.h"
*/
import "C"

import (
	"runtime"
	"unsafe"
)

// Coin ladder constants.
const (
	CoinRungs   = uint32(C.ZXV_COIN_RUNGS) // rungs 1..45 carry value
	CoinTopFace = uint64(1836311903)       // face of rung 45, F(46)
	// CoinSplitMaxCoins is the largest split CoinSplit returns as a slice.
	CoinSplitMaxCoins = 1 << 24
)

// CoinFace returns the face of ladder rung (0..CoinRungs); rung 0 is the
// V0 note, face 0. ZXV_E_RANGE above CoinRungs.
func CoinFace(rung uint32) (uint64, error) {
	var f C.uint64_t
	err := call(func() C.int32_t { return C.zxv_coin_face(C.uint32_t(rung), &f) })
	return uint64(f), err
}

// CoinRung returns the rung (1..CoinRungs) whose face is exactly face; 0
// and any face not on the ladder (including Fibonacci numbers above the
// top face) are ErrDenom.
func CoinRung(face uint64) (uint32, error) {
	var r C.uint32_t
	err := call(func() C.int32_t { return C.zxv_coin_rung(C.uint64_t(face), &r) })
	return uint32(r), err
}

// CoinSplit returns the coins for a whole amount: its Zeckendorf split
// into rungs, largest first (amounts at or above the top face take
// amount / CoinTopFace coins of rung 45 first). The split is unique and has
// the fewest coins possible. 0 is an empty split. The result holds one
// uint32 per coin, so a split of more than CoinSplitMaxCoins coins (amounts
// from about 3e16) is refused with ZXV_E_RANGE instead of allocating it:
// use CoinSplitCount for those. The library itself refuses splits of more
// than 4,294,967,294 coins.
func CoinSplit(amount uint64) ([]uint32, error) {
	n, err := CoinSplitCount(amount)
	if err != nil {
		return nil, err
	}
	if n == 0 {
		return []uint32{}, nil
	}
	if n > CoinSplitMaxCoins {
		return nil, newError(StatusRange, "the split has more than CoinSplitMaxCoins coins; use CoinSplitCount")
	}
	out := make([]uint32, n)
	var got C.size_t
	err = call(func() C.int32_t {
		return C.zxv_coin_split(C.uint64_t(amount), (*C.uint32_t)(unsafe.Pointer(&out[0])), C.size_t(n), &got)
	})
	if err != nil {
		return nil, err
	}
	return out[:got], nil
}

// CoinSplitCount returns how many coins CoinSplit(amount) has, without
// building the split.
func CoinSplitCount(amount uint64) (uint64, error) {
	var n C.size_t
	runtime.LockOSThread()
	defer runtime.UnlockOSThread()
	rc := C.zxv_coin_split(C.uint64_t(amount), nil, 0, &n)
	if rc != C.ZXV_OK && rc != C.ZXV_E_BUFFER {
		return 0, newError(int32(rc), lastError())
	}
	return uint64(n), nil
}

// ===== AI tokens ============================================================

// Token constants.
const (
	TokenTiers        = int(C.ZXV_TOKEN_TIERS)         // tiers 0..63
	TokenSuperTier    = uint32(C.ZXV_TOKEN_SUPER_TIER) // tier 10 (face 144) and up are super tokens
	TokenCodeMaxBits  = uint32(C.ZXV_TOKEN_CODE_MAX_BITS)
	TokenCodeMaxBytes = int(C.ZXV_TOKEN_CODE_MAX_BYTES)
)

// Octane is the densest tier a holding may use (0..OctaneMax).
type Octane uint32

// Named octanes, like a fuel pump's grades. Any value 0..63 is allowed.
const (
	OctanePlain    Octane = C.ZXV_OCTANE_PLAIN    // face 1 only
	OctaneRegular  Octane = C.ZXV_OCTANE_REGULAR  // up to face 8
	OctaneMidgrade Octane = C.ZXV_OCTANE_MIDGRADE // up to face 89
	OctanePremium  Octane = C.ZXV_OCTANE_PREMIUM  // up to face 987
	OctaneRacing   Octane = C.ZXV_OCTANE_RACING   // up to face 75025
	OctaneMax      Octane = C.ZXV_OCTANE_MAX      // up to face F(65)
)

// TokenFace returns the face of tier and whether it is a super token.
// ZXV_E_RANGE for tier >= 64.
func TokenFace(tier uint32) (face uint64, super bool, err error) {
	var f C.uint64_t
	var s C.int32_t
	err = call(func() C.int32_t { return C.zxv_token_face(C.uint32_t(tier), &f, &s) })
	return uint64(f), s != 0, err
}

// TokenTier returns the tier whose face is exactly face (ErrDenom if none).
func TokenTier(face uint64) (uint32, error) {
	var t C.uint32_t
	err := call(func() C.int32_t { return C.zxv_token_tier(C.uint64_t(face), &t) })
	return uint32(t), err
}

// TokenHolding counts tokens per tier: h[k] tokens of face TokenFace(k).
// Its value is the sum of h[k] * face(k). The zero value is empty.
type TokenHolding [C.ZXV_TOKEN_TIERS]uint64

func (h *TokenHolding) ptr() *C.uint64_t { return (*C.uint64_t)(unsafe.Pointer(&h[0])) }

// Value returns the holding's value and its number of pieces (the sum of
// the counts). ZXV_E_RANGE when the value passes 64 bits.
func (h TokenHolding) Value() (value, pieces uint64, err error) {
	var v, p C.uint64_t
	err = call(func() C.int32_t { return C.zxv_token_value(h.ptr(), &v, &p) })
	return uint64(v), uint64(p), err
}

// CanonicalHolding returns the canonical form of value at octane:
// value / face(octane) tokens on that tier and the rest in Zeckendorf form
// below it. Every value has exactly one canonical form per octane.
func CanonicalHolding(value uint64, octane Octane) (TokenHolding, error) {
	var h TokenHolding
	err := call(func() C.int32_t { return C.zxv_token_canonical(C.uint64_t(value), C.uint32_t(octane), h.ptr()) })
	return h, err
}

// IsCanonical reports whether h is the canonical form of its value at
// octane (false also when a tier above the octane is used).
func (h TokenHolding) IsCanonical(octane Octane) (bool, error) {
	var c C.int32_t
	err := call(func() C.int32_t { return C.zxv_token_is_canonical(h.ptr(), C.uint32_t(octane), &c) })
	return c == 1, err
}

// Compress rewrites h in place into the canonical form of its value at
// octane; the value never changes. h is unchanged on any failure.
func (h *TokenHolding) Compress(octane Octane) error {
	return call(func() C.int32_t { return C.zxv_token_compress(h.ptr(), C.uint32_t(octane)) })
}

// Fuse turns one tier-k and one tier-(k+1) token into one tier-(k+2) token
// in place (F(k+2) + F(k+3) = F(k+4)). ZXV_E_STATE when tier k+2 is denser
// than octane allows; ErrInsufficientFunds when a token is missing;
// ZXV_E_RANGE for tier > 61 or a full count. h is unchanged on failure.
func (h *TokenHolding) Fuse(octane Octane, tier uint32) error {
	return call(func() C.int32_t { return C.zxv_token_fuse(h.ptr(), C.uint32_t(octane), C.uint32_t(tier)) })
}

// Split turns one tier-k token into one tier-(k-1) and one tier-(k-2)
// token in place (tier 1 becomes two plain tokens; tier 0 is atomic,
// ZXV_E_RANGE). ErrInsufficientFunds without a tier-k token. Splitting
// never needs an octane: it only makes a holding plainer.
func (h *TokenHolding) Split(tier uint32) error {
	return call(func() C.int32_t { return C.zxv_token_split(h.ptr(), C.uint32_t(tier)) })
}

// TokenCodeBits returns the number of bits in the Fibonacci code of value
// (2..93). ZXV_E_RANGE for math.MaxUint64, which has no code.
func TokenCodeBits(value uint64) (uint32, error) {
	var b C.uint32_t
	err := call(func() C.int32_t { return C.zxv_token_code_bits(C.uint64_t(value), &b) })
	return uint32(b), err
}

// PutTokenCode writes the Fibonacci code of value (Zeckendorf bits of
// value+1, lowest first, then a closing 1, so every code ends in "11" and
// needs no length field) into buf at bit position bit (bit i is bit i%8 of
// byte i/8) and returns the bit position after it. Only the code's bits are
// written. ZXV_E_BUFFER when the code does not fit (nothing written).
func PutTokenCode(buf []byte, bit uint32, value uint64) (uint32, error) {
	b := C.uint32_t(bit)
	err := call(func() C.int32_t { return C.zxv_token_code_put(bytePtr(buf), C.size_t(len(buf)), &b, C.uint64_t(value)) })
	if err != nil {
		return bit, err
	}
	return uint32(b), nil
}

// GetTokenCode reads one Fibonacci code from buf at bit position bit and
// returns its value and the bit position after it. ZXV_E_FORMAT when no
// closing "11" comes before the end; ZXV_E_RANGE when bit is past the end.
func GetTokenCode(buf []byte, bit uint32) (value uint64, next uint32, err error) {
	b := C.uint32_t(bit)
	var v C.uint64_t
	err = call(func() C.int32_t { return C.zxv_token_code_get(bytePtr(buf), C.size_t(len(buf)), &b, &v) })
	if err != nil {
		return 0, bit, err
	}
	return uint64(v), uint32(b), nil
}

// EncodeTokenCodes writes the Fibonacci codes of values one after another
// and returns the bytes and the number of bits used.
func EncodeTokenCodes(values []uint64) ([]byte, uint32, error) {
	var total uint64
	for _, v := range values {
		n, err := TokenCodeBits(v)
		if err != nil {
			return nil, 0, err
		}
		total += uint64(n)
	}
	buf := make([]byte, (total+7)/8)
	var bit uint32
	for _, v := range values {
		var err error
		if bit, err = PutTokenCode(buf, bit, v); err != nil {
			return nil, 0, err
		}
	}
	return buf, bit, nil
}

// DecodeTokenCodes reads count Fibonacci codes from the start of buf.
func DecodeTokenCodes(buf []byte, count int) ([]uint64, error) {
	out := make([]uint64, 0, count)
	var bit uint32
	for i := 0; i < count; i++ {
		v, next, err := GetTokenCode(buf, bit)
		if err != nil {
			return nil, err
		}
		out = append(out, v)
		bit = next
	}
	return out, nil
}

// ===== the token wallet =====================================================

// TokenWalletInfo is what TokenWallet.Info reports.
type TokenWalletInfo struct {
	ModelID uint32 // the wallet's free label
	Octane  Octane // the octane the wallet keeps canonical
	Value   uint64 // tokens held, in plain tokens
	Pieces  uint64 // number of pieces at the wallet's octane
}

// TokenWallet is a holding kept canonical at its own octane
// (zxv_token_wallet). Every change uses the capability it was created
// with, which needs CapMint (the wallet is not tied to a swarm budget, so
// tokens enter only by Mint). Reads need none. Every call fails with
// ErrFatal once the wallet's context halts. Safe for concurrent use; close
// it before its capability.
type TokenWallet struct{ ctxHandle }

// NewTokenWallet creates an empty wallet at octane, labelled modelID (a
// free label, e.g. the swarm model that holds it). cap needs CapMint.
func NewTokenWallet(cap *Capability, modelID uint32, octane Octane) (*TokenWallet, error) {
	var r *C.zxv_token_wallet
	if err := cap.gated(func(p *C.zxv_cap) C.int32_t {
		return C.zxv_token_wallet_create(p, C.uint32_t(modelID), C.uint32_t(octane), &r)
	}); err != nil {
		return nil, err
	}
	w := &TokenWallet{newCtxHandle("token wallet", unsafe.Pointer(r), func(p unsafe.Pointer) {
		C.zxv_token_wallet_destroy((*C.zxv_token_wallet)(p))
	}, cap, false)}
	runtime.SetFinalizer(w, func(w *TokenWallet) { _ = w.h.close() })
	return w, nil
}

// Close destroys the wallet.
func (w *TokenWallet) Close() error { return w.h.close() }

func (w *TokenWallet) gatedW(f func(cp *C.zxv_cap, p *C.zxv_token_wallet) C.int32_t) error {
	return w.gated(func(cp *C.zxv_cap, p unsafe.Pointer) C.int32_t { return f(cp, (*C.zxv_token_wallet)(p)) })
}

// Mint creates amount new tokens in the wallet. ZXV_E_RANGE (nothing
// changes) when the value would pass 64 bits.
func (w *TokenWallet) Mint(amount uint64) error {
	return w.gatedW(func(cp *C.zxv_cap, p *C.zxv_token_wallet) C.int32_t {
		return C.zxv_token_wallet_mint(cp, p, C.uint64_t(amount))
	})
}

// Spend burns amount; the change stays canonical. ErrInsufficientFunds
// (nothing changes) when the wallet holds less.
func (w *TokenWallet) Spend(amount uint64) error {
	return w.gatedW(func(cp *C.zxv_cap, p *C.zxv_token_wallet) C.int32_t {
		return C.zxv_token_wallet_spend(cp, p, C.uint64_t(amount))
	})
}

// Transfer moves amount from w to to. The payer keeps its change at its
// octane; the receiver gets the payment at its own octane; the total is
// conserved exactly. Both wallets must belong to w's context
// (ErrCapabilityDenied otherwise). Transfer to itself only checks the
// funds. Neither wallet changes on failure.
func (w *TokenWallet) Transfer(to *TokenWallet, amount uint64) error {
	if to == nil {
		return newError(StatusArg, "nil wallet")
	}
	if to == w {
		return w.gatedW(func(cp *C.zxv_cap, p *C.zxv_token_wallet) C.int32_t {
			return C.zxv_token_wallet_transfer(cp, p, p, C.uint64_t(amount))
		})
	}
	return to.h.use(func(tp unsafe.Pointer) error {
		return w.gatedW(func(cp *C.zxv_cap, p *C.zxv_token_wallet) C.int32_t {
			return C.zxv_token_wallet_transfer(cp, p, (*C.zxv_token_wallet)(tp), C.uint64_t(amount))
		})
	})
}

// SetOctane changes the octane (up = denser, down = plainer) and
// re-expresses the same value canonically.
func (w *TokenWallet) SetOctane(octane Octane) error {
	return w.gatedW(func(cp *C.zxv_cap, p *C.zxv_token_wallet) C.int32_t {
		return C.zxv_token_wallet_set_octane(cp, p, C.uint32_t(octane))
	})
}

// Info returns the label, octane, value and piece count.
func (w *TokenWallet) Info() (TokenWalletInfo, error) {
	var m, o C.uint32_t
	var v, p C.uint64_t
	err := w.local(func(q unsafe.Pointer) C.int32_t {
		return C.zxv_token_wallet_info((*C.zxv_token_wallet)(q), &m, &o, &v, &p)
	})
	return TokenWalletInfo{ModelID: uint32(m), Octane: Octane(o), Value: uint64(v), Pieces: uint64(p)}, err
}

// Counts returns the wallet's counts per tier.
func (w *TokenWallet) Counts() (TokenHolding, error) {
	var h TokenHolding
	err := w.local(func(q unsafe.Pointer) C.int32_t {
		return C.zxv_token_wallet_counts((*C.zxv_token_wallet)(q), h.ptr())
	})
	return h, err
}
