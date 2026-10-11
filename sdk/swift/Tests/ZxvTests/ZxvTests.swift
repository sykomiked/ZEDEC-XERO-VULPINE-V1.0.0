// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// XCTest suite for the Swift SDK, run against the real libzxv and checked
// against sdk/conformance/vectors.json. No network. `swift test`.

import Foundation
import XCTest
import CZxv
@testable import Zxv
#if canImport(Glibc)
import Glibc
#elseif canImport(Darwin)
import Darwin
#endif

final class LibraryTests: XCTestCase {
    func testVersionAndFeatures() throws {
        let v = ZxvLibrary.version
        XCTAssertEqual(v.major, 1)
        XCTAssertGreaterThanOrEqual(v.minor, 2)
        try ZxvLibrary.ensureCompatible()
        XCTAssertTrue(ZxvLibrary.features.contains([.rational, .caps, .rmag, .phase]))
        XCTAssertFalse(ZxvLibrary.features.contains(.lora))
    }

    func testStatusClasses() {
        for case let s as [String: Any] in vectors.arr("status") {
            let code = Int32(s.int("code"))
            XCTAssertEqual(ZxvLibrary.statusName(code), s.str("name"))
            XCTAssertEqual(StatusClass(status: code).rawValue, Int32(s.int("class")))
            guard code != 0 else { continue }
            let e = ZxvError(status: code, detail: "probe")
            XCTAssertEqual(e.statusClass.rawValue, Int32(s.int("class")))
            XCTAssertEqual(e.statusName, s.str("name"))
            XCTAssertEqual(e.detail, "probe")
        }
        XCTAssertEqual(ZxvError(status: ZXV_E_CAP, detail: "").refusal, .capabilityDenied)
        XCTAssertEqual(ZxvError(status: ZXV_E_BUDGET, detail: "").refusal, .budgetExhausted)
        XCTAssertEqual(ZxvError(status: ZXV_E_FUNDS, detail: "").refusal, .insufficientFunds)
        XCTAssertEqual(ZxvError(status: ZXV_E_VERIFY, detail: "").refusal, .verificationFailed)
        XCTAssertEqual(ZxvError(status: ZXV_E_NOTIMPL, detail: "").refusal, .notImplemented)
        if case .system = ZxvError(status: -1234, detail: "") {} else { XCTFail("unknown status is SYSTEM (R8)") }
    }
}

final class RationalTests: XCTestCase {
    func testOperationsMatchVectors() throws {
        for case let c as [String: Any] in vectors.arr("rational_ops") {
            let a = pair(c["a"]!), b = pair(c["b"]!)
            XCTAssertEqual(try a + b, pair(c["add"]!))
            XCTAssertEqual(try a - b, pair(c["sub"]!))
            XCTAssertEqual(try a * b, pair(c["mul"]!))
            XCTAssertEqual(try a / b, pair(c["div"]!))
            let cmp = a < b ? -1 : (a == b ? 0 : 1)
            XCTAssertEqual(Int64(cmp), c.int("cmp"))
        }
    }

    func testParseAndFormat() {
        for case let c as [String: Any] in vectors.arr("rational_parse") {
            let st = Int32(c.int("status"))
            do {
                let r = try Rational(c.str("text"))
                XCTAssertEqual(st, 0)
                XCTAssertEqual(r, pair(c["value"]!))
                XCTAssertEqual(r.description, c.str("format"))
            } catch {
                XCTAssertEqual(error.status, st, c.str("text"))
            }
        }
    }

    func testFloatBoundary() {
        for case let c as [String: Any] in vectors.arr("ieee754") {
            let x = Double(bitPattern: UInt64(c.str("bits"), radix: 16)!)
            let st = Int32(c.int("status"))
            do {
                let r = try Rational.fromDoubleExact(x)
                XCTAssertEqual(r, pair(c["value"]!))
                XCTAssertEqual(st, 0)
            } catch {
                XCTAssertEqual(error.status, st)
                XCTAssertEqual(error.isFloatRejected, st == ZXV_E_FLOAT)
            }
        }
    }

    func testQ16AndOverflow() throws {
        for case let c as [String: Any] in vectors.arr("q16") {
            let r = pair(c["value"]!)
            if c.int("status") == 0 {
                XCTAssertEqual(Int64(try r.toQ16()), c.int("q16"))
            } else {
                XCTAssertThrowsError(try r.toQ16())
            }
        }
        let big = try Rational(Int64.max)
        XCTAssertNil(big.adding(1))
        do {
            _ = try big * 2
            XCTFail("overflow must throw")
        } catch {
            XCTAssertEqual(error.status, ZXV_E_RANGE)
        }
        XCTAssertThrowsError(try Rational(1, 0))
        XCTAssertThrowsError(try Rational(Int64.min))
        XCTAssertThrowsError(try Rational(1) / 0)
        XCTAssertEqual(try Rational(7, 10).toQ16(), 45875)
        XCTAssertEqual(Rational.fromQ16(-32768), try Rational(-1, 2))
        XCTAssertEqual(-(try Rational(1, 3)), try Rational(-1, 3))
    }
}

final class TruthTests: XCTestCase {
    func testTablesMatchVectorsAndC() {
        let lp = vectors.obj("lpres")
        let names = lp.arr("names") as! [String]
        let notT = lp.arr("not"), andT = lp.arr("and") as! [[Any]], orT = lp.arr("or") as! [[Any]]
        for a in Truth.allCases {
            XCTAssertEqual(a.description, names[Int(a.rawValue)])
            XCTAssertEqual(Int64(a.not().rawValue), i64(notT[Int(a.rawValue)]))
            for b in Truth.allCases {
                XCTAssertEqual(Int64(a.and(b).rawValue), i64(andT[Int(a.rawValue)][Int(b.rawValue)]))
                XCTAssertEqual(Int64(a.or(b).rawValue), i64(orT[Int(a.rawValue)][Int(b.rawValue)]))
                var r: Int32 = 0
                XCTAssertEqual(zxv_lp_or(a.cValue, b.cValue, &r), 0)
                XCTAssertEqual(r, a.or(b).cValue)
            }
        }
        XCTAssertEqual(Truth(positive: 2, negative: 1), .both)
        XCTAssertFalse(Truth.both.isCertainlyTrue)
        XCTAssertTrue(Truth.neither.toBool(onBoth: false, onNeither: true))
        XCTAssertThrowsError(try Truth.both.requireTrue())
        XCTAssertNil(Truth(c: 7))
    }
}

final class CapabilityLedgerTests: XCTestCase {
    func testCapabilitiesLedgerAndMessages() throws {
        let ctx = try ZxvContext()
        let root = try ctx.rootCapability(processId: 7)
        let ro = try root.attenuate(.ledgerRead)
        XCTAssertTrue(try ro.check(.ledgerRead))
        XCTAssertFalse(try ro.check(.ledgerPost))
        XCTAssertFalse(try ro.attenuate(.all).check(.ledgerPost))
        XCTAssertEqual(try root.info().processId, 7)
        XCTAssertThrowsError(try ctx.rootCapability(CapabilitySet(rawValue: 1 << 40)))

        try root.setAttestation(.both)
        do {
            try root.require(.ledgerRead)
            XCTFail()
        } catch .refused(let e) {
            XCTAssertEqual(e.reason, .capabilityDenied)
            XCTAssertTrue(e.detail.contains("contradictory"))
        }
        try root.setAttestation(.true)

        try ctx.enableCurrency("NGN", minorUnits: 2, capability: root)
        XCTAssertEqual(try ctx.enabledCurrencies(), ["NGN"])
        let ledger = Ledger(root)
        let issuer = try ledger.openAccount("issuer", currency: "NGN", kind: .issuer)
        let alice = try ledger.openAccount("alice", currency: "NGN")
        let bob = try ledger.openAccount("bob", currency: "NGN")
        XCTAssertEqual(try ledger.post(from: issuer, to: alice, amount: 500, currency: "NGN", reference: "r1"), 1)
        try ledger.post(from: alice, to: bob, amount: 120, currency: "NGN", reference: "r2")
        do {
            try ledger.post(from: bob, to: alice, amount: 1000, currency: "NGN", reference: "r3")
            XCTFail()
        } catch {
            XCTAssertEqual(error.zxv?.refusal, .insufficientFunds)
        }
        let reader = Ledger(ro)
        XCTAssertThrowsError(try reader.post(from: issuer, to: alice, amount: 1, currency: "NGN", reference: "r4"))
        XCTAssertEqual(try reader.balance(alice), Balance(debit: 500, credit: 120, equity: 380))
        let j = try reader.journal(alice)
        XCTAssertEqual(j.map(\.amount), [500, -120])
        XCTAssertEqual(j[1].reference, "r2")
        XCTAssertEqual(try reader.accountName(bob), "bob")
        try ledger.check()

        let msg = try Message(ro, kind: .pacs008)
        try msg.set(.msgId, "M1").set(.creDtTm, "2026-10-11T00:00:00Z").set(.debtorName, "Alice & Co")
            .set(.debtorAccount, "A1").set(.creditorName, "Bob").set(.creditorAccount, "B1")
            .set(.endToEndId, "E2E1").set(.currency, "NGN").setAmount(.settlement, units: 12075, fractionDigits: 2)
        let xml = try msg.render()
        XCTAssertTrue(xml.contains("Alice &amp; Co") && xml.contains("120.75"))
        XCTAssertEqual(try CurrencyInfo(alpha: "NGN").numeric, 566)
        XCTAssertTrue(try CardNetwork.dragon.isValid(pan: "4111111111111111"))

        try root.revoke()
        XCTAssertFalse(try ro.check(.ledgerRead))
    }

    func testFatalPathMarksHandlesDead() throws {
        let ctx = try ZxvContext()
        let cap = try ctx.rootCapability()
        let ledger = Ledger(cap)
        try ctx.halt("test halt")
        XCTAssertTrue(ctx.isPoisoned)
        do {
            try ledger.check()
            XCTFail()
        } catch .fatal(let e) {
            XCTAssertEqual(e.status, ZXV_E_POISONED)
        }
        // refused again, before reaching C
        do {
            _ = try ledger.balance(0)
            XCTFail()
        } catch {
            if case .fatal = error {} else { XCTFail("expected fatal, got \(error)") }
        }
    }

    func testFatalAbortPolicyTerminatesProcess() throws {
        #if canImport(Darwin)
        // Darwin's SDK marks fork() unavailable in Swift; the abort path is the
        // same C code, so the Linux run and libzxv's own tests cover it.
        throw XCTSkip("fork() is unavailable to Swift on Darwin")
        #elseif canImport(Glibc)
        let pid = fork()
        if pid == 0 {
            ZxvLibrary.fatalPolicy = .abort
            if let ctx = try? ZxvContext(), let cap = try? ctx.rootCapability() {
                try? ctx.halt("abort policy")
                try? Ledger(cap).check()
            }
            _exit(0) // reached only if the policy failed to abort
        }
        var status: Int32 = 0
        waitpid(pid, &status, 0)
        // WIFSIGNALED / WTERMSIG are macros; decode the wait status directly.
        let signalled = (status & 0x7f) != 0 && (status & 0x7f) != 0x7f
        XCTAssertTrue(signalled && (status & 0x7f) == SIGABRT, "status \(status)")
        #endif
    }
}

final class AttestationBudgetTests: XCTestCase {
    func testAttestations() throws {
        let ctx = try ZxvContext()
        let cap = try ctx.rootCapability()
        let reg = try Attestations(cap)
        let i = try reg.attest("model.gguf", subject: .model, state: .true, attester: "loader")
        XCTAssertTrue(try reg.gate())
        try reg.evidence(i, positive: true)
        try reg.evidence(i, positive: false)
        XCTAssertEqual(try reg.state(i), AttestationState(truth: .both, positive: 2, negative: 1))
        XCTAssertFalse(try reg.gate())
        XCTAssertEqual(try reg.find("model.gguf"), i)
        XCTAssertNil(try reg.find("nope"))
    }

    func testBudgetExhaustionAndAsyncWaiting() async throws {
        let ctx = try ZxvContext()
        let cap = try ctx.rootCapability()
        let budgets = try Budgets(cap)
        let b = try budgets.create("tokens", resource: .tokens, limit: 10)
        let denied = Locked(0)
        b.onExhausted { _ in denied.withLock { $0 += 1 } }
        try b.consume(try Rational(7, 2))
        try b.consume(try Rational(13, 2))
        do {
            try b.consume(try Rational(1, 3))
            XCTFail()
        } catch {
            XCTAssertEqual(error.zxv?.refusal, .budgetExhausted)
        }
        XCTAssertEqual(denied.withLock { $0 }, 1)
        do {
            try b.consume(-1)
            XCTFail()
        } catch .usage(let e) {
            XCTAssertEqual(e.status, ZXV_E_RMAG_INVALID)
        }
        var s = try b.status()
        XCTAssertEqual(s.remaining, 0)
        XCTAssertEqual(s.used, 10)
        try b.release(try Rational(1, 2))
        s = try b.status()
        XCTAssertEqual(s.remaining, try Rational(1, 2))
        XCTAssertEqual(s.epoch, 1)

        // a waiter suspends on exhaustion and resumes after a release
        let waiter = Task { try await b.consumeAsync(2) }
        try await Task.sleep(for: .milliseconds(50))
        try b.release(3)
        try await waiter.value
        // a growing limit also wakes waiters
        let w2 = Task { try await b.consumeAsync(5) }
        try await Task.sleep(for: .milliseconds(30))
        try b.setLimit(20)
        try await w2.value
        // timeout, cancellation, invalid amount, and close
        do {
            try await b.consumeAsync(100, timeout: .milliseconds(20))
            XCTFail()
        } catch let e as ZxvError {
            XCTAssertEqual(e.refusal, .budgetExhausted)
        }
        let w3 = Task { try await b.consumeAsync(100) }
        try await Task.sleep(for: .milliseconds(20))
        w3.cancel()
        do {
            try await w3.value
            XCTFail()
        } catch is CancellationError {}
        do {
            try await b.consumeAsync(-3)
            XCTFail()
        } catch let e as ZxvError {
            XCTAssertEqual(e.statusClass, .usage)
        }
        let w4 = Task { try await b.consumeAsync(100) }
        try await Task.sleep(for: .milliseconds(20))
        budgets.close()
        do {
            try await w4.value
            XCTFail()
        } catch is ZxvCancelled {}
    }
}

final class PhaseTests: XCTestCase {
    func testDeferralManualAndRealtime() async throws {
        let ctx = try ZxvContext()
        let cap = try ctx.rootCapability()
        let clock = try PhaseClock(cap)
        XCTAssertEqual(phaseCycle, UInt64(vectors.int("phase_cycle")))
        let ev = try clock.defer(until: phaseCycle)
        XCTAssertEqual(try clock.nextDue(), phaseCycle)
        let waiter = Task { try await ev.awaitCleared() }
        var r = try clock.advance(phaseCycle - 1)
        XCTAssertEqual(r.cleared, 0)
        XCTAssertEqual(try ev.state(), .pending)
        r = try clock.advance(1)
        XCTAssertEqual(r.cleared, 1)
        XCTAssertEqual(r.now, 27720)
        try await waiter.value
        XCTAssertEqual(try ev.state(), .cleared)
        XCTAssertNil(try clock.nextDue())

        var a = try clock.admit(phase: 6, step: 1, flags: .s0Pending)
        XCTAssertEqual(a.decision, .defer)
        try clock.setHealth(phase: 6, .healthy)
        a = try clock.admit(phase: 6, step: 2, flags: .requiresHealth)
        XCTAssertEqual(a.decision, .admit)
        XCTAssertNotEqual(a.token, 0)

        let ev2 = try clock.defer(until: try clock.now() + 100)
        try ev2.cancel()
        do {
            try await ev2.awaitCleared()
            XCTFail()
        } catch is ZxvCancelled {}
        XCTAssertThrowsError(try PhaseClock(try cap.attenuate(.ledgerRead)).advance(1))

        // real-time driver: 100,000 ticks/s reaches a tick 2,000 ahead
        let target = try clock.now() + 2000
        try clock.startRealtime(ticksPerSecond: 100_000)
        try await clock.untilTick(target)
        clock.stopRealtime()
        XCTAssertGreaterThanOrEqual(try clock.now(), target)
    }
}

final class WireSwarmTests: XCTestCase {
    func testUbh168ByteForByte() throws {
        for case let c as [String: Any] in vectors.arr("ubh168") {
            let tag = try Ubh168Frame.tag(trunk: UInt32(c.int("trunk")), shell: UInt32(c.int("shell")), sync: c.int("sync") != 0)
            XCTAssertEqual(Int64(tag), c.int("tag"))
            let sp = (c["s_plus"] as! [Any]).map(u32), sm = (c["s_minus"] as! [Any]).map(u32)
            let f = Ubh168Frame.pack(tag: tag, sPlus: (sp[0], sp[1], sp[2]), sMinus: (sm[0], sm[1]))
            XCTAssertEqual(hexString(f.bytes), c.str("frame"))
            XCTAssertEqual(f, Ubh168Frame.packNative(tag: tag, sPlus: (sp[0], sp[1], sp[2]), sMinus: (sm[0], sm[1])))
            XCTAssertEqual(f.sPlus, sp)
            XCTAssertEqual(f.sMinus, sm)
            XCTAssertEqual(Int64(f.resolve().rawValue), c.int("truth"))
        }
        for (t, h) in (vectors.arr("ubh168_idle") as! [String]).enumerated() {
            let idle = try Ubh168Frame.idle(trunk: UInt32(t))
            XCTAssertEqual(hexString(idle.bytes), h)
            XCTAssertEqual(idle.resolve(), .neutral)
        }
        XCTAssertThrowsError(try Ubh168Frame.tag(trunk: 10, shell: 0, sync: false))
    }

    func testSwarmAllotments() throws {
        let v = vectors.obj("swarm")
        let ctx = try ZxvContext()
        let sw = try Swarm(try ctx.rootCapability(), levels: UInt32(v.int("levels")), tokensPerCycle: v.uint("tokens_per_cycle"))
        for case let m as [Any] in v.arr("models") { try sw.register(model: u32(m[0]), level: u32(m[1])) }
        let mood = v.arr("mood")
        try sw.setMood(Unicode.Scalar(u32(mood[0]))!, intensity: u32(mood[1]))
        for case let f as [Any] in v.arr("feelings") {
            try sw.setFeeling(model: u32(f[0]), Unicode.Scalar(u32(f[1]))!, intensity: u32(f[2]))
        }
        try sw.beginCycle()
        for case let a as [Any] in v.arr("allotments") {
            let al = try sw.allotment(model: u32(a[0]))
            XCTAssertEqual(Int64(al.allotted), i64(a[1]))
            XCTAssertEqual(Int64(al.imaginary), i64(a[2]))
        }
        let first = try sw.allotment(model: 100).allotted
        XCTAssertFalse(try sw.consume(model: 100, requested: 10).exhausted)
        XCTAssertEqual(try sw.consume(model: 100, requested: first), SwarmGrant(granted: first - 10, exhausted: true))
        XCTAssertGreaterThan(try sw.endCycle(), 0)
        for (d, cap) in v.arr("level_capacity").enumerated() {
            XCTAssertEqual(Int64(try Swarm.levelCapacity(UInt32(d))), i64(cap))
        }
        XCTAssertThrowsError(try sw.setMood("😀", intensity: 1))
        _ = try Swarm.emotionProfile("🤔")
    }
}

final class CryptoTests: XCTestCase {
    func testHashes() throws {
        for case let c as [String: Any] in vectors.arr("hash") {
            let s = c.str("utf8")
            XCTAssertEqual(hexString(Hashes.keccak256(s)), c.str("keccak256"))
            XCTAssertEqual(hexString(Hashes.sha3_256(s)), c.str("sha3_256"))
            XCTAssertEqual(hexString(Hashes.shake256(Array(s.utf8), outputLength: 64)), c.str("shake256_64"))
        }
        XCTAssertNotEqual(try randomBytes(32), try randomBytes(32))
    }

    func testSignaturesAndKem() throws {
        let ctx = try ZxvContext()
        let cap = try ctx.rootCapability()
        let kemOnly = try cap.attenuate(.cryptoKem)

        let e = vectors.obj("ed25519")
        let ed = Ed25519(cap)
        let sk = SecretKey(copying: hex(e.str("sk")))
        let pk = try ed.publicKey(of: sk)
        XCTAssertEqual(hexString(pk), e.str("pk"))
        let msg = Array(e.str("msg_utf8").utf8)
        var sig = try ed.sign(msg, with: sk)
        XCTAssertEqual(hexString(sig), e.str("sig"))
        XCTAssertTrue(try ed.verify(sig, of: msg, publicKey: pk))
        sig[0] ^= 1
        XCTAssertFalse(try ed.verify(sig, of: msg, publicKey: pk))
        do {
            _ = try Ed25519(kemOnly).sign(msg, with: sk)
            XCTFail()
        } catch {
            XCTAssertEqual(error.zxv?.refusal, .capabilityDenied)
        }

        let d = vectors.obj("mldsa87")
        let dsa = MlDsa87(cap)
        let kp = try dsa.keygen(seed: hex(d.str("seed")))
        XCTAssertEqual(hexString(Hashes.sha3_256(kp.publicKey)), d.str("pk_sha3_256"))
        XCTAssertEqual(hexString(Hashes.sha3_256(kp.secretKey.exportBytesUnsafe())), d.str("sk_sha3_256"))
        let dm = Array(d.str("msg_utf8").utf8), dc = Array(d.str("ctx_utf8").utf8)
        let ds = try dsa.sign(dm, with: kp.secretKey, context: dc, deterministic: true)
        XCTAssertEqual(hexString(Hashes.sha3_256(ds)), d.str("sig_deterministic_sha3_256"))
        XCTAssertTrue(try dsa.verify(ds, of: dm, publicKey: kp.publicKey, context: dc))
        XCTAssertFalse(try dsa.verify(ds, of: dm, publicKey: kp.publicKey, context: Array("zxw".utf8)))
        XCTAssertTrue(try dsa.verify(try dsa.sign(dm, with: kp.secretKey, context: dc), of: dm, publicKey: kp.publicKey, context: dc))

        let k = vectors.obj("mlkem1024")
        let kem = MlKem1024(kemOnly)
        let kkp = try kem.keygen(d: hex(k.str("d")), z: hex(k.str("z")))
        XCTAssertEqual(hexString(Hashes.sha3_256(kkp.encapsulationKey)), k.str("ek_sha3_256"))
        let enc = try kem.encapsulate(to: kkp.encapsulationKey, m: hex(k.str("m")))
        XCTAssertEqual(hexString(Hashes.sha3_256(enc.ciphertext)), k.str("ct_sha3_256"))
        XCTAssertEqual(hexString(enc.sharedSecret.exportBytesUnsafe()), k.str("ss"))
        XCTAssertEqual(hexString(try kem.decapsulate(enc.ciphertext, with: kkp.decapsulationKey).exportBytesUnsafe()), k.str("ss"))
        var bad = try kem.keygen().encapsulationKey
        for i in 0..<32 { bad[i] = 0xFF }
        XCTAssertThrowsError(try kem.encapsulate(to: bad))

        let w = SecretKey(copying: [1, 2, 3])
        w.wipe()
        XCTAssertEqual(w.exportBytesUnsafe(), [0, 0, 0])
    }
}

final class EngineTests: XCTestCase {
    private func open() throws -> (ZxvContext, Capability, Model) {
        let ctx = try ZxvContext()
        let cap = try ctx.rootCapability()
        let model = try Model(cap, path: "\(conformanceDir)/fixtures/tiny-qwen2.gguf",
                              tokenizer: "\(conformanceDir)/fixtures/tiny-tokenizer.gguf")
        return (ctx, cap, model)
    }
    private var prompt: String {
        String(decoding: try! Data(contentsOf: URL(fileURLWithPath: "\(conformanceDir)/fixtures/prompt.txt")), as: UTF8.self)
    }

    func testGenerationMatchesVectors() async throws {
        try XCTSkipUnless(ZxvLibrary.features.contains(.tensor), "built without the engine")
        let g = vectors.obj("generate")
        let (_, cap, model) = try open()
        XCTAssertEqual(try model.info(.vocabularySize), 1200)
        XCTAssertEqual(try model.tokenize(prompt), ints(g["prompt_tokens"]!))
        XCTAssertEqual(try model.detokenize(try model.tokenize(prompt)), prompt)
        XCTAssertThrowsError(try Model(try cap.attenuate(.ledgerRead), path: "\(conformanceDir)/fixtures/tiny-qwen2.gguf"))
        do {
            _ = try Model(cap, path: "/nonexistent.gguf")
            XCTFail()
        } catch {
            XCTAssertEqual(error.zxv?.statusClass, .system)
        }

        let session = try model.newSession(contextLength: UInt32(g.int("n_ctx")))
        let greedy = GenerateOptions(maxTokens: UInt32(g.obj("greedy").int("max_tokens")))
        var r = try session.generate(prompt, options: greedy)
        XCTAssertEqual(r.tokens, ints(g.obj("greedy")["tokens"]!))
        XCTAssertEqual(r.stop, .maxTokens)
        XCTAssertEqual(Int(r.promptTokens), (g["prompt_tokens"] as! [Any]).count)

        let s = g.obj("sampled")
        let sampled = GenerateOptions(
            maxTokens: UInt32(s.int("max_tokens")), temperature: Rational.fromQ16(Int32(s.int("temperature_q16"))),
            topK: UInt32(s.int("top_k")), topP: Rational.fromQ16(Int32(s.int("top_p_q16"))),
            repeatPenalty: Rational.fromQ16(Int32(s.int("repeat_penalty_q16"))),
            repeatLastN: UInt32(s.int("repeat_last_n")), seed: s.uint("seed"))
        try session.reset()
        XCTAssertEqual(try session.generate(prompt, options: sampled).tokens, ints(s["tokens"]!))

        // callback stop
        try session.reset()
        var seen = 0
        r = try session.generate(prompt, options: greedy) { _ in
            seen += 1
            return seen < 3
        }
        XCTAssertEqual(r.stop, .callback)
        XCTAssertEqual(r.tokens.count, 3)

        // async generate and AsyncThrowingStream streaming give the same tokens
        try session.reset()
        let viaAsync = try await session.generateAsync(prompt, options: greedy)
        XCTAssertEqual(viaAsync.tokens, ints(g.obj("greedy")["tokens"]!))
        try session.reset()
        var streamed: [Int32] = []
        for try await t in session.stream(prompt, options: greedy) { streamed.append(t.id) }
        XCTAssertEqual(streamed, ints(g.obj("greedy")["tokens"]!))
        // breaking out early stops generation and frees the session
        try session.reset()
        for try await _ in session.stream(prompt, options: greedy) { break }
        var freed = false
        for _ in 0..<200 where !freed {
            do { try session.reset(); freed = true } catch { try await Task.sleep(for: .milliseconds(5)) }
        }
        XCTAssertTrue(freed)

        // budget binding
        let budgets = try Budgets(cap)
        let b = try budgets.create("gen", resource: .tokens, limit: Rational(Int64((g["prompt_tokens"] as! [Any]).count + 8)))
        try session.bindBudget(b)
        r = try session.generate(prompt, options: greedy)
        XCTAssertEqual(r.stop, .budget)
        XCTAssertEqual(r.tokens.count, 8)
        try session.reset()
        do {
            _ = try session.generate(prompt, options: greedy)
            XCTFail()
        } catch {
            XCTAssertEqual(error.zxv?.refusal, .budgetExhausted)
        }
        try session.bindBudget(nil)

        do {
            try model.loadLora(path: "x.gguf", scale: 1)
            XCTFail()
        } catch {
            XCTAssertEqual(error.zxv?.refusal, .notImplemented)
        }
    }

    func testLockstepIsDeterministicAcrossInstances() throws {
        try XCTSkipUnless(ZxvLibrary.features.contains(.tensor), "built without the engine")
        let (_, _, ma) = try open()
        let (_, _, mb) = try open()
        let sa = try ma.newSession(contextLength: 128), sb = try mb.newSession(contextLength: 128)
        for frame in UInt64(0)..<5 {
            let opts = GenerateOptions(maxTokens: 4, temperature: try Rational(7, 10), topK: 40, seed: frame &* 0x9E37_79B9_7F4A_7C15 &+ 1)
            try sa.reset()
            try sb.reset()
            let input = "frame \(frame): player moved north"
            XCTAssertEqual(try sa.generate(input, options: opts).tokens, try sb.generate(input, options: opts).tokens)
        }
    }
}

final class EvmTests: XCTestCase {
    func testStateEncoding() throws {
        let v = vectors.obj("evm_state")
        let ctx = try ZxvContext()
        let cap = try ctx.rootCapability()
        let ch = StateChannel(cap)
        let parties = (v["addresses"] as! [String]).map(hex)
        let balances = (v["balances"] as! [Any]).map(i64)
        let st = try ch.encodeState(channelId: hex(v.str("channel_id")), nonce: v.uint("nonce"), parties: parties, balances: balances)
        XCTAssertEqual(hexString(st.encoding), v.str("encoding"))
        XCTAssertEqual(hexString(st.digest), v.str("digest"))
        XCTAssertEqual(Hashes.keccak256(st.encoding), st.digest)

        try ctx.enableCurrency("USD", minorUnits: 2, capability: cap)
        let ledger = Ledger(cap)
        let a = try ledger.openAccount("a", currency: "USD", kind: .issuer)
        let b = try ledger.openAccount("b", currency: "USD")
        try ledger.post(from: a, to: b, amount: 250, currency: "USD", reference: "x")
        let id = [UInt8](repeating: 0, count: 32)
        let l = try ch.ledgerState(channelId: id, nonce: 1, currency: "USD", accounts: [a, b], parties: Array(parties.prefix(2)))
        XCTAssertEqual(l.digest, try ch.encodeState(channelId: id, nonce: 1, parties: Array(parties.prefix(2)), balances: [-250, 250]).digest)
        XCTAssertThrowsError(try StateChannel(try cap.attenuate(.ledgerRead)).encodeState(
            channelId: id, nonce: 1, parties: parties, balances: balances))
    }
}

/// ABI 1.2: every gated call goes through a capability (the `zxv_*_cap` forms).
final class CapFormTests: XCTestCase {
    private func assertDenied(_ body: @autoclosure () throws -> some Any, file: StaticString = #filePath,
                              line: UInt = #line) {
        do {
            _ = try body()
            XCTFail("not refused", file: file, line: line)
        } catch {
            XCTAssertEqual(error.zxv?.refusal, .capabilityDenied, "\(error)", file: file, line: line)
            XCTAssertEqual(error.zxv?.status, ZXV_E_CAP, file: file, line: line)
        }
    }

    func testCapFormsNeedTheRightBit() throws {
        let ctx = try ZxvContext()
        let root = try ctx.rootCapability()
        let post = try root.attenuate(.ledgerPost)
        let read = try root.attenuate(.ledgerRead)

        // .ledgerPost: currencies and opening accounts
        try ctx.enableCurrency("NGN", capability: post)
        try ctx.enableCurrency("GHS", minorUnits: 2, capability: post)
        try ctx.registerPrivateUnit("VFV", minorUnits: 2, capability: post)
        XCTAssertEqual(try ctx.enabledCurrencies(), ["NGN", "GHS", "VFV"])
        assertDenied(try ctx.enableCurrency("KES", capability: read))
        assertDenied(try ctx.enableCurrency("KES", minorUnits: 2, capability: read))
        assertDenied(try ctx.registerPrivateUnit("VFX", minorUnits: 2, capability: read))
        let writer = Ledger(post), reader = Ledger(read)
        let bank = try writer.openAccount("bank", currency: "NGN", kind: .issuer)
        let ada = try writer.openAccount("ada", currency: "NGN")
        assertDenied(try reader.openAccount("x", currency: "NGN"))
        try writer.post(from: bank, to: ada, amount: 7, currency: "NGN", reference: "R-1")

        // .ledgerRead: account metadata, journals and messages
        XCTAssertEqual(try reader.accountCount(), 2)
        XCTAssertEqual(try reader.accountName(ada), "ada")
        XCTAssertEqual(try reader.accountCurrency(ada), "NGN")
        XCTAssertEqual(try reader.accountKind(bank), .issuer)
        XCTAssertEqual(try reader.journal(ada).map(\.reference), ["R-1"])
        assertDenied(try writer.accountCount())
        assertDenied(try writer.accountName(ada))
        assertDenied(try writer.accountCurrency(ada))
        assertDenied(try writer.accountKind(ada))
        assertDenied(try writer.journal(ada))
        assertDenied(try Message(post, kind: .pacs008))
        let mcap = try root.attenuate(.ledgerRead)
        let m = try Message(mcap, kind: .pacs008)
        try m.set(.msgId, "M-1")
        try mcap.setAttestation(.neither) // re-checked on every message call
        assertDenied(try m.set(.msgId, "M-2"))

        // handle creation: .budget, .phase, .attest, .swarm
        assertDenied(try Budgets(read))
        assertDenied(try PhaseClock(read))
        assertDenied(try Attestations(read))
        assertDenied(try Swarm(read, levels: 2, tokensPerCycle: 10))
        _ = try Budgets(try root.attenuate(.budget))
        _ = try PhaseClock(try root.attenuate(.phase))
        _ = try Attestations(try root.attenuate(.attest))
        _ = try Swarm(try root.attenuate(.swarm), levels: 2, tokensPerCycle: 10)
    }

    func testCapFromAnotherContextIsRefused() throws {
        let ctx = try ZxvContext(), other = try ZxvContext()
        let foreign = try other.rootCapability()
        let calls: [() throws -> Void] = [
            { try ctx.enableCurrency("NGN", capability: foreign) },
            { try ctx.enableCurrency("NGN", minorUnits: 2, capability: foreign) },
            { try ctx.registerPrivateUnit("VFV", minorUnits: 2, capability: foreign) },
        ]
        for call in calls {
            do {
                try call()
                XCTFail("not refused")
            } catch {
                XCTAssertEqual(error.zxv?.refusal, .capabilityDenied)
                XCTAssertEqual(error.zxv?.status, ZXV_E_CAP)
                XCTAssertTrue(error.zxv?.detail.contains("different context") ?? false, "\(error)")
            }
        }
        XCTAssertEqual(try ctx.enabledCurrencies(), [])
        XCTAssertEqual(try other.enabledCurrencies(), [], "nothing leaked into the capability's own context")
    }

    func testDeprecatedNoCapFormsStillWork() throws {
        // Through a protocol, so the stable 1.0 surface stays tested without
        // deprecation warnings in this build.
        let ctx = try ZxvContext()
        let legacy: any LegacyContext = ctx
        try legacy.enableCurrency("NGN", minorUnits: nil)
        try legacy.enableCurrency("GHS", minorUnits: 2)
        try legacy.registerPrivateUnit("VFV", minorUnits: 2)
        XCTAssertEqual(try ctx.enabledCurrencies(), ["NGN", "GHS", "VFV"])
        let make: any LegacyMessage.Type = Message.self
        _ = try make.init(ctx, kind: .pacs008)
    }
}

/// The 1.0 forms without a capability (deprecated since ABI 1.2).
protocol LegacyContext {
    func enableCurrency(_ alpha: String, minorUnits: UInt8?) throws(ZxvError)
    func registerPrivateUnit(_ code: String, minorUnits: UInt8) throws(ZxvError)
}
extension ZxvContext: LegacyContext {}

/// The 1.0 message initializer (deprecated since ABI 1.2).
protocol LegacyMessage {
    init(_ context: ZxvContext, kind: MxKind) throws(ZxvError)
}
extension Message: LegacyMessage {}
