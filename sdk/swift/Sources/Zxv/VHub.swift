// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

import CZxv

/// ABI 1.5: a demonstration V-Bill hub for cross-currency clearing. Each
/// test currency has ONE exact peg against the V-Bill, set only by its
/// issuer with an ML-DSA-87 signature, and a swap from currency A to B goes
/// A -> V-Bill -> B as ONE balanced, all-or-nothing six-line posting on the
/// hub's own in-process ledger:
///
///     v = a * denA / numA,   b = v * numB / denB
///
/// Both divisions must be exact or the swap is refused (`.usage`,
/// `ZXV_E_RANGE`): nothing is rounded and there is no float rate anywhere.
/// `Hub.quote` gives the largest exact amount not above a requested one.
///
/// RULES: every rule of the sending AND receiving currency must allow a
/// swap, and a currency with no rule refuses every swap (fail closed,
/// `.refused` `ZXV_E_STATE`): add a limit to each currency first. Only the
/// built-in limit rule is exposed (`.limitRefused` when it refuses).
///
/// CAPABILITIES: creating a hub, adding currencies and rules, setting pegs,
/// opening and funding accounts and swapping need `.ledgerPost`; quotes,
/// balances, currency info and the check need `.ledgerRead`. The hub keeps
/// the capability it was created with, so give it both.
///
/// Honest limits: TEST CURRENCIES ONLY (ISO 4217 codes are refused) and no
/// real money: the ledger is in process memory, separate from the
/// context's, and is lost on close. No network: it is connected to no
/// settlement rail, central bank or issuer. Peg signatures use ML-DSA-87
/// (not FIPS 140-3 validated). V-Bills are likely securities in many
/// jurisdictions; real use needs legal review and a participating issuer.
/// A rule reports what it decided; it is not itself a legal check. An exact
/// peg removes rounding, not exchange-rate risk.
public enum VHub {
    /// Currency slots 0...7.
    public static let maxCurrencies = UInt32(ZXV_VHUB_MAX_CCY)
    /// Limit rules per currency.
    public static let maxRules = UInt32(ZXV_VHUB_MAX_RULES)
    /// Senders one limit tracks per epoch.
    public static let limitSenders = UInt32(ZXV_VHUB_LIMIT_SENDERS)
    /// Length of what an issuer signs.
    public static let pegMessageBytes = Int(ZXV_VHUB_PEG_MSG_BYTES)
    /// Longest test currency code.
    public static let codeMax = Int(ZXV_VHUB_CODE_MAX)
    /// The largest single amount (2^59).
    public static let amountMax: UInt64 = 1 << 59
    /// The V-Bill issuer's account.
    public static let vbillIssuer = UInt32(ZXV_VHUB_VBILL_ISSUER)

    /// The exact 40 bytes an issuer signs to set a peg:
    /// "zxv/vhub-peg/v1" || slot || num || den || epoch (each 8 bytes
    /// little-endian). A value type.
    public struct PegMessage: Sendable, Hashable {
        /// The 40 bytes.
        public let bytes: [UInt8]
    }

    /// The peg message for (slot, num, den, epoch). Sign it with
    /// `MlDsa87.sign` (empty context). Pure.
    public static func pegMessage(slot: UInt8, num: UInt64, den: UInt64, epoch: UInt64) throws(ZxvError) -> PegMessage {
        var m = [UInt8](repeating: 0, count: pegMessageBytes)
        try check(zxv_vhub_peg_msg(slot, num, den, epoch, &m))
        return PegMessage(bytes: m)
    }

    /// A currency's accounts and its current peg.
    public struct CurrencyInfo: Sendable, Equatable {
        /// The currency issuer's account.
        public let issuerAccount: UInt32
        /// The hub's pool of the currency.
        public let poolAccount: UInt32
        /// The currency's V-Bill float.
        public let vfloatAccount: UInt32
        /// `pegNum` minor units of the currency ...
        public let pegNum: UInt64
        /// ... per `pegDen` V-Bills (0/0: no peg yet).
        public let pegDen: UInt64
        /// The peg's epoch (0: no peg yet).
        public let pegEpoch: UInt64
        /// True once a peg was set.
        public var hasPeg: Bool { pegDen != 0 }
    }

    /// One account's rails: what it holds and owes. equity = debit - credit.
    public struct Balance: Sendable, Equatable {
        /// Held (A).
        public let debit: UInt64
        /// Owed (L; issuers only).
        public let credit: UInt64
        /// debit - credit (E).
        public let equity: Int64
    }

    /// A quote: the largest exact amount not above the request, and what it
    /// would carry.
    public struct Quote: Sendable, Equatable {
        /// The exact amount of the sending currency (0 if none).
        public let amount: UInt64
        /// V-Bills it would move.
        public let vbills: UInt64
        /// Units of the receiving currency.
        public let received: UInt64
    }

    /// What a swap moved.
    public struct Swap: Sendable, Equatable {
        /// V-Bills moved.
        public let vbills: UInt64
        /// Units the receiver got.
        public let received: UInt64
    }

    /// One demo hub and its ledger. It belongs to the capability's context
    /// and fails closed with it (`.fatal`). Internally serialized: any
    /// thread may call it. `close()` releases it (and its ledger) at once.
    public final class Hub: @unchecked Sendable {
        let cap: Capability
        let cell: HandleCell

        /// A hub whose ledger starts with the V-Bill asset ("VBL") and the
        /// V-Bill issuer's account (`vbillIssuer`). `seed` (32 bytes; nil for
        /// a fresh random one) derives every posting's idempotency key and
        /// UETR. Needs `.ledgerPost`.
        public init(_ cap: Capability, seed: [UInt8]? = nil) throws(ZxvError) {
            if let seed { try needSize(seed.count, 32, "hub seed") }
            self.cap = cap
            var h: OpaquePointer?
            try cap.call { zxv_vhub_create($0, seed, &h) }
            cell = HandleCell(h!, "VHub.Hub") { zxv_vhub_destroy($0) }
        }

        private func run(_ body: (OpaquePointer, OpaquePointer) -> Int32) throws(ZxvError) {
            let h = try cell.acquire()
            defer { cell.release() }
            try cap.call { body($0, h) }
        }

        /// Register test currency `slot` (0...7): `code` (3...12 of A-Z 0-9;
        /// not an ISO 4217 code, "VBL" or "VFV"), its minor units (0...4) and
        /// its issuer's ML-DSA-87 public key (2592 bytes; it alone may set the
        /// peg). Opens the issuer, pool and V-Bill float accounts and funds the
        /// pool with `poolFunding` and the float with `vbillBacking` V-Bills.
        /// The peg starts unset. `.usage` `ZXV_E_CURRENCY` (an ISO code),
        /// `ZXV_E_DUPLICATE` (slot or code taken), `ZXV_E_FORMAT`, `ZXV_E_RANGE`.
        public func addCurrency(slot: UInt8, code: String, minorUnits: UInt8, issuerPublicKey: [UInt8],
                                poolFunding: UInt64 = 0, vbillBacking: UInt64 = 0) throws(ZxvError) {
            try needSize(issuerPublicKey.count, Int(ZXV_MLDSA87_PK_BYTES), "issuer public key")
            let c = try cText(code, "currency code")
            try run { zxv_vhub_add_ccy($0, $1, slot, c, minorUnits, issuerPublicKey, poolFunding, vbillBacking) }
        }

        /// A currency's accounts and peg (`.ledgerRead`). `.usage`
        /// (`ZXV_E_NOT_FOUND`) for an unregistered slot.
        public func currency(_ slot: UInt8) throws(ZxvError) -> CurrencyInfo {
            var ia: UInt32 = 0, pa: UInt32 = 0, va: UInt32 = 0, n: UInt64 = 0, d: UInt64 = 0, e: UInt64 = 0
            try run { zxv_vhub_ccy_info($0, $1, slot, &ia, &pa, &va, &n, &d, &e) }
            return CurrencyInfo(issuerAccount: ia, poolAccount: pa, vfloatAccount: va, pegNum: n, pegDen: d, pegEpoch: e)
        }

        /// Set a currency's peg to num/den at `epoch` (above the current one)
        /// given the issuer's ML-DSA-87 signature (4627 bytes, empty context)
        /// over `pegMessage(slot:num:den:epoch:)`. `.refused` `.pegRefused`
        /// (a stale or replayed epoch), `.verificationFailed` (not the
        /// issuer's signature over these numbers); `.usage` `ZXV_E_RANGE`
        /// (num or den 0). A refused peg changes nothing.
        public func setPeg(slot: UInt8, num: UInt64, den: UInt64, epoch: UInt64, signature: [UInt8]) throws(ZxvError) {
            try needSize(signature.count, Int(ZXV_MLDSA87_SIG_BYTES), "peg signature")
            try run { zxv_vhub_set_peg($0, $1, slot, num, den, epoch, signature) }
        }

        /// Open a holder account in currency `slot` for `owner` (an id of the
        /// caller's choosing); returns the account number. `.refused`
        /// (`ZXV_E_CAPACITY`) when the ledger holds 512 accounts.
        public func openAccount(slot: UInt8, owner: UInt32) throws(ZxvError) -> UInt32 {
            var a: UInt32 = 0
            try run { zxv_vhub_open_account($0, $1, slot, owner, &a) }
            return a
        }

        /// Test faucet: issue `amount` to a holder account or a pool (by the
        /// currency's issuer), or V-Bills to a V-Bill float (by the V-Bill
        /// issuer). `.usage` `ZXV_E_RANGE` (0, above `amountMax`, or a balance
        /// reaching 2^62).
        public func issue(account: UInt32, amount: UInt64) throws(ZxvError) {
            try run { zxv_vhub_issue($0, $1, account, amount) }
        }

        /// One account's rails (`.ledgerRead`).
        public func balance(_ account: UInt32) throws(ZxvError) -> Balance {
            var d: UInt64 = 0, c: UInt64 = 0, e: Int64 = 0
            try run { zxv_vhub_balance($0, $1, account, &d, &c, &e) }
            return Balance(debit: d, credit: c, equity: e)
        }

        /// Add the built-in limit rule to currency `slot`: at most `limit`
        /// minor units out per sender per rule epoch (16 distinct senders per
        /// epoch; a 17th is refused). `.refused` (`ZXV_E_CAPACITY`) after 4 rules.
        public func addLimit(slot: UInt8, limit: UInt64) throws(ZxvError) {
            try run { zxv_vhub_add_limit($0, $1, slot, limit) }
        }

        /// Start the next rule epoch (every limit restarts every sender at
        /// zero); returns the new epoch.
        public func nextEpoch() throws(ZxvError) -> UInt64 {
            var e: UInt64 = 0
            try run { zxv_vhub_next_epoch($0, $1, &e) }
            return e
        }

        /// The largest exact amount of `from` not above `want`, and what it
        /// would carry (`.ledgerRead`). Runs no rules and checks no balances,
        /// so a quoted swap may still be refused. `.refused` `.pegRefused`
        /// (either peg unset), `.usage` (from == to).
        public func quote(from: UInt8, to: UInt8, want: UInt64) throws(ZxvError) -> Quote {
            var a: UInt64 = 0, v: UInt64 = 0, r: UInt64 = 0
            try run { zxv_vhub_quote($0, $1, from, to, want, &a, &v, &r) }
            return Quote(amount: a, vbills: v, received: r)
        }

        /// Swap `amount` minor units of `from` from holder account `sender` to
        /// holder account `receiver` in `to`, as one posting at `tick` (a
        /// caller-supplied time, never the wall clock). `.usage`
        /// `ZXV_E_RANGE` (not exact under the pegs) or `ZXV_E_CURRENCY` (an
        /// account of another currency); `.refused` `.pegRefused`,
        /// `ZXV_E_STATE` (a currency has no rule), `.limitRefused`,
        /// `.insufficientFunds`. A refused swap changes nothing.
        public func swap(from: UInt8, to: UInt8, sender: UInt32, receiver: UInt32,
                         amount: UInt64, tick: UInt64) throws(ZxvError) -> Swap {
            var v: UInt64 = 0, r: UInt64 = 0
            try run { zxv_vhub_swap($0, $1, from, to, sender, receiver, amount, tick, &v, &r) }
            return Swap(vbills: v, received: r)
        }

        /// Verify the whole hub ledger (A = L + E per account, no negative
        /// rail, DEBIT == CREDIT per asset, the posting hash chain) and return
        /// the number of postings so far. A failure is `.fatal`
        /// (`ZXV_E_INVARIANT`) and poisons the context.
        public func check() throws(ZxvError) -> UInt64 {
            var n: UInt64 = 0
            try run { zxv_vhub_check($0, $1, &n) }
            return n
        }

        /// Release the hub and its ledger now. Later calls throw `.usage`
        /// (`ZXV_E_ARG`).
        public func close() { cell.close() }
    }
}
