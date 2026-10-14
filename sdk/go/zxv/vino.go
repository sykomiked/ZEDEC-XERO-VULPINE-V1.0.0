// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

package zxv

// Vino Embed (ABI 1.6): what an app or game needs to take part in Vino
// without ever holding anyone's value or keys. There are two lanes, and
// they never meet:
//
//   - PLAY is a sandbox inside the app (VinoPlay). It has its own play units
//     (never live Vino), its own issuers and holders and its own vouchers,
//     and it lives in process memory only. Nothing in this package takes a
//     VinoPlay together with anything live, so a play unit can never be
//     converted into, redeemed for, or moved into live value. Play units are
//     not money and are lost when the sandbox is closed.
//   - LIVE is real Vino, held in the user's own ZXV wallet and never in the
//     app. The app builds a payment request (VinoRequest); the user approves
//     it in their wallet, which signs a receipt with its ML-DSA-87 key
//     (SignVinoReceipt, the wallet's call); the app checks the receipt
//     offline (VerifyVinoReceipt). The app never sees a balance, a key or a
//     custody account.
//
// VOUCHERS (play lane). A voucher is one coin whose face is a Fibonacci
// ladder face (see CoinFace). It is held by one holder on exactly one active
// rail at a time: VinoBearer (a note in the holder's hand) or VinoGhost (its
// twin on the play ledger). A transfer must name the active rail (else
// ErrRail), so one voucher can never be spent twice. Switching rails moves
// nothing.
//
// COVERAGE. An issuer posts play backing (VinoPlay.Back) and may issue only
// while its backing stays at least 1.8 times everything it has outstanding:
// 10*backing >= 18*outstanding, in exact integers. An issue or a withdrawal
// of backing that would break this is refused (ErrCoverage). Redeeming a
// voucher returns it to its issuer and retires it: the issuer's outstanding
// falls and nothing is paid out to anyone. No call turns a voucher into
// backing, play units or anything else, and there is no interest, yield or
// return of any kind.
//
// REQUESTS AND RECEIPTS. A request is a fixed 160-byte canonical record
// (VinoRequestRecord); its id is SHA-256 of those bytes (VinoRequestID). A
// receipt is the wallet's ML-DSA-87 signature (empty context) over the 88
// bytes of VinoReceiptMessageFor. The request carries its expiry; a receipt
// for an expired request, or one approved after the expiry, is ErrExpired.
// A LIVE request marked as a paid chance outcome (VinoFlagChance: loot
// boxes, paid draws) is refused when it is encoded (ErrChance). The raffle
// is a separate module, not a payment request.
//
// DAILY CAP. A deployment may set a per-device daily cap; VinoMeter counts
// what one device approved per day and refuses an amount that would pass the
// cap (ErrLimit). It is a counter, not a legal check, and makes no exemption
// claim.
//
// CAPABILITIES. Every play-lane call and the meter need CapVino (else
// ErrCapabilityDenied; a halted context gives ErrPoisoned). Building a
// receipt message and verifying a receipt are pure and need no capability.
// Signing a receipt needs CapCryptoSign: it is the wallet's call, not the
// app's.
//
// NOT HERE, by design: minting live Vino, setting pegs, any conversion to or
// from bank money, V-Bill auctions or bids, V-Bonds, and every operator or
// steward action. None of them is app-facing.
//
// Honest limits: the LIVE flow here is only the app's half; approving a
// request happens in the user's wallet (zxvd), which posts the payment on
// its own ledger. ML-DSA-87 here is the reference implementation, not FIPS
// 140-3 validated. Whether a given deployment of Vino is store credit, a
// prepaid access product or something else is a legal question pending
// counsel; nothing in this package decides it.

/*
#include <stdlib.h>
#include "zxv_api.h"
*/
import "C"

import (
	"fmt"
	"runtime"
	"unsafe"
)

// Feat2Vino is the Features2 bit of the Vino Embed group (ABI 1.6).
const Feat2Vino Features2 = C.ZXV_FEAT2_VINO

// Status codes added in ABI 1.6 (the vino group). -51 .. -59 are reserved
// for it.
const (
	StatusRail     int32 = -46 // not the voucher's active rail (REFUSED)
	StatusCoverage int32 = -47 // would take an issuer below 1.8x coverage (REFUSED)
	StatusLane     int32 = -48 // wrong lane for this call (USAGE)
	StatusExpired  int32 = -49 // the request expired, or was approved too late (REFUSED)
	StatusChance   int32 = -50 // a paid chance outcome in a live request (REFUSED)
)

// Sentinels for the ABI 1.6 statuses: errors.Is(err, zxv.ErrCoverage).
var (
	// ErrRail: a transfer named a rail that is not the voucher's active
	// rail, or SetRail asked for the rail it is already on (ZXV_E_RAIL,
	// REFUSED).
	ErrRail error = &sentinel{name: "not the active rail", status: StatusRail}
	// ErrCoverage: the issue or withdrawal would take the issuer below 1.8x
	// coverage (ZXV_E_COVERAGE, REFUSED). Post more backing or redeem first.
	ErrCoverage error = &sentinel{name: "coverage below 1.8x", status: StatusCoverage}
	// ErrLane: a lane that is neither VinoLanePlay nor VinoLaneLive
	// (ZXV_E_LANE, USAGE).
	ErrLane error = &sentinel{name: "wrong lane", status: StatusLane}
	// ErrExpired: the request has expired, or the receipt was approved
	// outside the request's window (ZXV_E_EXPIRED, REFUSED).
	ErrExpired error = &sentinel{name: "request expired", status: StatusExpired}
	// ErrChance: a LIVE request marked as a paid chance outcome
	// (ZXV_E_CHANCE, REFUSED). Live payments never buy a chance outcome.
	ErrChance error = &sentinel{name: "paid chance outcome in a live request", status: StatusChance}
)

// Sizes and limits of the vino group.
const (
	VinoRequestBytes    = int(C.ZXV_VINO_REQ_BYTES)      // 160: an encoded request
	VinoRequestIDBytes  = 32                             // SHA-256 of the encoded request
	VinoReceiptMsgBytes = int(C.ZXV_VINO_RCPT_MSG_BYTES) // 88: what a wallet signs
	VinoMemoMax         = int(C.ZXV_VINO_MEMO_MAX)       // 64 printable ASCII bytes at most
	VinoMaxAccounts     = int(C.ZXV_VINO_MAX_ACCOUNTS)   // issuers + holders per play sandbox
	VinoMaxVouchers     = int(C.ZXV_VINO_MAX_VOUCHERS)   // active (unredeemed) vouchers per sandbox
	VinoAmountMax       = uint64(1) << 59                // largest amount, backing step or daily cap
)

// VinoRail is the rail a voucher is active on. A voucher is on exactly one
// rail at a time and can only be spent on that rail.
type VinoRail int32

const (
	// VinoBearer: the note in the holder's hand.
	VinoBearer VinoRail = C.ZXV_VINO_RAIL_BEARER
	// VinoGhost: its twin on the play ledger.
	VinoGhost VinoRail = C.ZXV_VINO_RAIL_GHOST
)

// String returns "BEARER" or "GHOST" (or "RAIL(n)" for another value).
func (r VinoRail) String() string {
	switch r {
	case VinoBearer:
		return "BEARER"
	case VinoGhost:
		return "GHOST"
	}
	return fmt.Sprintf("RAIL(%d)", int32(r))
}

// VinoLane is the lane of a payment request.
type VinoLane int32

const (
	// VinoLanePlay: the amount is in play units of an app's sandbox.
	VinoLanePlay VinoLane = C.ZXV_VINO_LANE_PLAY
	// VinoLaneLive: the amount is in VFV minor units, paid from the user's
	// own wallet.
	VinoLaneLive VinoLane = C.ZXV_VINO_LANE_LIVE
)

// String returns "PLAY" or "LIVE" (or "LANE(n)").
func (l VinoLane) String() string {
	switch l {
	case VinoLanePlay:
		return "PLAY"
	case VinoLaneLive:
		return "LIVE"
	}
	return fmt.Sprintf("LANE(%d)", int32(l))
}

// VinoVoucherState is the state of a voucher.
type VinoVoucherState int32

const (
	// VinoActive: held by a holder and spendable on its active rail.
	VinoActive VinoVoucherState = C.ZXV_VINO_V_ACTIVE
	// VinoRedeemed: returned to its issuer and retired. Nothing was paid out.
	VinoRedeemed VinoVoucherState = C.ZXV_VINO_V_REDEEMED
)

// String returns "ACTIVE" or "REDEEMED" (or "STATE(n)").
func (s VinoVoucherState) String() string {
	switch s {
	case VinoActive:
		return "ACTIVE"
	case VinoRedeemed:
		return "REDEEMED"
	}
	return fmt.Sprintf("STATE(%d)", int32(s))
}

// VinoAccountKind says whether a play account is an issuer or a holder.
type VinoAccountKind int32

const (
	// VinoHolder holds vouchers and spends them.
	VinoHolder VinoAccountKind = 0
	// VinoIssuer posts backing and issues vouchers against it.
	VinoIssuer VinoAccountKind = 1
)

// String returns "HOLDER" or "ISSUER" (or "KIND(n)").
func (k VinoAccountKind) String() string {
	switch k {
	case VinoHolder:
		return "HOLDER"
	case VinoIssuer:
		return "ISSUER"
	}
	return fmt.Sprintf("KIND(%d)", int32(k))
}

// VinoRequestFlags are the flags of a payment request.
type VinoRequestFlags uint32

const (
	// VinoFlagChance marks a purchase with a paid chance outcome. Allowed on
	// the PLAY lane only; a LIVE request with it is refused (ErrChance).
	VinoFlagChance VinoRequestFlags = C.ZXV_VINO_REQ_CHANCE
	// VinoFlagsKnown is every flag this ABI defines; any other bit is
	// refused (ZXV_E_FORMAT, USAGE).
	VinoFlagsKnown VinoRequestFlags = C.ZXV_VINO_REQ_FLAGS_KNOWN
)

// VinoRequestRecord is an encoded payment request: exactly 160 canonical
// bytes. It is a value type; compare with ==.
type VinoRequestRecord [160]byte

// VinoRequestID is a request's id: SHA-256 of its 160 encoded bytes.
type VinoRequestID [32]byte

// VinoReceiptMessage is the 88 bytes a wallet signs to approve a request:
// "zxv/vino-rcpt/v1" || request id || SHA-256(wallet public key) ||
// approved_at (8 bytes, little-endian).
type VinoReceiptMessage [88]byte

// VinoRequest is a payment request as plain values. Encode it to the 160
// bytes a wallet approves; decode bytes back to it.
type VinoRequest struct {
	Lane      VinoLane         // VinoLanePlay or VinoLaneLive
	Flags     VinoRequestFlags // 0 or VinoFlagChance (PLAY lane only)
	Merchant  [32]byte         // the payee's id, e.g. SHA-256 of its public key
	Nonce     [16]byte         // unique per request, so two requests never share an id
	Amount    uint64           // 1 .. VinoAmountMax: VFV minor units (LIVE) or play units (PLAY)
	CreatedAt uint64           // seconds of the caller's clock (Unix time by convention)
	ExpiresAt uint64           // must be above CreatedAt
	Memo      string           // 0..64 bytes of printable ASCII (0x20..0x7e)
}

// Encode validates the request and returns its canonical 160 bytes and its
// id (SHA-256 of those bytes). Pure: no capability. Errors: ErrLane (lane
// not PLAY or LIVE, USAGE), ErrChance (a LIVE request with VinoFlagChance,
// REFUSED), ZXV_E_RANGE (Amount 0 or above VinoAmountMax, ExpiresAt not
// above CreatedAt), ZXV_E_FORMAT (an unknown flag, a memo byte outside
// printable ASCII), ZXV_E_TOO_LONG (memo above 64 bytes). A refusal returns
// zero values.
func (r VinoRequest) Encode() (VinoRequestRecord, VinoRequestID, error) {
	var out VinoRequestRecord
	var id VinoRequestID
	for i := 0; i < len(r.Memo); i++ {
		if r.Memo[i] == 0 {
			// A NUL would end the C string early and silently cut the memo.
			return out, id, newError(StatusFormat, "memo is not printable ASCII (NUL byte)")
		}
	}
	memo := C.CString(r.Memo)
	defer freeC(memo)
	err := call(func() C.int32_t {
		return C.zxv_vino_request_encode(C.int32_t(r.Lane), C.uint32_t(r.Flags),
			(*C.uint8_t)(unsafe.Pointer(&r.Merchant[0])), (*C.uint8_t)(unsafe.Pointer(&r.Nonce[0])),
			C.uint64_t(r.Amount), C.uint64_t(r.CreatedAt), C.uint64_t(r.ExpiresAt), memo,
			(*C.uint8_t)(unsafe.Pointer(&out[0])), (*C.uint8_t)(unsafe.Pointer(&id[0])))
	})
	if err != nil {
		return VinoRequestRecord{}, VinoRequestID{}, err
	}
	return out, id, nil
}

// ID returns the request's id: SHA-256 of its 160 encoded bytes. It is the
// same as the id Encode returns; the errors are Encode's.
func (r VinoRequest) ID() (VinoRequestID, error) {
	_, id, err := r.Encode()
	return id, err
}

// DecodeVinoRequest validates 160 request bytes and returns the request and
// its id. Pure: no capability. It applies Encode's rules plus the magic
// "ZXVR", version 1 and zero padding (ZXV_E_FORMAT); its errors are
// Encode's. Decoding then encoding gives back the same bytes.
func DecodeVinoRequest(b VinoRequestRecord) (VinoRequest, VinoRequestID, error) {
	var r VinoRequest
	var id VinoRequestID
	var lane C.int32_t
	var flags C.uint32_t
	var amount, created, expires C.uint64_t
	var memo [C.ZXV_VINO_MEMO_MAX + 1]C.char
	err := call(func() C.int32_t {
		return C.zxv_vino_request_decode((*C.uint8_t)(unsafe.Pointer(&b[0])), &lane, &flags,
			(*C.uint8_t)(unsafe.Pointer(&r.Merchant[0])), (*C.uint8_t)(unsafe.Pointer(&r.Nonce[0])),
			&amount, &created, &expires, &memo[0], (*C.uint8_t)(unsafe.Pointer(&id[0])))
	})
	if err != nil {
		return VinoRequest{}, VinoRequestID{}, err
	}
	r.Lane, r.Flags = VinoLane(lane), VinoRequestFlags(flags)
	r.Amount, r.CreatedAt, r.ExpiresAt = uint64(amount), uint64(created), uint64(expires)
	r.Memo = C.GoString(&memo[0])
	return r, id, nil
}

// VinoReceiptMessageFor returns the 88 bytes a wallet with public key
// walletPK (MLDSA87PKBytes) signs to approve request id at approvedAt. Pure:
// no capability. Errors: ErrUsage for a public key of the wrong length.
func VinoReceiptMessageFor(id VinoRequestID, walletPK []byte, approvedAt uint64) (VinoReceiptMessage, error) {
	var m VinoReceiptMessage
	if err := need(walletPK, MLDSA87PKBytes, "wallet public key"); err != nil {
		return m, err
	}
	err := call(func() C.int32_t {
		return C.zxv_vino_receipt_msg((*C.uint8_t)(unsafe.Pointer(&id[0])), bytePtr(walletPK), C.uint64_t(approvedAt),
			(*C.uint8_t)(unsafe.Pointer(&m[0])))
	})
	return m, err
}

// SignVinoReceipt is the WALLET's half of a payment: it approves request at
// approvedAt, which must lie within [CreatedAt, ExpiresAt] of the request,
// and returns the ML-DSA-87 signature (MLDSA87SigBytes) over
// VinoReceiptMessageFor. cap needs CapCryptoSign. An app never calls this:
// it never holds the wallet's key. walletSK is used in place (never copied
// by this package); walletPK must be its public key.
//
// Errors: ErrExpired (approvedAt outside the window), the decode errors
// for malformed request bytes (ZXV_E_FORMAT, ErrLane, ErrChance,
// ZXV_E_RANGE, ZXV_E_TOO_LONG), ErrNotImplemented (built without PQC),
// ErrCapabilityDenied.
func SignVinoReceipt(cap *Capability, request VinoRequestRecord, walletSK *SecretKey, walletPK []byte, approvedAt uint64) ([]byte, error) {
	if err := need(walletPK, MLDSA87PKBytes, "wallet public key"); err != nil {
		return nil, err
	}
	sig := make([]byte, MLDSA87SigBytes)
	err := walletSK.use(MLDSA87SKBytes, "wallet secret key", func(s *C.uint8_t) error {
		return cap.gated(func(cp *C.zxv_cap) C.int32_t {
			return C.zxv_vino_receipt_sign(cp, (*C.uint8_t)(unsafe.Pointer(&request[0])), s, bytePtr(walletPK),
				C.uint64_t(approvedAt), bytePtr(sig))
		})
	})
	if err != nil {
		return nil, err
	}
	return sig, nil
}

// VerifyVinoReceipt is the APP's half, offline and pure (no capability):
// it returns nil when sig is the wallet's (walletPK) approval of request at
// approvedAt, approvedAt lies within the request's window, and now is not
// past its expiry. A good receipt proves the wallet approved; the payment
// itself is posted by the wallet on its own ledger.
//
// Errors: ErrVerificationFailed (not that wallet's signature over this
// request and time), ErrExpired, the decode errors for malformed request
// bytes, ErrNotImplemented (built without PQC), ErrUsage for a key or
// signature of the wrong length.
func VerifyVinoReceipt(request VinoRequestRecord, walletPK []byte, approvedAt uint64, sig []byte, now uint64) error {
	if err := need(walletPK, MLDSA87PKBytes, "wallet public key"); err != nil {
		return err
	}
	if err := need(sig, MLDSA87SigBytes, "receipt signature"); err != nil {
		return err
	}
	return call(func() C.int32_t {
		return C.zxv_vino_receipt_verify((*C.uint8_t)(unsafe.Pointer(&request[0])), bytePtr(walletPK),
			C.uint64_t(approvedAt), bytePtr(sig), C.uint64_t(now))
	})
}

// ===== the play lane ======================================================

// VinoVoucher is what VinoPlay.Voucher reports about one voucher.
type VinoVoucher struct {
	Face   uint64           // its Fibonacci ladder face, in play units
	Issuer uint32           // the issuer account that issued it
	Holder uint32           // the holder account that holds it (or last held it)
	Rail   VinoRail         // its active rail
	State  VinoVoucherState // VinoActive or VinoRedeemed
}

// VinoAccount is what VinoPlay.Account reports about one account.
type VinoAccount struct {
	Owner uint32          // the owner id given to Open
	Kind  VinoAccountKind // VinoIssuer or VinoHolder
	// Backing is an issuer's posted play backing (0 for a holder).
	Backing uint64
	// Faces is, for an issuer, the sum of the faces it has outstanding; for
	// a holder, the sum of the faces it holds.
	Faces uint64
	// Count is the number of active vouchers it issued (issuer) or holds
	// (holder).
	Count uint32
}

// VinoPlay is one play-lane sandbox (zxv_vino_play): issuers, holders and
// vouchers of play units, in process memory only. Play units are not money,
// cannot become live value, and are lost on Close. Safe for concurrent use
// (the C sandbox serializes its calls). It uses the capability it was made
// with for every call, and that capability cannot close while it is open.
type VinoPlay struct{ ctxHandle }

// NewVinoPlay creates an empty play sandbox on cap's context. cap needs
// CapVino. Errors: ErrCapabilityDenied, ErrPoisoned, ZXV_E_NOMEM.
func NewVinoPlay(cap *Capability) (*VinoPlay, error) {
	var r *C.zxv_vino_play
	if err := cap.gated(func(p *C.zxv_cap) C.int32_t { return C.zxv_vino_play_create(p, &r) }); err != nil {
		return nil, err
	}
	h := &VinoPlay{newCtxHandle("vino play sandbox", unsafe.Pointer(r),
		func(p unsafe.Pointer) { C.zxv_vino_play_destroy((*C.zxv_vino_play)(p)) }, cap, false)}
	runtime.SetFinalizer(h, func(h *VinoPlay) { _ = h.h.close() })
	return h, nil
}

// Close destroys the sandbox and every play unit and voucher in it. A second
// Close is a no-op; any other call afterwards returns ErrClosed.
func (v *VinoPlay) Close() error { return v.h.close() }

func (v *VinoPlay) do(f func(cp *C.zxv_cap, p *C.zxv_vino_play) C.int32_t) error {
	return v.gated(func(cp *C.zxv_cap, p unsafe.Pointer) C.int32_t { return f(cp, (*C.zxv_vino_play)(p)) })
}

// Open opens an account of the given kind for owner (an id of your
// choosing; owners need not be distinct) and returns its account number.
// Account numbers count up from 0. Errors: ErrUsage (a kind other than
// VinoIssuer or VinoHolder), ZXV_E_CAPACITY (VinoMaxAccounts are open).
func (v *VinoPlay) Open(owner uint32, kind VinoAccountKind) (uint32, error) {
	var a C.uint32_t
	err := v.do(func(cp *C.zxv_cap, p *C.zxv_vino_play) C.int32_t {
		return C.zxv_vino_play_open(cp, p, C.uint32_t(owner), C.int32_t(kind), &a)
	})
	return uint32(a), err
}

// Back posts amount play units of backing to an issuer. This is the play
// faucet: play units come from nowhere and are worth nothing. Errors:
// ErrUsage (not an issuer), ErrNotFound (no such account), ZXV_E_RANGE
// (amount 0, above VinoAmountMax, or backing would pass 2^62).
func (v *VinoPlay) Back(issuer uint32, amount uint64) error {
	return v.do(func(cp *C.zxv_cap, p *C.zxv_vino_play) C.int32_t {
		return C.zxv_vino_play_back(cp, p, C.uint32_t(issuer), C.uint64_t(amount))
	})
}

// Unback withdraws amount of an issuer's backing, only while it stays at
// 1.8x coverage of what it has outstanding. Errors: ErrCoverage (it would
// not), ErrInsufficientFunds (more than its backing), and Back's errors. A
// refusal changes nothing.
func (v *VinoPlay) Unback(issuer uint32, amount uint64) error {
	return v.do(func(cp *C.zxv_cap, p *C.zxv_vino_play) C.int32_t {
		return C.zxv_vino_play_unback(cp, p, C.uint32_t(issuer), C.uint64_t(amount))
	})
}

// Issue issues one voucher of Fibonacci face from issuer to holder, active
// on rail, and returns its id (above 0, never reused in this sandbox).
// Errors: ErrDenom (face not a ladder face), ErrCoverage (the issuer would
// fall below 1.8x), ErrUsage (issuer not an issuer, holder not a holder,
// bad rail), ErrNotFound, ZXV_E_CAPACITY (VinoMaxVouchers active).
func (v *VinoPlay) Issue(issuer, holder uint32, face uint64, rail VinoRail) (uint64, error) {
	var id C.uint64_t
	err := v.do(func(cp *C.zxv_cap, p *C.zxv_vino_play) C.int32_t {
		return C.zxv_vino_issue(cp, p, C.uint32_t(issuer), C.uint32_t(holder), C.uint64_t(face), C.int32_t(rail), &id)
	})
	if err != nil {
		return 0, err
	}
	return uint64(id), nil
}

// Transfer spends an active voucher: it moves from holder from to holder
// to, on rail, which must be its active rail (the rail does not change).
// Errors: ErrRail (not the active rail), ZXV_E_STATE (already redeemed),
// ErrUsage (from is not its holder, to is not a holder, from == to, bad
// rail), ErrNotFound (no such voucher or account).
func (v *VinoPlay) Transfer(voucher uint64, from, to uint32, rail VinoRail) error {
	return v.do(func(cp *C.zxv_cap, p *C.zxv_vino_play) C.int32_t {
		return C.zxv_vino_transfer(cp, p, C.uint64_t(voucher), C.uint32_t(from), C.uint32_t(to), C.int32_t(rail))
	})
}

// SetRail switches an active voucher, by its holder, to rail (bearer <->
// ghost). Nothing moves. Errors: ErrRail (it is already on that rail),
// ZXV_E_STATE (redeemed), ErrUsage (not its holder, bad rail), ErrNotFound.
func (v *VinoPlay) SetRail(voucher uint64, holder uint32, rail VinoRail) error {
	return v.do(func(cp *C.zxv_cap, p *C.zxv_vino_play) C.int32_t {
		return C.zxv_vino_set_rail(cp, p, C.uint64_t(voucher), C.uint32_t(holder), C.int32_t(rail))
	})
}

// Redeem returns an active voucher, by its holder, to its issuer: it is
// retired and the issuer's outstanding falls by its face. Nothing is paid
// out to anyone. Errors: ZXV_E_STATE (already redeemed), ErrUsage (not its
// holder), ErrNotFound.
func (v *VinoPlay) Redeem(voucher uint64, holder uint32) error {
	return v.do(func(cp *C.zxv_cap, p *C.zxv_vino_play) C.int32_t {
		return C.zxv_vino_redeem(cp, p, C.uint64_t(voucher), C.uint32_t(holder))
	})
}

// Voucher reports one voucher, active or redeemed (ErrNotFound for an id
// never issued).
func (v *VinoPlay) Voucher(id uint64) (VinoVoucher, error) {
	var face C.uint64_t
	var iss, hold C.uint32_t
	var rail, state C.int32_t
	err := v.do(func(cp *C.zxv_cap, p *C.zxv_vino_play) C.int32_t {
		return C.zxv_vino_voucher_info(cp, p, C.uint64_t(id), &face, &iss, &hold, &rail, &state)
	})
	if err != nil {
		return VinoVoucher{}, err
	}
	return VinoVoucher{Face: uint64(face), Issuer: uint32(iss), Holder: uint32(hold),
		Rail: VinoRail(rail), State: VinoVoucherState(state)}, nil
}

// Account reports one account's owner, kind and totals (ErrNotFound past
// the last account).
func (v *VinoPlay) Account(account uint32) (VinoAccount, error) {
	var owner, count C.uint32_t
	var isIssuer C.int32_t
	var backing, faces C.uint64_t
	err := v.do(func(cp *C.zxv_cap, p *C.zxv_vino_play) C.int32_t {
		return C.zxv_vino_account_info(cp, p, C.uint32_t(account), &owner, &isIssuer, &backing, &faces, &count)
	})
	if err != nil {
		return VinoAccount{}, err
	}
	return VinoAccount{Owner: uint32(owner), Kind: VinoAccountKind(isIssuer), Backing: uint64(backing),
		Faces: uint64(faces), Count: uint32(count)}, nil
}

// Vouchers returns the ids of the active vouchers an account issued
// (issuer) or holds (holder), ascending; an empty, non-nil slice when there
// are none. ErrNotFound past the last account.
func (v *VinoPlay) Vouchers(account uint32) ([]uint64, error) {
	var out []uint64
	err := v.gatedSeq(func(cp *C.zxv_cap, p unsafe.Pointer) error {
		runtime.LockOSThread()
		defer runtime.UnlockOSThread()
		buf := make([]uint64, 8)
		for i := 0; i < 4; i++ {
			var n C.size_t
			rc := C.zxv_vino_vouchers(cp, (*C.zxv_vino_play)(p), C.uint32_t(account),
				(*C.uint64_t)(unsafe.Pointer(&buf[0])), C.size_t(len(buf)), &n)
			if rc == C.ZXV_OK {
				out = buf[:int(n):int(n)]
				return nil
			}
			if rc == C.ZXV_E_BUFFER && int(n) > len(buf) {
				buf = make([]uint64, int(n))
				continue
			}
			return newError(int32(rc), lastError())
		}
		return newError(StatusInternal, "the voucher list kept growing")
	})
	if err != nil {
		return nil, err
	}
	return out, nil
}

// Check re-checks every invariant of the sandbox: each active voucher has a
// ladder face, one rail, an issuer and a holder; each issuer's outstanding
// equals the sum of its active faces and 10*backing >= 18*outstanding; each
// holder's total equals what it holds. A failure is ErrFatal (ZXV_E_INVARIANT)
// and poisons the context.
func (v *VinoPlay) Check() error {
	return v.do(func(cp *C.zxv_cap, p *C.zxv_vino_play) C.int32_t { return C.zxv_vino_play_check(cp, p) })
}

// ===== the daily meter ====================================================

// VinoMeter counts what one device approved per day against a daily cap
// (zxv_vino_meter). It is a counter, not a legal check, and makes no
// exemption claim. Safe for concurrent use.
type VinoMeter struct{ ctxHandle }

// NewVinoMeter creates a meter with cap dailyCap (1 .. VinoAmountMax) per
// day. cap needs CapVino. Errors: ZXV_E_RANGE (dailyCap out of range),
// ErrCapabilityDenied, ErrPoisoned.
func NewVinoMeter(cap *Capability, dailyCap uint64) (*VinoMeter, error) {
	var r *C.zxv_vino_meter
	if err := cap.gated(func(p *C.zxv_cap) C.int32_t { return C.zxv_vino_meter_create(p, C.uint64_t(dailyCap), &r) }); err != nil {
		return nil, err
	}
	m := &VinoMeter{newCtxHandle("vino meter", unsafe.Pointer(r),
		func(p unsafe.Pointer) { C.zxv_vino_meter_destroy((*C.zxv_vino_meter)(p)) }, cap, false)}
	runtime.SetFinalizer(m, func(m *VinoMeter) { _ = m.h.close() })
	return m, nil
}

// Close destroys the meter. A second Close is a no-op.
func (m *VinoMeter) Close() error { return m.h.close() }

// Admit counts amount against day (for example Unix time / 86400) and
// returns what is left of that day's cap afterwards. A day above the last
// one starts a fresh count; a day below it is ZXV_E_RANGE (the clock went
// back). When the day's total would pass the cap the error is ErrLimit,
// nothing is counted, and left still reports what remains today. Other
// errors: ZXV_E_RANGE (amount 0 or above VinoAmountMax).
func (m *VinoMeter) Admit(day, amount uint64) (left uint64, err error) {
	var l C.uint64_t
	err = m.gated(func(cp *C.zxv_cap, p unsafe.Pointer) C.int32_t {
		return C.zxv_vino_meter_admit(cp, (*C.zxv_vino_meter)(p), C.uint64_t(day), C.uint64_t(amount), &l)
	})
	return uint64(l), err
}
