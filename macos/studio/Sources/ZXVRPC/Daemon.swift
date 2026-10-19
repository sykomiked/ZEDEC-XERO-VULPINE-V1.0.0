// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// Finding or starting zxvd. The app first tries the socket where the Mac
// shell (macos/ZXVApp.m, SOCKET) puts it; if nothing answers and the bundle
// carries zxvd, it starts it the same way: --rpc <socket> --exit-with-parent,
// with a stdin pipe the app holds, so zxvd stops when the app does.
import Foundation

public enum ZXVDLocation {
    /// ~/Library/Application Support/ZXV/run/zxvd.sock on macOS (the same
    /// path the AppKit shell uses), ~/.zxv/run/zxvd.sock elsewhere.
    /// ZXVD_SOCKET overrides both.
    public static var defaultSocketPath: String {
        if let p = ProcessInfo.processInfo.environment["ZXVD_SOCKET"], !p.isEmpty { return p }
        let home = FileManager.default.homeDirectoryForCurrentUser.path
        #if os(macOS)
        return home + "/Library/Application Support/ZXV/run/zxvd.sock"
        #else
        return home + "/.zxv/run/zxvd.sock"
        #endif
    }

    /// ~/Library/Application Support/ZXV/models (where the shell looks for *.gguf).
    public static var defaultModelsDir: String {
        let home = FileManager.default.homeDirectoryForCurrentUser.path
        #if os(macOS)
        return home + "/Library/Application Support/ZXV/models"
        #else
        return home + "/.zxv/models"
        #endif
    }

    /// The owner's books file, the same default zxvd's --desktop mode uses
    /// (zxvd_rpc.h F1). Without --books zxvd keeps the books in memory only.
    public static var defaultBooksPath: String {
        let home = FileManager.default.homeDirectoryForCurrentUser.path
        #if os(macOS)
        return home + "/Library/Application Support/ZXV/books.zxvb"
        #else
        let xdg = ProcessInfo.processInfo.environment["XDG_DATA_HOME"].flatMap { $0.isEmpty ? nil : $0 }
        return (xdg ?? home + "/.local/share") + "/zxv/books.zxvb"
        #endif
    }
}

/// A zxvd this process started. It stops when `stop()` is called or when this
/// process exits (zxvd sees EOF on its stdin).
public final class DaemonProcess: @unchecked Sendable {
    public let process: Process
    public let socketPath: String
    private let stdin: Pipe

    public enum StartError: Error, Equatable {
        case notExecutable(String)
        case noAnnouncement(String)
    }

    private init(process: Process, socketPath: String, stdin: Pipe) {
        self.process = process
        self.socketPath = socketPath
        self.stdin = stdin
    }

    /// Start `binary` serving `socketPath`; returns once zxvd prints its
    /// "ZXVD-RPC: <path>" line, or throws after `timeout` seconds.
    public static func start(binary: String, socketPath: String, modelsDir: String? = nil,
                             extraArguments: [String] = [], environment: [String: String]? = nil,
                             logURL: URL? = nil, timeout: TimeInterval = 30) throws -> DaemonProcess {
        guard FileManager.default.isExecutableFile(atPath: binary) else {
            throw StartError.notExecutable(binary)
        }
        let p = Process()
        p.executableURL = URL(fileURLWithPath: binary)
        var args = ["--rpc", socketPath, "--exit-with-parent"]
        if let m = modelsDir { args += ["--models-dir", m] }
        p.arguments = args + extraArguments
        if let e = environment { p.environment = e }
        let input = Pipe()
        let output = Pipe()
        p.standardInput = input
        p.standardOutput = output
        p.standardError = logURL.flatMap { try? FileHandle(forWritingTo: $0) } ?? FileHandle.nullDevice

        let announced = DispatchSemaphore(value: 0)
        let box = AnnounceBox()
        output.fileHandleForReading.readabilityHandler = { h in
            let d = h.availableData
            if d.isEmpty {
                h.readabilityHandler = nil
                announced.signal()
                return
            }
            if box.feed(d, want: socketPath) { announced.signal() }
        }
        try p.run()
        let waited = announced.wait(timeout: .now() + timeout)
        if waited == .timedOut || !box.seen {
            p.terminate()
            throw StartError.noAnnouncement(box.text)
        }
        return DaemonProcess(process: p, socketPath: socketPath, stdin: input)
    }

    /// Ask zxvd to stop over its socket, falling back to closing its stdin.
    public func stop() {
        _ = try? ZXVDClient(socketPath: socketPath, timeoutMs: 2000).call("zxvd.shutdown")
        try? stdin.fileHandleForWriting.close()
        process.waitUntilExit()
    }
}

/// Collects zxvd's stdout until the ZXVD-RPC line names our socket.
private final class AnnounceBox: @unchecked Sendable {
    private let lock = NSLock()
    private(set) var text = ""
    private(set) var seen = false

    func feed(_ d: Data, want path: String) -> Bool {
        lock.lock()
        defer { lock.unlock() }
        if seen { return false }
        text += String(decoding: d, as: UTF8.self)
        if text.count > 65536 { text = String(text.suffix(4096)) }
        for line in text.split(separator: "\n") where line.hasPrefix("ZXVD-RPC: ") {
            if line.dropFirst(10).trimmingCharacters(in: .whitespaces) == path {
                seen = true
                return true
            }
        }
        return false
    }
}
