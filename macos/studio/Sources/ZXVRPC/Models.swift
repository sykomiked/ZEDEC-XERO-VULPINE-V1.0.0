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

// MARK: phone.* (kernel/arch/hosted/zxvd_phone.h)

/// phone.link: whether paired phones may connect, and on which TCP port.
public struct PhoneLink: Decodable, Sendable {
    public let on: Bool
    public let port: Int
}

/// phone.pair: the link to show once, as a QR code, on the phone's screen.
public struct PhonePairing: Decodable, Sendable {
    public let phoneId: String
    public let name: String
    public let allowPost: Bool
    public let host: String
    public let port: Int
    /// zxv-phone:1?h=...&k=... holds the phone's secret key: show it, never log it.
    public let link: String
}

public struct PairedPhone: Decodable, Sendable, Identifiable {
    public let phoneId: String
    public let name: String
    public let allowPost: Bool
    public let connected: Int
    public var id: String { phoneId }
}

public struct PhoneList: Decodable, Sendable {
    public let on: Bool
    public let port: Int
    public let phones: [PairedPhone]
}

public struct Revoked: Decodable, Sendable {
    public let revoked: Bool
    public let closed: Int
}
