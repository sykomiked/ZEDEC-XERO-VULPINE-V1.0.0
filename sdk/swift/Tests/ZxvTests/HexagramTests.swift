// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// ABI 1.3: the 64-state hexagram tag and quarantine log against
// vectors.json "hex".

import CZxv
import XCTest
@testable import Zxv

private func statusName(_ body: () throws -> Void) -> String? {
    do { try body() } catch { return error.zxv?.statusName }
    return nil
}

final class HexagramTests: XCTestCase {
    private let hx = vectors.obj("hex")

    func testTagsMatchVectors() throws {
        XCTAssertTrue(ZxvLibrary.features.contains(.hex))
        XCTAssertEqual(HexBase.allCases.map(\.description), hx.arr("base_names") as! [String])
        XCTAssertEqual(HexPolarity.allCases.map(\.description), hx.arr("polarity_names") as! [String])
        XCTAssertEqual(HexPhase.allCases.map(\.description), hx.arr("phase_names") as! [String])
        XCTAssertEqual(HexAction.allCases.map(\.description), hx.arr("action_names") as! [String])
        for case let v as [String: Any] in hx.arr("tags") {
            let h = try Hexagram(tag: UInt8(v.uint("tag")))
            XCTAssertEqual(h.name, v.str("name"))
            XCTAssertEqual(Int64(h.base.rawValue), v.int("base"))
            XCTAssertEqual(Int64(h.polarity.rawValue), v.int("polarity"))
            XCTAssertEqual(Int64(h.phase.rawValue), v.int("phase"))
            XCTAssertEqual(Int64(h.action.rawValue), v.int("action"))
            XCTAssertEqual(Hexagram(base: h.base, polarity: h.polarity, phase: h.phase), h)
            XCTAssertEqual(Hexagram.all[Int(h.tag)], h)
        }
        let next = ints(hx["phase_next"]!)
        for p in HexPhase.allCases { XCTAssertEqual(Int32(p.next.rawValue), next[Int(p.rawValue)]) }
        XCTAssertEqual(Hexagram.all.count, Hexagram.states)
    }

    func testLogicTables() {
        let notT = ints(hx["not"]!), andT = hx.arr("and").map(ints), orT = hx.arr("or").map(ints)
        for a in Hexagram.all {
            XCTAssertEqual(Int32((~a).tag), notT[Int(a.tag)])
            for b in Hexagram.all {
                XCTAssertEqual(Int32((a & b).tag), andT[Int(a.tag)][Int(b.tag)])
                XCTAssertEqual(Int32(a.or(b).tag), orT[Int(a.tag)][Int(b.tag)])
            }
        }
    }

    func testStepAndWith() throws {
        let stepT = hx.arr("step").map(ints), setT = hx.arr("set").map(ints)
        for h in Hexagram.all {
            for p in HexPhase.allCases {
                let want = stepT[Int(h.tag)][Int(p.rawValue)]
                if want < 0 {
                    XCTAssertEqual(statusName { _ = try h.step(to: p) }, "ZXV_E_STATE")
                } else {
                    XCTAssertEqual(Int32(try h.step(to: p).tag), want)
                }
            }
            for b in HexBase.allCases {
                for pol in HexPolarity.allCases {
                    let want = setT[Int(h.tag)][Int(b.rawValue) * 4 + Int(pol.rawValue)]
                    if want < 0 {
                        XCTAssertEqual(statusName { _ = try h.with(base: b, polarity: pol) }, "ZXV_E_STATE")
                    } else {
                        XCTAssertEqual(Int32(try h.with(base: b, polarity: pol).tag), want)
                    }
                }
            }
            XCTAssertEqual(h.forbid(), Hexagram(base: h.base, polarity: .forbidden, phase: h.phase))
            XCTAssertEqual(h.advance().phase, h.phase.next)
        }
    }

    func testPolarityAndTruthBridge() {
        for case let c as [String: Any] in hx.arr("polarity") {
            let p = Hexagram.polarity(pos0: u32(c["pos0"]!), neg0: u32(c["neg0"]!), pos1: u32(c["pos1"]!),
                                      neg1: u32(c["neg1"]!), stale: c["stale"] as! Bool)
            XCTAssertEqual(Int64(p.rawValue), c.int("polarity"))
        }
        let lpToBase = ints(hx["lp_to_base"]!), baseToLp = ints(hx["base_to_lp"]!)
        for t in Truth.allCases {
            let h = Hexagram(truth: t, polarity: .neutral, phase: .potential)
            XCTAssertEqual(Int32(h.base.rawValue), lpToBase[Int(t.rawValue)])
            XCTAssertEqual(h.toTruth(), t)
        }
        for b in HexBase.allCases {
            XCTAssertEqual(Hexagram(base: b, polarity: .neutral, phase: .potential).toTruth().cValue,
                           baseToLp[Int(b.rawValue)])
        }
        // The numbering differs on purpose: Truth.true is 1, HexBase.true is 3.
        XCTAssertEqual(Truth.true.rawValue, 1)
        XCTAssertEqual(HexBase.true.rawValue, 3)
    }

    func testRefusalsAndValueSemantics() throws {
        for bad: UInt8 in [64, 100, 255] {
            XCTAssertEqual(statusName { _ = try Hexagram(tag: bad) }, "ZXV_E_ARG")
        }
        let top = Hexagram(base: .true, polarity: .negentropic, phase: .manifest)
        XCTAssertEqual(top.tag, 63)
        XCTAssertEqual(top.debugDescription, "Hexagram(true/negentropic/manifest, tag=63)")
        XCTAssertEqual("\(top)", "true/negentropic/manifest")
        var f = Hexagram(base: .true, polarity: .forbidden, phase: .potential)
        XCTAssertEqual(f.action, .rollback)
        for _ in 0..<4 {
            f = f.advance()
            XCTAssertTrue(f.isForbidden)
        }
        XCTAssertTrue((f & top).isForbidden && (f | top).isForbidden && (~f).isForbidden)
        XCTAssertThrowsError(try f.with(base: .true, polarity: .neutral))
        XCTAssertEqual(try f.with(base: .false, polarity: .forbidden).base, .false)
    }

    func testQuarantineChainMatchesVectors() throws {
        let q = hx.obj("qlog")
        let ctx = try ZxvContext()
        let cap = try ctx.rootCapability()
        let log = try QuarantineLog(cap, capacity: 3)
        XCTAssertEqual(try log.count(), 0)
        var prev = [UInt8](repeating: 0, count: 32)
        XCTAssertEqual(try log.head(), prev)
        for case let r as [String: Any] in q.arr("records") {
            let subject = hex(r.str("subject"))
            let tag = try Hexagram(tag: UInt8(r.uint("tag")))
            let seq = try log.append(tag, reason: UInt8(r.uint("reason")), submitter: UInt32(r.uint("submitter")),
                                     subject: subject)
            XCTAssertEqual(seq, r.uint("seq"))
            let rec = try log.record(at: UInt32(seq))
            XCTAssertEqual(rec.hash, hex(r.str("hash")))
            XCTAssertEqual(rec.prev, prev)
            XCTAssertEqual(rec.subject, subject)
            XCTAssertEqual(rec.tag, tag)
            XCTAssertEqual(UInt64(rec.reason), r.uint("reason"))
            XCTAssertEqual(UInt64(rec.submitter), r.uint("submitter"))
            prev = rec.hash
        }
        XCTAssertEqual(try log.head(), hex(q.str("head")))
        XCTAssertEqual(try log.records().map(\.seq), [0, 1, 2])
        try log.verify()
        XCTAssertEqual(statusName {
            try log.append(Hexagram(tag: 0x3B), reason: 0, submitter: 0, subject: [UInt8](repeating: 0, count: 32))
        }, "ZXV_E_CAPACITY")
    }

    func testQuarantineRefusals() throws {
        let zero = [UInt8](repeating: 0, count: 32)
        let ctx = try ZxvContext()
        let cap = try ctx.rootCapability()
        let noAttest = try cap.attenuate(CapabilitySet.all.subtracting(.attest))
        XCTAssertEqual(statusName { _ = try QuarantineLog(noAttest, capacity: 4) }, "ZXV_E_CAP")
        XCTAssertEqual(statusName { _ = try QuarantineLog(cap, capacity: 0) }, "ZXV_E_ARG")
        XCTAssertEqual(statusName { _ = try QuarantineLog(cap, capacity: QuarantineLog.maxRecords + 1) }, "ZXV_E_ARG")
        let log = try QuarantineLog(cap, capacity: 4)
        let plain = Hexagram(base: .true, polarity: .neutral, phase: .emerging)
        XCTAssertEqual(statusName { try log.append(plain, reason: 1, submitter: 1, subject: zero) }, "ZXV_E_ARG")
        let forb = Hexagram(base: .both, polarity: .forbidden, phase: .emerging)
        XCTAssertEqual(statusName { try log.append(forb, reason: 1, submitter: 1, subject: [0]) }, "ZXV_E_ARG")
        XCTAssertEqual(try log.append(forb, reason: 9, submitter: .max, subject: zero), 0)
        XCTAssertEqual(try log.record(at: 0).submitter, .max)
        try ctx.halt("test")
        do {
            _ = try log.count()
            XCTFail("expected fatal")
        } catch .fatal {
        } catch {
            XCTFail("\(error)")
        }
    }
}
