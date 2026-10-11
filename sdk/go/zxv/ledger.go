// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

package zxv

/*
#include <stdlib.h>
#include "zxv_api.h"
*/
import "C"

// AccountID identifies an account inside one context.
type AccountID uint32

// AccountKind is Holder or Issuer.
type AccountKind int32

const (
	// Holder accounts may never go below zero.
	Holder AccountKind = 0
	// Issuer accounts may carry a CREDIT (777) balance: issuance, nostro.
	Issuer AccountKind = 1
)

// AmountMax is the largest amount of one posting and largest |balance|
// (2^53 - 1).
const AmountMax int64 = 9007199254740991

// Balance holds the rail balances of an account in exact minor units.
type Balance struct {
	Debit  int64 // 555: sum received
	Credit int64 // 777: sum sent
	Equity int64 // 888: Debit - Credit, the spendable position
}

// AccountInfo is an account's name, currency and kind.
type AccountInfo struct {
	Name     string
	Currency string
	Kind     AccountKind
}

// JournalLine is one line of an account's journal.
type JournalLine struct {
	EntryID      uint64
	Amount       int64 // + received, - sent
	Counterparty AccountID
	Reference    string
}

// Ledger is a context's ledger seen through a capability. Posting and
// opening accounts need CapLedgerPost; balances, account metadata and
// journals need CapLedgerRead. Every call is the capability-gated *_cap C
// form (ABI 1.2).
type Ledger struct {
	cap *Capability
}

// NewLedger returns the ledger seen through cap.
func NewLedger(cap *Capability) *Ledger { return &Ledger{cap: cap} }

// OpenAccount opens an account in an enabled currency (name <= 63 bytes).
func (l *Ledger) OpenAccount(name, currency string, kind AccountKind) (AccountID, error) {
	n, err := cstring(name)
	if err != nil {
		return 0, err
	}
	defer freeC(n)
	c, err := cstring(currency)
	if err != nil {
		return 0, err
	}
	defer freeC(c)
	var id C.uint32_t
	err = l.cap.gated(func(cp *C.zxv_cap) C.int32_t {
		return C.zxv_ledger_open_account_cap(cp, n, c, C.int32_t(kind), &id)
	})
	return AccountID(id), err
}

// Post transfers amountMinor (> 0) and returns the entry id. reference
// (<= 35 bytes) must be unique in the context. The posting is atomic.
func (l *Ledger) Post(from, to AccountID, amountMinor int64, currency, reference string) (uint64, error) {
	c, err := cstring(currency)
	if err != nil {
		return 0, err
	}
	defer freeC(c)
	r, err := cstring(reference)
	if err != nil {
		return 0, err
	}
	defer freeC(r)
	var e C.uint64_t
	err = l.cap.gated(func(cp *C.zxv_cap) C.int32_t {
		return C.zxv_ledger_post_cap(cp, C.uint32_t(from), C.uint32_t(to), C.int64_t(amountMinor), c, r, &e)
	})
	return uint64(e), err
}

// Balance returns the rail balances of an account.
func (l *Ledger) Balance(account AccountID) (Balance, error) {
	var d, c, e C.int64_t
	err := l.cap.gated(func(cp *C.zxv_cap) C.int32_t {
		return C.zxv_ledger_balance_cap(cp, C.uint32_t(account), &d, &c, &e)
	})
	return Balance{int64(d), int64(c), int64(e)}, err
}

// Check verifies every ledger invariant. A failure matches ErrFatal: the
// context is halted.
func (l *Ledger) Check() error {
	return l.cap.gated(func(cp *C.zxv_cap) C.int32_t { return C.zxv_ledger_check_cap(cp) })
}

// Account returns an account's name, currency and kind.
func (l *Ledger) Account(account AccountID) (AccountInfo, error) {
	var info AccountInfo
	err := l.cap.gatedSeq(func(cp *C.zxv_cap) error {
		var err error
		if info.Name, err = readString(func(b *C.char, c C.size_t, n *C.size_t) C.int32_t {
			return C.zxv_ledger_account_name_cap(cp, C.uint32_t(account), b, c, n)
		}); err != nil {
			return err
		}
		if info.Currency, err = readString(func(b *C.char, c C.size_t, n *C.size_t) C.int32_t {
			return C.zxv_ledger_account_ccy_cap(cp, C.uint32_t(account), b, c, n)
		}); err != nil {
			return err
		}
		var k C.int32_t
		err = call(func() C.int32_t { return C.zxv_ledger_account_kind_cap(cp, C.uint32_t(account), &k) })
		info.Kind = AccountKind(k)
		return err
	})
	return info, err
}

// AccountCount returns the number of accounts.
func (l *Ledger) AccountCount() (uint32, error) {
	var n C.uint32_t
	err := l.cap.gated(func(cp *C.zxv_cap) C.int32_t { return C.zxv_ledger_account_count_cap(cp, &n) })
	return uint32(n), err
}

// Journal returns an account's journal, oldest first.
func (l *Ledger) Journal(account AccountID) ([]JournalLine, error) {
	var out []JournalLine
	err := l.cap.gatedSeq(func(cp *C.zxv_cap) error {
		var n C.uint32_t
		if err := call(func() C.int32_t { return C.zxv_ledger_entry_count_cap(cp, C.uint32_t(account), &n) }); err != nil {
			return err
		}
		for i := C.uint32_t(0); i < n; i++ {
			var id C.uint64_t
			var amt C.int64_t
			var cpty C.uint32_t
			ref, err := readString(func(b *C.char, c C.size_t, ln *C.size_t) C.int32_t {
				return C.zxv_ledger_entry_at_cap(cp, C.uint32_t(account), i, &id, &amt, &cpty, b, c, ln)
			})
			if err != nil {
				return err
			}
			out = append(out, JournalLine{uint64(id), int64(amt), AccountID(cpty), ref})
		}
		return nil
	})
	return out, err
}
