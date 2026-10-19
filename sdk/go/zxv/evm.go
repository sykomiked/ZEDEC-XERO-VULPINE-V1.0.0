// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

package zxv

/*
#include <stdlib.h>
#include "zxv_api.h"
*/
import "C"

import (
	"errors"
	"unsafe"
)

// Address is an Ethereum address.
type Address [20]byte

// EVMState is an encoded state-channel state and its digest.
type EVMState struct {
	// Encoding is abi.encode(bytes32 channelId, uint256 nonce, address[]
	// parties, int256[] balances).
	Encoding []byte
	// Digest is keccak256(Encoding): what the parties sign (EIP-191) in
	// their own wallets. ZXV has no secp256k1.
	Digest [32]byte
}

// StateChannel encodes exact ledger balances for
// sdk/solidity/ZxvStateChannel.sol. Needs CapEVM (and CapLedgerRead for
// LedgerState).
type StateChannel struct {
	cap *Capability
}

// NewStateChannel returns the encoder through cap.
func NewStateChannel(cap *Capability) *StateChannel { return &StateChannel{cap} }

func flatten(parties []Address) []byte {
	out := make([]byte, 0, 20*len(parties))
	for _, p := range parties {
		out = append(out, p[:]...)
	}
	return out
}

func (s *StateChannel) twoCall(f func(cp *C.zxv_cap, buf *C.uint8_t, capBytes C.size_t, n *C.size_t, dg *C.uint8_t) C.int32_t) (EVMState, error) {
	buf := make([]byte, 512)
	var st EVMState
	for {
		var n C.size_t
		err := s.cap.gated(func(cp *C.zxv_cap) C.int32_t {
			return f(cp, (*C.uint8_t)(unsafe.Pointer(&buf[0])), C.size_t(len(buf)), &n, (*C.uint8_t)(unsafe.Pointer(&st.Digest[0])))
		})
		if err == nil {
			st.Encoding = buf[:n]
			return st, nil
		}
		var e *Error
		if errors.As(err, &e) && e.Status == StatusBuffer && int(n) >= len(buf) {
			buf = make([]byte, int(n)+1)
			continue
		}
		return EVMState{}, err
	}
}

// EncodeState encodes explicit balances, one per party.
func (s *StateChannel) EncodeState(channelID [32]byte, nonce uint64, parties []Address, balances []int64) (EVMState, error) {
	if len(parties) != len(balances) || len(parties) == 0 {
		return EVMState{}, newError(StatusArg, "one balance per party, at least one party")
	}
	addrs := flatten(parties)
	bal := make([]C.int64_t, len(balances))
	for i, b := range balances {
		bal[i] = C.int64_t(b)
	}
	return s.twoCall(func(cp *C.zxv_cap, buf *C.uint8_t, cb C.size_t, n *C.size_t, dg *C.uint8_t) C.int32_t {
		return C.zxv_evm_encode_state(cp, (*C.uint8_t)(unsafe.Pointer(&channelID[0])), C.uint64_t(nonce),
			bytePtr(addrs), &bal[0], C.uint32_t(len(parties)), buf, cb, n, dg)
	})
}

// LedgerState encodes each account's EQUITY (888) balance, all in currency.
func (s *StateChannel) LedgerState(channelID [32]byte, nonce uint64, currency string, accounts []AccountID, parties []Address) (EVMState, error) {
	if len(parties) != len(accounts) || len(parties) == 0 {
		return EVMState{}, newError(StatusArg, "one account per party, at least one party")
	}
	ccy, err := cstring(currency)
	if err != nil {
		return EVMState{}, err
	}
	defer freeC(ccy)
	addrs := flatten(parties)
	ids := make([]C.uint32_t, len(accounts))
	for i, a := range accounts {
		ids[i] = C.uint32_t(a)
	}
	return s.twoCall(func(cp *C.zxv_cap, buf *C.uint8_t, cb C.size_t, n *C.size_t, dg *C.uint8_t) C.int32_t {
		return C.zxv_evm_ledger_state(cp, (*C.uint8_t)(unsafe.Pointer(&channelID[0])), C.uint64_t(nonce), ccy,
			&ids[0], bytePtr(addrs), C.uint32_t(len(parties)), buf, cb, n, dg)
	})
}
