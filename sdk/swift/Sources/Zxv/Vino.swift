// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

import CZxv

/// ABI 1.6: Vino Embed. What an app or game needs to take part in Vino
/// without ever holding anyone's value or keys. There are two lanes, and
/// they never meet:
///
/// - **Play** (`Vino.Play`): a sandbox inside the app with its own play
///   units, issuers, holders and Fibonacci-face vouchers, in process memory
///   only. No call takes a play sandbox and anything live, so a play unit
///   can never be converted into, redeemed for, or moved into live value.
///   Play units are not money and are lost when the sandbox is closed.
/// - **Live**: real Vino stays in the user's own ZXV wallet, never in the
///   app. The app builds a payment request (`Vino.Request`); the user
///   approves it in their wallet, which signs a receipt
///   (`Vino.signReceipt`, wallet side); the app checks the receipt offline
///   (`Vino.verifyReceipt`). The app never sees a balance, a key or a
///   custody account.
///
/// VOUCHERS (play lane). A voucher is one coin whose face is a Fibonacci
/// ladder face (`Coin.face(rung:)`), held by one holder on exactly ONE
/// active rail: `.bearer` (a note in the holder's hand) or `.ghost` (its
/// twin on the play ledger). A transfer must name the active rail (else
/// `.refused(.railMismatch)`), so one voucher can never be spent twice.
/// Switching rails moves nothing.
///
/// COVERAGE. An issuer posts play backing and may only issue while
///
///     10 * backing >= 18 * outstanding    (exact integers, no rounding)
///
/// that is, backing stays at least 1.8x the faces it has outstanding. An
/// issue or a withdrawal that would break this is refused
/// (`.refused(.coverageRefused)`). Redeeming a voucher hands it back to its
/// issuer and retires it: the issuer's outstanding falls and nothing is paid
/// out to anyone. No call turns a voucher into backing, play units or
/// anything else, and there is no interest, yield or return of any kind.
///
/// DAILY CAP. `Vino.SpendMeter` counts what one device approved per day and
/// refuses an amount that would pass a deployment's daily cap. It is a
/// counter, not a legal check, and makes no exemption claim.
///
/// CAPABILITIES. `Play` and `SpendMeter` need `.vino` (else
/// `.refused(.capabilityDenied)`; a halted context gives `.fatal`).
/// Requests, receipt messages and receipt verification are pure and need no
/// capability. Signing a receipt needs `.cryptoSign`: it is the wallet's
/// call, not the app's.
///
/// NOT HERE, by design: minting live Vino, setting pegs, any way into or
/// out of other currencies, V-Bill auctions or bids, V-Bonds and
/// derivatives, and every operator or steward action. None of them is
/// app-facing.
///
/// Honest limits: the live flow here is the app's half only; approving a
/// request happens in the user's wallet (zxvd), which posts the payment on
/// its own ledger. ML-DSA-87 is the reference implementation, not FIPS
/// 140-3 validated. Whether a given deployment of Vino is store credit, a
/// prepaid access product or something else is a legal question pending
/// counsel; nothing in this SDK decides it. The raffle is a separate
/// module, not a payment request.
public enum Vino {
    /// The most issuers plus holders one play sandbox can hold (256).
    public static let maxAccounts = UInt32(ZXV_VINO_MAX_ACCOUNTS)
    /// The most active (unredeemed) vouchers one play sandbox can hold (4096).
    public static let maxVouchers = UInt32(ZXV_VINO_MAX_VOUCHERS)
    /// The largest single amount: a backing post, a request amount, a daily
    /// cap or a meter admission (2^59).
    public static let amountMax: UInt64 = 1 << 59
    /// Size of an encoded payment request (160 bytes).
    public static let requestBytes = Int(ZXV_VINO_REQ_BYTES)
    /// Size of a request id: SHA-256 of the request bytes (32 bytes).
    public static let requestIdBytes = 32
    /// Size of a request's merchant id (32 bytes).
    public static let merchantBytes = 32
    /// Size of a request's nonce (16 bytes).
    public static let nonceBytes = 16
    /// Longest memo, in bytes of printable ASCII (64).
    public static let memoMax = Int(ZXV_VINO_MEMO_MAX)
    /// Size of the message a wallet signs to approve a request (88 bytes).
    public static let receiptMessageBytes = Int(ZXV_VINO_RCPT_MSG_BYTES)
    /// Size of a wallet's ML-DSA-87 public key (2592 bytes).
    public static let walletPublicKeyBytes = Int(ZXV_MLDSA87_PK_BYTES)
    /// Size of a wallet's ML-DSA-87 secret key (4896 bytes).
    public static let walletSecretKeyBytes = Int(ZXV_MLDSA87_SK_BYTES)
    /// Size of a receipt signature (ML-DSA-87, 4627 bytes).
    public static let receiptSignatureBytes = Int(ZXV_MLDSA87_SIG_BYTES)

    /// The rail a play voucher is on; exactly one is active at a time.
    ///
    /// A struct with named values rather than an enum, so a raw value read
    /// from elsewhere passes through unchanged and the library refuses it
    /// (`.usage`, `ZXV_E_ARG`) instead of the SDK guessing.
    public struct Rail: RawRepresentable, Sendable, Hashable, CustomStringConvertible {
        public let rawValue: Int32
        public init(rawValue: Int32) { self.rawValue = rawValue }
        /// A note in the holder's hand.
        public static let bearer = Rail(rawValue: Int32(ZXV_VINO_RAIL_BEARER))
        /// Its twin on the play ledger.
        public static let ghost = Rail(rawValue: Int32(ZXV_VINO_RAIL_GHOST))
        /// "bearer", "ghost", or "rail(n)".
        public var description: String { self == .bearer ? "bearer" : self == .ghost ? "ghost" : "rail(\(rawValue))" }
    }

    /// The lane of a payment request. The two lanes never meet. (A struct
    /// with named values, like `Rail`: an unknown lane reaches the library,
    /// which refuses it with `.usage` `ZXV_E_LANE`.)
    public struct Lane: RawRepresentable, Sendable, Hashable, CustomStringConvertible {
        public let rawValue: Int32
        public init(rawValue: Int32) { self.rawValue = rawValue }
        /// Play units inside the app; never live value.
        public static let play = Lane(rawValue: Int32(ZXV_VINO_LANE_PLAY))
        /// Real Vino, approved in the user's own wallet.
        public static let live = Lane(rawValue: Int32(ZXV_VINO_LANE_LIVE))
        /// "play", "live", or "lane(n)".
        public var description: String { self == .play ? "play" : self == .live ? "live" : "lane(\(rawValue))" }
    }

    /// Whether a play account issues vouchers or holds them. (A struct with
    /// named values, like `Rail`.)
    public struct AccountKind: RawRepresentable, Sendable, Hashable, CustomStringConvertible {
        public let rawValue: Int32
        public init(rawValue: Int32) { self.rawValue = rawValue }
        /// Receives, spends and redeems vouchers.
        public static let holder = AccountKind(rawValue: 0)
        /// Posts play backing and issues vouchers against it.
        public static let issuer = AccountKind(rawValue: 1)
        /// "holder", "issuer", or "kind(n)".
        public var description: String {
            self == .holder ? "holder" : self == .issuer ? "issuer" : "kind(\(rawValue))"
        }
    }

    /// A play voucher's life: active until its holder redeems it.
    public enum VoucherState: Int32, Sendable {
        /// Can be transferred, switched or redeemed.
        case active = 1
        /// Retired by its holder; still readable by id, does nothing.
        case redeemed = 2
    }

    /// Payment request flags (`Request.flags`).
    public struct RequestFlags: OptionSet, Sendable, Hashable {
        public let rawValue: UInt32
        public init(rawValue: UInt32) { self.rawValue = rawValue }
        /// The purchase has a paid chance outcome (a loot box, a paid draw).
        /// Allowed on the play lane only; a live request with it is refused
        /// (`.refused(.paidChanceRefused)`).
        public static let chance = RequestFlags(rawValue: ZXV_VINO_REQ_CHANCE)
        /// Every flag this ABI knows; any other bit is `.usage` `ZXV_E_FORMAT`.
        public static let known = RequestFlags(rawValue: ZXV_VINO_REQ_FLAGS_KNOWN)
    }

    // MARK: payment requests

    /// A payment request: a plain value the app builds and the user's
    /// wallet approves. It travels as exactly 160 canonical bytes
    /// (`encode()`); its id is SHA-256 of those bytes. Encoding and decoding
    /// are pure: no capability, no context, no network. Building a request
    /// moves nothing; only the wallet can approve it.
    ///
    /// Layout (integers little-endian): 0 "ZXVR", 4 version 1, 5 lane,
    /// 6 flags (2), 8 merchant (32), 40 nonce (16), 56 amount (8),
    /// 64 createdAt (8), 72 expiresAt (8), 80 memo length (1), 81 memo (NUL
    /// padded to 64), 145 fifteen zero bytes.
    public struct Request: Sendable, Hashable {
        /// Play units (`.play`) or real Vino (`.live`).
        public var lane: Lane
        /// Flags; `.chance` is play-only.
        public var flags: RequestFlags
        /// The payee's id (32 bytes, e.g. SHA-256 of its key).
        public var merchant: [UInt8]
        /// Unique per request (16 bytes, e.g. from `randomBytes(16)`).
        public var nonce: [UInt8]
        /// 1...amountMax: VFV minor units (live) or play units (play).
        public var amount: UInt64
        /// Seconds of the caller's clock (Unix time by convention).
        public var createdAt: UInt64
        /// After `createdAt`; the wallet cannot approve after it.
        public var expiresAt: UInt64
        /// 0...64 bytes of printable ASCII (0x20...0x7e).
        public var memo: String

        /// A request with every field given (`memo` and `flags` optional).
        public init(lane: Lane, flags: RequestFlags = [], merchant: [UInt8], nonce: [UInt8], amount: UInt64,
                    createdAt: UInt64, expiresAt: UInt64, memo: String = "") {
            self.lane = lane
            self.flags = flags
            self.merchant = merchant
            self.nonce = nonce
            self.amount = amount
            self.createdAt = createdAt
            self.expiresAt = expiresAt
            self.memo = memo
        }

        /// The canonical bytes and id of a request.
        public struct Encoded: Sendable, Hashable {
            /// The 160 canonical bytes: what the wallet approves.
            public let bytes: [UInt8]
            /// SHA-256 of `bytes` (32 bytes).
            public let id: [UInt8]
        }

        /// The canonical 160 bytes and the id. Throws `.usage` with
        /// `ZXV_E_LANE` (lane neither play nor live; `isLane`),
        /// `ZXV_E_FORMAT` (an unknown flag, a memo that is not printable
        /// ASCII), `ZXV_E_RANGE` (amount 0 or above `amountMax`, `expiresAt`
        /// not after `createdAt`), `ZXV_E_TOO_LONG` (memo above 64 bytes) or
        /// `ZXV_E_ARG` (merchant not 32 bytes, nonce not 16, memo with a NUL
        /// byte); `.refused(.paidChanceRefused)` for a live request with
        /// `.chance`.
        public func encode() throws(ZxvError) -> Encoded {
            try needSize(merchant.count, merchantBytes, "request merchant")
            try needSize(nonce.count, nonceBytes, "request nonce")
            let m = try cText(memo, "request memo")
            var out = [UInt8](repeating: 0, count: requestBytes)
            var id = [UInt8](repeating: 0, count: requestIdBytes)
            try check(zxv_vino_request_encode(lane.rawValue, flags.rawValue, merchant, nonce, amount,
                                              createdAt, expiresAt, m, &out, &id))
            return Encoded(bytes: out, id: id)
        }

        /// Parse and validate 160 request bytes under the same rules as
        /// `encode()`, plus the magic, the version and the zero padding
        /// (`.usage` `ZXV_E_FORMAT`). `.usage` `ZXV_E_ARG` when `bytes` is
        /// not 160 bytes long.
        public static func decode(_ bytes: [UInt8]) throws(ZxvError) -> Request {
            try decodeWithId(bytes).request
        }

        /// `decode(_:)` plus the request's id (SHA-256 of `bytes`).
        public static func decodeWithId(_ bytes: [UInt8]) throws(ZxvError) -> (request: Request, id: [UInt8]) {
            try needSize(bytes.count, requestBytes, "request")
            var lane: Int32 = 0, flags: UInt32 = 0
            var merchant = [UInt8](repeating: 0, count: merchantBytes)
            var nonce = [UInt8](repeating: 0, count: nonceBytes)
            var amount: UInt64 = 0, created: UInt64 = 0, expires: UInt64 = 0
            var memo = [CChar](repeating: 0, count: memoMax + 1)
            var id = [UInt8](repeating: 0, count: requestIdBytes)
            try check(zxv_vino_request_decode(bytes, &lane, &flags, &merchant, &nonce, &amount, &created,
                                              &expires, &memo, &id))
            let r = Request(lane: Lane(rawValue: lane), flags: RequestFlags(rawValue: flags), merchant: merchant,
                            nonce: nonce, amount: amount, createdAt: created, expiresAt: expires,
                            memo: cArrayText(memo))
            return (r, id)
        }
    }

    // MARK: receipts

    /// The exact 88 bytes a wallet signs to approve request `requestId` (32
    /// bytes) with its public key (2592 bytes) at `approvedAt`:
    /// "zxv/vino-rcpt/v1" || request id || SHA-256(wallet public key) ||
    /// approvedAt (8 bytes little-endian). Pure. `.usage` `ZXV_E_ARG` for a
    /// buffer of the wrong size.
    public static func receiptMessage(requestId: [UInt8], walletPublicKey: [UInt8],
                                      approvedAt: UInt64) throws(ZxvError) -> [UInt8] {
        try needSize(requestId.count, requestIdBytes, "request id")
        try needSize(walletPublicKey.count, walletPublicKeyBytes, "wallet public key")
        var m = [UInt8](repeating: 0, count: receiptMessageBytes)
        try check(zxv_vino_receipt_msg(requestId, walletPublicKey, approvedAt, &m))
        return m
    }

    /// WALLET SIDE: approve the encoded `request` at `approvedAt` (which
    /// must lie within the request's `createdAt...expiresAt`) by signing its
    /// receipt message with the wallet's ML-DSA-87 key. Returns the
    /// 4627-byte signature (hedged: two calls give different, equally valid
    /// signatures). Needs `.cryptoSign`.
    ///
    /// The secret key stays the caller's: it is read for this call only and
    /// never copied or kept. Throws `.refused(.requestExpired)` (outside the
    /// window), the request decoding errors, `.refused(.notImplemented)`
    /// (built without post-quantum crypto), `.refused(.capabilityDenied)`,
    /// `.usage` `ZXV_E_ARG` (a buffer of the wrong size).
    public static func signReceipt(_ cap: Capability, request: [UInt8], walletSecretKey: SecretKey,
                                   walletPublicKey: [UInt8], approvedAt: UInt64) throws(ZxvError) -> [UInt8] {
        try needSize(request.count, requestBytes, "request")
        try needSize(walletSecretKey.count, walletSecretKeyBytes, "wallet secret key")
        try needSize(walletPublicKey.count, walletPublicKeyBytes, "wallet public key")
        var sig = [UInt8](repeating: 0, count: receiptSignatureBytes)
        try cap.call {
            zxv_vino_receipt_sign($0, request, walletSecretKey.pointer, walletPublicKey, approvedAt, &sig)
        }
        return sig
    }

    /// APP SIDE, offline: check that `signature` is the wallet's approval of
    /// the encoded `request` at `approvedAt`, that `approvedAt` lies within
    /// the request's window and that `now` is not past its expiry. Returns
    /// normally when it is. Pure: no capability, no network.
    ///
    /// Throws `.refused(.verificationFailed)` (not that wallet's signature
    /// over this request and time), `.refused(.requestExpired)` (approved
    /// outside the window, or `now` past the expiry), the request decoding
    /// errors, `.refused(.notImplemented)` (built without post-quantum
    /// crypto), `.usage` `ZXV_E_ARG` (a buffer of the wrong size).
    public static func verifyReceipt(request: [UInt8], walletPublicKey: [UInt8], approvedAt: UInt64,
                                     signature: [UInt8], now: UInt64) throws(ZxvError) {
        try needSize(request.count, requestBytes, "request")
        try needSize(walletPublicKey.count, walletPublicKeyBytes, "wallet public key")
        try needSize(signature.count, receiptSignatureBytes, "receipt signature")
        try check(zxv_vino_receipt_verify(request, walletPublicKey, approvedAt, signature, now))
    }

    /// `verifyReceipt` as a yes/no answer: false for a signature that does
    /// not verify or an expired request. Every other error (a malformed
    /// request, a wrong-size buffer) still throws, since it is a bug, not a
    /// bad receipt.
    public static func isReceiptValid(request: [UInt8], walletPublicKey: [UInt8], approvedAt: UInt64,
                                      signature: [UInt8], now: UInt64) throws(ZxvError) -> Bool {
        do {
            try verifyReceipt(request: request, walletPublicKey: walletPublicKey, approvedAt: approvedAt,
                              signature: signature, now: now)
            return true
        } catch {
            switch error.refusal {
            case .verificationFailed?, .requestExpired?: return false
            default: throw error
            }
        }
    }

    // MARK: the play lane

    /// A snapshot of one play voucher (`Play.voucher(_:)`).
    public struct VoucherInfo: Sendable, Equatable {
        /// Its Fibonacci ladder face, in play units.
        public let face: UInt64
        /// The issuer account that issued it.
        public let issuer: UInt32
        /// The holder account that holds (or redeemed) it.
        public let holder: UInt32
        /// Its one active rail.
        public let rail: Rail
        /// Active or redeemed.
        public let state: VoucherState
        /// True while it can still be transferred, switched or redeemed.
        public var isActive: Bool { state == .active }
    }

    /// A snapshot of one play account (`Play.account(_:)`).
    public struct AccountInfo: Sendable, Equatable {
        /// The owner id given to `Play.open`.
        public let owner: UInt32
        /// Issuer or holder.
        public let kind: AccountKind
        /// An issuer's play backing; 0 for a holder.
        public let backing: UInt64
        /// An issuer's outstanding faces, or the faces a holder holds.
        public let faces: UInt64
        /// Active vouchers it issued (issuer) or holds (holder).
        public let count: UInt32
        /// True for an issuer account.
        public var isIssuer: Bool { kind == .issuer }
    }

    /// One play-lane sandbox: issuers, holders and Fibonacci-face vouchers
    /// in play units, in process memory only. Play units come from nowhere
    /// (`back` is a faucet), are worth nothing, are not money, can never
    /// become live value, and are lost when the sandbox is closed.
    ///
    /// Every call needs `.vino` from the capability the sandbox was created
    /// with (it keeps that capability). Refusals change nothing. Account ids
    /// count up from 0; voucher ids start at 1 and are never reused.
    ///
    /// Thread safety: internally serialized, so any thread may call it.
    /// `close()` releases it at once; any call after that throws `.usage`
    /// (`ZXV_E_ARG`) instead of reaching C. It keeps its context alive.
    public final class Play: @unchecked Sendable {
        let cap: Capability
        let cell: HandleCell

        /// An empty sandbox on the capability's context.
        /// `.refused(.capabilityDenied)` without `.vino`; `.fatal` on a
        /// halted context.
        public init(_ cap: Capability) throws(ZxvError) {
            self.cap = cap
            var p: OpaquePointer?
            try cap.call { zxv_vino_play_create($0, &p) }
            cell = HandleCell(p!, "Vino.Play") { zxv_vino_play_destroy($0) }
        }

        private func run(_ body: (OpaquePointer, OpaquePointer) -> Int32) throws(ZxvError) {
            let p = try cell.acquire()
            defer { cell.release() }
            try cap.call { body($0, p) }
        }

        /// Open an account of `kind` for `owner` (an id of your choosing;
        /// owners need not be distinct) and return its account id.
        /// `.refused` `ZXV_E_CAPACITY` once `maxAccounts` are open; `.usage`
        /// `ZXV_E_ARG` for a kind that is neither holder nor issuer.
        @discardableResult
        public func open(owner: UInt32, kind: AccountKind) throws(ZxvError) -> UInt32 {
            var a: UInt32 = 0
            try run { zxv_vino_play_open($0, $1, owner, kind.rawValue, &a) }
            return a
        }
        /// Open an issuer account for `owner`; returns its account id.
        public func openIssuer(owner: UInt32) throws(ZxvError) -> UInt32 { try open(owner: owner, kind: .issuer) }
        /// Open a holder account for `owner`; returns its account id.
        public func openHolder(owner: UInt32) throws(ZxvError) -> UInt32 { try open(owner: owner, kind: .holder) }

        /// Post `amount` play units of backing to `issuer` (the play
        /// faucet). `.usage` `ZXV_E_ARG` (not an issuer), `ZXV_E_NOT_FOUND`
        /// (no such account), `ZXV_E_RANGE` (amount 0, above `amountMax`, or
        /// backing would pass 2^62).
        public func back(issuer: UInt32, amount: UInt64) throws(ZxvError) {
            try run { zxv_vino_play_back($0, $1, issuer, amount) }
        }
        /// Withdraw `amount` of an issuer's play backing, only while it
        /// stays at 1.8x coverage. `.refused(.coverageRefused)` (it would
        /// not), `.refused(.insufficientFunds)` (more than its backing),
        /// plus `back`'s errors.
        public func unback(issuer: UInt32, amount: UInt64) throws(ZxvError) {
            try run { zxv_vino_play_unback($0, $1, issuer, amount) }
        }

        /// Issue one voucher of Fibonacci face `face` from `issuer` to
        /// `holder` on `rail`; returns its id (> 0, never reused here).
        /// `.usage` with `isDenomination` (not a ladder face: 4, 6, ...),
        /// `.refused(.coverageRefused)` (the issuer would fall below 1.8x),
        /// `.usage` `ZXV_E_ARG` (issuer not an issuer, holder not a holder,
        /// bad rail) / `ZXV_E_NOT_FOUND`, `.refused` `ZXV_E_CAPACITY`
        /// (`maxVouchers` active).
        @discardableResult
        public func issue(issuer: UInt32, holder: UInt32, face: UInt64, rail: Rail = .bearer) throws(ZxvError) -> UInt64 {
            var id: UInt64 = 0
            try run { zxv_vino_issue($0, $1, issuer, holder, face, rail.rawValue, &id) }
            return id
        }
        /// Spend active voucher `voucher` from its holder `from` to holder
        /// `to` on `rail`, which must be its active rail; the rail does not
        /// change. `.refused(.railMismatch)` (not the active rail),
        /// `.refused` `ZXV_E_STATE` (redeemed), `.usage` `ZXV_E_ARG` (`from`
        /// is not its holder, `to` not a holder, from == to) /
        /// `ZXV_E_NOT_FOUND`.
        public func transfer(_ voucher: UInt64, from: UInt32, to: UInt32, rail: Rail) throws(ZxvError) {
            try run { zxv_vino_transfer($0, $1, voucher, from, to, rail.rawValue) }
        }
        /// Switch active voucher `voucher`, by its holder, to `rail`
        /// (bearer <-> ghost). Nothing moves. `.refused(.railMismatch)`
        /// (already on that rail), `.refused` `ZXV_E_STATE` (redeemed),
        /// `.usage` `ZXV_E_ARG` (not its holder) / `ZXV_E_NOT_FOUND`.
        public func setRail(_ voucher: UInt64, holder: UInt32, rail: Rail) throws(ZxvError) {
            try run { zxv_vino_set_rail($0, $1, voucher, holder, rail.rawValue) }
        }
        /// Redeem active voucher `voucher`, by its holder, to its issuer: it
        /// is retired and the issuer's outstanding falls by its face.
        /// Nothing is paid out to anyone. `.refused` `ZXV_E_STATE` (already
        /// redeemed), `.usage` `ZXV_E_ARG` (not its holder) /
        /// `ZXV_E_NOT_FOUND`.
        public func redeem(_ voucher: UInt64, holder: UInt32) throws(ZxvError) {
            try run { zxv_vino_redeem($0, $1, voucher, holder) }
        }

        /// A voucher's face, issuer, holder, rail and state. `.usage`
        /// `ZXV_E_NOT_FOUND` for an id never issued here.
        public func voucher(_ id: UInt64) throws(ZxvError) -> VoucherInfo {
            var face: UInt64 = 0, issuer: UInt32 = 0, holder: UInt32 = 0, rail: Int32 = 0, state: Int32 = 0
            try run { zxv_vino_voucher_info($0, $1, id, &face, &issuer, &holder, &rail, &state) }
            return VoucherInfo(face: face, issuer: issuer, holder: holder, rail: Rail(rawValue: rail),
                               state: try cEnum(state, "voucher state"))
        }
        /// An account's owner, kind and totals. `.usage` `ZXV_E_NOT_FOUND`
        /// for an account never opened here.
        public func account(_ account: UInt32) throws(ZxvError) -> AccountInfo {
            var owner: UInt32 = 0, isIssuer: Int32 = 0, backing: UInt64 = 0, faces: UInt64 = 0, count: UInt32 = 0
            try run { zxv_vino_account_info($0, $1, account, &owner, &isIssuer, &backing, &faces, &count) }
            return AccountInfo(owner: owner, kind: isIssuer != 0 ? .issuer : .holder, backing: backing,
                               faces: faces, count: count)
        }
        /// The ids of the active vouchers `account` issued (issuer) or holds
        /// (holder), ascending. Redeemed vouchers are not listed. `.usage`
        /// `ZXV_E_NOT_FOUND` for an unknown account.
        public func vouchers(_ account: UInt32) throws(ZxvError) -> [UInt64] {
            var ids: [UInt64] = []
            try run { c, p in
                // Two-call pattern; retry if another thread grew the list in
                // between the two calls.
                while true {
                    var n = 0
                    let st = ids.withUnsafeMutableBufferPointer { b in
                        zxv_vino_vouchers(c, p, account, b.count == 0 ? nil : b.baseAddress, b.count, &n)
                    }
                    if st != ZXV_E_BUFFER {
                        if st == ZXV_OK { ids.removeLast(ids.count - n) }
                        return st
                    }
                    ids = [UInt64](repeating: 0, count: n)
                }
            }
            return ids
        }
        /// Re-check every invariant of the sandbox: each active voucher has
        /// a ladder face, one rail, an issuer and a holder; each issuer's
        /// outstanding is the sum of its active faces and keeps 1.8x
        /// coverage; each holder's total is what it holds. A failure is
        /// `.fatal` (`ZXV_E_INVARIANT`) and halts the context.
        public func check() throws(ZxvError) {
            try run { zxv_vino_play_check($0, $1) }
        }

        /// Release the sandbox now (its play units are gone). Idempotent.
        public func close() { cell.close() }
    }

    // MARK: the daily meter

    /// What `SpendMeter.tryAdmit` decided.
    public struct MeterDecision: Sendable, Equatable {
        /// True when the amount was counted.
        public let admitted: Bool
        /// What is left of today's cap after the call (either way).
        public let left: UInt64
    }

    /// One device's daily spend counter. It counts what the device approved
    /// per day index and refuses an amount that would pass the daily cap.
    /// It is a counter, not a legal check, and makes no exemption claim.
    /// Needs `.vino`. Internally serialized; `close()` releases it, and any
    /// call after that throws `.usage` (`ZXV_E_ARG`).
    public final class SpendMeter: @unchecked Sendable {
        let cap: Capability
        let cell: HandleCell

        /// A meter with cap `dailyCap` (1...amountMax) per day. `.usage`
        /// `ZXV_E_RANGE` outside that, `.refused(.capabilityDenied)` without
        /// `.vino`.
        public init(_ cap: Capability, dailyCap: UInt64) throws(ZxvError) {
            self.cap = cap
            var m: OpaquePointer?
            try cap.call { zxv_vino_meter_create($0, dailyCap, &m) }
            cell = HandleCell(m!, "Vino.SpendMeter") { zxv_vino_meter_destroy($0) }
        }

        /// Count `amount` against day `day` (for example Unix time / 86400)
        /// and return what is left today. A day after the last one starts a
        /// fresh count. `.refused(.limitRefused)` when today's total would
        /// pass the cap (nothing is counted; `tryAdmit` tells what is left),
        /// `.usage` `ZXV_E_RANGE` for an amount of 0 or above `amountMax`, or
        /// a day before the last one (the clock went back).
        @discardableResult
        public func admit(day: UInt64, amount: UInt64) throws(ZxvError) -> UInt64 {
            let m = try cell.acquire()
            defer { cell.release() }
            var left: UInt64 = 0
            try cap.call { zxv_vino_meter_admit($0, m, day, amount, &left) }
            return left
        }

        /// Like `admit`, but a cap refusal is an answer, not an error:
        /// `MeterDecision(admitted: false, left: ...)` when the amount would
        /// pass today's cap. Other errors still throw.
        public func tryAdmit(day: UInt64, amount: UInt64) throws(ZxvError) -> MeterDecision {
            let m = try cell.acquire()
            defer { cell.release() }
            try cap.context.ensureAlive()
            var left: UInt64 = 0
            let st = zxv_vino_meter_admit(cap.handle, m, day, amount, &left)
            if st == ZXV_E_LIMIT { return MeterDecision(admitted: false, left: left) }
            try Zxv.check(st, cap.context)
            return MeterDecision(admitted: true, left: left)
        }

        /// Release the meter now. Idempotent.
        public func close() { cell.close() }
    }
}
