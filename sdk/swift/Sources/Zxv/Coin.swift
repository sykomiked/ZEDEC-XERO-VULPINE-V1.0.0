// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

import CZxv

/// ABI 1.5: the VFV coin ladder (Vino). A coin's face is a rung of the
/// Fibonacci ladder: rung r (1...`rungs`) has face F(r+1), so the faces are
/// 1, 2, 3, 5, 8, 13, ... up to `topFace` (rung 45). Rung 0 is the
/// ceremonial V0 note, face 0. Any other face (4, 6, 100, ...) is not a
/// coin (`.usage`, `ZXV_E_DENOM`); pay an arbitrary amount with `split`.
/// Pure and integer-only.
///
/// Honest limit: minting coins is not in this ABI yet (the kernel's mint
/// needs a coin-store handle bound to a ledger and an integer mint path);
/// `.mint` is used only by `SwarmToken.Wallet`.
public enum Coin {
    /// Rungs 1...45 carry value.
    public static let rungs = UInt32(ZXV_COIN_RUNGS)
    /// The face of rung 45, F(46) = 1,836,311,903.
    public static let topFace: UInt64 = 1_836_311_903

    /// The face of ladder rung `rung` (0...`rungs`; rung 0 is the V0 note,
    /// face 0). `.usage` (`ZXV_E_RANGE`) above 45.
    public static func face(rung: UInt32) throws(ZxvError) -> UInt64 {
        var f: UInt64 = 0
        try check(zxv_coin_face(rung, &f))
        return f
    }

    /// The rung whose face is exactly `face` (face 1 is rung 1). `.usage`
    /// (`ZXV_E_DENOM`, `isDenomination`) for 0 and any face not on the
    /// ladder, including Fibonacci numbers above `topFace`.
    public static func rung(face: UInt64) throws(ZxvError) -> UInt32 {
        var r: UInt32 = 0
        try check(zxv_coin_rung(face, &r))
        return r
    }

    /// How many coins `split(amount)` returns, without allocating them.
    /// `.usage` (`ZXV_E_RANGE`) when the split would need more than
    /// 4,294,967,294 coins (amounts of about 7.9e18 and up).
    public static func splitCount(_ amount: UInt64) throws(ZxvError) -> Int {
        var n = 0
        let st = zxv_coin_split(amount, nil, 0, &n)
        if st != ZXV_E_BUFFER { try check(st) }
        return n
    }

    /// The coins for a whole `amount` as rungs, largest first: its
    /// Zeckendorf split (floor(amount / topFace) top coins first, then each
    /// lower rung at most once and never two neighbours), the unique
    /// fewest-coins form. 0 is an empty split. Check `splitCount` first for
    /// very large amounts: each coin takes 4 bytes here.
    public static func split(_ amount: UInt64) throws(ZxvError) -> [UInt32] {
        var v = [UInt32](repeating: 0, count: try splitCount(amount))
        var n = 0
        let st = v.withUnsafeMutableBufferPointer { b in
            zxv_coin_split(amount, b.count == 0 ? nil : b.baseAddress, b.count, &n)
        }
        try check(st)
        return Array(v.prefix(n))
    }
}

/// ABI 1.5: swarm AI token tiers, octane and Fibonacci codes. Tier k
/// (0..<`tiers`) has face F(k+2): 1, 2, 3, 5, 8, ... F(65); tiers
/// `superTier` (face 144) and up are super tokens. A holding is `Counts`
/// (tokens held per tier); its value is the sum of count * face. OCTANE is
/// the densest tier a holding may use (`Octane.plain` ... `Octane.max`,
/// named like a fuel pump's grades; any 0...63 is allowed). The canonical
/// form of a value at octane o puts floor(value / face(o)) tokens on tier o
/// and the rest in Zeckendorf form below it: one canonical form per value
/// per octane.
///
/// NO PREMIUM: a dense token is worth exactly its face in plain tokens.
/// Density earns no interest, costs no fee and buys no allotment or
/// priority; changing octane never changes the value. It buys only fewer
/// pieces (1,000,000 tokens are 17 pieces at `Octane.racing`), which helps
/// wherever tokens are handled one piece at a time. It does not make a
/// model faster or an amount shorter on the wire.
///
/// The free functions are pure. A value that would pass 64 bits throws
/// `.usage` (`ZXV_E_RANGE`) and changes nothing.
public enum SwarmToken {
    /// 64 tiers.
    public static let tiers = UInt32(ZXV_TOKEN_TIERS)
    /// The first super-token tier (face 144).
    public static let superTier = UInt32(ZXV_TOKEN_SUPER_TIER)
    /// The longest Fibonacci code (93 bits).
    public static let codeMaxBits = UInt32(ZXV_TOKEN_CODE_MAX_BITS)
    /// The largest code buffer.
    public static let codeMaxBytes = Int(ZXV_TOKEN_CODE_MAX_BYTES)

    /// The named octane grades.
    public enum Octane {
        /// Face 1 only.
        public static let plain = UInt32(ZXV_OCTANE_PLAIN)
        /// Up to face 8.
        public static let regular = UInt32(ZXV_OCTANE_REGULAR)
        /// Up to face 89.
        public static let midgrade = UInt32(ZXV_OCTANE_MIDGRADE)
        /// Up to face 987.
        public static let premium = UInt32(ZXV_OCTANE_PREMIUM)
        /// Up to face 75025.
        public static let racing = UInt32(ZXV_OCTANE_RACING)
        /// Up to face F(65).
        public static let max = UInt32(ZXV_OCTANE_MAX)
    }

    /// A holding: tokens held per tier (64 counts; index = tier).
    public struct Counts: Sendable, Hashable {
        /// The 64 counts.
        public internal(set) var tiers: [UInt64]
        /// An empty holding.
        public init() { tiers = [UInt64](repeating: 0, count: Int(ZXV_TOKEN_TIERS)) }
        /// A holding from 64 counts; `.usage` (`ZXV_E_ARG`) for another length.
        public init(_ counts: [UInt64]) throws(ZxvError) {
            try needSize(counts.count, Int(ZXV_TOKEN_TIERS), "token counts")
            tiers = counts
        }
        /// Tokens held on `tier` (0..<64).
        public subscript(tier: Int) -> UInt64 {
            get { tiers[tier] }
            set { tiers[tier] = newValue }
        }
    }

    /// The face of a tier and whether it is a super token.
    public struct TierFace: Sendable, Equatable {
        /// F(tier + 2).
        public let face: UInt64
        /// tier >= `superTier`.
        public let isSuper: Bool
    }

    /// The value of a holding and its number of pieces.
    public struct Value: Sendable, Equatable {
        /// The sum of count * face.
        public let value: UInt64
        /// The sum of counts.
        public let pieces: UInt64
    }

    /// The face of `tier`. `.usage` (`ZXV_E_RANGE`) for tier >= 64.
    public static func face(tier: UInt32) throws(ZxvError) -> TierFace {
        var f: UInt64 = 0, s: Int32 = 0
        try check(zxv_token_face(tier, &f, &s))
        return TierFace(face: f, isSuper: s != 0)
    }

    /// The tier whose face is exactly `face`. `.usage` (`ZXV_E_DENOM`) if it
    /// is not one of the 64 faces.
    public static func tier(face: UInt64) throws(ZxvError) -> UInt32 {
        var t: UInt32 = 0
        try check(zxv_token_tier(face, &t))
        return t
    }

    /// The value and piece count of a holding. `.usage` (`ZXV_E_RANGE`) when
    /// the value passes 64 bits.
    public static func value(_ counts: Counts) throws(ZxvError) -> Value {
        var v: UInt64 = 0, p: UInt64 = 0
        try check(zxv_token_value(counts.tiers, &v, &p))
        return Value(value: v, pieces: p)
    }

    /// The canonical form of `value` at `octane`. `.usage` (`ZXV_E_RANGE`)
    /// for octane >= 64.
    public static func canonical(value: UInt64, octane: UInt32) throws(ZxvError) -> Counts {
        var c = Counts()
        try check(zxv_token_canonical(value, octane, &c.tiers))
        return c
    }

    /// True if `counts` is the canonical form of its value at `octane`
    /// (false also when a tier above the octane is used).
    public static func isCanonical(_ counts: Counts, octane: UInt32) throws(ZxvError) -> Bool {
        var r: Int32 = 0
        try check(zxv_token_is_canonical(counts.tiers, octane, &r))
        return r != 0
    }

    /// Rewrite `counts` into the canonical form of its value at `octane`;
    /// the value never changes. Unchanged on any failure.
    public static func compress(_ counts: inout Counts, octane: UInt32) throws(ZxvError) {
        try check(zxv_token_compress(&counts.tiers, octane))
    }

    /// Fuse: one tier-k and one tier-(k+1) token become one tier-(k+2)
    /// token (F(k+2) + F(k+3) = F(k+4)). `.refused` (`.insufficientFunds`:
    /// no tier-k or tier-(k+1) token; `ZXV_E_STATE`: tier k+2 is denser
    /// than `octane` allows), `.usage` (`ZXV_E_RANGE`: tier > 61, octane >=
    /// 64, or a full count). Unchanged on any failure.
    public static func fuse(_ counts: inout Counts, octane: UInt32, tier: UInt32) throws(ZxvError) {
        try check(zxv_token_fuse(&counts.tiers, octane, tier))
    }

    /// Split: one tier-k token becomes one tier-(k-1) and one tier-(k-2)
    /// token (tier 1 becomes two plain tokens; tier 0 is atomic: `.usage`
    /// `ZXV_E_RANGE`). `.refused` (`.insufficientFunds`) when there is no
    /// tier-k token. Needs no octane: it only makes a holding plainer.
    public static func split(_ counts: inout Counts, tier: UInt32) throws(ZxvError) {
        try check(zxv_token_split(&counts.tiers, tier))
    }

    /// Number of bits in the Fibonacci code of `value` (2...93). `.usage`
    /// (`ZXV_E_RANGE`) for UInt64.max, which has no code.
    public static func codeBits(_ value: UInt64) throws(ZxvError) -> UInt32 {
        var b: UInt32 = 0
        try check(zxv_token_code_bits(value, &b))
        return b
    }

    /// Write the Fibonacci code of `value` (Zeckendorf bits of value+1,
    /// lowest first, then a closing 1, so every code ends in "11" and needs
    /// no length field) at bit `bit` of `buffer` (bit i is bit i%8 of byte
    /// i/8); `bit` advances past it. Only the code's bits are written.
    /// `.usage` (`ZXV_E_BUFFER`) when it does not fit: nothing is written.
    public static func codePut(_ buffer: inout [UInt8], bit: inout UInt32, value: UInt64) throws(ZxvError) {
        var b = bit
        let st = buffer.withUnsafeMutableBufferPointer { p in
            zxv_token_code_put(p.count == 0 ? nil : p.baseAddress, p.count, &b, value)
        }
        try check(st)
        bit = b
    }

    /// Read one Fibonacci code from bit `bit` of `buffer`; `bit` advances
    /// past it. `.usage` `ZXV_E_FORMAT` (no closing "11", or a value past
    /// 64 bits), `ZXV_E_RANGE` (`bit` past the end). Nothing changes on
    /// failure.
    public static func codeGet(_ buffer: [UInt8], bit: inout UInt32) throws(ZxvError) -> UInt64 {
        var b = bit, v: UInt64 = 0
        try check(zxv_token_code_get(buffer, buffer.count, &b, &v))
        bit = b
        return v
    }

    /// What `Wallet.info` reports.
    public struct WalletInfo: Sendable, Equatable {
        /// The wallet's label.
        public let modelId: UInt32
        /// Its octane.
        public let octane: UInt32
        /// What it holds.
        public let value: UInt64
        /// In how many tokens.
        public let pieces: UInt64
    }

    /// A token wallet: a holding kept canonical at its own octane. Every
    /// change needs `.mint` (tokens enter a wallet only by `mint`; it is not
    /// tied to a Swarm budget, whose authority would not cover creating
    /// tokens); reads need no capability. It belongs to the capability's
    /// context and fails closed with it (`.fatal`). Internally serialized:
    /// any thread may call it. `close()` releases it at once.
    public final class Wallet: @unchecked Sendable {
        let cap: Capability
        let cell: HandleCell

        /// An empty wallet at `octane`, labelled `modelId` (a free label,
        /// e.g. the swarm model that holds it). Needs `.mint`.
        public init(_ cap: Capability, modelId: UInt32, octane: UInt32) throws(ZxvError) {
            self.cap = cap
            var w: OpaquePointer?
            try cap.call { zxv_token_wallet_create($0, modelId, octane, &w) }
            cell = HandleCell(w!, "SwarmToken.Wallet") { zxv_token_wallet_destroy($0) }
        }

        private func gated(_ body: (OpaquePointer, OpaquePointer) -> Int32) throws(ZxvError) {
            let w = try cell.acquire()
            defer { cell.release() }
            try cap.call { body($0, w) }
        }

        /// Create `amount` new tokens in the wallet. `.usage` (`ZXV_E_RANGE`)
        /// when the value would pass 64 bits; nothing changes.
        public func mint(_ amount: UInt64) throws(ZxvError) {
            try gated { zxv_token_wallet_mint($0, $1, amount) }
        }

        /// Spend (burn) `amount`; the change stays canonical. `.refused`
        /// (`.insufficientFunds`) when the wallet holds less (nothing changes).
        public func spend(_ amount: UInt64) throws(ZxvError) {
            try gated { zxv_token_wallet_spend($0, $1, amount) }
        }

        /// Move `amount` to `other`: this wallet keeps its change at its
        /// octane, `other` receives at ITS octane, and the total is conserved
        /// exactly. Both wallets must belong to this wallet's context (else
        /// `.capabilityDenied`). Transferring to itself only checks the
        /// funds. Neither wallet changes on failure.
        public func transfer(to other: Wallet, amount: UInt64) throws(ZxvError) {
            let dst = try other.cell.acquire()
            defer { other.cell.release() }
            try gated { zxv_token_wallet_transfer($0, $1, dst, amount) }
        }

        /// Change the octane (up = denser, down = plainer); the same value is
        /// re-expressed canonically.
        public func setOctane(_ octane: UInt32) throws(ZxvError) {
            try gated { zxv_token_wallet_set_octane($0, $1, octane) }
        }

        /// Label, octane, value and piece count.
        public func info() throws(ZxvError) -> WalletInfo {
            let w = try cell.acquire()
            defer { cell.release() }
            var m: UInt32 = 0, o: UInt32 = 0, v: UInt64 = 0, p: UInt64 = 0
            try cap.read { zxv_token_wallet_info(w, &m, &o, &v, &p) }
            return WalletInfo(modelId: m, octane: o, value: v, pieces: p)
        }

        /// The counts per tier.
        public func counts() throws(ZxvError) -> Counts {
            let w = try cell.acquire()
            defer { cell.release() }
            var c = Counts()
            try cap.read { zxv_token_wallet_counts(w, &c.tiers) }
            return c
        }

        /// Release the wallet now. Later calls throw `.usage` (`ZXV_E_ARG`).
        public func close() { cell.close() }
    }
}

/// ABI 1.5: the 0.08889% assurance fee, exact and integer-only:
///
///     fee(g) = floor(g * 8889 / 10,000,000)
///
/// g is in the unit's own minor measure (money minor units, compute
/// token-cycles, bandwidth bytes, storage byte-hours). It is formed in 128
/// bits, so every g up to UInt64.max is exact; fee(1124) = 0, fee(1125) = 1.
/// The fee is charged ONCE per transaction, on its gross: never on a fee,
/// never compounded. A `Carry` keeps an account's sub-unit remainder so that
/// many small charges pay exactly what one charge of their sum pays.
///
/// A fee is split 50% reserve floor (backs Vino), 25% V-Bill dividend pool,
/// 15% infrastructure / node bounties, 10% regenerative capital; buckets
/// 1-3 get floor(fee * pct / 100) and the reserve floor gets every
/// remainder unit, so the four parts always sum to the fee exactly.
///
/// Honest limits: these are the arithmetic rules only. How a fee, a reserve
/// or a dividend pool is treated for tax, securities or consumer law is a
/// question for counsel; nothing here is a licence or a certification.
public enum Fee {
    /// 8889.
    public static let num = UInt64(ZXV_FEE_NUM)
    /// 10^7; a carry is always below it.
    public static let den = UInt64(ZXV_FEE_DEN)

    /// The four buckets a fee is split into (index into `split`).
    public enum Bucket: Int32, Sendable, CaseIterable, CustomStringConvertible {
        /// 50% plus every remainder unit; backs Vino.
        case reserveFloor = 0
        /// 25%.
        case vbillDividend = 1
        /// 15%: infrastructure and node bounties.
        case infraBounty = 2
        /// 10%: regenerative capital.
        case regenCapital = 3

        /// "reserve_floor", "vbill_dividend", "infra_bounty", "regen_capital".
        public var description: String {
            switch self {
            case .reserveFloor: return "reserve_floor"
            case .vbillDividend: return "vbill_dividend"
            case .infraBounty: return "infra_bounty"
            case .regenCapital: return "regen_capital"
            }
        }
    }

    /// The kinds of unit a fee may be charged in (index into `Policy.rates`).
    public enum Unit: Int32, Sendable, CaseIterable {
        /// Minor units of a ledger asset.
        case money = 0
        /// Token-cycles.
        case compute = 1
        /// Bytes.
        case bandwidth = 2
        /// Byte-hours.
        case storage = 3
    }

    /// An exact non-negative ratio num/den (same terms compare equal; not
    /// cross-multiplied).
    public struct Ratio: Sendable, Equatable {
        /// Numerator.
        public let num: UInt64
        /// Denominator.
        public let den: UInt64
        /// Build a ratio.
        public init(num: UInt64, den: UInt64) {
            self.num = num
            self.den = den
        }
    }

    /// A fee and the remainder to carry to the account's next charge.
    public struct Charge: Sendable, Equatable {
        /// What this charge pays.
        public let fee: UInt64
        /// Store it for the next charge (< `den`).
        public let remainder: UInt64
    }

    /// fee(gross) with no carry.
    public static func assure(_ gross: UInt64) throws(ZxvError) -> UInt64 {
        var f: UInt64 = 0
        try check(zxv_fee_assure(gross, &f))
        return f
    }

    /// The fee on `gross` with the account's incoming remainder:
    /// floor((gross * 8889 + r) / 10^7), and the new remainder. `.usage`
    /// (`ZXV_E_RANGE`) for a remainder >= `den` (a corrupt carry).
    public static func assureCarry(remainder: UInt64, gross: UInt64) throws(ZxvError) -> Charge {
        var f: UInt64 = 0, r: UInt64 = 0
        try check(zxv_fee_assure_carry(remainder, gross, &f, &r))
        return Charge(fee: f, remainder: r)
    }

    /// One account's fee carry: `charge` applies `assureCarry` and keeps the
    /// remainder. Starting from 0, many small charges pay exactly what one
    /// charge of their sum pays. A plain value.
    public struct Carry: Sendable, Equatable {
        /// The remainder to store for this account.
        public private(set) var remainder: UInt64
        /// Start from a stored remainder (0 for a new account). The first
        /// charge throws `.usage` (`ZXV_E_RANGE`) if it is not below `den`.
        public init(remainder: UInt64 = 0) { self.remainder = remainder }
        /// The fee on `gross`; the remainder is kept for the next charge
        /// (unchanged on failure).
        public mutating func charge(_ gross: UInt64) throws(ZxvError) -> UInt64 {
            let c = try Fee.assureCarry(remainder: remainder, gross: gross)
            remainder = c.remainder
            return c.fee
        }
    }

    /// Split `fee` into its four buckets, indexed by `Bucket.rawValue`
    /// (summing to `fee` exactly).
    public static func split(_ fee: UInt64) throws(ZxvError) -> [UInt64] {
        var s = [UInt64](repeating: 0, count: Int(ZXV_FEE_BUCKETS))
        try check(zxv_fee_split(fee, &s))
        return s
    }

    /// The default fee policy.
    public struct Policy: Sendable, Equatable {
        /// Bucket shares: 50, 25, 15, 10.
        public let percent: [UInt32]
        /// False: a contribution below the fee is raised to it.
        public let allowBelow: Bool
        /// The posted VFV store-credit rate per `Unit` for a voluntary
        /// contribution above the fee: money 1/1, the others 0/1 (unset: the
        /// excess earns no credit until a rate is posted).
        public let rates: [Ratio]
        /// The operator's credit multiplier, 1/1.
        public let multiplier: Ratio
        /// The no-monopoly cap on one recipient's share of the non-money
        /// commons pool, 8/21.
        public let commonsCap: Ratio
    }

    /// The default policy.
    public static func defaultPolicy() throws(ZxvError) -> Policy {
        var pct = [UInt32](repeating: 0, count: 4), below: Int32 = 0
        var rn = [UInt64](repeating: 0, count: 4), rd = rn
        var mn: UInt64 = 0, md: UInt64 = 0, cn: UInt64 = 0, cd: UInt64 = 0
        try check(zxv_fee_policy_default(&pct, &below, &rn, &rd, &mn, &md, &cn, &cd))
        return Policy(percent: pct, allowBelow: below != 0,
                      rates: (0..<4).map { Ratio(num: rn[$0], den: rd[$0]) },
                      multiplier: Ratio(num: mn, den: md), commonsCap: Ratio(num: cn, den: cd))
    }
}
