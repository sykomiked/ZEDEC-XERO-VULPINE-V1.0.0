// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
import Foundation
import Security
import ZXVCore

/// Callbacks from the core. Called on the thread that called into Core (the main thread).
public protocol CoreDelegate: AnyObject {
    func coreSend(to: Data, frame: Data) -> Int32
    func coreEvent(kind: Int32, peer: Data, sas: UInt32, value: UInt64)
    func coreReply(reqId: UInt32, text: String, final: Bool)
    func coreMoney(amountMinor: UInt64, rail: UInt32, memo: String)
}

public struct Device: Identifiable, Hashable {
    public let id: Data
    public let name: String
    public let role: Int32
    public let flags: Int32
    public let revoked: Bool
}

/// Thin wrapper over the flat C API. One identity per process. Not thread-safe.
public final class Core {
    public static let shared = Core()
    public weak var delegate: CoreDelegate?
    private init() {}

    public static func nowMs() -> UInt64 { UInt64(Date().timeIntervalSince1970 * 1000) }

    @discardableResult
    public func start(seed: Data, name: String, role: Int32, flags: Int32, level: Int32 = 2) -> Int32 {
        var host = zxv_host_t()
        host.ctx = Unmanaged.passUnretained(self).toOpaque()
        host.random = { _, out, n in
            guard let out = out else { return }
            // Without randomness keys would be predictable: stop rather than continue.
            precondition(SecRandomCopyBytes(kSecRandomDefault, Int(n), out) == errSecSuccess)
        }
        host.send = { ctx, to, frame, len in
            let me = Unmanaged<Core>.fromOpaque(ctx!).takeUnretainedValue()
            let t = Data(bytes: to!, count: Int(ZXV_ID_BYTES))
            let f = Data(bytes: frame!, count: Int(len))
            return me.delegate?.coreSend(to: t, frame: f) ?? -1
        }
        host.event = { ctx, kind, peer, sas, value in
            let me = Unmanaged<Core>.fromOpaque(ctx!).takeUnretainedValue()
            let p = peer.map { Data(bytes: $0, count: Int(ZXV_ID_BYTES)) } ?? Data(count: Int(ZXV_ID_BYTES))
            me.delegate?.coreEvent(kind: kind, peer: p, sas: sas, value: value)
        }
        host.reply = { ctx, reqId, data, len, final in
            let me = Unmanaged<Core>.fromOpaque(ctx!).takeUnretainedValue()
            let text = data.map { String(decoding: UnsafeBufferPointer(start: $0, count: Int(len)), as: UTF8.self) } ?? ""
            me.delegate?.coreReply(reqId: reqId, text: text, final: final != 0)
        }
        host.money = { ctx, amount, rail, memo in
            let me = Unmanaged<Core>.fromOpaque(ctx!).takeUnretainedValue()
            me.delegate?.coreMoney(amountMinor: amount, rail: rail, memo: memo.map { String(cString: $0) } ?? "")
        }
        precondition(seed.count == 64)
        return seed.withUnsafeBytes { s in
            zxv_start(&host, s.bindMemory(to: UInt8.self).baseAddress, name, role, flags, level)
        }
    }

    public func selfId() -> Data {
        var id = [UInt8](repeating: 0, count: Int(ZXV_ID_BYTES))
        _ = zxv_self_id(&id)
        return Data(id)
    }

    public func createMesh() -> Int32 { zxv_create_mesh(Core.nowMs()) }

    /// (QR text, 10-character code) or nil.
    public func invite(role: Int32, flags: Int32) -> (qr: String, code: String)? {
        var qr = [CChar](repeating: 0, count: 2048)
        var code = [CChar](repeating: 0, count: 11)
        guard zxv_invite(role, flags, Core.nowMs(), &qr, UInt32(qr.count), &code) == 0 else { return nil }
        return (String(cString: qr), String(cString: code))
    }

    public func joinQr(_ text: String) -> Int32 { zxv_join_qr_text(text, Core.nowMs()) }
    public func joinCode(_ code: String) -> Int32 { zxv_join_code(code, Core.nowMs()) }

    public func confirmSas(peer: Data, match: Bool) -> Int32 {
        withId(peer) { zxv_confirm_sas($0, match ? 1 : 0, Core.nowMs()) }
    }

    public func receive(_ frame: Data) -> Int32 {
        frame.withUnsafeBytes { zxv_receive($0.bindMemory(to: UInt8.self).baseAddress, UInt32(frame.count), Core.nowMs()) }
    }

    public func tick() { zxv_tick(Core.nowMs()) }

    public func devices() -> [Device] {
        (0..<zxv_device_count()).compactMap { i in
            var id = [UInt8](repeating: 0, count: Int(ZXV_ID_BYTES))
            var name = [CChar](repeating: 0, count: 33)
            let info = zxv_device(i, &id, &name)
            guard info >= 0 else { return nil }
            return Device(id: Data(id), name: String(cString: name), role: info & 0xff,
                          flags: (info >> 8) & 0xff, revoked: ((info >> 16) & 0xff) == 2)
        }
    }

    public func rename(_ id: Data, to name: String) -> Int32 { withId(id) { zxv_rename($0, name, Core.nowMs()) } }
    public func revoke(_ id: Data) -> Int32 { withId(id) { zxv_revoke($0, Core.nowMs()) } }

    public func setCaps(ramMb: UInt32, compute: UInt32, batteryPct: Int32, charging: Bool, net: Int32) -> Int32 {
        zxv_set_caps(ramMb, compute, batteryPct, charging ? 1 : 0, net, 0, Core.nowMs())
    }

    public func localModels() -> [String] {
        var out: [String] = []
        var i: Int32 = 0
        while let p = zxv_local_model(i) { out.append(String(cString: p)); i += 1 }
        return out
    }

    public func route(kind: Int32, modelClass: Int32) -> (route: Int32, target: Data) {
        var t = [UInt8](repeating: 0, count: Int(ZXV_ID_BYTES))
        let r = zxv_route(kind, modelClass, Core.nowMs(), &t)
        return (r, Data(t))
    }

    /// Request id (>0) or a negative status.
    public func prompt(target: Data, text: String) -> Int64 {
        var req: UInt32 = 0
        let r = withId(target) { zxv_prompt($0, text, Core.nowMs(), &req) }
        return r < 0 ? Int64(r) : Int64(req)
    }

    public func setSetting(_ key: UInt16, _ value: String) -> Int32 { zxv_set_setting(key, value, Core.nowMs()) }

    public func setting(_ key: UInt16) -> String? {
        var buf = [CChar](repeating: 0, count: 256)
        return zxv_get_setting(key, &buf, UInt32(buf.count)) < 0 ? nil : String(cString: buf)
    }

    public func moneyAnswer(approve: Bool) -> Int32 { zxv_money_answer(approve ? 1 : 0, Core.nowMs()) }
    public func tithe(_ amountMinor: UInt64) -> UInt64 { zxv_tithe(amountMinor) }

    private func withId<R>(_ id: Data, _ f: (UnsafePointer<UInt8>) -> R) -> R {
        precondition(id.count == Int(ZXV_ID_BYTES))
        return id.withUnsafeBytes { f($0.bindMemory(to: UInt8.self).baseAddress!) }
    }
}

/// VFV amounts are integer minor units (1 VFV = 100). Rails: as in the core.
public enum Vfv {
    public static func format(_ minor: Int64) -> String {
        let a = minor.magnitude
        let s = "\(a / 100).\(String(format: "%02llu", a % 100))"
        return minor < 0 ? "-" + s : s
    }

    public static func railName(_ rail: UInt32) -> String {
        switch rail {
        case 555: return "DEBIT 555"
        case 777: return "CREDIT 777"
        case 888: return "EQUITY 888"
        default: return "rail \(rail)"
        }
    }
}
