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

    func testPhonePairingDecodes() throws {
        let off = try client.call("phone.list", as: PhoneList.self)
        XCTAssertFalse(off.on)
        let on = try client.call("phone.link", ["on": true, "port": 0, "bind": "127.0.0.1"], as: PhoneLink.self)
        XCTAssertTrue(on.on)
        XCTAssertGreaterThan(on.port, 0)
        let p = try client.call("phone.pair", ["name": "Test phone"], as: PhonePairing.self)
        XCTAssertEqual(p.port, on.port)
        XCTAssertFalse(p.allowPost)
        XCTAssertTrue(p.link.hasPrefix("zxv-phone:1?"))
        XCTAssertTrue(p.link.contains("id=\(p.phoneId)"))
        let l = try client.call("phone.list", as: PhoneList.self)
        XCTAssertTrue(l.phones.contains { $0.phoneId == p.phoneId && $0.name == "Test phone" && $0.connected == 0 })
        let r = try client.call("phone.revoke", ["phone_id": .string(p.phoneId)], as: Revoked.self)
        XCTAssertTrue(r.revoked)
        XCTAssertFalse(try client.call("phone.link", ["on": false], as: PhoneLink.self).on)
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

    func testEscrowAsTheStorefrontDrivesIt() throws {
        let c = client
        XCTAssertTrue(try c.call("rpc.discover", as: Discovery.self).methods.contains("ledger.holds"))
        // escrow holds sit behind the "escrow" switch (zxvd_pay.h P6), off at start
        _ = try c.call("features.set", ["feature": "escrow", "enabled": true])
        _ = try c.call("currency.enable", ["code": "GHS"], as: Enabled.self)
        let bank = try c.call("ledger.open_account", ["name": "GHS bank", "currency": "GHS", "kind": "issuer"], as: Opened.self).account
        let buyer = try c.call("ledger.open_account", ["name": "Buyer", "currency": "GHS"], as: Opened.self).account
        let seller = try c.call("ledger.open_account", ["name": "Seller", "currency": "GHS"], as: Opened.self).account
        _ = try c.call("ledger.post", ["from": .int(Int64(bank)), "to": .int(Int64(buyer)), "amount_minor": Money.param(10_000),
                                       "currency": "GHS", "reference": "Escrow funding"], as: Posted.self)
        let amt = try Money.parse("40.00", currency: "GHS").get()
        let h = try c.call("ledger.hold", ["from": .int(Int64(buyer)), "to": .int(Int64(seller)), "amount_minor": Money.param(amt)],
                           as: Hold.self)
        XCTAssertTrue(h.isHeld)
        XCTAssertEqual(h.amount, 4_000)
        XCTAssertEqual(h.currency, "GHS")
        XCTAssertNil(h.closedEntryId)
        XCTAssertEqual(h.releasesOn, "owner")
        var all = try c.call("ledger.holds", as: Holds.self)
        XCTAssertEqual(all.holds.first { $0.hold == h.hold }, h)
        let esc = try XCTUnwrap(all.escrowAccounts.first { $0.account == h.escrowAccount })
        XCTAssertEqual(esc.heldMinor, "4000")
        XCTAssertTrue(esc.consistent)
        XCTAssertEqual(all.unmatched, 0)

        let r = try c.call("ledger.release", ["hold": .int(Int64(h.hold))], as: Hold.self)
        XCTAssertEqual(r.state, "released")
        XCTAssertNotNil(r.closedEntryId)
        XCTAssertEqual(try c.call("ledger.balance", ["account": .int(Int64(seller))], as: Balance.self).equity, 4_000)
        XCTAssertThrowsError(try c.call("ledger.refund", ["hold": .int(Int64(h.hold))], as: Hold.self)) { e in
            guard case ZXVDError.rpc(let err) = e else { return XCTFail("\(e)") }
            XCTAssertEqual(err.code, -32020)
            XCTAssertEqual(err.message, "hold \(h.hold) is already released")
        }

        let h2 = try c.call("ledger.hold", ["from": .int(Int64(buyer)), "to": .int(Int64(seller)), "amount_minor": "2500"],
                            as: Hold.self)
        XCTAssertGreaterThan(h2.hold, h.hold)
        let back = try c.call("ledger.refund", ["hold": .int(Int64(h2.hold))], as: Hold.self)
        XCTAssertEqual(back.state, "refunded")
        XCTAssertEqual(try c.call("ledger.balance", ["account": .int(Int64(buyer))], as: Balance.self).equity, 6_000)
        // more than the payer has: the libzxv status, and nothing moves
        XCTAssertThrowsError(try c.call("ledger.hold", ["from": .int(Int64(buyer)), "to": .int(Int64(seller)),
                                                        "amount_minor": Money.param(6_001)], as: Hold.self)) { e in
            guard case ZXVDError.rpc(let err) = e else { return XCTFail("\(e)") }
            XCTAssertEqual(err.code, -32000)
            XCTAssertEqual(err.statusName, "ZXV_E_FUNDS")
        }
        all = try c.call("ledger.holds", as: Holds.self)
        XCTAssertTrue(all.escrowAccounts.allSatisfy(\.consistent))
        XCTAssertTrue(try c.call("ledger.check", as: Checked.self).ok)
    }

    func testUnknownMethodIsTheJsonRpcError() {
        XCTAssertThrowsError(try client.call("ledger.no_such_method")) { e in
            guard case ZXVDError.rpc(let r) = e else { return XCTFail("\(e)") }
            XCTAssertEqual(r.code, -32601)
        }
    }
}
