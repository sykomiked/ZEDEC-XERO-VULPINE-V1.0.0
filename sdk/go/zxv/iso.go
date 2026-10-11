// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

package zxv

/*
#include <stdlib.h>
#include "zxv_api.h"
*/
import "C"

import (
	"runtime"
	"unsafe"
)

// Currency flags (ZXV_CCYF_*).
const (
	CcyFund      uint32 = 0x01 // fund code (CLF, USN)
	CcyMinorNA   uint32 = 0x02 // minor unit N.A.
	CcyNoCountry uint32 = 0x04 // not the currency of any country
	CcyPayable   uint32 = 0x08 // usable for a payment amount
	CcyPrivate   uint32 = 0x10 // platform-private unit
)

// Currency is one ISO 4217 entry.
type Currency struct {
	Alpha      string
	Numeric    uint16
	MinorUnits int // -1 when ISO says N.A.
	Flags      uint32
}

func minorUnits(m C.uint8_t) int {
	if m == C.ZXV_MINOR_NA {
		return -1
	}
	return int(m)
}

// CurrencyByAlpha looks up an ISO 4217 currency ("NGN"). Non-ISO codes
// (including platform units such as VFV) match ErrNotFound.
func CurrencyByAlpha(alpha string) (Currency, error) {
	a, err := cstring(alpha)
	if err != nil {
		return Currency{}, err
	}
	defer freeC(a)
	var num C.uint16_t
	var mu C.uint8_t
	var fl C.uint32_t
	if err := call(func() C.int32_t { return C.zxv_iso4217_by_alpha(a, &num, &mu, &fl) }); err != nil {
		return Currency{}, err
	}
	return Currency{alpha, uint16(num), minorUnits(mu), uint32(fl)}, nil
}

// CurrencyByNumeric looks up by numeric code (566 -> NGN). The Vino rail
// numerics 555/777/888 are not ISO codes.
func CurrencyByNumeric(numeric uint16) (Currency, error) {
	var mu C.uint8_t
	var fl C.uint32_t
	s, err := readString(func(b *C.char, c C.size_t, n *C.size_t) C.int32_t {
		return C.zxv_iso4217_by_numeric(C.uint16_t(numeric), b, c, n, &mu, &fl)
	})
	if err != nil {
		return Currency{}, err
	}
	return Currency{s, numeric, minorUnits(mu), uint32(fl)}, nil
}

// CurrencyName returns the English name of a currency ("Naira").
func CurrencyName(alpha string) (string, error) {
	a, err := cstring(alpha)
	if err != nil {
		return "", err
	}
	defer freeC(a)
	return readString(func(b *C.char, c C.size_t, n *C.size_t) C.int32_t { return C.zxv_iso4217_name(a, b, c, n) })
}

// Currencies lists every alpha-3 code of the compiled ISO 4217 table.
func Currencies() ([]string, error) {
	var n C.uint32_t
	if err := call(func() C.int32_t { return C.zxv_iso4217_count(&n) }); err != nil {
		return nil, err
	}
	out := make([]string, 0, int(n))
	for i := C.uint32_t(0); i < n; i++ {
		s, err := readString(func(b *C.char, c C.size_t, l *C.size_t) C.int32_t { return C.zxv_iso4217_at(i, b, c, l) })
		if err != nil {
			return nil, err
		}
		out = append(out, s)
	}
	return out, nil
}

// ISO4217Published returns the publication date of the compiled list.
func ISO4217Published() (string, error) {
	return readString(func(b *C.char, c C.size_t, n *C.size_t) C.int32_t { return C.zxv_iso4217_published(b, c, n) })
}

// CurrencyCaveat returns the interop caveat of a numeric code (e.g. 777),
// "" if none.
func CurrencyCaveat(numeric uint16) (string, error) {
	return readString(func(b *C.char, c C.size_t, n *C.size_t) C.int32_t {
		return C.zxv_ccy_caveat(C.uint16_t(numeric), b, c, n)
	})
}

// IsAUMember reports whether an ISO 3166 alpha-2 country is an African
// Union member.
func IsAUMember(country string) (bool, error) {
	a, err := cstring(country)
	if err != nil {
		return false, err
	}
	defer freeC(a)
	var m C.int32_t
	err = call(func() C.int32_t { return C.zxv_au_is_member(a, &m) })
	return m != 0, err
}

// Rail is one of the three Vino rails of every account.
type Rail int32

// The rails.
const (
	RailDebit  Rail = 0 // 555 NCR: asset / backing
	RailCredit Rail = 1 // 777 NRE: claim / liability
	RailEquity Rail = 2 // 888 PNS: debit - credit
)

// Numeric returns the platform-internal numeric (555, 777, 888).
func (r Rail) Numeric() (uint16, error) {
	var n C.uint16_t
	err := call(func() C.int32_t { return C.zxv_rail_numeric(C.int32_t(r), &n) })
	return uint16(n), err
}

// CardNetwork selects a check-digit scheme.
type CardNetwork int32

// Card networks.
const (
	Phoenix     CardNetwork = 1 // Damm
	Dragon      CardNetwork = 2 // Luhn
	Thunderbird CardNetwork = 3 // Verhoeff
)

// CheckDigitValid reports whether a decimal PAN has a valid check digit.
func (n CardNetwork) CheckDigitValid(pan string) (bool, error) {
	p, err := cstring(pan)
	if err != nil {
		return false, err
	}
	defer freeC(p)
	var v C.int32_t
	err = call(func() C.int32_t { return C.zxv_card_check_digit(C.int32_t(n), p, &v) })
	return v != 0, err
}

// MessageKind is an ISO 20022 message kind. Only Pacs008 and Camt053 are
// built into this library; the others match ErrNotImplemented.
type MessageKind int32

// Message kinds.
const (
	Pacs008 MessageKind = 1
	Camt053 MessageKind = 2
	Pacs002 MessageKind = 3
	Pacs004 MessageKind = 4
	Pacs009 MessageKind = 5
	Camt056 MessageKind = 6
	Camt029 MessageKind = 7
)

// TextField is a message text field.
type TextField int32

// Text fields (each kind accepts a subset; others are ZXV_E_FIELD).
const (
	FieldMsgID           TextField = 1 // GrpHdr/MsgId <= 35
	FieldCreDtTm         TextField = 2 // GrpHdr/CreDtTm (ISO 8601)
	FieldDebtorName      TextField = 3 // Dbtr/Nm <= 70
	FieldDebtorAccount   TextField = 4 // DbtrAcct/Id <= 35
	FieldCreditorName    TextField = 5 // Cdtr/Nm <= 70
	FieldCreditorAccount TextField = 6 // CdtrAcct/Id <= 35
	FieldEndToEndID      TextField = 7 // PmtId/EndToEndId <= 35
	FieldCurrency        TextField = 8 // ISO 4217 alpha-3
	FieldAccountID       TextField = 9 // camt053 Stmt/Acct/Id/Othr/Id
)

// AmountField is a message amount field.
type AmountField int32

// Amount fields.
const (
	AmountSettlement AmountField = 1 // pacs008 IntrBkSttlmAmt
	AmountOpening    AmountField = 2 // camt053 OPBD
	AmountClosing    AmountField = 3 // camt053 CLBD
)

// CreditDebit is CRDT or DBIT.
type CreditDebit int32

// Directions.
const (
	Credit CreditDebit = 0
	Debit  CreditDebit = 1
)

// EntryStatus is BOOK, PDNG or INFO.
type EntryStatus int32

// Statuses.
const (
	Booked  EntryStatus = 0
	Pending EntryStatus = 1
	Info    EntryStatus = 2
)

// Message is one ISO 20022 message under construction. Calls are
// serialized (the C handle is not thread-safe). Close it before its
// context.
type Message struct {
	h   *handle
	ctx *Context
	cap *Capability
}

// NewMessage starts a message of kind through c, which needs
// CapLedgerRead. The library re-checks c on every message call, so
// revoking it stops the message too; c cannot be closed while the message
// is open.
func (c *Capability) NewMessage(kind MessageKind) (*Message, error) {
	var m *C.zxv_msg
	if err := c.gated(func(p *C.zxv_cap) C.int32_t { return C.zxv_msg_create_cap(p, C.int32_t(kind), &m) }); err != nil {
		return nil, err
	}
	msg := &Message{ctx: c.ctx, cap: c}
	msg.h = newHandle("message", unsafe.Pointer(m), func(p unsafe.Pointer) { C.zxv_msg_destroy((*C.zxv_msg)(p)) }, c.ctx.h, c.h)
	msg.h.exclusive = true
	runtime.SetFinalizer(msg, func(m *Message) { _ = m.h.close() })
	return msg, nil
}

// NewMessage starts a message of kind in ctx.
//
// Deprecated: use (*Capability).NewMessage with a CapLedgerRead capability.
func (c *Context) NewMessage(kind MessageKind) (*Message, error) {
	var m *C.zxv_msg
	if err := c.ctxCall(func(p *C.zxv_ctx) C.int32_t { return C.zxv_msg_create(p, C.int32_t(kind), &m) }); err != nil {
		return nil, err
	}
	msg := &Message{ctx: c}
	msg.h = newHandle("message", unsafe.Pointer(m), func(p unsafe.Pointer) { C.zxv_msg_destroy((*C.zxv_msg)(p)) }, c.h)
	msg.h.exclusive = true
	runtime.SetFinalizer(msg, func(m *Message) { _ = m.h.close() })
	return msg, nil
}

// Close destroys the message.
func (m *Message) Close() error { return m.h.close() }

func (m *Message) do(f func(p *C.zxv_msg) C.int32_t) error {
	if err := m.ctx.guard(); err != nil {
		return err
	}
	err := m.h.use(func(p unsafe.Pointer) error { return call(func() C.int32_t { return f((*C.zxv_msg)(p)) }) })
	return m.ctx.fatal(err)
}

// SetText sets a text field (plain text; the serializer escapes XML).
func (m *Message) SetText(field TextField, value string) error {
	v, err := cstring(value)
	if err != nil {
		return err
	}
	defer freeC(v)
	return m.do(func(p *C.zxv_msg) C.int32_t { return C.zxv_msg_set_text(p, C.int32_t(field), v) })
}

// SetAmount sets an amount as exact minor units with fracDigits decimals.
func (m *Message) SetAmount(field AmountField, units int64, fracDigits uint8) error {
	return m.do(func(p *C.zxv_msg) C.int32_t {
		return C.zxv_msg_set_amount(p, C.int32_t(field), C.int64_t(units), C.uint8_t(fracDigits))
	})
}

// SetTxCount sets pacs.008 GrpHdr/NbOfTxs (default 1).
func (m *Message) SetTxCount(n uint32) error {
	return m.do(func(p *C.zxv_msg) C.int32_t { return C.zxv_msg_set_tx_count(p, C.uint32_t(n)) })
}

// AddEntry appends a camt.053 statement entry.
func (m *Message) AddEntry(units int64, fracDigits uint8, dir CreditDebit, status EntryStatus) error {
	return m.do(func(p *C.zxv_msg) C.int32_t {
		return C.zxv_msg_add_entry(p, C.int64_t(units), C.uint8_t(fracDigits), C.int32_t(dir), C.int32_t(status))
	})
}

// Render serializes to XML, validating mandatory fields and the currency.
func (m *Message) Render() (string, error) {
	if err := m.ctx.guard(); err != nil {
		return "", err
	}
	var s string
	err := m.h.use(func(p unsafe.Pointer) error {
		var e error
		s, e = readString(func(b *C.char, c C.size_t, n *C.size_t) C.int32_t {
			return C.zxv_msg_render((*C.zxv_msg)(p), b, c, n)
		})
		return e
	})
	return s, m.ctx.fatal(err)
}
