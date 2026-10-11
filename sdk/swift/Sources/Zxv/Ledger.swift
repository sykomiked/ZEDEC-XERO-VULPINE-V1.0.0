// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

import CZxv

/// Kind of ledger account.
public enum AccountKind: Int32, Sendable {
    /// May never go below zero.
    case holder = 0
    /// May carry a CREDIT (777) balance: issuance or nostro.
    case issuer = 1
}

/// Rail balances in exact minor units.
public struct Balance: Sendable, Equatable {
    /// 555 NCR: sum received.
    public let debit: Int64
    /// 777 NRE: sum sent.
    public let credit: Int64
    /// 888 PNS: debit - credit, the spendable position.
    public let equity: Int64
}

/// One journal line.
public struct JournalLine: Sendable, Equatable {
    /// Platform entry id (shared by both sides).
    public let entryId: UInt64
    /// + received / - sent, minor units.
    public let amount: Int64
    /// The other account.
    public let counterparty: UInt32
    /// The unique reference.
    public let reference: String
}

/// The triple ledger, used through a capability. Amounts are exact `Int64`
/// minor units. Posting and opening accounts need `.ledgerPost`; balances,
/// account metadata and journals need `.ledgerRead`. Every call is the
/// capability-gated `*_cap` C form.
public final class Ledger: @unchecked Sendable {
    private let cap: Capability

    /// A ledger acting with `cap`.
    public init(_ cap: Capability) { self.cap = cap }

    /// Open an account in an enabled currency. `name` <= 63 bytes.
    public func openAccount(_ name: String, currency: String, kind: AccountKind = .holder) throws(ZxvError) -> UInt32 {
        var id: UInt32 = 0
        try cap.call { zxv_ledger_open_account_cap($0, name, currency, kind.rawValue, &id) }
        return id
    }

    /// Post a balanced transfer (amount > 0) with a unique reference (<= 35
    /// bytes). Returns the entry id. A holder going negative is refused
    /// (`.insufficientFunds`).
    @discardableResult
    public func post(from: UInt32, to: UInt32, amount: Int64, currency: String, reference: String) throws(ZxvError) -> UInt64 {
        var e: UInt64 = 0
        try cap.call { zxv_ledger_post_cap($0, from, to, amount, currency, reference, &e) }
        return e
    }

    /// Rail balances of an account.
    public func balance(_ account: UInt32) throws(ZxvError) -> Balance {
        var d: Int64 = 0, c: Int64 = 0, q: Int64 = 0
        try cap.call { zxv_ledger_balance_cap($0, account, &d, &c, &q) }
        return Balance(debit: d, credit: c, equity: q)
    }

    /// Verify every invariant. A failure halts the context (`.fatal`).
    public func check() throws(ZxvError) { try cap.call { zxv_ledger_check_cap($0) } }

    /// Journal of an account, oldest first.
    public func journal(_ account: UInt32) throws(ZxvError) -> [JournalLine] {
        var n: UInt32 = 0
        try cap.call { zxv_ledger_entry_count_cap($0, account, &n) }
        let c = cap.handle
        var out: [JournalLine] = []
        for i in 0..<n {
            var id: UInt64 = 0, amt: Int64 = 0, cp: UInt32 = 0
            let ref = try readText(cap.context) { zxv_ledger_entry_at_cap(c, account, i, &id, &amt, &cp, $0, $1, $2) }
            out.append(JournalLine(entryId: id, amount: amt, counterparty: cp, reference: ref))
        }
        return out
    }

    /// Name of an account.
    public func accountName(_ account: UInt32) throws(ZxvError) -> String {
        try cap.context.ensureAlive()
        let c = cap.handle
        return try readText(cap.context) { zxv_ledger_account_name_cap(c, account, $0, $1, $2) }
    }

    /// Currency of an account.
    public func accountCurrency(_ account: UInt32) throws(ZxvError) -> String {
        try cap.context.ensureAlive()
        let c = cap.handle
        return try readText(cap.context) { zxv_ledger_account_ccy_cap(c, account, $0, $1, $2) }
    }

    /// Kind of an account.
    public func accountKind(_ account: UInt32) throws(ZxvError) -> AccountKind {
        var k: Int32 = 0
        try cap.call { zxv_ledger_account_kind_cap($0, account, &k) }
        return AccountKind(rawValue: k) ?? .holder
    }

    /// Number of accounts in the context.
    public func accountCount() throws(ZxvError) -> UInt32 {
        var n: UInt32 = 0
        try cap.call { zxv_ledger_account_count_cap($0, &n) }
        return n
    }
}

/// One ISO 4217 entry.
public struct CurrencyInfo: Sendable, Equatable {
    /// "NGN".
    public let alpha: String
    /// 566.
    public let numeric: UInt16
    /// 2; 255 = not applicable.
    public let minorUnits: UInt8
    /// `ZXV_CCYF_*` flags.
    public let flags: UInt32
    /// "Naira".
    public let name: String

    /// Look up by alpha code. Unknown codes throw `ZXV_E_NOT_FOUND`.
    public init(alpha: String) throws(ZxvError) {
        var num: UInt16 = 0, mu: UInt8 = 0, fl: UInt32 = 0
        try Zxv.check(zxv_iso4217_by_alpha(alpha, &num, &mu, &fl))
        self.alpha = alpha
        numeric = num
        minorUnits = mu
        flags = fl
        name = try readText { zxv_iso4217_name(alpha, $0, $1, $2) }
    }
}

/// Card networks for check-digit validation.
public enum CardNetwork: Int32, Sendable {
    /// Damm check digit.
    case phoenix = 1
    /// Luhn check digit.
    case dragon = 2
    /// Verhoeff check digit.
    case thunderbird = 3

    /// True if the decimal PAN's check digit is valid for this network.
    public func isValid(pan: String) throws(ZxvError) -> Bool {
        var ok: Int32 = 0
        try Zxv.check(zxv_card_check_digit(rawValue, pan, &ok))
        return ok != 0
    }
}

/// ISO 20022 message kinds (pacs.008 and camt.053 are live).
public enum MxKind: Int32, Sendable {
    case pacs008 = 1, camt053 = 2, pacs002 = 3, pacs004 = 4, pacs009 = 5, camt056 = 6, camt029 = 7
}

/// Text fields of a message.
public enum MxField: Int32, Sendable {
    case msgId = 1, creDtTm = 2, debtorName = 3, debtorAccount = 4, creditorName = 5
    case creditorAccount = 6, endToEndId = 7, currency = 8, accountId = 9
}

/// Amount fields of a message.
public enum MxAmount: Int32, Sendable {
    /// pacs008 IntrBkSttlmAmt.
    case settlement = 1
    /// camt053 OPBD.
    case opening = 2
    /// camt053 CLBD.
    case closing = 3
}

/// One ISO 20022 message under construction. Not thread-safe (not Sendable).
/// Made through a capability (`.ledgerRead`), which the library re-checks on
/// every call and which the message keeps alive.
public final class Message {
    private let context: ZxvContext
    private let capability: Capability? // the cap a *_cap message re-checks; outlives handle
    private let handle: OpaquePointer

    /// Create a message through `capability` (`.ledgerRead`). Pending kinds
    /// throw `.refused` (notImplemented).
    public init(_ capability: Capability, kind: MxKind) throws(ZxvError) {
        context = capability.context
        self.capability = capability
        var m: OpaquePointer?
        try capability.call { zxv_msg_create_cap($0, kind.rawValue, &m) }
        handle = m!
    }

    /// Create a message. Pending kinds throw `.refused` (notImplemented).
    @available(*, deprecated, message: "ABI 1.2: create messages through a .ledgerRead capability: Message(_:kind:)")
    public init(_ context: ZxvContext, kind: MxKind) throws(ZxvError) {
        self.context = context
        capability = nil
        var m: OpaquePointer?
        try context.call { zxv_msg_create(context.handle, kind.rawValue, &m) }
        handle = m!
    }
    deinit { zxv_msg_destroy(handle) }

    /// Set a text field (plain UTF-8; the serializer escapes XML).
    @discardableResult
    public func set(_ field: MxField, _ text: String) throws(ZxvError) -> Message {
        try context.ensureAlive()
        try check(zxv_msg_set_text(handle, field.rawValue, text), context)
        return self
    }
    /// Set an amount: exact units with `fractionDigits` decimal places.
    @discardableResult
    public func setAmount(_ field: MxAmount, units: Int64, fractionDigits: UInt8) throws(ZxvError) -> Message {
        try context.ensureAlive()
        try check(zxv_msg_set_amount(handle, field.rawValue, units, fractionDigits), context)
        return self
    }
    /// Serialize to XML.
    public func render() throws(ZxvError) -> String {
        try context.ensureAlive()
        return try readText(context) { zxv_msg_render(handle, $0, $1, $2) }
    }
}
