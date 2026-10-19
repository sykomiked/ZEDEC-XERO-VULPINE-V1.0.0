// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// ABI 1.6: the vino group (Vino Embed: the play lane, payment requests,
// receipts and the daily meter) against the real libzxv and the
// status_abi16 and vino sections of sdk/conformance/vectors.json, plus
// closed-handle refusals, the voucher-list buffer path, error mapping,
// capability refusals, a poisoned context and the README examples.
// Mirrors sdk/cpp/tests/test_zxv_abi16.cpp. 64-bit values in the vectors
// are decimal strings.

import CZxv
import Foundation
import XCTest
@testable import Zxv

private typealias J = [String: Any]

private func vino() -> J { vectors["vino"] as! J }
private func objs(_ x: Any?) -> [J] { (x as! [Any]).map { $0 as! J } }
private func S(_ x: Any?) -> String { x as! String }
/// A decimal string or a JSON number.
private func U(_ x: Any?) -> UInt64 {
    if let s = x as? String { return UInt64(s)! }
    if let n = x as? NSNumber { return n.uint64Value }
    fatalError("not an unsigned number: \(String(describing: x))")
}
private func I(_ x: Any?) -> Int64 {
    if let s = x as? String { return Int64(s)! }
    if let n = x as? NSNumber { return n.int64Value }
    fatalError("not a number: \(String(describing: x))")
}
private func U32(_ x: Any?) -> UInt32 { UInt32(U(x)) }
private func I32(_ x: Any?) -> Int32 { Int32(I(x)) }
private func hx(_ b: [UInt8]) -> String { hexString(b) }

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

private func request(_ r: J) -> Vino.Request {
    Vino.Request(lane: Vino.Lane(rawValue: I32(r["lane"])), flags: Vino.RequestFlags(rawValue: U32(r["flags"])),
                 merchant: hex(S(r["merchant"])), nonce: hex(S(r["nonce"])), amount: U(r["amount"]),
                 createdAt: U(r["created_at"]), expiresAt: U(r["expires_at"]), memo: S(r["memo"]))
}

private func goodRequest() -> [UInt8] { hex(S(objs(vino()["requests"])[0]["bytes"])) }

final class VinoTests: XCTestCase {
    // MARK: status_abi16, constants

    func testStatusAbi16() throws {
        XCTAssertGreaterThanOrEqual(ZxvLibrary.version.minor, 6)
        XCTAssertTrue(ZxvLibrary.features2.contains(.vino))
        XCTAssertTrue(ZxvLibrary.features2.isSuperset(of: .all1_6))
        XCTAssertEqual(Features2.vino.rawValue, 0x100)
        XCTAssertEqual(CapabilitySet.vino.rawValue, 1 << 15)
        XCTAssertEqual(CapabilitySet.all1_6.rawValue, (1 << 16) - 1)
        XCTAssertTrue(CapabilitySet.all1_6.isSuperset(of: .all1_5))
        XCTAssertFalse(CapabilitySet.all1_5.contains(.vino))
        XCTAssertEqual([Vino.requestBytes, Vino.requestIdBytes, Vino.receiptMessageBytes], [160, 32, 88])
        XCTAssertEqual([Vino.walletPublicKeyBytes, Vino.walletSecretKeyBytes, Vino.receiptSignatureBytes],
                       [2592, 4896, 4627])
        XCTAssertEqual(Vino.amountMax, UInt64(1) << 59)
        // 1<<15 is now a known capability bit; 1<<16 is the first unknown one.
        let ctx = try ZxvContext()
        XCTAssertEqual(st { _ = try ctx.rootCapability(.vino) }, "ZXV_OK")
        XCTAssertEqual(st { _ = try ctx.rootCapability(.all1_6) }, "ZXV_OK")
        XCTAssertEqual(st { _ = try ctx.rootCapability(CapabilitySet(rawValue: CapabilitySet.all1_6.rawValue + 1)) },
                       "ZXV_E_ARG")

        for s in objs(vectors["status_abi16"]) {
            let code = I32(s["code"]), name = S(s["name"]), cls = I(s["class"])
            XCTAssertEqual(ZxvLibrary.statusName(code), name)
            XCTAssertEqual(Int64(StatusClass(status: code).rawValue), cls)
            switch ZxvError(status: code, detail: "probe") {
            case .usage(let e): XCTAssertTrue(cls == 1 && e.status == code && e.statusName == name)
            case .refused(let e): XCTAssertTrue(cls == 2 && e.status == code && e.detail == "probe")
            default: XCTFail("\(name) mapped to the wrong class")
            }
        }
        // Each new status has its own reason (or flag) under its class.
        let reasons: [(Int32, ZxvRefusedError.Reason)] = [
            (ZXV_E_RAIL, .railMismatch), (ZXV_E_COVERAGE, .coverageRefused),
            (ZXV_E_EXPIRED, .requestExpired), (ZXV_E_CHANCE, .paidChanceRefused),
        ]
        for (code, reason) in reasons {
            XCTAssertEqual(ZxvError(status: code, detail: "").refusal, reason)
        }
        XCTAssertTrue(ZxvUsageError(status: ZXV_E_LANE, detail: "").isLane)
        if case .usage(let e) = ZxvError(status: ZXV_E_LANE, detail: "") { XCTAssertTrue(e.isLane) } else { XCTFail() }
        // A reserved code in the vino block is none of them.
        XCTAssertNil(ZxvError(status: -51, detail: "").refusal.flatMap { $0 == .other ? nil : $0 })
    }

    // MARK: payment requests

    func testRequests() throws {
        var ok = 0
        for r in objs(vino()["requests"]) {
            let q = request(r)
            let want = S(r["status"])
            XCTAssertEqual(st { _ = try q.encode() }, want, "\(r)")
            guard want == "ZXV_OK" else { continue }
            ok += 1
            let e = try q.encode()
            XCTAssertEqual(hx(e.bytes), S(r["bytes"]))
            XCTAssertEqual(hx(e.id), S(r["id"]))
            // Round trip: decode gives back the same value and id.
            let (back, id) = try Vino.Request.decodeWithId(e.bytes)
            XCTAssertEqual(back, q)
            XCTAssertEqual(id, e.id)
            XCTAssertEqual(try Vino.Request.decode(e.bytes).memo, S(r["memo"]))
        }
        XCTAssertGreaterThanOrEqual(ok, 6)

        var base = request(objs(vino()["requests"])[0])
        // A memo with a NUL byte would be silently cut by C: refused up front.
        var nul = base
        nul.memo = "ab\u{0}cd"
        XCTAssertEqual(st { _ = try nul.encode() }, "ZXV_E_ARG")
        // Wrong-size merchant or nonce never reaches C.
        var shortMerchant = base
        shortMerchant.merchant = [1, 2, 3]
        XCTAssertEqual(st { _ = try shortMerchant.encode() }, "ZXV_E_ARG")
        var longNonce = base
        longNonce.nonce = [UInt8](repeating: 0, count: 17)
        XCTAssertEqual(st { _ = try longNonce.encode() }, "ZXV_E_ARG")
        // The reasons a developer would match on.
        base.flags = .chance
        XCTAssertEqual(zerr { _ = try base.encode() }?.refusal, .paidChanceRefused)
        base.lane = .play // a chance outcome is fine on the play lane
        XCTAssertEqual(st { _ = try base.encode() }, "ZXV_OK")
        base.lane = Vino.Lane(rawValue: 3)
        if case .usage(let u)? = zerr({ _ = try base.encode() }) { XCTAssertTrue(u.isLane) } else { XCTFail() }

        // Decode refusals: the good request with one byte changed.
        let good = goodRequest()
        for d in objs(vino()["decode"]) {
            var b = good
            b[Int(U(d["offset"]))] = UInt8(U(d["value"]))
            XCTAssertEqual(st { _ = try Vino.Request.decode(b) }, S(d["status"]), "\(d)")
        }
        // Wrong lengths never reach C.
        for n in [0, 159, 161] {
            XCTAssertEqual(st { _ = try Vino.Request.decode([UInt8](repeating: 0, count: n)) }, "ZXV_E_ARG")
        }
    }

    // MARK: receipts

    func testReceipts() throws {
        let rc = vino()["receipt"] as! J
        let ctx = try ZxvContext()
        let cap = try ctx.rootCapability(.cryptoSign)
        let dsa = MlDsa87(cap)
        let kp = try dsa.keygen(seed: hex(S(rc["seed"])))
        XCTAssertEqual(hx(Hashes.sha3_256(kp.publicKey)), S(rc["pk_sha3_256"]))

        let req = goodRequest()
        let id = try Vino.Request.decodeWithId(req).id
        let approved = U(rc["approved_at"])
        let msg = try Vino.receiptMessage(requestId: id, walletPublicKey: kp.publicKey, approvedAt: approved)
        XCTAssertEqual(hx(msg), S(rc["msg"]))
        let sig = try dsa.sign(msg, with: kp.secretKey, deterministic: true)
        XCTAssertEqual(hx(sig), S(rc["sig"]))
        let vsig = hex(S(rc["sig"]))

        for c in objs(rc["checks"]) {
            var s = vsig
            let flip = I(c["flip_sig"])
            if flip >= 0 { s[Int(flip)] ^= 1 }
            let at = U(c["approved_at"]), now = U(c["now"]), want = S(c["status"])
            XCTAssertEqual(st { try Vino.verifyReceipt(request: req, walletPublicKey: kp.publicKey, approvedAt: at,
                                                       signature: s, now: now) }, want, "\(c)")
            XCTAssertEqual(try Vino.isReceiptValid(request: req, walletPublicKey: kp.publicKey, approvedAt: at,
                                                   signature: s, now: now), want == "ZXV_OK")
        }

        // The wallet's own (hedged) signature verifies offline too.
        let mine = try Vino.signReceipt(cap, request: req, walletSecretKey: kp.secretKey,
                                        walletPublicKey: kp.publicKey, approvedAt: approved)
        XCTAssertEqual(mine.count, Vino.receiptSignatureBytes)
        try Vino.verifyReceipt(request: req, walletPublicKey: kp.publicKey, approvedAt: approved, signature: mine,
                               now: approved)
        // Another wallet's key does not verify it.
        let other = try dsa.keygen(seed: [UInt8](repeating: 0x32, count: 32))
        XCTAssertFalse(try Vino.isReceiptValid(request: req, walletPublicKey: other.publicKey, approvedAt: approved,
                                               signature: mine, now: approved))
        // Signing outside the window is refused.
        XCTAssertEqual(zerr { _ = try Vino.signReceipt(cap, request: req, walletSecretKey: kp.secretKey,
                                                       walletPublicKey: kp.publicKey, approvedAt: 1_700_000_601) }?.refusal,
                       .requestExpired)
        XCTAssertEqual(st { _ = try Vino.signReceipt(cap, request: req, walletSecretKey: kp.secretKey,
                                                     walletPublicKey: kp.publicKey, approvedAt: 1_699_999_999) },
                       "ZXV_E_EXPIRED")
        // A damaged request is refused with the decode error, both sides.
        var bad = req
        bad[0] = UInt8(ascii: "Y")
        XCTAssertEqual(st { _ = try Vino.signReceipt(cap, request: bad, walletSecretKey: kp.secretKey,
                                                     walletPublicKey: kp.publicKey, approvedAt: approved) },
                       "ZXV_E_FORMAT")
        XCTAssertEqual(st { _ = try Vino.isReceiptValid(request: bad, walletPublicKey: kp.publicKey,
                                                        approvedAt: approved, signature: vsig, now: approved) },
                       "ZXV_E_FORMAT")
        // Signing needs CRYPTO_SIGN; a vino-only capability is refused.
        let vcap = try ctx.rootCapability(.vino)
        XCTAssertEqual(zerr { _ = try Vino.signReceipt(vcap, request: req, walletSecretKey: kp.secretKey,
                                                       walletPublicKey: kp.publicKey, approvedAt: approved) }?.refusal,
                       .capabilityDenied)
        // Wrong-size buffers never reach C.
        XCTAssertEqual(st { _ = try Vino.receiptMessage(requestId: id, walletPublicKey: [UInt8](repeating: 0, count: 2591),
                                                        approvedAt: approved) }, "ZXV_E_ARG")
        XCTAssertEqual(st { _ = try Vino.receiptMessage(requestId: [1], walletPublicKey: kp.publicKey,
                                                        approvedAt: approved) }, "ZXV_E_ARG")
        XCTAssertEqual(st { try Vino.verifyReceipt(request: req, walletPublicKey: kp.publicKey, approvedAt: approved,
                                                   signature: Array(vsig.dropLast()), now: approved) }, "ZXV_E_ARG")
        XCTAssertEqual(st { _ = try Vino.signReceipt(cap, request: req, walletSecretKey: SecretKey(count: 10),
                                                     walletPublicKey: kp.publicKey, approvedAt: approved) }, "ZXV_E_ARG")
    }

    // MARK: the play scenario

    func testPlayScenario() throws {
        let ctx = try ZxvContext()
        let cap = try ctx.rootCapability(.vino)
        let play = try Vino.Play(cap)
        let pv = vino()["play"] as! J
        for s in objs(pv["steps"]) {
            let op = S(s["op"]), want = S(s["status"])
            func rail() -> Vino.Rail { Vino.Rail(rawValue: I32(s["rail"])) }
            switch op {
            case "open":
                var got: UInt32 = 0
                XCTAssertEqual(st { got = try play.open(owner: U32(s["owner"]),
                                                        kind: Vino.AccountKind(rawValue: I32(s["is_issuer"]))) }, want)
                if want == "ZXV_OK" { XCTAssertEqual(got, U32(s["account"])) }
            case "back":
                XCTAssertEqual(st { try play.back(issuer: U32(s["issuer"]), amount: U(s["amount"])) }, want, "\(s)")
            case "unback":
                XCTAssertEqual(st { try play.unback(issuer: U32(s["issuer"]), amount: U(s["amount"])) }, want, "\(s)")
            case "issue":
                var got: UInt64 = 0
                XCTAssertEqual(st { got = try play.issue(issuer: U32(s["issuer"]), holder: U32(s["holder"]),
                                                         face: U(s["face"]), rail: rail()) }, want, "\(s)")
                if want == "ZXV_OK" { XCTAssertEqual(got, U(s["id"])) }
            case "transfer":
                XCTAssertEqual(st { try play.transfer(U(s["voucher"]), from: U32(s["from"]), to: U32(s["to"]),
                                                      rail: rail()) }, want, "\(s)")
            case "set_rail":
                XCTAssertEqual(st { try play.setRail(U(s["voucher"]), holder: U32(s["holder"]), rail: rail()) },
                               want, "\(s)")
            case "redeem":
                XCTAssertEqual(st { try play.redeem(U(s["voucher"]), holder: U32(s["holder"])) }, want, "\(s)")
            default:
                XCTFail("unknown play op \(op)")
            }
        }
        for a in objs(pv["accounts"]) {
            let info = try play.account(U32(a["account"]))
            XCTAssertEqual(info.owner, U32(a["owner"]))
            XCTAssertEqual(info.isIssuer, I(a["is_issuer"]) == 1)
            XCTAssertEqual(info.backing, U(a["backing"]))
            XCTAssertEqual(info.faces, U(a["faces"]))
            XCTAssertEqual(info.count, U32(a["count"]))
            XCTAssertEqual(try play.vouchers(U32(a["account"])), (a["vouchers"] as! [Any]).map { U($0) })
        }
        for v in objs(pv["vouchers"]) {
            let info = try play.voucher(U(v["id"]))
            XCTAssertEqual(info.face, U(v["face"]))
            XCTAssertEqual(info.issuer, U32(v["issuer"]))
            XCTAssertEqual(info.holder, U32(v["holder"]))
            XCTAssertEqual(info.rail.rawValue, I32(v["rail"]))
            XCTAssertEqual(info.state.rawValue, I32(v["state"]))
            XCTAssertEqual(info.isActive, I32(v["state"]) == 1)
        }
        try play.check()
        XCTAssertEqual(st { _ = try play.voucher(999) }, "ZXV_E_NOT_FOUND")
        XCTAssertEqual(st { _ = try play.account(999) }, "ZXV_E_NOT_FOUND")
        XCTAssertEqual(st { _ = try play.vouchers(999) }, "ZXV_E_NOT_FOUND")

        // The reasons a developer would match on, on a fresh sandbox.
        let p2 = try Vino.Play(cap)
        let shop = try p2.openIssuer(owner: 1)
        let alice = try p2.openHolder(owner: 2), bob = try p2.openHolder(owner: 3)
        XCTAssertEqual(try p2.account(shop).kind, .issuer)
        XCTAssertEqual(try p2.account(alice).kind, .holder)
        try p2.back(issuer: shop, amount: 18)
        XCTAssertEqual(zerr { try p2.issue(issuer: shop, holder: alice, face: 13) }?.refusal, .coverageRefused)
        let v = try p2.issue(issuer: shop, holder: alice, face: 8)
        XCTAssertEqual(try p2.voucher(v).rail, .bearer) // the default rail
        if case .usage(let u)? = zerr({ try p2.issue(issuer: shop, holder: alice, face: 4) }) {
            XCTAssertTrue(u.isDenomination)
        } else { XCTFail() }
        XCTAssertEqual(zerr { try p2.transfer(v, from: alice, to: bob, rail: .ghost) }?.refusal, .railMismatch)
        XCTAssertEqual(zerr { try p2.setRail(v, holder: alice, rail: .bearer) }?.refusal, .railMismatch)
        XCTAssertEqual(zerr { try p2.unback(issuer: shop, amount: 18) }?.refusal, .coverageRefused)
        XCTAssertEqual(zerr { try p2.unback(issuer: shop, amount: 19) }?.refusal, .insufficientFunds)
        try p2.redeem(v, holder: alice)
        XCTAssertEqual(st { try p2.redeem(v, holder: alice) }, "ZXV_E_STATE")
        XCTAssertEqual(try p2.account(shop).faces, 0)
        try p2.unback(issuer: shop, amount: 18)
    }

    // MARK: the meter scenario

    func testMeterScenario() throws {
        let ctx = try ZxvContext()
        let cap = try ctx.rootCapability(.vino)
        let mv = vino()["meter"] as! J
        let m = try Vino.SpendMeter(cap, dailyCap: U(mv["daily_cap"]))
        let m2 = try Vino.SpendMeter(cap, dailyCap: U(mv["daily_cap"])) // the same steps through admit()
        for s in objs(mv["steps"]) {
            let want = S(s["status"]), day = U(s["day"]), amount = U(s["amount"])
            var d = Vino.MeterDecision(admitted: false, left: 0)
            XCTAssertEqual(st { d = try m.tryAdmit(day: day, amount: amount) }, want == "ZXV_E_LIMIT" ? "ZXV_OK" : want)
            var left: UInt64 = 0
            XCTAssertEqual(st { left = try m2.admit(day: day, amount: amount) }, want, "\(s)")
            if want == "ZXV_OK" {
                XCTAssertEqual(d, Vino.MeterDecision(admitted: true, left: U(s["left"])))
                XCTAssertEqual(left, U(s["left"]))
            } else if want == "ZXV_E_LIMIT" {
                XCTAssertEqual(d, Vino.MeterDecision(admitted: false, left: U(s["left"])))
            }
        }
        XCTAssertEqual(zerr { try m2.admit(day: 100, amount: 101) }?.refusal, .limitRefused)
        XCTAssertEqual(st { _ = try Vino.SpendMeter(cap, dailyCap: 0) }, "ZXV_E_RANGE")
        XCTAssertEqual(st { _ = try Vino.SpendMeter(cap, dailyCap: Vino.amountMax + 1) }, "ZXV_E_RANGE")
        XCTAssertEqual(st { _ = try Vino.SpendMeter(cap, dailyCap: Vino.amountMax) }, "ZXV_OK")
    }

    // MARK: lifetimes, buffers, capabilities, poisoned context

    func testHandleLifetimes() throws {
        let ctx = try ZxvContext()
        let cap = try ctx.rootCapability(.vino)
        let p = try Vino.Play(cap)
        let i = try p.openIssuer(owner: 1)
        XCTAssertTrue(try p.account(i).isIssuer)
        p.close()
        p.close() // idempotent
        // Use after release is refused before reaching C.
        XCTAssertEqual(st { _ = try p.openHolder(owner: 2) }, "ZXV_E_ARG")
        XCTAssertEqual(st { try p.check() }, "ZXV_E_ARG")
        XCTAssertEqual(st { _ = try p.vouchers(0) }, "ZXV_E_ARG")
        XCTAssertEqual(zerr { _ = try p.account(i) }?.statusClass, .usage)
        let m = try Vino.SpendMeter(cap, dailyCap: 10)
        XCTAssertEqual(try m.admit(day: 1, amount: 1), 9)
        m.close()
        XCTAssertEqual(st { try m.admit(day: 1, amount: 1) }, "ZXV_E_ARG")
        XCTAssertEqual(st { _ = try m.tryAdmit(day: 1, amount: 1) }, "ZXV_E_ARG")
        // Handles keep their context alive after every other reference goes.
        var keep: Vino.Play?
        var keepMeter: Vino.SpendMeter?
        do {
            let c2 = try ZxvContext()
            let k2 = try c2.rootCapability(.vino)
            keep = try Vino.Play(k2)
            keepMeter = try Vino.SpendMeter(k2, dailyCap: 5)
        }
        let a = try keep!.openIssuer(owner: 7)
        try keep!.back(issuer: a, amount: 100)
        XCTAssertEqual(try keep!.account(a).backing, 100)
        XCTAssertEqual(try keepMeter!.admit(day: 1, amount: 5), 0)
        keep = nil // released after its context object, safely
        keepMeter = nil
        // A request is a plain value: copies are independent.
        var r1 = request(objs(vino()["requests"])[0])
        let r2 = r1
        r1.amount += 1
        XCTAssertNotEqual(r1, r2)
    }

    func testVoucherBuffer() throws {
        // vouchers() hides the two-call pattern: an empty list, and a list
        // long enough to need a second call.
        let ctx = try ZxvContext()
        let cap = try ctx.rootCapability(.vino)
        let p = try Vino.Play(cap)
        let shop = try p.openIssuer(owner: 1)
        let alice = try p.openHolder(owner: 2), bob = try p.openHolder(owner: 3)
        XCTAssertEqual(try p.vouchers(alice), [])
        XCTAssertEqual(try p.vouchers(shop), [])
        try p.back(issuer: shop, amount: 1_000_000)
        var want: [UInt64] = []
        for k in 0..<600 { want.append(try p.issue(issuer: shop, holder: alice, face: 1, rail: k % 2 == 1 ? .ghost : .bearer)) }
        XCTAssertEqual(try p.vouchers(alice), want)
        XCTAssertEqual(try p.vouchers(shop), want)
        XCTAssertEqual(try p.account(alice).count, 600)
        // Moving some to bob keeps both lists ascending and exact.
        var aWant: [UInt64] = [], bWant: [UInt64] = []
        for (k, v) in want.enumerated() {
            if k % 3 == 0 {
                try p.transfer(v, from: alice, to: bob, rail: k % 2 == 1 ? .ghost : .bearer)
                bWant.append(v)
            } else {
                aWant.append(v)
            }
        }
        XCTAssertEqual(try p.vouchers(alice), aWant)
        XCTAssertEqual(try p.vouchers(bob), bWant)
        // Redeemed vouchers leave the lists but stay readable by id.
        try p.redeem(bWant.removeFirst(), holder: bob)
        XCTAssertEqual(try p.vouchers(bob), bWant)
        XCTAssertEqual(try p.voucher(want[0]).state, .redeemed)
        // The C two-call contract the wrapper relies on: a short buffer gets
        // ZXV_E_BUFFER and the count, and is left untouched.
        let h = try p.cell.acquire()
        defer { p.cell.release() }
        var n = 0
        var one: [UInt64] = [77]
        XCTAssertEqual(zxv_vino_vouchers(cap.handle, h, alice, &one, 1, &n), ZXV_E_BUFFER)
        XCTAssertEqual(n, aWant.count)
        XCTAssertEqual(one, [77])
    }

    func testCapabilityRefusals() throws {
        let ctx = try ZxvContext()
        // A capability without VINO is refused.
        let weak = try ctx.rootCapability(.all1_5)
        XCTAssertEqual(zerr { _ = try Vino.Play(weak) }?.refusal, .capabilityDenied)
        XCTAssertEqual(st { _ = try Vino.Play(weak) }, "ZXV_E_CAP")
        XCTAssertEqual(zerr { _ = try Vino.SpendMeter(weak, dailyCap: 10) }?.refusal, .capabilityDenied)
        // A handle keeps its capability: revoking it refuses later calls.
        let vcap = try ctx.rootCapability(.vino)
        let child = try vcap.attenuate(.vino)
        let p = try Vino.Play(child)
        let m = try Vino.SpendMeter(child, dailyCap: 10)
        let a = try p.openIssuer(owner: 1)
        try vcap.revoke()
        XCTAssertEqual(st { _ = try p.openHolder(owner: 2) }, "ZXV_E_CAP")
        XCTAssertEqual(st { _ = try p.account(a) }, "ZXV_E_CAP")
        XCTAssertEqual(st { _ = try p.vouchers(a) }, "ZXV_E_CAP")
        XCTAssertEqual(st { try p.check() }, "ZXV_E_CAP")
        XCTAssertEqual(st { try m.admit(day: 1, amount: 1) }, "ZXV_E_CAP")
        XCTAssertEqual(zerr { _ = try m.tryAdmit(day: 1, amount: 1) }?.refusal, .capabilityDenied)
        // An unattested capability is refused too.
        let att = try ctx.rootCapability(.vino)
        try att.setAttestation(.neither)
        XCTAssertEqual(st { _ = try Vino.Play(att) }, "ZXV_E_CAP")
        // The pure calls need no capability at all.
        XCTAssertEqual(st { _ = try Vino.Request.decode(goodRequest()) }, "ZXV_OK")
    }

    func testPoisonedContext() throws {
        let ctx = try ZxvContext()
        let cap = try ctx.rootCapability(.all1_6)
        let p = try Vino.Play(cap)
        let m = try Vino.SpendMeter(cap, dailyCap: 100)
        let shop = try p.openIssuer(owner: 1), alice = try p.openHolder(owner: 2)
        try p.back(issuer: shop, amount: 100)
        let v = try p.issue(issuer: shop, holder: alice, face: 5)
        try ctx.halt("test")
        XCTAssertTrue(ctx.isPoisoned)
        // Every play and meter call on a halted context fails closed.
        let e = zerr { _ = try p.openHolder(owner: 3) }
        if case .fatal? = e {} else { XCTFail("expected .fatal, got \(String(describing: e))") }
        XCTAssertEqual(e?.statusName, "ZXV_E_POISONED")
        XCTAssertEqual(st { try p.back(issuer: shop, amount: 1) }, "ZXV_E_POISONED")
        XCTAssertEqual(st { try p.issue(issuer: shop, holder: alice, face: 1) }, "ZXV_E_POISONED")
        XCTAssertEqual(st { try p.transfer(v, from: alice, to: alice, rail: .bearer) }, "ZXV_E_POISONED")
        XCTAssertEqual(st { try p.redeem(v, holder: alice) }, "ZXV_E_POISONED")
        XCTAssertEqual(st { _ = try p.voucher(v) }, "ZXV_E_POISONED")
        XCTAssertEqual(st { _ = try p.account(shop) }, "ZXV_E_POISONED")
        XCTAssertEqual(st { _ = try p.vouchers(shop) }, "ZXV_E_POISONED")
        XCTAssertEqual(st { try p.check() }, "ZXV_E_POISONED")
        XCTAssertEqual(st { try m.admit(day: 1, amount: 1) }, "ZXV_E_POISONED")
        XCTAssertEqual(st { _ = try m.tryAdmit(day: 1, amount: 1) }, "ZXV_E_POISONED")
        XCTAssertEqual(st { _ = try Vino.Play(cap) }, "ZXV_E_POISONED")
        XCTAssertEqual(st { _ = try Vino.SpendMeter(cap, dailyCap: 1) }, "ZXV_E_POISONED")
        // Pure functions are unaffected.
        XCTAssertEqual(st { _ = try Vino.Request.decode(goodRequest()) }, "ZXV_OK")
    }

    // MARK: the README examples, verbatim, with the results their comments claim

    func testReadmePlayShop() throws {
        let ctx = try ZxvContext()
        let cap = try ctx.rootCapability(.vino)
        let play = try Vino.Play(cap)
        let shop = try play.openIssuer(owner: 1)           // account ids count up from 0
        let player = try play.openHolder(owner: 100)
        let friend = try play.openHolder(owner: 101)
        try play.back(issuer: shop, amount: 18)            // play units from the faucet; worth nothing
        let v = try play.issue(issuer: shop, holder: player, face: 8)   // 10*18 >= 18*8: allowed; id 1
        do { try play.issue(issuer: shop, holder: player, face: 13) }   // 10*18 < 18*(8+13): refused
        catch where error.refusal == .coverageRefused {}
        try play.transfer(v, from: player, to: friend, rail: .bearer)   // spend it on its active rail
        do { try play.transfer(v, from: friend, to: player, rail: .ghost) }
        catch where error.refusal == .railMismatch {}      // not its rail: one coin, one rail
        try play.redeem(v, holder: friend)                 // retired; nothing is paid out
        let retired = try play.voucher(v).state == .redeemed   // true
        let empty = try play.vouchers(friend).isEmpty          // true
        try play.check()                                   // re-checks every invariant
        XCTAssertTrue(retired && empty && v == 1)
    }

    func testReadmeLiveReceipt() throws {
        // App side: a plain value, encoded to 160 canonical bytes. Its id is SHA-256 of them.
        let req = Vino.Request(lane: .live,
                               merchant: [UInt8](repeating: 0x01, count: 32),   // the payee's id
                               nonce: try randomBytes(16),                      // unique per request
                               amount: 1_234_567,                               // VFV minor units
                               createdAt: 1_700_000_000,
                               expiresAt: 1_700_000_600,                        // ten minutes to approve
                               memo: "Sword of Dawn")                           // 0...64 printable ASCII
        let encoded = try req.encode()                     // encoded.bytes (160), encoded.id (32)

        // Wallet side (for this example only): approve with the wallet's ML-DSA-87 key.
        let walletCtx = try ZxvContext()
        let walletCap = try walletCtx.rootCapability(.cryptoSign)
        let wallet = try MlDsa87(walletCap).keygen()
        let sig = try Vino.signReceipt(walletCap, request: encoded.bytes, walletSecretKey: wallet.secretKey,
                                       walletPublicKey: wallet.publicKey, approvedAt: 1_700_000_100)

        // App side again, offline: no capability, no network.
        let paid = try Vino.isReceiptValid(request: encoded.bytes, walletPublicKey: wallet.publicKey,
                                           approvedAt: 1_700_000_100, signature: sig, now: 1_700_000_200)   // true
        let late = try Vino.isReceiptValid(request: encoded.bytes, walletPublicKey: wallet.publicKey,
                                           approvedAt: 1_700_000_100, signature: sig, now: 1_700_000_601)   // false
        let same = try Vino.Request.decode(encoded.bytes)  // == req
        XCTAssertTrue(paid)
        XCTAssertFalse(late)
        XCTAssertEqual(same, req)
    }

    func testReadmeMeter() throws {
        let ctx = try ZxvContext()
        let cap = try ctx.rootCapability(.vino)
        let meter = try Vino.SpendMeter(cap, dailyCap: 100)   // at most 100 per day index
        let left = try meter.admit(day: 19_700, amount: 60)   // 40 left today
        let d = try meter.tryAdmit(day: 19_700, amount: 41)   // admitted false, left 40: nothing counted
        XCTAssertEqual(left, 40)
        XCTAssertEqual(d, Vino.MeterDecision(admitted: false, left: 40))
    }
}
