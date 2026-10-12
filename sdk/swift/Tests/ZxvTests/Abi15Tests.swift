// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// ABI 1.5: treaty, cid/aipi, coin/token, fee, vhub, freight, web4 and
// paradox against the real libzxv and sdk/conformance/vectors.json, plus
// closed-handle refusals, the buffer-growth path, error mapping, capability
// refusals and a poisoned context. Mirrors sdk/cpp/tests/test_zxv_abi15.cpp.
// 64-bit values in the vectors are decimal strings.

import CZxv
import Foundation
import XCTest
@testable import Zxv

private typealias J = [String: Any]

private func sec(_ k: String) -> J { vectors[k] as! J }
private func objs(_ x: Any?) -> [J] { (x as! [Any]).map { $0 as! J } }
private func list(_ x: Any?) -> [Any] { x as! [Any] }
private func S(_ x: Any?) -> String { x as! String }
private func isNull(_ x: Any?) -> Bool { x == nil || x is NSNull }
/// A decimal string or a JSON number.
private func U(_ x: Any?) -> UInt64 {
    if let s = x as? String { return UInt64(s)! }
    if let n = x as? NSNumber { return n.uint64Value }
    if let i = x as? Int { return UInt64(i) }
    fatalError("not an unsigned number: \(String(describing: x))")
}
private func I(_ x: Any?) -> Int64 {
    if let s = x as? String { return Int64(s)! }
    if let n = x as? NSNumber { return n.int64Value }
    if let i = x as? Int { return Int64(i) }
    fatalError("not a number: \(String(describing: x))")
}
private func B(_ x: Any?) -> Bool {
    if let b = x as? Bool { return b }
    return (x as! NSNumber).boolValue
}
private func U32(_ x: Any?) -> UInt32 { UInt32(U(x)) }
private func I32(_ x: Any?) -> Int32 { Int32(I(x)) }
private func U8(_ x: Any?) -> UInt8 { UInt8(U(x)) }
private func hx(_ b: [UInt8]) -> String { hexString(b) }
private func utf8(_ s: String) -> [UInt8] { Array(s.utf8) }
private func ramp(_ n: Int) -> [UInt8] { (0..<n).map { UInt8($0 & 255) } }
private func fill32(_ b: UInt8) -> [UInt8] { [UInt8](repeating: b, count: 32) }
private func tag(_ t: Any?) -> Hexagram { Hexagram.all[Int(U(t))] }
private func kind(_ k: Any?) -> Paradox.Kind { Paradox.Kind(rawValue: I32(k))! }

/// The error `body` throws, or nil.
private func zerr(_ body: () throws -> Void) -> ZxvError? {
    do {
        try body()
    } catch {
        guard let z = error.zxv else {
            XCTFail("non-zxv error: \(error)")
            return nil
        }
        return z
    }
    return nil
}

/// "ZXV_OK" when `body` returns, else the thrown status name.
private func st(_ body: () throws -> Void) -> String { zerr(body)?.statusName ?? "ZXV_OK" }

final class Abi15Tests: XCTestCase {
    // MARK: status_abi15, features2, caps

    func testStatusAbi15() throws {
        XCTAssertGreaterThanOrEqual(ZxvLibrary.version.minor, 5)
        try ZxvLibrary.ensureCompatible()
        XCTAssertTrue(ZxvLibrary.features2.isSuperset(of: .all1_5))
        XCTAssertTrue(ZxvLibrary.features2.contains(.paradox))
        let new: CapabilitySet = [.aipi, .mint, .paradox]
        XCTAssertEqual(CapabilitySet.all1_5.intersection(new), new)
        XCTAssertTrue(CapabilitySet.all1_5.isSuperset(of: .all1_4))
        XCTAssertEqual(CapabilitySet.all1_5.rawValue, (1 << 15) - 1)

        for s in objs(vectors["status_abi15"]) {
            let code = I32(s["code"]), name = S(s["name"]), cls = I(s["class"])
            XCTAssertEqual(ZxvLibrary.statusName(code), name)
            XCTAssertEqual(Int64(StatusClass(status: code).rawValue), cls)
            switch ZxvError(status: code, detail: "probe") {
            case .usage(let e): XCTAssertTrue(cls == 1 && e.status == code && e.statusName == name)
            case .refused(let e): XCTAssertTrue(cls == 2 && e.status == code && e.detail == "probe")
            default: XCTFail("\(name) mapped to the wrong class")
            }
        }
        // Each new status is recognisable under its class.
        XCTAssertTrue(ZxvUsageError(status: ZXV_E_DENOM, detail: "").isDenomination)
        XCTAssertTrue(ZxvUsageError(status: ZXV_E_TREATY, detail: "").isTreaty)
        let reasons: [(Int32, ZxvRefusedError.Reason)] = [
            (ZXV_E_CID, .cidMismatch), (ZXV_E_CONSENT, .consentDenied), (ZXV_E_ERASURE, .erasure),
            (ZXV_E_PEG, .pegRefused), (ZXV_E_LIMIT, .limitRefused), (ZXV_E_PARADOX_STEP, .paradoxStepLimit),
            (ZXV_E_PARADOX_OWNER, .ownerApprovalRequired),
        ]
        for (code, reason) in reasons {
            XCTAssertEqual(ZxvError(status: code, detail: "").refusal, reason)
        }
        // The mapping is reached through real calls too.
        if case .usage(let e)? = zerr({ _ = try Coin.rung(face: 4) }) { XCTAssertTrue(e.isDenomination) } else { XCTFail() }
        if case .usage(let e)? = zerr({ _ = try Treaty(sutra: "TREATY") }) {
            XCTAssertTrue(e.isTreaty)
            XCTAssertNotNil(e.treatyFault)
        } else { XCTFail() }
        XCTAssertEqual(zerr({ _ = try Cid(text: "z") })?.refusal, .cidMismatch)
    }

    // MARK: treaty

    func testTreaty() throws {
        let t = sec("treaty")
        XCTAssertEqual(Treaty.recordBytes, Int(U(t["rec_len"])))
        XCTAssertEqual(Treaty.resultRecordBytes, Int(U(t["result_len"])))
        for c in objs(t["sutra"]) {
            let a = try Treaty(sutra: S(c["sutra"]))
            XCTAssertTrue(try a.isValid())
            XCTAssertEqual(hx(try a.record()), S(c["record"]))
            XCTAssertEqual(try a.sutra(), S(c["reemit"]))
            XCTAssertEqual(try a.name(), S(c["name"]))
            XCTAssertEqual(try a.asset(), S(c["asset"]))
            XCTAssertEqual(Int64(try a.op().rawValue), I(c["op"]))
            XCTAssertEqual(try a.partyCount(), U32(c["party_count"]))
            XCTAssertEqual(try a.obligationCount(), U32(c["owe_count"]))
            let ps = try a.parties(), want = objs(c["parties"])
            XCTAssertEqual(ps.count, want.count)
            for (p, w) in zip(ps, want) {
                let wt = list(w["weight"])
                XCTAssertEqual(p, Treaty.Party(id: S(w["id"]), deposit: I(w["deposit"]), claim: I(w["claim"]),
                                               weightNum: U32(wt[0]), weightDen: U32(wt[1])))
            }
            let os = try a.obligations(), wo = objs(c["owes"])
            XCTAssertEqual(os.count, wo.count)
            for (o, w) in zip(os, wo) {
                XCTAssertEqual(o, Treaty.Obligation(from: U32(w["from"]), to: U32(w["to"]), amount: I(w["amount"])))
            }
            let b = try Treaty(record: hex(S(c["record"])))
            XCTAssertEqual(try a.isEqual(to: b), B(c["record_parses_equal"]))
            XCTAssertEqual(try b.sutra(), S(c["reemit"]))
        }
        for c in objs(t["reject"]) {
            let e = zerr { _ = try Treaty(sutra: S(c["sutra"])) }
            XCTAssertEqual(e?.statusName ?? "ZXV_OK", S(c["status"]))
            if case .usage(let u)? = e {
                XCTAssertEqual(u.treatyFault?.code, I32(c["fault"]))
                XCTAssertEqual(u.treatyFault?.offset, U32(c["offset"]))
                XCTAssertEqual(u.treatyFault?.fault?.rawValue, I32(c["fault"]))
            } else { XCTFail("no treaty fault") }
        }
        let base = hex(S(objs(t["sutra"])[0]["record"]))
        for c in objs(t["reject_record"]) {
            var r = base
            r[Int(U(c["patch_offset"]))] = hex(S(c["patch_byte"]))[0]
            let e = zerr { _ = try Treaty(record: r) }
            XCTAssertEqual(e?.statusName ?? "ZXV_OK", S(c["status"]))
            if case .usage(let u)? = e, u.isTreaty {
                XCTAssertEqual(u.treatyFault, Treaty.FaultInfo(code: I32(c["fault"]), offset: U32(c["offset"])))
            }
            // A rejected parse leaves the handle empty but usable.
            let x = try Treaty()
            XCTAssertNotNil(zerr { try x.parseRecord(r) })
            XCTAssertFalse(try x.isValid())
        }
        for c in objs(t["pic"]) {
            let a = I(c["a"]), b = I(c["b"]), want = S(c["status"])
            switch S(c["op"]) {
            case "div":
                XCTAssertEqual(st { _ = try Treaty.picDiv(a, b) }, want)
                if want == "ZXV_OK" {
                    XCTAssertEqual(try Treaty.picDiv(a, b), Treaty.Division(quotient: I(c["out"]), remainder: I(c["rem"])))
                }
            case let op:
                func f() throws -> Int64 {
                    switch op {
                    case "add": return try Treaty.picAdd(a, b)
                    case "sub": return try Treaty.picSub(a, b)
                    default: return try Treaty.picMul(a, b)
                    }
                }
                XCTAssertEqual(st { _ = try f() }, want)
                if want == "ZXV_OK" { XCTAssertEqual(try f(), I(c["out"])) }
            }
        }
        for c in objs(t["run"]) {
            let tt = try Treaty(sutra: S(c["sutra"]))
            XCTAssertEqual(st { _ = try tt.run() }, S(c["status"]))
            if S(c["status"]) != "ZXV_OK" { continue }
            let r = try tt.run()
            XCTAssertEqual(try r.partyCount(), U32(c["parties"]))
            XCTAssertEqual(try r.legCount(), U32(c["legs"]))
            XCTAssertEqual(try r.fees(), I(c["fees"]))
            if c["party"] != nil {
                let ps = try r.parties(), want = objs(c["party"])
                XCTAssertEqual(ps.count, want.count)
                for (p, w) in zip(ps, want) {
                    XCTAssertEqual(p, Treaty.PartyResult(net: I(w["net"]), balance: I(w["balance"]), dividend: I(w["dividend"])))
                }
            }
            if c["leg"] != nil {
                let ls = try r.legs(), want = objs(c["leg"])
                XCTAssertEqual(ls.count, want.count)
                for (l, w) in zip(ls, want) {
                    XCTAssertEqual(l, Treaty.Leg(from: U32(w["from"]), to: U32(w["to"]), amount: I(w["amount"])))
                }
            }
            XCTAssertEqual(hx(try r.record()), S(c["result_record"]))
            XCTAssertEqual(try r.record().count, Treaty.resultRecordBytes)
        }
    }

    // MARK: cid

    func testCid() throws {
        let c = sec("cid")
        for e in objs(c["compute"]) {
            let x = try Cid.compute(utf8(S(e["utf8"])), codec: U32(e["codec"]))
            XCTAssertEqual(hx(x.bytes), S(e["cid"]))
            XCTAssertEqual(try x.text(), S(e["text_default"]))
            XCTAssertEqual(try x.text(base: .base58btc), S(e["text_58btc"]))
            let i = try x.info()
            XCTAssertTrue(i.version == U32(e["version"]) && i.hash == U32(e["hash"]) && i.codec == U32(e["codec"]))
            XCTAssertEqual(hx(i.digest), S(e["digest"]))
            XCTAssertEqual(try Cid(text: S(e["text_default"])), x)
            XCTAssertEqual(try Cid(text: S(e["text_58btc"])), x)
            XCTAssertTrue(try x.matches(utf8(S(e["utf8"]))))
            XCTAssertFalse(try x.matches(utf8(S(e["utf8"]) + "!")))
        }
        for e in objs(c["unixfs"]) {
            let input = e["ramp"] != nil ? ramp(Int(U(e["len"]))) : utf8(S(e["utf8"]))
            let x = try Cid.unixfs(input)
            XCTAssertEqual(hx(x.bytes), S(e["cid"]))
            XCTAssertEqual(try x.text(), S(e["text_default"]))
            if e["codec"] != nil { XCTAssertEqual(try x.info().codec, U32(e["codec"])) }
        }
        for e in objs(c["text"]) {
            let x = try Cid(text: S(e["text"]))
            XCTAssertEqual(hx(x.bytes), S(e["cid"]))
            XCTAssertEqual(try x.text(), S(e["text_default"]))
            let t58 = S(e["text_58btc"])
            if t58.hasPrefix("ZXV_E_") { XCTAssertEqual(st { _ = try x.text(base: .base58btc) }, t58) }
            else { XCTAssertEqual(try x.text(base: .base58btc), t58) }
            let i = try x.info()
            XCTAssertTrue(i.version == U32(e["version"]) && i.codec == U32(e["codec"]) && i.hash == U32(e["hash"]))
            XCTAssertEqual(hx(i.digest), S(e["digest"]))
            XCTAssertEqual(hx(try x.toV1().bytes), S(e["v1"]))
            XCTAssertEqual(try x.toV1().text(), S(e["v1_text"]))
            XCTAssertEqual(hx(try Aipi.providerKey(x)), S(e["provider_key"]))
            XCTAssertEqual(x.description, S(e["text_default"]))
        }
        for e in objs(c["text_reject"]) { XCTAssertEqual(st { _ = try Cid(text: S(e["text"])) }, S(e["status"])) }
        for e in objs(c["verify"]) {
            XCTAssertEqual(st { try Cid(bytes: hex(S(e["cid"]))).verify(utf8(S(e["utf8"]))) }, S(e["status"]))
        }
        XCTAssertEqual(st { _ = try Cid(bytes: [UInt8](repeating: 1, count: Cid.maxBytes + 1)) }, "ZXV_E_CID")
        XCTAssertEqual(st { _ = try Cid(text: "") }, "ZXV_E_ARG")
        XCTAssertEqual(st { _ = try Cid(text: "b\u{0}x") }, "ZXV_E_ARG")
    }

    // MARK: aipi

    private func aipiRef(_ ref: String) throws -> Cid {
        let a = sec("aipi")
        if ref == "module_cid" { return try Cid(bytes: hex(S(a["module_cid"]))) }
        // "fn[i].fn_cid" or "call[i].call_cid"
        let lb = ref.firstIndex(of: "[")!, rb = ref.firstIndex(of: "]")!
        let name = String(ref[..<lb]), idx = Int(ref[ref.index(after: lb)..<rb])!
        let field = String(ref[ref.index(rb, offsetBy: 2)...])
        return try Cid(bytes: hex(S(objs(a[name])[idx][field])))
    }

    func testAipi() throws {
        let a = sec("aipi")
        let module = try Cid(bytes: hex(S(a["module_cid"]))), contract = try Cid(bytes: hex(S(a["contract_cid"])))
        XCTAssertEqual(try Cid.compute(utf8(S(a["module_utf8"]))), module)
        XCTAssertEqual(try Cid.compute(utf8(S(a["contract_utf8"]))), contract)
        for e in objs(a["fn"]) {
            guard let intent = Aipi.Intent(rawValue: U32(e["intent"])),
                  let verify = Aipi.Verify(rawValue: U32(e["verify"])),
                  let privacy = Aipi.Privacy(rawValue: U32(e["privacy"])),
                  let determinism = Aipi.Determinism(rawValue: U32(e["determinism"])) else {
                // An out-of-range code is unrepresentable in the Swift enums; C refuses it with ZXV_E_ARG.
                XCTAssertEqual(S(e["status"]), "ZXV_E_ARG")
                continue
            }
            let f = Aipi.FunctionSpec(intent: intent, verify: verify, privacy: privacy, determinism: determinism,
                                      quorumN: U32(e["quorum_n"]), quorumK: U32(e["quorum_k"]), fuelMax: U(e["fuel_max"]),
                                      module: module, contract: B(e["contract"]) ? contract : nil, entry: S(e["entry"]),
                                      checker: isNull(e["checker"]) ? nil : S(e["checker"]),
                                      certifier: isNull(e["certifier"]) ? nil : S(e["certifier"]))
            XCTAssertEqual(st { _ = try Aipi.encodeFunction(f) }, S(e["status"]))
            if S(e["status"]) != "ZXV_OK" { continue }
            let rec = try Aipi.encodeFunction(f)
            XCTAssertEqual(hx(rec), S(e["record"]))
            let fc = try Aipi.functionCid(rec)
            XCTAssertEqual(hx(fc.bytes), S(e["fn_cid"]))
            XCTAssertEqual(try fc.text(base: .base58btc), S(e["fn_cid_text"]))
            let d = try Aipi.decodeFunction(rec)
            XCTAssertEqual(d, f)
            XCTAssertEqual(try Aipi.encodeFunction(d), rec)
        }
        for e in objs(a["call"]) {
            let fn = try aipiRef(S(e["fn"]))
            let input = utf8(S(e["input_utf8"]))
            let want = e["status"] != nil ? S(e["status"]) : "ZXV_OK"
            XCTAssertEqual(st { _ = try Aipi.encodeCall(fn, input: input) }, want)
            XCTAssertEqual(st { _ = try Aipi.callCid(fn, input: input) }, want)
            if want != "ZXV_OK" { continue }
            let rec = try Aipi.encodeCall(fn, input: input)
            XCTAssertEqual(hx(rec), S(e["record"]))
            let cc = try Aipi.callCid(fn, input: input)
            XCTAssertEqual(hx(cc.bytes), S(e["call_cid"]))
            XCTAssertEqual(try cc.text(base: .base58btc), S(e["call_cid_text"]))
            XCTAssertEqual(try Aipi.decodeCall(rec), Aipi.Call(function: fn, input: input))
        }
        for e in objs(a["claim"]) {
            let call = try aipiRef(S(e["call"]))
            let ex = hex(S(e["executor"])), out = utf8(S(e["output_utf8"])), cert = utf8(S(e["cert_utf8"]))
            let rec = try Aipi.encodeClaim(call, executor: ex, output: out, cert: cert)
            XCTAssertEqual(hx(rec), S(e["record"]))
            XCTAssertEqual(try Aipi.decodeClaim(rec), Aipi.Claim(call: call, executor: ex, output: out, cert: cert))
        }
        XCTAssertEqual(st { _ = try Aipi.decodeFunction([1, 2, 3]) }, "ZXV_E_FORMAT")

        // An offline node: put, get, publish, load, plan, info.
        let ctx = try ZxvContext()
        let cap = try ctx.rootCapability(.all1_5)
        let node = try Aipi.Node(cap, storeBytes: 1 << 20, maxBlocks: 64)
        XCTAssertEqual(try node.put(utf8(S(a["module_utf8"]))), module)
        XCTAssertEqual(try node.get(module), utf8(S(a["module_utf8"])))
        let fn0 = hex(S(objs(a["fn"])[0]["record"]))
        let fc0 = try node.publish(fn0)
        XCTAssertEqual(hx(fc0.bytes), S(objs(a["fn"])[0]["fn_cid"]))
        XCTAssertEqual(try node.load(fc0), fn0)
        XCTAssertEqual(try node.plan(fc0), .local)
        let empty = try Aipi.Node(cap, storeBytes: 1 << 20, maxBlocks: 8) // holds no module
        let fe0 = try empty.publish(fn0)
        XCTAssertEqual(try empty.plan(fe0, input: utf8("x")), .unavailable)
        XCTAssertEqual(st { _ = try node.publish(hex(S(objs(a["fn"])[1]["record"]))) }, "ZXV_E_STATE") // needs a key
        XCTAssertEqual(st { _ = try node.get(contract) }, "ZXV_E_NOT_FOUND")
        XCTAssertEqual(st { _ = try node.put(utf8("secret"), isPrivate: true) }, "ZXV_E_STATE")
        let info = try node.info()
        XCTAssertTrue(info.blocks == 2 && info.getsVerified >= 1 && info.forbidden == 0)
        // With a node key, private blocks are sealed, and never re-published as public.
        let keyed = try Aipi.Node(cap, storeBytes: 1 << 20, maxBlocks: 16, nodeKey: SecretKey(copying: fill32(0x5A)))
        let pc = try keyed.put(utf8("private"), isPrivate: true)
        XCTAssertEqual(try keyed.get(pc), utf8("private"))
        XCTAssertEqual(st { _ = try keyed.put(utf8("private"), isPrivate: false) }, "ZXV_E_STATE")
        XCTAssertEqual(st { _ = try Aipi.Node(cap, storeBytes: 1 << 20, maxBlocks: 4, nodeKey: SecretKey(count: 31)) },
                       "ZXV_E_ARG")
    }

    // MARK: coin / token

    private func counts(_ sparse: Any?) -> SwarmToken.Counts {
        var c = SwarmToken.Counts()
        for p in list(sparse) {
            let pair = list(p)
            c[Int(U(pair[0]))] = U(pair[1])
        }
        return c
    }

    func testCoin() throws {
        let c = sec("coin")
        XCTAssertEqual(Coin.rungs, U32(c["rungs"]))
        XCTAssertEqual(Coin.topFace, U(c["top_face"]))
        for e in objs(c["face"]) {
            XCTAssertEqual(st { _ = try Coin.face(rung: U32(e["rung"])) }, S(e["status"]))
            if e["face"] != nil { XCTAssertEqual(try Coin.face(rung: U32(e["rung"])), U(e["face"])) }
        }
        for e in objs(c["rung_of"]) {
            XCTAssertEqual(st { _ = try Coin.rung(face: U(e["face"])) }, S(e["status"]))
            if e["rung"] != nil { XCTAssertEqual(try Coin.rung(face: U(e["face"])), U32(e["rung"])) }
        }
        for e in objs(c["split"]) {
            XCTAssertEqual(st { _ = try Coin.splitCount(U(e["amount"])) }, S(e["status"]))
            if S(e["status"]) != "ZXV_OK" { continue }
            XCTAssertEqual(try Coin.splitCount(U(e["amount"])), Int(U(e["count"])))
            if e["rungs"] != nil { XCTAssertEqual(try Coin.split(U(e["amount"])), list(e["rungs"]).map { U32($0) }) }
        }
    }

    func testToken() throws {
        let c = sec("coin")
        for e in objs(c["token_face"]) {
            XCTAssertEqual(st { _ = try SwarmToken.face(tier: U32(e["tier"])) }, S(e["status"]))
            if e["face"] == nil { continue }
            XCTAssertEqual(try SwarmToken.face(tier: U32(e["tier"])), SwarmToken.TierFace(face: U(e["face"]), isSuper: B(e["super"])))
        }
        for e in objs(c["token_tier"]) {
            XCTAssertEqual(st { _ = try SwarmToken.tier(face: U(e["face"])) }, S(e["status"]))
            if e["tier"] != nil { XCTAssertEqual(try SwarmToken.tier(face: U(e["face"])), U32(e["tier"])) }
        }
        for e in objs(c["token_value"]) {
            XCTAssertEqual(st { _ = try SwarmToken.value(self.counts(e["counts"])) }, S(e["status"]))
            if e["value"] == nil { continue }
            XCTAssertEqual(try SwarmToken.value(counts(e["counts"])), SwarmToken.Value(value: U(e["value"]), pieces: U(e["pieces"])))
        }
        for e in objs(c["token_canonical"]) {
            XCTAssertEqual(st { _ = try SwarmToken.canonical(value: U(e["value"]), octane: U32(e["octane"])) }, S(e["status"]))
            if e["counts"] == nil { continue }
            let k = try SwarmToken.canonical(value: U(e["value"]), octane: U32(e["octane"]))
            XCTAssertEqual(k, counts(e["counts"]))
            XCTAssertEqual(try SwarmToken.value(k), SwarmToken.Value(value: U(e["value"]), pieces: U(e["pieces"])))
            XCTAssertTrue(try SwarmToken.isCanonical(k, octane: U32(e["octane"])))
        }
        for e in objs(c["token_is_canonical"]) {
            XCTAssertEqual(st { _ = try SwarmToken.isCanonical(self.counts(e["counts"]), octane: U32(e["octane"])) }, S(e["status"]))
            if e["canonical"] != nil {
                XCTAssertEqual(try SwarmToken.isCanonical(counts(e["counts"]), octane: U32(e["octane"])), B(e["canonical"]))
            }
        }
        for e in objs(c["token_compress"]) {
            var k = counts(e["counts"])
            XCTAssertEqual(st { try SwarmToken.compress(&k, octane: U32(e["octane"])) }, S(e["status"]))
            if e["out"] != nil { XCTAssertEqual(k, counts(e["out"])) }
        }
        for e in objs(c["token_fuse_split"]) {
            var k = counts(e["counts"])
            if S(e["op"]) == "fuse" {
                XCTAssertEqual(st { try SwarmToken.fuse(&k, octane: U32(e["octane"]), tier: U32(e["tier"])) }, S(e["status"]))
            } else {
                XCTAssertEqual(st { try SwarmToken.split(&k, tier: U32(e["tier"])) }, S(e["status"]))
            }
            XCTAssertEqual(k, counts(e["out"])) // unchanged on refusal
        }
        for e in objs(c["token_code"]) {
            XCTAssertEqual(st { _ = try SwarmToken.codeBits(U(e["value"])) }, S(e["status"]))
            if e["bits"] == nil { continue }
            let bits = U32(e["bits"])
            XCTAssertEqual(try SwarmToken.codeBits(U(e["value"])), bits)
            var buf = [UInt8](repeating: 0, count: 16)
            var bit: UInt32 = 0
            try SwarmToken.codePut(&buf, bit: &bit, value: U(e["value"]))
            XCTAssertEqual(bit, bits)
            buf = Array(buf.prefix(Int(bits + 7) / 8))
            XCTAssertEqual(hx(buf), S(e["bytes"]))
            let code = (0..<Int(bits)).map { (buf[$0 / 8] >> UInt8($0 % 8)) & 1 == 1 ? "1" : "0" }.joined()
            XCTAssertEqual(code, S(e["code"]))
            var rb: UInt32 = 0
            XCTAssertEqual(try SwarmToken.codeGet(buf, bit: &rb), U(e["value"]))
            XCTAssertEqual(rb, bits)
        }
        do {
            let s = c["token_code_stream"] as! J
            var buf = [UInt8](repeating: 0, count: 64)
            var bit: UInt32 = 0
            for v in list(s["values"]) { try SwarmToken.codePut(&buf, bit: &bit, value: U(v)) }
            XCTAssertEqual(bit, U32(s["bits"]))
            buf = Array(buf.prefix(Int(bit + 7) / 8))
            XCTAssertEqual(hx(buf), S(s["bytes"]))
            var rb: UInt32 = 0
            for v in list(s["values"]) { XCTAssertEqual(try SwarmToken.codeGet(buf, bit: &rb), U(v)) }
            XCTAssertEqual(rb, bit)
            // A full buffer refuses the next code and leaves `bit` alone.
            var at = bit
            var tiny = buf
            XCTAssertEqual(st { try SwarmToken.codePut(&tiny, bit: &at, value: UInt64.max - 1) }, "ZXV_E_BUFFER")
            XCTAssertEqual(at, bit)
        }
        for e in objs(c["token_code_get_reject"]) {
            var b = U32(e["bit"])
            let buf = hex(S(e["bytes"]))
            XCTAssertEqual(st { _ = try SwarmToken.codeGet(buf, bit: &b) }, S(e["status"]))
            XCTAssertEqual(b, U32(e["bit"]))
        }
        XCTAssertEqual(st { _ = try SwarmToken.Counts([1, 2]) }, "ZXV_E_ARG")

        // A wallet (.mint).
        let ctx = try ZxvContext()
        let cap = try ctx.rootCapability(.all1_5)
        let w = try SwarmToken.Wallet(cap, modelId: 7, octane: SwarmToken.Octane.midgrade)
        let v = try SwarmToken.Wallet(cap, modelId: 8, octane: SwarmToken.Octane.plain)
        try w.mint(1000)
        XCTAssertEqual(try w.info(), SwarmToken.WalletInfo(modelId: 7, octane: SwarmToken.Octane.midgrade, value: 1000,
                                                           pieces: try w.info().pieces))
        XCTAssertTrue(try SwarmToken.isCanonical(w.counts(), octane: SwarmToken.Octane.midgrade))
        try w.spend(1)
        XCTAssertEqual(try w.info().value, 999)
        try w.transfer(to: v, amount: 99)
        XCTAssertTrue(try w.info().value == 900 && v.info().value == 99 && v.counts()[0] == 99)
        XCTAssertEqual(st { try v.spend(100) }, "ZXV_E_FUNDS")
        XCTAssertEqual(zerr { try w.transfer(to: v, amount: 901) }?.refusal, .insufficientFunds)
        XCTAssertTrue(try w.info().value == 900 && v.info().value == 99)
        try v.setOctane(SwarmToken.Octane.racing)
        XCTAssertTrue(try v.info().value == 99 && v.info().pieces < 99)
        XCTAssertEqual(st { try v.setOctane(64) }, "ZXV_E_RANGE")
        XCTAssertEqual(st { _ = try SwarmToken.Wallet(cap, modelId: 0, octane: 64) }, "ZXV_E_RANGE")
    }

    // MARK: fee

    func testFee() throws {
        let f = sec("fee")
        XCTAssertTrue(Fee.num == U(f["num"]) && Fee.den == U(f["den"]))
        for e in objs(f["assure"]) { XCTAssertEqual(try Fee.assure(U(e["gross"])), U(e["fee"])) }
        for c in objs(f["carry"]) {
            var carry = Fee.Carry(remainder: U(c["rem_in"]))
            var rem = U(c["rem_in"])
            for s in objs(c["steps"]) {
                XCTAssertEqual(st { _ = try Fee.assureCarry(remainder: rem, gross: U(s["gross"])) }, S(s["status"]))
                let ch = try Fee.assureCarry(remainder: rem, gross: U(s["gross"]))
                XCTAssertEqual(ch, Fee.Charge(fee: U(s["fee"]), remainder: U(s["rem"])))
                rem = ch.remainder
                XCTAssertEqual(try carry.charge(U(s["gross"])), U(s["fee"]))
                XCTAssertEqual(carry.remainder, U(s["rem"]))
            }
        }
        for e in objs(f["carry_reject"]) {
            XCTAssertEqual(st { _ = try Fee.assureCarry(remainder: U(e["rem_in"]), gross: U(e["gross"])) }, S(e["status"]))
            var carry = Fee.Carry(remainder: U(e["rem_in"]))
            XCTAssertEqual(st { _ = try carry.charge(U(e["gross"])) }, S(e["status"]))
            XCTAssertEqual(carry.remainder, U(e["rem_in"])) // unchanged on refusal
        }
        for (i, name) in list(f["buckets"]).enumerated() {
            XCTAssertEqual(Fee.Bucket(rawValue: Int32(i))?.description, S(name))
        }
        for e in objs(f["split"]) {
            let s = try Fee.split(U(e["fee"]))
            XCTAssertEqual(s, list(e["parts"]).map { U($0) })
            XCTAssertEqual(s.reduce(0, +), U(e["fee"]))
        }
        let p = f["policy_default"] as! J
        let d = try Fee.defaultPolicy()
        XCTAssertEqual(d.percent, list(p["pct"]).map { U32($0) })
        XCTAssertEqual(d.allowBelow, U(p["allow_below"]) != 0)
        XCTAssertEqual(d.rates, list(p["rate"]).map { r in let q = list(r); return Fee.Ratio(num: U(q[0]), den: U(q[1])) })
        let m = list(p["multiplier"]), cc = list(p["commons_cap"])
        XCTAssertEqual(d.multiplier, Fee.Ratio(num: U(m[0]), den: U(m[1])))
        XCTAssertEqual(d.commonsCap, Fee.Ratio(num: U(cc[0]), den: U(cc[1])))
    }

    // MARK: vhub

    func testVHub() throws {
        let x = sec("vhub")
        for e in objs(x["peg_msg"]) {
            let m = try VHub.pegMessage(slot: U8(e["slot"]), num: U(e["num"]), den: U(e["den"]), epoch: U(e["epoch"]))
            XCTAssertEqual(hx(m.bytes), S(e["msg"]))
        }
        let s = x["scenario"] as! J
        let ctx = try ZxvContext()
        let cap = try ctx.rootCapability(.all1_5)
        let dsa = MlDsa87(cap)
        var keys: [SigningKeyPair] = []
        for k in objs(s["keys"]) {
            keys.append(try dsa.keygen(seed: hex(S(k["seed"]))))
            XCTAssertEqual(hx(Hashes.sha3_256(keys.last!.publicKey)), S(k["pk_sha3_256"]))
        }
        let hub = try VHub.Hub(cap, seed: hex(S(s["hub_seed"])))
        for c in objs(s["currencies"]) {
            let slot = U8(c["slot"])
            let key = c["issuer_key"] != nil ? Int(U(c["issuer_key"])) : 0
            let pool = c["pool_funding"] != nil ? U(c["pool_funding"]) : 0
            let back = c["vbill_backing"] != nil ? U(c["vbill_backing"]) : 0
            let minor = c["minor_units"] != nil ? U8(c["minor_units"]) : 2
            XCTAssertEqual(st { try hub.addCurrency(slot: slot, code: S(c["code"]), minorUnits: minor,
                                                    issuerPublicKey: keys[key].publicKey, poolFunding: pool,
                                                    vbillBacking: back) }, S(c["status"]))
            if S(c["status"]) != "ZXV_OK" { continue }
            let i = try hub.currency(slot)
            XCTAssertTrue(i.issuerAccount == U32(c["issuer_account"]) && i.poolAccount == U32(c["pool_account"]) &&
                          i.vfloatAccount == U32(c["vfloat_account"]))
            XCTAssertFalse(i.hasPeg)
        }
        XCTAssertEqual(zerr { try hub.addCurrency(slot: 5, code: "USD", minorUnits: 2, issuerPublicKey: keys[0].publicKey) }?
            .statusClass, .usage)
        for p in objs(s["pegs"]) {
            let slot = U8(p["slot"])
            let m = try VHub.pegMessage(slot: slot, num: U(p["num"]), den: U(p["den"]), epoch: U(p["epoch"]))
            let sig = try dsa.sign(m.bytes, with: keys[Int(U(p["signer"]))].secretKey, deterministic: true)
            XCTAssertEqual(hx(Hashes.sha3_256(sig)), S(p["sig_sha3_256"]))
            XCTAssertEqual(st { try hub.setPeg(slot: slot, num: U(p["num"]), den: U(p["den"]), epoch: U(p["epoch"]),
                                               signature: sig) }, S(p["status"]))
        }
        let c0 = try hub.currency(0)
        XCTAssertTrue(c0.hasPeg && c0.pegNum == 350 && c0.pegEpoch == 1)
        XCTAssertFalse(try hub.currency(3).hasPeg)
        for a in objs(s["accounts"]) {
            let acct = try hub.openAccount(slot: U8(a["slot"]), owner: U32(a["owner"]))
            XCTAssertEqual(acct, U32(a["account"]))
            if a["issued"] != nil { try hub.issue(account: acct, amount: U(a["issued"])) }
        }
        for q in objs(s["quotes"]) {
            XCTAssertEqual(st { _ = try hub.quote(from: U8(q["from"]), to: U8(q["to"]), want: U(q["want"])) }, S(q["status"]))
            if S(q["status"]) != "ZXV_OK" { continue }
            XCTAssertEqual(try hub.quote(from: U8(q["from"]), to: U8(q["to"]), want: U(q["want"])),
                           VHub.Quote(amount: U(q["amount"]), vbills: U(q["vbills"]), received: U(q["received"])))
        }
        for w in objs(s["swaps"]) {
            if let l = w["add_limit"] as? J { try hub.addLimit(slot: U8(l["slot"]), limit: U(l["limit"])) }
            var r: VHub.Swap?
            XCTAssertEqual(st {
                r = try hub.swap(from: U8(w["from"]), to: U8(w["to"]), sender: U32(w["sender"]), receiver: U32(w["receiver"]),
                                 amount: U(w["amount"]), tick: U(w["tick"]))
            }, S(w["status"]))
            if S(w["status"]) == "ZXV_OK" { XCTAssertEqual(r, VHub.Swap(vbills: U(w["vbills"]), received: U(w["received"]))) }
        }
        XCTAssertEqual(S(s["check"]), "ZXV_OK")
        XCTAssertEqual(try hub.check(), U(s["postings"]))
        for b in objs(s["balances"]) {
            XCTAssertEqual(try hub.balance(U32(b["account"])),
                           VHub.Balance(debit: U(b["debit"]), credit: U(b["credit"]), equity: I(b["equity"])))
        }
        XCTAssertEqual(st { _ = try hub.balance(UInt32(objs(s["balances"]).count)) }, "ZXV_E_NOT_FOUND")
        XCTAssertGreaterThanOrEqual(try hub.nextEpoch(), 1)
        XCTAssertEqual(st { _ = try VHub.Hub(cap, seed: [1, 2, 3]) }, "ZXV_E_ARG")
    }

    // MARK: freight

    func testFreight() throws {
        let x = sec("freight")
        for e in objs(x["header_serialize"]) {
            var f = Freight.HeaderFields()
            f.k = U8(e["k"])
            f.flags = U8(e["flags"])
            f.payloadLength = UInt16(U(e["payload_len"]))
            f.seq = UInt16(U(e["seq"]))
            f.seqCount = UInt16(U(e["seq_count"]))
            f.freightId = U(e["freight_id"])
            f.expandLimit = U(e["expand_limit"])
            f.payloadSha256 = hex(S(e["payload_sha256"]))
            XCTAssertEqual(st { _ = try Freight.Header(fields: f) }, S(e["status"]))
            if S(e["status"]) != "ZXV_OK" { continue }
            XCTAssertEqual(hx(try Freight.Header(fields: f).bytes), S(e["header"]))
            let g = try Freight.Header(bytes: hex(S(e["header"]))).fields()
            XCTAssertEqual(g.version, Freight.version)
            f.version = Freight.version
            XCTAssertEqual(g, f)
        }
        for c in objs(x["codec"]) {
            let payload = hex(S(c["payload"]))
            let h = try Freight.Header.forPayload(freightId: U(c["freight_id"]), k: U8(c["k"]), payload: payload)
            XCTAssertEqual(hx(h.bytes), S(c["header"]))
            let packets = try Freight.encode(h, payload: payload)
            XCTAssertEqual(hx(packets), S(c["packets"]))
            XCTAssertEqual(packets.count, Freight.freightBytes)
            for d in objs(c["decode"]) {
                var sub: [UInt8] = []
                for r in list(d["rows"]) {
                    let row = Int(U(r))
                    sub += packets[(21 * row)..<(21 * row + 21)]
                }
                if let flip = d["flip"] as? J {
                    sub[21 * Int(U(flip["index_in_rows"])) + Int(U(flip["byte"]))] ^= hex(S(flip["xor"]))[0]
                }
                let e = zerr { _ = try Freight.decode(h, packets: sub) }
                XCTAssertEqual(e?.statusName ?? "ZXV_OK", S(d["status"]))
                if S(d["status"]) == "ZXV_E_ERASURE" { XCTAssertEqual(e?.refusal, .erasure) }
                if d["matches_payload"] != nil { XCTAssertEqual(try Freight.decode(h, packets: sub) == payload, B(d["matches_payload"])) }
            }
        }
        let r201 = ramp(201)
        for e in objs(x["header_init_reject"]) {
            let lim = e["expand_limit"] != nil ? U(e["expand_limit"]) : 0
            let flags = e["flags"] != nil ? U8(e["flags"]) : 0
            XCTAssertEqual(st { _ = try Freight.Header.forPayload(freightId: 1, k: U8(e["k"]), payload: Array(r201.prefix(Int(U(e["len"])))),
                                                                  flags: flags, expandLimit: lim) }, S(e["status"]))
        }
        for e in objs(x["set_count"]) {
            XCTAssertEqual(st { _ = try Freight.setCount(length: U(e["len"]), k: U8(e["k"])) }, S(e["status"]))
            if e["count"] != nil { XCTAssertEqual(try Freight.setCount(length: U(e["len"]), k: U8(e["k"])), U32(e["count"])) }
        }
        do {
            let sp = x["split"] as! J
            let data = ramp(Int(U(sp["len"])))
            let join = try Freight.Join(capacity: 1 << 16)
            for m in objs(sp["members"]) {
                XCTAssertEqual(st { _ = try Freight.split(freightId: U(sp["freight_id"]), k: U8(sp["k"]), data: data, seq: U32(m["seq"])) },
                               S(m["status"]))
                if S(m["status"]) != "ZXV_OK" { continue }
                let mem = try Freight.split(freightId: U(sp["freight_id"]), k: U8(sp["k"]), data: data, seq: U32(m["seq"]))
                XCTAssertEqual(hx(mem.header.bytes), S(m["header"]))
                XCTAssertEqual(hx(Hashes.sha3_256(mem.packets)), S(m["packets_sha3_256"]))
                XCTAssertFalse(try join.status().complete)
                try join.add(mem)
            }
            XCTAssertEqual(try join.status(), Freight.JoinStatus(received: 3, count: 3, complete: true, total: UInt64(data.count)))
            XCTAssertEqual(try join.result(), data)
        }
        for e in objs(x["ops_encode"]) {
            let input = utf8(S(e["utf8"]))
            let ws = B(e["with_sha"])
            XCTAssertEqual(st { _ = try Freight.encodeOps(input, withSha: ws) }, S(e["status"]))
            let ops = try Freight.encodeOps(input, withSha: ws)
            XCTAssertEqual(hx(ops), S(e["ops"]))
            XCTAssertEqual(try Freight.opsBound(UInt64(input.count), withSha: ws), U(e["bound"]))
            XCTAssertLessThanOrEqual(UInt64(ops.count), try Freight.opsBound(UInt64(input.count), withSha: ws))
            XCTAssertEqual(st { _ = try Freight.expand(ops, limit: 256) }, S(e["expand_status"]))
            XCTAssertEqual(try Freight.expand(ops, limit: 256), input)
            XCTAssertEqual(try Freight.peek(ops), Freight.OpsInfo(total: UInt64(input.count), hasSha: ws))
        }
        do {
            let w = objs(x["ops_writer"])[0]
            let ow = try Freight.OpsWriter(declaredTotal: U(w["declared_total"]), maxBytes: 4096)
            for op in objs(w["ops_list"]) {
                switch S(op["op"]) {
                case "literal": XCTAssertEqual(st { try ow.literal(utf8(S(op["utf8"]))) }, S(op["status"]))
                case "copy": XCTAssertEqual(st { try ow.copy(distance: U(op["distance"]), length: U(op["len"])) }, S(op["status"]))
                default:
                    XCTAssertEqual(st { try ow.seed(Freight.Generator(rawValue: U8(op["gen"]))!, params: hex(S(op["params"])),
                                                    length: U(op["len"])) }, S(op["status"]))
                }
            }
            XCTAssertEqual(st { _ = try ow.finish() }, S(w["finish_status"]))
            let ops = try ow.finish()
            XCTAssertEqual(hx(ops), S(w["ops"]))
            XCTAssertEqual(st { _ = try Freight.expand(ops, limit: 1024) }, S(w["expand_status"]))
            XCTAssertEqual(hx(try Freight.expand(ops, limit: 1024)), S(w["expanded"]))
            XCTAssertEqual(try Freight.peek(ops), Freight.OpsInfo(total: U(w["peek_total"]), hasSha: B(w["peek_has_sha"])))
            XCTAssertEqual(st { _ = try Freight.expand(ops, limit: U(w["declared_total"]) - 1) }, S(w["expand_limit_too_small"]))
            XCTAssertEqual(st { _ = try Freight.expand(Array(ops.dropLast()), limit: 1024) }, S(w["expand_truncated"]))
        }
        do {
            let w = objs(x["ops_writer"])[1]
            let ow = try Freight.OpsWriter(declaredTotal: U(w["declared_total"]), maxBytes: 4096)
            var resolves = ""
            var ref: Cid?
            for op in objs(w["ops_list"]) {
                if S(op["op"]) == "literal" {
                    try ow.literal(utf8(S(op["utf8"])))
                } else {
                    let r = try Cid(bytes: hex(S(op["cid"])))
                    ref = r
                    try ow.cidref(r, length: U(op["len"]))
                    resolves = S(op["resolves_to_utf8"])
                }
            }
            let ops = try ow.finish()
            XCTAssertEqual(hx(ops), S(w["ops"]))
            XCTAssertEqual(st { _ = try Freight.expand(ops, limit: 1024) }, S(w["expand_without_resolver"]))
            nonisolated(unsafe) var calls = 0
            let want = ref!.bytes, content = utf8(resolves)
            let got = try Freight.expand(ops, limit: 1024) { (cid, length) throws -> [UInt8]? in
                calls += 1
                return cid == want && length == content.count ? content : nil
            }
            XCTAssertEqual(String(decoding: got, as: UTF8.self), S(w["expanded_with_resolver_utf8"]))
            XCTAssertGreaterThanOrEqual(calls, 1)
            // A resolver that declines: ZXV_E_NOT_FOUND.
            XCTAssertEqual(st { _ = try Freight.expand(ops, limit: 1024) { _, _ in nil } }, "ZXV_E_NOT_FOUND")
            // A throwing resolver: the error never crosses C; expand rethrows it.
            let boom = ZxvError.system(ZxvSystemError(status: ZXV_E_IO, detail: "resolver boom"))
            let e = zerr { _ = try Freight.expand(ops, limit: 1024) { (_, _) throws -> [UInt8]? in throw boom } }
            XCTAssertEqual(e, boom)
        }
        for e in objs(x["gen_run"]) {
            let gen = Freight.Generator(rawValue: U8(e["gen"]))
            let params = hex(S(e["params"])), len = Int(U(e["len"]))
            guard let gen else {
                XCTAssertEqual(S(e["status"]), "ZXV_E_ARG") // an unknown generator is not representable here
                continue
            }
            XCTAssertEqual(st { _ = try Freight.generate(gen, params: params, length: len) }, S(e["status"]))
            if e["out"] != nil { XCTAssertEqual(hx(try Freight.generate(gen, params: params, length: len)), S(e["out"])) }
        }
    }

    // MARK: web4

    func testWeb4() throws {
        let x = sec("web4")
        let sigb = Int(U(x["sig_bytes"]))
        XCTAssertEqual(sigb, Web4.sigBytes)
        let ctx = try ZxvContext()
        let cap = try ctx.rootCapability(.all1_5)
        var agents: [String: Web4.Agent] = [:]
        var order: [Web4.Agent] = []
        func body(_ b: [UInt8]) -> String { hx(Array(b.prefix(b.count - sigb))) }
        for a in objs(x["agents"]) {
            let seed = [UInt8](repeating: hex(S(a["seed_byte"]))[0], count: 32)
            let node = isNull(a["node"]) ? nil : agents[S(a["node"])]
            let ag = try Web4.Agent(cap, name: S(a["name"]), nowMs: U(a["now_ms"]), node: node, seed: seed)
            agents[S(a["name"])] = ag
            order.append(ag)
            XCTAssertEqual(hx(try ag.agentId()), S(a["agent_id"]))
            XCTAssertEqual(hx(try ag.peerId()), S(a["peer_id"]))
            XCTAssertEqual(hx(Hashes.sha3_256(try ag.publicKey())), S(a["pk_sha3_256"]))
            var card = try ag.card()
            XCTAssertEqual(card.count, Int(U(a["card_len"])))
            XCTAssertEqual(hx(Hashes.sha3_256(Array(card.prefix(card.count - sigb)))), S(a["card_signed_body_sha3_256"]))
            XCTAssertEqual(st { _ = try Web4.verifyCard(card) }, S(a["card_verify"]))
            let ci = try Web4.verifyCard(card)
            XCTAssertEqual(ci, Web4.CardInfo(agentId: try ag.agentId(), peerId: try ag.peerId(), name: S(a["name"]),
                                             boundMs: U(a["now_ms"])))
            card[card.count - 1] ^= 1
            XCTAssertEqual(st { _ = try Web4.verifyCard(card) }, S(a["card_verify_flipped_last_byte"]))
        }
        let alice = agents["alice"]!, bob = agents["bob"]!, human = agents["human"]!

        // Directory of cards.
        let dir = try Web4.Directory(capacity: 8)
        for a in order { try dir.add(try a.card()) }
        XCTAssertEqual(try dir.count(), UInt32(order.count))
        XCTAssertNotNil(try dir.find(try bob.agentId()))
        XCTAssertNil(try dir.find(fill32(0xEE)))

        let ad = x["action_digest"] as! J
        let from = hex(S(ad["from"])), to = hex(S(ad["to"]))
        for c in objs(ad["cases"]) {
            let p = c["payload_ramp_len"] != nil ? ramp(Int(U(c["payload_ramp_len"]))) : utf8(S(c["payload_utf8"]))
            XCTAssertEqual(st { _ = try Web4.actionDigest(tool: S(c["tool"]), from: from, to: to, requestId: U(c["req_id"]), payload: p) },
                           S(c["status"]))
            if c["digest"] != nil {
                XCTAssertEqual(hx(try Web4.actionDigest(tool: S(c["tool"]), from: from, to: to, requestId: U(c["req_id"]), payload: p)),
                               S(c["digest"]))
            }
        }

        // Manifest.
        let m = x["manifest"] as! J
        let man = try Web4.Manifest(version: U32(m["version"]), issuedMs: U(m["issued_ms"]), expiresMs: U(m["expires_ms"]))
        for t in objs(m["tools"]) {
            let tool = Web4.Tool(name: S(t["name"]), priceVfv: U(t["price_vfv"]), ratePerMin: U32(t["rate_per_min"]),
                                 burst: U32(t["burst"]), flags: Web4.ToolFlags(rawValue: U32(t["flags"])), maxInput: U32(t["max_input"]))
            XCTAssertEqual(st { try man.addTool(tool) }, S(t["status"]))
        }
        XCTAssertEqual(st { try man.addTool(Web4.Tool(name: "pay", priceVfv: 1)) }, S(m["add_duplicate"]))
        XCTAssertEqual(st { _ = try man.encode() }, S(m["encode_unsigned"]))
        XCTAssertFalse(try man.info().isSigned)
        try man.sign(with: agents[S(m["signer"])]!)
        let enc = try man.encode()
        XCTAssertEqual(enc.count, Int(U(m["encoding_len"])))
        XCTAssertEqual(body(enc), S(m["encoding_signed_body"]))
        XCTAssertEqual(try man.toJSON(), S(m["json"]))
        XCTAssertTrue(try man.info().isSigned)
        XCTAssertEqual(try man.info().toolCount, UInt32(objs(m["tools"]).count))
        XCTAssertEqual(try man.find("profile.read"), 1)
        XCTAssertNil(try man.find("nope"))
        XCTAssertTrue(try man.tool(at: 0).name == "pay" && man.tool(at: 0).priceVfv == 150 && man.tools().count == 3)
        XCTAssertEqual(st { _ = try Web4.Manifest.decode(enc) }, S(m["decode"]))
        let dec = try Web4.Manifest.decode(enc)
        XCTAssertEqual(try dec.info().agentId, try alice.agentId())
        for v in objs(m["verify"]) {
            XCTAssertEqual(st { try dec.verify(publicKey: try agents[S(v["key"])]!.publicKey(), nowMs: U(v["now_ms"])) },
                           S(v["status"]))
        }

        // Consent.
        let c = x["consent"] as! J
        let action = hex(S(objs(ad["cases"])[1]["digest"]))
        let nonce = hex(S(c["nonce"]))
        let agent = agents[S(c["agent"])]!
        let scope = Web4.ConsentScope(rawValue: U32(c["scope"]))
        let tok = try agents[S(c["human"])]!.issueConsent(agent: try agent.agentId(), action: action, scope: scope,
                                                         maxVfv: U(c["max_vfv"]), issuedMs: U(c["issued_ms"]),
                                                         expiresMs: U(c["expires_ms"]), nonce: nonce)
        XCTAssertEqual(tok.bytes.count, Int(U(c["token_len"])))
        XCTAssertEqual(body(tok.bytes), S(c["token_signed_body"]))
        let ti = try tok.info()
        XCTAssertEqual(ti.humanId, try human.agentId())
        XCTAssertEqual(ti.agentId, try agent.agentId())
        XCTAssertTrue(ti.action == action && ti.scope == scope && ti.maxVfv == U(c["max_vfv"]) && ti.nonce == nonce)
        XCTAssertEqual(try Web4.ConsentToken(bytes: tok.bytes), tok)
        var nonce2 = nonce
        nonce2[0] ^= 0xFF
        let tok2 = try human.issueConsent(agent: try agent.agentId(), action: action, scope: scope, maxVfv: U(c["max_vfv"]),
                                          issuedMs: U(c["issued_ms"]), expiresMs: U(c["expires_ms"]), nonce: nonce2)
        let log = try Web4.ConsentLog(capacity: 16)
        let hpk = try human.publicKey()
        for k in objs(c["check"]) {
            let second = S(k["what"]).contains("second token")
            if second { try log.revoke(tok2, nowMs: U(k["now_ms"])) }
            let e = zerr {
                try log.check(second ? tok2 : tok, humanPublicKey: hpk, agent: try agents[S(k["agent"])]!.agentId(), action: action,
                              need: Web4.ConsentScope(rawValue: U32(k["need"])), amount: U(k["amount"]), nowMs: U(k["now_ms"]))
            }
            XCTAssertEqual(e?.statusName ?? "ZXV_OK", S(k["status"]))
            if e != nil { XCTAssertEqual(e?.refusal, .consentDenied) }
        }
        XCTAssertEqual(zerr { try log.check(nil, humanPublicKey: hpk, agent: try alice.agentId(), action: action, need: .money,
                                            amount: 1, nowMs: U(c["issued_ms"]) + 1) }?.refusal, .consentDenied)
        XCTAssertGreaterThanOrEqual(try log.prune(nowMs: U(c["issued_ms"])), 1) // nothing expired yet
        XCTAssertEqual(try log.prune(nowMs: U(c["expires_ms"]) + 1), 0)          // every token expired

        // Envelopes.
        var req: [UInt8] = []
        for e in objs(x["envelopes"]) {
            let snd = agents[S(e["sender"])]!, rcv = agents[S(e["to"])]!
            var env: [UInt8] = []
            let kind = Web4.MessageKind(rawValue: I32(e["kind"]))!
            let flags = Web4.MessageFlags(rawValue: U32(e["flags"]))
            if S(e["op"]) == "seal" {
                XCTAssertEqual(st {
                    env = try snd.seal(kind, to: try rcv.agentId(), requestId: U(e["req_id"]), chunk: U32(e["chunk"]),
                                       tool: S(e["tool"]), flags: flags, payload: utf8(S(e["payload_utf8"])), nowMs: U(e["now_ms"]))
                }, S(e["status"]))
                req = env
            } else {
                XCTAssertEqual(st {
                    env = try snd.reply(to: req, kind, chunk: U32(e["chunk"]), flags: flags, payload: utf8(S(e["payload_utf8"])),
                                        nowMs: U(e["now_ms"]))
                }, S(e["status"]))
            }
            XCTAssertEqual(env.count, Int(U(e["env_len"])))
            XCTAssertEqual(body(env), S(e["env_signed_body"]))
            XCTAssertEqual(try Web4.verifyEnvelope(env, senderPublicKey: try snd.publicKey()), S(e["verify_sender"]) == "ZXV_OK")
            if e["verify_other"] != nil {
                XCTAssertEqual(try Web4.verifyEnvelope(env, senderPublicKey: try rcv.publicKey()), S(e["verify_other"]) == "ZXV_OK")
            }
            let d = try Web4.decodeEnvelope(env)
            XCTAssertEqual(d.kind, kind)
            XCTAssertEqual(d.from, try snd.agentId())
            XCTAssertEqual(d.to, try rcv.agentId())
            XCTAssertEqual(try Web4.envelopePayload(env), utf8(S(e["payload_utf8"])))
            XCTAssertNil(try Web4.envelopeConsent(env))
        }
        // A request carrying a consent token.
        let env = try alice.seal(.request, to: try bob.agentId(), requestId: 9, chunk: 0, tool: "pay", payload: utf8("amount=100"),
                                 nowMs: U(c["issued_ms"]) + 10, consent: tok)
        XCTAssertEqual(try Web4.envelopeConsent(env), tok)
        XCTAssertTrue(try Web4.decodeEnvelope(env).flags.contains(.consent))
    }

    // MARK: paradox

    private func checkHeads(_ b: Paradox.Book, _ h: Any?, file: StaticString = #filePath, line: UInt = #line) throws {
        let h = h as! J
        for (name, which) in [("ops", Paradox.Log.ops), ("frontier", .frontier), ("quarantine", .quarantine)] {
            let w = h[name] as! J
            XCTAssertEqual(try b.logCount(which), U32(w["count"]), file: file, line: line)
            XCTAssertEqual(hx(try b.logHead(which)), S(w["head"]), file: file, line: line)
        }
    }

    func testParadox() throws {
        let p = sec("paradox")
        for s in objs(p["signature"]) { XCTAssertEqual(hx(try Paradox.signature(S(s["utf8"]))), S(s["sig"])) }
        for (k, n) in list(p["kind_names"]).enumerated() { XCTAssertEqual(Paradox.kindName(Int32(k)), S(n)) }
        for (k, n) in list(p["outcome_names"]).enumerated() { XCTAssertEqual(Paradox.outcomeName(Int32(k)), S(n)) }
        for (k, n) in list(p["node_state_names"]).enumerated() { XCTAssertEqual(Paradox.nodeStateName(Int32(k)), S(n)) }
        XCTAssertEqual(Paradox.kindName(99), "INVALID")
        XCTAssertEqual(Paradox.Kind.special.description, "special")
        for (t, w) in list(p["kind_of_tag"]).enumerated() {
            let want = I(w)
            if want < 0 { XCTAssertEqual(st { _ = try Paradox.kindOfTag(Hexagram.all[t]) }, "ZXV_E_ARG") }
            else { XCTAssertEqual(Int64(try Paradox.kindOfTag(Hexagram.all[t]).rawValue), want) }
        }
        for (k, row) in list(p["action"]).enumerated() {
            for (t, w) in list(row).enumerated() {
                let want = I(w), kd = Paradox.Kind(rawValue: Int32(k))!
                if want < 0 { XCTAssertEqual(st { _ = try Paradox.action(kind: kd, tag: Hexagram.all[t]) }, "ZXV_E_ARG") }
                else { XCTAssertEqual(Int64(try Paradox.action(kind: kd, tag: Hexagram.all[t]).rawValue), want) }
            }
        }
        for (o, w) in list(p["outcome_action"]).enumerated() {
            XCTAssertEqual(Int64(try Paradox.outcomeAction(Paradox.Outcome(rawValue: Int32(o))!).rawValue), I(w))
        }
        XCTAssertNil(Paradox.Outcome(rawValue: 14)) // not representable, so never sent to C

        let ctx = try ZxvContext()
        let cap = try ctx.rootCapability(.all1_5)
        let book = try Paradox.Book(cap, logCapacity: 64, registryCapacity: 16)
        try checkHeads(book, p["heads_fresh"])
        let sig2 = hex(S(objs(p["signature"])[2]["sig"]))
        func sigRef(_ v: Any?) -> [UInt8]? {
            if isNull(v) { return nil }
            XCTAssertEqual(S(v), "signature[2]")
            return sig2
        }
        for c in objs(p["classify"]) {
            if let r = c["registry_add"] as? J {
                XCTAssertEqual(st { try book.registryAdd(sigRef(r["sig"])!, kind: kind(r["kind"]), reason: U8(r["reason"]),
                                                         flags: Paradox.Flags(rawValue: U8(r["flags"])), ownerOk: B(r["owner_ok"])) },
                               S(r["status"]))
                XCTAssertEqual(try book.registryFind(sigRef(r["sig"])!), U32(r["index"]))
                continue
            }
            var ev: Paradox.Evidence?
            if !isNull(c["evidence"]) {
                let e = list(c["evidence"])
                ev = Paradox.Evidence(pos0: U32(e[0]), neg0: U32(e[1]), pos1: U32(e[2]), neg1: U32(e[3]))
            }
            let r = try book.classify(tag(c["tag"]), evidence: ev, stale: B(c["stale"]), signature: sigRef(c["sig"]))
            XCTAssertEqual(Int64(r.kind.rawValue), I(c["kind"]))
            XCTAssertEqual(r.tag.tag, U8(c["out_tag"]))
            XCTAssertEqual(r.registryIndex, I32(c["reg_index"]))
            if isNull(c["sig"]) {
                let f = try Paradox.classify(tag(c["tag"]), evidence: ev, stale: B(c["stale"]))
                XCTAssertEqual(f, Paradox.Classification(kind: r.kind, tag: r.tag, registryIndex: -1))
            }
        }
        for r in objs(p["registry"]) {
            let sig = try Paradox.signature(S(r["utf8"]))
            XCTAssertEqual(st { try book.registryAdd(sig, kind: kind(r["kind"]), reason: 2, flags: Paradox.Flags(rawValue: U8(r["flags"])),
                                                     ownerOk: B(r["owner_ok"])) }, S(r["status"]))
            if r["index"] != nil { XCTAssertEqual(try book.registryFind(sig), U32(r["index"])) }
        }
        XCTAssertEqual(zerr { try book.registryAdd(try Paradox.signature("money:other"), kind: .neutral, reason: 2, flags: .money) }?
            .refusal, .ownerApprovalRequired)
        let e1 = try book.registryEntry(at: 1)
        XCTAssertEqual(e1, Paradox.RegistryEntry(signature: try Paradox.signature("money:drain"), kind: .neutral, reason: 2,
                                                 flags: .money, source: Paradox.sourceSeed))
        XCTAssertEqual(st { _ = try book.registryEntry(at: 99) }, "ZXV_E_NOT_FOUND")
        XCTAssertNil(try book.registryFind(fill32(0)))
        try checkHeads(book, p["heads_after_classify"])

        // constructive
        do {
            let c = p["constructive"] as! J
            func vec(_ a: Any?) -> [Int64] { list(a).map { I($0) } }
            let lo = vec(c["lo"]), hi = vec(c["hi"]), initial = vec(c["initial"])
            let plain = try Paradox.Policy(cap, lower: lo, upper: hi, initial: initial)
            let money = try Paradox.Policy(cap, lower: lo, upper: hi, initial: initial, flags: .money)
            XCTAssertTrue(try plain.lower() == lo && plain.upper() == hi && plain.flags() == [] && money.flags() == .money)
            for s in objs(c["steps"]) {
                let pol = S(s["policy"]) == "money" ? money : plain
                let synth: [Int64]? = isNull(s["synth"]) ? nil : vec(s["synth"])
                var out: Paradox.Outcome?
                XCTAssertEqual(st { out = try book.constructive(pol, kind: kind(s["kind"]), tag: tag(s["tag"]), synth: synth,
                                                                tension: U32(s["tension"])) }, S(s["status"]))
                if s["outcome"] != nil { XCTAssertEqual(out.map { Int64($0.rawValue) }, I(s["outcome"])) }
                XCTAssertEqual(try pol.state(), Paradox.PolicyState(values: vec(s["values"]), previous: vec(s["prev"]),
                                                                    armed: B(s["armed"]), tensionAt: U32(s["tension_at"])))
                if s["proposal"] != nil { XCTAssertEqual(hx(try pol.proposal(vec(s["synth"]))), S(s["proposal"])) }
            }
            XCTAssertEqual(st { _ = try Paradox.Policy(cap, lower: lo, upper: hi, initial: [0, 0, 0]) }, "ZXV_E_RANGE")
            XCTAssertEqual(st { _ = try Paradox.Policy(cap, lower: lo, upper: hi, initial: [1]) }, "ZXV_E_ARG")
            XCTAssertEqual(st { _ = try book.constructive(plain, kind: .constructive, tag: Hexagram.all[47], synth: [1], tension: 0) },
                           "ZXV_E_ARG")
        }
        // destructive
        for d in objs(p["destructive"]) {
            var state = utf8(S(d["state_utf8"]))
            let ck = utf8(S(d["checkpoint_utf8"]))
            let c = Paradox.Containment(steps: U32(d["steps"]), stepLimit: U32(d["step_limit"]), spent: U32(d["spent"]),
                                        budget: U32(d["budget"]), trace: list(d["trace"]).map { U($0) },
                                        invariantOk: B(d["invariant_ok"]), pattern: utf8(S(d["pattern_utf8"])),
                                        submitter: U32(d["submitter"]))
            var r: Paradox.Contained?
            XCTAssertEqual(st { r = try book.destructive(kind: kind(d["kind"]), tag: tag(d["tag"]), state: &state, checkpoint: ck,
                                                         containment: c) }, S(d["status"]))
            XCTAssertEqual(r.map { Int64($0.outcome.rawValue) }, I(d["outcome"]))
            XCTAssertEqual(hx(state), S(d["state_after"]))
            XCTAssertEqual(r?.proof != nil, d["repair_proof"] != nil)
            guard let proof = r?.proof else { continue }
            XCTAssertEqual(hx(proof.bytes), S(d["repair_proof"]))
            XCTAssertEqual(try proof.verify(), S(d["repair_verify"]) == "ZXV_OK")
            var flipped = proof.bytes
            flipped[0] ^= 1
            XCTAssertEqual(try Paradox.RepairProof(bytes: flipped).verify(), S(d["repair_verify_flipped_byte0"]) == "ZXV_OK")
            XCTAssertEqual(proof.signature, try Paradox.signature(S(d["pattern_utf8"])))
            XCTAssertTrue(proof.submitter == U32(d["submitter"]) && proof.stateLength == UInt32(state.count))
            XCTAssertTrue(proof.trips.isSuperset(of: [.steps, .cycle]))
            XCTAssertEqual(proof.quarantineIndex, 0)
            let q = try book.quarantineRecord(at: 0)
            XCTAssertTrue(q.subject == proof.proofHash && q.submitter == U32(d["submitter"]) && UInt32(q.reason) == proof.trips.rawValue)
            XCTAssertEqual(q.tag.polarity, .forbidden)
        }
        XCTAssertEqual(st { _ = try Paradox.RepairProof(bytes: [UInt8](repeating: 0, count: 10)) }, "ZXV_E_ARG")
        // neutral
        do {
            let n = p["neutral"] as! J
            let hold = try Paradox.Hold(cap, capacity: U32(n["capacity"]), decisive: U32(n["decisive"]))
            let ids = list(n["branches"]).map { hex(S($0)) }
            for id in ids {
                try hold.add(id)
                try hold.evidence(id, positive: true)
            }
            try hold.add(ids[0]) // the same id again is the same candidate
            XCTAssertEqual(try hold.info().count, UInt32(ids.count))
            XCTAssertEqual(try hold.info().settled, -1)
            for s in objs(n["steps"]) {
                if s["before"] != nil { continue }
                for _ in 0..<U(s["add_for_branch0"]) { try hold.evidence(ids[0], positive: true) }
                let pick = I32(s["owner_pick"])
                var r: Paradox.Settlement?
                XCTAssertEqual(st { r = try book.neutral(hold, kind: kind(s["kind"]), tag: tag(s["tag"]),
                                                         ownerPick: pick < 0 ? nil : UInt32(pick)) }, S(s["status"]))
                XCTAssertEqual(r.map { Int64($0.outcome.rawValue) }, I(s["outcome"]))
                XCTAssertEqual(r?.settled, I32(s["settled"]))
            }
            XCTAssertEqual(try hold.info().settled, 0)
            XCTAssertEqual(try hold.branch(0), Paradox.Branch(id: ids[0], pos: 3, neg: 0))
            XCTAssertEqual(try hold.branch(1).pos, 1)
            XCTAssertEqual(st { try hold.add(fill32(0x09)) }, "ZXV_E_STATE") // settled
            XCTAssertEqual(st { _ = try hold.branch(9) }, "ZXV_E_NOT_FOUND")
        }
        // special, drafts, reviews
        do {
            let s = p["special"] as! J
            let sig = try Paradox.signature(S(s["sig_utf8"]))
            XCTAssertEqual(hx(sig), S(s["sig"]))
            var fe: Paradox.FrontierEntry?
            XCTAssertEqual(st { fe = try book.special(kind: kind(s["kind"]), tag: tag(s["tag"]), signature: sig,
                                                      subject: hex(S(s["subject"]))) }, S(s["status"]))
            XCTAssertEqual(fe.map { Int64($0.outcome.rawValue) }, I(s["outcome"]))
            XCTAssertEqual(fe?.record, U32(s["rec"]))
            let fr0 = try book.opRecord(.frontier, at: 0)
            XCTAssertTrue(fr0.a == sig && fr0.b == hex(S(s["subject"])))
            var drafts: [String: Paradox.Draft] = [:]
            for name in ["draft", "draft_money"] {
                let d = s[name] as! J
                var dr: Paradox.Draft?
                XCTAssertEqual(st { dr = try book.draft(record: fe?.record ?? 0, kind: kind(d["kind"]), reason: U8(d["reason"]),
                                                        flags: Paradox.Flags(rawValue: U8(d["flags"]))) }, S(d["status"]))
                guard let dr else { continue }
                XCTAssertEqual(hx(dr.bytes), S(d["bytes"]))
                XCTAssertTrue(dr.record == U32(s["rec"]) && dr.signature == sig && Int64(dr.kindRaw) == I(d["kind"]) &&
                              dr.reason == U8(d["reason"]) && dr.flags.rawValue == U8(d["flags"]))
                XCTAssertEqual(try Paradox.Draft(bytes: hex(S(d["bytes"]))), dr)
                drafts[name] = dr
            }
            for r in objs(s["review"]) {
                var out: Paradox.Outcome?
                XCTAssertEqual(st { out = try book.review(drafts[S(r["draft"])]!, ownerOk: B(r["owner_ok"]),
                                                          consensusOk: B(r["consensus_ok"])) }, S(r["status"]))
                XCTAssertEqual(out.map { Int64($0.rawValue) }, I(r["outcome"]))
            }
            XCTAssertEqual(try book.registryCount(), U32(s["registry_count_after"]))
            var bad = drafts["draft"]!.bytes
            bad[10] ^= 1 // the record hash no longer matches
            XCTAssertEqual(zerr { _ = try book.review(try Paradox.Draft(bytes: bad), ownerOk: true, consensusOk: true) }?.refusal,
                           .verificationFailed)
            XCTAssertEqual(st { _ = try book.draft(record: 99, kind: .neutral, reason: 0) }, "ZXV_E_NOT_FOUND")
        }
        // graphs
        for g in objs(p["graphs"]) {
            let graph = try Paradox.Graph(cap, stepLimit: U32(g["step_limit"]))
            func id(_ v: Any?) -> [UInt8] { fill32(S(v).utf8.first!) }
            for n in objs(g["nodes"]) {
                XCTAssertEqual(st { try graph.addNode(id(n["id"]), kind: kind(n["kind"]), tag: tag(n["tag"]), magnitude: U32(n["mag"]),
                                                      direction: I32(n["dir"])) }, S(n["status"]))
            }
            for d in objs(g["deps"]) { XCTAssertEqual(st { try graph.addDependency(id(d["id"]), on: id(d["dep"])) }, S(d["status"])) }
            XCTAssertEqual(st { _ = try graph.node(at: 0) }, "ZXV_E_STATE") // before the first pass
            for ps in objs(g["passes"]) {
                var out: Paradox.Outcome?
                XCTAssertEqual(st { out = try book.pass(graph) }, S(ps["status"]))
                if S(ps["status"]) == "ZXV_OK" { XCTAssertEqual(out.map { Int64($0.rawValue) }, I(ps["outcome"])) }
                let i = try graph.info()
                XCTAssertEqual(i, Paradox.GraphInfo(nodes: UInt32(objs(g["nodes"]).count), passes: U32(ps["pass"]),
                                                    totalMagnitude: U32(ps["total_mag"]), totalDirection: I32(ps["total_dir"]),
                                                    escalated: B(ps["escalated"]), chain: hex(S(ps["chain"]))))
                for (k, v) in ps["states"] as! J {
                    XCTAssertEqual(Int64(try graph.state(fill32(k.utf8.first!)).rawValue), I(v))
                }
            }
            XCTAssertEqual(try graph.node(at: 0).id, id(objs(g["nodes"])[0]["id"])) // id order
            XCTAssertEqual(st { try graph.addNode(fill32(0x7A), kind: .none, tag: Hexagram.all[55], magnitude: 0, direction: 0) },
                           "ZXV_E_STATE")
        }
        try checkHeads(book, p["heads_final"])
        let g1 = try Paradox.Graph(cap, stepLimit: 1)
        try g1.addNode(fill32(UInt8(ascii: "S")), kind: .special, tag: Hexagram.all[36], magnitude: 1, direction: 0)
        XCTAssertEqual(zerr { _ = try book.pass(g1) }?.refusal, .paradoxStepLimit)
        let fin = (p["heads_final"] as! J)["ops"] as! J
        XCTAssertEqual(try book.logCount(.ops), U32(fin["count"]) + 1) // the step-limit pass is logged
    }

    /// Replay the paradox vector steps that write the book, in order, on a
    /// fresh book, and compare the final heads, every log record and verify.
    func testParadoxLogs() throws {
        let p = sec("paradox")
        let ctx = try ZxvContext()
        let cap = try ctx.rootCapability(.all1_5)
        let book = try Paradox.Book(cap, logCapacity: 64, registryCapacity: 16)
        let sigL = try Paradox.signature("loop:A->B->A"), sigM = try Paradox.signature("money:drain")
        try book.registryAdd(sigL, kind: .destructive, reason: 1)
        XCTAssertEqual(st { try book.registryAdd(sigM, kind: .neutral, reason: 2, flags: .money, ownerOk: false) }, "ZXV_E_PARADOX_OWNER")
        try book.registryAdd(sigM, kind: .neutral, reason: 2, flags: .money, ownerOk: true)
        XCTAssertEqual(st { try book.registryAdd(sigM, kind: .neutral, reason: 2, flags: .money, ownerOk: true) }, "ZXV_E_DUPLICATE")
        try checkHeads(book, p["heads_after_classify"])
        let lo: [Int64] = [0, 0, 10], hi: [Int64] = [100, 50, 20], initial: [Int64] = [50, 25, 15]
        let plain = try Paradox.Policy(cap, lower: lo, upper: hi, initial: initial)
        let money = try Paradox.Policy(cap, lower: lo, upper: hi, initial: initial, flags: .money)
        let ct = Hexagram.all[47], nt = Hexagram.all[39]
        _ = try book.constructive(plain, kind: .constructive, tag: ct, synth: [60, 20, 12], tension: 5)
        _ = try book.constructive(plain, kind: .constructive, tag: ct, tension: 3)
        _ = try book.constructive(plain, kind: .constructive, tag: ct, synth: [200, 20, 12], tension: 3)
        _ = try book.constructive(plain, kind: .constructive, tag: ct, synth: [55, 30, 18], tension: 4)
        _ = try book.constructive(plain, kind: .constructive, tag: ct, tension: 9)
        _ = try book.constructive(plain, kind: .neutral, tag: nt, synth: [60, 20, 12], tension: 1)
        XCTAssertEqual(st { _ = try book.constructive(plain, kind: .neutral, tag: ct, synth: [60, 20, 12], tension: 1) }, "ZXV_E_ARG")
        _ = try book.constructive(money, kind: .constructive, tag: ct, synth: [60, 20, 12], tension: 1)
        do {
            var state = [UInt8](repeating: UInt8(ascii: "X"), count: 16)
            let ck = [UInt8](repeating: UInt8(ascii: "A"), count: 16)
            let c = Paradox.Containment(steps: 10, stepLimit: 8, spent: 3, budget: 100, trace: [1, 2, 3, 2], invariantOk: true,
                                        pattern: utf8("loop:A->B->A"), submitter: 7)
            XCTAssertEqual(try book.destructive(kind: .destructive, tag: Hexagram.all[35], state: &state, checkpoint: ck,
                                                containment: c).outcome, .contained)
            XCTAssertEqual(state, ck)
            state = [UInt8](repeating: UInt8(ascii: "X"), count: 16)
            let c2 = Paradox.Containment(steps: 1, stepLimit: 8, submitter: 7)
            XCTAssertEqual(try book.destructive(kind: .constructive, tag: ct, state: &state, checkpoint: ck, containment: c2).outcome,
                           .refused)
        }
        do {
            let h = try Paradox.Hold(cap, capacity: 4, decisive: 2)
            let i1 = fill32(1), i2 = fill32(2)
            try h.add(i1)
            try h.add(i2)
            try h.evidence(i1, positive: true)
            try h.evidence(i2, positive: true)
            _ = try book.neutral(h, kind: .neutral, tag: nt)
            _ = try book.neutral(h, kind: .neutral, tag: Hexagram.all[21])
            _ = try book.neutral(h, kind: .neutral, tag: Hexagram.all[43])
            _ = try book.neutral(h, kind: .constructive, tag: ct)
            try h.evidence(i1, positive: true)
            try h.evidence(i1, positive: true)
            XCTAssertEqual(try book.neutral(h, kind: .neutral, tag: nt).outcome, .settled)
        }
        do {
            let subj = (0..<32).map { UInt8(0x80 + $0) }
            let fe = try book.special(kind: .special, tag: Hexagram.all[36], signature: try Paradox.signature("frontier:new"), subject: subj)
            let d = try book.draft(record: fe.record!, kind: .destructive, reason: 3)
            let dm = try book.draft(record: fe.record!, kind: .neutral, reason: 4, flags: .money)
            _ = try book.review(dm, ownerOk: false, consensusOk: true)
            _ = try book.review(d, ownerOk: false, consensusOk: false)
            _ = try book.review(d, ownerOk: false, consensusOk: true)
            _ = try book.review(d, ownerOk: true, consensusOk: false)
        }
        func graph(_ limit: UInt32, _ names: String, _ k: [Paradox.Kind], _ t: [Int], _ m: [UInt32], _ d: [Int32],
                   _ deps: String) throws {
            let g = try Paradox.Graph(cap, stepLimit: limit)
            for (i, ch) in names.utf8.enumerated() {
                try g.addNode(fill32(ch), kind: k[i], tag: Hexagram.all[t[i]], magnitude: m[i], direction: d[i])
            }
            let dd = Array(deps.utf8)
            for i in stride(from: 0, to: dd.count - 1, by: 2) { try g.addDependency(fill32(dd[i]), on: fill32(dd[i + 1])) }
            for _ in 0..<4 {
                var o: Paradox.Outcome?
                if zerr({ o = try book.pass(g) }) != nil || o == .resolved { break }
            }
        }
        try graph(4, "ABCD", [.constructive, .destructive, .constructive, .none], [47, 35, 47, 55], [3, 5, 2, 0], [1, -1, 1, 0], "CBDA")
        try graph(2, "EFG", [.special, .constructive, .neutral], [36, 47, 21], [1, 4, 2], [0, 1, 0], "FE")
        try checkHeads(book, p["heads_final"])
        let logs = p["logs"] as! J
        for (name, which) in [("ops", Paradox.Log.ops), ("frontier", .frontier), ("quarantine", .quarantine)] {
            let want = list(logs[name])
            XCTAssertEqual(try book.logCount(which), UInt32(want.count))
            for (i, w) in want.enumerated() { XCTAssertEqual(hx(try book.logRecord(which, at: UInt32(i))), S(w)) }
            XCTAssertEqual(st { _ = try book.logRecord(which, at: UInt32(want.count)) }, "ZXV_E_NOT_FOUND")
        }
        // Parsed records chain: each prev is the previous hash.
        let n = try book.logCount(.ops)
        for i in 1..<n {
            let r = try book.opRecord(.ops, at: i), prev = try book.opRecord(.ops, at: i - 1)
            XCTAssertTrue(r.prev == prev.hash && r.seq == UInt64(i))
        }
        XCTAssertEqual(try book.opRecord(.ops, at: n - 1).hash, try book.logHead(.ops))
        XCTAssertEqual(try book.quarantineRecord(at: 0).hash, try book.logHead(.quarantine))
        XCTAssertEqual(st { _ = try book.opRecord(.quarantine, at: 0) }, "ZXV_E_ARG")
        for (w, s) in list(p["log_verify"]).enumerated() {
            XCTAssertEqual(st { try book.logVerify(Paradox.Log(rawValue: Int32(w))!) }, S(s))
        }
    }

    // MARK: lifetimes, buffer growth, capabilities, poisoned context

    func testHandleLifetimes() throws {
        let ctx = try ZxvContext()
        let cap = try ctx.rootCapability(.all1_5)
        // A closed handle refuses use with .usage (ZXV_E_ARG), never reaching C.
        let a = try Paradox.Book(cap, logCapacity: 4, registryCapacity: 4)
        XCTAssertEqual(try a.logCount(.ops), 0)
        a.close()
        a.close() // idempotent
        XCTAssertEqual(st { _ = try a.logCount(.ops) }, "ZXV_E_ARG")
        XCTAssertEqual(zerr { _ = try a.logCount(.ops) }?.statusClass, .usage)
        let w = try SwarmToken.Wallet(cap, modelId: 1, octane: 0), w2 = try SwarmToken.Wallet(cap, modelId: 2, octane: 0)
        w.close()
        XCTAssertEqual(st { try w.mint(1) }, "ZXV_E_ARG")
        try w2.mint(5)
        XCTAssertEqual(st { try w2.transfer(to: w, amount: 1) }, "ZXV_E_ARG")
        XCTAssertEqual(try w2.info().value, 5) // nothing moved
        let t = try Treaty(sutra: "TREATY N USD NET\nPARTY A 1 1 1/1\nEND\n")
        XCTAssertTrue(try t.isValid())
        t.close()
        XCTAssertEqual(st { _ = try t.sutra() }, "ZXV_E_ARG")
        let ag = try Web4.Agent(cap, name: "temp", nowMs: 1, seed: [UInt8](repeating: 7, count: 32))
        XCTAssertGreaterThan(try ag.card().count, Web4.sigBytes)
        ag.close() // the key is wiped
        XCTAssertEqual(st { _ = try ag.agentId() }, "ZXV_E_ARG")
        XCTAssertEqual(st { _ = try ag.card() }, "ZXV_E_ARG")
        let man = try Web4.Manifest(version: 1, issuedMs: 0, expiresMs: 10)
        XCTAssertEqual(st { try man.sign(with: ag) }, "ZXV_E_ARG")
        let j = try Freight.Join(capacity: 1024)
        j.close()
        XCTAssertEqual(st { _ = try j.status() }, "ZXV_E_ARG")
        let h = try VHub.Hub(cap)
        h.close()
        XCTAssertEqual(st { _ = try h.check() }, "ZXV_E_ARG")
        let n = try Aipi.Node(cap, storeBytes: 1 << 20, maxBlocks: 4)
        n.close()
        XCTAssertEqual(st { _ = try n.info() }, "ZXV_E_ARG")
        let b = try Paradox.Book(cap, logCapacity: 4, registryCapacity: 4)
        let hd = try Paradox.Hold(cap, capacity: 2)
        hd.close()
        XCTAssertEqual(st { _ = try b.neutral(hd, kind: .neutral, tag: Hexagram.all[39]) }, "ZXV_E_ARG")
        XCTAssertEqual(try b.logCount(.ops), 0) // nothing ran
        let r = try t2Result()
        r.close()
        XCTAssertEqual(st { _ = try r.fees() }, "ZXV_E_ARG")
        // Handles outlive the context and capability objects that made them.
        var keep: Paradox.Book?
        do {
            let c2 = try ZxvContext()
            let k2 = try c2.rootCapability(.paradox)
            keep = try Paradox.Book(k2, logCapacity: 4, registryCapacity: 4)
        }
        XCTAssertEqual(try keep!.logCount(.ops), 0)
        try keep!.registryAdd(try Paradox.signature("x"), kind: .neutral, reason: 0)
        XCTAssertEqual(try keep!.registryCount(), 1)
        keep = nil // deinit releases it
        // Many handles created and dropped: each is released exactly once.
        for i in 0..<64 {
            _ = try SwarmToken.Wallet(cap, modelId: UInt32(i), octane: 0)
            let g = try Paradox.Graph(cap, stepLimit: 1)
            if i % 2 == 0 { g.close() }
        }
        // A close from another thread while a call runs: the call finishes,
        // then the handle is released; no use-after-free.
        let shared = try Paradox.Book(cap, logCapacity: 64, registryCapacity: 64)
        DispatchQueue.concurrentPerform(iterations: 8) { i in
            if i == 4 { shared.close() } else { _ = try? shared.logHead(.ops) }
        }
        XCTAssertEqual(st { _ = try shared.logCount(.ops) }, "ZXV_E_ARG")
    }

    private func t2Result() throws -> Treaty.Result {
        try Treaty(sutra: "TREATY N USD NET\nPARTY A 1 1 1/1\nEND\n").run()
    }

    func testBufferGrowth() throws {
        // The two-call pattern: a buffer that is too small is grown once to
        // the reported length, for text and for bytes.
        let input = utf8("the quick brown fox jumps over the lazy dog; the quick brown fox")
        let direct = try Freight.encodeOps(input, withSha: true)
        var calls = 0
        let grown = try readBytes(guess: 1) { b, c, n in
            calls += 1
            return zxv_freight_ops_encode(input, input.count, 1, b, c, n)
        }
        XCTAssertTrue(calls == 2 && grown == direct)
        calls = 0
        let emptyFirst = try readBytes(guess: 0) { b, c, n in
            calls += 1
            return zxv_freight_ops_encode(input, input.count, 1, b, c, n)
        }
        XCTAssertTrue(calls == 2 && emptyFirst == direct)
        calls = 0
        let roomy = try readBytes(guess: 4096) { b, c, n in
            calls += 1
            return zxv_freight_ops_encode(input, input.count, 1, b, c, n)
        }
        XCTAssertTrue(calls == 1 && roomy == direct)
        // Large results, all past the default 256-byte guess.
        let data = [UInt8](repeating: 0x42, count: 10000)
        let big = try Freight.split(freightId: 5, k: 168, data: data, seq: 0)
        let j = try Freight.Join(capacity: 20000)
        for s in 0..<(try Freight.setCount(length: 10000, k: 168)) {
            try j.add(try Freight.split(freightId: 5, k: 168, data: data, seq: s))
        }
        XCTAssertEqual(try j.result(), data)
        XCTAssertEqual(try Freight.decode(big.header, packets: big.packets).count, Freight.maxPayload)
        let t = try Treaty(sutra: "TREATY CORRIDOR-7 USD SETL\nPARTY ACME 5000000 4000 1/3\nPARTY BETA 3000000 3000 1/7\n" +
                                  "PARTY GAMMA 900000 0 11/21\nOWE ACME BETA 1200000\nOWE BETA GAMMA 700000\n" +
                                  "OWE GAMMA ACME 100000\nEND\n").sutra()
        XCTAssertGreaterThan(t.count, 100)
        // Text two-call: the manifest JSON.
        let m = try Web4.Manifest(version: 1, issuedMs: 0, expiresMs: 10)
        for i in 0..<16 { try m.addTool(Web4.Tool(name: "tool\(i)", priceVfv: 1)) }
        let ctx = try ZxvContext()
        let cap = try ctx.rootCapability(.all1_5)
        let a = try Web4.Agent(cap, name: "signer", nowMs: 0)
        try m.sign(with: a)
        XCTAssertGreaterThan(try m.toJSON().count, 1000)
        XCTAssertEqual(st { try m.addTool(Web4.Tool(name: "tool16", priceVfv: 1)) }, "ZXV_E_CAPACITY")
        XCTAssertGreaterThan(try m.encode().count, Web4.sigBytes)
    }

    func testErrorMappingPerStatus() throws {
        // Each new status reached through a real call, with its class.
        XCTAssertEqual(zerr { _ = try SwarmToken.tier(face: 4) }?.statusName, "ZXV_E_DENOM")
        XCTAssertEqual(zerr { _ = try Treaty(sutra: "NOPE") }?.statusClass, .usage)
        XCTAssertEqual(zerr { try Cid.compute(utf8("a")).verify(utf8("b")) }?.refusal, .cidMismatch)
        let h = try Freight.Header.forPayload(freightId: 1, k: 4, payload: utf8("abc"))
        let pk = try Freight.encode(h, payload: utf8("abc"))
        XCTAssertEqual(zerr { _ = try Freight.decode(h, packets: Array(pk.prefix(21 * 3))) }?.refusal, .erasure)
        let ctx = try ZxvContext()
        let cap = try ctx.rootCapability(.all1_5)
        let hub = try VHub.Hub(cap)
        let key = try MlDsa87(cap).keygen(seed: fill32(0x40))
        try hub.addCurrency(slot: 0, code: "ZTA", minorUnits: 2, issuerPublicKey: key.publicKey, poolFunding: 1000, vbillBacking: 1000)
        try hub.addCurrency(slot: 1, code: "ZTB", minorUnits: 2, issuerPublicKey: key.publicKey, poolFunding: 1000, vbillBacking: 1000)
        XCTAssertEqual(zerr { _ = try hub.quote(from: 0, to: 1, want: 100) }?.refusal, .pegRefused)
        let human = try Web4.Agent(cap, name: "h", nowMs: 0)
        let log = try Web4.ConsentLog(capacity: 2)
        XCTAssertEqual(zerr { try log.check(nil, humanPublicKey: try human.publicKey(), agent: fill32(1), action: fill32(2),
                                            need: .money, amount: 1, nowMs: 1) }?.refusal, .consentDenied)
        let book = try Paradox.Book(cap, logCapacity: 8, registryCapacity: 8)
        XCTAssertEqual(zerr { try book.registryAdd(fill32(3), kind: .neutral, reason: 0, flags: .device) }?.refusal,
                       .ownerApprovalRequired)
        // LIMIT: a limit rule refusing a swap.
        let pegs: [(UInt8, UInt64)] = [(0, 1), (1, 1)]
        for (slot, epoch) in pegs {
            let msg = try VHub.pegMessage(slot: slot, num: 1, den: 1, epoch: epoch)
            try hub.setPeg(slot: slot, num: 1, den: 1, epoch: epoch, signature: try MlDsa87(cap).sign(msg.bytes, with: key.secretKey))
        }
        try hub.addLimit(slot: 0, limit: 10)
        try hub.addLimit(slot: 1, limit: 1000)
        let s = try hub.openAccount(slot: 0, owner: 1), r = try hub.openAccount(slot: 1, owner: 2)
        try hub.issue(account: s, amount: 500)
        XCTAssertEqual(try hub.swap(from: 0, to: 1, sender: s, receiver: r, amount: 10, tick: 1), VHub.Swap(vbills: 10, received: 10))
        XCTAssertEqual(zerr { _ = try hub.swap(from: 0, to: 1, sender: s, receiver: r, amount: 10, tick: 2) }?.refusal, .limitRefused)
        // Embedded NUL bytes never reach C.
        XCTAssertEqual(st { try Web4.Manifest(version: 1, issuedMs: 0, expiresMs: 9).addTool(Web4.Tool(name: "a\u{0}b", priceVfv: 1)) },
                       "ZXV_E_ARG")
    }

    func testCapabilityRefusals() throws {
        let ctx = try ZxvContext()
        // A capability without AIPI, MINT or PARADOX is refused (.capabilityDenied).
        let weak = try ctx.rootCapability(.all1_4)
        XCTAssertEqual(zerr { _ = try Aipi.Node(weak, storeBytes: 1 << 20, maxBlocks: 4) }?.refusal, .capabilityDenied)
        XCTAssertEqual(st { _ = try Aipi.Node(weak, storeBytes: 1 << 20, maxBlocks: 4) }, "ZXV_E_CAP")
        XCTAssertEqual(zerr { _ = try SwarmToken.Wallet(weak, modelId: 1, octane: 0) }?.refusal, .capabilityDenied)
        XCTAssertEqual(zerr { _ = try Paradox.Book(weak, logCapacity: 4, registryCapacity: 4) }?.refusal, .capabilityDenied)
        XCTAssertEqual(zerr { _ = try Paradox.Policy(weak, lower: [0], upper: [1], initial: [0]) }?.refusal, .capabilityDenied)
        XCTAssertEqual(zerr { _ = try Paradox.Hold(weak, capacity: 2) }?.refusal, .capabilityDenied)
        XCTAssertEqual(zerr { _ = try Paradox.Graph(weak, stepLimit: 2) }?.refusal, .capabilityDenied)
        // Web4 needs crypto_sign; consent needs attest too.
        let noSign = try ctx.rootCapability(.all1_5.subtracting(.cryptoSign))
        XCTAssertEqual(zerr { _ = try Web4.Agent(noSign, name: "x", nowMs: 0) }?.refusal, .capabilityDenied)
        let signOnly = try ctx.rootCapability(.cryptoSign)
        let human = try Web4.Agent(signOnly, name: "human", nowMs: 0)
        XCTAssertEqual(zerr { _ = try human.issueConsent(agent: fill32(1), action: fill32(2), scope: .money, maxVfv: 1,
                                                         issuedMs: 0, expiresMs: 10) }?.refusal, .capabilityDenied)
        // The hub needs ledger_post to create and ledger_read to read.
        let postOnly = try ctx.rootCapability(.ledgerPost)
        let hub = try VHub.Hub(postOnly)
        XCTAssertEqual(zerr { _ = try hub.check() }?.refusal, .capabilityDenied)
        // Handles made with ONLY the 1.5 bit work for that group.
        let mintOnly = try ctx.rootCapability(.mint)
        let w = try SwarmToken.Wallet(mintOnly, modelId: 1, octane: 0)
        try w.mint(10)
        XCTAssertEqual(try w.info().value, 10)
        let aipiOnly = try ctx.rootCapability(.aipi)
        let n = try Aipi.Node(aipiOnly, storeBytes: 1 << 20, maxBlocks: 4)
        XCTAssertGreaterThan(try n.put(utf8("x")).count, 0)
        let pdxOnly = try ctx.rootCapability(.paradox)
        let b = try Paradox.Book(pdxOnly, logCapacity: 4, registryCapacity: 4)
        try b.registryAdd(try Paradox.signature("y"), kind: .neutral, reason: 0)
        XCTAssertEqual(try b.registryCount(), 1)
        // A revoked capability: the handle keeps it, so later gated calls fail.
        let child = try pdxOnly.attenuate(.paradox)
        let b2 = try Paradox.Book(child, logCapacity: 4, registryCapacity: 4)
        try child.revoke()
        XCTAssertEqual(zerr { try b2.registryAdd(try Paradox.signature("z"), kind: .neutral, reason: 0) }?.refusal, .capabilityDenied)
        // A handle from another context's capability is refused (no mixing).
        let other = try ZxvContext()
        let ocap = try other.rootCapability(.all1_5)
        let foreign = try Paradox.Hold(ocap, capacity: 2)
        XCTAssertEqual(zerr { _ = try b.neutral(foreign, kind: .neutral, tag: Hexagram.all[39]) }?.statusClass, .usage)
    }

    func testPoisonedContext() throws {
        let ctx = try ZxvContext()
        let cap = try ctx.rootCapability(.all1_5)
        let book = try Paradox.Book(cap, logCapacity: 8, registryCapacity: 8)
        let w = try SwarmToken.Wallet(cap, modelId: 1, octane: 0)
        let hub = try VHub.Hub(cap)
        let ag = try Web4.Agent(cap, name: "a", nowMs: 0)
        let node = try Aipi.Node(cap, storeBytes: 1 << 20, maxBlocks: 4)
        try w.mint(5)
        try ctx.halt("test")
        XCTAssertTrue(ctx.isPoisoned)
        // Every call on a handle of a halted context fails closed.
        let e = zerr { try book.registryAdd(try Paradox.signature("z"), kind: .neutral, reason: 0) }
        if case .fatal? = e {} else { XCTFail("expected .fatal, got \(String(describing: e))") }
        XCTAssertEqual(e?.statusName, "ZXV_E_POISONED")
        XCTAssertEqual(st { _ = try book.logCount(.ops) }, "ZXV_E_POISONED")
        XCTAssertEqual(st { try w.mint(1) }, "ZXV_E_POISONED")
        XCTAssertEqual(st { _ = try w.info() }, "ZXV_E_POISONED")
        XCTAssertEqual(st { _ = try hub.check() }, "ZXV_E_POISONED")
        XCTAssertEqual(st { _ = try ag.card() }, "ZXV_E_POISONED")
        XCTAssertEqual(st { _ = try node.get(try Cid.compute(utf8("x"))) }, "ZXV_E_POISONED")
        XCTAssertEqual(st { _ = try Paradox.Book(cap, logCapacity: 1, registryCapacity: 1) }, "ZXV_E_POISONED")
        XCTAssertEqual(st { _ = try Aipi.Node(cap, storeBytes: 1 << 20, maxBlocks: 1) }, "ZXV_E_POISONED")
        // Pure functions are unaffected.
        XCTAssertEqual(try Fee.assure(1125), 1)
        XCTAssertEqual(try Coin.face(rung: 5), 8)
    }
}
