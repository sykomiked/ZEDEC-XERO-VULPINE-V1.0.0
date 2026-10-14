// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// Typed results of zxvd's methods. Field names follow the daemon
// (api/zxvd.openrpc.json; swarm.state and notes.list are built by
// state_json and notes_json in kernel/arch/hosted/zxv_host.c), decoded
// snake_case -> camelCase. 64-bit amounts arrive as decimal strings and are
// read with Int64(exactly:) only; a value that does not parse shows as nil.
import Foundation

public struct Discovery: Decodable, Sendable {
    public let `protocol`: String
    public let methods: [String]
}

public struct Version: Decodable, Sendable {
    public let app: String
    public let `protocol`: String
    public let libzxv: String
    public let libzxvFeatures: Int64
}

public struct ModelInfo: Decodable, Sendable {
    public let loaded: Bool
    public let canGenerate: Bool
    public let name: String
    public let arch: String
    public let nLayers: Int
    public let nVocab: Int
    public let status: String
}

public struct Answer: Decodable, Sendable {
    public let answer: String
}

public struct Note: Decodable, Sendable, Identifiable {
    public let seq: Int
    public let kind: String
    public let pri: Int
    public let read: Bool
    public let count: Int
    public let source: String
    public let title: String
    public let body: String
    public let action: String
    public var id: Int { seq }
}

public struct Notes: Decodable, Sendable {
    public let unread: Int
    public let notes: [Note]
}

public struct Marked: Decodable, Sendable { public let marked: Int }
public struct NetResult: Decodable, Sendable { public let result: String }
public struct UpdateResult: Decodable, Sendable { public let status: String }
public struct Enabled: Decodable, Sendable { public let enabled: String }
public struct Opened: Decodable, Sendable { public let account: Int }
public struct Posted: Decodable, Sendable { public let entryId: String }
public struct Checked: Decodable, Sendable { public let ok: Bool }
public struct Stopping: Decodable, Sendable { public let stopping: Bool }

public struct Account: Decodable, Sendable, Identifiable, Hashable {
    public let account: Int
    public let name: String
    public let currency: String
    public let kind: String
    public var id: Int { account }
}

public struct Accounts: Decodable, Sendable { public let accounts: [Account] }

/// The three rails of one account, in minor units: debit 555, credit 777, equity 888.
public struct Balance: Decodable, Sendable {
    public let debitMinor: String
    public let creditMinor: String
    public let equityMinor: String
    public var debit: Int64? { Int64(debitMinor) }
    public var credit: Int64? { Int64(creditMinor) }
    public var equity: Int64? { Int64(equityMinor) }
}

public struct Entry: Decodable, Sendable, Identifiable {
    public let entryId: String
    public let signedMinor: String
    public let counterparty: Int
    public let reference: String
    public var id: String { entryId + "/" + String(counterparty) + "/" + signedMinor }
    public var signed: Int64? { Int64(signedMinor) }
}

public struct Entries: Decodable, Sendable { public let entries: [Entry] }

// MARK: swarm.state

public struct SwarmState: Decodable, Sendable {
    public let version: String
    public let host: String
    public let remote: Bool
    public let cores: Int
    public let coresMilli: Int
    public let memTotalMb: Int64
    public let memMb: Int64
    public let levels: Int
    public let tokensPerCycle: Int64
    public let tick: Int64
    public let cycle: Int64
    public let settled: Int64
    public let held: Int64
    public let moneySupply: Int64
    public let pot: Int64
    public let imag: Int64
    public let saved: Int64
    /// The swarm's mood: the emotion's emoji, a space, its intensity.
    public let mood: String
    public let ledgerPostings: Int64
    public let ledgerSynced: Int64
    /// 0 while the settlement spine runs; otherwise why it halted.
    public let ledgerHalt: Int
    public let levied: Int64
    public let fees: Int64
    public let bountiesReturned: Int64
    public let audit: String
    public let auditRecords: Int64
    public let ledgerAudited: Int64
    public let ledgerIssues: Int64
    public let quest: Quest
    public let agents: [Agent]
    public let model: StateModel
    public let companion: Companion
    public let net: NetStatus
    public let update: UpdateStatus
    public let notes: NotesSummary
}

public struct Quest: Decodable, Sendable {
    public let contributed: Int64
    public let refused: Int
    public let skills: [Skill]
}

public struct Skill: Decodable, Sendable, Identifiable {
    public let skill: String
    public let level: Int
    public let points: Int
    public var id: String { skill }
}

public struct Agent: Decodable, Sendable, Identifiable {
    public let id: Int
    public let name: String
    public let level: Int
    public let emoji: String
    public let intensity: Int
    /// Tokens from the Fibonacci floor (real), the market, and the emotional (imaginary) pool.
    public let re: Int64
    public let mk: Int64
    public let im: Int64
    public let money: Int64
    public let social: Int64
    public let gen: Int
    /// The witness verdict on this agent's last work (1 = true).
    public let witness: Int
    public let witnessBy: String
    public var tokens: Int64 { re + mk + im }
}

public struct StateModel: Decodable, Sendable {
    public let loaded: Bool
    public let tokenizerOk: Bool
    public let canGenerate: Bool
    public let name: String
    public let arch: String
    public let bytes: Int64
    public let layers: Int64
    public let context: Int64
    public let vocab: Int64
    public let status: String
}

public struct Companion: Decodable, Sendable {
    public let cycleOpen: Bool
    public let allotted: Int64
    public let used: Int64
    public let remaining: Int64
    public let exhausted: Bool
    public let maxPerAnswer: Int
    public let asks: Int64
    public let exhaustedAsks: Int64
    public let lastAnswer: LastAnswer?
    public let lastTag: LastTag?
    public let quarantined: Int
    public let quarantineOk: Bool
}

public struct LastAnswer: Decodable, Sendable {
    public let remainingBefore: Int64
    public let allowed: Int
    public let generated: Int
    public let charged: Int64
    public let exhausted: Bool
}

/// The hexagram tag on the last answer (kernel/src/lpres/lpres_hex.c).
public struct LastTag: Decodable, Sendable {
    public let code: Int
    public let base: String
    public let polarity: String
    public let phase: String
    public let action: String
    public let reason: Int
    public let tokens: Int
    public let marginFirst: Int64
    public let marginSecond: Int64
    public let saturated: Int
}

public struct NetStatus: Decodable, Sendable {
    public let mode: String
    public let bound: Bool
    public let port: Int
    public let bind: String
    public let nodeId: String
    public let id: String
    public let shareCompute: Int64
    public let shareStorage: Int64
    public let wants: Int
    public let peers: Int
    public let inbound: Int
    public let outbound: Int
    public let refused: Int
    public let policyDrops: Int
    public let sendErrors: Int
    public let trades: Int
    public let receiptsRefused: Int
    public let imported: Int64
    public let conserved: Bool
    public let error: String

    enum CodingKeys: String, CodingKey {
        case mode, bound, port, bind, nodeId, id, shareCompute, shareStorage, wants, peers
        case inbound = "in"
        case outbound = "out"
        case refused, policyDrops, sendErrors, trades, receiptsRefused, imported, conserved, error
    }
}

public struct UpdateStatus: Decodable, Sendable {
    public let checked: Bool
    public let installable: Bool
    public let auto: Bool
    public let status: String
    public let title: String
    public let body: String
    public let gateway: String
    public let checks: Int
    public let requests: Int
    public let lastCheck: Int64
    public let error: String
}

public struct NotesSummary: Decodable, Sendable {
    public let unread: Int
    public let os: String
}

// MARK: views for the Studio panels (zxvd_rpc.h V1-V3, J1-J5)

/// One verified contact from net.peers, joined with its mesh-book account.
public struct Peer: Decodable, Sendable, Identifiable {
    public let id: String
    public let addr: String
    public let bucket: Int
    public let ubh: Bool
    public let lastSeenMs: String
    public let inBook: Bool
    public let member: Bool
    public let held: String
    public let receivable: String
    public let social: String
    public let pending: Int
    public var lastSeen: Date? {
        Int64(lastSeenMs).map { Date(timeIntervalSince1970: TimeInterval($0) / 1000) }
    }
}

public struct Peers: Decodable, Sendable {
    public let peers: [Peer]
    public let count: Int
}

public struct Share: Decodable, Sendable {
    public let compute: String
    public let storage: String
}

public struct Want: Decodable, Sendable, Identifiable {
    public let slot: Int
    public let resource: String
    public let unitsLeft: String
    public let maxPrice: String
    public var id: Int { slot }
}

/// A receipt waiting on the other side: a sell offer not yet accepted, or a buy not yet committed.
public struct Outstanding: Decodable, Sendable, Identifiable {
    public let role: String
    public let peer: String
    public let units: String
    public let seq: String
    public let tries: Int
    public let want: Int
    public var id: String { role + peer + seq }
}

public struct Offers: Decodable, Sendable {
    public let share: Share
    public let wants: [Want]
    public let outstanding: [Outstanding]
}

public struct CardInput: Decodable, Sendable {
    public let value: String
    public let source: String
}

public struct NextTier: Decodable, Sendable {
    public let tier: Int
    public let metal: String
    public let stone: String
    public let credibility: String
    public let bond: String
    public let uptimeBps: String
    public let missing: [String]
}

/// player.card: the tier the measured inputs earn (sicily.h S2). No priority, discount or credit.
public struct PlayerCard: Decodable, Sendable {
    public let tier: Int
    public let metal: String
    public let stone: String
    public let inputs: [String: CardInput]
    public let next: NextTier?
    public let perks: [String]
}

public struct Jurisdiction: Decodable, Sendable {
    public let market: String
    public let standard: String
    public let country: String
    public let standards: [String]
    /// False: the settings are recorded only (zxvd_rpc.h J5).
    public let inEffect: Bool
}

// MARK: chat.* (zxvd_chat.h C5)

/// chat.me: the owner's chat identity.
public struct ChatMe: Decodable, Sendable {
    public let nodeId: String
    /// False: in memory for this run only (zxvd --books -).
    public let saved: Bool
    public let records: Int
    public let following: [String]
}

public struct ChatReaction: Decodable, Sendable, Hashable {
    public let emoji: String
    public let count: Int
    public let mine: Bool
}

/// A signed post, reply or reaction with its replies and reactions.
public struct ChatRecord: Decodable, Sendable, Identifiable, Hashable {
    public let id: String
    public let author: String
    public let mine: Bool
    public let kind: String
    public let lamport: String
    /// Milliseconds since 1970 as a decimal string, "0" when absent (a hint only).
    public let wallMs: String
    public let text: String
    public let replyTo: String?
    public let replies: Int
    public let reactions: [ChatReaction]

    public var date: Date? {
        guard let ms = Double(wallMs), ms > 0 else { return nil }
        return Date(timeIntervalSince1970: ms / 1000)
    }
}

public struct ChatFeed: Decodable, Sendable {
    public let posts: [ChatRecord]
}

public struct ChatThread: Decodable, Sendable {
    public let root: ChatRecord
    public let replies: [ChatRecord]
}

public struct ChatFollow: Decodable, Sendable {
    public let nodeId: String
    public let following: Bool
}

// MARK: escrow (zxvd_pay.h P5)

/// One escrow hold, read back from the escrow account's entries.
public struct Hold: Decodable, Sendable, Identifiable, Hashable {
    public let hold: Int
    /// "held", "released" or "refunded".
    public let state: String
    /// The payer's account.
    public let from: Int
    /// The payee's account.
    public let to: Int
    public let amountMinor: String
    public let currency: String
    /// The holder account "escrow: <CCY>" the funds sit in.
    public let escrowAccount: Int
    public let entryId: String
    /// The release or refund posting; nil while held.
    public let closedEntryId: String?
    /// What releases it: "owner" (only the owner's own call; no conditions yet).
    public let releasesOn: String
    public var id: Int { hold }
    public var amount: Int64? { Int64(amountMinor) }
    public var isHeld: Bool { state == "held" }
}

/// An escrow account: its balance against what its open holds add up to.
public struct EscrowAccount: Decodable, Sendable, Identifiable, Hashable {
    public let account: Int
    public let currency: String
    public let balanceMinor: String
    public let heldMinor: String
    public let consistent: Bool
    public var id: Int { account }
}

/// ledger.holds: every hold, the escrow accounts, and the escrow entries that are no hold's step.
public struct Holds: Decodable, Sendable {
    public let holds: [Hold]
    public let escrowAccounts: [EscrowAccount]
    public let unmatched: Int
}
