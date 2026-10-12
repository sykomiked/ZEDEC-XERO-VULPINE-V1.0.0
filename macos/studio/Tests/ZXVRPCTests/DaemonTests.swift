// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// The client against the real daemon. Set ZXVD_BIN to a zxvd built by
// kernel/arch/hosted/build_zxvd.sh; without it these tests are skipped.
// Every call the app's five views make is exercised here.
import Foundation
import XCTest

@testable import ZXVRPC

final class DaemonTests: XCTestCase {
    static var daemon: DaemonProcess?
    static var home: String = ""

    var client: ZXVDClient { ZXVDClient(socketPath: Self.daemon!.socketPath, timeoutMs: 30_000) }

    override class func setUp() {
        super.setUp()
        guard let bin = ProcessInfo.processInfo.environment["ZXVD_BIN"], !bin.isEmpty else { return }
        // a short path: sun_path holds about 100 bytes
        let dir = "/tmp/zxs-\(getpid())"
        try? FileManager.default.createDirectory(atPath: dir + "/models", withIntermediateDirectories: true)
        home = dir
        var env = ProcessInfo.processInfo.environment
        env["HOME"] = dir
        env["XDG_DATA_HOME"] = dir + "/xdg"
        daemon = try? DaemonProcess.start(binary: bin, socketPath: dir + "/run/zxvd.sock",
                                          modelsDir: dir + "/models", environment: env)
    }

    override class func tearDown() {
        daemon?.stop()
        daemon = nil
        if !home.isEmpty { try? FileManager.default.removeItem(atPath: home) }
        super.tearDown()
    }

    override func setUpWithError() throws {
        try XCTSkipIf(ProcessInfo.processInfo.environment["ZXVD_BIN"] == nil, "ZXVD_BIN not set")
        XCTAssertNotNil(Self.daemon, "zxvd did not start")
        try XCTSkipIf(Self.daemon == nil)
    }

    func testDiscoveryAndVersion() throws {
        let d = try client.call("rpc.discover", as: Discovery.self)
        XCTAssertEqual(d.protocol, "zxvd-rpc/1")
        for m in ["swarm.state", "swarm.ask", "model.info", "net.command", "ledger.post", "notes.list"] {
            XCTAssertTrue(d.methods.contains(m), m)
        }
        let v = try client.call("zxvd.version", as: Version.self)
        XCTAssertEqual(v.protocol, "zxvd-rpc/1")
        XCTAssertTrue(v.libzxv.hasPrefix("1."))
    }

    func testSwarmStateDecodesFully() throws {
        let s = try client.call("swarm.state", as: SwarmState.self)
        XCTAssertGreaterThanOrEqual(s.levels, 1)
        XCTAssertFalse(s.agents.isEmpty)
        XCTAssertEqual(s.agents.first?.name, "Companion")
        XCTAssertEqual(s.quest.skills.count, 8)
        XCTAssertEqual(s.net.mode, "off")
        XCTAssertEqual(s.ledgerHalt, 0)
        XCTAssertTrue(s.companion.cycleOpen || s.cycle >= 0)
    }

    func testModelNotesAndAsk() async throws {
        let m = try await client.request("model.info", as: ModelInfo.self)
        XCTAssertFalse(m.status.isEmpty)
        let n = try await client.request("notes.list", as: Notes.self)
        XCTAssertGreaterThanOrEqual(n.unread, 0)
        let a = try await client.request("swarm.ask", ["prompt": "fan_out(question) -> verify(answers)"], as: Answer.self)
        XCTAssertFalse(a.answer.isEmpty)
        _ = try client.call("notes.mark_read", as: Marked.self)
    }

    func testLedgerFlowAsTheLedgerViewDrivesIt() throws {
        let c = client
        _ = try c.call("currency.enable", ["code": "USD"], as: Enabled.self)
        let issuer = try c.call("ledger.open_account", ["name": "House issuer", "currency": "USD", "kind": "issuer"], as: Opened.self).account
        let ops = try c.call("ledger.open_account", ["name": "Operating", "currency": "USD", "kind": "holder"], as: Opened.self).account
        let sav = try c.call("ledger.open_account", ["name": "Savings", "currency": "USD", "kind": "holder"], as: Opened.self).account
        let amt = try Money.parse("1,500.00", currency: "USD").get()
        let p = try c.call("ledger.post", ["from": .int(Int64(issuer)), "to": .int(Int64(ops)), "amount_minor": Money.param(amt),
                                           "currency": "USD", "reference": "Opening balance"], as: Posted.self)
        XCTAssertNotNil(Int64(p.entryId))
        _ = try c.call("ledger.post", ["from": .int(Int64(ops)), "to": .int(Int64(sav)), "amount_minor": Money.param(26_000),
                                       "currency": "USD", "reference": "To savings"], as: Posted.self)
        let b = try c.call("ledger.balance", ["account": .int(Int64(ops))], as: Balance.self)
        XCTAssertNotNil(b.debit)
        let accts = try c.call("ledger.accounts", as: Accounts.self).accounts
        XCTAssertTrue(accts.contains { $0.name == "Savings" && $0.currency == "USD" })
        let e = try c.call("ledger.entries", ["account": .int(Int64(ops))], as: Entries.self).entries
        XCTAssertEqual(e.compactMap(\.signed).reduce(0, +), 150_000 - 26_000)
        XCTAssertTrue(try c.call("ledger.check", as: Checked.self).ok)

        // an overdraft is refused with the libzxv status, and nothing moves
        XCTAssertThrowsError(try c.call("ledger.post", ["from": .int(Int64(sav)), "to": .int(Int64(ops)),
                                                        "amount_minor": Money.param(9_999_999), "currency": "USD",
                                                        "reference": "Too much"], as: Posted.self)) { err in
            guard case ZXVDError.rpc(let r) = err else { return XCTFail("\(err)") }
            XCTAssertEqual(r.code, -32000)
            XCTAssertNotNil(r.statusName)
        }
        // a fraction is refused by the daemon too, never rounded
        XCTAssertThrowsError(try c.call("ledger.post", ["from": .int(Int64(ops)), "to": .int(Int64(sav)),
                                                        "amount_minor": .double(1.5), "currency": "USD",
                                                        "reference": "Half"], as: Posted.self))
        XCTAssertTrue(try c.call("ledger.check", as: Checked.self).ok)
    }

    func testNetCommandsTheMeshAndStorefrontSend() throws {
        let c = client
        let off = try c.call("net.command", ["command": .string(try NetCommand.mode(.off).text())], as: NetResult.self)
        XCTAssertFalse(off.result.isEmpty)
        let share = try c.call("net.command", ["command": .string(try NetCommand.share(compute: 0, storage: 0).text())], as: NetResult.self)
        XCTAssertFalse(share.result.isEmpty)
        let s = try c.call("swarm.state", as: SwarmState.self)
        XCTAssertEqual(s.net.mode, "off")
        XCTAssertFalse(s.net.bound)
    }

    func testTheStudioPanelsViewsDecode() throws {
        let c = client
        let d = try c.call("rpc.discover", as: Discovery.self)
        for m in ["net.peers", "net.offers", "player.card", "jurisdiction.get", "jurisdiction.set"] {
            XCTAssertTrue(d.methods.contains(m), m)
        }
        let p = try c.call("net.peers", as: Peers.self)
        XCTAssertEqual(p.count, 0)
        XCTAssertTrue(p.peers.isEmpty)
        let o = try c.call("net.offers", as: Offers.self)
        XCTAssertTrue(o.wants.isEmpty && o.outstanding.isEmpty)
        let card = try c.call("player.card", as: PlayerCard.self)
        XCTAssertEqual(card.metal, "Copper")
        XCTAssertEqual(card.next?.metal, "Bronze")
        XCTAssertEqual(card.inputs["bond"]?.value, "0")
        let j = try c.call("jurisdiction.get", as: Jurisdiction.self)
        XCTAssertEqual(j.market, "closed")
        XCTAssertTrue(j.inEffect)
        XCTAssertTrue(j.standards.contains("OHADA"))
        let s = try c.call("jurisdiction.set", ["standard": "IFRS", "country": "GH"], as: Jurisdiction.self)
        XCTAssertEqual(s.standard, "IFRS")
        XCTAssertEqual(s.country, "GH")
        XCTAssertThrowsError(try c.call("jurisdiction.set", ["country": "ghana"], as: Jurisdiction.self)) { e in
            guard case ZXVDError.rpc(let r) = e else { return XCTFail("\(e)") }
            XCTAssertEqual(r.code, -32602)
        }
    }

    func testTheCommonsRoundTrip() throws {
        let c = client
        let me = try c.call("chat.me", as: ChatMe.self)
        XCTAssertEqual(me.nodeId.count, 64)
        let post = try c.call("chat.post", ["text": "from Swift \u{1F680}"], as: ChatRecord.self)
        XCTAssertTrue(post.mine)
        XCTAssertEqual(post.kind, "post")
        XCTAssertEqual(post.text, "from Swift \u{1F680}")
        XCTAssertNil(post.replyTo)
        let reply = try c.call("chat.post", ["text": "a reply", "reply_to": .string(post.id)], as: ChatRecord.self)
        XCTAssertEqual(reply.replyTo, post.id)
        _ = try c.call("chat.react", ["to": .string(post.id), "emoji": "\u{1F44D}"], as: ChatRecord.self)
        let t = try c.call("chat.thread", ["id": .string(post.id)], as: ChatThread.self)
        XCTAssertEqual(t.root.replies, 1)
        XCTAssertEqual(t.root.reactions.map { "\($0.emoji) \($0.count) \($0.mine)" }, ["\u{1F44D} 1 true"])
        XCTAssertEqual(t.replies.map { $0.id }, [reply.id])
        let f = try c.call("chat.feed", as: ChatFeed.self)
        XCTAssertTrue(f.posts.contains { $0.id == post.id })
        XCTAssertFalse(f.posts.contains { $0.id == reply.id })
        let fol = try c.call("chat.follow", ["node_id": .string(String(repeating: "ab", count: 32))], as: ChatFollow.self)
        XCTAssertTrue(fol.following)
    }

    func testUnknownMethodIsTheJsonRpcError() {
        XCTAssertThrowsError(try client.call("ledger.holds")) { e in
            guard case ZXVDError.rpc(let r) = e else { return XCTFail("\(e)") }
            XCTAssertEqual(r.code, -32601)
        }
    }
}
