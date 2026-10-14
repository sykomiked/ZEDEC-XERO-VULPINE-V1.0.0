// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// The README's ABI 1.5 examples, verbatim, run against the real libzxv,
// with the results their comments claim checked.

import XCTest
import Zxv

/// Keeps the README ABI 1.5 examples compiling and true.
final class ReadmeSnippetTests: XCTestCase {
    func test_treaty() throws {
        let t = try Treaty(sutra: "TREATY CORRIDOR-7 USD NET\nPARTY ACME 5000 4000 1/2\n"
                                + "PARTY BETA 3000 3000 1/2\nOWE ACME BETA 1200\nEND\n")
        let same = try Treaty(record: try t.record())          // try same.isEqual(to: t) == true
        let r = try t.run()                                     // r.legs(), r.parties(), r.record()
        do { _ = try Treaty(sutra: "TREATY") } catch {
            if case .usage(let u) = error { print(u.treatyFault as Any) }   // .name at offset 0
        }
        let d = try Treaty.picDiv(-7, 2)                        // quotient -3, remainder -1
        _ = (same, r, d)
        XCTAssertTrue(try same.isEqual(to: t))
        XCTAssertEqual([d.quotient, d.remainder], [-3, -1])
    }
    func test_cid() throws {
        let c = try Cid.compute(Array("abc".utf8))              // CIDv1 raw sha2-256
        let text = try c.text()                                 // "bafkrei..."
        let back = try Cid(text: text)                          // == c
        try c.verify(Array("abc".utf8))                         // .refused(.cidMismatch) on other bytes
        let big = try Cid.unixfs([UInt8](repeating: 0, count: 300_000))   // dag-pb above 256 KiB
        _ = (back, big)
        XCTAssertEqual(back, c)
        XCTAssertTrue(text.hasPrefix("bafkrei"))
    }
    func test_aipi() throws {
        let ctx = try ZxvContext()
        let cap = try ctx.rootCapability(.all1_5)
        let node = try Aipi.Node(cap, storeBytes: 1 << 20, maxBlocks: 64)
        let module = try node.put(Array("zxv module bytes".utf8))
        let spec = Aipi.FunctionSpec(verify: .checker, fuelMax: 1_000_000, module: module,
                                     entry: "main", checker: "check")
        let fn = try node.publish(try Aipi.encodeFunction(spec))    // function CID
        let call = try Aipi.callCid(fn, input: Array("hello".utf8))
        let runsHere = try node.plan(fn) == .local                  // true: the module is held
        _ = (call, runsHere)
        XCTAssertTrue(runsHere)
    }
    func test_coin() throws {
        let rungs = try Coin.split(100)                             // [10, 5, 3]: 89 + 8 + 3
        let counts = try SwarmToken.canonical(value: 1000, octane: SwarmToken.Octane.midgrade)
        var buf = [UInt8](repeating: 0, count: 16), bit: UInt32 = 0
        try SwarmToken.codePut(&buf, bit: &bit, value: 100)         // bit == 11
        let ctx = try ZxvContext()
        let w = try SwarmToken.Wallet(try ctx.rootCapability(.mint), modelId: 7,
                                      octane: SwarmToken.Octane.regular)
        try w.mint(1000); try w.spend(1)                            // try w.info().value == 999
        _ = (rungs, counts)
        XCTAssertEqual(rungs, [10, 5, 3])
        XCTAssertEqual(bit, 11)
        XCTAssertEqual(try w.info().value, 999)
    }
    func test_fee() throws {
        let f = try Fee.assure(1_000_000)                           // 888
        var acct = Fee.Carry()                                      // store acct.remainder per account
        for _ in 0..<8 { _ = try acct.charge(100) }
        let parts = try Fee.split(f)                                // [445, 222, 133, 88]
        let policy = try Fee.defaultPolicy()
        _ = (parts, policy)
        XCTAssertEqual(f, 888)
        XCTAssertEqual(parts, [445, 222, 133, 88])
    }
    func test_vhub() throws {
        let ctx = try ZxvContext()
        let cap = try ctx.rootCapability(.all1_5)
        let dsa = MlDsa87(cap)
        let key = try dsa.keygen()
        let hub = try VHub.Hub(cap)
        try hub.addCurrency(slot: 0, code: "ZTA", minorUnits: 2, issuerPublicKey: key.publicKey,
                            poolFunding: 500_000, vbillBacking: 10_000)
        let msg = try VHub.pegMessage(slot: 0, num: 350, den: 1, epoch: 1)
        try hub.setPeg(slot: 0, num: 350, den: 1, epoch: 1, signature: try dsa.sign(msg.bytes, with: key.secretKey))
        let acct = try hub.openAccount(slot: 0, owner: 50)
        try hub.issue(account: acct, amount: 1_000_000)
        let postings = try hub.check()                              // A = L + E, DEBIT == CREDIT, hash chain
        _ = postings
        XCTAssertGreaterThan(postings, 0)
    }
    func test_freight() throws {
        let payload = Array("any k rows rebuild the payload".utf8)
        let h = try Freight.Header.forPayload(freightId: 42, k: 10, payload: payload)
        let packets = try Freight.encode(h, payload: payload)       // 3528 bytes
        let back = try Freight.decode(h, packets: Array(packets.suffix(210)))   // == payload
        let ops = try Freight.OpsWriter(declaredTotal: 5, maxBytes: 4096)
            .literal(Array("<".utf8)).cidref(try Cid.compute(Array("abc".utf8)), length: 3)
            .literal(Array(">".utf8)).finish()
        let out = try Freight.expand(ops, limit: 1024) { _, _ in Array("abc".utf8) }   // "<abc>"
        _ = (back, out)
        XCTAssertEqual(packets.count, 3528)
        XCTAssertEqual(back, payload)
        XCTAssertEqual(out, Array("<abc>".utf8))
    }
    func test_web4() throws {
        let ctx = try ZxvContext()
        let cap = try ctx.rootCapability(.all1_5)
        let now: UInt64 = 1_700_000_000_000
        let alice = try Web4.Agent(cap, name: "alice", nowMs: now)
        let human = try Web4.Agent(cap, name: "human", nowMs: now)
        let m = try Web4.Manifest(version: 1, issuedMs: now, expiresMs: now + 86_400_000)
        try m.addTool(Web4.Tool(name: "pay", priceVfv: 150, ratePerMin: 60, burst: 10, flags: .money, maxInput: 256))
        try m.sign(with: alice)
        let id = try alice.agentId()
        let action = try Web4.actionDigest(tool: "pay", from: id, to: id, requestId: 7, payload: Array("amount=100".utf8))
        let token = try human.issueConsent(agent: id, action: action, scope: .money, maxVfv: 1000,
                                           issuedMs: now, expiresMs: now + 600_000)
        let log = try Web4.ConsentLog(capacity: 16)
        try log.check(token, humanPublicKey: try human.publicKey(), agent: id, action: action,
                      need: .money, amount: 500, nowMs: now + 1)
        // A second check of the same token throws .refused(.consentDenied): single use.
        do {
            try log.check(token, humanPublicKey: try human.publicKey(), agent: id, action: action,
                          need: .money, amount: 500, nowMs: now + 2)
            XCTFail("consent reused")
        } catch {
            XCTAssertEqual(error.zxv?.statusName, "ZXV_E_CONSENT")
        }
    }
    func test_paradox() throws {
        let ctx = try ZxvContext()
        let cap = try ctx.rootCapability(.all1_5)
        let book = try Paradox.Book(cap, logCapacity: 64, registryCapacity: 16)
        let c = try book.classify(Hexagram.all[39], evidence: Paradox.Evidence(pos0: 3, neg0: 1, pos1: 5, neg1: 1))
        let policy = try Paradox.Policy(cap, lower: [0, 0, 10], upper: [100, 50, 20], initial: [50, 25, 15])
        let o = try book.constructive(policy, kind: c.kind, tag: c.tag, synth: [60, 20, 12], tension: 5)
        var state = [UInt8](repeating: UInt8(ascii: "X"), count: 16)
        let k = try book.destructive(kind: .destructive, tag: Hexagram.all[35], state: &state,
                                     checkpoint: [UInt8](repeating: UInt8(ascii: "A"), count: 16),
                                     containment: Paradox.Containment(steps: 10, stepLimit: 8, spent: 3, budget: 100,
                                                                      trace: [1, 2, 3, 2], invariantOk: true,
                                                                      pattern: Array("loop".utf8), submitter: 7))
        let ok = try k.proof?.verify() ?? false                     // state is "AAAA..." again
        _ = (o, ok)
        XCTAssertTrue(ok)
        XCTAssertEqual(state, [UInt8](repeating: UInt8(ascii: "A"), count: 16))
    }
}
