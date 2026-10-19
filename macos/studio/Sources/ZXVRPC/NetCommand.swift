// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// The text commands net.command takes (zxv_host.c): off, lan, online,
// peer IP:PORT, share C S (0 0 stops sharing), want RES UNITS MAX,
// sell RES UNITS PRICE NODEID (the buyer's 64-hex NodeID; sold on credit).
// Built and checked here so the app never sends a malformed line.
import Foundation

public enum NetMode: String, CaseIterable, Sendable {
    case off, lan, online
}

public enum NetResource: String, CaseIterable, Sendable {
    case compute, storage
}

public enum NetCommand: Equatable, Sendable {
    case mode(NetMode)
    case peer(String)
    case share(compute: UInt64, storage: UInt64)
    case want(NetResource, units: UInt64, maxPrice: UInt64)
    case sell(NetResource, units: UInt64, price: UInt64, nodeID: String)

    public enum Invalid: Error, Equatable {
        case peerAddress
        case nodeID
        case zero
    }

    /// The default Vinea port (ZXV_NET_DEFAULT_PORT).
    public static let defaultPort = 8723

    /// The line sent as net.command's `command`.
    public func text() throws -> String {
        switch self {
        case .mode(let m):
            return m.rawValue
        case .peer(let a):
            guard Self.isIPv4Port(a) else { throw Invalid.peerAddress }
            return "peer " + a
        case .share(let c, let s):
            return "share \(c) \(s)"
        case .want(let r, let u, let m):
            guard u > 0, m > 0 else { throw Invalid.zero }
            return "want \(r.rawValue) \(u) \(m)"
        case .sell(let r, let u, let p, let n):
            guard u > 0, p > 0 else { throw Invalid.zero }
            guard Self.isNodeID(n) else { throw Invalid.nodeID }
            return "sell \(r.rawValue) \(u) \(p) \(n.lowercased())"
        }
    }

    /// "192.168.1.20:8723": four 0-255 octets and a 1-65535 port.
    public static func isIPv4Port(_ s: String) -> Bool {
        let hp = s.split(separator: ":", omittingEmptySubsequences: false)
        guard hp.count == 2, let port = Int(hp[1]), (1...65535).contains(port),
              hp[1].allSatisfy({ $0.isASCII && $0.isNumber })
        else { return false }
        let oct = hp[0].split(separator: ".", omittingEmptySubsequences: false)
        return oct.count == 4 && oct.allSatisfy { o in
            !o.isEmpty && o.count <= 3 && o.allSatisfy({ $0.isASCII && $0.isNumber }) && Int(o)! <= 255
        }
    }

    /// A buyer's full NodeID: 64 hex digits (zxvd reads them with hex32).
    public static func isNodeID(_ s: String) -> Bool {
        s.count == 64 && s.allSatisfy { $0.isHexDigit && $0.isASCII }
    }
}
