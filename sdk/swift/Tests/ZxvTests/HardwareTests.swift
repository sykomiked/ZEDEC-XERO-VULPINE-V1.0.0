// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// ABI 1.4: hardware emulation and hardware updates. Mirrors the key cases
// of sdk/libzxv/test/test_zxv_abi14.c.

import CZxv
import XCTest
@testable import Zxv

private func status(_ body: () throws -> Void) -> Int32? {
    do { try body() } catch { return error.zxv?.status }
    return nil
}

private func hex(_ b: [UInt8]) -> String { b.map { String(format: "%02x", $0) }.joined() }
private func le64(_ v: UInt64) -> [UInt8] { (0..<8).map { UInt8(truncatingIfNeeded: v >> (8 * $0)) } }

private let sha256Abc = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
/// RFC 4231 test case 2 (key "Jefe").
private let hmacJefe = "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843"
private let macInput: [UInt8] = [4, 0] + Array("Jefe".utf8) + Array("what do ya want for nothing?".utf8)

/// A two-slot device; readback asks a separate matrix's SHA-256 engine
/// (a callback calling back into libzxv).
private final class SimDevice: HwDevice {
    let oracle: HwMatrix
    var slots: [[UInt8]] = [[], []]
    var active = -1
    init(oracle: HwMatrix) { self.oracle = oracle }
    func load(slot: UInt32, payload: [UInt8]) throws {
        if slot > 1 || payload.count > 256 { throw ZxvError(status: ZXV_E_RANGE, detail: "slot") }
        slots[Int(slot)] = payload
    }
    func readback(slot: UInt32) throws -> [UInt8] { try oracle.call(HwEngine.sha256, slots[Int(slot)]) }
    func activate(slot: UInt32) throws { active = Int(slot) }
    func run(_ input: [UInt8]) throws -> [UInt8] {
        if active < 0 { throw ZxvError(status: ZXV_E_STATE, detail: "no image") }
        return input.map { slots[active][Int($0)] }
    }
}

final class HardwareTests: XCTestCase {
    private func withHw(_ body: (ZxvContext, Capability, Capability) throws -> Void) throws {
        let ctx = try ZxvContext()
        let cap = try ctx.rootCapability(.all1_4)
        try body(ctx, cap, try cap.attenuate(.all))
    }

    func testVersionStatusesAndCaps() throws {
        XCTAssertGreaterThanOrEqual(ZxvLibrary.version.minor, 4)
        XCTAssertTrue(ZxvLibrary.features.contains(.hw))
        let codes: [(Int32, String)] = [(ZXV_E_NOPROVIDER, "ZXV_E_NOPROVIDER"), (ZXV_E_DISPUTE, "ZXV_E_DISPUTE"),
                                        (ZXV_E_QUARANTINED, "ZXV_E_QUARANTINED"), (ZXV_E_ROLLBACK, "ZXV_E_ROLLBACK"),
                                        (ZXV_E_BLOCKED, "ZXV_E_BLOCKED"), (ZXV_E_TARGET, "ZXV_E_TARGET"),
                                        (ZXV_E_POLICY, "ZXV_E_POLICY")]
        for (i, (code, name)) in codes.enumerated() {
            XCTAssertEqual(code, Int32(-25 - i))
            XCTAssertEqual(ZxvLibrary.statusName(code), name)
        }
        XCTAssertEqual(StatusClass(status: ZXV_E_DISPUTE), .refused)
        XCTAssertEqual(StatusClass(status: ZXV_E_TARGET), .usage)
        XCTAssertEqual(StatusClass(status: ZXV_E_POLICY), .refused)
        XCTAssertEqual(CapabilitySet.all.rawValue, 0x3FF)
        XCTAssertEqual(CapabilitySet.all1_4.rawValue, 0xFFF)
        try withHw { ctx, cap, nohw in
            XCTAssertTrue(try cap.info().bits.isSuperset(of: [.hwMatrix, .hwReconf]))
            XCTAssertFalse(try nohw.info().bits.contains(.hwMatrix))
            XCTAssertEqual(try ctx.rootCapability().info().bits, .all)
        }
    }

    func testProfiles() throws {
        let host = try HwProfile.probeHost()
        let r1 = try HwProfile.reference()
        XCTAssertEqual(try r1.count(), 16)
        XCTAssertGreaterThanOrEqual(try host.count(), 2)
        XCTAssertEqual(try host.cap(node: 2, key: HwCapKey.wordBits), UInt64(MemoryLayout<Int>.size * 8))
        XCTAssertGreaterThanOrEqual(try host.cap(node: 2, key: HwCapKey.count) ?? 0, 1)

        let m = try host.match(r1)
        XCTAssertGreaterThanOrEqual(m.native, 1)
        XCTAssertEqual(m.native + m.emulated, 14)
        XCTAssertEqual(try host.pair(r1, index: 1).pairing, .group)
        let cpu = try host.pair(r1, index: 2)
        XCTAssertEqual([cpu.needId, cpu.haveId], [3, 2])
        XCTAssertEqual(cpu.pairing, .native)
        XCTAssertEqual(try host.pair(r1, index: 7).pairing, .emulate)
        XCTAssertEqual(status { _ = try host.pair(r1, index: 99) }, ZXV_E_NOT_FOUND)

        XCTAssertEqual(try r1.cap(node: 6, key: HwCapKey.contractId), UInt64(HwEngine.tensor))
        let pres = try HwProfile.present(host: host, target: r1)
        XCTAssertEqual(try pres.cap(node: 3, key: HwCapKey.kind), UInt64(HwKind.hard.rawValue))
        XCTAssertEqual(try pres.cap(node: 8, key: HwCapKey.kind), UInt64(HwKind.soft.rawValue))
        XCTAssertNil(try pres.cap(node: 1, key: HwCapKey.kind))

        let bytes = try r1.encode()
        let copy = try HwProfile.decode(bytes)
        XCTAssertEqual(try r1.digest(), try copy.digest())
        XCTAssertEqual(try r1.nodes(), try copy.nodes())
        XCTAssertEqual(status { _ = try HwProfile.decode(Array(bytes.dropLast())) }, ZXV_E_FORMAT)

        let mine = try HwProfile()
        try mine.addNode(id: 1, parent: Hw.root, cls: HwClass.machine)
        try mine.addNode(id: 2, parent: 1, cls: HwClass.ternaryUnit)
        try mine.setCap(node: 2, key: HwCapKey.radix, value: 3)
        XCTAssertEqual(status { try mine.addNode(id: 2, parent: 1, cls: HwClass.cache) }, ZXV_E_ARG)
        XCTAssertEqual(status { try mine.setCap(node: 9, key: HwCapKey.radix, value: 3) }, ZXV_E_NOT_FOUND)
        try mine.addNode(id: 3, parent: 1, cls: HwClass.privateBase | 1)
        let t = try mine.pair(r1, index: 7)
        XCTAssertEqual(t.pairing, .native)
        XCTAssertEqual(t.haveId, 2)
        let d1 = try mine.digest()
        try mine.setExtension(node: 3, [0xA5, 0x00, 0x5A])
        XCTAssertNotEqual(d1, try mine.digest())
        XCTAssertEqual(try mine.ext(node: 3), [0xA5, 0x00, 0x5A])
        XCTAssertEqual(try mine.ext(node: 2), [])
        XCTAssertEqual(status { try mine.setExtension(node: 3, [UInt8](repeating: 0, count: Hw.extMax + 1)) }, ZXV_E_RANGE)
        XCTAssertEqual(try mine.node(at: 2), HwNode(id: 3, parent: 1, cls: HwClass.privateBase | 1, capCount: 0))
    }

    func testStandardEnginesAndDiscovery() throws {
        try withHw { _, cap, nohw in
            XCTAssertEqual(status { _ = try HwMatrix(nohw) }, ZXV_E_CAP)
            let mx = try HwMatrix(cap)
            XCTAssertEqual(hex(try mx.call(HwEngine.sha256, Array("abc".utf8))), sha256Abc)
            XCTAssertEqual(hex(try mx.call(HwEngine.mac, macInput)), hmacJefe)
            XCTAssertEqual(try mx.call(HwEngine.gcd, le64(12) + le64(18)), le64(6))
            let engines = try mx.providers()
            XCTAssertGreaterThanOrEqual(engines.count, 5)
            for e in engines {
                XCTAssertEqual(e.kind, .soft)
                XCTAssertEqual(e.proof, .true)
                XCTAssertEqual(e.contract, 1)
                XCTAssertFalse(e.flags.contains(.fault))
            }
            let fns = Set(engines.map(\.fnId))
            XCTAssertTrue(fns.isSuperset(of: [HwEngine.sha256, HwEngine.keccak, HwEngine.mac, HwEngine.gcd, HwEngine.logic64]))
            XCTAssertEqual(status { _ = try mx.providerInfo(UInt32(engines.count)) }, ZXV_E_NOT_FOUND)
            let info = try mx.functionInfo(HwEngine.mac)
            XCTAssertEqual(info.contract, 1)
            XCTAssertTrue(info.policy.contains(.keyBearing))
            let sha = try XCTUnwrap(try mx.bound(HwEngine.sha256))
            XCTAssertTrue(try mx.providerInfo(sha).flags.contains(.serving))
        }
    }

    func testMacRefusesRemoteAndPlacementPolicy() throws {
        try withHw { _, cap, _ in
            let mx = try HwMatrix(cap)
            let local = try XCTUnwrap(try mx.bound(HwEngine.mac))
            nonisolated(unsafe) var reached = 0
            try mx.addResource(7)
            let remote = try mx.addProvider(HwEngine.mac, resource: 7, kind: .remote, cost: 1) { _, _ in
                reached += 1
                throw ZxvError(status: ZXV_E_STATE, detail: "remote")
            }
            _ = try mx.attest(remote)
            let before = reached
            XCTAssertEqual(hex(try mx.call(HwEngine.mac, macInput)), hmacJefe)
            XCTAssertEqual(reached, before, "key-bearing work never reaches a REMOTE implementation")
            XCTAssertEqual(try mx.bound(HwEngine.mac), local)
            XCTAssertTrue(try mx.providerInfo(remote).flags.contains(.policy))
            XCTAssertEqual(status { try mx.setPolicy(HwEngine.mac) }, ZXV_E_POLICY)
            XCTAssertEqual(status { try mx.memo(HwEngine.mac, entries: 16) }, ZXV_E_POLICY)
            XCTAssertEqual(status { try mx.setPolicy(HwEngine.mac, flags: HwPolicy(rawValue: 0x80)) }, ZXV_E_ARG)

            let g = le64(12) + le64(18)
            try mx.setPolicy(HwEngine.gcd, powerBudgetMw: 500)
            XCTAssertEqual(status { _ = try mx.call(HwEngine.gcd, g) }, ZXV_E_NOPROVIDER)
            XCTAssertNil(try mx.bound(HwEngine.gcd))
            let gcd = try XCTUnwrap(try mx.providers().first { $0.fnId == HwEngine.gcd }).index
            try mx.setTraits(gcd, latencyNs: 2000, powerMw: 300, throughputClass: 4)
            XCTAssertEqual(status { try mx.setTraits(gcd, latencyNs: 2000, powerMw: 300, throughputClass: 16) }, ZXV_E_RANGE)
            XCTAssertEqual(try mx.call(HwEngine.gcd, g), le64(6))
            let i = try mx.providerInfo(gcd)
            XCTAssertEqual(i.throughputClass, 4)
            XCTAssertEqual(i.powerClass, .upTo1W)
            try mx.setPolicy(HwEngine.gcd, flags: .localOnly, maxLatencyNs: 1000)
            XCTAssertEqual(status { _ = try mx.call(HwEngine.gcd, g) }, ZXV_E_NOPROVIDER)
            try mx.setPolicy(HwEngine.gcd)
            XCTAssertEqual(try mx.call(HwEngine.gcd, g), le64(6))
        }
    }

    func testSwiftImplementationsProofFailoverShadowMemoContracts() throws {
        try withHw { _, cap, _ in
            let mx = try HwMatrix(cap)
            try mx.addFunction(0x100, name: "add8")
            try mx.addFunction(0x101, name: "add16")
            XCTAssertEqual(status { try mx.addFunction(0x100, name: "again") }, ZXV_E_DUPLICATE)
            try mx.addResource(1)
            try mx.addResource(2)
            let input: [UInt8] = [0, 1, 9, 200], ex8: [UInt8] = [8, 9, 17, 208], ex16: [UInt8] = [16, 17, 25, 216]
            try mx.addVector(0x100, input: input, expected: ex8)
            try mx.addVector(0x101, input: input, expected: ex16)
            nonisolated(unsafe) var fx = 0
            nonisolated(unsafe) var sw = 0
            func add8(_ bad: Int, _ d: [UInt8]) throws -> [UInt8] {
                if bad == 2 { throw ZxvError(status: ZXV_E_IO, detail: "unit offline") }
                return d.map { $0 &+ 8 &+ (bad == 1 && $0 == 9 ? 1 : 0) }
            }
            let pfx = try mx.addProvider(0x100, resource: 2, kind: .hard, cost: 1) { _, d in try add8(fx, d) }
            let psw = try mx.addProvider(0x100, resource: 1, kind: .soft, cost: 10) { _, d in try add8(sw, d) }
            let p16 = try mx.addProvider(0x101, resource: 1, kind: .soft, cost: 20) { [unowned mx] op, d in
                try mx.call(0x100, try mx.call(0x100, d, op: op), op: op)
            }
            XCTAssertEqual(status { _ = try mx.addProvider(0x100, resource: 1, rawKind: 9, cost: 1) { _, _ in [] } }, ZXV_E_ARG)
            XCTAssertEqual(status { _ = try mx.call(0x100, input) }, ZXV_E_NOPROVIDER)
            XCTAssertEqual(try mx.attest(pfx), .true)
            XCTAssertEqual(try mx.attest(psw), .true)
            XCTAssertEqual(try mx.attest(p16), .false, "an undeclared dependency must not prove")
            try mx.addDependency(p16, on: 0x100)
            XCTAssertEqual(try mx.attest(p16), .true)
            XCTAssertEqual(try mx.call(0x101, input), ex16)

            XCTAssertEqual(try mx.bound(0x100), pfx)
            try mx.setResource(2, state: .faulted)
            XCTAssertEqual(try mx.call(0x100, input), ex8)
            XCTAssertEqual(try mx.bound(0x100), psw)
            try mx.setResource(2, state: .ok)

            fx = 2
            XCTAssertEqual(status { _ = try mx.call(0x100, input) }, ZXV_E_IO)
            fx = 0

            let s = try mx.shadow(0x100, input)
            XCTAssertEqual(s.output, ex8)
            XCTAssertTrue(s.checked)
            sw = 1
            XCTAssertEqual(status { _ = try mx.shadow(0x100, input) }, ZXV_E_DISPUTE)
            XCTAssertEqual(status { _ = try mx.call(0x100, input) }, ZXV_E_NOPROVIDER)
            XCTAssertEqual(try mx.attest(pfx), .true)
            XCTAssertEqual(try mx.attest(psw), .false)

            XCTAssertEqual(status { try mx.memo(0x100, entries: 6) }, ZXV_E_ARG)
            try mx.memo(0x100, entries: 16)
            let calls0 = try mx.providerInfo(pfx).calls
            XCTAssertEqual(try mx.call(0x100, input), ex8)
            XCTAssertEqual(try mx.call(0x100, input), ex8)
            XCTAssertEqual(try mx.providerInfo(pfx).calls, calls0 + 1)
            let memo = try mx.memo(0x100, entries: 16)
            XCTAssertEqual([memo.hits, memo.misses], [1, 1])

            XCTAssertEqual(try mx.functionInfo(0x100), HwFunctionInfo(contract: 0, policy: [], vectors: 1))
            try mx.startContract(0x100, version: 2)
            XCTAssertEqual(status { try mx.startContract(0x100, version: 2) }, ZXV_E_ROLLBACK)
            try mx.addVector(0x100, input: input, expected: ex8)
            _ = try mx.attest(pfx)
            XCTAssertEqual(try mx.providerInfo(pfx).contract, 2)
            XCTAssertTrue(try mx.providerInfo(psw).flags.contains(.unproven))
        }
    }

    /// Three image builds in total (SLH-DSA signing takes seconds each).
    func testSignedImagesRegionsAndContractPackages() throws {
        try withHw { ctx, cap, nohw in
            let key = try Hw.keygen(cap)
            let other = try Hw.keygen(nohw) // .cryptoSign is an ABI 1.1 bit
            defer {
                key.wipe()
                other.wipe()
            }
            XCTAssertEqual(key.publicKey.count, Hw.pkBytes)
            XCTAssertEqual(key.mldsaSecret.count, Hw.mldsaSecretBytes)
            XCTAssertNotEqual(key.publicKey, other.publicKey)

            let oracle = try HwMatrix(cap)
            let mx = try HwMatrix(cap)
            try mx.addFunction(0x200, name: "inc")
            try mx.addResource(1)
            try mx.addResource(2)
            let vin = (0..<256).map { UInt8($0) }
            let inc = vin.map { $0 &+ 1 }
            try mx.addVector(0x200, input: vin, expected: inc)
            let sim = SimDevice(oracle: oracle)
            let psoft = try mx.addProvider(0x200, resource: 1, kind: .soft, cost: 50) { _, d in d.map { $0 &+ 1 } }
            let preg = try mx.addProvider(0x200, resource: 2, kind: .fabric, cost: 1) { _, d in try sim.run(d) }
            XCTAssertEqual(try mx.attest(psoft), .true)

            XCTAssertEqual(status { _ = try HwRegion(nohw, matrix: mx, provider: preg, regionId: 1, targetClass: 0xD0, device: sim, logCapacity: 12) }, ZXV_E_CAP)
            XCTAssertEqual(status { _ = try HwRegion(cap, matrix: mx, provider: psoft, regionId: 1, targetClass: 0xD0, device: sim, logCapacity: 12) }, ZXV_E_ARG)
            let rg = try HwRegion(cap, matrix: mx, provider: preg, regionId: 1, targetClass: 0xD0, device: sim, logCapacity: 12)
            try rg.trustKey(key.publicKey)
            let events = Locked<[(HwEvent, UInt32)]>([])
            try rg.onEvent { ev, _, version in events.withLock { $0.append((ev, version)) } }

            let forged = try Hw.buildImage(cap, kind: .partial, targetClass: 0xD0, fnId: 0x200, version: 1, svn: 1, payload: inc, key: other)
            let good = try Hw.buildImage(cap, kind: .partial, targetClass: 0xD0, fnId: 0x200, version: 1, svn: 1, payload: inc, key: key)
            XCTAssertEqual(good.count, Hw.headerBytes + 256 + Hw.signatureBytes)
            XCTAssertEqual(status { try rg.stage(forged) }, ZXV_E_VERIFY)
            var wrongHw = good
            wrongHw.replaceSubrange(12..<16, with: [0xD1, 0, 0, 0])
            XCTAssertEqual(status { try rg.stage(wrongHw) }, ZXV_E_TARGET)

            try rg.stage(good)
            XCTAssertEqual(status { try rg.commit() }, ZXV_E_STATE)
            try rg.trial(ticks: 5)
            XCTAssertEqual(try mx.bound(0x200), preg)
            try rg.commit()
            XCTAssertEqual(try rg.info(), HwRegionInfo(state: .active, version: 1, floor: 1, quarantined: 1))
            let seen = events.withLock { $0 }
            XCTAssertEqual(seen.map(\.0), [.rejected, .staged, .tested, .activated])
            XCTAssertEqual(seen.map(\.1), [1, 1, 1, 1])
            XCTAssertEqual(try mx.call(0x200, Array(vin.prefix(4))), Array(inc.prefix(4)))

            XCTAssertEqual(status { try rg.stage(good) }, ZXV_E_ROLLBACK)
            let log = try rg.log()
            XCTAssertEqual(log.count, 5)
            let last = try rg.logEntry(at: log.count - 1)
            XCTAssertEqual(last.event, .rejected)
            XCTAssertEqual(last.id, 1)
            XCTAssertEqual(last.hash, log.head)
            let act = try rg.logEntry(at: 3)
            XCTAssertEqual(act.event, .activated)
            XCTAssertEqual(act.version, 1)
            XCTAssertNotEqual((act.tag >> 2) & 3, 2)
            XCTAssertEqual(try rg.logEntries().count, Int(log.count))
            XCTAssertEqual(status { _ = try rg.logEntry(at: log.count) }, ZXV_E_NOT_FOUND)

            let payload = Hw.vectorList([(op: 0, input: [5, 250], expected: [6, 251])])
            XCTAssertEqual(payload.count, 16)
            let pkg = try Hw.buildImage(cap, kind: .vectors, targetClass: 0, fnId: 0x200, version: 7, svn: 3, payload: payload, key: key)
            let matrixOnly = try HwMatrix(try cap.attenuate(CapabilitySet.all.union(.hwMatrix)))
            XCTAssertEqual(status { try matrixOnly.applyContract(pkg) }, ZXV_E_CAP)
            XCTAssertEqual(status { try mx.applyContract(pkg) }, ZXV_E_VERIFY) // no key trusted yet
            try mx.trustKey(key.publicKey)
            XCTAssertEqual(status { try mx.applyContract(good) }, ZXV_E_FORMAT)
            XCTAssertEqual(status { try rg.stage(pkg) }, ZXV_E_FORMAT)
            XCTAssertEqual(try mx.applyContract(pkg), 2)
            let info = try mx.functionInfo(0x200)
            XCTAssertEqual([info.contract, info.vectors], [7, 1])
            XCTAssertEqual(status { try mx.applyContract(pkg) }, ZXV_E_ROLLBACK)
            let contract = try XCTUnwrap(try mx.logEntries().first { $0.event == .contract })
            XCTAssertEqual([contract.id, contract.version], [0x200, 7])

            // Fill the region's log (12): the next recorded refusal halts the
            // region and poisons the context. Fail closed.
            while try rg.log().count < 12 {
                XCTAssertEqual(status { try rg.stage(forged) }, ZXV_E_VERIFY)
            }
            XCTAssertEqual(status { try rg.stage(forged) }, ZXV_E_POISONED)
            XCTAssertTrue(ctx.isPoisoned)
        }
    }
}
