// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// The app's live model of zxvd. Every value shown comes from a zxvd method;
// nothing is computed on the side except formatting. Calls run off the main
// thread (ZXVDClient.request) and land back here on the main actor.
import AppKit
import Foundation
import UserNotifications
import ZXVRPC

/// Finds zxvd at the socket, or starts the bundled one.
enum Connector {
    static var bundledZXVD: String? {
        Bundle.main.url(forAuxiliaryExecutable: "zxvd")?.path
    }

    static var logURL: URL? {
        let dir = FileManager.default.homeDirectoryForCurrentUser
            .appendingPathComponent("Library/Logs/ZXV", isDirectory: true)
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        let f = dir.appendingPathComponent("studio-zxvd.log")
        if !FileManager.default.fileExists(atPath: f.path) {
            FileManager.default.createFile(atPath: f.path, contents: nil)
        }
        return f
    }

    /// Discover over the socket; if nothing listens and the bundle has zxvd, start it first.
    static func connect(socket: String) throws -> (ZXVDClient, DaemonProcess?, Discovery) {
        let client = ZXVDClient(socketPath: socket)
        do {
            return (client, nil, try client.call("rpc.discover", as: Discovery.self))
        } catch ZXVDError.noDaemon(let p) {
            guard let bin = bundledZXVD else { throw ZXVDError.noDaemon(p) }
            // zxvd makes run/ itself (0700); its parent and the models folder must exist
            let fm = FileManager.default
            let runParent = (socket as NSString).deletingLastPathComponent.deletingLastPathComponentString
            try? fm.createDirectory(atPath: runParent, withIntermediateDirectories: true,
                                    attributes: [.posixPermissions: 0o700])
            try? fm.createDirectory(atPath: ZXVDLocation.defaultModelsDir, withIntermediateDirectories: true)
            let d = try DaemonProcess.start(binary: bin, socketPath: socket,
                                            modelsDir: ZXVDLocation.defaultModelsDir, logURL: logURL)
            return (client, d, try client.call("rpc.discover", as: Discovery.self))
        }
    }
}

private extension String {
    var deletingLastPathComponentString: String { (self as NSString).deletingLastPathComponent }
}

struct ChatLine: Identifiable {
    let id = UUID()
    let mine: Bool
    let text: String
    let tag: LastTag?
    let charged: Int64?
}

struct LogLine: Identifiable {
    let id = UUID()
    let command: String
    let result: String
    let ok: Bool
}

@MainActor
final class AppStore: ObservableObject {
    enum Link: Equatable {
        case connecting
        case connected
        case down(String)
    }

    @Published var link: Link = .connecting
    @Published var methods: Set<String> = []
    @Published var version: Version?
    @Published var state: SwarmState?
    @Published var model: ModelInfo?
    @Published var notes: [Note] = []
    @Published var unread = 0
    @Published var accounts: [Account] = []
    @Published var selectedAccount: Int?
    @Published var balance: Balance?
    @Published var entries: [Entry] = []
    @Published var booksOK: Bool?
    @Published var booksCheckedAt: Date?
    @Published var chat: [ChatLine] = []
    @Published var asking = false
    @Published var netLog: [LogLine] = []
    @Published var currenciesEnabled: [String] = []

    let socketPath: String
    private var client: ZXVDClient
    private var daemon: DaemonProcess?
    private var poll: Task<Void, Never>?
    private var lastNotified = 0
    private var primed = false
    /// Set when the owner stops zxvd from Settings: no automatic restart.
    @Published private(set) var stoppedByOwner = false

    init(socketPath: String = ZXVDLocation.defaultSocketPath) {
        self.socketPath = socketPath
        self.client = ZXVDClient(socketPath: socketPath)
    }

    func has(_ method: String) -> Bool { methods.contains(method) }

    var selected: Account? { accounts.first { $0.account == selectedAccount } }

    func name(of account: Int) -> String {
        accounts.first { $0.account == account }?.name ?? "Account \(account)"
    }

    // MARK: connection and polling

    func start() {
        guard poll == nil else { return }
        poll = Task { [weak self] in
            await self?.connect()
            while !Task.isCancelled {
                await self?.refresh()
                try? await Task.sleep(nanoseconds: 1_000_000_000)
            }
        }
        requestNotificationPermission()
    }

    func connect() async {
        stoppedByOwner = false
        link = .connecting
        let path = socketPath
        do {
            let (c, d, disc) = try await Task.detached { try Connector.connect(socket: path) }.value
            client = c
            if let d { daemon = d }
            methods = Set(disc.methods)
            version = try? await client.request("zxvd.version", as: Version.self)
            model = try? await client.request("model.info", as: ModelInfo.self)
            link = .connected
            await refreshLedger()
        } catch {
            link = .down(String(describing: error))
        }
    }

    func refresh() async {
        guard link == .connected else {
            if case .down = link, !stoppedByOwner { await connect() }
            return
        }
        do {
            state = try await client.request("swarm.state", as: SwarmState.self)
            let n = try await client.request("notes.list", as: Notes.self)
            notes = n.notes
            unread = n.unread
            NSApp?.dockTile.badgeLabel = n.unread > 0 ? String(n.unread) : nil
            postSystemNotifications(n.notes)
        } catch ZXVDError.noDaemon(let p) {
            link = .down(ZXVDError.noDaemon(p).description)
        } catch {
            // one missed poll is not a lost daemon; the next tick retries
        }
    }

    func stopDaemonIfOurs() {
        poll?.cancel()
        daemon?.stop()
        daemon = nil
    }

    // MARK: Local AI Studio

    func ask(_ prompt: String) async {
        let p = prompt.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !p.isEmpty, !asking else { return }
        chat.append(ChatLine(mine: true, text: p, tag: nil, charged: nil))
        asking = true
        defer { asking = false }
        do {
            // a real model on a slow CPU can take a while
            let a = try await client.request("swarm.ask", ["prompt": .string(String(p.prefix(4095)))],
                                             as: Answer.self, timeoutMs: 300_000)
            let s = try? await client.request("swarm.state", as: SwarmState.self)
            if let s { state = s }
            chat.append(ChatLine(mine: false, text: a.answer, tag: s?.companion.lastTag,
                                 charged: s?.companion.lastAnswer?.charged))
        } catch {
            chat.append(ChatLine(mine: false, text: "zxvd could not answer: \(error)", tag: nil, charged: nil))
        }
    }

    // MARK: Sicily Ledger

    func refreshLedger() async {
        guard let a = try? await client.request("ledger.accounts", as: Accounts.self) else { return }
        accounts = a.accounts
        for c in a.accounts.map(\.currency) where !currenciesEnabled.contains(c) {
            currenciesEnabled.append(c)
        }
        if selectedAccount == nil || !accounts.contains(where: { $0.account == selectedAccount }) {
            selectedAccount = accounts.first?.account
        }
        await loadSelected()
    }

    func select(_ account: Int) async {
        selectedAccount = account
        await loadSelected()
    }

    private func loadSelected() async {
        guard let id = selectedAccount else {
            balance = nil
            entries = []
            return
        }
        balance = try? await client.request("ledger.balance", ["account": .int(Int64(id))], as: Balance.self)
        entries = (try? await client.request("ledger.entries", ["account": .int(Int64(id))], as: Entries.self))?
            .entries ?? []
    }

    /// Returns the line shown under the transfer form.
    func post(to: Int, amount: String, reference: String) async -> (Bool, String) {
        guard let from = selected else { return (false, "Pick an account to send from.") }
        let minor: Int64
        switch Money.parse(amount, currency: from.currency) {
        case .success(let v): minor = v
        case .failure(.fraction(let n)):
            return (false, "Refused: \(from.currency) has \(n) decimal places. zxvd never rounds.")
        case .failure(.zero): return (false, "The amount must be at least one minor unit.")
        case .failure(.tooLarge): return (false, "That amount is too large.")
        case .failure: return (false, "Enter an amount like 125.00.")
        }
        let ref = reference.trimmingCharacters(in: .whitespaces)
        do {
            let p = try await client.request("ledger.post", [
                "from": .int(Int64(from.account)), "to": .int(Int64(to)), "amount_minor": Money.param(minor),
                "currency": .string(from.currency), "reference": .string(ref.isEmpty ? "Transfer" : String(ref.prefix(35))),
            ], as: Posted.self)
            await checkBooks()
            await loadSelected()
            return (true, "Posted as entry \(p.entryId) (\(minor) minor units). Books \(booksOK == true ? "balanced" : "not verified").")
        } catch {
            return (false, "Refused: \(error)")
        }
    }

    func openAccount(name: String, currency: String, kind: String) async -> (Bool, String) {
        let n = name.trimmingCharacters(in: .whitespaces)
        guard !n.isEmpty else { return (false, "Give the account a name.") }
        do {
            let o = try await client.request("ledger.open_account", [
                "name": .string(String(n.prefix(63))), "currency": .string(currency), "kind": .string(kind),
            ], as: Opened.self)
            await refreshLedger()
            await select(o.account)
            return (true, "Opened \(n).")
        } catch {
            return (false, "Refused: \(error)")
        }
    }

    func checkBooks() async {
        booksOK = (try? await client.request("ledger.check", as: Checked.self))?.ok
        booksCheckedAt = Date()
    }

    // MARK: P2P Mesh and Storefront

    @discardableResult
    func net(_ command: NetCommand) async -> Bool {
        let line: String
        do {
            line = try command.text()
        } catch {
            netLog.insert(LogLine(command: "(not sent)", result: Self.explain(error), ok: false), at: 0)
            return false
        }
        do {
            let r = try await client.request("net.command", ["command": .string(line)], as: NetResult.self)
            let ok = r.result != "refused" && r.result != "could not start"
            netLog.insert(LogLine(command: line, result: r.result, ok: ok), at: 0)
            state = try? await client.request("swarm.state", as: SwarmState.self)
            return ok
        } catch {
            netLog.insert(LogLine(command: line, result: "\(error)", ok: false), at: 0)
            return false
        }
    }

    static func explain(_ e: Error) -> String {
        switch e as? NetCommand.Invalid {
        case .peerAddress: return "Use an address like 192.168.1.20:8723."
        case .nodeID: return "The buyer's NodeID is 64 hex digits."
        case .zero: return "Units and price must be above zero."
        case nil: return "\(e)"
        }
    }

    // MARK: Settings

    func checkUpdate() async -> String {
        do {
            return try await client.request("update.check", as: UpdateResult.self, timeoutMs: 60_000).status
        } catch {
            return "\(error)"
        }
    }

    func markAllRead() async {
        _ = try? await client.request("notes.mark_read", as: Marked.self)
        await refresh()
    }

    func enableCurrency(_ code: String) async -> (Bool, String) {
        let c = code.trimmingCharacters(in: .whitespaces).uppercased()
        guard c.count >= 3, c.count <= 15, c.allSatisfy({ $0.isASCII && ($0.isLetter || $0.isNumber) }) else {
            return (false, "Use a code like GBP.")
        }
        do {
            let e = try await client.request("currency.enable", ["code": .string(c)], as: Enabled.self)
            if !currenciesEnabled.contains(e.enabled) { currenciesEnabled.append(e.enabled) }
            return (true, "\(e.enabled) enabled.")
        } catch {
            return (false, "Refused: \(error)")
        }
    }

    func shutdownDaemon() async {
        _ = try? await client.request("zxvd.shutdown", as: Stopping.self)
        daemon = nil
        stoppedByOwner = true
        link = .down("zxvd stopped")
    }

    // MARK: macOS notifications

    private var canNotify: Bool { Bundle.main.bundleIdentifier != nil }

    private func requestNotificationPermission() {
        guard canNotify else { return }
        UNUserNotificationCenter.current().requestAuthorization(options: [.alert, .badge]) { _, _ in }
    }

    private func postSystemNotifications(_ notes: [Note]) {
        let top = notes.map(\.seq).max() ?? 0
        guard primed else {
            // notes from before the app opened are in the window, not re-announced
            lastNotified = top
            primed = true
            return
        }
        let fresh = notes.filter { !$0.read && $0.seq > lastNotified }
        lastNotified = max(lastNotified, top)
        guard canNotify, !fresh.isEmpty, !NSApp.isActive else { return }
        for n in fresh.prefix(3) {
            let c = UNMutableNotificationContent()
            c.title = n.title
            c.body = n.body
            UNUserNotificationCenter.current().add(
                UNNotificationRequest(identifier: "zxv-note-\(n.seq)", content: c, trigger: nil))
        }
    }
}
