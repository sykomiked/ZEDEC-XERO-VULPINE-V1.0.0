// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
import Foundation
import Network

/// Devmesh frames over UDP port 47400, same wire format as the Android
/// LanTransport: 1200-byte fragments "ZF" | msg id u32 | index u16 | count u16 | data.
/// Frames are already encrypted by the core. Frames for unknown devices are broadcast
/// (iOS needs the multicast-networking entitlement for that); peers' addresses are
/// learned from the sender id in the devmesh header (bytes 20..35).
public final class LanTransport {
    private let port: NWEndpoint.Port = 47400
    private var listener: NWListener?
    private var peers: [Data: NWEndpoint] = [:]
    private var parts: [String: [Data?]] = [:]
    private var nextId = UInt32.random(in: 0...UInt32.max)
    private let queue = DispatchQueue(label: "zxv-lan")
    private let deliver: (Data) -> Void

    public init(deliver: @escaping (Data) -> Void) { self.deliver = deliver }

    public func start() throws {
        let params = NWParameters.udp
        params.allowLocalEndpointReuse = true
        let l = try NWListener(using: params, on: port)
        l.newConnectionHandler = { [weak self] c in
            c.start(queue: self?.queue ?? .main)
            self?.receive(on: c)
        }
        l.start(queue: queue)
        listener = l
    }

    public func stop() { listener?.cancel() }

    private func receive(on c: NWConnection) {
        c.receiveMessage { [weak self] data, _, _, err in
            if let d = data { self?.onPacket(d, from: c.endpoint) }
            if err == nil { self?.receive(on: c) }
        }
    }

    /// Called by the core on the main thread.
    public func send(to: Data, frame: Data) -> Int32 {
        let dst: NWEndpoint
        if to.allSatisfy({ $0 == 0 }) { dst = .hostPort(host: "255.255.255.255", port: port) }
        else { dst = queue.sync { peers[to] } ?? .hostPort(host: "255.255.255.255", port: port) }
        nextId &+= 1
        let id = nextId, chunk = 1200
        let count = (frame.count + chunk - 1) / chunk
        let c = NWConnection(to: dst, using: .udp)
        c.start(queue: queue)
        for i in 0..<count {
            var b = Data([0x5A, 0x46])
            withUnsafeBytes(of: id.bigEndian) { b.append(contentsOf: $0) }
            withUnsafeBytes(of: UInt16(i).bigEndian) { b.append(contentsOf: $0) }
            withUnsafeBytes(of: UInt16(count).bigEndian) { b.append(contentsOf: $0) }
            b.append(frame.subdata(in: i * chunk..<min(frame.count, (i + 1) * chunk)))
            c.send(content: b, completion: .contentProcessed { _ in })
        }
        c.send(content: nil, contentContext: .finalMessage, isComplete: true,
               completion: .contentProcessed { _ in c.cancel() })
        return 0
    }

    private func onPacket(_ p: Data, from: NWEndpoint) {
        let b = [UInt8](p)
        guard b.count >= 10, b[0] == 0x5A, b[1] == 0x46 else { return }
        let id = b[2..<6].reduce(UInt32(0)) { $0 << 8 | UInt32($1) }
        let idx = Int(b[6]) << 8 | Int(b[7]), count = Int(b[8]) << 8 | Int(b[9])
        guard count > 0, count <= 96, idx < count else { return }
        let key = "\(from)/\(id)"
        var arr = parts[key] ?? [Data?](repeating: nil, count: count)
        guard arr.count == count else { return }
        arr[idx] = Data(b[10...])
        if arr.contains(where: { $0 == nil }) {
            if parts.count > 64 { parts.removeAll() } // bounded: drop stale partial frames
            parts[key] = arr
            return
        }
        parts[key] = nil
        let frame = arr.reduce(Data()) { $0 + $1! }
        if frame.count >= 36 { peers[frame.subdata(in: 20..<36)] = from }
        DispatchQueue.main.async { self.deliver(frame) }
    }
}

/// The 64-byte identity seed, kept in the Keychain (this device only, after first unlock).
public enum SeedStore {
    private static let account = "zxv-devmesh-seed"

    public static func load() -> Data {
        let q: [String: Any] = [kSecClass as String: kSecClassGenericPassword,
                                kSecAttrAccount as String: account,
                                kSecReturnData as String: true]
        var out: CFTypeRef?
        if SecItemCopyMatching(q as CFDictionary, &out) == errSecSuccess, let d = out as? Data, d.count == 64 {
            return d
        }
        var seed = Data(count: 64)
        let ok = seed.withUnsafeMutableBytes { SecRandomCopyBytes(kSecRandomDefault, 64, $0.baseAddress!) }
        precondition(ok == errSecSuccess)
        let add: [String: Any] = [kSecClass as String: kSecClassGenericPassword,
                                  kSecAttrAccount as String: account,
                                  kSecAttrAccessible as String: kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly,
                                  kSecValueData as String: seed]
        SecItemAdd(add as CFDictionary, nil)
        return seed
    }
}
