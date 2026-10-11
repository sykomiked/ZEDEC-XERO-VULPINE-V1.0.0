// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// The client side of zxvd's control socket: JSON-RPC 2.0, one request line
// out, one reply line back, over the owner-only UNIX socket (docs/ZXVD.md).
// One connection per call, like macos/zxv_rpc_client.c: zxvd serves up to 8
// clients and drops idle ones, so nothing is held open between calls.
// Calls block; use the async wrappers from the main actor.
import Foundation

#if canImport(Darwin)
import Darwin
#elseif canImport(Glibc)
import Glibc
#endif

/// error.data on a -32000 reply: the libzxv status behind it.
public struct RPCErrorData: Decodable, Equatable, Sendable {
    public let status: Int
    public let name: String
    public let kind: String

    enum CodingKeys: String, CodingKey {
        case status, name
        case kind = "class"
    }
}

/// A JSON-RPC error object from zxvd.
public struct RPCError: Error, Decodable, Equatable, Sendable {
    public let code: Int
    public let message: String
    public let data: RPCErrorData?

    /// The libzxv status name (ZXV_E_FUNDS ...) when there is one.
    public var statusName: String? { data?.name }
}

public enum ZXVDError: Error, Equatable, CustomStringConvertible {
    /// Nothing is listening at the socket path.
    case noDaemon(String)
    /// The socket failed mid-call.
    case io(String)
    case timedOut
    /// The reply was not a JSON-RPC response to this request.
    case badReply(String)
    /// zxvd answered with an error.
    case rpc(RPCError)

    public var description: String {
        switch self {
        case .noDaemon(let p): return "zxvd is not running at \(p)"
        case .io(let m): return "socket error: \(m)"
        case .timedOut: return "zxvd did not answer in time"
        case .badReply(let m): return "unexpected reply from zxvd: \(m)"
        case .rpc(let e):
            if let n = e.statusName { return "\(e.message) (\(n))" }
            return e.message
        }
    }
}

private struct Request: Encodable {
    let jsonrpc = "2.0"
    let id: Int64
    let method: String
    let params: [String: JSON]?
}

private struct Envelope<T: Decodable>: Decodable {
    let id: Int64?
    let result: T?
    let error: RPCError?
}

public final class ZXVDClient: @unchecked Sendable {
    public let socketPath: String
    public let timeoutMs: Int
    /// A reply larger than this is refused (swarm.state is a few KiB).
    public static let maxReply = 8 << 20

    private let lock = NSLock()
    private var nextID: Int64 = 1

    public init(socketPath: String, timeoutMs: Int = 10_000) {
        self.socketPath = socketPath
        self.timeoutMs = timeoutMs
    }

    /// Typed results use snake_case -> camelCase; untyped JSON keeps keys as sent.
    static let typedDecoder: JSONDecoder = {
        let d = JSONDecoder()
        d.keyDecodingStrategy = .convertFromSnakeCase
        return d
    }()

    /// Call `method` and decode its result as `T`.
    public func call<T: Decodable>(_ method: String, _ params: [String: JSON]? = nil, as type: T.Type,
                                   timeoutMs: Int? = nil) throws -> T {
        let (id, line) = try exchange(method, params, timeoutMs ?? self.timeoutMs)
        return try Self.decodeReply(line, id: id, as: type, decoder: Self.typedDecoder)
    }

    /// Call `method` and return its result untouched.
    @discardableResult
    public func call(_ method: String, _ params: [String: JSON]? = nil, timeoutMs: Int? = nil) throws -> JSON {
        let (id, line) = try exchange(method, params, timeoutMs ?? self.timeoutMs)
        return try Self.decodeReply(line, id: id, as: JSON.self, decoder: JSONDecoder())
    }

    public func request<T: Decodable>(_ method: String, _ params: [String: JSON]? = nil, as type: T.Type,
                                      timeoutMs: Int? = nil) async throws -> T {
        try await withCheckedThrowingContinuation { cont in
            DispatchQueue.global(qos: .userInitiated).async {
                cont.resume(with: Result { try self.call(method, params, as: type, timeoutMs: timeoutMs) })
            }
        }
    }

    @discardableResult
    public func request(_ method: String, _ params: [String: JSON]? = nil, timeoutMs: Int? = nil) async throws -> JSON {
        try await withCheckedThrowingContinuation { cont in
            DispatchQueue.global(qos: .userInitiated).async {
                cont.resume(with: Result { try self.call(method, params, timeoutMs: timeoutMs) })
            }
        }
    }

    static func decodeReply<T: Decodable>(_ line: Data, id: Int64, as type: T.Type, decoder: JSONDecoder) throws -> T {
        let env: Envelope<T>
        do {
            env = try decoder.decode(Envelope<T>.self, from: line)
        } catch {
            throw ZXVDError.badReply(String(describing: error))
        }
        if let e = env.error { throw ZXVDError.rpc(e) }
        guard env.id == id else { throw ZXVDError.badReply("reply id does not match the request") }
        guard let r = env.result else { throw ZXVDError.badReply("no result") }
        return r
    }

    private func exchange(_ method: String, _ params: [String: JSON]?, _ timeoutMs: Int) throws -> (Int64, Data) {
        lock.lock()
        let id = nextID
        nextID += 1
        lock.unlock()
        var body = try JSONEncoder().encode(Request(id: id, method: method, params: params))
        body.append(0x0A)
        return (id, try Self.roundTrip(socketPath, body, timeoutMs))
    }

    /// Connect, send one line, read one line back (without its newline).
    static func roundTrip(_ path: String, _ line: Data, _ timeoutMs: Int) throws -> Data {
        let fd = try connectUnix(path, timeoutMs)
        defer { close(fd) }
        try line.withUnsafeBytes { raw in
            var off = 0
            while off < raw.count {
                let n = sendNoSignal(fd, raw.baseAddress! + off, raw.count - off)
                if n < 0 {
                    if errno == EINTR { continue }
                    if errno == EAGAIN || errno == EWOULDBLOCK { throw ZXVDError.timedOut }
                    throw ZXVDError.io(String(cString: strerror(errno)))
                }
                off += n
            }
        }
        var reply = Data()
        var chunk = [UInt8](repeating: 0, count: 65536)
        while true {
            let n = chunk.withUnsafeMutableBytes { recv(fd, $0.baseAddress, $0.count, 0) }
            if n < 0 {
                if errno == EINTR { continue }
                if errno == EAGAIN || errno == EWOULDBLOCK { throw ZXVDError.timedOut }
                throw ZXVDError.io(String(cString: strerror(errno)))
            }
            if n == 0 { throw ZXVDError.io("zxvd closed the connection") }
            if let nl = chunk[0..<n].firstIndex(of: 0x0A) {
                reply.append(contentsOf: chunk[0..<nl])
                return reply
            }
            reply.append(contentsOf: chunk[0..<n])
            if reply.count > maxReply { throw ZXVDError.badReply("reply too large") }
        }
    }

    private static func connectUnix(_ path: String, _ timeoutMs: Int) throws -> Int32 {
        #if canImport(Darwin)
        let fd = socket(AF_UNIX, SOCK_STREAM, 0)
        #else
        let fd = socket(AF_UNIX, Int32(SOCK_STREAM.rawValue), 0)
        #endif
        if fd < 0 { throw ZXVDError.io(String(cString: strerror(errno))) }
        var tv = timeval(tv_sec: timeoutMs / 1000, tv_usec: .init((timeoutMs % 1000) * 1000))
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, socklen_t(MemoryLayout<timeval>.size))
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, socklen_t(MemoryLayout<timeval>.size))
        #if canImport(Darwin)
        var one: Int32 = 1
        setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, socklen_t(MemoryLayout<Int32>.size))
        #endif

        var addr = sockaddr_un()
        addr.sun_family = sa_family_t(AF_UNIX)
        let bytes = Array(path.utf8)
        let cap = MemoryLayout.size(ofValue: addr.sun_path)
        guard bytes.count < cap else {
            close(fd)
            throw ZXVDError.io("socket path is too long")
        }
        withUnsafeMutableBytes(of: &addr.sun_path) { dst in
            dst.copyBytes(from: bytes)
            dst[bytes.count] = 0
        }
        #if canImport(Darwin)
        addr.sun_len = UInt8(MemoryLayout<sockaddr_un>.size)
        #endif
        let rc = withUnsafePointer(to: &addr) {
            $0.withMemoryRebound(to: sockaddr.self, capacity: 1) {
                connect(fd, $0, socklen_t(MemoryLayout<sockaddr_un>.size))
            }
        }
        if rc != 0 {
            let e = errno
            close(fd)
            if e == ENOENT || e == ECONNREFUSED { throw ZXVDError.noDaemon(path) }
            throw ZXVDError.io(String(cString: strerror(e)))
        }
        return fd
    }

    private static func sendNoSignal(_ fd: Int32, _ p: UnsafeRawPointer, _ n: Int) -> Int {
        #if canImport(Darwin)
        return send(fd, p, n, 0)
        #else
        return send(fd, p, n, Int32(MSG_NOSIGNAL))
        #endif
    }
}
